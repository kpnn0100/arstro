/*
 * Cosmo by arstro — touch/home.js: the phone Home screen (and its Loading screen).
 *
 * Ports PhoneApp.cpp's HomeScreen (:1057-1158): the 36 px wordmark (no letter-spacing, the native
 * touch's), the tagline, the three 52 px action rows (New Project / Open Project / Import Catalog),
 * "Recent Projects" with its count badge, the search field, the 80 px project cards, the solid
 * New-Project card, the links and the version line; and LoadingScreen (:1160-1191): 60 twinkling
 * stars, the 32 px wordmark, the project name, the 200 x 3 bar and "Loading <k> / <N> photos".
 *
 * Deliberate deviations (screens_touch.md §14.3-14.4, §14.16): the column scrolls (the native
 * clips in landscape); the cards list animates as the search filters it; a card shows its cover
 * when the service can send one (adapter `cover`, core gap G1 otherwise: the INPUT well); a card
 * for the project this session already has open goes back to it instead of reloading; "Settings"
 * opens the Engine Settings sheet (inert natively); the loading bar follows the real progress
 * (loadPermille / loadDone / loadTotal) instead of the native's fake 1100 ms timer.
 */
import { h, bindEffect, bindText, bindClass, keyed, own } from "../core/dom.js";
import { signal, computed } from "../core/signal.js";
import { Tween, Ease, reducedMotion } from "../core/motion.js";
import { tx, ticon, gestures, pressWash, baseOffset, canvasFor, tok } from "./tk.js";

const quiet = (p) => { if (p && p.catch) p.catch(() => {}); return p; };

export function mountHome(parent, vm, ctx, env) {
  const el = h("div.t-home.vscroll");
  parent.append(el);
  const col = h("div.t-hcol");
  el.append(col);

  // wordmark + tagline (y 0..120)
  const head = h("div.t-hhead");
  const mark = h("span.tt.t-mark", {}, h("span", { text: "cosmo" }), h("span.dot", { text: "." }));
  mark.style.left = "24px"; mark.style.top = 70 - baseOffset(36, "semibold") + "px";
  head.append(mark, tx("Dark-room tools for serious work.", 24, 96, 14, "sans", "left", "t-muted"));
  col.append(head);

  // the three actions (16, y, W-32, 52), stride 60
  const acts = h("div.t-hacts");
  [["New Project", "plusCircle", 1.75, true, () => newProject(vm, env)],
   ["Open Project", "folderOpen", 1.6, false, () => openProject(vm, env)],
   ["Import Catalog", "importDown", 1.6, false, () => importCatalog(vm, env)]].forEach(([label, ic, sw, accent, fn]) => {
    const a = h("div.t-hact" + (accent ? ".accent" : ""));
    const i = ticon(ic, 20, sw); i.classList.add("t-hact-ic");
    a.append(i, tx(label, 48, 32, 15, "medium"));
    gestures(a, { down: () => pressWash(a), tap: fn });
    acts.append(a);
  });
  col.append(acts);

  // Recent Projects + badge, search
  const q = signal("");
  const filtered = computed(() => {
    const s = q.value.toLowerCase();
    return vm.recents.value.map((r, i) => ({ ...r, i })).filter((r) => !s || (r.name || "").toLowerCase().includes(s));
  });
  const rt = h("div.t-hrt");
  const title = tx("Recent Projects", 16, 14, 15, "semibold", "left", "t-fg");
  const badge = h("div.t-badge");
  const count = tx("", 11, 13, 11, "mono", "center", "t-muted");
  badge.append(count);
  bindText(count, () => String(filtered.value.length));
  rt.append(title, badge);
  requestAnimationFrame(() => { badge.style.left = 16 + title.getBoundingClientRect().width + 10 + "px"; });
  document.fonts && document.fonts.ready.then(() => { badge.style.left = 16 + title.getBoundingClientRect().width + 10 + "px"; });
  col.append(rt);

  const search = h("div.t-search");
  const si = ticon("search", 18, 1.5); si.classList.add("t-search-ic");
  const input = h("input.t-sinput", { type: "search", placeholder: "Search projects", spellcheck: "false", autocomplete: "off", enterkeyhint: "search" });
  input.addEventListener("input", () => { q.value = input.value.replace(/[^\x20-\x7e]/g, ""); });
  input.addEventListener("focus", () => search.classList.add("focus"));
  input.addEventListener("blur", () => search.classList.remove("focus"));
  search.append(si, input);
  col.append(search);

  // cards
  const cards = h("div.t-hcards");
  const none = h("div.t-hnone");
  const noneT = tx("", "50%", 30, 13, "sans", "center", "t-muted");
  none.append(noneT);
  bindText(noneT, () => (vm.recents.value.length ? "No projects match your search" : "No recent projects"));
  bindClass(none, "off", () => filtered.value.length > 0);
  col.append(none, cards);
  const covers = coverCache(vm);
  keyed(cards, () => filtered.value, (r) => r.path, (r) => {
    const c = h("div.t-pcard");
    const well = h("div.t-pwell");
    const img = h("img.t-pcover", { alt: "" });
    well.append(img);
    const name = tx("", 104, 30, 14, "medium", "left", "t-fg t-ell");
    const meta = tx("", 104, 50, 12, "sans", "left", "t-muted");
    c.append(well, name, meta);
    c.__n = name; c.__m = meta; c.__img = img;
    gestures(c, { down: () => pressWash(c), tap: () => openRecent(vm, env, c.__r) });
    return c;
  }, (c, r) => {
    c.__r = r;
    c.__n.textContent = r.name || "Untitled";
    c.__m.textContent = r.photoCount + (r.photoCount === 1 ? " photo" : " photos");
    const url = covers.get(r.firstImagePath);
    if (url && c.__img.dataset.src !== url) { c.__img.dataset.src = url; c.__img.src = url; c.__img.classList.add("on"); }
  });

  // the New Project card, the links, the version
  const nc = h("div.t-newcard");
  const pi = ticon("plusCircle", 18, 1.6); pi.classList.add("t-newcard-ic");
  nc.append(pi, tx("New Project", "calc(50% - 20px)", 41, 14, "sans", "left", "t-muted"));
  gestures(nc, { down: () => pressWash(nc), tap: () => newProject(vm, env) });
  col.append(nc);
  const links = h("div.t-hlinks");
  ["Settings", "What's New", "Help & Documentation"].forEach((l, i) => {
    const r = h("div.t-hlink");
    r.append(tx(l, 24, 27, 13, "sans", "left", "t-muted"), h("i.t-hl.link"));
    if (i === 0) gestures(r, { down: () => pressWash(r), tap: () => env.sheets.settings() });
    links.append(r);
  });
  col.append(links, h("div.t-hver", {}, tx("cosmo v1.0.0-beta", 24, 24, 11, "mono", "left", "t-muted")));
  return { el };
}

