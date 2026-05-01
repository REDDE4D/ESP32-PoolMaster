#include "WebSocketHub.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_system.h>
#include "Config.h"

// Defined in Setup.cpp — boot-count + previous-session uptime used by
// buildStateJson() so the Diagnostics screen can surface reboot cadence.
extern uint32_t g_boot_count;
extern uint32_t g_prev_uptime_s;
#include "PoolMaster.h"
#include "Credentials.h"
#include "MqttBridge.h"          // enqueueCommand()
#include "HistoryBuffer.h"
#include "LogBuffer.h"
#include "Arduino_DebugUtils.h"
#include "Drivers.h"
#include "OutputDriver.h"
#include "Presets.h"

namespace WebSocketHub {

static AsyncWebSocket ws("/ws");

// Human-readable label for esp_reset_reason_t so the Diagnostics screen
// can tell the user *why* the device last rebooted (WDT, PANIC, SW,
// BROWNOUT, etc) without making them guess from the crash state.
static const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external reset";
    case ESP_RST_SW:        return "software reset (ESP.restart)";
    case ESP_RST_PANIC:     return "panic (crash)";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog timeout";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    case ESP_RST_BROWNOUT:  return "brownout (undervoltage)";
    case ESP_RST_SDIO:      return "SDIO reset";
    case ESP_RST_UNKNOWN:
    default:                return "unknown";
  }
}

// Per-client subscription flags (bitmask) — one byte per client, index by
// AsyncWebSocketClient::id() % MAX_SLOTS.
static constexpr size_t MAX_SLOTS = 8;
static uint8_t  g_subs[MAX_SLOTS] = {0};
static uint32_t g_ids[MAX_SLOTS] = {0};

static constexpr uint8_t SUB_STATE    = 1 << 0;
static constexpr uint8_t SUB_LOGS     = 1 << 1;
static constexpr uint8_t SUB_HISTORY  = 1 << 2;

static uint32_t g_last_state_broadcast = 0;
static String   g_last_state_json;     // for change detection

static int findSlot(uint32_t id) {
  for (size_t i = 0; i < MAX_SLOTS; ++i) if (g_ids[i] == id) return (int)i;
  return -1;
}

static int allocSlot(uint32_t id) {
  for (size_t i = 0; i < MAX_SLOTS; ++i) {
    if (g_ids[i] == 0) { g_ids[i] = id; g_subs[i] = 0; return (int)i; }
  }
  return -1;
}

static void freeSlot(uint32_t id) {
  int s = findSlot(id);
  if (s >= 0) { g_ids[s] = 0; g_subs[s] = 0; }
}

static void setSubsFromArray(int slot, JsonArrayConst arr) {
  uint8_t flags = 0;
  for (JsonVariantConst v : arr) {
    const char* t = v.as<const char*>();
    if (!t) continue;
    if (strcmp(t, "state")   == 0) flags |= SUB_STATE;
    if (strcmp(t, "logs")    == 0) flags |= SUB_LOGS;
    if (strcmp(t, "history") == 0) flags |= SUB_HISTORY;
  }
  g_subs[slot] = flags;
}

