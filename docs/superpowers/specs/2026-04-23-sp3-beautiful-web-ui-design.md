# ESP32-PoolMaster — Sub-Project 3: Beautiful Web UI

**Status:** Approved (design)
**Date:** 2026-04-23
**Scope:** Responsive Preact + Tailwind + Vite PWA served from LittleFS, backed by a WebSocket data feed and the existing SP1 REST surface. 12 feature screens under 5 sections, "Pool Aqua Glass" aesthetic, admin-gated writes.
**Owner:** Sebastian

---

## 0. Roadmap context

Third of five modernization sub-projects. SP1 shipped (deps bump, web server, OTA, captive portal, HA Discovery). SP2 (settings promotion) was deferred — we'll drip its features into SP3's UI as concrete screens instead of a standalone refactor.

| # | Sub-project | Status |
|---|---|---|
| SP1 | Modern plumbing | Shipped |
| SP2 | Settings promotion | Deferred — subsumed into SP3 screens |
| **SP3** | **Beautiful web UI** (this spec) | **In design** |
| SP4 | External relay outputs (Shelly/MQTT) | Not started |
| SP5 | Nextion HMI redesign + wireless `.tft` upload | Not started |

**Upstream decisions that constrain SP3:**
- SP1 `AsyncWebServer` on port 80, `LittleFS` at `/data/`, admin auth via `WebAuth::requireAdmin`, `Credentials::*` NVS-backed config, `enqueueCommand()` → `queueIn` → `ProcessCommand` one-way command path. SP3 builds on all of this without refactoring the backend.
- Current bench device is on AP Garten WiFi, running `ESP-SP1` with DS18B20 temps + internal ADS1115 + PCF8574A connected; pH/ORP calibration defaults reset to single-ended coefficients (pending recalibration with buffer solutions).
- No TLS (home-LAN trust model, SP1 §6.6). No physical access required for iteration (OTA works).

---

## 1. Problem statement

After SP1, the device is reachable at `http://poolmaster.local/` but serves only a placeholder HTML page + `/healthz` + `/api/state` + `/api/i2c/scan` + a handful of POST endpoints. Every interaction today is either:
- a `curl` command to an `/api/...` endpoint,
- a toggle in Home Assistant (which duplicates the MQTT-discovery entity set), or
- a Nextion touch tap (untouched, unchanged, utilitarian).

For everyday at-the-pool use with a phone, none of those is great:
- HA is clicky for quick read-and-dismiss.
- `curl` needs a laptop.
- Nextion is fine for "glance" but has no guided wizards (calibration, PID tuning) and is locked to the Nextion Editor workflow.

SP3 adds a polished, responsive web UI that makes everyday pool control, calibration, and diagnostics pleasant on a phone and useful on a desktop.

## 2. Goals and non-goals

### Goals
1. A single-bundle Preact SPA served from LittleFS, with an Aqua-glass aesthetic tailored to outdoor phone use.
2. Twelve feature screens across five top-level sections (Home, Control, Tune, Insights, Settings).
3. Real-time data via a single WebSocket endpoint (`/ws`): state updates, history, logs, alarms — no polling.
4. Full PWA: manifest, icons, service worker for offline-shell loading and "Add to Home Screen" install.
5. Admin-gated write endpoints via HTTP Basic auth; public read access to dashboard / state / logs / diagnostics so the device remains embeddable and friction-free on a trusted LAN.
6. Hot-reload dev workflow (`npm run dev` with Vite proxy to the physical device) so frontend changes land in < 1 second, and OTA `uploadfs` to flash the LittleFS bundle wirelessly.

### Non-goals
- Permanent time-series storage. History is a 60-minute RAM ring; HA handles long-term history.
- Web push notifications. HA does that better.
- A native mobile app. PWA is the native-app moment.
- Any Nextion HMI changes. SP5 handles the touch panel.
- Migrating remaining compile-time `Config.h` entries to NVS via a separate pass. Done opportunistically in the relevant screen (pump flow rates in Tanks, schedule in Schedule, etc.).
- External relay output routing. SP4.

