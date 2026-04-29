#pragma once
#include <ESPAsyncWebServer.h>

namespace Provisioning {
  // Returns true if NVS has no WiFi SSID (i.e., we should enter AP mode).
  bool needed();

  // Starts AP mode, DNSServer, registers wizard routes on the given server.
  // Does NOT block — caller is expected to loop calling dnsTick() until
  // provisioning is marked complete and the device reboots.
  void start(AsyncWebServer& srv);

  // Pumps the DNSServer in a tight loop (call from loop() / AP-mode task).
  void dnsTick();

  // True once the user has finished step 1 (WiFi saved).
  bool wifiSavedThisSession();

  // Register WiFi change + factory-reset endpoints on the normal-boot server
  // (gated by WebAuth::requireAdmin). Lets the user swap WiFi networks at
  // runtime without USB access. Called from WebServerInit().
  void registerRuntimeRoutes(AsyncWebServer& srv);
}
