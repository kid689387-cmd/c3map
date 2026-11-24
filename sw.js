// service worker ขั้นต่ำ: ทำให้ติดตั้งเป็นแอปได้ (จำเป็นสำหรับเมนู "แชร์" ของ Android)
self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', e => e.waitUntil(self.clients.claim()));
self.addEventListener('fetch', e => {
  if (e.request.method !== 'GET' || new URL(e.request.url).origin !== location.origin) return;
  e.respondWith(fetch(e.request).catch(() => caches.match(e.request)));
});
