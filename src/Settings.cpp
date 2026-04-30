#include <Arduino.h>
#include <Preferences.h>
#include "Arduino_DebugUtils.h"
#include "Config.h"
#include "PoolMaster.h"
#include "Settings.h"

extern Preferences nvs;

/* ----- loadConfig, saveConfig, and the 4 saveParam overloads (moved from Setup.cpp) ----- */

bool loadConfig()
{
  nvs.begin("PoolMaster",true);

  storage.ConfigVersion         = nvs.getUChar("ConfigVersion",0);
  storage.Ph_RegulationOnOff    = nvs.getBool("Ph_RegOnOff",false);
  storage.Orp_RegulationOnOff   = nvs.getBool("Orp_RegOnOff",false);
  storage.AutoMode              = nvs.getBool("AutoMode",true);
  storage.WinterMode            = nvs.getBool("WinterMode",false);
  storage.FiltrationDuration    = nvs.getUChar("FiltrDuration",12);
  storage.FiltrationStart       = nvs.getUChar("FiltrStart",8);
  storage.FiltrationStop        = nvs.getUChar("FiltrStop",20);
  storage.FiltrationStartMin    = nvs.getUChar("FiltrStartMin",8);
  storage.FiltrationStopMax     = nvs.getUChar("FiltrStopMax",22);
  storage.DelayPIDs             = nvs.getUChar("DelayPIDs",0);
  storage.PhPumpUpTimeLimit     = nvs.getULong("PhPumpUTL",900);
  storage.ChlPumpUpTimeLimit    = nvs.getULong("ChlPumpUTL",2500);
  storage.PublishPeriod         = nvs.getULong("PublishPeriod",30000);
  storage.PhPIDWindowSize       = nvs.getULong("PhPIDWSize",60000);
  storage.OrpPIDWindowSize      = nvs.getULong("OrpPIDWSize",60000);
  storage.PhPIDwindowStartTime  = nvs.getULong("PhPIDwStart",0);
  storage.OrpPIDwindowStartTime = nvs.getULong("OrpPIDwStart",0);
  storage.Ph_SetPoint           = nvs.getDouble("Ph_SetPoint",7.3);
  storage.Orp_SetPoint          = nvs.getDouble("Orp_SetPoint",750);
  storage.PSI_HighThreshold     = nvs.getDouble("PSI_High",0.5);
  storage.PSI_MedThreshold      = nvs.getDouble("PSI_Med",0.25);
  storage.WaterTempLowThreshold = nvs.getDouble("WaterTempLow",10.);
  storage.WaterTemp_SetPoint    = nvs.getDouble("WaterTempSet",27.);
  storage.TempExternal          = nvs.getDouble("TempExternal",3.);
  storage.pHCalibCoeffs0        = nvs.getDouble("pHCalibCoeffs0",4.3);
  storage.pHCalibCoeffs1        = nvs.getDouble("pHCalibCoeffs1",-2.63);
  storage.OrpCalibCoeffs0       = nvs.getDouble("OrpCalibCoeffs0",-1189.);
  storage.OrpCalibCoeffs1       = nvs.getDouble("OrpCalibCoeffs1",2564.);
  storage.PSICalibCoeffs0       = nvs.getDouble("PSICalibCoeffs0",1.11);
  storage.PSICalibCoeffs1       = nvs.getDouble("PSICalibCoeffs1",0.);
  storage.Ph_Kp                 = nvs.getDouble("Ph_Kp",2000000.);
  storage.Ph_Ki                 = nvs.getDouble("Ph_Ki",0.);
  storage.Ph_Kd                 = nvs.getDouble("Ph_Kd",0.);
  storage.Orp_Kp                = nvs.getDouble("Orp_Kp",2500.);
  storage.Orp_Ki                = nvs.getDouble("Orp_Ki",0.);
  storage.Orp_Kd                = nvs.getDouble("Orp_Kd",0.);
  storage.PhPIDOutput           = nvs.getDouble("PhPIDOutput",0.);
  storage.OrpPIDOutput          = nvs.getDouble("OrpPIDOutput",0.);
  storage.TempValue             = nvs.getDouble("TempValue",18.);
  storage.PhValue               = nvs.getDouble("PhValue",0.);
  storage.OrpValue              = nvs.getDouble("OrpValue",0.);
  storage.PSIValue              = nvs.getDouble("PSIValue",0.4);
  storage.AcidFill              = nvs.getDouble("AcidFill",100.);
  storage.ChlFill               = nvs.getDouble("ChlFill",100.);
  storage.pHTankVol             = nvs.getDouble("pHTankVol",20.);
  storage.ChlTankVol            = nvs.getDouble("ChlTankVol",20.);
  storage.pHPumpFR              = nvs.getDouble("pHPumpFR",1.5);
  storage.ChlPumpFR             = nvs.getDouble("ChlPumpFR",1.5);

  nvs.end();

  Debug.print(DBG_INFO,"%d",storage.ConfigVersion);
  Debug.print(DBG_INFO,"%d, %d, %d, %d",storage.Ph_RegulationOnOff,storage.Orp_RegulationOnOff,storage.AutoMode,storage.WinterMode);
  Debug.print(DBG_INFO,"%d, %d, %d, %d, %d, %d",storage.FiltrationDuration,storage.FiltrationStart,storage.FiltrationStop,
              storage.FiltrationStartMin,storage.FiltrationStopMax,storage.DelayPIDs);
  Debug.print(DBG_INFO,"%d, %d, %d",storage.PhPumpUpTimeLimit,storage.ChlPumpUpTimeLimit,storage.PublishPeriod);
  Debug.print(DBG_INFO,"%d, %d, %d, %d",storage.PhPIDWindowSize,storage.OrpPIDWindowSize,storage.PhPIDwindowStartTime,storage.OrpPIDwindowStartTime);
  Debug.print(DBG_INFO,"%3.1f, %4.0f, %3.1f, %3.1f, %3.0f, %3.0f, %4.1f, %8.6f, %9.6f, %11.6f, %11.6f, %3.1f, %3.1f",
              storage.Ph_SetPoint,storage.Orp_SetPoint,storage.PSI_HighThreshold,
              storage.PSI_MedThreshold,storage.WaterTempLowThreshold,storage.WaterTemp_SetPoint,storage.TempExternal,
              storage.pHCalibCoeffs0,storage.pHCalibCoeffs1,storage.OrpCalibCoeffs0,storage.OrpCalibCoeffs1,
              storage.PSICalibCoeffs0,storage.PSICalibCoeffs1);
  Debug.print(DBG_INFO,"%8.0f, %3.0f, %3.0f, %6.0f, %3.0f, %3.0f, %7.0f, %7.0f, %4.2f, %4.2f, %4.0f, %4.2f",
              storage.Ph_Kp,storage.Ph_Ki,storage.Ph_Kd,storage.Orp_Kp,storage.Orp_Ki,storage.Orp_Kd,
              storage.PhPIDOutput,storage.OrpPIDOutput,storage.TempValue,storage.PhValue,storage.OrpValue,storage.PSIValue);
  Debug.print(DBG_INFO,"%3.0f, %3.0f, %3.0f, %3.0f, %3.1f, %3.1f ",storage.AcidFill,storage.ChlFill,storage.pHTankVol,storage.ChlTankVol,
              storage.pHPumpFR,storage.ChlPumpFR);

  return (storage.ConfigVersion == CONFIG_VERSION);
}

