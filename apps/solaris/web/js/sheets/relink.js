/*
 * Solaris — sheets/relink.js: RelinkSheet (app/widgets/RelinkSheet.{h,cpp}) — File › Relink…, the files the song names
 * that cannot be read (R-COLLECT-1).
 *
 * The Export sheet's look (css/sheets/export.css): scrim, a 560-wide card, 150 / 120 ms fade. A row (48) per file in
 * the model's `missing[]`: its name, its folder · what uses it (mono 10), and Locate… (84×28) — ctx.pick a file, then
 * ONE `project relink <file> --to <picked>`. Search a Folder… — ctx.pick a folder, then ONE
 * `project relink --folder <dir>` (App.cpp:104-112). The sheet holds nothing: it draws the model.
 * Nothing snaps: rows keyed by path — a found one fades out where it is (motion::kCrossFadeMs), the rest slide up
 * (motion::kSelectMs), both EaseOutCubic; the card grows and shrinks with them (≤ 7 rows, then the list scrolls).
 * Done, Escape, Enter, the scrim close it.
 * NOT PORTED: the a11y nodes (R-ACCESS-1); the eased wheel scroll (the list scrolls natively).
 */
import { effect, computed, jsonEqual } from "../core/signal.js";
import { own } from "../core/dom.js";
import { useCss } from "../kit/css.js";

const CARD_W = 560, PAD = 24, ROW_H = 48, MAX_ROWS = 7, HEADER_H = 60, FOOTER_H = 56;
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };

export function mount(host, vm, ctx) {
  useCss("sheets/export.css");
  useCss("sheets/relink.css");
  const tr = (s) => vm.tr(s);

  const root = el("div", "sx-sheet sx-relink");
  const scrim = el("div", "sx-scrim");
  const card = el("div", "sx-card");
  const title = el("div", "sx-title");
  const sub = el("div", "sx-sub");
  const list = el("div", "sx-body sr-list");
  const inner = el("div", "sr-inner");
  const empty = el("div", "sr-empty");
  list.append(inner, empty);
  const foot = el("div", "sx-foot");
  const search = el("button", "sx-btn sx-secondary sr-search");
  const done = el("button", "sx-btn sx-primary sr-done");
  foot.append(search, done);
  card.append(title, sub, list, foot);
  root.append(scrim, card);
  host.append(root);

  const missing = computed(() => { const m = vm.model.value; return (m && m.missing) || []; }, jsonEqual);
  const rows = new Map(); // path -> {el, name, where, btn, leaving}
  let placeAll = true, cardH = 0;

  function layoutCard() {
    const b = ctx.box.peek(), n = missing.peek().length;
    const want = HEADER_H + Math.min(Math.max(n, 1), MAX_ROWS) * ROW_H + 12 + FOOTER_H;
    cardH = want;
    const w = Math.min(CARD_W, b.w - 32), hh = Math.min(cardH, b.h - 48);
    Object.assign(card.style, { left: Math.floor((b.w - w) * 0.5) + "px", top: Math.floor((b.h - hh) * 0.5) + "px", width: w + "px", height: hh + "px" });
  }

  function words() {
    const n = missing.peek().length;
    title.textContent = tr("Relink");
    sub.textContent = n === 0 ? tr("every file the song names is found") : vm.fillIn(vm.trn(n, "{1} file is missing", "{1} files are missing"), String(n));
    sub.style.left = 24 + title.offsetWidth + 10 + "px";
    empty.textContent = tr("Nothing is missing.");
    search.textContent = tr("Search a Folder…");
    done.textContent = tr("Done");
    for (const r of rows.values()) r.btn.textContent = tr("Locate…");
  }

  function bind() {
    const want = missing.value;
    want.forEach((w, i) => {
      let r = rows.get(w.path);
      if (!r) {
        const slash = w.path.lastIndexOf("/");
        r = { el: el("div", "sr-row"), name: el("div", "sr-name"), where: el("div", "sr-where"), btn: el("button", "sx-btn sx-secondary sr-locate"), leaving: false };
        r.file = slash < 0 ? w.path : w.path.slice(slash + 1);
        r.folder = slash < 0 ? "" : w.path.slice(0, slash);
        r.name.textContent = r.file;
        r.btn.textContent = tr("Locate…");
        r.btn.addEventListener("click", () => locate(w.path, r.file));
        r.el.append(r.name, r.where, r.btn);
        r.el.style.top = i * ROW_H + "px";
        if (!placeAll) { r.el.style.opacity = "0"; requestAnimationFrame(() => requestAnimationFrame(() => { r.el.style.opacity = ""; })); }
        inner.append(r.el);
        rows.set(w.path, r);
      }
      r.leaving = false;
      r.el.classList.remove("sr-leaving");
      const users = (w.users || []).join(", ");
      r.where.textContent = (r.folder ? r.folder + "  ·  " : "") + users;
      r.el.style.top = i * ROW_H + "px";
    });
    for (const [path, r] of rows) {
      if (want.some((w) => w.path === path) || r.leaving) continue;
      r.leaving = true;
      r.el.classList.add("sr-leaving");              // fades out where it is
      setTimeout(() => { if (r.leaving) { r.el.remove(); rows.delete(path); } }, 260);
    }
    inner.style.height = want.length * ROW_H + "px";
    empty.style.display = want.length === 0 ? "" : "none";
    placeAll = false;
    layoutCard();
    words();
  }

  function locate(path, file) {
    Promise.resolve(ctx.pick({ mode: "open", title: vm.fillIn(tr("Locate {1}"), file) })).then((p) => {
      if (p) vm.cmd("project relink " + vm.q(path) + " --to " + vm.q(p));
    });
  }
  search.addEventListener("click", () => {
    Promise.resolve(ctx.pick({ mode: "folder", title: tr("Search a folder for the missing files") })).then((d) => {
      if (d) vm.cmd("project relink --folder " + vm.q(d));
    });
  });

  own(host, effect(bind));
  own(host, effect(() => { vm.lang.value; words(); }));
  own(host, effect(() => { ctx.box.value; layoutCard(); }));
  requestAnimationFrame(() => root.classList.add("sx-in", "sx-placed"));
  own(host, effect(() => { if (!ctx.open.value) { root.classList.remove("sx-in"); root.classList.add("sx-out"); } }));

  const close = () => vm.closeSheet();
  scrim.addEventListener("click", close);
  done.addEventListener("click", close);
  const onKey = (e) => {
    if (!ctx.open.peek() || (e.key !== "Escape" && e.key !== "Enter")) return;
    if (e.key === "Enter" && e.target && e.target.tagName === "BUTTON" && root.contains(e.target)) return; // the focused button's own
    close();
    e.preventDefault();
    e.stopPropagation();
  };
  window.addEventListener("keydown", onKey, true);
  own(host, () => window.removeEventListener("keydown", onKey, true));
}