## 3. Success criteria

1. `npm run build` emits `< 500 KB` uncompressed JS into `data/assets/` and the full `data/` LittleFS image fits under 500 KB.
2. Fresh SP3 firmware + LittleFS: loading `http://poolmaster.local/` on a phone shows the dashboard with live readings in under 1 s on warm cache (< 3 s cold).
3. Toggling a switch on a desktop propagates to a phone connected to the same device within 1 s via WebSocket.
4. Running through the pH calibration wizard with actual buffer solutions produces pH readings within ±0.1 of buffer values at the relevant temperature.
5. Pulling WiFi briefly drops the UI into a "reconnecting" banner state; reconnect auto-recovers without a page reload.
6. Bundle builds and installs as a PWA on both iOS Safari (with minor feature gaps: no service-worker offline cache) and Chromium (full PWA).
7. Flash stays ≤ 93 % of 1.44 MB partition after all SP3 C++ modules merge. RAM free heap stays ≥ 120 KB under four simultaneous WebSocket clients.

## 4. Architecture

```
┌──────────────── Phone / Desktop browser ────────────────┐
│                                                          │
│  [Installed PWA or regular tab]                          │
│  Preact SPA (one bundle)                                 │
│   - Router: wouter / preact-iso (~2 KB)                  │
│   - State: Preact signals, small stores per concern      │
│   - uPlot for charts                                     │
│   - Service worker for offline shell + cached assets     │
│                                                          │
└────────┬─────────────────────────────────────────────────┘
         │  HTTP on :80 (home LAN, no TLS — SP1 §6.6)
         │
  ┌──────┴──────────────────────────────────────┐
  │  WebSocket  /ws         primary live bus     │
  │  REST       /api/state  snapshot             │
  │             /api/health liveness             │
  │             /api/i2c/scan diagnostic         │
  │             POST /api/cmd          gated     │
  │             POST /api/wifi/save    gated     │
  │             POST /api/mqtt/save    gated     │
  │             POST /api/calib/reset  gated     │
  │             POST /update           gated     │
  │  Static     /, /*.js, /*.css  (LittleFS)     │
  └────────────────┬─────────────────────────────┘
                   │
                   ▼
  ┌──────────────────────────────────────────────┐
  │  ESP32: ESPAsyncWebServer + AsyncWebSocket   │
  │  SP3 new:                                     │
  │    WebSocketHub / HistoryBuffer / LogBuffer   │
  │    ApiCommand (POST /api/cmd)                 │
  │  SP1 existing (unchanged):                    │
  │    Credentials / WebAuth / Provisioning       │
  │    MqttBridge / MqttPublish / HaDiscovery     │
  │    CommandQueue / Settings / Setup            │
  │    10 FreeRTOS pool tasks                     │
  └──────────────────────────────────────────────┘
```

**Principles:**
1. WebSocket is the live read path. No polling.
2. REST is the mutate path. Every mutation has an admin-gated POST.
3. UI commands flow through the same `enqueueCommand()` bottleneck as MQTT set-topic messages.
4. No backend business logic duplication anywhere.
5. One bundle, one `data/` partition, one build step (`npm run build && pio run -t uploadfs`).

## 5. Library stack

| Concern | Library | Notes |
|---|---|---|
| UI framework | `preact` + `preact/hooks` + `@preact/signals` | 3 KB React-compatible renderer; signals for store |
| Router | `preact-iso` | Official Preact router, ~2 KB, history mode, includes `Suspense` helpers |
| CSS | `tailwindcss` + `postcss` + `autoprefixer` | JIT-purged to ~15 KB gzipped |
| Charts | `uplot` | 40 KB gzip, industry-leading time-series |
| WebSocket | Browser `WebSocket` API | No library |
| Build | `vite` + `@preact/preset-vite` | HMR, proxy, TS, bundle analysis |
| Types | TypeScript (`tsc --noEmit` in lint) | No runtime cost |
| Lint | `eslint` + `@typescript-eslint` | Optional but recommended |
| PWA | hand-written `public/sw.js` + `public/manifest.webmanifest` | Simpler than vite-plugin-pwa for this scale |

