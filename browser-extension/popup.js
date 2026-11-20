const api = globalThis.browser ?? globalThis.chrome;
const PORTS = [47615, 47616, 47617, 47618, 47619];
let appPort = null;
let tabId = null;

function fmt(sec) {
  if (!sec) return "";
  sec = Math.round(sec);
  const h = Math.floor(sec / 3600), m = Math.floor(sec / 60) % 60, s = sec % 60;
  return (h ? h + ":" + String(m).padStart(2, "0") : m) + ":" + String(s).padStart(2, "0");
}

async function findApp() {
  const el = document.getElementById("app");
  for (const p of PORTS) {
    try {
      const r = await fetch(`http://127.0.0.1:${p}/ping`, { cache: "no-store" });
      const j = await r.json();
      if (j.app === "aristocrat") {
        appPort = p;
        el.textContent = "● Программа подключена";
        el.className = "app ok";
        return true;
      }
    } catch (e) { }
  }
  el.textContent = "● Запустите Aristocrat Видеоконвертер";
  el.className = "app bad";
  return false;
}

// Название и картинка страницы
async function pageMeta() {
  try {
    const [res] = await api.scripting.executeScript({
      target: { tabId },
      func: () => {
        const q = (s) => document.querySelector(s);
        const og = (p) => q(`meta[property="${p}"]`)?.content || q(`meta[name="${p}"]`)?.content || "";
        const video = q("video");
        return {
          title: og("og:title") || document.title || "",
          thumb: og("og:image") || video?.poster || "",
          page: location.href
        };
      }
    });
    return res?.result || {};
  } catch (e) {
    return {};
  }
}

function cleanTitle(t) {
  t = t || "video";
  const parts = t.split(/\s+[|–—]\s+/);   // «Название | Сайт» → «Название»
  if (parts[0].length >= 8) t = parts[0];
  return t.replace(/[\\/:*?"<>|]+/g, " ").replace(/\s+/g, " ").trim().slice(0, 150) || "video";
}

function render(items, meta) {
  const list = document.getElementById("list");
  list.innerHTML = "";
  document.getElementById("empty").hidden = items.length > 0;
  const tpl = document.getElementById("item");
  items.slice().reverse().forEach((it, idx) => {
    const node = tpl.content.firstElementChild.cloneNode(true);
    const img = node.querySelector(".thumb img");
    if (meta.thumb) img.src = meta.thumb; else img.remove();
    node.querySelector(".dur").textContent = fmt(it.duration);
    if (!it.duration) node.querySelector(".dur").remove();
    node.querySelector(".kind").textContent = it.kind;
    const title = node.querySelector(".title");
    title.value = cleanTitle(meta.title) + (items.length > 1 && idx > 0 ? ` (${idx + 1})` : "");
    const sel = node.querySelector(".quality");
    const heights = [...new Set((it.variants || []).map(v => v.h).filter(Boolean))];
    sel.add(new Option(heights.length ? `Лучшее (${heights[0]}p)` : "Лучшее качество", "0"));
    heights.forEach(h => sel.add(new Option(`${h}p`, String(h))));
    if (!heights.length && it.kind !== "HLS" && it.kind !== "DASH") sel.disabled = true;
    node.querySelector(".info").textContent = it.size ? (it.size / 1048576).toFixed(0) + " МБ" : "";
    const btn = node.querySelector(".go");
    btn.addEventListener("click", () => send(it, meta, title.value, +sel.value, btn));
    list.appendChild(node);
  });
}

async function send(it, meta, title, height, btn) {
  const status = document.getElementById("status");
  if (!appPort && !(await findApp())) {
    status.textContent = "Программа не запущена. Откройте Aristocrat Видеоконвертер и нажмите ещё раз.";
    return;
  }
  btn.disabled = true;
  btn.textContent = "…";
  try {
    const r = await fetch(`http://127.0.0.1:${appPort}/add`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        url: it.url, kind: it.kind, title, height, headers: it.headers || {},
        pageUrl: meta.page || "", thumbnail: meta.thumb || "", duration: it.duration || 0
      })
    });
    if (!r.ok) throw new Error(String(r.status));
    btn.textContent = "✔ В программе";
    btn.classList.add("done");
    status.textContent = "Загрузка началась в Aristocrat Видеоконвертер.";
  } catch (e) {
    btn.disabled = false;
    btn.textContent = "Скачать";
    status.textContent = "Не удалось передать видео в программу.";
  }
}

async function init() {
  const [tab] = await api.tabs.query({ active: true, currentWindow: true });
  tabId = tab.id;
  findApp();
  const meta = await pageMeta();
  if (meta.title) await api.runtime.sendMessage({ type: "meta", tabId, meta });
  const res = await api.runtime.sendMessage({ type: "get", tabId });
  render(res.items || [], Object.assign({ title: tab.title, page: tab.url }, res.meta || {}, meta));
  document.getElementById("clear").addEventListener("click", async () => {
    await api.runtime.sendMessage({ type: "clear", tabId });
    render([], {});
  });
}

init();
