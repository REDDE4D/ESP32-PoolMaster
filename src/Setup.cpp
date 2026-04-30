#undef __STRICT_ANSI__              // work-around for Time Zone definition
#include <stdint.h>                 // std lib (types definitions)
#include <Arduino.h>                // Arduino framework
#include <esp_sntp.h>

#include "Config.h"
#include "PoolMaster.h"
#include "Settings.h"
#include "WiFiService.h"
#include "MqttBridge.h"
#include "WebServer.h"
#include "OtaService.h"
#include "Provisioning.h"
#include "HistoryBuffer.h"
#include "WebSocketHub.h"
#include "Drivers.h"
#include "LogBuffer.h"

#ifdef SIMU
bool init_simu = true;
double pHLastValue = 7.;
unsigned long pHLastTime = 0;
double OrpLastValue = 730.;
unsigned long OrpLastTime = 0;
double pHTab [3] {0.,0.,0.};
double ChlTab [3] {0.,0.,0.};
uint8_t iw = 0;
uint8_t jw = 0;
bool newpHOutput = false;
bool newChlOutput = false;
double pHCumul = 0.;
double ChlCumul = 0.;
#endif

// Firmware revision
String Firmw = FIRMW;

// Reboot / crash bookkeeping exposed on Diagnostics (see WebSocketHub::buildStateJson).
// boot_count is incremented on every boot; prev_uptime_s is the highest uptime reached
// in the last session before it ended (crash / reboot / power-off). Read on boot, then
// updated periodically from the WsBroadcast task (see Setup.cpp WsBroadcast lambda).
uint32_t g_boot_count = 0;
uint32_t g_prev_uptime_s = 0;

//Settings structure and its default values
// si pH+ : Kp=2250000.
// si pH- : Kp=2700000.
#ifdef EXT_ADS1115
StoreStruct storage =
{ 
  CONFIG_VERSION,
  0, 0, 1, 0,
  13, 8, 21, 8, 22, 20,
  2700, 2700, 30000,
  1800000, 1800000, 0, 0,
  7.3, 720.0, 1.8, 0.7, 10.0, 18.0, 3.0, -2.2183, 7.0, 431.03, 0.0, 1.31, -0.1,
  2700000.0, 0.0, 0.0, 18000.0, 0.0, 0.0, 0.0, 0.0, 28.0, 7.3, 720., 1.3,
  25.0, 60.0, 20.0, 20.0, 1.5, 1.5
};
#else
StoreStruct storage =
{ 
  CONFIG_VERSION,
  0, 0, 1, 0,
  12, 9, 21, 8, 22, 20,
  2700, 2700, 30000,
  1800000, 1800000, 0, 0,
  7.3, 650.0, 1.8, 0.7, 10.0, 18.0, 3.0, 3.61078313, -3.88020422, -966.946396, 2526.88809, 1.31, -0.1,
  2700000.0, 0.0, 0.0, 18000.0, 0.0, 0.0, 0.0, 0.0, 28.0, 7.3, 650., 1.3,
  50.0, 68.0, 20.0, 20.0, 1.5, 1.5
};
#endif

tm timeinfo;

// Various global flags
volatile bool startTasks = false;               // Signal to start loop tasks

bool AntiFreezeFiltering = false;               // Filtration anti freeze mode
bool EmergencyStopFiltPump = false;             // flag will be (re)set by double-tapp button
bool PSIError = false;                          // Water pressure OK
bool cleaning_done = false;                     // daily cleaning done   

// Queue object to store incoming JSON commands (up to 10)
QueueHandle_t queueIn;

// NVS Non Volatile SRAM (eqv. EEPROM)
Preferences nvs;      

// Instanciations of Pump and PID objects to make them global. But the constructors are then called 
// before loading of the storage struct. At run time, the attributes take the default
// values of the storage struct as they are compiled, just a few lines above, and not those which will 
// be read from NVS later. This means that the correct objects attributes must be set later in
// the setup function (fortunatelly, init methods exist).