Backend additions:
- `ESPAsyncWebServer::AsyncWebSocket` (already in the ESP32Async fork installed by SP1) — no new PlatformIO dep.

## 6. Detailed design

### 6.1 Module layout

**Backend — new:**
| File | Responsibility |
|---|---|
| `src/WebSocketHub.cpp` / `include/WebSocketHub.h` | `AsyncWebSocket` instance, client tracking, `broadcast(type, payload)`, per-client subscription state, routing of incoming `cmd`/`subscribe`/`ping` |
| `src/HistoryBuffer.cpp` / `include/HistoryBuffer.h` | Ring buffer: 5 series × 120 slots × `float32`, 30 s cadence, `append(series, value)`, `snapshot(series) → {t0, step, values}` |
| `src/LogBuffer.cpp` / `include/LogBuffer.h` | 16 KB character ring buffer of structured `{ts, level, msg}` lines, `append(level, fmt, …)`, `snapshot()`, broadcast hook |
| `src/ApiCommand.cpp` / `include/ApiCommand.h` | `POST /api/cmd` admin-gated handler; forwards body to `enqueueCommand()`; thin wrapper |

**Backend — modified (tiny):**
| File | Change |
|---|---|
| `src/Setup.cpp` | Register WebSocketHub, wire Debug.print tee into LogBuffer, periodic state broadcast via WebSocketHub |
| `src/WebServer.cpp` | `onNotFound` now serves `/index.html` from LittleFS for non-`/api/*`, non-asset GETs (SPA history routing) |
| `src/MqttPublish.cpp` | Also `HistoryBuffer::append(...)` on each measurement cycle |

**Frontend — new directory `web/`:**
```
web/
├── package.json, vite.config.ts, tsconfig.json, tailwind.config.js, postcss.config.js
├── index.html               SPA entry
├── public/
│   ├── manifest.webmanifest
│   ├── icon-192.png, icon-512.png
│   └── sw.js                service worker
├── src/
│   ├── main.tsx, app.tsx, styles.css
│   ├── stores/              connection.ts, state.ts, log.ts, history.ts
│   ├── screens/             Dashboard, Control, Setpoints, Calibration, PidTuning,
│   │                        Schedule, Tanks, History, Logs, Diagnostics,
│   │                        Settings, FirmwareUpdate
│   ├── components/          Tile, Toggle, Slider, Modal, Chart, AlarmBanner, NavShell
│   └── lib/                 api.ts, ws.ts, format.ts
└── README.md                dev/build instructions
```

**`.gitignore` additions** (SP3):
```
# SP3 frontend
/web/node_modules/
/web/dist/
# SP3 build artifacts in LittleFS source
/data/index.html
/data/manifest.webmanifest
/data/sw.js
/data/icon-*.png
/data/assets/
```
`data/setup.html`, `data/setup.css`, `data/robots.txt` stay tracked (hand-written captive-portal assets from SP1).

### 6.2 Build pipeline

- `web/vite.config.ts` outputs to `../data/` (absolute path resolves to LittleFS source).
- `web/package.json` scripts: `dev`, `build`, `preview`, `lint`, `upload` (calls `pio run -e OTA_upload -t uploadfs` from repo root).
- Dev proxy routes `/api/*`, `/healthz`, `/ws` to `http://poolmaster.local`; browser loads SPA from `http://localhost:5173` with HMR, live data from the real device. No CORS setup needed.
- Bundle size guard: the build step fails if total `data/assets/*.js` exceeds 500 KB uncompressed.

### 6.3 WebSocket protocol

**Endpoint:** `GET /ws`, upgraded to WebSocket. Practical ceiling ~4 concurrent clients (RAM-bounded; `AsyncWebSocket::cleanupClients()` is called each second to evict zombies).

**Message envelope (both directions):** `{ "type": "<tag>", ...fields }`

