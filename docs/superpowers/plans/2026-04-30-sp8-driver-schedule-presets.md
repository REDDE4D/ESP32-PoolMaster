# SP8 Driver Schedule Presets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the single hard-coded filtration window with 5 user-configurable presets (4 windows each, manual or auto-temp), driven from a new dashboard preset manager and persisted in NVS. Spec: [docs/superpowers/specs/2026-04-30-sp8-driver-schedule-presets-design.md](../specs/2026-04-30-sp8-driver-schedule-presets-design.md).

**Architecture:** New `Presets` C++ module owns schedule state and the daily auto-temp recompute. PoolMaster supervisory loop, Setup boot path, WebSocket, and MQTT all delegate to it. CONFIG_VERSION bumps 51 → 52; legacy `FiltrStart/Stop/StartMin/StopMax` values rescue-migrate into slot 0 as an `auto_temp` preset so existing devices upgrade in place. Frontend gets new `TimePicker`, `PresetCard`, `PresetEditModal` components and a rewritten `Schedule.tsx` preset manager.

**Tech Stack:** ESP32 Arduino (PlatformIO 6.10.0, ESP32-Arduino-Core 2.0.17), `Preferences` NVS API, `ArduinoJson` 7, ESPAsyncWebServer + AsyncTCP, espMqttClient. Frontend: Preact 10 + `@preact/signals`, Vite 5, TypeScript 5, TailwindCSS 3.

**Pre-conditions:** The WS-storm bundle (PSIError clear, NTP guard, NVS leak fix, WS backpressure, frontend backoff debounce + visibilitychange) and the `LedPanel` Diagnostics card must be committed before this plan runs. Check `git status --short` is empty (other than `.DS_Store`s) before starting Task 1.

---

## Resume point — 2026-05-01

**Tasks 1-6 are complete.** Land the remaining work starting from **Task 7**.

| # | Done | Commit |
|---|------|--------|
| 1 | ✅ | `21d3d58` native test env (Unity) + smoke test |
| 2 | ✅ | `1d9d08a` Presets API headers (PresetsLogic.h + Presets.h) |
| 3 | ✅ | `e7fb361` TDD `isInActiveWindow` (7 tests) |
| 4 | ✅ | `77a1eaf` TDD `computeAutoTempWindow` (5 + 1 odd-duration tests, round-half-up parity with legacy) |
| 5 | ✅ | `c053a0e` Presets in-RAM state + activate/save/clear (incl. hour-bounds + NUL-terminator validation) |
| 6 | ✅ | `4e9fc73` NVS load/save + seedDefaults + calibration preservation, CONFIG_VERSION 51 → 52 |

**Current state of the world:**
- 13 native unit tests pass (`pio test -e native -f "native/test_presets"`).
- Firmware builds clean: `pio run -e OTA_upload` → SUCCESS, Flash 85.1%, RAM 23.6%.
- `Presets::begin()` exists but is **not yet called** from anywhere — Task 7 wires it in.
- `Presets::tickDailyAutoTemp()` is still a stub — Task 9 wires it.
- The schedule-check delegation (Tasks 8) and WS/MQTT/frontend wiring (Tasks 10-17) are untouched.

**Resume instructions for the next session:**

1. Open the project at `/Users/sebastiankuprat/GitHub/ESP32-PoolMaster`.
2. Confirm clean tree: `git status --short` should show only `.DS_Store` noise.
3. Re-invoke the subagent-driven workflow on this plan, **starting at Task 7**. The implementer prompts can quote each task verbatim from this file.
4. Tasks 7-12 are firmware integration; Tasks 13-17 are frontend; Task 18 is README + size record.

---

## File map

| File | Disposition | Responsibility |
|------|-------------|----------------|
| `include/Presets.h` | create | Public API + `PresetData` / `Window` / `Type` types |
| `src/Presets.cpp` | create | Implementation: NVS load/save, seedDefaults, activate, isInActiveWindow, tickDailyAutoTemp |
| `include/PresetsLogic.h` | create | Pure-function declarations (extracted for native testing) |
| `src/PresetsLogic.cpp` | create | Pure-function implementations of `isInActiveWindow` + `computeAutoTempWindow` |
| `test/native/test_presets/test_main.cpp` | create | Unity test runner for pure functions |
| `platformio.ini` | modify | Add `[env:native]` env for unit tests |
| `include/Config.h` | modify | `CONFIG_VERSION 51` → `52` |
| `include/PoolMaster.h` | modify | `QUEUE_ITEM_SIZE 100` → `256` |
| `src/Settings.cpp` | modify | Calibration-preservation hook around CONFIG_VERSION upgrade |
| `src/Setup.cpp` | modify | Call `Presets::begin()`; replace boot auto-start with `Presets::isInActiveWindow` |
| `src/PoolMaster.cpp` | modify | Replace inline schedule check + 15:00 recompute with Presets calls |
| `src/CommandQueue.cpp` | modify | Add `PresetSave` / `PresetActivate` / `PresetDelete` handlers |
| `src/WebSocketHub.cpp` | modify | Add `schedule` block to state JSON; trigger broadcast on preset cmds |
| `src/HaDiscovery.cpp` | modify | Add `active_preset` sensor discovery config |
| `src/MqttPublish.cpp` | modify | Publish active-preset name on activate / save-to-active / boot |
| `web/src/stores/state.ts` | modify | Add `SchedulePayload` / `PresetSlot` types to `PoolState` |
| `web/src/components/TimePicker.tsx` | create | `HH:MM` minute-granular picker |
| `web/src/components/PresetCard.tsx` | create | One preset card with 24-hour timeline strip |
| `web/src/components/PresetEditModal.tsx` | create | Edit modal with manual + auto-temp variants |
| `web/src/screens/Schedule.tsx` | modify | Rewritten as preset manager |
| `README.md` | modify | Feature paragraph + post-SP8 size record |

---

## Task 1: Add native test environment

**Files:**
- Modify: `platformio.ini`
- Create: `test/native/test_smoke/test_main.cpp`

- [ ] **Step 1: Add `[env:native]` to platformio.ini**

Append at the end of `platformio.ini`:

```ini
[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17 -DUNIT_TEST
test_build_src = no
```

- [ ] **Step 2: Create a smoke test to confirm the env runs**

Write `test/native/test_smoke/test_main.cpp`:

```cpp
#include <unity.h>

void test_unity_works(void) {
    TEST_ASSERT_EQUAL(2, 1 + 1);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_unity_works);
    return UNITY_END();
}
```

- [ ] **Step 3: Run the smoke test**

Run: `pio test -e native -f "native/test_smoke"`
Expected: `1 Tests 0 Failures 0 Ignored OK`. If `clang` isn't found, install Xcode CLT (`xcode-select --install`).

- [ ] **Step 4: Verify production firmware build still works**

Run: `pio run -e OTA_upload`
Expected: `[SUCCESS]`. Native env must not break the firmware build.

- [ ] **Step 5: Commit**

```bash
git add platformio.ini test/native/test_smoke/test_main.cpp
git commit -m "$(cat <<'EOF'
sp8: add native test env (Unity) + smoke test

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Define Presets types + pure-function header

**Files:**
- Create: `include/PresetsLogic.h`
- Create: `include/Presets.h`

The split (`PresetsLogic.h` for pure functions, `Presets.h` for NVS-aware module) lets us unit-test pure logic in the native env without pulling in Arduino headers.

- [ ] **Step 1: Create `include/PresetsLogic.h`**

```cpp
#pragma once
#include <stdint.h>

namespace Presets {

  static constexpr uint8_t MAX_PRESETS  = 5;
  static constexpr uint8_t WINDOWS_PER  = 4;
  static constexpr uint8_t NAME_MAX_LEN = 16;

  enum class Type : uint8_t { Manual = 0, AutoTemp = 1 };

  struct Window {
    uint16_t start_min;   // 0..1439
    uint16_t end_min;     // 0..1439, start_min <= end_min
    bool     enabled;
  };

  struct PresetData {
    char    name[NAME_MAX_LEN];
    Type    type;
    Window  windows[WINDOWS_PER];
    uint8_t startMinHour;     // 0..23
    uint8_t stopMaxHour;      // 0..23
    uint8_t centerHour;       // 0..23
  };

  // Pure: returns true if any enabled window in `data` contains `now_min`.
  bool isInActiveWindow(const PresetData& data, uint16_t now_min);

  // Pure: computes the single auto-temp window from environmental inputs.
  // Returns the resulting Window (always enabled). Caller assigns to slot[0].
  Window computeAutoTempWindow(double tempValue,
                               double waterTempLowThreshold,
                               double waterTempSetPoint,
                               uint8_t centerHour,
                               uint8_t startMinHour,
                               uint8_t stopMaxHour);

}
```

- [ ] **Step 2: Create `include/Presets.h` (NVS-aware API)**

```cpp
#pragma once
#include "PresetsLogic.h"

