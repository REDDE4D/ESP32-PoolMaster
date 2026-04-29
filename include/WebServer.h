#pragma once
#include <ESPAsyncWebServer.h>

// Global server instance — visible to OtaService.cpp and Provisioning.cpp
// so they can register their routes on the same server.
extern AsyncWebServer webServer;

// Call once from setup() AFTER WiFi is connected. Mounts LittleFS,
// registers the static file handler for `/`, and stands up core routes.
void WebServerInit();