**Client → Server:**
| Type | Payload | Purpose |
|---|---|---|
| `hello` | `{ ver: 1, subs: ["state","logs","history"] }` | first message after connect |
| `subscribe` | `{ topics: [...] }` | opt-in to additional streams |
| `unsubscribe` | `{ topics: [...] }` | drop streams |
| `cmd` | `{ id: "uuid-v4", payload: "{\"FiltPump\":1}" }` | equivalent to POST `/api/cmd`; server ACKs with matching `id` |
| `ping` | `{}` | keepalive |

**Server → Client:**
| Type | Fields | When |
|---|---|---|
| `welcome` | `{ device, firmware, server_time }` | on connect |
| `state` | `{ data: {...same as /api/state } }` | on connect + on change + at most every 5 s |
| `alarm` | `{ id, on, msg? }` | on state change only |
| `log` | `{ ts, level, msg }` | for subscribed clients |
| `history` | initial: `{ series, t0, step_s, values }`; incremental: `{ series, append: [...] }` | on subscribe (full) + on measurement cycle (append) |
| `ack` | `{ id, ok, queued?, error? }` | response to `cmd` |
| `pong` | `{}` | reply to `ping` |

**Auth on `/ws`:** unauthenticated by default (read-only streams). `cmd` messages require the browser to have sent Basic auth on a prior request (browsers cache Basic creds per-origin). If a `cmd` arrives without auth, server responds `ack { ok: false, error: "auth_required" }` and the UI triggers a silent GET to a gated endpoint (`/api/whoami` — 401 unless authed) to force the browser's native auth dialog, then retries.

**Reconnect:** client-side exponential backoff 1 → 2 → 4 → 8 → 16 → 30 s cap; replays `subscribe` intent on reconnect.

**Backpressure:** before each broadcast, check `client->queueLen()`; if a client's pending queue exceeds a tunable threshold (default 16 messages), skip that client for the current broadcast. If the client stays over threshold for > 5 s, the server closes it and the client auto-reconnects. Log broadcasts skip first in preference to state/alarm.

### 6.4 Screen inventory & navigation

Five top-level sections, 12 screens total:

```
🏠 Home
   └── Dashboard                 live tiles + alarm banner + pump state row
🎛 Control
   ├── Manual                     toggle pumps, mode, relays (optimistic UI)
   └── Setpoints                  pH / ORP / water-temp target sliders
🧪 Tune
   ├── Calibration                multi-step wizard for pH + ORP
   ├── PID                        Kp/Ki/Kd + live output chart
   ├── Schedule                   filtration/robot timing
   └── Tanks                      refill + volume/flow-rate config
📊 Insights
   ├── History                    60-min pH/ORP/temp/pressure charts
   ├── Logs                       streamed Debug.print + level filter
   └── Diagnostics                firmware info, I2C scan, clear errors, reboot
⚙ Settings
   ├── Network                    WiFi, MQTT, timezone, admin password, factory reset
   └── Firmware                   drag-drop OTA (firmware + LittleFS) with progress
```

**Responsive nav:** sidebar on ≥ 768 px, top-brand-bar + bottom tabs on mobile, connection-state indicator always visible.

**Route table:** history-mode routing. `/`, `/control`, `/control/setpoints`, `/tune`, `/tune/pid`, `/tune/schedule`, `/tune/tanks`, `/insights`, `/insights/logs`, `/insights/diagnostics`, `/settings`, `/settings/firmware`. Deep links work; `manifest.webmanifest.start_url` configurable.

### 6.5 Visual system — Pool Aqua Glass

**Palette:**
- Background gradient: `#062e4a → #0a4565 → #0e5d75` (140° angle)
- Surface (tiles): `rgba(255,255,255,0.07)` + `backdrop-filter: blur(10px)` + border `rgba(125,211,252,0.15)` + inner top highlight `inset 0 1px 0 rgba(255,255,255,0.08)`
- Elevated surface: `rgba(255,255,255,0.12)` + border `rgba(125,211,252,0.25)`
- Text: primary `#e6f8ff`, label `#7dd3fc` (uppercase caps, letter-spacing 0.08 em), muted `#64748b`
- Accent: cyan-400 `#22d3ee` (primary), cyan-300 `#67e8f9` (hover)
- Status: ok `#34d399`, warn `#fbbf24`, alarm `#f43f5e`, info `#22d3ee`

