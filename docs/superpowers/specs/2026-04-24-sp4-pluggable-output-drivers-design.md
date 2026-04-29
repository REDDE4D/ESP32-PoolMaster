# SP4 Pluggable Output Drivers Design

**Status:** design approved, ready for plan
**Scope:** firmware-side refactor of the six physical-output devices + small Settings UI + config persistence
**Predecessors:** SP1 (Modern Plumbing — adds MqttBridge, Credentials, Provisioning), SP3 (Beautiful Web UI — adds WebSocketHub, Settings screen, LogBuffer)
**Branch target:** new branch `sp4-pluggable-drivers` off `sp3-web-ui` once all outstanding SP3 hotfixes have stabilised

---

## 1. Goal

Let the user rebind each of the six existing physical-output devices — `FiltrationPump`, `PhPump`, `ChlPump`, `RobotPump`, relay `R0`, relay `R1` — to either a local GPIO pin (current behaviour, still the default) or an external MQTT-controlled relay (Shelly-compatible, or any generic "publish on/off payload + subscribe to state" device).

The user already has their filter pump wired to a Shelly, so the ESP32 is currently flipping a GPIO that is not physically connected to anything useful. Rebinding that device to MQTT lets the firmware command the Shelly directly, while keeping all the pool-control logic that's coupled to `FiltrationPump` (PID loops, uptime, filtration schedule, anti-freeze, emergency stop, tank tracking).

## 2. Non-goals

- No new user-defined switches beyond the six existing devices — deferred to a possible SP5.
- No HTTP-URL driver. The generic MQTT driver covers >90 % of use cases via Home Assistant automations that bridge MQTT to HTTP.
- No compound / multi-step / sequenced controls.
- No change to the existing legacy JSON command shapes (`{"FiltPump":1}`, `{"Relay":[0,1]}`, etc.) — these keep working.
- No change to PID / uptime / schedule / anti-freeze / emergency-stop logic — all continues to operate on the same `Pump` objects.
- No change to Home Assistant discovery topics or entity shapes — HA still sees the same six switches.
- No runtime driver swap. A driver change requires a reboot (same pattern as WiFi / MQTT credential saves).
- No collision-prevention logic against assigning two slots to the same GPIO pin — warned on save, not blocked.

## 3. Success criteria

1. A user configures Filtration pump driver → MQTT with command topic `shellies/garden-filter/relay/0/command`, payloads `on` / `off`, state topic `shellies/garden-filter/relay/0`. Saves. Device reboots.
2. After reboot, toggling Filtration in the SP3 UI publishes `on` to that command topic.
3. The Shelly flips its physical relay and publishes `on` on the state topic.
4. The firmware sees the state ack and updates `FiltrationPump.IsRunning()` accordingly — which causes PID loops, filtration schedule, and the HA `filtration_pump/state` broadcast to reflect the real state.
5. If the Shelly is toggled externally (its own button, HA automation, etc.), the firmware sees the state change within ~200 ms.
6. Every existing behaviour still works when a driver is left at GPIO — zero regression. This is validated by first flashing with no NVS config (defaults = GPIO everywhere) and running a full smoke pass.

## 4. Architecture overview

```
┌─────────────────────────────────────────────────────────────────┐
│                       SP3 web UI + legacy JSON commands          │
│             ({"FiltPump":1}, {"Relay":[0,1]}, …)                 │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│    ProcessCommand / CommandQueue.cpp (unchanged command names)   │
└─────────────────────────────────────────────────────────────────┘
                              │
                  ┌───────────┴───────────┐
                  ▼                       ▼
┌─────────────────────────┐   ┌──────────────────────────┐
│   Pump::Start/Stop       │   │ relay_drivers[0]->set() │
│   (FiltPump / PhPump /   │   │ relay_drivers[1]->set() │
│    ChlPump / RobotPump)  │   │   (R0 / R1)              │
└─────────────────────────┘   └──────────────────────────┘
            │                               │
            └──────────────┬────────────────┘
                           ▼
              ┌─────────────────────────┐
              │   OutputDriver*         │  ◄── one per device, pinned at boot
              │   (virtual interface)   │
              └─────────────────────────┘
                  │                    │
                  ▼                    ▼
      ┌──────────────────┐   ┌─────────────────────────┐
      │ GpioOutputDriver │   │ MqttOutputDriver         │
      │  ─ digitalWrite  │   │  ─ publish cmdTopic      │
      │  ─ cached state  │   │  ─ subscribe stateTopic  │
      │                  │   │  ─ cache state from sub  │
      └──────────────────┘   └─────────────────────────┘
```

