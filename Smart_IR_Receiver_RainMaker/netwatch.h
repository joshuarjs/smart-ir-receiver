#pragma once
#include <Arduino.h>

// Wi-Fi tuning, watchdog and link diagnostics.
void applyWifiTuning();          // TX power + modem sleep from settings
void netwatchBegin();            // call before rmakerBegin(): registers Wi-Fi events
void netwatchTick();             // call from loop()

struct NetStats {
  uint32_t uptimeS;
  uint32_t drops;                // link losses since boot
  int32_t  lastDropAgoS;         // -1 = none since boot
  const char* lastDropReason;    // e.g. "Beacon timeout"
  const char* restartReason;     // why the board last started
  uint32_t watchdogBoots;        // watchdog reboots since last power-on
};
void netwatchStats(NetStats& s);

// OTA rollback guard: a freshly updated app boots "pending verify" and must
// confirm itself or the bootloader reverts to the previous app on the next reset.
// (The RainMaker library disables the core's automatic confirm.)
bool otaConfirming();               // true until this boot's firmware is confirmed

// Restart after leaving the Wi-Fi network cleanly, so the router drops the old
// association at once (otherwise the next connect is refused and retried).
void cleanRestart();
