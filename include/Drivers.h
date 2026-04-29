#pragma once
#include <Arduino.h>
#include "OutputDriver.h"

// Module-scope registry of the six output drivers — one per device slot.
// Drivers live for the lifetime of the program. Configuration is read
// once at boot via Credentials::drivers::load; after beginAll() the
// pointers are immutable.

namespace Drivers {

// Populate the six drivers from NVS + call begin() on each.
// Also releases any GPIO pin that was the saved default for a slot now
// bound to MQTT (pinMode(old_pin, INPUT) to tri-state the output).
void beginAll();

// Returns the driver for a slot id ("filt", "ph", "chl", "robot", "r0", "r1").
// Returns nullptr if the slot id is unknown. Callers in setup() can deref
// directly; runtime callers should null-check.
OutputDriver* get(const char* slot);

// Iterate all MQTT drivers with a non-empty state topic and subscribe.
// Called from MqttBridge::onMqttConnect so subscriptions survive broker
// reconnects. Safe to call multiple times.
void resubscribeStateTopics();

// Dispatch an inbound MQTT message to the matching driver's state topic,
// if any. Returns true if a driver consumed the message.
// Called from MqttBridge::onMqttMessage BEFORE the existing HA set-topic
// handler, so driver state-topic traffic doesn't leak into the HA path.
bool tryRouteIncoming(const char* topic, const String& payload);

// SP5 — custom slot helpers.
size_t customSlotCount();                    // returns 8 (compile-time constant)
bool   isCustomEnabled(uint8_t idx);         // idx 0..7
const char* customDisplayName(uint8_t idx);  // returns "" for disabled
void   setCustomDisplayName(uint8_t idx, const char* name);

} // namespace Drivers
