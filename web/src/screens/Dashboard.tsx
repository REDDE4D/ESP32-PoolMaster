import { useComputed } from '@preact/signals';
import { poolState } from '../stores/state';
import { Tile } from '../components/Tile';
import { Badge } from '../components/Badge';
import { AlarmBanner } from '../components/AlarmBanner';
import { fmtNum, fmtInt, fmtDuration } from '../lib/format';

export function Dashboard() {
  const s = useComputed(() => poolState.value);

  if (!s.value) {
    return <div class="glass p-6">Loading pool state…</div>;
  }
  const st = s.value;

  return (
    <div class="space-y-4 max-w-4xl">
      <AlarmBanner />

      <div class="flex items-baseline justify-between">
        <h1 class="text-2xl font-bold">🏊 Pool</h1>
        <div class="flex gap-2">
          {st.modes.auto   && <Badge variant="info" dot>Auto</Badge>}
          {st.modes.winter && <Badge variant="info" dot>Winter</Badge>}
        </div>
      </div>

      <div class="grid grid-cols-2 lg:grid-cols-4 gap-3">
        <Tile label="pH"        value={fmtNum(st.measurements.ph, 2)}        sub={`target ${fmtNum(st.setpoints.ph, 1)}`} />
        <Tile label="ORP"       value={fmtInt(st.measurements.orp)}   unit="mV" sub={`target ${fmtInt(st.setpoints.orp)} mV`} />
        <Tile label="Water"     value={fmtNum(st.measurements.water_temp, 1)} unit="°C" sub={`target ${fmtNum(st.setpoints.water_temp, 1)} °C`} />
        <Tile label="Air"       value={fmtNum(st.measurements.air_temp, 1)}   unit="°C" />
        <Tile label="Pressure"  value={fmtNum(st.measurements.pressure, 2)}    unit="bar"
              sub={`${fmtNum(st.measurements.pressure_psi, 1)} psi`} />
        <Tile label="Acid tank" value={fmtInt(st.tanks.acid_fill_pct)}  unit="%" />
        <Tile label="Chl tank"  value={fmtInt(st.tanks.chl_fill_pct)}   unit="%" />
        <Tile label="Uptime"    value={fmtDuration(st.diagnostics.uptime_s)} />
      </div>

      <div class="glass p-3">
        <div class="label-caps mb-2">Pumps</div>
        <div class="flex flex-wrap gap-2">
          <Badge variant={st.pumps.filtration ? 'info' : 'muted'} dot>
            Filtration · {st.pumps.filtration ? `running · ${fmtDuration(st.pumps.filt_uptime_s)}` : 'idle'}
          </Badge>
          <Badge variant={st.pumps.ph  ? 'info' : 'muted'} dot>pH pump · {st.pumps.ph  ? 'running' : 'idle'}</Badge>
          <Badge variant={st.pumps.chl ? 'info' : 'muted'} dot>Chl pump · {st.pumps.chl ? 'running' : 'idle'}</Badge>
          <Badge variant={st.pumps.robot ? 'info' : 'muted'} dot>Robot · {st.pumps.robot ? 'running' : 'idle'}</Badge>
        </div>
      </div>

      <div class="glass p-3">
        <div class="label-caps mb-2">Device</div>
        <div class="grid grid-cols-2 gap-x-4 gap-y-1 text-sm">
          <div class="opacity-60">Firmware</div>      <div class="val-num">{st.diagnostics.firmware}</div>
          <div class="opacity-60">IP</div>            <div class="val-num">{st.diagnostics.ip}</div>
          <div class="opacity-60">WiFi SSID</div>     <div class="val-num">{st.diagnostics.ssid}</div>
          <div class="opacity-60">RSSI</div>          <div class="val-num">{fmtInt(st.diagnostics.wifi_rssi)} dBm</div>
          <div class="opacity-60">Free heap</div>     <div class="val-num">{fmtInt(st.diagnostics.free_heap)} B</div>
        </div>
      </div>
    </div>
  );
}