namespace Presets {

  // Load state from NVS, seed defaults if first boot at this CONFIG_VERSION.
  // Call once from setup() AFTER loadConfig().
  void begin();

  // Persist current in-RAM state to NVS. Triggers WS state broadcast and
  // (if active slot changed) MQTT republish via callbacks installed below.
  void save();

  // Replace slot contents. Returns false on validation error.
  bool savePreset(uint8_t slot, const PresetData& data);

  // Switch active slot. Returns false on out-of-range slot.
  bool activate(uint8_t slot);

  // Reset slot to empty manual. If `slot` is active, switches active to 0
  // first. Returns false on out-of-range slot.
  bool clearPreset(uint8_t slot);

  // Schedule check used by the supervisory loop and boot-auto-start.
  bool isInActiveWindow(uint16_t now_min);

  // Recompute the active preset's window 0 from water temperature, if its
  // type is AutoTemp. No-op for Manual presets. Called once per day from
  // the midnight-reset block in PoolMaster.cpp.
  void tickDailyAutoTemp();

  // Read accessors (used by WebSocketHub state broadcaster + MqttPublish).
  uint8_t                   activeSlot();
  const PresetData&         slot(uint8_t i);     // i in [0, MAX_PRESETS)

  // Optional callbacks the WS / MQTT layers can install at startup so we
  // don't introduce header coupling between Presets.cpp and those modules.
  using OnChangeCb = void(*)();
  void setOnChange(OnChangeCb cb);    // fired after every successful save/activate/clear

}
```

- [ ] **Step 3: Verify both headers compile**

Run: `pio run -e OTA_upload`
Expected: `[SUCCESS]` (no `.cpp` files reference these yet, so compilation is just header parsing where included).

- [ ] **Step 4: Commit**

```bash
git add include/PresetsLogic.h include/Presets.h
git commit -m "$(cat <<'EOF'
sp8: Presets — public API headers (logic + NVS-aware split)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: TDD `isInActiveWindow`

**Files:**
- Create: `test/native/test_presets/test_main.cpp`
- Create: `src/PresetsLogic.cpp`

- [ ] **Step 1: Write the failing test**

Create `test/native/test_presets/test_main.cpp`:

```cpp
#include <unity.h>
#include <cstring>
#include "PresetsLogic.h"

using namespace Presets;

static PresetData makeManual(std::initializer_list<Window> ws) {
    PresetData d{};
    std::strcpy(d.name, "test");
    d.type = Type::Manual;
    int i = 0;
    for (auto& w : ws) { if (i < WINDOWS_PER) d.windows[i++] = w; }
    while (i < WINDOWS_PER) d.windows[i++] = {0, 0, false};
    return d;
}

void test_no_enabled_windows_returns_false(void) {
    auto d = makeManual({});
    TEST_ASSERT_FALSE(isInActiveWindow(d, 600));
}

void test_inside_single_enabled_window_returns_true(void) {
    auto d = makeManual({ { 480, 1080, true } });   // 08:00 – 18:00
    TEST_ASSERT_TRUE(isInActiveWindow(d, 600));     // 10:00
}

void test_at_window_start_returns_true(void) {
    auto d = makeManual({ { 480, 1080, true } });
    TEST_ASSERT_TRUE(isInActiveWindow(d, 480));
}

void test_at_window_end_returns_false(void) {
    // half-open [start, end) — end is exclusive
    auto d = makeManual({ { 480, 1080, true } });
    TEST_ASSERT_FALSE(isInActiveWindow(d, 1080));
}

void test_disabled_window_does_not_match(void) {
    auto d = makeManual({ { 480, 1080, false } });
    TEST_ASSERT_FALSE(isInActiveWindow(d, 600));
}

void test_multiple_windows_any_match_returns_true(void) {
    auto d = makeManual({
        { 360, 540,  true },  // 06:00 – 09:00
        { 720, 990,  true },  // 12:00 – 16:30
        { 0,   0,    false },
        { 1080, 1260, true }, // 18:00 – 21:00
    });
    TEST_ASSERT_TRUE(isInActiveWindow(d, 800));
    TEST_ASSERT_TRUE(isInActiveWindow(d, 1200));
    TEST_ASSERT_FALSE(isInActiveWindow(d, 1000));
}

void test_edges_at_midnight_boundaries(void) {
    auto d = makeManual({ { 0, 1, true } });
    TEST_ASSERT_TRUE(isInActiveWindow(d, 0));
    TEST_ASSERT_FALSE(isInActiveWindow(d, 1));
    TEST_ASSERT_FALSE(isInActiveWindow(d, 1439));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_no_enabled_windows_returns_false);
    RUN_TEST(test_inside_single_enabled_window_returns_true);
    RUN_TEST(test_at_window_start_returns_true);
    RUN_TEST(test_at_window_end_returns_false);
    RUN_TEST(test_disabled_window_does_not_match);
    RUN_TEST(test_multiple_windows_any_match_returns_true);
    RUN_TEST(test_edges_at_midnight_boundaries);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `pio test -e native -f "native/test_presets"`
Expected: link error — `Presets::isInActiveWindow` not defined.

- [ ] **Step 3: Create `src/PresetsLogic.cpp` with the implementation**

```cpp
#include "PresetsLogic.h"
#include <math.h>
#include <algorithm>

namespace Presets {

bool isInActiveWindow(const PresetData& data, uint16_t now_min) {
    for (uint8_t i = 0; i < WINDOWS_PER; ++i) {
        const Window& w = data.windows[i];
        if (!w.enabled) continue;
        if (now_min >= w.start_min && now_min < w.end_min) return true;
    }
    return false;
}

// computeAutoTempWindow stub — implemented in Task 4.
Window computeAutoTempWindow(double, double, double, uint8_t, uint8_t, uint8_t) {
    return Window{0, 0, false};
}

} // namespace Presets
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `pio test -e native -f "native/test_presets"`
Expected: `7 Tests 0 Failures 0 Ignored OK`.

- [ ] **Step 5: Commit**

```bash
git add include/PresetsLogic.h src/PresetsLogic.cpp test/native/test_presets/test_main.cpp
git commit -m "$(cat <<'EOF'
sp8: Presets — isInActiveWindow with native unit tests

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: TDD `computeAutoTempWindow`

**Files:**
- Modify: `test/native/test_presets/test_main.cpp`
- Modify: `src/PresetsLogic.cpp`

- [ ] **Step 1: Append failing tests for the auto-temp computation**

Add these tests to `test/native/test_presets/test_main.cpp` (above `main`):

```cpp
void test_autotemp_cold_uses_2h_duration(void) {
    // tempValue < lowThreshold → fixed 2h duration centered on centerHour
    Window w = computeAutoTempWindow(
        /*temp*/ 5.0, /*low*/ 10.0, /*setpoint*/ 27.0,
        /*center*/ 15, /*startMin*/ 8, /*stopMax*/ 22);
    TEST_ASSERT_TRUE(w.enabled);
    TEST_ASSERT_EQUAL_UINT16(14 * 60, w.start_min);   // 14:00
    TEST_ASSERT_EQUAL_UINT16(16 * 60, w.end_min);     // 16:00
}

void test_autotemp_mid_uses_temp_div3(void) {
    // tempValue in [low, setpoint) → duration = round(temp/3)
    // temp=24 → duration=8h, start=15-4=11, stop=11+8=19
    Window w = computeAutoTempWindow(
        24.0, 10.0, 27.0, 15, 8, 22);
    TEST_ASSERT_EQUAL_UINT16(11 * 60, w.start_min);
    TEST_ASSERT_EQUAL_UINT16(19 * 60, w.end_min);
}

void test_autotemp_warm_uses_temp_div2(void) {
    // tempValue >= setpoint → duration = round(temp/2)
    // temp=28 → duration=14h, start=clamp(15-7,8,21)=8, stop=min(8+14,22)=22
    Window w = computeAutoTempWindow(
        28.0, 10.0, 27.0, 15, 8, 22);
    TEST_ASSERT_EQUAL_UINT16(8 * 60,  w.start_min);
    TEST_ASSERT_EQUAL_UINT16(22 * 60, w.end_min);
}

void test_autotemp_clamps_start_to_startminhour(void) {
    // duration=20h, start=15-10=5 → clamp up to startMinHour=8
    Window w = computeAutoTempWindow(
        40.0, 10.0, 27.0, 15, 8, 22);
    TEST_ASSERT_EQUAL_UINT16(8 * 60, w.start_min);
}

