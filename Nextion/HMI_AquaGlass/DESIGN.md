# Aqua Glass HMI — Build Sheet

Self-contained reference for replicating the SP7 Aqua Glass HMI in Nextion Editor. Keep this open in a side window while you build.

**Panel:** Nextion NX4832T035_011R · 480 × 320 · resistive · landscape · UTF-8.

---

## 0 — Workflow checklist

1. [Setup](#1--project-setup) — new project, model, fonts, 4 pages
2. [Page 0 Home](#5--page-0--home)
3. [Page 1 Switches](#6--page-1--switches)
4. [Page 2 Calibs](#7--page-2--calibs-orphan-copy-from-hmi_origine) — copy verbatim from HMI_Origine, do not redesign
5. [Page 3 Settings](#8--page-3--settings)
6. [Compile](#10--compile--deploy) → `bianyi/PoolMaster.tft`
7. [SD-flash](#10--compile--deploy) → smoke test on hardware

Whenever you save: **Ctrl+S**. To compile a check build: **F5**. To produce the deployment `.tft`: **Ctrl+B** or *File → TFT File Output*.

---

## 1 — Project setup

| Setting | Value |
|---|---|
| File menu | *File → New* |
| Save as | `Nextion/HMI_AquaGlass/PoolMaster.HMI` |
| Device model | **NX4832T035_011R** |
| Orientation | **Horizontal (Landscape)** |
| Encoding | **UTF-8** |
| Initial page count | **4** (page0, page1, page2, page3 — Nextion auto-numbers) |
| Default page background | `#062E4A` (RGB565 = `361`) |

Add the four bitmap fonts via *Tools → Font → Add Font*, in this order so the indices match the table below:

| Index | File | Approx px height |
|---|---|---|
| 0 | `ressources/AN16.zi` | 16 |
| 1 | `ressources/AN24.zi` | 24 |
| 2 | `ressources/AN32.zi` | 32 |
| 3 | `ressources/AN72.zi` | 72 |

(Already copied from `Nextion/HMI_BlueTheme/ressources/`.)

---

## 2 — Color palette (Aqua Glass)

Nextion uses RGB565 (0–65535). The decimal value is what you type in the color picker; hex is just for visual reference.

| Role | Hex | RGB565 dec | Where it appears |
|---|---|---|---|
| Background (panel base) | `#062E4A` | `361` | Page background everywhere |
| Card background | `#0A3850` | `1130` | All glass cards |
| Card border | `#1A5070` | `2698` | 1-px outer rectangle around cards |
| Text primary | `#D8F0FF` | `55679` | Numbers, button labels |
| Text label / secondary | `#7FC5E8` | `32669` | Card labels (pH, ORP, …), setpoints, status |
| Accent / "on" cyan bg | `#0E4A5C` | `1929` | On-state card background |
| Accent / "on" cyan border | `#22D3EE` | `9181` | On-state border + cyan numbers |
| Accent / "on" cyan text | `#67E8F9` | `26367` | "Running"/"On" labels, active nav |
| Warn / dosing bg | `#4A2D0A` | `19232` | Dosing-state pH/Chl card bg |
| Warn / dosing border | `#FF8C00` | `64480` | Dosing border + Clear-errors button border |
| Warn / dosing text | `#FFCC66` | `65420` | Dosing labels, Clear-errors button label |
| Alarm bg | `#3C0A0A` | `15393` | Alarm banner + Reboot button bg |
| Alarm border | `#FF5050` | `65034` | Alarm banner left border + Reboot button border |
| Alarm text | `#FFD0D0` | `65426` | Alarm banner text + Reboot button label |
| Nav bar bg | `#051F33` | `296` | Bottom nav background tint |
| Nav active pill bg | `#0A4A5A` | `1163` | Active tab pill background |
| Nav inactive separator | `#1A5070` | `2698` | 1-px top border of nav |

If your color picker only takes hex, paste the `#` value. If it only takes decimal, paste the RGB565 column. Both yield the same color.

---

## 3 — Geometry constants

Reused on every page so the chrome lines up perfectly.

| Element | x | y | w | h |
|---|---|---|---|---|
| Top bar | 0 | 0 | 480 | 24 |
| Body area | 0 | 24 | 480 | 256 |
| Bottom nav background | 0 | 280 | 480 | 40 |
| Nav top border (1px line) | 0 | 280 | 480 | 1 |

Body padding: **10 px** outer on all sides → usable inner: `(10, 34)` to `(470, 270)` = 460 × 236 px.

---

## 4 — Bottom nav (identical on page0, page1, page3)

Place these exact widgets on each visible page. **Do not put nav widgets on page2** (orphan).

| objname | type | x | y | w | h | font | bco | pco | text | Touch Release |
|---|---|---|---|---|---|---|---|---|---|---|
| `navBg` | Rectangle | 0 | 280 | 480 | 40 | — | `#051F33` (296) | — | — | — |
| `navTopLine` | Rectangle | 0 | 280 | 480 | 1 | — | `#1A5070` (2698) | — | — | — |
| `navHomeBtn` | Hotspot (`m0`) | 12 | 286 | 152 | 28 | — | — | — | — | `page page0` |
| `navHomeLbl` | Text | 12 | 286 | 152 | 28 | 0 | `#0A4A5A` on home, else transparent | `#67E8F9` on home, else `#D8F0FF` | `Home` | — |
| `navSwBtn` | Hotspot | 170 | 286 | 152 | 28 | — | — | — | — | `page page1` |
| `navSwLbl` | Text | 170 | 286 | 152 | 28 | 0 | `#0A4A5A` on switches, else transparent | `#67E8F9` on switches, else `#D8F0FF` | `Switches` | — |
| `navSetBtn` | Hotspot | 328 | 286 | 152 | 28 | — | — | — | — | `page page3` |
| `navSetLbl` | Text | 328 | 286 | 152 | 28 | 0 | `#0A4A5A` on settings, else transparent | `#67E8F9` on settings, else `#D8F0FF` | `Settings` | — |

For each Text label set: **font = 0 (AN16)**, **xcen = 1 (centered horizontally)**, **ycen = 1 (centered vertically)**.

The active-pill effect: on page0 the `navHomeLbl` gets `bco = #0A4A5A` and `pco = #67E8F9`; on page1 the `navSwLbl` gets the same; on page3 the `navSetLbl`. The other two stay with transparent bg + `#D8F0FF` text. This is set in the *Attribute panel*, not via script.

**Tip:** build the nav once on page0, then *select all 8 nav widgets → copy → paste onto page1/page3 → adjust the active label*.

---

## 5 — Page 0 — Home

### Top bar

| objname | type | x | y | w | h | font | pco | text / binding | notes |
|---|---|---|---|---|---|---|---|---|---|
| `vaTime` | Text | 8 | 4 | 240 | 18 | 0 | `#7FC5E8` | (firmware writes) | xcen=0, left-align |
| `vaModeLbl` | Text | 380 | 4 | 92 | 18 | 0 | `#67E8F9` | "AUTO"/"MANUAL" | xcen=2, right-align |

Wire the mode display: in the page0 Postinitialize Event:
```
if(vaMode.val==1)
{
  vaModeLbl.txt="AUTO"
}else
{
  vaModeLbl.txt="MANUAL"
}
```

Add the same script to the *Touch Release Event* of the existing `vaMode` Number variable to live-update.

### 2 × 2 measurement cards

Card geometry (4 cards, 8 px gap):

| Card | x | y | w | h |
|---|---|---|---|---|
| pH (top-left) | 10 | 30 | 226 | 90 |
| ORP (top-right) | 244 | 30 | 226 | 90 |
| Temp (bot-left) | 10 | 128 | 226 | 90 |
| Pressure (bot-right) | 244 | 128 | 226 | 90 |

For each card: place a Rectangle at the card geometry with `bco = #0A3850 (1130)`, then a 1-px outer Rectangle at the same coords with `bco = #1A5070 (2698)` for the border (Nextion has no native border attribute).

#### pH card contents

| objname | type | x | y | w | h | font | pco | text |
|---|---|---|---|---|---|---|---|---|
| `phLbl` | Text | 18 | 38 | 100 | 14 | 0 | `#7FC5E8` | `pH` |
| `vapH` | Text | 18 | 56 | 210 | 36 | 2 | `#D8F0FF` | (firmware) |
| `vapHSP` | Text | 18 | 96 | 210 | 14 | 0 | `#7FC5E8` | (firmware) |

Use AN32 for the value, AN16 for label and setpoint. Firmware writes `vapH.txt = "7.31"` and `vapHSP.txt = "SP 7.30"` (already in current firmware).

#### ORP card contents

| objname | type | x | y | w | h | font | pco | text |
|---|---|---|---|---|---|---|---|---|
| `orpLbl` | Text | 252 | 38 | 100 | 14 | 0 | `#7FC5E8` | `ORP` |
| `vaOrp` | Text | 252 | 56 | 180 | 36 | 2 | `#D8F0FF` | (firmware) |
| `orpUnit` | Text | 432 | 70 | 30 | 18 | 0 | `#7FC5E8` | `mV` |
| `vaOrpSP` | Text | 252 | 96 | 210 | 14 | 0 | `#7FC5E8` | (firmware) |

#### Temperature card contents (split — Water primary, Air secondary)

| objname | type | x | y | w | h | font | pco | text |
|---|---|---|---|---|---|---|---|---|
| `wtLbl` | Text | 18 | 136 | 100 | 14 | 0 | `#7FC5E8` | `Water` |
| `vaWT` | Text | 18 | 154 | 100 | 36 | 2 | `#D8F0FF` | (firmware) |
| `wtUnit` | Text | 96 | 168 | 22 | 18 | 0 | `#7FC5E8` | `°C` |
| `tempSep` | Rectangle | 124 | 138 | 1 | 70 | — | `#1A5070` | (vertical separator) |
| `atLbl` | Text | 134 | 136 | 100 | 14 | 0 | `#9FD3EE` | `Air` |
| `vaAT` | Text | 134 | 158 | 90 | 30 | 1 | `#CAE8F6` | (firmware) |
| `atUnit` | Text | 200 | 168 | 22 | 18 | 0 | `#9FD3EE` | `°C` |
| `tempSP` | Text | 18 | 194 | 210 | 14 | 0 | `#7FC5E8` | (water SP, manual or wire to existing) |

Air uses font 1 (AN24) instead of 2 (AN32) and dimmer colors.

#### Pressure card contents

| objname | type | x | y | w | h | font | pco | text |
|---|---|---|---|---|---|---|---|---|
| `presLbl` | Text | 252 | 136 | 100 | 14 | 0 | `#7FC5E8` | `Pressure` |
| `vaPress` | Text | 252 | 154 | 140 | 36 | 2 | `#D8F0FF` | (firmware NEW) |
| `presUnit` | Text | 396 | 168 | 36 | 18 | 0 | `#7FC5E8` | `bar` |
| `presStatus` | Text | 252 | 194 | 210 | 14 | 0 | `#7FC5E8` | (set in script) |

Pressure-status script (in `vaPressOK` post-event):
```
if(vaPressOK.val==1)
{
  presStatus.txt="OK"
  presStatus.pco=32669    // #7FC5E8
}else
{
  presStatus.txt="WARN"
  presStatus.pco=64480    // #FF8C00
}
```

### Pump strip (5 chips, just above alarm banner)

5 chips in a row at y=224, gap 6 px:

| Chip | objname | x | y | w | h | font | bound to | active visual | tap |
|---|---|---|---|---|---|---|---|---|---|
| Filt | `vaFiltChip` | 10 | 224 | 88 | 26 | 0 | `vaFilt.val` | bg `#0E4A5C`, text `#67E8F9` when val==1 | `printh 23 02 54 06` |
| Robot | `vaRobotChip` | 104 | 224 | 88 | 26 | 0 | `vaRobot.val` | same | `printh 23 02 54 07` |
| R0 | `vaR0Chip` | 198 | 224 | 88 | 26 | 0 | `vaR0.val` | same | `printh 23 02 54 08` |
| R1 | `vaR1Chip` | 292 | 224 | 88 | 26 | 0 | `vaR1.val` | same | `printh 23 02 54 09` |
| pH | `vaPhChip` | 386 | 224 | 84 | 26 | 0 | `vaPhPump.val` | bg `#4A2D0A`, text `#FFCC66` when val==1 | (no event — display only) |

For each chip: use a Dual-state Button (`bt`) with two background colors via `bco`/`bco2`, OR a Hotspot + Rectangle + Text trio with a script that swaps colors on val change.

Chip script template (replace `<chip>` and `<var>`):
```
if(<var>.val==1)
{
  <chip>.bco=1929        // #0E4A5C cyan on
  <chip>.pco=26367       // #67E8F9
}else
{
  <chip>.bco=1130        // #0A3850 default
  <chip>.pco=55679       // #D8F0FF
}
```

For the pH chip (orange dosing instead of cyan), use `bco=19232` and `pco=65420`.

### Alarm banner (conditional, above nav)

| objname | type | x | y | w | h | font | bco | pco | text |
|---|---|---|---|---|---|---|---|---|---|
| `vaAlarmTxt` | Text | 10 | 254 | 460 | 22 | 0 | `#3C0A0A` (15393) | `#FFD0D0` (65426) | (set in script) |

Initial `vis = 0` (hidden).

Script wired to **post-event of each of these vars**: `vapHUTErr`, `vaChlUTErr`, `vaPSIErr`, `vapHLevel`, `vaChlLevel`. Same script in all 5:

```
if(vaPSIErr.val==1)
{
  vaAlarmTxt.txt="⚠ Water pressure"
  vis vaAlarmTxt,1
}else if(vapHUTErr.val==1)
{
  vaAlarmTxt.txt="⚠ pH pump overtime"
  vis vaAlarmTxt,1
}else if(vaChlUTErr.val==1)
{
  vaAlarmTxt.txt="⚠ Chl pump overtime"
  vis vaAlarmTxt,1
}else if(vapHLevel.val==1)
{
  vaAlarmTxt.txt="⚠ Acid tank low"
  vis vaAlarmTxt,1
}else if(vaChlLevel.val==1)
{
  vaAlarmTxt.txt="⚠ Chlorine tank low"
  vis vaAlarmTxt,1
}else
{
  vis vaAlarmTxt,0
}
```

### Page-loaded trigger

In page0's **Postinitialize Event**, add as the *last* line (after any other init):
```
printh 23 02 54 01
```
This fires firmware's `trigger1()`. Required — no firmware change without it.

### Bottom nav

Build per [Section 4](#4--bottom-nav-identical-on-page0-page1-page3). Active = Home.

---

## 6 — Page 1 — Switches

### Top bar + nav

Copy from page0. Update nav active = Switches.

Postinitialize Event:
```
printh 23 02 54 02
```

### 3 × 2 toggle grid

Card geometry (6 cards, 8 px gap):

| Position | Card | x | y | w | h |
|---|---|---|---|---|---|
| (col 1, row 1) | Filt | 10 | 30 | 144 | 84 |
| (col 2, row 1) | Robot | 162 | 30 | 144 | 84 |
| (col 3, row 1) | pH | 314 | 30 | 144 | 84 |
| (col 1, row 2) | Chl | 10 | 122 | 144 | 84 |
| (col 2, row 2) | R0 | 162 | 122 | 144 | 84 |
| (col 3, row 2) | R1 | 314 | 122 | 144 | 84 |

For each card, place:

| Sub-element | type | rel x | rel y | rel w | rel h | font | text |
|---|---|---|---|---|---|---|---|
| Card bg | Rectangle | 0 | 0 | 144 | 84 | — | (state-driven bco) |
| Card border | Rectangle (1-px) | 0 | 0 | 144 | 84 | — | `#1A5070` |
| Name label | Text | 4 | 8 | 136 | 22 | 1 (AN24) | (chip name) |
| State label | Text | 4 | 38 | 136 | 16 | 0 | (state-driven) |
| Uptime label | Text | 4 | 60 | 136 | 16 | 0 | (firmware var) |

`xcen = 1` (centered) for all 3 text widgets.

#### Bindings + events per chip

| Card | bg objname | name | state from | uptime var | tap event |
|---|---|---|---|---|---|
| Filt | `vaFiltBtn` | `Filtration` | `vaFilt.val` | `vaFiltUp.txt` | `printh 23 02 54 06` |
| Robot | `vaRobotBtn` | `Robot` | `vaRobot.val` | `vaRobotUp.txt` | `printh 23 02 54 07` |
| pH | `vaPhBtn` | `pH Pump` | `vaPhPump.val` (NEW) | `vaPhUp.txt` (NEW) | (none — display only) |
| Chl | `vaChlBtn` | `Chl Pump` | `vaChlPump.val` (NEW) | `vaChlUp.txt` (NEW) | (none) |
| R0 | `vaR0Btn` | `Relay R0` | `vaR0.val` | `—` (literal text) | `printh 23 02 54 08` |
| R1 | `vaR1Btn` | `Relay R1` | `vaR1.val` | `—` (literal text) | `printh 23 02 54 09` |

#### State-color script (wire to post-event of each binding var)

For Filt / Robot / R0 / R1 chips (cyan on / default off):

```
if(vaFilt.val==1)
{
  vaFiltBtn.bco=1929        // #0E4A5C cyan
  vaFiltStateTxt.txt="Running"
  vaFiltStateTxt.pco=26367
  vaFiltNameTxt.pco=55679
}else
{
  vaFiltBtn.bco=2898        // #0A3850 default
  vaFiltStateTxt.txt="Off"
  vaFiltStateTxt.pco=32669
  vaFiltNameTxt.pco=55679
}
```

For pH / Chl chips (orange dosing / default off):

```
if(vaPhPump.val==1)
{
  vaPhBtn.bco=19232        // #4A2D0A orange
  vaPhStateTxt.txt="Dosing"
  vaPhStateTxt.pco=65420
}else
{
  vaPhBtn.bco=2898         // default
  vaPhStateTxt.txt="Off"
  vaPhStateTxt.pco=32669
}
```

(Replace `Filt` / `Ph` with the relevant chip's prefix in each script.)

---

## 7 — Page 2 — Calibs (orphan, copy from `HMI_Origine`)

**Do not redesign.** This page exists only so firmware's `trigger3` and `trigger11` stay valid.

1. Open `Nextion/HMI_Origine/PoolMaster.HMI` in a separate Nextion Editor window.
2. Navigate to its page2 (Calibs).
3. *Select all → Ctrl+C*.
4. Switch to your `HMI_AquaGlass` project, navigate to its page2.
5. *Ctrl+V* to paste.

Confirm page2's Postinitialize Event contains:
```
printh 23 02 54 03
```

That's it. The page is unreachable from the new nav but its widgets are intact and `trigger3` / `trigger11` will fire if anyone ever lands on it.

---

## 8 — Page 3 — Settings

### Top bar + nav

Copy from page0. Update nav active = Settings.

Postinitialize Event (also runs the concat-line scripts below):
```
printh 23 02 54 04
vaFwLine.txt=vaMCFW.txt+" · "+vaTFTFW.txt
vaWifiLine.txt=vaSSID.txt+" · "+vaRSSI.txt
```

### Info card

Outer: a Rectangle at `(10, 30, 460, 178)` with `bco = #0A3850 (1130)`, plus a 1-px border Rectangle in `#1A5070 (2698)`.

Inside, 7 rows. Each row has a label on the left and a value on the right. Label font 0 (AN16) `#7FC5E8`, value font 0 (AN16) `#D8F0FF`.

Add a 1-px separator Rectangle (`#1A5070`) at each `y + 18` between rows.

| Row | y | Label objname | Label text | Value objname | Bound to firmware |
|---|---|---|---|---|---|
| 1 | 38 | `fwLbl` | `FIRMWARE` | `vaFwLine` | (script-built from `vaMCFW` + `vaTFTFW`) |
| 2 | 60 | `wifiLbl` | `WIFI` | `vaWifiLine` | (script-built from `vaSSID` + `vaRSSI`) |
| 3 | 82 | `ipLbl` | `IP` | `vaIP` | `page3.vaIP.txt` (existing) |
| 4 | 104 | `mqttLbl` | `MQTT` | `vaMqttStat` | `page3.vaMqttStat.txt` (NEW) |
| 5 | 126 | `upLbl` | `UPTIME` | `vaUptime` | `page3.vaUptime.txt` (NEW) |
| 6 | 148 | `heapLbl` | `FREE HEAP` | `vaFreeHeap` | `page3.vaFreeHeap.txt` (NEW) |
| 7 | 170 | `winLbl` | `WINTER MODE` | `vaWinter` | `page3.vaWinter.txt` (NEW) |

Each label: `(x=20, w=140, h=18)`, `xcen=0` (left-align). Each value: `(x=170, w=290, h=18)`, `xcen=2` (right-align).

`vaFwLine` and `vaWifiLine` are **String variables** added to page3 via right-click → Add → Variable. Their `.txt` is populated by the Postinitialize script + by re-running the same 2 lines on the post-events of `vaMCFW`, `vaTFTFW`, `vaSSID`, `vaRSSI` (so they stay live as firmware updates the underlying vars).

### Action row (bottom of body, above nav)

Two buttons, each 224 × 32, gap 12 px, at y=218:

| objname | type | x | y | w | h | font | bco | bordercolor | pco | text | Touch Release |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `btnClear` | Button | 10 | 218 | 224 | 32 | 1 | `#3C2008` (15392) | `#FF8C00` (64480) | `#FFCC66` (65420) | `Clear errors` | `printh 23 02 54 0F` |
| `btnReboot` | Button | 246 | 218 | 224 | 32 | 1 | `#3C0A0A` (15393) | `#FF5050` (65034) | `#FFD0D0` (65426) | `Reboot` | `printh 23 02 54 10` |

`xcen = 1` (centered text). Border emulated by a 1-px outer Rectangle in the bordercolor (Nextion buttons don't have a native border attribute).

---

## 9 — Variable bindings cheat sheet (firmware → HMI)

These are the 30+ variables firmware already wrote, plus the 13 SP7 added. The HMI must contain Number / Text Variables with these exact `objname`s on the listed page.

### Already written by firmware (existing — do not rename)

| Page | objname | type | source |
|---|---|---|---|
| page0 | `vaTime` | Text | clock string |
| page0 | `vaNetW` | Number | MQTT badge 0/1 |
| page0 | `vaStaSto` | Text | pump status string |
| page0 | `vaMode` | Number | auto/manual 0/1 |
| page0 | `vapH` | Text | "7.31" |
| page0 | `vaOrp` | Text | "685" |
| page0 | `vaWT` | Text | "26.4" |
| page0 | `vaAT` | Text | "22.1" |
| page0 | `vapHSP` | Text | "SP 7.30" |
| page0 | `vaOrpSP` | Text | "SP 700" |
| page0 | `vapHPID` | Text | PID output |
| page0 | `vaOrpPID` | Text | PID output |
| page0 | `vapHLevel` | Number | 0/1 acid tank low |
| page0 | `vaChlLevel` | Number | 0/1 chl tank low |
| page0 | `vaPSIErr` | Number | 0/1 |
| page0 | `vaChlUTErr` | Number | 0/1 chl pump overtime |
| page0 | `vapHUTErr` | Number | 0/1 ph pump overtime |
| page1 | `vaFilt` | Number | 0/1 |
| page1 | `vaRobot` | Number | 0/1 |
| page1 | `vaR0` | Number | 0/1 |
| page1 | `vaR1` | Number | 0/1 |
| page1 | `vaR2` | Number | 0/1 (winter mode — also displayed on Settings now) |
| page3 | `vaMCFW` | Text | "ESP-SP1" |
| page3 | `vaTFTFW` | Text | "TFT-2.0" |
| page3 | `vaSSID` | Text | WiFi SSID |
| page3 | `vaIP` | Text | "10.25.25.40" |

### NEW for SP7 (firmware now writes these — Task 4)

| Page | objname | type | example |
|---|---|---|---|
| page0 | `vaPress` | Text | "1.32" |
| page0 | `vaPressOK` | Number | 0/1 (1=OK) |
| page1 | `vaPhPump` | Number | 0/1 |
| page1 | `vaChlPump` | Number | 0/1 |
| page1 | `vaFiltUp` | Text | "3h 24m" |
| page1 | `vaRobotUp` | Text | "0m" |
| page1 | `vaPhUp` | Text | "12m" |
| page1 | `vaChlUp` | Text | "8m" |
| page3 | `vaRSSI` | Text | "-66 dBm" |
| page3 | `vaMqttStat` | Text | "connected" / "disconnected" |
| page3 | `vaUptime` | Text | "3h 24m" |
| page3 | `vaFreeHeap` | Text | "102 KB" |
| page3 | `vaWinter` | Text | "on" / "off" |

To add a Variable in Nextion Editor: right-click the page in the Page panel → Add → Variable → choose Number or Text → set `objname` to match exactly.

---

## 10 — Trigger numbers cheat sheet (HMI button → firmware)

Wire each interactive widget's *Touch Release Event* with the matching `printh` line. Bytes are: `23 02 54 NN` where NN is the trigger number in hex.

| Trigger | NN hex | What | Wired on |
|---|---|---|---|
| `trigger1` | 01 | page0 loaded | page0 Postinitialize |
| `trigger2` | 02 | page1 loaded | page1 Postinitialize |
| `trigger3` | 03 | page2 loaded | page2 Postinitialize (orphan, copied from HMI_Origine) |
| `trigger4` | 04 | page3 loaded | page3 Postinitialize |
| `trigger5` | 05 | Mode toggle | (legacy — no widget on new HMI; trigger keeps working if anyone fires it) |
| `trigger6` | 06 | Filt toggle | Home Filt chip + Switches Filt card |
| `trigger7` | 07 | Robot toggle | Home Robot chip + Switches Robot card |
| `trigger8` | 08 | R0 toggle | Home R0 chip + Switches R0 card |
| `trigger9` | 09 | R1 toggle | Home R1 chip + Switches R1 card |
| `trigger10` | 0A | Winter toggle | (legacy — no widget on new HMI) |
| `trigger11` | 0B | Calibs OK | (orphan, page2 only) |
| `trigger12` | 0C | Clear errors (legacy) | (legacy — superseded by trigger15 on Settings) |
| `trigger13` | 0D | pH PID toggle | (legacy — no widget on new HMI) |
| `trigger14` | 0E | Orp PID toggle | (legacy — no widget on new HMI) |
| **`trigger15`** | **0F** | **Clear errors button (NEW)** | **Settings → Clear errors** |
| **`trigger16`** | **10** | **Reboot button (NEW)** | **Settings → Reboot** |

The "legacy — no widget on new HMI" triggers stay compiled in firmware. They simply never fire because no widget is wired to them in the redesign. That's fine.

---

## 11 — Compile + deploy

### Compile

1. *File → Compile* (or hit **F5**) to validate. Fix any red errors in the compile log.
2. *File → TFT File Output* (or **Ctrl+B**). Output goes to `Nextion/HMI_AquaGlass/bianyi/PoolMaster.tft`.
3. Confirm file exists and is ~1–4 MB:
   ```bash
   ls -lh Nextion/HMI_AquaGlass/bianyi/PoolMaster.tft
   ```

### Deploy via SD card

1. Format a microSD as **FAT32** (Disk Utility on Mac, or just any small SD that's already FAT32).
2. Copy:
   ```bash
   cp Nextion/HMI_AquaGlass/bianyi/PoolMaster.tft /Volumes/<SD_NAME>/
   dot_clean -m /Volumes/<SD_NAME>/
   ls -la /Volumes/<SD_NAME>/    # confirm exactly ONE .tft, no ._ files
   diskutil eject /Volumes/<SD_NAME>
   ```
3. Power off the Nextion (unplug 5V).
4. Insert SD into the panel slot.
5. Power on. Bootloader auto-detects → flashes (~60–90 s, progress on screen).
6. When done, "Update OK" appears. Power off, remove SD, power on. Panel boots into the new HMI.

### Smoke test

Run SC1–SC9 from §14 of the design spec. If anything fails, fix in Editor, re-compile, re-flash.

---

## 12 — Common gotchas

- **"variable not defined" error mid-runtime:** typo in `objname`. Check exact spelling case-sensitively against §9.
- **Button doesn't fire firmware action:** wrong byte in `printh`. Check §10 NN column. The 4 bytes are `23 02 54 NN`.
- **Color looks wrong:** Nextion's color picker rounds to RGB565. The hex column in §2 is the *true* color; what the panel renders may be 1–2 shades off. Acceptable.
- **Page background not applied:** Page → Attribute → bco. Each of the 4 pages needs it set independently.
- **macOS "multiple TFT files" error on SD-flash:** Finder wrote `._PoolMaster.tft` shadow. Run `dot_clean -m /Volumes/SD/` to strip them.
- **Nextion Editor crashes during compile:** save first, restart Editor, try again. Common with large `.HMI` files (>10 MB).
- **Panel shows "system data error" after a partial flash:** the previous attempt corrupted the firmware. Just SD-flash a complete `.tft` again — recovers cleanly.

---

## 13 — Reference files

- This sheet: `Nextion/HMI_AquaGlass/DESIGN.md`
- Mockups (open in browser, optionally): `.superpowers/brainstorm/30732-1777212516/content/density-v3.html` (Home), `switches-settings.html` (Switches), `settings-v3.html` (Settings)
- Spec: `docs/superpowers/specs/2026-04-26-sp7-nextion-aqua-glass-design.md`
- Plan: `docs/superpowers/plans/2026-04-26-sp7-nextion-aqua-glass.md`
