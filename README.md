## 🗓️ SP8 Driver Schedule Presets (2026-05)

**SP8 — Driver schedule presets**: Five named presets, each with up to four configurable timeslots, replace the single hard-coded filtration window. Switch profiles ("Summer", "Vacation", "Maintenance", etc.) from the dashboard — the active preset persists across reboots and is published over MQTT for Home Assistant. The legacy `auto-temp` behavior survives as the type for slot 0; existing devices upgrade in place via a CONFIG_VERSION 51 → 52 migration that rescues `FiltrStart/Stop/StartMin/StopMax` into the new layout.

### Firmware size — post-SP8

**Flash 85.4 %** (1 287 793 of 1 507 328 bytes) · **RAM 23.7 %** (77 540 of 327 680 bytes).
Down from SP5's 99.2 % Flash after architectural refactoring across SP6–SP8 reclaimed ~207 KB.

---

## ➕ SP5 Custom User-Defined Output Switches (2026-04)

On top of the six fixed physical-output slots from SP4, up to **8 user-defined custom
switches** can be added from the UI — pool lights, a salt chlorinator, a heat pump, garden
lights, anything MQTT-controllable or wired to a spare GPIO. Each custom slot reuses the
same `OutputDriver` interface (GPIO or MQTT), has its own display name, and appears in Home
Assistant as a retained `switch.poolmaster_<mac>_custom_N` entity whose `unique_id` is
pinned to the slot index — renaming the display name updates HA's `friendly_name` in place
without orphaning automations.

### Configure

Open `http://poolmaster.local/settings/drivers`. Below the six fixed drivers, the **Custom
switches** section shows a list of configured slots with live inline toggles, Edit / × /
`+ Add custom switch`. The modal form handles both kinds; on save the device reboots and
the entity appears in HA automatically.

### Command shape

On `PoolTopicAPI` or the web-UI WebSocket channel:

```json
{"CustomOutput":[3, 1]}       // turn slot 3 on
{"CustomOutput":[0, "off"]}   // turn slot 0 off
```

HA's per-entity `.../custom_N/set` topic is routed into the same command path by
`MqttBridge::dispatchHaSet`. Delete retracts the HA entity on next boot (empty retained
discovery payload).

### Firmware size — post-SP5

**Flash 99.2 %** (1 495 981 of 1 507 328 bytes) · **RAM 23.6 %** (77 276 of 327 680 bytes).
About +17 KB flash vs the SP4 ship commit (`ad27b9e` at 98.1 %). The cut list in
[docs/superpowers/specs/2026-04-24-sp5-custom-output-switches-design.md](docs/superpowers/specs/2026-04-24-sp5-custom-output-switches-design.md)
§11 stays on-file for the next sprint if headroom gets tighter.

---

## 🔌 SP4 Pluggable Output Drivers (2026-04)

Each of the six physical-output devices (`FiltrationPump`, `PhPump`, `ChlPump`, `RobotPump`,
relays `R0`, `R1`) can be rebound at runtime to either a local GPIO pin (current behaviour,
still the default) or an external MQTT relay (Shelly-compatible). PID, uptime, filtration
schedule, anti-freeze, emergency-stop, and interlock logic all continue to operate correctly
regardless of which driver each device is using — the abstraction is invisible above the driver.

### Configure

Open `http://poolmaster.local/settings/drivers`. For each device, pick **GPIO** (with pin
number + active-level) or **MQTT** (command topic, payload_on / payload_off, optional state
topic for authoritative feedback from the external device). Save + reboot.

Example — binding the filter pump to a Shelly:

| Field | Value |
| ----- | ----- |
| Kind | MQTT |
| Command topic | `shellies/garden-filter/relay/0/command` |
| Payload on / off | `on` / `off` |
| State topic | `shellies/garden-filter/relay/0` |
| State on / off | `on` / `off` |

With state feedback, the firmware sees external toggles of the Shelly (its button, HA,
another automation) within ~200 ms and updates PID / HA / dashboard state to match.

See [docs/superpowers/specs/2026-04-24-sp4-pluggable-output-drivers-design.md](docs/superpowers/specs/2026-04-24-sp4-pluggable-output-drivers-design.md)
for the full design.

---

## 🧊 SP3 Beautiful Web UI (2026-04)