void test_autotemp_clamps_stop_to_stopmaxhour(void) {
    // start clamped to 8, duration=20, stop would be 28 → clamp to 22
    Window w = computeAutoTempWindow(
        40.0, 10.0, 27.0, 15, 8, 22);
    TEST_ASSERT_EQUAL_UINT16(22 * 60, w.end_min);
}
```

Also add the `RUN_TEST` lines inside `main`:

```cpp
    RUN_TEST(test_autotemp_cold_uses_2h_duration);
    RUN_TEST(test_autotemp_mid_uses_temp_div3);
    RUN_TEST(test_autotemp_warm_uses_temp_div2);
    RUN_TEST(test_autotemp_clamps_start_to_startminhour);
    RUN_TEST(test_autotemp_clamps_stop_to_stopmaxhour);
```

- [ ] **Step 2: Run the tests to verify the new ones fail**

Run: `pio test -e native -f "native/test_presets"`
Expected: 5 new tests fail with "Expected ..., Was 0" because `computeAutoTempWindow` returns `{0,0,false}`.

- [ ] **Step 3: Replace the stub with the real implementation**

In `src/PresetsLogic.cpp`, replace the `computeAutoTempWindow` stub:

```cpp
Window computeAutoTempWindow(double tempValue,
                             double waterTempLowThreshold,
                             double waterTempSetPoint,
                             uint8_t centerHour,
                             uint8_t startMinHour,
                             uint8_t stopMaxHour) {
    int duration_h;
    if (tempValue < waterTempLowThreshold)        duration_h = 2;
    else if (tempValue < waterTempSetPoint)       duration_h = (int)(tempValue / 3.0 + 0.5);
    else                                          duration_h = (int)(tempValue / 2.0 + 0.5);

    // Round-half-up of duration_h/2 to match legacy behavior in
    // PoolMaster.cpp:136 (`(int)round(FiltrationDuration / 2.)`). For an
    // odd duration like 7, half_dur = 4 (not 3), so a 7h window centered
    // at 15:00 is 11:00–18:00, not 12:00–19:00.
    int half_dur = (duration_h + 1) / 2;
    int start_h  = (int)centerHour - half_dur;
    if (start_h < (int)startMinHour) start_h = (int)startMinHour;
    if (start_h > (int)stopMaxHour - 1) start_h = (int)stopMaxHour - 1;

    int stop_h = start_h + duration_h;
    if (stop_h > (int)stopMaxHour) stop_h = (int)stopMaxHour;

    Window w;
    w.start_min = (uint16_t)(start_h * 60);
    w.end_min   = (uint16_t)(stop_h  * 60);
    w.enabled   = true;
    return w;
}
```

- [ ] **Step 4: Run the tests to verify all pass**

Run: `pio test -e native -f "native/test_presets"`
Expected: `13 Tests 0 Failures 0 Ignored OK`.

- [ ] **Step 5: Commit**

```bash
git add src/PresetsLogic.cpp test/native/test_presets/test_main.cpp
git commit -m "$(cat <<'EOF'
sp8: Presets — computeAutoTempWindow with clamp tests

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Implement Presets module — in-RAM state + activate/save/clear

**Files:**
- Create: `src/Presets.cpp`

NVS persistence is split out into Task 6 so this task is testable on the firmware build alone.

- [ ] **Step 1: Create `src/Presets.cpp` with in-RAM state**

```cpp
#include "Presets.h"
#include <Arduino.h>
#include <string.h>

namespace Presets {

namespace {
  PresetData g_slots[MAX_PRESETS];
  uint8_t    g_active = 0;
  OnChangeCb g_onChange = nullptr;

  void emitChange() { if (g_onChange) g_onChange(); }

  void resetSlotToEmptyManual(uint8_t i, const char* name) {
    PresetData& d = g_slots[i];
    memset(&d, 0, sizeof(d));
    strncpy(d.name, name, NAME_MAX_LEN - 1);
    d.type = Type::Manual;
    d.startMinHour = 8;
    d.stopMaxHour  = 22;
    d.centerHour   = 15;
    for (uint8_t w = 0; w < WINDOWS_PER; ++w)
      d.windows[w] = { 0, 0, false };
  }
} // namespace

void setOnChange(OnChangeCb cb) { g_onChange = cb; }

uint8_t           activeSlot()         { return g_active; }
const PresetData& slot(uint8_t i)      { return g_slots[i < MAX_PRESETS ? i : 0]; }

bool isInActiveWindow(uint16_t now_min) {
  return isInActiveWindow(g_slots[g_active], now_min);
}

bool savePreset(uint8_t target, const PresetData& data) {
  if (target >= MAX_PRESETS) return false;
  // Validation
  if (data.type != Type::Manual && data.type != Type::AutoTemp) return false;
  for (uint8_t i = 0; i < WINDOWS_PER; ++i) {
    if (data.windows[i].start_min > 1439 || data.windows[i].end_min > 1439) return false;
    if (data.windows[i].start_min > data.windows[i].end_min) return false;
  }
  if (data.type == Type::AutoTemp) {
    if (data.startMinHour >= data.stopMaxHour) return false;
    if (data.centerHour < data.startMinHour || data.centerHour > data.stopMaxHour) return false;
  }
  if (data.name[0] == '\0') return false;

  g_slots[target] = data;
  g_slots[target].name[NAME_MAX_LEN - 1] = '\0';   // belt-and-braces null terminator
  save();          // persists + emits change
  return true;
}

bool activate(uint8_t target) {
  if (target >= MAX_PRESETS) return false;
  g_active = target;
  save();
  return true;
}

bool clearPreset(uint8_t target) {
  if (target >= MAX_PRESETS) return false;
  if (target == g_active) g_active = 0;
  char fallback[NAME_MAX_LEN];
  snprintf(fallback, sizeof(fallback), "Preset %u", (unsigned)(target + 1));
  resetSlotToEmptyManual(target, fallback);
  save();
  return true;
}

void tickDailyAutoTemp() {
  // Stub — implemented in Task 9 once water-temp storage is reachable.
}

void begin() {
  // Stub — populated in Task 6 once NVS load lands. For now, seed in-RAM
  // defaults so unit-test-style firmware bring-up is sane.
  for (uint8_t i = 0; i < MAX_PRESETS; ++i) {
    char name[NAME_MAX_LEN];
    snprintf(name, sizeof(name), (i == 0) ? "Auto-temp" : "Preset %u", (unsigned)(i + 1));
    resetSlotToEmptyManual(i, name);
    if (i == 0) g_slots[i].type = Type::AutoTemp;
  }
  g_active = 0;
}

void save() {
  // Stub — implemented in Task 6.
  emitChange();
}

} // namespace Presets
```

- [ ] **Step 2: Verify firmware builds clean**

Run: `pio run -e OTA_upload`
Expected: `[SUCCESS]` and a tiny size increase (~few hundred bytes flash).

- [ ] **Step 3: Verify the native unit tests still pass**

Run: `pio test -e native -f "native/test_presets"`
Expected: `13 Tests 0 Failures 0 Ignored OK`. (The new module includes Arduino.h, so it can't be linked in the native env — but the pure-function test target only includes `PresetsLogic.cpp`, so it stays green.)

- [ ] **Step 4: Commit**

```bash
git add src/Presets.cpp
git commit -m "$(cat <<'EOF'
sp8: Presets — in-RAM state + activate/savePreset/clearPreset

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Implement NVS load/save + seedDefaults + calibration preservation

**Files:**
- Modify: `src/Presets.cpp`
- Modify: `include/Config.h`
- Modify: `src/Settings.cpp`

This task bumps `CONFIG_VERSION` from 51 to 52, adds NVS persistence to `Presets`, and routes the existing `loadConfig` migration through a calibration-preservation hook so the bump doesn't reset calibrations.

- [ ] **Step 1: Bump `CONFIG_VERSION`**

In `include/Config.h`, change:

```cpp
#define CONFIG_VERSION 51
```

to:

```cpp
#define CONFIG_VERSION 52
```

- [ ] **Step 2: Add NVS load/save + seed-defaults + legacy rescue to `src/Presets.cpp`**

Add these includes at the top:

```cpp
#include <Preferences.h>
```

Replace the `begin()` stub with the full implementation:

```cpp
namespace {
  static constexpr const char* NS = "presets";
}

void save() {
  Preferences p;
  p.begin(NS, false);
  p.putUChar("active", g_active);
  for (uint8_t i = 0; i < MAX_PRESETS; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "slot%u", (unsigned)i);
    p.putBytes(key, &g_slots[i], sizeof(PresetData));
  }
  p.end();
  emitChange();
}

static bool loadFromNVS() {
  Preferences p;
  p.begin(NS, true);
  bool foundAny = false;
  for (uint8_t i = 0; i < MAX_PRESETS; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "slot%u", (unsigned)i);
    size_t got = p.getBytes(key, &g_slots[i], sizeof(PresetData));
    if (got == sizeof(PresetData)) foundAny = true;
    else {
      // Corrupt or missing — reset to a safe empty manual.
      char fallback[NAME_MAX_LEN];
      snprintf(fallback, sizeof(fallback), "Preset %u", (unsigned)(i + 1));
      resetSlotToEmptyManual(i, fallback);
    }
  }
  g_active = p.getUChar("active", 0);
  if (g_active >= MAX_PRESETS) g_active = 0;
  p.end();
  return foundAny;
}

