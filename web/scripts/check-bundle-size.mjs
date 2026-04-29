import { readdir, stat } from 'node:fs/promises';
import { join } from 'node:path';

const MAX_JS_BYTES = 500 * 1024;  // 500 KB uncompressed
const LIMIT_KB     = MAX_JS_BYTES / 1024;

const assetsDir = new URL('../../data/assets/', import.meta.url);
let total = 0;

try {
  const files = await readdir(assetsDir);
  for (const f of files) {
    if (!f.endsWith('.js')) continue;
    const s = await stat(new URL(f, assetsDir));
    total += s.size;
  }
} catch (err) {
  console.error('[bundle-size] could not read data/assets:', err.message);
  process.exit(1);
}

const kb = (total / 1024).toFixed(1);
if (total > MAX_JS_BYTES) {
  console.error(`[bundle-size] FAIL: ${kb} KB of JS exceeds ${LIMIT_KB} KB limit.`);
  process.exit(1);
}
console.log(`[bundle-size] OK: ${kb} KB of JS (${((total / MAX_JS_BYTES) * 100).toFixed(0)}% of ${LIMIT_KB} KB budget)`);
