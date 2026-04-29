#include "Drivers.h"
#include "GpioOutputDriver.h"
#include "MqttOutputDriver.h"
#include "Credentials.h"
#include "LogBuffer.h"
#include <espMqttClientAsync.h>

extern espMqttClientAsync mqttClient;   // defined in MqttBridge.cpp (global scope)

namespace Drivers {

static const char* const SLOTS[] = {
  "filt", "ph", "chl", "robot", "r0", "r1",            // 0..5   fixed (SP4)
  "custom_0","custom_1","custom_2","custom_3",         // 6..13  custom (SP5)
  "custom_4","custom_5","custom_6","custom_7",
};
static constexpr size_t   FIXED_COUNT  = 6;
static constexpr size_t   CUSTOM_COUNT = 8;
static constexpr size_t   SLOT_COUNT   = 14;

static MqttOutputDriver* asMqtt(OutputDriver* d) {
  if (!d) return nullptr;
  if (strcmp(d->kindName(), "mqtt") != 0) return nullptr;
  return static_cast<MqttOutputDriver*>(d);
}

// Static storage for driver objects. We construct one of the two union
// members per slot at beginAll() time via placement new.
alignas(GpioOutputDriver) static uint8_t gpioStorage[SLOT_COUNT][sizeof(GpioOutputDriver)];
alignas(MqttOutputDriver) static uint8_t mqttStorage[SLOT_COUNT][sizeof(MqttOutputDriver)];

// The active pointer for each slot. Exactly one of gpioStorage[i] or
// mqttStorage[i] is constructed; the other is unused memory.
static OutputDriver* g_drivers[SLOT_COUNT] = {};

// SP5 — per-custom-slot metadata (index 0..7). enabled+displayName mirror the
// values in NVS. Disabled slots stay nullptr in g_drivers[] and are skipped
// everywhere (HA discovery, commands, WS broadcast).
struct CustomMeta { bool enabled; char displayName[24]; };
static CustomMeta g_customMeta[CUSTOM_COUNT] = {};

static int slotIndex(const char* slot) {
  for (size_t i = 0; i < SLOT_COUNT; ++i) if (strcmp(SLOTS[i], slot) == 0) return (int)i;
  return -1;
}

void beginAll() {
  // Fixed slots — unchanged from SP4. Loops 0..5.
  for (size_t i = 0; i < FIXED_COUNT; ++i) {
    Credentials::drivers::DriverConfig cfg = Credentials::drivers::load(SLOTS[i]);

    if (cfg.kind == 1) {
      if (cfg.pin < 40) pinMode(cfg.pin, INPUT);

      MqttDriverConfig mc;
      mc.cmdTopic    = cfg.cmdTopic;
      mc.payloadOn   = cfg.payloadOn;
      mc.payloadOff  = cfg.payloadOff;
      mc.stateTopic  = cfg.stateTopic;
      mc.stateOn     = cfg.stateOn;
      mc.stateOff    = cfg.stateOff;
      g_drivers[i] = new (mqttStorage[i]) MqttOutputDriver(mc);
      LogBuffer::append(LogBuffer::L_INFO, "[drv] %s kind=mqtt cmd=%s state=%s",
        SLOTS[i], mc.cmdTopic.c_str(), mc.stateTopic.c_str());
    } else {
      bool activeLow = (cfg.activeLevel == 0);
      g_drivers[i] = new (gpioStorage[i]) GpioOutputDriver(cfg.pin, activeLow);
      LogBuffer::append(LogBuffer::L_INFO, "[drv] %s kind=gpio pin=%u al=%d",
        SLOTS[i], (unsigned)cfg.pin, activeLow ? 1 : 0);
    }
    g_drivers[i]->begin();
  }

  // Custom slots — SP5. Loops 0..7, stored at g_drivers[FIXED_COUNT + i].
  for (size_t i = 0; i < CUSTOM_COUNT; ++i) {
    Credentials::drivers::CustomDriverConfig cfg = Credentials::drivers::loadCustom(i);
    g_customMeta[i].enabled = cfg.enabled;
    strlcpy(g_customMeta[i].displayName, cfg.displayName.c_str(),
            sizeof(CustomMeta::displayName));

    size_t s = FIXED_COUNT + i;
    if (!cfg.enabled) {
      g_drivers[s] = nullptr;
      LogBuffer::append(LogBuffer::L_INFO, "[drv] custom_%u disabled", (unsigned)i);
      continue;
    }

    if (cfg.kind == 1) {
      MqttDriverConfig mc;
      mc.cmdTopic    = cfg.cmdTopic;
      mc.payloadOn   = cfg.payloadOn;
      mc.payloadOff  = cfg.payloadOff;
      mc.stateTopic  = cfg.stateTopic;
      mc.stateOn     = cfg.stateOn;
      mc.stateOff    = cfg.stateOff;
      g_drivers[s] = new (mqttStorage[s]) MqttOutputDriver(mc);
      LogBuffer::append(LogBuffer::L_INFO, "[drv] custom_%u kind=mqtt cmd=%s name=\"%s\"",
        (unsigned)i, mc.cmdTopic.c_str(), g_customMeta[i].displayName);
    } else {
      bool activeLow = (cfg.activeLevel == 0);
      g_drivers[s] = new (gpioStorage[s]) GpioOutputDriver(cfg.pin, activeLow);
      LogBuffer::append(LogBuffer::L_INFO, "[drv] custom_%u kind=gpio pin=%u al=%d name=\"%s\"",
        (unsigned)i, (unsigned)cfg.pin, activeLow ? 1 : 0, g_customMeta[i].displayName);
    }
    g_drivers[s]->begin();
  }
}

OutputDriver* get(const char* slot) {
  int i = slotIndex(slot);
  return (i < 0) ? nullptr : g_drivers[i];
}

void resubscribeStateTopics() {
  for (size_t i = 0; i < SLOT_COUNT; ++i) {
    MqttOutputDriver* m = asMqtt(g_drivers[i]);
    if (!m) continue;
    const String& st = m->config().stateTopic;
    if (st.isEmpty()) continue;
    mqttClient.subscribe(st.c_str(), 1);
    LogBuffer::append(LogBuffer::L_INFO, "[drv] subscribed %s → %s",
      SLOTS[i], st.c_str());
  }
}

bool tryRouteIncoming(const char* topic, const String& payload) {
  for (size_t i = 0; i < SLOT_COUNT; ++i) {
    MqttOutputDriver* m = asMqtt(g_drivers[i]);
    if (!m) continue;
    const String& st = m->config().stateTopic;
    if (st.isEmpty()) continue;
    if (st == topic) {
      m->onStateMessage(payload);
      return true;
    }
  }
  return false;
}

size_t customSlotCount() { return CUSTOM_COUNT; }

bool isCustomEnabled(uint8_t idx) {
  if (idx >= CUSTOM_COUNT) return false;
  return g_customMeta[idx].enabled && g_drivers[FIXED_COUNT + idx] != nullptr;
}

const char* customDisplayName(uint8_t idx) {
  if (idx >= CUSTOM_COUNT) return "";
  return g_customMeta[idx].displayName;
}

// Accept a display-name-only update without a reboot. The driver kind/pins/
// topics still require reboot (existing SP4 pattern). Called from the REST
// save path so the Settings UI sees the new name on the very next GET
// without waiting for reboot to complete. In-memory mirror only; NVS is
// the source of truth on boot, so no divergence risk.
void setCustomDisplayName(uint8_t idx, const char* name) {
  if (idx >= CUSTOM_COUNT) return;
  strlcpy(g_customMeta[idx].displayName, name ? name : "",
          sizeof(CustomMeta::displayName));
}

} // namespace Drivers
