// Aristocrat — захват видео.
// Следит за сетевыми запросами вкладки и запоминает потоки видео (HLS .m3u8, DASH .mpd, файлы MP4/WebM),
// вместе с заголовками запроса (Referer, Origin, Cookie, User-Agent), чтобы программа могла скачать их так же,
// как их получает браузер.

const api = globalThis.browser ?? globalThis.chrome;
const IS_FIREFOX = typeof globalThis.browser !== "undefined" && !!api.runtime.getBrowserInfo;

const MEDIA_EXT = /\.(m3u8|mpd|mp4|m4v|webm|mov|mkv|flv)(\?|#|$)/i;
const SKIP_EXT = /\.(ts|m4s|m4a|aac|key|vtt|webvtt|srt|jpe?g|png|gif|webp|svg|js|css|json|woff2?|ico)(\?|#|$)/i;
const MEDIA_TYPES = /(mpegurl|dash\+xml|^video\/)/i;
const KEEP_HEADERS = ["referer", "origin", "cookie", "user-agent", "authorization"];

let tabs = {};            // tabId -> { items: { [url]: item }, children: [url], meta: {...} }
const pending = {};       // requestId -> headers
let loaded = false;

async function load() {
  if (loaded) return;
  try {
    const s = await api.storage.session.get("tabs");
    if (s && s.tabs) tabs = s.tabs;
  } catch (e) { /* storage.session недоступно — работаем в памяти */ }
  loaded = true;
}

function save() {
  try { api.storage.session.set({ tabs }); } catch (e) { }
}

function kindOf(url, type) {
  const u = url.toLowerCase();
  if (/\.m3u8/.test(u) || /mpegurl/i.test(type || "")) return "HLS";
  if (/\.mpd/.test(u) || /dash\+xml/i.test(type || "")) return "DASH";
  const m = u.match(/\.(mp4|m4v|webm|mov|mkv|flv)/);
  return m ? m[1].toUpperCase() : "VIDEO";
}

function tabData(tabId) {
  if (!tabs[tabId]) tabs[tabId] = { items: {}, children: [], meta: {} };
  return tabs[tabId];
}

function updateBadge(tabId) {
  const t = tabs[tabId];
  const n = t ? visibleItems(t).length : 0;
  try {
    api.action.setBadgeBackgroundColor({ tabId, color: "#C99A38" });
    api.action.setBadgeText({ tabId, text: n ? String(n) : "" });
  } catch (e) { }
}

function visibleItems(t) {
  const list = Object.values(t.items).filter(i => !t.children.includes(i.url));
  // Если на странице есть HLS/DASH, мелкие MP4 (реклама, превью) не показываем
  const hasStream = list.some(i => i.kind === "HLS" || i.kind === "DASH");
  return list.filter(i => !hasStream || i.kind === "HLS" || i.kind === "DASH" || (i.size || 0) > 50 * 1024 * 1024)
             .sort((a, b) => a.ts - b.ts);
}

function resolve(base, rel) {
  try { return new URL(rel, base).href; } catch (e) { return rel; }
}

// Разбор плейлиста HLS: список качеств и длительность
async function inspectHls(tabId, item) {
  try {
    const r = await fetch(item.url, { credentials: "include" });
    if (!r.ok) return;
    const text = await r.text();
    const t = tabData(tabId);
    if (text.includes("#EXT-X-STREAM-INF")) {
      const lines = text.split(/\r?\n/);
      const variants = [];
      for (let i = 0; i < lines.length; i++) {
        if (!lines[i].startsWith("#EXT-X-STREAM-INF")) continue;
        const res = /RESOLUTION=(\d+)x(\d+)/.exec(lines[i]);
        const bw = /BANDWIDTH=(\d+)/.exec(lines[i]);
        let j = i + 1;
        while (j < lines.length && (lines[j].startsWith("#") || !lines[j].trim())) j++;
        const vurl = resolve(item.url, (lines[j] || "").trim());
        variants.push({ w: res ? +res[1] : 0, h: res ? +res[2] : 0, bw: bw ? +bw[1] : 0, url: vurl });
        if (!t.children.includes(vurl)) t.children.push(vurl);
      }
      variants.sort((a, b) => (b.h - a.h) || (b.bw - a.bw));
      item.variants = variants;
      item.master = true;
      // длительность — по первому варианту
      if (variants[0]) {
        try {
          const r2 = await fetch(variants[0].url, { credentials: "include" });
          if (r2.ok) item.duration = sumDuration(await r2.text());
        } catch (e) { }
      }
    } else {
      item.duration = sumDuration(text);
    }
    save();
    updateBadge(tabId);
  } catch (e) { /* поток недоступен без заголовков — программа скачает сама */ }
}

function sumDuration(text) {
  let d = 0;
  for (const m of text.matchAll(/#EXTINF:([\d.]+)/g)) d += parseFloat(m[1]);
  return Math.round(d);
}

function record(tabId, url, headers, type, size) {
  if (tabId < 0) return;
  const t = tabData(tabId);
  if (t.items[url]) {
    Object.assign(t.items[url].headers, headers);
    return;
  }
  const item = { url, kind: kindOf(url, type), headers: headers || {}, ts: Date.now(), size: size || 0, variants: [], duration: 0 };
  t.items[url] = item;
  save();
  updateBadge(tabId);
  if (item.kind === "HLS") inspectHls(tabId, item);
}

function pickHeaders(list) {
  const h = {};
  for (const x of list || []) {
    if (KEEP_HEADERS.includes(x.name.toLowerCase())) h[x.name] = x.value;
  }
  return h;
}

const sendSpec = IS_FIREFOX ? ["requestHeaders"] : ["requestHeaders", "extraHeaders"];
api.webRequest.onSendHeaders.addListener(async (d) => {
  if (d.tabId < 0) return;
  await load();
  const h = pickHeaders(d.requestHeaders);
  if (MEDIA_EXT.test(d.url) && !SKIP_EXT.test(d.url)) {
    record(d.tabId, d.url, h);
  } else {
    pending[d.requestId] = h;   // тип станет известен по ответу
  }
}, { urls: ["<all_urls>"] }, sendSpec);

api.webRequest.onHeadersReceived.addListener(async (d) => {
  const h = pending[d.requestId];
  delete pending[d.requestId];
  if (d.tabId < 0 || SKIP_EXT.test(d.url)) return;
  let type = "", size = 0;
  for (const x of d.responseHeaders || []) {
    const n = x.name.toLowerCase();
    if (n === "content-type") type = x.value || "";
    if (n === "content-length") size = parseInt(x.value || "0", 10);
  }
  if (!MEDIA_TYPES.test(type)) return;
  if (/^video\//i.test(type) && size && size < 2 * 1024 * 1024) return;   // кусочки и превью
  await load();
  record(d.tabId, d.url, h || {}, type, size);
}, { urls: ["<all_urls>"] }, ["responseHeaders"]);

api.webRequest.onErrorOccurred.addListener((d) => { delete pending[d.requestId]; }, { urls: ["<all_urls>"] });
api.webRequest.onCompleted.addListener((d) => { delete pending[d.requestId]; }, { urls: ["<all_urls>"] });

// Новая страница во вкладке — очищаем список
api.tabs.onUpdated.addListener(async (tabId, info) => {
  if (info.status === "loading" && info.url) {
    await load();
    delete tabs[tabId];
    save();
    updateBadge(tabId);
  }
});
api.tabs.onRemoved.addListener(async (tabId) => {
  await load();
  delete tabs[tabId];
  save();
});

api.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  (async () => {
    await load();
    if (msg.type === "get") {
      const t = tabs[msg.tabId];
      sendResponse(t ? { items: visibleItems(t), meta: t.meta } : { items: [], meta: {} });
    } else if (msg.type === "meta") {
      tabData(msg.tabId).meta = msg.meta;
      save();
      sendResponse({ ok: true });
    } else if (msg.type === "clear") {
      delete tabs[msg.tabId];
      save();
      updateBadge(msg.tabId);
      sendResponse({ ok: true });
    }
  })();
  return true;
});