static void seedDefaults() {
  // Slot 0 — auto-temp, rescued from legacy FiltrStart/Stop in PoolMaster ns.
  Preferences legacy;
  legacy.begin("PoolMaster", true);
  uint8_t fStart    = legacy.getUChar("FiltrStart",    8);
  uint8_t fStop     = legacy.getUChar("FiltrStop",     20);
  uint8_t fStartMin = legacy.getUChar("FiltrStartMin", 8);
  uint8_t fStopMax  = legacy.getUChar("FiltrStopMax",  22);
  legacy.end();

  resetSlotToEmptyManual(0, "Auto-temp");
  g_slots[0].type         = Type::AutoTemp;
  g_slots[0].startMinHour = fStartMin;
  g_slots[0].stopMaxHour  = fStopMax;
  g_slots[0].centerHour   = 15;
  g_slots[0].windows[0]   = { uint16_t(fStart * 60), uint16_t(fStop * 60), true };

  for (uint8_t i = 1; i < MAX_PRESETS; ++i) {
    char name[NAME_MAX_LEN];
    snprintf(name, sizeof(name), "Preset %u", (unsigned)(i + 1));
    resetSlotToEmptyManual(i, name);
  }
  g_active = 0;
  save();    // writes back without emitting (g_onChange not yet wired)
}

void begin() {
  bool foundAny = loadFromNVS();
  if (!foundAny) seedDefaults();
}
```

- [ ] **Step 3: Add calibration-preservation hook in `src/Settings.cpp`**

Settings.cpp's `loadConfig` returns `(storage.ConfigVersion == CONFIG_VERSION)` — callers (Setup.cpp around line 256) trigger `saveConfig` when this is false to write the in-RAM defaults. We need to preserve the existing calibration coefficients across that re-save.

Add this helper at the top of `src/Settings.cpp` (after the existing `extern Preferences nvs;`):

```cpp
// Read the six calibration coefficients before a CONFIG_VERSION upgrade
// rewrites them to defaults, and restore them after. Without this, every
// version bump resets the user's calibration to seed values.
namespace {
  struct CalSnapshot {
    double phC0, phC1, orpC0, orpC1, psiC0, psiC1;
  };

  CalSnapshot snapshotCalibrations() {
    Preferences p;
    p.begin("PoolMaster", true);
    CalSnapshot s;
    s.phC0  = p.getDouble("pHCalibCoeffs0",  4.3);
    s.phC1  = p.getDouble("pHCalibCoeffs1",  -2.63);
    s.orpC0 = p.getDouble("OrpCalibCoeffs0", -1189.0);
    s.orpC1 = p.getDouble("OrpCalibCoeffs1", 2564.0);
    s.psiC0 = p.getDouble("PSICalibCoeffs0", 1.11);
    s.psiC1 = p.getDouble("PSICalibCoeffs1", 0.0);
    p.end();
    return s;
  }

  void applyCalibrations(const CalSnapshot& s) {
    storage.pHCalibCoeffs0  = s.phC0;
    storage.pHCalibCoeffs1  = s.phC1;
    storage.OrpCalibCoeffs0 = s.orpC0;
    storage.OrpCalibCoeffs1 = s.orpC1;
    storage.PSICalibCoeffs0 = s.psiC0;
    storage.PSICalibCoeffs1 = s.psiC1;
  }
}

void preserveCalibrationsAcrossUpgrade() {
  CalSnapshot snap = snapshotCalibrations();
  applyCalibrations(snap);
}
```

Add forward declaration in `include/Settings.h`:

```cpp
// Read calibration coefficients from NVS into in-RAM storage. Call after
// loadConfig() returns false (CONFIG_VERSION mismatch) and before
// saveConfig() so the bump doesn't reset calibrations.
void preserveCalibrationsAcrossUpgrade();
```

- [ ] **Step 4: Wire the hook into `src/Setup.cpp`**

Find the loadConfig fallback block (around line 256, where `loadConfig()` returns false and `saveConfig()` is called). Insert `preserveCalibrationsAcrossUpgrade()` between the two:

```cpp
if (loadConfig()) {
  Debug.print(DBG_INFO, "Loaded NVS configuration");
} else {
  Debug.print(DBG_WARNING, "NVS configuration version mismatch — re-saving defaults");
  preserveCalibrationsAcrossUpgrade();   // ← new line
  saveConfig();
}
```

- [ ] **Step 5: Verify firmware build is clean**

Run: `pio run -e OTA_upload`
Expected: `[SUCCESS]`. Flash usage should still be under 90%.

- [ ] **Step 6: Verify native tests still pass**

Run: `pio test -e native -f "native/test_presets"`
Expected: `13 Tests 0 Failures 0 Ignored OK`.

- [ ] **Step 7: Commit**

```bash
git add include/Config.h include/Settings.h src/Presets.cpp src/Settings.cpp src/Setup.cpp
git commit -m "$(cat <<'EOF'
sp8: Presets — NVS load/save/seedDefaults + calibration preservation

CONFIG_VERSION 51 → 52. seedDefaults rescues legacy
FiltrStart/Stop/StartMin/StopMax into slot 0 as an auto-temp preset so
existing devices upgrade in place. Calibration coefficients are snapshotted
and restored around the version bump so the upgrade doesn't reset them.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Wire Presets into Setup.cpp boot path

**Files:**
- Modify: `src/Setup.cpp`

- [ ] **Step 1: Include `Presets.h` and call `Presets::begin()`**

At the top of `src/Setup.cpp`, near the existing includes:

```cpp
#include "Presets.h"
```

In the `setup()` function, after the `loadConfig()` block (around line 260, before any pump initialization), add:

```cpp
Presets::begin();
```

- [ ] **Step 2: Replace the boot auto-start check (line ~373)**

Find:

```cpp
if (storage.AutoMode && (hour() >= storage.FiltrationStart) && (hour() < storage.FiltrationStop))
  FiltrationPump.Start();
else FiltrationPump.Stop();
```

Replace with:

```cpp
uint16_t now_min = (uint16_t)(hour() * 60 + minute());
if (storage.AutoMode && Presets::isInActiveWindow(now_min)) FiltrationPump.Start();
else FiltrationPump.Stop();
```

- [ ] **Step 3: Verify firmware builds**

Run: `pio run -e OTA_upload`
Expected: `[SUCCESS]`.

- [ ] **Step 4: Commit**

```bash
git add src/Setup.cpp
git commit -m "$(cat <<'EOF'
sp8: Setup — call Presets::begin() and route boot auto-start through it

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Replace the inline schedule check in PoolMaster.cpp

**Files:**
- Modify: `src/PoolMaster.cpp`

- [ ] **Step 1: Include `Presets.h`**

At the top of `src/PoolMaster.cpp`:

```cpp
#include "Presets.h"
```

- [ ] **Step 2: Replace the start-pump check (around line 158)**

Find:

```cpp
if (!EmergencyStopFiltPump && !FiltrationPump.IsRunning() && storage.AutoMode &&
    !PSIError && hour() >= storage.FiltrationStart && hour() < storage.FiltrationStop )
    FiltrationPump.Start();
```

Replace with:

```cpp
{
  uint16_t now_min = (uint16_t)(hour() * 60 + minute());
  if (!EmergencyStopFiltPump && !FiltrationPump.IsRunning() && storage.AutoMode &&
      !PSIError && Presets::isInActiveWindow(now_min))
      FiltrationPump.Start();
}
```

- [ ] **Step 3: Replace the PID start guard (around line 178)**

Find:

```cpp
if (FiltrationPump.IsRunning() && storage.AutoMode && !storage.WinterMode && !PhPID.GetMode() &&
    ((millis() - FiltrationPump.LastStartTime) / 1000 / 60 >= storage.DelayPIDs) &&
    (hour() >= storage.FiltrationStart) && (hour() < storage.FiltrationStop) &&
    storage.TempValue >= storage.WaterTempLowThreshold)
