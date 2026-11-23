#include "settings.h"
#include <Preferences.h>
#include <IRremoteESP8266.h>

Settings settings;
volatile uint32_t settingsRev = 1;
static Preferences prefs;
static const uint8_t SCHEMA = 1;

static void setSlot(int s, const char* label, uint8_t mode, uint64_t nec) {
  Slot& z = settings.slots[s];
  memset(&z, 0, sizeof(Slot));
  strlcpy(z.label, label, LABEL_LEN);
  z.mode = mode;
  if (nec) { z.codes[0].proto = (int16_t)NEC; z.codes[0].bits = 32; z.codes[0].value = nec; z.count = 1; }
}

// First boot: the v1 hard-coded mapping, so the upgrade keeps working untouched.
static void defaults() {
  settings.holdMs = DEF_HOLD_MS; settings.releaseMs = DEF_RELEASE_MS;
  settings.txPower = DEF_TX_POWER; settings.wifiSleep = 0;
  setSlot(0, "Home theatre on",  MODE_MOMENTARY, 0xFFA25D);
  setSlot(1, "Home theatre off", MODE_MOMENTARY, 0xFF629D);
  setSlot(2, "Lights on",        MODE_MOMENTARY, 0xF7C03F);
  setSlot(3, "Lights off",       MODE_MOMENTARY, 0xF740BF);
  setSlot(4, "Subwoofer",        MODE_TOGGLE,    0xFF02FD);
  for (int s = 5; s < NUM_SLOTS; s++) settingsDefaultSlot(s);
}

void settingsDefaultSlot(int s) {
  char l[LABEL_LEN]; snprintf(l, sizeof(l), "Sensor %d", s + 1);
  setSlot(s, l, MODE_MOMENTARY, 0);
}

void settingsSaveSlot(int s) {
  char k[8]; snprintf(k, sizeof(k), "slot%d", s);
  prefs.putBytes(k, &settings.slots[s], sizeof(Slot));
  settingsRev++;
}
void settingsSaveAllSlots() { for (int s = 0; s < NUM_SLOTS; s++) settingsSaveSlot(s); }

void settingsSaveGlobals() {
  prefs.putUInt("hold", settings.holdMs); prefs.putUInt("release", settings.releaseMs);
  prefs.putUChar("txp", settings.txPower); prefs.putUChar("wsl", settings.wifiSleep);
  settingsRev++;
}

void settingsLoad() {
  prefs.begin("irrx", false);
  defaults();
  if (prefs.getUChar("schema", 0) != SCHEMA) {
    settingsSaveAllSlots(); settingsSaveGlobals(); prefs.putUChar("schema", SCHEMA);
    Serial.println("Settings: first boot, v1 button mapping loaded");
    return;
  }
  settings.holdMs    = constrain(prefs.getUInt("hold", DEF_HOLD_MS), 200, 30000);
  settings.releaseMs = constrain(prefs.getUInt("release", DEF_RELEASE_MS), 150, 3000);
  settings.txPower   = min<uint8_t>(prefs.getUChar("txp", DEF_TX_POWER), 8);
  settings.wifiSleep = prefs.getUChar("wsl", 0) ? 1 : 0;
  for (int s = 0; s < NUM_SLOTS; s++) {
    char k[8]; snprintf(k, sizeof(k), "slot%d", s);
    Slot z;
    if (prefs.getBytesLength(k) == sizeof(Slot) && prefs.getBytes(k, &z, sizeof(Slot)) == sizeof(Slot)) {
      z.label[LABEL_LEN - 1] = 0;
      if (z.count > MAX_CODES) z.count = 0;
      if (z.mode > MODE_TOGGLE) z.mode = MODE_MOMENTARY;
      settings.slots[s] = z;
    }
  }
}