bool saveConfig()
{
  nvs.begin("PoolMaster",false);

  size_t i = nvs.putUChar("ConfigVersion",storage.ConfigVersion);
  i += nvs.putBool("Ph_RegOnOff",storage.Ph_RegulationOnOff);
  i += nvs.putBool("Orp_RegOnOff",storage.Orp_RegulationOnOff);
  i += nvs.putBool("AutoMode",storage.AutoMode);
  i += nvs.putBool("WinterMode",storage.WinterMode);
  i += nvs.putUChar("FiltrDuration",storage.FiltrationDuration);
  i += nvs.putUChar("FiltrStart",storage.FiltrationStart);
  i += nvs.putUChar("FiltrStop",storage.FiltrationStop);
  i += nvs.putUChar("FiltrStartMin",storage.FiltrationStartMin);
  i += nvs.putUChar("FiltrStopMax",storage.FiltrationStopMax);
  i += nvs.putUChar("DelayPIDs",storage.DelayPIDs);
  i += nvs.putULong("PhPumpUTL",storage.PhPumpUpTimeLimit);
  i += nvs.putULong("ChlPumpUTL",storage.ChlPumpUpTimeLimit);
  i += nvs.putULong("PublishPeriod",storage.PublishPeriod);
  i += nvs.putULong("PhPIDWSize",storage.PhPIDWindowSize);
  i += nvs.putULong("OrpPIDWSize",storage.OrpPIDWindowSize);
  i += nvs.putULong("PhPIDwStart",storage.PhPIDwindowStartTime);
  i += nvs.putULong("OrpPIDwStart",storage.OrpPIDwindowStartTime);
  i += nvs.putDouble("Ph_SetPoint",storage.Ph_SetPoint);
  i += nvs.putDouble("Orp_SetPoint",storage.Orp_SetPoint);
  i += nvs.putDouble("PSI_High",storage.PSI_HighThreshold);
  i += nvs.putDouble("PSI_Med",storage.PSI_MedThreshold);
  i += nvs.putDouble("WaterTempLow",storage.WaterTempLowThreshold);
  i += nvs.putDouble("WaterTempSet",storage.WaterTemp_SetPoint);
  i += nvs.putDouble("TempExternal",storage.TempExternal);
  i += nvs.putDouble("pHCalibCoeffs0",storage.pHCalibCoeffs0);
  i += nvs.putDouble("pHCalibCoeffs1",storage.pHCalibCoeffs1);
  i += nvs.putDouble("OrpCalibCoeffs0",storage.OrpCalibCoeffs0);
  i += nvs.putDouble("OrpCalibCoeffs1",storage.OrpCalibCoeffs1);
  i += nvs.putDouble("PSICalibCoeffs0",storage.PSICalibCoeffs0);
  i += nvs.putDouble("PSICalibCoeffs1",storage.PSICalibCoeffs1);
  i += nvs.putDouble("Ph_Kp",storage.Ph_Kp);
  i += nvs.putDouble("Ph_Ki",storage.Ph_Ki);
  i += nvs.putDouble("Ph_Kd",storage.Ph_Kd);
  i += nvs.putDouble("Orp_Kp",storage.Orp_Kp);
  i += nvs.putDouble("Orp_Ki",storage.Orp_Ki);
  i += nvs.putDouble("Orp_Kd",storage.Orp_Kd);
  i += nvs.putDouble("PhPIDOutput",storage.PhPIDOutput);
  i += nvs.putDouble("OrpPIDOutput",storage.OrpPIDOutput);
  i += nvs.putDouble("TempValue",storage.TempValue);
  i += nvs.putDouble("PhValue",storage.PhValue);
  i += nvs.putDouble("OrpValue",storage.OrpValue);
  i += nvs.putDouble("PSIValue",storage.PSIValue);
  i += nvs.putDouble("AcidFill",storage.AcidFill);
  i += nvs.putDouble("ChlFill",storage.ChlFill);
  i += nvs.putDouble("pHTankVol",storage.pHTankVol);
  i += nvs.putDouble("ChlTankVol",storage.ChlTankVol);
  i += nvs.putDouble("pHPumpFR",storage.pHPumpFR);
  i += nvs.putDouble("ChlPumpFR",storage.ChlPumpFR);

  nvs.end();

  Debug.print(DBG_INFO,"Bytes saved: %d / %d\n",i,sizeof(storage));
  return (i == sizeof(storage)) ;

}

// functions to save any type of parameter (4 overloads with same name but different arguments)

// Each overload must pair nvs.begin with nvs.end. Without the end, the
// namespace handle is leaked and subsequent putXxx calls (here or in
// saveConfig/loadConfig) can silently write 0 bytes.
bool saveParam(const char* key, uint8_t val)
{
  nvs.begin("PoolMaster",false);
  size_t i = nvs.putUChar(key,val);
  nvs.end();
  return(i == sizeof(val));
}

bool saveParam(const char* key, bool val)
{
  nvs.begin("PoolMaster",false);
  size_t i = nvs.putBool(key,val);
  nvs.end();
  return(i == sizeof(val));
}

bool saveParam(const char* key, unsigned long val)
{
  nvs.begin("PoolMaster",false);
  size_t i = nvs.putULong(key,val);
  nvs.end();
  return(i == sizeof(val));
}

bool saveParam(const char* key, double val)
{
  nvs.begin("PoolMaster",false);
  size_t i = nvs.putDouble(key,val);
  nvs.end();
  return(i == sizeof(val));
}
