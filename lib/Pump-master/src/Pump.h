/*
            Pump - a simple library to handle home-pool filtration and peristaltic pumps
                 (c) Loic74 <loic74650@gmail.com> 2017-2020
Features: 

- keeps track of running time
- keeps track of Tank Levels
- set max running time limit

NB: all timings are in milliseconds
*/

#ifndef PUMP_h
#define PUMP_h
#define PUMP_VERSION "1.0.1"

//Constants used in some of the functions below
#define PUMP_ON  0
#define PUMP_OFF 1
#define TANK_FULL  1
#define TANK_EMPTY 0
#define INTERLOCK_OK  0
#define INTERLOCK_NOK 1
#define NO_LEVEL 170           // Pump with tank but without level switch
#define NO_TANK 255            // Pump without tank
#define NO_INTERLOCK 255  

#define DefaultMaxUpTime 30*60*1000 //default value is 30mins

// SP4 — forward decl to avoid pulling OutputDriver.h into every Pump consumer.
class OutputDriver;

class Pump{
  public:

    Pump(uint8_t, uint8_t, uint8_t = NO_TANK, uint8_t = NO_INTERLOCK, double = 0., double = 0., double =100.);

    // SP4 — replace the internal driver at runtime. Called once from setup()
    // after Drivers::beginAll() has built the configured drivers.
    void setDriver(OutputDriver* driver);

    // SP4 — if the driver is MQTT-kind with state feedback, pull its cached
    // state into the internal running flag. No-op for GPIO drivers.
    // Called once per second by the WsBroadcast task.
    void syncStateFromDriver();

    // SP4 — interlock override. If set, Interlock() returns src->IsRunning()
    // instead of digitalRead(interlockPin). Used to keep the dosing-pump
    // interlock correct when FiltrationPump is bound to an MQTT driver and
    // its GPIO pin is no longer authoritative.
    void setInterlockSource(const Pump* src);

    void loop();
    bool Start();
    bool Stop();
    bool IsRunning();
    bool TankLevel();
    double GetTankUsage();    
    void SetTankVolume(double Volume);
    void SetFlowRate(double FlowRate);
    bool Interlock();
    void SetMaxUpTime(unsigned long Max);
    void ResetUpTime();
    void SetTankFill(double);
    double GetTankFill();

    void ClearErrors();
    
    unsigned long UpTime;
    unsigned long MaxUpTime;
    unsigned long CurrMaxUpTime;
    bool UpTimeError;
    unsigned long StartTime;
    unsigned long LastStartTime;
    unsigned long StopTime; 
    double flowrate, tankvolume, tankfill;          
  private:
     
    uint8_t pumppin;
    uint8_t isrunningsensorpin;
    uint8_t tanklevelpin;
    uint8_t interlockpin;

    // SP4 — output-driver abstraction. Built by the constructor as a default
    // GpioOutputDriver so existing file-scope globals keep booting; replaced
    // at runtime via setDriver() after Drivers::beginAll().
    OutputDriver* _driver;          // always non-null; owned elsewhere
    const Pump*   _interlockSrc;    // optional; if set, overrides digitalRead

    // SP4 — authoritative "is pump currently on?" check. Consults the
    // driver's cached state when available (driver's _state reflects the
    // last set() call for GPIO drivers and the state-topic echo for MQTT
    // drivers), else falls back to the legacy digitalRead(isrunningsensorpin)
    // pattern. Use this everywhere Pump.cpp used to digitalRead the pin.
    bool isPumpOn() const;

};
#endif
