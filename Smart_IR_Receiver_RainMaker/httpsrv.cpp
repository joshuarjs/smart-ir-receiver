#include "httpsrv.h"
#include <freertos/semphr.h>
#include <stdio.h>

static SemaphoreHandle_t g_lock = nullptr;
void webLock()   { if (!g_lock) g_lock = xSemaphoreCreateRecursiveMutex(); xSemaphoreTakeRecursive(g_lock, portMAX_DELAY); }
void webUnlock() { if (g_lock) xSemaphoreGiveRecursive(g_lock); }

static httpd_handle_t g_h = nullptr;
static uint32_t g_sendErrs = 0;
Request request;
char g_jsonBuf[4096];

// ---- text helpers ----------------------------------------------------------------
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

String jsonQuote(const char* s) {
  String o = "\"";
  for (; s && *s; s++) {
    unsigned char c = *s;
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c == '\n') o += "\\n";
    else if (c < 0x20) { char h[8]; snprintf(h, sizeof(h), "\\u%04x", c); o += h; }
    else o += (char)c;
  }
  return o + "\"";
}

String urlDecode(const String& in) {
  String o; o.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '+') o += ' ';
    else if (c == '%' && i + 2 < in.length() && isxdigit((uint8_t)in[i + 1]) && isxdigit((uint8_t)in[i + 2])) {
      o += (char)strtol(in.substring(i + 1, i + 3).c_str(), nullptr, 16); i += 2;
    } else o += c;
  }
  return o;
}

int jsonObjEnd(const String& b, int open) {
  int d = 0;
  for (int i = open; i < (int)b.length(); i++) { if (b[i] == '{') d++; else if (b[i] == '}' && --d == 0) return i; }
  return -1;
}
static int jsonKeyAt(const String& b, const char* key, int from, int to) {
  int k = b.indexOf(String("\"") + key + "\"", from); return (k < 0 || k >= to) ? -1 : k;
}
bool jsonObjRange(const String& b, const char* key, int from, int to, int& o, int& e) {
  int k = jsonKeyAt(b, key, from, to); if (k < 0) return false;
  o = b.indexOf('{', k); if (o < 0 || o >= to) return false;
  e = jsonObjEnd(b, o); return e >= 0;
}
bool jsonNum(const String& b, const char* key, int from, int to, double& v) {
  int k = jsonKeyAt(b, key, from, to); if (k < 0) return false;
  int c = b.indexOf(':', k); if (c < 0 || c >= to) return false;
  v = b.substring(c + 1, min(to, c + 24)).toDouble(); return true;
}
bool jsonStr(const String& b, const char* key, int from, int to, String& out) {
  int k = jsonKeyAt(b, key, from, to); if (k < 0) return false;
  int q = b.indexOf('"', b.indexOf(':', k)); if (q < 0 || q >= to) return false;
  out = "";
  for (int i = q + 1; i < to; i++) { char ch = b[i]; if (ch == '\\' && i + 1 < to) { out += b[++i]; continue; } if (ch == '"') return true; out += ch; }
  return false;
}
bool jsonArrRange(const String& b, const char* key, int from, int to, int& o, int& e) {
  int k = jsonKeyAt(b, key, from, to); if (k < 0) return false;
  int i = b.indexOf('[', k); e = b.indexOf(']', i); if (i < 0 || e < 0 || e > to) return false;
  o = i + 1; return true;
}

// name=value&... lookup with URL decoding
static bool formFind(const String& f, const char* name, String& out) {
  size_t n = strlen(name); int i = 0;
  while (i < (int)f.length()) {
    int amp = f.indexOf('&', i); if (amp < 0) amp = f.length();
    int eq = f.indexOf('=', i);
    if (eq > 0 && eq < amp && eq - i == (int)n && f.substring(i, eq) == name) { out = urlDecode(f.substring(eq + 1, amp)); return true; }
    if ((eq < 0 || eq >= amp) && amp - i == (int)n && f.substring(i, amp) == name) { out = ""; return true; }
    i = amp + 1;
  }
  return false;
}

// ---- Request -----------------------------------------------------------------------
void Request::bind(httpd_req_t* r) {
  req = r; q = ""; body = "";
  size_t ql = httpd_req_get_url_query_len(r);
  if (ql) { char* b = (char*)malloc(ql + 1); if (b && httpd_req_get_url_query_str(r, b, ql + 1) == ESP_OK) q = b; free(b); }
  if (r->method == HTTP_POST && r->content_len > 0 && r->content_len <= 8192) {
    body.reserve(r->content_len); char buf[256]; size_t left = r->content_len;
    while (left) {
      int n = httpd_req_recv(r, buf, left < sizeof(buf) ? left : sizeof(buf));
      if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
      if (n <= 0) break;
      body.concat(buf, n); left -= n;
    }
  }
}
String Request::arg(const char* name) {
  if (!strcmp(name, "plain")) return body;
  String v; if (formFind(q, name, v)) return v;
  if (req && req->method == HTTP_POST && formFind(body, name, v)) return v;
  return String();
}
bool Request::hasArg(const char* name) { String v; return formFind(q, name, v) || (req && req->method == HTTP_POST && formFind(body, name, v)); }
int  Request::argInt(const char* name, int def) { return hasArg(name) ? arg(name).toInt() : def; }
void Request::send(int code, const char* type, const char* data, size_t len) {
  if (!req) return;
  httpd_resp_set_status(req, code == 200 ? "200 OK" : code == 404 ? "404 Not Found" : code == 400 ? "400 Bad Request" : "500 Internal Server Error");
  httpd_resp_set_type(req, type);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  if (httpd_resp_send(req, data, len) != ESP_OK) g_sendErrs++;
}
void Request::sendOk(bool ok, const char* msg) {
  JsonW w(g_jsonBuf, sizeof(g_jsonBuf)); w.obj(); w.boolean("ok", ok); w.str("msg", msg); w.endObj();
  sendJson(w.c_str());
}
// A gzipped page in 2 KB chunks, so one slow stretch of Wi-Fi doesn't abort the whole
// transfer; an ETag per firmware build lets a browser that already has this version
// reload it from its cache with a tiny 304.
void Request::sendGz(const uint8_t* gz, size_t len, const char* etag) {
  if (!req) return;
  char inm[40] = "";
  if (etag && httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK && !strcmp(inm, etag)) {
    httpd_resp_set_status(req, "304 Not Modified");
    httpd_resp_set_hdr(req, "ETag", etag); httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_send(req, nullptr, 0);
    return;
  }
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", etag ? "no-cache" : "no-store");
  if (etag) httpd_resp_set_hdr(req, "ETag", etag);
  const char* p = (const char*)gz; size_t left = len;
  while (left) {
    size_t n = left < 2048 ? left : 2048;
    if (httpd_resp_send_chunk(req, p, n) != ESP_OK) { g_sendErrs++; return; }
    p += n; left -= n;
  }
  httpd_resp_send_chunk(req, nullptr, 0);
}

