#include "ApiCommand.h"
#include <ArduinoJson.h>
#include "WebAuth.h"
#include "MqttBridge.h"   // enqueueCommand()
#include "Arduino_DebugUtils.h"

namespace ApiCommand {

static void handleBody(AsyncWebServerRequest* req,
                       uint8_t* data, size_t len,
                       size_t index, size_t total) {
  // Accumulate on the request; AsyncWebServer offers `_tempObject` for this.
  // We use a simple approach: small bodies arrive in one chunk in practice,
  // so require index == 0 && len == total.
  if (index == 0 && len == total) {
    req->_tempObject = strndup((const char*)data, len);
  }
}

static void handlePost(AsyncWebServerRequest* req) {
  if (!WebAuth::requireAdmin(req)) return;

  const char* payload = (const char*) req->_tempObject;
  if (!payload || !*payload) {
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"empty body\"}");
    return;
  }

  bool ok = enqueueCommand(payload);
  JsonDocument d;
  d["ok"]     = ok;
  d["queued"] = ok;
  if (!ok) d["error"] = "queue full";
  String out;
  serializeJson(d, out);
  req->send(ok ? 200 : 503, "application/json", out);

  free(req->_tempObject);
  req->_tempObject = nullptr;
}

void registerRoutes(AsyncWebServer& srv) {
  srv.on("/api/cmd", HTTP_POST,
         handlePost,
         nullptr,   // no upload handler
         handleBody);

  // /api/whoami — returns 200 if authed, 401 otherwise. Used by the SPA
  // to force a Basic-auth challenge without side effects.
  srv.on("/api/whoami", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;   // sends 401 if not authed
    req->send(200, "application/json", "{\"ok\":true,\"user\":\"admin\"}");
  });
}

} // namespace ApiCommand
