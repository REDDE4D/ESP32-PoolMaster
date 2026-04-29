#pragma once
#include <Arduino.h>

namespace HaDiscovery {
  // Build + publish all entity config messages. Retained.
  void publishAll();
  // Publish avail="online" on the LWT topic.
  void publishAvail();
  // Helper used by MqttPublish.cpp for state topics.
  String stateTopic(const char* entityId);
  String setTopic(const char* entityId);
  String availTopic();
}