Six `OutputDriver*` pointers live in module-scope static storage for the lifetime of the program. On every successful MQTT broker connect, any MqttOutputDrivers re-subscribe to their state topics. The set of active subscriptions is dispatched from `MqttBridge::onMqttMessage` before the existing HA set-topic handler, and drivers cache state updates into internal `bool _state` members that `Pump` reads via `driver->get()` when it wants authoritative feedback.

## 5. `OutputDriver` interface

New header `include/OutputDriver.h`:

```cpp
class OutputDriver {
 public:
  virtual ~OutputDriver() = default;
  virtual void begin() = 0;                   // called once at boot
  virtual void set(bool on) = 0;              // command the output
  virtual bool get() const = 0;               // last-known authoritative state
  virtual const char* kindName() const = 0;   // "gpio" | "mqtt"
};
```

Lifetime: every driver lives in module-scope static storage inside `Drivers.cpp` (new module). No heap allocations. No ownership handoff.

## 6. `GpioOutputDriver`

New file `src/GpioOutputDriver.cpp` + declaration in the `OutputDriver.h` header (or a sibling header).

```cpp
class GpioOutputDriver : public OutputDriver {
 public:
  GpioOutputDriver(uint8_t pin, bool activeLow);
  void begin() override;            // pinMode(OUTPUT); write OFF initial
  void set(bool on) override;       // digitalWrite(_pin, _activeLow ? !on : on)
  bool get() const override;        // return _state
  const char* kindName() const override { return "gpio"; }
 private:
  uint8_t _pin;
  bool _activeLow;
  bool _state;
};
```

Semantics match today's behaviour exactly:
- `FILTRATION_PUMP`, `PH_PUMP`, `CHL_PUMP`, `ROBOT_PUMP` are active-low today → `activeLow = true` in defaults.
- `RELAY_R0`, `RELAY_R1` are active-low today → `activeLow = true` in defaults.

`begin()` calls `pinMode(_pin, OUTPUT)` then writes the OFF level. This mirrors the current bring-up in `Setup.cpp`.

## 7. `MqttOutputDriver`

```cpp
struct MqttDriverConfig {
  String cmdTopic, payloadOn, payloadOff;
  String stateTopic, stateOn, stateOff;   // stateTopic empty = fire-and-forget
};

class MqttOutputDriver : public OutputDriver {
 public:
  MqttOutputDriver(const MqttDriverConfig& cfg);
  void begin() override;              // register for state-topic dispatch
  void set(bool on) override;         // publish cmdTopic + payload; no optimistic state update
  bool get() const override;          // return _state (only changes on state-topic echo)
  const char* kindName() const override { return "mqtt"; }
  // Called by MqttBridge message dispatch when stateTopic matches:
  void onStateMessage(const String& payload);
 private:
  MqttDriverConfig _cfg;
  bool _state;
};
```

**`set(bool on)`:** publishes via the global `mqttClient.publish(_cfg.cmdTopic.c_str(), 1, false, on ? _cfg.payloadOn.c_str() : _cfg.payloadOff.c_str())`. QoS 1, retain=false. On `publish` returning 0 (queue full or disconnected), log a warning via `LogBuffer::append(L_WARN, "[drv] %s publish failed", _cfg.cmdTopic.c_str())` and return. QoS 1 means the library will retry on reconnect — no local retry logic needed.

**State feedback:** Never updates `_state` from a write. Only `onStateMessage` updates it, after matching `payload == _cfg.stateOn` → `_state = true`, else `_cfg.stateOff` → `_state = false`, else leave unchanged (unknown payload, log at DEBUG).

