#pragma once

void mqttInit();
void connectToMqtt();
void startMqttReconnectTimer();
void stopMqttReconnectTimer();

// Legacy error-topic publish helper used by CommandQueue.cpp and
// PoolMaster.cpp. Removed in Task 23 together with the legacy topic
// sweep.
void mqttErrorPublish(const char* payload);

// Enqueue a legacy-format JSON command string onto queueIn.
// Used by MQTT onMessage AND (later) web POST handlers.
bool enqueueCommand(const char* jsonStr);
