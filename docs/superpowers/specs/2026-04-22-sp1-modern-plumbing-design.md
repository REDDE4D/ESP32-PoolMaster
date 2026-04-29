# ESP32-PoolMaster — Sub-Project 1: Modern Plumbing

**Status:** Approved (design)
**Date:** 2026-04-22
**Scope:** Dependency modernization, web server, OTA, captive-portal onboarding, HA Discovery
**Owner:** Sebastian

---

## 0. Roadmap context

This spec covers the first of five modernization sub-projects. Later sub-projects each get their own spec and implementation cycle.

| # | Sub-project | Status |
|---|---|---|
| **SP1** | **Modern plumbing** (this spec): deps bump, web server, OTA, LittleFS, captive-portal WiFi onboarding, HA Discovery, admin auth | **In design** |
| SP2 | Settings promotion: move remaining compile-time fields from `Config.h` into NVS + simple web forms | Not started |
| SP3 | Beautiful web UI: dashboard, calibration wizards, charts, control surfaces | Not started |
| SP4 | External relay outputs: per-output routing to local GPIO, Shelly (HTTP Gen1/Gen2+, MQTT), or generic MQTT switch | Not started |
| SP5 | Nextion HMI redesign as lean status panel (the "beautiful" UI lives in SP3 on the browser, not on the Nextion) | Not started |

**Locked upstream decisions that constrain SP1:**
- Display hardware: keep existing AiHasd 3.5" 480×320 Nextion serial HMI. Touch UI strategy is "Nextion stays lean, web UI carries the weight" (SP3/SP5).
- MQTT role: Home Assistant bridge only. Legacy consumers (Node-RED, Blynk, Jeedom, Grafana-via-MQTT) are explicitly not preserved.
- MQTT topology: full HA Discovery cutover. Old `/Home/Pool/Meas1`, `/Home/Pool/API` JSON scheme is removed.
- Network/trust model: home LAN, no internet exposure, no TLS. Single shared admin password protects web UI and OTA.

---

## 1. Problem statement

The existing firmware is functional but aging:
- Library pins are 3–5 years stale; `AsyncMqttClient` and `me-no-dev/AsyncTCP` are unmaintained.
- No HTTP interface. mDNS advertises `http._tcp` on port 8060 but nothing listens.
- OTA works but uses a non-standard port and a compile-time password hash.
- WiFi credentials, MQTT broker IP, and OTA password are hardcoded in `Config.h` — every user must edit source and reflash over USB.
- Home Assistant integration requires hand-written YAML to decode the custom JSON topics.
- `DISABLE_FLASH` build flag disables LittleFS, so there's no way to ship static assets.

SP1 modernizes the plumbing needed to fix all of the above, without touching pool control logic.

## 2. Goals and non-goals

### Goals
1. Bump every library to a currently-maintained version on a supported ESP32 Arduino core.
2. Introduce a password-protected HTTP server on port 80, serving a placeholder page from LittleFS.
3. Introduce a password-protected web OTA endpoint (`POST /update`), retaining ArduinoOTA for developer push.
4. Replace hardcoded WiFi credentials with a first-run captive-portal setup wizard. Optional postponable steps for admin password, MQTT/HA, timezone, pool basics.
5. Cut MQTT over to Home Assistant Discovery. One HA device with ~30 entities. No custom YAML required in HA.
6. Refactor the source tree so the new modules are each independently understandable and reviewable.

### Non-goals
- Any REST/JSON API beyond `GET /`, `POST /update`, and the wizard routes. No `/api/state`, no WebSocket. (Deferred to SP3.)
- Any UI beyond the placeholder home page and the setup wizard. (Deferred to SP3.)
- Any change to pool control logic, Nextion display code, or the `Pump` class. (Respectively: permanent boundary, deferred to SP5, deferred to SP4.)
- Promoting remaining `Config.h` fields to NVS. (Deferred to SP2, except WiFi/admin/OTA/MQTT-broker credentials which must land now.)
- Automated test infrastructure. (Manual smoke checklist only for SP1.)