```

Replace the two `hour()` clauses on the third line with the preset call:

```cpp
{
  uint16_t now_min = (uint16_t)(hour() * 60 + minute());
  if (FiltrationPump.IsRunning() && storage.AutoMode && !storage.WinterMode && !PhPID.GetMode() &&
      ((millis() - FiltrationPump.LastStartTime) / 1000 / 60 >= storage.DelayPIDs) &&
      Presets::isInActiveWindow(now_min) &&
      storage.TempValue >= storage.WaterTempLowThreshold)
  {
    SetPhPID(true);
    SetOrpPID(true);
  }
}
```

- [ ] **Step 4: Replace the stop-pump check (around line 189)**

Find:

```cpp
if (storage.AutoMode && FiltrationPump.IsRunning() && !AntiFreezeFiltering && (hour() >= storage.FiltrationStop || hour() < storage.FiltrationStart))
{
    SetPhPID(false);
    SetOrpPID(false);
    FiltrationPump.Stop();
}
```

Replace with:

```cpp
{
  uint16_t now_min = (uint16_t)(hour() * 60 + minute());
  if (storage.AutoMode && FiltrationPump.IsRunning() && !AntiFreezeFiltering &&
      !Presets::isInActiveWindow(now_min))
  {
      SetPhPID(false);
      SetOrpPID(false);
      FiltrationPump.Stop();
  }
}
```

- [ ] **Step 5: Replace the AntiFreeze "outside windows" guards (lines ~197 and ~204)**

Both of these branches currently check `(hour() < FiltrationStart) || (hour() > FiltrationStop)` — i.e. *outside* the schedule. Replace both with `!Presets::isInActiveWindow(now_min)`. The two updated lines:

```cpp
// AntiFreeze start (line ~197)
if (!EmergencyStopFiltPump && storage.AutoMode && !PSIError && !FiltrationPump.IsRunning() &&
    !Presets::isInActiveWindow(now_min) && (storage.TempExternal < -2.0))

// AntiFreeze stop (line ~204)
if (storage.AutoMode && FiltrationPump.IsRunning() &&
    !Presets::isInActiveWindow(now_min) && AntiFreezeFiltering && (storage.TempExternal > 2.0))
```

(Reuse the `now_min` already computed in Step 4's block; you may need to widen the surrounding scope or re-compute. Cleanest: compute `now_min` once at the top of the loop iteration and use it throughout.)

- [ ] **Step 6: Verify firmware builds**

Run: `pio run -e OTA_upload`
Expected: `[SUCCESS]`.

- [ ] **Step 7: Commit**

```bash
git add src/PoolMaster.cpp
git commit -m "$(cat <<'EOF'
sp8: PoolMaster — schedule check delegates to Presets::isInActiveWindow

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Replace the inline 15:00 recompute with `tickDailyAutoTemp`

**Files:**
- Modify: `src/Presets.cpp`
- Modify: `src/PoolMaster.cpp`

- [ ] **Step 1: Implement `Presets::tickDailyAutoTemp()` properly**

Replace the stub in `src/Presets.cpp` with:

```cpp
extern struct StoreStruct storage;   // declared in PoolMaster.h

void tickDailyAutoTemp() {
  PresetData& d = g_slots[g_active];
  if (d.type != Type::AutoTemp) return;

  Window w = computeAutoTempWindow(
    storage.TempValue,
    storage.WaterTempLowThreshold,
    storage.WaterTemp_SetPoint,
    d.centerHour,
    d.startMinHour,
    d.stopMaxHour);

  d.windows[0] = w;
  for (uint8_t i = 1; i < WINDOWS_PER; ++i) d.windows[i] = { 0, 0, false };
  save();
}
```

Add `#include "PoolMaster.h"` to the top of `src/Presets.cpp` (so `storage` and `StoreStruct` are visible).

- [ ] **Step 2: Replace the inline 15:00 recompute in PoolMaster.cpp**

Find the recompute block (line ~123-152, gated on `hour() == 15`). Delete it entirely. The midnight reset block (line ~89-113) gains one new line right after the `setTime` call: `Presets::tickDailyAutoTemp();`.

The midnight block should look like:

```cpp
if (hour() == 0 && !DoneForTheDay)
{
    storage.AcidFill = PhPump.GetTankFill();
    storage.ChlFill = ChlPump.GetTankFill();
    saveParam("AcidFill", storage.AcidFill);
    saveParam("ChlFill", storage.ChlFill);
    FiltrationPump.ResetUpTime();
    PhPump.ResetUpTime();
    PhPump.SetTankFill(storage.AcidFill);
    ChlPump.ResetUpTime();
    ChlPump.SetTankFill(storage.ChlFill);
    RobotPump.ResetUpTime();
    EmergencyStopFiltPump = false;
    PSIError = false;
    DoneForTheDay = true;
    cleaning_done = false;
    if (readLocalTime())
      setTime(timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec,timeinfo.tm_mday,timeinfo.tm_mon+1,timeinfo.tm_year-100);
    Presets::tickDailyAutoTemp();    // ← new line

    if(WiFi.status() != WL_CONNECTED) esp_restart();
}
```

The `bool d_calc` local variable in the supervisory task is now unused — delete its declaration too.

- [ ] **Step 3: Verify firmware builds**

Run: `pio run -e OTA_upload`
Expected: `[SUCCESS]`.

- [ ] **Step 4: Commit**

```bash
git add src/Presets.cpp src/PoolMaster.cpp
git commit -m "$(cat <<'EOF'
sp8: Presets — tickDailyAutoTemp replaces inline 15:00 recompute

Recompute now runs once per day from the midnight-reset block (right after
setTime), so the new schedule is in place by the start of the day rather
than mid-day.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: Bump WS cmd queue size + add Preset cmd handlers

**Files:**
- Modify: `include/PoolMaster.h`
- Modify: `src/CommandQueue.cpp`

- [ ] **Step 1: Bump `QUEUE_ITEM_SIZE`**

In `include/PoolMaster.h`, change:

```cpp
#define QUEUE_ITEM_SIZE 100
```

to:

```cpp
#define QUEUE_ITEM_SIZE 256
```

- [ ] **Step 2: Add include + cmd handlers to `src/CommandQueue.cpp`**

Add at the top (with other includes):

```cpp
#include "Presets.h"
```

In the `ProcessCommand` task body — find the `if (command["X"].is<JsonVariant>())` chain where the existing cmds are dispatched. Append three new branches before the final `else { /* unknown */ }`:

```cpp
else if (command[F("PresetActivate")].is<JsonVariant>())
{
  uint8_t s = command[F("PresetActivate")][F("slot")] | 0xFF;
  if (s < Presets::MAX_PRESETS) Presets::activate(s);
}
else if (command[F("PresetDelete")].is<JsonVariant>())
{
  uint8_t s = command[F("PresetDelete")][F("slot")] | 0xFF;
  if (s < Presets::MAX_PRESETS) Presets::clearPreset(s);
}
else if (command[F("PresetSave")].is<JsonVariant>())
{
  JsonObject o = command[F("PresetSave")];
  uint8_t s = o[F("slot")] | 0xFF;
  if (s >= Presets::MAX_PRESETS) return;

  Presets::PresetData d{};
  const char* name = o[F("name")] | "";
  strncpy(d.name, name, Presets::NAME_MAX_LEN - 1);
  const char* tStr = o[F("presetType")] | "manual";
  d.type = (strcmp(tStr, "auto_temp") == 0) ? Presets::Type::AutoTemp : Presets::Type::Manual;

  JsonArray ws = o[F("windows")].as<JsonArray>();
  uint8_t i = 0;
  for (JsonObject w : ws) {
    if (i >= Presets::WINDOWS_PER) break;
    d.windows[i].start_min = w[F("start")] | 0;
    d.windows[i].end_min   = w[F("end")]   | 0;
    d.windows[i].enabled   = w[F("enabled")] | false;
    ++i;
  }
  while (i < Presets::WINDOWS_PER) { d.windows[i++] = { 0, 0, false }; }

  JsonObject autoBlk = o[F("auto")].as<JsonObject>();
  if (!autoBlk.isNull()) {
    d.startMinHour = autoBlk[F("startMinHour")] | 8;
    d.stopMaxHour  = autoBlk[F("stopMaxHour")]  | 22;
    d.centerHour   = autoBlk[F("centerHour")]   | 15;
  } else {
    d.startMinHour = 8;
    d.stopMaxHour  = 22;
    d.centerHour   = 15;
  }

  Presets::savePreset(s, d);
}
```

(The existing handler chain returns void on each branch; the `return` in the slot-range early-out matches that convention.)

- [ ] **Step 3: Verify firmware builds**

Run: `pio run -e OTA_upload`
Expected: `[SUCCESS]`.

- [ ] **Step 4: Commit**

```bash
git add include/PoolMaster.h src/CommandQueue.cpp
git commit -m "$(cat <<'EOF'
sp8: CommandQueue — PresetSave / PresetActivate / PresetDelete handlers

Bumps WS cmd queue item size 100 → 256 to fit a full PresetSave payload
(16-char name + 4 windows ≈ 180 bytes JSON).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: Add `schedule` block to state JSON broadcast

**Files:**
- Modify: `src/WebSocketHub.cpp`

- [ ] **Step 1: Include `Presets.h`**

At the top of `src/WebSocketHub.cpp`:

```cpp
#include "Presets.h"
```

- [ ] **Step 2: Append a `schedule` block to `buildStateJson()`**

