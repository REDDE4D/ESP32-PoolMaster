# SP8 — Driver schedule presets

**Status:** design  ·  **Date:** 2026-04-30

## Problem

The current schedule logic ([PoolMaster.cpp:119-208](../../../src/PoolMaster.cpp)) supports exactly one filtration window per day, derived once at 15:00 from water temperature, with bounds set via the dashboard's "earliest start / latest stop" inputs. Users can't:

- Run filtration in multiple intervals across the day (split runs in heatwaves, lunch break for chemistry mixing).
- Switch between named operating modes (Summer / Winter / Vacation / Maintenance / Off) without re-typing values.
- Disable filtration on demand without toggling AutoMode (which also disables every safety net).
- Edit the upcoming day's schedule without waiting for the daily 15:00 recompute or reverse-engineering the temp-derivation math.

A previous attempt at this feature ("SP8") was lost in a git sync incident; this spec rebuilds it from first principles using the seed "5 presets, 4 timeslots each."

## Goal

Replace the single hard-coded filtration window with **5 user-configurable presets**, each holding **up to 4 daily windows**. Exactly one preset is active at any time and drives the filtration schedule. The active preset is settable from the dashboard, broadcast over WebSocket, and published over MQTT for Home Assistant.

## Non-goals

- Per-preset chemistry setpoints (pH / ORP / water temp). These remain global. Presets are about **when** to circulate, not **what** to dose.
- Cross-midnight windows (a 22:00 → 02:00 window is two windows: 22:00 – 24:00 and 00:00 – 02:00).
- Per-driver scheduling. r0 / r1 / `custom_*` are MQTT- or manually-controlled and unaffected.
- Multiple simultaneously-active presets.
- Dynamic preset count (the 5-slot array is fixed for predictable NVS sizing).

---

## Design overview

```
┌─ Frontend (web/src) ──────────────┐         ┌─ Firmware ─────────────────────┐
│                                    │         │                                │
│  Schedule.tsx (preset manager)     │         │  Presets module                │
│    ├ 5 preset cards                │         │    ├ load() / save()           │
│    ├ TimePicker (new)              │  WS    │    ├ seedDefaults()            │
│    └ PresetEditModal (new)         │  cmds  │    ├ activate(slot)            │
│        ├ Manual mode → 4 windows   │ ◄────► │    ├ savePreset(slot, data)   │
│        └ Auto-temp → bounds + readout         │    ├ clearPreset(slot)        │
│                                    │  state  │    ├ isInActiveWindow(now)    │
│  poolState.schedule (new)          │ broad-  │    └ tickDailyAutoTemp()      │
│    └ active_slot, presets[]        │  cast   │                                │
│                                    │         │  PoolMaster.cpp                │
│                                    │         │    └ schedule check delegates │
│                                    │         │       to Presets              │
│                                    │         │                                │
│                                    │ MQTT    │  HaDiscovery / MqttBridge      │
│                                    │ ◄─────  │    └ active_preset/state      │
└────────────────────────────────────┘         └────────────────────────────────┘
```

The new `Presets` C++ module is the single source of truth for schedule data. Everything else (PoolMaster supervisory loop, Setup boot path, WebSocket state broadcaster, MQTT publisher) reads through its API.

---

## Data model

### Firmware types — `include/Presets.h`

```cpp
namespace Presets {

  static constexpr uint8_t MAX_PRESETS  = 5;
  static constexpr uint8_t WINDOWS_PER  = 4;
  static constexpr uint8_t NAME_MAX_LEN = 16;   // null-terminated → 15 visible chars

  enum class Type : uint8_t { Manual = 0, AutoTemp = 1 };

  struct Window {
    uint16_t start_min;   // 0..1439 minutes-of-day
    uint16_t end_min;     // 0..1439 (start_min <= end_min, no cross-midnight)
    bool     enabled;
  };

  struct PresetData {
    char    name[NAME_MAX_LEN];
    Type    type;
    Window  windows[WINDOWS_PER];

    // Auto-temp parameters — only consulted when type == AutoTemp
    uint8_t startMinHour;     // 0..23, lower bound for the daily-derived window
    uint8_t stopMaxHour;      // 0..23, upper bound (must be > startMinHour)
    uint8_t centerHour;       // 0..23, the "filter at the hottest hour" anchor
  };

  // Public API
  void   begin();                               // call once from setup() after loadConfig()
  bool   isInActiveWindow(uint16_t now_min);
  void   tickDailyAutoTemp();                   // called from PoolMaster midnight reset
  bool   activate(uint8_t slot);
  bool   savePreset(uint8_t slot, const PresetData& data);
  bool   clearPreset(uint8_t slot);             // resets to empty manual; if active, switches to slot 0 first
  uint8_t activeSlot();
  const PresetData& slot(uint8_t i);            // 0..MAX_PRESETS-1

}
```