Version **ESP-SP1** now ships with a responsive Preact + Tailwind SPA served from LittleFS.
`http://poolmaster.local/` opens a 12-screen dashboard (Home, Control, Tune, Insights, Settings)
with live tiles, a calibration wizard, PID tuning, history charts, log viewer, and diagnostics.

### Building the web UI

```bash
cd web
nvm use                                 # Node 20 LTS per .nvmrc
npm install                             # first time only
npm run dev                             # Vite dev + HMR @ http://localhost:5173 (proxies to poolmaster.local)
npm run build                           # writes /data/ (LittleFS image source)
npm run upload                          # OTA LittleFS push via `pio run -e OTA_upload -t uploadfs`
```

### Install as a PWA

On Chrome / Edge: open `http://poolmaster.local/` → address-bar install icon → "Install PoolMaster".
On iOS Safari: Share → Add to Home Screen. (Service-worker offline caching is Chromium-only; iOS runs the
app as a standalone web view without the SW.)

### Screens

| Section | Screens |
|---|---|
| 🏠 Home | Dashboard |
| 🎛 Control | Manual, Setpoints |
| 🧪 Tune | Calibration wizard, PID tuning, Schedule, Tanks |
| 📊 Insights | History (60-min charts), Logs, Diagnostics |
| ⚙ Settings | Network (WiFi/MQTT/admin/factory-reset), Firmware update |

All writes are HTTP Basic-auth gated once an admin password is set via Settings.

---

## ⚡ SP1 Modern Plumbing (2026-04) — breaking changes

Version **ESP-SP1** modernizes the firmware plumbing. Upgrading from an earlier
version is a one-way migration; read this before flashing.

### First-install flow (new devices)
1. Copy `include/Secrets.h.example` to `include/Secrets.h` (gitignored). Leave all
   `SEED_*` values empty if you want to go through the captive-portal wizard on
   first boot, OR fill `SEED_WIFI_SSID`/`SEED_WIFI_PSK` for a seeded first boot.
2. Serial-flash the firmware **and** the LittleFS image:
   ```bash
   pio run -e serial_upload -t upload
   pio run -e serial_upload -t uploadfs
   ```
3. On first boot with no WiFi creds, the device starts in AP mode as
   `PoolMaster-Setup-XXXX` (no password). Join from a phone — captive-portal
   popup auto-opens.
4. Complete the wizard: WiFi (required), admin password (skippable), MQTT broker
   (skippable), timezone (skippable).
5. Device reboots, joins your WiFi, appears at `http://poolmaster.local/`.
6. If Home Assistant is running with MQTT, the `PoolMaster` device auto-appears
   in Devices (no YAML required).

### Upgrade flow (existing ESP-3.3b devices)

Your existing `WIFI_NETWORK`/`WIFI_PASSWORD`/`OTA_PWDHASH`/`MQTT_SERVER_IP`
macros in `Config.h` are gone. Copy their values into
`include/Secrets.h.example` → `include/Secrets.h` (gitignored) as the `SEED_*`
fields. On first boot after flashing, the seed values are copied into NVS; you
can then blank `Secrets.h` and subsequent boots use NVS.

**Flash with serial once** — OTA from ESP-3.3b to SP1 is unsupported because the
partition table changes:

```bash
pio run -e serial_upload -t erase         # wipes NVS + OTA slots
pio run -e serial_upload -t upload
pio run -e serial_upload -t uploadfs
```

After the first successful SP1 boot, subsequent firmware updates use OTA:

```bash
POOLMASTER_OTA_PWD='<your admin pwd>' pio run -e OTA_upload -t upload
```

Or upload a binary from the web UI at `http://poolmaster.local/update`.

### Home Assistant migration

The legacy topics (`/Home/Pool/Meas1`, `/Home/Pool/API`, etc.) are no longer
published. Tear down any custom `sensor:` / `switch:` MQTT YAML in
`configuration.yaml` that referenced those topics. SP1 publishes HA Discovery
configs; your PoolMaster will appear automatically under Settings → Devices →
PoolMaster with ~38 entities (sensors, switches, numbers, binary sensors,
buttons).

On first MQTT connect after the upgrade, the firmware also publishes empty
retained payloads to the old `/Home/Pool*` topics to evict them from the broker
(one-shot, gated by an NVS flag).

### Troubleshooting

- **Device not on mDNS?** Check your router's DHCP client list for the MAC
  `ESP32-PoolMaster` or similar; browse to that IP directly.
