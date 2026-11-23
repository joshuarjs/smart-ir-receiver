#pragma once
// ---------------------------------------------------------------------------
// Smart IR Receiver - board and default configuration
// Change the PIN block only when moving to another board.
// ---------------------------------------------------------------------------
#define FW_VERSION        "2.1.1"
#define DEVICE_NAME       "Smart IR Receiver"
#define NODE_NAME         "ESP RainMaker Node"   // unchanged from v1 so the app keeps the same node name
#define MDNS_NAME         "irremote"             // http://irremote.local
#define HOTSPOT_SSID      "IRRemote-Setup"       // setup hotspot (open, no password)

// ---- Pins ---------------------------------------------------------------------
// Same header pins on both SuperMinis, so the wiring doesn't change between them.
#if CONFIG_IDF_TARGET_ESP32C6
// ESP32-C6 SuperMini (this receiver). GPIO 8 is the WS2812 RGB LED (kept off), the
// plain status LED is on GPIO 15. 16/17 are UART0. GPIO 5 is a strapping pin, but only
// for the JTAG source (unused eFuse), and the button is open at power-up.
#define PIN_IR            20   // IR receiver OUT (VS1838B / TSOP38238 type, 38 kHz)
#define PIN_LED           15   // plain status LED
#define LED_ACTIVE_LOW    0    // flip to 1 if the LED is lit while idle on Wi-Fi
#define PIN_RGB           8    // WS2812 RGB LED: switched off at boot
#define PIN_HOTSPOT       5    // button to GND: held 2 s = open the setup hotspot now
#define DEF_TX_POWER      8    // index into txPowerTable[] in netwatch.cpp (19.5 dBm, what v1 ran at)
#else
// ESP32-C3 SuperMini. 20 (RX0) is free with USB serial; 21 (TX0) carries the boot ROM log.
#define PIN_IR            20
#define PIN_LED           8    // onboard LED, active low on the C3 SuperMini
#define LED_ACTIVE_LOW    1
#define PIN_HOTSPOT       5
#define DEF_TX_POWER      3    // 8.5 dBm: the C3 SuperMini antenna desenses itself at high power
#endif

// ---- RainMaker --------------------------------------------------------------
// Device and parameter names are the v1 names: keep them, or Alexa routines break.
#define SENSOR_NAME_FMT   "IR Remote Sensor %d"
#define SENSOR_PARAM      "IR Remote Button State"
#define PROV_SERVICE_NAME "PROV_IRREMOTE"   // SoftAP provisioning network (only if no Wi-Fi is stored)
#define PROV_POP          "esp32"           // proof of possession typed into the app

// ---- Sensors and learning ----------------------------------------------------
#define NUM_SLOTS         8      // RainMaker contact sensors
#define MAX_CODES         4      // learned buttons per sensor
#define LOG_SIZE          8      // "Recently received" entries
#define LABEL_LEN         24
#define LEARN_TIMEOUT_MS  10000
#define HIT_SHOW_MS       700    // console dot lit after a button
#define MSG_SHOW_MS       6000   // console status message

// ---- Defaults ---------------------------------------------------------------
#define DEF_HOLD_MS       2000   // momentary sensor close time
#define DEF_RELEASE_MS    500    // signal gap after which a held button counts as released
#define HOTSPOT_HOLD_MS   2000   // GPIO 5 hold time
#define HOTSPOT_MANUAL_MS (10UL * 60 * 1000)   // a button-opened hotspot closes after 10 min with nobody on it
