/* Smart Parking Finder service worker
   - Pages: network first (always the latest version), cached copy if offline
   - Icons, manifest, screenshots: cache first
   - Firebase, Google, Razorpay and other sites: never touched (always live) */
const VERSION = 'sp-v1';
const SHELL = ['./', './offline.html', './manifest.webmanifest',
               './icons/icon-192.png', './icons/icon-512.png', './icons/favicon-48.png'];

// Firebase cleanUrls redirects *.html, and a redirected response can't be used for a page load.
const clean = r => (r && r.redirected)
  ? r.blob().then(body => new Response(body, { status: 200, headers: r.headers }))
  : r;

self.addEventListener('install', e => {
  e.waitUntil(caches.open(VERSION).then(c => c.addAll(SHELL)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', e => {
  e.waitUntil(
    caches.keys()
      .then(keys => Promise.all(keys.filter(k => k !== VERSION).map(k => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', e => {
  const req = e.request;
  if (req.method !== 'GET') return;
  const url = new URL(req.url);
  if (url.origin !== self.location.origin) return;      // Firebase, Razorpay, fonts: straight to network
  if (url.pathname.startsWith('/__/')) return;           // Firebase reserved URLs (auth handler)

  if (req.mode === 'navigate') {
    e.respondWith(
      fetch(req).then(res => {
        if (res.ok && !res.redirected) {
          const copy = res.clone();
          caches.open(VERSION).then(c => c.put(req, copy));
        }
        return res;
      }).catch(() => caches.match(req)
          .then(hit => hit || caches.match('./offline.html'))
          .then(clean))
    );
    return;
  }

  e.respondWith(
    caches.match(req).then(hit => hit || fetch(req).then(res => {
      if (res.ok && /\.(png|webmanifest|svg|ico|jpg)$/.test(url.pathname)) {
        const copy = res.clone();
        caches.open(VERSION).then(c => c.put(req, copy));
      }
      return res;
    }))
  );
});
