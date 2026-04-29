#pragma once
#include <Arduino.h>
#include <functional>

namespace LogBuffer {

enum Level : uint8_t { L_DEBUG = 0, L_INFO = 1, L_WARN = 2, L_ERROR = 3 };

struct Entry {
  // Unix epoch milliseconds when system time is NTP-synced, else uptime
  // millis() as a fallback. 64-bit because unix-ms overflows uint32 and
  // we want monotonic ordering for either mode.
  uint64_t ts_ms;
  Level    level;
  char     msg[112]; // fixed-width to keep ring simple; longer lines truncated
};

// Sink called synchronously after each append. Used by WebSocketHub to
// broadcast log lines to subscribed clients. Optional.
using Sink = std::function<void(const Entry&)>;

// Initialize ring (caller supplies capacity in entries — typical 128 → 16 KB).
void begin(uint16_t capacity = 128);

// Append formatted log line. Truncates to fit entry.msg. Thread-safe.
void append(Level level, const char* fmt, ...);

// Install broadcast sink.
void setSink(Sink sink);

// Snapshot: copies up to `max` entries from oldest to newest into `out`.
// Returns actual number copied. Used by WS subscribe to replay history.
uint16_t snapshot(Entry* out, uint16_t max);

// Stats.
uint16_t size();      // current entry count (<= capacity)
uint16_t capacity();

// Level → short 3-letter string for UI.
const char* levelStr(Level lvl);

} // namespace LogBuffer
