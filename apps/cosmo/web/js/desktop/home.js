/*
 * Cosmo by arstro — home.js: the launcher (widgets/HomeScreen.cpp + widgets/ProjectCard.cpp).
 *
 * The sidebar (wordmark, New Project / Open Project… / Import Catalog…, Settings / What's New /
 * Help & Documentation, version) and the Recent Projects grid with its search field, cards and
 * the dashed New-Project card. Geometry is HomeScreen.cpp's, in logical px, re-derived on every
 * resize like HomeScreen::layout(); text sits on the native's baselines and is placed with its
 * width ESTIMATE where the native uses it (est()). Strokes are SVG so they straddle their edge
 * like Cairo's (a 1 px hairline at an integer x is two half pixels); the grid clip at
 * (332, 84) therefore eats the outer half of the first column's left and top edges, as native.
 *
 * Motion (R-G-1): hover cross-fades 120 ms per region (HoverFade); cards reflow toward new
 * slots over 260 ms EaseOutCubic (the first placement of a card snaps - HomeScreen.cpp:311);
 * the wheel scroll glides 180 ms; covers fade in as they arrive (240 ms, the loading cover's
 * fade - they arrive after Home is on screen here, natively they are decoded first).
 *
 * The search text is this page's view state. Recents, covers and counts come from the model:
 * `recents` and the adapter method `cover {path, edge}` (blob stream `cover`, meta {path}).
 *
 * Deliberate deviations: a card name wider than the card is end-ellipsised (native overdraws);
 * a card that joins or leaves the list while Home is up fades (native rebuilds the grid).
 */
import { h, bindEffect, own, destroy } from "../core/dom.js";
import { signal, computed, effect } from "../core/signal.js";
import { icon } from "../ui/icons.js";
import { est, text, svg, glyph, at, tokens, lerpColor, brighten, withAlpha, setVars, HOVER_LIFT,
         fitEnd, cardBytes, relativeTime, photosLabel } from "../dialogs/paint.js";

// HomeScreen.cpp:17-53
const SIDEBAR = 300, PAD = 32, HEADER = 60, GAP = 16, MIN_CARD = 220, META = 46, ACTION_H = 34;
const ACTIONS_TOP = 183, BOTTOM_BLOCK = 137, SEARCH_W = 168, SEARCH_H = 26, SEARCH_MIN = 72, HEADER_GAP = 12;
const GRID_TOP = HEADER + 24;

// HomeScreen.cpp:62-117 inline glyphs, at their only call sizes.
const G = {
  plus13: () => glyph(["M1.04 6.5 L11.96 6.5", "M6.5 1.04 L6.5 11.96"], 13, 1.6),
  plus20: () => glyph(["M1.6 10 L18.4 10", "M10 1.6 L10 18.4"], 20, 1.4),
  folder13: () => glyph(["M1 3.9 L5.46 3.9 L6.76 1.95 L12 1.95 L12 12 L1 12 Z"], 13, 1.2),
  clock13: () => glyph([{ circle: [6.5, 6.5, 5.46] }, "M6.5 6.5 L6.5 3.224", "M6.5 6.5 L9.23 6.5"], 13, 1.2),
  search24: () => glyph([{ circle: [8.2, 8.2, 7.2] }, "M13.96 13.96 L23 23"], 24, 1.4),
  gear11: () => glyph([{ circle: [5.5, 5.5, 2.42] }, { circle: [5.5, 5.5, 4.62] }], 11, 1.1),
  spark11: () => glyph(["M5.5 0.88 L5.5 10.12", "M0.88 5.5 L10.12 5.5", "M2.728 2.728 L8.272 8.272", "M2.728 8.272 L8.272 2.728"], 11, 1.1),
  help11: () => {
    // iconHelp: the "?" is drawText("?", cx - 2.4, cy + 3.5, 9, sansMedium) - a glyph baseline.
    const q = svg("text", { x: 3.1, y: 9, fill: "currentColor", "font-size": 9, style: "font-family: var(--font-sans-medium)" });
    q.textContent = "?";
    return glyph([{ circle: [5.5, 5.5, 4.62] }], 11, 1.1, 11, q);
  },
};