**Typography:**
- Font stack: `-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif` (zero-KB system UI)
- Monospace (logs, badges): `ui-monospace, SFMono-Regular, Menlo, monospace`
- Scale: 32/700 hero, 20/600 section, 16/400 body, 14/500 label-large, 12/600 small-caps (uppercase tracking)
- Numeric data: `font-variant-numeric: tabular-nums`, weight 700 for prominent values (pH, ORP, temp)

**Components (named, reusable):**
- `<Tile>` — glass primitive, composable header / value / sub
- `<Toggle>` — 42×24 pill switch with cyan on-state
- `<Slider>` — range input + tabular-numeric value display
- `<Modal>` — for wizard steps
- `<Chart>` — uPlot wrapper with Aqua theme (cyan strokes, grey grid on transparent bg)
- `<AlarmBanner>` — full-width rose-tinted bar, auto-hides when clear
- `<NavShell>` — provides sidebar / bottom tabs, connection indicator, section scaffold
- `<Badge>` — dot prefix + label; variants ok / info / warn / alarm

**Icons:** inline SVG from `lucide-preact` (~5 KB, tree-shaken) — only 15-ish icons used total.

### 6.6 Backend implementation notes

- **`Debug.print` tee** (the Arduino_DebugUtils library doesn't expose a sink interface): introduce `src/DebugTee.h` that wraps `Debug.print(...)` behind a macro that also calls `LogBuffer::append(level, fmt, ...)`. Replace every call site via sed; low-risk mechanical change. If the Arduino_DebugUtils API grows a sink interface in a future release, migrate cleanly.
- **Periodic WebSocket state broadcast**: a new FreeRTOS task `WsBroadcast` (2 KB stack, priority 1) running every 1 s that compares a snapshot of `storage` + pump states to the previous snapshot and broadcasts a `state` message only when something changed, with a 5 s safety heartbeat.
- **History sampling:** `MqttPublish::MeasuresPublish` already runs every 30 s; hook `HistoryBuffer::append` at the same cadence at the end of that loop — single source of truth.
- **Alarm detection:** derive from `PSIError`, `PhPump.UpTimeError`, `ChlPump.UpTimeError`, `!PhPump.TankLevel()`, `!ChlPump.TankLevel()` in `WebSocketHub::broadcastStateIfChanged`. When any flag flips on, emit `alarm { on: true }`; when it flips off, emit `alarm { on: false }`. No runtime poll — change detection only.

### 6.7 Auth model

- Read-only routes unauthenticated: `/`, `/*.js`, `/*.css`, `/healthz`, `/api/state`, `/api/i2c/scan`, `/ws` (and all stream messages on it).
- Write routes admin-gated via existing `WebAuth::requireAdmin`: `/api/cmd`, `/api/wifi/save`, `/api/wifi/factory-reset`, `/api/mqtt/save`, `/api/calib/reset-defaults`, `/update`.
- Behavior when no admin password set in NVS: `requireAdmin` grants access (pre-setup state, SP1 §6.6).
- UI pattern: on the first `cmd` with auth_required, force a GET to `/api/whoami` which 401s, triggering the browser's native Basic auth dialog. Credential is then cached for the page session.

## 7. Migration

| Area | Before SP3 | After SP3 |
|---|---|---|
| `data/index.html` | 30-line placeholder | Vite-generated SPA entry (gitignored) |
| SPA routing | N/A | `onNotFound` serves `/index.html` for non-`/api/` GETs |
| Live state access | `curl /api/state` or HA Discovery | `/ws` push + SPA |
| Log access | Serial monitor or SD (disabled) | Ring buffer via `/ws logs` subscription |
| Frontend source | No frontend source | `/web/` Vite project |
| Node toolchain | Not required | Node 20 LTS required for `npm run build` |

## 8. Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| `AsyncWebSocket` drops clients under memory pressure | Medium | Medium | Backpressure: skip broadcast to any client with `queueLen()` above threshold; close client after 5 s sustained over-limit; client auto-reconnects. Drop log broadcasts first. |
| `Debug.print` hook breaks semantics | Medium | Low | Wrapper + sed migration; existing call sites unchanged in behavior. |
| SPA history routing breaks on refresh | Medium | Medium | `onNotFound` fallback to `/index.html` for non-`/api/`, non-asset GETs. Router falls through client-side. |
| PWA install fails on HTTP non-localhost | Low | Low | Chromium allows private-IP HTTP PWA install; Firefox/Safari require HTTPS. Document in README. |
| Calibration wizard captures noisy ADC readings | Medium | Medium | Live reading with 30 s rolling-std indicator; "Capture" disabled until `std < 2 mV` sustained 5 s. |
| Bundle outgrows LittleFS as features grow | Very low | Medium | Guard rail fails build if bundle > 500 KB. Room for ~5× growth before pressure. |
| Running `npm run build` on stale Node | Low | Low | `.nvmrc` pinning Node 20 LTS; README step. |
| Pool safety regression | Low | Critical | SP3 does not touch `pHRegulation`, `OrpRegulation`, `PoolMaster`, or the `Pump` class. All changes sit at the presentation layer. |

## 9. Testing plan

- **Static:** `npm run lint` (ESLint + `tsc --noEmit`). `pio run` with `-Werror=return-type`, `-Wall`.
- **Bundle guard:** build step fails if `data/assets/*.js` > 500 KB uncompressed.
- **Bench smoke checklist** (run once per major feature merge, full pass before shipping):
  1. Load `http://poolmaster.local/` on a phone → SPA shell renders < 1 s warm, < 3 s cold.
  2. Add to Home Screen → PWA launches fullscreen.
  3. Dashboard live tiles; WiFi off → "reconnecting" banner; WiFi back → clears, readings resume.
  4. Toggle filtration pump → relay clicks; state reflects within 1 s.
  5. Slide pH setpoint to 7.4 → `/api/state` confirms + NVS persists across reboot.
  6. Calibration → pH buffer flow → coefficients update in `storage`.
  7. PID → change Kp → output chart reacts within one regulation window.
  8. Schedule 9-20 → filtration task respects new bounds.
  9. Tanks → refill acid → `acid_fill_pct` = 100.
  10. History → charts populate with 60 min of data; refresh rebuilds from server.
  11. Logs → level filter works; new lines stream live.
  12. Diagnostics → firmware info matches `/healthz`; I2C scan matches curl result.
  13. Settings → change admin password → subsequent writes prompt for new password.
  14. Firmware → drag-drop `.bin` → reboot + reconnect + new version visible.
- **Multi-client test:** phone + desktop connected simultaneously → pump toggle from desktop reflects on phone within 1 s.
- **24-hour burn-in** after merge: pool logic unchanged; verify no WS-induced heap leak, `free_heap` stable.
- **No automated E2E** for SP3. Playwright against physical hardware is backlogged for when a regression earns it.

## 10. Forward-compatibility notes

- **SP4** will add per-output relay routing (GPIO, Shelly HTTP, Shelly MQTT, generic MQTT). The SP3 Control and Tanks screens will grow one extra field per pump for "output" selection. The `/api/state` shape stays stable; SP4 just adds configurable routing behind the scenes.
- **SP5** (Nextion redesign + wireless `.tft` upload) will add a "Display" section to the Settings sidebar with a `.tft` drag-drop endpoint built on the same pattern as `/update`.
- **Long-term history** beyond 60 minutes remains HA's responsibility. If we ever need on-device multi-day history, we'd add an SD card + a new `HistoryStore` with the same `HistoryBuffer` interface.
- **Audit log / command history** is an obvious SP3.5 add — record every `cmd` to a persistent ring on LittleFS with timestamp + source (UI / HA / curl) so we can track who changed what.
