#pragma once

void initWiFiTimer();
void connectToWiFi();
void reconnectToWiFi();
void WiFiEvent(WiFiEvent_t event);

// Blocks up to totalMs waiting for STA to associate + get an IP.
// Returns true on success, false if timeout.
bool waitForWiFiOrTimeout(uint32_t totalMs);
