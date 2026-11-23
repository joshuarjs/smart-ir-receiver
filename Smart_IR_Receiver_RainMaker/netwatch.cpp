#include "netwatch.h"
#include "wifimgr.h"
#include "config.h"
#include "settings.h"
#include "cloud.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"

// ---------------------------------------------------------------------------
// The SuperMini antennas are marginal (the C3 especially): high TX power can desense its own
// receiver, and the driver's built-in auto-reconnect can stall after a
// beacon timeout. So:
//  - TX power and sleep are applied as soon as the Wi-Fi driver starts, so
//    every association and reconnect attempt uses the low setting, not the
//    19.5 dBm default.
//  - A watchdog escalates when the link stays down:
//      30 s  -> WiFi.reconnect()
//      2 min -> full disconnect + fresh begin, repeated every 2 min
//      10 min, setup hotspot not open -> reboot
//    Learned buttons and settings are in flash, so a reboot loses nothing.
// ---------------------------------------------------------------------------

static const wifi_power_t txPowerTable[] = {
  WIFI_POWER_2dBm, WIFI_POWER_5dBm, WIFI_POWER_7dBm, WIFI_POWER_8_5dBm, WIFI_POWER_11dBm,
  WIFI_POWER_13dBm, WIFI_POWER_15dBm, WIFI_POWER_17dBm, WIFI_POWER_19_5dBm };

static const uint32_t WD_RECONNECT_MS = 30UL * 1000;
static const uint32_t WD_REBEGIN_MS   = 2UL * 60 * 1000;
static const uint32_t WD_REBOOT_MS    = 10UL * 60 * 1000;
static const uint32_t RTC_MAGIC       = 0x5B3A57D0;

// Survive a software reset (not a power cut), so a watchdog reboot can be reported.
RTC_NOINIT_ATTR uint32_t nw_rtcMagic;
RTC_NOINIT_ATTR uint32_t nw_rtcWdBoots;
RTC_NOINIT_ATTR uint32_t nw_rtcWdFlag;

static volatile bool     linkUp = false;
static volatile uint32_t drops = 0;
static volatile uint32_t lastDropMs = 0;
static volatile uint8_t  lastDropReason = 0;
static uint32_t downSince = 0;
static uint32_t lastActionMs = 0;
static uint8_t  stage = 0;
static uint32_t bootFlag = 0;          // 1 = Wi-Fi watchdog reboot
static char     reasonBuf[32] = "";
static bool     otaPending = false;
static const uint32_t OTA_CONFIRM_MS = 30UL * 1000;   // up this long and on Wi-Fi = firmware is good

bool otaConfirming() { return otaPending; }

static void otaCheckPending() {
  esp_ota_img_states_t st;
  const esp_partition_t* run = esp_ota_get_running_partition();
  otaPending = run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  if (otaPending) Serial.println("New firmware: confirming after 30 s on Wi-Fi");
}

static void otaConfirmTick() {
  if (!otaPending || millis() < OTA_CONFIRM_MS || WiFi.status() != WL_CONNECTED) return;
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) Serial.println("New firmware confirmed");
  otaPending = false;
}

void cleanRestart() {
  if (WiFi.status() == WL_CONNECTED) { WiFi.disconnect(false, false); delay(200); }
  ESP.restart();
}

void applyWifiTuning() {
  WiFi.setSleep(settings.wifiSleep != 0);
  uint8_t i = settings.txPower > 8 ? 3 : settings.txPower;
  WiFi.setTxPower(txPowerTable[i]);
}

static void wifiEvent(arduino_event_t* e) {
  switch (e->event_id) {
    case ARDUINO_EVENT_WIFI_STA_START:
      applyWifiTuning();
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      linkUp = true;
      applyWifiTuning();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (linkUp && !wifiMgrTesting()) {   // a Wi-Fi change test is not a drop
        drops = drops + 1;
        lastDropMs = millis();
        lastDropReason = e->event_info.wifi_sta_disconnected.reason;
        Serial.printf("Wi-Fi lost: reason %u\n", lastDropReason);
      }
      linkUp = false;
      break;
    default: break;
  }
}

