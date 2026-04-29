#include "Credentials.h"
#include "Secrets.h"
#include "Config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <string.h>
#include <mbedtls/md.h>

namespace Credentials {

static Preferences prefs;

static String getOrSeed(const char* ns, const char* key, const char* seed) {
  prefs.begin(ns, false);
  String val = prefs.getString(key, "");
  if (val.isEmpty() && seed && seed[0] != '\0') {
    prefs.putString(key, seed);
    val = String(seed);
  }
  prefs.end();
  return val;
}

static uint16_t getU16OrSeed(const char* ns, const char* key, uint16_t seed) {
  prefs.begin(ns, false);
  uint16_t v = prefs.getUShort(key, 0);
  if (v == 0) {
    prefs.putUShort(key, seed);
    v = seed;
  }
  prefs.end();
  return v;
}

static void setString(const char* ns, const char* key, const String& v) {
  prefs.begin(ns, false);
  prefs.putString(key, v);
  prefs.end();
}

String wifiSsid() { return getOrSeed("wifi", "ssid", SEED_WIFI_SSID); }
String wifiPsk()  { return getOrSeed("wifi", "psk",  SEED_WIFI_PSK); }

String mqttHost()   { return getOrSeed("mqtt", "host", SEED_MQTT_HOST); }
uint16_t mqttPort() { return getU16OrSeed("mqtt", "port", SEED_MQTT_PORT); }
String mqttUser()   { return getOrSeed("mqtt", "user", SEED_MQTT_USER); }
String mqttPass()   { return getOrSeed("mqtt", "pass", SEED_MQTT_PASS); }

String sha256Hex(const String& input) {
  uint8_t digest[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, reinterpret_cast<const uint8_t*>(input.c_str()), input.length());
  mbedtls_md_finish(&ctx, digest);
  mbedtls_md_free(&ctx);
  char hex[65];
  for (int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
  hex[64] = '\0';
  return String(hex);
}

// Cache for the admin password hash. Read-once at first call, invalidated
// when setAdminPassword writes a new value. Without this cache, every
// authenticated request (and during a .tft upload, every body chunk) would
// open the NVS "admin" namespace — diagnosed during SP6 smoke testing as
// ~110 NVS opens/sec, starving the device.
static String   s_adminPwdCache;
static bool     s_adminPwdCached = false;

String adminPwdSha256Hex() {
  if (s_adminPwdCached) return s_adminPwdCache;
  prefs.begin("admin", true);
  String h = prefs.getString("pwd_sha256", "");
  prefs.end();
  if (h.isEmpty() && SEED_ADMIN_PWD && SEED_ADMIN_PWD[0] != '\0') {
    h = sha256Hex(String(SEED_ADMIN_PWD));
    prefs.begin("admin", false);
    prefs.putString("pwd_sha256", h);
    prefs.end();
  }
  s_adminPwdCache  = h;
  s_adminPwdCached = true;
  return h;
}

String otaPwdSha256Hex() {
  prefs.begin("ota", true);
  String h = prefs.getString("pwd_sha256", "");
  prefs.end();
  if (h.isEmpty()) h = adminPwdSha256Hex();   // fall back to admin
  return h;
}

String timezone() {
  prefs.begin("time", true);
  String tz = prefs.getString("tz", "");
  prefs.end();
  return tz;
}

void setWifi(const String& ssid, const String& psk) {
  setString("wifi", "ssid", ssid);
  setString("wifi", "psk", psk);
}

void setMqtt(const String& host, uint16_t port, const String& user, const String& pass) {
  setString("mqtt", "host", host);
  prefs.begin("mqtt", false); prefs.putUShort("port", port); prefs.end();
  setString("mqtt", "user", user);
  setString("mqtt", "pass", pass);
}

void setAdminPassword(const String& plaintext) {
  String h = sha256Hex(plaintext);
  setString("admin", "pwd_sha256", h);
  s_adminPwdCache  = h;        // keep cache coherent
  s_adminPwdCached = true;
}

void setOtaPassword(const String& plaintext) {
  String h = sha256Hex(plaintext);
  setString("ota", "pwd_sha256", h);
  setString("ota", "pwd_plain", plaintext);   // ArduinoOTA needs plaintext; acceptable for home-LAN threat model
}

void setTimezone(const String& tz) {
  setString("time", "tz", tz);
}

void clearWifi() {
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
}

bool provisioningComplete() {
  prefs.begin("prov", true);
  bool v = prefs.getBool("done", false);
  prefs.end();
  return v;
}

void setProvisioningComplete(bool v) {
  prefs.begin("prov", false);
  prefs.putBool("done", v);
  prefs.end();
}

String deviceId() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[13];
  snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

namespace drivers {

// Hardcoded per-slot default: pin from Config.h; every slot today is
// active-low (matches the PUMP_ON=0, PUMP_OFF=1 convention + current
// RELAY_R{0,1} wiring).
static DriverConfig slotDefault(const char* slot) {
  DriverConfig d;
  d.kind = 0;
  d.activeLevel = 0;
  if      (strcmp(slot, "filt")  == 0) d.pin = FILTRATION_PUMP;
  else if (strcmp(slot, "ph")    == 0) d.pin = PH_PUMP;
  else if (strcmp(slot, "chl")   == 0) d.pin = CHL_PUMP;
  else if (strcmp(slot, "robot") == 0) d.pin = ROBOT_PUMP;
  else if (strcmp(slot, "r0")    == 0) d.pin = RELAY_R0;
  else if (strcmp(slot, "r1")    == 0) d.pin = RELAY_R1;
  else                                 d.pin = 0xFF; // unknown slot
  return d;
}

static String keyOf(const char* slot, const char* suffix) {
  String k = slot;
  k += '.';
  k += suffix;
  return k;
}

DriverConfig load(const char* slot) {
  DriverConfig d = slotDefault(slot);
  Preferences nvs;
  if (!nvs.begin("drivers", true)) return d;
  d.kind        = nvs.getUChar(keyOf(slot, "kind").c_str(),  d.kind);
  d.pin         = nvs.getUChar(keyOf(slot, "pin").c_str(),   d.pin);
  d.activeLevel = nvs.getUChar(keyOf(slot, "al").c_str(),    d.activeLevel);
  d.cmdTopic    = nvs.getString(keyOf(slot, "ct").c_str(),   d.cmdTopic);
  d.payloadOn   = nvs.getString(keyOf(slot, "pn").c_str(),   d.payloadOn);
  d.payloadOff  = nvs.getString(keyOf(slot, "pf").c_str(),   d.payloadOff);
  d.stateTopic  = nvs.getString(keyOf(slot, "st").c_str(),   d.stateTopic);
  d.stateOn     = nvs.getString(keyOf(slot, "sn").c_str(),   d.stateOn);
  d.stateOff    = nvs.getString(keyOf(slot, "sf").c_str(),   d.stateOff);
  nvs.end();
  return d;
}

bool save(const char* slot, const DriverConfig& cfg) {
  Preferences nvs;
  if (!nvs.begin("drivers", false)) return false;
  size_t w = 0;
  w += nvs.putUChar(keyOf(slot, "kind").c_str(), cfg.kind);
  w += nvs.putUChar(keyOf(slot, "pin").c_str(),  cfg.pin);
  w += nvs.putUChar(keyOf(slot, "al").c_str(),   cfg.activeLevel);
  w += nvs.putString(keyOf(slot, "ct").c_str(),  cfg.cmdTopic);
  w += nvs.putString(keyOf(slot, "pn").c_str(),  cfg.payloadOn);
  w += nvs.putString(keyOf(slot, "pf").c_str(),  cfg.payloadOff);
  w += nvs.putString(keyOf(slot, "st").c_str(),  cfg.stateTopic);
  w += nvs.putString(keyOf(slot, "sn").c_str(),  cfg.stateOn);
  w += nvs.putString(keyOf(slot, "sf").c_str(),  cfg.stateOff);
  nvs.end();
  return w > 0;
}

// SP5 — custom slot NVS persistence. Reuses SP4's per-field key pattern
// (keyOf + typed NVS getters/setters) to avoid pulling ArduinoJson template
// instantiations into the binary, which cost ~20 KB of flash in an earlier
// JSON-blob implementation. Slot keys are "custom_N.<suffix>" with the same
// 2-letter suffixes SP4 uses (kind/pin/al/ct/pn/pf/st/sn/sf) plus two
// SP5-only fields: "en" (enabled) and "nm" (displayName).
static String customSlotId(uint8_t idx) {
  char buf[12];
  snprintf(buf, sizeof(buf), "custom_%u", idx);
  return String(buf);
}

CustomDriverConfig loadCustom(uint8_t idx) {
  CustomDriverConfig cfg;
  if (idx >= 8) return cfg;

  String slot = customSlotId(idx);
  Preferences nvs;
  if (!nvs.begin("drivers", /*readOnly=*/true)) return cfg;

  cfg.enabled     = nvs.getUChar(keyOf(slot.c_str(), "en").c_str(), 0) != 0;
  cfg.displayName = nvs.getString(keyOf(slot.c_str(), "nm").c_str(), "");
  cfg.kind        = nvs.getUChar(keyOf(slot.c_str(), "kind").c_str(), 0);
  cfg.pin         = nvs.getUChar(keyOf(slot.c_str(), "pin").c_str(), 0xFF);
  cfg.activeLevel = nvs.getUChar(keyOf(slot.c_str(), "al").c_str(), 0);
  cfg.cmdTopic    = nvs.getString(keyOf(slot.c_str(), "ct").c_str(), "");
  cfg.payloadOn   = nvs.getString(keyOf(slot.c_str(), "pn").c_str(), "on");
  cfg.payloadOff  = nvs.getString(keyOf(slot.c_str(), "pf").c_str(), "off");
  cfg.stateTopic  = nvs.getString(keyOf(slot.c_str(), "st").c_str(), "");
  cfg.stateOn     = nvs.getString(keyOf(slot.c_str(), "sn").c_str(), "on");
  cfg.stateOff    = nvs.getString(keyOf(slot.c_str(), "sf").c_str(), "off");
  nvs.end();
  return cfg;
}

// Validation rules:
//  - idx must be 0..7
//  - displayName non-empty after trim, ≤23 chars, all bytes 0x20..0x7E
//  - If kind == gpio: pin < 40
//  - If kind == mqtt: cmdTopic non-empty, ≤63 chars, no '#'/'+', no quotes/control
//  - payloadOn/payloadOff non-empty
bool saveCustom(uint8_t idx, const CustomDriverConfig& cfg) {
  if (idx >= 8) return false;

  String name = cfg.displayName;
  name.trim();

  // displayName is only user-facing when the slot is enabled. Disabled slots
  // (including clearCustom on a never-configured slot where loadCustom
  // returned a default with empty displayName) skip the name rules so the
  // delete path always succeeds.
  if (cfg.enabled) {
    if (name.isEmpty() || name.length() > 23) return false;
    for (size_t i = 0; i < name.length(); ++i) {
      char c = name[i];
      if (c < 0x20 || c > 0x7E) return false;
    }

    if (cfg.kind == 0) {                          // gpio
      if (cfg.pin >= 40) return false;
    } else if (cfg.kind == 1) {                   // mqtt
      if (cfg.cmdTopic.isEmpty() || cfg.cmdTopic.length() > 63) return false;
      if (cfg.cmdTopic.indexOf('#') >= 0 || cfg.cmdTopic.indexOf('+') >= 0) return false;
      if (cfg.payloadOn.isEmpty() || cfg.payloadOff.isEmpty()) return false;
      auto badChars = [](const String& s) {
        for (size_t i = 0; i < s.length(); ++i) {
          char c = s[i];
          if (c == '"' || c == '\\' || (uint8_t)c < 0x20) return true;
        }
        return false;
      };
      if (badChars(cfg.cmdTopic) || badChars(cfg.payloadOn) || badChars(cfg.payloadOff) ||
          badChars(cfg.stateTopic) || badChars(cfg.stateOn) || badChars(cfg.stateOff)) {
        return false;
      }
    } else {
      return false;
    }
  }

  String slot = customSlotId(idx);
  Preferences nvs;
  if (!nvs.begin("drivers", /*readOnly=*/false)) return false;
  size_t w = 0;
  w += nvs.putUChar (keyOf(slot.c_str(), "en").c_str(),   cfg.enabled ? 1 : 0);
  w += nvs.putString(keyOf(slot.c_str(), "nm").c_str(),   name);
  w += nvs.putUChar (keyOf(slot.c_str(), "kind").c_str(), cfg.kind);
  w += nvs.putUChar (keyOf(slot.c_str(), "pin").c_str(),  cfg.pin);
  w += nvs.putUChar (keyOf(slot.c_str(), "al").c_str(),   cfg.activeLevel);
  w += nvs.putString(keyOf(slot.c_str(), "ct").c_str(),   cfg.cmdTopic);
  w += nvs.putString(keyOf(slot.c_str(), "pn").c_str(),   cfg.payloadOn);
  w += nvs.putString(keyOf(slot.c_str(), "pf").c_str(),   cfg.payloadOff);
  w += nvs.putString(keyOf(slot.c_str(), "st").c_str(),   cfg.stateTopic);
  w += nvs.putString(keyOf(slot.c_str(), "sn").c_str(),   cfg.stateOn);
  w += nvs.putString(keyOf(slot.c_str(), "sf").c_str(),   cfg.stateOff);
  nvs.end();
  return w > 0;
}

bool clearCustom(uint8_t idx) {
  // Flip enabled=false and preserve other fields so HA retract publish has
  // a friendly log line. Loading first avoids rewriting field defaults over
  // what the user had.
  CustomDriverConfig cfg = loadCustom(idx);
  cfg.enabled = false;
  return saveCustom(idx, cfg);
}

} // namespace drivers

} // namespace Credentials
