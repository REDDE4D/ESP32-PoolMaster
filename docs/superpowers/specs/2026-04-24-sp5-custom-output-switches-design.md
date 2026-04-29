# SP5 Custom User-Defined Output Switches Design

**Status:** design approved, ready for plan
**Scope:** extend the SP4 driver registry with 8 user-defined custom output slots — firmware + web UI + HA discovery
**Predecessors:** SP1 (Modern Plumbing), SP3 (Beautiful Web UI), SP4 (Pluggable Output Drivers — ships the reusable `OutputDriver` interface and per-slot NVS pattern)
**Branch target:** `sp5-custom-outputs` off `sp3-web-ui` tip `ad27b9e`

---

## 1. Goal

Let the user add net-new switches on top of the six fixed physical-output devices that SP4 made rebindable (`FiltrationPump`, `PhPump`, `ChlPump`, `RobotPump`, relay `R0`, relay `R1`). Use cases: pool lights, salt chlorinator, heat pump, garden lights, fountain, waterfall — anything MQTT-controllable (typically a Shelly) or wired to a spare GPIO.

Custom switches are deliberately decoupled from pool-control logic — no PID, no uptime tracking, no interlock, no anti-freeze. They are just on/off outputs exposed to the web UI, the command API, and Home Assistant.

## 2. Non-goals

- Scheduling custom switches (beyond what HA automations offer). PoolMaster's filtration schedule remains filter-only.
- Dependencies / interlocks between custom switches ("turn off lights when chlorinator runs").
- Grouping / scenes.
- Per-slot HA `device_class` or custom icons — users can customize in HA.
- Dashboard tiles for custom switches on the main SP3 dashboard. Schema is broadcast over WebSocket; rendering deferred.
- Runtime driver swap without reboot — still reboot-on-save, matching SP4.
- Unbounded slot count. Fixed cap of 8.
- A third driver kind — still just GPIO or MQTT.
- No change to legacy command shapes (`{"FiltPump":1}`, `{"Relay":[0,1]}`, etc).
- No change to the six fixed HA entities or their unique_ids.

## 3. Success criteria

