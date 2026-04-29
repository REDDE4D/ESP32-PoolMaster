# SP4 Pluggable Output Drivers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the six physical-output devices (`FiltrationPump`, `PhPump`, `ChlPump`, `RobotPump`, relays `R0`, `R1`) so each can be rebound at runtime to either a local GPIO pin (current behaviour, still the default) or an external MQTT relay (Shelly-compatible), without changing any of the existing PID / uptime / schedule / anti-freeze / emergency-stop logic.

**Architecture:** New `OutputDriver` virtual interface with two concrete implementations (`GpioOutputDriver`, `MqttOutputDriver`) pinned at boot in module-scope static storage inside a new `Drivers` module. The `Pump` class gets a `setDriver(OutputDriver*)` post-construction hook so existing file-scope pump globals in `Setup.cpp` keep working. Interlock reads are rewired from `digitalRead(FILTRATION_PUMP)` to `FiltrationPump.IsRunning()` via a new `Pump::setInterlockSource` method so interlock stays correct when Filtration is bound to MQTT. Config lives in a new NVS namespace `"drivers"`; first-boot defaults preserve current behaviour exactly. A new Settings → Drivers sub-tab + `GET/POST /api/drivers` endpoints let the user configure each slot.

**Tech Stack:** ESP32 Arduino framework, `espMqttClient` v1.7, `Preferences` NVS, `ESPAsyncWebServer`, ArduinoJson v7. Frontend: Preact + `@preact/signals` + Tailwind (same stack as SP3).

**Spec reference:** [docs/superpowers/specs/2026-04-24-sp4-pluggable-output-drivers-design.md](../specs/2026-04-24-sp4-pluggable-output-drivers-design.md)

**Out of scope:** HTTP-URL driver, net-new user-defined controls, compound/multi-step controls, state-change rate limiting, per-driver telemetry on Diagnostics.

---

## Prerequisites

- Working directory: `/Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster`
- Branch: new `sp4-pluggable-drivers` cut from `sp3-web-ui` tip
- PlatformIO CLI at `~/.platformio/penv/bin/pio`
- An ESP32 Devkit V1 reachable at `http://poolmaster.local` running the latest SP3 firmware
- A second MQTT client machine (the Mac will do) with `mosquitto_pub` / `mosquitto_sub` installed (`brew install mosquitto`)
- OTA password env var: `POOLMASTER_OTA_PWD=""`
- Web toolchain: Node 20 via `web/.nvmrc`, dependencies installed (`cd web && npm install`)

---

## File Structure Overview

```
include/
├── OutputDriver.h                (NEW) abstract interface
├── GpioOutputDriver.h            (NEW) concrete GPIO impl header
├── MqttOutputDriver.h            (NEW) concrete MQTT impl header
├── Drivers.h                     (NEW) module-level registry API
├── Credentials.h                 (mod) add drivers namespace decls
└── PoolMaster.h                  (unchanged)

src/
├── GpioOutputDriver.cpp          (NEW) digitalWrite wrapper + cached state
├── MqttOutputDriver.cpp          (NEW) publish + state-topic feedback
├── Drivers.cpp                   (NEW) slot registry, beginAll, tryRouteIncoming
├── Credentials.cpp               (mod) add drivers::load / drivers::save
├── Provisioning.cpp              (mod) add GET/POST /api/drivers endpoints
├── Setup.cpp                     (mod) call Drivers::beginAll, setDriver,
│                                         setInterlockSource, release old pins
├── CommandQueue.cpp              (mod) Relay command uses relay_drivers[i]
├── WebSocketHub.cpp              (mod) relays.r0/r1 read via driver->get()
├── MqttPublish.cpp               (mod) relay state publish via driver->get()
└── MqttBridge.cpp                (mod) onMqttConnect → resubscribe state topics
                                         onMqttMessage → dispatch to drivers

lib/Pump-master/src/
├── Pump.h                        (mod) setDriver, setInterlockSource,
│                                         syncStateFromDriver
└── Pump.cpp                      (mod) Start/Stop use _driver->set;
                                         Interlock() consults _interlockSrc;
                                         constructor builds default GPIO driver

web/src/
├── components/SectionTabs.tsx    (mod) TABS_SETTINGS += Drivers
├── app.tsx                       (mod) add /settings/drivers Route
├── lib/api.ts                    (unchanged)
└── screens/Drivers.tsx           (NEW) 6 per-slot configuration cards
```

---

## Phase 1 — Foundation (no runtime behaviour change)

Each task in this phase is additive. After Phase 1 is complete the device runs exactly as it does today; we've only added new code that's not yet wired into the runtime paths.

### Task 1: Create `sp4-pluggable-drivers` branch and record baseline

**Files:** None (branch + stored note).

- [ ] **Step 1: Create the branch from current sp3-web-ui tip**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
git status                 # verify clean tree (pre-existing .vscode/ untracked is fine)
git checkout -b sp4-pluggable-drivers
git branch --show-current  # expect: sp4-pluggable-drivers
```

- [ ] **Step 2: Capture baseline sizes for later diff**

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e serial_upload 2>&1 | grep -E 'RAM:|Flash:' > docs/superpowers/notes/sp4-baseline-size.txt
cat docs/superpowers/notes/sp4-baseline-size.txt
```

Expected: two lines showing RAM and Flash percentages. Typically around `RAM: ~23%` and `Flash: ~97%` given the current SP3 state.

- [ ] **Step 3: Commit the baseline note**

```bash
git add docs/superpowers/notes/sp4-baseline-size.txt
git commit -m "chore(sp4): record baseline size before pluggable drivers"
```

---

### Task 2: `OutputDriver` abstract interface

**Files:**
- Create: `include/OutputDriver.h`

- [ ] **Step 1: Create the header**

Content:
```cpp
#pragma once
// Output-driver abstract interface. Each of the six physical-output devices
// (FiltrationPump, PhPump, ChlPump, RobotPump, relay R0, relay R1) can be
// backed by either a GpioOutputDriver (local GPIO pin) or a MqttOutputDriver
// (MQTT-controlled external relay such as a Shelly). Drivers live in
// module-scope static storage inside Drivers.cpp for the program lifetime.

class OutputDriver {
public:
  virtual ~OutputDriver() = default;
  virtual void begin() = 0;                 // called once at boot
  virtual void set(bool on) = 0;            // command the output
  virtual bool get() const = 0;             // last-known authoritative state
  virtual const char* kindName() const = 0; // "gpio" | "mqtt"
};
```

- [ ] **Step 2: Verify it compiles stand-alone**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e serial_upload 2>&1 | tail -5
```
Expected: SUCCESS. A header that nothing includes is benign.

- [ ] **Step 3: Commit**

```bash
git add include/OutputDriver.h
git commit -m "sp4: add OutputDriver abstract interface"
```

---

### Task 3: `GpioOutputDriver` implementation

**Files:**
- Create: `include/GpioOutputDriver.h`
- Create: `src/GpioOutputDriver.cpp`

- [ ] **Step 1: Create the header `include/GpioOutputDriver.h`**

```cpp
#pragma once
#include <stdint.h>
#include "OutputDriver.h"

// Local-GPIO driver. Owns a pin number + active-level and translates
// OutputDriver::set(true/false) to digitalWrite. Cached state mirrors
// the last commanded value (same semantics the existing firmware
// already relies on via FiltrationPump.IsRunning() etc.).
class GpioOutputDriver : public OutputDriver {
public:
  GpioOutputDriver(uint8_t pin, bool activeLow);
  void begin() override;
  void set(bool on) override;
  bool get() const override { return _state; }
  const char* kindName() const override { return "gpio"; }
private:
  uint8_t _pin;
  bool    _activeLow;
  bool    _state;
};
```

- [ ] **Step 2: Create the impl `src/GpioOutputDriver.cpp`**

```cpp
#include "GpioOutputDriver.h"
#include <Arduino.h>

GpioOutputDriver::GpioOutputDriver(uint8_t pin, bool activeLow)
  : _pin(pin), _activeLow(activeLow), _state(false) {}

void GpioOutputDriver::begin() {
  pinMode(_pin, OUTPUT);
  // Initial level = OFF: active-low → HIGH, active-high → LOW.
  digitalWrite(_pin, _activeLow ? HIGH : LOW);
  _state = false;
}