// Build the "state" JSON — shape matches /api/state one-to-one.
static String buildStateJson() {
  JsonDocument d;
  d["type"] = "state";
  JsonObject data = d["data"].to<JsonObject>();

  JsonObject m = data["measurements"].to<JsonObject>();
  m["ph"] = storage.PhValue;
  m["orp"] = storage.OrpValue;
  m["water_temp"] = storage.TempValue;
  m["air_temp"] = storage.TempExternal;
  m["pressure"]     = storage.PSIValue;                      // bar (sensor native unit)
  m["pressure_psi"] = storage.PSIValue * 14.5037738f;        // derived for convenience

  JsonObject sp = data["setpoints"].to<JsonObject>();
  sp["ph"] = storage.Ph_SetPoint;
  sp["orp"] = storage.Orp_SetPoint;
  sp["water_temp"] = storage.WaterTemp_SetPoint;

  JsonObject pumps = data["pumps"].to<JsonObject>();
  pumps["filtration"]    = FiltrationPump.IsRunning();
  pumps["ph"]            = PhPump.IsRunning();
  pumps["chl"]           = ChlPump.IsRunning();
  pumps["robot"]         = RobotPump.IsRunning();
  pumps["filt_uptime_s"] = FiltrationPump.UpTime / 1000;
  pumps["ph_uptime_s"]   = PhPump.UpTime / 1000;
  pumps["chl_uptime_s"]  = ChlPump.UpTime / 1000;

  JsonObject tanks = data["tanks"].to<JsonObject>();
  tanks["acid_fill_pct"] = PhPump.GetTankFill();
  tanks["chl_fill_pct"]  = ChlPump.GetTankFill();

  // Relays are GPIO-driven, active-low. !digitalRead → true when energised.
  JsonObject relays = data["relays"].to<JsonObject>();
  OutputDriver* dR0 = Drivers::get("r0");
  OutputDriver* dR1 = Drivers::get("r1");
  relays["r0"] = dR0 ? dR0->get() : false;
  relays["r1"] = dR1 ? dR1->get() : false;

  JsonObject modes = data["modes"].to<JsonObject>();
  modes["auto"]       = storage.AutoMode;
  modes["winter"]     = storage.WinterMode;
  modes["ph_pid"]     = (PhPID.GetMode() == AUTOMATIC);
  modes["orp_pid"]    = (OrpPID.GetMode() == AUTOMATIC);
  modes["antifreeze"] = AntiFreezeFiltering;

  JsonObject al = data["alarms"].to<JsonObject>();
  al["pressure"]          = PSIError;
  al["ph_pump_overtime"]  = PhPump.UpTimeError;
  al["chl_pump_overtime"] = ChlPump.UpTimeError;
  al["acid_tank_low"]     = !PhPump.TankLevel();
  al["chl_tank_low"]      = !ChlPump.TankLevel();

  JsonObject di = data["diagnostics"].to<JsonObject>();
  di["firmware"]  = FIRMW;
  di["uptime_s"]  = (uint32_t)(millis() / 1000);
  di["free_heap"] = ESP.getFreeHeap();
  di["wifi_rssi"] = WiFi.RSSI();
  di["ssid"]      = WiFi.SSID();
  di["ip"]        = WiFi.localIP().toString();
  di["reset_reason"]      = (int)esp_reset_reason();
  di["reset_reason_text"] = resetReasonName(esp_reset_reason());
  di["boot_count"]     = ::g_boot_count;       // defined at global scope in Setup.cpp
  di["prev_uptime_s"]  = ::g_prev_uptime_s;

  // MQTT broker connection state — used by the Diagnostics screen tile so
  // the user can tell at a glance whether HA autodiscovery has any chance
  // of having run (publishAll fires only on successful onMqttConnect).
  // host/port were here too but each call opened the NVS "mqtt" namespace,
  // which under WS reconnection storms (browser tab thrash) blew up to
  // ~110 NVS opens/sec and starved the device. The frontend already
  // fetches host/port from /api/mqtt/config, so they don't need to be in
  // the per-tick payload.
  JsonObject mq = data["mqtt"].to<JsonObject>();
  mq["connected"]            = MQTTConnection;
  mq["last_disconnect_code"] = MqttLastDisconnectReason;

  // SP5 — broadcast the state of each enabled custom switch. Disabled slots
  // are omitted so the client can treat missing slot IDs as "not configured".
  {
    JsonArray arr = data["customOutputs"].to<JsonArray>();
    for (uint8_t i = 0; i < Drivers::customSlotCount(); ++i) {
      if (!Drivers::isCustomEnabled(i)) continue;
      char slot[12]; snprintf(slot, sizeof(slot), "custom_%u", (unsigned) i);
      OutputDriver* drv = Drivers::get(slot);
      JsonObject o = arr.add<JsonObject>();
      o["slot"] = i;
      o["name"] = Drivers::customDisplayName(i);
      o["on"]   = (drv && drv->get());
    }
  }

  {
    JsonObject sched = data["schedule"].to<JsonObject>();
    sched["active_slot"] = Presets::activeSlot();
    JsonArray arr = sched["presets"].to<JsonArray>();
    for (uint8_t i = 0; i < Presets::MAX_PRESETS; ++i) {
      const Presets::PresetData& p = Presets::slot(i);
      JsonObject o = arr.add<JsonObject>();
      o["slot"] = i;
      o["name"] = p.name;
      o["type"] = (p.type == Presets::Type::AutoTemp) ? "auto_temp" : "manual";
      JsonArray winArr = o["windows"].to<JsonArray>();
      for (uint8_t w = 0; w < Presets::WINDOWS_PER; ++w) {
        JsonObject wo = winArr.add<JsonObject>();
        wo["start"]   = p.windows[w].start_min;
        wo["end"]     = p.windows[w].end_min;
        wo["enabled"] = p.windows[w].enabled;
      }
      if (p.type == Presets::Type::AutoTemp) {
        JsonObject ab = o["auto"].to<JsonObject>();
        ab["startMinHour"] = p.startMinHour;
        ab["stopMaxHour"]  = p.stopMaxHour;
        ab["centerHour"]   = p.centerHour;
      } else {
        o["auto"] = nullptr;
      }
    }
  }

  String out;
  serializeJson(d, out);
  return out;
}

