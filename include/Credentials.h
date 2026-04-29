#pragma once
#include <Arduino.h>

namespace Credentials {
  // Lazy-loaded: first call reads from NVS. If NVS is empty,
  // falls back once to SEED_* macros from Secrets.h and writes the seed
  // into NVS (so subsequent boots do not need Secrets.h).
  String wifiSsid();
  String wifiPsk();
  String mqttHost();
  uint16_t mqttPort();
  String mqttUser();
  String mqttPass();
  String adminPwdSha256Hex();   // empty when no password set
  String otaPwdSha256Hex();     // empty when no password set
  String timezone();            // POSIX TZ string; empty → use default

  void setWifi(const String& ssid, const String& psk);
  void setMqtt(const String& host, uint16_t port, const String& user, const String& pass);
  void setAdminPassword(const String& plaintext);
  void setOtaPassword(const String& plaintext);
  void setTimezone(const String& tz);

  void clearWifi();             // used by "Factory reset WiFi" button

  // Provisioning flag — true when WiFi creds exist and user finished wizard
  bool provisioningComplete();
  void setProvisioningComplete(bool v);

  String deviceId();            // stable lowercase-hex MAC, no colons

  // Internal helper exposed for WebAuth.cpp
  String sha256Hex(const String& input);

  // SP4 — per-slot output-driver configuration. Slot ids are the six short
  // strings "filt", "ph", "chl", "robot", "r0", "r1". kind 0 = gpio, 1 = mqtt.
  namespace drivers {
    struct DriverConfig {
      uint8_t kind        = 0;    // 0 gpio, 1 mqtt
      uint8_t pin         = 0xFF; // gpio pin number (invalid = 0xFF)
      uint8_t activeLevel = 0;    // gpio: 0 low, 1 high
      String cmdTopic;
      String payloadOn    = "on";
      String payloadOff   = "off";
      String stateTopic;
      String stateOn      = "on";
      String stateOff     = "off";
    };

    // Load returns per-slot defaults if the NVS keys are absent.
    DriverConfig load(const char* slot);
    bool save(const char* slot, const DriverConfig& cfg);

    // SP5 — per-slot configuration for user-defined custom switches.
    // Custom slots are indexed 0..7. Backing NVS keys are "drv_c0".."drv_c7".
    // enabled=false marks the slot empty; NVS blob is preserved so HA discovery
    // can publish a retracting empty payload on the next boot.
    struct CustomDriverConfig : DriverConfig {
      bool   enabled     = false;
      String displayName;      // ≤23 chars after trim, printable-ASCII (0x20–0x7E)
    };

    // idx must be in [0, 7]. Returns a default-constructed config (enabled=false)
    // if the NVS key is absent.
    CustomDriverConfig loadCustom(uint8_t idx);

    // Validates the config and writes it to NVS. Returns false on validation
    // failure or NVS write error. See src/Credentials.cpp for the validation rules.
    bool saveCustom(uint8_t idx, const CustomDriverConfig& cfg);

    // Marks the slot empty by flipping enabled=false; does NOT erase the blob.
    // Factory reset erases it via Credentials::clearAll() (existing path).
    bool clearCustom(uint8_t idx);
  } // namespace drivers
}
