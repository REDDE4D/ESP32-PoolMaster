#pragma once
#include <ESPAsyncWebServer.h>

namespace WebAuth {
  // Returns true if the request carries valid admin Basic-auth credentials.
  // If false, has already called request->send(401, ...) with the WWW-Authenticate header.
  bool requireAdmin(AsyncWebServerRequest* request);

  // Verifies plaintext password against stored admin hash. Used by wizard & OTA.
  bool checkAdminPassword(const String& plaintext);
}
