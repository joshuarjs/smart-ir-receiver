#include "cloud.h"
#include "config.h"
#include "ir.h"
#include "httpsrv.h"
#include "RMaker.h"
#include "WiFi.h"
#include "esp_rmaker_common_events.h"
#include <network_provisioning/manager.h>
#include <network_provisioning/scheme_softap.h>
#include "esp_wifi.h"

// Eight read-only contact sensors. Alexa routines trigger on "closes" / "opens".
// There is no Bluetooth in this build (saves ~26 KB RAM, ~220 KB flash). This board was
// claimed by v1 over Bluetooth; the claim lives in the fctry partition, which updates
// leave alone. A brand-new board is claimed once with the RainMaker Setup sketch.
// Wi-Fi setup without stored credentials uses SoftAP provisioning (PROV_IRREMOTE);
// Wi-Fi changes on a working board use the console or the setup hotspot instead.

static Device* g_dev[NUM_SLOTS];
static char g_names[NUM_SLOTS][28];
static bool g_reported[NUM_SLOTS];          // last reported "closed" state
static volatile bool g_mqtt = false, g_provisioning = false;

static void commonEvent(void*, esp_event_base_t base, int32_t id, void*) {
  if (base != RMAKER_COMMON_EVENT) return;
  if (id == RMAKER_MQTT_EVENT_CONNECTED) g_mqtt = true;
  if (id == RMAKER_MQTT_EVENT_DISCONNECTED) g_mqtt = false;
}

static void provEvent(arduino_event_t* e) {
  if (e->event_id == ARDUINO_EVENT_PROV_START) {
    g_provisioning = true;
    Serial.printf("SoftAP provisioning: join %s in the RainMaker app, PoP %s\n", PROV_SERVICE_NAME, PROV_POP);
  }
  if (e->event_id == ARDUINO_EVENT_PROV_END) g_provisioning = false;
}

void cloudBegin() {
  Node node = RMaker.initNode(NODE_NAME);
  for (int s = 0; s < NUM_SLOTS; s++) {
    snprintf(g_names[s], sizeof(g_names[s]), SENSOR_NAME_FMT, s + 1);
    g_dev[s] = new Device(g_names[s], "esp.device.contact-sensor", nullptr);
    g_dev[s]->addNameParam();
    Param p(SENSOR_PARAM, "esp.param.contact-detection-state", value(true), PROP_FLAG_READ);
    g_dev[s]->addParam(p);
    node.addDevice(*g_dev[s]);
  }
  esp_event_handler_register(RMAKER_COMMON_EVENT, ESP_EVENT_ANY_ID, &commonEvent, nullptr);
  RMaker.enableOTA(OTA_USING_PARAMS);
  RMaker.start();

  WiFi.onEvent(provEvent);
  WiFi.setHostname(MDNS_NAME);
  // Same steps as Arduino's WiFiProv, SoftAP only: provision if no Wi-Fi is stored,
  // otherwise just connect.
  network_prov_mgr_config_t pc = {};
  pc.scheme = network_prov_scheme_softap;
  pc.scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE;
  WiFi.STA.begin(false);
  bool provisioned = false;
  if (network_prov_mgr_init(pc) == ESP_OK && network_prov_mgr_is_wifi_provisioned(&provisioned) == ESP_OK) {
    if (!provisioned) {
      if (network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1, PROV_POP, PROV_SERVICE_NAME, nullptr) != ESP_OK)
        Serial.println("SoftAP provisioning failed to start");
    } else {
      esp_wifi_start();
      network_prov_mgr_deinit();
      WiFi.begin();
    }
  } else {
    Serial.println("Provisioning manager init failed; trying stored Wi-Fi");
    WiFi.begin();
  }
}

// Every loop pass. The desired states are read under the web lock; the reports go out
// after it is released, because an MQTT publish can block on a slow link.
void cloudTick() {
  bool want[NUM_SLOTS];
  webLock();
  for (int s = 0; s < NUM_SLOTS; s++) want[s] = irClosed(s);
  webUnlock();
  for (int s = 0; s < NUM_SLOTS; s++) {
    if (want[s] == g_reported[s]) continue;
    g_reported[s] = want[s];
    g_dev[s]->updateAndReportParam(SENSOR_PARAM, !want[s]);   // the parameter is true = open
  }
}

const char* cloudStatus() {
  if (g_provisioning) return "Provisioning";
  if (WiFi.status() != WL_CONNECTED) return "No Wi-Fi";
  return g_mqtt ? "Connected" : "Connecting";
}