1. Fresh boot with no custom slots configured → HA sees the 6 fixed entities, zero custom entities. No new memory/CPU cost vs SP4.
2. User creates `custom_0` as MQTT targeting a Shelly, saves → reboot → HA sees a 7th switch entity named "Salt chlorinator" with `unique_id: poolmaster_custom_0`. Toggling from HA publishes `{"CustomOutput":[0,1]}` on `PoolTopicAPI`, firmware publishes `on` to the Shelly's command topic, Shelly acks, state propagates back to HA within ~300 ms.
3. User renames "Salt chlorinator" → "Saltwater system" and saves → reboot → HA friendly_name updates in place. `entity_id: switch.poolmaster_custom_0` is unchanged. Existing automations referencing that entity still fire.
4. User deletes `custom_0` → reboot → HA entity disappears (empty retained discovery payload). `g_drivers[6]` is `nullptr` on next boot.
5. User configures two custom slots with GPIO drivers → reboot → toggling each flips the right GPIO. Legacy `{"FiltPump":1}` and `{"Relay":[0,1]}` still work. PID / uptime / schedule for fixed slots unchanged.
6. All SP4 smoke tests still pass — zero regression on the six fixed slots.
7. Final flash usage measured and recorded in README (same ritual as SP4's `ad27b9e`).

## 4. Architecture overview

Extend the SP4 driver registry with 8 dynamic-feel custom slots alongside the fixed 6. No new driver kinds — `GpioOutputDriver` and `MqttOutputDriver` from SP4 are reused unchanged.

```
┌────────────────────────────────────────────────────────────────┐
│              SP3 web UI + legacy JSON + SP5 CustomOutput       │
└────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌────────────────────────────────────────────────────────────────┐
│          ProcessCommand / CommandQueue.cpp                     │
│   (legacy commands unchanged + new "CustomOutput" case)        │
└────────────────────────────────────────────────────────────────┘
                              │
       ┌──────────────────────┼──────────────────────┐
       ▼                      ▼                      ▼
┌──────────────┐   ┌─────────────────────┐   ┌────────────────────┐
│ Pump::Start  │   │ relay_drivers[i]    │   │ Drivers::get       │
│   (fixed 4)  │   │   (R0, R1)           │   │   ("custom_N")     │
└──────────────┘   └─────────────────────┘   └────────────────────┘
          │                   │                       │
          └───────────────────┼───────────────────────┘
                              ▼
            ┌─────────────────────────────────┐
            │  g_drivers[0..13]: OutputDriver*│
            │  0..5 fixed (SP4), 6..13 custom │
            │  disabled custom → nullptr      │
            └─────────────────────────────────┘
                       │                 │
                       ▼                 ▼
            ┌────────────────┐   ┌────────────────────┐
            │ GpioOutputDrv  │   │ MqttOutputDriver   │
            └────────────────┘   └────────────────────┘
```

Net-new moving parts:
- Slot metadata: 8-entry `g_customMeta[]` carrying `enabled` + `displayName` for custom slots.
- `Credentials::drivers::loadCustom / saveCustom / clearCustom` in the existing `drivers` NVS namespace.
- HA discovery loop: second pass over `custom_0..7`, publishing retained discovery for enabled slots and empty payloads for disabled ones.
- `ProcessCommand` case: `{"CustomOutput":[N, value]}` → dispatch.
- REST endpoints: `GET`, `POST`, `DELETE /api/custom-outputs`.
- Settings UI section: list view with inline toggle + Edit / Delete + [+ Add].

Reused from SP4 unchanged:
- `OutputDriver` interface.
- `GpioOutputDriver`, `MqttOutputDriver` classes.
- Per-slot placement-new static storage (extended from 6 to 14 slots).
- MQTT state-topic subscription / route-incoming plumbing.
- Admin-auth gating on config endpoints.
- Reboot-on-save lifecycle.
- The Settings-page driver modal component (extended to accept an optional `displayName` field + slot-type prop; same modal serves fixed and custom).

## 5. Slot registry + storage

`src/Drivers.cpp`:

```cpp
static const char* const SLOTS[] = {
  "filt", "ph", "chl", "robot", "r0", "r1",       // 0..5  fixed (SP4)
  "custom_0","custom_1","custom_2","custom_3",    // 6..13 custom (SP5)
  "custom_4","custom_5","custom_6","custom_7",
};
static constexpr size_t FIXED_COUNT  = 6;
static constexpr size_t CUSTOM_COUNT = 8;
static constexpr size_t SLOT_COUNT   = 14;
```

Per-slot metadata (custom slots only):

```cpp
struct CustomMeta { bool enabled; char displayName[24]; };
static CustomMeta g_customMeta[CUSTOM_COUNT] = {};
```

`displayName` is a fixed 24-byte buffer (23 printable chars + null) to avoid runtime heap churn — matches the SP4 discipline of not allocating in driver paths.

`beginAll()` grows a second loop (abbreviated):

```cpp
for (size_t i = 0; i < CUSTOM_COUNT; ++i) {
  CustomDriverConfig cfg = Credentials::drivers::loadCustom(i);
  g_customMeta[i].enabled = cfg.enabled;
  strlcpy(g_customMeta[i].displayName, cfg.displayName.c_str(),
          sizeof(CustomMeta::displayName));

  size_t s = FIXED_COUNT + i;
  if (!cfg.enabled) { g_drivers[s] = nullptr; continue; }

  if (cfg.kind == 1) {
    MqttDriverConfig mc = { cfg.cmdTopic, cfg.payloadOn, cfg.payloadOff,
                            cfg.stateTopic, cfg.stateOn, cfg.stateOff };
    g_drivers[s] = new (mqttStorage[s]) MqttOutputDriver(mc);
  } else {
    bool activeLow = (cfg.activeLevel == 0);
    g_drivers[s] = new (gpioStorage[s]) GpioOutputDriver(cfg.pin, activeLow);
  }
  g_drivers[s]->begin();
}
```

Disabled slots stay `nullptr` — existing call sites already null-check `Drivers::get()`.

New public helpers on the `Drivers::` namespace:
- `bool isCustomEnabled(uint8_t idx)` — true if `custom_idx` is configured and constructed.
- `const char* customDisplayName(uint8_t idx)` — returns `""` for disabled slots.
- `size_t customSlotCount()` — returns `CUSTOM_COUNT` (for UI rendering).

Static RAM cost: 8 slots × (`sizeof(MqttOutputDriver)` ≈ 72 B + `sizeof(CustomMeta)` = 25 B) ≈ **780 B**.

## 6. NVS persistence

Extend the existing `Credentials::drivers` namespace — no new namespace.

```cpp
struct CustomDriverConfig : DriverConfig {
  bool   enabled;       // false → slot empty, skipped in beginAll + HA
  String displayName;   // ≤23 chars after trim, printable-ASCII (0x20–0x7E)
};

CustomDriverConfig loadCustom(uint8_t idx);
bool               saveCustom(uint8_t idx, const CustomDriverConfig&);
bool               clearCustom(uint8_t idx);    // sets enabled=false, preserves blob
```

- NVS keys: `drv_c0` .. `drv_c7` (within the 15-char NVS limit).
- Serialization: existing `DriverConfig` blob layout + two prefixed fields (`enabled` byte, length-prefixed `displayName`).
- Backward compatible: unused keys simply don't exist until first save.
- `clearCustom` intentionally keeps the blob and flips `enabled=false` so the next boot can publish an empty retained HA discovery payload (retracting the entity cleanly). Actual NVS erase happens at factory-reset time via the existing `Credentials::clearAll()`.

Validation in `saveCustom` — fail-fast, return false with log line:
- `displayName` non-empty after trim, ≤23 chars, printable-ASCII only (bytes `0x20`–`0x7E`).
- If `kind == gpio`: `pin < 40`. Collision with a fixed slot's pin → warning only (matches SP4 behavior).
- If `kind == mqtt`: `cmdTopic` non-empty and ≤63 chars; `payloadOn`/`payloadOff` non-empty. Topics must not contain `#` or `+` (wildcards). No quotes, no unprintable bytes, no Unicode.

Full-cap NVS footprint: 8 × ~200 B = 1.6 KB. Well within the `credentials` partition.

## 7. Command API

One new `ProcessCommand` case in `CommandQueue.cpp`:

```cpp
// {"CustomOutput":[N, value]}  — N is 0..7, value is 0|1 or "on"|"off"
else if (command.containsKey("CustomOutput")) {
  JsonArray a = command["CustomOutput"].as<JsonArray>();
  uint8_t idx   = a[0].as<uint8_t>();
  bool    value = parseBool(a[1]);   // same helper Relay uses
  if (idx >= Drivers::customSlotCount() || !Drivers::isCustomEnabled(idx)) {
    // log + ignore — matches Relay's silent-drop on bad index
  } else {
    char slot[12]; snprintf(slot, sizeof(slot), "custom_%u", idx);
    Drivers::get(slot)->set(value);
    publishCustomState(idx, value);   // retained on poolmaster/custom/N/state
  }
}
```

Echo-back on the state topic uses the "publish what we just set" pattern from the six fixed pumps. If the driver is MQTT and the underlying device reports a different state within ~200 ms, the async state-topic subscription reconciles via the existing `Drivers::tryRouteIncoming` path.

Legacy command shapes — `{"FiltPump":1}`, `{"PhPump":1}`, `{"ChlPump":1}`, `{"RobotPump":1}`, `{"Relay":[N, value]}` — are unchanged.

## 8. Home Assistant discovery

Extend `publishHADiscovery()` with a second loop:

```cpp
for (uint8_t i = 0; i < Drivers::customSlotCount(); ++i) {
  char topic[64];
  snprintf(topic, sizeof(topic),
           "homeassistant/switch/poolmaster_custom_%u/config", i);

  if (!Drivers::isCustomEnabled(i)) {
    mqttClient.publish(topic, 1, /*retain=*/true, "");   // retract
    continue;
  }

  StaticJsonDocument<384> cfg;
  cfg["name"]    = Drivers::customDisplayName(i);
  cfg["uniq_id"] = (String)"poolmaster_custom_" + i;      // pinned forever
  cfg["cmd_t"]   = "PoolTopicAPI";
  cfg["cmd_tpl"] = (String)"{\"CustomOutput\":[" + i + ",{{value}}]}";
  cfg["stat_t"]  = (String)"poolmaster/custom/" + i + "/state";
  cfg["pl_on"]   = "1"; cfg["pl_off"]   = "0";
  cfg["stat_on"] = "1"; cfg["stat_off"] = "0";
  cfg["dev"]     = deviceBlock();   // shared device block, same as fixed pumps
  publishRetained(topic, cfg);
}
```

- `unique_id` is pinned to the slot index (`poolmaster_custom_N`) forever. Display-name edits only update HA's `friendly_name`; `entity_id`, automations, and dashboard references survive renames.
- Discovery is published exactly once per boot, after the MQTT client connects and before `resubscribeStateTopics`. Matches the existing fixed-pump path.
- Any `POST` or `DELETE` on `/api/custom-outputs` reboots the device, which re-runs boot discovery. No in-flight discovery mutations.

## 9. Web API

Admin-auth gated, parallel to the existing `/api/drivers`.

```
GET    /api/custom-outputs        → JSON array of 8 slot states
POST   /api/custom-outputs        → create or update a slot; reboots on 2xx
DELETE /api/custom-outputs/{idx}  → clear a slot; reboots on 2xx
```

GET response shape (empty slots return `{slot, enabled:false}`):

```json
[
  {"slot": 0, "enabled": true,  "displayName": "Salt chlorinator",
   "kind": "mqtt", "cmdTopic": "shellies/salt/relay/0/command",
   "payloadOn": "on", "payloadOff": "off",
   "stateTopic": "shellies/salt/relay/0",
   "stateOn": "on", "stateOff": "off"},
  {"slot": 1, "enabled": false}
]
```

POST body mirrors one GET element and includes `"slot": N` as the required discriminator (same shape as `/api/drivers` POST). Server-side validation reuses `saveCustom` from §6; returns `400` with inline error on failure. `204` success reboots.

## 10. Settings UI

New section on the Settings page, below the existing "Drivers" section:

```
┌─ Custom switches (3 of 8 configured) ───────────────────┐
│ ● Salt chlorinator        [mqtt]   [ON]  [Edit] [×]     │
│ ○ Pool lights             [gpio]   [OFF] [Edit] [×]     │
│ ○ Heat pump               [mqtt]   [OFF] [Edit] [×]     │
│ ┌─────────────────────────────────────────────────────┐ │
│ │            + Add custom switch                      │ │
│ └─────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

- Each row: status dot, display name, driver-kind badge, inline toggle, Edit, Delete.
- Toggle click → sends `{"CustomOutput":[N, 1|0]}` via the existing WebSocket command channel. No page reload. State updates flow back via the existing state broadcast, animating the toggle.
- Edit / Add → modal containing the SP4 driver form with one extra field at the top (`displayName`). Same modal component, new prop `slotType: "fixed" | "custom"`.
- Save → POST. On 2xx, UI shows "Saved. Rebooting..." banner and polls `/api/health` until the device is back.
- Delete → confirm dialog ("This retracts the HA entity. Continue?") → DELETE → same reboot flow.
- Empty state (0 configured): the `+ Add custom switch` card on its own with hint text "Control an external MQTT device or spare GPIO."

WebSocket broadcast schema — add one array to the existing per-tick push:

```json
{ "customOutputs": [{"slot": 0, "on": true}, {"slot": 1, "on": false}, ...] }
```

Only enabled slots appear. Dashboard tiles for custom switches are out of scope for SP5 — the broadcast lands now so SP6 can consume it without another wire-format change.

## 11. Flash / RAM budget

Post-SP4: **98.1 % flash, ~29 KB headroom**. SP5 estimate:

| Area                              | Flash   | RAM    |
|-----------------------------------|---------|--------|
| Drivers registry extension         | ~400 B  | 780 B  |
| `Credentials::drivers` custom path | ~800 B  | —      |
| `ProcessCommand` CustomOutput case | ~300 B  | —      |
| HA discovery loop                  | ~500 B  | —      |
| REST endpoints (3)                 | ~2.5 KB | —      |
| Settings UI section + modal ext.   | ~4 KB   | —      |
| WS broadcast schema extension      | ~200 B  | —      |
| **Total**                          | ~8.7 KB | 780 B  |

Remaining flash margin after SP5: ~20 KB. Comfortable.

Cut list, ordered least-painful first — trigger if build >99.5 % flash:

1. Drop driver-kind badges in the list; render `mqtt` / `gpio` as plain text → ~200 B.
2. Reduce `CUSTOM_COUNT` from 8 to 4 → ~300 B RAM + ~150 B flash.
3. Collapse inline toggle → Edit-only (user flips via HA or dashboard) → ~1 KB JS.
4. Drop the reusable-modal refactor; ship a dedicated custom-switch modal at some duplication cost → ~500 B.

Cuts are agreed ahead of time so execution doesn't stall on scope debates. Each is independently reversible in a later sprint.

## 12. Smoke test checklist

To be executed once against hardware before the ship commit. Numbering matches §3 success criteria.

- [ ] SC1 — Fresh boot, no custom slots: HA sees 6 fixed entities only. Verify `homeassistant/switch/poolmaster_custom_*/config` topics are absent or empty.
- [ ] SC2 — Create `custom_0` (MQTT, Shelly): toggling from HA flips the Shelly relay; state ack reaches HA within 300 ms.
- [ ] SC3 — Rename: HA `friendly_name` updates; `entity_id` unchanged; a test automation referencing the entity still fires.
- [ ] SC4 — Delete: HA entity disappears; `nvs_get_blob("drv_c0")` still returns a blob but `enabled=false`.
- [ ] SC5 — Two GPIO custom slots: toggling each flips the correct GPIO with scope or multimeter. Legacy `{"FiltPump":1}` and `{"Relay":[0,1]}` still work during this scenario.
- [ ] SC6 — Re-run the SP4 smoke checklist from `2026-04-24-sp4-pluggable-output-drivers-design.md`. Zero regression.
- [ ] SC7 — Record final flash % and free heap in README (new commit at the tip of `sp5-custom-outputs`).

## 13. Open items / risks

- **Display-name truncation UX.** Server truncates silently today; spec calls for a user-visible warning. Decide in plan: inline error on save if input >23 chars, or accept + show truncated echo.
- **GPIO collision with fixed slots.** SP4 warns but does not block; SP5 inherits that. Users can pin two slots to the same GPIO and get undefined behavior. Documented as a known quirk; no code enforcement.
- **State topic collisions.** Two custom MQTT slots pointing at the same `stateTopic` will both subscribe and both consume incoming messages. Unlikely misconfig; log a warning at boot, don't block.
- **Reboot storm on bulk reconfiguration.** Every `POST`/`DELETE` reboots. A user adding 4 switches reboots 4 times. Acceptable for a config screen; matches SP4 behavior. If it becomes annoying, a "batch save" endpoint can be added later.