// The four pumps of the system (instanciate the Pump class)
// In this case, all pumps start/Stop are managed by relays. pH, ORP and Robot pumps are interlocked with 
// filtration pump
Pump FiltrationPump(FILTRATION_PUMP, FILTRATION_PUMP);
Pump PhPump(PH_PUMP, PH_PUMP, NO_LEVEL, FILTRATION_PUMP, storage.pHPumpFR, storage.pHTankVol, storage.AcidFill);
Pump ChlPump(CHL_PUMP, CHL_PUMP, NO_LEVEL, FILTRATION_PUMP, storage.ChlPumpFR, storage.ChlTankVol, storage.ChlFill);
Pump RobotPump(ROBOT_PUMP, ROBOT_PUMP, NO_TANK, FILTRATION_PUMP);

// PIDs instances
//Specify the direction and initial tuning parameters
PID PhPID(&storage.PhValue, &storage.PhPIDOutput, &storage.Ph_SetPoint, storage.Ph_Kp, storage.Ph_Ki, storage.Ph_Kd, PhPID_DIRECTION);
PID OrpPID(&storage.OrpValue, &storage.OrpPIDOutput, &storage.Orp_SetPoint, storage.Orp_Kp, storage.Orp_Ki, storage.Orp_Kd, OrpPID_DIRECTION);

// Publishing tasks handles to notify them
static TaskHandle_t pubSetTaskHandle;
static TaskHandle_t pubMeasTaskHandle;

// Mutex to share access to I2C bus among two tasks: AnalogPoll and StatusLights
static SemaphoreHandle_t mutex;

// Functions prototypes
void StartTime(void);
bool readLocalTime(void);
void InitTFT(void);
void ResetTFT(void);
void PublishSettings(void);
void SetPhPID(bool);
void SetOrpPID(bool);
int  freeRam (void);
void AnalogInit(void);
void TempInit(void);
unsigned stack_hwm();
void stack_mon(UBaseType_t&);
void info();

// Functions used as Tasks
void PoolMaster(void*);
void AnalogPoll(void*);
void pHRegulation(void*);
void OrpRegulation(void*);
void getTemp(void*);
void ProcessCommand(void*);
void SettingsPublish(void*);
void MeasuresPublish(void*);
void StatusLights(void*);

