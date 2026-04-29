import { useComputed, useSignal } from '@preact/signals';
import { logEntries, clearLogs } from '../stores/log';
import { fmtTimestamp } from '../lib/format';
import { SectionTabs, TABS_INSIGHTS } from '../components/SectionTabs';

const LEVELS = ['dbg', 'inf', 'wrn', 'err'] as const;
type LvlFilter = typeof LEVELS[number] | 'all';

const COLOR: Record<string, string> = {
  dbg: 'text-slate-400',
  inf: 'text-cyan-300',
  wrn: 'text-amber-300',
  err: 'text-rose-300',
};

export function Logs() {
  const filter = useSignal<LvlFilter>('all');
  const search = useSignal('');

  const visible = useComputed(() => {
    return logEntries.value.filter(e => {
      if (filter.value !== 'all' && e.level !== filter.value) return false;
      if (search.value && !e.msg.toLowerCase().includes(search.value.toLowerCase())) return false;
      return true;
    });
  });

  return (
    <div class="space-y-3 max-w-4xl">
      <SectionTabs current="/insights/logs" tabs={TABS_INSIGHTS} />
      <div class="flex items-center justify-between">
        <h1 class="text-xl font-bold">Logs</h1>
        <button class="text-xs px-3 py-1 rounded-md bg-white/5 border border-aqua-border"
                onClick={() => clearLogs()}>Clear</button>
      </div>

      <div class="glass p-3 flex gap-2 items-center flex-wrap">
        <div class="flex gap-1">
          {(['all', ...LEVELS] as LvlFilter[]).map(lv => (
            <button key={lv}
              onClick={() => (filter.value = lv)}
              class={`text-xs px-2 py-1 rounded-md ${filter.value === lv ? 'bg-aqua-primary/25 text-cyan-100' : 'bg-white/5'}`}>
              {lv}
            </button>
          ))}
        </div>
        <input type="search" placeholder="Filter…" value={search.value}
               onInput={e => (search.value = (e.target as HTMLInputElement).value)}
               class="flex-1 bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1 text-sm" />
      </div>

      <div class="glass p-0 overflow-hidden">
        <div class="font-mono text-xs max-h-[60vh] overflow-y-auto">
          {visible.value.length === 0 && (
            <div class="p-4 opacity-60">No logs to show.</div>
          )}
          {visible.value.map((e, i) => (
            <div key={i} class="grid grid-cols-[6rem_3rem_1fr] gap-2 px-3 py-1 border-b border-white/5">
              <span class="opacity-60">{fmtTimestamp(e.ts)}</span>
              <span class={COLOR[e.level] ?? ''}>{e.level}</span>
              <span class="truncate whitespace-pre-wrap">{e.msg}</span>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