// ------------------------------------------------------------------ covers (adapter `cover`)
const COVERS = new WeakMap();          // session -> {map: path -> signal(url|null), off}
/** A recent project's cover as a signal of an object URL (null until / unless it arrives). */
export function coverOf(vm, path) {
  const S = vm.session;
  let st = COVERS.get(S);
  if (!st) {
    st = { map: new Map(), unsupported: false };
    COVERS.set(S, st);
    try {
      S.bridge.onBlob("cover", (blob, hdr) => {
        const p = hdr && hdr.meta && hdr.meta.path;
        if (!p) return;
        let e = st.map.get(p);
        if (!e) { e = { sig: signal(null), asked: true }; st.map.set(p, e); }
        const old = e.sig.peek();
        e.sig.value = URL.createObjectURL(blob);
        if (old) setTimeout(() => URL.revokeObjectURL(old), 4000);
      });
    } catch (err) { st.unsupported = true; }
  }
  if (!path) return signal(null);
  let e = st.map.get(path);
  if (!e) { e = { sig: signal(null), asked: false }; st.map.set(path, e); }
  if (!e.asked && !st.unsupported && vm.status.peek() === "running") {
    e.asked = true;
    S.call("cover", { path, edge: 480 }).catch(() => { e.asked = "failed"; });
  }
  return e.sig;
}

// ------------------------------------------------------------------ helpers
/** A rect painted like drawRoundedRect(filledStroked): an SVG so the stroke straddles. */
function box(cls, x, y, w, hh, r = 2) {
  const s = svg("svg", { class: "hm-box " + cls, width: w, height: hh });
  s.append(svg("rect", { x: 0, y: 0, width: "100%", height: "100%", rx: r, ry: r }));
  return at(s, x, y);
}
const T = (cls, str, x, base, px, fam, spacing) => text(h("div.hm-t" + (cls ? "." + cls : ""), { text: str }), x, base, px, fam, spacing);

function titleBlockW(title, count) { return 20 + est(title, 13) + 8 + est(String(count), 10); }

/**
 * mountHome(el, vm, ctx, on) - `on` = {newProject(), openProject(), importCatalog(),
 * openRecent(recent, rectInRoot, cardData), settings()}; returns {setWordmarkHidden(b),
 * reset()} for the transitions in screens.js.
 */
