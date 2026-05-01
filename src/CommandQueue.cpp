// CommandQueue: task to process JSON commands received via MQTT.
// If the command modifies a setting parameter, it is saved in NVS and published back
// for other MQTT clients (dashboards).

#include <Arduino.h>
#include "Config.h"
#include "PoolMaster.h"
#include "MqttBridge.h"   // connectToMqtt() / stopMqttReconnectTimer() for MqttReconnect command
#include "Drivers.h"
#include "Presets.h"
#include "OutputDriver.h"
#include "Credentials.h"
#include <espMqttClientAsync.h>

extern espMqttClientAsync mqttClient;   // defined in MqttBridge.cpp (global scope)

// Functions prototypes
bool saveParam(const char*,uint8_t );
bool saveParam(const char*,bool );
bool saveParam(const char*,unsigned long );
bool saveParam(const char*,double );
void PublishSettings(void);
void mqttErrorPublish(const char*);
void simpLinReg(float * , float * , double & , double &, int );
void PublishMeasures();
void SetPhPID(bool);
void SetOrpPID(bool);
void stack_mon(UBaseType_t&);

void ProcessCommand(void *pvParameters)
{
  //Json Document
  JsonDocument command;
  char JSONCommand[150] = "";                         // JSON command to process  

  while (!startTasks) ;
  vTaskDelay(DT2);                                // Scheduling offset   

  TickType_t period = PT2;  
  static UBaseType_t hwm = 0;
  TickType_t ticktime = xTaskGetTickCount(); 

  #ifdef CHRONO
  unsigned long td;
  int t_act=0,t_min=999,t_max=0;
  float t_mean=0.;
  int n=1;
  #endif

  for(;;) {
    #ifdef CHRONO
    td = millis();
    #endif
    //Is there any incoming JSON commands
    if (uxQueueMessagesWaiting(queueIn) != 0)
    {  
      xQueueReceive(queueIn,&JSONCommand,0);

      //Parse Json object and find which command it is
      DeserializationError error = deserializeJson(command,JSONCommand);

      // Test if parsing succeeds.
      if (error)
      {
        Debug.print(DBG_WARNING,"Json parseObject() failed");
      }
      else
      {
        Debug.print(DBG_DEBUG,"Json parseObject() success: %s",JSONCommand);

        //Provide the external temperature. Should be updated regularly and will be used to start filtration for 10mins every hour when temperature is negative
        if (command[F("TempExt")].is<JsonVariant>())
        {
          storage.TempExternal = command["TempExt"].as<float>();
          Debug.print(DBG_DEBUG,"External Temperature: %4.1f°C",storage.TempExternal);
        }
        //"PhCalib" command which computes and sets the calibration coefficients of the pH sensor response based on a multi-point linear regression
        //{"PhCalib":[4.02,3.8,9.0,9.11]}  -> multi-point linear regression calibration (minimum 1 point-couple, 6 max.) in the form [ProbeReading_0, BufferRating_0, xx, xx, ProbeReading_n, BufferRating_n]
        else if (command[F("PhCalib")].is<JsonVariant>())
        {
          float CalibPoints[12]; //Max six calibration point-couples! Should be plenty enough
          int NbPoints = (int)copyArray(command[F("PhCalib")].as<JsonArray>(),CalibPoints);        
          Debug.print(DBG_DEBUG,"PhCalib command - %d points received",NbPoints);
          for (int i = 0; i < NbPoints; i += 2)
            Debug.print(DBG_DEBUG,"%10.2f - %10.2f",CalibPoints[i],CalibPoints[i + 1]);

          if (NbPoints == 2) //Only one pair of points. Perform a simple offset calibration
          {
            Debug.print(DBG_DEBUG,"2 points. Performing a simple offset calibration");

            //compute offset correction
            storage.pHCalibCoeffs1 += CalibPoints[1] - CalibPoints[0];

            //Set slope back to default value
            storage.pHCalibCoeffs0 = 3.76;

            Debug.print(DBG_DEBUG,"Calibration completed. Coeffs are: %10.2f, %10.2f",storage.pHCalibCoeffs0,storage.pHCalibCoeffs1);
          }
          else if ((NbPoints > 3) && (NbPoints % 2 == 0)) //we have at least 4 points as well as an even number of points. Perform a linear regression calibration
          {
            Debug.print(DBG_DEBUG,"%d points. Performing a linear regression calibration",NbPoints / 2);

            float xCalibPoints[NbPoints / 2];
            float yCalibPoints[NbPoints / 2];

            //generate array of x sensor values (in volts) and y rated buffer values
            //storage.PhValue = (storage.pHCalibCoeffs0 * ph_sensor_value) + storage.pHCalibCoeffs1;
            for (int i = 0; i < NbPoints; i += 2)
            {
              xCalibPoints[i / 2] = (CalibPoints[i] - storage.pHCalibCoeffs1) / storage.pHCalibCoeffs0;
              yCalibPoints[i / 2] = CalibPoints[i + 1];
            }

            //Compute linear regression coefficients
            simpLinReg(xCalibPoints, yCalibPoints, storage.pHCalibCoeffs0, storage.pHCalibCoeffs1, NbPoints / 2);

            Debug.print(DBG_DEBUG,"Calibration completed. Coeffs are: %10.2f, %10.2f",storage.pHCalibCoeffs0 ,storage.pHCalibCoeffs1);
          }
          //Store the new coefficients in eeprom
          saveParam("pHCalibCoeffs0",storage.pHCalibCoeffs0);
          saveParam("pHCalibCoeffs1",storage.pHCalibCoeffs1);          
          PublishSettings();
        }
        //"OrpCalib" command which computes and sets the calibration coefficients of the Orp sensor response based on a multi-point linear regression
        //{"OrpCalib":[450,465,750,784]}   -> multi-point linear regression calibration (minimum 1 point-couple, 6 max.) in the form [ProbeReading_0, BufferRating_0, xx, xx, ProbeReading_n, BufferRating_n]
        else if (command[F("OrpCalib")].is<JsonVariant>())
        {
          float CalibPoints[12]; //Max six calibration point-couples! Should be plenty enough
          int NbPoints = (int)copyArray(command[F("OrpCalib")].as<JsonArray>(),CalibPoints);
          Debug.print(DBG_DEBUG,"OrpCalib command - %d points received",NbPoints);
          for (int i = 0; i < NbPoints; i += 2)
            Debug.print(DBG_DEBUG,"%10.2f - %10.2f",CalibPoints[i],CalibPoints[i + 1]);        
          if (NbPoints == 2) //Only one pair of points. Perform a simple offset calibration
          {
            Debug.print(DBG_DEBUG,"2 points. Performing a simple offset calibration");

            //compute offset correction
            storage.OrpCalibCoeffs1 += CalibPoints[1] - CalibPoints[0];

            //Set slope back to default value
            storage.OrpCalibCoeffs0 = -1000;

            Debug.print(DBG_DEBUG,"Calibration completed. Coeffs are: %10.2f, %10.2f",storage.OrpCalibCoeffs0,storage.OrpCalibCoeffs1);
          }
          else if ((NbPoints > 3) && (NbPoints % 2 == 0)) //we have at least 4 points as well as an even number of points. Perform a linear regression calibration
          {
            Debug.print(DBG_DEBUG,"%d points. Performing a linear regression calibration",NbPoints / 2);

            float xCalibPoints[NbPoints / 2];
            float yCalibPoints[NbPoints / 2];

            //generate array of x sensor values (in volts) and y rated buffer values
            //storage.OrpValue = (storage.OrpCalibCoeffs0 * orp_sensor_value) + storage.OrpCalibCoeffs1;
            for (int i = 0; i < NbPoints; i += 2)
            {
              xCalibPoints[i / 2] = (CalibPoints[i] - storage.OrpCalibCoeffs1) / storage.OrpCalibCoeffs0;
              yCalibPoints[i / 2] = CalibPoints[i + 1];
            }

            //Compute linear regression coefficients
            simpLinReg(xCalibPoints, yCalibPoints, storage.OrpCalibCoeffs0, storage.OrpCalibCoeffs1, NbPoints / 2);

            Debug.print(DBG_DEBUG,"Calibration completed. Coeffs are: %10.2f, %10.2f",storage.OrpCalibCoeffs0,storage.OrpCalibCoeffs1);
          }
          //Store the new coefficients in eeprom
          saveParam("OrpCalibCoeffs0",storage.OrpCalibCoeffs0);
          saveParam("OrpCalibCoeffs1",storage.OrpCalibCoeffs1);          
          PublishSettings();
        }
        //"PSICalib" command which computes and sets the calibration coefficients of the Electronic Pressure sensor response based on a linear regression and a reference mechanical sensor (typically located on the sand filter)
        //{"PSICalib":[0,0,0.71,0.6]}   -> multi-point linear regression calibration (minimum 2 point-couple, 6 max.) in the form [ElectronicPressureSensorReading_0, MechanicalPressureSensorReading_0, xx, xx, ElectronicPressureSensorReading_n, ElectronicPressureSensorReading_n]
        else if (command[F("PSICalib")].is<JsonVariant>())
        {
          float CalibPoints[12];//Max six calibration point-couples! Should be plenty enough, typically use two point-couples (filtration ON and filtration OFF)
          int NbPoints = (int)copyArray(command[F("PSICalib")].as<JsonArray>(),CalibPoints);
          Debug.print(DBG_DEBUG,"PSICalib command - %d points received",NbPoints);
          for (int i = 0; i < NbPoints; i += 2)
            Debug.print(DBG_DEBUG,"%10.2f, %10.2f",CalibPoints[i],CalibPoints[i + 1]);

          if ((NbPoints > 3) && (NbPoints % 2 == 0)) //we have at least 4 points as well as an even number of points. Perform a linear regression calibration
          {
            Debug.print(DBG_DEBUG,"%d points. Performing a linear regression calibration",NbPoints / 2);

            float xCalibPoints[NbPoints / 2];
            float yCalibPoints[NbPoints / 2];

            //generate array of x sensor values (in volts) and y rated buffer values
            //storage.OrpValue = (storage.OrpCalibCoeffs0 * orp_sensor_value) + storage.OrpCalibCoeffs1;
            //storage.PSIValue = (storage.PSICalibCoeffs0 * psi_sensor_value) + storage.PSICalibCoeffs1;
            for (int i = 0; i < NbPoints; i += 2)
            {
              xCalibPoints[i / 2] = (CalibPoints[i] - storage.PSICalibCoeffs1) / storage.PSICalibCoeffs0;
              yCalibPoints[i / 2] = CalibPoints[i + 1];
            }

            //Compute linear regression coefficients
            simpLinReg(xCalibPoints, yCalibPoints, storage.PSICalibCoeffs0, storage.PSICalibCoeffs1, NbPoints / 2);

            //Store the new coefficients in eeprom
            saveParam("PSICalibCoeffs0",storage.PSICalibCoeffs0);
            saveParam("PSICalibCoeffs1",storage.PSICalibCoeffs1);          
            PublishSettings();
            Debug.print(DBG_DEBUG,"Calibration completed. Coeffs are: %10.2f, %10.2f",storage.PSICalibCoeffs0,storage.PSICalibCoeffs1);
          }
        }
        //"Mode" command which sets regulation and filtration to manual or auto modes
        else if (command[F("Mode")].is<JsonVariant>())
        {
          if ((int)command[F("Mode")] == 0)
          {
            storage.AutoMode = 0;

            //Stop PIDs
            SetPhPID(false);
            SetOrpPID(false);
          }
          else
          {
            storage.AutoMode = 1;
          }
          saveParam("AutoMode",storage.AutoMode);
        }
        else if (command[F("Winter")].is<JsonVariant>()) //"Winter" command which activate/deactivate Winter Mode
        {
          (bool)command[F("Winter")] ? storage.WinterMode = true : storage.WinterMode = false;
          saveParam("WinterMode",storage.WinterMode);
          PublishSettings(); 
        }
        else if (command[F("PhSetPoint")].is<JsonVariant>()) //"PhSetPoint" command which sets the setpoint for Ph
        {
          storage.Ph_SetPoint = command[F("PhSetPoint")].as<double>();
          Debug.print(DBG_DEBUG,"Command PhSetPoint: %13.9f",storage.Ph_SetPoint);
          saveParam("Ph_SetPoint",storage.Ph_SetPoint);
          PublishSettings();
        }
        else if (command[F("OrpSetPoint")].is<JsonVariant>()) //"OrpSetPoint" command which sets the setpoint for ORP
        {
          storage.Orp_SetPoint = command[F("OrpSetPoint")].as<double>();
          saveParam("Orp_SetPoint",storage.Orp_SetPoint);
          PublishSettings();
        }
        else if (command[F("WSetPoint")].is<JsonVariant>()) //"WSetPoint" command which sets the setpoint for Water temp (currently not in use)
        {
          storage.WaterTemp_SetPoint = (double)command[F("WSetPoint")];
          saveParam("WaterTempSet",storage.WaterTemp_SetPoint);
          PublishSettings();
        }
        //"pHTank" command which is called when the pH tank is changed or refilled
        //First parameter is volume of tank in Liters, second parameter is percentage Fill of the tank (typically 100% when new)
        else if (command[F("pHTank")].is<JsonVariant>())
        {
          storage.pHTankVol = (double)command[F("pHTank")][0];
          PhPump.SetTankVolume(storage.pHTankVol);
          storage.AcidFill = (double)command[F("pHTank")][1];
          PhPump.SetTankFill(storage.AcidFill);
        //  PhPump.ResetUpTime();
          saveParam("pHTankVol",storage.pHTankVol);
          saveParam("AcidFill",storage.AcidFill);               
          PublishSettings();
        }
        //"ChlTank" command which is called when the Chl tank is changed or refilled
        //First parameter is volume of tank in Liters, second parameter is percentage Fill of the tank (typically 100% when new)
        else if (command[F("ChlTank")].is<JsonVariant>())
        {
          storage.ChlTankVol = (double)command[F("ChlTank")][0];
          ChlPump.SetTankVolume(storage.ChlTankVol);
          storage.ChlFill = (double)command[F("ChlTank")][1];
          ChlPump.SetTankFill(storage.ChlFill);
        //  ChlPump.ResetUpTime();
          saveParam("ChlTankVol",storage.ChlTankVol);
          saveParam("ChlFill",storage.ChlFill);
          PublishSettings();
        }
        else if (command[F("WTempLow")].is<JsonVariant>()) //"WTempLow" command which sets the setpoint for Water temp low threshold
        {
          storage.WaterTempLowThreshold = (double)command[F("WTempLow")];
          saveParam("WaterTempLow",storage.WaterTempLowThreshold);
          PublishSettings();
        }
        else if (command[F("PumpsMaxUp")].is<JsonVariant>()) //"PumpsMaxUp" command which sets the Max UpTime for pumps
        {
          storage.PhPumpUpTimeLimit = (unsigned int)command[F("PumpsMaxUp")];
          PhPump.SetMaxUpTime(storage.PhPumpUpTimeLimit * 1000);
          storage.ChlPumpUpTimeLimit = (unsigned int)command[F("PumpsMaxUp")];
          ChlPump.SetMaxUpTime(storage.ChlPumpUpTimeLimit * 1000);
          saveParam("PhPumpUTL",storage.PhPumpUpTimeLimit);
          saveParam("ChlPumpUTL",storage.ChlPumpUpTimeLimit);                    
          PublishSettings();
        }
        else if (command[F("OrpPIDParams")].is<JsonVariant>()) //"OrpPIDParams" command which sets the Kp, Ki and Kd values for Orp PID loop
        {
          storage.Orp_Kp = (double)command[F("OrpPIDParams")][0];
          storage.Orp_Ki = (double)command[F("OrpPIDParams")][1];
          storage.Orp_Kd = (double)command[F("OrpPIDParams")][2];
          saveParam("Orp_Kp",storage.Orp_Kp);
          saveParam("Orp_Ki",storage.Orp_Ki);
          saveParam("Orp_Kd",storage.Orp_Kd);
          OrpPID.SetTunings(storage.Orp_Kp, storage.Orp_Ki, storage.Orp_Kd);
          PublishSettings();
        }
        else if (command[F("PhPIDParams")].is<JsonVariant>()) //"PhPIDParams" command which sets the Kp, Ki and Kd values for Ph PID loop
        {
          storage.Ph_Kp = (double)command[F("PhPIDParams")][0];
          storage.Ph_Ki = (double)command[F("PhPIDParams")][1];
          storage.Ph_Kd = (double)command[F("PhPIDParams")][2];
          saveParam("Ph_Kp",storage.Ph_Kp);
          saveParam("Ph_Ki",storage.Ph_Ki);
          saveParam("Ph_Kd",storage.Ph_Kd);
          PhPID.SetTunings(storage.Ph_Kp, storage.Ph_Ki, storage.Ph_Kd);
          PublishSettings();
        }
        else if (command[F("OrpPIDWSize")].is<JsonVariant>()) //"OrpPIDWSize" command which sets the window size of the Orp PID loop
        {
          storage.OrpPIDWindowSize = (unsigned long)command[F("OrpPIDWSize")]*60*1000;
          saveParam("OrpPIDWSize",storage.OrpPIDWindowSize);
          OrpPID.SetSampleTime((int)storage.OrpPIDWindowSize);
          OrpPID.SetOutputLimits(0, storage.OrpPIDWindowSize);  //Whatever happens, don't allow continuous injection of Chl for more than a PID Window
          PublishSettings();
        }
        else if (command[F("PhPIDWSize")].is<JsonVariant>()) //"PhPIDWSize" command which sets the window size of the Ph PID loop
        {
          storage.PhPIDWindowSize = (unsigned long)command[F("PhPIDWSize")]*60*1000;
          saveParam("PhPIDWSize",storage.PhPIDWindowSize);
          PhPID.SetSampleTime((int)storage.PhPIDWindowSize);
          PhPID.SetOutputLimits(0, storage.PhPIDWindowSize);    //Whatever happens, don't allow continuous injection of Acid for more than a PID Window
          PublishSettings();
        }
        else if (command[F("Date")].is<JsonVariant>()) //"Date" command which sets the Date of RTC module
        {
          setTime((uint8_t)command[F("Date")][4], (uint8_t)command[F("Date")][5], (uint8_t)command[F("Date")][6], (uint8_t)command[F("Date")][0], (uint8_t)command[F("Date")][2], (uint8_t)command[F("Date")][3]); //(Day of the month, Day of the week, Month, Year, Hour, Minute, Second)
        }
        else if (command[F("FiltT0")].is<JsonVariant>()) //"FiltT0" command which sets the earliest hour when starting Filtration pump
        {
          storage.FiltrationStartMin = (unsigned int)command[F("FiltT0")];
          saveParam("FiltrStartMin",storage.FiltrationStartMin);
          PublishSettings();
        }
        else if (command[F("FiltT1")].is<JsonVariant>()) //"FiltT1" command which sets the latest hour for running Filtration pump
        {
          storage.FiltrationStopMax = (unsigned int)command[F("FiltT1")];
          saveParam("FiltrStopMax",storage.FiltrationStopMax);
          PublishSettings();
        }
        else if (command[F("PubPeriod")].is<JsonVariant>()) //"PubPeriod" command which sets the periodicity for publishing system info to MQTT broker
        {
          storage.PublishPeriod = (unsigned long)command[F("PubPeriod")] * 1000; //in secs
          saveParam("PublishPeriod",storage.PublishPeriod);
          PublishSettings();
        }
        else if (command[F("DelayPID")].is<JsonVariant>()) //"DelayPID" command which sets the delay from filtering start before PID loops start regulating
        {
          storage.DelayPIDs = (unsigned int)command[F("DelayPID")];
          saveParam("DelayPIDs",storage.DelayPIDs);
          PublishSettings();
        }
        else if (command[F("PSIHigh")].is<JsonVariant>()) //"PSIHigh" command which sets the water high-pressure threshold
        {
          storage.PSI_HighThreshold = (double)command[F("PSIHigh")];
          saveParam("PSI_High",storage.PSI_HighThreshold);
          PublishSettings();
        }
        else if (command[F("PSILow")].is<JsonVariant>()) //"PSILow" command which sets the water low-pressure threshold
        {
          storage.PSI_MedThreshold = (double)command[F("PSILow")];
          saveParam("PSI_Med",storage.PSI_MedThreshold);
          PublishSettings();
        }         
        else if (command[F("pHPumpFR")].is<JsonVariant>())//"PhPumpFR" set flow rate of Ph pump
        {
          storage.pHPumpFR = (double)command[F("pHPumpFR")];
          PhPump.SetFlowRate((double)command[F("pHPumpFR")]);
          saveParam("pHPumpFR",storage.pHPumpFR);
          PublishSettings();
        }
        else if (command[F("ChlPumpFR")].is<JsonVariant>())//"ChlPumpFR" set flow rate of Chl pump
        {
          storage.ChlPumpFR = (double)command[F("ChlPumpFR")];
          ChlPump.SetFlowRate((double)command[F("ChlpumpFR")]);
          saveParam("ChlPumpFR",storage.ChlPumpFR);
          PublishSettings();
        }
        else if (command[F("RstpHCal")].is<JsonVariant>())//"RstpHCal" reset the calibration coefficients of the pH probe
        {
          storage.pHCalibCoeffs0 = 3.51;
          storage.pHCalibCoeffs1 = -2.73;
          saveParam("pHCalibCoeffs0",storage.pHCalibCoeffs0);
          saveParam("pHCalibCoeffs1",storage.pHCalibCoeffs1);
          PublishSettings();
        }
        else if (command[F("RstOrpCal")].is<JsonVariant>())//"RstOrpCal" reset the calibration coefficients of the Orp probe
        {
          storage.OrpCalibCoeffs0 = (double)-930.;
          storage.OrpCalibCoeffs1 = (double)2455.;
          saveParam("OrpCalibCoeffs0",storage.OrpCalibCoeffs0);
          saveParam("OrpCalibCoeffs1",storage.OrpCalibCoeffs1);
          PublishSettings();
        }
        else if (command[F("RstPSICal")].is<JsonVariant>())//"RstPSICal" reset the calibration coefficients of the pressure sensor
        {
          storage.PSICalibCoeffs0 = (double)1.31;
          storage.PSICalibCoeffs1 = (double)-0.1;
          saveParam("PSICalibCoeffs0",storage.PSICalibCoeffs0);
          saveParam("PSICalibCoeffs1",storage.PSICalibCoeffs1);
          PublishSettings();
        }
        else if (command[F("Settings")].is<JsonVariant>())//Pubilsh settings to refresh data on remote displays
        {
          PublishSettings();
        }         
        else if (command[F("FiltPump")].is<JsonVariant>()) //"FiltPump" command which starts or stops the filtration pump
        {
          if ((int)command[F("FiltPump")] == 0)
          {
            EmergencyStopFiltPump = true;
            FiltrationPump.Stop();  //stop filtration pump

            //Stop PIDs
            SetPhPID(false);
            SetOrpPID(false);
          }
          else
          {
            EmergencyStopFiltPump = false;
            FiltrationPump.Start();   //start filtration pump
          }
        }
        else if (command[F("RobotPump")].is<JsonVariant>()) //"RobotPump" command which starts or stops the Robot pump
        {
          if ((int)command[F("RobotPump")] == 0){
            RobotPump.Stop();    //stop robot pump
            cleaning_done = true;
          } else {
            RobotPump.Start();   //start robot pump
            cleaning_done = false;
          }  
        }
        else if (command[F("PhPump")].is<JsonVariant>()) //"PhPump" command which starts or stops the Acid pump
        {
          if ((int)command[F("PhPump")] == 0)
            PhPump.Stop();       //stop Acid pump
          else
            PhPump.Start();      //start Acid pump
        }
        else if (command[F("ChlPump")].is<JsonVariant>()) //"ChlPump" command which starts or stops the Acid pump
        {
          if ((int)command[F("ChlPump")] == 0)
            ChlPump.Stop();      //stop Chl pump
          else
            ChlPump.Start();     //start Chl pump
        }
        else if (command[F("PhPID")].is<JsonVariant>()) //"PhPID" command which starts or stops the Ph PID loop
        {
          if ((int)command[F("PhPID")] == 0)
          {
            //Stop PID
            SetPhPID(false);
          }
          else
          {
            //Start PID
            SetPhPID(true);
          }
        }
        else if (command[F("OrpPID")].is<JsonVariant>()) //"OrpPID" command which starts or stops the Orp PID loop
        {
          if ((int)command[F("OrpPID")] == 0)
          {
            //Stop PID
            SetOrpPID(false);
          }
          else
          {
            //Start PID
            SetOrpPID(true);
          }
        }
        //"Relay" command which is called to actuate relays
        //Parameter 1 is the relay number (R0 in this example), parameter 2 is the relay state (ON in this example).
        else if (command[F("Relay")].is<JsonVariant>())
        {
          int idx = (int)command[F("Relay")][0];
          bool on = (bool)command[F("Relay")][1];
          const char* slot = (idx == 0) ? "r0" : (idx == 1) ? "r1" : nullptr;
          if (slot) {
            OutputDriver* d = Drivers::get(slot);
            if (d) d->set(on);
          }
        }
        //{"CustomOutput":[N, value]}  — N in 0..7. value is 0|1 or "on"|"off".
        // Index out of range or slot disabled → silent drop (matches Relay).
        else if (command[F("CustomOutput")].is<JsonVariant>())
        {
          uint8_t idx   = (uint8_t) (int) command[F("CustomOutput")][0];
          JsonVariant v = command[F("CustomOutput")][1];
          bool value;
          if (v.is<const char*>()) {
            const char* s = v.as<const char*>();
            value = (strcasecmp(s, "on") == 0) || (strcasecmp(s, "1") == 0);
          } else {
            value = (bool) (int) v;
          }

          if (idx >= Drivers::customSlotCount() || !Drivers::isCustomEnabled(idx)) {
            Debug.print(DBG_WARNING, "[cmd] CustomOutput idx=%u ignored (disabled or out of range)",
              (unsigned) idx);
          } else {
            char slot[12];
            snprintf(slot, sizeof(slot), "custom_%u", (unsigned) idx);
            OutputDriver* d = Drivers::get(slot);
            if (d) {
              d->set(value);
              // Publish retained state echo on the per-slot state topic, matching
              // the pattern used by the six fixed switches.
              String topic = String("poolmaster/") + Credentials::deviceId()
                           + "/" + slot + "/state";
              mqttClient.publish(topic.c_str(), 1, /*retain=*/true, value ? "ON" : "OFF");
            }
          }
        }
        //{"Reboot":1}  — restart the ESP32. SP7 — defer 500 ms via xTaskCreate
        // so any in-flight HTTP/MQTT response can flush before the reset
        // (matches SP4 /api/drivers + SP5 /api/custom-outputs reboot pattern).
        else if (command[F("Reboot")].is<JsonVariant>())
        {
          Debug.print(DBG_INFO, "[cmd] Reboot requested");
          xTaskCreate([](void*) { vTaskDelay(pdMS_TO_TICKS(500)); ESP.restart(); },
                      "reboot", 2048, nullptr, 1, nullptr);
        }
        else if (command[F("MqttReconnect")].is<JsonVariant>())//"MqttReconnect" forces an immediate MQTT (re)connection attempt
        {
          Debug.print(DBG_INFO, "MqttReconnect command — kicking connectToMqtt()");
          stopMqttReconnectTimer();
          connectToMqtt();
        }
        else if (command[F("Clear")].is<JsonVariant>()) //"Clear" command which clears the UpTime and pressure errors of the Pumps
        {
          if (PSIError)
            PSIError = false;

          if (PhPump.UpTimeError)
            PhPump.ClearErrors();

          if (ChlPump.UpTimeError)
            ChlPump.ClearErrors();

          mqttErrorPublish(""); // publish clearing of error(s)

          //start filtration pump if within scheduled time slots
          if (!EmergencyStopFiltPump && storage.AutoMode && (hour() >= storage.FiltrationStart) && (hour() < storage.FiltrationStop))
            FiltrationPump.Start();
        }
        else if (command[F("PresetActivate")].is<JsonVariant>())
        {
          uint8_t s = command[F("PresetActivate")][F("slot")] | 0xFF;
          if (s < Presets::MAX_PRESETS) Presets::activate(s);
        }
        else if (command[F("PresetDelete")].is<JsonVariant>())
        {
          uint8_t s = command[F("PresetDelete")][F("slot")] | 0xFF;
          if (s < Presets::MAX_PRESETS) Presets::clearPreset(s);
        }
        else if (command[F("PresetSave")].is<JsonVariant>())
        {
          JsonObject o = command[F("PresetSave")];
          uint8_t s = o[F("slot")] | 0xFF;
          if (s < Presets::MAX_PRESETS) {
            Presets::PresetData d{};
            const char* name = o[F("name")] | "";
            strncpy(d.name, name, Presets::NAME_MAX_LEN - 1);
            const char* tStr = o[F("presetType")] | "manual";
            d.type = (strcmp(tStr, "auto_temp") == 0) ? Presets::Type::AutoTemp : Presets::Type::Manual;

            JsonArray ws = o[F("windows")].as<JsonArray>();
            uint8_t i = 0;
            for (JsonObject w : ws) {
              if (i >= Presets::WINDOWS_PER) break;
              d.windows[i].start_min = w[F("start")] | 0;
              d.windows[i].end_min   = w[F("end")]   | 0;
              d.windows[i].enabled   = w[F("enabled")] | false;
              ++i;
            }
            while (i < Presets::WINDOWS_PER) { d.windows[i++] = { 0, 0, false }; }

            JsonObject autoBlk = o[F("auto")].as<JsonObject>();
            if (!autoBlk.isNull()) {
              d.startMinHour = autoBlk[F("startMinHour")] | 8;
              d.stopMaxHour  = autoBlk[F("stopMaxHour")]  | 22;
              d.centerHour   = autoBlk[F("centerHour")]   | 15;
            } else {
              d.startMinHour = 8;
              d.stopMaxHour  = 22;
              d.centerHour   = 15;
            }

            Presets::savePreset(s, d);
          }
        }
        //Publish Update on the MQTT broker the status of our variables
        PublishMeasures();
      }
    }
    #ifdef CHRONO
    t_act = millis() - td;
    if(t_act > t_max) t_max = t_act;
    if(t_act < t_min) t_min = t_act;
    t_mean += (t_act - t_mean)/n;
    ++n;
    Debug.print(DBG_INFO,"[CommandQueue] td: %d t_act: %d t_min: %d t_max: %d t_mean: %4.1f",td,t_act,t_min,t_max,t_mean);
    #endif 
    stack_mon(hwm); 
    vTaskDelayUntil(&ticktime,period);
  }  
}

//Linear regression coefficients calculation function
// pass x and y arrays (pointers), lrCoef pointer, and n.
//The lrCoef array is comprised of the slope=lrCoef[0] and intercept=lrCoef[1].  n is the length of the x and y arrays.
//http://jwbrooks.blogspot.com/2014/02/arduino-linear-regression-function.html
void simpLinReg(float * x, float * y, double & lrCoef0, double & lrCoef1, int n)
{
  // initialize variables
  float xbar = 0;
  float ybar = 0;
  float xybar = 0;
  float xsqbar = 0;

  // calculations required for linear regression
  for (int i = 0; i < n; i++)
  {
    xbar += x[i];
    ybar += y[i];
    xybar += x[i] * y[i];
    xsqbar += x[i] * x[i];
  }

  xbar /= n;
  ybar /= n;
  xybar /= n;
  xsqbar /= n;

  // simple linear regression algorithm
  lrCoef0 = (xybar - xbar * ybar) / (xsqbar - xbar * xbar);
  lrCoef1 = ybar - lrCoef0 * xbar;
}