- **Forgot admin password?** Erase the whole NVS (requires USB):
  `pio run -e serial_upload -t erase` then re-upload firmware + LittleFS.
- **Stuck in AP mode after WiFi change?** Your saved WiFi is unreachable —
  connect to `PoolMaster-Setup-XXXX` and re-run the wizard.

---

<h2>ESP32 PoolMaster</h2>
<h4>Brief description</h4>
<p> This project is a software port to an ESP32 platform of the PoolMaster project from Loic74650 (https://github.com/Loic74650/PoolMaster)<br />
Compared to the initial project, the main differences are:
<ul>
  <li> ESP32 MCU with WiFi (multiple access points)</li>
  <li> ESP32-Arduino framework, PlatformIO IDE </li>
  <li> async MQTT client</li>
  <li> JSON upgrade to version 6</li>
  <li> lots of code modifications, keeping the general behaviour</li>
  <li> add a fourth pump (cleaning robot)</li>
  <li> manage only 2 relays (+ the four pumps)</li>
  <li> analog measurements done by external ADC I2C module (ADS1115), in async mode</li>
  <li> and more...
</ul>  <br />
  The project isn't a fork of the original one due to the different structure of source files with PlatformIO ((.cpp, .h).
  A dedicated board has been designed to host all components. There are 8 LEDs at the bottom to display status, warnings and alarms.
  <br /><br />
  In version ESP-3.0, the display function has been very simplified (twice less code), using Nextion variables only to deport the logic 
  into the Nextion and updating the display only when it is ON. 
  <br /><br />
  A new version of the board allows the connection of the pH_Orp board from Loïc (https://github.com/Loic74650/pH_Orp_Board/tree/main)  on an additional I2C connector. The sofware is modified accordingly. The configuration is defined in the config.h file. CAD_files 2 Gerber 3 files are provided.
  <br /><br />
  The version V6, (aka ESP-2.0) implement direct usage of FreeRTOS functions for managing tasks and queues. There are 10 tasks sharing the
  app_CPU :
  - The Arduino loopTask, with only the setup() function. When the setup is finished, the task deletes itself to recover memory;
  - PoolMaster, running every 500ms, which mainly supervises the overall timing of the system;
  - AnalogPoll, running every 125ms, to acquire analog measurements of pH, ORP and Pressure with an ADS115 sensor on an I2C bus;
  - GetTemp, running every 1000ms, to acquire water and air temperatures with DS18B20 sensors on two 1Wire busses;
  - ORPRegulation, running every 1000ms, to manage Chlorine pump;
  - pHRegulation, running every 1000ms, to manage Acid/Soda pump;
  - ProcessCommand, running every 500ms, to process commands received on /Home/Pool6/API MQTT Topic;
  - SettingsPublish, running when notified only (e.g with external command), to publish settings on the MQTT topic;
  - MeasuresPublish, running every 30s and when notified, to publish actual measures and status;
  - StatusLights, running every 3000ms, to display a row of 8 status LEDs on the mother board, through a PCF8574A on the I2C bus.

  </p><br /><br />

<p align="center"> <img src="/docs/Profiling.jpg" width="802" title=""> </p>  <br /><br />
<p align="center"> <img src="/docs/PoolMaster_board.JPG" width="802" title="Board"> </p> <br /><br />  
<p align="center"> <img src="/docs/Page 0.JPG" width="300" title=""> </p>
<p align="center"> <img src="/docs/Page 1.JPG" width="300" title=""> </p>
<p align="center"> <img src="/docs/Page 3.JPG" width="300" title=""> </p><br /><br />  
 


<h2>PoolMaster 5.0.0</h2>
<h2>Arduino Mega2560 (or Controllino-Maxi) Ph/Orp (Chlorine) regulation system for home pools</h2>

<br />
<p align="center"> <img src="/docs/PoolMaster_2.jpg" width="802" title="Overview"> </p> <br /><br />

<br />
<p align="center"> <img src="/docs/Grafana.png" width="802" title="Dashboard"> </p> <br /><br />

<br />
<p align="center"> <img src="/docs/Nextion.png" width="802" title="Nextion 3.5" touch screen"> </p> <br /><br />

<h4>Brief description</h4>
	
<p>Four main metrics are measured and periodically reported over MQTT and a 3.5" Nextion touch screen: water temperature and pressure, pH and ORP values.<br />
Pumps states, tank-levels estimates and other parameters are also periodically reported<br />
Two PID regulation loops are running in parallel: one for PH, one for ORP<br />
An additional simple (on/off) regulation loop is handling the water temperature (it starts/stops the house-heating system circulator which brings heat to a heat exchanger mounted on the pool water pipes)<br />
pH is regulated by injecting Acid from a tank into the pool water (a relay starts/stops the Acid peristaltic pump)<br />
ORP is regulated by injecting Chlorine from a tank into the pool water (a relay starts/stops the Chlorine peristaltic pump)<br />
Defined time-slots and water temperature are used to start/stop the filtration pump for a daily given amount of time (a relay starts/stops the filtration pump)<br />
Tank-levels are estimated based on the running-time and flow-rate of each pump.<br />
Ethernet connectivity parameters can be set through a webpage accessible from the LAN at http://PoolMaster.local.<br />
If an ethernet connection is available, the internal clock (RTC) is synchronized with a time-server every day at midnight.<br />

An API function enables telling the system what the outside air temperature is. In case it is below -2.0°C, filtration is started until it rises back above +2.0°C<br />
Communication with the system is performed using the MQTT protocol over an Ethernet connection to the local network/MQTT broker.<br /><br />

Every 30 seconds (by default), the system will publish on the "PoolTopicMeas1" and "PoolTopicMeas2"(see in code below) the following payloads in Json format:<br />
  {"Tmp":818,"pH":321,"PSI":56,"Orp":583,"FilUpT":8995,"PhUpT":0,"ChlUpT":0}<br />
  {"AcidF":100,"ChlF":100,"IO":11,"IO2":0}<br />
  Tmp: measured Water temperature value in °C x100 (8.18°C in the above example payload)<br />
  pH: measured pH value x100 (3.21 in the above example payload)<br />
  Orp: measured Orp (aka Redox) value in mV (583mV in the above example payload)<br />
  PSI: measured Water pressure value in bar x100 (0.56bar in the above example payload)<br />
  FiltUpT: current running time of Filtration pump in seconds (reset every 24h. 8995secs in the above example payload)<br />
  PhUpT: current running time of Ph pump in seconds (reset every 24h. 0secs in the above example payload)<br />
  ChlUpT: current running time of Chl pump in seconds (reset every 24h. 0secs in the above example payload)<br />
  AcidF: percentage fill estimate of acid tank ("pHTank" command must have been called when a new acid tank was set in place in order to have accurate value)<br />
  ChlF: percentage fill estimate of Chlorine tank ("ChlTank" command must have been called when a new Chlorine tank was set in place in order to have accurate value)<br />
IO: a variable of type BYTE where each individual bit is the state of a digital input on the Arduino. These are :<br />	
<ul>
<li>FiltPump: current state of Filtration Pump (0=on, 1=off)</li>
<li>PhPump: current state of Ph Pump (0=on, 1=off)</li>
<li>ChlPump: current state of Chl Pump (0=on, 1=off)</li>
<li>PhlLevel: current state of Acid tank level (0=empty, 1=ok)</li>
<li>ChlLevel: current state of Chl tank level (0=empty, 1=ok)</li>
<li>PSIError: over-pressure error</li>
<li>pHErr: pH pump overtime error flag</li>
<li>ChlErr: Chl pump overtime error flag</li>
</ul><br />
IO2: a variable of type BYTE where each individual bit is the state of a digital input on the Arduino. These are :<br /><br />
<ul>
<li>pHPID: current state of pH PID regulation loop (1=on, 0=off)</li>
<li>OrpPID: current state of Orp PID regulation loop (1=on, 0=off)</li>
<li>Mode: state of pH and Orp regulation mode (0=manual, 1=auto)</li>
<li>Heat: state of water heat command (0=off, 1=on)</li>
<li>R1: state of Relay1 (0=off, 1=on)</li>
<li>R2: state of Relay2 (0=off, 1=on)</li>
<li>R6: state of Relay6 (0=off, 1=on)</li>
<li>R7: state of Relay7 (0=off, 1=on)</li>
</ul><br />

<h4>How to compile</h4>
<p>
- this code was developped for two main hardware configurations (see list in the hardware section below):<br /> 
<ul>
<li>Controllino-Maxi or</li> 
<li>Arduino Mega 2560 + Ethernet shield + relay shield + RTC module</li></ul>
- select the target board type in the Arduino IDE (either "Arduino Mega 2560" or "Controllino Maxi") code should compile for both types<br />


<h4>Compatibility</h4>
	
<p>For this sketch to work on your setup you must change the following in the code (in the "Config.h" file):<br />
- possibly the pinout definitions depending on your wiring<br />
- the unique address of the DS18b20 water temperature sensor<br />
- MAC and IP address of the Ethernet shield<br />
- MQTT broker IP address and login credentials<br />
- possibly the topic names on the MQTT broker to subscribe and publish to<br />
- the Kp,Ki,Kd parameters for both PID loops in case your peristaltic pumps have a different throughput than 1.5Liters/hour for the pH pump and 3.0Liters/hour for the Chlorine pump. Also the default Kp values were adjusted for a 50m3 pool volume. You might have to adjust the Kp values in case of a different pool volume and/or peristaltic pumps throughput (start by adjusting it proportionally). In any case these parameters are likely to require adjustments for every pool<br /></p>

<h4>Tips</h4>
Before attempting to regulate your pool water with this automated system, it is essential that you start with:<br />
1- testing your water quality (using liquid kits and/or test strips for instance) and balancing it properly (pH, Chlorine, Alkalinity, Hardness). Proper water balancing will greatly ease the pH stability and regulation<br />
2- calibrating the pH probe using calibrated buffer solutions (pay attention to the water temperature which plays a big role in pH readings)<br />
3- adjusting pH to 7.4 <br />
4- once above steps 1 to 3 are ok, you can start regulating ORP<br /><br />
 
Notes:<br />
a/ the ORP sensor should theoretically not be calibrated nore temperature compensated (by nature its 0mV pivot point cannot shift)<br />
b/ the ORP reading is strongly affected by the pH value and the water temperature. Make sure pH is adjusted at 7.4<br />
c/ prefer platinium ORP probes for setpoints >500mV (ie. Pools and spas)<br />
e/ the response time of ORP sensors can be fast in reference buffer solutions (10 secs) and yet very slow in pool water (minutes or more) as it depends on the water composition <br /><br />


<p align="center"> <img src="/docs/PoolMaster.jpg" width="702" title="Overview"> </p> <br /><br />
<p align="center"> <img src="/docs/PoolMasterBox_pf.jpg" width="702" title="Overview"> </p> <br /><br />

<h4>MQTT API</h4>
<p>
Below are the Payloads/commands to publish on the "PoolTopicAPI" topic (see hardcoded in code) in Json format in order to launch actions on the Arduino:<br />
<ul>
<li>{"Mode":1} or {"Mode":0}         -> set "Mode" to manual (0) or Auto (1). In Auto, filtration starts/stops at set times of the day and pH and Orp are regulated</li> 
<li>{"Heat":1} or {"Heat":0}         -> start/stop the regulation of the pool water temperature</li>
<li>{"FiltPump":1} or {"FiltPump":0} -> manually start/stop the filtration pump</li>
<li>{"ChlPump":1} or {"ChlPump":0}   -> manually start/stop the Chl pump to add more Chlorine</li>
<li>{"PhPump":1} or {"PhPump":0}     -> manually start/stop the Acid pump to lower the Ph</li>
<li>{"PhPID":1} or {"PhPID":0}       -> start/stop the Ph PID regulation loop</li>
<li>{"OrpPID":1} or {"OrpPID":0}     -> start/stop the Orp PID regulation loop</li>
<li>{"PhCalib":[4.02,3.8,9.0,9.11]}  -> multi-point linear regression calibration (minimum 1 point-couple, 6 max.) in the form [ProbeReading_0, BufferRating_0, xx, xx, ProbeReading_n, BufferRating_n]
<li>{"OrpCalib":[450,465,750,784]}   -> multi-point linear regression calibration (minimum 1 point-couple, 6 max.) in the form [ProbeReading_0, BufferRating_0, xx, xx, ProbeReading_n, BufferRating_n]
<li>{"PhSetPoint":7.4}               -> set the Ph setpoint, 7.4 in this example</li>
<li>{"OrpSetPoint":750.0}            -> set the Orp setpoint, 750mV in this example</li>
<li>{"WSetPoint":27.0}               -> set the water temperature setpoint, 27.0deg in this example</li>
<li>{"WTempLow":10.0}                -> set the water low-temperature threshold below which there is no need to regulate Orp and Ph (ie. in winter)</li>
<li>{"OrpPIDParams":[2857,0,0]}      -> respectively set Kp,Ki,Kd parameters of the Orp PID loop. In this example they are set to 2857, 0 and 0</li>
<li>{"PhPIDParams":[1330000,0,0.0]}  -> respectively set Kp,Ki,Kd parameters of the Ph PID loop. In this example they are set to 1330000, 0 and 0</li>
<li>{"OrpPIDWSize":3600000}           -> set the window size of the Orp PID loop in msec, 60mins in this example</li>
<li>{"PhPIDWSize":1200000}            -> set the window size of the Ph PID loop in msec, 20mins in this example</li>
<li>{"Date":[1,1,1,18,13,32,0]}      -> set date/time of RTC module in the following format: (Day of the month, Day of the week, Month, Year, Hour, Minute, Seconds), in this example: Monday 1st January 2018 - 13h32mn00secs</li>
<li>{"FiltT0":9}                     -> set the earliest hour (9:00 in this example) to run filtration pump. Filtration pump will not run beofre that hour</li>
<li>{"FiltT1":20}                    -> set the latest hour (20:00 in this example) to run filtration pump. Filtration pump will not run after that hour</li>
<li>{"PubPeriod":30}                 -> set the periodicity (in seconds) at which the system info (pumps states, tank levels states, measured values, etc) will be published to the MQTT broker</li>
<li>{"PumpsMaxUp":1800}              -> set the Max Uptime (in secs) for the Ph and Chl pumps over a 24h period. If over, PID regulation is stopped and a warning flag is raised</li>
<li>{"Clear":1}                      -> reset the pH and Orp pumps overtime error flags in order to let the regulation loops continue. "Mode" also needs to be switched back to Auto (1) after an error flag was raised</li>
<li>{"DelayPID":30}                  -> Delay (in mins) after FiltT0 before the PID regulation loops will start. This is to let the Orp and pH readings stabilize first. 30mins in this example. Should not be > 59mins</li>
<li>{"TempExt":4.2}                  -> Provide the system with the external temperature. Should be updated regularly and will be used to start filtration when temperature is less than 2°C. 4.2deg in this example</li>
<li>{"PSIHigh":1.0}                  -> set the water high-pressure threshold (1.0bar in this example). When water pressure is over that threshold, an error flag is set</li>
<li>{"pHTank":[20,100]}              -> call this function when the Acid tank is replaced or refilled. First parameter is the tank volume in Liters, second parameter is its percentage fill (100% when full)</li>
<li>{"ChlTank":[20,100]}             -> call this function when the Chlorine tank is replaced or refilled. First parameter is the tank volume in Liters, second parameter is its percentage fill (100% when full)</li>
<li>{"Relay":[1,1]}                  -> call this generic command to actuate spare relays. Parameter 1 is the relay number (R1 in this example), parameter 2 is the relay state (ON in this example). This command is useful to use spare relays for additional features (lighting, etc). Available relay numbers are 1,2,6,7,8,9</li>
<li>{"Reboot":1}                     -> call this command to reboot the controller (after 8 seconds from calling this command)</li>
<li>{"pHPumpFR":1.5}                 -> call this command to set pH pump flow rate un L/s. In this example 1.5L/s</li>
<li>{"ChlPumpFR":3}                  -> call this command to set Chl pump flow rate un L/s. In this example 3L/s</li>
<li>{"RstpHCal":1}                   -> call this command to reset the calibration coefficients of the pH probe</li>
<li>{"RstOrpCal":1}                  -> call this command to reset the calibration coefficients of the Orp probe</li>
<li>{"RstPSICal":1}                  -> call this command to reset the calibration coefficients of the pressure sensor</li>


</ul>
</p><br />


<h4>Hardware</h4>
<p>
<ul>
<li><a title="https://www.controllino.biz/product/controllino-maxi/" href="https://www.controllino.biz/product/controllino-maxi/">x1 CONTROLLINO MAXI (ATmega2560)</a> or Arduino Mega 2560 + Ethernet shield + relay shield + RTC module</li>
<li><a title="https://www.phidgets.com/?tier=3&catid=11&pcid=9&prodid=103" href="https://www.phidgets.com/?tier=3&catid=11&pcid=9&prodid=103">x2 Phidgets PH/ORB amplifier modules</a></li> 
<li><a title="https://www.dfrobot.com/product-1621.html" href="https://www.dfrobot.com/product-1621.html">x2 Galvanic isolator for the pH and Orp probes</a></li> 
<li><a title="https://www.trattamento-acque.net/dosaggio/pompe-peristaltiche/pompe-a-portata-fissa/pompa-serie-mp2-p-detail.html" href="https://www.trattamento-acque.net/dosaggio/pompe-peristaltiche/pompe-a-portata-fissa/pompa-serie-mp2-p-detail.html">x2 Peristaltic pumps, suction lances for tanks, pH and Orp probes</a></li>
<li><a title="http://electrolyseur.fr/pool-terre.html" href="http://electrolyseur.fr/pool-terre.html">x1 Water grounding</a></li>
<li><a title="http://electrolyseur.fr/kit-sonde-DS18B20-filtration-piscine.html" href="http://electrolyseur.fr/kit-sonde-DS18B20-filtration-piscine.html">x1 Water temperature probe (DS18B20)</a></li>
<li><a title="https://fr.aliexpress.com/item/OOTDTY-G1-4-Pouces-5-v-0-0-5-MPa-Pression-Capteur-Capteur-D-huile-Carburant/32851667666.html?transAbTest=ae803_5&ws_ab_test=searchweb0_0%2Csearchweb201602_3_10065_10068_319_10892_317_10696_10084_453_454_10083_10618_10304_10307_10820_10821_537_10302_536_10902_10843_10059_10884_10887_321_322_10103%2Csearchweb201603_57%2CppcSwitch_0&algo_pvid=2456b33d-d7ee-4515-863d-af0c6b322395&algo_expid=2456b33d-d7ee-4515-863d-af0c6b322395-20
" href="https://fr.aliexpress.com/item/OOTDTY-G1-4-Pouces-5-v-0-0-5-MPa-Pression-Capteur-Capteur-D-huile-Carburant/32851667666.html?transAbTest=ae803_5&ws_ab_test=searchweb0_0%2Csearchweb201602_3_10065_10068_319_10892_317_10696_10084_453_454_10083_10618_10304_10307_10820_10821_537_10302_536_10902_10843_10059_10884_10887_321_322_10103%2Csearchweb201603_57%2CppcSwitch_0&algo_pvid=2456b33d-d7ee-4515-863d-af0c6b322395&algo_expid=2456b33d-d7ee-4515-863d-af0c6b322395-20
">x1 Pressure sensor</a></li>
<li><a title="https://www.itead.cc/nextion-nx4832k035.html" href="https://www.itead.cc/nextion-nx4832k035.html">x1 Nextion Enhanced NX4832K035 - Generic 3.5'' HMI Touch Display</a></li>

</ul>
</p><br />

<h4>Wiring</h4>
<p>
Below is a (quick and dirty) wiring diagram with the Controllino MAXI. Right click and display image in full screen to see the details.
<p align="center"> <img src="/docs/Wiring.jpg" width="702" title="Wiring"> </p> <br />
</p>

<h4>Cloud integration example (<a title="https://blynk.io/" href="https://blynk.io/">BLYNK</a>)</h4>
<p>
See NodeRed folder for more info and code
<p align="center"> <img src="/docs/Blynk2.jpg" width="302" title="Blynk"> </p> <br />
<p align="center"> <img src="/docs/NodeRed-to-Blynk.jpg" width="702" title="Blynk"> </p> <br />
</p>

<h4>Non-cloud home automation integration example (Node-Red + InfluxDB + Grafana)</h4>
<p>
See the NodeRed folder for more info and this <a title="https://www.youtube.com/watch?v=JdV4x925au0" href="https://www.youtube.com/watch?v=JdV4x925au0">tutorial</a> on how to create a Grafana dashboard from MQTT data.<br />
<p align="center"> <img src="/docs/Grafana.png" width="702" title="Overview"> </p> <br />
<p align="center"> <img src="/docs/NodeRedDashboard.png" width="702" title="Overview"> </p> <br />

</p>

<h4>Non-cloud home automation integration example (<a title="https://www.jeedom.com" href="https://www.jeedom.com">JEEDOM</a>)</h4>
<p>
<p align="center"> <img src="/docs/JeedomInterface.jpg" width="702" title="Overview"> </p> <br />
<p align="center"> <img src="/docs/TuileJeedom.jpg" width="702" title="Overview"> </p> <br />
<p align="center"> <img src="/docs/VirtuelJeedom.jpg" width="702" title="Overview"> </p> <br />
</p>