## 3. Success criteria

1. `pio run` compiles clean for both `serial_upload` and `OTA_upload` environments with no deprecated-warnings.
2. A factory-wiped device boots into AP mode, a phone can join `PoolMaster-Setup-XXXX`, the wizard walks through WiFi in under 60 seconds, device joins the home network.
3. `ping poolmaster.local` resolves. `http://poolmaster.local` serves the placeholder page behind HTTP Basic auth.
4. Home Assistant auto-discovers one "PoolMaster" device with all measurement sensors, mode switches, pump switches, setpoint numbers, alarms, and action buttons listed in §6.3 — with correct units, device classes, and icons — without any user-written YAML.
5. `pio run -e OTA_upload -t upload` pushes firmware successfully to port 3232.
6. Uploading a firmware binary via browser `POST /update` reboots the device onto the new image; a failed upload leaves the device running the previous image.
7. Pool control behavior (pH/ORP regulation, filtration scheduling, Nextion display, pump interlocks) is indistinguishable from pre-SP1 in a 24-hour burn-in.

## 4. Architecture

```
                       ┌───────────────────────────────────────┐
                       │                ESP32                  │
                       │                                       │
  Browser / HA ──HTTP──┤  AsyncWebServer :80                   │
   (LAN)               │    ├─ GET  /           → LittleFS     │
                       │    ├─ POST /update     → Update.h OTA │
                       │    └─ (wizard routes in AP mode only) │
                       │                                       │
  PlatformIO ──espota──┤  ArduinoOTA :3232 (core default)      │
                       │                                       │
  HA broker  ──MQTT────┤  espMqttClient                        │
                       │    ├─ publish homeassistant/.../config│  ← Discovery on connect
                       │    ├─ publish poolmaster/<mac>/.../state  ← on change + periodic
                       │    └─ subscribe poolmaster/<mac>/.../set  → queueIn
                       │                                       │
                       │  ┌────────── queueIn ──────────┐      │
                       │  │ ProcessCommand task (unchanged)    │
                       │  │   mutates StoreStruct, saveConfig()│
                       │  └────────────────────────────────────┘
                       │                                       │
                       │  Existing 10 FreeRTOS tasks           │
                       │  (AnalogPoll, PID, Publish, etc.)     │
                       │                                       │
                       │  Nextion (unchanged)                  │
                       └───────────────────────────────────────┘
```

### Key architectural principles
1. **No new command path.** REST (later) and MQTT both enqueue JSON strings into the existing `queueIn`, processed by the existing `ProcessCommand` task. Zero duplication of business logic.
2. **Async everywhere.** ESPAsyncWebServer runs on AsyncTCP (own task). espMqttClient runs on its own FreeRTOS task. Neither blocks the 10 existing tasks.
3. **One settings mutator.** Only `ProcessCommand` writes `StoreStruct` and calls `saveConfig()`. Read-only access is allowed from anywhere; the PID/pump tasks already do this.
4. **MQTT is a publisher/subscriber of state, not a command bus.** Separate state topic and set topic per entity, canonical HA payloads.

## 5. Library stack

| Concern | Library | Notes |
|---|---|---|
| ESP32 Arduino core | 2.0.17 | Pinned at `platform = espressif32 @ ~6.9` in `platformio.ini` |
| Web server | `ESP32Async/ESPAsyncWebServer` | Community-maintained fork; API identical to the abandoned `me-no-dev/ESPAsyncWebServer` |
| Async TCP | `ESP32Async/AsyncTCP` | Matching fork, replaces `me-no-dev/AsyncTCP` |
| MQTT | `bertmelis/espMqttClient` | Modern async MQTT 3.1.1/5 client, replaces `AsyncMqttClient` |
| JSON | `bblanchon/ArduinoJson @ ^7` | v6 → v7 migration is mechanical across every JSON call site |
| Filesystem | `LittleFS` (from core) | For UI static assets and setup wizard HTML |
| PID | `br3ttb/PID @ ^1.2` | Already in use, keep |
| DS18B20 | `milesburton/DallasTemperature` | Bump to latest |
| Running median | `robtillaart/RunningMedian` | Bump to latest |
| Nextion | `seithan/Easy Nextion Library` | Keep pinned; do not touch |
| Debug | `Arduino_DebugUtils` | Keep |
| Time | `paulstoffregen/Time` | Keep (hosts the `tm` wrappers used throughout) |
| Email | `mobizt/ESP Mail Client` | Keep; SMTP alerts unchanged |
| Preferences (NVS) | From core | Used for WiFi/MQTT/admin creds |
| Pump class | `lib/Pump-master/` | Local, untouched in SP1 |

