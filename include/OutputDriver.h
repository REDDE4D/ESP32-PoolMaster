#pragma once
// Output-driver abstract interface. Each of the six physical-output devices
// (FiltrationPump, PhPump, ChlPump, RobotPump, relay R0, relay R1) can be
// backed by either a GpioOutputDriver (local GPIO pin) or a MqttOutputDriver
// (MQTT-controlled external relay such as a Shelly). Drivers live in
// module-scope static storage inside Drivers.cpp for the program lifetime.

class OutputDriver {
public:
  virtual ~OutputDriver() = default;
  virtual void begin() = 0;                 // called once at boot
  virtual void set(bool on) = 0;            // command the output
  virtual bool get() const = 0;             // last-known authoritative state
  virtual const char* kindName() const = 0; // "gpio" | "mqtt"
};
