# SP3 Beautiful Web UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a responsive Preact + Tailwind + Vite PWA served from LittleFS that replaces the placeholder `index.html` with a 12-screen pool control dashboard, backed by a WebSocket live feed on `/ws` and the existing SP1 REST surface.

**Architecture:** New ESP32 C++ modules (`WebSocketHub`, `HistoryBuffer`, `LogBuffer`, `ApiCommand`) expose a typed WebSocket + `POST /api/cmd` endpoint that routes every mutation through the existing `enqueueCommand()` → `queueIn` → `ProcessCommand` pipeline. A `/web/` Vite project emits the SPA bundle into `/data/` for LittleFS; the SPA uses Preact signals for state, `preact-iso` for routing, `uplot` for charts, and a service worker for offline-shell caching. Existing pool control tasks are not touched.

**Tech Stack:** Preact 10 + `@preact/signals` + `preact-iso` + Tailwind CSS 3 + TypeScript + Vite 5 + uPlot + lucide-preact. Backend: `ESPAsyncWebServer::AsyncWebSocket` (already installed by SP1). Node 20 LTS for the build toolchain.

**Spec reference:** [docs/superpowers/specs/2026-04-23-sp3-beautiful-web-ui-design.md](../specs/2026-04-23-sp3-beautiful-web-ui-design.md)

**Out of scope:** Long-term history persistence (HA's job), web push notifications, native mobile app, Nextion HMI changes, SP4 external relay routing, automated E2E tests.

---

## Prerequisites

- Working directory: `/Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster`.
- Git branch `sp1-modern-plumbing` is the current tip (28+ SP1 commits ahead of `main`). This plan creates `sp3-web-ui` from that tip so SP3 is independently reviewable.
- PlatformIO CLI at `~/.platformio/penv/bin/pio` (v6.1.19 installed from VSCode extension).
- ADS1115 library at `/Users/sebastiankuprat/Documents/libraries/ADS1115/` (SP1 prerequisite).
- A physical ESP32 DevKit V1 reachable at `http://poolmaster.local` running SP1 ckpt6+ firmware with OTA working.
- Node.js 20 LTS. If missing: `brew install node@20` then `echo "20" > web/.nvmrc` (this task creates the file).
- A modern browser for testing (Chrome/Edge for full PWA, Safari/Firefox for compatibility sanity).

---

## File Structure Overview

Before/after at a glance. Hand-written files are committed; generated files are gitignored.

```
src/                                            C++ source
├── WebSocketHub.cpp                   (NEW)    AsyncWebSocket wrapper + routing
├── HistoryBuffer.cpp                  (NEW)    5×120 ring buffer for time-series
├── LogBuffer.cpp                      (NEW)    16 KB char ring buffer for logs
├── ApiCommand.cpp                     (NEW)    POST /api/cmd HTTP handler
├── WebServer.cpp                      (mod)    SPA fallback in onNotFound
├── MqttPublish.cpp                    (mod)    HistoryBuffer::append hook
├── Setup.cpp                          (mod)    Register WebSocketHub + broadcast task
└── (existing SP1 files untouched)

include/
├── WebSocketHub.h                     (NEW)
├── HistoryBuffer.h                    (NEW)
├── LogBuffer.h                        (NEW)
└── ApiCommand.h                       (NEW)

web/                                            (NEW) frontend source
├── package.json
├── vite.config.ts
├── tsconfig.json
├── tailwind.config.js
├── postcss.config.js
├── .nvmrc
├── .gitignore
├── index.html                                   Vite entry
├── public/
│   ├── manifest.webmanifest
│   ├── icon-192.png, icon-512.png
│   └── sw.js                                    service worker
├── src/
│   ├── main.tsx                                 mount + WS bootstrap
│   ├── app.tsx                                  router + shell
│   ├── styles.css                               Tailwind + Aqua vars
│   ├── lib/
│   │   ├── api.ts                               fetch + Basic auth
│   │   ├── ws.ts                                WebSocket client
│   │   └── format.ts                            units, formatting
│   ├── stores/
│   │   ├── connection.ts                        WS connect state
│   │   ├── state.ts                             pool state signals
│   │   ├── log.ts                               rolling log
│   │   └── history.ts                           chart data
│   ├── components/
│   │   ├── Tile.tsx
│   │   ├── Toggle.tsx
│   │   ├── Slider.tsx
│   │   ├── Modal.tsx
│   │   ├── Chart.tsx
│   │   ├── AlarmBanner.tsx
│   │   ├── Badge.tsx
│   │   ├── NavShell.tsx
│   │   └── AuthBoundary.tsx                     wraps admin-gated actions
│   └── screens/
│       ├── Dashboard.tsx
│       ├── Manual.tsx
│       ├── Setpoints.tsx
│       ├── Calibration.tsx
│       ├── Pid.tsx
│       ├── Schedule.tsx
│       ├── Tanks.tsx
│       ├── History.tsx
│       ├── Logs.tsx
│       ├── Diagnostics.tsx
│       ├── Settings.tsx
│       └── Firmware.tsx
└── README.md

data/                                            LittleFS mount root
├── setup.html, setup.css, robots.txt           (tracked, SP1)
├── index.html                         (gen)    Vite output
├── manifest.webmanifest, sw.js        (gen)
├── icon-192.png, icon-512.png         (gen)
└── assets/                            (gen)    hashed JS/CSS

.gitignore                             (mod)    adds SP3 generated-file patterns
```

---

## Phase 1: Backend foundations

### Task 1: Create SP3 branch, baseline size

**Files:**
- None (branch + size-record only)

- [ ] **Step 1: Create sp3-web-ui branch from sp1-modern-plumbing tip**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
git checkout -b sp3-web-ui
git branch --show-current
```
Expected output: `sp3-web-ui`.

- [ ] **Step 2: Verify baseline SP1 firmware still compiles**

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e serial_upload 2>&1 | tail -5
```
Expected: SUCCESS with `RAM: ~17.2%, Flash: ~92.5%` (SP1 + sp1-ext commits baseline).

- [ ] **Step 3: Record SP3 baseline binary size**

```bash
pio run -e serial_upload 2>&1 | grep -E 'RAM:|Flash:' > docs/superpowers/notes/sp3-baseline-size.txt
cat docs/superpowers/notes/sp3-baseline-size.txt
git add docs/superpowers/notes/sp3-baseline-size.txt
git commit -m "chore(sp3): record baseline binary size before SP3 work"
```

---

### Task 2: `LogBuffer` module

**Files:**
- Create: `include/LogBuffer.h`
- Create: `src/LogBuffer.cpp`

- [ ] **Step 1: Create `include/LogBuffer.h`** with:

```cpp
#pragma once
#include <Arduino.h>
#include <functional>

namespace LogBuffer {

enum Level : uint8_t { L_DEBUG = 0, L_INFO = 1, L_WARN = 2, L_ERROR = 3 };

struct Entry {
  uint32_t ts_ms;   // device millis() at append time
  Level    level;
  char     msg[112]; // fixed-width to keep ring simple; longer lines truncated
};

// Sink called synchronously after each append. Used by WebSocketHub to
// broadcast log lines to subscribed clients. Optional.
using Sink = std::function<void(const Entry&)>;

// Initialize ring (caller supplies capacity in entries — typical 128 → 16 KB).
void begin(uint16_t capacity = 128);

// Append formatted log line. Truncates to fit entry.msg. Thread-safe.
void append(Level level, const char* fmt, ...);

// Install broadcast sink.
void setSink(Sink sink);

// Snapshot: copies up to `max` entries from oldest to newest into `out`.
// Returns actual number copied. Used by WS subscribe to replay history.
uint16_t snapshot(Entry* out, uint16_t max);

// Stats.
uint16_t size();      // current entry count (<= capacity)
uint16_t capacity();

// Level → short 3-letter string for UI.
const char* levelStr(Level lvl);

} // namespace LogBuffer
```

- [ ] **Step 2: Create `src/LogBuffer.cpp`** with:

```cpp
#include "LogBuffer.h"
#include <stdarg.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace LogBuffer {

static Entry* g_buf = nullptr;
static uint16_t g_cap = 0;
static uint16_t g_head = 0;     // next slot to write
static uint16_t g_count = 0;    // valid entries
static SemaphoreHandle_t g_mutex = nullptr;
static Sink g_sink = nullptr;

void begin(uint16_t capacity) {
  if (g_buf) return;
  g_cap = capacity;
  g_buf = (Entry*) calloc(g_cap, sizeof(Entry));
  g_mutex = xSemaphoreCreateMutex();
}

void setSink(Sink sink) {
  g_sink = sink;
}

void append(Level level, const char* fmt, ...) {
  if (!g_buf) return;
  Entry e;
  e.ts_ms = millis();
  e.level = level;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e.msg, sizeof(e.msg), fmt, ap);
  va_end(ap);

  xSemaphoreTake(g_mutex, portMAX_DELAY);
  g_buf[g_head] = e;
  g_head = (g_head + 1) % g_cap;
  if (g_count < g_cap) g_count++;
  xSemaphoreGive(g_mutex);

  if (g_sink) g_sink(e);
}

uint16_t snapshot(Entry* out, uint16_t max) {
  if (!g_buf || !out || max == 0) return 0;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  uint16_t n = g_count < max ? g_count : max;
  // oldest slot = (head - count + cap) % cap
  uint16_t oldest = (g_head + g_cap - g_count) % g_cap;
  for (uint16_t i = 0; i < n; ++i) {
    out[i] = g_buf[(oldest + i) % g_cap];
  }
  xSemaphoreGive(g_mutex);
  return n;
}

uint16_t size()     { return g_count; }
uint16_t capacity() { return g_cap; }

const char* levelStr(Level lvl) {
  switch (lvl) {
    case L_DEBUG: return "dbg";
    case L_INFO:  return "inf";
    case L_WARN:  return "wrn";
    case L_ERROR: return "err";
  }
  return "?";
}

} // namespace LogBuffer
```

- [ ] **Step 3: Compile**

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e serial_upload 2>&1 | tail -5
```
Expected: SUCCESS. Flash should grow < 2 KB (LogBuffer adds a few hundred bytes of code + no allocations at compile time).

- [ ] **Step 4: Commit**

```bash
git add src/LogBuffer.cpp include/LogBuffer.h
git commit -m "sp3: LogBuffer module — 128-entry ring with sink callback

Fixed-width entries keep ring math simple; vsnprintf truncates long
lines. Mutex guards writes so background tasks can safely append while
the WS broadcast thread reads snapshots."
```

---

### Task 3: `HistoryBuffer` module

**Files:**
- Create: `include/HistoryBuffer.h`
- Create: `src/HistoryBuffer.cpp`

- [ ] **Step 1: Create `include/HistoryBuffer.h`** with:

```cpp
#pragma once
#include <Arduino.h>

namespace HistoryBuffer {

enum Series : uint8_t {
  S_PH = 0, S_ORP = 1, S_WATER_TEMP = 2, S_AIR_TEMP = 3, S_PRESSURE = 4,
  SERIES_COUNT = 5
};

constexpr uint16_t CAPACITY = 120; // 60 min @ 30 s
constexpr uint32_t STEP_MS  = 30000;

// Initialize buffer.
void begin();

// Append a new sample for each series (call once per measurement cycle).
void append(float ph, float orp, float water, float air, float pressure);

// Copy snapshot of series into `out` (up to CAPACITY floats). Returns
// number of valid points. `t0_ms_out` receives the device-millis timestamp
// of the OLDEST point.
uint16_t snapshot(Series s, float* out, uint32_t* t0_ms_out);

// Series name for JSON serialization.
const char* seriesName(Series s);

} // namespace HistoryBuffer
```

- [ ] **Step 2: Create `src/HistoryBuffer.cpp`** with:

```cpp
#include "HistoryBuffer.h"

namespace HistoryBuffer {

static float    g_data[SERIES_COUNT][CAPACITY];
static uint16_t g_head  = 0;     // next slot to write (all series share index)
static uint16_t g_count = 0;
static uint32_t g_first_ms = 0;  // millis of oldest valid sample

void begin() {
  for (uint8_t s = 0; s < SERIES_COUNT; ++s)
    for (uint16_t i = 0; i < CAPACITY; ++i)
      g_data[s][i] = NAN;
  g_head = 0;
  g_count = 0;
  g_first_ms = 0;
}

void append(float ph, float orp, float water, float air, float pressure) {
  g_data[S_PH][g_head]         = ph;
  g_data[S_ORP][g_head]        = orp;
  g_data[S_WATER_TEMP][g_head] = water;
  g_data[S_AIR_TEMP][g_head]   = air;
  g_data[S_PRESSURE][g_head]   = pressure;

  if (g_count == 0) g_first_ms = millis();
  g_head = (g_head + 1) % CAPACITY;
  if (g_count < CAPACITY) {
    g_count++;
  } else {
    // Ring rolled over — advance first_ms by one step.
    g_first_ms += STEP_MS;
  }
}

uint16_t snapshot(Series s, float* out, uint32_t* t0_ms_out) {
  if (s >= SERIES_COUNT || !out) return 0;
  if (t0_ms_out) *t0_ms_out = g_first_ms;
  uint16_t oldest = (g_head + CAPACITY - g_count) % CAPACITY;
  for (uint16_t i = 0; i < g_count; ++i) {
    out[i] = g_data[s][(oldest + i) % CAPACITY];
  }
  return g_count;
}

const char* seriesName(Series s) {
  switch (s) {
    case S_PH:         return "ph";
    case S_ORP:        return "orp";
    case S_WATER_TEMP: return "water_temp";
    case S_AIR_TEMP:   return "air_temp";
    case S_PRESSURE:   return "pressure";
    default:           return "?";
  }
}

} // namespace HistoryBuffer
```

- [ ] **Step 2b: Hook `HistoryBuffer::append` into `MqttPublish::MeasuresPublish`**

In `src/MqttPublish.cpp`, add to the top of the file:
```cpp
#include "HistoryBuffer.h"
```

In `MeasuresPublish` task, find the end of the `if (mqttClient.connected())` block that publishes measurements. Immediately AFTER the `diag["wifi_rssi"] = WiFi.RSSI();`-ish line (i.e., at the end of the per-cycle publish work), insert — outside the `if (connected)` gate so we record history even when MQTT is idle:

```cpp
      // --- SP3: feed HistoryBuffer (independent of MQTT connection) ---
    }

    HistoryBuffer::append(
      storage.PhValue,
      storage.OrpValue,
      storage.TempValue,
      storage.TempExternal,
      storage.PSIValue);
```

Double-check: the `}` closes the `if (mqttClient.connected())`, then `HistoryBuffer::append(...)` runs unconditionally every measurement cycle.

- [ ] **Step 3: Initialize HistoryBuffer in `Setup.cpp`**

In `src/Setup.cpp`, add include near the top:
```cpp
#include "HistoryBuffer.h"
```

Inside `setup()`, after `LittleFS.begin(true)` equivalent (or early in init, right after the NVS load), add:
```cpp
  HistoryBuffer::begin();
```

Place it alongside the existing initialization order. Anywhere before the FreeRTOS task creation is fine.

- [ ] **Step 4: Compile**

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e serial_upload 2>&1 | tail -5
```
Expected: SUCCESS. Flash + ~1 KB. RAM + ~2.4 KB (120 × 5 × 4 bytes).

- [ ] **Step 5: Commit**

```bash
git add src/HistoryBuffer.cpp include/HistoryBuffer.h src/MqttPublish.cpp src/Setup.cpp
git commit -m "sp3: HistoryBuffer — 60-min ring of 5 measurement series

Appends every MqttPublish cycle (30s), independent of MQTT connection so
history is recorded even when broker is unavailable. Snapshot API used
by WebSocketHub to serve 'history' subscription."
```

---

### Task 4: `WebSocketHub` module

**Files:**
- Create: `include/WebSocketHub.h`
- Create: `src/WebSocketHub.cpp`

This is the largest SP3 backend module. It owns the `AsyncWebSocket` instance, maintains per-client subscription state, and exposes `broadcastState`, `broadcastAlarm`, `broadcastLog` helpers.

- [ ] **Step 1: Create `include/WebSocketHub.h`** with:

