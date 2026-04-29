# SP5 Custom Output Switches Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 8 user-defined custom output switches on top of the 6 fixed SP4 slots — configurable via Settings UI, exposed to Home Assistant with stable unique IDs, reusing the existing `OutputDriver` interface and reboot-on-save pattern.

**Architecture:** Extend SP4's 6-slot driver registry to 14 slots (6 fixed + 8 `custom_N`). Custom slots carry `enabled` + `displayName` metadata in NVS, publish/retract retained HA discovery on boot, dispatch incoming commands via a new `CustomOutput` JSON shape, and render in the existing Drivers screen as a list below the fixed-slot section.

**Tech Stack:** ESP32 Arduino / PlatformIO, ArduinoJson v7, espMqttClientAsync, ESPAsyncWebServer, Preact + TypeScript + Vite + Tailwind for the web UI.

**Reference spec:** [docs/superpowers/specs/2026-04-24-sp5-custom-output-switches-design.md](../specs/2026-04-24-sp5-custom-output-switches-design.md)

**Alignment note on HA topics:** The spec §8 pseudocoded HA discovery against `PoolTopicAPI` with a `cmd_tpl` of `{"CustomOutput":[N,value]}`. The plan implements HA discovery using the existing codebase convention — per-entity `setTopic(id)` + `stateTopic(id)` with `ON`/`OFF` payloads, handled by `MqttBridge::dispatchHaSet` — which matches the six fixed pumps and avoids forcing an HA template. The `{"CustomOutput":[N,value]}` JSON shape is still the command format on the web-UI WebSocket channel (§7), so the spec's external contract is preserved.

---

## File Structure

**Firmware — modified:**
- `include/Credentials.h` — add `CustomDriverConfig` + `loadCustom`/`saveCustom`/`clearCustom` declarations inside the existing `Credentials::drivers` namespace.
- `src/Credentials.cpp` — implement the three new functions against NVS keys `drv_c0`..`drv_c7`.
- `include/Drivers.h` — add 3 public helpers: `isCustomEnabled`, `customDisplayName`, `customSlotCount`.
- `src/Drivers.cpp` — extend `SLOTS[]` to 14 entries; split into `FIXED_COUNT`/`CUSTOM_COUNT`; add `g_customMeta[]`; extend `beginAll()` with a custom loop.
- `src/CommandQueue.cpp` — add one `CustomOutput` branch in `ProcessCommand`.
- `src/HaDiscovery.cpp` — extend `publishAll()` with a second loop over custom slots.
- `src/MqttBridge.cpp` — extend `dispatchHaSet` to route `custom_N` entity IDs.
- `src/Provisioning.cpp` — add `GET`/`POST`/`DELETE /api/custom-outputs` endpoints.
- `src/WebSocketHub.cpp` — add `customOutputs` array to the per-tick state broadcast.
- `web/src/screens/Drivers.tsx` — append a `CustomSwitchesSection` component below the existing fixed-slot list.
- `README.md` — append a "Custom switches" paragraph and record the post-SP5 flash usage.

**Firmware — no changes expected to:** the `OutputDriver` interface, `GpioOutputDriver`, `MqttOutputDriver`, auth helpers, PID / schedule / anti-freeze / emergency-stop logic.

**Branch:** `sp5-custom-outputs` (already cut; spec committed at HEAD).

---

## Task 1: Extend Credentials::drivers with custom slot API

**Files:**
- Modify: `include/Credentials.h` (existing `Credentials::drivers` namespace)

- [ ] **Step 1: Add `CustomDriverConfig` struct and function declarations**

Open `include/Credentials.h` and locate the closing `} // namespace drivers` (around line 53). Directly before that closing brace, append:

```cpp
    // SP5 — per-slot configuration for user-defined custom switches.
    // Custom slots are indexed 0..7. Backing NVS keys are "drv_c0".."drv_c7".
    // enabled=false marks the slot empty; NVS blob is preserved so HA discovery
    // can publish a retracting empty payload on the next boot.
    struct CustomDriverConfig : DriverConfig {
      bool   enabled     = false;
      String displayName;      // ≤23 chars after trim, printable-ASCII (0x20–0x7E)
    };

    // idx must be in [0, 7]. Returns a default-constructed config (enabled=false)
    // if the NVS key is absent.
    CustomDriverConfig loadCustom(uint8_t idx);

    // Validates the config and writes it to NVS. Returns false on validation
    // failure or NVS write error. See src/Credentials.cpp for the validation rules.
    bool saveCustom(uint8_t idx, const CustomDriverConfig& cfg);

    // Marks the slot empty by flipping enabled=false; does NOT erase the blob.
    // Factory reset erases it via Credentials::clearAll() (existing path).
    bool clearCustom(uint8_t idx);
```

- [ ] **Step 2: Build the firmware to confirm the header compiles in isolation**

Run:

```bash
pio run -e serial_upload -s 2>&1 | tail -20
```

Expected: build fails at the link stage with undefined references to `Credentials::drivers::loadCustom`, `saveCustom`, `clearCustom`. This confirms the header is syntactically valid; the linker errors will be resolved in Task 2.

If the compile stage itself fails with anything other than an "undefined reference", stop and fix the header syntax before continuing.

- [ ] **Step 3: Commit**

```bash
git add include/Credentials.h
git commit -m "sp5: Credentials — declare CustomDriverConfig + custom slot API"
```

---

## Task 2: Implement NVS persistence for custom slots

**Files:**
- Modify: `src/Credentials.cpp` (existing `Credentials::drivers::load` / `save` lives here)

- [ ] **Step 1: Locate the existing drivers::save function**

Open `src/Credentials.cpp` and find the existing `save(const char* slot, const DriverConfig& cfg)` function — it uses `Preferences` keyed by `slot` directly. The new functions will reuse that same `Preferences` object under the same namespace, just with the `drv_c{idx}` key format.

- [ ] **Step 2: Append the three custom-slot functions directly after the existing `save` implementation**

