// Correlation-ID generator for WS `cmd` frames.
//
// We can't use `crypto.randomUUID()` directly: browsers gate it to secure
// contexts (HTTPS or literal localhost), and the device serves the SPA over
// plain HTTP at http://poolmaster.local/ — `crypto.randomUUID` is undefined
// there. The ID is only used to correlate `cmd` → `ack` frames, so a non-
// crypto-grade pseudo-random string is fine.

export function randomId(): string {
  // Prefer the real one when available (HTTPS deploys, dev server on localhost).
  const c = (globalThis as { crypto?: Crypto }).crypto;
  if (c && typeof c.randomUUID === 'function') return c.randomUUID();
  // Fallback: 16 random hex chars + millis suffix → ~unique per session.
  const r = Math.random().toString(16).slice(2, 18).padEnd(16, '0');
  return `${r}-${Date.now().toString(16)}`;
}