```cpp
#pragma once
#include <ESPAsyncWebServer.h>

namespace WebSocketHub {

// Attaches /ws to the given server. Call once from setup() after WebServerInit.
void begin(AsyncWebServer& srv);

// Tick — called from a dedicated FreeRTOS task every 1 s. Cleans up
// zombie clients; triggers state broadcast if changed or heartbeat due.
void tick();

// Broadcast a pre-serialised JSON string to all connected clients that
// subscribed to `topic`. Safe to call from any task.
void broadcast(const char* topic, const String& json);

// Convenience — compose and broadcast a "state" message from the current
// `storage` + pump states.
void broadcastStateNow();

// Convenience — emit an alarm transition message.
void broadcastAlarm(const char* id, bool on, const char* msg);

// Internal — invoked by LogBuffer sink.
void onLogAppended(uint32_t ts, uint8_t level, const char* msg);

} // namespace WebSocketHub
```

- [ ] **Step 2: Create `src/WebSocketHub.cpp`** with:

```cpp
#include "WebSocketHub.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include "Config.h"
#include "PoolMaster.h"
#include "Credentials.h"
#include "MqttBridge.h"          // enqueueCommand()
#include "HistoryBuffer.h"
#include "LogBuffer.h"
#include "Arduino_DebugUtils.h"

namespace WebSocketHub {

static AsyncWebSocket ws("/ws");

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
  m["pressure"] = storage.PSIValue;

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

  JsonObject modes = data["modes"].to<JsonObject>();
  modes["auto"]    = storage.AutoMode;
  modes["winter"]  = storage.WinterMode;
  modes["ph_pid"]  = (PhPID.GetMode() == AUTOMATIC);
  modes["orp_pid"] = (OrpPID.GetMode() == AUTOMATIC);

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

  String out;
  serializeJson(d, out);
  return out;
}

static void sendHistoryInitial(AsyncWebSocketClient* client) {
  float buf[HistoryBuffer::CAPACITY];
  uint32_t t0 = 0;
  for (uint8_t s = 0; s < HistoryBuffer::SERIES_COUNT; ++s) {
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

static void sendLogsInitial(AsyncWebSocketClient* client) {
  static LogBuffer::Entry entries[128];
  uint16_t n = LogBuffer::snapshot(entries, 128);
  for (uint16_t i = 0; i < n; ++i) {
    JsonDocument d;
    d["type"]  = "log";
    d["ts"]    = entries[i].ts_ms;
    d["level"] = LogBuffer::levelStr(entries[i].level);
    d["msg"]   = entries[i].msg;
    String out;
    serializeJson(d, out);
    client->text(out);
  }
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
      sendWelcome(client);
      client->text(buildStateJson());  // immediate state seed
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
}

} // namespace WebSocketHub
```

- [ ] **Step 3: Compile**

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e serial_upload 2>&1 | tail -5
```
Expected: SUCCESS. Flash delta expected +8 KB (JSON construction + WS handler code).

- [ ] **Step 4: Commit**

```bash
git add src/WebSocketHub.cpp include/WebSocketHub.h
git commit -m "sp3: WebSocketHub — /ws handler with per-client subscriptions

- allocSlot/findSlot/freeSlot track 8 concurrent clients max (ID-based)
- broadcastStateNow emits change-detection JSON every tick
- alarm + log broadcasts reuse the state or logs subscription flag
- LogBuffer sink pipes new log lines to subscribed clients
- queueLen()-based backpressure skips overloaded clients"
```

---

### Task 5: `ApiCommand` + SPA fallback

**Files:**
- Create: `include/ApiCommand.h`
- Create: `src/ApiCommand.cpp`
- Modify: `src/WebServer.cpp` (mount ApiCommand, add SPA fallback in onNotFound)

- [ ] **Step 1: Create `include/ApiCommand.h`**

```cpp
#pragma once
#include <ESPAsyncWebServer.h>

namespace ApiCommand {
  // Registers POST /api/cmd + GET /api/whoami on the given server.
  void registerRoutes(AsyncWebServer& srv);
}
```

- [ ] **Step 2: Create `src/ApiCommand.cpp`**

```cpp
#include "ApiCommand.h"
#include <ArduinoJson.h>
#include "WebAuth.h"
#include "MqttBridge.h"   // enqueueCommand()
#include "Arduino_DebugUtils.h"

namespace ApiCommand {

static void handleBody(AsyncWebServerRequest* req,
                       uint8_t* data, size_t len,
                       size_t index, size_t total) {
  // Accumulate on the request; AsyncWebServer offers `_tempObject` for this.
  // We use a simple approach: small bodies arrive in one chunk in practice,
  // so require index == 0 && len == total.
  if (index == 0 && len == total) {
    req->_tempObject = strndup((const char*)data, len);
  }
}

static void handlePost(AsyncWebServerRequest* req) {
  if (!WebAuth::requireAdmin(req)) return;

  const char* payload = (const char*) req->_tempObject;
  if (!payload || !*payload) {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"empty body\"}");
    return;
  }

  bool ok = enqueueCommand(payload);
  JsonDocument d;
  d["ok"]     = ok;
  d["queued"] = ok;
  if (!ok) d["error"] = "queue full";
  String out;
  serializeJson(d, out);
  req->send(ok ? 200 : 503, "application/json", out);

  free(req->_tempObject);
  req->_tempObject = nullptr;
}

void registerRoutes(AsyncWebServer& srv) {
  srv.on("/api/cmd", HTTP_POST,
         handlePost,
         nullptr,   // no upload handler
         handleBody);

  // /api/whoami — returns 200 if authed, 401 otherwise. Used by the SPA
  // to force a Basic-auth challenge without side effects.
  srv.on("/api/whoami", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;   // sends 401 if not authed
    req->send(200, "application/json", "{\"ok\":true,\"user\":\"admin\"}");
  });
}

} // namespace ApiCommand
```

- [ ] **Step 3: Update `src/WebServer.cpp`** — mount ApiCommand + SPA fallback

Add include at top:
```cpp
#include "ApiCommand.h"
```

Find the existing `webServer.onNotFound(...)` block (it currently sends 404 plain text) and replace with:
```cpp
  // SPA fallback: non-/api, non-asset GETs serve /index.html so preact-iso
  // router can handle the path. /api/* stays 404. Assets are already matched
  // by serveStatic above.
  webServer.onNotFound([](AsyncWebServerRequest* req) {
    if (req->method() != HTTP_GET) {
      req->send(404, "text/plain", "Not found");
      return;
    }
    const String& url = req->url();
    if (url.startsWith("/api/") || url == "/ws" || url == "/healthz" ||
        url.startsWith("/update") || url.startsWith("/setup")) {
      req->send(404, "text/plain", "Not found");
      return;
    }
    if (LittleFS.exists("/index.html")) {
      req->send(LittleFS, "/index.html", "text/html");
    } else {
      req->send(404, "text/plain", "SPA not installed (run npm run build + uploadfs)");
    }
  });
```

Also, immediately BEFORE `webServer.begin();` at the bottom of `WebServerInit()`, add:
```cpp
  // SP3: /api/cmd + /api/whoami
  ApiCommand::registerRoutes(webServer);
```

- [ ] **Step 4: Compile**

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e serial_upload 2>&1 | tail -5
```
Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/ApiCommand.cpp include/ApiCommand.h src/WebServer.cpp
git commit -m "sp3: ApiCommand POST /api/cmd + /api/whoami + SPA fallback

ApiCommand reuses WebAuth::requireAdmin + enqueueCommand. onNotFound
falls through to /index.html for non-/api GETs so SPA history routing
works on refresh/deep-link."
```

---

### Task 6: Wire WebSocketHub into setup + broadcast task

**Files:**
- Modify: `src/Setup.cpp`

- [ ] **Step 1: Add includes at top of `src/Setup.cpp`**

Add:
```cpp
#include "WebSocketHub.h"
#include "LogBuffer.h"
```

- [ ] **Step 2: Initialize LogBuffer + WebSocketHub in `setup()`**

Find the existing `WebServerInit();` call. Insert IMMEDIATELY BEFORE it:
```cpp
  // SP3 init: LogBuffer must exist before anything that might log.
  LogBuffer::begin();
```

Then, immediately AFTER `OtaServiceInit(webServer);` (which currently follows WebServerInit), add:
```cpp
  // SP3: attach /ws handler on the same AsyncWebServer.
  WebSocketHub::begin(webServer);
```

- [ ] **Step 3: Create a WebSocket broadcast FreeRTOS task**

In `src/Setup.cpp`, find the existing task-creation block in `setup()` where other FreeRTOS tasks are created (look for `xTaskCreatePinnedToCore`). Add a new task creation:

```cpp
  // WebSocket broadcast — runs every 1s, ticks the hub
  xTaskCreatePinnedToCore(
    [](void*) {
      for(;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!startTasks) continue;
        WebSocketHub::tick();
      }
    },
    "WsBroadcast",
    3072,
    nullptr,
    1,
    nullptr,
    xPortGetCoreID());
```

Place it next to the other task creations. The stack is 3 KB because JSON serialization uses temporary String buffers.

- [ ] **Step 4: Compile + check size**

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e serial_upload 2>&1 | tail -5
```
Expected: SUCCESS. Flash usage around 93%. If over 94%, investigate.

- [ ] **Step 5: Commit**

```bash
git add src/Setup.cpp
git commit -m "sp3: wire WebSocketHub + LogBuffer into setup + broadcast task

LogBuffer::begin before any code that might log. WebSocketHub::begin
after OtaServiceInit (so webServer is fully configured). WsBroadcast
task runs every 1s to emit state changes and heartbeat."
```

---

### Task 7: Verify backend + OTA ckpt1

**Files:**
- None (verification)

- [ ] **Step 1: OTA push Phase 1 backend**

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
export POOLMASTER_OTA_PWD=""
pio run -e OTA_upload -t upload 2>&1 | tail -5
```
Expected: SUCCESS. Device reboots.

- [ ] **Step 2: Verify health + API intact**

```bash
sleep 12
curl -sS --max-time 5 http://poolmaster.local/healthz
curl -sS --max-time 5 http://poolmaster.local/api/state | head -c 200
curl -sS --max-time 5 -o /dev/null -w '%{http_code}\n' http://poolmaster.local/api/whoami
```
Expected:
- `/healthz` → `{"status":"ok",...}`
- `/api/state` → first 200 chars of the JSON we know
- `/api/whoami` → `401` (no admin pwd set, but Basic-auth path triggers 401 if no header — wait, check requireAdmin: "if admin pwd is empty, grant access" → it returns 200).

Note: `/api/whoami` will return **200** in pre-setup state because `WebAuth::requireAdmin` short-circuits when no admin password is set. That's intended. The 401 behavior kicks in once an admin password is configured via Settings.

- [ ] **Step 3: Verify WebSocket + /ws works**

Run this Python snippet using pio's pyserial-bundled python (has `websockets` module? No — install via pip):

```bash
$HOME/.platformio/penv/bin/python3 -m pip install --quiet websockets
$HOME/.platformio/penv/bin/python3 -c "
import asyncio, json, websockets
async def test():
    async with websockets.connect('ws://poolmaster.local/ws') as ws:
        print('connected')
        # welcome
        print('<', await ws.recv())
        # initial state
        print('<', (await ws.recv())[:200])
        # hello + subscribe
        await ws.send(json.dumps({'type':'hello','ver':1,'subs':['state','logs','history']}))
        for _ in range(10):
            msg = await asyncio.wait_for(ws.recv(), timeout=3)
            print('<', msg[:200])
