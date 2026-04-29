#include "LogBuffer.h"
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace LogBuffer {

// Returns unix epoch milliseconds when NTP has sync'd the RTC, else
// falls back to millis() (uptime). Pre-sync entries are obvious in the
// UI because they render as 1970-01-01T00:00:<uptime> — which at least
// tells the user the device wasn't time-synced yet.
static uint64_t wallMs() {
  time_t t = time(nullptr);
  if (t > 1700000000) {  // >= 2023-11-14: sanity threshold that NTP completed
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
  }
  return (uint64_t)millis();
}

static Entry* g_buf = nullptr;
static uint16_t g_cap = 0;
static uint16_t g_head = 0;     // next slot to write
static uint16_t g_count = 0;    // valid entries
static SemaphoreHandle_t g_mutex = nullptr;
static Sink g_sink = nullptr;

void begin(uint16_t capacity) {
  if (g_buf) return;
  g_cap = capacity;
  g_buf = (Entry*) calloc(g_cap, sizeof(Entry));
  g_mutex = xSemaphoreCreateMutex();
}

void setSink(Sink sink) {
  g_sink = sink;
}

void append(Level level, const char* fmt, ...) {
  if (!g_buf) return;
  Entry e;
  e.ts_ms = wallMs();
  e.level = level;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e.msg, sizeof(e.msg), fmt, ap);
  va_end(ap);

  xSemaphoreTake(g_mutex, portMAX_DELAY);
  g_buf[g_head] = e;
  g_head = (g_head + 1) % g_cap;
  if (g_count < g_cap) g_count++;
  xSemaphoreGive(g_mutex);

  if (g_sink) g_sink(e);
}

uint16_t snapshot(Entry* out, uint16_t max) {
  if (!g_buf || !out || max == 0) return 0;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  uint16_t n = g_count < max ? g_count : max;
  // oldest slot = (head - count + cap) % cap
  uint16_t oldest = (g_head + g_cap - g_count) % g_cap;
  for (uint16_t i = 0; i < n; ++i) {
    out[i] = g_buf[(oldest + i) % g_cap];
  }
  xSemaphoreGive(g_mutex);
  return n;
}

uint16_t size()     { return g_count; }
uint16_t capacity() { return g_cap; }

const char* levelStr(Level lvl) {
  switch (lvl) {
    case L_DEBUG: return "dbg";
    case L_INFO:  return "inf";
    case L_WARN:  return "wrn";
    case L_ERROR: return "err";
  }
  return "?";
}

} // namespace LogBuffer