export function mountHome(el, vm, ctx, on) {
  const root = h("div.hm");
  el.append(root);

  // Hover end-states the native derives from two tokens (HomeScreen.cpp:525-545, ProjectCard.cpp:17-20,
  // TextBox hover). Derived here from the same tokens, never typed.
  const tk = tokens();
  setVars(root, {
    "hm-act0-h": brighten(tk.primary, HOVER_LIFT),
    "hm-act1-fill-h": withAlpha(tk.primary, 0.10),
    "hm-act1-stroke-h": lerpColor(tk.border, tk.primary, 0.5),
    "hm-act-fg-h": lerpColor(tk["muted-foreground"], tk.foreground, 0.6),
    "hm-link-fg-h": lerpColor(tk["muted-foreground"], tk.foreground, 0.7),
    "hm-card-fill-h": brighten(tk["folder-chip-bg"], 0.06),
    "hm-card-stroke-h": withAlpha(tk.primary, 0.7),
    "hm-new-dash": withAlpha(tk.border, 0.6),      // Color{border.r,g,b, 0.6} - HomeScreen.cpp:612
    "hm-new-dash-h": lerpColor(withAlpha(tk.border, 0.6), tk.primary, 0.5),
    "hm-search-fill-h": brighten(tk.input, HOVER_LIFT),
    "hm-search-stroke-h": lerpColor(tk.border, tk.primary, 0.5),
  });

  // ---------------------------------------------------------------- static chrome (SVG)
  const chrome = svg("svg", { class: "hm-chrome" });
  const sideRect = svg("rect", { x: 0, y: 0, width: SIDEBAR, class: "hm-side" });
  const vLine = svg("line", { x1: SIDEBAR, y1: 0, x2: SIDEBAR, class: "hm-hair" });
  const hLine = svg("line", { x1: SIDEBAR, y1: HEADER, y2: HEADER, class: "hm-hair" });
  const logoDiv = svg("line", { x1: PAD, y1: 159, x2: SIDEBAR - PAD, y2: 159, class: "hm-hair" });
  const botDiv = svg("line", { x1: PAD, x2: SIDEBAR - PAD, class: "hm-hair" });
  chrome.append(sideRect, vLine, hLine, logoDiv, botDiv);
  root.append(chrome);

  // ---------------------------------------------------------------- sidebar
  const sp = -0.03 * 46;
  const wordmark = T("hm-wm", "cosmo", PAD, 96, 46, "semibold", sp);
  wordmark.append(h("span.hm-dot", { text: "." }));
  root.append(wordmark,
    T("hm-muted", "Develop, grade, and export", PAD, 118, 11),
    T("hm-muted", "your photography.", PAD, 133, 11));

  const acts = [
    { label: "New Project", icon: G.plus13, fam: "semibold", go: () => on.newProject() },
    { label: "Open Project…", icon: G.folder13, fam: "sans", go: () => on.openProject() },
    { label: "Import Catalog…", icon: () => icon("upload", 13, 1.2), fam: "sans", go: () => on.importCatalog() },
  ];
  acts.forEach((a, i) => {
    const y = ACTIONS_TOP + i * (ACTION_H + 6);
    const b = h("button.hm-act.hm-act" + i, { style: { left: PAD + "px", top: y + "px", width: SIDEBAR - 2 * PAD + "px", height: ACTION_H + "px" },
      "aria-label": a.label, onclick: a.go });
    b.append(box("hm-act-bg", 0, 0, SIDEBAR - 2 * PAD, ACTION_H));
    b.append(at(a.icon(), 14, (ACTION_H - 13) / 2, "hm-ic"), T("hm-act-label", a.label, 36, ACTION_H / 2 + 4, 12, a.fam));
    root.append(b);
  });

  // bottom block: anchored to the window bottom (blockTop = H - 137)
  const bottom = h("div.hm-bottom", { style: { top: `calc(100% - ${BOTTOM_BLOCK}px)` } });
  const links = [["Settings", G.gear11, () => on.settings()], ["What's New", G.spark11, null], ["Help & Documentation", G.help11, null]];
  links.forEach(([label, ig, go], i) => {
    const y = 16 + 20 * i;
    const b = h("button.hm-link", { style: { left: PAD + "px", top: y + "px", width: SIDEBAR - 2 * PAD + "px", height: "18px" },
      "aria-label": label, onclick: () => { if (go) go(); } });   // What's New / Help: inert (R-HOME-8)
    b.append(box("hm-link-wash", -6, -1, SIDEBAR - 2 * PAD + 12, 20));
    b.append(at(ig(), 0, 3.5, "hm-ic"), T("hm-link-label", label, 18, 13, 11));
    bottom.append(b);
  });
  bottom.append(T("hm-version", "cosmo v1.0.0", PAD, 92, 10, "mono"));
  root.append(bottom);

  // ---------------------------------------------------------------- header
  root.append(at(G.clock13(), SIDEBAR + PAD, HEADER / 2 - 6.5, "hm-ic hm-muted-ic"));
  const titleEl = T("hm-title", "Recent Projects", SIDEBAR + PAD + 20, HEADER / 2 + 4, 13, "semibold");
  const countEl = T("hm-count", "0", 0, HEADER / 2 + 4, 10);
  root.append(titleEl, countEl);

  const query = signal("");                       // VIEW: this page's search text
  const searchWrap = h("div.hm-search");
  // Artboard TextBox strokes its chrome INSIDE its box (the native render's field edge is one
  // full pixel at x, not two halves), so this rect is inset by half the stroke.
  const searchBox = box("hm-search-bg", 0, 0, SEARCH_W, SEARCH_H);
  const searchRect = searchBox.firstChild;
  Object.entries({ x: 0.5, y: 0.5, height: SEARCH_H - 1, rx: 1.5, ry: 1.5 }).forEach(([k, v]) => searchRect.setAttribute(k, v));
  const input = h("input.hm-search-in", { type: "text", placeholder: "Search…", spellcheck: "false", autocomplete: "off",
    "aria-label": "Search projects", oninput: () => { query.value = input.value; } });
  searchWrap.append(searchBox, input);
  root.append(searchWrap);

  // ---------------------------------------------------------------- grid
  const grid = h("div.hm-grid");
  const content = h("div.hm-grid-content");
  grid.append(content);
  root.append(grid);

  const empty = h("div.hm-empty");
  const emptyIc = at(G.search24(), 0, 0, "hm-ic hm-empty-ic");
  const emptyText = T("hm-empty-text", "", 0, 0, 12);
  empty.append(emptyIc, emptyText);
  grid.append(empty);

  // The dashed New-Project card (HomeScreen.cpp:607-622): the thumbnail band only.
  const newCard = h("button.hm-new", { "aria-label": "New Project", onclick: () => on.newProject() });
  const dash = svg("svg", { class: "hm-new-dash" });
  const dashLines = [0, 1, 2, 3].map(() => svg("line", {}));
  dash.append(...dashLines);
  const newIc = at(G.plus20(), 0, 0, "hm-ic hm-new-ic");
  const newLabel = T("hm-new-label", "New Project", 0, 0, 11);
  newCard.append(dash, newIc, newLabel);
  content.append(newCard);

  // ---------------------------------------------------------------- state
  const home = { el: root, visible: true };
  const size = signal({ w: el.clientWidth || 1600, h: el.clientHeight || 1000 });
  const ro = new ResizeObserver(() => { size.value = { w: el.clientWidth, h: el.clientHeight }; });
  ro.observe(el);
  own(root, () => ro.disconnect());

  const cards = new Map();                        // path -> {el, placed, recent, cover}
  let scroll = 0, contentH = 0;
  const shown = computed(() => {
    const q = query.value.toLowerCase();
    return vm.recents.value.filter((r) => !q || String(r.name || "").toLowerCase().includes(q));
  });

  function makeCard(r) {
    const c = h("button.hm-card", { "aria-label": r.name });
    const body = svg("svg", { class: "hm-card-body" });
    body.append(svg("rect", { x: 0, y: 0, width: "100%", height: "100%", rx: 2, ry: 2 }));
    const band = h("div.hm-card-band");
    const img = h("img.hm-card-cover", { alt: "", draggable: "false" });
    band.append(img);
    const meta = h("div.hm-card-meta");
    const name = T("hm-card-name", "", 12, 18, 11, "medium");
    const line = T("hm-card-line", "", 12, 33, 10);
    const date = T("hm-card-date", "", 0, 33, 9);
    meta.append(name, line, date);
    c.append(body, band, meta);
    const card = { el: c, placed: false, recent: r, img, name, line, date, w: 0, x: 0, y: 0 };
    c.addEventListener("click", () => {
      const rr = c.getBoundingClientRect(), rootR = el.getBoundingClientRect();
      const k = el.offsetWidth ? rootR.width / el.offsetWidth : 1;
      const rect = { x: (rr.left - rootR.left) / k, y: (rr.top - rootR.top) / k, w: card.w, h: card.w * 9 / 16 + META };
      on.openRecent(card.recent, rect, cardData(card.recent), card.coverUrl || null);
    });
    img.addEventListener("load", () => img.classList.add("on"));
    own(c, effect(() => {
      const url = coverOf(vm, card.recent.firstImagePath).value;
      card.coverUrl = url;
      if (url && img.getAttribute("src") !== url) { img.classList.remove("on"); img.src = url; }
    }));
    return card;
  }
  function cardData(r) {
    return { name: r.name || "", photos: photosLabel(r.photoCount || 0), size: cardBytes(r.sizeBytes), date: relativeTime(r.lastOpened) };
  }
  function fillCard(card, w) {
    const d = cardData(card.recent);
    card.name.textContent = fitEnd(d.name, w - 24, 11);
    card.line.textContent = d.photos + (d.size ? "  ·  " + d.size : "");
    card.date.textContent = d.date;
    card.date.style.left = `calc(100% - ${12 + est(d.date, 9)}px)`;
  }

  /** HomeScreen::layout + relayoutGrid, from the current window size and filter. */
  function layout() {
    const { w: W, h: H } = size.value;
    const list = shown.value;
    const count = list.length;
    // header row (HomeScreen.cpp:217-238)
    const room = W - PAD - SEARCH_MIN - HEADER_GAP - (SIDEBAR + PAD);
    const title = titleBlockW("Recent Projects", count) <= room ? "Recent Projects" : "Recent";
    titleEl.textContent = title;
    countEl.textContent = String(count);
    countEl.style.left = SIDEBAR + PAD + 20 + est(title, 13) + 8 + "px";
    const sx = Math.max(SIDEBAR + PAD + titleBlockW(title, count) + HEADER_GAP, W - PAD - SEARCH_W);
    const sw = Math.max(SEARCH_MIN, W - PAD - sx);
    Object.assign(searchWrap.style, { left: Math.floor(sx) + "px", top: (HEADER - SEARCH_H) / 2 + "px", width: sw + "px", height: SEARCH_H + "px" });
    at(searchBox, sx - Math.floor(sx), 0);
    input.style.left = sx - Math.floor(sx) + "px";
    searchBox.setAttribute("width", sw);
    searchRect.setAttribute("width", sw - 1);
    // chrome
    chrome.setAttribute("width", W); chrome.setAttribute("height", H);
    sideRect.setAttribute("height", H);
    vLine.setAttribute("y2", H);
    hLine.setAttribute("x2", W);
    botDiv.setAttribute("y1", H - BOTTOM_BLOCK); botDiv.setAttribute("y2", H - BOTTOM_BLOCK);
    // grid
    const gridLeft = SIDEBAR + PAD;
    const availW = Math.max(MIN_CARD, W - PAD - gridLeft);
    const cols = Math.max(1, Math.floor((availW + GAP) / (MIN_CARD + GAP)));
    const cardW = (availW - (cols - 1) * GAP) / cols;
    const thumbH = cardW * 9 / 16, cardH = thumbH + META;
    Object.assign(grid.style, { left: gridLeft + "px", top: GRID_TOP + "px", width: Math.max(0, W - PAD - gridLeft) + "px",
                                height: Math.max(0, H - GRID_TOP) + "px" });
    const seen = new Set();
    let slot = 0;
    for (const r of list) {
      seen.add(r.path);
      let card = cards.get(r.path);
      const fresh = !card;
      if (fresh) {
        card = makeCard(r);
        cards.set(r.path, card);
        content.insertBefore(card.el, newCard);
      } else if (card.leaving) {
        card.leaving = false; card.el.classList.remove("leave");
        card.placed = false;                    // it reappears where it now belongs
      }
      card.recent = r;
      const x = (slot % cols) * (cardW + GAP), y = Math.floor(slot / cols) * (cardH + GAP);
      place(card, x, y, cardW, cardH, fresh);
      fillCard(card, cardW);
      slot++;
    }
    for (const [path, card] of cards) {
      if (seen.has(path) || card.leaving) continue;
      // Filtered out or dropped from the recents: fade out, then go (R-G-1; natively the
      // card is simply not drawn any more). Coming back before it has gone cancels it.
      card.leaving = true;
      card.el.classList.add("leave");
      card.el.classList.remove("live");
      setTimeout(() => { if (card.leaving) { destroy(card.el); card.el.remove(); cards.delete(path); } }, 260);
    }
    // the trailing New-Project card
    const nx = (slot % cols) * (cardW + GAP), ny = Math.floor(slot / cols) * (cardH + GAP);
    Object.assign(newCard.style, { transform: `translate(${nx}px, ${ny}px)`, width: cardW + "px", height: thumbH + "px" });
    if (!newCard.placed) { newCard.placed = true; requestAnimationFrame(() => newCard.classList.add("live")); }
    dash.setAttribute("width", cardW); dash.setAttribute("height", thumbH);
    const E = [[0, 0, cardW, 0], [cardW, 0, cardW, thumbH], [cardW, thumbH, 0, thumbH], [0, thumbH, 0, 0]];
    dashLines.forEach((l, i) => { const [x1, y1, x2, y2] = E[i]; l.setAttribute("x1", x1); l.setAttribute("y1", y1); l.setAttribute("x2", x2); l.setAttribute("y2", y2); });
    at(newIc, cardW / 2 - 10, thumbH / 2 - 16);
    text(newLabel, cardW / 2 - est("New Project", 11) / 2, thumbH / 2 + 18, 11);
    slot++;
    const rows = Math.ceil(slot / cols);
    contentH = rows * cardH + Math.max(0, rows - 1) * GAP;
    // search with no match (HomeScreen.cpp:584-590), in grid-clip coordinates
    const q = query.value;
    const noMatch = count === 0 && vm.recents.value.length > 0;
    empty.classList.toggle("on", noMatch);
    if (noMatch) {
      const cx = (W + SIDEBAR) / 2 - gridLeft;
      at(emptyIc, cx - 12, 40);
      const msg = `No projects match "${q}"`;
      emptyText.textContent = msg;
      text(emptyText, cx - est(msg, 12) / 2, 80, 12);
    }
    scrollTo(scroll, true);
  }

  function place(card, x, y, w, hh, fresh) {
    card.x = x; card.y = y; card.w = w;
    const s = card.el.style;
    if (fresh || !card.placed) card.el.classList.remove("live");
    s.transform = `translate(${x}px, ${y}px)`;
    s.width = w + "px";
    s.height = hh + "px";
    if (!card.placed) {
      card.placed = true;
      requestAnimationFrame(() => requestAnimationFrame(() => card.el.classList.add("live")));
      if (fresh && root.isConnected && home.visible) {
        card.el.classList.add("enter");
        requestAnimationFrame(() => requestAnimationFrame(() => card.el.classList.remove("enter")));
      }
    }
  }

  function maxScroll() { return Math.max(0, contentH - (size.peek().h - GRID_TOP) + PAD); }
  function scrollTo(v, clampOnly) {
    const next = Math.min(Math.max(0, v), maxScroll());
    if (clampOnly && next === scroll) return;
    scroll = next;
    content.style.transform = `translateY(${-scroll}px)`;
  }
  root.addEventListener("wheel", (e) => {
    e.preventDefault();
    const notch = -e.deltaY / (e.deltaMode === 1 ? 3 : 100);
    scrollTo(scroll - notch * 60);
  }, { passive: false });

  bindEffect(root, () => { size.value; shown.value; vm.recents.value; layout(); });

  return Object.assign(home, {
    setWordmarkHidden(b) { wordmark.classList.toggle("hidden", !!b); },
    /** A fresh visit (return transition's ReturnLoad): the list starts at the top, placed
     *  without travel, like setRecents(). */
    reset() {
      content.classList.add("snap");
      scroll = 0;
      content.style.transform = "translateY(0px)";
      for (const c of cards.values()) { c.placed = false; c.el.classList.remove("live"); }
      layout();
      requestAnimationFrame(() => requestAnimationFrame(() => content.classList.remove("snap")));
    },
    focusSearch() { input.focus(); },
    isSearch(t) { return t === input; },
  });
}