/** Covers of the recent projects: adapter `cover {path, edge}` -> blob `cover` {path} (core gap G1
 *  until it exists: nothing is asked again after the adapter says it has no such method). */
function coverCache(vm) {
  const urls = signal(new Map());
  const asked = new Set();
  let missing = false;
  vm.session.bridge.onBlob("cover", (blob, hd) => {       // lives as long as the page
    const p = hd && hd.meta && hd.meta.path;
    if (!p) return;
    const m = new Map(urls.peek());
    m.set(p, URL.createObjectURL(blob));
    urls.value = m;
  });
  return {
    get(path) {
      const m = urls.value;
      if (!path || missing) return m.get(path) || "";
      if (!m.has(path) && !asked.has(path) && vm.status.peek() === "running") {
        asked.add(path);
        vm.session.call("cover", { path, edge: 160 }).catch((e) => { if (/no method/i.test(String(e && e.message))) missing = true; });
      }
      return m.get(path) || "";
    },
  };
}

// ---------------------------------------------------------------- the project flows (§5, §14.3)
export async function newProject(vm, env) {
  const path = await env.sheets.browse({ mode: "new", name: "Untitled.cmp" });
  if (path) { env.opening(path); quiet(vm.newProject(path)); }
}
export async function openProject(vm, env) {
  const path = await env.sheets.browse({ mode: "open" });
  if (path) { env.opening(path); quiet(vm.openProject(path)); }
}
export async function importCatalog(vm, env) {
  const pick = await env.sheets.browse({ mode: "import" });
  if (!pick || !pick.paths.length) return;
  const cmp = await env.sheets.browse({ mode: "new", dir: pick.dir, name: "Imported.cmp" });
  if (!cmp) return;
  env.opening(cmp);
  try { await vm.newProject(cmp); await vm.importPhotos(pick.paths); } catch { /* toasted */ }
}
function openRecent(vm, env, r) {
  if (!r) return;
  if (r.path === vm.project.peek().path && vm.imageCount.peek()) { vm.backToEditor(); return; }
  env.opening(r.path, r.name);
  quiet(vm.openProject(r.path));
}

// ---------------------------------------------------------------- Loading
export function mountLoading(parent, vm, ctx, env) {
  const el = h("div.t-loading");
  parent.append(el);
  const sky = h("div.t-sky");
  el.append(sky);
  const cv = canvasFor(sky);
  const mid = h("div.t-lmid");
  const mark = h("span.tt.t-lmark", {}, h("span", { text: "cosmo" }), h("span.dot", { text: "." }));
  mark.style.top = -baseOffset(32, "semibold") + "px";
  const name = tx("", "50%", 40, 18, "medium", "center", "t-fg t-ell");
  const bar = h("div.t-lbar", {}, h("i"));
  const status = tx("", "50%", 100, 12, "sans", "center", "t-muted");
  mid.append(mark, name, bar, status);
  el.append(mid);
  bindText(name, () => vm.project.value.name || env.loadingName.value || "");
  const prog = new Tween(0, (v) => { bar.firstChild.style.width = v * 100 + "%"; });
  bindEffect(el, () => {
    const L = vm.load.value;
    const p = L.total ? Math.max(L.fraction, L.done / L.total) : L.fraction;
    if (vm.screen.value !== "loading") { prog.set(0); return; }
    prog.to(Math.min(1, p), 200, Ease.EaseOutCubic);
    status.textContent = `Loading ${L.done} / ${L.total} photos`;
  });
  // the 60 stars (PhoneApp.cpp:1170-1175), twinkling while this screen shows
  let raf = 0;
  const draw = (now) => {
    const [w, hh] = cv.size();
    const c = cv.ctx;
    c.clearRect(0, 0, w, hh);
    c.fillStyle = tok(el, "--white");
    const tw = !reducedMotion();
    for (let i = 0; i < 60; i++) {
      const x = (i * 137.5) % w, y = (i * 269.13 + 40) % hh, r = 0.4 + (i % 3) * 0.45, base = 0.2 + (i % 5) * 0.12;
      c.globalAlpha = tw ? base * (0.4 + 0.6 * Math.abs(Math.sin(now / 700 + i))) : base;
      c.beginPath(); c.arc(x + r, y + r, r, 0, Math.PI * 2); c.fill();
    }
    c.globalAlpha = 1;
    raf = vm.screen.peek() === "loading" ? requestAnimationFrame(draw) : 0;
  };
  bindEffect(el, () => { if (vm.screen.value === "loading" && !raf) raf = requestAnimationFrame(draw); });
  own(el, () => cancelAnimationFrame(raf));
  return { el };
}