// Skip the dump if AsyncTCP is already backlogged. During a tab-resume
// reconnect storm, a saturated send queue is what fragments the heap and
// eventually panics the device — better to drop a snapshot than to push
// frames into a queue we know won't drain.
static constexpr uint16_t INITIAL_DUMP_QUEUE_CEILING = 8;

static void sendHistoryInitial(AsyncWebSocketClient* client) {
  float buf[HistoryBuffer::CAPACITY];
  uint64_t t0 = 0;
  for (uint8_t s = 0; s < HistoryBuffer::SERIES_COUNT; ++s) {
    if (client->queueLen() > INITIAL_DUMP_QUEUE_CEILING) return;
    uint16_t n = HistoryBuffer::snapshot((HistoryBuffer::Series)s, buf, &t0);
    if (n == 0) continue;
    JsonDocument d;
    d["type"]   = "history";
    d["series"] = HistoryBuffer::seriesName((HistoryBuffer::Series)s);
    d["t0"]     = t0;
    d["step_s"] = HistoryBuffer::STEP_MS / 1000;
    JsonArray arr = d["values"].to<JsonArray>();
    for (uint16_t i = 0; i < n; ++i) arr.add(isnan(buf[i]) ? 0.0f : buf[i]);
    String out;
    serializeJson(d, out);
    client->text(out);
  }
}

// Single-frame batched delivery. The previous version emitted one WS frame
// per log entry (up to 128 frames), which blew through the AsyncTCP send
// queue under reconnect-storm conditions.
static void sendLogsInitial(AsyncWebSocketClient* client) {
  if (client->queueLen() > INITIAL_DUMP_QUEUE_CEILING) return;
  static LogBuffer::Entry entries[128];
  uint16_t n = LogBuffer::snapshot(entries, 128);
  if (n == 0) return;
  JsonDocument d;
  d["type"] = "logs";
  JsonArray arr = d["entries"].to<JsonArray>();
  for (uint16_t i = 0; i < n; ++i) {
    JsonObject o = arr.add<JsonObject>();
    o["ts"]    = entries[i].ts_ms;
    o["level"] = LogBuffer::levelStr(entries[i].level);
    o["msg"]   = entries[i].msg;
  }
  String out;
  serializeJson(d, out);
  client->text(out);
}

