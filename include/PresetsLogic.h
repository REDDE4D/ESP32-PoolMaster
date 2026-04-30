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
