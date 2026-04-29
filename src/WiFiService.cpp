#include <Arduino.h>
#include <WiFi.h>
#include "Config.h"
#include "PoolMaster.h"
#include "Credentials.h"
#include "WiFiService.h"
#include "MqttBridge.h"

static TimerHandle_t wifiReconnectTimer;

void UpdateWiFi(bool);   // defined in Nextion.cpp

void initWiFiTimer()
{
  wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0,
                                    reinterpret_cast<TimerCallbackFunction_t>(reconnectToWiFi));
}

void connectToWiFi()
{
  const String ssid = Credentials::wifiSsid();
  const String psk  = Credentials::wifiPsk();
  if (ssid.isEmpty()) {
    Debug.print(DBG_WARNING, "[WiFi] No SSID configured");
    return;
  }
  Debug.print(DBG_INFO, "[WiFi] Connecting to %s...", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(ssid.c_str(), psk.c_str());
}

void reconnectToWiFi()
{
  if (WiFi.status() != WL_CONNECTED) {
    Debug.print(DBG_INFO, "[WiFi] Reconnecting...");
    WiFi.reconnect();
  } else {
    Debug.print(DBG_INFO, "[WiFi] Spurious disconnect event ignored");
  }
}

void WiFiEvent(WiFiEvent_t event)
{
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      xTimerStop(wifiReconnectTimer, 0);
      Debug.print(DBG_INFO, "[WiFi] Connected: %s", WiFi.SSID().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      xTimerStop(wifiReconnectTimer, 0);
      Debug.print(DBG_INFO, "[WiFi] IP: %s", WiFi.localIP().toString().c_str());
      UpdateWiFi(true);
      startMqttReconnectTimer();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Debug.print(DBG_WARNING, "[WiFi] Connection lost");
      stopMqttReconnectTimer();
      xTimerStart(wifiReconnectTimer, 0);
      UpdateWiFi(false);
      break;
    default: break;
  }
}

bool waitForWiFiOrTimeout(uint32_t totalMs) {
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > totalMs) return false;
    delay(250);
  }
  return true;
}