// ---- routes ------------------------------------------------------------------------
struct Route { void (*fn)(); bool locked; };
static esp_err_t routeEntry(httpd_req_t* r) {
  const Route* rt = (const Route*)r->user_ctx;
  if (rt->locked) webLock();
  request.bind(r);
  rt->fn();
  if (rt->locked) webUnlock();
  return ESP_OK;
}
void httpAddRoute(const char* uri, httpd_method_t m, void (*fn)(), bool locked) {
  if (!g_h) return;
  Route* rt = new Route{fn, locked};
  httpd_uri_t u = {}; u.uri = uri; u.method = m; u.handler = routeEntry; u.user_ctx = rt;
  httpd_register_uri_handler(g_h, &u);
}
void httpAddRawRoute(const char* uri, httpd_method_t m, esp_err_t (*fn)(httpd_req_t*)) {
  if (!g_h) return;
  httpd_uri_t u = {}; u.uri = uri; u.method = m; u.handler = fn; u.user_ctx = nullptr;
  httpd_register_uri_handler(g_h, &u);
}
bool httpStart() {
  httpd_config_t c = HTTPD_DEFAULT_CONFIG();
  c.server_port = 80;
  c.ctrl_port = 32790;
  c.stack_size = 8192;
  c.max_uri_handlers = 24;
  c.uri_match_fn = httpd_uri_match_wildcard;   // exact routes still match exactly; "/*" catches the rest
  c.max_open_sockets = 6;                      // lwIP has 16; RainMaker and mDNS need some
  c.lru_purge_enable = true;                   // full: drop the least recently used idle connection
  c.recv_wait_timeout = 10;
  c.send_wait_timeout = 15;                    // a slow link gets time; other connections are still served
  c.backlog_conn = 4;
  if (httpd_start(&g_h, &c) != ESP_OK) { g_h = nullptr; return false; }
  return true;
}
bool httpRunning() { return g_h != nullptr; }
uint32_t httpSendErrors() { return g_sendErrs; }

// ---- JsonW -------------------------------------------------------------------------
JsonW::JsonW(char* buf, size_t c) : b(buf), cap(c) { first[0] = true; }
void JsonW::put(char ch) { if (n + 1 < cap) b[n++] = ch; else overflow = true; }
void JsonW::puts(const char* s) { while (*s) put(*s++); }
void JsonW::quote(const char* s) {
  put('"');
  for (; s && *s; s++) {
    unsigned char ch = *s;
    if (ch == '"' || ch == '\\') { put('\\'); put((char)ch); }
    else if (ch == '\n') puts("\\n");
    else if (ch < 0x20) { char h[8]; snprintf(h, sizeof(h), "\\u%04x", ch); puts(h); }
    else put((char)ch);
  }
  put('"');
}
void JsonW::comma() { if (!first[depth]) put(','); first[depth] = false; }
void JsonW::obj()    { if (afterKey) afterKey = false; else comma(); put('{'); if (depth < 5) depth++; first[depth] = true; }
void JsonW::endObj() { put('}'); if (depth) depth--; }
void JsonW::arr(const char* k) { key(k); afterKey = false; put('['); if (depth < 5) depth++; first[depth] = true; }
void JsonW::endArr() { put(']'); if (depth) depth--; }
void JsonW::key(const char* k) { comma(); quote(k); put(':'); afterKey = true; }   // followed by obj() or a value
void JsonW::str(const char* k, const char* v) { key(k); quote(v); afterKey = false; }
void JsonW::num(const char* k, long v) { key(k); char t[16]; snprintf(t, sizeof(t), "%ld", v); puts(t); afterKey = false; }
void JsonW::num(const char* k, double v, int dec) { key(k); char t[24]; snprintf(t, sizeof(t), "%.*f", dec, v); puts(t); afterKey = false; }
void JsonW::boolean(const char* k, bool v) { key(k); puts(v ? "true" : "false"); afterKey = false; }
void JsonW::raw(const char* k, const char* v) { key(k); puts(v); afterKey = false; }
void JsonW::rawItem(const char* v) { comma(); puts(v); }
void JsonW::strItem(const char* v) { comma(); quote(v); }
