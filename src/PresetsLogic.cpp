#include "PresetsLogic.h"

namespace Presets {

bool isInActiveWindow(const PresetData& data, uint16_t now_min) {
    for (uint8_t i = 0; i < WINDOWS_PER; ++i) {
        const Window& w = data.windows[i];
        if (!w.enabled) continue;
        if (now_min >= w.start_min && now_min < w.end_min) return true;
    }
    return false;
}

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

} // namespace Presets
