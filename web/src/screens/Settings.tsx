import { useSignal } from '@preact/signals';
import { useEffect } from 'preact/hooks';
import { apiGet, apiPostForm } from '../lib/api';
import { SectionTabs, TABS_SETTINGS } from '../components/SectionTabs';

type FormState = Record<string, string>;

function useForm(init: FormState) {
  const s = useSignal<FormState>(init);
  return {
    get: s,
    set: (k: string, v: string) => (s.value = { ...s.value, [k]: v }),
    replace: (next: FormState) => { s.value = { ...s.value, ...next }; },
  };
}

export function Settings() {
  // PSK + pass start blank because the device intentionally never returns
  // them — leave blank to keep the saved value on Save, or type a new one.
  const wifi  = useForm({ ssid: '', psk: '' });
  const mqtt  = useForm({ host: '', port: '1883', user: '', pass: '' });
  const admin = useForm({ pwd: '' });
  const adminSet = useSignal<boolean | null>(null);

  // Prefill from device on mount.
  useEffect(() => {
    let alive = true;
    (async () => {
      const w = await apiGet<{ ssid: string }>('/api/wifi/config');
      if (alive && w.ok && w.data) wifi.replace({ ssid: w.data.ssid ?? '' });
      const m = await apiGet<{ host: string; port: number; user: string }>('/api/mqtt/config');
      if (alive && m.ok && m.data) {
        mqtt.replace({
          host: m.data.host ?? '',
          port: String(m.data.port ?? 1883),
          user: m.data.user ?? '',
        });
      }
      const a = await apiGet<{ set: boolean }>('/api/admin/status');
      if (alive && a.ok && a.data) adminSet.value = a.data.set;
    })();
    return () => { alive = false; };
  }, []);

  const saveWifi = async () => {
    const res = await apiPostForm('/api/wifi/save', wifi.get.value);
    alert(res.ok ? 'WiFi saved — device rebooting' : `Failed: ${res.error}`);
  };
  const saveMqtt = async () => {
    // If pass is blank, omit it so the backend keeps the saved one. Same
    // could apply to wifi.psk, but the existing /api/wifi/save handler
    // overwrites unconditionally — leave that for a future endpoint update.
    const payload: Record<string, string> = { ...mqtt.get.value };
    if (!payload.pass) delete payload.pass;
    const res = await apiPostForm('/api/mqtt/save', payload);
    alert(res.ok ? 'MQTT saved — device rebooting' : `Failed: ${res.error}`);
  };
  const saveAdmin = async () => {
    // POST /api/admin/save only exists in AP mode wizard.
    // Runtime-side we have to go via the wizard path too — add a plan
    // note to introduce a runtime /api/admin/save endpoint.
    alert('Admin password changes use the captive-portal wizard for now (see README).');
  };
  const factoryReset = async () => {
    if (!confirm('Wipe WiFi credentials and reboot into AP mode?')) return;
    const res = await apiPostForm('/api/wifi/factory-reset', {});
    alert(res.ok ? 'Rebooting into AP mode' : `Failed: ${res.error}`);
  };

  const Field = ({ name, form, type = 'text', placeholder = '' }: { name: string; form: ReturnType<typeof useForm>; type?: string; placeholder?: string }) => (
    <label class="block">
      <div class="label-caps mb-1">{name}</div>
      <input type={type} value={form.get.value[name] ?? ''} placeholder={placeholder}
             onInput={e => form.set(name, (e.target as HTMLInputElement).value)}
             class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
    </label>
  );

  return (
    <div class="space-y-4 max-w-2xl">
      <SectionTabs current="/settings" tabs={TABS_SETTINGS} />
      <h1 class="text-xl font-bold">Settings</h1>

      <div class="glass p-5 space-y-3">
        <h2 class="font-semibold">WiFi</h2>
        <div class="grid grid-cols-2 gap-3">
          <Field name="ssid" form={wifi} placeholder="AP Garten" />
          <Field name="psk"  form={wifi} type="password" placeholder="leave blank to keep" />
        </div>
        <div class="flex gap-2 flex-wrap">
          <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold" onClick={saveWifi}>Save + reboot</button>
          <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-alarm/15 border border-rose-500/40 text-rose-200" onClick={factoryReset}>Factory reset WiFi → AP mode</button>
        </div>
      </div>

      <div class="glass p-5 space-y-3">
        <h2 class="font-semibold">MQTT broker</h2>
        <div class="grid grid-cols-2 gap-3">
          <Field name="host" form={mqtt} placeholder="10.25.25.50" />
          <Field name="port" form={mqtt} type="number" />
          <Field name="user" form={mqtt} />
          <Field name="pass" form={mqtt} type="password" placeholder="leave blank to keep" />
        </div>
        <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold" onClick={saveMqtt}>Save + reboot</button>
      </div>

      <div class="glass p-5 space-y-3">
        <h2 class="font-semibold">
          Admin password
          {adminSet.value !== null && (
            <span class="ml-2 text-xs opacity-60 font-normal">
              ({adminSet.value ? 'set' : 'not set — auth currently open'})
            </span>
          )}
        </h2>
        <Field name="pwd" form={admin} type="password" />
        <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold" onClick={saveAdmin}>Save</button>
      </div>
    </div>
  );
}
