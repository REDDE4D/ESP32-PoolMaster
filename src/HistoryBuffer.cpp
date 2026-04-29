#include "HistoryBuffer.h"
#include <time.h>
#include <sys/time.h>

namespace HistoryBuffer {

static float    g_data[SERIES_COUNT][CAPACITY];
static uint16_t g_head  = 0;     // next slot to write (all series share index)
static uint16_t g_count = 0;
static uint64_t g_first_ms = 0;  // unix-ms of oldest sample (or millis() pre-NTP)

// Unix epoch milliseconds when NTP has sync'd, else millis() fallback.
static uint64_t wallMs() {
  time_t t = time(nullptr);
  if (t > 1700000000) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
  }
  return (uint64_t)millis();
}

void begin() {
  for (uint8_t s = 0; s < SERIES_COUNT; ++s)
    for (uint16_t i = 0; i < CAPACITY; ++i)
      g_data[s][i] = NAN;
  g_head = 0;
  g_count = 0;
  g_first_ms = 0;
}

void append(float ph, float orp, float water, float air, float pressure) {
  g_data[S_PH][g_head]         = ph;
  g_data[S_ORP][g_head]        = orp;
  g_data[S_WATER_TEMP][g_head] = water;
  g_data[S_AIR_TEMP][g_head]   = air;
  g_data[S_PRESSURE][g_head]   = pressure;

  if (g_count == 0) g_first_ms = wallMs();
  g_head = (g_head + 1) % CAPACITY;
  if (g_count < CAPACITY) {
    g_count++;
  } else {
    // Ring rolled over — advance first_ms by one step.
    g_first_ms += STEP_MS;
  }
}

uint16_t snapshot(Series s, float* out, uint64_t* t0_ms_out) {
  if (s >= SERIES_COUNT || !out) return 0;
  if (t0_ms_out) *t0_ms_out = g_first_ms;
  uint16_t oldest = (g_head + CAPACITY - g_count) % CAPACITY;
  for (uint16_t i = 0; i < g_count; ++i) {
    out[i] = g_data[s][(oldest + i) % CAPACITY];
  }
  return g_count;
}

const char* seriesName(Series s) {
  switch (s) {
    case S_PH:         return "ph";
    case S_ORP:        return "orp";
    case S_WATER_TEMP: return "water_temp";
    case S_AIR_TEMP:   return "air_temp";
    case S_PRESSURE:   return "pressure";
    default:           return "?";
  }
}

} // namespace HistoryBuffer