static void sendWelcome(AsyncWebSocketClient* client) {
  JsonDocument d;
  d["type"]        = "welcome";
  d["device"]      = Credentials::deviceId();
  d["firmware"]    = FIRMW;
  d["server_time"] = (uint32_t)(millis() / 1000);
  String out;
  serializeJson(d, out);
  client->text(out);
}

static void handleCmd(AsyncWebSocketClient* client, JsonDocument& msg) {
  const char* id      = msg["id"]      | "";
  const char* payload = msg["payload"] | "";

  JsonDocument resp;
  resp["type"] = "ack";
  resp["id"]   = id;
  if (!payload || !*payload) {
    resp["ok"]    = false;
    resp["error"] = "empty payload";
  } else {
    bool ok = enqueueCommand(payload);
    resp["ok"]     = ok;
    resp["queued"] = ok;
    if (!ok) resp["error"] = "queue full";
  }
  String out;
  serializeJson(resp, out);
  client->text(out);
}

static void onClientEvent(AsyncWebSocket* srv, AsyncWebSocketClient* client,
                          AwsEventType type, void* arg,
                          uint8_t* data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT: {
      allocSlot(client->id());
      Debug.print(DBG_INFO, "[WS] client %u connected from %s",
                  client->id(), client->remoteIP().toString().c_str());
      LogBuffer::append(LogBuffer::L_INFO, "[WS] client %u connected", client->id());
      // Welcome only — the state seed (and any logs/history backlog) is
      // deferred to the hello handler so we don't blast a heavy payload
      // into AsyncTCP for a connection that may already be flapping.
      sendWelcome(client);
      break;
    }
    case WS_EVT_DISCONNECT: {
      freeSlot(client->id());
      Debug.print(DBG_INFO, "[WS] client %u disconnected", client->id());
      LogBuffer::append(LogBuffer::L_INFO, "[WS] client %u disconnected", client->id());
      break;
    }
    case WS_EVT_DATA: {
      AwsFrameInfo* info = (AwsFrameInfo*) arg;
      if (!info->final || info->index != 0 || info->len != len) return; // multi-frame unsupported
      if (info->opcode != WS_TEXT) return;

      JsonDocument msg;
      if (deserializeJson(msg, data, len)) {
        Debug.print(DBG_WARNING, "[WS] bad JSON from client %u", client->id());
        return;
      }
      const char* type = msg["type"] | "";
      int slot = findSlot(client->id());

      if (strcmp(type, "hello") == 0 && slot >= 0) {
        JsonArrayConst subs = msg["subs"].as<JsonArrayConst>();
        setSubsFromArray(slot, subs);
        if (g_subs[slot] & SUB_STATE)   client->text(buildStateJson());
        if (g_subs[slot] & SUB_LOGS)    sendLogsInitial(client);
        if (g_subs[slot] & SUB_HISTORY) sendHistoryInitial(client);
      }
      else if (strcmp(type, "subscribe") == 0 && slot >= 0) {
        JsonArrayConst arr = msg["topics"].as<JsonArrayConst>();
        uint8_t before = g_subs[slot];
        for (JsonVariantConst v : arr) {
          const char* t = v.as<const char*>();
          if (!t) continue;
          if (strcmp(t, "state")   == 0) g_subs[slot] |= SUB_STATE;
          if (strcmp(t, "logs")    == 0) g_subs[slot] |= SUB_LOGS;
          if (strcmp(t, "history") == 0) g_subs[slot] |= SUB_HISTORY;
        }
        uint8_t added = g_subs[slot] & ~before;
        if (added & SUB_LOGS)    sendLogsInitial(client);
        if (added & SUB_HISTORY) sendHistoryInitial(client);
      }
      else if (strcmp(type, "unsubscribe") == 0 && slot >= 0) {
        JsonArrayConst arr = msg["topics"].as<JsonArrayConst>();
        for (JsonVariantConst v : arr) {
          const char* t = v.as<const char*>();
          if (!t) continue;
          if (strcmp(t, "state")   == 0) g_subs[slot] &= ~SUB_STATE;
          if (strcmp(t, "logs")    == 0) g_subs[slot] &= ~SUB_LOGS;
          if (strcmp(t, "history") == 0) g_subs[slot] &= ~SUB_HISTORY;
        }
      }
      else if (strcmp(type, "cmd") == 0) {
        handleCmd(client, msg);
      }
      else if (strcmp(type, "ping") == 0) {
        client->text("{\"type\":\"pong\"}");
      }
      break;
    }
    default: break;
  }
}

