#include <Arduino.h>
#include <espMqttClientAsync.h>
#include <Preferences.h>
#include "Config.h"
#include "PoolMaster.h"
#include "Credentials.h"
#include "MqttBridge.h"
#include "HaDiscovery.h"
#include "LogBuffer.h"   // tee MQTT lifecycle into the WS-visible log ring
#include "Drivers.h"

espMqttClientAsync mqttClient;
bool MQTTConnection = false;
int  MqttLastDisconnectReason = -1;  // -1 = never disconnected; >=0 = espMqttClientTypes::DisconnectReason

static TimerHandle_t mqttReconnectTimer;

// Legacy error topic kept for mqttErrorPublish() call sites; full removal in a later pass.
static const char* PoolTopicError_Legacy  = POOLTOPIC_LEGACY "Err";

static bool legacyCleanupDone() {
  Preferences p; p.begin("mqtt", true);
  bool done = p.getBool("legacy_swept", false);
  p.end();
  return done;
}

static void markLegacyCleanupDone() {
  Preferences p; p.begin("mqtt", false);
  p.putBool("legacy_swept", true);
  p.end();
}

static void sweepLegacyRetainedTopics() {
  if (legacyCleanupDone()) return;
  Debug.print(DBG_INFO, "[MQTT] Sweeping legacy retained topics");
  static const char* LEGACY[] = {
    "Home/Pool/Meas1", "Home/Pool/Meas2", "Home/Pool/Status", "Home/Pool/Err",
    "Home/Pool/API",   "Home/Pool/Set1",  "Home/Pool/Set2",   "Home/Pool/Set3",
    "Home/Pool/Set4",  "Home/Pool/Set5",
    "Home/Pool6/Meas1","Home/Pool6/Meas2","Home/Pool6/Status","Home/Pool6/Err",
    "Home/Pool6/API",  "Home/Pool6/Set1", "Home/Pool6/Set2",  "Home/Pool6/Set3",
    "Home/Pool6/Set4", "Home/Pool6/Set5",
  };
  for (const char* t : LEGACY) {
    mqttClient.publish(t, 1, true, "");   // empty retained => broker drops
  }
  markLegacyCleanupDone();
}

static void onMqttConnect(bool sessionPresent);
static void onMqttDisconnect(espMqttClientTypes::DisconnectReason reason);
static void onMqttMessage(const espMqttClientTypes::MessageProperties& properties,
                          const char* topic,
                          const uint8_t* payload,
                          size_t len, size_t index, size_t total);

void startMqttReconnectTimer() { if (mqttReconnectTimer) xTimerStart(mqttReconnectTimer, 0); }
void stopMqttReconnectTimer()  { if (mqttReconnectTimer) xTimerStop(mqttReconnectTimer, 0); }

// espMqttClient stores raw const char* pointers from setServer() and
// setCredentials() WITHOUT copying — see espMqttClient/src/MqttClientSetup.h
// and MqttClient.h `_host`, `_username`, `_password` members. If we pass
// pointers into local Strings that go out of scope, the library reads
// freed memory on every connect attempt and the destination IP comes back
// as garbage (we observed it as 0.0.0.0 in tcpdump on the broker host).
// Keep these alive for the lifetime of the process via static storage.
static String s_mqttHost;
static String s_mqttUser;
static String s_mqttPass;

void mqttInit()
{
  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0,
                                    reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onMessage(onMqttMessage);

  s_mqttHost = Credentials::mqttHost();
  if (s_mqttHost.isEmpty()) {
    Debug.print(DBG_INFO, "[MQTT] No broker configured — bridge idle");
    return;
  }
  mqttClient.setServer(s_mqttHost.c_str(), Credentials::mqttPort());

  s_mqttUser = Credentials::mqttUser();
  s_mqttPass = Credentials::mqttPass();
  if (!s_mqttUser.isEmpty()) mqttClient.setCredentials(s_mqttUser.c_str(), s_mqttPass.c_str());

  static char willTopic[64];
  snprintf(willTopic, sizeof(willTopic), "poolmaster/%s/avail", Credentials::deviceId().c_str());
  mqttClient.setWill(willTopic, 1, true, "offline");
}