**Per-slot size:** 16 (name) + 1 (type) + 4 × 5 (windows) + 3 (auto bounds) = **40 bytes**.
**Total preset state:** 5 × 40 + 1 (active_slot) = **201 bytes**.

### NVS layout

New namespace **`"presets"`**:

| Key      | Type          | Bytes | Description                            |
|----------|---------------|-------|----------------------------------------|
| `active` | `uint8_t`     | 1     | Active slot, 0..4                      |
| `slot0`  | `bytes` blob  | 40    | Packed `PresetData` for slot 0         |
| `slot1`  | `bytes` blob  | 40    | Packed `PresetData` for slot 1         |
| `slot2`  | `bytes` blob  | 40    | …                                      |
| `slot3`  | `bytes` blob  | 40    | …                                      |
| `slot4`  | `bytes` blob  | 40    | …                                      |

Existing `"PoolMaster"` namespace keys stay untouched. After migration the legacy `FiltrStart`, `FiltrStop`, `FiltrStartMin`, `FiltrStopMax`, `FiltrDuration` keys are written-but-never-read — they remain as a downgrade-rescue copy.

### `CONFIG_VERSION` bump → 52

Migration on first boot at version 52:

1. Read calibration coefficients (`pHCalibCoeffs0/1`, `OrpCalibCoeffs0/1`, `PSICalibCoeffs0/1`) into RAM.
2. Read legacy `FiltrStart`, `FiltrStop`, `FiltrStartMin`, `FiltrStopMax` values.
3. Run `Presets::seedDefaults()`:
   - **Slot 0** — `name="Auto-temp"`, `type=AutoTemp`, `startMinHour=FiltrStartMin`, `stopMaxHour=FiltrStopMax`, `centerHour=15`. Window 0 is `[FiltrStart×60, FiltrStop×60, enabled=true]`. Windows 1–3 disabled.
   - **Slots 1–4** — `name="Preset 2"…"Preset 5"`, `type=Manual`, all windows disabled.
   - `active = 0`.
4. Write back the calibration coefficients.
5. Write `ConfigVersion = 52`.

Calibration restoration sits before *and* after the seed path so a partial failure mid-migration can't lose calibrations.

---

## Firmware behavior

### Schedule check (replaces existing logic)

`PoolMaster.cpp` start-pump check at line ~158:

```cpp
uint16_t now_min = (uint16_t)(hour() * 60 + minute());

if (!EmergencyStopFiltPump && !FiltrationPump.IsRunning() && storage.AutoMode &&
    !PSIError && Presets::isInActiveWindow(now_min))
    FiltrationPump.Start();
```

Stop-pump check at line ~189 inverts the same call:

```cpp
if (storage.AutoMode && FiltrationPump.IsRunning() && !AntiFreezeFiltering &&
    !Presets::isInActiveWindow(now_min))
{
    SetPhPID(false); SetOrpPID(false);
    FiltrationPump.Stop();
}
```

`isInActiveWindow(now_min)` returns `true` if **any enabled window** in the active preset contains `now_min` (`start_min <= now_min < end_min`). PID-start, robot, and 45-second PSI-startup safety logic continues to key off `FiltrationPump.IsRunning()` — none of them need to know about presets.

### Daily auto-temp tick

The current 15:00 inline recompute moves into `Presets::tickDailyAutoTemp()`, called once per day from `PoolMaster.cpp`'s existing midnight reset block (right after the `setTime` step, ~line 110), so the new schedule is in place by the start of the day rather than mid-day.

Logic, applied **only if the active preset is `AutoTemp`**:

```cpp
uint8_t duration_h =
    (storage.TempValue < storage.WaterTempLowThreshold)             ? 2 :
    (storage.TempValue < storage.WaterTemp_SetPoint)                ? round(storage.TempValue / 3.0) :
                                                                       round(storage.TempValue / 2.0);

uint8_t start_h = clamp((int)preset.centerHour - (int)round(duration_h / 2.0),
                        (int)preset.startMinHour,
                        (int)preset.stopMaxHour - 1);
uint8_t stop_h  = min(start_h + duration_h, preset.stopMaxHour);

preset.windows[0] = { uint16_t(start_h * 60), uint16_t(stop_h * 60), true };
preset.windows[1] = preset.windows[2] = preset.windows[3] = {0, 0, false};
Presets::save();        // persists + triggers state broadcast + MQTT republish
```

Manual presets are not touched by the daily tick.

### Boot auto-start

`Setup.cpp` at the end of `setup()`, replacing the inline check at ~line 373:

```cpp
uint16_t now_min = (uint16_t)(hour() * 60 + minute());
if (storage.AutoMode && Presets::isInActiveWindow(now_min)) FiltrationPump.Start();
else FiltrationPump.Stop();
```

### AntiFreeze (unchanged behavior, code relocated for cohesion)

The two existing AntiFreeze branches at `PoolMaster.cpp:197` and `:204` keep their semantics: when air temp < −2°C the pump runs regardless of preset windows; when air > 2°C the AntiFreeze override stops. The check sits *outside* `isInActiveWindow` so safety is independent of schedule data.

### Cmd handlers (in `CommandQueue.cpp`)

Three new payload shapes routed through `enqueueCommand`:

| Command          | Payload                                                                  | Effect                                                                                                |
|------------------|---------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------|
| `PresetSave`     | `{slot, name, presetType, windows[4], auto}`                              | Validate, persist, rebroadcast state, republish MQTT if slot is active                                |
| `PresetActivate` | `{slot}`                                                                  | Validate slot range, write `active`, rebroadcast, republish MQTT, run schedule check on next tick     |
| `PresetDelete`   | `{slot}`                                                                  | If `slot == active`, first activate slot 0; then reset target slot to empty manual; rebroadcast/MQTT |

WS command queue size bumped **100 → 256 bytes** (a full `PresetSave` JSON payload with a 16-char name and 4 windows is ~180 bytes).

### Validation (server-side, all cmds reject with `{ok: false, error: "..."}`)

- `slot` must be `0..4`.
- `name` length 1..15, UTF-8.
- `windows` length exactly 4. For each: `start ∈ [0,1439]`, `end ∈ [0,1439]`, `start <= end`.
- For `auto_temp`: `startMinHour < stopMaxHour`, `centerHour ∈ [startMinHour, stopMaxHour]`.
- `PresetActivate` only fails on out-of-range slot. Activating a preset with all windows disabled is a valid "off" state.

---

## Wire protocol

### WebSocket — outbound state shape

Adds a `schedule` block to the existing state JSON:

```jsonc
{
  "type": "state",
  "data": {
    // ...existing fields...
    "schedule": {
      "active_slot": 0,
      "presets": [
        {
          "slot": 0,
          "name": "Auto-temp",
          "type": "auto_temp",
          "windows": [
            { "start": 480, "end": 1080, "enabled": true },
            { "start": 0, "end": 0, "enabled": false },
            { "start": 0, "end": 0, "enabled": false },
            { "start": 0, "end": 0, "enabled": false }
          ],
          "auto": { "startMinHour": 8, "stopMaxHour": 22, "centerHour": 15 }
        },
        {
          "slot": 1,
          "name": "Summer",
          "type": "manual",
          "windows": [/* ... */],
          "auto": null
        }
        // ... slots 2..4
      ]
    }
  }
}
```

Window times are minutes-of-day to keep the JSON compact and parsing trivial. Broadcast on every regular state tick (1 Hz) and immediately after a successful preset cmd.

### WebSocket — inbound cmds

Routed through the existing `{type:"cmd", id, payload:"<JSON-string>"}` envelope; payload is a stringified JSON object whose first key names the cmd:

```jsonc
{ "type": "cmd", "id": "<uuid>",
  "payload": "{\"PresetSave\":{\"slot\":1,\"name\":\"Summer\",\"presetType\":\"manual\",\"windows\":[...],\"auto\":null}}" }

{ "type": "cmd", "id": "<uuid>",
  "payload": "{\"PresetActivate\":{\"slot\":1}}" }

{ "type": "cmd", "id": "<uuid>",
  "payload": "{\"PresetDelete\":{\"slot\":3}}" }
```

Acks use the standard `{type:"ack", id, ok, error?}` reply pattern.

### MQTT publish

| Topic                                          | Retained | Payload                                  | Triggered by                                      |
|------------------------------------------------|----------|------------------------------------------|---------------------------------------------------|
| `homeassistant/<device>/active_preset/state`   | yes      | `{"slot": 0, "name": "Auto-temp"}`       | `activate`, `savePreset` of active slot, boot     |

A discovery config for this topic is added to `HaDiscovery.cpp` so Home Assistant exposes a sensor entity for the active preset name.

---

## Frontend UX

### Routing & components

- `web/src/screens/Schedule.tsx` — rewritten as the preset manager. The current "earliest start / latest stop / DelayPID" inputs go away. DelayPIDs (still global, not per-preset) moves into a small "Global" section at the bottom of the page.
- `web/src/components/PresetCard.tsx` — new. Renders one preset slot with a 24-hour timeline strip, type badge, and active-state styling.
- `web/src/components/PresetEditModal.tsx` — new. Controlled modal: `{ slot, initialData, onSave, onDelete, onClose }`. Renders the manual editor (4 window rows) or the auto-temp editor (bounds + live readout), depending on the selected type.
- `web/src/components/TimePicker.tsx` — new. `HH:MM` minute-granular input with up/down chevrons; emits minutes-of-day. Used by the manual window rows.

### State store — `web/src/stores/state.ts`

Adds a `schedule` field to `PoolState`:

```ts
export type PresetType = 'manual' | 'auto_temp';

export interface PresetWindow { start: number; end: number; enabled: boolean }

export interface AutoTempBounds { startMinHour: number; stopMaxHour: number; centerHour: number }

export interface PresetSlot {
  slot: number;
  name: string;
  type: PresetType;
  windows: PresetWindow[];   // always length 4
  auto: AutoTempBounds | null;
}

export interface SchedulePayload {
  active_slot: number;
  presets: PresetSlot[];     // always length 5
}

// Added to PoolState:
schedule: SchedulePayload;
```

### Cmd helpers — `web/src/lib/api.ts` or `lib/ws.ts`

```ts
poolWs.sendCommand(JSON.stringify({ PresetSave: { slot, name, presetType, windows, auto } }));
poolWs.sendCommand(JSON.stringify({ PresetActivate: { slot } }));
poolWs.sendCommand(JSON.stringify({ PresetDelete: { slot } }));
```

Reuses the existing `sendCommand` plumbing (no new transport).

### Wireframe

See `.superpowers/brainstorm/31619-1777560564/content/schedule-screen.html` (the visual companion mockup). Three views:

1. **Schedule screen** — 5 cards, the active one highlighted in cyan, each showing a 24-hour timeline strip and the windows count or `HH:MM – HH:MM` summary. Clicking a non-active card sends `PresetActivate`. Clicking the pencil opens the edit modal.
2. **PresetEditModal — Manual** — name input, type radio, 4 window rows (enabled toggle + start TimePicker + end TimePicker). Cancel / Save / Delete-slot.
3. **PresetEditModal — Auto-temp** — name input, type radio, "Auto-temp bounds" panel with `startMinHour` / `stopMaxHour` / `centerHour` numeric inputs, plus a read-only live readout: "Currently: 08:00 – 18:00 (10 h, water 24°C)". Cancel / Save / Delete-slot — Delete is offered on every slot including auto-temp; it resets the slot to an empty manual preset (and switches active to slot 0 first if the deleted slot was active, per the firmware rules above).

---

## Failure modes & error handling