In `buildStateJson()`, after the existing `data["customOutputs"]` block, add:

```cpp
{
  JsonObject sched = data["schedule"].to<JsonObject>();
  sched["active_slot"] = Presets::activeSlot();
  JsonArray  arr   = sched["presets"].to<JsonArray>();
  for (uint8_t i = 0; i < Presets::MAX_PRESETS; ++i) {
    const Presets::PresetData& p = Presets::slot(i);
    JsonObject o = arr.add<JsonObject>();
    o["slot"] = i;
    o["name"] = p.name;
    o["type"] = (p.type == Presets::Type::AutoTemp) ? "auto_temp" : "manual";
    JsonArray ws = o["windows"].to<JsonArray>();
    for (uint8_t w = 0; w < Presets::WINDOWS_PER; ++w) {
      JsonObject wo = ws.add<JsonObject>();
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
```

- [ ] **Step 3: Force an immediate broadcast on preset change**

Wire the Presets onChange callback inside `WebSocketHub::begin()` (after the existing `srv.addHandler(&ws)` line):

```cpp
Presets::setOnChange([]() {
  // Schedule changed — invalidate the cache so the next tick re-broadcasts.
  g_last_state_json = "";
});
```

(The cache invalidation forces `broadcastStateNow()` on the next tick to re-serialise and push, which is the same pattern the existing 5-second heartbeat uses — see `tick()` at the bottom of the file.)

- [ ] **Step 4: Verify firmware builds**

Run: `pio run -e OTA_upload`
Expected: `[SUCCESS]`. Flash growth: ~1-2 KB.

- [ ] **Step 5: Commit**

```bash
git add src/WebSocketHub.cpp
git commit -m "$(cat <<'EOF'
sp8: WebSocketHub — broadcast presets[] + active_slot in state JSON

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 12: MQTT active_preset publish + HaDiscovery config

**Files:**
- Modify: `src/MqttPublish.cpp`
- Modify: `src/HaDiscovery.cpp`

- [ ] **Step 1: Add a discovery entry for the active_preset sensor**

Open `src/HaDiscovery.cpp`. Find the `ENTITIES` array (around line 28). Add this line in the `// ---- modes ----` block (or anywhere logical — order is purely for human readability):

```cpp
{"sensor", "active_preset",      "Active preset",          "mdi:calendar-clock", nullptr, nullptr, false, 0, 0, 0, nullptr},
```

This matches the existing `Entity` struct shape: `{component, id, name, icon, unit, deviceClass, controllable, minVal, maxVal, step, buttonAction}`.

- [ ] **Step 2: Add an `extern void publishActivePreset();` declaration**

In `include/MqttBridge.h` (or whatever header MqttPublish.cpp publishes through), add:

```cpp
void publishActivePreset();
```

- [ ] **Step 3: Implement publishActivePreset in `src/MqttPublish.cpp`**

```cpp
#include "Presets.h"

void publishActivePreset() {
  if (!MQTTConnection) return;
  const Presets::PresetData& p = Presets::slot(Presets::activeSlot());
  char payload[64];
  snprintf(payload, sizeof(payload),
           "{\"slot\":%u,\"name\":\"%s\"}",
           (unsigned)Presets::activeSlot(), p.name);
  mqttClient.publish(HaDiscovery::stateTopic("active_preset").c_str(), 0, true, payload);
}
```

- [ ] **Step 4: Wire publishActivePreset into the existing onChange flow**

In `src/Setup.cpp`, after the `Presets::begin()` call, install a chained onChange so MQTT republishes alongside the WS cache invalidation.

The cleanest pattern: have `WebSocketHub::begin()` install its callback first; then in Setup, register a second-stage callback. But `Presets::setOnChange` currently takes a single function pointer.

To support two listeners without a list, change `Presets::setOnChange` semantics in `src/Presets.cpp` to dispatch a fixed pair of callbacks:

```cpp
namespace {
  OnChangeCb g_onChangePrimary = nullptr;   // WebSocketHub
  OnChangeCb g_onChangeSecondary = nullptr; // MQTT publish
  void emitChange() {
    if (g_onChangePrimary)   g_onChangePrimary();
    if (g_onChangeSecondary) g_onChangeSecondary();
  }
}

void setOnChange(OnChangeCb cb)          { g_onChangePrimary = cb; }
void setOnChangeSecondary(OnChangeCb cb) { g_onChangeSecondary = cb; }
```

And in `include/Presets.h`:

```cpp
void setOnChangeSecondary(OnChangeCb cb);
```

Then in `src/Setup.cpp`, after `Presets::begin()`:

```cpp
Presets::setOnChangeSecondary(publishActivePreset);
```

Also publish once on first MQTT connect — find the existing `onMqttConnect` handler in `src/MqttBridge.cpp` (where HA discovery configs are fired) and add `publishActivePreset();` to that callback.

- [ ] **Step 5: Verify firmware builds**

Run: `pio run -e OTA_upload`
Expected: `[SUCCESS]`.

- [ ] **Step 6: Commit**

```bash
git add include/MqttBridge.h include/Presets.h src/HaDiscovery.cpp src/MqttBridge.cpp src/MqttPublish.cpp src/Presets.cpp src/Setup.cpp
git commit -m "$(cat <<'EOF'
sp8: MQTT — publish active_preset name + HA discovery config

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 13: Frontend types — `SchedulePayload` / `PresetSlot`

**Files:**
- Modify: `web/src/stores/state.ts`

- [ ] **Step 1: Add the new types and extend `PoolState`**

Add at the top of `web/src/stores/state.ts` (above the existing `PoolState` interface):

```ts
export type PresetType = 'manual' | 'auto_temp';

export interface PresetWindow {
  start: number;       // minutes-of-day, 0..1439
  end: number;         // minutes-of-day, 0..1439
  enabled: boolean;
}

export interface AutoTempBounds {
  startMinHour: number;     // 0..23
  stopMaxHour: number;      // 0..23
  centerHour: number;       // 0..23
}

export interface PresetSlot {
  slot: number;
  name: string;
  type: PresetType;
  windows: PresetWindow[];          // always length 4
  auto: AutoTempBounds | null;      // null when type === 'manual'
}

export interface SchedulePayload {
  active_slot: number;
  presets: PresetSlot[];            // always length 5
}
```

In the existing `PoolState` interface, add the field:

```ts
  schedule: SchedulePayload;
```

- [ ] **Step 2: Run the frontend type-check**

Run: `cd web && npx tsc --noEmit`
Expected: clean (no output). Other files don't yet read `state.schedule`, so adding the field doesn't break anything.

- [ ] **Step 3: Commit**

```bash
git add web/src/stores/state.ts
git commit -m "$(cat <<'EOF'
sp8: state.ts — add SchedulePayload + PresetSlot types

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 14: TimePicker component

**Files:**
- Create: `web/src/components/TimePicker.tsx`

- [ ] **Step 1: Create the component**

```tsx
import { useEffect, useRef, useState } from 'preact/hooks';

interface TimePickerProps {
  /** Minutes-of-day, 0..1439. */
  value: number;
  onChange: (minutes: number) => void;
  disabled?: boolean;
}

function pad(n: number): string {
  return n < 10 ? `0${n}` : String(n);
}

export function TimePicker({ value, onChange, disabled }: TimePickerProps) {
  const [text, setText] = useState(`${pad(Math.floor(value / 60))}:${pad(value % 60)}`);
  const lastEmitted = useRef(value);

  useEffect(() => {
    if (value !== lastEmitted.current) {
      setText(`${pad(Math.floor(value / 60))}:${pad(value % 60)}`);
      lastEmitted.current = value;
    }
  }, [value]);

  const commit = (raw: string) => {
    const m = /^(\d{1,2}):(\d{2})$/.exec(raw.trim());
    if (!m) {
      setText(`${pad(Math.floor(value / 60))}:${pad(value % 60)}`);
      return;
    }
    const h = Math.min(23, Math.max(0, parseInt(m[1], 10)));
    const min = Math.min(59, Math.max(0, parseInt(m[2], 10)));
    const total = h * 60 + min;
    setText(`${pad(h)}:${pad(min)}`);
    if (total !== lastEmitted.current) {
      lastEmitted.current = total;
      onChange(total);
    }
  };

  return (
    <input
      type="text"
      inputMode="numeric"
      pattern="[0-9]{1,2}:[0-9]{2}"
      class="bg-slate-900/40 border border-aqua-border rounded-md px-2 py-1 w-20 text-center font-mono text-sm focus:outline-none focus:ring-1 focus:ring-aqua-primary disabled:opacity-40"
      value={text}
      disabled={disabled}
      onInput={e => setText((e.target as HTMLInputElement).value)}
      onBlur={e => commit((e.target as HTMLInputElement).value)}
      onKeyDown={e => { if (e.key === 'Enter') (e.target as HTMLInputElement).blur(); }}
    />
  );
}
```