```cpp
// SP5 — custom slot NVS helpers. Keyed "drv_c0".."drv_c7". Serialization shape:
//   byte 0   : version (0x01)
//   byte 1   : enabled (0|1)
//   bytes 2..: displayName length-prefixed (uint8_t len) + name bytes, then
//              the same DriverConfig blob the fixed slots use, sharing a helper.
namespace {
  constexpr uint8_t CUSTOM_BLOB_VERSION = 0x01;

  String customKey(uint8_t idx) {
    char buf[8];
    snprintf(buf, sizeof(buf), "drv_c%u", idx);
    return String(buf);
  }

  // Serialize a DriverConfig to a JSON string — matches the existing save()
  // path which also uses JSON for the fixed slots. Keeps one source of truth.
  String serializeDriver(const DriverConfig& cfg) {
    JsonDocument doc;
    doc["kind"]         = cfg.kind;
    doc["pin"]          = cfg.pin;
    doc["active_level"] = cfg.activeLevel;
    doc["cmd"]          = cfg.cmdTopic;
    doc["p_on"]         = cfg.payloadOn;
    doc["p_off"]        = cfg.payloadOff;
    doc["s_topic"]      = cfg.stateTopic;
    doc["s_on"]         = cfg.stateOn;
    doc["s_off"]        = cfg.stateOff;
    String out;
    serializeJson(doc, out);
    return out;
  }

  void deserializeDriver(const String& json, DriverConfig& out) {
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;
    out.kind        = doc["kind"]         | 0;
    out.pin         = doc["pin"]          | 0xFF;
    out.activeLevel = doc["active_level"] | 0;
    out.cmdTopic    = String((const char*)(doc["cmd"]    | ""));
    out.payloadOn   = String((const char*)(doc["p_on"]   | "on"));
    out.payloadOff  = String((const char*)(doc["p_off"]  | "off"));
    out.stateTopic  = String((const char*)(doc["s_topic"]| ""));
    out.stateOn     = String((const char*)(doc["s_on"]   | "on"));
    out.stateOff    = String((const char*)(doc["s_off"]  | "off"));
  }
}

CustomDriverConfig loadCustom(uint8_t idx) {
  CustomDriverConfig cfg;
  if (idx >= 8) return cfg;

  Preferences p;
  if (!p.begin("creds", /*readOnly=*/true)) return cfg;
  String key = customKey(idx);
  if (!p.isKey(key.c_str())) { p.end(); return cfg; }

  String blob = p.getString(key.c_str(), "");
  p.end();
  if (blob.isEmpty()) return cfg;

  JsonDocument doc;
  if (deserializeJson(doc, blob) != DeserializationError::Ok) return cfg;
  if ((doc["v"] | 0) != CUSTOM_BLOB_VERSION) return cfg;

  cfg.enabled     = doc["en"] | false;
  cfg.displayName = String((const char*)(doc["name"] | ""));

  String inner = doc["drv"] | "";
  deserializeDriver(inner, cfg);
  return cfg;
}

// Validation rules:
//  - idx must be 0..7
//  - displayName non-empty after trim, ≤23 chars, all bytes 0x20..0x7E
//  - If kind == gpio: pin < 40
//  - If kind == mqtt: cmdTopic non-empty, ≤63 chars, no '#'/'+', no quotes/control
//  - payloadOn/payloadOff non-empty
bool saveCustom(uint8_t idx, const CustomDriverConfig& cfg) {
  if (idx >= 8) return false;

  String name = cfg.displayName;
  name.trim();
  if (name.isEmpty() || name.length() > 23) return false;
  for (size_t i = 0; i < name.length(); ++i) {
    char c = name[i];
    if (c < 0x20 || c > 0x7E) return false;
  }

  if (cfg.enabled) {
    if (cfg.kind == 0) {                          // gpio
      if (cfg.pin >= 40) return false;
    } else if (cfg.kind == 1) {                   // mqtt
      if (cfg.cmdTopic.isEmpty() || cfg.cmdTopic.length() > 63) return false;
      if (cfg.cmdTopic.indexOf('#') >= 0 || cfg.cmdTopic.indexOf('+') >= 0) return false;
      if (cfg.payloadOn.isEmpty() || cfg.payloadOff.isEmpty()) return false;
      auto badChars = [](const String& s) {
        for (size_t i = 0; i < s.length(); ++i) {
          char c = s[i];
          if (c == '"' || c == '\\' || (uint8_t)c < 0x20) return true;
        }
        return false;
      };
      if (badChars(cfg.cmdTopic) || badChars(cfg.payloadOn) || badChars(cfg.payloadOff) ||
          badChars(cfg.stateTopic) || badChars(cfg.stateOn) || badChars(cfg.stateOff)) {
        return false;
      }
    } else {
      return false;
    }
  }

  JsonDocument doc;
  doc["v"]   = CUSTOM_BLOB_VERSION;
  doc["en"]  = cfg.enabled;
  doc["name"]= name;
  doc["drv"] = serializeDriver(cfg);
  String out;
  serializeJson(doc, out);

  Preferences p;
  if (!p.begin("creds", /*readOnly=*/false)) return false;
  bool ok = p.putString(customKey(idx).c_str(), out) > 0;
  p.end();
  return ok;
}

bool clearCustom(uint8_t idx) {
  CustomDriverConfig cfg = loadCustom(idx);
  cfg.enabled = false;
  // displayName preserved so HA retract publish has a friendly log line if
  // someone checks the MQTT stream after delete.
  return saveCustom(idx, cfg);
}
```

- [ ] **Step 3: Build the firmware**

```bash
pio run -e serial_upload -s 2>&1 | tail -10
```

Expected: build succeeds. `=== [SUCCESS] Took …s ===` line at the end.

If the build fails with `deserializeDriver`/`serializeDriver` undefined when called from `saveCustom`/`loadCustom`, check that the anonymous namespace declaration is above the functions that use it.

- [ ] **Step 4: Commit**

```bash
git add src/Credentials.cpp
git commit -m "sp5: Credentials — NVS persistence for 8 custom driver slots"
```

---

## Task 3: Extend the Drivers slot registry from 6 to 14

**Files:**
- Modify: `src/Drivers.cpp` (SLOTS table + storage arrays)

- [ ] **Step 1: Replace the SLOTS[] declaration and add COUNTs**

In `src/Drivers.cpp`, find the existing block at lines 12–14:

```cpp
static const char* const SLOTS[] = { "filt", "ph", "chl", "robot", "r0", "r1" };
static constexpr size_t   SLOT_COUNT = 6;
```

Replace with:

```cpp
static const char* const SLOTS[] = {
  "filt", "ph", "chl", "robot", "r0", "r1",            // 0..5   fixed (SP4)
  "custom_0","custom_1","custom_2","custom_3",         // 6..13  custom (SP5)
  "custom_4","custom_5","custom_6","custom_7",
};
static constexpr size_t   FIXED_COUNT  = 6;
static constexpr size_t   CUSTOM_COUNT = 8;
static constexpr size_t   SLOT_COUNT   = 14;
```

- [ ] **Step 2: Add custom-slot metadata and extend storage arrays**

Find the existing `gpioStorage` / `mqttStorage` block (around lines 23–29):

```cpp
alignas(GpioOutputDriver) static uint8_t gpioStorage[SLOT_COUNT][sizeof(GpioOutputDriver)];
alignas(MqttOutputDriver) static uint8_t mqttStorage[SLOT_COUNT][sizeof(MqttOutputDriver)];
static OutputDriver* g_drivers[SLOT_COUNT] = {};
```

Those declarations remain as-is — `SLOT_COUNT` is now 14, so the arrays auto-grow.

Directly below that block, add the custom metadata array:

```cpp
// SP5 — per-custom-slot metadata (index 0..7). enabled+displayName mirror the
// values in NVS. Disabled slots stay nullptr in g_drivers[] and are skipped
// everywhere (HA discovery, commands, WS broadcast).
struct CustomMeta { bool enabled; char displayName[24]; };
static CustomMeta g_customMeta[CUSTOM_COUNT] = {};
```

- [ ] **Step 3: Build — will fail because beginAll doesn't handle custom slots yet**

```bash
pio run -e serial_upload -s 2>&1 | tail -10
```

