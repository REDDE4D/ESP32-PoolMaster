#pragma once
#include <ESPAsyncWebServer.h>

namespace ApiCommand {
  // Registers POST /api/cmd + GET /api/whoami on the given server.
  void registerRoutes(AsyncWebServer& srv);
}
