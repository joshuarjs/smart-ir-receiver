#pragma once
#include <Arduino.h>

// Wi-Fi changes without the RainMaker app (from Sub Masker v1.2.5):
//  - Change Wi-Fi from the console: try a new network for up to 30 s, keep it if it
//    connects, otherwise go back to the old one and report why.
//  - Fallback hotspot: after 5 min without Wi-Fi (credentials stored, not provisioning)
//    the board opens an open hotspot with a small setup page, keeps retrying the saved
//    network, and closes the hotspot once it is back online.
//  - Setup hotspot on demand: GPIO 5 held 2 s opens it right away (wifiMgrOpenHotspot).
// RainMaker, the account link, learned buttons and settings are never touched.
void wifiMgrBegin();             // after netwatchBegin()
void wifiMgrTick();              // every loop()
bool wifiMgrStartTest(const String& ssid, const String& pass);   // false if busy
bool wifiMgrTesting();
bool wifiMgrHotspot();           // hotspot is up
bool wifiMgrHotspotManual();     // opened with the button
void wifiMgrOpenHotspot();       // open it now (or restart its idle timer)
bool wifiMgrQuiet();             // netwatch should not reconnect / reboot right now
String  wifiMgrSsid();            // connected or saved network name
uint8_t wifiMgrTestState();       // 0 idle, 1 testing, 2 ok, 3 failed
String  wifiMgrTestSsid();
String  wifiMgrTestMsg();
void wifiMgrHotspotJson(String& j);  // full object for the hotspot page
void wifiMgrScanJson(String& j);     // {"busy":true} or {"nets":[...]}