Expected: build succeeds (we didn't change behavior, just made room). `beginAll()` still loops `SLOT_COUNT` — which is now 14 — and will try to `Credentials::drivers::load("custom_0")` via the existing path. That path returns a default `DriverConfig` (kind=0, pin=0xFF) for unknown slots, and `GpioOutputDriver(0xFF, ...)` is inert. No crash at this step, but behavior is incorrect; Task 4 fixes it.

If the build fails for any other reason, stop and fix.

- [ ] **Step 4: Commit**

```bash
git add src/Drivers.cpp
git commit -m "sp5: Drivers — extend SLOTS[] to 14 and add custom metadata array"
```

---

## Task 4: beginAll() custom-slot branch + public helpers

**Files:**
- Modify: `src/Drivers.cpp` (extend `beginAll`)
- Modify: `include/Drivers.h` (declare public helpers)

- [ ] **Step 1: Split beginAll into a fixed loop and a custom loop**

In `src/Drivers.cpp`, find the existing `beginAll()` body (around lines 36–63). Replace the `for (size_t i = 0; i < SLOT_COUNT; ++i)` loop with two separate loops — the fixed one stays identical, the custom one uses `loadCustom`:

```cpp
void beginAll() {
  // Fixed slots — unchanged from SP4. Loops 0..5.
  for (size_t i = 0; i < FIXED_COUNT; ++i) {
    Credentials::drivers::DriverConfig cfg = Credentials::drivers::load(SLOTS[i]);

    if (cfg.kind == 1) {
      if (cfg.pin < 40) pinMode(cfg.pin, INPUT);

      MqttDriverConfig mc;
      mc.cmdTopic    = cfg.cmdTopic;
      mc.payloadOn   = cfg.payloadOn;
      mc.payloadOff  = cfg.payloadOff;
      mc.stateTopic  = cfg.stateTopic;
      mc.stateOn     = cfg.stateOn;
      mc.stateOff    = cfg.stateOff;
      g_drivers[i] = new (mqttStorage[i]) MqttOutputDriver(mc);
      LogBuffer::append(LogBuffer::L_INFO, "[drv] %s kind=mqtt cmd=%s state=%s",
        SLOTS[i], mc.cmdTopic.c_str(), mc.stateTopic.c_str());
    } else {
      bool activeLow = (cfg.activeLevel == 0);
      g_drivers[i] = new (gpioStorage[i]) GpioOutputDriver(cfg.pin, activeLow);
      LogBuffer::append(LogBuffer::L_INFO, "[drv] %s kind=gpio pin=%u al=%d",
        SLOTS[i], (unsigned)cfg.pin, activeLow ? 1 : 0);
    }
    g_drivers[i]->begin();
  }

  // Custom slots — SP5. Loops 0..7, stored at g_drivers[FIXED_COUNT + i].
  for (size_t i = 0; i < CUSTOM_COUNT; ++i) {
    Credentials::drivers::CustomDriverConfig cfg = Credentials::drivers::loadCustom(i);
    g_customMeta[i].enabled = cfg.enabled;
    strlcpy(g_customMeta[i].displayName, cfg.displayName.c_str(),
            sizeof(CustomMeta::displayName));

    size_t s = FIXED_COUNT + i;
    if (!cfg.enabled) {
      g_drivers[s] = nullptr;
      LogBuffer::append(LogBuffer::L_INFO, "[drv] custom_%u disabled", (unsigned)i);
      continue;
    }

    if (cfg.kind == 1) {
      MqttDriverConfig mc;
      mc.cmdTopic    = cfg.cmdTopic;
      mc.payloadOn   = cfg.payloadOn;
      mc.payloadOff  = cfg.payloadOff;
      mc.stateTopic  = cfg.stateTopic;
      mc.stateOn     = cfg.stateOn;
      mc.stateOff    = cfg.stateOff;
      g_drivers[s] = new (mqttStorage[s]) MqttOutputDriver(mc);
      LogBuffer::append(LogBuffer::L_INFO, "[drv] custom_%u kind=mqtt cmd=%s name=\"%s\"",
        (unsigned)i, mc.cmdTopic.c_str(), g_customMeta[i].displayName);
    } else {
      bool activeLow = (cfg.activeLevel == 0);
      g_drivers[s] = new (gpioStorage[s]) GpioOutputDriver(cfg.pin, activeLow);
      LogBuffer::append(LogBuffer::L_INFO, "[drv] custom_%u kind=gpio pin=%u al=%d name=\"%s\"",
        (unsigned)i, (unsigned)cfg.pin, activeLow ? 1 : 0, g_customMeta[i].displayName);
    }
    g_drivers[s]->begin();
  }
}
```

- [ ] **Step 2: Add the three public helper implementations at the end of the `Drivers::` namespace**

Directly before the closing `} // namespace Drivers` at the bottom of `src/Drivers.cpp`, append:

```cpp
size_t customSlotCount() { return CUSTOM_COUNT; }

bool isCustomEnabled(uint8_t idx) {
  if (idx >= CUSTOM_COUNT) return false;
  return g_customMeta[idx].enabled && g_drivers[FIXED_COUNT + idx] != nullptr;
}

const char* customDisplayName(uint8_t idx) {
  if (idx >= CUSTOM_COUNT) return "";
  return g_customMeta[idx].displayName;
}

// Accept a new custom-slot metadata update without a reboot for display-name
// only changes. The driver kind/pins/topics still require reboot (existing
// SP4 pattern). Called from the REST save path so the Settings UI sees the
// new name on the very next GET without waiting for reboot to complete.
// Currently only updates the in-memory mirror; NVS is the source of truth on
// boot, so there's no divergence risk.
void setCustomDisplayName(uint8_t idx, const char* name) {
  if (idx >= CUSTOM_COUNT) return;
  strlcpy(g_customMeta[idx].displayName, name ? name : "",
          sizeof(CustomMeta::displayName));
}
```

- [ ] **Step 3: Declare the helpers in `include/Drivers.h`**

Open `include/Drivers.h` and directly before the closing `} // namespace Drivers`, append:

```cpp
// SP5 — custom slot helpers.
size_t customSlotCount();                    // returns 8 (compile-time constant)
bool   isCustomEnabled(uint8_t idx);         // idx 0..7
const char* customDisplayName(uint8_t idx);  // returns "" for disabled
void   setCustomDisplayName(uint8_t idx, const char* name);
```

- [ ] **Step 4: Build**

```bash
pio run -e serial_upload -s 2>&1 | tail -10
```

Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add include/Drivers.h src/Drivers.cpp
git commit -m "sp5: Drivers — beginAll custom loop + isCustomEnabled/customDisplayName/customSlotCount"
```

---

## Task 5: CustomOutput command in ProcessCommand

**Files:**
- Modify: `src/CommandQueue.cpp` (add one branch in `ProcessCommand`)

- [ ] **Step 1: Locate the Relay branch**

In `src/CommandQueue.cpp`, find the existing `{"Relay": [...]}` branch (around line 474). This is the closest sibling — index-addressed, two-element array.

- [ ] **Step 2: Add a new `CustomOutput` branch directly AFTER the Relay branch**

Insert this block after the closing brace of the `else if (command[F("Relay")].is<JsonVariant>())` branch and before the next `else if`:

```cpp
//{"CustomOutput":[N, value]}  — N in 0..7. value is 0|1 or "on"|"off".
// Index out of range or slot disabled → silent drop (matches Relay).
else if (command[F("CustomOutput")].is<JsonVariant>())
{
  uint8_t idx   = (uint8_t) (int) command[F("CustomOutput")][0];
  JsonVariant v = command[F("CustomOutput")][1];
  bool value;
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    value = (strcasecmp(s, "on") == 0) || (strcasecmp(s, "1") == 0);
  } else {
    value = (bool) (int) v;
  }

  if (idx >= Drivers::customSlotCount() || !Drivers::isCustomEnabled(idx)) {
    Debug.print(DBG_WARNING, "[cmd] CustomOutput idx=%u ignored (disabled or out of range)",
      (unsigned) idx);
  } else {
    char slot[12];
    snprintf(slot, sizeof(slot), "custom_%u", (unsigned) idx);
    OutputDriver* d = Drivers::get(slot);
    if (d) {
      d->set(value);
      // Publish retained state echo on the per-slot state topic, matching
      // the pattern used by the six fixed switches.
      String topic = String("poolmaster/") + Credentials::deviceId()
                   + "/" + slot + "/state";
      mqttClient.publish(topic.c_str(), 1, /*retain=*/true, value ? "ON" : "OFF");
    }
  }
}
```

- [ ] **Step 3: Ensure the needed headers are included at the top of the file**

Check the top of `src/CommandQueue.cpp` — it should already include `Drivers.h`, `OutputDriver.h`, `Credentials.h`, and `espMqttClientAsync.h` (for `mqttClient`). If any are missing, add them.

Run a quick grep to confirm:

```bash
head -30 src/CommandQueue.cpp | grep -E '#include' 
```

If `Drivers.h` is missing, add `#include "Drivers.h"` near the top.