void connectToMqtt()
{
  if (Credentials::mqttHost().isEmpty()) {
    LogBuffer::append(LogBuffer::L_WARN, "[MQTT] connect skipped: no broker host configured");
    return;
  }
  Debug.print(DBG_INFO, "[MQTT] Connecting...");
  LogBuffer::append(LogBuffer::L_INFO, "[MQTT] connecting to %s:%u as '%s'",
                    Credentials::mqttHost().c_str(),
                    (unsigned)Credentials::mqttPort(),
                    Credentials::mqttUser().c_str());
  mqttClient.connect();
}

void mqttErrorPublish(const char* payload)
{
  if (!mqttClient.connected()) return;
  mqttClient.publish(PoolTopicError_Legacy, 1, true, payload);
}

bool enqueueCommand(const char* jsonStr)
{
  char buf[100] = "";
  size_t n = strlen(jsonStr);
  if (n >= sizeof(buf)) n = sizeof(buf) - 1;
  memcpy(buf, jsonStr, n);
  buf[n] = '\0';
  return xQueueSendToBack(queueIn, &buf, 0) == pdPASS;
}

struct HaCmdMap { const char* entityId; const char* jsonKeyOnOff; const char* jsonKeyNum; };

static const HaCmdMap HA_SWITCH_MAP[] = {
  {"auto_mode",        "Mode",     nullptr},
  {"winter_mode",      "Winter",   nullptr},
  {"ph_pid",           "PhPID",    nullptr},
  {"orp_pid",          "OrpPID",   nullptr},
  {"heating",          "Heat",     nullptr},
  {"filtration_pump",  "FiltPump", nullptr},
  {"ph_pump",          "PhPump",   nullptr},
  {"chl_pump",         "ChlPump",  nullptr},
  {"robot_pump",       "RobotPump",nullptr},
};

static const HaCmdMap HA_NUMBER_MAP[] = {
  {"ph_setpoint",         nullptr, "PhSetPoint"},
  {"orp_setpoint",        nullptr, "OrpSetPoint"},
  {"water_temp_setpoint", nullptr, "WSetPoint"},
};

struct RelayMap { const char* entityId; int relayNum; };
static const RelayMap HA_RELAY_MAP[] = {
  {"relay_r0_projecteur", 1},   // legacy R1 -> projecteur
  {"relay_r1_spare",      2},
};

struct HaButtonMap { const char* entityId; const char* jsonPayload; };
static const HaButtonMap HA_BUTTON_MAP[] = {
  {"clear_errors",     "{\"Clear\":1}"},
  {"acid_tank_refill", "{\"pHTank\":[20,100]}"},
  {"chl_tank_refill",  "{\"ChlTank\":[20,100]}"},
  {"reboot",           "{\"Reboot\":1}"},
  {"reset_ph_calib",   "{\"RstpHCal\":1}"},
  {"reset_orp_calib",  "{\"RstOrpCal\":1}"},
  {"reset_psi_calib",  "{\"RstPSICal\":1}"},
};

// Extract entity ID from "poolmaster/<mac>/<entityId>/set"
static bool parseSetTopic(const char* topic, char* entityIdOut, size_t capacity) {
  const char* prefix = "poolmaster/";
  if (strncmp(topic, prefix, strlen(prefix)) != 0) return false;
  const char* slash2 = strchr(topic + strlen(prefix), '/');
  if (!slash2) return false;
  const char* slash3 = strchr(slash2 + 1, '/');
  if (!slash3) return false;
  size_t len = slash3 - (slash2 + 1);
  if (len >= capacity) return false;
  memcpy(entityIdOut, slash2 + 1, len);
  entityIdOut[len] = '\0';
  return strcmp(slash3, "/set") == 0;
}

