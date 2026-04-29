#pragma once
#include <stdint.h>
#include "OutputDriver.h"

// Local-GPIO driver. Owns a pin number + active-level and translates
// OutputDriver::set(true/false) to digitalWrite. Cached state mirrors
// the last commanded value (same semantics the existing firmware
// already relies on via FiltrationPump.IsRunning() etc.).
class GpioOutputDriver : public OutputDriver {
public:
  GpioOutputDriver(uint8_t pin, bool activeLow);
  void begin() override;
  void set(bool on) override;
  bool get() const override { return _state; }
  const char* kindName() const override { return "gpio"; }
private:
  uint8_t _pin;
  bool    _activeLow;
  bool    _state;
};
