#include "wifimgr.h"
#include "config.h"
#include "cloud.h"
#include "httpsrv.h"
#include <WiFi.h>
#include <DNSServer.h>
#include "esp_wifi.h"

static const uint32_t TEST_MS = 30000, HOTSPOT_AFTER_MS = 5UL * 60 * 1000, CLOSE_AFTER_MS = 15000;

static volatile bool pending = false, testing = false;
static String pSsid, pPass, tSsid, tMsg;
static volatile uint8_t wt = 0;                 // 0 idle, 1 testing, 2 ok, 3 failed
static volatile uint8_t tReason = 0;
static uint32_t t0 = 0;
static wifi_config_t oldCfg;
static bool haveOld = false;

static bool hs = false, hsManual = false;
static uint32_t hsIdleSince = 0;
static uint32_t downSince = 0, closeAt = 0;
static DNSServer dns;

static String savedSsid() {
  wifi_config_t c; if (esp_wifi_get_config(WIFI_IF_STA, &c) != ESP_OK) return "";
  return String((const char*)c.sta.ssid);
}
static bool haveCreds() { return savedSsid().length() > 0; }

static void onEvent(arduino_event_t* e) {
  if (e->event_id == ARDUINO_EVENT_WIFI_STA_DISCONNECTED && testing) tReason = e->event_info.wifi_sta_disconnected.reason;
}

void wifiMgrBegin() { WiFi.onEvent(onEvent); }
bool wifiMgrTesting() { return testing || pending; }
bool wifiMgrHotspot() { return hs; }
bool wifiMgrQuiet() { return testing || pending || (hs && WiFi.softAPgetStationNum() > 0); }

bool wifiMgrStartTest(const String& ssid, const String& pass) {
  if (testing || pending || ssid.length() == 0 || ssid.length() > 32 || pass.length() > 63) return false;
  pSsid = ssid; pPass = pass; tSsid = ssid; tMsg = ""; wt = 1;
  pending = true;
  return true;
}

static const char* reasonText(uint8_t r) {
  switch (r) {
    case WIFI_REASON_NO_AP_FOUND: return "network not found";
    case WIFI_REASON_AUTH_FAIL: case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_802_1X_AUTH_FAILED: return "wrong password";
    case 0: return "no answer from the network";
    default: return WiFi.disconnectReasonName((wifi_err_reason_t)r);
  }
}

static void startHotspot(bool manual) {
  Serial.println(manual ? "Button: opening hotspot " HOTSPOT_SSID : "Wi-Fi lost for 5 min: opening hotspot " HOTSPOT_SSID);
  hsManual = manual; hsIdleSince = millis();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(HOTSPOT_SSID);                        // open network, no password
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());               // every name -> the setup page
  hs = true; closeAt = 0;
}
static void stopHotspot() {
  Serial.println("Back online: closing hotspot");
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  hs = false; hsManual = false; closeAt = 0; downSince = 0;
}

void wifiMgrOpenHotspot() {
  if (hs) { hsIdleSince = millis(); return; }   // already open: restart its timer
  startHotspot(true);
}
bool wifiMgrHotspotManual() { return hs && hsManual; }