// Setup
void setup()
{
  //Serial port for debug info
  Serial.begin(115200);

  // Set appropriate debug level. The level is defined in PoolMaster.h
  Debug.setDebugLevel(DEBUG_LEVEL);
  Debug.timestampOn();
  Debug.debugLabelOn();
  Debug.formatTimestampOn();

  // SP6 hotfix diagnostic — log reset reason + initial heap on every boot
  // so silent reboots can be triaged from the serial log.
  esp_reset_reason_t r = esp_reset_reason();
  const char* rs = "?";
  switch (r) {
    case ESP_RST_POWERON:   rs = "POWERON"; break;
    case ESP_RST_EXT:       rs = "EXT"; break;
    case ESP_RST_SW:        rs = "SW (e.g. ESP.restart)"; break;
    case ESP_RST_PANIC:     rs = "PANIC (crash)"; break;
    case ESP_RST_INT_WDT:   rs = "INT_WDT (interrupt watchdog)"; break;
    case ESP_RST_TASK_WDT:  rs = "TASK_WDT (task watchdog)"; break;
    case ESP_RST_WDT:       rs = "WDT (other watchdog)"; break;
    case ESP_RST_DEEPSLEEP: rs = "DEEPSLEEP"; break;
    case ESP_RST_BROWNOUT:  rs = "BROWNOUT (power dip)"; break;
    case ESP_RST_SDIO:      rs = "SDIO"; break;
    default:                rs = "UNKNOWN"; break;
  }
  Debug.print(DBG_INFO, "[Boot] reset_reason=%s heap_free=%u min_free=%u",
              rs, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());

  //get board info
  info();
  
  // Initialize Nextion TFT
  InitTFT();
  ResetTFT();

  //Read ConfigVersion. If does not match expected value, restore default values
  if(nvs.begin("PoolMaster",true))
  {
    uint8_t vers = nvs.getUChar("ConfigVersion",0);
    Debug.print(DBG_INFO,"Stored version: %d",vers);
    nvs.end();

    if (vers == CONFIG_VERSION)
    {
      Debug.print(DBG_INFO,"Same version: %d / %d. Loading settings from NVS",vers,CONFIG_VERSION);
      if(loadConfig()) Debug.print(DBG_INFO,"Config loaded"); //Restore stored values from NVS
    }
    else
    {
      Debug.print(DBG_INFO,"New version: %d / %d. Loading new default settings",vers,CONFIG_VERSION);      
      if(saveConfig()) Debug.print(DBG_INFO,"Config saved");  //First time use. Save new default values to NVS
    }

  } else {
    Debug.print(DBG_ERROR,"NVS Error");
    nvs.end();
    Debug.print(DBG_INFO,"New version: %d. First saving of settings",CONFIG_VERSION);
      if(saveConfig()) Debug.print(DBG_INFO,"Config saved");  //First time use. Save new default values to NVS

  }

  // Initialize in-memory history ring for measurement series
  HistoryBuffer::begin();

  LogBuffer::begin();

  // Boot / crash stats — increment counter and read previous session's final uptime so
  // the Diagnostics page can show "last session lasted N seconds before reset_reason X".
  {
    Preferences stats;
    if (stats.begin("bootstats", false)) {
      g_boot_count = stats.getUInt("count", 0) + 1;
      g_prev_uptime_s = stats.getUInt("lastUptime", 0);
      stats.putUInt("count", g_boot_count);
      stats.putUInt("lastUptime", 0);  // reset until the periodic save updates it
      stats.end();
    }
  }

  //SP4 — build the six output drivers from NVS config + call begin() on each.
  //GpioOutputDriver::begin() does the pinMode(OUTPUT) + initial OFF write.
  Drivers::beginAll();

  //SP4 — swap each pump's internal default driver for the configured one.
  //Default drivers constructed inside each Pump's constructor are harmless
  //placeholders; they've already run their begin() identical to the new one
  //so no extra hardware flap. After this line the pumps are driven by the
  //configured drivers (GPIO or MQTT).
  FiltrationPump.setDriver(Drivers::get("filt"));
  PhPump.setDriver       (Drivers::get("ph"));
  ChlPump.setDriver      (Drivers::get("chl"));
  RobotPump.setDriver    (Drivers::get("robot"));

  //SP4 — rewire dosing-pump interlock from digitalRead(FILTRATION_PUMP) to
  //FiltrationPump.IsRunning(). Stays correct if FiltrationPump is bound
  //to an MQTT driver (the GPIO pin is then released and would read garbage).
  //See design spec §8.1.
  PhPump.setInterlockSource (&FiltrationPump);
  ChlPump.setInterlockSource(&FiltrationPump);
  RobotPump.setInterlockSource(&FiltrationPump);

  pinMode(BUZZER, OUTPUT);

// Warning: pins used here have no pull-ups, provide external ones
  pinMode(CHL_LEVEL, INPUT);
  pinMode(PH_LEVEL, INPUT);

  // Initialize watch-dog
  esp_task_wdt_init(WDT_TIMEOUT, true);

  //Initialize MQTT
  mqttInit();

  // Provisioning check — if NVS has no WiFi SSID, start AP mode and loop.
  // Pool tasks are never created in this branch; loop() pumps DNS until the
  // wizard calls /api/finish which triggers ESP.restart().
  if (Provisioning::needed()) {
    Debug.print(DBG_INFO, "[Boot] WiFi not provisioned — entering AP mode");
    Provisioning::start(webServer);
    for (;;) {
      Provisioning::dnsTick();
      delay(10);
    }
  }

  // Initialize WiFi events management (on connect/disconnect)
  WiFi.onEvent(WiFiEvent);
  initWiFiTimer();

  bool connected = false;
  for (int attempt = 0; attempt < 3 && !connected; ++attempt) {
    Debug.print(DBG_INFO, "[Boot] WiFi attempt %d/3", attempt + 1);
    connectToWiFi();
    connected = waitForWiFiOrTimeout(15000);
    if (!connected) {
      Debug.print(DBG_WARNING, "[Boot] WiFi attempt %d timed out", attempt + 1);
      WiFi.disconnect(true);
      delay(500);
    }
  }
  if (!connected) {
    Debug.print(DBG_ERROR, "[Boot] WiFi failed after 3 attempts — entering AP mode");
    Provisioning::start(webServer);
    for (;;) {
      Provisioning::dnsTick();
      delay(10);
    }
  }

  // Config NTP, get time and set system time. This is done here in setup then every day at midnight
  // note: in timeinfo struct, months are from 0 to 11 and years are from 1900. Thus the corrections
  // to pass arguments to setTime which needs months from 1 to 12 and years from 2000...
  // DST (Daylight Saving Time) is managed automatically
  StartTime();
  // Only seed the Time library from NTP if the SNTP fetch actually
  // succeeded — otherwise timeinfo holds zeros (or stale values) and we'd
  // jump the clock to year 2000, which then breaks the schedule window
  // until the next successful midnight resync.
  if (readLocalTime())
    setTime(timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec,timeinfo.tm_mday,timeinfo.tm_mon+1,timeinfo.tm_year-100);
  else
    Debug.print(DBG_WARNING,"NTP unavailable at boot — schedule disabled until next sync");
  Debug.print(DBG_INFO,"%d/%02d/%02d %02d:%02d:%02d",year(),month(),day(),hour(),minute(),second());

  // Initialize the mDNS library.
  while (!MDNS.begin("PoolMaster")) {
    Debug.print(DBG_ERROR,"Error setting up MDNS responder!");
    delay(1000);
  }
  MDNS.addService("http", "tcp", 80);

  WebServerInit();
  OtaServiceInit(webServer);

  // SP3: attach /ws handler on the same AsyncWebServer.
  WebSocketHub::begin(webServer);

  // Start I2C for ADS1115 and status lights through PCF8574A.
  // Explicit pull-ups + 100 kHz clock — ESP32 Arduino core 2.0.x default
  // pull-up/clock behavior differs from the unpinned older version the repo
  // used pre-SP1, which caused timeouts on this bus after the platform bump.
  pinMode(I2C_SDA, INPUT_PULLUP);
  pinMode(I2C_SCL, INPUT_PULLUP);
  Wire.begin(I2C_SDA, I2C_SCL, 100000);

  // Init pH, ORP and PSI analog measurements
  AnalogInit();

  // Init Water and Air temperatures measurements
  TempInit();

  // Clear status LEDs

  Wire.beginTransmission(PCF8574ADDRESS);
  Wire.write((uint8_t)0xFF);
  Wire.endTransmission();

  // Initialize PIDs
  storage.PhPIDwindowStartTime  = millis();
  storage.OrpPIDwindowStartTime = millis();

  // Limit the PIDs output range in order to limit max. pumps runtime (safety first...)

  PhPID.SetTunings(storage.Ph_Kp, storage.Ph_Ki, storage.Ph_Kd);
  PhPID.SetControllerDirection(PhPID_DIRECTION);
  PhPID.SetSampleTime((int)storage.PhPIDWindowSize);
  PhPID.SetOutputLimits(0, storage.PhPIDWindowSize);    //Whatever happens, don't allow continuous injection of Acid for more than a PID Window

  OrpPID.SetTunings(storage.Orp_Kp, storage.Orp_Ki, storage.Orp_Kd);
  OrpPID.SetControllerDirection(OrpPID_DIRECTION);
  OrpPID.SetSampleTime((int)storage.OrpPIDWindowSize);
  OrpPID.SetOutputLimits(0, storage.OrpPIDWindowSize);  //Whatever happens, don't allow continuous injection of Chl for more than a PID Window

 // PIDs off at start
  SetPhPID (false);
  SetOrpPID(false);

  //Initialize pump instances with stored config data
  FiltrationPump.SetMaxUpTime(0);     //no runtime limit for the filtration pump

  RobotPump.SetMaxUpTime(0);          //no runtime limit for the robot pump

  PhPump.SetFlowRate(storage.pHPumpFR);
  PhPump.SetTankVolume(storage.pHTankVol);
  PhPump.SetTankFill(storage.AcidFill);
  PhPump.SetMaxUpTime(storage.PhPumpUpTimeLimit * 1000);

  ChlPump.SetFlowRate(storage.ChlPumpFR);
  ChlPump.SetTankVolume(storage.ChlTankVol);
  ChlPump.SetTankFill(storage.ChlFill);
  ChlPump.SetMaxUpTime(storage.ChlPumpUpTimeLimit * 1000);

  // Start filtration pump at power-on if within scheduled time slots -- You can choose not to do this and start pump manually
  if (storage.AutoMode && (hour() >= storage.FiltrationStart) && (hour() < storage.FiltrationStop))
    FiltrationPump.Start();
  else FiltrationPump.Stop();

  // Robot pump off at start
  RobotPump.Stop();

  // Create queue for external commands
  queueIn = xQueueCreate((UBaseType_t)QUEUE_ITEMS_NBR,(UBaseType_t)QUEUE_ITEM_SIZE);

  // Create loop tasks in the scheduler.
  //------------------------------------
  int app_cpu = xPortGetCoreID();

  Debug.print(DBG_DEBUG,"Creating loop Tasks");

  // Create I2C sharing mutex
  mutex = xSemaphoreCreateMutex();

  // Analog measurement polling task
  xTaskCreatePinnedToCore(
    AnalogPoll,
    "AnalogPoll",
    3072,
    NULL,
    1,
    nullptr,
    app_cpu
  );

  // MQTT commands processing
  xTaskCreatePinnedToCore(
    ProcessCommand,
    "ProcessCommand",
    3584,
    NULL,
    1,
    nullptr,
    app_cpu
  );

  // PoolMaster: Supervisory task
  xTaskCreatePinnedToCore(
    PoolMaster,
    "PoolMaster",
    5120,
    NULL,
    1,
    nullptr,
    app_cpu
  );

  // Temperatures measurement
  xTaskCreatePinnedToCore(
    getTemp,
    "GetTemp",
    3072,
    NULL,
    1,
    nullptr,
    app_cpu
  );
  
 // ORP regulation loop
    xTaskCreatePinnedToCore(
    OrpRegulation,
    "ORPRegulation",
    2048,
    NULL,
    1,
    nullptr,
    app_cpu
  );

  // pH regulation loop
    xTaskCreatePinnedToCore(
    pHRegulation,
    "pHRegulation",
    2048,
    NULL,
    1,
    nullptr,
    app_cpu
  );

  // Status lights display
  xTaskCreatePinnedToCore(
    StatusLights,
    "StatusLights",
    2048,
    NULL,
    1,
    nullptr,
    app_cpu
  );  

  // Measures MQTT publish 
  xTaskCreatePinnedToCore(
    MeasuresPublish,
    "MeasuresPublish",
    3072,
    NULL,
    1,
    &pubMeasTaskHandle,               // needed to notify task later
    app_cpu
  );

  // MQTT Settings publish
  xTaskCreatePinnedToCore(
    SettingsPublish,
    "SettingsPublish",
    3584,
    NULL,
    1,
    &pubSetTaskHandle,                // needed to notify task later
    app_cpu
  );

  // WebSocket broadcast — runs every 1s, ticks the hub. Also persists the
  // current uptime to NVS every 60 s so after a crash the Diagnostics
  // screen can show how long the previous session lasted.
  xTaskCreatePinnedToCore(
    [](void*) {
      uint32_t persistTick = 0;
      for(;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!startTasks) continue;
        // SP4 — keep pump internal-running flag in sync with MQTT driver
        // state feedback (no-op for GPIO drivers).
        FiltrationPump.syncStateFromDriver();
        PhPump.syncStateFromDriver();
        ChlPump.syncStateFromDriver();
        RobotPump.syncStateFromDriver();
        WebSocketHub::tick();
        if (++persistTick >= 60) {
          persistTick = 0;
          Preferences stats;
          if (stats.begin("bootstats", false)) {
            stats.putUInt("lastUptime", (uint32_t)(millis() / 1000));
            stats.end();
          }
        }
      }
    },
    "WsBroadcast",
    4096,   // was 3072; bumped for Preferences NVS writes + JSON serialization margin
    nullptr,
    1,
    nullptr,
    xPortGetCoreID());

  //display remaining RAM/Heap space.
  Debug.print(DBG_DEBUG,"[memCheck] Stack: %d bytes - Heap: %d bytes",stack_hwm(),freeRam());

  // Start loops tasks
  Debug.print(DBG_INFO,"Init done, starting loop tasks");
  startTasks = true;

  delay(1000);          // wait for tasks to start

}