**If `stateTopic` is empty (fire-and-forget):** `_state` stays at the initial `false` forever. `Pump::IsRunning()` will perpetually report "not running" for this device. That's fine for GPIO-style Shellys where state feedback is available; document "always configure a state topic when available" in the Settings UI helper text.

## 8. Integration with the `Pump` class

`lib/Pump-master/src/Pump.h` — non-breaking additive change.

**Constraint: the four pumps are file-scope globals in `Setup.cpp`** (lines 98–101):

```cpp
Pump FiltrationPump(FILTRATION_PUMP, FILTRATION_PUMP);
Pump PhPump(PH_PUMP, PH_PUMP, NO_LEVEL, FILTRATION_PUMP, …);
Pump ChlPump(CHL_PUMP, CHL_PUMP, NO_LEVEL, FILTRATION_PUMP, …);
Pump RobotPump(ROBOT_PUMP, ROBOT_PUMP, NO_TANK, FILTRATION_PUMP);
```

Their constructors run before `main()`, so `Drivers::beginAll()` (which reads NVS) cannot be called in time to feed driver pointers into the constructors. Approach: keep the existing pin-based constructor (which continues to build a default internal `GpioOutputDriver`), and add a **post-construction `setDriver(OutputDriver*)`** call used by `setup()` after `Drivers::beginAll()` has built the six drivers from NVS config.

```cpp
class Pump {
 public:
  // Existing constructor — builds an internal GpioOutputDriver from pumppin.
  Pump(uint8_t pumppin, uint8_t relaypin, uint8_t tankpin = NO_TANK,
       uint8_t interlockpin = NO_INTERLOCK, double flowrate = 0.,
       double tankvolume = 0., double tankfill = 100.);

  // New — replace the internal driver at runtime. Called once from setup()
  // after Drivers::beginAll() has built the configured drivers.
  void setDriver(OutputDriver* driver);

  // New — if the driver is MQTT-kind with state feedback, pull its cached
  // state into the internal running flag. No-op for GPIO drivers.
  // Called once per second by the WsBroadcast task.
  void syncStateFromDriver();

  // New — interlock override. If set, Interlock() returns src->IsRunning()
  // instead of digitalRead(interlockPin). Used by setup() to rewire
  // PhPump / ChlPump interlock from the hardware pin onto FiltrationPump
  // directly — see section 8.1.
  void setInterlockSource(const Pump* src);

  // Unchanged: Start / Stop / IsRunning / UpTime / TankLevel / …
  // Start() now internally calls _driver->set(true); Stop() calls set(false).
 private:
  OutputDriver* _driver;           // never null; internal default GpioOutputDriver
  const Pump* _interlockSrc;       // optional override for Interlock()
  // …existing fields…
};
```

`Pump::Start()` / `Pump::Stop()` replace their `digitalWrite(pumppin, ...)` calls with `_driver->set(true)` / `_driver->set(false)`. Uptime tracking, tank tracking, and the rest of the class stay as they are.

`Pump::IsRunning()` continues to return the internal running flag. For MQTT drivers that is kept fresh by `syncStateFromDriver()`; for GPIO drivers it matches the last `Start()`/`Stop()` call as today.

### 8.1 Interlock rewiring — critical for safety

`PhPump`, `ChlPump`, and `RobotPump` are constructed with `interlockpin = FILTRATION_PUMP`. Today, `Pump::Interlock()` decides whether the dosing pump may run by calling `digitalRead(interlockPin)` — i.e. it reads the filtration pump's GPIO. If the user rebinds `FiltrationPump` to an MQTT driver and `Drivers::beginAll()` then releases GPIO `FILTRATION_PUMP` to `INPUT` (see section 11 — pin-release safety), the dosing pumps' `digitalRead(FILTRATION_PUMP)` returns a floating value and **the interlock becomes meaningless — dosing could run while filtration is off, wasting chemicals and risking probe damage**.

