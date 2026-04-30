import { signal } from '@preact/signals';
import { randomId } from './ids';

export type WsStatus = 'connecting' | 'connected' | 'reconnecting' | 'stopped';

export interface WsMessage {
  type: string;
  [k: string]: unknown;
}

type Listener = (msg: WsMessage) => void;

export const wsStatus = signal<WsStatus>('connecting');

// A connection only counts as "successful" — i.e. resets the backoff — if
// it stayed open at least this long. Without this, an open-then-immediately-
// close pattern (which is exactly what happens when the ESP32 chokes during
// a tab-resume reconnect storm) keeps backoff pinned at 1 s and hammers
// the device at ~1 Hz.
const MIN_ALIVE_FOR_BACKOFF_RESET_MS = 5000;

export class PoolMasterWs {
  private socket: WebSocket | null = null;
  private listeners = new Set<Listener>();
  private backoffMs = 1000;
  private readonly maxBackoffMs = 30000;
  private pingTimer: number | null = null;
  private reconnectTimer: number | null = null;
  private stopped = false;
  private pendingSubs: string[] = ['state', 'logs', 'history'];
  private connectedAt = 0;
  private visibilityHandler: (() => void) | null = null;

  constructor(private readonly url: string = buildWsUrl()) {}

  start() {
    this.stopped = false;
    this.installVisibilityHandler();
    this.connect();
  }

  stop() {
    this.stopped = true;
    if (this.pingTimer) { clearInterval(this.pingTimer); this.pingTimer = null; }
    if (this.reconnectTimer) { clearTimeout(this.reconnectTimer); this.reconnectTimer = null; }
    if (this.visibilityHandler) {
      document.removeEventListener('visibilitychange', this.visibilityHandler);
      this.visibilityHandler = null;
    }
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
      this.connectedAt = Date.now();
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
      const aliveMs = this.connectedAt ? Date.now() - this.connectedAt : 0;
      this.connectedAt = 0;
      this.socket = null;
      if (this.stopped) return;
      wsStatus.value = 'reconnecting';
      // Only credit the backoff reset if the connection actually stayed
      // alive — otherwise treat open-then-close as part of the same
      // failure run so we back off properly.
      if (aliveMs >= MIN_ALIVE_FOR_BACKOFF_RESET_MS) this.backoffMs = 1000;
      this.scheduleReconnect(this.backoffMs);
      this.backoffMs = Math.min(this.backoffMs * 2, this.maxBackoffMs);
    };
  }

  private scheduleReconnect(delayMs: number) {
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
    this.reconnectTimer = window.setTimeout(() => {
      this.reconnectTimer = null;
      this.connect();
    }, delayMs);
  }

  private installVisibilityHandler() {
    if (this.visibilityHandler) return;
    this.visibilityHandler = () => {
      if (document.visibilityState !== 'visible' || this.stopped) return;
      // Tab just came back. iOS PWAs in particular can return holding a
      // socket that the OS already killed at the TCP layer; reconnecting
      // immediately (and resetting backoff) gets the UI live again
      // without waiting out an exponential backoff window from events
      // that fired while we were suspended.
      const open = this.socket && this.socket.readyState === WebSocket.OPEN;
      if (open) return;
      this.backoffMs = 1000;
      if (this.socket) {
        const s = this.socket;
        this.socket = null;
        try { s.close(); } catch { /* ignore */ }
      }
      this.scheduleReconnect(0);
    };
    document.addEventListener('visibilitychange', this.visibilityHandler);
  }
}

function buildWsUrl(): string {
  const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${proto}//${window.location.host}/ws`;
}

// Singleton — most of the app just imports this.
export const poolWs = new PoolMasterWs();
