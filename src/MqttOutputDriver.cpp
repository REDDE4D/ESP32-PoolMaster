#include "MqttOutputDriver.h"
#include <espMqttClientAsync.h>
#include "LogBuffer.h"

// Defined in MqttBridge.cpp.
extern espMqttClientAsync mqttClient;

MqttOutputDriver::MqttOutputDriver(const MqttDriverConfig& cfg)
  : _cfg(cfg), _state(false) {}

void MqttOutputDriver::begin() {
  // No-op. Subscriptions are registered by Drivers::resubscribeStateTopics
  // from MqttBridge::onMqttConnect so they survive broker reconnects.
}

void MqttOutputDriver::set(bool on) {
  const char* topic   = _cfg.cmdTopic.c_str();
  const char* payload = on ? _cfg.payloadOn.c_str() : _cfg.payloadOff.c_str();
  uint16_t packetId = mqttClient.publish(topic, /*qos=*/1, /*retain=*/false, payload);
  if (packetId == 0) {
    LogBuffer::append(LogBuffer::L_WARN,
      "[drv] publish failed: %s payload=%s (broker disconnected or queue full)",
      topic, payload);
  }
  // Optimistically cache the commanded state. If stateTopic is set, the
  // broker's echo via onStateMessage will reconcile within ~200 ms and
  // remains authoritative. Without a state topic this cache is the only
  // source of truth, which lets the UI toggle animate immediately
  // (fire-and-forget devices otherwise stuck "off" visually).
  _state = on;
}

void MqttOutputDriver::onStateMessage(const String& payload) {
  if (payload == _cfg.stateOn)       _state = true;
  else if (payload == _cfg.stateOff) _state = false;
  // Unrecognised payloads are ignored — leave _state unchanged.
}
