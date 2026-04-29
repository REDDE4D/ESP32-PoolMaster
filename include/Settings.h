#pragma once
#include <Arduino.h>

bool loadConfig();
bool saveConfig();

bool saveParam(const char* key, uint8_t val);
bool saveParam(const char* key, bool val);
bool saveParam(const char* key, unsigned long val);
bool saveParam(const char* key, double val);