Libraries removed: `me-no-dev/AsyncTCP`, `marvinroger/AsyncMqttClient`.

## 6. Detailed design

### 6.1 File layout

| Before | After | Responsibility |
|---|---|---|
| `src/Setup.cpp` (24 KB) | `src/Setup.cpp` (slim) | Boot orchestration only |
| — | `src/Settings.cpp` (NEW) | `loadConfig`, `saveConfig`, `saveParam` extracted from Setup |
| `src/mqtt_comm.cpp` (split) | `src/WiFiService.cpp` (NEW) | WiFi events, reconnect timers, AP-mode fallback trigger |
| — | `src/Provisioning.cpp` (NEW) | AP mode bring-up, DNSServer loop, wizard POST handlers |
| — | `src/OtaService.cpp` (NEW) | ArduinoOTA config + `/update` HTTP handler |
| — | `src/WebServer.cpp` (NEW) | AsyncWebServer instance, route registration, Basic auth middleware |
| — | `src/WebAuth.cpp` (NEW) | Password-hash compare; NVS-backed admin credential |
| — | `src/HaDiscovery.cpp` (NEW) | Builds + publishes `homeassistant/.../config` payloads |
| `src/mqtt_comm.cpp` (split) | `src/MqttBridge.cpp` | espMqttClient wrapper, HA state/command topics |
| `src/PoolServer.cpp` | `src/CommandQueue.cpp` (renamed) | `ProcessCommand` task body and JSON handlers; ArduinoJson v7 syntax |
| `src/Publish.cpp` | `src/MqttPublish.cpp` (renamed) | Publishes to HA state topics instead of legacy `Meas1`/`Meas2` |
| `src/Loops.cpp` | `src/Loops.cpp` | Untouched except v7 JSON syntax on any JSON calls |
| `src/PoolMaster.cpp` | `src/PoolMaster.cpp` | Untouched |
| `src/Nextion.cpp` | `src/Nextion.cpp` | **Permanently out of scope for SP1** |
| `src/mqtt_comm.cpp` | *(deleted)* | Replaced by the three files above |
| `include/Config.h` | `include/Config.h` | Pins, task periods, compile-time constants only |
| — | `include/Secrets.h.example` (NEW) | Template for optional dev-only WiFi seed + OTA password seed |
| — | `include/Secrets.h` (NEW, gitignored) | Local developer fill; optional fallback on first boot |
| `include/PoolMaster.h` | `include/PoolMaster.h` | `StoreStruct` + externs; swap `AsyncMqttClient` type to `espMqttClient::Client` |
| — | `data/index.html` (NEW) | Placeholder "PoolMaster alive — UI lands in SP3" |
| — | `data/setup.html` (NEW) | Self-contained onboarding wizard, no framework, ~15 KB |
| — | `data/setup.css` (NEW) | Minimal styling, ~2 KB |
| — | `data/robots.txt` (NEW) | `Disallow: /` |
| — | `partitions.csv` (NEW) | 2× 1.5 MB app + 1 MB LittleFS + coredump |

### 6.2 New module public interfaces (sketches)