Fix: add `Pump::setInterlockSource(const Pump* src)`. When set, `Interlock()` returns `src->IsRunning()` (the authoritative running state — which for an MQTT-bound FiltrationPump is driven by state-topic echoes from the Shelly). When unset, falls back to the current `digitalRead(interlockPin)` behaviour.

In `setup()`, after `Drivers::beginAll()` and the per-pump `setDriver()` calls:

```cpp
PhPump.setInterlockSource(&FiltrationPump);
ChlPump.setInterlockSource(&FiltrationPump);
RobotPump.setInterlockSource(&FiltrationPump);
```

This makes the interlock correct regardless of which driver `FiltrationPump` is using. It also means the interlock keeps working properly for the GPIO case (since `FiltrationPump.IsRunning()` already reflects the last `Start()` / `Stop()` call).

**Test before shipping:** confirm that `PhPump.Interlock()` returns "NOK" (blocked) when `FiltrationPump.IsRunning()` is false, and "OK" when it's true — with FiltrationPump bound to both GPIO and MQTT drivers.

## 9. Integration with R0 / R1 relays

Today the two relays are driven by direct `digitalWrite(RELAY_R0/R1, ...)` calls in `src/CommandQueue.cpp` (the `"Relay"` JSON command handler) and read by `src/WebSocketHub.cpp::buildStateJson` via `!digitalRead(RELAY_R0/R1)` for the `relays.r0/r1` state.

Replace both with a module-level `OutputDriver* relay_drivers[2]` built at boot:

```cpp
// In CommandQueue.cpp (Relay handler):
case 0:
  relay_drivers[0]->set((bool)command[F("Relay")][1]);
  break;
case 1:
  relay_drivers[1]->set((bool)command[F("Relay")][1]);
  break;

// In WebSocketHub.cpp (buildStateJson):
relays["r0"] = relay_drivers[0]->get();
relays["r1"] = relay_drivers[1]->get();
```

And `MqttPublish.cpp` swaps its `!digitalRead(RELAY_R0)` reads the same way.

## 10. Configuration storage

NVS namespace `"drivers"` — kept separate from `"PoolMaster"` so that a driver-only save doesn't cause a full config rewrite on the busy main namespace.

Per-slot keys, prefixed with a short slot id (`filt`, `ph`, `chl`, `robot`, `r0`, `r1`). Keys are at most 15 characters to fit NVS's key-length cap.

| NVS key | Type   | Meaning                                                           |
| ---     | ---    | ---                                                               |
| `<slot>.kind` | u8  | 0 = gpio (default), 1 = mqtt                                      |
| `<slot>.pin`  | u8  | GPIO pin number (GPIO driver only)                                |
| `<slot>.al`   | u8  | active-level: 0 = active-low (matches current), 1 = active-high   |
| `<slot>.ct`   | str | MQTT command topic                                                |
| `<slot>.pn`   | str | MQTT payload-on, default `"on"`                                   |
| `<slot>.pf`   | str | MQTT payload-off, default `"off"`                                 |
| `<slot>.st`   | str | MQTT state topic (optional; empty = fire-and-forget)              |
| `<slot>.sn`   | str | MQTT state-payload-on match, default `"on"`                       |
| `<slot>.sf`   | str | MQTT state-payload-off match, default `"off"`                     |

Per-slot cost at full MQTT usage ≈ 300 bytes; six slots ≈ 2 KB worst case.

New accessor in `Credentials.cpp/h`:

```cpp
namespace Credentials::drivers {
  struct DriverConfig {
    uint8_t kind = 0;          // 0 gpio, 1 mqtt
    uint8_t pin = 0xFF;        // gpio
    uint8_t activeLevel = 0;   // gpio: 0 low, 1 high
    String cmdTopic, payloadOn = "on", payloadOff = "off";
    String stateTopic, stateOn = "on", stateOff = "off";
  };

  DriverConfig load(const char* slot);   // falls back to hardcoded per-slot defaults
  bool save(const char* slot, const DriverConfig& cfg);
}
```

Hardcoded per-slot defaults (preserved from `Config.h`):