void GpioOutputDriver::set(bool on) {
  digitalWrite(_pin, _activeLow ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
  _state = on;
}
```

- [ ] **Step 3: Verify it compiles**

```bash
pio run -e serial_upload 2>&1 | tail -5
```
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add include/GpioOutputDriver.h src/GpioOutputDriver.cpp
git commit -m "sp4: GpioOutputDriver — digitalWrite wrapper with cached state"
```

---

### Task 4: `MqttOutputDriver` implementation (publish only — state subscription wired in Phase 3)

**Files:**
- Create: `include/MqttOutputDriver.h`
- Create: `src/MqttOutputDriver.cpp`

- [ ] **Step 1: Create the header `include/MqttOutputDriver.h`**

```cpp
#pragma once
#include <Arduino.h>
#include "OutputDriver.h"

struct MqttDriverConfig {
  String cmdTopic;
  String payloadOn    = "on";
  String payloadOff   = "off";
  String stateTopic;              // empty = fire-and-forget
  String stateOn      = "on";
  String stateOff     = "off";
};

// MQTT-controlled external relay. Publishing uses the shared mqttClient
// instance defined in MqttBridge.cpp. State feedback is driven from
// Drivers::tryRouteIncoming which is called by MqttBridge::onMqttMessage;
// onStateMessage updates the cached _state bit.
class MqttOutputDriver : public OutputDriver {
public:
  explicit MqttOutputDriver(const MqttDriverConfig& cfg);
  void begin() override;                          // no-op; subscription is done by Drivers
  void set(bool on) override;                     // publish cmdTopic
  bool get() const override { return _state; }
  const char* kindName() const override { return "mqtt"; }

  const MqttDriverConfig& config() const { return _cfg; }
  void onStateMessage(const String& payload);     // called by Drivers::tryRouteIncoming
private:
  MqttDriverConfig _cfg;
  bool _state;
};
```

- [ ] **Step 2: Create the impl `src/MqttOutputDriver.cpp`**

```cpp
#include "MqttOutputDriver.h"
#include <espMqttClientAsync.h>
#include "LogBuffer.h"

// Defined in MqttBridge.cpp.
extern espMqttClientAsync mqttClient;

MqttOutputDriver::MqttOutputDriver(const MqttDriverConfig& cfg)
  : _cfg(cfg), _state(false) {}

void MqttOutputDriver::begin() {
  // No-op. Subscriptions are registered by Drivers::resubscribeStateTopics
  // from MqttBridge::onMqttConnect so they survive broker reconnects.
}

void MqttOutputDriver::set(bool on) {
  const char* topic   = _cfg.cmdTopic.c_str();
  const char* payload = on ? _cfg.payloadOn.c_str() : _cfg.payloadOff.c_str();
  uint16_t packetId = mqttClient.publish(topic, /*qos=*/1, /*retain=*/false, payload);
  if (packetId == 0) {
    LogBuffer::append(LogBuffer::L_WARN,
      "[drv] publish failed: %s payload=%s (broker disconnected or queue full)",
      topic, payload);
  }
  // Deliberately NOT caching _state here — authoritative state comes only
  // from the broker's echo on stateTopic. For fire-and-forget configs
  // (empty stateTopic) the cached _state therefore stays false; this is
  // documented in the UI as "Pump will report 'not running'".
}

void MqttOutputDriver::onStateMessage(const String& payload) {
  if (payload == _cfg.stateOn)       _state = true;
  else if (payload == _cfg.stateOff) _state = false;
  // Unrecognised payloads are ignored — leave _state unchanged.
}
```

- [ ] **Step 3: Verify it compiles**

```bash
pio run -e serial_upload 2>&1 | tail -5
```
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add include/MqttOutputDriver.h src/MqttOutputDriver.cpp
git commit -m "sp4: MqttOutputDriver — publish cmd + cached state from echo"
```

---

### Task 5: `Credentials::drivers` NVS load/save

**Files:**
- Modify: `include/Credentials.h`
- Modify: `src/Credentials.cpp`

- [ ] **Step 1: Add the decls to `include/Credentials.h`**

Find the closing `} // namespace Credentials` line. Insert, immediately before that closing brace:

```cpp
// SP4 — per-slot output-driver configuration. Slot ids are the six short
// strings "filt", "ph", "chl", "robot", "r0", "r1". kind 0 = gpio, 1 = mqtt.
namespace drivers {
  struct DriverConfig {
    uint8_t kind        = 0;    // 0 gpio, 1 mqtt
    uint8_t pin         = 0xFF; // gpio pin number (invalid = 0xFF)
    uint8_t activeLevel = 0;    // gpio: 0 low, 1 high
    String cmdTopic;
    String payloadOn    = "on";
    String payloadOff   = "off";
    String stateTopic;
    String stateOn      = "on";
    String stateOff     = "off";
  };

  // Load returns per-slot defaults if the NVS keys are absent.
  DriverConfig load(const char* slot);
  bool save(const char* slot, const DriverConfig& cfg);
} // namespace drivers
```

- [ ] **Step 2: Add the impl in `src/Credentials.cpp`**

Append at the end of the `namespace Credentials {` block (just before the closing brace of the namespace):

```cpp
namespace drivers {

// Hardcoded per-slot default: pin from Config.h; every slot today is
// active-low (matches the PUMP_ON=0, PUMP_OFF=1 convention + current
// RELAY_R{0,1} wiring).
static DriverConfig slotDefault(const char* slot) {
  DriverConfig d;
  d.kind = 0;
  d.activeLevel = 0;
  if      (strcmp(slot, "filt")  == 0) d.pin = FILTRATION_PUMP;
  else if (strcmp(slot, "ph")    == 0) d.pin = PH_PUMP;
  else if (strcmp(slot, "chl")   == 0) d.pin = CHL_PUMP;
  else if (strcmp(slot, "robot") == 0) d.pin = ROBOT_PUMP;
  else if (strcmp(slot, "r0")    == 0) d.pin = RELAY_R0;
  else if (strcmp(slot, "r1")    == 0) d.pin = RELAY_R1;
  else                                 d.pin = 0xFF; // unknown slot
  return d;
}

static String keyOf(const char* slot, const char* suffix) {
  String k = slot;
  k += '.';
  k += suffix;
  return k;
}

DriverConfig load(const char* slot) {
  DriverConfig d = slotDefault(slot);
  Preferences nvs;
  if (!nvs.begin("drivers", true)) return d;
  d.kind        = nvs.getUChar(keyOf(slot, "kind").c_str(),  d.kind);
  d.pin         = nvs.getUChar(keyOf(slot, "pin").c_str(),   d.pin);
  d.activeLevel = nvs.getUChar(keyOf(slot, "al").c_str(),    d.activeLevel);
  d.cmdTopic    = nvs.getString(keyOf(slot, "ct").c_str(),   d.cmdTopic);
  d.payloadOn   = nvs.getString(keyOf(slot, "pn").c_str(),   d.payloadOn);
  d.payloadOff  = nvs.getString(keyOf(slot, "pf").c_str(),   d.payloadOff);
  d.stateTopic  = nvs.getString(keyOf(slot, "st").c_str(),   d.stateTopic);
  d.stateOn     = nvs.getString(keyOf(slot, "sn").c_str(),   d.stateOn);
  d.stateOff    = nvs.getString(keyOf(slot, "sf").c_str(),   d.stateOff);
  nvs.end();
  return d;
}

bool save(const char* slot, const DriverConfig& cfg) {
  Preferences nvs;
  if (!nvs.begin("drivers", false)) return false;
  size_t w = 0;
  w += nvs.putUChar(keyOf(slot, "kind").c_str(), cfg.kind);
  w += nvs.putUChar(keyOf(slot, "pin").c_str(),  cfg.pin);
  w += nvs.putUChar(keyOf(slot, "al").c_str(),   cfg.activeLevel);
  w += nvs.putString(keyOf(slot, "ct").c_str(),  cfg.cmdTopic);
  w += nvs.putString(keyOf(slot, "pn").c_str(),  cfg.payloadOn);
  w += nvs.putString(keyOf(slot, "pf").c_str(),  cfg.payloadOff);
  w += nvs.putString(keyOf(slot, "st").c_str(),  cfg.stateTopic);
  w += nvs.putString(keyOf(slot, "sn").c_str(),  cfg.stateOn);
  w += nvs.putString(keyOf(slot, "sf").c_str(),  cfg.stateOff);
  nvs.end();
  return w > 0;
}

} // namespace drivers
```

Note: the existing `Credentials.cpp` already includes `<Preferences.h>` and `Config.h`. No new includes required.

- [ ] **Step 3: Verify it compiles**

```bash
pio run -e serial_upload 2>&1 | tail -5
```
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add include/Credentials.h src/Credentials.cpp
git commit -m "sp4: Credentials::drivers namespace — per-slot NVS load/save"
```

---

### Task 6: `Drivers` registry module — GPIO only for now

**Files:**
- Create: `include/Drivers.h`
- Create: `src/Drivers.cpp`

The MQTT-specific methods (`resubscribeStateTopics`, `tryRouteIncoming`) are declared now so MqttBridge can be wired up later in Phase 3, but they're implemented as no-ops until Task 11.

- [ ] **Step 1: Create the header `include/Drivers.h`**

```cpp
#pragma once
#include <Arduino.h>
#include "OutputDriver.h"

// Module-scope registry of the six output drivers — one per device slot.
// Drivers live for the lifetime of the program. Configuration is read
// once at boot via Credentials::drivers::load; after beginAll() the
// pointers are immutable.

namespace Drivers {

// Populate the six drivers from NVS + call begin() on each.
// Also releases any GPIO pin that was the saved default for a slot now
// bound to MQTT (pinMode(old_pin, INPUT) to tri-state the output).
void beginAll();

// Returns the driver for a slot id ("filt", "ph", "chl", "robot", "r0", "r1").
// Returns nullptr if the slot id is unknown. Callers in setup() can deref
// directly; runtime callers should null-check.
OutputDriver* get(const char* slot);

// Iterate all MQTT drivers with a non-empty state topic and subscribe.
// Called from MqttBridge::onMqttConnect so subscriptions survive broker
// reconnects. Safe to call multiple times.
void resubscribeStateTopics();

// Dispatch an inbound MQTT message to the matching driver's state topic,
// if any. Returns true if a driver consumed the message.
// Called from MqttBridge::onMqttMessage BEFORE the existing HA set-topic
// handler, so driver state-topic traffic doesn't leak into the HA path.
bool tryRouteIncoming(const char* topic, const String& payload);

} // namespace Drivers
```

- [ ] **Step 2: Create the impl `src/Drivers.cpp`**

```cpp
#include "Drivers.h"
#include "GpioOutputDriver.h"
#include "MqttOutputDriver.h"
#include "Credentials.h"
#include "LogBuffer.h"