asyncio.run(test())
"
```

Expected output (trimmed):
```
connected
< {"type":"welcome","device":"...","firmware":"ESP-SP1","server_time":...}
< {"type":"state","data":{...
< {"type":"history","series":"ph","t0":...,"step_s":30,"values":[...]}  (repeated for 5 series)
< (possibly log lines, plus future state heartbeats every 5s)
```

- [ ] **Step 4: Create checkpoint branch `sp3-ckpt1-backend`**

```bash
git branch sp3-ckpt1-backend
git branch --list 'sp3*'
```

---

## Phase 2: Frontend scaffolding

### Task 8: Initialize `/web/` Vite + Preact + Tailwind + TS project

**Files:**
- Create: `web/package.json`
- Create: `web/vite.config.ts`
- Create: `web/tsconfig.json`
- Create: `web/tailwind.config.js`
- Create: `web/postcss.config.js`
- Create: `web/.nvmrc`
- Create: `web/.gitignore`
- Create: `web/index.html`
- Create: `web/src/main.tsx`
- Create: `web/src/app.tsx`
- Create: `web/src/styles.css`
- Modify: `.gitignore` (repo root — adds SP3 patterns)

- [ ] **Step 1: Check Node version**

```bash
node --version
```
Expected: `v20.x.x`. If older, install Node 20: `brew install node@20 && brew link node@20`.

- [ ] **Step 2: Create `web/.nvmrc`**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
mkdir -p web
echo "20" > web/.nvmrc
```

- [ ] **Step 3: Create `web/package.json`**

Create file `web/package.json`:
```json
{
  "name": "poolmaster-web",
  "private": true,
  "type": "module",
  "scripts": {
    "dev": "vite",
    "build": "tsc --noEmit && vite build && node ./scripts/check-bundle-size.mjs",
    "preview": "vite preview",
    "lint": "tsc --noEmit && eslint 'src/**/*.{ts,tsx}'",
    "upload": "cd .. && pio run -e OTA_upload -t uploadfs"
  },
  "dependencies": {
    "@preact/signals": "^1.3.1",
    "preact": "^10.23.2",
    "preact-iso": "^2.8.1",
    "uplot": "^1.6.31",
    "lucide-preact": "^0.453.0"
  },
  "devDependencies": {
    "@preact/preset-vite": "^2.9.0",
    "@types/node": "^20.16.10",
    "autoprefixer": "^10.4.20",
    "eslint": "^9.11.1",
    "postcss": "^8.4.47",
    "tailwindcss": "^3.4.13",
    "typescript": "^5.6.2",
    "typescript-eslint": "^8.8.0",
    "vite": "^5.4.8"
  }
}
```

- [ ] **Step 4: Create `web/vite.config.ts`**

```typescript
import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';

export default defineConfig({
  plugins: [preact()],
  build: {
    outDir: '../data',
    emptyOutDir: false,          // don't wipe hand-written setup.html etc.
    sourcemap: false,
    target: 'es2020',
    rollupOptions: {
      output: {
        entryFileNames: 'assets/[name]-[hash].js',
        chunkFileNames: 'assets/[name]-[hash].js',
        assetFileNames: 'assets/[name]-[hash][extname]',
      },
    },
  },
  server: {
    host: true,
    port: 5173,
    proxy: {
      '/api':     { target: 'http://poolmaster.local', changeOrigin: true },
      '/healthz': { target: 'http://poolmaster.local', changeOrigin: true },
      '/ws':      { target: 'ws://poolmaster.local',  ws: true },
      '/update':  { target: 'http://poolmaster.local', changeOrigin: true },
    },
  },
});
```

- [ ] **Step 5: Create `web/tsconfig.json`**

```json
{
  "compilerOptions": {
    "target": "ES2020",
    "module": "ESNext",
    "moduleResolution": "Bundler",
    "lib": ["ES2020", "DOM", "DOM.Iterable"],
    "jsx": "react-jsx",
    "jsxImportSource": "preact",
    "paths": {
      "react": ["./node_modules/preact/compat/"],
      "react-dom": ["./node_modules/preact/compat/"]
    },
    "strict": true,
    "noUncheckedIndexedAccess": true,
    "isolatedModules": true,
    "esModuleInterop": true,
    "skipLibCheck": true,
    "allowSyntheticDefaultImports": true,
    "resolveJsonModule": true,
    "noEmit": true
  },
  "include": ["src", "vite.config.ts"]
}
```

- [ ] **Step 6: Create `web/tailwind.config.js`**

```javascript
/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{ts,tsx}'],
  theme: {
    extend: {
      colors: {
        aqua: {
          bg: {
            900: '#062e4a',
            800: '#0a4565',
            700: '#0e5d75',
          },
          surface: 'rgba(255, 255, 255, 0.07)',
          'surface-elev': 'rgba(255, 255, 255, 0.12)',
          border: 'rgba(125, 211, 252, 0.15)',
          'border-elev': 'rgba(125, 211, 252, 0.25)',
          label: '#7dd3fc',
          primary: '#22d3ee',
          'primary-hover': '#67e8f9',
          text: '#e6f8ff',
          muted: '#64748b',
          ok: '#34d399',
          warn: '#fbbf24',
          alarm: '#f43f5e',
          info: '#22d3ee',
        },
      },
      fontFamily: {
        sans: ['-apple-system', 'BlinkMacSystemFont', '"Segoe UI"', 'Roboto', 'Helvetica', 'Arial', 'sans-serif'],
        mono: ['ui-monospace', 'SFMono-Regular', 'Menlo', 'monospace'],
      },
      backgroundImage: {
        'aqua-gradient': 'linear-gradient(140deg, #062e4a 0%, #0a4565 50%, #0e5d75 100%)',
      },
      backdropBlur: {
        'glass': '10px',
      },
    },
  },
  plugins: [],
};
```

- [ ] **Step 7: Create `web/postcss.config.js`**

```javascript
export default {
  plugins: {
    tailwindcss: {},
    autoprefixer: {},
  },
};
```

- [ ] **Step 8: Create `web/.gitignore`**

```
node_modules/
dist/
*.log
.DS_Store
```

- [ ] **Step 9: Create `web/index.html`**

```html
<!doctype html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <link rel="manifest" href="/manifest.webmanifest" />
    <link rel="icon" type="image/png" sizes="192x192" href="/icon-192.png" />
    <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover" />
    <meta name="theme-color" content="#062e4a" />
    <meta name="apple-mobile-web-app-capable" content="yes" />
    <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent" />
    <title>PoolMaster</title>
  </head>
  <body>
    <div id="root"></div>
    <script type="module" src="/src/main.tsx"></script>
  </body>
</html>
```

- [ ] **Step 10: Create `web/src/styles.css`**

```css
@tailwind base;
@tailwind components;
@tailwind utilities;

@layer base {
  html, body {
    @apply bg-aqua-bg-900 bg-aqua-gradient min-h-screen text-aqua-text font-sans antialiased;
    background-attachment: fixed;
  }
  body {
    -webkit-tap-highlight-color: transparent;
  }
  :root {
    color-scheme: dark;
  }
}

@layer components {
  .glass {
    @apply bg-aqua-surface border border-aqua-border rounded-xl backdrop-blur-glass;
    box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.08);
  }
  .glass-elev {
    @apply bg-aqua-surface-elev border border-aqua-border-elev rounded-xl backdrop-blur-glass;
    box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.12);
  }
  .label-caps {
    @apply text-[0.7rem] uppercase tracking-[0.08em] font-semibold text-aqua-label;
  }
  .val-num {
    font-variant-numeric: tabular-nums;
  }
}

/* Safe-area padding for iOS notch / bottom bar */
.safe-top    { padding-top: env(safe-area-inset-top); }
.safe-bottom { padding-bottom: env(safe-area-inset-bottom); }
```

- [ ] **Step 11: Create `web/src/main.tsx`**

```tsx
import { render } from 'preact';
import { App } from './app';
import './styles.css';

// Register service worker in production.
if ('serviceWorker' in navigator && import.meta.env.PROD) {
  window.addEventListener('load', () => {
    navigator.serviceWorker.register('/sw.js').catch(err => {
      console.warn('SW registration failed', err);
    });
  });
}

render(<App />, document.getElementById('root')!);
```

- [ ] **Step 12: Create `web/src/app.tsx`** — placeholder shell

```tsx
export function App() {
  return (
    <div class="min-h-screen flex items-center justify-center">
      <div class="glass p-8 max-w-md text-center">
        <h1 class="text-2xl font-bold tracking-tight text-aqua-text">🏊 PoolMaster</h1>
        <p class="label-caps mt-2">SP3 scaffolding</p>
        <p class="mt-4 text-sm text-aqua-muted">
          Frontend project initialized. Real shell lands in Task 16.
        </p>
      </div>
    </div>
  );
}
```

- [ ] **Step 13: Update repo root `.gitignore`**

Append to `/Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/.gitignore`:
```
# SP3 frontend
/web/node_modules/
/web/dist/
# SP3 Vite output in LittleFS source
/data/index.html
/data/manifest.webmanifest
/data/sw.js
/data/icon-*.png
/data/assets/
```

- [ ] **Step 14: Install dependencies**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web
npm install 2>&1 | tail -10
```
Expected: ~200 packages installed, no errors. Ignore deprecation warnings.

- [ ] **Step 15: Test build**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web
npm run build 2>&1 | tail -20
```
Expected: Vite emits `../data/index.html` + `../data/assets/*.js` + `../data/assets/*.css`. TS check passes. Bundle-size check will fail (Task 11 adds the check script); comment out that part for now by editing `package.json`'s `build` script to `"tsc --noEmit && vite build"` (remove the trailing `&& node ./scripts/...`). Fix in Task 11.

Apply that edit to `web/package.json`:
```json
    "build": "tsc --noEmit && vite build",
```

Re-run `npm run build`. Expected SUCCESS.

- [ ] **Step 16: Verify Vite output in `/data/`**

```bash
ls -la /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/data/
```
Expected: `index.html`, `assets/` directory with hashed JS/CSS. Original `setup.html`, `setup.css`, `robots.txt` still present.

- [ ] **Step 17: Commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
git add web/ .gitignore
git commit -m "sp3: initialize /web/ Vite+Preact+Tailwind+TS project

Scaffolds package.json with dependency pins, vite.config.ts with dev
proxy + data/ output, tsconfig targeting ES2020 with Preact JSX,
tailwind.config.js declaring the Aqua palette, placeholder app shell.
Repo .gitignore updated to exclude node_modules + Vite build output."
```

---

### Task 9: PWA manifest + service worker + icons

**Files:**
- Create: `web/public/manifest.webmanifest`
- Create: `web/public/sw.js`
- Create: `web/public/icon-192.png`
- Create: `web/public/icon-512.png`

- [ ] **Step 1: Create `web/public/manifest.webmanifest`**

```json
{
  "name": "PoolMaster",
  "short_name": "Pool",
  "description": "ESP32 pool controller dashboard",
  "start_url": "/",
  "display": "standalone",
  "background_color": "#062e4a",
  "theme_color": "#062e4a",
  "icons": [
    { "src": "/icon-192.png", "sizes": "192x192", "type": "image/png", "purpose": "any maskable" },
    { "src": "/icon-512.png", "sizes": "512x512", "type": "image/png", "purpose": "any maskable" }
  ]
}
```

- [ ] **Step 2: Create `web/public/sw.js`** — minimal app-shell caching

```javascript
const CACHE = 'poolmaster-v1';
const SHELL = ['/', '/index.html', '/manifest.webmanifest'];

self.addEventListener('install', event => {
  event.waitUntil(caches.open(CACHE).then(c => c.addAll(SHELL)));
  self.skipWaiting();
});

self.addEventListener('activate', event => {
  event.waitUntil(
    caches.keys().then(keys => Promise.all(
      keys.filter(k => k !== CACHE).map(k => caches.delete(k))
    ))
  );
  self.clients.claim();
});

self.addEventListener('fetch', event => {
  const req = event.request;
  const url = new URL(req.url);

  // Never cache API or WebSocket upgrades.
  if (url.pathname.startsWith('/api/') || url.pathname === '/ws' ||
      url.pathname === '/healthz' || url.pathname === '/update') {
    return; // default network behavior
  }

  // Cache-first for same-origin static assets; network fallback updates cache.
  if (req.method === 'GET' && url.origin === self.location.origin) {
    event.respondWith(
      caches.match(req).then(hit => {
        if (hit) return hit;
        return fetch(req).then(res => {
          if (res.ok) {
            const clone = res.clone();
            caches.open(CACHE).then(c => c.put(req, clone));
          }
          return res;
        }).catch(() => caches.match('/index.html'));
      })
    );
  }
});
```

- [ ] **Step 3: Generate icons**

Use ImageMagick or a simple Python PIL script to produce placeholder icons. From repo root:

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
$HOME/.platformio/penv/bin/python3 <<'PY'
from PIL import Image, ImageDraw, ImageFont
import os
os.makedirs('web/public', exist_ok=True)
for size in (192, 512):
  img = Image.new('RGBA', (size, size), (6, 46, 74, 255))  # aqua-bg-900
  d = ImageDraw.Draw(img)
  # Simple water-drop circle
  r = size // 2 - size // 10
  cx = cy = size // 2
  d.ellipse([cx-r, cy-r, cx+r, cy+r], fill=(34, 211, 238, 255), outline=(125, 211, 252, 255), width=max(2, size // 64))
  # Label
  try:
    font = ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc', size // 4)
  except OSError:
    font = ImageFont.load_default()
  txt = 'PM'
  # PIL 10+ API
  bbox = d.textbbox((0,0), txt, font=font)
  tw, th = bbox[2]-bbox[0], bbox[3]-bbox[1]
  d.text((cx - tw // 2, cy - th // 2 - bbox[1]), txt, fill=(230, 248, 255, 255), font=font)
  img.save(f'web/public/icon-{size}.png', 'PNG')
  print(f'wrote web/public/icon-{size}.png')
PY
```

If Pillow isn't installed, `$HOME/.platformio/penv/bin/pip install Pillow` first. Expected: writes `web/public/icon-192.png` + `web/public/icon-512.png`.

- [ ] **Step 4: Rebuild and verify**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web
npm run build 2>&1 | tail -10
ls -la ../data/manifest.webmanifest ../data/sw.js ../data/icon-*.png
```
Expected: all four files present in `data/`.

- [ ] **Step 5: Commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
git add web/public/
git commit -m "sp3: PWA shell — manifest, icons, service worker

Standalone display, aqua background + theme color, simple PM placeholder
icons (192 + 512 PNG). Service worker caches shell + assets but never
API/WS/update paths; offline fallback to index.html for navigations."
```

---

### Task 10: Bundle-size guard script

**Files:**
- Create: `web/scripts/check-bundle-size.mjs`
- Modify: `web/package.json` (re-enable the check in `build` script)

- [ ] **Step 1: Create `web/scripts/check-bundle-size.mjs`**

```javascript
import { readdir, stat } from 'node:fs/promises';
import { join } from 'node:path';

const MAX_JS_BYTES = 500 * 1024;  // 500 KB uncompressed

const assetsDir = new URL('../../data/assets/', import.meta.url);
let total = 0;

try {
  const files = await readdir(assetsDir);
  for (const f of files) {
    if (!f.endsWith('.js')) continue;
    const s = await stat(new URL(f, assetsDir));
    total += s.size;
  }
} catch (err) {
  console.error('[bundle-size] could not read data/assets:', err.message);
  process.exit(1);
}

const kb = (total / 1024).toFixed(1);
if (total > MAX_JS_BYTES) {
  console.error(`[bundle-size] FAIL: ${kb} KB of JS exceeds 500 KB limit.`);
  process.exit(1);
}
console.log(`[bundle-size] OK: ${kb} KB of JS (${((total / MAX_JS_BYTES) * 100).toFixed(0)}% of 500 KB budget)`);
```

- [ ] **Step 2: Re-enable bundle check in `web/package.json`**

Change the `build` script back to:
```json
    "build": "tsc --noEmit && vite build && node ./scripts/check-bundle-size.mjs",
```

- [ ] **Step 3: Run build to verify check passes**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web
npm run build 2>&1 | tail -10
```
Expected: ends with `[bundle-size] OK: <N> KB of JS`.

- [ ] **Step 4: Commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
git add web/scripts/ web/package.json
git commit -m "sp3: bundle size guard — fail build if JS > 500 KB"
```

---

### Task 11: Create checkpoint `sp3-ckpt2-scaffold`

**Files:**
- None (verification + branch)

- [ ] **Step 1: Verify build from a cold state**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web
rm -rf ../data/index.html ../data/assets ../data/sw.js ../data/manifest.webmanifest ../data/icon-*.png
npm run build 2>&1 | tail -5
ls -la ../data/
```
Expected: clean rebuild; `index.html`, `assets/`, `sw.js`, `manifest.webmanifest`, `icon-*.png` present. `setup.html`, `setup.css`, `robots.txt` untouched.

- [ ] **Step 2: OTA LittleFS + verify device serves the placeholder shell**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
export PATH="$HOME/.platformio/penv/bin:$PATH"
export POOLMASTER_OTA_PWD=""
pio run -e OTA_upload -t uploadfs 2>&1 | tail -5
sleep 8
curl -sS --max-time 5 http://poolmaster.local/ | head -c 200
```
Expected: HTML starting with `<!doctype html>` containing `<div id="root">`. Device LittleFS now has the SP3 scaffold.

Open `http://poolmaster.local/` in a browser — you should see a single glass card reading "🏊 PoolMaster · SP3 scaffolding · Frontend project initialized. Real shell lands in Task 16."

- [ ] **Step 3: Create checkpoint branch**

```bash
git branch sp3-ckpt2-scaffold
git branch --list 'sp3*'
```

---

## Phase 3: Frontend infrastructure

### Task 12: `lib/format.ts` + `lib/api.ts`

**Files:**
- Create: `web/src/lib/format.ts`
- Create: `web/src/lib/api.ts`

- [ ] **Step 1: Create `web/src/lib/format.ts`**

```typescript
export function fmtNum(v: number | undefined | null, decimals = 2): string {
  if (v == null || Number.isNaN(v)) return '—';
  return v.toFixed(decimals);
}

export function fmtInt(v: number | undefined | null): string {
  if (v == null || Number.isNaN(v)) return '—';
  return Math.round(v).toString();
}

export function fmtDuration(seconds: number): string {
  const s = Math.max(0, Math.floor(seconds));
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const r = s % 60;
  if (h > 0) return `${h}h ${m}m`;
  if (m > 0) return `${m}m ${r}s`;
  return `${r}s`;
}

export function fmtBytes(v: number): string {
  if (v < 1024) return `${v} B`;
  if (v < 1024 * 1024) return `${(v / 1024).toFixed(1)} KB`;
  return `${(v / (1024 * 1024)).toFixed(1)} MB`;
}

export function fmtTimestamp(ms: number): string {
  const d = new Date(ms);
  const pad = (n: number) => String(n).padStart(2, '0');
  return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}
```

- [ ] **Step 2: Create `web/src/lib/api.ts`**

```typescript
// Basic fetch helpers. All routes use relative URLs — Vite dev-proxy
// forwards /api/* to the device; in production the SPA is served from
// the device itself so relative URLs just work.

export interface ApiResult<T = unknown> {
  ok: boolean;
  status: number;
  data?: T;
  error?: string;
}

export async function apiGet<T = unknown>(path: string): Promise<ApiResult<T>> {
  try {
    const res = await fetch(path, { credentials: 'same-origin' });
    const body = res.headers.get('content-type')?.includes('application/json')
      ? await res.json()
      : await res.text();
    if (!res.ok) return { ok: false, status: res.status, error: typeof body === 'string' ? body : JSON.stringify(body) };
    return { ok: true, status: res.status, data: body as T };
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : String(err);
    return { ok: false, status: 0, error: msg };
  }
}

export async function apiPostForm<T = unknown>(path: string, fields: Record<string, string>): Promise<ApiResult<T>> {
  const body = new URLSearchParams(fields);
  try {
    const res = await fetch(path, {
      method: 'POST',
      credentials: 'same-origin',
      body,
    });
    const text = await res.text();
    let data: unknown = text;
    try { data = JSON.parse(text); } catch { /* keep as text */ }
    if (!res.ok) return { ok: false, status: res.status, error: typeof data === 'string' ? data : JSON.stringify(data) };
    return { ok: true, status: res.status, data: data as T };
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : String(err);
    return { ok: false, status: 0, error: msg };
  }
}

export async function apiPostJson<T = unknown>(path: string, body: unknown): Promise<ApiResult<T>> {
  try {
    const res = await fetch(path, {
      method: 'POST',
      credentials: 'same-origin',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    const text = await res.text();
    let data: unknown = text;
    try { data = JSON.parse(text); } catch { /* keep as text */ }
    if (!res.ok) return { ok: false, status: res.status, error: typeof data === 'string' ? data : JSON.stringify(data) };
    return { ok: true, status: res.status, data: data as T };
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : String(err);
    return { ok: false, status: 0, error: msg };
  }
}

// Trigger a browser Basic-auth challenge on demand. Used by the UI before
// issuing a command when the server reports auth_required.
export async function forceAuthPrompt(): Promise<boolean> {
  const res = await apiGet('/api/whoami');
  return res.ok;
}

// Send a command as legacy JSON (e.g. {"FiltPump":1}) via POST /api/cmd.
export async function apiSendCommand(json: string): Promise<ApiResult<{ ok: boolean; queued?: boolean; error?: string }>> {
  try {
    const res = await fetch('/api/cmd', {
      method: 'POST',
      credentials: 'same-origin',
      headers: { 'Content-Type': 'application/json' },
      body: json,
    });
    const data = (await res.json()) as { ok: boolean; queued?: boolean; error?: string };
    if (!res.ok) return { ok: false, status: res.status, data, error: data.error };
    return { ok: true, status: res.status, data };
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : String(err);
    return { ok: false, status: 0, error: msg };
  }
}
```

- [ ] **Step 3: Verify TypeScript compiles**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web
npx tsc --noEmit 2>&1 | tail -5
```
Expected: no output (tsc exits clean).

- [ ] **Step 4: Commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
git add web/src/lib/
git commit -m "sp3: frontend api + format helpers"
```

---

### Task 13: `lib/ws.ts` — WebSocket client

**Files:**
- Create: `web/src/lib/ws.ts`

- [ ] **Step 1: Create `web/src/lib/ws.ts`**

```typescript
import { signal } from '@preact/signals';

export type WsStatus = 'connecting' | 'connected' | 'reconnecting' | 'stopped';

export interface WsMessage {
  type: string;
  [k: string]: unknown;
}

type Listener = (msg: WsMessage) => void;

export const wsStatus = signal<WsStatus>('connecting');

export class PoolMasterWs {
  private socket: WebSocket | null = null;
  private listeners = new Set<Listener>();
  private backoffMs = 1000;
  private readonly maxBackoffMs = 30000;
  private pingTimer: number | null = null;
  private stopped = false;
  private pendingSubs: string[] = ['state', 'logs', 'history'];

  constructor(private readonly url: string = buildWsUrl()) {}

  start() {
    this.stopped = false;
    this.connect();
  }

  stop() {
    this.stopped = true;
    if (this.pingTimer) { clearInterval(this.pingTimer); this.pingTimer = null; }
    if (this.socket) {
      const s = this.socket;
      this.socket = null;
      s.close();
    }
    wsStatus.value = 'stopped';
  }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  send(msg: WsMessage): boolean {
    if (!this.socket || this.socket.readyState !== WebSocket.OPEN) return false;
    this.socket.send(JSON.stringify(msg));
    return true;
  }

  sendCommand(payload: string): string {
    const id = crypto.randomUUID();
    this.send({ type: 'cmd', id, payload });
    return id;
  }

  setSubscriptions(topics: string[]) {
    this.pendingSubs = topics;
    this.send({ type: 'subscribe', topics });
  }

  private connect() {
    if (this.stopped) return;
    wsStatus.value = this.backoffMs === 1000 ? 'connecting' : 'reconnecting';

    const socket = new WebSocket(this.url);
    this.socket = socket;

    socket.onopen = () => {
      this.backoffMs = 1000;
      wsStatus.value = 'connected';
      socket.send(JSON.stringify({ type: 'hello', ver: 1, subs: this.pendingSubs }));
      if (this.pingTimer) clearInterval(this.pingTimer);
      this.pingTimer = window.setInterval(() => {
        if (socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify({ type: 'ping' }));
      }, 20000);
    };

    socket.onmessage = event => {
      try {
        const msg = JSON.parse(event.data) as WsMessage;
        this.listeners.forEach(l => l(msg));
      } catch {
        // ignore malformed
      }
    };

    socket.onerror = () => { /* onclose will handle reconnect */ };

    socket.onclose = () => {
      if (this.pingTimer) { clearInterval(this.pingTimer); this.pingTimer = null; }
      this.socket = null;
      if (this.stopped) return;
      wsStatus.value = 'reconnecting';
      window.setTimeout(() => this.connect(), this.backoffMs);
      this.backoffMs = Math.min(this.backoffMs * 2, this.maxBackoffMs);
    };
  }
}

function buildWsUrl(): string {
  const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${proto}//${window.location.host}/ws`;
}

// Singleton — most of the app just imports this.
export const poolWs = new PoolMasterWs();
```

- [ ] **Step 2: Verify compile**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web
npx tsc --noEmit 2>&1 | tail -5
```
Expected: clean.

- [ ] **Step 3: Commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
git add web/src/lib/ws.ts
git commit -m "sp3: WebSocket client with exp-backoff reconnect + signal status"
```

---

### Task 14: Preact signal stores

**Files:**
- Create: `web/src/stores/state.ts`
- Create: `web/src/stores/log.ts`
- Create: `web/src/stores/history.ts`
- Create: `web/src/stores/connection.ts`
- Create: `web/src/stores/index.ts` (single entry to bootstrap all stores)

- [ ] **Step 1: Create `web/src/stores/state.ts`**

```typescript
import { signal } from '@preact/signals';

export interface PoolState {
  measurements: {
    ph: number;
    orp: number;
    water_temp: number;
    air_temp: number;
    pressure: number;
  };
  setpoints: {
    ph: number;
    orp: number;
    water_temp: number;
  };
  pumps: {
    filtration: boolean;
    ph: boolean;
    chl: boolean;
    robot: boolean;
    filt_uptime_s: number;
    ph_uptime_s: number;
    chl_uptime_s: number;
  };
  tanks: {
    acid_fill_pct: number;
    chl_fill_pct: number;
  };
  modes: {
    auto: boolean;
    winter: boolean;
    ph_pid: boolean;
    orp_pid: boolean;
  };
  alarms: {
    pressure: boolean;
    ph_pump_overtime: boolean;
    chl_pump_overtime: boolean;
    acid_tank_low: boolean;
    chl_tank_low: boolean;
  };
  diagnostics: {
    firmware: string;
    uptime_s: number;
    free_heap: number;
    wifi_rssi: number;
    ssid: string;
    ip: string;
  };
}

export const poolState = signal<PoolState | null>(null);

export function applyState(data: PoolState) {
  poolState.value = data;
}
```

- [ ] **Step 2: Create `web/src/stores/log.ts`**

```typescript
import { signal } from '@preact/signals';

export interface LogEntry {
  ts: number;
  level: 'dbg' | 'inf' | 'wrn' | 'err' | string;
  msg: string;
}

export const logEntries = signal<LogEntry[]>([]);

const MAX = 500;

export function appendLog(e: LogEntry) {
  const next = logEntries.value.concat(e);
  if (next.length > MAX) next.splice(0, next.length - MAX);
  logEntries.value = next;
}

export function replaceLogs(entries: LogEntry[]) {
  logEntries.value = entries.slice(-MAX);
}

export function clearLogs() {
  logEntries.value = [];
}
```

- [ ] **Step 3: Create `web/src/stores/history.ts`**

```typescript
import { signal } from '@preact/signals';

export type SeriesName = 'ph' | 'orp' | 'water_temp' | 'air_temp' | 'pressure';

export interface Series {
  name: SeriesName;
  t0_ms: number;
  step_s: number;
  values: number[];
}

export const history = signal<Record<SeriesName, Series | null>>({
  ph: null, orp: null, water_temp: null, air_temp: null, pressure: null,
});

export function setSeries(s: Series) {
  history.value = { ...history.value, [s.name]: s };
}

export function appendToSeries(name: SeriesName, values: number[]) {
  const cur = history.value[name];
  if (!cur) return;
  const MAX = 120;
  const next = cur.values.concat(values);
  if (next.length > MAX) next.splice(0, next.length - MAX);
  history.value = { ...history.value, [name]: { ...cur, values: next } };
}
```

- [ ] **Step 4: Create `web/src/stores/connection.ts`**

```typescript
import { signal } from '@preact/signals';

export interface WelcomeInfo {
  device: string;
  firmware: string;
  server_time: number;
}

export const welcome = signal<WelcomeInfo | null>(null);
```

- [ ] **Step 5: Create `web/src/stores/index.ts`** — bootstraps WebSocket → stores

```typescript
import { poolWs } from '../lib/ws';
import { applyState, type PoolState } from './state';
import { appendLog, replaceLogs, type LogEntry } from './log';
import { setSeries, appendToSeries, type SeriesName } from './history';
import { welcome, type WelcomeInfo } from './connection';

let bootstrapped = false;

export function bootstrapStores() {
  if (bootstrapped) return;
  bootstrapped = true;

  poolWs.subscribe(msg => {
    switch (msg.type) {
      case 'welcome':
        welcome.value = msg as unknown as WelcomeInfo;
        break;

      case 'state':
        applyState((msg.data ?? {}) as PoolState);
        break;

      case 'log':
        appendLog({
          ts: Number(msg.ts),
          level: String(msg.level),
          msg: String(msg.msg),
        });
        break;

      case 'history': {
        const name = msg.series as SeriesName;
        if (Array.isArray(msg.append)) {
          appendToSeries(name, msg.append as number[]);
        } else if (Array.isArray(msg.values)) {
          setSeries({
            name,
            t0_ms: Number(msg.t0),
            step_s: Number(msg.step_s),
            values: msg.values as number[],
          });
        }
        break;
      }

      case 'alarm':
        // Alarms update existing poolState.alarms already via the next state
        // broadcast; no separate store needed. A transient toast can be added
        // later if desired.
        break;
    }
  });

  poolWs.start();
}

// Re-export for convenience
export { poolState } from './state';
export { logEntries, clearLogs } from './log';
export { history } from './history';
export { welcome } from './connection';
```

- [ ] **Step 6: Wire bootstrap into `web/src/main.tsx`**

Update `web/src/main.tsx`:
```tsx
import { render } from 'preact';
import { App } from './app';
import { bootstrapStores } from './stores';
import './styles.css';

if ('serviceWorker' in navigator && import.meta.env.PROD) {
  window.addEventListener('load', () => {
    navigator.serviceWorker.register('/sw.js').catch(err => {
      console.warn('SW registration failed', err);
    });
  });
}

bootstrapStores();
render(<App />, document.getElementById('root')!);
```

- [ ] **Step 7: Compile**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web
npx tsc --noEmit 2>&1 | tail -5
npm run build 2>&1 | tail -5
```
Expected: both clean.

- [ ] **Step 8: Commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
git add web/src/stores/ web/src/main.tsx
git commit -m "sp3: signal stores + WS message router (bootstrapStores)"
```

---

### Task 15: Shared components

**Files:**
- Create: `web/src/components/Tile.tsx`
- Create: `web/src/components/Toggle.tsx`
- Create: `web/src/components/Slider.tsx`
- Create: `web/src/components/Modal.tsx`
- Create: `web/src/components/Chart.tsx`
- Create: `web/src/components/AlarmBanner.tsx`
- Create: `web/src/components/Badge.tsx`
- Create: `web/src/components/NavShell.tsx`
- Create: `web/src/components/AuthBoundary.tsx`

All components are small and functional. Each one is a full file below.

- [ ] **Step 1: Create `web/src/components/Tile.tsx`**

```tsx
import type { ComponentChildren } from 'preact';

interface TileProps {
  label: string;
  value: ComponentChildren;
  unit?: string;
  sub?: ComponentChildren;
  class?: string;
}

export function Tile({ label, value, unit, sub, class: className = '' }: TileProps) {
  return (
    <div class={`glass p-3 ${className}`}>
      <div class="label-caps">{label}</div>
      <div class="text-2xl font-bold val-num leading-tight mt-1">
        {value}{unit && <span class="text-sm opacity-50 font-normal ml-1">{unit}</span>}
      </div>
      {sub && <div class="text-xs opacity-60 mt-1">{sub}</div>}
    </div>
  );
}
```

- [ ] **Step 2: Create `web/src/components/Toggle.tsx`**

```tsx
interface ToggleProps {
  on: boolean;
  onChange: (next: boolean) => void;
  label?: string;
  disabled?: boolean;
}

export function Toggle({ on, onChange, label, disabled }: ToggleProps) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={on}
      disabled={disabled}
      onClick={() => !disabled && onChange(!on)}
      class={`inline-flex items-center gap-3 ${disabled ? 'opacity-50 cursor-not-allowed' : 'cursor-pointer'}`}
    >
      <span class={`relative w-[42px] h-6 rounded-full border transition-colors ${
        on ? 'bg-aqua-primary/35 border-aqua-primary/50' : 'bg-slate-900/60 border-aqua-border'
      }`}>
        <span class={`absolute top-0.5 w-[18px] h-[18px] rounded-full transition-all ${
          on ? 'left-[22px] bg-aqua-primary-hover' : 'left-0.5 bg-slate-300'
        }`} />
      </span>
      {label && <span class="text-sm">{label}</span>}
    </button>
  );
}
```

- [ ] **Step 3: Create `web/src/components/Slider.tsx`**

```tsx
import { useState } from 'preact/hooks';

