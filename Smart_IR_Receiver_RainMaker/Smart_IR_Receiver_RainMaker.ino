// ---------------------------------------------------------------------------
// Smart IR Receiver - IR remote buttons become ESP RainMaker contact sensors
// for Alexa routines. Buttons are taught from the web console (irremote.local).
// Board: ESP32-C6 SuperMini (also builds for the C3 SuperMini, see config.h)
// Core: arduino-esp32 3.3.x   Partition: RainMaker 4MB
// Library: IRremoteESP8266 2.9.0. See README.md.
// ---------------------------------------------------------------------------
#include <WiFi.h>
#include "config.h"
#include "settings.h"
#include "ir.h"
#include "web.h"
#include "cloud.h"
#include "netwatch.h"
#include "wifimgr.h"

static void ledWrite(bool on) { digitalWrite(PIN_LED, (on ^ LED_ACTIVE_LOW) ? HIGH : LOW); }

// LED: blip on every IR code, slow blink while learning, double blip while the setup
// hotspot is open, fast blink without Wi-Fi, otherwise off.
static void ledTick() {
  uint32_t now = millis();
  bool on;
  if (irLearnSlot() >= 0)                  on = ((now / 250) % 2) == 0;
  else if (irRecentActivity())             on = true;
  else if (wifiMgrHotspot())               { uint32_t t = now % 2000; on = t < 80 || (t > 200 && t < 280); }
  else if (WiFi.status() != WL_CONNECTED)  on = ((now / 100) % 5) == 0;
  else                                     on = false;
  ledWrite(on);
}

// GPIO 5 button: held 2 s = open the setup hotspot now (the LED flashes to confirm).
static void hotspotButton() {
  static uint32_t heldSince = 0; static bool done = false;
  if (digitalRead(PIN_HOTSPOT) != LOW) { heldSince = 0; done = false; return; }
  if (!heldSince) heldSince = millis();
  if (!done && millis() - heldSince >= HOTSPOT_HOLD_MS) {
    done = true;
    for (int i = 0; i < 6; i++) { ledWrite(!(i & 1)); delay(60); }
    wifiMgrOpenHotspot();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT); ledWrite(false);
#ifdef PIN_RGB
  rgbLedWrite(PIN_RGB, 0, 0, 0);   // the C6 SuperMini's RGB LED stays off
#endif
  pinMode(PIN_HOTSPOT, INPUT_PULLUP);

  settingsLoad();          // learned buttons + settings (the v1 mapping on a fresh board)
  netwatchBegin();         // Wi-Fi tuning on driver start, link watchdog, loop watchdog, OTA confirm
  wifiMgrBegin();          // Wi-Fi changes from the console + setup hotspot
  cloudBegin();            // RainMaker + Wi-Fi; SoftAP provisioning if no Wi-Fi is stored
  webBegin();
  irBegin();
  Serial.printf("%s v%s ready\n", DEVICE_NAME, FW_VERSION);
}

void loop() {
  netwatchTick();
  wifiMgrTick();
  // Console handlers run in the HTTP server task and share the IR / sensor state, so
  // the work that touches it runs under the lock.
  webLock();
  irTick();
  ledTick();
  webUnlock();
  cloudTick();             // RainMaker reports, outside the lock
  webTick();
  hotspotButton();
  delay(2);
}
