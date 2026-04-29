import { signal } from '@preact/signals';

export interface PoolState {
  measurements: {
    ph: number;
    orp: number;
    water_temp: number;
    air_temp: number;
    pressure: number;       // bar
    pressure_psi: number;   // derived from pressure × 14.5038
  };
  setpoints: {
    ph: number;
    orp: number;
    water_temp: number;
  };
  pumps: {
    filtration: boolean;
    ph: boolean;
    chl: boolean;
    robot: boolean;
    filt_uptime_s: number;
    ph_uptime_s: number;
    chl_uptime_s: number;
  };
  tanks: {
    acid_fill_pct: number;
    chl_fill_pct: number;
  };
  relays: {
    r0: boolean;
    r1: boolean;
  };
  modes: {
    auto: boolean;
    winter: boolean;
    ph_pid: boolean;
    orp_pid: boolean;
  };
  alarms: {
    pressure: boolean;
    ph_pump_overtime: boolean;
    chl_pump_overtime: boolean;
    acid_tank_low: boolean;
    chl_tank_low: boolean;
  };
  diagnostics: {
    firmware: string;
    uptime_s: number;
    free_heap: number;
    wifi_rssi: number;
    ssid: string;
    ip: string;
    reset_reason: number;
    reset_reason_text: string;
    boot_count: number;
    prev_uptime_s: number;
  };
  mqtt: {
    connected: boolean;
    host: string;
    port: number;
    last_disconnect_code: number;  // -1 = never; >=0 = espMqttClient DisconnectReason
  };
}

export const poolState = signal<PoolState | null>(null);

export function applyState(data: PoolState) {
  poolState.value = data;
}