```cpp
// WebServer.cpp
void WebServerInit(const char* adminPwdNvsKey);   // called from setup() after WiFi up

// OtaService.cpp
void OtaServiceInit(AsyncWebServer& srv, const char* adminPwdNvsKey);

// Provisioning.cpp
bool ProvisioningNeeded();                        // true if NVS lacks wifi_ssid
void ProvisioningRun();                           // blocks in AP mode until wizard finishes + saves; reboots

// HaDiscovery.cpp
void HaDiscoveryPublishAll();                     // called on MQTT connect
void HaDiscoveryClearAll();                       // on explicit request; removes retained configs

// CommandQueue.cpp (was PoolServer.cpp)
bool enqueueCommand(const char* jsonStr);         // used by MqttBridge AND web handlers

// MqttBridge.cpp
void MqttBridgeInit();                            // reads broker config from NVS; connects if configured
void MqttBridgePublishState(const char* entityId, const char* payload, bool retain);
```

### 6.3 HA Discovery entity inventory

All entities belong to one device:
```json
{
  "identifiers": ["poolmaster-<mac>"],
  "name": "PoolMaster",
  "manufacturer": "DIY (Loic74650 / ESP32 port)",
  "model": "ESP32 PoolMaster",
  "sw_version": "ESP-3.3b",
  "configuration_url": "http://poolmaster.local/"
}
```

Topic layout:
```
poolmaster/<mac>/avail                           ← LWT, "online"/"offline"
poolmaster/<mac>/<entity_id>/state               ← published by firmware
poolmaster/<mac>/<entity_id>/set                 ← subscribed by firmware (for controllable entities)
homeassistant/<component>/<entity_id>/config     ← retained discovery message
```

Entities:

| Category | Component | Entity | Unit | Controllable? |
|---|---|---|---|---|
| Measurements | sensor | `ph` | pH | no |
| | sensor | `orp` | mV | no |
| | sensor | `water_temp` | °C | no |
| | sensor | `air_temp` | °C | no |
| | sensor | `pressure` | bar | no |
| Pump runtimes | sensor | `filt_uptime_today` | s | no |
| | sensor | `ph_pump_uptime_today` | s | no |
| | sensor | `chl_pump_uptime_today` | s | no |
| Tank fills | sensor | `acid_fill` | % | no |
| | sensor | `chl_fill` | % | no |
| Modes | switch | `auto_mode` | — | yes |
| | switch | `winter_mode` | — | yes |
| | switch | `ph_pid` | — | yes |
| | switch | `orp_pid` | — | yes |
| | switch | `heating` | — | yes |
| Pumps / manual | switch | `filtration_pump` | — | yes |
| | switch | `ph_pump` | — | yes |
| | switch | `chl_pump` | — | yes |
| | switch | `robot_pump` | — | yes |
| | switch | `relay_r0_projecteur` | — | yes |
| | switch | `relay_r1_spare` | — | yes |
| Setpoints | number | `ph_setpoint` | pH | yes (6.5–8.0, step 0.1) |
| | number | `orp_setpoint` | mV | yes (400–900, step 10) |
| | number | `water_temp_setpoint` | °C | yes (15–35, step 0.5) |
| Alarms | binary_sensor | `pressure_alarm` | — | no |
| | binary_sensor | `ph_pump_overtime` | — | no |
| | binary_sensor | `chl_pump_overtime` | — | no |
| | binary_sensor | `acid_tank_low` | — | no |
| | binary_sensor | `chl_tank_low` | — | no |
| Diagnostics | sensor | `uptime` | s | no |
| | sensor | `free_heap` | bytes | no |
| | sensor | `wifi_rssi` | dBm | no |
| | sensor | `firmware_version` | — | no |
| Actions | button | `clear_errors` | — | triggers |
| | button | `acid_tank_refill` | — | triggers (marks acid tank = 100%) |
| | button | `chl_tank_refill` | — | triggers (marks chlorine tank = 100%) |
| | button | `reboot` | — | triggers (reboots after 5 s delay) |
| | button | `reset_ph_calib` | — | triggers |
| | button | `reset_orp_calib` | — | triggers |
| | button | `reset_psi_calib` | — | triggers |

