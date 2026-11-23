#include "ir.h"
#include "config.h"
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <stdarg.h>

static IRrecv irrecv(PIN_IR, 1024, 50, true);   // 50 ms gap timeout so A/C remotes capture whole
static decode_results results;

static bool     closed[NUM_SLOTS];
static uint32_t closedAt[NUM_SLOTS], hitAt[NUM_SLOTS], reTriggerAt[NUM_SLOTS];
static LogEntry logBuf[LOG_SIZE];
static int      logHead = 0, logCount = 0;
static uint32_t nextLogId = 1;
static int      learnSlot = -1;
static uint32_t learnStart = 0;
static IrCode   lastCode = {};
static uint32_t lastCodeAt = 0, lastAnyAt = 0;
static int      heldSlot = -1;
static char     msgText[128] = "";
static const char* msgType = "info";
static uint32_t msgUntil = 0;

// ---- helpers -------------------------------------------------------------------------
static bool codeEq(const IrCode& a, const IrCode& b) {
  return a.proto == b.proto && a.value == b.value && (a.proto == UNKNOWN || a.bits == b.bits);
}
String irCodeType(const IrCode& c) { return typeToString((decode_type_t)c.proto); }
String irCodeHex(const IrCode& c) { String h = uint64ToString(c.value, 16); h.toUpperCase(); return h; }
static String codeName(const IrCode& c) { return irCodeType(c) + " 0x" + irCodeHex(c); }

static uint64_t fnv1a(const uint8_t* d, size_t n) {
  uint64_t h = 1469598103934665603ULL;
  for (size_t i = 0; i < n; i++) { h ^= d[i]; h *= 1099511628211ULL; }
  return h;
}

// A decode result -> a storable code. False for junk.
static bool normalise(const decode_results& r, IrCode& c) {
  c = {};
  c.proto = (int16_t)r.decode_type;
  c.bits = r.bits;
  if (hasACState(r.decode_type)) {             // A/C remotes send a whole state block
    size_t n = r.bits / 8;
    if (n == 0 || n > kStateSizeMax) return false;
    c.value = fnv1a(r.state, n);
  } else c.value = r.value;
  // RC5/RC6 flip a toggle bit on every new press; ignore it so a button is one code.
  if (r.decode_type == RC5 || r.decode_type == RC5X) c.value &= ~0x800ULL;
  if (r.decode_type == RC6 && r.bits == 20)          c.value &= ~0x10000ULL;
  if (r.decode_type == RC6 && r.bits == 36)          c.value &= ~0x8000ULL;
  return !(c.proto == UNKNOWN && c.value == 0);
}

int irFindSlot(const IrCode& c) {
  for (int s = 0; s < NUM_SLOTS; s++)
    for (int k = 0; k < settings.slots[s].count; k++)
      if (codeEq(settings.slots[s].codes[k], c)) return s;
  return -1;
}

void irMsg(const char* type, const char* fmt, ...) {
  va_list ap; va_start(ap, fmt); vsnprintf(msgText, sizeof(msgText), fmt, ap); va_end(ap);
  msgType = type; msgUntil = millis() + MSG_SHOW_MS;
  Serial.println(msgText);
}

void irCurrentMsg(char* out, size_t n, const char*& type) {
  uint32_t now = millis();
  if (learnSlot >= 0) {
    uint32_t left = (LEARN_TIMEOUT_MS - (now - learnStart) + 999) / 1000;
    snprintf(out, n, "Learning for %s: press a button on the remote (%lu s)", settings.slots[learnSlot].label, (unsigned long)left);
    type = "learn"; return;
  }
  if ((int32_t)(msgUntil - now) > 0) { strlcpy(out, msgText, n); type = msgType; return; }
  strlcpy(out, "Ready", n); type = "info";
}

// ---- sensors -------------------------------------------------------------------------
static void press(int s, uint32_t now) {
  hitAt[s] = now;
  Slot& z = settings.slots[s];
  if (z.mode == MODE_TOGGLE) {
    closed[s] = !closed[s];
    irMsg("ok", "%s: %s", z.label, closed[s] ? "closed" : "opened");
  } else {
    if (closed[s]) { closed[s] = false; reTriggerAt[s] = now + 300; }   // still closed from the last press: open, then close again
    else { closed[s] = true; closedAt[s] = now; }
    irMsg("ok", "%s triggered", z.label);
  }
}

