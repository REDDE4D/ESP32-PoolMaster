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

// computeAutoTempWindow stub -- implemented in Task 4.
Window computeAutoTempWindow(double, double, double, uint8_t, uint8_t, uint8_t) {
    return Window{0, 0, false};
}

} // namespace Presets
