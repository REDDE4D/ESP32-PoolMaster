import { signal } from '@preact/signals';
import { randomId } from './ids';

export type WsStatus = 'connecting' | 'connected' | 'reconnecting' | 'stopped';

export interface WsMessage {
  type: string;
  [k: string]: unknown;
}

type Listener = (msg: WsMessage) => void;

export const wsStatus = signal<WsStatus>('connecting');

export class PoolMasterWs {
  private socket: WebSocket | null = null;
  private listeners = new Set<Listener>();
  private backoffMs = 1000;
  private readonly maxBackoffMs = 30000;
  private pingTimer: number | null = null;
  private stopped = false;
  private pendingSubs: string[] = ['state', 'logs', 'history'];

  constructor(private readonly url: string = buildWsUrl()) {}

  start() {
    this.stopped = false;
    this.connect();
  }

  stop() {
    this.stopped = true;
    if (this.pingTimer) { clearInterval(this.pingTimer); this.pingTimer = null; }
    if (this.socket) {
      const s = this.socket;
      this.socket = null;
      s.close();
    }
    wsStatus.value = 'stopped';
  }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  send(msg: WsMessage): boolean {
    if (!this.socket || this.socket.readyState !== WebSocket.OPEN) return false;
    this.socket.send(JSON.stringify(msg));
    return true;
  }

  sendCommand(payload: string): string {
    const id = randomId();
    this.send({ type: 'cmd', id, payload });
    return id;
  }

  setSubscriptions(topics: string[]) {
    this.pendingSubs = topics;
    this.send({ type: 'subscribe', topics });
  }

  private connect() {
    if (this.stopped) return;
    wsStatus.value = this.backoffMs === 1000 ? 'connecting' : 'reconnecting';

    const socket = new WebSocket(this.url);
    this.socket = socket;

    socket.onopen = () => {
      this.backoffMs = 1000;
      wsStatus.value = 'connected';
      socket.send(JSON.stringify({ type: 'hello', ver: 1, subs: this.pendingSubs }));
      if (this.pingTimer) clearInterval(this.pingTimer);
      this.pingTimer = window.setInterval(() => {
        if (socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify({ type: 'ping' }));
      }, 20000);
    };

    socket.onmessage = event => {
      try {
        const msg = JSON.parse(event.data) as WsMessage;
        this.listeners.forEach(l => l(msg));
      } catch {
        // ignore malformed
      }
    };

    socket.onerror = () => { /* onclose will handle reconnect */ };

    socket.onclose = () => {
      if (this.pingTimer) { clearInterval(this.pingTimer); this.pingTimer = null; }
      this.socket = null;
      if (this.stopped) return;
      wsStatus.value = 'reconnecting';
      window.setTimeout(() => this.connect(), this.backoffMs);
      this.backoffMs = Math.min(this.backoffMs * 2, this.maxBackoffMs);
    };
  }
}

function buildWsUrl(): string {
  const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${proto}//${window.location.host}/ws`;
}

// Singleton — most of the app just imports this.
export const poolWs = new PoolMasterWs();