- [ ] **Step 4: Build**

```bash
pio run -e serial_upload -s 2>&1 | tail -15
```

Expected: build succeeds.

If `mqttClient` is not visible, add `extern espMqttClientAsync mqttClient;` alongside the other externs near the top of the file (mirroring the pattern already used by `src/Drivers.cpp`).

- [ ] **Step 5: Commit**

```bash
git add src/CommandQueue.cpp
git commit -m "sp5: ProcessCommand — add CustomOutput[N, value] branch"
```

---

## Task 6: HA discovery for custom slots

**Files:**
- Modify: `src/HaDiscovery.cpp` (extend `publishAll`)

- [ ] **Step 1: Add a helper that builds the custom-slot discovery payload**

In `src/HaDiscovery.cpp`, directly after the static `uniqueId(...)` helper (around line 114), append:

```cpp
// SP5 — custom slot helpers. The slot id is the user-facing "custom_N"; the
// topic prefix reuses the standard poolmaster/<mac>/<id> convention so the
// existing MqttBridge::dispatchHaSet path can route incoming commands.
static String customConfigTopic(uint8_t idx) {
  char buf[8]; snprintf(buf, sizeof(buf), "custom_%u", (unsigned) idx);
  return String("homeassistant/switch/poolmaster_") +
         Credentials::deviceId() + "_" + buf + "/config";
}
static String customUniqueId(uint8_t idx) {
  char buf[8]; snprintf(buf, sizeof(buf), "custom_%u", (unsigned) idx);
  return String("poolmaster_") + Credentials::deviceId() + "_" + buf;
}
static String customStateTopic(uint8_t idx) {
  char buf[8]; snprintf(buf, sizeof(buf), "custom_%u", (unsigned) idx);
  return String("poolmaster/") + Credentials::deviceId() + "/" + buf + "/state";
}
static String customSetTopic(uint8_t idx) {
  char buf[8]; snprintf(buf, sizeof(buf), "custom_%u", (unsigned) idx);
  return String("poolmaster/") + Credentials::deviceId() + "/" + buf + "/set";
}
```

- [ ] **Step 2: Add the custom discovery loop at the end of `publishAll()`**

Inside `publishAll()`, find the closing `publishAvail();` line (around line 172). Directly BEFORE `publishAvail()`, insert:

```cpp
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
```

- [ ] **Step 3: Add the `Drivers.h` include at the top of the file if missing**

Check the includes near the top of `src/HaDiscovery.cpp`. If `Drivers.h` is not included, add it:

```cpp
#include "Drivers.h"
```

- [ ] **Step 4: Build**

```bash
pio run -e serial_upload -s 2>&1 | tail -10
```

Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/HaDiscovery.cpp
git commit -m "sp5: HaDiscovery — publish retained discovery for 8 custom slots"
```

---

## Task 7: Route HA set-topic incoming for custom slots

**Files:**
- Modify: `src/MqttBridge.cpp` (`dispatchHaSet` / entity-id lookup)

- [ ] **Step 1: Locate dispatchHaSet**

In `src/MqttBridge.cpp`, find `static void dispatchHaSet(const char* entityId, const char* payload)` (around line 179). The function looks up the entity ID in a fixed table and translates `ON`/`OFF` into JSON commands like `{"FiltPump":1}`.

- [ ] **Step 2: Add a custom-slot branch before the table lookup**

At the very top of `dispatchHaSet`, before the existing table iteration, insert:

```cpp
  // SP5 — entity IDs "custom_0".."custom_7" dispatch to the generic
  // CustomOutput command. parseBool semantics match the fixed switches.
  if (strncmp(entityId, "custom_", 7) == 0 && entityId[7] >= '0' && entityId[7] <= '7' && entityId[8] == '\0') {
    uint8_t idx = (uint8_t)(entityId[7] - '0');
    bool    on  = (strcasecmp(payload, "ON") == 0) || (strcasecmp(payload, "1") == 0);
    char json[40];
    snprintf(json, sizeof(json), "{\"CustomOutput\":[%u,%d]}", (unsigned) idx, on ? 1 : 0);
    queueApiCommand(json);   // same helper the table-dispatch path uses
    return;
  }
```

- [ ] **Step 3: Confirm `queueApiCommand` is the right name**

Run:

```bash
grep -nE 'queueApiCommand|CommandQueue::push|ApiCommand::push' src/MqttBridge.cpp | head -5
```

If the existing table dispatch uses a different function (e.g. `CommandQueue::push(json)` or `ApiCommand::enqueue(json)`), replace `queueApiCommand(json)` in the snippet above with the actual name used.

- [ ] **Step 4: Build**

```bash
pio run -e serial_upload -s 2>&1 | tail -10
```

Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/MqttBridge.cpp
git commit -m "sp5: MqttBridge — route custom_N/set to CustomOutput command"
```

---

## Task 8: GET /api/custom-outputs

**Files:**
- Modify: `src/Provisioning.cpp` (alongside the existing `/api/drivers` handlers)

- [ ] **Step 1: Locate the existing `/api/drivers` GET handler**

In `src/Provisioning.cpp`, find the `srv.on("/api/drivers", HTTP_GET, ...)` handler (around line 182). The new endpoint lives directly below the `/api/drivers` POST handler for visual grouping.

- [ ] **Step 2: Append the GET handler**

Directly after the closing `});` of the existing `/api/drivers` POST handler (around line 275), insert:

```cpp
  // SP5 — GET /api/custom-outputs returns the 8 custom-slot states as an
  // array. Same auth pattern as /api/drivers. Disabled slots return
  // {"slot":N,"enabled":false} with no other fields.
  srv.on("/api/custom-outputs", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;
    String body = "[";
    for (uint8_t i = 0; i < 8; ++i) {
      auto c = Credentials::drivers::loadCustom(i);
      if (i) body += ",";
      body += "{\"slot\":";      body += i;
      body += ",\"enabled\":";   body += (c.enabled ? "true" : "false");
      if (c.enabled) {
        auto esc = [](const String& s) {
          String out; out.reserve(s.length() + 2);
          for (size_t j = 0; j < s.length(); ++j) {
            char ch = s[j];
            if (ch == '"' || ch == '\\') { out += '\\'; out += ch; }
            else if ((uint8_t)ch < 0x20)  out += ' ';
            else                          out += ch;
          }
          return out;
        };
        body += ",\"display_name\":\"";  body += esc(c.displayName);  body += "\"";
        body += ",\"kind\":\"";          body += (c.kind == 1 ? "mqtt" : "gpio"); body += "\"";
        body += ",\"pin\":";             body += (int) c.pin;
        body += ",\"active_level\":\""; body += (c.activeLevel == 1 ? "high" : "low"); body += "\"";
        body += ",\"cmd_topic\":\"";     body += esc(c.cmdTopic);     body += "\"";
        body += ",\"payload_on\":\"";    body += esc(c.payloadOn);    body += "\"";
        body += ",\"payload_off\":\"";   body += esc(c.payloadOff);   body += "\"";
        body += ",\"state_topic\":\"";   body += esc(c.stateTopic);   body += "\"";
        body += ",\"state_on\":\"";      body += esc(c.stateOn);      body += "\"";
        body += ",\"state_off\":\"";     body += esc(c.stateOff);     body += "\"";
      }
      body += "}";
    }
    body += "]";
    req->send(200, "application/json", body);
  });
```

