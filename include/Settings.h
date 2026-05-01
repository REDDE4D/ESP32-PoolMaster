#pragma once
#include <Arduino.h>

bool loadConfig();
bool saveConfig();

// Read calibration coefficients from NVS into in-RAM storage. Call after
// loadConfig() returns false (CONFIG_VERSION mismatch) and before
// saveConfig() so the bump doesn't reset calibrations.
void preserveCalibrationsAcrossUpgrade();

bool saveParam(const char* key, uint8_t val);
bool saveParam(const char* key, bool val);
bool saveParam(const char* key, unsigned long val);
bool saveParam(const char* key, double val);