//Compute free RAM
//useful to check if it does not shrink over time
int freeRam () {
  int v = xPortGetFreeHeapSize();
  return v;
}

// Get current free stack 
unsigned stack_hwm(){
  return uxTaskGetStackHighWaterMark(nullptr);
}

// Monitor free stack (display smallest value)
void stack_mon(UBaseType_t &hwm)
{
  UBaseType_t temp = uxTaskGetStackHighWaterMark(nullptr);
  if(!hwm || temp < hwm)
  {
    hwm = temp;
    Debug.print(DBG_DEBUG,"[stack_mon] %s: %d bytes",pcTaskGetTaskName(NULL), hwm);
  }  
}

// Get exclusive access of I2C bus
void lockI2C(){
  xSemaphoreTake(mutex, portMAX_DELAY);
}

// Release I2C bus access
void unlockI2C(){
  xSemaphoreGive(mutex);  
}

// Set time parameters, including DST
void StartTime(){
  configTime(0, 0,"0.pool.ntp.org","1.pool.ntp.org","2.pool.ntp.org"); // 3 possible NTP servers
  setenv("TZ","CET-1CEST,M3.5.0/2,M10.5.0/3",3);                       // configure local time with automatic DST  
  tzset();
  int retry = 0;
  const int retry_count = 15;
  while(sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count){
    Serial.print(".");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
  Serial.println("");
  Debug.print(DBG_INFO,"NTP configured");
}

bool readLocalTime(){
  if(!getLocalTime(&timeinfo,5000U)){
    Debug.print(DBG_WARNING,"Failed to obtain time");
    return false;
  }
  Serial.println(&timeinfo,"%A, %B %d %Y %H:%M:%S");
  return true;
}

// Notify PublishSettings task 
void PublishSettings()
{
  xTaskNotifyGive(pubSetTaskHandle);
}

// Notify PublishMeasures task
void PublishMeasures()
{
  xTaskNotifyGive(pubMeasTaskHandle);
}

//board info
void info(){
  esp_chip_info_t out_info;
  esp_chip_info(&out_info);
  Debug.print(DBG_INFO,"CPU frequency       : %dMHz",ESP.getCpuFreqMHz());
  Debug.print(DBG_INFO,"CPU Cores           : %d",out_info.cores);
  Debug.print(DBG_INFO,"Flash size          : %dMB",ESP.getFlashChipSize()/1000000);
  Debug.print(DBG_INFO,"Free RAM            : %d bytes",ESP.getFreeHeap());
  Debug.print(DBG_INFO,"Min heap            : %d bytes",esp_get_free_heap_size());
  Debug.print(DBG_INFO,"tskIDLE_PRIORITY    : %d",tskIDLE_PRIORITY);
  Debug.print(DBG_INFO,"confixMAX_PRIORITIES: %d",configMAX_PRIORITIES);
  Debug.print(DBG_INFO,"configTICK_RATE_HZ  : %d",configTICK_RATE_HZ);
}


// Pseudo loop, which deletes loopTask of the Arduino framework
void loop()
{
  delay(1000);
  vTaskDelete(nullptr);
}