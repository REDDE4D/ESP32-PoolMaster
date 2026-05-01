// MQTT state-topic publisher for HA Discovery entities.
// Publishes simple string payloads (not JSON) — HA-native conventions.

#undef __STRICT_ANSI__
#include <Arduino.h>
#include <WiFi.h>
#include "Config.h"
#include "PoolMaster.h"
#include "HaDiscovery.h"
#include "Credentials.h"
#include "HistoryBuffer.h"
#include "Drivers.h"
#include "OutputDriver.h"
#include "Presets.h"

int freeRam(void);
void stack_mon(UBaseType_t&);

static const char* ON  = "ON";
static const char* OFF = "OFF";

static void pubStr(const char* entityId, const char* val) {
  if (!mqttClient.connected()) return;
  mqttClient.publish(HaDiscovery::stateTopic(entityId).c_str(), 0, true, val);
}

static void pubFloat(const char* entityId, double val, int decimals = 2) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%.*f", decimals, val);
  pubStr(entityId, buf);
}

static void pubInt(const char* entityId, long val) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%ld", val);
  pubStr(entityId, buf);
}

static void pubBool(const char* entityId, bool v) { pubStr(entityId, v ? ON : OFF); }

void publishActivePreset() {
  if (!MQTTConnection) return;
  const Presets::PresetData& p = Presets::slot(Presets::activeSlot());
  char payload[64];
  snprintf(payload, sizeof(payload),
           "{\"slot\":%u,\"name\":\"%s\"}",
           (unsigned)Presets::activeSlot(), p.name);
  mqttClient.publish(HaDiscovery::stateTopic("active_preset").c_str(), 0, true, payload);
}

// ---- Tasks ----

// Publishes settings (setpoints, PID params, etc.) — value entities only,
// plus diagnostic firmware_version. Called when notified.
void SettingsPublish(void *pvParameters)
{
  while(!startTasks);
  vTaskDelay(DT9);
  static UBaseType_t hwm = 0;

  for(;;)
  {
    if (mqttClient.connected())
    {
      pubFloat("ph_setpoint",         storage.Ph_SetPoint, 2);
      pubFloat("orp_setpoint",        storage.Orp_SetPoint, 0);
      pubFloat("water_temp_setpoint", storage.WaterTemp_SetPoint, 1);
      pubStr  ("firmware_version",    FIRMW);
    }

    stack_mon(hwm);
    ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
  }
}

// Publishes measurements every PublishPeriod or when notified.
void MeasuresPublish(void *pvParameters)
{
  while(!startTasks);
  vTaskDelay(DT8);

  TickType_t WaitTimeOut = (TickType_t)storage.PublishPeriod / portTICK_PERIOD_MS;
  static UBaseType_t hwm = 0;

  for(;;)
  {
    ulTaskNotifyTake(pdFALSE, WaitTimeOut);

    if (mqttClient.connected())
    {
      // measurements
      pubFloat("ph",          storage.PhValue, 2);
      pubFloat("orp",         storage.OrpValue, 0);
      pubFloat("water_temp",  storage.TempValue, 1);
      pubFloat("air_temp",    storage.TempExternal, 1);
      pubFloat("pressure",     storage.PSIValue, 2);                       // bar
      pubFloat("pressure_psi", storage.PSIValue * 14.5037738f, 2);          // 1 bar = 14.5038 psi

      // runtimes
      pubInt("filt_uptime_today",      FiltrationPump.UpTime / 1000);
      pubInt("ph_pump_uptime_today",   PhPump.UpTime / 1000);
      pubInt("chl_pump_uptime_today",  ChlPump.UpTime / 1000);

      // fills
      pubFloat("acid_fill", PhPump.GetTankFill(), 0);
      pubFloat("chl_fill",  ChlPump.GetTankFill(), 0);

      // modes / switches
      pubBool("auto_mode",       storage.AutoMode);
      pubBool("winter_mode",     storage.WinterMode);
      pubBool("ph_pid",          PhPID.GetMode() == AUTOMATIC);
      pubBool("orp_pid",         OrpPID.GetMode() == AUTOMATIC);
      pubBool("heating",         false);   // TODO-SP2: wire to heat flag once moved into StoreStruct

      // pumps
      pubBool("filtration_pump", FiltrationPump.IsRunning());
      pubBool("ph_pump",         PhPump.IsRunning());
      pubBool("chl_pump",        ChlPump.IsRunning());
      pubBool("robot_pump",      RobotPump.IsRunning());
      {
        OutputDriver* dR0 = Drivers::get("r0");
        OutputDriver* dR1 = Drivers::get("r1");
        pubBool("relay_r0_projecteur", dR0 ? dR0->get() : false);
        pubBool("relay_r1_spare",      dR1 ? dR1->get() : false);
      }

      // alarms
      pubBool("pressure_alarm",    PSIError);
      pubBool("ph_pump_overtime",  PhPump.UpTimeError);
      pubBool("chl_pump_overtime", ChlPump.UpTimeError);
      pubBool("acid_tank_low",     !PhPump.TankLevel());
      pubBool("chl_tank_low",      !ChlPump.TankLevel());

      // diagnostics
      pubInt("uptime",     millis() / 1000);
      pubInt("free_heap",  ESP.getFreeHeap());
      pubInt("wifi_rssi",  WiFi.RSSI());
    }

    HistoryBuffer::append(
      storage.PhValue,
      storage.OrpValue,
      storage.TempValue,
      storage.TempExternal,
      storage.PSIValue);

    WaitTimeOut = (TickType_t)storage.PublishPeriod / portTICK_PERIOD_MS;
    stack_mon(hwm);
  }
}