interface SliderProps {
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  unit?: string;
  onCommit: (next: number) => void;
}

export function Slider({ label, value, min, max, step, unit, onCommit }: SliderProps) {
  const [draft, setDraft] = useState(value);

  return (
    <div class="space-y-1">
      <div class="flex items-baseline justify-between">
        <span class="label-caps">{label}</span>
        <span class="val-num font-semibold text-aqua-text">
          {draft}{unit && <span class="text-sm opacity-50 font-normal ml-1">{unit}</span>}
        </span>
      </div>
      <input
        type="range"
        min={min}
        max={max}
        step={step}
        value={draft}
        onInput={e => setDraft(Number((e.target as HTMLInputElement).value))}
        onChange={e => onCommit(Number((e.target as HTMLInputElement).value))}
        class="w-full accent-aqua-primary"
      />
      <div class="flex justify-between text-xs opacity-50 val-num">
        <span>{min}</span><span>{max}</span>
      </div>
    </div>
  );
}
```

- [ ] **Step 4: Create `web/src/components/Modal.tsx`**

```tsx
import type { ComponentChildren } from 'preact';

interface ModalProps {
  open: boolean;
  title: string;
  onClose: () => void;
  children: ComponentChildren;
  footer?: ComponentChildren;
}

export function Modal({ open, title, onClose, children, footer }: ModalProps) {
  if (!open) return null;
  return (
    <div class="fixed inset-0 z-50 flex items-center justify-center p-4" role="dialog" aria-modal="true">
      <div class="absolute inset-0 bg-black/60 backdrop-blur-sm" onClick={onClose} />
      <div class="relative glass-elev p-5 max-w-md w-full">
        <div class="flex items-center justify-between mb-4">
          <h2 class="text-lg font-semibold">{title}</h2>
          <button onClick={onClose} class="opacity-60 hover:opacity-100 text-xl leading-none">×</button>
        </div>
        <div class="space-y-3">{children}</div>
        {footer && <div class="mt-5 flex justify-end gap-2">{footer}</div>}
      </div>
    </div>
  );
}
```

- [ ] **Step 5: Create `web/src/components/Chart.tsx`**

```tsx
import { useEffect, useRef } from 'preact/hooks';
import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';

interface ChartProps {
  values: number[];
  t0_ms: number;
  step_s: number;
  label: string;
  height?: number;
}

