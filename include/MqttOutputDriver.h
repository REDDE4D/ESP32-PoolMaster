#pragma once
#include <Arduino.h>
#include "OutputDriver.h"

struct MqttDriverConfig {
  String cmdTopic;
  String payloadOn    = "on";
  String payloadOff   = "off";
  String stateTopic;              // empty = fire-and-forget
  String stateOn      = "on";
  String stateOff     = "off";
};

// MQTT-controlled external relay. Publishing uses the shared mqttClient
// instance defined in MqttBridge.cpp. State feedback is driven from
// Drivers::tryRouteIncoming which is called by MqttBridge::onMqttMessage;
// onStateMessage updates the cached _state bit.
class MqttOutputDriver : public OutputDriver {
public:
  explicit MqttOutputDriver(const MqttDriverConfig& cfg);
  void begin() override;                          // no-op; subscription is done by Drivers
  void set(bool on) override;                     // publish cmdTopic
  bool get() const override { return _state; }
  const char* kindName() const override { return "mqtt"; }

  const MqttDriverConfig& config() const { return _cfg; }
  void onStateMessage(const String& payload);     // called by Drivers::tryRouteIncoming
private:
  MqttDriverConfig _cfg;
  bool _state;
};
