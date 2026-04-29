#include <ArduinoOTA.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "OtaService.h"
#include "WebAuth.h"
#include "Credentials.h"
#include "Config.h"
#include "Arduino_DebugUtils.h"

static void setupArduinoOTA();
static void handleUpdatePost(AsyncWebServerRequest* req,
                             const String& /*filename*/,
                             size_t index, uint8_t* data, size_t len, bool final);

void OtaServiceInit(AsyncWebServer& srv)
{
  setupArduinoOTA();

  srv.on("/update", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;
    const char* html =
      "<!doctype html><meta charset=utf-8><title>PoolMaster OTA</title>"
      "<style>body{font:16px system-ui;margin:2rem auto;max-width:24rem}"
      "button{padding:.5rem 1rem}</style>"
      "<h1>Firmware update</h1>"
      "<form method=POST enctype=multipart/form-data action=/update>"
      "<p><label>Type: <select name=type>"
      "<option value=firmware>firmware.bin</option>"
      "<option value=littlefs>littlefs.bin</option></select></label></p>"
      "<p><input type=file name=file required></p>"
      "<p><button>Upload</button></p>"
      "<p style=color:#b45309>Device reboots on success.</p></form>";
    req->send(200, "text/html", html);
  });

  srv.on("/update", HTTP_POST,
    [](AsyncWebServerRequest* req) {
      if (!WebAuth::requireAdmin(req)) return;
      AsyncWebServerResponse* resp = req->beginResponse(
          Update.hasError() ? 400 : 200, "text/plain",
          Update.hasError() ? Update.errorString() : "Update OK — rebooting");
      resp->addHeader("Connection", "close");
      req->send(resp);
      if (!Update.hasError()) {
        // Delay reboot so the HTTP response flushes.
        xTaskCreate([](void*) { vTaskDelay(pdMS_TO_TICKS(500)); ESP.restart(); },
                    "reboot", 2048, nullptr, 1, nullptr);
      }
    },
    handleUpdatePost);

  Debug.print(DBG_INFO, "[OTA] ArduinoOTA :3232 + web /update ready");
}

static void setupArduinoOTA()
{
  ArduinoOTA.setPort(3232);
  ArduinoOTA.setHostname(HOSTNAME);

  // ArduinoOTA's auth needs plaintext OR MD5 hash. We store plaintext in
  // NVS under "ota"/"pwd_plain" (Task 13). Read it here; if absent, fall
  // back to asking for admin_pwd_plain (also stored under "admin"/"pwd_plain"
  // IF the wizard chose to; otherwise OTA is simply password-less locally —
  // which is fine for home-LAN flashing over WiFi).
  String otaPlain;
  {
    Preferences p;
    p.begin("ota", true);
    otaPlain = p.getString("pwd_plain", "");
    p.end();
  }
  if (!otaPlain.isEmpty()) {
    ArduinoOTA.setPassword(otaPlain.c_str());
  }

  ArduinoOTA.onStart([]() {
    Debug.print(DBG_INFO, "[OTA] Start: %s",
      ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "fs");
  });
  ArduinoOTA.onEnd([]() { Debug.print(DBG_INFO, "[OTA] End"); });
  ArduinoOTA.onProgress([](unsigned p, unsigned t) {
    esp_task_wdt_reset();
    Serial.printf("OTA %u%%\r", (p / (t / 100)));
  });
  ArduinoOTA.onError([](ota_error_t e) {
    Debug.print(DBG_ERROR, "[OTA] Error %u", e);
  });
  ArduinoOTA.begin();
}

static void handleUpdatePost(AsyncWebServerRequest* req,
                             const String& /*filename*/,
                             size_t index, uint8_t* data, size_t len, bool final)
{
  if (!WebAuth::requireAdmin(req)) return;

  if (index == 0) {
    String typeParam = req->hasParam("type", true) ? req->getParam("type", true)->value() : "firmware";
    int cmd = (typeParam == "littlefs") ? U_SPIFFS : U_FLASH;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
      Debug.print(DBG_ERROR, "[OTA/web] begin failed: %s", Update.errorString());
      return;
    }
    Debug.print(DBG_INFO, "[OTA/web] upload begin, cmd=%d", cmd);
  }
  if (len > 0) {
    if (Update.write(data, len) != len) {
      Debug.print(DBG_ERROR, "[OTA/web] write failed: %s", Update.errorString());
    }
    esp_task_wdt_reset();
  }
  if (final) {
    if (!Update.end(true)) {
      Debug.print(DBG_ERROR, "[OTA/web] end failed: %s", Update.errorString());
    } else {
      Debug.print(DBG_INFO, "[OTA/web] upload complete, size=%u", index + len);
    }
  }
}