static void dispatchHaSet(const char* entityId, const char* payload) {
  // SP5 — entity IDs "custom_0".."custom_7" dispatch to the generic
  // CustomOutput command. parseBool semantics match the fixed switches.
  if (strncmp(entityId, "custom_", 7) == 0 && entityId[7] >= '0' && entityId[7] <= '7' && entityId[8] == '\0') {
    uint8_t idx = (uint8_t)(entityId[7] - '0');
    bool    on  = (strcasecmp(payload, "ON") == 0) || (strcasecmp(payload, "1") == 0);
    char json[40];
    snprintf(json, sizeof(json), "{\"CustomOutput\":[%u,%d]}", (unsigned) idx, on ? 1 : 0);
    enqueueCommand(json);
    return;
  }

  char json[100];
  for (const auto& m : HA_SWITCH_MAP) {
    if (strcmp(entityId, m.entityId) == 0) {
      snprintf(json, sizeof(json), "{\"%s\":%d}", m.jsonKeyOnOff, (strcmp(payload, "ON") == 0) ? 1 : 0);
      enqueueCommand(json);
      return;
    }
  }
  for (const auto& m : HA_NUMBER_MAP) {
    if (strcmp(entityId, m.entityId) == 0) {
      double v = atof(payload);
      snprintf(json, sizeof(json), "{\"%s\":%.3f}", m.jsonKeyNum, v);
      enqueueCommand(json);
      return;
    }
  }
  for (const auto& m : HA_RELAY_MAP) {
    if (strcmp(entityId, m.entityId) == 0) {
      int on = (strcmp(payload, "ON") == 0) ? 1 : 0;
      snprintf(json, sizeof(json), "{\"Relay\":[%d,%d]}", m.relayNum, on);
      enqueueCommand(json);
      return;
    }
  }
  for (const auto& b : HA_BUTTON_MAP) {
    if (strcmp(entityId, b.entityId) == 0) {
      enqueueCommand(b.jsonPayload);
      return;
    }
  }
  Debug.print(DBG_WARNING, "[MQTT] Unknown set topic entity: %s", entityId);
}

static const char* disconnectReasonName(int r) {
  switch (r) {
    case 0: return "USER_OK";
    case 1: return "UNACCEPTABLE_PROTOCOL_VERSION";
    case 2: return "IDENTIFIER_REJECTED";
    case 3: return "SERVER_UNAVAILABLE";
    case 4: return "MALFORMED_CREDENTIALS";
    case 5: return "NOT_AUTHORIZED";
    case 6: return "TLS_BAD_FINGERPRINT";
    case 7: return "TCP_DISCONNECTED";
    default: return "UNKNOWN";
  }
}

static void onMqttConnect(bool sessionPresent)
{
  Debug.print(DBG_INFO, "[MQTT] Connected, session: %d", sessionPresent);
  LogBuffer::append(LogBuffer::L_INFO, "[MQTT] connected (sessionPresent=%d) — publishing HA discovery",
                    (int)sessionPresent);
  HaDiscovery::publishAll();
  sweepLegacyRetainedTopics();
  Drivers::resubscribeStateTopics();
  MQTTConnection = true;
  publishActivePreset();
}

static void onMqttDisconnect(espMqttClientTypes::DisconnectReason reason)
{
  int code = static_cast<int>(reason);
  Debug.print(DBG_WARNING, "[MQTT] Disconnected, reason: %d", code);
  LogBuffer::append(LogBuffer::L_WARN, "[MQTT] disconnected: code %d (%s)", code, disconnectReasonName(code));
  if (WiFi.isConnected()) startMqttReconnectTimer();
  MQTTConnection = false;
  MqttLastDisconnectReason = code;
}

static void onMqttMessage(const espMqttClientTypes::MessageProperties& /*props*/,
                          const char* topic,
                          const uint8_t* payload,
                          size_t len, size_t /*index*/, size_t /*total*/)
{
  char payloadStr[32];
  size_t n = len < sizeof(payloadStr) - 1 ? len : sizeof(payloadStr) - 1;
  memcpy(payloadStr, payload, n);
  payloadStr[n] = '\0';

  // SP4 — if an MQTT driver owns this topic (state feedback for a Shelly or
  // similar), route it and return before the HA set-topic handler runs.
  if (Drivers::tryRouteIncoming(topic, String(payloadStr))) return;

  // HA set topic path
  char entityId[48];
  if (parseSetTopic(topic, entityId, sizeof(entityId))) {
    Debug.print(DBG_INFO, "[MQTT] HA set: entity=%s payload=%s", entityId, payloadStr);
    dispatchHaSet(entityId, payloadStr);
    return;
  }

  Debug.print(DBG_WARNING, "[MQTT] Unexpected topic: %s", topic);
}
