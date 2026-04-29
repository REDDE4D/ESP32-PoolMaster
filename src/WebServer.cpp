#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <WiFi.h>
#include "Config.h"
#include "PoolMaster.h"
#include "WebServer.h"
#include "WebAuth.h"
#include "Provisioning.h"
#include "MqttBridge.h"   // enqueueCommand()
#include "ApiCommand.h"
#include "Arduino_DebugUtils.h"

AsyncWebServer webServer(80);

void WebServerInit()
{
  if (!LittleFS.begin(true)) {
    Debug.print(DBG_ERROR, "[Web] LittleFS mount failed");
    return;
  }
  Debug.print(DBG_INFO, "[Web] LittleFS mounted: %u/%u bytes used",
              LittleFS.usedBytes(), LittleFS.totalBytes());

  // Root static handler. `index.html` is the placeholder until SP3.
  webServer.serveStatic("/", LittleFS, "/")
           .setDefaultFile("index.html")
           .setCacheControl("max-age=3600");

  webServer.on("/robots.txt", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(LittleFS, "/robots.txt", "text/plain");
  });

  // Health endpoint (public; no auth) — a tiny diag surface usable from
  // any device on the LAN for "is the box alive?" checks.
  webServer.on("/healthz", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["status"] = "ok";
    doc["uptime_s"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Current pool state snapshot — public, read-only. Primary diagnostic
  // surface until the SP3 dashboard lands. Mirrors what MqttPublish sends
  // to HA state topics, but accessible without MQTT.
  webServer.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    JsonObject meas = doc["measurements"].to<JsonObject>();
    meas["ph"]         = storage.PhValue;
    meas["orp"]        = storage.OrpValue;
    meas["water_temp"] = storage.TempValue;
    meas["air_temp"]   = storage.TempExternal;
    meas["pressure"]   = storage.PSIValue;

    JsonObject sp = doc["setpoints"].to<JsonObject>();
    sp["ph"]         = storage.Ph_SetPoint;
    sp["orp"]        = storage.Orp_SetPoint;
    sp["water_temp"] = storage.WaterTemp_SetPoint;

    JsonObject pumps = doc["pumps"].to<JsonObject>();
    pumps["filtration"]      = FiltrationPump.IsRunning();
    pumps["ph"]              = PhPump.IsRunning();
    pumps["chl"]             = ChlPump.IsRunning();
    pumps["robot"]           = RobotPump.IsRunning();
    pumps["filt_uptime_s"]   = FiltrationPump.UpTime / 1000;
    pumps["ph_uptime_s"]     = PhPump.UpTime / 1000;
    pumps["chl_uptime_s"]    = ChlPump.UpTime / 1000;

    JsonObject tanks = doc["tanks"].to<JsonObject>();
    tanks["acid_fill_pct"] = PhPump.GetTankFill();
    tanks["chl_fill_pct"]  = ChlPump.GetTankFill();

    JsonObject modes = doc["modes"].to<JsonObject>();
    modes["auto"]    = storage.AutoMode;
    modes["winter"]  = storage.WinterMode;
    modes["ph_pid"]  = (PhPID.GetMode()  == AUTOMATIC);
    modes["orp_pid"] = (OrpPID.GetMode() == AUTOMATIC);

    JsonObject alarms = doc["alarms"].to<JsonObject>();
    alarms["pressure"]         = PSIError;
    alarms["ph_pump_overtime"] = PhPump.UpTimeError;
    alarms["chl_pump_overtime"]= ChlPump.UpTimeError;
    alarms["acid_tank_low"]    = !PhPump.TankLevel();
    alarms["chl_tank_low"]     = !ChlPump.TankLevel();

    JsonObject diag = doc["diagnostics"].to<JsonObject>();
    diag["firmware"]  = FIRMW;
    diag["uptime_s"]  = millis() / 1000;
    diag["free_heap"] = ESP.getFreeHeap();
    diag["wifi_rssi"] = WiFi.RSSI();
    diag["ssid"]      = WiFi.SSID();
    diag["ip"]        = WiFi.localIP().toString();

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Calibration reset — fires the three existing Rst*Cal JSON commands into
  // the ProcessCommand queue (single source of truth for reset defaults).
  // Useful after an EXT_ADS1115 flag flip leaves NVS calibration mismatched.
  webServer.on("/api/calib/reset-defaults", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;
    bool ok1 = enqueueCommand("{\"RstpHCal\":1}");
    bool ok2 = enqueueCommand("{\"RstOrpCal\":1}");
    bool ok3 = enqueueCommand("{\"RstPSICal\":1}");
    JsonDocument doc;
    doc["ok"] = ok1 && ok2 && ok3;
    doc["queued"]["ph"]  = ok1;
    doc["queued"]["orp"] = ok2;
    doc["queued"]["psi"] = ok3;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Runtime WiFi management: POST /api/wifi/save + /api/wifi/factory-reset + /api/mqtt/save.
  Provisioning::registerRuntimeRoutes(webServer);

  // I2C bus scanner — returns list of addresses that ACK. Useful for
  // diagnosing "is my ADS1115 / PCF8574 talking?" without USB access.
  webServer.on("/api/i2c/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    JsonArray found = doc["found"].to<JsonArray>();
    int count = 0;
    for (uint8_t addr = 0x03; addr <= 0x77; ++addr) {
      Wire.beginTransmission(addr);
      uint8_t rc = Wire.endTransmission();
      if (rc == 0) {
        JsonObject o = found.add<JsonObject>();
        char hex[5]; snprintf(hex, sizeof(hex), "0x%02x", addr);
        o["addr"] = hex;
        // Annotate known PoolMaster devices
        if (addr == 0x48) o["likely"] = "ADS1115 (internal, pH/ORP/PSI)";
        else if (addr == 0x49) o["likely"] = "ADS1115 (external, pH_Orp_Board V2 ch A)";
        else if (addr == 0x4a) o["likely"] = "ADS1115 (pH_Orp_Board V2 ch B)";
        else if (addr == 0x4b) o["likely"] = "ADS1115 (pH_Orp_Board V2 ch C)";
        else if (addr == 0x20) o["likely"] = "PCF8574 (status LEDs, N variant)";
        else if (addr == 0x38) o["likely"] = "PCF8574A (status LEDs, A variant)";
        count++;
      }
    }
    doc["count"] = count;
    doc["sda"] = I2C_SDA;
    doc["scl"] = I2C_SCL;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // SPA fallback: non-/api, non-asset GETs serve /index.html so preact-iso
  // router can handle the path. /api/* stays 404. Assets are already matched
  // by serveStatic above.
  webServer.onNotFound([](AsyncWebServerRequest* req) {
    if (req->method() != HTTP_GET) {
      req->send(404, "text/plain", "Not found");
      return;
    }
    const String& url = req->url();
    if (url.startsWith("/api/") || url == "/ws" || url == "/healthz" ||
        url.startsWith("/update") || url.startsWith("/setup")) {
      req->send(404, "text/plain", "Not found");
      return;
    }
    if (LittleFS.exists("/index.html")) {
      req->send(LittleFS, "/index.html", "text/html");
    } else {
      req->send(404, "text/plain", "SPA not installed (run npm run build + uploadfs)");
    }
  });

  // SP3: /api/cmd + /api/whoami
  ApiCommand::registerRoutes(webServer);

  webServer.begin();
  Debug.print(DBG_INFO, "[Web] HTTP server listening on :80");
}
