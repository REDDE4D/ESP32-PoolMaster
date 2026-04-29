# SP1 Modern Plumbing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Modernize the ESP32-PoolMaster firmware plumbing: bump dependencies, add an HTTP server with auth, add web-based OTA, replace first-run USB-flashing with a captive-portal setup wizard, and switch MQTT to Home Assistant Discovery — all without touching pool control logic or the Nextion display firmware.

**Architecture:** AsyncWebServer on :80 backed by LittleFS for static assets. espMqttClient replaces AsyncMqttClient; ArduinoJson v7 replaces v6. A new captive-portal Provisioning module runs in AP mode when no WiFi credentials are in NVS. Home Assistant Discovery publishes one device with ~35 entities on MQTT connect. REST and MQTT both feed the existing `queueIn` → `ProcessCommand` task, so no business logic is duplicated. The four new network-facing modules (WebServer, OtaService, Provisioning, HaDiscovery) are additive; existing pool-control tasks are unchanged.

**Tech Stack:** ESP32 Arduino core 2.0.17 (PlatformIO `espressif32 @ 6.10.0`), ESP32Async/ESPAsyncWebServer + AsyncTCP forks, bertmelis/espMqttClient, ArduinoJson 7, LittleFS, Preferences (NVS), mbedTLS SHA-256. Existing Nextion, PID, DallasTemperature, RunningMedian, Pump-master libraries are preserved.

**Spec reference:** [docs/superpowers/specs/2026-04-22-sp1-modern-plumbing-design.md](../specs/2026-04-22-sp1-modern-plumbing-design.md)

**Out of scope:** Nextion HMI changes (SP5), any UI beyond the placeholder and wizard (SP3), RelayOutput abstraction (SP4), promoting remaining `Config.h` fields to NVS (SP2), automated unit tests.

---

## Prerequisites

This plan assumes:
- Working directory is `/Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster`.
- PlatformIO Core is installed (`pio --version` works).
- The ADS1115 library symlink at `../../libraries/ADS1115` (referenced in `platformio.ini`) remains available.
- A physical ESP32 DevKit V1 is available for bench smoke tests; a mosquitto broker is reachable on the LAN for HA Discovery validation.

---

## File Structure Overview

Before/after at a glance:

```
src/
├── Setup.cpp              (modified: slimmed — boot orchestration only)
├── Settings.cpp           (NEW: loadConfig/saveConfig/saveParam extracted)
├── WiFiService.cpp        (NEW: WiFi events, reconnect timers, AP fallback trigger)
├── Provisioning.cpp       (NEW: AP mode, DNSServer, wizard POST handlers)
├── OtaService.cpp         (NEW: ArduinoOTA + /update HTTP handler)
├── WebServer.cpp          (NEW: AsyncWebServer, route registration)
├── WebAuth.cpp            (NEW: SHA-256 password check, Basic auth helper)
├── Credentials.cpp        (NEW: NVS accessors for wifi/mqtt/admin/ota)
├── HaDiscovery.cpp        (NEW: HA Discovery config publisher)
├── MqttBridge.cpp         (NEW: was mqtt_comm.cpp, now espMqttClient + HA translation)
├── CommandQueue.cpp       (RENAMED from PoolServer.cpp, JSON v7 migration)
├── MqttPublish.cpp        (RENAMED from Publish.cpp, HA state-topic rewrite)
├── Loops.cpp              (modified: JSON v7 + minor lib bumps)
├── PoolMaster.cpp         (modified: JSON v7 only)
└── Nextion.cpp            (UNTOUCHED — SP1 boundary)

include/
├── Config.h               (modified: secrets removed, compile-time constants stay)
├── Secrets.h.example      (NEW: template)
├── Secrets.h              (NEW, gitignored: optional dev fallback)
└── PoolMaster.h           (modified: espMqttClient include, ArduinoJson v7)

data/                       (NEW: LittleFS source)
├── index.html             (placeholder SP3 will replace)
├── setup.html             (~15 KB vanilla wizard)
├── setup.css              (~2 KB minimal styling)
└── robots.txt             (Disallow: /)

partitions.csv              (NEW: 2×1.44 MB app + 1 MB LittleFS + coredump)
platformio.ini              (modified: new libs, FS, partition table)
.gitignore                  (modified: add Secrets.h)
README.md                   (modified: first-boot + OTA workflow)
```

---

## Phase 1: Foundation

### Task 1: Create feature branch and verify baseline compile

**Files:**
- No code changes this task.

- [ ] **Step 1: Create feature branch**

Run:
```bash
git checkout -b sp1-modern-plumbing
```
Expected: `Switched to a new branch 'sp1-modern-plumbing'`.

- [ ] **Step 2: Verify baseline compiles on current dependencies**

Run:
```bash
pio run -e serial_upload
```
Expected: build succeeds. If it fails, stop and resolve before proceeding — every later task assumes a clean starting state.

- [ ] **Step 3: Record baseline binary size**