void wifiMgrTick() {
  uint32_t now = millis();
  bool up = WiFi.status() == WL_CONNECTED;

  // ---- change Wi-Fi: start a test ----
  if (pending) {
    pending = false;
    haveOld = esp_wifi_get_config(WIFI_IF_STA, &oldCfg) == ESP_OK && oldCfg.sta.ssid[0];
    tReason = 0; testing = true; t0 = now;
    Serial.printf("Wi-Fi: trying %s\n", pSsid.c_str());
    esp_wifi_set_storage(WIFI_STORAGE_RAM);          // nothing is saved unless it works
    WiFi.disconnect(false, false);
    delay(100);
    WiFi.begin(pSsid.c_str(), pPass.length() ? pPass.c_str() : nullptr);
    pPass = "";
    return;
  }
  if (testing) {
    if (up && WiFi.SSID() == tSsid) {
      wifi_config_t c;
      if (esp_wifi_get_config(WIFI_IF_STA, &c) == ESP_OK) { esp_wifi_set_storage(WIFI_STORAGE_FLASH); esp_wifi_set_config(WIFI_IF_STA, &c); }
      else esp_wifi_set_storage(WIFI_STORAGE_FLASH);
      testing = false; wt = 2;
      Serial.printf("Wi-Fi: now on %s (saved)\n", tSsid.c_str());
    } else if (now - t0 > TEST_MS) {
      tMsg = reasonText(tReason);
      Serial.printf("Wi-Fi: %s failed (%s), going back\n", tSsid.c_str(), tMsg.c_str());
      WiFi.disconnect(false, false);
      delay(100);
      esp_wifi_set_storage(WIFI_STORAGE_FLASH);
      if (haveOld) { esp_wifi_set_config(WIFI_IF_STA, &oldCfg); WiFi.begin(); }
      testing = false; wt = 3;
    }
    return;
  }

  // ---- fallback hotspot ----
  if (hs) {
    dns.processNextRequest();
    if (hsManual) {
      // Opened with the button: stays up while someone is on it, closes 10 min after
      // they leave, or 15 s after a Wi-Fi change from its page succeeds.
      if (WiFi.softAPgetStationNum() > 0) hsIdleSince = now;
      if (wt == 2 && up && !closeAt) closeAt = now + CLOSE_AFTER_MS;
      if ((closeAt && (int32_t)(now - closeAt) >= 0) || now - hsIdleSince >= HOTSPOT_MANUAL_MS) { wt = 0; stopHotspot(); }
      return;
    }
    if (up) { if (!closeAt) closeAt = now + CLOSE_AFTER_MS; else if ((int32_t)(now - closeAt) >= 0) stopHotspot(); }
    else closeAt = 0;
    return;
  }
  if (up || strcmp(cloudStatus(), "Provisioning") == 0 || !haveCreds()) { downSince = 0; return; }
  if (!downSince) { downSince = now; return; }
  if (now - downSince >= HOTSPOT_AFTER_MS) startHotspot(false);
}

String  wifiMgrSsid() { return WiFi.status() == WL_CONNECTED ? WiFi.SSID() : savedSsid(); }
uint8_t wifiMgrTestState() { return wt; }
String  wifiMgrTestSsid() { return tSsid; }
String  wifiMgrTestMsg() { return tMsg; }
void wifiMgrHotspotJson(String& j) {
  j = "{\"name\":\"" DEVICE_NAME "\",\"ssid\":" + jsonQuote(savedSsid()) + ",\"mins\":5,\"mdns\":\"" MDNS_NAME "\"";
  j += String(",\"manual\":") + (hsManual ? "true" : "false") + ",\"online\":" + (WiFi.status() == WL_CONNECTED ? "true" : "false");
  j += ",\"wt\":" + String(wt) + ",\"wtSsid\":" + jsonQuote(tSsid) + ",\"wtMsg\":" + jsonQuote(tMsg) + "}";
}
void wifiMgrScanJson(String& j) {
  if (testing || pending) { j = "{\"busy\":true}"; return; }
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) { j = "{\"busy\":true}"; return; }
  if (n < 0) { WiFi.scanNetworks(true, false); j = "{\"busy\":true}"; return; }
  // strongest first, one entry per name, at most 15
  int idx[40]; int m = 0;
  for (int i = 0; i < n && m < 40; i++) { if (WiFi.SSID(i).length()) idx[m++] = i; }
  for (int a = 0; a < m; a++) for (int b = a + 1; b < m; b++) if (WiFi.RSSI(idx[b]) > WiFi.RSSI(idx[a])) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
  j = "{\"nets\":["; int out = 0; String seen = "\n";
  for (int a = 0; a < m && out < 15; a++) {
    String s = WiFi.SSID(idx[a]); if (seen.indexOf("\n" + s + "\n") >= 0) continue; seen += s + "\n";
    if (out++) j += ",";
    j += "{\"ssid\":" + jsonQuote(s) + ",\"rssi\":" + String(WiFi.RSSI(idx[a])) + (WiFi.encryptionType(idx[a]) == WIFI_AUTH_OPEN ? ",\"open\":true" : "") + "}";
  }
  j += "]}";
  WiFi.scanDelete();
}
