#pragma once
#include "Secrets.h"

// Firmware revisions
#define FIRMW "ESP-SP1"
#define TFT_FIRMW "TFT-2.0"

#define DEBUG_LEVEL DBG_INFO

// NVS config version — bump to force defaults on next boot
#define CONFIG_VERSION 52

#ifdef DEVT
  #define HOSTNAME "PoolMaster_Dev"
#else
  #define HOSTNAME "PoolMaster"
#endif

// ---- SMTP (optional alerting) ------------------------------------
#if !(__has_include("Secrets.h"))
  #error "Create include/Secrets.h from Secrets.h.example first."
#endif
#define EMAIL_ALERT
#define SMTP_HOST        SEED_SMTP_HOST
#define SMTP_PORT        SEED_SMTP_PORT
#define AUTHOR_EMAIL     SEED_AUTHOR_EMAIL
#define AUTHOR_LOGIN     SEED_AUTHOR_LOGIN
#define AUTHOR_PASSWORD  SEED_AUTHOR_PASSWORD
#define RECIPIENT_EMAIL  SEED_RECIPIENT_EMAIL

// ---- PID direction -----------------------------------------------
#define PhPID_DIRECTION REVERSE
#define OrpPID_DIRECTION DIRECT

// ---- GPIO pinout -------------------------------------------------
#define FILTRATION_PUMP 32
#define ROBOT_PUMP      33
#define PH_PUMP         25
#define CHL_PUMP        26
#define RELAY_R0        27
#define RELAY_R1         4
#define CHL_LEVEL       39
#define PH_LEVEL        36
#define ONE_WIRE_BUS_A  18
#define ONE_WIRE_BUS_W  19
#define I2C_SDA         21
#define I2C_SCL         22
#define PCF8574ADDRESS  0x38
#define BUZZER          2

// ---- Sensor acquisition config -----------------------------------
// EXT_ADS1115: enable only if you have a pH_Orp_Board V2 with a SECOND
// ADS1115 at addr 0x49 for differential pH/ORP readings. Leave commented
// for the single-ADC setup (pH ch0, ORP ch1, PSI ch2 on the internal
// ADS1115 at 0x48).
// #define EXT_ADS1115
#define INT_ADS1115_ADDR ADS1115ADDRESS
#define EXT_ADS1115_ADDR ADS1115ADDRESS+1

#define WDT_TIMEOUT     10

#define TEMPERATURE_RESOLUTION 12

// ---- MQTT state-publish cadence ----------------------------------
#define PUBLISHINTERVAL 30000

#ifdef DEVT
  #define POOLTOPIC_LEGACY "Home/Pool6/"
#else
  #define POOLTOPIC_LEGACY "Home/Pool/"
#endif

// Compile-shim during Phase 2-7: references to the old POOLTOPIC name in
// MqttPublish.cpp / WiFiService.cpp / MqttBridge.cpp / CommandQueue.cpp
// remain until those files are rewritten in Task 21. Remove this alias
// once that rewrite is merged.
#define POOLTOPIC POOLTOPIC_LEGACY

// ---- Robot pump timing -------------------------------------------
#define ROBOT_DELAY    60
#define ROBOT_DURATION 90

// ---- Nextion display timeout -------------------------------------
#define TFT_SLEEP 300000L   // SP7 — 5 min idle (was 60 s); see Nextion.cpp UpdateTFT() sleep block

// ---- FreeRTOS task scheduling ------------------------------------
#define PT1 125
#define PT2 500
#define PT3 500
#define PT4 (1000 / (1 << (12 - TEMPERATURE_RESOLUTION)))
#define PT5 1000
#define PT6 1000
#define PT7 3000
#define PT8 30000

#define DT2 (190 / portTICK_PERIOD_MS)
#define DT3 (310 / portTICK_PERIOD_MS)
#define DT4 (440 / portTICK_PERIOD_MS)
#define DT5 (560 / portTICK_PERIOD_MS)
#define DT6 (920 / portTICK_PERIOD_MS)
#define DT7 (100 / portTICK_PERIOD_MS)
#define DT8 (570 / portTICK_PERIOD_MS)
#define DT9 (940 / portTICK_PERIOD_MS)
