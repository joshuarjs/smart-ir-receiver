#pragma once
#include <Arduino.h>
#include "settings.h"

// IR receiving, learning and the sensor states. Everything here runs under webLock():
// loop() calls irTick() with the lock held, and the console handlers hold it too.
// The RainMaker reports happen in cloud.cpp, outside the lock.

struct LogEntry { uint32_t id; uint32_t at; IrCode code; };

void irBegin();
void irTick();                         // decode, learn, momentary release

bool irStartLearn(int s);
void irCancelLearn();
int  irLearnSlot();                    // -1 = not learning
bool irAddCode(int s, const IrCode& c);    // learn / assign; sets the console message
void irRemoveCode(int s, int i);
void irClearSlot(int s);
void irModeChanged(int s);

bool irClosed(int s);                  // desired contact state (true = closed / triggered)
bool irHit(int s);                     // a button for this sensor arrived just now
bool irRecentActivity();               // any IR code in the last ~60 ms (LED blink)
int  irLogCount();
const LogEntry& irLogAt(int k);        // 0 = newest
bool irLogFind(uint32_t id, IrCode& out);
int  irFindSlot(const IrCode& c);

String irCodeType(const IrCode& c);    // "NEC", "SAMSUNG", "UNKNOWN", ...
String irCodeHex(const IrCode& c);     // "FFA25D"

void irMsg(const char* type, const char* fmt, ...);   // type: ok | warn | info
void irCurrentMsg(char* out, size_t n, const char*& type);   // what the display shows now
