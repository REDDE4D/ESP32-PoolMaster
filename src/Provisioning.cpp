#include <WiFi.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "Provisioning.h"
#include "Credentials.h"
#include "WebAuth.h"
#include "Arduino_DebugUtils.h"

namespace Provisioning {

static DNSServer dnsServer;
static bool running = false;
static bool wifiSaved = false;

bool needed() { return Credentials::wifiSsid().isEmpty(); }
bool wifiSavedThisSession() { return wifiSaved; }

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);

static String apSsid() {
  String id = Credentials::deviceId();
  String last4 = id.substring(id.length() - 4);
  last4.toUpperCase();
  return String("PoolMaster-Setup-") + last4;
}

static void registerProbeRoutes(AsyncWebServer& srv) {
  // Force captive-portal popup on common OSes by redirecting probe URLs.
  auto redirect = [](AsyncWebServerRequest* req) {
    req->redirect("http://192.168.4.1/setup.html");
  };
  srv.on("/generate_204",              HTTP_GET, redirect);   // Android
  srv.on("/gen_204",                   HTTP_GET, redirect);   // Android (newer)
  srv.on("/hotspot-detect.html",       HTTP_GET, redirect);   // iOS / macOS
  srv.on("/library/test/success.html", HTTP_GET, redirect);   // iOS (legacy)
  srv.on("/ncsi.txt",                  HTTP_GET, redirect);   // Windows
  srv.on("/connecttest.txt",           HTTP_GET, redirect);   // Windows
  srv.on("/redirect",                  HTTP_GET, redirect);   // Microsoft Connect probe
}

