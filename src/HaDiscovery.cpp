#include <Arduino.h>
#include <espMqttClientAsync.h>
#include <ArduinoJson.h>
#include "HaDiscovery.h"
#include "Credentials.h"
#include "Config.h"
#include "Drivers.h"
#include "Arduino_DebugUtils.h"

extern espMqttClientAsync mqttClient;

namespace HaDiscovery {

struct Entity {
  const char* component;      // "sensor", "switch", "number", "binary_sensor", "button"
  const char* id;             // "ph", "filtration_pump", etc.
  const char* name;           // "pH"
  const char* icon;           // "mdi:..." or nullptr
  const char* unit;           // "°C", "bar", nullptr
  const char* deviceClass;    // "temperature", "pressure", nullptr
  bool controllable;          // true => include command_topic
  // Extras for `number`:
  float minVal, maxVal, step;
  // Extras for `button`:
  const char* buttonAction;   // opaque; carried in command_topic and decoded by MqttBridge
};

static const Entity ENTITIES[] = {
  // ---- measurements ----
  {"sensor", "ph",                 "pH",                     "mdi:alpha-p-circle", nullptr,    nullptr,       false,0,0,0, nullptr},
  {"sensor", "orp",                "ORP",                    "mdi:flash",          "mV",       "voltage",     false,0,0,0, nullptr},
  {"sensor", "water_temp",         "Water temperature",      nullptr,              "\xc2\xb0""C", "temperature", false,0,0,0, nullptr},
  {"sensor", "air_temp",           "Air temperature",        nullptr,              "\xc2\xb0""C", "temperature", false,0,0,0, nullptr},
  // HA auto-converts sensors with device_class=pressure to the user's
  // preferred pressure unit (kPa on metric systems by default). We leave
  // device_class off so HA displays the values exactly as we publish
  // them — one entity in bar, one in psi.
  {"sensor", "pressure",           "Water pressure",         nullptr,              "bar",      nullptr,       false,0,0,0, nullptr},
  {"sensor", "pressure_psi",       "Water pressure (psi)",   nullptr,              "psi",      nullptr,       false,0,0,0, nullptr},
  {"sensor", "filt_uptime_today",  "Filtration uptime today","mdi:timer",          "s",        "duration",    false,0,0,0, nullptr},
  {"sensor", "ph_pump_uptime_today","pH pump uptime today",  "mdi:timer",          "s",        "duration",    false,0,0,0, nullptr},
  {"sensor", "chl_pump_uptime_today","Chl pump uptime today","mdi:timer",          "s",        "duration",    false,0,0,0, nullptr},
  {"sensor", "acid_fill",          "Acid tank fill",         "mdi:water-percent",  "%",        nullptr,       false,0,0,0, nullptr},
  {"sensor", "chl_fill",           "Chlorine tank fill",     "mdi:water-percent",  "%",        nullptr,       false,0,0,0, nullptr},

  // ---- modes ----
  {"switch", "auto_mode",          "Auto mode",              "mdi:auto-fix",       nullptr, nullptr, true, 0,0,0, nullptr},
  {"switch", "winter_mode",        "Winter mode",            "mdi:snowflake",      nullptr, nullptr, true, 0,0,0, nullptr},
  {"switch", "ph_pid",             "pH PID",                 "mdi:chart-line",     nullptr, nullptr, true, 0,0,0, nullptr},
  {"switch", "orp_pid",            "ORP PID",                "mdi:chart-line",     nullptr, nullptr, true, 0,0,0, nullptr},
  {"switch", "heating",            "Heating",                "mdi:radiator",       nullptr, nullptr, true, 0,0,0, nullptr},

  // ---- pumps ----
  {"switch", "filtration_pump",    "Filtration pump",        "mdi:pump",           nullptr, nullptr, true, 0,0,0, nullptr},
  {"switch", "ph_pump",            "pH pump",                "mdi:pump",           nullptr, nullptr, true, 0,0,0, nullptr},
  {"switch", "chl_pump",           "Chlorine pump",          "mdi:pump",           nullptr, nullptr, true, 0,0,0, nullptr},
  {"switch", "robot_pump",         "Robot pump",             "mdi:robot-vacuum",   nullptr, nullptr, true, 0,0,0, nullptr},
  {"switch", "relay_r0_projecteur","Projecteur",             "mdi:spotlight",      nullptr, nullptr, true, 0,0,0, nullptr},
  {"switch", "relay_r1_spare",     "Spare relay",            "mdi:electric-switch",nullptr, nullptr, true, 0,0,0, nullptr},

  // ---- setpoints ----
  {"number", "ph_setpoint",        "pH setpoint",            nullptr,              "pH",       nullptr,       true,  6.5f, 8.0f, 0.1f, nullptr},
  {"number", "orp_setpoint",       "ORP setpoint",           nullptr,              "mV",       "voltage",     true,  400,  900,  10,   nullptr},
  {"number", "water_temp_setpoint","Water temperature setpoint", nullptr,          "\xc2\xb0""C", "temperature", true,  15,   35,   0.5f, nullptr},

  // ---- alarms ----
  {"binary_sensor", "pressure_alarm",   "Pressure alarm",    "mdi:alert",          nullptr, "problem",        false,0,0,0, nullptr},
  {"binary_sensor", "ph_pump_overtime", "pH pump overtime",  "mdi:alert-circle",   nullptr, "problem",        false,0,0,0, nullptr},
  {"binary_sensor", "chl_pump_overtime","Chl pump overtime", "mdi:alert-circle",   nullptr, "problem",        false,0,0,0, nullptr},
  {"binary_sensor", "acid_tank_low",    "Acid tank low",     "mdi:alert",          nullptr, "problem",        false,0,0,0, nullptr},
  {"binary_sensor", "chl_tank_low",     "Chl tank low",      "mdi:alert",          nullptr, "problem",        false,0,0,0, nullptr},

  // ---- diagnostics ----
  {"sensor", "uptime",             "Uptime",                 "mdi:timer-outline",  "s",   "duration", false,0,0,0, nullptr},
  {"sensor", "free_heap",          "Free heap",              "mdi:memory",         "B",   nullptr,    false,0,0,0, nullptr},
  {"sensor", "wifi_rssi",          "WiFi RSSI",              "mdi:wifi",           "dBm", "signal_strength", false,0,0,0, nullptr},
  {"sensor", "firmware_version",   "Firmware version",       "mdi:information",    nullptr, nullptr, false,0,0,0, nullptr},

  // ---- action buttons ----
  {"button", "clear_errors",       "Clear errors",           "mdi:check",          nullptr, nullptr, true, 0,0,0, "clear_errors"},
  {"button", "acid_tank_refill",   "Acid tank refill",       "mdi:water",          nullptr, nullptr, true, 0,0,0, "acid_tank_refill"},
  {"button", "chl_tank_refill",    "Chlorine tank refill",   "mdi:water",          nullptr, nullptr, true, 0,0,0, "chl_tank_refill"},
  {"button", "reboot",             "Reboot",                 "mdi:restart",        nullptr, nullptr, true, 0,0,0, "reboot"},
  {"button", "reset_ph_calib",     "Reset pH calibration",   "mdi:backup-restore", nullptr, nullptr, true, 0,0,0, "reset_ph_calib"},
  {"button", "reset_orp_calib",    "Reset ORP calibration",  "mdi:backup-restore", nullptr, nullptr, true, 0,0,0, "reset_orp_calib"},
  {"button", "reset_psi_calib",    "Reset pressure calibration", "mdi:backup-restore", nullptr, nullptr, true, 0,0,0, "reset_psi_calib"},
};
static const size_t N_ENTITIES = sizeof(ENTITIES) / sizeof(ENTITIES[0]);

static void addDevice(JsonObject obj) {
  JsonObject dev = obj["device"].to<JsonObject>();
  JsonArray ids = dev["identifiers"].to<JsonArray>();
  ids.add(String("poolmaster-") + Credentials::deviceId());
  dev["name"] = "PoolMaster";
  dev["manufacturer"] = "DIY (Loic74650 / ESP32 port)";
  dev["model"] = "ESP32 PoolMaster";
  dev["sw_version"] = FIRMW;
  dev["configuration_url"] = String("http://") + HOSTNAME + ".local/";
}

String availTopic() {
  return String("poolmaster/") + Credentials::deviceId() + "/avail";
}
String stateTopic(const char* id) {
  return String("poolmaster/") + Credentials::deviceId() + "/" + id + "/state";
}
String setTopic(const char* id) {
  return String("poolmaster/") + Credentials::deviceId() + "/" + id + "/set";
}
static String configTopic(const Entity& e) {
  return String("homeassistant/") + e.component + "/poolmaster_" + Credentials::deviceId() + "_" + e.id + "/config";
}
static String uniqueId(const Entity& e) {
  return String("poolmaster_") + Credentials::deviceId() + "_" + e.id;
}

// SP5 — custom slot helpers. The slot id is the user-facing "custom_N"; the
// topic prefix reuses the standard poolmaster/<mac>/<id> convention so the
// existing MqttBridge::dispatchHaSet path can route incoming commands.
static String customConfigTopic(uint8_t idx) {
  char buf[12]; snprintf(buf, sizeof(buf), "custom_%u", (unsigned) idx);
  return String("homeassistant/switch/poolmaster_") +
         Credentials::deviceId() + "_" + buf + "/config";
}
static String customUniqueId(uint8_t idx) {
  char buf[12]; snprintf(buf, sizeof(buf), "custom_%u", (unsigned) idx);
  return String("poolmaster_") + Credentials::deviceId() + "_" + buf;
}
static String customStateTopic(uint8_t idx) {
  char buf[12]; snprintf(buf, sizeof(buf), "custom_%u", (unsigned) idx);
  return String("poolmaster/") + Credentials::deviceId() + "/" + buf + "/state";
}
static String customSetTopic(uint8_t idx) {
  char buf[12]; snprintf(buf, sizeof(buf), "custom_%u", (unsigned) idx);
  return String("poolmaster/") + Credentials::deviceId() + "/" + buf + "/set";
}

void publishAvail() {
  mqttClient.publish(availTopic().c_str(), 1, true, "online");
}

void publishAll() {
  Debug.print(DBG_INFO, "[HA] Publishing %u discovery configs", (unsigned) N_ENTITIES);
  for (size_t i = 0; i < N_ENTITIES; ++i) {
    const Entity& e = ENTITIES[i];
    JsonDocument doc;
    doc["uniq_id"] = uniqueId(e);
    doc["name"] = e.name;
    doc["avty_t"] = availTopic();
    // .to<JsonObject>() on the document root CLEARS the document in
    // ArduinoJson v7 — that was silently dropping uniq_id/name/avty_t
    // and producing discovery payloads HA refused to register. Use
    // .as<JsonObject>() for a non-destructive view instead.
    addDevice(doc.as<JsonObject>());

    if (e.icon) doc["icon"] = e.icon;
    if (e.unit) doc["unit_of_meas"] = e.unit;
    if (e.deviceClass) doc["dev_cla"] = e.deviceClass;

    if (strcmp(e.component, "button") == 0) {
      doc["cmd_t"] = setTopic(e.id);
      doc["payload_press"] = "PRESS";
    } else {
      doc["stat_t"] = stateTopic(e.id);
      if (e.controllable) {
        doc["cmd_t"] = setTopic(e.id);
      }
    }

    if (strcmp(e.component, "switch") == 0) {
      doc["pl_on"] = "ON";
      doc["pl_off"] = "OFF";
      doc["stat_on"] = "ON";
      doc["stat_off"] = "OFF";
    } else if (strcmp(e.component, "binary_sensor") == 0) {
      doc["pl_on"] = "ON";
      doc["pl_off"] = "OFF";
    } else if (strcmp(e.component, "number") == 0) {
      doc["min"] = e.minVal;
      doc["max"] = e.maxVal;
      doc["step"] = e.step;
      doc["mode"] = "box";
    }

    String payload;
    serializeJson(doc, payload);
    mqttClient.publish(configTopic(e).c_str(), 1, true, payload.c_str());

    // If controllable, subscribe to its set topic
    if (e.controllable || strcmp(e.component, "button") == 0) {
      mqttClient.subscribe(setTopic(e.id).c_str(), 1);
    }
  }

  // SP5 — custom switches. Publish retained discovery for enabled slots;
  // publish an empty retained payload for disabled slots so HA retracts any
  // previously-announced entity.
  for (uint8_t i = 0; i < Drivers::customSlotCount(); ++i) {
    String cfgTopic = customConfigTopic(i);
    if (!Drivers::isCustomEnabled(i)) {
      mqttClient.publish(cfgTopic.c_str(), 1, /*retain=*/true, "");
      continue;
    }

    JsonDocument doc;
    doc["uniq_id"] = customUniqueId(i);
    doc["name"]    = Drivers::customDisplayName(i);
    doc["avty_t"]  = availTopic();
    addDevice(doc.as<JsonObject>());
    doc["stat_t"]  = customStateTopic(i);
    doc["cmd_t"]   = customSetTopic(i);
    doc["pl_on"]   = "ON";  doc["pl_off"]   = "OFF";
    doc["stat_on"] = "ON";  doc["stat_off"] = "OFF";

    String payload;
    serializeJson(doc, payload);
    mqttClient.publish(cfgTopic.c_str(), 1, /*retain=*/true, payload.c_str());
    mqttClient.subscribe(customSetTopic(i).c_str(), 1);
    Debug.print(DBG_INFO, "[HA] published custom_%u name=\"%s\"",
      (unsigned) i, Drivers::customDisplayName(i));
  }

  publishAvail();
}

} // namespace HaDiscovery