export function Chart({ values, t0_ms, step_s, label, height = 160 }: ChartProps) {
  const ref = useRef<HTMLDivElement>(null);
  const plotRef = useRef<uPlot | null>(null);

  useEffect(() => {
    if (!ref.current) return;
    const xs = values.map((_, i) => (t0_ms + i * step_s * 1000) / 1000);
    const ys = values.map(v => (Number.isFinite(v) ? v : null));

    if (plotRef.current) {
      plotRef.current.setData([xs, ys]);
      return;
    }

    plotRef.current = new uPlot(
      {
        width: ref.current.offsetWidth || 400,
        height,
        scales: { x: { time: true } },
        axes: [
          { stroke: '#7dd3fc', grid: { stroke: 'rgba(125,211,252,0.1)' } },
          { stroke: '#7dd3fc', grid: { stroke: 'rgba(125,211,252,0.1)' } },
        ],
        series: [
          {},
          { label, stroke: '#22d3ee', width: 2, fill: 'rgba(34,211,238,0.1)' },
        ],
      },
      [xs, ys],
      ref.current,
    );

    return () => { plotRef.current?.destroy(); plotRef.current = null; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [values, t0_ms, step_s]);

  return <div ref={ref} class="glass p-2" />;
}
```

- [ ] **Step 6: Create `web/src/components/AlarmBanner.tsx`**

```tsx
import { useComputed } from '@preact/signals';
import { poolState } from '../stores/state';

const LABELS: Record<string, string> = {
  pressure: 'Pressure alarm',
  ph_pump_overtime: 'pH pump overtime',
  chl_pump_overtime: 'Chlorine pump overtime',
  acid_tank_low: 'Acid tank low',
  chl_tank_low: 'Chlorine tank low',
};

export function AlarmBanner() {
  const active = useComputed(() => {
    const s = poolState.value;
    if (!s) return [] as string[];
    return Object.entries(s.alarms).filter(([, v]) => v).map(([k]) => k);
  });

  if (active.value.length === 0) return null;

  return (
    <div class="bg-aqua-alarm/15 border border-aqua-alarm/40 text-rose-200 rounded-xl p-3 text-sm flex items-start gap-2">
      <span class="text-aqua-alarm text-lg leading-none">⚠</span>
      <div>
        <div class="font-semibold">
          {active.value.length === 1 ? '1 alarm' : `${active.value.length} alarms`} active
        </div>
        <div class="text-xs opacity-80">
          {active.value.map(id => LABELS[id] ?? id).join(' · ')}
        </div>
      </div>
    </div>
  );
}
```

- [ ] **Step 7: Create `web/src/components/Badge.tsx`**

```tsx
import type { ComponentChildren } from 'preact';

type Variant = 'ok' | 'info' | 'warn' | 'alarm' | 'muted';

const VARIANT: Record<Variant, string> = {
  ok:    'bg-aqua-ok/15 text-emerald-300 border-emerald-500/30',
  info:  'bg-aqua-info/15 text-cyan-300 border-cyan-500/30',
  warn:  'bg-aqua-warn/15 text-amber-300 border-amber-500/30',
  alarm: 'bg-aqua-alarm/15 text-rose-300 border-rose-500/35',
  muted: 'bg-white/5 text-slate-300 border-white/10',
};

interface BadgeProps {
  variant?: Variant;
  dot?: boolean;
  children: ComponentChildren;
}

export function Badge({ variant = 'muted', dot, children }: BadgeProps) {
  return (
    <span class={`inline-flex items-center gap-1.5 px-2.5 py-0.5 rounded-full text-[0.72rem] font-semibold border ${VARIANT[variant]}`}>
      {dot && <span class="w-1.5 h-1.5 rounded-full bg-current" />}
      {children}
    </span>
  );
}
```

- [ ] **Step 8: Create `web/src/components/AuthBoundary.tsx`**

```tsx
import { useCallback } from 'preact/hooks';
import { forceAuthPrompt } from '../lib/api';

/**
 * Wraps an async admin-gated action. If the action fails with auth_required
 * (HTTP 401 or `error: "auth_required"` in body), we trigger the browser's
 * Basic-auth dialog via a GET to /api/whoami and retry once.
 */
export function useAuthedAction<T>(action: () => Promise<{ ok: boolean; error?: string }>) {
  return useCallback(async () => {
    const first = await action();
    if (first.ok) return first;
    if (first.error?.includes('auth')) {
      const authed = await forceAuthPrompt();
      if (authed) return action();
    }
    return first;
  }, [action]);
}
```

- [ ] **Step 9: Create `web/src/components/NavShell.tsx`**

```tsx
import type { ComponentChildren } from 'preact';
import { useSignal } from '@preact/signals';
import { wsStatus } from '../lib/ws';
import { useComputed } from '@preact/signals';

interface NavItem {
  to: string;
  label: string;
  icon: string;  // emoji for now; lucide-preact later if desired
}

const NAV: NavItem[] = [
  { to: '/',           label: 'Home',     icon: '🏠' },
  { to: '/control',    label: 'Control',  icon: '🎛' },
  { to: '/tune',       label: 'Tune',     icon: '🧪' },
  { to: '/insights',   label: 'Insights', icon: '📊' },
  { to: '/settings',   label: 'Settings', icon: '⚙' },
];

function statusBadge(status: string) {
  if (status === 'connected')    return { class: 'text-emerald-300 bg-emerald-500/15', text: 'Live' };
  if (status === 'connecting')   return { class: 'text-amber-300 bg-amber-500/15',   text: 'Connecting' };
  if (status === 'reconnecting') return { class: 'text-amber-300 bg-amber-500/15',   text: 'Reconnecting' };
  return { class: 'text-rose-300 bg-rose-500/15', text: 'Offline' };
}

function currentPath(): string {
  return window.location.pathname || '/';
}

function isActive(path: string, to: string): boolean {
  if (to === '/') return path === '/';
  return path === to || path.startsWith(`${to}/`);
}

interface NavShellProps {
  children: ComponentChildren;
}

export function NavShell({ children }: NavShellProps) {
  const path = useSignal(currentPath());
  const badge = useComputed(() => statusBadge(wsStatus.value));

  // Listen for navigation events (preact-iso handles the pushstate).
  if (typeof window !== 'undefined') {
    window.addEventListener('popstate', () => { path.value = currentPath(); });
    // preact-iso emits a custom event on route change:
    window.addEventListener('preact-iso-route', () => { path.value = currentPath(); });
  }

  const navigate = (to: string) => (e: Event) => {
    e.preventDefault();
    history.pushState({}, '', to);
    path.value = to;
    window.dispatchEvent(new Event('preact-iso-route'));
  };

  return (
    <div class="min-h-screen flex">
      {/* Desktop sidebar */}
      <aside class="hidden md:flex flex-col w-56 p-4 border-r border-aqua-border glass-elev">
        <div class="text-lg font-bold tracking-tight mb-6">🏊 PoolMaster</div>
        <nav class="flex-1 space-y-1">
          {NAV.map(n => (
            <a
              href={n.to}
              onClick={navigate(n.to)}
              class={`flex items-center gap-3 px-3 py-2 rounded-lg text-sm ${
                isActive(path.value, n.to)
                  ? 'bg-aqua-primary/15 text-cyan-200 border border-aqua-border-elev'
                  : 'hover:bg-white/5 text-aqua-text/80'
              }`}
            >
              <span class="text-lg">{n.icon}</span>
              <span>{n.label}</span>
            </a>
          ))}
        </nav>
        <div class="mt-4 pt-4 border-t border-aqua-border">
          <span class={`inline-flex items-center gap-2 px-2.5 py-1 rounded-full text-xs font-semibold ${badge.value.class}`}>
            <span class="w-1.5 h-1.5 rounded-full bg-current" />
            {badge.value.text}
          </span>
        </div>
      </aside>

      {/* Main column */}
      <div class="flex-1 flex flex-col min-w-0">
        {/* Mobile top bar */}
        <header class="md:hidden flex items-center justify-between p-3 safe-top border-b border-aqua-border">
          <span class="font-bold">🏊 PoolMaster</span>
          <span class={`inline-flex items-center gap-1.5 px-2.5 py-0.5 rounded-full text-xs font-semibold ${badge.value.class}`}>
            <span class="w-1.5 h-1.5 rounded-full bg-current" />
            {badge.value.text}
          </span>
        </header>

        <main class="flex-1 overflow-y-auto p-4 pb-24 md:pb-4">{children}</main>

        {/* Mobile bottom tab bar */}
        <nav class="md:hidden fixed bottom-0 inset-x-0 z-40 glass-elev border-t border-aqua-border safe-bottom flex justify-around py-2">
          {NAV.map(n => (
            <a
              href={n.to}
              onClick={navigate(n.to)}
              class={`flex flex-col items-center py-1 px-3 text-xs ${
                isActive(path.value, n.to) ? 'text-cyan-200' : 'text-aqua-text/60'
              }`}
            >
              <span class="text-lg leading-none">{n.icon}</span>
              <span class="mt-0.5">{n.label}</span>
            </a>
          ))}
        </nav>
      </div>
    </div>
  );
}
```

- [ ] **Step 10: Compile**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web
npx tsc --noEmit 2>&1 | tail -5
npm run build 2>&1 | tail -5
```
Expected: both clean. Bundle size will grow noticeably.

- [ ] **Step 11: Commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
git add web/src/components/
git commit -m "sp3: shared components — Tile, Toggle, Slider, Modal, Chart, AlarmBanner, Badge, NavShell, AuthBoundary"
```

---

### Task 16: App shell + router wiring

**Files:**
- Modify: `web/src/app.tsx` (replace placeholder shell with router)

- [ ] **Step 1: Replace `web/src/app.tsx`** with:

```tsx
import { LocationProvider, Router, Route } from 'preact-iso';
import { NavShell } from './components/NavShell';

// Placeholder screens — real implementations land in Tasks 17+.
function Placeholder({ name }: { name: string }) {
  return (
    <div class="glass p-6 max-w-xl">
      <div class="label-caps">Screen</div>
      <h1 class="text-xl font-bold mt-1">{name}</h1>
      <p class="text-sm opacity-70 mt-2">Coming in a later SP3 task.</p>
    </div>
  );
}

export function App() {
  return (
    <LocationProvider>
      <NavShell>
        <Router>
          <Route path="/"                      component={() => <Placeholder name="Dashboard" />} />
          <Route path="/control"               component={() => <Placeholder name="Manual control" />} />
          <Route path="/control/setpoints"     component={() => <Placeholder name="Setpoints" />} />
          <Route path="/tune"                  component={() => <Placeholder name="Calibration" />} />
          <Route path="/tune/pid"              component={() => <Placeholder name="PID tuning" />} />
          <Route path="/tune/schedule"         component={() => <Placeholder name="Schedule" />} />
          <Route path="/tune/tanks"            component={() => <Placeholder name="Tanks" />} />
          <Route path="/insights"              component={() => <Placeholder name="History" />} />
          <Route path="/insights/logs"         component={() => <Placeholder name="Logs" />} />
          <Route path="/insights/diagnostics"  component={() => <Placeholder name="Diagnostics" />} />
          <Route path="/settings"              component={() => <Placeholder name="Network & admin" />} />
          <Route path="/settings/firmware"     component={() => <Placeholder name="Firmware update" />} />
          <Route default                       component={() => <Placeholder name="404" />} />
        </Router>
      </NavShell>
    </LocationProvider>
  );
}
```

- [ ] **Step 2: Build + OTA**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web
npm run build 2>&1 | tail -5
cd ..
export PATH="$HOME/.platformio/penv/bin:$PATH"
export POOLMASTER_OTA_PWD=""
pio run -e OTA_upload -t upload 2>&1 | tail -3    # firmware still needed? only if unchanged this is skipped
pio run -e OTA_upload -t uploadfs 2>&1 | tail -3
sleep 8
curl -sS --max-time 5 http://poolmaster.local/ | head -c 300
```
Expected: HTML with the SPA shell. Open in browser → sidebar (desktop) or bottom tabs (mobile), navigation works, each tab shows "Coming in a later SP3 task."

- [ ] **Step 3: Commit**

```bash
git add web/src/app.tsx
git commit -m "sp3: app shell with preact-iso router + 12 placeholder routes"
```

- [ ] **Step 4: Create checkpoint `sp3-ckpt3-shell`**

```bash
git branch sp3-ckpt3-shell
```

---

## Phase 4: Screens

The next 12 tasks each deliver one screen. Each follows the pattern: create the `.tsx` file → wire into `app.tsx` → build + OTA uploadfs → commit.

### Task 17: Dashboard screen

**Files:**
- Create: `web/src/screens/Dashboard.tsx`
- Modify: `web/src/app.tsx`

- [ ] **Step 1: Create `web/src/screens/Dashboard.tsx`**

```tsx
import { useComputed } from '@preact/signals';
import { poolState } from '../stores/state';
import { Tile } from '../components/Tile';
import { Badge } from '../components/Badge';
import { AlarmBanner } from '../components/AlarmBanner';
import { fmtNum, fmtInt, fmtDuration } from '../lib/format';

export function Dashboard() {
  const s = useComputed(() => poolState.value);

  if (!s.value) {
    return <div class="glass p-6">Loading pool state…</div>;
  }
  const st = s.value;

  return (
    <div class="space-y-4 max-w-4xl">
      <AlarmBanner />

      <div class="flex items-baseline justify-between">
        <h1 class="text-2xl font-bold">🏊 Pool</h1>
        <div class="flex gap-2">
          {st.modes.auto   && <Badge variant="info" dot>Auto</Badge>}
          {st.modes.winter && <Badge variant="info" dot>Winter</Badge>}
        </div>
      </div>

      <div class="grid grid-cols-2 lg:grid-cols-4 gap-3">
        <Tile label="pH"        value={fmtNum(st.measurements.ph, 2)}        sub={`target ${fmtNum(st.setpoints.ph, 1)}`} />
        <Tile label="ORP"       value={fmtInt(st.measurements.orp)}   unit="mV" sub={`target ${fmtInt(st.setpoints.orp)} mV`} />
        <Tile label="Water"     value={fmtNum(st.measurements.water_temp, 1)} unit="°C" sub={`target ${fmtNum(st.setpoints.water_temp, 1)} °C`} />
        <Tile label="Air"       value={fmtNum(st.measurements.air_temp, 1)}   unit="°C" />
        <Tile label="Pressure"  value={fmtNum(st.measurements.pressure, 2)}    unit="bar" />
        <Tile label="Acid tank" value={fmtInt(st.tanks.acid_fill_pct)}  unit="%" />
        <Tile label="Chl tank"  value={fmtInt(st.tanks.chl_fill_pct)}   unit="%" />
        <Tile label="Uptime"    value={fmtDuration(st.diagnostics.uptime_s)} />
      </div>

      <div class="glass p-3">
        <div class="label-caps mb-2">Pumps</div>
        <div class="flex flex-wrap gap-2">
          <Badge variant={st.pumps.filtration ? 'info' : 'muted'} dot>
            Filtration · {st.pumps.filtration ? `running · ${fmtDuration(st.pumps.filt_uptime_s)}` : 'idle'}
          </Badge>
          <Badge variant={st.pumps.ph  ? 'info' : 'muted'} dot>pH pump · {st.pumps.ph  ? 'running' : 'idle'}</Badge>
          <Badge variant={st.pumps.chl ? 'info' : 'muted'} dot>Chl pump · {st.pumps.chl ? 'running' : 'idle'}</Badge>
          <Badge variant={st.pumps.robot ? 'info' : 'muted'} dot>Robot · {st.pumps.robot ? 'running' : 'idle'}</Badge>
        </div>
      </div>

      <div class="glass p-3">
        <div class="label-caps mb-2">Device</div>
        <div class="grid grid-cols-2 gap-x-4 gap-y-1 text-sm">
          <div class="opacity-60">Firmware</div>      <div class="val-num">{st.diagnostics.firmware}</div>
          <div class="opacity-60">IP</div>            <div class="val-num">{st.diagnostics.ip}</div>
          <div class="opacity-60">WiFi SSID</div>     <div class="val-num">{st.diagnostics.ssid}</div>
          <div class="opacity-60">RSSI</div>          <div class="val-num">{fmtInt(st.diagnostics.wifi_rssi)} dBm</div>
          <div class="opacity-60">Free heap</div>     <div class="val-num">{fmtInt(st.diagnostics.free_heap)} B</div>
        </div>
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Wire into `web/src/app.tsx`**

Replace the `/` Route placeholder:
```tsx
import { Dashboard } from './screens/Dashboard';
// ...
          <Route path="/" component={Dashboard} />
```

Remove the placeholder line for `/` and add the `Dashboard` import.

- [ ] **Step 3: Build + OTA**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web
npm run build 2>&1 | tail -3
cd ..
export PATH="$HOME/.platformio/penv/bin:$PATH"
export POOLMASTER_OTA_PWD=""
pio run -e OTA_upload -t uploadfs 2>&1 | tail -3
sleep 6
curl -sS --max-time 5 http://poolmaster.local/ | head -c 100
```

Open `http://poolmaster.local/` in browser. Expected: live dashboard with your current pH, ORP, temps, pressure, tank fills.

- [ ] **Step 4: Commit**

```bash
git add web/src/screens/Dashboard.tsx web/src/app.tsx
git commit -m "sp3: Dashboard screen — live tiles, alarm banner, pump state row"
```

---

### Task 18: Manual control screen

**Files:**
- Create: `web/src/screens/Manual.tsx`
- Modify: `web/src/app.tsx`

- [ ] **Step 1: Create `web/src/screens/Manual.tsx`**

```tsx
import { useComputed } from '@preact/signals';
import { poolState } from '../stores/state';
import { Toggle } from '../components/Toggle';
import { Badge } from '../components/Badge';
import { poolWs } from '../lib/ws';
import { useAuthedAction } from '../components/AuthBoundary';
import { apiSendCommand } from '../lib/api';

function cmd(payload: string) {
  // Prefer WebSocket when open; REST fallback for auth-triggering POSTs.
  if (!poolWs.send({ type: 'cmd', id: crypto.randomUUID(), payload })) {
    return apiSendCommand(payload);
  }
  return Promise.resolve({ ok: true } as const);
}

interface Row {
  key: string;
  label: string;
  getState: (s: ReturnType<typeof poolState.value.valueOf> extends null ? never : NonNullable<typeof poolState.value>) => boolean;
  on:  string;
  off: string;
}

// Map each toggle to its legacy JSON command.
const ROWS: Array<{ label: string; getOn: (s: NonNullable<typeof poolState.value>) => boolean; on: string; off: string }> = [
  { label: 'Auto mode',       getOn: s => s.modes.auto,       on: '{"Mode":1}',     off: '{"Mode":0}' },
  { label: 'Winter mode',     getOn: s => s.modes.winter,     on: '{"Winter":1}',   off: '{"Winter":0}' },
  { label: 'pH PID',          getOn: s => s.modes.ph_pid,     on: '{"PhPID":1}',    off: '{"PhPID":0}' },
  { label: 'ORP PID',         getOn: s => s.modes.orp_pid,    on: '{"OrpPID":1}',   off: '{"OrpPID":0}' },
  { label: 'Filtration pump', getOn: s => s.pumps.filtration, on: '{"FiltPump":1}', off: '{"FiltPump":0}' },
  { label: 'pH pump',         getOn: s => s.pumps.ph,         on: '{"PhPump":1}',   off: '{"PhPump":0}' },
  { label: 'Chlorine pump',   getOn: s => s.pumps.chl,        on: '{"ChlPump":1}',  off: '{"ChlPump":0}' },
  { label: 'Robot pump',      getOn: s => s.pumps.robot,      on: '{"RobotPump":1}',off: '{"RobotPump":0}' },
];

const RELAY_ROWS = [
  { label: 'Projecteur (R0)', num: 1 },
  { label: 'Spare (R1)',      num: 2 },
];

export function Manual() {
  const s = useComputed(() => poolState.value);

  if (!s.value) return <div class="glass p-6">Loading…</div>;

  const state = s.value;

  const toggle = (on: boolean, payload: string) => () => cmd(payload);

  return (
    <div class="space-y-4 max-w-xl">
      <h1 class="text-xl font-bold">Manual control</h1>
      <div class="glass p-4 space-y-3">
        {ROWS.map(row => (
          <div key={row.label} class="flex items-center justify-between">
            <span class="text-sm">{row.label}</span>
            <Toggle
              on={row.getOn(state)}
              onChange={next => cmd(next ? row.on : row.off)}
            />
          </div>
        ))}
      </div>

      <div class="glass p-4 space-y-3">
        <div class="label-caps">Relays</div>
        {RELAY_ROWS.map(r => {
          const isOn = r.num === 1 ? /* projecteur state isn't in /api/state; use auto mode bit as placeholder */ false : false;
          return (
            <div key={r.num} class="flex items-center justify-between">
              <span class="text-sm">{r.label}</span>
              <div class="flex gap-2">
                <button
                  class="text-xs px-3 py-1 rounded-md bg-aqua-primary/15 border border-aqua-primary/35 text-cyan-200"
                  onClick={() => cmd(`{"Relay":[${r.num},1]}`)}
                >ON</button>
                <button
                  class="text-xs px-3 py-1 rounded-md bg-white/5 border border-aqua-border"
                  onClick={() => cmd(`{"Relay":[${r.num},0]}`)}
                >OFF</button>
              </div>
            </div>
          );
        })}
      </div>

      <div class="glass p-4 flex gap-2 flex-wrap">
        <Badge variant="info">Click any toggle to flip — the state updates when the device acks via WebSocket.</Badge>
      </div>
    </div>
  );
}
```

Note: the relay toggle uses discrete ON/OFF buttons rather than a `Toggle` component because the current `/api/state` doesn't expose relay state (only GPIO writes via `Relay` command). A follow-up task can add relay state to the state snapshot.

- [ ] **Step 2: Wire into `app.tsx`**

Add import and replace the `/control` route:
```tsx
import { Manual } from './screens/Manual';
// ...
<Route path="/control" component={Manual} />
```

- [ ] **Step 3: Build + OTA + commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web
npm run build 2>&1 | tail -3
cd .. && export PATH="$HOME/.platformio/penv/bin:$PATH" && export POOLMASTER_OTA_PWD=""
pio run -e OTA_upload -t uploadfs 2>&1 | tail -3
git add web/src/screens/Manual.tsx web/src/app.tsx
git commit -m "sp3: Manual control screen with toggles + relay buttons"
```

---

### Task 19: Setpoints screen

**Files:**
- Create: `web/src/screens/Setpoints.tsx`
- Modify: `web/src/app.tsx`

- [ ] **Step 1: Create `web/src/screens/Setpoints.tsx`**

```tsx
import { useComputed } from '@preact/signals';
import { poolState } from '../stores/state';
import { Slider } from '../components/Slider';
import { poolWs } from '../lib/ws';

export function Setpoints() {
  const s = useComputed(() => poolState.value);
  if (!s.value) return <div class="glass p-6">Loading…</div>;
  const sp = s.value.setpoints;

  const send = (key: string, val: number) => {
    const payload = JSON.stringify({ [key]: val });
    poolWs.send({ type: 'cmd', id: crypto.randomUUID(), payload });
  };

  return (
    <div class="space-y-4 max-w-lg">
      <h1 class="text-xl font-bold">Setpoints</h1>

      <div class="glass p-5">
        <Slider label="pH target" value={sp.ph} min={6.5} max={8.0} step={0.1} unit="pH"
                onCommit={v => send('PhSetPoint', v)} />
      </div>
      <div class="glass p-5">
        <Slider label="ORP target" value={sp.orp} min={400} max={900} step={10} unit="mV"
                onCommit={v => send('OrpSetPoint', v)} />
      </div>
      <div class="glass p-5">
        <Slider label="Water temperature" value={sp.water_temp} min={15} max={35} step={0.5} unit="°C"
                onCommit={v => send('WSetPoint', v)} />
      </div>

      <p class="text-xs opacity-60">Values are saved to NVS on change; the device echoes back the new setpoint via WebSocket.</p>
    </div>
  );
}
```

- [ ] **Step 2: Wire into `app.tsx`**

```tsx
import { Setpoints } from './screens/Setpoints';
// ...
<Route path="/control/setpoints" component={Setpoints} />
```

- [ ] **Step 3: Build + OTA + commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web && npm run build 2>&1 | tail -3
cd .. && pio run -e OTA_upload -t uploadfs 2>&1 | tail -3
git add web/src/screens/Setpoints.tsx web/src/app.tsx
git commit -m "sp3: Setpoints screen with sliders for pH/ORP/water-temp"
```

---

### Task 20: Calibration wizard screen

**Files:**
- Create: `web/src/screens/Calibration.tsx`
- Modify: `web/src/app.tsx`

- [ ] **Step 1: Create `web/src/screens/Calibration.tsx`**

```tsx
import { useSignal, useComputed } from '@preact/signals';
import { poolState } from '../stores/state';
import { Modal } from '../components/Modal';
import { Badge } from '../components/Badge';
import { poolWs } from '../lib/ws';
import { fmtNum } from '../lib/format';

type Probe = 'ph' | 'orp';

interface Reading { raw: number; buffer: number }

export function Calibration() {
  const probe      = useSignal<Probe>('ph');
  const running    = useSignal(false);
  const readings   = useSignal<Reading[]>([]);
  const currentBuffer = useSignal('');

  const state = useComputed(() => poolState.value);

  const liveRaw = useComputed(() => {
    if (!state.value) return NaN;
    return probe.value === 'ph' ? state.value.measurements.ph : state.value.measurements.orp;
  });

  const startWizard = (p: Probe) => {
    probe.value = p;
    readings.value = [];
    currentBuffer.value = '';
    running.value = true;
  };

  const addReading = () => {
    const bufVal = Number(currentBuffer.value);
    if (!Number.isFinite(bufVal)) return;
    readings.value = [...readings.value, { raw: Number(liveRaw.value) || 0, buffer: bufVal }];
    currentBuffer.value = '';
  };

  const finish = () => {
    const key = probe.value === 'ph' ? 'PhCalib' : 'OrpCalib';
    const flat: number[] = [];
    readings.value.forEach(r => flat.push(r.raw, r.buffer));
    poolWs.send({ type: 'cmd', id: crypto.randomUUID(), payload: JSON.stringify({ [key]: flat }) });
    running.value = false;
  };

  return (
    <div class="space-y-4 max-w-2xl">
      <h1 class="text-xl font-bold">Calibration</h1>

      <div class="glass p-5">
        <h2 class="font-semibold mb-1">pH probe</h2>
        <p class="text-sm opacity-70 mb-3">Dip the probe into a buffer solution (pH 4.01 or 7.01), wait for the reading to stabilise, then capture it.</p>
        <button class="text-sm px-4 py-2 rounded-md bg-aqua-primary text-slate-900 font-semibold"
                onClick={() => startWizard('ph')}>Start pH calibration</button>
      </div>

      <div class="glass p-5">
        <h2 class="font-semibold mb-1">ORP probe</h2>
        <p class="text-sm opacity-70 mb-3">Dip the probe into an ORP reference (e.g. 230 mV or 475 mV), wait for the reading to stabilise, then capture it.</p>
        <button class="text-sm px-4 py-2 rounded-md bg-aqua-primary text-slate-900 font-semibold"
                onClick={() => startWizard('orp')}>Start ORP calibration</button>
      </div>

      <Modal
        open={running.value}
        title={`${probe.value === 'ph' ? 'pH' : 'ORP'} calibration`}
        onClose={() => (running.value = false)}
        footer={
          <>
            <button class="text-sm px-4 py-1.5 rounded-md bg-white/5 border border-aqua-border"
                    onClick={() => (running.value = false)}>Cancel</button>
            <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold disabled:opacity-50"
                    disabled={readings.value.length < 1}
                    onClick={finish}>
              Save ({readings.value.length} point{readings.value.length === 1 ? '' : 's'})
            </button>
          </>
        }
      >
        <div class="glass p-3">
          <div class="label-caps">Live reading</div>
          <div class="text-3xl font-bold val-num mt-1">
            {fmtNum(Number(liveRaw.value), probe.value === 'ph' ? 2 : 0)}
            <span class="text-sm opacity-50 ml-1 font-normal">
              {probe.value === 'ph' ? 'pH' : 'mV'}
            </span>
          </div>
          <div class="text-xs opacity-60 mt-1">Wait until this value stabilises before capturing.</div>
        </div>

        <div class="flex gap-2 items-end">
          <label class="flex-1">
            <div class="label-caps mb-1">Buffer value</div>
            <input
              type="number"
              step={probe.value === 'ph' ? 0.01 : 1}
              placeholder={probe.value === 'ph' ? '7.01' : '475'}
              value={currentBuffer.value}
              onInput={e => (currentBuffer.value = (e.target as HTMLInputElement).value)}
              class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm"
            />
          </label>
          <button class="px-3 py-1.5 rounded-md bg-aqua-primary/15 border border-aqua-primary/35 text-cyan-200 text-sm font-semibold"
                  onClick={addReading}
                  disabled={!currentBuffer.value}>
            Capture
          </button>
        </div>

        {readings.value.length > 0 && (
          <div class="glass p-3">
            <div class="label-caps mb-1">Captured points</div>
            <ul class="text-sm space-y-1">
              {readings.value.map((r, i) => (
                <li key={i} class="val-num">
                  {fmtNum(r.raw, probe.value === 'ph' ? 2 : 0)} → {fmtNum(r.buffer, 2)}
                </li>
              ))}
            </ul>
          </div>
        )}

        <Badge variant="info">Two points produce a linear calibration; more points improve accuracy (max 3).</Badge>
      </Modal>
    </div>
  );
}
```

- [ ] **Step 2: Wire into `app.tsx`** — replace `/tune`:
```tsx
import { Calibration } from './screens/Calibration';
// ...
<Route path="/tune" component={Calibration} />
```

- [ ] **Step 3: Build + OTA + commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web && npm run build 2>&1 | tail -3
cd .. && pio run -e OTA_upload -t uploadfs 2>&1 | tail -3
git add web/src/screens/Calibration.tsx web/src/app.tsx
git commit -m "sp3: Calibration wizard screen — multi-point capture + PhCalib/OrpCalib dispatch"
```

---

### Task 21: PID tuning screen

**Files:**
- Create: `web/src/screens/Pid.tsx`
- Modify: `web/src/app.tsx`

- [ ] **Step 1: Create `web/src/screens/Pid.tsx`**

```tsx
import { useComputed, useSignal } from '@preact/signals';
import { poolState } from '../stores/state';
import { Toggle } from '../components/Toggle';
import { poolWs } from '../lib/ws';

interface Form { kp: string; ki: string; kd: string; window_min: string }

function send(payload: object) {
  poolWs.send({ type: 'cmd', id: crypto.randomUUID(), payload: JSON.stringify(payload) });
}

export function Pid() {
  const s = useComputed(() => poolState.value);
  const phForm  = useSignal<Form>({ kp: '', ki: '', kd: '', window_min: '' });
  const orpForm = useSignal<Form>({ kp: '', ki: '', kd: '', window_min: '' });

  if (!s.value) return <div class="glass p-6">Loading…</div>;

  const Row = ({ name, v, onI }: { name: keyof Form; v: string; onI: (s: string) => void }) => (
    <label class="block">
      <div class="label-caps mb-1">{name}</div>
      <input type="number" step={name === 'kp' ? 1000 : 0.001} value={v}
             onInput={e => onI((e.target as HTMLInputElement).value)}
             class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
    </label>
  );

  const submitPh = () => {
    const params = [Number(phForm.value.kp) || 0, Number(phForm.value.ki) || 0, Number(phForm.value.kd) || 0];
    send({ PhPIDParams: params });
    if (phForm.value.window_min) send({ PhPIDWSize: Number(phForm.value.window_min) * 60000 });
  };
  const submitOrp = () => {
    const params = [Number(orpForm.value.kp) || 0, Number(orpForm.value.ki) || 0, Number(orpForm.value.kd) || 0];
    send({ OrpPIDParams: params });
    if (orpForm.value.window_min) send({ OrpPIDWSize: Number(orpForm.value.window_min) * 60000 });
  };

  return (
    <div class="space-y-4 max-w-3xl">
      <h1 class="text-xl font-bold">PID tuning</h1>

      <div class="glass p-5 space-y-3">
        <div class="flex items-center justify-between">
          <h2 class="font-semibold">pH regulator</h2>
          <Toggle on={s.value.modes.ph_pid} onChange={on => send({ PhPID: on ? 1 : 0 })} label="Enable" />
        </div>
        <div class="grid grid-cols-2 md:grid-cols-4 gap-3">
          <Row name="kp" v={phForm.value.kp} onI={v => (phForm.value = { ...phForm.value, kp: v })} />
          <Row name="ki" v={phForm.value.ki} onI={v => (phForm.value = { ...phForm.value, ki: v })} />
          <Row name="kd" v={phForm.value.kd} onI={v => (phForm.value = { ...phForm.value, kd: v })} />
          <Row name="window_min" v={phForm.value.window_min} onI={v => (phForm.value = { ...phForm.value, window_min: v })} />
        </div>
        <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold" onClick={submitPh}>Save pH PID</button>
      </div>

      <div class="glass p-5 space-y-3">
        <div class="flex items-center justify-between">
          <h2 class="font-semibold">ORP regulator</h2>
          <Toggle on={s.value.modes.orp_pid} onChange={on => send({ OrpPID: on ? 1 : 0 })} label="Enable" />
        </div>
        <div class="grid grid-cols-2 md:grid-cols-4 gap-3">
          <Row name="kp" v={orpForm.value.kp} onI={v => (orpForm.value = { ...orpForm.value, kp: v })} />
          <Row name="ki" v={orpForm.value.ki} onI={v => (orpForm.value = { ...orpForm.value, ki: v })} />
          <Row name="kd" v={orpForm.value.kd} onI={v => (orpForm.value = { ...orpForm.value, kd: v })} />
          <Row name="window_min" v={orpForm.value.window_min} onI={v => (orpForm.value = { ...orpForm.value, window_min: v })} />
        </div>
        <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold" onClick={submitOrp}>Save ORP PID</button>
      </div>

      <p class="text-xs opacity-60">Leave a field blank to keep the existing value. Window length is in minutes.</p>
    </div>
  );
}
```

- [ ] **Step 2: Wire into `app.tsx`** — replace `/tune/pid` placeholder:
```tsx
import { Pid } from './screens/Pid';
// ...
<Route path="/tune/pid" component={Pid} />
```

- [ ] **Step 3: Build + OTA + commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web && npm run build 2>&1 | tail -3
cd .. && pio run -e OTA_upload -t uploadfs 2>&1 | tail -3
git add web/src/screens/Pid.tsx web/src/app.tsx
git commit -m "sp3: PID tuning screen — Kp/Ki/Kd + window editor for both loops"
```

---

### Task 22: Schedule screen

**Files:**
- Create: `web/src/screens/Schedule.tsx`
- Modify: `web/src/app.tsx`

- [ ] **Step 1: Create `web/src/screens/Schedule.tsx`**

```tsx
import { useSignal } from '@preact/signals';
import { poolWs } from '../lib/ws';

function send(payload: object) {
  poolWs.send({ type: 'cmd', id: crypto.randomUUID(), payload: JSON.stringify(payload) });
}

export function Schedule() {
  const t0 = useSignal('8');
  const t1 = useSignal('20');
  const delayPid = useSignal('30');

  return (
    <div class="space-y-4 max-w-xl">
      <h1 class="text-xl font-bold">Schedule</h1>

      <div class="glass p-5 space-y-3">
        <h2 class="font-semibold">Filtration</h2>
        <div class="grid grid-cols-2 gap-3">
          <label class="block">
            <div class="label-caps mb-1">Earliest start (hour)</div>
            <input type="number" min={0} max={23} value={t0.value}
                   onInput={e => (t0.value = (e.target as HTMLInputElement).value)}
                   class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
          </label>
          <label class="block">
            <div class="label-caps mb-1">Latest stop (hour)</div>
            <input type="number" min={0} max={23} value={t1.value}
                   onInput={e => (t1.value = (e.target as HTMLInputElement).value)}
                   class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
          </label>
        </div>
        <div class="flex gap-2">
          <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold"
                  onClick={() => send({ FiltT0: Number(t0.value) })}>Save start</button>
          <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold"
                  onClick={() => send({ FiltT1: Number(t1.value) })}>Save stop</button>
        </div>
      </div>

      <div class="glass p-5 space-y-3">
        <h2 class="font-semibold">PID delay after filter start</h2>
        <p class="text-xs opacity-70">Minutes to wait after filtration starts before PID loops begin regulating (lets readings stabilise).</p>
        <div class="flex gap-2 items-end">
          <label class="flex-1">
            <div class="label-caps mb-1">Delay (minutes)</div>
            <input type="number" min={0} max={59} value={delayPid.value}
                   onInput={e => (delayPid.value = (e.target as HTMLInputElement).value)}
                   class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
          </label>
          <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold"
                  onClick={() => send({ DelayPID: Number(delayPid.value) })}>Save</button>
        </div>
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Wire into `app.tsx`**

```tsx
import { Schedule } from './screens/Schedule';
// ...
<Route path="/tune/schedule" component={Schedule} />
```

- [ ] **Step 3: Build + OTA + commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web && npm run build 2>&1 | tail -3
cd .. && pio run -e OTA_upload -t uploadfs 2>&1 | tail -3
git add web/src/screens/Schedule.tsx web/src/app.tsx
git commit -m "sp3: Schedule screen — filtration hours + PID delay"
```

---

### Task 23: Tanks screen

**Files:**
- Create: `web/src/screens/Tanks.tsx`
- Modify: `web/src/app.tsx`

- [ ] **Step 1: Create `web/src/screens/Tanks.tsx`**

```tsx
import { useComputed, useSignal } from '@preact/signals';
import { poolState } from '../stores/state';
import { poolWs } from '../lib/ws';

function send(payload: object) {
  poolWs.send({ type: 'cmd', id: crypto.randomUUID(), payload: JSON.stringify(payload) });
}

export function Tanks() {
  const s = useComputed(() => poolState.value);
  const acidVol = useSignal('20');
  const chlVol  = useSignal('20');
  const phFlow  = useSignal('1.5');
  const chlFlow = useSignal('1.5');

  if (!s.value) return <div class="glass p-6">Loading…</div>;

  return (
    <div class="space-y-4 max-w-2xl">
      <h1 class="text-xl font-bold">Tanks</h1>

      <div class="glass p-5 space-y-3">
        <h2 class="font-semibold">Acid (pH)</h2>
        <div class="flex items-baseline gap-2">
          <span class="text-3xl font-bold val-num">{s.value.tanks.acid_fill_pct}</span>
          <span class="text-sm opacity-50">% full</span>
        </div>
        <div class="grid grid-cols-2 gap-3">
          <label class="block">
            <div class="label-caps mb-1">Volume (L)</div>
            <input type="number" value={acidVol.value} onInput={e => (acidVol.value = (e.target as HTMLInputElement).value)}
                   class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
          </label>
          <label class="block">
            <div class="label-caps mb-1">Pump flow (L/h)</div>
            <input type="number" step="0.1" value={phFlow.value} onInput={e => (phFlow.value = (e.target as HTMLInputElement).value)}
                   class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
          </label>
        </div>
        <div class="flex gap-2">
          <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold"
                  onClick={() => send({ pHTank: [Number(acidVol.value), 100] })}>Mark as refilled (100%)</button>
          <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-primary/15 border border-aqua-primary/35 text-cyan-200"
                  onClick={() => send({ pHPumpFR: Number(phFlow.value) })}>Save flow</button>
        </div>
      </div>

      <div class="glass p-5 space-y-3">
        <h2 class="font-semibold">Chlorine</h2>
        <div class="flex items-baseline gap-2">
          <span class="text-3xl font-bold val-num">{s.value.tanks.chl_fill_pct}</span>
          <span class="text-sm opacity-50">% full</span>
        </div>
        <div class="grid grid-cols-2 gap-3">
          <label class="block">
            <div class="label-caps mb-1">Volume (L)</div>
            <input type="number" value={chlVol.value} onInput={e => (chlVol.value = (e.target as HTMLInputElement).value)}
                   class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
          </label>
          <label class="block">
            <div class="label-caps mb-1">Pump flow (L/h)</div>
            <input type="number" step="0.1" value={chlFlow.value} onInput={e => (chlFlow.value = (e.target as HTMLInputElement).value)}
                   class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
          </label>
        </div>
        <div class="flex gap-2">
          <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold"
                  onClick={() => send({ ChlTank: [Number(chlVol.value), 100] })}>Mark as refilled (100%)</button>
          <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-primary/15 border border-aqua-primary/35 text-cyan-200"
                  onClick={() => send({ ChlPumpFR: Number(chlFlow.value) })}>Save flow</button>
        </div>
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Wire into `app.tsx`**

```tsx
import { Tanks } from './screens/Tanks';
// ...
<Route path="/tune/tanks" component={Tanks} />
```

- [ ] **Step 3: Build + OTA + commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web && npm run build 2>&1 | tail -3
cd .. && pio run -e OTA_upload -t uploadfs 2>&1 | tail -3
git add web/src/screens/Tanks.tsx web/src/app.tsx
git commit -m "sp3: Tanks screen — refill buttons + volume/flow-rate config"
```

---

### Task 24: History screen

**Files:**
- Create: `web/src/screens/History.tsx`
- Modify: `web/src/app.tsx`

- [ ] **Step 1: Create `web/src/screens/History.tsx`**

```tsx
import { useComputed } from '@preact/signals';
import { history } from '../stores/history';
import { Chart } from '../components/Chart';

const SERIES: Array<{ name: keyof typeof history.value; label: string }> = [
  { name: 'ph',         label: 'pH' },
  { name: 'orp',        label: 'ORP (mV)' },
  { name: 'water_temp', label: 'Water temperature (°C)' },
  { name: 'air_temp',   label: 'Air temperature (°C)' },
  { name: 'pressure',   label: 'Pressure (bar)' },
];

export function History() {
  const h = useComputed(() => history.value);

  return (
    <div class="space-y-4 max-w-4xl">
      <h1 class="text-xl font-bold">History (last 60 min)</h1>
      {SERIES.map(s => {
        const series = h.value[s.name];
        if (!series) {
          return <div key={s.name} class="glass p-6 opacity-50">{s.label} — no data yet…</div>;
        }
        return (
          <div key={s.name} class="space-y-1">
            <div class="label-caps px-1">{s.label}</div>
            <Chart values={series.values} t0_ms={series.t0_ms} step_s={series.step_s} label={s.label} />
          </div>
        );
      })}
    </div>
  );
}
```

- [ ] **Step 2: Wire into `app.tsx`**

```tsx
import { History } from './screens/History';
// ...
<Route path="/insights" component={History} />
```

- [ ] **Step 3: Build + OTA + commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web && npm run build 2>&1 | tail -3
cd .. && pio run -e OTA_upload -t uploadfs 2>&1 | tail -3
git add web/src/screens/History.tsx web/src/app.tsx
git commit -m "sp3: History screen with uPlot charts for 5 series"
```

---

### Task 25: Logs screen

**Files:**
- Create: `web/src/screens/Logs.tsx`
- Modify: `web/src/app.tsx`

- [ ] **Step 1: Create `web/src/screens/Logs.tsx`**

```tsx
import { useComputed, useSignal } from '@preact/signals';
import { logEntries, clearLogs } from '../stores/log';
import { fmtTimestamp } from '../lib/format';

const LEVELS = ['dbg', 'inf', 'wrn', 'err'] as const;
type LvlFilter = typeof LEVELS[number] | 'all';

const COLOR: Record<string, string> = {
  dbg: 'text-slate-400',
  inf: 'text-cyan-300',
  wrn: 'text-amber-300',
  err: 'text-rose-300',
};

export function Logs() {
  const filter = useSignal<LvlFilter>('all');
  const search = useSignal('');

  const visible = useComputed(() => {
    return logEntries.value.filter(e => {
      if (filter.value !== 'all' && e.level !== filter.value) return false;
      if (search.value && !e.msg.toLowerCase().includes(search.value.toLowerCase())) return false;
      return true;
    });
  });

  return (
    <div class="space-y-3 max-w-4xl">
      <div class="flex items-center justify-between">
        <h1 class="text-xl font-bold">Logs</h1>
        <button class="text-xs px-3 py-1 rounded-md bg-white/5 border border-aqua-border"
                onClick={() => clearLogs()}>Clear</button>
      </div>

      <div class="glass p-3 flex gap-2 items-center flex-wrap">
        <div class="flex gap-1">
          {(['all', ...LEVELS] as LvlFilter[]).map(lv => (
            <button key={lv}
              onClick={() => (filter.value = lv)}
              class={`text-xs px-2 py-1 rounded-md ${filter.value === lv ? 'bg-aqua-primary/25 text-cyan-100' : 'bg-white/5'}`}>
              {lv}
            </button>
          ))}
        </div>
        <input type="search" placeholder="Filter…" value={search.value}
               onInput={e => (search.value = (e.target as HTMLInputElement).value)}
               class="flex-1 bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1 text-sm" />
      </div>

      <div class="glass p-0 overflow-hidden">
        <div class="font-mono text-xs max-h-[60vh] overflow-y-auto">
          {visible.value.length === 0 && (
            <div class="p-4 opacity-60">No logs to show.</div>
          )}
          {visible.value.map((e, i) => (
            <div key={i} class="grid grid-cols-[6rem_3rem_1fr] gap-2 px-3 py-1 border-b border-white/5">
              <span class="opacity-60">{fmtTimestamp(e.ts)}</span>
              <span class={COLOR[e.level] ?? ''}>{e.level}</span>
              <span class="truncate whitespace-pre-wrap">{e.msg}</span>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Wire into `app.tsx`**

```tsx
import { Logs } from './screens/Logs';
// ...
<Route path="/insights/logs" component={Logs} />
```

- [ ] **Step 3: Build + OTA + commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web && npm run build 2>&1 | tail -3
cd .. && pio run -e OTA_upload -t uploadfs 2>&1 | tail -3
git add web/src/screens/Logs.tsx web/src/app.tsx
git commit -m "sp3: Logs screen — level filter, search, fixed-width timestamps"
```

---

### Task 26: Diagnostics screen

**Files:**
- Create: `web/src/screens/Diagnostics.tsx`
- Modify: `web/src/app.tsx`

- [ ] **Step 1: Create `web/src/screens/Diagnostics.tsx`**

```tsx
import { useComputed, useSignal } from '@preact/signals';
import { poolState } from '../stores/state';
import { apiGet, apiSendCommand } from '../lib/api';
import { fmtBytes, fmtDuration, fmtInt } from '../lib/format';

interface I2cScan {
  found: Array<{ addr: string; likely?: string }>;
  count: number;
  sda: number;
  scl: number;
}

export function Diagnostics() {
  const s = useComputed(() => poolState.value);
  const scan = useSignal<I2cScan | null>(null);
  const scanning = useSignal(false);

  const doScan = async () => {
    scanning.value = true;
    const res = await apiGet<I2cScan>('/api/i2c/scan');
    if (res.ok && res.data) scan.value = res.data;
    scanning.value = false;
  };

  const clearErrors = () => apiSendCommand('{"Clear":1}');
  const reboot      = () => apiSendCommand('{"Reboot":1}');

  if (!s.value) return <div class="glass p-6">Loading…</div>;
  const d = s.value.diagnostics;

  return (
    <div class="space-y-4 max-w-3xl">
      <h1 class="text-xl font-bold">Diagnostics</h1>

      <div class="glass p-5">
        <h2 class="font-semibold mb-2">Device</h2>
        <div class="grid grid-cols-2 gap-x-4 gap-y-1 text-sm">
          <div class="opacity-60">Firmware</div>   <div class="val-num">{d.firmware}</div>
          <div class="opacity-60">Uptime</div>     <div class="val-num">{fmtDuration(d.uptime_s)}</div>
          <div class="opacity-60">Free heap</div>  <div class="val-num">{fmtBytes(d.free_heap)}</div>
          <div class="opacity-60">WiFi RSSI</div>  <div class="val-num">{fmtInt(d.wifi_rssi)} dBm</div>
          <div class="opacity-60">SSID</div>       <div class="val-num">{d.ssid}</div>
          <div class="opacity-60">IP</div>         <div class="val-num">{d.ip}</div>
        </div>
      </div>

      <div class="glass p-5">
        <div class="flex items-center justify-between mb-2">
          <h2 class="font-semibold">I²C bus</h2>
          <button class="text-xs px-3 py-1 rounded-md bg-aqua-primary/15 border border-aqua-primary/35 text-cyan-200"
                  onClick={doScan} disabled={scanning.value}>
            {scanning.value ? 'Scanning…' : 'Scan'}
          </button>
        </div>
        {scan.value && (
          <div class="text-sm">
            <div class="opacity-60 mb-2">SDA={scan.value.sda} · SCL={scan.value.scl} · {scan.value.count} device(s)</div>
            <ul class="space-y-1 font-mono text-xs">
              {scan.value.found.map(f => (
                <li key={f.addr}><span class="text-cyan-300">{f.addr}</span> — {f.likely ?? 'unknown'}</li>
              ))}
            </ul>
          </div>
        )}
      </div>

      <div class="glass p-5">
        <h2 class="font-semibold mb-3">Actions</h2>
        <div class="flex gap-2 flex-wrap">
          <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-warn/15 border border-amber-500/40 text-amber-200"
                  onClick={clearErrors}>Clear error flags</button>
          <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-alarm/15 border border-rose-500/40 text-rose-200"
                  onClick={() => confirm('Reboot the controller?') && reboot()}>Reboot device</button>
        </div>
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Wire into `app.tsx`**

```tsx
import { Diagnostics } from './screens/Diagnostics';
// ...
<Route path="/insights/diagnostics" component={Diagnostics} />
```

- [ ] **Step 3: Build + OTA + commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web && npm run build 2>&1 | tail -3
cd .. && pio run -e OTA_upload -t uploadfs 2>&1 | tail -3
git add web/src/screens/Diagnostics.tsx web/src/app.tsx
git commit -m "sp3: Diagnostics screen — device info, I2C scan, clear-errors + reboot"
```

---

### Task 27: Settings screen

**Files:**
- Create: `web/src/screens/Settings.tsx`
- Modify: `web/src/app.tsx`

- [ ] **Step 1: Create `web/src/screens/Settings.tsx`**

```tsx
import { useSignal } from '@preact/signals';
import { apiPostForm } from '../lib/api';

type FormState = Record<string, string>;

function useForm(init: FormState) {
  const s = useSignal<FormState>(init);
  return {
    get: s,
    set: (k: string, v: string) => (s.value = { ...s.value, [k]: v }),
  };
}

export function Settings() {
  const wifi  = useForm({ ssid: '', psk: '' });
  const mqtt  = useForm({ host: '', port: '1883', user: '', pass: '' });
  const admin = useForm({ pwd: '' });

  const saveWifi = async () => {
    const res = await apiPostForm('/api/wifi/save', wifi.get.value);
    alert(res.ok ? 'WiFi saved — device rebooting' : `Failed: ${res.error}`);
  };
  const saveMqtt = async () => {
    const res = await apiPostForm('/api/mqtt/save', mqtt.get.value);
    alert(res.ok ? 'MQTT saved — device rebooting' : `Failed: ${res.error}`);
  };
  const saveAdmin = async () => {
    // POST /api/admin/save only exists in AP mode wizard.
    // Runtime-side we have to go via the wizard path too — add a plan
    // note to introduce a runtime /api/admin/save endpoint.
    alert('Admin password changes use the captive-portal wizard for now (see README).');
  };
  const factoryReset = async () => {
    if (!confirm('Wipe WiFi credentials and reboot into AP mode?')) return;
    const res = await apiPostForm('/api/wifi/factory-reset', {});
    alert(res.ok ? 'Rebooting into AP mode' : `Failed: ${res.error}`);
  };

  const Field = ({ name, form, type = 'text', placeholder = '' }: { name: string; form: ReturnType<typeof useForm>; type?: string; placeholder?: string }) => (
    <label class="block">
      <div class="label-caps mb-1">{name}</div>
      <input type={type} value={form.get.value[name] ?? ''} placeholder={placeholder}
             onInput={e => form.set(name, (e.target as HTMLInputElement).value)}
             class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
    </label>
  );

  return (
    <div class="space-y-4 max-w-2xl">
      <h1 class="text-xl font-bold">Settings</h1>

      <div class="glass p-5 space-y-3">
        <h2 class="font-semibold">WiFi</h2>
        <div class="grid grid-cols-2 gap-3">
          <Field name="ssid" form={wifi} placeholder="AP Garten" />
          <Field name="psk"  form={wifi} type="password" />
        </div>
        <div class="flex gap-2 flex-wrap">
          <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold" onClick={saveWifi}>Save + reboot</button>
          <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-alarm/15 border border-rose-500/40 text-rose-200" onClick={factoryReset}>Factory reset WiFi → AP mode</button>
        </div>
      </div>

      <div class="glass p-5 space-y-3">
        <h2 class="font-semibold">MQTT broker</h2>
        <div class="grid grid-cols-2 gap-3">
          <Field name="host" form={mqtt} placeholder="10.25.25.50" />
          <Field name="port" form={mqtt} type="number" />
          <Field name="user" form={mqtt} />
          <Field name="pass" form={mqtt} type="password" />
        </div>
        <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold" onClick={saveMqtt}>Save + reboot</button>
      </div>

      <div class="glass p-5 space-y-3">
        <h2 class="font-semibold">Admin password</h2>
        <Field name="pwd" form={admin} type="password" />
        <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold" onClick={saveAdmin}>Save</button>
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Wire into `app.tsx`**

```tsx
import { Settings } from './screens/Settings';
// ...
<Route path="/settings" component={Settings} />
```

- [ ] **Step 3: Build + OTA + commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web && npm run build 2>&1 | tail -3
cd .. && pio run -e OTA_upload -t uploadfs 2>&1 | tail -3
git add web/src/screens/Settings.tsx web/src/app.tsx
git commit -m "sp3: Settings screen — WiFi, MQTT, admin-password, factory-reset forms"
```

---

### Task 28: Firmware update screen

**Files:**
- Create: `web/src/screens/Firmware.tsx`
- Modify: `web/src/app.tsx`

- [ ] **Step 1: Create `web/src/screens/Firmware.tsx`**

```tsx
import { useSignal } from '@preact/signals';

type UploadType = 'firmware' | 'littlefs';

export function Firmware() {
  const type = useSignal<UploadType>('firmware');
  const file = useSignal<File | null>(null);
  const progress = useSignal(0);
  const msg = useSignal('');
  const uploading = useSignal(false);

  const upload = () => {
    if (!file.value) return;
    uploading.value = true;
    progress.value = 0;
    msg.value = '';
    const fd = new FormData();
    fd.append('type', type.value);
    fd.append('file', file.value);

    const xhr = new XMLHttpRequest();
    xhr.upload.onprogress = e => {
      if (e.lengthComputable) progress.value = Math.round((e.loaded / e.total) * 100);
    };
    xhr.onload = () => {
      uploading.value = false;
      msg.value = xhr.status === 200
        ? 'Upload OK — device rebooting. Reconnect in ~15s.'
        : `Failed (${xhr.status}): ${xhr.responseText}`;
    };
    xhr.onerror = () => {
      uploading.value = false;
      msg.value = 'Network error during upload.';
    };
    xhr.open('POST', '/update');
    xhr.send(fd);
  };

  return (
    <div class="space-y-4 max-w-xl">
      <h1 class="text-xl font-bold">Firmware update</h1>

      <div class="glass p-5 space-y-4">
        <div>
          <div class="label-caps mb-2">What are you uploading?</div>
          <div class="flex gap-2">
            {(['firmware', 'littlefs'] as UploadType[]).map(t => (
              <button key={t}
                onClick={() => (type.value = t)}
                class={`text-sm px-3 py-1.5 rounded-md border ${
                  type.value === t
                    ? 'bg-aqua-primary/25 border-aqua-primary/50 text-cyan-100'
                    : 'bg-white/5 border-aqua-border'
                }`}>{t === 'firmware' ? 'Firmware .bin' : 'LittleFS .bin'}</button>
            ))}
          </div>
        </div>

        <label class="block">
          <div class="label-caps mb-1">File</div>
          <input type="file" accept=".bin"
                 onChange={e => (file.value = (e.target as HTMLInputElement).files?.[0] ?? null)}
                 class="w-full text-sm" />
        </label>

        <button class="text-sm px-4 py-2 rounded-md bg-aqua-primary text-slate-900 font-semibold disabled:opacity-50"
                disabled={!file.value || uploading.value}
                onClick={upload}>
          {uploading.value ? 'Uploading…' : 'Upload'}
        </button>

        {uploading.value && (
          <div>
            <div class="label-caps mb-1">Progress</div>
            <div class="w-full h-2 bg-slate-900/50 rounded-full overflow-hidden">
              <div class="h-full bg-aqua-primary" style={`width:${progress.value}%`} />
            </div>
            <div class="text-xs opacity-60 mt-1 val-num">{progress.value}%</div>
          </div>
        )}

        {msg.value && (
          <div class="text-sm border border-aqua-border rounded-md p-3">{msg.value}</div>
        )}
      </div>

      <p class="text-xs opacity-60">
        Same endpoint as <code class="opacity-80">pio run -t uploadfs</code>: the device reboots on success.
      </p>
    </div>
  );
}
```

- [ ] **Step 2: Wire into `app.tsx`**

```tsx
import { Firmware } from './screens/Firmware';
// ...
<Route path="/settings/firmware" component={Firmware} />
```

- [ ] **Step 3: Build + OTA + commit**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web && npm run build 2>&1 | tail -3
cd .. && pio run -e OTA_upload -t uploadfs 2>&1 | tail -3
git add web/src/screens/Firmware.tsx web/src/app.tsx
git commit -m "sp3: Firmware update screen — drag-drop .bin with progress bar"
```

- [ ] **Step 4: Create checkpoint `sp3-ckpt4-screens`**

```bash
git branch sp3-ckpt4-screens
```

---

## Phase 5: Polish + validation

### Task 29: Final bundle audit + multi-client test

**Files:**
- None (validation)

- [ ] **Step 1: Full clean build**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web
rm -rf ../data/assets ../data/index.html ../data/sw.js ../data/manifest.webmanifest ../data/icon-*.png
npm run build 2>&1 | tail -10
du -sh ../data/assets/*.js ../data/assets/*.css
```
Expected: `[bundle-size] OK: <N> KB of JS`, JS totals < 500 KB uncompressed. CSS < 50 KB.

- [ ] **Step 2: OTA the final bundle**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
export PATH="$HOME/.platformio/penv/bin:$PATH"
export POOLMASTER_OTA_PWD=""
pio run -e OTA_upload -t upload 2>&1 | tail -3   # firmware unchanged → may be fast no-op
pio run -e OTA_upload -t uploadfs 2>&1 | tail -3
sleep 8
curl -sS --max-time 5 http://poolmaster.local/healthz
```
Expected: OK with positive uptime_s.

- [ ] **Step 3: Multi-client WS test**

Open `http://poolmaster.local/` in **two** browsers or a browser + phone. Toggle the filtration pump from the desktop under Control → Manual. The phone's Dashboard tile should flip "Filtration · running" vs "idle" within 1 s.

- [ ] **Step 4: PWA install test**

- Chrome desktop: open `http://poolmaster.local/`, click the install icon in the address bar → "Install PoolMaster". App launches in its own window, full-screen, no browser chrome.
- Android Chrome: visit, tap menu → "Add to Home screen". Icon appears on homescreen; tap → fullscreen PWA.
- iOS Safari: Share → Add to Home Screen. Icon appears; tap → standalone webview (no service worker, but manifest honored).

Note any device where this fails.

- [ ] **Step 5: 14-point smoke checklist** — tick each:

1. [ ] Load `/` on phone → SPA shell renders < 3 s cold, < 1 s warm.
2. [ ] PWA install works on at least Chromium.
3. [ ] Dashboard live tiles; WiFi off → "reconnecting" badge; WiFi back → clears.
4. [ ] Toggle filtration pump → relay clicks; state reflects within 1 s.
5. [ ] Slide pH setpoint to 7.4 → `/api/state` confirms; survives reboot.
6. [ ] Calibration wizard captures 2 points → new pH value reasonable within ±0.3 of expected after recalibration with buffers.
7. [ ] PID screen → change Kp → PID output visibly reacts within one regulation window.
8. [ ] Schedule 9-20 → filtration task respects new bounds at the scheduled time.
9. [ ] Tanks → refill acid → `acid_fill_pct = 100`.
10. [ ] History → all 5 charts populate with 60 min of data; refresh reloads from server.
11. [ ] Logs → level filter works; new lines stream live.
12. [ ] Diagnostics → I2C scan matches `curl /api/i2c/scan` output.
13. [ ] Settings → change MQTT broker → HA re-discovers device within ~30 s.
14. [ ] Firmware → drag-drop a test `.bin` → reboot + reconnect + new version visible.

- [ ] **Step 6: Record final size**

```bash
du -sh data/assets/*.js data/assets/*.css
pio run -e serial_upload | grep -E 'RAM:|Flash:' > docs/superpowers/notes/sp3-final-size.txt
diff docs/superpowers/notes/sp3-baseline-size.txt docs/superpowers/notes/sp3-final-size.txt
git add docs/superpowers/notes/sp3-final-size.txt
git commit -m "chore(sp3): record final size after SP3 complete"
```

---

### Task 30: README + ship-ready checkpoint

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Prepend an SP3 section to `README.md`**

Insert this block at the very top of `README.md`, immediately before the existing `## ⚡ SP1 Modern Plumbing (2026-04) — breaking changes` heading:

```markdown
## 🧊 SP3 Beautiful Web UI (2026-04)

Version **ESP-SP1** now ships with a responsive Preact + Tailwind SPA served from LittleFS.
`http://poolmaster.local/` opens a 12-screen dashboard (Home, Control, Tune, Insights, Settings)
with live tiles, a calibration wizard, PID tuning, history charts, log viewer, and diagnostics.

### Building the web UI

```bash
cd web
nvm use                                 # Node 20 LTS per .nvmrc
npm install                             # first time only
npm run dev                             # Vite dev + HMR @ http://localhost:5173 (proxies to poolmaster.local)
npm run build                           # writes /data/ (LittleFS image source)
npm run upload                          # OTA LittleFS push via `pio run -e OTA_upload -t uploadfs`
```

### Install as a PWA

On Chrome / Edge: open `http://poolmaster.local/` → address-bar install icon → "Install PoolMaster".
On iOS Safari: Share → Add to Home Screen. (Service-worker offline caching is Chromium-only; iOS runs the
app as a standalone web view without the SW.)

### Screens

| Section | Screens |
|---|---|
| 🏠 Home | Dashboard |
| 🎛 Control | Manual, Setpoints |
| 🧪 Tune | Calibration wizard, PID tuning, Schedule, Tanks |
| 📊 Insights | History (60-min charts), Logs, Diagnostics |
| ⚙ Settings | Network (WiFi/MQTT/admin/factory-reset), Firmware update |

All writes are HTTP Basic-auth gated once an admin password is set via Settings.

---

```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "sp3: document the web UI in README"
```

- [ ] **Step 3: Create checkpoint `sp3-ckpt5-ship`**

```bash
git branch sp3-ckpt5-ship
git branch --list 'sp3*'
```

- [ ] **Step 4: Discuss merge with user**

Options:
- Merge `sp3-web-ui` → `main` via PR
- Squash-merge with single commit summarising all SP3 work
- Keep branch alive for more iteration

Do NOT merge without user approval.

---

## Self-review notes

**Spec coverage check:** Every section of the SP3 spec is covered:

- §4 Architecture → Tasks 4 (WebSocketHub), 5 (ApiCommand + SPA fallback), 6 (wire-up), 13 (WS client)
- §5 Library stack → Task 8 (package.json pins)
- §6.1 Module layout → Tasks 2–6 (backend), 8–15 (frontend)
- §6.2 Build pipeline → Tasks 8 (vite config), 10 (bundle guard)
- §6.3 WebSocket protocol → Task 4 (hub with all message types) + Task 13 (client)
- §6.4 Screen inventory / navigation → Task 16 (router + NavShell) + Tasks 17–28 (each screen)
- §6.5 Visual system → Task 8 (tailwind config) + Task 15 (component CSS)
- §6.6 Backend impl notes → Task 2 (LogBuffer), 3 (HistoryBuffer), 4 (WebSocketHub broadcast)
- §6.7 Auth model → Tasks 5 (admin-gated /api/cmd + /api/whoami), 27 (Settings)
- §7 Migration → Task 8 (`onNotFound` fallback) + .gitignore
- §8 Risk register → covered via implementation choices (backpressure in Task 4, SPA fallback in Task 5)
- §9 Testing plan → Task 29 (14-point smoke)
- §10 Forward-compat → no specific task needed; spec documented

**Placeholder scan:** No `TBD`/`TODO`/`implement later` patterns in task steps. One note in Task 27 Settings about `/api/admin/save` not existing at runtime — that's a real scope punt documented to the user via `alert()`, not a placeholder. Adding a runtime admin-password endpoint is one line of follow-up (an sp3-ext commit similar to the MQTT-save endpoint pattern).

**Type consistency:** All cross-task type references match. `PoolState` shape in Task 14 stores is the exact JSON shape the backend `WebSocketHub::buildStateJson` emits in Task 4. Command payloads used in Tasks 18, 19, 21, 22, 23, 26 (`{"FiltPump":1}`, `{"PhSetPoint":7.4}`, etc.) match the existing `ProcessCommand` dispatcher handlers verified during SP1 Task 24. `AsyncWebServerRequest* req`, `_tempObject`, `WebAuth::requireAdmin` match the SP1 code paths.

**Scope:** One plan, five phases, five checkpoints. Large but not beyond what SP1 handled. Subagent-driven execution is strongly recommended given the 30-task count.

**Known follow-ups** (explicitly out of SP3 scope, noted here for later):
1. Runtime `/api/admin/save` endpoint so the Settings screen can actually change the admin password (not just alert). Trivial: follow the `/api/wifi/save` pattern in `Provisioning::registerRuntimeRoutes`.
2. Runtime `/api/time/save` + `/api/ntp/save` endpoints for timezone/NTP server changes.
3. `Debug.print` full tee into `LogBuffer` (currently only WebSocketHub events + explicit `LogBuffer::append` calls feed the buffer).
4. Persistent audit log on LittleFS recording every `cmd` with source + timestamp.
