import { useSignal } from '@preact/signals';
import { SectionTabs, TABS_SETTINGS } from '../components/SectionTabs';

type UploadType = 'firmware' | 'littlefs';

export function Firmware() {
  const type = useSignal<UploadType>('firmware');
  const file = useSignal<File | null>(null);
  const progress = useSignal(0);
  const msg = useSignal('');
  const uploading = useSignal(false);

  const upload = () => {
    if (!file.value) return;
    uploading.value = true;
    progress.value = 0;
    msg.value = '';
    const fd = new FormData();
    fd.append('type', type.value);
    fd.append('file', file.value);

    const xhr = new XMLHttpRequest();
    xhr.upload.onprogress = e => {
      if (e.lengthComputable) progress.value = Math.round((e.loaded / e.total) * 100);
    };
    xhr.onload = () => {
      uploading.value = false;
      msg.value = xhr.status === 200
        ? 'Upload OK — device rebooting. Reconnect in ~15s.'
        : `Failed (${xhr.status}): ${xhr.responseText}`;
    };
    xhr.onerror = () => {
      uploading.value = false;
      msg.value = 'Network error during upload.';
    };
    xhr.open('POST', '/update');
    xhr.send(fd);
  };

  return (
    <div class="space-y-4 max-w-xl">
      <SectionTabs current="/settings/firmware" tabs={TABS_SETTINGS} />
      <h1 class="text-xl font-bold">Firmware update</h1>

      <div class="glass p-5 space-y-4">
        <div>
          <div class="label-caps mb-2">What are you uploading?</div>
          <div class="flex gap-2">
            {(['firmware', 'littlefs'] as UploadType[]).map(t => (
              <button key={t}
                onClick={() => (type.value = t)}
                class={`text-sm px-3 py-1.5 rounded-md border ${
                  type.value === t
                    ? 'bg-aqua-primary/25 border-aqua-primary/50 text-cyan-100'
                    : 'bg-white/5 border-aqua-border'
                }`}>{t === 'firmware' ? 'Firmware .bin' : 'LittleFS .bin'}</button>
            ))}
          </div>
        </div>

        <label class="block">
          <div class="label-caps mb-1">File</div>
          <input type="file" accept=".bin"
                 onChange={e => (file.value = (e.target as HTMLInputElement).files?.[0] ?? null)}
                 class="w-full text-sm" />
        </label>

        <button class="text-sm px-4 py-2 rounded-md bg-aqua-primary text-slate-900 font-semibold disabled:opacity-50"
                disabled={!file.value || uploading.value}
                onClick={upload}>
          {uploading.value ? 'Uploading…' : 'Upload'}
        </button>

        {uploading.value && (
          <div>
            <div class="label-caps mb-1">Progress</div>
            <div class="w-full h-2 bg-slate-900/50 rounded-full overflow-hidden">
              <div class="h-full bg-aqua-primary" style={`width:${progress.value}%`} />
            </div>
            <div class="text-xs opacity-60 mt-1 val-num">{progress.value}%</div>
          </div>
        )}

        {msg.value && (
          <div class="text-sm border border-aqua-border rounded-md p-3">{msg.value}</div>
        )}
      </div>

      <p class="text-xs opacity-60">
        Same endpoint as <code class="opacity-80">pio run -t uploadfs</code>: the device reboots on success.
      </p>
    </div>
  );
}