void netwatchBegin() {
  if (nw_rtcMagic != RTC_MAGIC) { nw_rtcMagic = RTC_MAGIC; nw_rtcWdBoots = 0; nw_rtcWdFlag = 0; }
  if (esp_reset_reason() == ESP_RST_POWERON) { nw_rtcWdBoots = 0; nw_rtcWdFlag = 0; }
  bootFlag = nw_rtcWdFlag;
  nw_rtcWdFlag = 0;
  // Loop watchdog: if loop() stops running for 30 s (a hang), reboot. Nothing in
  // loop() legitimately blocks for more than a second. The core feeds it every pass.
  esp_task_wdt_config_t wc = { .timeout_ms = 30000, .idle_core_mask = 0, .trigger_panic = true };
  esp_task_wdt_reconfigure(&wc);
  enableLoopWDT();
  WiFi.onEvent(wifiEvent);
  otaCheckPending();
}

// Only act once the device has credentials and isn't in the provisioning flow.
static bool watchArmed() {
  if (strcmp(cloudStatus(), "Provisioning") == 0) return false;
  wifi_config_t c;
  if (esp_wifi_get_config(WIFI_IF_STA, &c) != ESP_OK) return false;
  return c.sta.ssid[0] != 0;
}

void netwatchTick() {
  uint32_t now = millis();
  otaConfirmTick();
  if (WiFi.status() == WL_CONNECTED) { downSince = 0; stage = 0; return; }
  if (!downSince) { downSince = now; stage = 0; return; }
  if (!watchArmed()) { downSince = now; return; }
  if (wifiMgrQuiet()) return;               // testing a new network, or a phone is on the hotspot

  uint32_t down = now - downSince;
  if (stage == 0 && down >= WD_RECONNECT_MS) {
    Serial.println("Wi-Fi watchdog: reconnect");
    stage = 1; lastActionMs = now;
    WiFi.reconnect();
  } else if (stage >= 1 && down >= WD_REBEGIN_MS && (stage == 1 || now - lastActionMs >= WD_REBEGIN_MS)) {
    Serial.println("Wi-Fi watchdog: restart Wi-Fi link");
    stage = 2; lastActionMs = now;
    WiFi.disconnect(false, false);
    delay(100);
    WiFi.begin();
  }

  if (down >= WD_REBOOT_MS) {
    if (!wifiMgrHotspot()) {
      Serial.println("Wi-Fi watchdog: reboot");
      nw_rtcWdBoots = nw_rtcWdBoots + 1;
      nw_rtcWdFlag = 1;
      cleanRestart();
    }
  }
}

static const char* restartName() {
  if (bootFlag == 1) return "Wi-Fi watchdog";
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "Power on";
    case ESP_RST_EXT:       return "Reset pin";
    case ESP_RST_SW:        return "Restart / update";
    case ESP_RST_PANIC:     return "Crash";
    case ESP_RST_INT_WDT:   return "System watchdog";
    case ESP_RST_WDT:       return "System watchdog";
    case ESP_RST_TASK_WDT:  return "Loop watchdog";
    case ESP_RST_BROWNOUT:  return "Brownout";
    case ESP_RST_DEEPSLEEP: return "Deep sleep";
    default:                return "Other";
  }
}

// "BEACON_TIMEOUT" -> "Beacon timeout"
static const char* reasonText(uint8_t r) {
  const char* n = WiFi.disconnectReasonName((wifi_err_reason_t)r);
  if (!n || !*n) { snprintf(reasonBuf, sizeof(reasonBuf), "Code %u", r); return reasonBuf; }
  size_t i = 0;
  for (; n[i] && i < sizeof(reasonBuf) - 1; i++) {
    char c = n[i];
    if (c == '_') c = ' ';
    else if (i > 0 && c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    reasonBuf[i] = c;
  }
  reasonBuf[i] = 0;
  return reasonBuf;
}

void netwatchStats(NetStats& s) {
  s.uptimeS = millis() / 1000;
  s.drops = drops;
  s.lastDropAgoS = drops ? (int32_t)((millis() - lastDropMs) / 1000) : -1;
  s.lastDropReason = drops ? reasonText(lastDropReason) : "";
  s.restartReason = restartName();
  s.watchdogBoots = nw_rtcWdBoots;
}