Discovery lifecycle:
- On MQTT connect: publish all `homeassistant/.../config` messages (retained), then `avail=online` (retained, LWT=`offline`), then initial state for every entity.
- State publishes: measurements every `PublishPeriod` seconds (default 30 s). Switches/numbers immediately on change. Diagnostics every 60 s.
- On first SP1 boot against a broker that still holds pre-SP1 retained topics (`Home/Pool/Meas1`, `Home/Pool/Meas2`, `Home/Pool/Status`, `Home/Pool/Err`, `Home/Pool/API`, plus `_Dev` variants), publish retained empty payloads to those topics once to evict them from the broker. One-shot; controlled by an NVS flag that flips after the sweep runs successfully.
- On topic-scheme changes in future releases, publish retained empty payload to old `.../config` topics to clean up stale HA entities.

HA payloads are not JSON. `MqttBridge.cpp` translates incoming `.../set` payloads (`"ON"`, `"OFF"`, `"7.5"`, etc.) into the existing JSON command format that `ProcessCommand` already understands (`{"FiltPump":1}`, `{"PhSetPoint":7.5}`, etc.) before enqueueing. This lookup table is the one place where HA entity IDs and the legacy command vocabulary are wired together; keeping it in a single file means the rest of the codebase is unaware of the translation.

### 6.4 First-run / setup flow

Boot decision tree:

```
power on
   │
   ▼
NVS has wifi_ssid?
   │
   ├─ no  ──▶  AP MODE
   │            SSID: "PoolMaster-Setup-XXXX" (last 4 of MAC), open (no password)
   │            IP:   192.168.4.1
   │            DNSServer catches all queries → 192.168.4.1 (triggers OS captive portal)
   │            Serves /setup wizard from LittleFS
   │            Pool logic tasks NOT started (no point)
   │            Nextion untouched — shows its boot/default screen (dedicated setup-mode
   │            screen is deferred to SP5 since it requires HMI firmware changes)
   │
   └─ yes ──▶  STA MODE
                Try connect, up to 3 retries × 15 s
                   │
                   ├─ success ──▶  normal boot — start all 10 FreeRTOS tasks +
                   │               web server on :80 + MQTT if configured
                   │
                   └─ fail ──▶    fallback to AP MODE (so user can reprovision
                                   if router or password changed)
```

Wizard structure — every step except WiFi is skippable:

| # | Step | Required? | Behavior if skipped |
|---|---|---|---|
| 1 | WiFi scan + select + password | required | — |
| 2 | Admin password | skippable | Web UI runs with no auth until set later |
| 3 | MQTT broker + HA toggle | skippable | MQTT bridge + Discovery stay dormant; pool runs standalone |
| 4 | Timezone / NTP server | skippable | Defaults to current hardcoded `CET-1CEST` + pool.ntp.org |
| 5 | Pool basics (volume, pump flow rates, setpoints) | skippable | Defaults from existing `StoreStruct` |

Post-setup reachability:
- All wizard steps are revisitable from the main web UI under **Settings → WiFi / MQTT / Admin / Pool** (individual sections — no forced re-run).
- **Settings → Factory reset WiFi** wipes the `wifi_ssid`/`wifi_psk` NVS keys and reboots → back to AP mode. Does not touch pool settings.

Captive-portal probe handling: `DNSServer.processNextRequest()` redirects all DNS. HTTP routes serve 302 to `/setup` on these well-known probe paths, to reliably trigger the captive-portal popup on iOS, Android, and Windows respectively:
- `/generate_204` (Android)
- `/hotspot-detect.html` (iOS / macOS)
- `/ncsi.txt` and `/connecttest.txt` (Windows)

Library choice: roll our own using `WiFi.softAP()` + `DNSServer` + our own LittleFS-served HTML. No `tzapu/WiFiManager` (its UI can't be themed to match SP3's dashboard, and we own this ~300-LOC module cleanly).

### 6.5 OTA

Two complementary OTA paths, both authenticated.

**Path 1: ArduinoOTA (developer push)**
- Port 3232 (core default). The legacy `OTA_PORT 8063` is dropped.
- Password stored in NVS under key `ota_pwd`, not baked at compile time. Falls back to the admin password if `ota_pwd` is unset (one password to remember).
- Used via `pio run -e OTA_upload -t upload` with `upload_flags = --auth=<pwd>`.