- [ ] **Step 2: Run the type-check**

Run: `cd web && npx tsc --noEmit`
Expected: clean.

- [ ] **Step 3: Commit**

```bash
git add web/src/components/TimePicker.tsx
git commit -m "$(cat <<'EOF'
sp8: TimePicker component (HH:MM minute-granular input)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 15: PresetCard component

**Files:**
- Create: `web/src/components/PresetCard.tsx`

- [ ] **Step 1: Create the component**

```tsx
import type { PresetSlot } from '../stores/state';

interface PresetCardProps {
  preset: PresetSlot;
  active: boolean;
  onActivate: () => void;
  onEdit: () => void;
}

function fmt(min: number): string {
  const h = Math.floor(min / 60);
  const m = min % 60;
  return `${h < 10 ? '0' : ''}${h}:${m < 10 ? '0' : ''}${m}`;
}

export function PresetCard({ preset, active, onActivate, onEdit }: PresetCardProps) {
  const enabled = preset.windows.filter(w => w.enabled);
  const summary = enabled.length === 0
    ? 'no windows'
    : enabled.length === 1
      ? `${fmt(enabled[0].start)} – ${fmt(enabled[0].end)}`
      : `${enabled.length} windows`;

  return (
    <div
      class={`rounded-lg border p-3 cursor-pointer flex flex-col min-h-[140px] transition-colors ${
        active
          ? 'border-aqua-primary bg-aqua-primary/8 shadow-[0_0_16px_-4px_rgba(34,211,238,0.5)]'
          : 'border-aqua-border bg-white/5 hover:bg-white/8'
      }`}
      onClick={onActivate}
    >
      <button
        class="absolute top-2 right-3 text-aqua-label/70 hover:text-aqua-label text-sm"
        onClick={e => { e.stopPropagation(); onEdit(); }}
        aria-label={`Edit ${preset.name}`}
      >
        ✎
      </button>

      <div class="flex items-center justify-between mb-2 pr-5">
        <span class="font-semibold text-sm">{preset.name}</span>
        <span class="text-[0.6rem] uppercase tracking-wider px-1.5 py-0.5 rounded-full bg-aqua-label/15 text-aqua-label">
          {preset.type === 'auto_temp' ? 'AUTO' : 'MANUAL'}
        </span>
      </div>

      <div class="relative h-5 bg-black/25 rounded my-auto mb-2 overflow-hidden">
        <div class="absolute inset-y-0 w-px bg-white/10" style={{ left: '25%' }} />
        <div class="absolute inset-y-0 w-px bg-white/10" style={{ left: '50%' }} />
        <div class="absolute inset-y-0 w-px bg-white/10" style={{ left: '75%' }} />
        {preset.windows.filter(w => w.enabled).map((w, i) => (
          <div
            key={i}
            class={`absolute inset-y-0.5 rounded-sm ${active ? 'bg-aqua-primary' : 'bg-aqua-primary/55'}`}
            style={{
              left: `${(w.start / 1440) * 100}%`,
              width: `${((w.end - w.start) / 1440) * 100}%`,
            }}
          />
        ))}
      </div>

      <div class="flex items-center justify-between text-[0.7rem]">
        <span class="font-mono opacity-60">{summary}</span>
        <span class={active ? 'text-aqua-primary font-semibold' : 'opacity-60'}>
          {active ? '● ACTIVE' : 'Activate'}
        </span>
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Run the type-check**

Run: `cd web && npx tsc --noEmit`
Expected: clean.

- [ ] **Step 3: Commit**

```bash
git add web/src/components/PresetCard.tsx
git commit -m "$(cat <<'EOF'
sp8: PresetCard component with 24-hour timeline strip

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 16: PresetEditModal component

**Files:**
- Create: `web/src/components/PresetEditModal.tsx`

- [ ] **Step 1: Create the component**

```tsx
import { useSignal } from '@preact/signals';
import type { PresetSlot, PresetType, PresetWindow } from '../stores/state';
import { TimePicker } from './TimePicker';

interface PresetEditModalProps {
  initial: PresetSlot;
  onSave: (next: PresetSlot) => void;
  onDelete: () => void;
  onClose: () => void;
}

const EMPTY_WINDOW: PresetWindow = { start: 0, end: 0, enabled: false };

export function PresetEditModal({ initial, onSave, onDelete, onClose }: PresetEditModalProps) {
  const name    = useSignal(initial.name);
  const type    = useSignal<PresetType>(initial.type);
  const windows = useSignal<PresetWindow[]>(
    [0, 1, 2, 3].map(i => initial.windows[i] ?? EMPTY_WINDOW)
  );
  const startMinHour = useSignal(initial.auto?.startMinHour ?? 8);
  const stopMaxHour  = useSignal(initial.auto?.stopMaxHour ?? 22);
  const centerHour   = useSignal(initial.auto?.centerHour ?? 15);

  const setWindow = (i: number, patch: Partial<PresetWindow>) => {
    const next = windows.value.slice();
    next[i] = { ...next[i], ...patch };
    windows.value = next;
  };

  const valid = (() => {
    if (!name.value.trim()) return false;
    if (name.value.length > 15) return false;
    if (type.value === 'auto_temp') {
      if (startMinHour.value >= stopMaxHour.value) return false;
      if (centerHour.value < startMinHour.value || centerHour.value > stopMaxHour.value) return false;
    } else {
      for (const w of windows.value) {
        if (!w.enabled) continue;
        if (w.start < 0 || w.start > 1439 || w.end < 0 || w.end > 1439) return false;
        if (w.start > w.end) return false;
      }
    }
    return true;
  })();

  const submit = () => {
    if (!valid) return;
    onSave({
      slot: initial.slot,
      name: name.value.trim(),
      type: type.value,
      windows: windows.value,
      auto: type.value === 'auto_temp'
        ? { startMinHour: startMinHour.value, stopMaxHour: stopMaxHour.value, centerHour: centerHour.value }
        : null,
    });
  };

  return (
    <div class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/50" onClick={onClose}>
      <div
        class="glass-elev max-w-md w-full p-5 space-y-4 rounded-lg border border-aqua-border-elev"
        onClick={e => e.stopPropagation()}
      >
        <h3 class="text-lg font-bold">Edit "{initial.name}"</h3>

        <label class="block">
          <div class="label-caps mb-1">Name</div>
          <input type="text" value={name.value} maxLength={15}
                 onInput={e => name.value = (e.target as HTMLInputElement).value}
                 class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
        </label>

        <div>
          <div class="label-caps mb-1">Type</div>
          <div class="flex gap-2">
            <button
              class={`flex-1 px-3 py-2 rounded-md text-sm border ${type.value === 'auto_temp'
                ? 'border-aqua-primary bg-aqua-primary/10 text-aqua-primary'
                : 'border-aqua-border bg-black/20'}`}
              onClick={() => type.value = 'auto_temp'}>Auto-temp</button>
            <button
              class={`flex-1 px-3 py-2 rounded-md text-sm border ${type.value === 'manual'
                ? 'border-aqua-primary bg-aqua-primary/10 text-aqua-primary'
                : 'border-aqua-border bg-black/20'}`}
              onClick={() => type.value = 'manual'}>Manual</button>
          </div>
        </div>

        {type.value === 'auto_temp' ? (
          <div class="bg-aqua-primary/8 border-l-2 border-aqua-primary p-3 rounded grid grid-cols-3 gap-2">
            <NumField label="Earliest start" value={startMinHour.value} min={0} max={23}
                      onChange={v => startMinHour.value = v} />
            <NumField label="Latest stop"    value={stopMaxHour.value}  min={1} max={23}
                      onChange={v => stopMaxHour.value = v} />
            <NumField label="Center hour"    value={centerHour.value}   min={0} max={23}
                      onChange={v => centerHour.value = v} />
          </div>
        ) : (
          <div class="bg-black/20 rounded p-3 space-y-2">
            {windows.value.map((w, i) => (
              <div key={i} class="flex items-center gap-3 text-sm">
                <input type="checkbox" checked={w.enabled}
                       onChange={e => setWindow(i, { enabled: (e.target as HTMLInputElement).checked })} />
                <TimePicker value={w.start} disabled={!w.enabled}
                            onChange={v => setWindow(i, { start: v })} />
                <span>—</span>
                <TimePicker value={w.end}   disabled={!w.enabled}
                            onChange={v => setWindow(i, { end: v })} />
              </div>
            ))}
          </div>
        )}

        <div class="flex justify-between items-center pt-2">
          <button class="text-xs px-3 py-1.5 rounded-md bg-aqua-alarm/15 border border-rose-500/40 text-rose-200"
                  onClick={onDelete}>Delete slot</button>
          <div class="flex gap-2">
            <button class="text-sm px-3 py-1.5 rounded-md bg-white/10 border border-aqua-border"
                    onClick={onClose}>Cancel</button>
            <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold disabled:opacity-40"
                    disabled={!valid} onClick={submit}>Save</button>
          </div>
        </div>
      </div>
    </div>
  );
}

interface NumFieldProps {
  label: string;
  value: number;
  min: number;
  max: number;
  onChange: (v: number) => void;
}

function NumField({ label, value, min, max, onChange }: NumFieldProps) {
  return (
    <label class="block text-xs">
      <div class="opacity-70 mb-1">{label}</div>
      <input type="number" min={min} max={max} value={value}
             onInput={e => onChange(parseInt((e.target as HTMLInputElement).value, 10) || 0)}
             class="w-full bg-black/30 border border-aqua-border rounded px-2 py-1 text-sm font-mono text-center" />
    </label>
  );
}
```

- [ ] **Step 2: Run the type-check**

Run: `cd web && npx tsc --noEmit`
Expected: clean.

- [ ] **Step 3: Commit**

```bash
git add web/src/components/PresetEditModal.tsx
git commit -m "$(cat <<'EOF'
sp8: PresetEditModal component (manual + auto-temp variants)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 17: Rewrite Schedule.tsx as the preset manager

**Files:**
- Modify: `web/src/screens/Schedule.tsx`

- [ ] **Step 1: Replace the file contents**

Overwrite `web/src/screens/Schedule.tsx` with:

```tsx
import { useComputed, useSignal } from '@preact/signals';
import { poolWs } from '../lib/ws';
import { randomId } from '../lib/ids';
import { poolState } from '../stores/state';
import type { PresetSlot } from '../stores/state';
import { PresetCard } from '../components/PresetCard';
import { PresetEditModal } from '../components/PresetEditModal';
import { SectionTabs, TABS_TUNE } from '../components/SectionTabs';

function sendCmd(payload: object) {
  poolWs.send({ type: 'cmd', id: randomId(), payload: JSON.stringify(payload) });
}

export function Schedule() {
  const editing = useSignal<PresetSlot | null>(null);
  const schedule = useComputed(() => poolState.value?.schedule ?? null);
  const delayPid = useSignal('30');

  if (!schedule.value) {
    return <div class="glass p-6">Loading schedule…</div>;
  }
  const { active_slot, presets } = schedule.value;

  const onActivate = (slot: number) => {
    if (slot === active_slot) return;
    sendCmd({ PresetActivate: { slot } });
  };
  const onEdit = (preset: PresetSlot) => {
    editing.value = preset;
  };
  const onSave = (next: PresetSlot) => {
    sendCmd({
      PresetSave: {
        slot: next.slot,
        name: next.name,
        presetType: next.type,
        windows: next.windows,
        auto: next.auto,
      },
    });
    editing.value = null;
  };
  const onDelete = (slot: number) => {
    sendCmd({ PresetDelete: { slot } });
    editing.value = null;
  };

  return (
    <div class="space-y-4 max-w-3xl">
      <SectionTabs current="/tune/schedule" tabs={TABS_TUNE} />
      <h1 class="text-xl font-bold">Schedule</h1>
      <p class="text-sm opacity-70">Choose an active preset. Edit any preset to adjust windows.</p>

      <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-5 gap-3">
        {presets.map(p => (
          <div key={p.slot} class="relative">
            <PresetCard
              preset={p}
              active={p.slot === active_slot}
              onActivate={() => onActivate(p.slot)}
              onEdit={() => onEdit(p)}
            />
          </div>
        ))}
      </div>

      <div class="glass p-5 space-y-3">
        <h2 class="font-semibold">Global</h2>
        <p class="text-xs opacity-70">DelayPIDs is shared across all presets — minutes to wait after filtration starts before PIDs begin regulating.</p>
        <div class="flex gap-2 items-end">
          <label class="flex-1">
            <div class="label-caps mb-1">DelayPID (minutes)</div>
            <input type="number" min={0} max={59} value={delayPid.value}
                   onInput={e => (delayPid.value = (e.target as HTMLInputElement).value)}
                   class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
          </label>
          <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold"
                  onClick={() => sendCmd({ DelayPID: Number(delayPid.value) })}>Save</button>
        </div>
      </div>

      {editing.value && (
        <PresetEditModal
          initial={editing.value}
          onSave={onSave}
          onDelete={() => onDelete(editing.value!.slot)}
          onClose={() => (editing.value = null)}
        />
      )}
    </div>
  );
}
```

- [ ] **Step 2: Run the type-check**

Run: `cd web && npx tsc --noEmit`
Expected: clean.

- [ ] **Step 3: Run the full frontend build**

Run: `cd web && npm run build`
Expected: `[bundle-size] OK: <NN> KB of JS (… of 500 KB budget)`. JS budget should still pass.

- [ ] **Step 4: Commit**

```bash
git add web/src/screens/Schedule.tsx
git commit -m "$(cat <<'EOF'
sp8: Schedule screen — preset manager with edit modal

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 18: README + size record + final build

**Files:**
- Modify: `README.md`
- Create: `docs/superpowers/notes/sp8-final-size.txt`

- [ ] **Step 1: Add a feature paragraph to `README.md`**

Find the section listing features (look for the existing "What's new" or feature bullet list near the top). Add:

```markdown
- **SP8 — Driver schedule presets**: Five named presets, each with up to four configurable timeslots, replace the single hard-coded filtration window. Switch profiles ("Summer", "Vacation", "Maintenance", etc.) from the dashboard — the active preset persists across reboots and is published over MQTT for Home Assistant. The legacy `auto-temp` behavior survives as the type for slot 0; existing devices upgrade in place via a CONFIG_VERSION 51 → 52 migration that rescues `FiltrStart/Stop/StartMin/StopMax` into the new layout.
```

- [ ] **Step 2: Build firmware (production env) and capture size**

Run: `pio run -e OTA_upload 2>&1 | tail -5`
Expected: `[SUCCESS]`. Capture the `Flash:` and `RAM:` percentage lines.

Write the captured values to `docs/superpowers/notes/sp8-final-size.txt`:

```
SP8 final binary size (OTA_upload env)
======================================
RAM:    [paste from build output]
Flash:  [paste from build output]
Date:   2026-04-30
Branch: sp7-aqua-glass
```

- [ ] **Step 3: Build frontend and verify bundle budget**

Run: `cd web && npm run build`
Expected: `[bundle-size] OK: <NN> KB of JS`.

- [ ] **Step 4: Run native tests one more time**

Run: `pio test -e native -f "native/test_presets"`
Expected: `13 Tests 0 Failures 0 Ignored OK`.

- [ ] **Step 5: Commit**

```bash
git add README.md docs/superpowers/notes/sp8-final-size.txt
git commit -m "$(cat <<'EOF'
sp8: README feature paragraph + post-SP8 size record

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Acceptance checklist (manual, post-flash)

After OTA-flashing both firmware and filesystem:

- [ ] Device boots; Diagnostics shows `Free heap` stable; LedPanel renders without errors.
- [ ] Schedule screen shows 5 cards. Slot 0 named "Auto-temp" with the rescued legacy hours visible on its timeline. `● ACTIVE` shown on slot 0.
- [ ] Click a non-active card; pill on the active card moves; pump behavior reflects new windows on the next 1-Hz tick.
- [ ] Open the edit modal on slot 1, name it "Summer", add three windows, Save. State payload re-broadcasts; the card updates without a refresh.
- [ ] Activate slot 1; verify filtration matches the new windows over a real day.
- [ ] Delete slot 1 while active; device switches active to slot 0, doesn't crash.
- [ ] Power-cycle. Active slot persists; pump comes up with the right schedule.
- [ ] Wait until 00:01 the next day; for an active `auto_temp` preset, slot 0's window 0 reflects that day's water-temp-derived value.
- [ ] Calibration coefficients (visible in Calibration screen) are unchanged from before the upgrade.
- [ ] Home Assistant entity `sensor.poolmaster_active_preset` shows the active preset name and updates on activate.

---

## Spec coverage check

| Spec section | Covered by |
|---|---|
| Architecture overview | Tasks 2, 7, 8, 11, 12 |
| Data model & NVS | Tasks 2, 5, 6 |
| `CONFIG_VERSION` bump + calibration preservation | Task 6 |
| Schedule check (firmware behavior) | Task 8 |
| Daily auto-temp tick | Task 9 |
| Boot auto-start | Task 7 |
| AntiFreeze interaction | Task 8 (step 5) |
| Cmd handlers + WS queue size | Task 10 |
| WS state-payload extension | Task 11 |
| MQTT publish + HaDiscovery | Task 12 |
| Frontend types | Task 13 |
| TimePicker / PresetCard / PresetEditModal | Tasks 14, 15, 16 |
| Schedule screen rewrite | Task 17 |
| README + size record | Task 18 |
| Acceptance checklist | This document, post-flash |
