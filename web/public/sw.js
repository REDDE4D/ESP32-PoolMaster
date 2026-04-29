const CACHE = 'poolmaster-v1';
const SHELL = ['/', '/index.html', '/manifest.webmanifest'];

self.addEventListener('install', event => {
  event.waitUntil(caches.open(CACHE).then(c => c.addAll(SHELL)));
  self.skipWaiting();
});

self.addEventListener('activate', event => {
  event.waitUntil(
    caches.keys().then(keys => Promise.all(
      keys.filter(k => k !== CACHE).map(k => caches.delete(k))
    ))
  );
  self.clients.claim();
});

self.addEventListener('fetch', event => {
  const req = event.request;
  const url = new URL(req.url);

  // Never cache API or WebSocket upgrades.
  if (url.pathname.startsWith('/api/') || url.pathname === '/ws' ||
      url.pathname === '/healthz' || url.pathname === '/update') {
    return; // default network behavior
  }

  // Cache-first for same-origin static assets; network fallback updates cache.
  if (req.method === 'GET' && url.origin === self.location.origin) {
    event.respondWith(
      caches.match(req).then(hit => {
        if (hit) return hit;
        return fetch(req).then(res => {
          if (res.ok) {
            const clone = res.clone();
            caches.open(CACHE).then(c => c.put(req, clone));
          }
          return res;
        }).catch(() => caches.match('/index.html'));
      })
    );
  }
});
