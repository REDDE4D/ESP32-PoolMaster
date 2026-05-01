#include "Presets.h"
#include "PoolMaster.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace Presets {

namespace {
  static constexpr const char* NS = "presets";

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

  // Hour fields are uint8_t (so 0..255 fits). Spec confines them to 0..23
  // for both Manual and AutoTemp presets — Manual ignores them, but if
  // the user later type-switches via UI without re-saving, AutoTemp would
  // see out-of-range values and produce windows past 23:59.
  if (data.startMinHour > 23 || data.stopMaxHour > 23 || data.centerHour > 23) return false;

  // Reject a name without an in-range NUL terminator. A 16-byte non-NUL
  // payload would otherwise be silently truncated by the forced NUL on
  // line below, losing the last character.
  if (memchr(data.name, '\0', NAME_MAX_LEN) == nullptr) return false;

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
  save();    // writes back without emitting (g_onChange not yet wired at this point)
}

void begin() {
  bool foundAny = loadFromNVS();
  if (!foundAny) seedDefaults();
}

} // namespace Presets