**Path 2: Web-UI `/update` endpoint (end-user OTA)**
- `POST /update` on port 80, multipart form upload, HTTP Basic auth gated by admin password.
- Accepts either `firmware.bin` (U_FLASH) or `littlefs.bin` (U_SPIFFS); discriminated by form field name or `?type=littlefs` query.
- Uses ESP32 core's `Update.h` streaming API; writes to the inactive OTA partition.
- On success: responds 200, then `ESP.restart()` after 500 ms delay to flush the response.
- On failure: responds 400 with `Update.errorString()`; does NOT reboot; active partition unchanged.
- Watchdog fed each chunk (`esp_task_wdt_reset()`).

Partition table (`partitions.csv`):
```
# Name,   Type, SubType,  Offset,   Size,     Flags
nvs,      data, nvs,      0x9000,   0x5000
otadata,  data, ota,      0xe000,   0x2000
app0,     app,  ota_0,    0x10000,  0x170000   # 1.44 MB
app1,     app,  ota_1,    0x180000, 0x170000   # 1.44 MB
spiffs,   data, spiffs,   0x2f0000, 0x100000   # 1 MB LittleFS (subtype name stays "spiffs")
coredump, data, coredump, 0x3f0000, 0x10000
```

Rollback safety: ESP32's default OTA `otadata` tracks the active slot. If freshly flashed firmware panics before `esp_ota_mark_app_valid_cancel_rollback()`, the bootloader reverts automatically. SP1 does **not** call `mark_valid` ourselves — backlogged for a future QOL pass.

### 6.6 Security model

- Home LAN only. No TLS, no internet exposure. Documented in README.
- Single shared admin password, stored in NVS as a SHA-256 digest (via `mbedtls_md_sha256`), seeded during setup wizard. Note: SHA-256 is not a password-hashing function in the strict sense (no salt, no work factor). It is deliberate here — a home-LAN device with one administrator and a single credential does not justify the memory and CPU cost of bcrypt/argon2 on an ESP32, and the threat model excludes offline attackers with filesystem access.
- HTTP Basic auth on every non-public route (`/update`, any future `/api/*`). Public routes: `/`, static assets, `/setup` wizard in AP mode only.
- ArduinoOTA password stored in NVS under `ota_pwd`; falls back to admin password if unset.
- AP mode is open (no password) for ease of onboarding — acceptable because the AP window is short and only exposes the wizard.
- No session cookies, no rate limiting — YAGNI for a home-LAN device with one user.

## 7. Migration — breaking changes vs. current firmware

| Area | Before | After | Migration action |
|---|---|---|---|
| MQTT topics | `Home/Pool/Meas1`, `Home/Pool/API` (JSON command bus) | `poolmaster/<mac>/<entity>/...` HA Discovery | Tear down old HA YAML config for pool; HA auto-discovers new entities on first MQTT connect |
| OTA port | 8063 | 3232 (core default) | Update `platformio.ini` `upload_port` / `--port` flag |
| OTA password | `OTA_PWDHASH` macro | NVS key `ota_pwd` | Set during first-run wizard; falls back to admin password if unset |
| WiFi creds | `WIFI_NETWORK` / `WIFI_PASSWORD` macros | NVS keys `wifi_ssid` / `wifi_psk` via captive portal | Existing installs: on first boot of new firmware, if NVS has no `wifi_ssid`, fall back to compiled-in `Config.h` values for one boot and auto-seed NVS (seed shim removed in SP2) |
| Build flags | `-D DISABLE_FLASH` | LittleFS enabled | Remove flag; run `pio run -t uploadfs` once after first install |
| Libraries | `AsyncMqttClient`, `me-no-dev/AsyncTCP`, ArduinoJson 6 | `espMqttClient`, `ESP32Async/AsyncTCP`, ArduinoJson 7 | `platformio.ini` rewrite; every `.cpp` touching MQTT or JSON gets mechanical updates |