| Slot     | kind  | pin              | activeLevel |
| ---      | ---   | ---              | ---         |
| `filt`   | gpio  | FILTRATION_PUMP  | low         |
| `ph`     | gpio  | PH_PUMP          | low         |
| `chl`    | gpio  | CHL_PUMP         | low         |
| `robot`  | gpio  | ROBOT_PUMP       | low         |
| `r0`     | gpio  | RELAY_R0         | low         |
| `r1`     | gpio  | RELAY_R1         | low         |

## 11. Runtime initialisation

New module `src/Drivers.cpp` + `include/Drivers.h`:

```cpp
namespace Drivers {
  void beginAll();                       // read NVS, build six drivers, call each begin()
  OutputDriver* get(const char* slot);   // lookup by slot id
  void resubscribeStateTopics();         // called from MqttBridge::onMqttConnect
  bool tryRouteIncoming(const char* topic, const String& payload);  // called from onMqttMessage
}
```

`beginAll()` replaces today's `pinMode(RELAY_R*, OUTPUT); digitalWrite(RELAY_R*, HIGH)` block in `Setup.cpp` and the per-pump pin setup. The pumps themselves remain file-scope globals (see section 8) — we swap their drivers after `beginAll()`. Exact ordering inside `setup()`:

1. `Credentials::loadAll()`                   (existing)
2. `Drivers::beginAll()`                      **(new)** — reads NVS, builds six `OutputDriver*`, calls `begin()` on each
3. `FiltrationPump.setDriver(Drivers::get("filt"))` **(new)**  — plus the same for ph/chl/robot
4. `PhPump.setInterlockSource(&FiltrationPump)` **(new)** — see §8.1, also for ChlPump + RobotPump
5. `HistoryBuffer::begin(); LogBuffer::begin();` (existing SP3)
6. `WebServerInit(); WebSocketHub::begin();`   (existing)
7. `mqttInit();`                              (existing)
8. After broker connect → `Drivers::resubscribeStateTopics()` from inside `onMqttConnect`.

**Pin-release safety:** in `beginAll()`, for any slot whose `kind == 1` (MQTT) AND whose saved `.pin` is a valid GPIO (0..39), do a `pinMode(old_pin, INPUT)` once at init. Prevents a previously-active GPIO from continuing to drive a floating output after the user rebinds the slot to MQTT.

## 12. State feedback subscription lifecycle

**On `MqttBridge::onMqttConnect`**, after `HaDiscovery::publishAll()`:

```cpp
Drivers::resubscribeStateTopics();
```

Which iterates the six drivers, and for each MQTT driver with a non-empty state topic:

```cpp
mqttClient.subscribe(stateTopic.c_str(), 1);
```

Shelly-style devices publish their state topic with retain=true, so we receive the current state immediately on subscribe — the driver's cached state converges to truth within ~200 ms of reconnect.

**On `MqttBridge::onMqttMessage`**, add one line at the top of the existing handler:

```cpp
if (Drivers::tryRouteIncoming(topic, payloadStr)) return;
// …existing HA set-topic handling below…
```

`Drivers::tryRouteIncoming` does a linear scan (≤6 entries) of `{stateTopic → driver}`. If it matches, calls `driver->onStateMessage(payload)` and returns true. Otherwise returns false and the existing HA set-topic handler processes the message.