namespace Drivers {

// Six slot ids. Fixed set — no runtime resize.
static const char* const SLOTS[] = { "filt", "ph", "chl", "robot", "r0", "r1" };
static constexpr size_t   SLOT_COUNT = 6;

// Static storage for driver objects. We construct one of the two union
// members per slot at beginAll() time via placement new.
alignas(GpioOutputDriver) static uint8_t gpioStorage[SLOT_COUNT][sizeof(GpioOutputDriver)];
alignas(MqttOutputDriver) static uint8_t mqttStorage[SLOT_COUNT][sizeof(MqttOutputDriver)];

// The active pointer for each slot. Exactly one of gpioStorage[i] or
// mqttStorage[i] is constructed; the other is unused memory.
static OutputDriver* g_drivers[SLOT_COUNT] = {};

static int slotIndex(const char* slot) {
  for (size_t i = 0; i < SLOT_COUNT; ++i) if (strcmp(SLOTS[i], slot) == 0) return (int)i;
  return -1;
}

void beginAll() {
  for (size_t i = 0; i < SLOT_COUNT; ++i) {
    Credentials::drivers::DriverConfig cfg = Credentials::drivers::load(SLOTS[i]);

    if (cfg.kind == 1) {
      // MQTT-bound: release the old default GPIO to tri-state so we don't
      // keep driving a pin that may be wired to something else now.
      Credentials::drivers::DriverConfig dflt;
      dflt.pin = 0xFF;
      // Use the default table's pin (not cfg.pin, which may have been set
      // by the user in the UI to a new GPIO when they switched to MQTT —
      // we only need to release whatever pin is saved).
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
}

OutputDriver* get(const char* slot) {
  int i = slotIndex(slot);
  return (i < 0) ? nullptr : g_drivers[i];
}

void resubscribeStateTopics() {
  // Phase 1: no-op. Wired up in Task 11.
}

bool tryRouteIncoming(const char* /*topic*/, const String& /*payload*/) {
  // Phase 1: no-op. Wired up in Task 11.
  return false;
}

} // namespace Drivers
```

- [ ] **Step 3: Verify it compiles**

```bash
pio run -e serial_upload 2>&1 | tail -5
```
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add include/Drivers.h src/Drivers.cpp
git commit -m "sp4: Drivers module — slot registry, beginAll (GPIO only for now)"
```

---

### Task 7: Extend `Pump` class

**Files:**
- Modify: `lib/Pump-master/src/Pump.h`
- Modify: `lib/Pump-master/src/Pump.cpp`

The existing `Pump(pumppin, relaypin, …)` constructor must keep working — it's used by the four file-scope globals in `Setup.cpp`. That constructor now builds a default `GpioOutputDriver` internally. `setDriver()` replaces the internal driver at runtime.

- [ ] **Step 1: Read the existing Pump.h to find insertion points**

```bash
sed -n '1,80p' lib/Pump-master/src/Pump.h
```
Note the existing class body (particularly `public:` / `private:` sections) so the additions land in sensible places.

- [ ] **Step 2: Edit `lib/Pump-master/src/Pump.h` — add forward decl, new public methods, new private fields**

At the top of the file, AFTER the existing `#define` block and BEFORE `class Pump {`, add:

```cpp
// SP4 — forward decl to avoid pulling OutputDriver.h into every Pump consumer.
class OutputDriver;
```

Inside `class Pump {` under the existing `public:` section (right after the constructor declaration):

```cpp
    // SP4 — replace the internal driver at runtime. Called once from setup()
    // after Drivers::beginAll() has built the configured drivers.
    void setDriver(OutputDriver* driver);

    // SP4 — if the driver is MQTT-kind with state feedback, pull its cached
    // state into the internal running flag. No-op for GPIO drivers.
    // Called once per second by the WsBroadcast task.
    void syncStateFromDriver();

    // SP4 — interlock override. If set, Interlock() returns src->IsRunning()
    // instead of digitalRead(interlockPin). Used to keep the dosing-pump
    // interlock correct when FiltrationPump is bound to an MQTT driver and
    // its GPIO pin is no longer authoritative.
    void setInterlockSource(const Pump* src);
```

Inside `class Pump {` under the `private:` section, add:

```cpp
    OutputDriver* _driver;          // always non-null; owned elsewhere
    const Pump*   _interlockSrc;    // optional; if set, overrides digitalRead
```

- [ ] **Step 3: Edit `lib/Pump-master/src/Pump.cpp` — constructor builds a default GpioOutputDriver**

At the very top of the file, add the includes (adjust if Pump.cpp uses a different include style — match what's already there):

```cpp
#include "../../../include/OutputDriver.h"
#include "../../../include/GpioOutputDriver.h"
```

(These relative paths work because PlatformIO puts `include/` on the global include path anyway; if the compiler complains switch to plain `"OutputDriver.h"` and `"GpioOutputDriver.h"`.)

Find the constructor definition (something like `Pump::Pump(uint8_t pumppin, …)`) and at its end, after all the existing member-initialisers / assignments, add:

```cpp
  // SP4 — build a default internal driver from the legacy pin parameter.
  // pumppin is wired active-low on all six existing devices.
  static GpioOutputDriver defaultGpio(pumppin, /*activeLow=*/true);
  _driver = &defaultGpio;
  _interlockSrc = nullptr;
```

**Important:** the `static GpioOutputDriver defaultGpio(...)` line constructs a function-local static, which means its storage persists for the lifetime of the program. Since it's inside a non-template constructor, ONE static instance exists per translation unit — but Pump is instantiated four times (file-scope globals). Each invocation of the constructor would attempt to redefine the same static — that is a bug.

**Correct approach:** allocate the default driver with `new`. The driver is leaked at program exit which is fine on an embedded device that never exits.

Replace the two lines above with:

```cpp
  // SP4 — build a default internal driver from the legacy pin parameter.
  // pumppin is wired active-low on all six existing devices. This default
  // is replaced by setDriver() from setup() after Drivers::beginAll().
  _driver = new GpioOutputDriver(pumppin, /*activeLow=*/true);
  _interlockSrc = nullptr;
```

- [ ] **Step 4: Edit Pump.cpp — `Start()` and `Stop()` use the driver**

Find `bool Pump::Start()` — inside it, find the `digitalWrite(pumppin, …)` call that activates the pump and replace it with:

```cpp
  _driver->set(true);
```

Find `bool Pump::Stop()` — find the `digitalWrite(pumppin, …)` call that deactivates the pump and replace it with:

```cpp
  _driver->set(false);
```

Leave everything else (UpTime tracking, tank logic, the return value) untouched.

- [ ] **Step 5: Edit Pump.cpp — `Interlock()` consults the interlock source**

Find `bool Pump::Interlock()` — change the body from whatever it is today (roughly `return digitalRead(interlockpin) == INTERLOCK_OK;`) to:

```cpp
bool Pump::Interlock() {
  if (_interlockSrc) return _interlockSrc->IsRunning();
  if (interlockpin == NO_INTERLOCK) return true;
  return digitalRead(interlockpin) == INTERLOCK_OK;
}
```

- [ ] **Step 6: Edit Pump.cpp — add the three new methods at the bottom of the file**

```cpp
void Pump::setDriver(OutputDriver* driver) {
  if (driver) _driver = driver;
}

void Pump::setInterlockSource(const Pump* src) {
  _interlockSrc = src;
}

void Pump::syncStateFromDriver() {
  // Only pull state from the driver when it's MQTT-kind — GPIO drivers
  // already reflect exactly what Start()/Stop() wrote.
  if (!_driver) return;
  if (strcmp(_driver->kindName(), "mqtt") != 0) return;
  // When driver reports ON but our internal flag says OFF (or vice-versa),
  // update the flag but do NOT re-trigger Start()/Stop() logic — just
  // reflect reality into IsRunning so PID loops and HA state match.
  // Existing fields (StartTime/StopTime/UpTime) are intentionally not
  // updated here: those track commanded activity, not observed activity.
  bool observed = _driver->get();
  // `isrunning` is the existing private bool backing IsRunning(). If the
  // existing code uses a different name (check Pump.h), adjust:
  if (isrunning != observed) isrunning = observed;
}
```

**Naming check**: if Pump.h's private running flag is called something other than `isrunning` (e.g. `_isrunning`, `running`, etc.), use that name instead. Verify with `grep 'bool.*running' lib/Pump-master/src/Pump.h`.

- [ ] **Step 7: Verify it compiles**

```bash
pio run -e serial_upload 2>&1 | tail -10
```
Expected: SUCCESS. If there's an "undefined reference" or "no member named 'isrunning'" error, fix the field name per the note in Step 6.

- [ ] **Step 8: Commit**

```bash
git add lib/Pump-master/src/Pump.h lib/Pump-master/src/Pump.cpp
git commit -m "sp4: Pump — setDriver, setInterlockSource, syncStateFromDriver"
```

---

### Task 8: Wire `Drivers::beginAll` + per-pump `setDriver` / `setInterlockSource` into `setup()`

**Files:**
- Modify: `src/Setup.cpp`

- [ ] **Step 1: Add the Drivers include**

Find the existing include block at the top of `src/Setup.cpp` and add:

```cpp
#include "Drivers.h"
```

near the other SP3 includes (`#include "WebSocketHub.h"` etc).

- [ ] **Step 2: Find the current pinMode/digitalWrite block for the pumps and relays**

```bash
grep -n 'pinMode\|digitalWrite.*RELAY_R\|FILTRATION_PUMP\|PH_PUMP\|CHL_PUMP\|ROBOT_PUMP' src/Setup.cpp | head -20
```

You should see something like lines 186–202 that call `pinMode(X, OUTPUT)` and `digitalWrite(X, HIGH)` for the four pumps and the two relays.

- [ ] **Step 3: Replace the pump/relay pin setup block with a `Drivers::beginAll()` call + per-pump driver wiring**

In `src/Setup.cpp`, locate the block (approx. lines 186-202) that currently reads:

```cpp
  //Define pins directions
  pinMode(FILTRATION_PUMP, OUTPUT);
  pinMode(PH_PUMP, OUTPUT);
  pinMode(CHL_PUMP, OUTPUT);
  pinMode(ROBOT_PUMP, OUTPUT);

  pinMode(RELAY_R0, OUTPUT);
  pinMode(RELAY_R1, OUTPUT);

  //Turn off the pumps
  digitalWrite(FILTRATION_PUMP, HIGH);
  digitalWrite(PH_PUMP, HIGH);
  digitalWrite(CHL_PUMP, HIGH);
  digitalWrite(ROBOT_PUMP, HIGH);
  digitalWrite(RELAY_R0, HIGH);
  digitalWrite(RELAY_R1, HIGH);
```

Replace the ENTIRE block with:

```cpp
  //SP4 — build the six output drivers from NVS config + call begin() on each.
  //GpioOutputDriver::begin() does the pinMode(OUTPUT) + initial OFF write.
  Drivers::beginAll();

  //SP4 — swap each pump's internal default driver for the configured one.
  //Default drivers constructed inside each Pump's constructor are harmless
  //placeholders; they've already run their begin() identical to the new one
  //so no extra hardware flap. After this line the pumps are driven by the
  //configured drivers (GPIO or MQTT).
  FiltrationPump.setDriver(Drivers::get("filt"));
  PhPump.setDriver       (Drivers::get("ph"));
  ChlPump.setDriver      (Drivers::get("chl"));
  RobotPump.setDriver    (Drivers::get("robot"));

  //SP4 — rewire dosing-pump interlock from digitalRead(FILTRATION_PUMP) to
  //FiltrationPump.IsRunning(). Stays correct if FiltrationPump is bound
  //to an MQTT driver (the GPIO pin is then released and would read garbage).
  //See design spec §8.1.
  PhPump.setInterlockSource (&FiltrationPump);
  ChlPump.setInterlockSource(&FiltrationPump);
  RobotPump.setInterlockSource(&FiltrationPump);
```

- [ ] **Step 4: Find the `WsBroadcast` task body and call `syncStateFromDriver()` each tick**

Find the `WsBroadcast` task definition (approx. line 465+) that looks like:

```cpp
  xTaskCreatePinnedToCore(
    [](void*) {
      uint32_t persistTick = 0;
      for(;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!startTasks) continue;
        WebSocketHub::tick();
        if (++persistTick >= 60) { … }
      }
    },
```

Just before the `WebSocketHub::tick();` line inside the loop, add:

```cpp
        // SP4 — keep pump internal-running flag in sync with MQTT driver
        // state feedback (no-op for GPIO drivers).
        FiltrationPump.syncStateFromDriver();
        PhPump.syncStateFromDriver();
        ChlPump.syncStateFromDriver();
        RobotPump.syncStateFromDriver();
```

- [ ] **Step 5: Verify it compiles**

```bash
pio run -e serial_upload 2>&1 | tail -10
```
Expected: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add src/Setup.cpp
git commit -m "sp4: wire Drivers::beginAll + per-pump setDriver + interlock rewiring"
```

---

### Task 9: Replace `Relay` command direct `digitalWrite` with driver calls

**Files:**
- Modify: `src/CommandQueue.cpp`

- [ ] **Step 1: Add Drivers include**

Near the top of `src/CommandQueue.cpp` (alongside the existing `#include "MqttBridge.h"` we added in the SP3 hotfix), add:

```cpp
#include "Drivers.h"
```

- [ ] **Step 2: Replace the `Relay` command branch**

Find the block (approx. lines 471–481):

```cpp
        else if (command[F("Relay")].is<JsonVariant>())
        {
          switch ((int)command[F("Relay")][0])
          {
            case 0:
              (bool)command[F("Relay")][1] ? digitalWrite(RELAY_R0, LOW) : digitalWrite(RELAY_R0, HIGH);
              break;
            case 1:
              (bool)command[F("Relay")][1] ? digitalWrite(RELAY_R1, LOW) : digitalWrite(RELAY_R1, HIGH);
              break;
          }
        }
```

Replace with:

```cpp
        else if (command[F("Relay")].is<JsonVariant>())
        {
          int idx = (int)command[F("Relay")][0];
          bool on = (bool)command[F("Relay")][1];
          const char* slot = (idx == 0) ? "r0" : (idx == 1) ? "r1" : nullptr;
          if (slot) {
            OutputDriver* d = Drivers::get(slot);
            if (d) d->set(on);
          }
        }
```

- [ ] **Step 3: Verify compile**

```bash
pio run -e serial_upload 2>&1 | tail -5
```
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/CommandQueue.cpp
git commit -m "sp4: Relay JSON cmd uses Drivers::get(slot)->set — no direct digitalWrite"
```

---

### Task 10: Relay state reads via driver

**Files:**
- Modify: `src/WebSocketHub.cpp`
- Modify: `src/MqttPublish.cpp`

- [ ] **Step 1: WebSocketHub — replace `!digitalRead(RELAY_R*)`**

Add `#include "Drivers.h"` near the other SP3 includes in `src/WebSocketHub.cpp`.

Find the block in `buildStateJson()` (added in SP3):

```cpp
  JsonObject relays = data["relays"].to<JsonObject>();
  relays["r0"] = !digitalRead(RELAY_R0);
  relays["r1"] = !digitalRead(RELAY_R1);
```

Replace with:

```cpp
  JsonObject relays = data["relays"].to<JsonObject>();
  OutputDriver* dR0 = Drivers::get("r0");
  OutputDriver* dR1 = Drivers::get("r1");
  relays["r0"] = dR0 ? dR0->get() : false;
  relays["r1"] = dR1 ? dR1->get() : false;
```

- [ ] **Step 2: MqttPublish — replace `!digitalRead(RELAY_R*)` in the same way**

Add `#include "Drivers.h"` near the top of `src/MqttPublish.cpp`.

Find lines 106-107:

```cpp
      pubBool("relay_r0_projecteur", !digitalRead(RELAY_R0));
      pubBool("relay_r1_spare",      !digitalRead(RELAY_R1));
```

Replace with:

```cpp
      {
        OutputDriver* dR0 = Drivers::get("r0");
        OutputDriver* dR1 = Drivers::get("r1");
        pubBool("relay_r0_projecteur", dR0 ? dR0->get() : false);
        pubBool("relay_r1_spare",      dR1 ? dR1->get() : false);
      }
```

- [ ] **Step 3: Verify compile**

```bash
pio run -e serial_upload 2>&1 | tail -5
```
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/WebSocketHub.cpp src/MqttPublish.cpp
git commit -m "sp4: relay state reads go via Drivers::get()->get()"
```

---

### Task 11: Phase 1 checkpoint — OTA + zero-regression smoke

**Files:** none (verification + OTA).

This is the end of Phase 1. All six devices still use GPIO drivers by default; nothing has changed from the user's perspective. We flash and verify that everything still works.

- [ ] **Step 1: Build locally and inspect bundle size**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e serial_upload 2>&1 | grep -E 'RAM:|Flash:'
```
Expected: Flash < 100%. Phase 1 additions are small (~2-4 KB).

- [ ] **Step 2: Pre-flash sanity**

```bash
curl -sS --max-time 5 -o /dev/null -w 'HTTP %{http_code}\n' http://poolmaster.local/healthz
```
Expected: HTTP 200.

- [ ] **Step 3: OTA firmware**

```bash
export POOLMASTER_OTA_PWD=""
pio run -e OTA_upload -t upload 2>&1 | tail -3
sleep 18
curl -sS --max-time 5 -o /dev/null -w 'post-fw HTTP %{http_code}\n' http://poolmaster.local/healthz
```
Expected: SUCCESS + HTTP 200 after reboot.

- [ ] **Step 4: Smoke-test the six devices — all still work via GPIO**

From a separate shell, pull state and exercise each device:

```bash
# Confirm all driver slots booted as GPIO:
curl -sS http://poolmaster.local/api/state | python3 -c 'import json,sys;d=json.load(sys.stdin);print("pumps:",d["pumps"]);print("relays:",d["relays"])'

# Flip filtration via WS cmd; state should change within 1s:
curl -sS -H 'Content-Type: application/json' -X POST -d '{"FiltPump":1}' http://poolmaster.local/api/cmd
sleep 2
curl -sS http://poolmaster.local/api/state | python3 -c 'import json,sys;print("filt:",json.load(sys.stdin)["pumps"]["filtration"])'
# Expect: filt: True

curl -sS -H 'Content-Type: application/json' -X POST -d '{"FiltPump":0}' http://poolmaster.local/api/cmd

# Flip R0 via WS cmd:
curl -sS -H 'Content-Type: application/json' -X POST -d '{"Relay":[0,1]}' http://poolmaster.local/api/cmd
sleep 1
curl -sS http://poolmaster.local/api/state | python3 -c 'import json,sys;print("r0:",json.load(sys.stdin)["relays"]["r0"])'
# Expect: r0: True
curl -sS -H 'Content-Type: application/json' -X POST -d '{"Relay":[0,0]}' http://poolmaster.local/api/cmd
```

- [ ] **Step 5: Verify `[drv]` init lines appear in Logs**

Open the SP3 UI at `http://poolmaster.local/insights/logs` (or via API). Expect 6 entries:
```
inf [drv] filt kind=gpio pin=... al=1
inf [drv] ph kind=gpio pin=... al=1
inf [drv] chl kind=gpio pin=... al=1
inf [drv] robot kind=gpio pin=... al=1
inf [drv] r0 kind=gpio pin=27 al=1
inf [drv] r1 kind=gpio pin=4 al=1
```

- [ ] **Step 6: Create Phase 1 checkpoint branch**

```bash
git branch sp4-ckpt1-phase1
git branch --list 'sp4*'
```

---

## Phase 2 — MQTT driver runtime integration

Add state-topic subscription + message dispatch so the MQTT driver is fully functional. Still no UI; we test by configuring a slot manually via `Preferences` or `curl` after the backend endpoints land in Phase 3.

### Task 12: Implement `Drivers::resubscribeStateTopics` and `Drivers::tryRouteIncoming`

**Files:**
- Modify: `src/Drivers.cpp`

- [ ] **Step 1: Extend `Drivers.cpp` — replace the Phase-1 no-op impls**

At the top of the file, add an include (keep existing ones):

```cpp
#include <espMqttClientAsync.h>
```

Add, near the top of the `namespace Drivers {` body, a small helper for iterating active MQTT drivers:

```cpp
extern espMqttClientAsync mqttClient;   // defined in MqttBridge.cpp

static MqttOutputDriver* asMqtt(OutputDriver* d) {
  if (!d) return nullptr;
  if (strcmp(d->kindName(), "mqtt") != 0) return nullptr;
  return static_cast<MqttOutputDriver*>(d);
}
```

Replace the existing stub `void resubscribeStateTopics() { … }` with:

```cpp
void resubscribeStateTopics() {
  for (size_t i = 0; i < SLOT_COUNT; ++i) {
    MqttOutputDriver* m = asMqtt(g_drivers[i]);
    if (!m) continue;
    const String& st = m->config().stateTopic;
    if (st.isEmpty()) continue;
    mqttClient.subscribe(st.c_str(), 1);
    LogBuffer::append(LogBuffer::L_INFO, "[drv] subscribed %s → %s",
      SLOTS[i], st.c_str());
  }
}
```

Replace the existing stub `bool tryRouteIncoming(...) { … }` with:

```cpp
bool tryRouteIncoming(const char* topic, const String& payload) {
  for (size_t i = 0; i < SLOT_COUNT; ++i) {
    MqttOutputDriver* m = asMqtt(g_drivers[i]);
    if (!m) continue;
    const String& st = m->config().stateTopic;
    if (st.isEmpty()) continue;
    if (st == topic) {
      m->onStateMessage(payload);
      return true;
    }
  }
  return false;
}
```

- [ ] **Step 2: Verify compile**

```bash
pio run -e serial_upload 2>&1 | tail -5
```
Expected: SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add src/Drivers.cpp
git commit -m "sp4: Drivers — resubscribeStateTopics + tryRouteIncoming impl"
```

---

### Task 13: Hook MqttBridge into the Drivers subsystem

**Files:**
- Modify: `src/MqttBridge.cpp`

- [ ] **Step 1: Add the Drivers include**

Near the top of `src/MqttBridge.cpp`, alongside the existing includes, add:

```cpp
#include "Drivers.h"
```

- [ ] **Step 2: Call `Drivers::resubscribeStateTopics()` from `onMqttConnect`**

Find the existing `onMqttConnect` body. It currently calls `HaDiscovery::publishAll()` and `sweepLegacyRetainedTopics()`. Add one line right before `MQTTConnection = true;`:

```cpp
  Drivers::resubscribeStateTopics();
```

So it ends up looking like:

```cpp
static void onMqttConnect(bool sessionPresent)
{
  Debug.print(DBG_INFO, "[MQTT] Connected, session: %d", sessionPresent);
  LogBuffer::append(LogBuffer::L_INFO, "[MQTT] connected (sessionPresent=%d) — publishing HA discovery",
                    (int)sessionPresent);
  HaDiscovery::publishAll();
  sweepLegacyRetainedTopics();
  Drivers::resubscribeStateTopics();
  MQTTConnection = true;
}
```

- [ ] **Step 3: Dispatch via `Drivers::tryRouteIncoming` in `onMqttMessage`**

Find `onMqttMessage` (the callback that handles inbound broker traffic). Inside it — right AFTER the `char payloadStr[32]; … payloadStr[n] = '\0';` block and BEFORE the existing HA set-topic handler dispatch — insert:

```cpp
  // SP4 — if an MQTT driver owns this topic (state feedback for a Shelly or
  // similar), route it and return before the HA set-topic handler runs.
  if (Drivers::tryRouteIncoming(topic, String(payloadStr))) return;
```

- [ ] **Step 4: Verify compile**

```bash
pio run -e serial_upload 2>&1 | tail -5
```
Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/MqttBridge.cpp
git commit -m "sp4: MqttBridge — re-subscribe driver state topics + dispatch to drivers"
```

---

### Task 14: Backend `GET /api/drivers`

**Files:**
- Modify: `src/Provisioning.cpp`

- [ ] **Step 1: Add the endpoint inside `registerRuntimeRoutes`**

Just below the existing `GET /api/admin/status` handler (~line 40 of the function), add:

```cpp
  // SP4 — GET /api/drivers returns the six slot configs. Admin-auth gated
  // like the other /api/*/config endpoints. No secrets — driver config
  // doesn't contain credentials.
  srv.on("/api/drivers", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;
    static const char* const SLOTS[] = { "filt", "ph", "chl", "robot", "r0", "r1" };
    String body = "[";
    for (size_t i = 0; i < 6; ++i) {
      auto c = Credentials::drivers::load(SLOTS[i]);
      if (i) body += ",";
      body += "{\"slot\":\"";     body += SLOTS[i];       body += "\"";
      body += ",\"kind\":\"";     body += (c.kind == 1 ? "mqtt" : "gpio"); body += "\"";
      body += ",\"pin\":";        body += (int)c.pin;
      body += ",\"active_level\":\""; body += (c.activeLevel == 1 ? "high" : "low"); body += "\"";
      body += ",\"cmd_topic\":";   body += '"'; body += c.cmdTopic;    body += '"';
      body += ",\"payload_on\":";  body += '"'; body += c.payloadOn;   body += '"';
      body += ",\"payload_off\":"; body += '"'; body += c.payloadOff;  body += '"';
      body += ",\"state_topic\":"; body += '"'; body += c.stateTopic;  body += '"';
      body += ",\"state_on\":";    body += '"'; body += c.stateOn;     body += '"';
      body += ",\"state_off\":";   body += '"'; body += c.stateOff;    body += '"';
      body += "}";
    }
    body += "]";
    req->send(200, "application/json", body);
  });
```

Note: string values inside `cmdTopic` etc. are currently written without JSON-escaping. For the MVP this is acceptable because the UI only writes topic strings without characters that need escaping (`"`, `\`, control chars). Task 15 adds server-side validation to reject problematic characters.

- [ ] **Step 2: Verify compile**

```bash
pio run -e serial_upload 2>&1 | tail -5
```
Expected: SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add src/Provisioning.cpp
git commit -m "sp4: GET /api/drivers returns the six slot configs"
```

---

### Task 15: Backend `POST /api/drivers/<slot>` + validation

**Files:**
- Modify: `src/Provisioning.cpp`

- [ ] **Step 1: Add a catch-all `/api/drivers/` POST handler**

Insert immediately after the GET /api/drivers handler from Task 14:

```cpp
  // SP4 — POST /api/drivers/<slot> saves the config for one slot + reboots.
  // Form-encoded body to match the WiFi/MQTT save pattern. Admin-auth gated.
  srv.on("/api/drivers", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;
    // Slot is the path suffix, e.g. /api/drivers/filt
    String path = req->url();
    int slashAt = path.lastIndexOf('/');
    if (slashAt < 0 || (size_t)slashAt + 1 >= path.length()) {
      req->send(400, "text/plain", "missing slot in path"); return;
    }
    String slot = path.substring(slashAt + 1);
    static const char* const SLOTS[] = { "filt", "ph", "chl", "robot", "r0", "r1" };
    bool validSlot = false;
    for (auto s : SLOTS) if (slot == s) { validSlot = true; break; }
    if (!validSlot) { req->send(400, "text/plain", "unknown slot"); return; }

    auto cfg = Credentials::drivers::load(slot.c_str());

    auto grab = [&](const char* name, String& dst) {
      if (req->hasParam(name, true)) dst = req->getParam(name, true)->value();
    };

    if (req->hasParam("kind", true)) {
      String k = req->getParam("kind", true)->value();
      cfg.kind = (k == "mqtt") ? 1 : 0;
    }
    if (req->hasParam("pin", true)) {
      int p = req->getParam("pin", true)->value().toInt();
      if (p < 0 || p > 39) { req->send(400, "text/plain", "pin out of range"); return; }
      cfg.pin = (uint8_t)p;
    }
    if (req->hasParam("active_level", true)) {
      String al = req->getParam("active_level", true)->value();
      cfg.activeLevel = (al == "high") ? 1 : 0;
    }
    grab("cmd_topic",    cfg.cmdTopic);
    grab("payload_on",   cfg.payloadOn);
    grab("payload_off",  cfg.payloadOff);
    grab("state_topic",  cfg.stateTopic);
    grab("state_on",     cfg.stateOn);
    grab("state_off",    cfg.stateOff);

    // Server-side validation beyond what the UI already does.
    if (cfg.kind == 1 && cfg.cmdTopic.isEmpty()) {
      req->send(400, "text/plain", "mqtt kind requires cmd_topic"); return;
    }
    if (cfg.payloadOn.isEmpty() || cfg.payloadOff.isEmpty()) {
      req->send(400, "text/plain", "payloads may not be empty"); return;
    }
    // Reject control chars and quotes in any string field (they'd break
    // the naïvely-serialised GET response in Task 14).
    auto badChars = [](const String& s) {
      for (size_t i = 0; i < s.length(); ++i) {
        char c = s.charAt(i);
        if (c == '"' || c == '\\' || c < 0x20) return true;
      }
      return false;
    };
    if (badChars(cfg.cmdTopic) || badChars(cfg.payloadOn) || badChars(cfg.payloadOff) ||
        badChars(cfg.stateTopic) || badChars(cfg.stateOn) || badChars(cfg.stateOff)) {
      req->send(400, "text/plain", "control chars / quotes not allowed"); return;
    }

    if (!Credentials::drivers::save(slot.c_str(), cfg)) {
      req->send(500, "text/plain", "NVS write failed"); return;
    }
    Debug.print(DBG_INFO, "[Prov] driver %s saved, rebooting", slot.c_str());
    req->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    xTaskCreate([](void*) { vTaskDelay(pdMS_TO_TICKS(500)); ESP.restart(); },
                "reboot", 2048, nullptr, 1, nullptr);
  });
```

- [ ] **Step 2: Register the route to match `/api/drivers/*` — AsyncWebServer route patterns**

AsyncWebServer does NOT auto-match wildcard path suffixes the way the handler above assumes. `srv.on("/api/drivers", HTTP_POST, …)` only matches the exact path `/api/drivers` — a POST to `/api/drivers/filt` returns 404.

Two workable options: (a) use `AsyncCallbackWebHandler` with a path prefix, or (b) change the API to `POST /api/drivers` with a `slot` form field. Option (b) is simpler and keeps the same spirit.

**Switch to option (b)**. In the handler above, replace the slot-extraction block:

```cpp
    String path = req->url();
    int slashAt = path.lastIndexOf('/');
    if (slashAt < 0 || (size_t)slashAt + 1 >= path.length()) {
      req->send(400, "text/plain", "missing slot in path"); return;
    }
    String slot = path.substring(slashAt + 1);
```

with:

```cpp
    if (!req->hasParam("slot", true)) {
      req->send(400, "text/plain", "missing slot"); return;
    }
    String slot = req->getParam("slot", true)->value();
```

Update the spec's section 16 note about API shape — we're now using form field `slot=filt` instead of path suffix. The frontend (Task 17) will send the slot as a form field.

- [ ] **Step 3: Verify compile**

```bash
pio run -e serial_upload 2>&1 | tail -5
```
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/Provisioning.cpp
git commit -m "sp4: POST /api/drivers with slot form field + server-side validation"
```

---

### Task 16: Phase 2 checkpoint — OTA + end-to-end MQTT driver test

**Files:** none (verification + OTA).

This is where we confirm the MQTT driver path works. No UI yet — we drive via `curl` + `mosquitto_sub` / `mosquitto_pub`.

- [ ] **Step 1: OTA firmware**

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
export POOLMASTER_OTA_PWD=""
curl -sS --max-time 5 -o /dev/null -w 'pre: HTTP %{http_code}\n' http://poolmaster.local/healthz
pio run -e OTA_upload -t upload 2>&1 | tail -3
sleep 18
curl -sS --max-time 5 -o /dev/null -w 'post: HTTP %{http_code}\n' http://poolmaster.local/healthz
```
Expected: SUCCESS + HTTP 200 both times.

- [ ] **Step 2: Confirm GET endpoint works**

```bash
curl -sS http://poolmaster.local/api/drivers | python3 -m json.tool | head -40
```
Expected: JSON array of 6 objects; each has `slot`, `kind:"gpio"`, `pin`, `active_level`, and the empty MQTT strings.

- [ ] **Step 3: Configure the `chl` slot to MQTT against a test topic (harmless — we're not actually connected to a chlorine pump)**

```bash
curl -sS -X POST \
  -d 'slot=chl&kind=mqtt&cmd_topic=pool/test/chl/command&payload_on=on&payload_off=off&state_topic=pool/test/chl/state&state_on=on&state_off=off' \
  http://poolmaster.local/api/drivers
# Expect: {"ok":true,"rebooting":true}
sleep 18
curl -sS --max-time 5 -o /dev/null -w 'post-save: HTTP %{http_code}\n' http://poolmaster.local/healthz
# Expect: 200
```

- [ ] **Step 4: Verify persistence**

```bash
curl -sS http://poolmaster.local/api/drivers | python3 -c '
import json, sys
cfgs = {c["slot"]: c for c in json.load(sys.stdin)}
print("chl:", cfgs["chl"])
'
# Expect: chl: {"slot": "chl", "kind": "mqtt", ... "cmd_topic": "pool/test/chl/command", ...}
```

- [ ] **Step 5: Drive the MQTT path end-to-end**

From another shell (broker:  replace `<broker>` and `<pw>` with the real values):

```bash
# Subscribe to the test topic so we see publishes from the device:
mosquitto_sub -h <broker> -u homeassistant -P '<pw>' -t 'pool/test/#' -v &
SUB_PID=$!

# Fire a ChlPump-ON command via the SP3 REST endpoint:
curl -sS -H 'Content-Type: application/json' -X POST -d '{"ChlPump":1}' http://poolmaster.local/api/cmd
# Expect: mosquitto_sub shows:
#   pool/test/chl/command on

# Simulate the Shelly's echo:
mosquitto_pub -h <broker> -u homeassistant -P '<pw>' -t 'pool/test/chl/state' -m 'on' -r

# Verify the pump's IsRunning catches up within 1-2s:
sleep 2
curl -sS http://poolmaster.local/api/state | python3 -c 'import json,sys;print("chl:",json.load(sys.stdin)["pumps"]["chl"])'
# Expect: chl: True

# Simulate external off:
mosquitto_pub -h <broker> -u homeassistant -P '<pw>' -t 'pool/test/chl/state' -m 'off' -r
sleep 2
curl -sS http://poolmaster.local/api/state | python3 -c 'import json,sys;print("chl:",json.load(sys.stdin)["pumps"]["chl"])'
# Expect: chl: False

kill $SUB_PID
```

- [ ] **Step 6: Roll back the `chl` slot to GPIO for safety (we don't want the test config to persist)**

```bash
curl -sS -X POST -d 'slot=chl&kind=gpio&pin=26&active_level=low&cmd_topic=&payload_on=on&payload_off=off&state_topic=&state_on=on&state_off=off' \
  http://poolmaster.local/api/drivers
# pin=26 is the CHL_PUMP default per include/Config.h:39.
sleep 18
curl -sS http://poolmaster.local/api/state | python3 -c 'import json,sys;print("chl:",json.load(sys.stdin)["pumps"]["chl"])'
# Expect: chl: False (back to GPIO, OFF)
```

- [ ] **Step 7: Create Phase 2 checkpoint branch**

```bash
git branch sp4-ckpt2-phase2
git branch --list 'sp4*'
```

---

## Phase 3 — Configuration UI

### Task 17: Add `/settings/drivers` route + SectionTabs entry

**Files:**
- Modify: `web/src/components/SectionTabs.tsx`
- Modify: `web/src/app.tsx`

- [ ] **Step 1: Extend the Settings tabs list**

In `web/src/components/SectionTabs.tsx`, find `export const TABS_SETTINGS = [...]` and change to:

```tsx
export const TABS_SETTINGS = [
  { to: '/settings',          label: 'Network' },
  { to: '/settings/drivers',  label: 'Drivers' },
  { to: '/settings/firmware', label: 'Firmware' },
];
```

- [ ] **Step 2: Register the route in `web/src/app.tsx`**

Near the existing `/settings/firmware` Route, add (preserve alignment):

```tsx
import { Drivers } from './screens/Drivers';
// …
          <Route path="/settings/drivers"     component={Drivers} />
```

And the existing `/settings/firmware` line becomes the last of the three settings routes — no change to it aside from being preceded by the new one.

- [ ] **Step 3: Verify TS still compiles (even though Drivers.tsx isn't written yet, leave this for Task 18 — we commit Tasks 17 and 18 together as one unit to keep the tree green)**

Don't run `npm run build` yet — wait for Task 18.

- [ ] **Step 4: Commit later — flagged as part of Task 18's commit**

(No commit here. Continue to Task 18.)

---

### Task 18: Implement `web/src/screens/Drivers.tsx`

**Files:**
- Create: `web/src/screens/Drivers.tsx`

- [ ] **Step 1: Create the screen file**

```tsx
import { useSignal, useComputed } from '@preact/signals';
import { useEffect } from 'preact/hooks';
import { apiGet, apiPostForm } from '../lib/api';
import { SectionTabs, TABS_SETTINGS } from '../components/SectionTabs';

interface DriverCfg {
  slot: string;
  kind: 'gpio' | 'mqtt';
  pin: number;
  active_level: 'low' | 'high';
  cmd_topic: string;
  payload_on: string;
  payload_off: string;
  state_topic: string;
  state_on: string;
  state_off: string;
}

const SLOT_LABEL: Record<string, string> = {
  filt:  'Filtration pump',
  ph:    'pH dosing pump',
  chl:   'Chlorine dosing pump',
  robot: 'Robot pump',
  r0:    'Relay R0 — Projecteur',
  r1:    'Relay R1 — Spare',
};

function toFormPayload(c: DriverCfg): Record<string, string> {
  return {
    slot:         c.slot,
    kind:         c.kind,
    pin:          String(c.pin),
    active_level: c.active_level,
    cmd_topic:    c.cmd_topic,
    payload_on:   c.payload_on,
    payload_off:  c.payload_off,
    state_topic:  c.state_topic,
    state_on:     c.state_on,
    state_off:    c.state_off,
  };
}

export function Drivers() {
  const configs = useSignal<DriverCfg[] | null>(null);
  const saving  = useSignal<string | null>(null);

  useEffect(() => {
    let alive = true;
    (async () => {
      const res = await apiGet<DriverCfg[]>('/api/drivers');
      if (alive && res.ok && res.data) configs.value = res.data;
    })();
    return () => { alive = false; };
  }, []);

  if (!configs.value) return (
    <div class="space-y-4 max-w-3xl">
      <SectionTabs current="/settings/drivers" tabs={TABS_SETTINGS} />
      <div class="glass p-6">Loading drivers…</div>
    </div>
  );

  const update = (slot: string, patch: Partial<DriverCfg>) => {
    configs.value = (configs.value ?? []).map(c => c.slot === slot ? { ...c, ...patch } : c);
  };

  const save = async (cfg: DriverCfg) => {
    if (cfg.kind === 'mqtt' && !cfg.cmd_topic) {
      alert('MQTT kind requires a command topic'); return;
    }
    if (!cfg.payload_on || !cfg.payload_off) {
      alert('Payloads may not be empty'); return;
    }
    saving.value = cfg.slot;
    const res = await apiPostForm('/api/drivers', toFormPayload(cfg));
    saving.value = null;
    alert(res.ok ? 'Saved — device rebooting. Reconnect in ~15s.' : `Failed: ${res.error}`);
  };

  return (
    <div class="space-y-4 max-w-3xl">
      <SectionTabs current="/settings/drivers" tabs={TABS_SETTINGS} />
      <h1 class="text-xl font-bold">Output drivers</h1>
      <p class="text-xs opacity-70">
        Each physical output can be driven by a local GPIO pin or via MQTT to an external relay
        (Shelly, Tasmota, etc). Defaults match the original wiring — change only if your hardware
        setup is different.
      </p>

      {configs.value.map(cfg => (
        <div key={cfg.slot} class="glass p-5 space-y-3">
          <div class="flex items-center justify-between">
            <h2 class="font-semibold">
              {SLOT_LABEL[cfg.slot] ?? cfg.slot}
              <span class={`ml-2 inline-block text-[0.7rem] px-2 py-0.5 rounded-full border
                ${cfg.kind === 'gpio' ? 'bg-aqua-ok/15 text-emerald-300 border-emerald-500/30'
                                      : 'bg-aqua-info/15 text-cyan-300 border-cyan-500/30'}`}>
                {cfg.kind}
              </span>
            </h2>
          </div>

          <div class="flex gap-2">
            {(['gpio', 'mqtt'] as const).map(k => (
              <button key={k}
                class={`text-xs px-3 py-1.5 rounded-md border ${
                  cfg.kind === k
                    ? 'bg-aqua-primary/25 border-aqua-primary/50 text-cyan-100'
                    : 'bg-white/5 border-aqua-border text-aqua-text/70'
                }`}
                onClick={() => update(cfg.slot, { kind: k })}>
                {k === 'gpio' ? 'GPIO' : 'MQTT'}
              </button>
            ))}
          </div>

          {cfg.kind === 'gpio' ? (
            <div class="grid grid-cols-2 gap-3">
              <label class="block">
                <div class="label-caps mb-1">GPIO pin (0–39)</div>
                <input type="number" min={0} max={39} value={cfg.pin}
                       onInput={e => update(cfg.slot, { pin: Number((e.target as HTMLInputElement).value) })}
                       class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
              </label>
              <label class="block">
                <div class="label-caps mb-1">Active level</div>
                <div class="flex gap-2">
                  {(['low', 'high'] as const).map(al => (
                    <button key={al}
                      class={`text-xs px-3 py-1.5 rounded-md border ${
                        cfg.active_level === al
                          ? 'bg-aqua-primary/25 border-aqua-primary/50 text-cyan-100'
                          : 'bg-white/5 border-aqua-border text-aqua-text/70'
                      }`}
                      onClick={() => update(cfg.slot, { active_level: al })}>
                      {al === 'low' ? 'Active low (default)' : 'Active high'}
                    </button>
                  ))}
                </div>
              </label>
            </div>
          ) : (
            <div class="grid grid-cols-2 gap-3">
              <label class="block col-span-2">
                <div class="label-caps mb-1">Command topic (required)</div>
                <input type="text" value={cfg.cmd_topic}
                       placeholder="shellies/garden-filter/relay/0/command"
                       onInput={e => update(cfg.slot, { cmd_topic: (e.target as HTMLInputElement).value })}
                       class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
              </label>
              <label class="block">
                <div class="label-caps mb-1">Payload for ON</div>
                <input type="text" value={cfg.payload_on} placeholder="on"
                       onInput={e => update(cfg.slot, { payload_on: (e.target as HTMLInputElement).value })}
                       class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
              </label>
              <label class="block">
                <div class="label-caps mb-1">Payload for OFF</div>
                <input type="text" value={cfg.payload_off} placeholder="off"
                       onInput={e => update(cfg.slot, { payload_off: (e.target as HTMLInputElement).value })}
                       class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
              </label>
              <label class="block col-span-2">
                <div class="label-caps mb-1">
                  State topic (optional — empty = fire-and-forget, Pump will report "not running")
                </div>
                <input type="text" value={cfg.state_topic}
                       placeholder="shellies/garden-filter/relay/0"
                       onInput={e => update(cfg.slot, { state_topic: (e.target as HTMLInputElement).value })}
                       class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
              </label>
              {cfg.state_topic && (
                <>
                  <label class="block">
                    <div class="label-caps mb-1">State-on payload</div>
                    <input type="text" value={cfg.state_on} placeholder="on"
                           onInput={e => update(cfg.slot, { state_on: (e.target as HTMLInputElement).value })}
                           class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
                  </label>
                  <label class="block">
                    <div class="label-caps mb-1">State-off payload</div>
                    <input type="text" value={cfg.state_off} placeholder="off"
                           onInput={e => update(cfg.slot, { state_off: (e.target as HTMLInputElement).value })}
                           class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
                  </label>
                </>
              )}
            </div>
          )}

          <button
            class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold disabled:opacity-50"
            disabled={saving.value === cfg.slot}
            onClick={() => save(cfg)}>
            {saving.value === cfg.slot ? 'Saving…' : 'Save + reboot'}
          </button>
        </div>
      ))}
    </div>
  );
}
```

- [ ] **Step 2: Build the web bundle**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster/web
npm run build 2>&1 | tail -3
```
Expected: `[bundle-size] OK: <N> KB of JS ...`. The new Drivers screen adds roughly 4-6 KB, well within budget.

- [ ] **Step 3: Commit BOTH Task 17 and Task 18 together**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
git add web/src/components/SectionTabs.tsx web/src/app.tsx web/src/screens/Drivers.tsx
git commit -m "sp4: Settings → Drivers UI with per-slot GPIO/MQTT configuration

Six cards, one per device slot. GPIO mode: pin number + active-level.
MQTT mode: cmd topic + payload-on/off + optional state topic + state-on/off
match strings. Save reboots the device. Prefills from GET /api/drivers on
mount. Tab lives between Network and Firmware in the Settings sub-nav."
```

---

### Task 19: Phase 3 checkpoint — OTA filesystem + real-hardware Shelly test

**Files:** none (verification + OTA).

- [ ] **Step 1: OTA the filesystem bundle**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
export PATH="$HOME/.platformio/penv/bin:$PATH"
export POOLMASTER_OTA_PWD=""
pio run -e OTA_upload -t uploadfs 2>&1 | tail -3
sleep 8
curl -sS --max-time 5 -o /dev/null -w 'HTTP %{http_code}\n' http://poolmaster.local/healthz
```
Expected: SUCCESS + HTTP 200.

- [ ] **Step 2: In the browser, hard-refresh and navigate to Settings → Drivers**

Open `http://poolmaster.local/settings/drivers`. Expected: six cards render with the current configs. Default state: all six show `gpio` badges with their default pins.

- [ ] **Step 3: Bind your real Filtration pump to the real Shelly**

In the Filtration pump card:
- Click MQTT.
- Command topic: the real Shelly's command topic (e.g. `shellies/garden-filter/relay/0/command`).
- Payload on / off: leave as `on` / `off` (Shelly's defaults).
- State topic: the real Shelly's state topic (e.g. `shellies/garden-filter/relay/0`).
- State on / off: `on` / `off`.
- Click **Save + reboot**.

Wait ~15 s for reconnect.

- [ ] **Step 4: Verify on the device**

```bash
curl -sS http://poolmaster.local/api/drivers | python3 -m json.tool | python3 -c '
import json, sys
text = sys.stdin.read()
# Find the filt block:
import re
m = re.search(r"\"slot\":\"filt\".*?\}", text)
print(m.group(0) if m else "NOT FOUND")
'
```

Expected: `{"slot":"filt","kind":"mqtt","pin":...,"cmd_topic":"shellies/garden-filter/relay/0/command","state_topic":"shellies/garden-filter/relay/0",…}`.

- [ ] **Step 5: Toggle Filtration from the SP3 Dashboard + verify physical action**

Open `http://poolmaster.local/control` (Manual control). Toggle Filtration pump ON. Expected:
- The Shelly audibly clicks within 1 second.
- Within 2 s, the UI toggle slides to ON.
- Dashboard tile shows "Filtration · running · 0s" (uptime starts advancing).
- `curl http://poolmaster.local/api/state` shows `"filtration": true`.

Toggle OFF. Expected: Shelly clicks off, UI slides off, state confirms.

- [ ] **Step 6: Verify interlock still works**

With Filtration OFF, try to start the pH pump:
```bash
curl -sS -H 'Content-Type: application/json' -X POST -d '{"PhPump":1}' http://poolmaster.local/api/cmd
sleep 2
curl -sS http://poolmaster.local/api/state | python3 -c 'import json,sys;print("ph:",json.load(sys.stdin)["pumps"]["ph"])'
```
Expected: `ph: False`. PhPump refused to start because `FiltrationPump.IsRunning() == false`. This confirms Task 8's interlock rewiring works for MQTT-bound Filtration.

Now turn Filtration ON (via Shelly), wait for the state ack, then retry:
```bash
curl -sS -H 'Content-Type: application/json' -X POST -d '{"FiltPump":1}' http://poolmaster.local/api/cmd
sleep 3   # Shelly click + state echo
curl -sS -H 'Content-Type: application/json' -X POST -d '{"PhPump":1}' http://poolmaster.local/api/cmd
sleep 2
curl -sS http://poolmaster.local/api/state | python3 -c 'import json,sys;print("ph:",json.load(sys.stdin)["pumps"]["ph"])'
```
Expected: `ph: True`. pH pump started because Filtration is now running (interlock OK).

Clean up:
```bash
curl -sS -H 'Content-Type: application/json' -X POST -d '{"PhPump":0}' http://poolmaster.local/api/cmd
curl -sS -H 'Content-Type: application/json' -X POST -d '{"FiltPump":0}' http://poolmaster.local/api/cmd
```

- [ ] **Step 7: Verify HA sees the same state**

Open HA → Devices & services → MQTT → PoolMaster → Filtration pump. Toggling from the Shelly's own button should now flip the HA switch too. Toggling from HA should flip the Shelly.

- [ ] **Step 8: Create Phase 3 checkpoint branch**

```bash
git branch sp4-ckpt3-phase3
git branch --list 'sp4*'
```

---

## Phase 4 — Finishing touches

### Task 20: Record final size + update README

**Files:**
- Modify: `README.md`
- Create: `docs/superpowers/notes/sp4-final-size.txt`

- [ ] **Step 1: Capture final sizes**

```bash
cd /Users/sebastiankuprat/Documents/GitHub/ESP32-PoolMaster
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e serial_upload 2>&1 | grep -E 'RAM:|Flash:' > docs/superpowers/notes/sp4-final-size.txt
cat docs/superpowers/notes/sp4-final-size.txt
```

Expected: two lines. Compare against `docs/superpowers/notes/sp4-baseline-size.txt` — typically +1-2% Flash for SP4 additions.

- [ ] **Step 2: Prepend an SP4 section to `README.md`**

Find the existing `## 🧊 SP3 Beautiful Web UI` heading (near the top of README). Immediately before it, insert:

```markdown
## 🔌 SP4 Pluggable Output Drivers (2026-04)

Each of the six physical-output devices (`FiltrationPump`, `PhPump`, `ChlPump`, `RobotPump`,
relays `R0`, `R1`) can be rebound at runtime to either a local GPIO pin (current behaviour,
still the default) or an external MQTT relay (Shelly-compatible). PID, uptime, filtration
schedule, anti-freeze, emergency-stop, and interlock logic all continue to operate correctly
regardless of which driver each device is using — the abstraction is invisible above the driver.

### Configure

Open `http://poolmaster.local/settings/drivers`. For each device, pick **GPIO** (with pin
number + active-level) or **MQTT** (command topic, payload_on / payload_off, optional state
topic for authoritative feedback from the external device). Save + reboot.

Example — binding the filter pump to a Shelly:

| Field | Value |
| ----- | ----- |
| Kind | MQTT |
| Command topic | `shellies/garden-filter/relay/0/command` |
| Payload on / off | `on` / `off` |
| State topic | `shellies/garden-filter/relay/0` |
| State on / off | `on` / `off` |

With state feedback, the firmware sees external toggles of the Shelly (its button, HA,
another automation) within ~200 ms and updates PID / HA / dashboard state to match.

See [docs/superpowers/specs/2026-04-24-sp4-pluggable-output-drivers-design.md](docs/superpowers/specs/2026-04-24-sp4-pluggable-output-drivers-design.md)
for the full design.

---

```

- [ ] **Step 3: Commit**

```bash
git add README.md docs/superpowers/notes/sp4-final-size.txt
git commit -m "sp4: document pluggable output drivers in README + record final size"
```

- [ ] **Step 4: Create the ship-ready checkpoint branch**

```bash
git branch sp4-ckpt4-ship
git branch --list 'sp4*'
```

Expected list:
- `sp4-ckpt1-phase1`
- `sp4-ckpt2-phase2`
- `sp4-ckpt3-phase3`
- `sp4-ckpt4-ship`
- `sp4-pluggable-drivers` (current, `*`)

---

## Self-review notes

**Spec coverage.** Every numbered section of the design spec maps to at least one task:

- §4 Architecture → Tasks 2 (interface), 3 (GPIO), 4 (MQTT), 6 (registry)
- §5 OutputDriver interface → Task 2
- §6 GpioOutputDriver → Task 3
- §7 MqttOutputDriver → Tasks 4 (publish) + 12 (state routing)
- §8 Pump class integration → Task 7
- §8.1 Interlock rewiring → Task 8 (Step 3)
- §9 Relay integration → Tasks 9 + 10
- §10 Configuration storage → Task 5
- §11 Runtime init ordering → Task 8
- §12 State feedback lifecycle → Tasks 12 + 13
- §13 HA integration — no code change, validated in Task 19 Step 7
- §14 Error handling → Tasks 4 (publish failure log) + 15 (server-side validation)
- §15 Configuration UI → Tasks 17 + 18
- §16 Backend endpoints → Tasks 14 + 15
- §17 Migration + defaults → Task 5 (`slotDefault`) + Task 11 (smoke-test zero-regression)
- §18 Testing plan → Tasks 11, 16, 19 (the three checkpoint tasks)

**Placeholder scan.** No "TBD", "TODO", "implement later", or "similar to earlier task" references; every code step contains the actual code. The one "adjust field name per grep" note in Task 7 Step 6 is a concrete fallback instruction, not a placeholder.

**Type consistency.** The three new methods on `Pump` are named `setDriver`, `setInterlockSource`, `syncStateFromDriver` everywhere they appear. `OutputDriver`'s four virtual methods (`begin`, `set`, `get`, `kindName`) are used consistently. Slot ids (`filt`, `ph`, `chl`, `robot`, `r0`, `r1`) are the same string literals in `Credentials::drivers::load`, `Drivers.cpp::SLOTS[]`, `Provisioning.cpp` slot validation, and `Drivers.tsx::SLOT_LABEL`.

**Scope.** One feature, four phases, four checkpoints. Fits a single plan; no decomposition needed.

**Known follow-ups (explicitly out of plan scope):**
1. HTTP-URL driver.
2. Net-new user-defined controls (pool lights, salt chlorinator, heat pump).
3. State-change rate limiting (Shelly flapping protection).
4. Per-driver telemetry (publish count, failure count) on Diagnostics.
5. Collision prevention when two slots use the same GPIO pin (currently warned, not blocked).