## 8. Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| ArduinoJson 6→7 migration misses a call site → runtime crash on first MQTT publish | Medium | High | Grep for every `StaticJsonDocument`/`DynamicJsonDocument`/`.createNestedObject`/`.createNestedArray` and port in one commit; compile with `-Werror=deprecated-declarations` |
| `espMqttClient` callback signatures differ from `AsyncMqttClient` → silent behavior change | Medium | Medium | Side-by-side diff of every callback; smoke-test by publishing from `mosquitto_pub` and verifying `queueIn` receives |
| Partition table change on first OTA → device boots into wrong partition | Low | Critical | First deploy is USB-serial only, not OTA. Document in README. Validate partition-table hash before flashing. |
| Web OTA times out on large firmware through slow WiFi | Medium | Medium | 120 s upload timeout; feed WDT each chunk; progress reporting |
| Captive-portal detection broken on some phones | Medium | Low | DNSServer + explicit 302 on `/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`, `/connecttest.txt` |
| Nextion display breaks via Serial2/UART timing | Low | Medium | SP1 explicitly forbids touching `Nextion.cpp`. Any PR modifying it is rejected at review. |
| LittleFS partition too small for SP3's future UI bundle | Low | Medium | 1 MB target; typical Vite+Preact+Tailwind bundle gzipped is ~80–200 KB. Headroom is comfortable. |
| Pool safety regression — pump stays on when it shouldn't | Low | Critical | SP1 deliberately does not touch `pHRegulation`, `OrpRegulation`, `PoolMaster` supervisory task, or `Pump` class. Changes isolated to network/settings/web layers. |

## 9. Testing plan

- **Static:** `pio run` compiles clean in both envs with `-Werror=return-type`, `-Wall`.
- **Bench smoke test (manual checklist):**
  1. Fresh-flash with wiped NVS → AP appears → wizard completes → device joins WiFi → `ping poolmaster.local` resolves.
  2. `http://poolmaster.local` → serves placeholder page, HTTP Basic auth prompt fires.
  3. `mosquitto_sub -h <broker> -t 'homeassistant/#' -v` → ~30 retained config messages on first MQTT connect.
  4. HA "Devices" page → "PoolMaster" appears with all listed entities, correct units and icons.
  5. Toggle `switch.poolmaster_filtration_pump` in HA → relay audibly clicks.
  6. Publish to `poolmaster/<mac>/ph_setpoint/set` value `7.5` → `storage.Ph_SetPoint` updates, saved to NVS, echoed back on `.../state`.
  7. `pio run -e OTA_upload -t upload` → succeeds on port 3232.
  8. Browser `POST /update` with a new firmware binary → device reboots to new version, WiFi reconnects, HA entities reappear.
  9. Pull WiFi on the router → device retries 3× → falls into AP mode → reprovision → rejoins.
  10. Nextion display still updates readings correctly throughout all of the above.
- **Regression test for pool logic:** 24-hour burn-in after merge. pH/ORP regulation loops must exhibit the same setpoint-tracking behavior as pre-SP1 firmware (compare logs or Grafana).
- **No automated unit tests** — embedded firmware, no existing harness, YAGNI for SP1.

## 10. Forward-compatibility notes

- **SP4 will introduce a `RelayOutput` abstraction** so each pump/relay can route to local GPIO, Shelly (HTTP Gen1/Gen2+, MQTT), or generic MQTT. SP1 deliberately does not preempt this by adding the abstraction — keeping the `Pump` public API untouched so SP4 can reshape it cleanly when a concrete user exists.
- **SP3 will add REST + WebSocket API** on the same AsyncWebServer instance from §6.2. `WebServerInit()` is the intended extension point.
- **SP2 will promote remaining `Config.h` compile-time fields to NVS** and remove the `Secrets.h` fallback shim introduced in §7.
- **SP5 Nextion redesign** will happen entirely within `src/Nextion.cpp` and the Nextion firmware project; SP1 touches neither.