void begin(AsyncWebServer& srv) {
  ws.onEvent(onClientEvent);
  srv.addHandler(&ws);
  Presets::setOnChange([]() {
    // Schedule changed — invalidate the cache so the next tick re-broadcasts.
    g_last_state_json = "";
  });
  LogBuffer::setSink([](const LogBuffer::Entry& e) {
    onLogAppended(e.ts_ms, e.level, e.msg);
  });
  Debug.print(DBG_INFO, "[WS] /ws handler registered");
}

static void broadcastToSubscribers(uint8_t requiredFlag, const String& json) {
  for (size_t i = 0; i < MAX_SLOTS; ++i) {
    if (g_ids[i] == 0) continue;
    if ((g_subs[i] & requiredFlag) == 0) continue;
    AsyncWebSocketClient* c = ws.client(g_ids[i]);
    if (!c) continue;
    if (c->queueLen() > 16) continue;  // backpressure: skip overloaded clients
    c->text(json);
  }
}

void broadcast(const char* topic, const String& json) {
  uint8_t flag = 0;
  if (strcmp(topic, "state")   == 0) flag = SUB_STATE;
  if (strcmp(topic, "logs")    == 0) flag = SUB_LOGS;
  if (strcmp(topic, "history") == 0) flag = SUB_HISTORY;
  if (!flag) return;
  broadcastToSubscribers(flag, json);
}

void broadcastStateNow() {
  String json = buildStateJson();
  if (json != g_last_state_json) {
    g_last_state_json = json;
    broadcastToSubscribers(SUB_STATE, json);
    g_last_state_broadcast = millis();
  }
}

void broadcastAlarm(const char* id, bool on, const char* msg) {
  JsonDocument d;
  d["type"] = "alarm";
  d["id"]   = id;
  d["on"]   = on;
  if (msg) d["msg"] = msg;
  String out;
  serializeJson(d, out);
  broadcastToSubscribers(SUB_STATE, out);  // alarms go to state subscribers
}

void onLogAppended(uint32_t ts, uint8_t level, const char* msg) {
  JsonDocument d;
  d["type"]  = "log";
  d["ts"]    = ts;
  d["level"] = LogBuffer::levelStr((LogBuffer::Level) level);
  d["msg"]   = msg;
  String out;
  serializeJson(d, out);
  broadcastToSubscribers(SUB_LOGS, out);
}

void tick() {
  ws.cleanupClients();
  broadcastStateNow();
  // Heartbeat state every 5 s even if unchanged (helps UIs detect stalls).
  if (millis() - g_last_state_broadcast > 5000) {
    g_last_state_json = "";  // force re-broadcast
    broadcastStateNow();
  }

  // SP6 hotfix diagnostic — heap trajectory every 30 s. If a silent reset
  // happens, the last logged heap value tells us whether memory was
  // dropping (leak) or stable. Also reports number of connected WS
  // clients to spot connection storms.
  static uint32_t s_lastHeapLog = 0;
  uint32_t now = millis();
  if (now - s_lastHeapLog >= 30000) {
    s_lastHeapLog = now;
    Debug.print(DBG_INFO,
      "[Heap] free=%u min=%u largest_block=%u ws_clients=%u",
      (unsigned) ESP.getFreeHeap(),
      (unsigned) ESP.getMinFreeHeap(),
      (unsigned) ESP.getMaxAllocHeap(),
      (unsigned) ws.count());
  }
}

} // namespace WebSocketHub