Run:
```bash
pio run -e serial_upload | grep -E 'RAM:|Flash:' | tee /tmp/sp1-baseline-size.txt
```
Expected: two lines showing `RAM:` and `Flash:` percentages. File saved for later comparison (last task re-checks size hasn't ballooned).

- [ ] **Step 4: Commit the baseline note to the branch**

Run:
```bash
mkdir -p docs/superpowers/notes
mv /tmp/sp1-baseline-size.txt docs/superpowers/notes/sp1-baseline-size.txt
git add docs/superpowers/notes/sp1-baseline-size.txt
git commit -m "chore(sp1): record baseline binary size before SP1 work"
```

---

### Task 2: Partition table, LittleFS, and placeholder assets

**Files:**
- Create: `partitions.csv`
- Create: `data/index.html`
- Create: `data/robots.txt`
- Modify: `platformio.ini` (remove `-D DISABLE_FLASH`, add FS + partition lines)

- [ ] **Step 1: Create `partitions.csv`**

Create file `partitions.csv`:
```
# Name,   Type, SubType,  Offset,   Size,      Flags
nvs,      data, nvs,      0x9000,   0x5000
otadata,  data, ota,      0xe000,   0x2000
app0,     app,  ota_0,    0x10000,  0x170000
app1,     app,  ota_1,    0x180000, 0x170000
spiffs,   data, spiffs,   0x2f0000, 0x100000
coredump, data, coredump, 0x3f0000, 0x10000
```

- [ ] **Step 2: Create `data/index.html` placeholder**

Create file `data/index.html`:
```html
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>PoolMaster</title>
    <style>
      body { font: 16px system-ui, sans-serif; margin: 2rem auto; max-width: 36rem; color: #1e293b; }
      h1 { font-size: 1.4rem; margin-bottom: 0.25rem; }
      p.hint { color: #64748b; }
      code { background: #e2e8f0; padding: 0.1rem 0.3rem; border-radius: 0.2rem; }
    </style>
  </head>
  <body>
    <h1>PoolMaster is alive.</h1>
    <p>Firmware modern-plumbing stage. The full dashboard ships in SP3.</p>
    <p class="hint">REST API and WebSocket endpoints land in SP3. For now, Home Assistant Discovery publishes all controllable entities on MQTT.</p>
    <p>Admin actions:</p>
    <ul>
      <li><a href="/update">Firmware update</a> (requires admin password)</li>
    </ul>
  </body>
</html>
```

- [ ] **Step 3: Create `data/robots.txt`**

Create file `data/robots.txt`:
```
User-agent: *
Disallow: /
```

- [ ] **Step 4: Update `platformio.ini` with new platform + FS + partitions**

Replace the full contents of `platformio.ini` with:
```ini
; PlatformIO Project Configuration File

[platformio]
default_envs = serial_upload

[env]
platform = espressif32 @ 6.10.0   ; pins ESP32 Arduino core 2.0.17
board = esp32doit-devkit-v1
framework = arduino

board_build.filesystem = littlefs
board_build.partitions = partitions.csv

monitor_speed = 115200
monitor_filters = esp32_exception_decoder

lib_ldf_mode = chain
build_flags = -D ESP32_DEVKITV1
              -D DISABLE_IMAP
              -D DISABLE_SD
              -D DISABLE_NTP_TIME
              -D SILENT_MODE

lib_deps =
    symlink://../../libraries/ADS1115

    Arduino_DebugUtils @ ^1.4.0
    paulstoffregen/Time @ ^1.6
    robtillaart/RunningMedian @ ^0.3.9
    br3ttb/PID @ ^1.2.1
    bblanchon/ArduinoJson @ ^7.2.0
    seithan/Easy Nextion Library @ ^1.0.6
    milesburton/DallasTemperature @ ^3.9.1
    ESP32Async/AsyncTCP @ ^3.2.0
    ESP32Async/ESPAsyncWebServer @ ^3.3.0
    bertmelis/espMqttClient @ ^1.7.0
    mobizt/ESP Mail Client @ ^3.4.19

[env:serial_upload]
debug_tool = esp-prog
debug_init_break = tbreak setup
build_type = debug
lib_deps = ${env.lib_deps}
build_flags =
    ${env.build_flags}
    -D DEVT
upload_protocol = esptool

[env:OTA_upload]
lib_deps = ${env.lib_deps}
build_flags =
    ${env.build_flags}
upload_protocol = espota
upload_port = poolmaster.local
upload_flags =
    --port=3232
    --auth=${sysenv.POOLMASTER_OTA_PWD}
```

Note: `DISABLE_FLASH` removed (LittleFS now used). `OTA_PORT 8063` → core default `3232`. OTA password now comes from env var at upload time rather than a baked macro.

- [ ] **Step 5: Trigger PlatformIO to install new libraries**

Run:
```bash
pio pkg install -e serial_upload
```
Expected: downloads + installs ESPAsyncWebServer, AsyncTCP (ESP32Async forks), espMqttClient, ArduinoJson 7. Existing libs update to pinned versions.

- [ ] **Step 6: Verify it still compiles (it won't — expected)**

Run:
```bash
pio run -e serial_upload 2>&1 | tail -40
```
Expected: **compile failures**. `AsyncMqttClient.h` no longer resolves, `StaticJsonDocument<>` is deprecated, etc. These are fixed across Phases 2–4. Do NOT try to fix them individually in this task.

- [ ] **Step 7: Commit**

Run:
```bash
git add partitions.csv data/ platformio.ini
git commit -m "sp1: add partitions.csv, LittleFS data dir, bump platform and libs

Switches to ESP32Async forks of AsyncTCP/ESPAsyncWebServer, swaps
AsyncMqttClient for bertmelis/espMqttClient, bumps ArduinoJson to v7.
Compile will fail until the code migration in Phases 2-4 completes."
```

---

### Task 3: Introduce `Secrets.h` scaffolding

**Files:**
- Create: `include/Secrets.h.example`
- Create: `include/Secrets.h` (gitignored)
- Modify: `include/Config.h` (remove hardcoded secrets, `#include "Secrets.h"` instead)
- Modify: `.gitignore` (add `include/Secrets.h`)

- [ ] **Step 1: Create `include/Secrets.h.example`**

Create file `include/Secrets.h.example`:
```cpp
#pragma once
// Copy this file to include/Secrets.h and fill in your own values.
// Secrets.h is gitignored.
//
// These values are OPTIONAL. On first boot, the firmware will start in AP
// mode and the setup wizard will write credentials to NVS. These defines
// only serve as a one-time seed for developer flashing convenience —
// if NVS already has credentials, NVS wins.

#define SEED_WIFI_SSID        ""
#define SEED_WIFI_PSK         ""

#define SEED_MQTT_HOST        ""      // e.g. "192.168.1.50" ; empty disables MQTT until wizard runs
#define SEED_MQTT_PORT        1883
#define SEED_MQTT_USER        ""
#define SEED_MQTT_PASS        ""

#define SEED_ADMIN_PWD        ""      // plaintext; firmware hashes it on first save

// Mail parameters (legacy SMTP alerting) — optional
#define SEED_SMTP_HOST        ""
#define SEED_SMTP_PORT        587
#define SEED_AUTHOR_EMAIL     ""
#define SEED_AUTHOR_LOGIN     ""
#define SEED_AUTHOR_PASSWORD  ""
#define SEED_RECIPIENT_EMAIL  ""
```

- [ ] **Step 2: Create `include/Secrets.h`** (developer fill)

Create file `include/Secrets.h`:
```cpp
#pragma once
// Local developer secrets. Gitignored. See Secrets.h.example for schema.

#define SEED_WIFI_SSID        ""
#define SEED_WIFI_PSK         ""
#define SEED_MQTT_HOST        ""
#define SEED_MQTT_PORT        1883
#define SEED_MQTT_USER        ""
#define SEED_MQTT_PASS        ""
#define SEED_ADMIN_PWD        ""
#define SEED_SMTP_HOST        ""
#define SEED_SMTP_PORT        587
#define SEED_AUTHOR_EMAIL     ""
#define SEED_AUTHOR_LOGIN     ""
#define SEED_AUTHOR_PASSWORD  ""
#define SEED_RECIPIENT_EMAIL  ""
```

- [ ] **Step 3: Update `.gitignore`**

Append to `.gitignore` (create the file if it doesn't exist):
```
include/Secrets.h
docs/superpowers/notes/*.local
```

- [ ] **Step 4: Rewrite `include/Config.h` to strip secrets**

Replace the entire contents of `include/Config.h` with:
```cpp
#pragma once
#include "Secrets.h"

// Firmware revisions
#define FIRMW "ESP-SP1"
#define TFT_FIRMW "TFT-2.0"

#define DEBUG_LEVEL DBG_INFO

// NVS config version — bump to force defaults on next boot
#define CONFIG_VERSION 51

#ifdef DEVT
  #define HOSTNAME "PoolMaster_Dev"
#else
  #define HOSTNAME "PoolMaster"
#endif

// ---- SMTP (optional alerting) ------------------------------------
#if !(__has_include("Secrets.h"))
  #error "Create include/Secrets.h from Secrets.h.example first."
#endif
#define EMAIL_ALERT
#define SMTP_HOST        SEED_SMTP_HOST
#define SMTP_PORT        SEED_SMTP_PORT
#define AUTHOR_EMAIL     SEED_AUTHOR_EMAIL
#define AUTHOR_LOGIN     SEED_AUTHOR_LOGIN
#define AUTHOR_PASSWORD  SEED_AUTHOR_PASSWORD
#define RECIPIENT_EMAIL  SEED_RECIPIENT_EMAIL

// ---- PID direction -----------------------------------------------
#define PhPID_DIRECTION REVERSE
#define OrpPID_DIRECTION DIRECT

// ---- GPIO pinout -------------------------------------------------
#define FILTRATION_PUMP 32
#define ROBOT_PUMP      33
#define PH_PUMP         25
#define CHL_PUMP        26
#define RELAY_R0        27
#define RELAY_R1         4
#define CHL_LEVEL       39
#define PH_LEVEL        36
#define ONE_WIRE_BUS_A  18
#define ONE_WIRE_BUS_W  19
#define I2C_SDA         21
#define I2C_SCL         22
#define PCF8574ADDRESS  0x38
#define BUZZER          2

// ---- Sensor acquisition config -----------------------------------
#define EXT_ADS1115
#define INT_ADS1115_ADDR ADS1115ADDRESS
#define EXT_ADS1115_ADDR ADS1115ADDRESS+1

#define WDT_TIMEOUT     10

#define TEMPERATURE_RESOLUTION 12

// ---- MQTT state-publish cadence ----------------------------------
#define PUBLISHINTERVAL 30000

#ifdef DEVT
  #define POOLTOPIC_LEGACY "Home/Pool6/"
#else
  #define POOLTOPIC_LEGACY "Home/Pool/"
#endif

// ---- Robot pump timing -------------------------------------------
#define ROBOT_DELAY    60
#define ROBOT_DURATION 90

// ---- Nextion display timeout -------------------------------------
#define TFT_SLEEP 60000L

// ---- FreeRTOS task scheduling ------------------------------------
#define PT1 125
#define PT2 500
#define PT3 500
#define PT4 (1000 / (1 << (12 - TEMPERATURE_RESOLUTION)))
#define PT5 1000
#define PT6 1000
#define PT7 3000
#define PT8 30000

#define DT2 (190 / portTICK_PERIOD_MS)
#define DT3 (310 / portTICK_PERIOD_MS)
#define DT4 (440 / portTICK_PERIOD_MS)
#define DT5 (560 / portTICK_PERIOD_MS)
#define DT6 (920 / portTICK_PERIOD_MS)
#define DT7 (100 / portTICK_PERIOD_MS)
#define DT8 (570 / portTICK_PERIOD_MS)
#define DT9 (940 / portTICK_PERIOD_MS)
```

Notes:
- Firmware version bumped to `ESP-SP1` so deployments clearly identify SP1 vs. legacy builds.
- `WIFI_NETWORK`, `WIFI_PASSWORD`, `OTA_PWDHASH`, `MQTT_SERVER_IP`, `MQTT_SERVER_PORT`, `MQTT_LOGIN`, `SERVER_PORT`, `OTA_PORT` all **removed**. Their replacements come from NVS (Task 14) via the `Credentials` module.
- `POOLTOPIC` renamed to `POOLTOPIC_LEGACY` because it's only used for the one-shot retained-topic cleanup sweep in Task 24. New topics live under `poolmaster/<mac>/...`.

- [ ] **Step 5: Commit**

Run:
```bash
git add include/Secrets.h.example include/Config.h .gitignore
git commit -m "sp1: move WiFi/MQTT/OTA secrets from Config.h to Secrets.h

Hardcoded credentials are removed from tracked source. Secrets.h (git-
ignored) provides optional seed values; runtime values come from NVS
(Task 14). Firmware version bumped to ESP-SP1 to mark the boundary."
```

Note: `include/Secrets.h` itself is gitignored so it will NOT be staged.

---

## Phase 2: ArduinoJson v6 → v7 migration

### Task 4: Update `PoolMaster.h` includes and storage type

**Files:**
- Modify: `include/PoolMaster.h`

ArduinoJson v7 renames:
- `StaticJsonDocument<N>` / `DynamicJsonDocument(N)` → `JsonDocument` (capacity-free)
- Calls `root.createNestedObject(...)` / `root.createNestedArray(...)` → `root[key].to<JsonObject>()` / `root[key].to<JsonArray>()`
- `JSON_OBJECT_SIZE(N)` macro no longer needed

- [ ] **Step 1: Swap the MQTT client forward-declaration**

In `include/PoolMaster.h`, find line 22:
```cpp
#include "AsyncMqttClient.h"      // Async. MQTT client
```
Replace with:
```cpp
#include <espMqttClient.h>        // Modern async MQTT 3.1.1/5 client
```

- [ ] **Step 2: Swap the MQTT client extern**

In `include/PoolMaster.h`, find line 64:
```cpp
extern AsyncMqttClient mqttClient;                     // MQTT async. client
```
Replace with:
```cpp
extern espMqttClientAsync mqttClient;                  // MQTT async. client (espMqttClient)
```

- [ ] **Step 3: Commit**

Run:
```bash
git add include/PoolMaster.h
git commit -m "sp1: point PoolMaster.h at espMqttClient and v7 JSON (header only)"
```

Note: this WILL break compilation; remaining breakage is fixed in subsequent tasks.

---

### Task 5: Migrate `src/Publish.cpp` JSON to v7 and publish signature

**Files:**
- Modify: `src/Publish.cpp`

Existing file uses `StaticJsonDocument<capacity>` + `mqttClient.publish(topic, qos, retain, payload, len)`. espMqttClient's publish signature is `publish(topic, qos, retain, payload)` (length auto-derived when passing a C-string) — same order, length arg dropped.

- [ ] **Step 1: Replace the `PublishTopic` helper**

In `src/Publish.cpp`, find lines 56–69 (the `PublishTopic` helper) and replace with:
```cpp
void PublishTopic(const char* topic, JsonDocument& root)
{
  char Payload[PAYLOAD_BUFFER_LENGTH];
  size_t n = serializeJson(root, Payload, sizeof(Payload));
  if (mqttClient.publish(topic, 1, true, Payload) != 0)
  {
    Debug.print(DBG_DEBUG, "Publish: %s - size: %d/%d", Payload, root.size(), n);
  }
  else
  {
    Debug.print(DBG_DEBUG, "Unable to publish: %s", Payload);
  }
}
```

- [ ] **Step 2: Migrate every `StaticJsonDocument<capacity>` declaration**

In `src/Publish.cpp`, find every occurrence of:
```cpp
const int capacity = JSON_OBJECT_SIZE(N) + M;   // N and M vary
StaticJsonDocument<capacity> root;
```
Replace each with:
```cpp
JsonDocument root;
```

There are six such blocks: lines ~101–102, 121–122, 141–142, 159–160, 181–182, 275–276, 296–297 (two in MeasuresPublish). Replace all; no other logic changes.

- [ ] **Step 3: Compile-check this file in isolation**

Run:
```bash
pio run -e serial_upload 2>&1 | grep -E 'Publish.cpp' | head -20
```
Expected: no errors referencing `Publish.cpp` (errors in other files may still appear — that's fine).

- [ ] **Step 4: Commit**

Run:
```bash
git add src/Publish.cpp
git commit -m "sp1: migrate Publish.cpp JSON to ArduinoJson v7"
```

---

### Task 6: Migrate `src/PoolServer.cpp` JSON to v7

**Files:**
- Modify: `src/PoolServer.cpp`

This file is the JSON command dispatcher — ~26 KB with many `StaticJsonDocument` usages and one or two `createNestedArray` calls.

- [ ] **Step 1: Replace every `StaticJsonDocument<...>` declaration**

Run this one-shot transformation from the repo root:
```bash
# Preview first
grep -nE 'StaticJsonDocument|DynamicJsonDocument|JSON_OBJECT_SIZE|createNestedObject|createNestedArray' src/PoolServer.cpp
```

- [ ] **Step 2: Apply the mechanical replacements**

For each line matching `StaticJsonDocument<...> <name>;` or similar, edit the file manually (or with sed) to:
- Replace the whole declaration with `JsonDocument <name>;`
- Delete any preceding `const int capacity = JSON_OBJECT_SIZE(...);` helper line.

For any `root.createNestedObject("key")` calls, replace with `root["key"].to<JsonObject>()`.
For any `root.createNestedArray("key")` calls, replace with `root["key"].to<JsonArray>()`.
For any `auto arr = arr.createNestedArray()` inside a loop, replace with `arr.add<JsonObject>()`.

- [ ] **Step 3: Compile-check this file**

Run:
```bash
pio run -e serial_upload 2>&1 | grep 'PoolServer' | head -40
```
Expected: no JSON-related errors in PoolServer.cpp. Remaining errors from other files are fine.

- [ ] **Step 4: Commit**

Run:
```bash
git add src/PoolServer.cpp
git commit -m "sp1: migrate PoolServer.cpp JSON to ArduinoJson v7"
```

---

### Task 7: Migrate `src/Nextion.cpp` and `src/PoolMaster.cpp` and any remaining JSON sites

**Files:**
- Modify: `src/Nextion.cpp` (if any JSON uses exist)
- Modify: `src/PoolMaster.cpp` (if any JSON uses exist)
- Modify: `src/Loops.cpp` (if any JSON uses exist)
- Modify: `src/mqtt_comm.cpp` (for the queue decode, if any JSON uses exist)

- [ ] **Step 1: Find remaining JSON call sites**

Run:
```bash
grep -nRE 'StaticJsonDocument|DynamicJsonDocument|JSON_OBJECT_SIZE|createNestedObject|createNestedArray' src/ include/
```

- [ ] **Step 2: Apply identical transformations as in Tasks 5–6 to each remaining file**

For each file reported, apply the pattern:
- `StaticJsonDocument<N> foo;` → `JsonDocument foo;`
- `DynamicJsonDocument foo(N);` → `JsonDocument foo;`
- `root.createNestedObject("k")` → `root["k"].to<JsonObject>()`
- `root.createNestedArray("k")` → `root["k"].to<JsonArray>()`
- Delete any `const int capacity = JSON_OBJECT_SIZE(...)` helper variables.

- [ ] **Step 3: Re-run the grep to confirm zero remaining matches**

Run:
```bash
grep -nRE 'StaticJsonDocument|DynamicJsonDocument|JSON_OBJECT_SIZE|createNestedObject|createNestedArray' src/ include/
```
Expected: no output.

- [ ] **Step 4: Commit**

Run:
```bash
git add -u src/ include/
git commit -m "sp1: migrate remaining JSON call sites to ArduinoJson v7"
```

---

## Phase 3: Async MQTT library swap

### Task 8: Rewrite `src/mqtt_comm.cpp` against espMqttClient

**Files:**
- Modify: `src/mqtt_comm.cpp`

espMqttClient has a similar event model but different callback signatures and requires `setServer(host, port)` takes `IPAddress` OR `const char*` — we'll use `const char*` so the hostname from NVS works. Command-topic subscription is unchanged in behavior.

- [ ] **Step 1: Replace entire contents of `src/mqtt_comm.cpp`**

Replace the entire file with:
```cpp
// MQTT + WiFi glue — bridged to espMqttClient. Will be split into
// WiFiService.cpp + MqttBridge.cpp in Task 10.

#undef __STRICT_ANSI__
#include <Arduino.h>
#include <espMqttClient.h>
#include "Config.h"
#include "PoolMaster.h"
#include "Credentials.h"   // introduced in Task 14; forward-declared here

espMqttClientAsync mqttClient;

bool MQTTConnection = false;
static TimerHandle_t mqttReconnectTimer;
static TimerHandle_t wifiReconnectTimer;

// Legacy command topic (removed in Task 24's HA cutover; kept compiling for now).
static const char* PoolTopicAPI_Legacy    = POOLTOPIC_LEGACY "API";
static const char* PoolTopicStatus_Legacy = POOLTOPIC_LEGACY "Status";

void initTimers(void);
void mqttInit(void);
void connectToWiFi(void);
void reconnectToWiFi(void);
void connectToMqtt(void);
void WiFiEvent(WiFiEvent_t);
void UpdateWiFi(bool);
int  freeRam(void);

static void onMqttConnect(bool sessionPresent);
static void onMqttDisconnect(espMqttClientTypes::DisconnectReason reason);
static void onMqttMessage(const espMqttClientTypes::MessageProperties& properties,
                          const char* topic,
                          const uint8_t* payload,
                          size_t len, size_t index, size_t total);

void initTimers()
{
  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0,
                                    reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0,
                                    reinterpret_cast<TimerCallbackFunction_t>(reconnectToWiFi));
}

void mqttInit()
{
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onMessage(onMqttMessage);

  const String host = Credentials::mqttHost();
  if (host.isEmpty()) {
    Debug.print(DBG_INFO, "[MQTT] No broker configured — bridge idle");
    return;
  }
  mqttClient.setServer(host.c_str(), Credentials::mqttPort());

  const String user = Credentials::mqttUser();
  const String pass = Credentials::mqttPass();
  if (!user.isEmpty()) {
    mqttClient.setCredentials(user.c_str(), pass.c_str());
  }

  static char willTopic[64];
  snprintf(willTopic, sizeof(willTopic), "poolmaster/%s/avail", Credentials::deviceId().c_str());
  mqttClient.setWill(willTopic, 1, true, "offline");
}

void connectToWiFi()
{
  const String ssid = Credentials::wifiSsid();
  const String psk  = Credentials::wifiPsk();
  if (ssid.isEmpty()) {
    Debug.print(DBG_WARNING, "[WiFi] No SSID configured");
    return;
  }
  Debug.print(DBG_INFO, "[WiFi] Connecting to %s...", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(ssid.c_str(), psk.c_str());
}

void reconnectToWiFi()
{
  if (WiFi.status() != WL_CONNECTED) {
    Debug.print(DBG_INFO, "[WiFi] Reconnecting...");
    WiFi.reconnect();
  } else {
    Debug.print(DBG_INFO, "[WiFi] Spurious disconnect event ignored");
  }
}

void connectToMqtt()
{
  if (Credentials::mqttHost().isEmpty()) return;
  Debug.print(DBG_INFO, "[MQTT] Connecting...");
  mqttClient.connect();
}

void WiFiEvent(WiFiEvent_t event)
{
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      xTimerStop(wifiReconnectTimer, 0);
      Debug.print(DBG_INFO, "[WiFi] Connected: %s", WiFi.SSID().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      xTimerStop(wifiReconnectTimer, 0);
      Debug.print(DBG_INFO, "[WiFi] IP: %s", WiFi.localIP().toString().c_str());
      UpdateWiFi(true);
      xTimerStart(mqttReconnectTimer, 0);
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Debug.print(DBG_WARNING, "[WiFi] Connection lost");
      xTimerStop(mqttReconnectTimer, 0);
      xTimerStart(wifiReconnectTimer, 0);
      UpdateWiFi(false);
      break;
    default: break;
  }
}

static void onMqttConnect(bool sessionPresent)
{
  Debug.print(DBG_INFO, "[MQTT] Connected, session: %d", sessionPresent);
  mqttClient.subscribe(PoolTopicAPI_Legacy, 2);   // backward-compat during migration; removed in Task 24
  MQTTConnection = true;
}

static void onMqttDisconnect(espMqttClientTypes::DisconnectReason reason)
{
  Debug.print(DBG_WARNING, "[MQTT] Disconnected, reason: %d", static_cast<int>(reason));
  if (WiFi.isConnected()) xTimerStart(mqttReconnectTimer, 0);
  MQTTConnection = false;
}

static void onMqttMessage(const espMqttClientTypes::MessageProperties& /*props*/,
                          const char* topic,
                          const uint8_t* payload,
                          size_t len, size_t /*index*/, size_t /*total*/)
{
  if (strcmp(topic, PoolTopicAPI_Legacy) == 0) {
    char Command[100] = "";
    size_t n = len < sizeof(Command) - 1 ? len : sizeof(Command) - 1;
    memcpy(Command, payload, n);
    Command[n] = '\0';
    if (xQueueSendToBack(queueIn, &Command, 0) == pdPASS) {
      Debug.print(DBG_INFO, "[MQTT] Queued: %s", Command);
    } else {
      Debug.print(DBG_ERROR, "[MQTT] Queue full: %s", Command);
    }
  }
}
```

- [ ] **Step 2: Forward-declare the `Credentials` namespace**

Create a stub `include/Credentials.h` (real implementation lands in Task 14):
```cpp
#pragma once
#include <Arduino.h>

namespace Credentials {
  // Phase 3 seed: falls back to SEED_* macros from Secrets.h until NVS
  // storage lands in Task 14. Interface is stable across both phases.
  String wifiSsid();
  String wifiPsk();
  String mqttHost();
  uint16_t mqttPort();
  String mqttUser();
  String mqttPass();
  String adminPwdSha256Hex();   // empty when no password set
  String otaPwdSha256Hex();     // empty when no password set
  String deviceId();            // stable lowercase-hex MAC, no colons
}
```

Create `src/Credentials.cpp` (seed-only stub for Phase 3; expanded in Task 14):
```cpp
#include "Credentials.h"
#include "Secrets.h"
#include <WiFi.h>

namespace Credentials {

String wifiSsid()         { return String(SEED_WIFI_SSID); }
String wifiPsk()          { return String(SEED_WIFI_PSK); }
String mqttHost()         { return String(SEED_MQTT_HOST); }
uint16_t mqttPort()       { return SEED_MQTT_PORT; }
String mqttUser()         { return String(SEED_MQTT_USER); }
String mqttPass()         { return String(SEED_MQTT_PASS); }
String adminPwdSha256Hex(){ return String(""); }   // stubbed; NVS-backed in Task 14
String otaPwdSha256Hex()  { return String(""); }   // stubbed; NVS-backed in Task 14

String deviceId() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[13];
  snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

} // namespace Credentials
```

- [ ] **Step 3: Compile**

Run:
```bash
pio run -e serial_upload 2>&1 | tail -30
```
Expected: errors should be much reduced. Any remaining ones relate to `Setup.cpp` still calling the old `ArduinoOTA.setPasswordHash(OTA_PWDHASH)` — that's Task 16.

- [ ] **Step 4: Commit**

Run:
```bash
git add src/mqtt_comm.cpp src/Credentials.cpp include/Credentials.h
git commit -m "sp1: swap AsyncMqttClient for espMqttClient; add Credentials seed stub

Credentials::* reads from Secrets.h seeds for now; Task 14 replaces
with NVS-backed storage. Legacy /Home/Pool/API command topic stays
subscribed during the migration window and is removed in Task 24."
```

---

### Task 9: Update `src/Setup.cpp` to compile with new APIs

**Files:**
- Modify: `src/Setup.cpp`

- [ ] **Step 1: Remove the ArduinoOTA password-hash line and update port**

In `src/Setup.cpp`, find lines 403–406:
```cpp
  ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.setHostname("PoolMaster");
  ArduinoOTA.setPasswordHash(OTA_PWDHASH);
```
Replace with:
```cpp
  ArduinoOTA.setPort(3232);
  ArduinoOTA.setHostname(HOSTNAME);
  // OTA password comes from NVS in Task 16; setPassword(...) set there.
```

- [ ] **Step 2: Update the mDNS addService port**

In `src/Setup.cpp`, find line 231:
```cpp
  MDNS.addService("http", "tcp", SERVER_PORT);
```
Replace with:
```cpp
  MDNS.addService("http", "tcp", 80);
```

- [ ] **Step 3: Compile**

Run:
```bash
pio run -e serial_upload
```
Expected: clean compile. If anything still fails, investigate — this is the first green compile of the migration.

- [ ] **Step 4: Bench smoke — flash and verify pool logic still works**

Run:
```bash
pio run -e serial_upload -t upload -t monitor
```
Expected:
- Device boots, reads NVS, loads settings.
- `[WiFi] Connecting to ...` — assumes `SEED_WIFI_SSID`/`SEED_WIFI_PSK` in local `Secrets.h`; if blank, will log `[WiFi] No SSID configured` and not connect.
- All 10 FreeRTOS tasks start. pH / ORP / temp values appear in the serial log.
- Nextion shows pool data as before.

If `Secrets.h` is empty, temporarily fill with real WiFi values to validate this task, then revert to empty and commit. The point is proving the refactor didn't break pool logic.

- [ ] **Step 5: Commit**

Run:
```bash
git add src/Setup.cpp
git commit -m "sp1: unbreak Setup.cpp after lib swap; OTA port 3232; mDNS http :80"
```

---

## Phase 4: File reorganization

### Task 10: Split `mqtt_comm.cpp` into `WiFiService.cpp` + `MqttBridge.cpp`

**Files:**
- Create: `src/WiFiService.cpp`
- Create: `src/MqttBridge.cpp`
- Delete: `src/mqtt_comm.cpp`
- Modify: `src/Setup.cpp` (include swap — now `#include "WiFiService.h"` + `#include "MqttBridge.h"` via forward-decls)
- Create: `include/WiFiService.h`
- Create: `include/MqttBridge.h`

- [ ] **Step 1: Create `include/WiFiService.h`**

Create file `include/WiFiService.h`:
```cpp
#pragma once

void initTimers();
void connectToWiFi();
void reconnectToWiFi();
void WiFiEvent(WiFiEvent_t event);
```

- [ ] **Step 2: Create `include/MqttBridge.h`**

Create file `include/MqttBridge.h`:
```cpp
#pragma once

void mqttInit();
void connectToMqtt();
// Enqueue a legacy-format JSON command string onto queueIn.
// Used by MQTT onMessage AND (later) web POST handlers.
bool enqueueCommand(const char* jsonStr);
```

- [ ] **Step 3: Create `src/WiFiService.cpp`**

Create file `src/WiFiService.cpp` with the WiFi-only subset of the old `mqtt_comm.cpp`:
```cpp
#include <Arduino.h>
#include <WiFi.h>
#include "Config.h"
#include "PoolMaster.h"
#include "Credentials.h"
#include "WiFiService.h"
#include "MqttBridge.h"

static TimerHandle_t mqttReconnectTimer;
static TimerHandle_t wifiReconnectTimer;

void UpdateWiFi(bool);   // defined in Nextion.cpp

void initTimers()
{
  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0,
                                    reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0,
                                    reinterpret_cast<TimerCallbackFunction_t>(reconnectToWiFi));
}

void connectToWiFi()
{
  const String ssid = Credentials::wifiSsid();
  const String psk  = Credentials::wifiPsk();
  if (ssid.isEmpty()) {
    Debug.print(DBG_WARNING, "[WiFi] No SSID configured");
    return;
  }
  Debug.print(DBG_INFO, "[WiFi] Connecting to %s...", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(ssid.c_str(), psk.c_str());
}

void reconnectToWiFi()
{
  if (WiFi.status() != WL_CONNECTED) {
    Debug.print(DBG_INFO, "[WiFi] Reconnecting...");
    WiFi.reconnect();
  } else {
    Debug.print(DBG_INFO, "[WiFi] Spurious disconnect event ignored");
  }
}

void WiFiEvent(WiFiEvent_t event)
{
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      xTimerStop(wifiReconnectTimer, 0);
      Debug.print(DBG_INFO, "[WiFi] Connected: %s", WiFi.SSID().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      xTimerStop(wifiReconnectTimer, 0);
      Debug.print(DBG_INFO, "[WiFi] IP: %s", WiFi.localIP().toString().c_str());
      UpdateWiFi(true);
      xTimerStart(mqttReconnectTimer, 0);
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Debug.print(DBG_WARNING, "[WiFi] Connection lost");
      xTimerStop(mqttReconnectTimer, 0);
      xTimerStart(wifiReconnectTimer, 0);
      UpdateWiFi(false);
      break;
    default: break;
  }
}
```

- [ ] **Step 4: Create `src/MqttBridge.cpp`**

Create file `src/MqttBridge.cpp`:
```cpp
#include <Arduino.h>
#include <espMqttClient.h>
#include "Config.h"
#include "PoolMaster.h"
#include "Credentials.h"
#include "MqttBridge.h"

espMqttClientAsync mqttClient;
bool MQTTConnection = false;

static const char* PoolTopicAPI_Legacy = POOLTOPIC_LEGACY "API";

static void onMqttConnect(bool sessionPresent);
static void onMqttDisconnect(espMqttClientTypes::DisconnectReason reason);
static void onMqttMessage(const espMqttClientTypes::MessageProperties& properties,
                          const char* topic,
                          const uint8_t* payload,
                          size_t len, size_t index, size_t total);

void mqttInit()
{
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onMessage(onMqttMessage);

  const String host = Credentials::mqttHost();
  if (host.isEmpty()) {
    Debug.print(DBG_INFO, "[MQTT] No broker configured — bridge idle");
    return;
  }
  mqttClient.setServer(host.c_str(), Credentials::mqttPort());

  const String user = Credentials::mqttUser();
  const String pass = Credentials::mqttPass();
  if (!user.isEmpty()) mqttClient.setCredentials(user.c_str(), pass.c_str());

  static char willTopic[64];
  snprintf(willTopic, sizeof(willTopic), "poolmaster/%s/avail", Credentials::deviceId().c_str());
  mqttClient.setWill(willTopic, 1, true, "offline");
}

void connectToMqtt()
{
  if (Credentials::mqttHost().isEmpty()) return;
  Debug.print(DBG_INFO, "[MQTT] Connecting...");
  mqttClient.connect();
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

static void onMqttConnect(bool sessionPresent)
{
  Debug.print(DBG_INFO, "[MQTT] Connected, session: %d", sessionPresent);
  mqttClient.subscribe(PoolTopicAPI_Legacy, 2);   // removed in Task 24
  MQTTConnection = true;
}

static void onMqttDisconnect(espMqttClientTypes::DisconnectReason reason)
{
  extern TimerHandle_t mqttReconnectTimer;   // kept in WiFiService.cpp until Task 24
  Debug.print(DBG_WARNING, "[MQTT] Disconnected, reason: %d", static_cast<int>(reason));
  MQTTConnection = false;
}

static void onMqttMessage(const espMqttClientTypes::MessageProperties& /*props*/,
                          const char* topic,
                          const uint8_t* payload,
                          size_t len, size_t /*index*/, size_t /*total*/)
{
  if (strcmp(topic, PoolTopicAPI_Legacy) == 0) {
    char Command[100];
    size_t n = len < sizeof(Command) - 1 ? len : sizeof(Command) - 1;
    memcpy(Command, payload, n);
    Command[n] = '\0';
    if (xQueueSendToBack(queueIn, &Command, 0) != pdPASS) {
      Debug.print(DBG_ERROR, "[MQTT] Queue full: %s", Command);
    } else {
      Debug.print(DBG_INFO, "[MQTT] Queued: %s", Command);
    }
  }
}
```

Note: `mqttReconnectTimer` is kept static-to-its-creating-file per the original code structure. We can't `extern` it cleanly across the split. For now, leave the `xTimerStart(mqttReconnectTimer, 0)` call in `WiFiService::WiFiEvent` as-is — it compiles against `extern TimerHandle_t mqttReconnectTimer;` declared in `WiFiService.cpp`. Task 24 consolidates this.

Actually, simpler: move `mqttReconnectTimer` to file-scope in `MqttBridge.cpp` and expose a `void startMqttReconnectTimer()` from `MqttBridge.h` that `WiFiService.cpp` calls. Update `MqttBridge.h`:
```cpp
#pragma once

void mqttInit();
void connectToMqtt();
void startMqttReconnectTimer();
void stopMqttReconnectTimer();
bool enqueueCommand(const char* jsonStr);
```

And implement `startMqttReconnectTimer()` / `stopMqttReconnectTimer()` in `MqttBridge.cpp`:
```cpp
static TimerHandle_t mqttReconnectTimer;

void startMqttReconnectTimer() { if (mqttReconnectTimer) xTimerStart(mqttReconnectTimer, 0); }
void stopMqttReconnectTimer()  { if (mqttReconnectTimer) xTimerStop(mqttReconnectTimer, 0); }
```

And `mqttReconnectTimer` gets created inside `mqttInit()`:
```cpp
void mqttInit() {
  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0,
                                    reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  /* ... rest as above ... */
}
```

And update `WiFiService.cpp` to call `startMqttReconnectTimer()` / `stopMqttReconnectTimer()` in `WiFiEvent` instead of manipulating the timer handle directly. The `initTimers()` in WiFiService now only creates `wifiReconnectTimer`.

- [ ] **Step 5: Delete `src/mqtt_comm.cpp`**

Run:
```bash
rm src/mqtt_comm.cpp
```

- [ ] **Step 6: Update `src/Setup.cpp` includes**

At the top of `src/Setup.cpp`, alongside the existing includes, add:
```cpp
#include "WiFiService.h"
#include "MqttBridge.h"
```

The function prototypes listed around lines 99–119 (`void initTimers(void);`, `void connectToWiFi(void);`, `void mqttInit();`, etc.) become redundant and can be deleted. Do not delete other prototypes.

- [ ] **Step 7: Compile**

Run:
```bash
pio run -e serial_upload
```
Expected: clean compile.

- [ ] **Step 8: Commit**

Run:
```bash
git add src/WiFiService.cpp src/MqttBridge.cpp include/WiFiService.h include/MqttBridge.h src/Setup.cpp
git rm src/mqtt_comm.cpp
git commit -m "sp1: split mqtt_comm.cpp into WiFiService + MqttBridge

No behavior change. Timer ownership moves into MqttBridge.cpp behind
start/stopMqttReconnectTimer() so WiFiService does not poke at its
internals."
```

---

### Task 11: Rename `PoolServer.cpp` → `CommandQueue.cpp` and `Publish.cpp` → `MqttPublish.cpp`

**Files:**
- Rename: `src/PoolServer.cpp` → `src/CommandQueue.cpp`
- Rename: `src/Publish.cpp` → `src/MqttPublish.cpp`
- Modify: any `#include` referencing the old names (should be zero — they're `.cpp` not headers)

- [ ] **Step 1: Use `git mv` to preserve history**

Run:
```bash
git mv src/PoolServer.cpp src/CommandQueue.cpp
git mv src/Publish.cpp src/MqttPublish.cpp
```

- [ ] **Step 2: Scan for any surviving string references**

Run:
```bash
grep -rn "PoolServer\|Publish\.cpp" src/ include/
```
Expected: no source code references (comments with the old name are OK but should be updated; no `.h` includes because these are .cpp-only).

- [ ] **Step 3: Update in-file header comments**

In `src/CommandQueue.cpp` top-of-file comment, change references to "PoolServer" → "CommandQueue".
In `src/MqttPublish.cpp` top-of-file comment, change references to "Publish" → "MqttPublish".

- [ ] **Step 4: Compile**

Run:
```bash
pio run -e serial_upload
```
Expected: clean compile.

- [ ] **Step 5: Commit**

Run:
```bash
git add -u src/
git commit -m "sp1: rename PoolServer.cpp -> CommandQueue.cpp, Publish.cpp -> MqttPublish.cpp

Names now reflect actual responsibility. No behavior change."
```

---

### Task 12: Extract `Settings.cpp` from `Setup.cpp`

**Files:**
- Create: `src/Settings.cpp`
- Create: `include/Settings.h`
- Modify: `src/Setup.cpp` (remove extracted functions)

- [ ] **Step 1: Create `include/Settings.h`**

Create file `include/Settings.h`:
```cpp
#pragma once
#include <Arduino.h>

bool loadConfig();
bool saveConfig();

bool saveParam(const char* key, uint8_t val);
bool saveParam(const char* key, bool val);
bool saveParam(const char* key, unsigned long val);
bool saveParam(const char* key, double val);
```

- [ ] **Step 2: Create `src/Settings.cpp`**

Cut the functions `loadConfig`, `saveConfig`, and the four `saveParam` overloads from `src/Setup.cpp` (lines ~445–610 in the current file) and paste into a new file `src/Settings.cpp` with this header:
```cpp
#include <Arduino.h>
#include <Preferences.h>
#include "Arduino_DebugUtils.h"
#include "Config.h"
#include "PoolMaster.h"
#include "Settings.h"

extern Preferences nvs;

/* ----- paste loadConfig, saveConfig, and the 4 saveParam overloads here ----- */
```

- [ ] **Step 3: Add `#include "Settings.h"` to `src/Setup.cpp`**

Near the other includes in `src/Setup.cpp`.

- [ ] **Step 4: Remove the now-duplicated forward declarations**

In `src/Setup.cpp`, delete the prototype lines:
```cpp
bool loadConfig(void);
bool saveConfig(void);
bool saveParam(const char*,uint8_t );
```

- [ ] **Step 5: Compile**

Run:
```bash
pio run -e serial_upload
```
Expected: clean.

- [ ] **Step 6: Commit**

Run:
```bash
git add src/Settings.cpp include/Settings.h src/Setup.cpp
git commit -m "sp1: extract Settings.cpp (loadConfig/saveConfig/saveParam) from Setup.cpp

Setup.cpp shrinks. No behavior change."
```

---

## Phase 5: NVS credentials + web infrastructure

### Task 13: Promote `Credentials` from seed-stub to NVS-backed

**Files:**
- Modify: `src/Credentials.cpp`
- Modify: `include/Credentials.h` (add setters)

- [ ] **Step 1: Extend `include/Credentials.h` with setters**

Replace the contents of `include/Credentials.h` with:
```cpp
#pragma once
#include <Arduino.h>

namespace Credentials {
  // Lazy-loaded: first call reads from NVS. If NVS is empty,
  // falls back once to SEED_* macros from Secrets.h and writes the seed
  // into NVS (so subsequent boots do not need Secrets.h).
  String wifiSsid();
  String wifiPsk();
  String mqttHost();
  uint16_t mqttPort();
  String mqttUser();
  String mqttPass();
  String adminPwdSha256Hex();   // empty when no password set
  String otaPwdSha256Hex();     // empty when no password set
  String timezone();            // POSIX TZ string; empty → use default

  void setWifi(const String& ssid, const String& psk);
  void setMqtt(const String& host, uint16_t port, const String& user, const String& pass);
  void setAdminPassword(const String& plaintext);
  void setOtaPassword(const String& plaintext);
  void setTimezone(const String& tz);

  void clearWifi();             // used by "Factory reset WiFi" button

  // Provisioning flag — true when WiFi creds exist and user finished wizard
  bool provisioningComplete();
  void setProvisioningComplete(bool v);

  String deviceId();            // stable lowercase-hex MAC, no colons

  // Internal helper exposed for WebAuth.cpp
  String sha256Hex(const String& input);
}
```

- [ ] **Step 2: Replace `src/Credentials.cpp` with full NVS implementation**

Replace the entire contents of `src/Credentials.cpp` with:
```cpp
#include "Credentials.h"
#include "Secrets.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <mbedtls/md.h>

namespace Credentials {

static Preferences prefs;

static String getOrSeed(const char* ns, const char* key, const char* seed) {
  prefs.begin(ns, false);
  String val = prefs.getString(key, "");
  if (val.isEmpty() && seed && seed[0] != '\0') {
    prefs.putString(key, seed);
    val = String(seed);
  }
  prefs.end();
  return val;
}

static uint16_t getU16OrSeed(const char* ns, const char* key, uint16_t seed) {
  prefs.begin(ns, false);
  uint16_t v = prefs.getUShort(key, 0);
  if (v == 0) {
    prefs.putUShort(key, seed);
    v = seed;
  }
  prefs.end();
  return v;
}

static void setString(const char* ns, const char* key, const String& v) {
  prefs.begin(ns, false);
  prefs.putString(key, v);
  prefs.end();
}

String wifiSsid() { return getOrSeed("wifi", "ssid", SEED_WIFI_SSID); }
String wifiPsk()  { return getOrSeed("wifi", "psk",  SEED_WIFI_PSK); }

String mqttHost()   { return getOrSeed("mqtt", "host", SEED_MQTT_HOST); }
uint16_t mqttPort() { return getU16OrSeed("mqtt", "port", SEED_MQTT_PORT); }
String mqttUser()   { return getOrSeed("mqtt", "user", SEED_MQTT_USER); }
String mqttPass()   { return getOrSeed("mqtt", "pass", SEED_MQTT_PASS); }

String sha256Hex(const String& input) {
  uint8_t digest[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, reinterpret_cast<const uint8_t*>(input.c_str()), input.length());
  mbedtls_md_finish(&ctx, digest);
  mbedtls_md_free(&ctx);
  char hex[65];
  for (int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
  hex[64] = '\0';
  return String(hex);
}

String adminPwdSha256Hex() {
  prefs.begin("admin", true);
  String h = prefs.getString("pwd_sha256", "");
  prefs.end();
  if (h.isEmpty() && SEED_ADMIN_PWD && SEED_ADMIN_PWD[0] != '\0') {
    h = sha256Hex(String(SEED_ADMIN_PWD));
    prefs.begin("admin", false);
    prefs.putString("pwd_sha256", h);
    prefs.end();
  }
  return h;
}

String otaPwdSha256Hex() {
  prefs.begin("ota", true);
  String h = prefs.getString("pwd_sha256", "");
  prefs.end();
  if (h.isEmpty()) h = adminPwdSha256Hex();   // fall back to admin
  return h;
}

String timezone() {
  prefs.begin("time", true);
  String tz = prefs.getString("tz", "");
  prefs.end();
  return tz;
}

void setWifi(const String& ssid, const String& psk) {
  setString("wifi", "ssid", ssid);
  setString("wifi", "psk", psk);
}

void setMqtt(const String& host, uint16_t port, const String& user, const String& pass) {
  setString("mqtt", "host", host);
  prefs.begin("mqtt", false); prefs.putUShort("port", port); prefs.end();
  setString("mqtt", "user", user);
  setString("mqtt", "pass", pass);
}

void setAdminPassword(const String& plaintext) {
  String h = sha256Hex(plaintext);
  setString("admin", "pwd_sha256", h);
}

void setOtaPassword(const String& plaintext) {
  String h = sha256Hex(plaintext);
  setString("ota", "pwd_sha256", h);
}

void setTimezone(const String& tz) {
  setString("time", "tz", tz);
}

void clearWifi() {
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
}

bool provisioningComplete() {
  prefs.begin("prov", true);
  bool v = prefs.getBool("done", false);
  prefs.end();
  return v;
}

void setProvisioningComplete(bool v) {
  prefs.begin("prov", false);
  prefs.putBool("done", v);
  prefs.end();
}

String deviceId() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[13];
  snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

} // namespace Credentials
```

- [ ] **Step 3: Compile**

Run:
```bash
pio run -e serial_upload
```
Expected: clean.

- [ ] **Step 4: Bench smoke — verify NVS read/write**

Flash the device, watch serial log during boot. First boot after update:
- If `Secrets.h` has `SEED_WIFI_SSID` filled, that value seeds NVS on first read and WiFi connects normally.
- Reflash with `Secrets.h` emptied → device should still connect (creds now live in NVS from previous boot).

- [ ] **Step 5: Commit**

Run:
```bash
git add src/Credentials.cpp include/Credentials.h
git commit -m "sp1: promote Credentials to NVS-backed with SEED_* one-time seed

First boot consumes Secrets.h seeds into NVS; subsequent boots read
from NVS only. Admin/OTA passwords stored as SHA-256 hex digest."
```

---

### Task 14: `WebAuth.cpp` — HTTP Basic auth middleware

**Files:**
- Create: `include/WebAuth.h`
- Create: `src/WebAuth.cpp`

- [ ] **Step 1: Create `include/WebAuth.h`**

Create file `include/WebAuth.h`:
```cpp
#pragma once
#include <ESPAsyncWebServer.h>

namespace WebAuth {
  // Returns true if the request carries valid admin Basic-auth credentials.
  // If false, has already called request->send(401, ...) with the WWW-Authenticate header.
  bool requireAdmin(AsyncWebServerRequest* request);

  // Verifies plaintext password against stored admin hash. Used by wizard & OTA.
  bool checkAdminPassword(const String& plaintext);
}
```

- [ ] **Step 2: Create `src/WebAuth.cpp`**

Create file `src/WebAuth.cpp`:
```cpp
#include "WebAuth.h"
#include "Credentials.h"
#include <base64.h>

namespace WebAuth {

static constexpr const char* ADMIN_USER = "admin";

bool checkAdminPassword(const String& plaintext) {
  const String stored = Credentials::adminPwdSha256Hex();
  if (stored.isEmpty()) return false;   // no password set = no admin access (deliberate)
  return Credentials::sha256Hex(plaintext) == stored;
}

bool requireAdmin(AsyncWebServerRequest* request) {
  // If no admin password is set yet, grant access (pre-setup state).
  // The setup wizard will prompt to set one; until then, UX > security.
  if (Credentials::adminPwdSha256Hex().isEmpty()) return true;

  if (!request->hasHeader("Authorization")) {
    AsyncWebServerResponse* r = request->beginResponse(401, "text/plain", "Authentication required");
    r->addHeader("WWW-Authenticate", "Basic realm=\"PoolMaster\", charset=\"UTF-8\"");
    request->send(r);
    return false;
  }
  String hdr = request->header("Authorization");
  if (!hdr.startsWith("Basic ")) {
    request->send(401, "text/plain", "Bad auth scheme");
    return false;
  }
  String b64 = hdr.substring(6);
  b64.trim();
  String decoded = base64::decode(b64);
  int colon = decoded.indexOf(':');
  if (colon <= 0) {
    request->send(401, "text/plain", "Malformed auth");
    return false;
  }
  String user = decoded.substring(0, colon);
  String pwd  = decoded.substring(colon + 1);
  if (user != ADMIN_USER || !checkAdminPassword(pwd)) {
    AsyncWebServerResponse* r = request->beginResponse(401, "text/plain", "Bad credentials");
    r->addHeader("WWW-Authenticate", "Basic realm=\"PoolMaster\"");
    request->send(r);
    return false;
  }
  return true;
}

} // namespace WebAuth
```

Note: `<base64.h>` ships with the ESP32 Arduino core (`core/core/base64.h`). If the include fails at compile time, replace with `#include <mbedtls/base64.h>` and use `mbedtls_base64_decode(...)` instead.

- [ ] **Step 3: Compile**

Run:
```bash
pio run -e serial_upload
```
Expected: clean.

- [ ] **Step 4: Commit**

Run:
```bash
git add src/WebAuth.cpp include/WebAuth.h
git commit -m "sp1: WebAuth module — HTTP Basic auth against NVS admin hash"
```

---

### Task 15: `WebServer.cpp` — AsyncWebServer on port 80

**Files:**
- Create: `include/WebServer.h`
- Create: `src/WebServer.cpp`
- Modify: `src/Setup.cpp` (call `WebServerInit()` after WiFi up)

- [ ] **Step 1: Create `include/WebServer.h`**

Create file `include/WebServer.h`:
```cpp
#pragma once
#include <ESPAsyncWebServer.h>

// Global server instance — visible to OtaService.cpp and Provisioning.cpp
// so they can register their routes on the same server.
extern AsyncWebServer webServer;

// Call once from setup() AFTER WiFi is connected. Mounts LittleFS,
// registers the static file handler for `/`, and stands up core routes.
void WebServerInit();
```

- [ ] **Step 2: Create `src/WebServer.cpp`**

Create file `src/WebServer.cpp`:
```cpp
#include <LittleFS.h>
#include "WebServer.h"
#include "WebAuth.h"
#include "Arduino_DebugUtils.h"

AsyncWebServer webServer(80);

void WebServerInit()
{
  if (!LittleFS.begin(true)) {
    Debug.print(DBG_ERROR, "[Web] LittleFS mount failed");
    return;
  }
  Debug.print(DBG_INFO, "[Web] LittleFS mounted: %u/%u bytes used",
              LittleFS.usedBytes(), LittleFS.totalBytes());

  // Root static handler. `index.html` is the placeholder until SP3.
  webServer.serveStatic("/", LittleFS, "/")
           .setDefaultFile("index.html")
           .setCacheControl("max-age=3600");

  webServer.on("/robots.txt", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(LittleFS, "/robots.txt", "text/plain");
  });

  // Health endpoint (public; no auth) — a tiny diag surface usable from
  // any device on the LAN for "is the box alive?" checks.
  webServer.on("/healthz", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["status"] = "ok";
    doc["uptime_s"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // 404 fallback
  webServer.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "Not found");
  });

  webServer.begin();
  Debug.print(DBG_INFO, "[Web] HTTP server listening on :80");
}
```

- [ ] **Step 3: Call `WebServerInit()` from `Setup.cpp`**

In `src/Setup.cpp`, near the existing `MDNS.begin(...)` block (around line 227), add after the `MDNS.addService(...)` line:
```cpp
  #include "WebServer.h"   // add to top of file, not here — this comment is for navigation only
  WebServerInit();
```

Actually do this cleanly — add `#include "WebServer.h"` to the top of `Setup.cpp` alongside the other `.h` includes, then just the `WebServerInit();` call inline where indicated.

- [ ] **Step 4: Upload LittleFS contents**

Run:
```bash
pio run -t uploadfs -e serial_upload
```
Expected: uploads `data/` directory to LittleFS partition.

- [ ] **Step 5: Flash + smoke test**

Run:
```bash
pio run -e serial_upload -t upload -t monitor
```
Wait for WiFi + mDNS logs. Then in a browser:
```
http://poolmaster.local/healthz
```
Expected: JSON response. If `admin` password is unset, `/` also loads without prompt.

- [ ] **Step 6: Commit**

Run:
```bash
git add src/WebServer.cpp include/WebServer.h src/Setup.cpp
git commit -m "sp1: AsyncWebServer on :80 with LittleFS root + /healthz

Placeholder UI served from data/index.html until SP3."
```

---

### Task 16: `OtaService.cpp` — ArduinoOTA auth + web `/update` endpoint

**Files:**
- Create: `include/OtaService.h`
- Create: `src/OtaService.cpp`
- Modify: `src/Setup.cpp` (replace inline ArduinoOTA block with `OtaServiceInit(webServer)`)

- [ ] **Step 1: Create `include/OtaService.h`**

Create file `include/OtaService.h`:
```cpp
#pragma once
#include <ESPAsyncWebServer.h>

void OtaServiceInit(AsyncWebServer& srv);
```

- [ ] **Step 2: Create `src/OtaService.cpp`**

Create file `src/OtaService.cpp`:
```cpp
#include <ArduinoOTA.h>
#include <Update.h>
#include <esp_task_wdt.h>
#include "OtaService.h"
#include "WebAuth.h"
#include "Credentials.h"
#include "Arduino_DebugUtils.h"

static void setupArduinoOTA();
static void handleUpdatePost(AsyncWebServerRequest* req,
                             const String& /*filename*/,
                             size_t index, uint8_t* data, size_t len, bool final);

void OtaServiceInit(AsyncWebServer& srv)
{
  setupArduinoOTA();

  srv.on("/update", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;
    const char* html =
      "<!doctype html><meta charset=utf-8><title>PoolMaster OTA</title>"
      "<style>body{font:16px system-ui;margin:2rem auto;max-width:24rem}"
      "button{padding:.5rem 1rem}</style>"
      "<h1>Firmware update</h1>"
      "<form method=POST enctype=multipart/form-data action=/update>"
      "<p><label>Type: <select name=type>"
      "<option value=firmware>firmware.bin</option>"
      "<option value=littlefs>littlefs.bin</option></select></label></p>"
      "<p><input type=file name=file required></p>"
      "<p><button>Upload</button></p>"
      "<p style=color:#b45309>Device reboots on success.</p></form>";
    req->send(200, "text/html", html);
  });

  srv.on("/update", HTTP_POST,
    [](AsyncWebServerRequest* req) {
      if (!WebAuth::requireAdmin(req)) return;
      AsyncWebServerResponse* resp = req->beginResponse(
          Update.hasError() ? 400 : 200, "text/plain",
          Update.hasError() ? Update.errorString() : "Update OK — rebooting");
      resp->addHeader("Connection", "close");
      req->send(resp);
      if (!Update.hasError()) {
        // Delay reboot so the HTTP response flushes.
        xTaskCreate([](void*) { vTaskDelay(pdMS_TO_TICKS(500)); ESP.restart(); },
                    "reboot", 2048, nullptr, 1, nullptr);
      }
    },
    handleUpdatePost);

  Debug.print(DBG_INFO, "[OTA] ArduinoOTA :3232 + web /update ready");
}

static void setupArduinoOTA()
{
  ArduinoOTA.setPort(3232);
  ArduinoOTA.setHostname(HOSTNAME);

  String pwdHash = Credentials::otaPwdSha256Hex();
  if (!pwdHash.isEmpty()) {
    // ArduinoOTA expects an MD5 hash OR a plaintext password. We stored SHA-256,
    // so use the user-supplied plaintext as the password setter before NVS save.
    // In practice: OTA password set via the wizard gets stored plaintext in NVS
    // under "ota" namespace / key "pwd_plain" for ArduinoOTA consumption.
    // (We do not expose pwd_plain elsewhere — it stays inside the ota namespace.)
    // See Credentials::otaPwdForArduinoOTA() added below.
  }
  // Prefer plaintext for ArduinoOTA's .setPassword(); SHA-256 hex would also work
  // if we used .setPasswordHash() with MD5, which we don't. Resolve by adding a
  // parallel NVS key for OTA plaintext at wizard-set time. See Task 17.
  String otaPlain;
  {
    // inline NVS read — kept local to avoid leaking plaintext through Credentials
    Preferences p;
    p.begin("ota", true);
    otaPlain = p.getString("pwd_plain", "");
    p.end();
  }
  if (!otaPlain.isEmpty()) ArduinoOTA.setPassword(otaPlain.c_str());

  ArduinoOTA.onStart([]() {
    Debug.print(DBG_INFO, "[OTA] Start: %s",
      ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "fs");
  });
  ArduinoOTA.onEnd([]() { Debug.print(DBG_INFO, "[OTA] End"); });
  ArduinoOTA.onProgress([](unsigned p, unsigned t) {
    esp_task_wdt_reset();
    Serial.printf("OTA %u%%\r", (p / (t / 100)));
  });
  ArduinoOTA.onError([](ota_error_t e) {
    Debug.print(DBG_ERROR, "[OTA] Error %u", e);
  });
  ArduinoOTA.begin();
}

static void handleUpdatePost(AsyncWebServerRequest* req,
                             const String& /*filename*/,
                             size_t index, uint8_t* data, size_t len, bool final)
{
  if (!WebAuth::requireAdmin(req)) return;

  if (index == 0) {
    String typeParam = req->hasParam("type", true) ? req->getParam("type", true)->value() : "firmware";
    int cmd = (typeParam == "littlefs") ? U_SPIFFS : U_FLASH;
    size_t maxSize = (cmd == U_FLASH) ? UPDATE_SIZE_UNKNOWN : UPDATE_SIZE_UNKNOWN;
    if (!Update.begin(maxSize, cmd)) {
      Debug.print(DBG_ERROR, "[OTA/web] begin failed: %s", Update.errorString());
      return;
    }
    Debug.print(DBG_INFO, "[OTA/web] upload begin, cmd=%d", cmd);
  }
  if (len > 0) {
    if (Update.write(data, len) != len) {
      Debug.print(DBG_ERROR, "[OTA/web] write failed: %s", Update.errorString());
    }
    esp_task_wdt_reset();
  }
  if (final) {
    if (!Update.end(true)) {
      Debug.print(DBG_ERROR, "[OTA/web] end failed: %s", Update.errorString());
    } else {
      Debug.print(DBG_INFO, "[OTA/web] upload complete, size=%u", index + len);
    }
  }
}
```

Add a new setter to `Credentials` for the plaintext OTA password (needed by ArduinoOTA):

- [ ] **Step 3: Update `Credentials::setOtaPassword` to also store plaintext**

In `src/Credentials.cpp`, find the function `setOtaPassword(...)` and replace with:
```cpp
void setOtaPassword(const String& plaintext) {
  String h = sha256Hex(plaintext);
  setString("ota", "pwd_sha256", h);
  setString("ota", "pwd_plain", plaintext);   // for ArduinoOTA.setPassword()
}
```

Plaintext OTA storage is a deliberate tradeoff — ArduinoOTA's protocol doesn't support hash-based auth in a way the PlatformIO client will consume by default. The threat model (home LAN, no remote attackers) makes this acceptable.

- [ ] **Step 4: Wire into `Setup.cpp`**

In `src/Setup.cpp`:
1. Add `#include "OtaService.h"` to the top.
2. Delete the existing inline `ArduinoOTA.setPort(...)` block (lines 403–432).
3. After the existing `WebServerInit();` call, add:
   ```cpp
     OtaServiceInit(webServer);
   ```

- [ ] **Step 5: Compile + flash**

Run:
```bash
pio run -e serial_upload -t upload -t monitor
```
Expected: clean boot. Serial log shows `[OTA] ArduinoOTA :3232 + web /update ready`.

- [ ] **Step 6: Smoke test web OTA**

In a browser: `http://poolmaster.local/update` — prompts for Basic auth if admin password set, else shows form directly.

Upload a freshly-built firmware:
```bash
pio run -e serial_upload   # produces .pio/build/serial_upload/firmware.bin
```
Then POST the `firmware.bin` from the web form. Expected: device reboots to the new image.

- [ ] **Step 7: Commit**

Run:
```bash
git add src/OtaService.cpp include/OtaService.h src/Setup.cpp src/Credentials.cpp
git commit -m "sp1: OtaService — ArduinoOTA :3232 + web /update endpoint

Both paths guarded by NVS OTA password (falls back to admin password).
Web path uses Update.h streaming; feeds WDT each chunk."
```

---

## Phase 6: First-run provisioning

### Task 17: `Provisioning.cpp` — AP mode, DNSServer, captive-portal probes

**Files:**
- Create: `include/Provisioning.h`
- Create: `src/Provisioning.cpp`
- Modify: `src/Setup.cpp` (branch boot flow)

- [ ] **Step 1: Create `include/Provisioning.h`**

Create file `include/Provisioning.h`:
```cpp
#pragma once
#include <ESPAsyncWebServer.h>

namespace Provisioning {
  // Returns true if NVS has no WiFi SSID (i.e., we should enter AP mode).
  bool needed();

  // Starts AP mode, DNSServer, registers wizard routes on the given server.
  // Does NOT block — caller is expected to loop calling dnsTick() until
  // provisioning is marked complete and the device reboots.
  void start(AsyncWebServer& srv);

  // Pumps the DNSServer in a tight loop (call from loop() / AP-mode task).
  void dnsTick();

  // True once the user has finished step 1 (WiFi saved).
  bool wifiSavedThisSession();
}
```

- [ ] **Step 2: Create `src/Provisioning.cpp`**

Create file `src/Provisioning.cpp`:
```cpp
#include <WiFi.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include "Provisioning.h"
#include "Credentials.h"
#include "Arduino_DebugUtils.h"

namespace Provisioning {

static DNSServer dnsServer;
static bool running = false;
static bool wifiSaved = false;

bool needed() { return Credentials::wifiSsid().isEmpty(); }
bool wifiSavedThisSession() { return wifiSaved; }

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);

static String apSsid() {
  String id = Credentials::deviceId();
  String last4 = id.substring(id.length() - 4);
  last4.toUpperCase();
  return String("PoolMaster-Setup-") + last4;
}

static void registerProbeRoutes(AsyncWebServer& srv) {
  // Force captive-portal popup on common OSes by redirecting probe URLs.
  auto redirect = [](AsyncWebServerRequest* req) {
    req->redirect("http://192.168.4.1/setup.html");
  };
  srv.on("/generate_204",      HTTP_GET, redirect);   // Android
  srv.on("/gen_204",           HTTP_GET, redirect);   // Android (newer)
  srv.on("/hotspot-detect.html", HTTP_GET, redirect); // iOS / macOS
  srv.on("/library/test/success.html", HTTP_GET, redirect); // iOS (legacy)
  srv.on("/ncsi.txt",          HTTP_GET, redirect);   // Windows
  srv.on("/connecttest.txt",   HTTP_GET, redirect);   // Windows
  srv.on("/redirect",          HTTP_GET, redirect);   // Microsoft Connect probe
}

static void registerWizardRoutes(AsyncWebServer& srv) {
  srv.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->redirect("/setup.html");
  });

  srv.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
    int n = WiFi.scanComplete();
    if (n == -2) { WiFi.scanNetworks(true); n = -1; }    // async-start
    JsonDocument doc;
    JsonArray arr = doc["networks"].to<JsonArray>();
    if (n >= 0) {
      for (int i = 0; i < n; ++i) {
        JsonObject net = arr.add<JsonObject>();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
        net["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      }
      WiFi.scanDelete();
    }
    doc["scanning"] = (n < 0);
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  srv.on("/api/wifi/save", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!req->hasParam("ssid", true) || !req->hasParam("psk", true)) {
      req->send(400, "text/plain", "missing ssid/psk");
      return;
    }
    String ssid = req->getParam("ssid", true)->value();
    String psk  = req->getParam("psk",  true)->value();
    if (ssid.isEmpty()) { req->send(400, "text/plain", "empty ssid"); return; }
    Credentials::setWifi(ssid, psk);
    wifiSaved = true;
    req->send(200, "application/json", "{\"ok\":true}");
  });

  srv.on("/api/admin/save", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!req->hasParam("pwd", true)) { req->send(400, "text/plain", "missing pwd"); return; }
    Credentials::setAdminPassword(req->getParam("pwd", true)->value());
    Credentials::setOtaPassword(req->getParam("pwd", true)->value());   // default: same as admin
    req->send(200, "application/json", "{\"ok\":true}");
  });

  srv.on("/api/mqtt/save", HTTP_POST, [](AsyncWebServerRequest* req) {
    String host = req->hasParam("host", true) ? req->getParam("host", true)->value() : "";
    uint16_t port = req->hasParam("port", true) ? (uint16_t) req->getParam("port", true)->value().toInt() : 1883;
    String user = req->hasParam("user", true) ? req->getParam("user", true)->value() : "";
    String pass = req->hasParam("pass", true) ? req->getParam("pass", true)->value() : "";
    Credentials::setMqtt(host, port, user, pass);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  srv.on("/api/time/save", HTTP_POST, [](AsyncWebServerRequest* req) {
    String tz = req->hasParam("tz", true) ? req->getParam("tz", true)->value() : "";
    Credentials::setTimezone(tz);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  srv.on("/api/finish", HTTP_POST, [](AsyncWebServerRequest* req) {
    Credentials::setProvisioningComplete(true);
    req->send(200, "application/json", "{\"ok\":true}");
    xTaskCreate([](void*) { vTaskDelay(pdMS_TO_TICKS(500)); ESP.restart(); },
                "reboot", 2048, nullptr, 1, nullptr);
  });
}

void start(AsyncWebServer& srv) {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
  WiFi.softAP(apSsid().c_str());
  Debug.print(DBG_INFO, "[Prov] AP up: %s  IP: %s",
              apSsid().c_str(), WiFi.softAPIP().toString().c_str());

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", AP_IP);

  if (!LittleFS.begin(true)) {
    Debug.print(DBG_ERROR, "[Prov] LittleFS mount failed");
  }

  registerProbeRoutes(srv);
  registerWizardRoutes(srv);

  // Serve setup.html + setup.css directly from LittleFS at AP-mode root.
  srv.serveStatic("/setup.html", LittleFS, "/setup.html").setCacheControl("no-store");
  srv.serveStatic("/setup.css",  LittleFS, "/setup.css").setCacheControl("no-store");

  srv.onNotFound([](AsyncWebServerRequest* req) {
    req->redirect("http://192.168.4.1/setup.html");
  });

  srv.begin();
  running = true;
}

void dnsTick() {
  if (running) dnsServer.processNextRequest();
}

} // namespace Provisioning
```

- [ ] **Step 3: Branch boot flow in `Setup.cpp`**

In `src/Setup.cpp`, after `Serial.begin(115200)` and before `InitTFT()`, add:
```cpp
  #include "Provisioning.h"   // add to includes at top of file
  #include "WebServer.h"

  if (Provisioning::needed()) {
    Debug.print(DBG_INFO, "[Boot] WiFi not provisioned — entering AP mode");
    Provisioning::start(webServer);
    // Pool logic tasks NOT started. Loop serves DNS until reboot.
    for (;;) {
      Provisioning::dnsTick();
      delay(10);
    }
  }
```

This is the hard branch: in AP mode, `Setup.cpp` never reaches the pool-tasks-create block. The `for(;;)` loop replaces the normal `startTasks = true;` path. `ESP.restart()` (called by `/api/finish` handler) brings the device back through `setup()` after wizard completion.

- [ ] **Step 4: Upload LittleFS again (it doesn't have setup.html yet — Task 18)**

Skip this step for now; Task 18 creates `data/setup.html`.

- [ ] **Step 5: Compile**

Run:
```bash
pio run -e serial_upload
```
Expected: clean. (Cannot bench-test fully until Task 18 ships `setup.html`.)

- [ ] **Step 6: Commit**

Run:
```bash
git add src/Provisioning.cpp include/Provisioning.h src/Setup.cpp
git commit -m "sp1: Provisioning module — AP mode, DNSServer, wizard POST routes

Setup.cpp branches hard when WiFi creds are missing: AP up, pool tasks
skipped, DNS pumped in loop() until /api/finish triggers reboot."
```

---

### Task 18: Setup wizard HTML + CSS

**Files:**
- Create: `data/setup.html`
- Create: `data/setup.css`

- [ ] **Step 1: Create `data/setup.html`**

Create file `data/setup.html`:
```html
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>PoolMaster Setup</title>
  <link rel="stylesheet" href="/setup.css" />
</head>
<body>
  <main>
    <header>
      <h1>PoolMaster</h1>
      <p class="sub">First-run setup</p>
    </header>

    <section id="step-wifi" class="step active">
      <h2>1. Connect to WiFi</h2>
      <p class="hint">Pick your home network. Your phone will briefly disconnect from this AP while the device joins.</p>
      <button type="button" id="scanBtn">Scan networks</button>
      <ul id="netList" aria-label="Scanned networks"></ul>
      <form id="wifiForm">
        <label>Network SSID
          <input name="ssid" id="ssidInput" required autocomplete="off" />
        </label>
        <label>Password
          <input type="password" name="psk" id="pskInput" autocomplete="new-password" />
        </label>
        <div class="actions">
          <button type="submit">Save &amp; continue</button>
        </div>
        <p class="err" id="wifiErr" hidden></p>
      </form>
    </section>

    <section id="step-admin" class="step">
      <h2>2. Set an admin password</h2>
      <p class="hint">Protects the web UI and OTA. You can skip this — you can set one later.</p>
      <form id="adminForm">
        <label>Admin password
          <input type="password" name="pwd" id="adminPwd" autocomplete="new-password" />
        </label>
        <div class="actions">
          <button type="button" class="link" data-skip="admin">Skip</button>
          <button type="submit">Save &amp; continue</button>
        </div>
      </form>
    </section>

    <section id="step-mqtt" class="step">
      <h2>3. Home Assistant / MQTT</h2>
      <p class="hint">Enter your MQTT broker to enable HA auto-discovery. Skip to configure later.</p>
      <form id="mqttForm">
        <label>Broker host
          <input name="host" id="mqttHost" placeholder="192.168.1.50" autocomplete="off" />
        </label>
        <label>Port
          <input name="port" type="number" value="1883" min="1" max="65535" />
        </label>
        <label>Username (optional)
          <input name="user" autocomplete="off" />
        </label>
        <label>Password (optional)
          <input name="pass" type="password" autocomplete="new-password" />
        </label>
        <div class="actions">
          <button type="button" class="link" data-skip="mqtt">Skip</button>
          <button type="submit">Save &amp; continue</button>
        </div>
      </form>
    </section>

    <section id="step-time" class="step">
      <h2>4. Timezone</h2>
      <p class="hint">POSIX TZ string. Default is Central European (DST aware).</p>
      <form id="timeForm">
        <label>TZ
          <input name="tz" value="CET-1CEST,M3.5.0/2,M10.5.0/3" />
        </label>
        <div class="actions">
          <button type="button" class="link" data-skip="time">Skip</button>
          <button type="submit">Save &amp; continue</button>
        </div>
      </form>
    </section>

    <section id="step-done" class="step">
      <h2>All set</h2>
      <p>Device will reboot, join your WiFi, and come online at <code>poolmaster.local</code>.</p>
      <p class="hint">If it doesn't, your router may not support mDNS — check its IP in the DHCP client list.</p>
      <div class="actions">
        <button type="button" id="finishBtn">Finish &amp; reboot</button>
      </div>
    </section>
  </main>

  <script>
  (function () {
    const steps = ['wifi', 'admin', 'mqtt', 'time', 'done'];
    let current = 0;

    function show(i) {
      current = i;
      steps.forEach((k, idx) => {
        document.getElementById(`step-${k}`).classList.toggle('active', idx === i);
      });
    }

    async function postForm(url, form) {
      const body = new URLSearchParams(new FormData(form));
      const r = await fetch(url, { method: 'POST', body });
      if (!r.ok) throw new Error(await r.text());
      return r.json();
    }

    document.querySelectorAll('[data-skip]').forEach(btn => {
      btn.addEventListener('click', () => show(current + 1));
    });

    document.getElementById('scanBtn').addEventListener('click', async () => {
      const list = document.getElementById('netList');
      list.innerHTML = '<li>Scanning…</li>';
      async function poll() {
        const r = await fetch('/api/wifi/scan');
        const j = await r.json();
        if (j.scanning) { setTimeout(poll, 1500); return; }
        list.innerHTML = '';
        (j.networks || []).sort((a,b) => b.rssi - a.rssi).forEach(n => {
          const li = document.createElement('li');
          li.textContent = `${n.ssid}  (${n.rssi} dBm)${n.secure ? ' 🔒' : ''}`;
          li.addEventListener('click', () => {
            document.getElementById('ssidInput').value = n.ssid;
            document.getElementById('pskInput').focus();
          });
          list.appendChild(li);
        });
      }
      poll();
    });

    document.getElementById('wifiForm').addEventListener('submit', async e => {
      e.preventDefault();
      const err = document.getElementById('wifiErr');
      err.hidden = true;
      try {
        await postForm('/api/wifi/save', e.target);
        show(current + 1);
      } catch (ex) {
        err.textContent = ex.message;
        err.hidden = false;
      }
    });

    document.getElementById('adminForm').addEventListener('submit', async e => {
      e.preventDefault();
      await postForm('/api/admin/save', e.target);
      show(current + 1);
    });

    document.getElementById('mqttForm').addEventListener('submit', async e => {
      e.preventDefault();
      await postForm('/api/mqtt/save', e.target);
      show(current + 1);
    });

    document.getElementById('timeForm').addEventListener('submit', async e => {
      e.preventDefault();
      await postForm('/api/time/save', e.target);
      show(current + 1);
    });

    document.getElementById('finishBtn').addEventListener('click', async () => {
      await fetch('/api/finish', { method: 'POST' });
      document.body.innerHTML = '<main><h1>Rebooting…</h1><p>You can close this tab.</p></main>';
    });
  })();
  </script>
</body>
</html>
```

- [ ] **Step 2: Create `data/setup.css`**

Create file `data/setup.css`:
```css
:root {
  --bg:#f8fafc; --fg:#1e293b; --muted:#64748b; --brand:#0284c7;
  --border:#cbd5e1; --danger:#b91c1c;
}
* { box-sizing:border-box; }
html,body { margin:0; padding:0; background:var(--bg); color:var(--fg);
  font:16px/1.5 system-ui, -apple-system, Segoe UI, Roboto, sans-serif; }
main { max-width:32rem; margin:1rem auto; padding:1.5rem; }
header h1 { font-size:1.8rem; margin:0; color:var(--brand); }
header .sub { color:var(--muted); margin:.25rem 0 1.5rem; }
.step { display:none; background:#fff; border:1px solid var(--border);
  border-radius:.75rem; padding:1.25rem; box-shadow:0 1px 2px rgba(0,0,0,.04); }
.step.active { display:block; }
.step h2 { margin:0 0 .25rem; font-size:1.2rem; }
.step .hint { color:var(--muted); margin:0 0 1rem; font-size:.95rem; }
form label { display:block; margin:.75rem 0; }
form input, form select {
  width:100%; padding:.55rem .7rem; border:1px solid var(--border);
  border-radius:.4rem; font:inherit; background:#fff; }
form input:focus { outline:2px solid var(--brand); outline-offset:1px; }
.actions { display:flex; justify-content:flex-end; gap:.5rem; margin-top:1rem; }
button {
  background:var(--brand); color:#fff; border:none; padding:.55rem 1rem;
  border-radius:.4rem; font:inherit; cursor:pointer; }
button.link { background:none; color:var(--muted); }
button.link:hover { color:var(--fg); }
#scanBtn { background:#e2e8f0; color:var(--fg); margin-bottom:.5rem; }
#netList { list-style:none; padding:0; margin:0 0 1rem; max-height:12rem;
  overflow-y:auto; border:1px solid var(--border); border-radius:.4rem; }
#netList li { padding:.5rem .7rem; border-bottom:1px solid var(--border);
  cursor:pointer; font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size:.9rem; }
#netList li:last-child { border-bottom:none; }
#netList li:hover { background:#f1f5f9; }
.err { color:var(--danger); font-size:.9rem; margin-top:.5rem; }
code { background:#e2e8f0; padding:.1rem .3rem; border-radius:.2rem;
  font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size:.85em; }
```

- [ ] **Step 3: Upload LittleFS**

Run:
```bash
pio run -t uploadfs -e serial_upload
```

- [ ] **Step 4: Bench smoke — AP mode end-to-end**

1. Wipe NVS to force AP mode:
   ```bash
   pio run -t erase -e serial_upload && pio run -e serial_upload -t upload -t monitor
   ```
2. Serial log: `[Prov] AP up: PoolMaster-Setup-XXXX IP: 192.168.4.1`.
3. Join the AP from a phone. Expected: captive-portal popup opens automatically.
4. Wizard walks through Steps 1–5. Step 1: scan, pick network, enter password. Step 2–4: skippable. Step 5: "Finish & reboot".
5. Device reboots, joins home WiFi, `ping poolmaster.local` resolves.

- [ ] **Step 5: Commit**

Run:
```bash
git add data/setup.html data/setup.css
git commit -m "sp1: setup wizard — 5-step onboarding UI"
```

---

### Task 19: Fallback-to-AP on WiFi connect failure

**Files:**
- Modify: `src/Setup.cpp`
- Modify: `src/WiFiService.cpp`

- [ ] **Step 1: Add a retry-then-fail helper in `WiFiService.cpp`**

In `src/WiFiService.cpp`, add a new exported function `bool waitForWiFiOrTimeout(uint32_t totalMs);` and declare it in `WiFiService.h`:

`include/WiFiService.h` — append:
```cpp
// Blocks up to totalMs waiting for STA to associate + get an IP.
// Returns true on success, false if timeout.
bool waitForWiFiOrTimeout(uint32_t totalMs);
```

`src/WiFiService.cpp` — append:
```cpp
bool waitForWiFiOrTimeout(uint32_t totalMs) {
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > totalMs) return false;
    delay(250);
  }
  return true;
}
```

- [ ] **Step 2: Wrap WiFi attempt with retry-and-fallback in `Setup.cpp`**

In `src/Setup.cpp`, replace the existing WiFi startup block (around lines 207–215):
```cpp
  WiFi.onEvent(WiFiEvent);
  initTimers();
  connectToWiFi();

  delay(500);
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
```
with:
```cpp
  WiFi.onEvent(WiFiEvent);
  initTimers();

  bool connected = false;
  for (int attempt = 0; attempt < 3 && !connected; ++attempt) {
    Debug.print(DBG_INFO, "[Boot] WiFi attempt %d/3", attempt + 1);
    connectToWiFi();
    connected = waitForWiFiOrTimeout(15000);
    if (!connected) {
      Debug.print(DBG_WARNING, "[Boot] WiFi attempt %d timed out", attempt + 1);
      WiFi.disconnect(true);
      delay(500);
    }
  }
  if (!connected) {
    Debug.print(DBG_ERROR, "[Boot] WiFi failed after 3 attempts — entering AP mode");
    Provisioning::start(webServer);
    for (;;) {
      Provisioning::dnsTick();
      delay(10);
    }
  }
```

- [ ] **Step 3: Compile + bench smoke**

Run:
```bash
pio run -e serial_upload -t upload -t monitor
```
Temporarily change your router's WiFi password to force a failure, or change `SEED_WIFI_PSK` to a wrong value. Expected: device retries 3× over ~45s, then comes up as `PoolMaster-Setup-XXXX`. Provision through the wizard; restore correct password.

- [ ] **Step 4: Commit**

Run:
```bash
git add src/Setup.cpp src/WiFiService.cpp include/WiFiService.h
git commit -m "sp1: fall back to AP mode after 3 failed WiFi attempts

Lets the user re-provision if their router changed without needing
USB access to the board."
```

---

## Phase 7: Home Assistant Discovery

### Task 20: `HaDiscovery.cpp` — publish all entity configs on MQTT connect

**Files:**
- Create: `include/HaDiscovery.h`
- Create: `src/HaDiscovery.cpp`
- Modify: `src/MqttBridge.cpp` (call `HaDiscovery::publishAll()` from `onMqttConnect`)

HA Discovery publishes one retained JSON blob per entity to `homeassistant/<component>/<entity_id>/config`. All entities share one `device:` object to keep them grouped.

- [ ] **Step 1: Create `include/HaDiscovery.h`**

Create file `include/HaDiscovery.h`:
```cpp
#pragma once

namespace HaDiscovery {
  // Build + publish all entity config messages. Retained.
  void publishAll();
  // Publish avail="online" on the LWT topic.
  void publishAvail();
  // Helper used by MqttPublish.cpp for state topics.
  String stateTopic(const char* entityId);
  String setTopic(const char* entityId);
  String availTopic();
}
```

- [ ] **Step 2: Create `src/HaDiscovery.cpp`**

Create file `src/HaDiscovery.cpp`:
```cpp
#include <Arduino.h>
#include <espMqttClient.h>
#include <ArduinoJson.h>
#include "HaDiscovery.h"
#include "Credentials.h"
#include "Config.h"
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
  // Extras for `button`: (payload_press defaults to "PRESS")
  const char* buttonAction;   // opaque; carried in command_topic and decoded by MqttBridge
};

static const Entity ENTITIES[] = {
  // ---- measurements ----
  {"sensor", "ph",                 "pH",                     "mdi:alpha-p-circle", nullptr,    nullptr,       false,0,0,0, nullptr},
  {"sensor", "orp",                "ORP",                    "mdi:flash",          "mV",       "voltage",     false,0,0,0, nullptr},
  {"sensor", "water_temp",         "Water temperature",      nullptr,              "°C",       "temperature", false,0,0,0, nullptr},
  {"sensor", "air_temp",           "Air temperature",        nullptr,              "°C",       "temperature", false,0,0,0, nullptr},
  {"sensor", "pressure",           "Water pressure",         nullptr,              "bar",      "pressure",    false,0,0,0, nullptr},
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
  {"number", "water_temp_setpoint","Water temperature setpoint", nullptr,          "°C",       "temperature", true,  15,   35,   0.5f, nullptr},

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
    addDevice(doc.to<JsonObject>());

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
  publishAvail();
}

} // namespace HaDiscovery
```

- [ ] **Step 3: Invoke from `MqttBridge::onMqttConnect`**

In `src/MqttBridge.cpp`, modify `onMqttConnect`:
```cpp
#include "HaDiscovery.h"   // add to includes

static void onMqttConnect(bool sessionPresent)
{
  Debug.print(DBG_INFO, "[MQTT] Connected, session: %d", sessionPresent);
  // Legacy compatibility subscription — removed in Task 23.
  mqttClient.subscribe(PoolTopicAPI_Legacy, 2);

  HaDiscovery::publishAll();
  MQTTConnection = true;
}
```

- [ ] **Step 4: Compile**

Run:
```bash
pio run -e serial_upload
```
Expected: clean.

- [ ] **Step 5: Bench smoke**

Flash, boot. On the broker, subscribe to `homeassistant/#` and count retained messages:
```bash
mosquitto_sub -h <broker> -t 'homeassistant/#' -v | head -50
```
Expected: 38 config messages appear on first MQTT connect (matching the `ENTITIES` table length). HA's "Devices" page shows one PoolMaster device.

- [ ] **Step 6: Commit**

Run:
```bash
git add src/HaDiscovery.cpp include/HaDiscovery.h src/MqttBridge.cpp
git commit -m "sp1: HaDiscovery — publish ~38 entity configs on MQTT connect

Retained discovery messages let HA auto-populate the PoolMaster device
without user-written YAML. Subscribes to set topics for controllable
entities as part of the same pass."
```

---

### Task 21: Rewrite `MqttPublish.cpp` to publish HA state topics

**Files:**
- Replace contents of: `src/MqttPublish.cpp` (was `Publish.cpp`)

The existing file publishes 7 legacy JSON messages on 7 legacy topics. The new version publishes simple string payloads per HA entity to its state topic.

- [ ] **Step 1: Replace `src/MqttPublish.cpp` contents**

Replace the entire contents of `src/MqttPublish.cpp` with:
```cpp
// MQTT state-topic publisher for HA Discovery entities.
// Publishes simple string payloads (not JSON) — HA-native conventions.

#undef __STRICT_ANSI__
#include <Arduino.h>
#include "Config.h"
#include "PoolMaster.h"
#include "HaDiscovery.h"
#include "Credentials.h"

int freeRam(void);
void stack_mon(UBaseType_t&);

static const char* ON  = "ON";
static const char* OFF = "OFF";

static void pubStr(const char* entityId, const char* val) {
  if (!mqttClient.connected()) return;
  mqttClient.publish(HaDiscovery::stateTopic(entityId).c_str(), 0, true, val);
}

static void pubFloat(const char* entityId, double val, int decimals = 2) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%.*f", decimals, val);
  pubStr(entityId, buf);
}

static void pubInt(const char* entityId, long val) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%ld", val);
  pubStr(entityId, buf);
}

static void pubBool(const char* entityId, bool v) { pubStr(entityId, v ? ON : OFF); }

// ---- Tasks ----

// Publishes settings (setpoints, PID params, etc.) — value entities only,
// plus diagnostic firmware_version. Called when notified.
void SettingsPublish(void *pvParameters)
{
  while(!startTasks);
  vTaskDelay(DT9);
  static UBaseType_t hwm = 0;

  for(;;)
  {
    if (mqttClient.connected())
    {
      pubFloat("ph_setpoint",         storage.Ph_SetPoint, 2);
      pubFloat("orp_setpoint",        storage.Orp_SetPoint, 0);
      pubFloat("water_temp_setpoint", storage.WaterTemp_SetPoint, 1);
      pubStr  ("firmware_version",    FIRMW);
    }

    stack_mon(hwm);
    ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
  }
}

// Publishes measurements every PublishPeriod or when notified.
void MeasuresPublish(void *pvParameters)
{
  while(!startTasks);
  vTaskDelay(DT8);

  TickType_t WaitTimeOut = (TickType_t)storage.PublishPeriod / portTICK_PERIOD_MS;
  static UBaseType_t hwm = 0;

  for(;;)
  {
    ulTaskNotifyTake(pdFALSE, WaitTimeOut);

    if (mqttClient.connected())
    {
      // measurements
      pubFloat("ph",          storage.PhValue, 2);
      pubFloat("orp",         storage.OrpValue, 0);
      pubFloat("water_temp",  storage.TempValue, 1);
      pubFloat("air_temp",    storage.TempExternal, 1);
      pubFloat("pressure",    storage.PSIValue, 2);

      // runtimes
      pubInt("filt_uptime_today",      FiltrationPump.UpTime / 1000);
      pubInt("ph_pump_uptime_today",   PhPump.UpTime / 1000);
      pubInt("chl_pump_uptime_today",  ChlPump.UpTime / 1000);

      // fills
      pubFloat("acid_fill", PhPump.GetTankFill(), 0);
      pubFloat("chl_fill",  ChlPump.GetTankFill(), 0);

      // modes / switches
      pubBool("auto_mode",       storage.AutoMode);
      pubBool("winter_mode",     storage.WinterMode);
      pubBool("ph_pid",          PhPID.GetMode() == AUTOMATIC);
      pubBool("orp_pid",         OrpPID.GetMode() == AUTOMATIC);
      pubBool("heating",         false);   // TODO-SP2: wire to heat flag once moved into StoreStruct

      // pumps
      pubBool("filtration_pump", FiltrationPump.IsRunning());
      pubBool("ph_pump",         PhPump.IsRunning());
      pubBool("chl_pump",        ChlPump.IsRunning());
      pubBool("robot_pump",      RobotPump.IsRunning());
      pubBool("relay_r0_projecteur", !digitalRead(RELAY_R0));
      pubBool("relay_r1_spare",      !digitalRead(RELAY_R1));

      // alarms
      pubBool("pressure_alarm",    PSIError);
      pubBool("ph_pump_overtime",  PhPump.UpTimeError);
      pubBool("chl_pump_overtime", ChlPump.UpTimeError);
      pubBool("acid_tank_low",     !PhPump.TankLevel());
      pubBool("chl_tank_low",      !ChlPump.TankLevel());

      // diagnostics
      pubInt("uptime",     millis() / 1000);
      pubInt("free_heap",  ESP.getFreeHeap());
      pubInt("wifi_rssi",  WiFi.RSSI());
    }

    WaitTimeOut = (TickType_t)storage.PublishPeriod / portTICK_PERIOD_MS;
    stack_mon(hwm);
  }
}
```

- [ ] **Step 2: Compile**

Run:
```bash
pio run -e serial_upload
```
Expected: clean (the `heating` TODO-SP2 is a runtime note, not a compile issue).

- [ ] **Step 3: Bench smoke**

Flash, boot with an MQTT broker configured. Subscribe:
```bash
mosquitto_sub -h <broker> -t 'poolmaster/+/+/state' -v
```
Expected: ~30 state messages on the publish cycle (every 30s). HA entities populate with actual values.

- [ ] **Step 4: Commit**

Run:
```bash
git add src/MqttPublish.cpp
git commit -m "sp1: MqttPublish — publish HA state topics with simple payloads

Replaces legacy Meas1/Meas2/Set1-5 JSON topics with per-entity state
payloads matching the Discovery configs from Task 20."
```

---

### Task 22: HA set-topic translation in `MqttBridge.cpp`

**Files:**
- Modify: `src/MqttBridge.cpp`

espMqttClient's onMessage now receives messages on both the legacy `/Home/Pool/API` topic AND the HA `.../set` topics. The translation table turns HA payloads (`ON`/`OFF`/`7.5`) into the existing JSON command format that `ProcessCommand` understands (`{"FiltPump":1}`, `{"PhSetPoint":7.5}`).

- [ ] **Step 1: Add translation table and dispatch**

In `src/MqttBridge.cpp`, add the following helper functions near the top (after includes, before `onMqttMessage`):

```cpp
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

// Relay number commands use the generic {"Relay":[N,state]} form
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
```

- [ ] **Step 2: Route incoming set-topic messages through `dispatchHaSet`**

Replace the existing `onMqttMessage` function with:
```cpp
static void onMqttMessage(const espMqttClientTypes::MessageProperties& /*props*/,
                          const char* topic,
                          const uint8_t* payload,
                          size_t len, size_t /*index*/, size_t /*total*/)
{
  char payloadStr[32];
  size_t n = len < sizeof(payloadStr) - 1 ? len : sizeof(payloadStr) - 1;
  memcpy(payloadStr, payload, n);
  payloadStr[n] = '\0';

  // Legacy path — retained until Task 23 removes it.
  if (strcmp(topic, PoolTopicAPI_Legacy) == 0) {
    char Command[100];
    size_t m = len < sizeof(Command) - 1 ? len : sizeof(Command) - 1;
    memcpy(Command, payload, m);
    Command[m] = '\0';
    enqueueCommand(Command);
    return;
  }

  // HA set topic path
  char entityId[48];
  if (parseSetTopic(topic, entityId, sizeof(entityId))) {
    Debug.print(DBG_INFO, "[MQTT] HA set: entity=%s payload=%s", entityId, payloadStr);
    dispatchHaSet(entityId, payloadStr);
    return;
  }

  Debug.print(DBG_WARNING, "[MQTT] Unexpected topic: %s", topic);
}
```

- [ ] **Step 3: Compile**

Run:
```bash
pio run -e serial_upload
```
Expected: clean.

- [ ] **Step 4: Bench smoke**

Flash, boot. From HA's "Developer tools → Services", call `switch.turn_on` / `switch.turn_off` on `switch.poolmaster_filtration_pump`. Listen with:
```bash
mosquitto_sub -h <broker> -t 'poolmaster/+/+/set' -v
```
Expected: see the set payloads arrive. On the ESP32 serial log: `[MQTT] HA set: entity=filtration_pump payload=ON` followed by `[MQTT] Queued: {"FiltPump":1}`. Relay clicks.

Publish a setpoint via mosquitto:
```bash
mosquitto_pub -h <broker> -t "poolmaster/$(hexdump -ve '/1 "%02x"' /dev/urandom | head -c12)/ph_setpoint/set" -m "7.4"
```
Use your actual device MAC. Expected: `storage.Ph_SetPoint` updates, `poolmaster/.../ph_setpoint/state` echoes `7.40` within the next publish cycle.

- [ ] **Step 5: Commit**

Run:
```bash
git add src/MqttBridge.cpp
git commit -m "sp1: HA set-topic translation — HA payloads to legacy JSON commands

Four translation tables (switches, numbers, relays, buttons) map HA
entity set topics onto the existing ProcessCommand JSON vocabulary
without duplicating business logic."
```

---

### Task 23: Retire legacy MQTT topics and cleanup sweep

**Files:**
- Modify: `src/MqttBridge.cpp`

- [ ] **Step 1: Remove the legacy API subscription + topic handling**

In `src/MqttBridge.cpp`:

1. In `onMqttConnect`, delete the line:
   ```cpp
   mqttClient.subscribe(PoolTopicAPI_Legacy, 2);
   ```
2. In `onMqttMessage`, delete the legacy-path block (the `if (strcmp(topic, PoolTopicAPI_Legacy) == 0)` branch).
3. Delete the constant `static const char* PoolTopicAPI_Legacy = POOLTOPIC_LEGACY "API";`.

- [ ] **Step 2: Add a one-shot cleanup sweep**

Add a new function to `MqttBridge.cpp`, called from `onMqttConnect` after `HaDiscovery::publishAll()`:

```cpp
#include <Preferences.h>

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
```

Then in `onMqttConnect`:
```cpp
static void onMqttConnect(bool sessionPresent)
{
  Debug.print(DBG_INFO, "[MQTT] Connected, session: %d", sessionPresent);
  HaDiscovery::publishAll();
  sweepLegacyRetainedTopics();
  MQTTConnection = true;
}
```

- [ ] **Step 3: Compile + flash**

Run:
```bash
pio run -e serial_upload -t upload -t monitor
```
Expected: clean. Serial log shows the sweep run once.

- [ ] **Step 4: Verify sweep**

Before flashing, confirm your broker retains at least one of the old topics:
```bash
mosquitto_sub -h <broker> -t 'Home/Pool/#' -v
```
If any retained messages exist, you'll see them. After the ESP32 boots with the new firmware and runs the sweep once, re-run the sub — they should be gone.

- [ ] **Step 5: Commit**

Run:
```bash
git add src/MqttBridge.cpp
git commit -m "sp1: retire legacy MQTT topics + one-shot retained-topic sweep

Legacy /Home/Pool(6)/{API,Status,Err,Meas1-2,Set1-5} are evicted from
the broker on first boot and never subscribed again. Sweep is idempotent:
guarded by NVS flag mqtt/legacy_swept."
```

---

### Task 24: Wire action buttons to `ProcessCommand`

**Files:**
- Modify: `src/CommandQueue.cpp` (was `PoolServer.cpp`)

The translation table in Task 22 already produces `{"Winter":1}`, `{"Heat":1}`, and `{"RobotPump":1}` — but `ProcessCommand` in CommandQueue.cpp may not handle those exact keys today. Audit and extend.

- [ ] **Step 1: Audit existing command dispatch**

Run:
```bash
grep -n '"Mode"\|"Heat"\|"FiltPump"\|"PhPump"\|"ChlPump"\|"Winter"\|"RobotPump"\|"Clear"' src/CommandQueue.cpp
```
Expected: most are present. `"Winter"` and `"RobotPump"` may be missing (check).

- [ ] **Step 2: Add any missing command handlers**

For each command not handled, add a branch in `ProcessCommand`. Example for `Winter`:
```cpp
else if (command["Winter"].is<int>()) {
  storage.WinterMode = (command["Winter"].as<int>() == 1);
  saveParam("WinterMode", storage.WinterMode);
  Debug.print(DBG_INFO, "Winter mode: %d", storage.WinterMode);
  PublishSettings();   // echo on state topic
}
```

For `RobotPump`:
```cpp
else if (command["RobotPump"].is<int>()) {
  if (command["RobotPump"].as<int>() == 1) RobotPump.Start();
  else RobotPump.Stop();
  PublishSettings();
}
```

Insert these in the existing `ProcessCommand` dispatch chain, alphabetically near the other pump/mode handlers. Use the existing code style.

- [ ] **Step 3: Compile**

Run:
```bash
pio run -e serial_upload
```
Expected: clean.

- [ ] **Step 4: Bench test button dispatch**

Flash. From HA's UI, press each of the 7 action buttons. Serial log should show the corresponding queue insertion. Verify:
- `button.poolmaster_clear_errors` → `storage.PhPumpUpTimeLimit` flags reset.
- `button.poolmaster_acid_tank_refill` → `PhPump.GetTankFill()` returns to 100 on next publish.
- `button.poolmaster_reboot` → device reboots after 5s (ProcessCommand's existing `Reboot` handler does this).

- [ ] **Step 5: Commit**

Run:
```bash
git add src/CommandQueue.cpp
git commit -m "sp1: wire missing HA set-topic commands (Winter, RobotPump) into ProcessCommand"
```

---

## Phase 8: Polish + validation

### Task 25: Update `README.md`

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add a new top-level section**

Prepend (before the existing `<h2>ESP32 PoolMaster</h2>`) the following section:
```markdown
## ⚡ SP1 Modern Plumbing (2026-04) — breaking changes

Version **ESP-SP1** modernizes the firmware plumbing. Upgrading from an earlier
version is a one-way migration; read this before flashing.

### First-install flow (new devices)
1. Serial-flash the firmware **and** the LittleFS image:
   ```bash
   pio run -e serial_upload -t upload
   pio run -e serial_upload -t uploadfs
   ```
2. On first boot, the device starts in AP mode as `PoolMaster-Setup-XXXX` (no password).
3. Join from a phone — captive-portal popup auto-opens.
4. Complete the wizard: WiFi (required), admin password (skippable), MQTT broker (skippable),
   timezone (skippable).
5. Device reboots, joins your WiFi, appears at `http://poolmaster.local/`.
6. If Home Assistant is running with MQTT, `PoolMaster` device auto-appears in Devices
   (no YAML required).

### Upgrade flow (existing SP-3.3b devices)

Your existing `WIFI_NETWORK`/`WIFI_PASSWORD`/`OTA_PWDHASH`/`MQTT_SERVER_IP` macros in
`Config.h` are gone. Copy their values into `include/Secrets.h.example` → `include/Secrets.h`
(gitignored) as the `SEED_*` fields. On first boot after flashing, the seed values are
copied into NVS; you can then blank `Secrets.h` and subsequent boots use NVS.

**Flash with serial once** — OTA from SP-3.3b to SP1 is unsupported because the partition
table changes:

```bash
pio run -e serial_upload -t erase         # wipes NVS + OTA slots
pio run -e serial_upload -t upload
pio run -e serial_upload -t uploadfs
```

After the first successful SP1 boot, subsequent firmware updates use OTA:

```bash
POOLMASTER_OTA_PWD='<your admin pwd>' pio run -e OTA_upload -t upload
```

Or upload a binary from the web UI at `http://poolmaster.local/update`.

### Home Assistant migration

The legacy topics (`/Home/Pool/Meas1`, `/Home/Pool/API`, etc.) are no longer published.
Tear down any custom `sensor:` / `switch:` MQTT YAML in `configuration.yaml` that referenced
those topics. SP1 publishes HA Discovery configs; your PoolMaster will appear automatically
under Settings → Devices → PoolMaster.
```

- [ ] **Step 2: Commit**

Run:
```bash
git add README.md
git commit -m "sp1: document first-install and upgrade flows in README"
```

---

### Task 26: Final bench smoke checklist

**Files:** none

- [ ] **Step 1: Clean room rebuild and flash**

Run:
```bash
pio run -e serial_upload -t erase
pio run -e serial_upload -t upload
pio run -e serial_upload -t uploadfs
pio device monitor
```

- [ ] **Step 2: Run through the 10-step spec §9 smoke checklist**

Tick each:
1. [ ] Device enters AP mode, `PoolMaster-Setup-XXXX` visible, phone captive-portal opens automatically.
2. [ ] Wizard completes in under 60 seconds, device reboots into STA mode, joins home WiFi.
3. [ ] `ping poolmaster.local` resolves.
4. [ ] `http://poolmaster.local/` shows placeholder page; `/update` prompts for Basic auth.
5. [ ] `mosquitto_sub -h <broker> -t 'homeassistant/#' -v | head -50` → see ~38 config messages.
6. [ ] HA "Devices" shows the PoolMaster device with all listed entities and correct units/icons.
7. [ ] `switch.poolmaster_filtration_pump` toggle in HA → relay audibly clicks.
8. [ ] `mosquitto_pub -h <broker> -t "poolmaster/$DEVICE_ID/ph_setpoint/set" -m "7.4"` → `storage.Ph_SetPoint` updates + echoed on state topic.
9. [ ] `pio run -e OTA_upload -t upload` succeeds on port 3232.
10. [ ] Upload a test firmware via `/update` in browser → device reboots to new version, HA entities reappear.
11. [ ] Pull WiFi on router for 60s → device retries 3× → enters AP mode → reprovision → rejoins.
12. [ ] Nextion display continues to show live readings throughout.

- [ ] **Step 3: 24-hour burn-in — compare vs. baseline**

Leave device running overnight. After 24h:
```bash
pio device monitor -e serial_upload  # reconnect to check
```
Expected:
- pH/ORP/temp values stable and reasonable
- FiltrationPump followed its schedule
- No watchdog resets in log (`grep 'rst_reason' /tmp/serial-24h.log`)
- HA history graph for `sensor.poolmaster_ph` is continuous (no gaps)

- [ ] **Step 4: Compare binary size to baseline**

Run:
```bash
pio run -e serial_upload | grep -E 'RAM:|Flash:' | tee /tmp/sp1-final-size.txt
diff docs/superpowers/notes/sp1-baseline-size.txt /tmp/sp1-final-size.txt
```
Expected: Flash use grew significantly (ESPAsyncWebServer + LittleFS + new modules) but stays under 1.4 MB (fits in either OTA slot). RAM growth should be under 20%.

- [ ] **Step 5: Final commit**

Run:
```bash
mv /tmp/sp1-final-size.txt docs/superpowers/notes/sp1-final-size.txt
git add docs/superpowers/notes/sp1-final-size.txt
git commit -m "chore(sp1): record final binary size after SP1 complete"
```

- [ ] **Step 6: Merge plan**

Discuss with user whether to:
- Merge `sp1-modern-plumbing` to `main` via PR
- Squash-merge with a single commit message summarizing all SP1 work
- Or keep the branch alive for more iteration

Do NOT merge without the user's explicit approval.

---

## Self-review notes (inline, not run)

**Spec coverage check:** Every section of the spec is covered by at least one task:
- §4 Architecture → Tasks 9, 15, 20 (web, OTA, MQTT Discovery wiring)
- §5 Library stack → Task 2 (platformio.ini)
- §6.1 File layout → Tasks 10, 11, 12 (reorg)
- §6.2 Module interfaces → Tasks 13–17, 20
- §6.3 HA Discovery entities → Task 20 (includes all categories incl. actions + diagnostics)
- §6.4 Provisioning flow → Tasks 17, 18, 19
- §6.5 OTA → Tasks 15, 16 (both paths, partitions from Task 2)
- §6.6 Security → Tasks 13, 14 (SHA-256 + Basic auth)
- §7 Migration → Task 25 (README), Tasks 2 + 3 (platformio + Secrets.h)
- §8 Risks → Mitigations distributed across tasks (ArduinoJson migration uses Phase 2 one-file-at-a-time strategy; Nextion untouched confirmed in task set; partition table change handled by Task 2 + Task 25 README upgrade instruction)
- §9 Testing → Task 26 (checklist)
- §10 Forward-compat → RelayOutput is deliberately absent from the plan (correct per spec §10)

**Type consistency check:** `Credentials::` namespace functions are referenced consistently across Tasks 8, 9, 13, 15, 16, 17, 20. The `webServer` global is defined once (Task 15) and used in Tasks 16, 17. `enqueueCommand` is defined in Task 10 (in the header) and called in Task 22. `HaDiscovery::stateTopic`/`setTopic`/`availTopic` are defined in Task 20 and consumed in Task 21.

**Placeholder scan:** One residual `TODO-SP2` comment in Task 21 for the `heating` entity (currently hardcoded to `false`) — this is a deliberate punt to SP2 when heating state moves into `StoreStruct`. All other steps contain concrete code or concrete commands.
