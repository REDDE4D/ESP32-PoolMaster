#include "Arduino.h"
#include "Pump.h"
#include <string.h>
#include "../../../include/OutputDriver.h"
#include "../../../include/GpioOutputDriver.h"

//Constructor
//PumpPin is the Arduino relay output pin number to be switched to start/stop the pump
//TankLevelPin is the Arduino digital input pin number connected to the tank level switch
//Interlockpin is the Arduino digital input number connected to an "interlock". 
//If this input is LOW, pump is stopped and/or cannot start. This is used for instance to stop
//the Orp or pH pumps in case filtration pump is not running
//IsRunningSensorPin is the pin which is checked to know whether the pump is running or not. 
//It can be the same pin as "PumpPin" in case there is no sensor on the pump (pressure, current, etc) which is not as robust. 
//This option is especially useful in the case where the filtration pump is not managed by the Arduino. 
//FlowRate is the flow rate of the pump in Liters/Hour, typically 1.5 or 3.0 L/hour for peristaltic pumps for pools. This is used to compute how much of the tank we have emptied out
//TankVolume is used here to compute the percentage fill used
Pump::Pump(uint8_t PumpPin, uint8_t IsRunningSensorPin, uint8_t TankLevelPin, 
           uint8_t Interlockpin, double FlowRate, double TankVolume, double TankFill)
{
  pumppin = PumpPin;
  isrunningsensorpin = IsRunningSensorPin;
  tanklevelpin = TankLevelPin;
  interlockpin = Interlockpin;
  flowrate = FlowRate; //in Liters per hour
  tankvolume = TankVolume; //in Liters
  tankfill = TankFill; // in percent
  StartTime = 0;
  LastStartTime = 0;
  StopTime = 0;
  UpTime = 0;        
  UpTimeError = 0;
  MaxUpTime = DefaultMaxUpTime;
  CurrMaxUpTime = MaxUpTime;

  // SP4 — build a default internal driver from the legacy pin parameter.
  // pumppin is wired active-low on all six existing devices. This default
  // is replaced by setDriver() from setup() after Drivers::beginAll().
  _driver = new GpioOutputDriver(pumppin, /*activeLow=*/true);
  _interlockSrc = nullptr;
}

//Call this in the main loop, for every loop, as often as possible
void Pump::loop()
{
  if(isPumpOn())
  {
    UpTime += millis() - StartTime;
    StartTime = millis();
  }

  if((CurrMaxUpTime > 0) && (UpTime >= CurrMaxUpTime))
  {
    Stop();
    UpTimeError = true;
  }

  if(!this->Pump::TankLevel()) this->Pump::Stop();

  if(!Interlock())
    Stop();
}

//Switch pump ON if over time was not reached, tank is not empty and interlock is OK
bool Pump::Start()
{
  if((!isPumpOn())
    && !UpTimeError
    && this->Pump::TankLevel()
    && this->Pump::Interlock())
  {
    _driver->set(true);
    StartTime = LastStartTime = millis();
    return true;
  }
  else return false;
}

//Switch pump OFF
bool Pump::Stop()
{
  if(isPumpOn())
  {
    _driver->set(false);
    UpTime += millis() - StartTime;
    return true;
  }
  else return false;
}

//Reset the tracking of running time
//This is typically called every day at midnight
void Pump::ResetUpTime()
{
  StartTime = 0;
  StopTime = 0;
  UpTime = 0;
  CurrMaxUpTime = MaxUpTime;
}

//Set a maximum running time (in millisecs) per day (in case ResetUpTime() is called once per day)
//Once reached, pump is stopped and "UpTimeError" error flag is raised
//Set "Max" to 0 to disable limit
void Pump::SetMaxUpTime(unsigned long Max)
{
  MaxUpTime = Max;
  CurrMaxUpTime = MaxUpTime;
}

//Clear "UpTimeError" error flag and allow the pump to run for an extra MaxUpTime
void Pump::ClearErrors()
{
  if(UpTimeError)
  {
    CurrMaxUpTime += MaxUpTime;
    UpTimeError = false;
  }
}

//tank level status (true = full, false = empty)
bool Pump::TankLevel()
{
  if(tanklevelpin == NO_TANK)
  {
    return true;
  }
  else if (tanklevelpin == NO_LEVEL)
  {
    return (this->Pump::GetTankFill() > 5.); //alert below 5% 
  }
  else
  {
    return (digitalRead(tanklevelpin) == TANK_FULL);
  } 
}

//Return the percentage used since last reset of UpTime
double Pump::GetTankUsage() 
{
  float PercentageUsed = -1.0;
  if((tankvolume != 0.0) && (flowrate !=0.0))
  {
    double MinutesOfUpTime = (double)UpTime/1000.0/60.0;
    double Consumption = flowrate/60.0*MinutesOfUpTime;
    PercentageUsed = Consumption/tankvolume*100.0;
  }
  return (PercentageUsed);  
}

//Return the remaining quantity in tank in %. When resetting UpTime, SetTankFill must be called accordingly
double Pump::GetTankFill()
{
  return (tankfill - this->Pump::GetTankUsage());
}

//Set Tank volume
//Typically call this function when changing tank and set it to the full volume
void Pump::SetTankVolume(double Volume)
{
  tankvolume = Volume;
}

//Set flow rate of the pump in Liters/hour
void Pump::SetFlowRate(double FlowRate)
{
  flowrate = FlowRate;
}

//Set tank fill (percentage of tank volume)
void Pump::SetTankFill(double TankFill)
{
  tankfill = TankFill;
}

//interlock status
bool Pump::Interlock()
{
  // SP4 — if an override source is set (e.g. FiltrationPump is bound to an
  // MQTT driver so its GPIO pin is no longer authoritative), consult the
  // source Pump's running state directly.
  if (_interlockSrc) return const_cast<Pump*>(_interlockSrc)->IsRunning();
  if (interlockpin == NO_INTERLOCK) return true;
  return (digitalRead(interlockpin) == INTERLOCK_OK);
}

//pump status
bool Pump::IsRunning()
{
  return isPumpOn();
}

// SP4 — replace the default internal driver with an externally-built one.
// Caller retains ownership; the previous driver (built by the ctor) is
// intentionally leaked — this is called at most once per Pump, at boot.
void Pump::setDriver(OutputDriver* driver)
{
  if (driver) _driver = driver;
}

// SP4 — bind another Pump as the interlock source for this pump. nullptr
// restores the default digitalRead(interlockpin) behaviour.
void Pump::setInterlockSource(const Pump* src)
{
  _interlockSrc = src;
}

void Pump::syncStateFromDriver() {
  // SP4 — with isPumpOn() now consulting _driver->get() directly, no
  // separate sync is needed. Kept for future use (e.g. to detect
  // state-topic-driven transitions and trigger side effects) and so
  // Setup.cpp's periodic call from WsBroadcast still resolves.
}

// SP4 — authoritative "is pump currently on?" check. Prefers the driver's
// cached state (which reflects the last set() call for GPIO drivers and
// the state-topic echo for MQTT drivers). Falls back to the legacy
// digitalRead(isrunningsensorpin) only defensively — the constructor
// always builds a non-null _driver, so the fallback is unreachable in
// normal operation.
bool Pump::isPumpOn() const
{
  if (_driver) return _driver->get();
  return digitalRead(isrunningsensorpin) == PUMP_ON;
}