static void registerWizardRoutes(AsyncWebServer& srv) {
  srv.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->redirect("/setup.html");
  });

  srv.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
    int n = WiFi.scanComplete();
    if (n == -2) { WiFi.scanNetworks(true); n = -1; }    // async-start
    JsonDocument doc;
    JsonArray arr = doc["networks"].to<JsonArray>();
    if (n >= 0) {
      for (int i = 0; i < n; ++i) {
        JsonObject net = arr.add<JsonObject>();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
        net["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      }
      WiFi.scanDelete();
    }
    doc["scanning"] = (n < 0);
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  srv.on("/api/wifi/save", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!req->hasParam("ssid", true) || !req->hasParam("psk", true)) {
      req->send(400, "text/plain", "missing ssid/psk");
      return;
    }
    String ssid = req->getParam("ssid", true)->value();
    String psk  = req->getParam("psk",  true)->value();
    if (ssid.isEmpty()) { req->send(400, "text/plain", "empty ssid"); return; }
    Credentials::setWifi(ssid, psk);
    wifiSaved = true;
    req->send(200, "application/json", "{\"ok\":true}");
  });

  srv.on("/api/admin/save", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!req->hasParam("pwd", true)) { req->send(400, "text/plain", "missing pwd"); return; }
    String pwd = req->getParam("pwd", true)->value();
    Credentials::setAdminPassword(pwd);
    Credentials::setOtaPassword(pwd);    // default: same as admin
    req->send(200, "application/json", "{\"ok\":true}");
  });

  srv.on("/api/mqtt/save", HTTP_POST, [](AsyncWebServerRequest* req) {
    String host = req->hasParam("host", true) ? req->getParam("host", true)->value() : "";
    uint16_t port = req->hasParam("port", true) ? (uint16_t) req->getParam("port", true)->value().toInt() : 1883;
    String user = req->hasParam("user", true) ? req->getParam("user", true)->value() : "";
    String pass = req->hasParam("pass", true) ? req->getParam("pass", true)->value() : "";
    Credentials::setMqtt(host, port, user, pass);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  srv.on("/api/time/save", HTTP_POST, [](AsyncWebServerRequest* req) {
    String tz = req->hasParam("tz", true) ? req->getParam("tz", true)->value() : "";
    Credentials::setTimezone(tz);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  srv.on("/api/finish", HTTP_POST, [](AsyncWebServerRequest* req) {
    Credentials::setProvisioningComplete(true);
    req->send(200, "application/json", "{\"ok\":true}");
    xTaskCreate([](void*) { vTaskDelay(pdMS_TO_TICKS(500)); ESP.restart(); },
                "reboot", 2048, nullptr, 1, nullptr);
  });
}

void start(AsyncWebServer& srv) {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
  WiFi.softAP(apSsid().c_str());
  Debug.print(DBG_INFO, "[Prov] AP up: %s  IP: %s",
              apSsid().c_str(), WiFi.softAPIP().toString().c_str());

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", AP_IP);

  if (!LittleFS.begin(true)) {
    Debug.print(DBG_ERROR, "[Prov] LittleFS mount failed");
  }

  registerProbeRoutes(srv);
  registerWizardRoutes(srv);

  // Serve setup.html + setup.css directly from LittleFS at AP-mode root.
  srv.serveStatic("/setup.html", LittleFS, "/setup.html").setCacheControl("no-store");
  srv.serveStatic("/setup.css",  LittleFS, "/setup.css").setCacheControl("no-store");

  srv.onNotFound([](AsyncWebServerRequest* req) {
    req->redirect("http://192.168.4.1/setup.html");
  });

  srv.begin();
  running = true;
}

void dnsTick() {
  if (running) dnsServer.processNextRequest();
}

// Called from WebServerInit() during normal-boot setup. Registers WiFi
// switchover + factory-reset endpoints on the running web server.
void registerRuntimeRoutes(AsyncWebServer& srv) {
  // GET /api/wifi/config → { ssid, set }. Never returns the PSK (NVS-stored
  // plaintext is read-only here; UI shows the saved SSID and a blank PSK
  // field that means "leave unchanged" if not re-entered).
  srv.on("/api/wifi/config", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;
    String ssid = Credentials::wifiSsid();
    String body = String("{\"ssid\":\"") + ssid + "\",\"set\":" + (ssid.isEmpty() ? "false" : "true") + "}";
    req->send(200, "application/json", body);
  });

  // GET /api/mqtt/config → { host, port, user, set }. Never returns pass.
  srv.on("/api/mqtt/config", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;
    String host = Credentials::mqttHost();
    String body = String("{\"host\":\"") + host
                + "\",\"port\":" + String(Credentials::mqttPort())
                + ",\"user\":\"" + Credentials::mqttUser()
                + "\",\"set\":" + (host.isEmpty() ? "false" : "true") + "}";
    req->send(200, "application/json", body);
  });

  // GET /api/admin/status → { set }. Indicates whether an admin password
  // has been configured (without revealing the hash).
  srv.on("/api/admin/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    bool set = !Credentials::adminPwdSha256Hex().isEmpty();
    String body = String("{\"set\":") + (set ? "true" : "false") + "}";
    req->send(200, "application/json", body);
  });

  // SP4 — GET /api/drivers returns the six slot configs. Admin-auth gated
  // like the other /api/*/config endpoints. No secrets — driver config
  // doesn't contain credentials. String values (topics/payloads) are emitted
  // without JSON-escaping; Task 15 adds server-side validation on save to
  // reject problematic chars (", \, control).
  srv.on("/api/drivers", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;
    static const char* const SLOTS[] = { "filt", "ph", "chl", "robot", "r0", "r1" };
    String body = "[";
    for (size_t i = 0; i < 6; ++i) {
      auto c = Credentials::drivers::load(SLOTS[i]);
      if (i) body += ",";
      body += "{\"slot\":\"";     body += SLOTS[i];       body += "\"";
      body += ",\"kind\":\"";     body += (c.kind == 1 ? "mqtt" : "gpio"); body += "\"";
      body += ",\"pin\":";        body += (int)c.pin;
      body += ",\"active_level\":\""; body += (c.activeLevel == 1 ? "high" : "low"); body += "\"";
      body += ",\"cmd_topic\":";   body += '"'; body += c.cmdTopic;    body += '"';
      body += ",\"payload_on\":";  body += '"'; body += c.payloadOn;   body += '"';
      body += ",\"payload_off\":"; body += '"'; body += c.payloadOff;  body += '"';
      body += ",\"state_topic\":"; body += '"'; body += c.stateTopic;  body += '"';
      body += ",\"state_on\":";    body += '"'; body += c.stateOn;     body += '"';
      body += ",\"state_off\":";   body += '"'; body += c.stateOff;    body += '"';
      body += "}";
    }
    body += "]";
    req->send(200, "application/json", body);
  });

  // SP4 — POST /api/drivers saves the config for one slot + reboots.
  // Form-encoded body to match the WiFi/MQTT save pattern. Admin-auth gated.
  // Slot is passed as a form field (not path suffix) because AsyncWebServer
  // does not wildcard-match path segments.
  srv.on("/api/drivers", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;

    if (!req->hasParam("slot", true)) {
      req->send(400, "text/plain", "missing slot"); return;
    }
    String slot = req->getParam("slot", true)->value();
    static const char* const SLOTS[] = { "filt", "ph", "chl", "robot", "r0", "r1" };
    bool validSlot = false;
    for (auto s : SLOTS) if (slot == s) { validSlot = true; break; }
    if (!validSlot) { req->send(400, "text/plain", "unknown slot"); return; }

    auto cfg = Credentials::drivers::load(slot.c_str());

    auto grab = [&](const char* name, String& dst) {
      if (req->hasParam(name, true)) dst = req->getParam(name, true)->value();
    };

    if (req->hasParam("kind", true)) {
      String k = req->getParam("kind", true)->value();
      cfg.kind = (k == "mqtt") ? 1 : 0;
    }
    if (req->hasParam("pin", true)) {
      int p = req->getParam("pin", true)->value().toInt();
      if (p < 0 || p > 39) { req->send(400, "text/plain", "pin out of range"); return; }
      cfg.pin = (uint8_t)p;
    }
    if (req->hasParam("active_level", true)) {
      String al = req->getParam("active_level", true)->value();
      cfg.activeLevel = (al == "high") ? 1 : 0;
    }
    grab("cmd_topic",    cfg.cmdTopic);
    grab("payload_on",   cfg.payloadOn);
    grab("payload_off",  cfg.payloadOff);
    grab("state_topic",  cfg.stateTopic);
    grab("state_on",     cfg.stateOn);
    grab("state_off",    cfg.stateOff);

    // Server-side validation beyond what the UI already does.
    if (cfg.kind == 1 && cfg.cmdTopic.isEmpty()) {
      req->send(400, "text/plain", "mqtt kind requires cmd_topic"); return;
    }
    if (cfg.payloadOn.isEmpty() || cfg.payloadOff.isEmpty()) {
      req->send(400, "text/plain", "payloads may not be empty"); return;
    }
    // Reject control chars and quotes in any string field (they'd break
    // the naively-serialised GET response in Task 14).
    auto badChars = [](const String& s) {
      for (size_t i = 0; i < s.length(); ++i) {
        char c = s.charAt(i);
        if (c == '"' || c == '\\' || c < 0x20) return true;
      }
      return false;
    };
    if (badChars(cfg.cmdTopic) || badChars(cfg.payloadOn) || badChars(cfg.payloadOff) ||
        badChars(cfg.stateTopic) || badChars(cfg.stateOn) || badChars(cfg.stateOff)) {
      req->send(400, "text/plain", "control chars / quotes not allowed"); return;
    }

    if (!Credentials::drivers::save(slot.c_str(), cfg)) {
      req->send(500, "text/plain", "NVS write failed"); return;
    }
    Debug.print(DBG_INFO, "[Prov] driver %s saved, rebooting", slot.c_str());
    req->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    xTaskCreate([](void*) { vTaskDelay(pdMS_TO_TICKS(500)); ESP.restart(); },
                "reboot", 2048, nullptr, 1, nullptr);
  });

  // SP5 — GET /api/custom-outputs returns the 8 custom-slot states as an
  // array. Same auth pattern as /api/drivers. Disabled slots return
  // {"slot":N,"enabled":false} with no other fields.
  srv.on("/api/custom-outputs", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;
    String body = "[";
    for (uint8_t i = 0; i < 8; ++i) {
      auto c = Credentials::drivers::loadCustom(i);
      if (i) body += ",";
      body += "{\"slot\":";      body += i;
      body += ",\"enabled\":";   body += (c.enabled ? "true" : "false");
      if (c.enabled) {
        auto esc = [](const String& s) {
          String out; out.reserve(s.length() + 2);
          for (size_t j = 0; j < s.length(); ++j) {
            char ch = s[j];
            if (ch == '"' || ch == '\\') { out += '\\'; out += ch; }
            else if ((uint8_t)ch < 0x20)  out += ' ';
            else                          out += ch;
          }
          return out;
        };
        body += ",\"display_name\":\"";  body += esc(c.displayName);  body += "\"";
        body += ",\"kind\":\"";          body += (c.kind == 1 ? "mqtt" : "gpio"); body += "\"";
        body += ",\"pin\":";             body += (int) c.pin;
        body += ",\"active_level\":\""; body += (c.activeLevel == 1 ? "high" : "low"); body += "\"";
        body += ",\"cmd_topic\":\"";     body += esc(c.cmdTopic);     body += "\"";
        body += ",\"payload_on\":\"";    body += esc(c.payloadOn);    body += "\"";
        body += ",\"payload_off\":\"";   body += esc(c.payloadOff);   body += "\"";
        body += ",\"state_topic\":\"";   body += esc(c.stateTopic);   body += "\"";
        body += ",\"state_on\":\"";      body += esc(c.stateOn);      body += "\"";
        body += ",\"state_off\":\"";     body += esc(c.stateOff);     body += "\"";
      }
      body += "}";
    }
    body += "]";
    req->send(200, "application/json", body);
  });

  // SP5 — POST /api/custom-outputs. Form-encoded to match /api/drivers POST.
  // Required: slot (0..7). If enabled=true, kind + driver fields must be
  // present and valid — saveCustom() runs the full validation and returns
  // false on failure. Success reboots the device (SP4 pattern).
  srv.on("/api/custom-outputs", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;

    if (!req->hasParam("slot", true)) {
      req->send(400, "text/plain", "missing slot"); return;
    }
    long slot = req->getParam("slot", true)->value().toInt();
    if (slot < 0 || slot > 7) {
      req->send(400, "text/plain", "slot out of range"); return;
    }

    Credentials::drivers::CustomDriverConfig cfg;

    auto grab = [&](const char* name, String& dst) {
      if (req->hasParam(name, true)) dst = req->getParam(name, true)->value();
    };

    cfg.enabled = req->hasParam("enabled", true)
      ? (req->getParam("enabled", true)->value() == "true")
      : false;

    grab("display_name", cfg.displayName);

    if (req->hasParam("kind", true)) {
      String k = req->getParam("kind", true)->value();
      cfg.kind = (k == "mqtt") ? 1 : 0;
    }
    if (req->hasParam("pin", true)) {
      cfg.pin = (uint8_t) req->getParam("pin", true)->value().toInt();
    }
    if (req->hasParam("active_level", true)) {
      cfg.activeLevel = (req->getParam("active_level", true)->value() == "high") ? 1 : 0;
    }
    grab("cmd_topic",   cfg.cmdTopic);
    grab("payload_on",  cfg.payloadOn);
    grab("payload_off", cfg.payloadOff);
    grab("state_topic", cfg.stateTopic);
    grab("state_on",    cfg.stateOn);
    grab("state_off",   cfg.stateOff);

    if (!Credentials::drivers::saveCustom((uint8_t) slot, cfg)) {
      req->send(400, "text/plain", "validation failed or NVS write error"); return;
    }
    Debug.print(DBG_INFO, "[Prov] custom_%ld saved (enabled=%d), rebooting",
      slot, cfg.enabled ? 1 : 0);
    req->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    // Defer the restart so the 200 response fully flushes to the client.
    // A plain delay() inside the AsyncTCP callback would block the TCP
    // stack and cause a connection reset before the body reaches the
    // browser — matches the SP4 /api/drivers pattern at line 273.
    xTaskCreate([](void*) { vTaskDelay(pdMS_TO_TICKS(500)); ESP.restart(); },
                "reboot", 2048, nullptr, 1, nullptr);
  });

  // SP5 — DELETE /api/custom-outputs. Form-encoded slot (0..7). Marks the
  // slot enabled=false via clearCustom(), then reboots so HA discovery
  // publishes an empty retained payload for that slot's config topic.
  srv.on("/api/custom-outputs", HTTP_DELETE, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;

    if (!req->hasParam("slot", true)) {
      req->send(400, "text/plain", "missing slot"); return;
    }
    long slot = req->getParam("slot", true)->value().toInt();
    if (slot < 0 || slot > 7) {
      req->send(400, "text/plain", "slot out of range"); return;
    }

    if (!Credentials::drivers::clearCustom((uint8_t) slot)) {
      req->send(500, "text/plain", "NVS write failed"); return;
    }
    Debug.print(DBG_INFO, "[Prov] custom_%ld cleared, rebooting", slot);
    req->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    xTaskCreate([](void*) { vTaskDelay(pdMS_TO_TICKS(500)); ESP.restart(); },
                "reboot", 2048, nullptr, 1, nullptr);
  });

  // POST /api/wifi/save  ssid=...&psk=...  → save + reboot to use new WiFi.
  // Admin auth gated (no-op when admin pwd not set; real gate once set).
  srv.on("/api/wifi/save", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;
    if (!req->hasParam("ssid", true)) {
      req->send(400, "text/plain", "missing ssid");
      return;
    }
    String ssid = req->getParam("ssid", true)->value();
    if (ssid.isEmpty()) { req->send(400, "text/plain", "empty ssid"); return; }
    // Blank PSK = keep existing (UI never receives the saved PSK back).
    String psk = req->hasParam("psk", true) ? req->getParam("psk", true)->value() : String();
    if (psk.isEmpty()) psk = Credentials::wifiPsk();
    Credentials::setWifi(ssid, psk);
    Debug.print(DBG_INFO, "[Prov] WiFi creds updated at runtime, rebooting");
    req->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    xTaskCreate([](void*) { vTaskDelay(pdMS_TO_TICKS(500)); ESP.restart(); },
                "reboot", 2048, nullptr, 1, nullptr);
  });

  // POST /api/wifi/factory-reset  → wipe WiFi NVS + reboot (triggers AP mode
  // via Provisioning::needed() on next boot).
  srv.on("/api/wifi/factory-reset", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;
    Credentials::clearWifi();
    Debug.print(DBG_INFO, "[Prov] Factory-reset WiFi, rebooting to AP mode");
    req->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    xTaskCreate([](void*) { vTaskDelay(pdMS_TO_TICKS(500)); ESP.restart(); },
                "reboot", 2048, nullptr, 1, nullptr);
  });

  // POST /api/mqtt/save  host=...&port=...&user=...&pass=...
  //   Saves broker credentials to NVS and reboots so MqttBridge re-initializes.
  //   Empty host disables MQTT (bridge idle).
  srv.on("/api/mqtt/save", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!WebAuth::requireAdmin(req)) return;
    String host = req->hasParam("host", true) ? req->getParam("host", true)->value() : "";
    uint16_t port = req->hasParam("port", true) ? (uint16_t) req->getParam("port", true)->value().toInt() : 1883;
    String user = req->hasParam("user", true) ? req->getParam("user", true)->value() : "";
    // Blank pass = keep existing (UI never receives the saved pass back).
    String pass = req->hasParam("pass", true) ? req->getParam("pass", true)->value() : String();
    if (pass.isEmpty()) pass = Credentials::mqttPass();
    Credentials::setMqtt(host, port, user, pass);
    Debug.print(DBG_INFO, "[Prov] MQTT broker updated at runtime, rebooting");
    req->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    xTaskCreate([](void*) { vTaskDelay(pdMS_TO_TICKS(500)); ESP.restart(); },
                "reboot", 2048, nullptr, 1, nullptr);
  });
}

} // namespace Provisioning
