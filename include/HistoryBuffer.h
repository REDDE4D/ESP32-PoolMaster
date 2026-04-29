#pragma once
#include <Arduino.h>

namespace HistoryBuffer {

enum Series : uint8_t {
  S_PH = 0, S_ORP = 1, S_WATER_TEMP = 2, S_AIR_TEMP = 3, S_PRESSURE = 4,
  SERIES_COUNT = 5
};

constexpr uint16_t CAPACITY = 120; // 60 min @ 30 s
constexpr uint32_t STEP_MS  = 30000;

// Initialize buffer.
void begin();

// Append a new sample for each series (call once per measurement cycle).
void append(float ph, float orp, float water, float air, float pressure);

// Copy snapshot of series into `out` (up to CAPACITY floats). Returns
// number of valid points. `t0_ms_out` receives the unix-epoch-ms
// timestamp of the OLDEST point (falls back to millis() when NTP hasn't
// synced yet, see HistoryBuffer.cpp).
uint16_t snapshot(Series s, float* out, uint64_t* t0_ms_out);

// Series name for JSON serialization.
const char* seriesName(Series s);

} // namespace HistoryBuffer
