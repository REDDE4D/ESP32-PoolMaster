import { poolWs } from '../lib/ws';
import { applyState, type PoolState } from './state';
import { appendLog, replaceLogs, type LogEntry } from './log';
import { setSeries, appendToSeries, type SeriesName } from './history';
import { welcome, type WelcomeInfo } from './connection';

let bootstrapped = false;

export function bootstrapStores() {
  if (bootstrapped) return;
  bootstrapped = true;

  poolWs.subscribe(msg => {
    switch (msg.type) {
      case 'welcome':
        welcome.value = msg as unknown as WelcomeInfo;
        break;

      case 'state':
        applyState((msg.data ?? {}) as PoolState);
        break;

      case 'log':
        appendLog({
          ts: Number(msg.ts),
          level: String(msg.level),
          msg: String(msg.msg),
        });
        break;

      case 'history': {
        const name = msg.series as SeriesName;
        if (Array.isArray(msg.append)) {
          appendToSeries(name, msg.append as number[]);
        } else if (Array.isArray(msg.values)) {
          setSeries({
            name,
            t0_ms: Number(msg.t0),
            step_s: Number(msg.step_s),
            values: msg.values as number[],
          });
        }
        break;
      }

      case 'alarm':
        // Alarms update existing poolState.alarms already via the next state
        // broadcast; no separate store needed. A transient toast can be added
        // later if desired.
        break;
    }
  });

  poolWs.start();
}

// Re-export for convenience
export { poolState } from './state';
export { logEntries, clearLogs } from './log';
export { history } from './history';
export { welcome } from './connection';