- [ ] **Step 3: Build**

```bash
pio run -e serial_upload -s 2>&1 | tail -10
```

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/Provisioning.cpp
git commit -m "sp5: GET /api/custom-outputs returns 8 custom slot states"
```

---

## Task 9: POST /api/custom-outputs

**Files:**
- Modify: `src/Provisioning.cpp`

- [ ] **Step 1: Append the POST handler below the GET handler**

Directly after the closing `});` of the `/api/custom-outputs` GET handler, append:

```cpp
  // SP5 — POST /api/custom-outputs. Form-encoded to match /api/drivers POST.
  // Required: slot (0..7). If enabled=true, kind + driver fields must be
  // present and valid — saveCustom() runs the full validation and returns
  // false on failure. Success reboots the device (SP4 pattern).
  srv.on("/api/custom-outputs", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;

    if (!req->hasParam("slot", true)) {
      req->send(400, "text/plain", "missing slot"); return;
    }
    long slot = req->getParam("slot", true)->value().toInt();
    if (slot < 0 || slot > 7) {
      req->send(400, "text/plain", "slot out of range"); return;
    }

    Credentials::drivers::CustomDriverConfig cfg;

    auto grab = [&](const char* name, String& dst) {
      if (req->hasParam(name, true)) dst = req->getParam(name, true)->value();
    };

    cfg.enabled = req->hasParam("enabled", true)
      ? (req->getParam("enabled", true)->value() == "true")
      : false;

    grab("display_name", cfg.displayName);

    if (req->hasParam("kind", true)) {
      String k = req->getParam("kind", true)->value();
      cfg.kind = (k == "mqtt") ? 1 : 0;
    }
    if (req->hasParam("pin", true)) {
      cfg.pin = (uint8_t) req->getParam("pin", true)->value().toInt();
    }
    if (req->hasParam("active_level", true)) {
      cfg.activeLevel = (req->getParam("active_level", true)->value() == "high") ? 1 : 0;
    }
    grab("cmd_topic",   cfg.cmdTopic);
    grab("payload_on",  cfg.payloadOn);
    grab("payload_off", cfg.payloadOff);
    grab("state_topic", cfg.stateTopic);
    grab("state_on",    cfg.stateOn);
    grab("state_off",   cfg.stateOff);

    if (!Credentials::drivers::saveCustom((uint8_t) slot, cfg)) {
      req->send(400, "text/plain", "validation failed or NVS write error"); return;
    }
    Debug.print(DBG_INFO, "[Prov] custom_%ld saved (enabled=%d), rebooting",
      slot, cfg.enabled ? 1 : 0);
    req->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    delay(200);
    ESP.restart();
  });
```

- [ ] **Step 2: Build**

```bash
pio run -e serial_upload -s 2>&1 | tail -10
```

Expected: build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/Provisioning.cpp
git commit -m "sp5: POST /api/custom-outputs — save slot + reboot"
```

---

## Task 10: DELETE /api/custom-outputs

**Files:**
- Modify: `src/Provisioning.cpp`

- [ ] **Step 1: Append DELETE handler below the POST handler**

AsyncWebServer doesn't path-wildcard, so DELETE takes the slot in a form field just like POST. Append:

```cpp
  // SP5 — DELETE /api/custom-outputs. Form-encoded slot (0..7). Marks the
  // slot enabled=false via clearCustom(), then reboots so HA discovery
  // publishes an empty retained payload for that slot's config topic.
  srv.on("/api/custom-outputs", HTTP_DELETE, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;

    if (!req->hasParam("slot", true)) {
      req->send(400, "text/plain", "missing slot"); return;
    }
    long slot = req->getParam("slot", true)->value().toInt();
    if (slot < 0 || slot > 7) {
      req->send(400, "text/plain", "slot out of range"); return;
    }

    if (!Credentials::drivers::clearCustom((uint8_t) slot)) {
      req->send(500, "text/plain", "NVS write failed"); return;
    }
    Debug.print(DBG_INFO, "[Prov] custom_%ld cleared, rebooting", slot);
    req->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    delay(200);
    ESP.restart();
  });
```

- [ ] **Step 2: Build**

```bash
pio run -e serial_upload -s 2>&1 | tail -10
```

Expected: build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/Provisioning.cpp
git commit -m "sp5: DELETE /api/custom-outputs — clear slot + reboot"
```

---

## Task 11: WebSocket broadcast — customOutputs array

**Files:**
- Modify: `src/WebSocketHub.cpp` (existing per-tick state-push payload builder)

- [ ] **Step 1: Locate the existing WS state broadcast function**

Run:

```bash
grep -n 'buildStatePayload\|broadcastState\|broadcastTick\|JsonDocument' src/WebSocketHub.cpp | head -10
```

Use the function name that builds the per-tick JSON payload (most likely `buildStatePayload` or `broadcastTick`). Open `src/WebSocketHub.cpp` and find where that function composes the top-level JSON object.

- [ ] **Step 2: Add the `customOutputs` array alongside the other top-level fields**

Inside the state-builder function, find where sibling arrays/objects are added (e.g. `doc["relays"] = ...`). Directly after the last existing field, add:

```cpp
  // SP5 — broadcast the state of each enabled custom switch. Disabled slots
  // are omitted so the client can treat missing slot IDs as "not configured".
  {
    JsonArray arr = doc["customOutputs"].to<JsonArray>();
    for (uint8_t i = 0; i < Drivers::customSlotCount(); ++i) {
      if (!Drivers::isCustomEnabled(i)) continue;
      char slot[12]; snprintf(slot, sizeof(slot), "custom_%u", (unsigned) i);
      OutputDriver* d = Drivers::get(slot);
      JsonObject o = arr.add<JsonObject>();
      o["slot"] = i;
      o["name"] = Drivers::customDisplayName(i);
      o["on"]   = (d && d->get());
    }
  }
```

- [ ] **Step 3: Confirm `Drivers.h` and `OutputDriver.h` are included**

Check the includes at the top of `src/WebSocketHub.cpp`. Add any that are missing:

```cpp
#include "Drivers.h"
#include "OutputDriver.h"
```

- [ ] **Step 4: Build**

```bash
pio run -e serial_upload -s 2>&1 | tail -10
```

Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/WebSocketHub.cpp
git commit -m "sp5: WebSocketHub — broadcast customOutputs array each tick"
```

---

## Task 12: Web UI — extend Drivers.tsx with CustomSwitchesSection fetch + list render

**Files:**
- Modify: `web/src/screens/Drivers.tsx`
- Modify: `web/src/lib/api.ts` (only if a DELETE helper is missing — check before adding)

- [ ] **Step 1: Check whether `apiDelete` helper already exists**

```bash
grep -n 'apiDelete\|apiPostForm\|apiGet' web/src/lib/api.ts
```

If `apiDelete` is missing, add it at the end of `web/src/lib/api.ts`:

```ts
export async function apiDelete(path: string, form?: Record<string, string>): Promise<void> {
  const body = form
    ? new URLSearchParams(form).toString()
    : undefined;
  const r = await fetch(path, {
    method: 'DELETE',
    headers: form ? { 'Content-Type': 'application/x-www-form-urlencoded' } : {},
    body,
    credentials: 'same-origin',
  });
  if (!r.ok) throw new Error(`${path} → ${r.status}`);
}
```