bool irAddCode(int s, const IrCode& c) {
  String nm = codeName(c);
  int o = irFindSlot(c);
  Slot& z = settings.slots[s];
  if (o == s) { irMsg("warn", "%s is already learned for %s", nm.c_str(), z.label); return false; }
  if (o >= 0) { irMsg("warn", "%s is already used by %s. Remove it there first.", nm.c_str(), settings.slots[o].label); return false; }
  if (z.count >= MAX_CODES) { irMsg("warn", "%s already has %d buttons. Remove one first.", z.label, MAX_CODES); return false; }
  z.codes[z.count++] = c;
  settingsSaveSlot(s);
  irMsg("ok", "Learned %s for %s", nm.c_str(), z.label);
  return true;
}

void irRemoveCode(int s, int i) {
  Slot& z = settings.slots[s];
  if (i < 0 || i >= z.count) return;
  String nm = codeName(z.codes[i]);
  for (int k = i; k < z.count - 1; k++) z.codes[k] = z.codes[k + 1];
  z.count--;
  settingsSaveSlot(s);
  irMsg("info", "Removed %s from %s", nm.c_str(), z.label);
}

void irClearSlot(int s) {
  settings.slots[s].count = 0;
  settingsSaveSlot(s);
  irMsg("info", "Cleared all buttons from %s", settings.slots[s].label);
}

void irModeChanged(int s) { if (settings.slots[s].mode == MODE_MOMENTARY && closed[s]) closedAt[s] = millis(); }

bool irStartLearn(int s) { if (s < 0 || s >= NUM_SLOTS) return false; learnSlot = s; learnStart = millis(); return true; }
void irCancelLearn() { if (learnSlot >= 0) { learnSlot = -1; irMsg("info", "Learning cancelled"); } }
int  irLearnSlot() { return learnSlot; }
bool irClosed(int s) { return closed[s]; }
bool irHit(int s) { return hitAt[s] && millis() - hitAt[s] < HIT_SHOW_MS; }
bool irRecentActivity() { return lastAnyAt && millis() - lastAnyAt < 60; }
int  irLogCount() { return logCount; }
const LogEntry& irLogAt(int k) { return logBuf[(logHead - 1 - k + LOG_SIZE * 2) % LOG_SIZE]; }
bool irLogFind(uint32_t id, IrCode& out) {
  for (int k = 0; k < logCount; k++) if (logBuf[k].id == id) { out = logBuf[k].code; return true; }
  return false;
}

// ---- main ----------------------------------------------------------------------------
void irBegin() {
  irrecv.setUnknownThreshold(12);   // ignore very short noise bursts
  irrecv.enableIRIn();
  Serial.printf("IR receiver on GPIO %d\n", PIN_IR);
}

void irTick() {
  uint32_t now = millis();

  if (irrecv.decode(&results)) {
    lastAnyAt = now;
    bool within = now - lastCodeAt <= settings.releaseMs;
    IrCode c;
    if (results.repeat) {
      // Repeat frame (e.g. NEC): only extends a press that is already held.
      if (within) { lastCodeAt = now; if (heldSlot >= 0) hitAt[heldSlot] = now; }
    } else if (normalise(results, c)) {
      if (within && codeEq(c, lastCode)) {
        // The same full frame repeated while the button is held (Sony, Samsung, ...).
        lastCodeAt = now; if (heldSlot >= 0) hitAt[heldSlot] = now;
      } else {
        lastCode = c; lastCodeAt = now;
        logBuf[logHead] = { nextLogId++, now, c };
        logHead = (logHead + 1) % LOG_SIZE;
        if (logCount < LOG_SIZE) logCount++;
        Serial.printf("IR: %s (%u bits)\n", codeName(c).c_str(), c.bits);
        if (learnSlot >= 0) { irAddCode(learnSlot, c); learnSlot = -1; heldSlot = -1; }
        else { heldSlot = irFindSlot(c); if (heldSlot >= 0) press(heldSlot, now); }
      }
    }
    irrecv.resume();
  }

  if (heldSlot >= 0 && now - lastCodeAt > settings.releaseMs) heldSlot = -1;
  for (int s = 0; s < NUM_SLOTS; s++) {
    if (reTriggerAt[s] && (int32_t)(now - reTriggerAt[s]) >= 0) { reTriggerAt[s] = 0; closed[s] = true; closedAt[s] = now; }
    else if (closed[s] && settings.slots[s].mode == MODE_MOMENTARY && now - closedAt[s] >= settings.holdMs) closed[s] = false;
  }
  if (learnSlot >= 0 && now - learnStart > LEARN_TIMEOUT_MS) {
    irMsg("warn", "No button received for %s. Try again.", settings.slots[learnSlot].label);
    learnSlot = -1;
  }
}
