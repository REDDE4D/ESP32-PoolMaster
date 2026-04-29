#pragma once
#include <ESPAsyncWebServer.h>

namespace WebSocketHub {

// Attaches /ws to the given server. Call once from setup() after WebServerInit.
void begin(AsyncWebServer& srv);

// Tick — called from a dedicated FreeRTOS task every 1 s. Cleans up
// zombie clients; triggers state broadcast if changed or heartbeat due.
void tick();

// Broadcast a pre-serialised JSON string to all connected clients that
// subscribed to `topic`. Safe to call from any task.
void broadcast(const char* topic, const String& json);

// Convenience — compose and broadcast a "state" message from the current
// `storage` + pump states.
void broadcastStateNow();

// Convenience — emit an alarm transition message.
void broadcastAlarm(const char* id, bool on, const char* msg);

// Internal — invoked by LogBuffer sink.
void onLogAppended(uint32_t ts, uint8_t level, const char* msg);

} // namespace WebSocketHub