- [ ] **Step 2: Add types + list rendering at the bottom of `web/src/screens/Drivers.tsx`**

Open `web/src/screens/Drivers.tsx`. Directly before the final `export function Drivers()` export (or immediately after it — just above the file's closing EOF is fine), append:

```tsx
// ───────────────────────── SP5: Custom Switches ─────────────────────────

interface CustomOutputCfg {
  slot: number;
  enabled: boolean;
  display_name?: string;
  kind?: 'gpio' | 'mqtt';
  pin?: number;
  active_level?: 'low' | 'high';
  cmd_topic?: string;
  payload_on?: string;
  payload_off?: string;
  state_topic?: string;
  state_on?: string;
  state_off?: string;
}

const EMPTY_CUSTOM: CustomOutputCfg = {
  slot: 0, enabled: true, display_name: '',
  kind: 'mqtt', pin: 0xFF, active_level: 'low',
  cmd_topic: '', payload_on: 'on', payload_off: 'off',
  state_topic: '', state_on: 'on', state_off: 'off',
};

function toCustomForm(c: CustomOutputCfg): Record<string, string> {
  return {
    slot:          String(c.slot),
    enabled:       c.enabled ? 'true' : 'false',
    display_name:  c.display_name ?? '',
    kind:          c.kind ?? 'mqtt',
    pin:           String(c.pin ?? 0xFF),
    active_level:  c.active_level ?? 'low',
    cmd_topic:     c.cmd_topic ?? '',
    payload_on:    c.payload_on ?? 'on',
    payload_off:   c.payload_off ?? 'off',
    state_topic:   c.state_topic ?? '',
    state_on:      c.state_on ?? 'on',
    state_off:     c.state_off ?? 'off',
  };
}

export function CustomSwitchesSection() {
  const configs = useSignal<CustomOutputCfg[] | null>(null);
  const editing = useSignal<CustomOutputCfg | null>(null);
  const error   = useSignal<string | null>(null);

  useEffect(() => {
    let alive = true;
    (async () => {
      try {
        const res = await apiGet<CustomOutputCfg[]>('/api/custom-outputs');
        if (alive) configs.value = res;
      } catch (e: any) {
        if (alive) error.value = String(e);
      }
    })();
    return () => { alive = false; };
  }, []);

  if (configs.value === null) {
    return <div class="text-sm text-slate-400">Loading custom switches…</div>;
  }

  const enabled = configs.value.filter(c => c.enabled);
  const nextFreeSlot = configs.value.findIndex(c => !c.enabled);

  return (
    <section class="mt-8">
      <h2 class="text-lg font-semibold mb-2">
        Custom switches <span class="text-slate-400 text-sm">({enabled.length} of 8)</span>
      </h2>

      {error.value && (
        <div class="rounded bg-red-900/40 border border-red-700 p-2 mb-2 text-sm">
          {error.value}
        </div>
      )}

      {enabled.length === 0 && (
        <p class="text-sm text-slate-400 mb-3">
          No custom switches yet. Add one to control an external MQTT device or a spare GPIO.
        </p>
      )}

      <ul class="divide-y divide-slate-800 border border-slate-800 rounded">
        {enabled.map(c => (
          <li key={c.slot} class="flex items-center gap-3 px-3 py-2">
            <span class="flex-1 font-medium">{c.display_name || `Slot ${c.slot}`}</span>
            <span class={`text-xs px-2 py-0.5 rounded ${c.kind === 'mqtt' ? 'bg-sky-900/60 text-sky-300' : 'bg-emerald-900/60 text-emerald-300'}`}>
              {c.kind}
            </span>
            <CustomSwitchToggle slot={c.slot} />
            <button class="text-sky-400 text-sm hover:underline"
                    onClick={() => { editing.value = c; }}>
              Edit
            </button>
            <button class="text-red-400 text-sm hover:underline"
                    onClick={() => onDelete(c, configs)}>
              ×
            </button>
          </li>
        ))}
      </ul>

      <button class="mt-2 block w-full border border-dashed border-slate-700 rounded py-3 text-sky-400 hover:bg-slate-800"
              disabled={nextFreeSlot < 0}
              onClick={() => {
                if (nextFreeSlot < 0) return;
                editing.value = { ...EMPTY_CUSTOM, slot: nextFreeSlot };
              }}>
        {nextFreeSlot < 0 ? 'All 8 slots in use' : '+ Add custom switch'}
      </button>

      {editing.value && (
        <CustomSwitchModal
          initial={editing.value}
          onClose={() => { editing.value = null; }}
          onSaved={() => { editing.value = null; /* page reloads after device reboot */ }}
        />
      )}
    </section>
  );
}

async function onDelete(c: CustomOutputCfg, configs: any) {
  const name = c.display_name || `Slot ${c.slot}`;
  if (!confirm(`Delete "${name}"? This retracts the HA entity and reboots the device.`)) return;
  try {
    await apiDelete('/api/custom-outputs', { slot: String(c.slot) });
    // Device is rebooting — show a banner and poll health
    showRebootBanner();
  } catch (e: any) {
    alert(`Delete failed: ${e}`);
  }
}

function showRebootBanner() {
  const el = document.createElement('div');
  el.className = 'fixed top-4 right-4 bg-amber-900/90 border border-amber-700 text-amber-100 px-3 py-2 rounded text-sm';
  el.textContent = 'Saved. Device rebooting…';
  document.body.appendChild(el);
  const poll = setInterval(async () => {
    try {
      const r = await fetch('/api/health', { credentials: 'same-origin' });
      if (r.ok) { clearInterval(poll); location.reload(); }
    } catch {}
  }, 1500);
}
```

- [ ] **Step 3: Also export an empty `CustomSwitchToggle` and `CustomSwitchModal` stub so the file compiles**

Directly before `CustomSwitchesSection`, add these stubs. They'll be filled in by Task 13 and 14:

```tsx
function CustomSwitchToggle(_props: { slot: number }) {
  return <span class="text-xs text-slate-500">(toggle)</span>;
}

function CustomSwitchModal(_props: { initial: CustomOutputCfg; onClose: () => void; onSaved: () => void }) {
  return null;
}
```

- [ ] **Step 4: Mount CustomSwitchesSection in the Drivers page**

Find the `Drivers` component's JSX return statement. It currently renders `<SectionTabs …/>` followed by the six-slot list. At the very end of the returned fragment, just before the closing `</main>` or outer `</div>`, add:

```tsx
      <CustomSwitchesSection />
```

- [ ] **Step 5: Add the `apiGet`/`apiPostForm`/`apiDelete` imports if any are missing at the top of the file**

Check the top of `web/src/screens/Drivers.tsx`:

```tsx
import { apiGet, apiPostForm, apiDelete } from '../lib/api';
```

- [ ] **Step 6: Build the frontend**

```bash
cd web && npm run build 2>&1 | tail -15
```

Expected: `tsc --noEmit` succeeds, `vite build` succeeds, bundle-size check passes. Assets land in `data/assets/`.

If `tsc` reports unused imports or signature mismatches, fix them.

- [ ] **Step 7: Return to repo root and commit**

```bash
cd ..
git add web/src/screens/Drivers.tsx web/src/lib/api.ts data/assets/
git commit -m "sp5: Drivers screen — CustomSwitchesSection list with Edit/Delete/Add"
```

---

## Task 13: Wire inline toggle to WebSocket command

**Files:**
- Modify: `web/src/screens/Drivers.tsx` (fill in `CustomSwitchToggle`)
- Modify: `web/src/stores/*` (use the same store the dashboard uses for per-tick state)

- [ ] **Step 1: Find the existing per-tick state store**

Run:

```bash
grep -rn 'customOutputs\|relays\|filtrationPump' web/src/stores/ 2>/dev/null | head -10
```

This reveals the signal/store name that mirrors the WebSocket broadcast. Most likely `useStateStore()` or a signal named `state` in `web/src/stores/state.ts`. Inspect that file to confirm:

```bash
ls web/src/stores/ && cat web/src/stores/*.ts 2>/dev/null | head -80
```

- [ ] **Step 2: Replace the `CustomSwitchToggle` stub**

Replace the `function CustomSwitchToggle(_props: { slot: number }) { … }` stub in `web/src/screens/Drivers.tsx` with the real implementation. Substitute `stateStore` / `sendCommand` with the actual names found in Step 1:

```tsx
import { stateStore, sendCommand } from '../stores/state';  // add near top of file

function CustomSwitchToggle({ slot }: { slot: number }) {
  const state = stateStore.value;
  const on = (state?.customOutputs ?? []).find((c: any) => c.slot === slot)?.on ?? false;

  const flip = () => {
    // {"CustomOutput":[slot, 1|0]}
    sendCommand({ CustomOutput: [slot, on ? 0 : 1] });
  };

  return (
    <button
      class={`w-9 h-5 rounded-full relative transition-colors ${on ? 'bg-emerald-500' : 'bg-slate-600'}`}
      onClick={flip}
      aria-label={on ? 'Turn off' : 'Turn on'}
    >
      <span class={`absolute top-0.5 w-4 h-4 rounded-full bg-white transition-all ${on ? 'right-0.5' : 'left-0.5'}`} />
    </button>
  );
}
```

If the actual API is `stateSignal` not `stateStore`, or the send function is `wsSend` or `commandQueue.push`, swap the names accordingly — the shape (subscribe to latest state + send JSON) is the same.

- [ ] **Step 3: Build**

```bash
cd web && npm run build 2>&1 | tail -10
```

Expected: TypeScript passes, vite build succeeds.

- [ ] **Step 4: Commit**

```bash
cd ..
git add web/src/screens/Drivers.tsx data/assets/
git commit -m "sp5: CustomSwitchToggle — WS-driven toggle emits CustomOutput command"
```

---

## Task 14: Edit / Add modal

**Files:**
- Modify: `web/src/screens/Drivers.tsx` (fill in `CustomSwitchModal`)

- [ ] **Step 1: Replace the `CustomSwitchModal` stub with the full form**

Replace the `function CustomSwitchModal(_props: …) { return null; }` stub with:

```tsx
function CustomSwitchModal({ initial, onClose, onSaved }: {
  initial: CustomOutputCfg;
  onClose: () => void;
  onSaved: () => void;
}) {
  const cfg = useSignal<CustomOutputCfg>({ ...initial });
  const saving = useSignal(false);
  const err    = useSignal<string | null>(null);

  const set = <K extends keyof CustomOutputCfg>(k: K, v: CustomOutputCfg[K]) => {
    cfg.value = { ...cfg.value, [k]: v };
  };

  const save = async () => {
    err.value = null;
    // Client-side length guard — server also enforces this.
    const name = (cfg.value.display_name ?? '').trim();
    if (!name) { err.value = 'Display name required'; return; }
    if (name.length > 23) { err.value = 'Display name must be ≤23 chars'; return; }

    saving.value = true;
    try {
      await apiPostForm('/api/custom-outputs', toCustomForm(cfg.value));
      onSaved();
      showRebootBanner();
    } catch (e: any) {
      err.value = String(e);
      saving.value = false;
    }
  };

  return (
    <div class="fixed inset-0 bg-black/60 flex items-center justify-center z-50" onClick={onClose}>
      <div class="bg-slate-900 border border-slate-700 rounded-lg p-5 w-full max-w-md"
           onClick={e => e.stopPropagation()}>
        <h3 class="text-lg font-semibold mb-3">
          {initial.enabled ? 'Edit' : 'Add'} custom switch — slot {cfg.value.slot}
        </h3>

        {err.value && (
          <div class="rounded bg-red-900/40 border border-red-700 p-2 mb-3 text-sm">
            {err.value}
          </div>
        )}

        <label class="block mb-3">
          <span class="block text-xs text-slate-400 mb-1">Display name (≤23 chars)</span>
          <input class="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1"
                 value={cfg.value.display_name ?? ''}
                 onInput={e => set('display_name', (e.target as HTMLInputElement).value)}
                 maxLength={23} />
        </label>

        <div class="flex gap-4 mb-3">
          <label class="flex items-center gap-1">
            <input type="radio" name="kind" checked={cfg.value.kind === 'gpio'}
                   onChange={() => set('kind', 'gpio')} />
            <span>GPIO</span>
          </label>
          <label class="flex items-center gap-1">
            <input type="radio" name="kind" checked={cfg.value.kind === 'mqtt'}
                   onChange={() => set('kind', 'mqtt')} />
            <span>MQTT</span>
          </label>
        </div>

        {cfg.value.kind === 'gpio' && (
          <div class="space-y-2 mb-3">
            <label class="block">
              <span class="block text-xs text-slate-400 mb-1">GPIO pin (0–39)</span>
              <input type="number" min={0} max={39}
                     class="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1"
                     value={cfg.value.pin ?? 0xFF}
                     onInput={e => set('pin', parseInt((e.target as HTMLInputElement).value, 10))} />
            </label>
            <label class="block">
              <span class="block text-xs text-slate-400 mb-1">Active level</span>
              <select class="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1"
                      value={cfg.value.active_level ?? 'low'}
                      onChange={e => set('active_level', (e.target as HTMLSelectElement).value as 'low'|'high')}>
                <option value="low">Active low</option>
                <option value="high">Active high</option>
              </select>
            </label>
          </div>
        )}

        {cfg.value.kind === 'mqtt' && (
          <div class="space-y-2 mb-3">
            {([
              ['cmd_topic',   'Command topic'],
              ['payload_on',  'Payload on'],
              ['payload_off', 'Payload off'],
              ['state_topic', 'State topic (optional)'],
              ['state_on',    'State on'],
              ['state_off',   'State off'],
            ] as const).map(([key, label]) => (
              <label key={key} class="block">
                <span class="block text-xs text-slate-400 mb-1">{label}</span>
                <input class="w-full bg-slate-800 border border-slate-700 rounded px-2 py-1"
                       value={(cfg.value as any)[key] ?? ''}
                       onInput={e => set(key as any, (e.target as HTMLInputElement).value)} />
              </label>
            ))}
          </div>
        )}

        <div class="flex justify-end gap-2 mt-4">
          <button class="px-3 py-1 text-slate-300 hover:text-white" onClick={onClose}>
            Cancel
          </button>
          <button class="px-3 py-1 bg-sky-600 hover:bg-sky-500 rounded disabled:opacity-50"
                  disabled={saving.value}
                  onClick={save}>
            {saving.value ? 'Saving…' : 'Save + reboot'}
          </button>
        </div>
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Build**

```bash
cd web && npm run build 2>&1 | tail -10
```

Expected: build succeeds.

- [ ] **Step 3: Commit**

```bash
cd ..
git add web/src/screens/Drivers.tsx data/assets/
git commit -m "sp5: CustomSwitchModal — Add/Edit form with GPIO + MQTT fields"
```

---

## Task 15: Hardware smoke test

**Files:** None modified — this task runs the 7-point smoke checklist from the spec against real hardware.

- [ ] **Step 1: Flash the firmware + filesystem**

```bash
pio run -e serial_upload -t upload
pio run -e serial_upload -t uploadfs
```

Expected: both finish with `=== [SUCCESS] ===`.

Open a serial monitor on 115200 for the duration of the smoke test:

```bash
pio device monitor -e serial_upload -b 115200
```

- [ ] **Step 2: SC1 — no custom slots configured**

After boot, open the web UI. Navigate to Settings → Drivers. Confirm the "Custom switches (0 of 8)" section renders with the "No custom switches yet" hint and the `+ Add custom switch` button.

Check HA: no `switch.poolmaster_*_custom_*` entities should exist. If an MQTT inspector is handy, confirm `homeassistant/switch/poolmaster_*_custom_0/config` through `_custom_7/config` are either absent or empty retained.

Record PASS/FAIL on paper.

- [ ] **Step 3: SC2 — create custom_0 targeting a Shelly**

In the UI, click `+ Add custom switch`. Fill:
- Display name: `Salt chlorinator`
- Kind: `mqtt`
- Command topic: `shellies/salt/relay/0/command`
- Payload on: `on`, Payload off: `off`
- State topic: `shellies/salt/relay/0`, State on: `on`, State off: `off`

Click Save + reboot. After the device comes back, check HA — a new switch entity `switch.poolmaster_<mac>_custom_0` should appear, named "Salt chlorinator". Toggle it from HA; the Shelly should click. State should propagate back to HA within 300 ms.

Record PASS/FAIL.

- [ ] **Step 4: SC3 — rename the slot**

Edit slot 0, change display name to `Saltwater system`. Save + reboot. Verify in HA:
- `friendly_name` updates to "Saltwater system"
- `entity_id` stays `switch.poolmaster_<mac>_custom_0`
- Any automation referencing that entity_id still fires (create a trivial test automation if none exists)

Record PASS/FAIL.

- [ ] **Step 5: SC4 — delete the slot**

Click the × next to the slot, confirm. After reboot:
- HA entity disappears (retracting empty payload received)
- The list UI shows 0 configured switches again

Record PASS/FAIL.

- [ ] **Step 6: SC5 — two GPIO custom slots**

Add `custom_0` as GPIO pin 27 active-high, `custom_1` as GPIO pin 26 active-high. Save + reboot each. After both are live:
- Toggle each from the UI — verify the correct GPIO flips with a multimeter or scope.
- Send `{"FiltPump":1}` and `{"FiltPump":0}` via MQTT to PoolTopicAPI — verify the filter pump still responds.
- Send `{"Relay":[0,1]}` and `{"Relay":[0,0]}` — verify R0 still responds.

Record PASS/FAIL for each sub-check.

- [ ] **Step 7: SC6 — SP4 regression pass**

Re-run the SP4 smoke checklist from `docs/superpowers/specs/2026-04-24-sp4-pluggable-output-drivers-design.md` (§3 success criteria 1–6). Every item must still pass. The SP4 design file lists them.

Record PASS/FAIL for each.

- [ ] **Step 8: If any smoke test failed, STOP and fix**

Do not proceed to Task 16 until all 7 scenarios pass. When fixing:
- A failing SC6 (SP4 regression) means something in Tasks 3–11 broke the fixed slots — likely the `beginAll` split or a typo in the `SLOTS[]` reorder.
- A failing SC2/SC3/SC5 is typically a field-name mismatch between the form, the POST handler, and `loadCustom` — grep both sides for the suspect field.
- Missing HA entity (SC2/SC3/SC4) is usually the discovery topic typo — confirm it is `homeassistant/switch/poolmaster_<mac>_custom_N/config`.

- [ ] **Step 9: Once all 7 pass, commit the smoke-test checklist results**

The plan doesn't add a new commit at this step — the green flash is the sign this task passes.

---

## Task 16: Record final flash usage + README paragraph

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Capture the post-SP5 firmware size**

Run a clean build and read the reported RAM / flash percentages from the output:

```bash
pio run -e serial_upload 2>&1 | grep -A1 'RAM:\|Flash:'
```

Expected output looks something like:

```
RAM:   [=====     ]  52.1% (used 170648 bytes from 327680 bytes)
Flash: [==========]  98.9% (used 1942384 bytes from 1964032 bytes)
```

Copy the two percentages (Flash + RAM) and the absolute used bytes.

- [ ] **Step 2: Append a README paragraph**

Open `README.md` and find the SP4-era "Custom drivers" or "Drivers" paragraph — it's the last one that describes the output pipeline. Below it, append:

```markdown
### Custom switches (SP5)

Beyond the six fixed physical-output slots, up to 8 user-defined custom
switches can be added in Settings → Drivers → Custom switches. Each custom
slot reuses the same `OutputDriver` interface (GPIO or MQTT) and appears in
Home Assistant as a retained `switch.poolmaster_<mac>_custom_N` entity whose
`unique_id` is pinned to the slot index — renaming the display name
updates HA's `friendly_name` in place without orphaning automations.

Command shape on `PoolTopicAPI`: `{"CustomOutput":[N, 1|0]}`. Delete clears
the slot and retracts the HA entity on next boot.

Post-SP5 firmware size: **Flash X.X%** (… bytes), **RAM Y.Y%** (… bytes).
```

Replace the `X.X%` / `Y.Y%` / byte counts with the real numbers from Step 1.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "sp5: document custom switches in README + record final size"
```

- [ ] **Step 4: Verify commit log**

Run:

```bash
git log --oneline sp3-web-ui..HEAD
```

Expected: ~14–16 commits, one per task, with clear `sp5:` subject prefixes. The tip is the ship commit for this feature branch.

---

## Plan Self-Review Notes

A fresh-eyes pass against the spec §1–§13 confirms:

- **§4 architecture** — Tasks 3, 4 cover the slot-registry extension; Tasks 5, 6, 11 cover the downstream integration points (commands, HA, WS broadcast).
- **§5 registry / §6 NVS** — Tasks 1, 2, 3, 4.
- **§7 command API** — Task 5 (`ProcessCommand` branch) + Task 13 (client sends the same JSON shape over WebSocket).
- **§8 HA discovery** — Task 6 (publish loop) + Task 7 (dispatchHaSet routing). Note the refinement from the spec: per-entity `setTopic`/`stateTopic` with `ON`/`OFF` payloads instead of `PoolTopicAPI + cmd_tpl` — rationale documented in the header of this plan.
- **§9 REST API** — Tasks 8, 9, 10.
- **§10 Settings UI** — Tasks 12, 13, 14.
- **§11 budget** — measured + documented in Task 16. If the build crosses 99.5 % flash at the end of Task 14, apply the spec's cut list (Task 12 badges first, then cap-4, then drop inline toggle) before running Task 15.
- **§12 smoke checklist** — Task 15 is the seven-point checklist.
- **§13 open items** — Display-name truncation is handled: server rejects >23 chars in `saveCustom` validation (Task 2), UI enforces via `maxLength={23}` and an inline error (Task 14). No silent truncation. GPIO-collision and state-topic-collision remain warnings only, as documented.

No placeholders remain. Each task has complete code for the file edits it describes. Type consistency verified: `CustomDriverConfig` used identically in Tasks 1, 2, 9; `isCustomEnabled` signature matches across Tasks 4, 5, 6, 11.

---

## Execution Handoff

Plan complete and saved to [docs/superpowers/plans/2026-04-24-sp5-custom-output-switches.md](./2026-04-24-sp5-custom-output-switches.md). Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
