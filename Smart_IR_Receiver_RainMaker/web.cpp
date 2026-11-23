#include "web.h"
#include "webui.h"
#include "config.h"
#include "settings.h"
#include "ir.h"
#include "cloud.h"
#include "netwatch.h"
#include "wifimgr.h"
#include "esp_heap_caps.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>
#include <lwip/sockets.h>

static int argSlot() {
  if (!request.hasArg("slot")) return -1;
  int s = request.argInt("slot", -1);
  return (s >= 0 && s < NUM_SLOTS) ? s : -1;
}

static void codeJson(JsonW& w, const IrCode& c, bool withProto) {
  w.obj();
  w.str("t", irCodeType(c).c_str()); w.str("v", irCodeHex(c).c_str()); w.num("b", (long)c.bits);
  if (withProto) w.num("p", (long)c.proto);
  w.endObj();
}

// ---- GET /api/status: everything that changes by itself (polled about once a second) ----
static void handleStatus() {
  NetStats n; netwatchStats(n);
  bool up = WiFi.status() == WL_CONNECTED;
  char msg[160]; const char* mt; irCurrentMsg(msg, sizeof(msg), mt);
  uint32_t now = millis();
  JsonW w(g_jsonBuf, sizeof(g_jsonBuf)); w.obj();
  w.str("fw", FW_VERSION); w.boolean("fwConfirming", otaConfirming());
  w.str("host", MDNS_NAME);
  w.str("ip", up ? WiFi.localIP().toString().c_str() : "");
  w.str("ssid", wifiMgrSsid().c_str()); w.num("rssi", up ? (long)WiFi.RSSI() : 0L);
  w.str("cloud", cloudStatus()); w.boolean("hotspot", wifiMgrHotspot());
  w.num("heapFree", (long)ESP.getFreeHeap()); w.num("heapMax", (long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  w.num("heapMin", (long)ESP.getMinFreeHeap());
  w.num("uptime", (long)n.uptimeS); w.num("drops", (long)n.drops); w.num("dropAgo", (long)n.lastDropAgoS);
  w.str("dropReason", n.lastDropReason); w.str("restart", n.restartReason); w.num("wdBoots", (long)n.watchdogBoots);
  w.num("sendErrs", (long)httpSendErrors());
  w.num("wt", wifiMgrTestState()); w.str("wtSsid", wifiMgrTestSsid().c_str()); w.str("wtMsg", wifiMgrTestMsg().c_str());
  w.num("rev", (long)settingsRev); w.num("learn", (long)irLearnSlot());
  w.str("mt", mt); w.str("msg", msg);
  w.arr("closed"); for (int s = 0; s < NUM_SLOTS; s++) w.rawItem(irClosed(s) ? "1" : "0"); w.endArr();
  w.arr("hit");    for (int s = 0; s < NUM_SLOTS; s++) w.rawItem(irHit(s) ? "1" : "0"); w.endArr();
  w.arr("log");
  for (int k = 0; k < irLogCount(); k++) {
    const LogEntry& e = irLogAt(k);
    w.obj();
    w.num("id", (long)e.id); w.num("age", (long)(now - e.at));
    w.str("t", irCodeType(e.code).c_str()); w.str("v", irCodeHex(e.code).c_str()); w.num("b", (long)e.code.bits);
    w.num("slot", (long)irFindSlot(e.code));
    w.endObj();
  }
  w.endArr();
  w.endObj();
  request.sendJson(w.c_str());
}

// ---- GET /api/config: names, modes, learned buttons, settings (reloaded when rev moves) ----
static void handleConfig() {
  JsonW w(g_jsonBuf, sizeof(g_jsonBuf)); w.obj();
  w.num("rev", (long)settingsRev);
  w.num("hold", (long)settings.holdMs); w.num("release", (long)settings.releaseMs);
  w.num("txPower", settings.txPower); w.num("wifiSleep", settings.wifiSleep);
  w.arr("slots");
  for (int s = 0; s < NUM_SLOTS; s++) {
    const Slot& z = settings.slots[s];
    w.obj(); w.str("label", z.label); w.num("mode", z.mode);
    w.arr("codes"); for (int k = 0; k < z.count; k++) codeJson(w, z.codes[k], false); w.endArr();
    w.endObj();
  }
  w.endArr(); w.endObj();
  if (!w.ok()) { request.sendOk(false, "Config too large"); return; }
  request.sendJson(w.c_str());
}

// ---- POST actions ----------------------------------------------------------------------
static void handleLearn()  { int s = argSlot(); request.sendOk(irStartLearn(s)); }
static void handleCancel() { irCancelLearn(); request.sendOk(true); }
static void handleClear()  { int s = argSlot(); if (s >= 0) irClearSlot(s); request.sendOk(s >= 0); }
static void handleDelCode() { int s = argSlot(); if (s >= 0) irRemoveCode(s, request.argInt("i", -1)); request.sendOk(s >= 0); }
static void handleSlot() {
  int s = argSlot(); if (s < 0) { request.sendOk(false); return; }
  Slot& z = settings.slots[s];
  if (request.hasArg("label")) {
    String l = request.arg("label"); l.trim();
    if (!l.length()) l = "Sensor " + String(s + 1);
    memset(z.label, 0, LABEL_LEN);
    strlcpy(z.label, l.c_str(), LABEL_LEN);
  }
  if (request.hasArg("mode")) { z.mode = request.argInt("mode", 0) == MODE_TOGGLE ? MODE_TOGGLE : MODE_MOMENTARY; irModeChanged(s); }
  settingsSaveSlot(s);
  irMsg("ok", "Saved sensor %d", s + 1);
  request.sendOk(true);
}
static void handleAssign() {
  int s = argSlot(); IrCode c;
  bool ok = s >= 0 && irLogFind((uint32_t)request.argInt("id", 0), c) && irAddCode(s, c);
  request.sendOk(ok);
}
static void handleSettings() {
  Request& r = request;
  settings.holdMs    = clampi(r.argInt("hold", settings.holdMs), 200, 30000);
  settings.releaseMs = clampi(r.argInt("release", settings.releaseMs), 150, 3000);
  settings.txPower   = clampi(r.argInt("txPower", settings.txPower), 0, 8);
  settings.wifiSleep = r.argInt("wifiSleep", settings.wifiSleep) ? 1 : 0;
  settingsSaveGlobals();
  applyWifiTuning();
  irMsg("ok", "Settings saved");
  r.sendOk(true);
}
static void handleCmd() {
  if (request.arg("cmd") == "reboot") { request.sendOk(true, "Restarting"); delay(200); cleanRestart(); return; }
  request.sendOk(false, "Unknown command");
}

// ---- Backup: GET = download, POST = import. Format 1. --------------------------------------
static void handleBackupGet() {
  JsonW w(g_jsonBuf, sizeof(g_jsonBuf)); w.obj();
  w.str("app", "SmartIRReceiver"); w.num("format", 1L); w.str("fw", FW_VERSION);
  time_t t = time(nullptr); w.num("savedAt", t > 1700000000 ? (long)t : 0L);
  w.key("settings"); w.obj();
  w.num("hold", (long)settings.holdMs); w.num("release", (long)settings.releaseMs);
  w.num("txPower", settings.txPower); w.num("wifiSleep", settings.wifiSleep);
  w.endObj();
  w.arr("slots");
  for (int s = 0; s < NUM_SLOTS; s++) {
    const Slot& z = settings.slots[s];
    w.obj(); w.str("label", z.label); w.num("mode", z.mode);
    w.arr("codes"); for (int k = 0; k < z.count; k++) codeJson(w, z.codes[k], true); w.endArr();
    w.endObj();
  }
  w.endArr(); w.endObj();
  if (!w.ok()) { request.sendOk(false, "Backup too large"); return; }
  request.sendJson(w.c_str());
}

static void handleBackupPost() {
  Request& r = request;
  String b = r.arg("plain");
  int so, se;
  if (b.length() < 20 || b.indexOf("\"SmartIRReceiver\"") < 0 || b.indexOf("\"slots\"") < 0) { r.sendOk(false, "That file isn't a Smart IR Receiver backup."); return; }
  int k = b.indexOf("\"slots\""); so = b.indexOf('[', k);
  if (so < 0) { r.sendOk(false, "That file isn't a Smart IR Receiver backup."); return; }
  Slot in[NUM_SLOTS]; int n = 0, i = so + 1;
  while (n < NUM_SLOTS) {
    int o = b.indexOf('{', i), close = b.indexOf(']', i);
    // the slots array ends at the first ']' that comes before the next slot object
    // (each slot's own "codes" array is skipped by jumping to the end of its object)
    if (o < 0 || (close >= 0 && close < o)) break;
    se = jsonObjEnd(b, o); if (se < 0) break;
    Slot& z = in[n]; memset(&z, 0, sizeof(Slot));
    String label; double v;
    if (jsonStr(b, "label", o, se, label)) { label.trim(); strlcpy(z.label, label.c_str(), LABEL_LEN); }
    if (!z.label[0]) snprintf(z.label, LABEL_LEN, "Sensor %d", n + 1);
    z.mode = (jsonNum(b, "mode", o, se, v) && v == 1) ? MODE_TOGGLE : MODE_MOMENTARY;
    int ck = b.indexOf("\"codes\"", o);
    if (ck >= 0 && ck < se) {
      int ci = b.indexOf('[', ck), ce = b.indexOf(']', ci);
      int p = ci + 1;
      while (z.count < MAX_CODES && ci >= 0 && ce > ci) {
        int co = b.indexOf('{', p); if (co < 0 || co > ce) break;
        int cc = jsonObjEnd(b, co); if (cc < 0 || cc > ce) break;
        IrCode& c = z.codes[z.count]; c = {};
        String ts, vs; double pv = 0, bv = 0;
        bool haveP = jsonNum(b, "p", co, cc, pv);
        if (!haveP && jsonStr(b, "t", co, cc, ts)) pv = strToDecodeType(ts.c_str());
        if (jsonStr(b, "v", co, cc, vs) && (haveP || ts.length())) {
          c.proto = (int16_t)pv; c.bits = jsonNum(b, "b", co, cc, bv) ? (uint16_t)bv : 0;
          c.value = strtoull(vs.c_str(), nullptr, 16);
          z.count++;
        }
        p = cc + 1;
      }
    }
    n++; i = se + 1;
  }
  if (!n) { r.sendOk(false, "That backup has no sensors in it."); return; }
  irCancelLearn();
  for (int s = 0; s < NUM_SLOTS; s++) { if (s < n) settings.slots[s] = in[s]; else settingsDefaultSlot(s); }
  int go, ge; double v;
  if (jsonObjRange(b, "settings", 0, b.length(), go, ge)) {
    if (jsonNum(b, "hold", go, ge, v))      settings.holdMs = clampi((int)v, 200, 30000);
    if (jsonNum(b, "release", go, ge, v))   settings.releaseMs = clampi((int)v, 150, 3000);
    if (jsonNum(b, "txPower", go, ge, v))   settings.txPower = clampi((int)v, 0, 8);
    if (jsonNum(b, "wifiSleep", go, ge, v)) settings.wifiSleep = v ? 1 : 0;
  }
  settingsSaveAllSlots(); settingsSaveGlobals(); applyWifiTuning();
  irMsg("ok", "Restored %d sensors from the backup", n);
  r.sendOk(true);
}

// ---- POST /update: the console sends the .bin as the raw request body --------------------------
static esp_err_t handleUpdate(httpd_req_t* r) {
  auto fail = [&](const char* m) { httpd_resp_set_status(r, "500 Internal Server Error"); httpd_resp_set_type(r, "text/plain"); httpd_resp_sendstr(r, m); return ESP_OK; };
  webLock(); irCancelLearn(); webUnlock();
  if (r->content_len <= 0) return fail("No file");
  if (!Update.begin(r->content_len)) return fail("Not enough space");
  static uint8_t buf[2048]; size_t left = r->content_len; int timeouts = 0;
  while (left) {
    int n = httpd_req_recv(r, (char*)buf, left < sizeof(buf) ? left : sizeof(buf));
    if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) continue;
    if (n <= 0) { Update.abort(); return fail("Upload interrupted"); }
    timeouts = 0;
    if (Update.write(buf, n) != (size_t)n) { Update.abort(); return fail(Update.errorString()); }
    left -= n;
  }
  if (!Update.end(true)) return fail(Update.errorString());
  httpd_resp_set_type(r, "text/plain"); httpd_resp_sendstr(r, "ok");
  delay(500);
  cleanRestart();
  return ESP_OK;
}

// ---- pages -----------------------------------------------------------------------------------
// The setup page is served to anyone who reaches the board over its own hotspot, and to
// everyone while the hotspot is up and the board is off Wi-Fi.
static bool onHotspotLink(httpd_req_t* r) {
  if (!wifiMgrHotspot()) return false;
  int fd = httpd_req_to_sockfd(r);
  struct sockaddr_storage a; socklen_t l = sizeof(a);
  if (getsockname(fd, (struct sockaddr*)&a, &l) != 0) return false;
  uint32_t ip = 0;
  if (a.ss_family == AF_INET) ip = ((struct sockaddr_in*)&a)->sin_addr.s_addr;
#if LWIP_IPV6
  else if (a.ss_family == AF_INET6) ip = ((struct sockaddr_in6*)&a)->sin6_addr.un.u32_addr[3];
#endif
  return ip == (uint32_t)WiFi.softAPIP();
}
static const char* PAGE_ETAG = "\"irx-" FW_VERSION "\"";
static void handleIndex() {
  if (wifiMgrHotspot() && (WiFi.status() != WL_CONNECTED || onHotspotLink(request.raw()))) request.sendGz(HOTSPOT_HTML_GZ, HOTSPOT_HTML_GZ_LEN, nullptr);
  else request.sendGz(INDEX_HTML_GZ, INDEX_HTML_GZ_LEN, PAGE_ETAG);
}
static void handleWifiScan() { String j; wifiMgrScanJson(j); request.sendJson(j); }
static void handleWifiSet()  { bool ok = wifiMgrStartTest(request.arg("ssid"), request.arg("pass")); request.sendOk(ok, ok ? "Trying" : "Busy"); }
static void handleHotspot()  { String j; wifiMgrHotspotJson(j); request.sendJson(j); }
// Captive portal: phones probe random URLs after joining the hotspot; send them to the page.
static esp_err_t handleCatchAll(httpd_req_t* r) {
  if (!wifiMgrHotspot()) { httpd_resp_send_404(r); return ESP_OK; }
  httpd_resp_set_status(r, "302 Found");
  httpd_resp_set_hdr(r, "Location", "http://192.168.4.1/");
  httpd_resp_send(r, nullptr, 0);
  return ESP_OK;
}

static void addRoutes() {
  httpAddRoute("/", HTTP_GET, handleIndex, false);
  httpAddRoute("/api/status", HTTP_GET, handleStatus);
  httpAddRoute("/api/config", HTTP_GET, handleConfig);
  httpAddRoute("/api/learn", HTTP_POST, handleLearn);
  httpAddRoute("/api/cancel", HTTP_POST, handleCancel);
  httpAddRoute("/api/clear", HTTP_POST, handleClear);
  httpAddRoute("/api/delcode", HTTP_POST, handleDelCode);
  httpAddRoute("/api/slot", HTTP_POST, handleSlot);
  httpAddRoute("/api/assign", HTTP_POST, handleAssign);
  httpAddRoute("/api/settings", HTTP_POST, handleSettings);
  httpAddRoute("/api/cmd", HTTP_POST, handleCmd);
  httpAddRoute("/api/backup", HTTP_GET, handleBackupGet);
  httpAddRoute("/api/backup", HTTP_POST, handleBackupPost);
  httpAddRoute("/api/wifi/scan", HTTP_GET, handleWifiScan, false);
  httpAddRoute("/api/wifi", HTTP_POST, handleWifiSet, false);
  httpAddRoute("/api/hotspot", HTTP_GET, handleHotspot, false);
  httpAddRawRoute("/update", HTTP_POST, handleUpdate);
  httpAddRawRoute("/*", HTTP_GET, handleCatchAll);                       // must be registered last
}

void webBegin() { webLock(); webUnlock(); }   // creates the lock

void webTick() {
  static uint32_t lastTry = 0; static bool mdnsUp = false;
  if (!httpRunning() && (WiFi.status() == WL_CONNECTED || wifiMgrHotspot()) && millis() - lastTry > 5000) {
    lastTry = millis();
    if (httpStart()) { addRoutes(); Serial.printf("Console: http://%s.local\n", MDNS_NAME); }
  }
  if (!mdnsUp && WiFi.status() == WL_CONNECTED && MDNS.begin(MDNS_NAME)) { MDNS.addService("http", "tcp", 80); mdnsUp = true; }
}