| Failure                                                       | Behavior                                                                                            |
|---------------------------------------------------------------|------------------------------------------------------------------------------------------------------|
| NVS read of `slot<n>` returns 0 bytes (corrupt/missing)       | Treat as empty manual preset, log warning, continue. `seedDefaults` runs only on CONFIG_VERSION bump. |
| Active slot points to a corrupt preset                        | Schedule check returns `false` (no windows match) → pump stays off until user fixes it.             |
| `tickDailyAutoTemp` runs but `TempValue` is stale/NaN         | Skip the recompute, log warning, keep yesterday's window. The next day's tick re-attempts.          |
| `PresetSave` validation fails                                 | NVS untouched, ack returns `{ok: false, error: "<reason>"}`, no broadcast.                          |
| `PresetActivate` to a slot that's never been written          | Treated as activating the (empty) default — valid, just means "no windows."                         |
| `PresetDelete` on slot 0                                      | Allowed; slot 0 resets to an empty manual. `seedDefaults` only runs on version bump, so slot 0 stays empty until user repopulates it. |
| AntiFreeze active and user activates a preset with no windows | Pump still runs (AntiFreeze takes precedence) — by design.                                          |
| Boot before NTP sync completes                                | The NTP-guard fix in `Setup.cpp` already ensures `setTime` is only called after a successful `getLocalTime`. Until then, `hour()/minute()` return values from the millis-relative epoch (typically 0) — `isInActiveWindow` is queried with whatever those return, and the daily midnight-resync recovers once NTP comes online. Pump won't run during this window unless the user has a window starting at 00:00. (Future work: a `time_valid` gate could veto the schedule check entirely until NTP succeeds at least once — out of scope for this spec.) |

---

## Testing approach

Embedded firmware doesn't have a native unit-test target in this project (`platformio.ini` has `serial_upload` and `OTA_upload` envs only). Tests are a mix of:

1. **Algorithmic unit tests for the Presets module** — extract `isInActiveWindow` and the auto-temp computation into pure functions, write a small native test runner (`pio test -e native`) covering:
   - Window matching: empty windows, single window, multiple windows, overlapping windows, edge cases at 00:00 and 23:59.
   - Auto-temp duration table: cold (< low_threshold), mid, warm (> setpoint).
   - Auto-temp clamping: `centerHour - duration/2 < startMinHour`, `start + duration > stopMaxHour`.
2. **Frontend type-checks** — `tsc --noEmit` already runs; the new state shape gates the build.
3. **Firmware build verification** — both `serial_upload` and `OTA_upload` envs must compile clean. Flash growth target: stay under 90% on `OTA_upload` (currently 85.1%).
4. **On-device acceptance checklist** (manual, post-flash):
   - First-boot migration: device with old NVS comes up at version 52, slot 0 named "Auto-temp" with rescued values, `active_slot = 0`. Filtration runs at the same window it did pre-migration.
   - Save & activate a manual preset with two windows; verify `FiltrationPump.IsRunning()` matches expected windows over a real day.
   - `PresetDelete` of the active slot: device switches to slot 0, doesn't crash, MQTT republishes the new active name.
   - Power-cycle: active slot persists; device picks up the right schedule on boot.
   - Auto-temp recompute: at 00:01 the next day, slot 0's window 0 reflects current water-temp-derived values.
   - Calibration coefficients survive the migration (verify in Diagnostics screen).

---

## Migration & rollout

1. Land the Presets module + CONFIG_VERSION bump in one commit. Existing devices upgrade in-place via the seed-defaults path.
2. Land the WebSocket cmd handlers and state-payload extension. Old frontends ignore the new `schedule` field.
3. Land the frontend rewrite in a dedicated commit. Old firmware doesn't see `Preset*` cmds and would reject them, so mismatched-version operation is degraded (UI shows empty presets) but safe.
4. Update `README.md` with a feature paragraph and post-SP8 size record (matches the lost commit `2c4bc79`).

## Open questions

None — all clarifying questions in the brainstorm round resolved.

## Out-of-scope follow-ups

- Per-preset chemistry setpoints (own spec if needed).
- Cross-midnight windows.
- Per-driver scheduling (would build on top of this, not replace it).
- Calibration import/export (lost commit `156284b sp9: brainstorm — calibration import/export + safer NVS layout` was a separate spec — left for SP9).
