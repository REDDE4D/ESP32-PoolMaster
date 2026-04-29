#include "GpioOutputDriver.h"
#include <Arduino.h>

GpioOutputDriver::GpioOutputDriver(uint8_t pin, bool activeLow)
  : _pin(pin), _activeLow(activeLow), _state(false) {}

void GpioOutputDriver::begin() {
  pinMode(_pin, OUTPUT);
  // Initial level = OFF: active-low → HIGH, active-high → LOW.
  digitalWrite(_pin, _activeLow ? HIGH : LOW);
  _state = false;
}

void GpioOutputDriver::set(bool on) {
  digitalWrite(_pin, _activeLow ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
  _state = on;
}
