#pragma once
#include <Arduino.h>
#include "config.h"

// One learned button. proto is IRremoteESP8266's decode_type_t; A/C remotes store a
// hash of their state block in value, RC5/RC6 store it with the toggle bit cleared.
struct IrCode {
  int16_t  proto;
  uint16_t bits;
  uint32_t reserved;
  uint64_t value;
};

enum : uint8_t { MODE_MOMENTARY = 0, MODE_TOGGLE = 1 };

struct Slot {
  char    label[LABEL_LEN];   // console name only (Alexa uses the RainMaker name)
  uint8_t mode;
  uint8_t count;
  IrCode  codes[MAX_CODES];
};

struct Settings {
  uint32_t holdMs;      // momentary close time, 200..30000
  uint32_t releaseMs;   // held-button release gap, 150..3000
  uint8_t  txPower;     // index into txPowerTable (netwatch.cpp)
  uint8_t  wifiSleep;   // 0 = modem sleep off
  Slot     slots[NUM_SLOTS];
};

extern Settings settings;
extern volatile uint32_t settingsRev;   // bumps on every change; the console reloads /api/config when it moves

void settingsLoad();          // defaults (the v1 button mapping on a fresh board), then NVS
void settingsSaveSlot(int s);
void settingsSaveAllSlots();
void settingsSaveGlobals();
void settingsDefaultSlot(int s);
