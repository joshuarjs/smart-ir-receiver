#pragma once
#include <Arduino.h>
#include "esp_http_server.h"

// ---------------------------------------------------------------------------
// HTTP server adapter around ESP-IDF's esp_http_server, plus the small text helpers
// the API handlers share. The server runs in its own task and serves several
// connections at once with keep-alive; handlers that touch session / settings /
// Tune state run under webLock(), which loop() also takes while it works on them.
// ---------------------------------------------------------------------------

void webLock();
void webUnlock();

// Request being handled (valid inside a route handler only).
class Request {
 public:
  void bind(httpd_req_t* r);
  String arg(const char* name);          // query or form field ("plain" = raw body)
  bool   hasArg(const char* name);
  int    argInt(const char* name, int def);
  void   send(int code, const char* type, const char* data, size_t len);
  void   send(int code, const char* type, const String& s) { send(code, type, s.c_str(), s.length()); }
  void   sendJson(const char* j) { send(200, "application/json", j, strlen(j)); }
  void   sendJson(const String& j) { send(200, "application/json", j); }
  void   sendOk(bool ok, const char* msg = "");
  void   sendGz(const uint8_t* gz, size_t len, const char* etag);   // gzipped page, chunked, with ETag / 304
  httpd_req_t* raw() { return req; }
 private:
  httpd_req_t* req = nullptr;
  String q, body;
};
extern Request request;

// Routes. Handlers are plain functions; locked ones hold webLock() while they run.
void httpAddRoute(const char* uri, httpd_method_t m, void (*fn)(), bool locked = true);
void httpAddRawRoute(const char* uri, httpd_method_t m, esp_err_t (*fn)(httpd_req_t*));   // streaming (firmware upload)
bool httpStart();
bool httpRunning();
uint32_t httpSendErrors();            // responses that timed out on the socket (System card)

// JSON writer into a fixed buffer: no heap churn for the status poll.
class JsonW {
 public:
  JsonW(char* buf, size_t cap);
  void obj();  void endObj();                       // {  }
  void arr(const char* key);  void endArr();        // "key":[  ]
  void key(const char* k);                          // "k": (then raw)
  void str(const char* k, const char* v);
  void num(const char* k, long v);
  void num(const char* k, double v, int dec);
  void boolean(const char* k, bool v);
  void raw(const char* k, const char* v);           // pre-formatted value
  void rawItem(const char* v);                      // array element
  void strItem(const char* v);
  const char* c_str() { b[n] = 0; return b; }
  size_t length() { return n; }
  bool   ok() { return !overflow; }
 private:
  void put(char c); void puts(const char* s); void quote(const char* s); void comma();
  char* b; size_t cap, n = 0; bool overflow = false;
  bool first[6]; int depth = 0; bool afterKey = false;
};
extern char g_jsonBuf[4096];          // shared by handlers (one HTTP task)

// Text helpers
String jsonQuote(const char* s);      // "..." with JSON escaping
// Tiny read-only JSON lookups for the backup import (good enough for our own files):
int  jsonObjEnd(const String& b, int open);                                   // index of the matching '}', -1 if none
bool jsonObjRange(const String& b, const char* key, int from, int to, int& o, int& e);   // "key":{ ... } inside [from,to)
bool jsonNum(const String& b, const char* key, int from, int to, double& v);
bool jsonStr(const String& b, const char* key, int from, int to, String& out);
bool jsonArrRange(const String& b, const char* key, int from, int to, int& o, int& e);   // "key":[ ... ] -> o after '[', e at ']' 
inline String jsonQuote(const String& s) { return jsonQuote(s.c_str()); }
String urlDecode(const String& in);
int    clampi(int v, int lo, int hi);
