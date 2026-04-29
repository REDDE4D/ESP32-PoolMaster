import { useComputed, useSignal } from '@preact/signals';
import { poolState } from '../stores/state';
import { apiGet, apiSendCommand } from '../lib/api';
import { fmtBytes, fmtDuration, fmtInt } from '../lib/format';
import { Badge } from '../components/Badge';
import { SectionTabs, TABS_INSIGHTS } from '../components/SectionTabs';

interface I2cScan {
  found: Array<{ addr: string; likely?: string }>;
  count: number;
  sda: number;
  scl: number;
}

// espMqttClient DisconnectReason → human label
// (espMqttClient/src/TypeDefs.h, enum class DisconnectReason).
const MQTT_DISCONNECT_REASONS: Record<number, string> = {
  [-1]: 'never disconnected',
   [0]: 'user disconnected',
   [1]: 'unacceptable protocol version',
   [2]: 'identifier rejected',
   [3]: 'server unavailable',
   [4]: 'malformed credentials',
   [5]: 'not authorized (bad user/pass)',
   [6]: 'TLS bad fingerprint',
   [7]: "TCP disconnected (broker unreachable / wrong host or port)",
};

export function Diagnostics() {
  const s = useComputed(() => poolState.value);
  const scan = useSignal<I2cScan | null>(null);
  const scanning = useSignal(false);

  const doScan = async () => {
    scanning.value = true;
    const res = await apiGet<I2cScan>('/api/i2c/scan');
    if (res.ok && res.data) scan.value = res.data;
    scanning.value = false;
  };

  const clearErrors    = () => apiSendCommand('{"Clear":1}');
  const reboot         = () => apiSendCommand('{"Reboot":1}');
  const reconnectMqtt  = () => apiSendCommand('{"MqttReconnect":1}');

  if (!s.value) return <div class="glass p-6">Loading…</div>;
  const d = s.value.diagnostics;
  const m = s.value.mqtt;

  return (
    <div class="space-y-4 max-w-3xl">
      <SectionTabs current="/insights/diagnostics" tabs={TABS_INSIGHTS} />
      <h1 class="text-xl font-bold">Diagnostics</h1>

      <div class="glass p-5">
        <h2 class="font-semibold mb-2">Device</h2>
        <div class="grid grid-cols-2 gap-x-4 gap-y-1 text-sm">
          <div class="opacity-60">Firmware</div>   <div class="val-num">{d.firmware}</div>
          <div class="opacity-60">Uptime</div>     <div class="val-num">{fmtDuration(d.uptime_s)}</div>
          <div class="opacity-60">Free heap</div>  <div class="val-num">{fmtBytes(d.free_heap)}</div>
          <div class="opacity-60">WiFi RSSI</div>  <div class="val-num">{fmtInt(d.wifi_rssi)} dBm</div>
          <div class="opacity-60">SSID</div>       <div class="val-num">{d.ssid}</div>
          <div class="opacity-60">IP</div>         <div class="val-num">{d.ip}</div>
          <div class="opacity-60">Last reset</div>
          <div class="val-num">
            {d.reset_reason_text}
            {(d.reset_reason === 3 /* PANIC */ || d.reset_reason === 4 /* INT_WDT */ || d.reset_reason === 5 /* TASK_WDT */ || d.reset_reason === 6 /* WDT */ || d.reset_reason === 8 /* BROWNOUT */) && (
              <span class="ml-2 text-rose-300 text-xs">⚠</span>
            )}
          </div>
          <div class="opacity-60">Boot count</div>   <div class="val-num">{fmtInt(d.boot_count)}</div>
          <div class="opacity-60">Prev session uptime</div>
          <div class="val-num">
            {d.prev_uptime_s > 0 ? fmtDuration(d.prev_uptime_s) : '—'}
          </div>
        </div>
      </div>

      <div class="glass p-5">
        <div class="flex items-center justify-between mb-2">
          <h2 class="font-semibold">MQTT broker</h2>
          <Badge variant={m.connected ? 'ok' : (m.host ? 'alarm' : 'muted')} dot>
            {m.connected ? 'Connected' : (m.host ? 'Disconnected' : 'Not configured')}
          </Badge>
        </div>
        <div class="grid grid-cols-2 gap-x-4 gap-y-1 text-sm">
          <div class="opacity-60">Broker</div>
          <div class="val-num">{m.host ? `${m.host}:${m.port}` : '—'}</div>
          {!m.connected && m.host && (
            <>
              <div class="opacity-60">Last reason</div>
              <div class="val-num">{MQTT_DISCONNECT_REASONS[m.last_disconnect_code] ?? `code ${m.last_disconnect_code}`}</div>
            </>
          )}
        </div>
        {m.connected && (
          <p class="text-xs opacity-60 mt-2">
            HA autodiscovery payloads were published on the last connect. If HA isn't picking them up,
            check that its MQTT integration uses the same broker and is subscribed to <code>homeassistant/#</code>.
          </p>
        )}
        <div class="mt-3">
          <button class="text-xs px-3 py-1.5 rounded-md bg-aqua-primary/15 border border-aqua-primary/35 text-cyan-200"
                  onClick={reconnectMqtt}>
            Reconnect MQTT
          </button>
        </div>
      </div>

      <div class="glass p-5">
        <div class="flex items-center justify-between mb-2">
          <h2 class="font-semibold">I²C bus</h2>
          <button class="text-xs px-3 py-1 rounded-md bg-aqua-primary/15 border border-aqua-primary/35 text-cyan-200"
                  onClick={doScan} disabled={scanning.value}>
            {scanning.value ? 'Scanning…' : 'Scan'}
          </button>
        </div>
        {scan.value && (
          <div class="text-sm">
            <div class="opacity-60 mb-2">SDA={scan.value.sda} · SCL={scan.value.scl} · {scan.value.count} device(s)</div>
            <ul class="space-y-1 font-mono text-xs">
              {scan.value.found.map(f => (
                <li key={f.addr}><span class="text-cyan-300">{f.addr}</span> — {f.likely ?? 'unknown'}</li>
              ))}
            </ul>
          </div>
        )}
      </div>

      <div class="glass p-5">
        <h2 class="font-semibold mb-3">Actions</h2>
        <div class="flex gap-2 flex-wrap">
          <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-warn/15 border border-amber-500/40 text-amber-200"
                  onClick={clearErrors}>Clear error flags</button>
          <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-alarm/15 border border-rose-500/40 text-rose-200"
                  onClick={() => confirm('Reboot the controller?') && reboot()}>Reboot device</button>
        </div>
      </div>
    </div>
  );
}