State-topic and set-topic namespaces are disjoint by design (user's state topics are typically `shellies/<id>/relay/0`, set topics are our own `poolmaster/<mac>/<slot>/set`), so no double-handling risk.

## 13. Home Assistant integration — unchanged

HA still sees the same six switches via `HaDiscovery`. HA still commands via `poolmaster/<mac>/<slot>/set`. The driver indirection is invisible.

End-to-end flow when the user presses Filtration-ON from HA:

1. HA publishes `ON` to `poolmaster/<mac>/filtration_pump/set`.
2. `MqttBridge::onMqttMessage` — `Drivers::tryRouteIncoming` returns false (not a driver state topic) — existing HA set-topic handler routes to `enqueueCommand("{\"FiltPump\":1}")`.
3. `ProcessCommand` picks up the command, calls `FiltrationPump.Start()`.
4. `Pump::Start()` calls `_driver->set(true)`.
5. If the driver is MQTT, it publishes `on` to `shellies/garden-filter/relay/0/command`.
6. Shelly flips its relay, echoes `on` to `shellies/garden-filter/relay/0`.
7. `Drivers::tryRouteIncoming` matches — `driver->onStateMessage("on")` — driver's `_state = true`.
8. Within 1 s, `WsBroadcast`'s per-tick `Pump::syncStateFromDriver()` updates `FiltrationPump.IsRunning()` to true.
9. Next state broadcast goes to HA via `poolmaster/<mac>/filtration_pump/state: ON`, to the SP3 dashboard via WS, to the history buffer, etc.

No loops — each direction uses distinct topics, and the firmware never self-commands as a side-effect of its own echo.

## 14. Error handling

**MQTT broker disconnected + user toggles a pump.** `MqttOutputDriver::set` calls `mqttClient.publish` which returns 0 (failure). Driver logs a warning to `LogBuffer` but does not cache any optimistic local state. QoS 1 means the espMqttClient library re-queues the message and flushes it when the broker reconnects, so the command is not lost for a transient disconnect. For a long disconnect, the user sees the UI not flipping (because no state echo arrives) — that is the correct safety behaviour, and the Diagnostics → MQTT card already tells them why.

**Garbage state topic payload.** Driver's `onStateMessage` ignores payloads not matching `stateOn` or `stateOff`, logs at DEBUG. `_state` is unchanged.

**User sets two slots to the same GPIO pin.** The Settings UI shows a warning but does not block save. On boot, both GpioOutputDrivers write to the same pin — last write wins. User's responsibility.

**User sets an impossible pin (e.g. 0xFF).** GpioOutputDriver's `begin()` calls `pinMode(0xFF, OUTPUT)` — Arduino-ESP32 ignores invalid pins silently. The driver will happily call `digitalWrite(0xFF, ...)` on every toggle — also silent. Nothing bad happens, but the user's output won't work; diagnosable because `driver->get()` returns its cached `_state` normally.

**User rebinds a slot GPIO → MQTT but leaves the old GPIO still physically wired.** The pin-release step in `beginAll()` sets the pin to `INPUT`, floating. Anything still wired to that pin will see a high-impedance line; behaviour depends on the external relay board but typically the relay goes to its "unpowered" state. No damage.

## 15. Configuration UI

**Placement.** New sub-tab under **Settings**, alongside Network / Firmware. Route `/settings/drivers`, screen `web/src/screens/Drivers.tsx`. NavShell sub-tabs bar (`TABS_SETTINGS`) gains `{ to: '/settings/drivers', label: 'Drivers' }`.

**Layout.** Six cards, one per device slot, top-to-bottom. Card structure:

- Header: device display name + current kind as a Badge (`gpio` → green `ok`, `mqtt` → cyan `info`).
- Kind picker: two pill buttons **GPIO / MQTT** (reuses the pattern from Firmware-update type picker).
- Conditional body:
  - GPIO: pin (number input 0–39) + active-level toggle (Low / High).
  - MQTT: six text inputs — command topic, payload-on (default `on`), payload-off (default `off`), state topic (optional, helper text "leave empty for fire-and-forget — Pump will report 'not running'"), state-on (default `on`), state-off (default `off`). Placeholder values all use a Shelly example for the first-time user.
- **Save** button per card. Click → POST that slot's config → device reboots.

**Prefill.** `useEffect` on mount → `GET /api/drivers` → populate each card's form state. Same pattern as Settings/Network tab.

**Validation (client-side).**
- GPIO pin 0–39 — hard-block save if out of range.
- GPIO pin already used by another slot OR listed in a known-reserved-pin table (`ONE_WIRE_BUS`, `I2C_SDA`, `I2C_SCL`, `PIN_PH_ADC`, etc) — warn but allow save.
- MQTT kind + command topic empty — hard-block save.
- No payload validation beyond non-empty for `payloadOn` / `payloadOff`.

**Error / reboot UX.** Same pattern as Network tab: save button disables → shows "Saving…" → on HTTP 200 shows "Saved — rebooting. Reconnect in ~15 s" → NavShell WS badge auto-flips to Reconnecting while the device boots.

## 16. Backend endpoints

Added to `src/Provisioning.cpp::registerRuntimeRoutes` (admin-auth gated, matching the existing WiFi / MQTT endpoints):

**`GET /api/drivers`** — returns a JSON array of six objects, one per slot, with all fields including the current kind, pin, active-level, and the MQTT strings. No secrets in driver config; nothing is redacted.

```json
[
  { "slot":"filt", "kind":"gpio", "pin":38, "active_level":"low",
    "cmd_topic":"", "payload_on":"on", "payload_off":"off",
    "state_topic":"", "state_on":"on", "state_off":"off" },
  …six entries…
]
```

**`POST /api/drivers/<slot>`** — body is form-encoded (matching the WiFi / MQTT save pattern; frontend uses `apiPostForm`). Fields: `kind`, `pin`, `active_level`, `cmd_topic`, `payload_on`, `payload_off`, `state_topic`, `state_on`, `state_off`. Validates server-side (same rules as client-side). On success, writes to NVS, returns `{"ok":true,"rebooting":true}`, schedules a reboot.

## 17. Migration and defaults

**First-boot after firmware upgrade.** `Credentials::drivers::load` falls back to the hardcoded per-slot defaults when NVS keys are missing. First boot writes nothing until the user hits Save. Net effect: an existing user flashing the new firmware sees zero behaviour change — same GPIO pins, same active-low convention, same everything.

**Downgrade.** The NVS `drivers` namespace is ignored by earlier firmware. Downgrading is safe.

## 18. Testing plan

No automated tests (matching the SP3 precedent). Manual pass on real hardware:

1. **Zero-regression smoke** — flash the new firmware with an empty `drivers` NVS namespace. Confirm all four pumps and both relays behave exactly as before: toggling via SP3 UI flips the expected GPIO, HA state topic reports correctly, PID loops still operate against filter-pump state, filtration schedule still starts/stops on time, anti-freeze still triggers, emergency-stop still works.

2. **MQTT driver happy path** — configure Filtration → MQTT with a test topic (`pool/test/relay1/command` for cmd, `pool/test/relay1/state` for state). Save, device reboots. On a host, run `mosquitto_sub -h 10.25.25.25 -u … -t 'pool/test/#' -v`. Toggle Filtration in SP3 UI → verify the `command` publish appears. Run `mosquitto_pub -h 10.25.25.25 -u … -t 'pool/test/relay1/state' -m 'on' -r` → within 1 s, Dashboard tile shows Filtration ON, HA `filtration_pump/state` flips to `ON`, PID loops start (if conditions met), `poolmaster/<mac>/filt_uptime_today/state` starts advancing.

3. **External toggle sync** — with the above MQTT driver binding, use `mosquitto_pub -t 'pool/test/relay1/state' -m 'off' -r` to simulate someone toggling the Shelly externally. Within 1 s the UI flips and PID loops stop.

4. **Rollback** — switch Filtration back to GPIO. Save, device reboots. Confirm GPIO-based control works again.

5. **Fire-and-forget mode** — configure Chlorine pump → MQTT with an empty state topic. Toggle via UI. Confirm `command` publish happens but `ChlPump.IsRunning()` stays false. Document this clearly in the UI ("leave empty for fire-and-forget — Pump will report 'not running'").

6. **Reboot survives crash** — leave the device running overnight; verify that after a spontaneous panic-reboot (SP3 known issue), the driver config restores from NVS and the Shelly binding is still active.

## 19. Out of scope (explicit future work)

- HTTP-URL driver.
- Net-new user-defined controls (pool lights, salt chlorinator, heat pump, etc. — a brand-new switch that isn't one of the six existing devices).
- Compound / multi-step controls.
- State-change rate limiting / relay-flap protection.
- Per-driver telemetry on the Diagnostics page (publish counter, failure counter).

## 20. Open questions

None outstanding. Design was fully iterated through brainstorming; no TBD / placeholder items remain.
