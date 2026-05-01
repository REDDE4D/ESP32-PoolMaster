#pragma once
#include "PresetsLogic.h"

namespace Presets {

  // Load state from NVS, seed defaults if first boot at this CONFIG_VERSION.
  // Call once from setup() AFTER loadConfig().
  void begin();

  // Persist current in-RAM state to NVS. Triggers WS state broadcast and
  // (if active slot changed) MQTT republish via callbacks installed below.
  void save();

  // Replace slot contents. Returns false on validation error.
  bool savePreset(uint8_t slot, const PresetData& data);

  // Switch active slot. Returns false on out-of-range slot.
  bool activate(uint8_t slot);

  // Reset slot to empty manual. If `slot` is active, switches active to 0
  // first. Returns false on out-of-range slot.
  bool clearPreset(uint8_t slot);

  // Schedule check used by the supervisory loop and boot-auto-start.
  bool isInActiveWindow(uint16_t now_min);

  // Recompute the active preset's window 0 from water temperature, if its
  // type is AutoTemp. No-op for Manual presets. Called once per day from
  // the midnight-reset block in PoolMaster.cpp.
  void tickDailyAutoTemp();

  // Read accessors (used by WebSocketHub state broadcaster + MqttPublish).
  uint8_t                   activeSlot();
  const PresetData&         slot(uint8_t i);     // i in [0, MAX_PRESETS)

  // Optional callbacks the WS / MQTT layers can install at startup so we
  // don't introduce header coupling between Presets.cpp and those modules.
  using OnChangeCb = void(*)();
  void setOnChange(OnChangeCb cb);          // fired after every successful save/activate/clear
  void setOnChangeSecondary(OnChangeCb cb); // secondary callback (MQTT publish)

}
