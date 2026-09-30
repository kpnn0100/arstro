/*
 *  Cosmo by arstro — the web front end (R-NTWB-5): the third view of CosmoService.
 *
 *  Seam rule (R-SVC-2/3/4): every change leaves as ONE LINE of the command grammar
 *  (`command` over NTWB); everything drawn comes from the `model` state, the service's
 *  events and the `preview` / `thumb` frames. What stays here is presentation — easing,
 *  hover, which tab is open, a drag in flight — and nothing a second front end would also
 *  have to compute: the slider catalogue and its unit conversions come from the adapter
 *  (`controls`), sampled from the same C++ functions the window uses.
 *
 *  R-G-1 applies: no visible property changes in one frame. CSS transitions carry the
 *  easing for positions, sizes and opacities; the histogram eases between frames here.
 */
(function () {
  "use strict";
  const app = NTWB.connect();
  const $ = (id) => document.getElementById(id);
  const h = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; };

  let model = null;
  let controls = null;
  const sliders = new Map();        // field -> {set(engine), el}
  let dragging = null;              // field being dragged (the model does not move that thumb)
  let toastedEvent = false;         // a command.rejected event already said what lastError says
  let wbArmed = false;

  // ── commands: the only way anything changes ──────────────────────────────────────────
  function q(s) {
    if (/["\n]/.test(s)) throw new Error("paths with quotes or line breaks are not supported");
    return /\s/.test(s) ? `"${s}"` : s;
  }
  function cmd(line) {
    return app.call("command", { line }).catch((e) => { toast(e.message, true); throw e; });
  }
  const cmdQuiet = (line) => cmd(line).catch(() => {});
  const notify = (line) => app.notify("command", { line });
  const num = (v) => (Math.abs(v) >= 1000 ? v.toFixed(1) : String(+v.toFixed(4)));

  // ── toasts ───────────────────────────────────────────────────────────────────────────
  function toast(text, err) {
    const t = h("div", "toast" + (err ? " err" : ""), text);
    $("toasts").append(t);
    requestAnimationFrame(() => requestAnimationFrame(() => t.classList.add("on")));
    setTimeout(() => { t.classList.remove("on"); setTimeout(() => t.remove(), 260); }, err ? 5000 : 2600);
  }

  // ── screens ──────────────────────────────────────────────────────────────────────────
  function showScreen(name) {
    const id = name === "editor" ? "editor" : name === "loading" ? "loading" : "home";
    for (const s of document.querySelectorAll(".screen")) s.classList.toggle("on", s.id === id);
  }

  // ── the model -> every view ──────────────────────────────────────────────────────────
  function parseParams(text) {
    const p = {};
    for (const line of (text || "").split("\n")) {
      const i = line.indexOf("=");
      if (i > 0) { const v = Number(line.slice(i + 1)); if (!Number.isNaN(v)) p[line.slice(0, i)] = v; }
    }
    return p;
  }

  function render(m) {
    const prev = model;
    model = m;
    showScreen(m.screen);
    renderHome(m);
    renderLoading(m);
    renderEditor(m, prev);
  }

  function renderHome(m) {
    const box = $("recents");
    const sig = JSON.stringify((m.recents || []).map((r) => [r.path, r.photoCount, r.lastOpened]));
    if (box.dataset.sig === sig) return;
    box.dataset.sig = sig;
    box.replaceChildren();
    if (!(m.recents || []).length) { box.append(h("div", "empty", "No recent projects. Open one or import photos.")); return; }
    for (const r of m.recents) {
      const c = h("div", "card");
      c.append(h("div", "name", r.name || r.path.split("/").pop()),
               h("div", "meta", `${r.photoCount} photos · ${ago(r.lastOpened)}`),
               h("div", "meta", r.path));
      c.onclick = () => cmdQuiet("project open " + q(r.path));
      box.append(c);
    }
  }
  function ago(unix) {
    if (!unix) return "never";
    const s = Math.max(0, Date.now() / 1000 - unix);
    return s < 60 ? "just now" : s < 3600 ? Math.floor(s / 60) + " min ago" : s < 86400 ? Math.floor(s / 3600) + " h ago" : Math.floor(s / 86400) + " d ago";
  }

  function renderLoading(m) {
    $("load-bar").style.width = (m.loadPermille || 0) / 10 + "%";
    $("load-status").textContent = m.loadStatus || (m.loadStage ? m.loadStage + "…" : "Loading…");
    $("load-count").textContent = m.loadTotal ? `${m.loadDone} / ${m.loadTotal}` : "";
  }

  function renderEditor(m, prev) {
    $("project-name").textContent = m.projectName || (m.imageCount ? `${m.imageCount} photos (unsaved)` : "");
    $("dirty").classList.toggle("on", !!m.dirty);
    for (const id of ["undo", "undo2"]) $(id).disabled = !m.canUndo;
    for (const id of ["redo", "redo2"]) $(id).disabled = !m.canRedo;
    renderTree(m);
    renderFilmstrip(m, prev);
    renderBreadcrumb(m);
    renderSliders(m);
    const sel = m.nodes.find((n) => n.node === m.selectedNode);
    $("bypass").disabled = !sel;
    $("bypass").textContent = sel && sel.bypass ? "Bypassed" : "Bypass";
    $("bypass").classList.toggle("primary", !!(sel && sel.bypass));
    const exp = $("export");
    exp.textContent = m.exportActive ? `Exporting ${m.exportDone}/${m.exportTotal}` : "Export";
    exp.disabled = !m.imageCount || m.exportActive;
    $("canvas-hint").style.opacity = m.currentSlot >= 0 ? 0 : 1;
    if (m.currentSlot < 0) for (const im of [$("img-a"), $("img-b")]) im.classList.remove("on");
    kv($("history-kv"), [["Steps", `${m.historyCurrent + 1} / ${m.historyNodes}`], ["Last", m.historyLabel || "–"]]);
    kv($("meta-kv"), (m.metadata || []).map((r) => [r.label, r.value]).concat(m.metadata ? [] : [["", "Press Read metadata"]]));
    kv($("machine-kv"), [["Source", m.sourceWidth ? `${m.sourceWidth} × ${m.sourceHeight}` : "–"],
                         ["Preview", `${m.frameLevelEdge || "–"} px · level ${m.frameLevel}${m.refining ? " · refining" : ""}`],
                         ["CPU", `${m.budgetPercent}% · ${m.budgetEngineThreads} engine + ${m.budgetDecodeWorkers} decode`],
                         ["GPU", m.gpuAvailable ? (m.gpuActive ? "in use" : "available, off") : "none"],
                         ["Memory", `${m.engineResidentMB} MB resident`]]);
    // Only a NEW rejection is news: the model keeps the last one, which a page opened later must
    // not announce as if it had just happened (command.rejected events carry the live ones).
    if (prev && m.lastError && prev.lastError !== m.lastError && !toastedEvent) toast(m.lastError, true);
    toastedEvent = false;
  }

  function kv(dl, rows) {
    const sig = JSON.stringify(rows);
    if (dl.dataset.sig === sig) return;
    dl.dataset.sig = sig;
    dl.replaceChildren(...rows.flatMap(([k, v]) => [h("dt", null, k), h("dd", null, v)]));
  }

  // ── the tree (LeftRail) ──────────────────────────────────────────────────────────────
  const rows = new Map();
  const EYE = '<svg viewBox="0 0 24 24"><path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/></svg>';
  const EYE_OFF = '<svg viewBox="0 0 24 24"><path d="M3 3l18 18M10.6 5.1A10 10 0 0 1 12 5c6.5 0 10 7 10 7a17 17 0 0 1-3 3.9M6.6 6.6A17 17 0 0 0 2 12s3.5 7 10 7a9.6 9.6 0 0 0 4.4-1"/></svg>';
  const FOLDER = '<svg viewBox="0 0 24 24"><path d="M3 7.5A2.5 2.5 0 0 1 5.5 5H9l2 2.5h7.5A2.5 2.5 0 0 1 21 10v7.5a2.5 2.5 0 0 1-2.5 2.5h-13A2.5 2.5 0 0 1 3 17.5z"/></svg>';
  const PHOTO = '<svg viewBox="0 0 24 24"><rect x="3" y="4" width="18" height="16" rx="2"/><circle cx="9" cy="10" r="2"/><path d="m21 17-5-5-9 8"/></svg>';
  function svg(markup) { const s = h("span"); s.innerHTML = markup; return s.firstChild; }   // static markup only

  function renderTree(m) {
    const tree = $("tree");
    const seen = new Set();
    let before = tree.firstChild;
    for (const n of m.nodes) {
      seen.add(n.node);
      let r = rows.get(n.node);
      if (!r) {
        r = { el: h("div", "row"), icon: null, nm: h("span", "nm"), tag: h("span", "tag"), eye: h("button", "btn ghost icon-btn act") };
        r.el.append(r.nm, r.tag, r.eye);
        r.el.onclick = (e) => cmdQuiet(`select ${n.node}${e.shiftKey ? " range" : e.ctrlKey || e.metaKey ? " add" : ""}`);
        r.eye.onclick = (e) => { e.stopPropagation(); const cur = model.nodes.find((x) => x.node === n.node); cmdQuiet(`bypass ${n.node} ${cur && cur.bypass ? "off" : "on"}`); };
        r.el.style.animation = "fade-in var(--t-scroll) var(--ease)";
        rows.set(n.node, r);
      }
      const kind = n.kind;
      if (r.kind !== kind) { r.kind = kind; if (r.icon) r.icon.remove(); r.icon = svg(kind === "group" ? FOLDER : PHOTO); r.el.prepend(r.icon); }
      r.el.style.paddingLeft = 9.75 + n.depth * 13 + "px";
      r.nm.textContent = n.name || (kind === "group" ? "Group" : "photo");
      r.tag.textContent = kind === "pending" ? "loading" : kind === "failed" ? "missing" : "";
      r.el.classList.toggle("sel", n.selected || n.node === m.selectedNode);
      r.el.classList.toggle("bypassed", n.bypass);
      if (r.bypass !== n.bypass) { r.bypass = n.bypass; r.eye.replaceChildren(svg(n.bypass ? EYE_OFF : EYE)); }
      r.eye.classList.toggle("on", n.bypass);
      if (r.el !== before) tree.insertBefore(r.el, before); else before = before.nextSibling;
    }
    for (const [id, r] of rows) if (!seen.has(id)) { r.el.remove(); rows.delete(id); }
  }

  // ── filmstrip + thumbnails ───────────────────────────────────────────────────────────
  const cells = new Map();
  let thumbSig = "";
  function renderFilmstrip(m, prev) {
    const strip = $("filmstrip");
    const images = m.nodes.filter((n) => n.kind !== "group");
    const seen = new Set();
    let before = $("ring").nextSibling;
    for (const n of images) {
      seen.add(n.node);
      let c = cells.get(n.node);
      if (!c) {
        c = { el: h("div", "cell"), img: h("img") };
        c.img.alt = "";
        c.el.append(c.img);
        c.el.onclick = () => cmdQuiet(`select ${n.node}`);
        cells.set(n.node, c);
      }
      c.el.classList.toggle("bypassed", n.bypass);
      c.el.title = n.name;
      if (c.el !== before) strip.insertBefore(c.el, before); else before = before.nextSibling;
    }
    for (const [id, c] of cells) if (!seen.has(id)) { c.el.remove(); cells.delete(id); }
    const sig = images.filter((n) => n.slot >= 0).map((n) => n.node + ":" + n.slot).join(",");
    if (sig !== thumbSig && sig) { thumbSig = sig; app.call("thumbs", {}).catch(() => {}); }
    requestAnimationFrame(placeRing);
  }
  function placeRing() {
    const ring = $("ring");
    const c = model && cells.get(model.selectedNode);
    if (!c) { ring.style.opacity = 0; return; }
    ring.style.left = c.el.offsetLeft + "px";
    ring.style.width = c.el.offsetWidth + "px";
    ring.style.opacity = 1;
    const strip = $("filmstrip");
    if (c.el.offsetLeft < strip.scrollLeft || c.el.offsetLeft + c.el.offsetWidth > strip.scrollLeft + strip.clientWidth)
      strip.scrollLeft = c.el.offsetLeft - strip.clientWidth / 2 + c.el.offsetWidth / 2;   // scroll-behavior: smooth
  }
  app.onBlob("thumb", (blob, hd) => {
    const c = cells.get(hd.meta.node);
    if (!c) return;
    const url = URL.createObjectURL(blob);
    c.img.onload = () => { c.img.classList.add("on"); if (c.url) URL.revokeObjectURL(c.url); c.url = url; };
    c.img.src = url;
  });

  function renderBreadcrumb(m) {
    const bc = $("breadcrumb");
    const byNode = new Map(m.nodes.map((n, i) => [n.node, i]));
    const i = byNode.get(m.selectedNode);
    const parts = [];
    for (let k = i; k !== undefined && k >= 0; k = m.nodes[k].parent) parts.unshift(m.nodes[k].name || "…");
    const text = [m.projectName || "Project", ...parts];
    const sig = text.join("/");
    if (bc.dataset.sig === sig) return;
    bc.dataset.sig = sig;
    bc.replaceChildren(...text.flatMap((t, k) => (k ? [h("span", null, "›"), k === text.length - 1 ? h("b", null, t) : h("span", null, t)] : [h("span", null, t)])));
  }

  // ── preview ──────────────────────────────────────────────────────────────────────────
  let front = "img-a";
  app.onBlob("preview", (blob, hd) => {
    const back = front === "img-a" ? "img-b" : "img-a";
    const img = $(back);
    const url = URL.createObjectURL(blob);
    img.onload = () => {
      img.classList.add("on");
      $(front).classList.remove("on");
      const old = $(front).dataset.url;
      setTimeout(() => { if (old) URL.revokeObjectURL(old); }, 400);
      front = back;
    };
    img.dataset.url = url;
    img.src = url;
    const m = hd.meta || {};
    const badge = $("badge");
    badge.textContent = `${m.w}×${m.h} · ${Math.round(m.ms || 0)} ms${m.level ? " · level " + m.level : ""}`;
    badge.classList.add("on");
    if (m.hist) setHistogram(m.hist);
  });

  // ── histogram: eased between frames (R-G-1) ──────────────────────────────────────────
  let histFrom = null, histTo = null, histT0 = 0;
  function setHistogram(hist) {
    histFrom = histNow();
    histTo = hist;
    histT0 = performance.now();
    requestAnimationFrame(drawHistogram);
  }
  function histNow() {
    if (!histTo) return null;
    const t = Math.min(1, (performance.now() - histT0) / 180);
    const e = 1 - Math.pow(1 - t, 3);
    const out = {};
    for (const k of ["r", "g", "b", "lum"]) out[k] = histTo[k].map((v, i) => (histFrom ? histFrom[k][i] + (v - histFrom[k][i]) * e : v));
    return out;
  }
  function drawHistogram() {
    const cv = $("hist");
    const w = cv.clientWidth, hgt = cv.clientHeight, dpr = window.devicePixelRatio || 1;
    if (cv.width !== w * dpr) { cv.width = w * dpr; cv.height = hgt * dpr; }
    const ctx = cv.getContext("2d");
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, hgt);
    const hs = histNow();
    if (!hs) return;
    const max = Math.max(1, ...hs.r, ...hs.g, ...hs.b) * 1.05;
    const pad = 6;
    const path = (bins) => {
      ctx.beginPath();
      ctx.moveTo(pad, hgt - pad);
      bins.forEach((v, i) => ctx.lineTo(pad + (i / (bins.length - 1)) * (w - 2 * pad), hgt - pad - Math.sqrt(v / max) * (hgt - 2 * pad)));
      ctx.lineTo(w - pad, hgt - pad);
      ctx.closePath();
    };
    ctx.globalCompositeOperation = "lighter";
    for (const [k, c] of [["r", "rgba(229,83,75,0.55)"], ["g", "rgba(63,185,80,0.55)"], ["b", "rgba(79,126,247,0.6)"]]) {
      path(hs[k]);
      ctx.fillStyle = c;
      ctx.fill();
    }
    ctx.globalCompositeOperation = "source-over";
    if (performance.now() - histT0 < 200) requestAnimationFrame(drawHistogram);
  }
  window.addEventListener("resize", () => requestAnimationFrame(drawHistogram));

  // ── sliders (SliderRow), from the adapter's catalogue ────────────────────────────────
  const ENGINE_DEFAULT = { temp: 6500, sharpenRadius: 1 };   // EditParams defaults that are not 0
  function interp(stops, x, from, to) {
    // stops: [[track, engine], ...]; `from`/`to` pick the axis (0 = track, 1 = engine)
    const asc = stops[stops.length - 1][from] >= stops[0][from];
    for (let i = 1; i < stops.length; i++) {
      const a = stops[i - 1], b = stops[i];
      const lo = Math.min(a[from], b[from]), hi = Math.max(a[from], b[from]);
      if (x >= lo && x <= hi) {
        const t = b[from] === a[from] ? 0 : (x - a[from]) / (b[from] - a[from]);
        return a[to] + (b[to] - a[to]) * t;
      }
    }
    const first = stops[0], last = stops[stops.length - 1];
    return (asc ? x < first[from] : x > first[from]) ? first[to] : last[to];
  }
  function readout(c, track, engine) {
    if (c.field === "exposure") return (engine >= 0 ? "+" : "") + engine.toFixed(2);
    if (c.field === "temp") return Math.round(engine) + "K";
    if (c.field === "sharpenRadius") return engine.toFixed(1);
    return String(Math.round(track));
  }

  function buildSliders() {
    const page = $("page-adjust");
    page.replaceChildren();
    sliders.clear();
    for (const sec of controls) {
      const head = h("div", "section-h", sec.title);
      if (sec.whiteBalancePicker) {
        head.append(h("span", "grow"));
        const pick = h("button", "btn ghost", "Pick white");
        pick.title = "Click a neutral point of the photo";
        pick.onclick = () => armWb(!wbArmed, pick);
        head.append(pick);
      }
      page.append(head);
      for (const c of sec.controls) page.append(sliderRow(c));
    }
    if (model) renderSliders(model);
  }

  function sliderRow(c) {
    const row = h("div", "srow");
    const top = h("div", "top");
    const val = h("span", "val");
    top.append(h("span", null, c.label), val);
    const track = h("div", "track" + (c.rampFrom ? " ramp" : ""));
    const line = h("div", "rail-line"), fill = h("div", "fill"), thumb = h("div", "thumb");
    if (c.rampFrom) line.style.background = `linear-gradient(90deg, ${c.rampFrom}, ${c.rampTo})`;
    track.append(line, fill, thumb);
    row.append(top, track);
    const span = c.max - c.min;
    const zero = Math.min(Math.max(0, c.min), c.max);         // bipolar tracks fill from 0
    let current = 0;
    const show = (t) => {
      current = t;
      const x = ((t - c.min) / span) * 100, z = ((zero - c.min) / span) * 100;
      thumb.style.left = x + "%";
      fill.style.left = Math.min(x, z) + "%";
      fill.style.width = Math.abs(x - z) + "%";
      val.textContent = readout(c, t, interp(c.stops, t, 0, 1));
    };
    const trackAt = (ev) => {
      const r = track.getBoundingClientRect();
      const t = c.min + Math.min(1, Math.max(0, (ev.clientX - r.left) / r.width)) * span;
      return Math.round(t * 10) / 10;
    };
    let pending = null, raf = 0;
    const send = () => { raf = 0; if (pending !== null) { notify(`set ${c.field}=${num(interp(c.stops, pending, 0, 1))}`); pending = null; } };
    track.onpointerdown = (ev) => {
      track.setPointerCapture(ev.pointerId);
      dragging = c.field;
      track.classList.add("drag");
      notify("gesture on");                                  // R-PREVIEW-1: render coarse while dragging
      pending = trackAt(ev);
      show(pending);
      raf = raf || requestAnimationFrame(send);
    };
    track.onpointermove = (ev) => {
      if (dragging !== c.field) return;
      pending = trackAt(ev);
      show(pending);
      raf = raf || requestAnimationFrame(send);
    };
    const end = () => {
      if (dragging !== c.field) return;
      dragging = null;
      track.classList.remove("drag");
      if (raf) { cancelAnimationFrame(raf); raf = 0; }
      pending = null;
      cmdQuiet(`set ${c.field}=${num(interp(c.stops, current, 0, 1))}`).finally(() => notify("gesture off"));
    };
    track.onpointerup = end;
    track.onpointercancel = end;
    track.ondblclick = () => {                               // back to neutral
      const e = ENGINE_DEFAULT[c.field] ?? 0;
      cmdQuiet(`set ${c.field}=${num(e)}`);
    };
    sliders.set(c.field, { c, show: (engine) => show(interp(c.stops, engine, 1, 0)) });
    show(Math.min(Math.max(0, c.min), c.max));
    return row;
  }

  function renderSliders(m) {
    if (!controls) return;
    const p = parseParams(m.ownParams || m.params);
    for (const [field, s] of sliders) {
      if (field === dragging) continue;                     // the pointer owns that thumb
      const v = field in p ? p[field] : ENGINE_DEFAULT[field] ?? 0;
      s.show(v);
    }
    $("page-adjust").style.opacity = m.currentSlot >= 0 || m.editGroup >= 0 ? 1 : 0.45;
  }

  // ── white balance pick (R-WB-1): a command, the solve is the service's ─────────────
  function armWb(on, btn) {
    wbArmed = on;
    $("canvas").classList.toggle("picking", on);
    btn.classList.toggle("primary", on);
    if (on) toast("Click a neutral grey or white in the photo");
  }
  $("canvas").addEventListener("click", (ev) => {
    if (!wbArmed) return;
    const img = $(front);
    if (!img.naturalWidth) return;
    const r = img.getBoundingClientRect();
    const s = Math.min(r.width / img.naturalWidth, r.height / img.naturalHeight);
    const w = img.naturalWidth * s, hh = img.naturalHeight * s;
    const x = (ev.clientX - r.left - (r.width - w) / 2) / w, y = (ev.clientY - r.top - (r.height - hh) / 2) / hh;
    if (x < 0 || x > 1 || y < 0 || y > 1) return;
    cmdQuiet(`wb pick --x ${x.toFixed(4)} --y ${y.toFixed(4)}`);
    const btn = document.querySelector("#page-adjust .section-h .btn.primary");
    if (btn) armWb(false, btn);
  });

  // ── tabs ─────────────────────────────────────────────────────────────────────────────
  function selectTab(btn) {
    for (const b of $("tabs").querySelectorAll("button")) b.classList.toggle("on", b === btn);
    for (const p of document.querySelectorAll(".page")) p.classList.toggle("on", p.id === "page-" + btn.dataset.page);
    const acc = $("tab-acc");
    acc.style.left = btn.offsetLeft + "px";
    acc.style.width = btn.offsetWidth + "px";
  }
  for (const b of $("tabs").querySelectorAll("button")) b.onclick = () => selectTab(b);
  requestAnimationFrame(() => selectTab($("tabs").querySelector("button.on")));
  window.addEventListener("resize", () => selectTab($("tabs").querySelector("button.on")));

  // ── events: the service's own log lines (R-SVC-5) ────────────────────────────────────
  const QUIET = new Set(["frame.ready", "load.progress", "entry.progress"]);
  app.onEvent("*", (name, data) => {
    if (name === "command.rejected") { toast(data.text || data.line, true); toastedEvent = true; }
    if (name === "export.finished") toast(`Exported ${data.a} photo${data.a === 1 ? "" : "s"}${data.b ? `, ${data.b} failed` : ""}`, !!data.b);
    if (name === "project.saved") toast("Saved");
    if (QUIET.has(name)) return;
    const box = $("events");
    box.prepend(h("div", null, data.line));
    while (box.childElementCount > 80) box.lastChild.remove();
  });

  // ── dialogs ──────────────────────────────────────────────────────────────────────────
  function dialog(title, body, actions) {
    const scrim = h("div", "scrim"), d = h("div", "dialog");
    const foot = h("div", "foot");
    const close = () => { scrim.classList.remove("on"); d.classList.remove("on"); setTimeout(() => { scrim.remove(); d.remove(); }, 220); };
    for (const [label, cls, fn] of actions) {
      const b = h("button", "btn " + cls, label);
      b.onclick = async () => { if (!fn || (await fn()) !== false) close(); };
      foot.append(b);
    }
    d.append(h("h3", null, title), body, foot);
    scrim.onclick = close;
    document.body.append(scrim, d);
    requestAnimationFrame(() => requestAnimationFrame(() => { scrim.classList.add("on"); d.classList.add("on"); }));
    return close;
  }

  /** mode: "project" (one .cmp) | "images" (several photos) | "folder". Resolves the choice. */
  function pick(mode, title) {
    return new Promise((resolve) => {
      const body = h("div", "body");
      const pathEl = h("div", "picker-path"), list = h("div", "picker");
      body.append(pathEl, list);
      let cur = null, chosen = new Set();
      const load = async (path) => {
        let r;
        try { r = await app.call("browse", path ? { path } : {}); } catch (e) { toast(e.message, true); return; }
        cur = r.path;
        chosen.clear();
        pathEl.textContent = r.path;
        list.replaceChildren();
        if (r.parent) { const up = h("div", "row", "‹ .."); up.onclick = () => load(r.parent); list.append(up); }
        for (const e of r.entries) {
          if (mode === "folder" && e.kind !== "dir") continue;
          if (mode === "project" && e.kind === "image") continue;
          const row = h("div", "row");
          row.append(svg(e.kind === "dir" ? FOLDER : PHOTO), h("span", "nm", e.name));
          row.onclick = () => {
            if (e.kind === "dir") return load(e.path);
            if (mode === "images") { chosen.has(e.path) ? chosen.delete(e.path) : chosen.add(e.path); row.classList.toggle("chosen"); }
            else if (mode === "project") { close(); resolve(e.path); }
          };
          list.append(row);
        }
      };
      const actions = [["Cancel", "ghost", () => resolve(null)]];
      if (mode === "folder") actions.push(["Choose this folder", "primary", () => resolve(cur)]);
      if (mode === "images") actions.push(["Import", "primary", () => { if (!chosen.size) { toast("Select photos first", true); return false; } resolve([...chosen]); }]);
      const close = dialog(title, body, actions);
      load(null);
    });
  }

  async function openProject() { const p = await pick("project", "Open project"); if (p) cmdQuiet("project open " + q(p)); }

  /** A project file name inside `dir`, asked for in a small dialog. */
  function askName(title, dir, initial) {
    return new Promise((resolve) => {
      const name = h("input", "input");
      name.value = initial;
      const body = h("div", "body");
      body.append(h("div", "picker-path", dir), field("File name", name));
      dialog(title, body, [["Cancel", "ghost", () => resolve(null)], ["OK", "primary", () => {
        let n = name.value.trim();
        if (!n || n.includes("/")) { toast("A file name, without folders", true); return false; }
        if (!/\.cmp$/i.test(n)) n += ".cmp";
        resolve(dir.replace(/\/$/, "") + "/" + n);
      }]]);
      setTimeout(() => name.select(), 60);
    });
  }

  /** `import` needs a project (the service says "open or create a project first"): on the home
   *  screen the photos go into a new one, in the editor into the open one. */
  async function importPhotos() {
    const ps = await pick("images", "Import photos");
    if (!ps || !ps.length) return;
    try {
      if (!model || !model.projectPath) {
        const dir = await pick("folder", "Where should the new project live?");
        if (!dir) return;
        const path = await askName("New project", dir, "project.cmp");
        if (!path) return;
        await cmd("project new " + q(path));
      }
      await cmd("import " + ps.map(q).join(" "));
    } catch (e) { /* cmd() already said why */ }
  }

  function field(label, input) { const f = h("div", "field"); f.append(h("label", null, label), input); return f; }

  async function saveProject() {
    if (model.projectPath) return cmdQuiet("project save");
    const dir = await pick("folder", "Save the project in…");
    if (!dir) return;
    const path = await askName("Save project", dir, "project.cmp");
    if (path) { try { cmdQuiet("project save " + q(path)); } catch (e) { toast(e.message, true); } }
  }

  async function exportDialog() {
    let dir = null;
    try { dir = (await app.call("browse", {})).path + "/cosmo-export"; } catch (e) { dir = ""; }
    const body = h("div", "body");
    const dirIn = h("input", "input");
    dirIn.value = dir;
    const browse = h("button", "btn", "Browse…");
    browse.onclick = async () => { const d = await pick("folder", "Export to…"); if (d) dirIn.value = d; };
    const line = h("div", "line");
    line.append(dirIn, browse);
    const fmt = h("select", "input");
    for (const f of ["jpg", "png", "tiff"]) fmt.append(new Option(f.toUpperCase(), f));
    const quality = h("input", "input");
    quality.type = "number"; quality.min = 1; quality.max = 100; quality.value = 90;
    const edge = h("input", "input");
    edge.type = "number"; edge.min = 0; edge.placeholder = "full size"; edge.value = "";
    const f1 = field("Folder", line);
    body.append(f1, field("Format", fmt), field("Quality (JPEG)", quality), field("Long edge (px)", edge));
    dialog(`Export ${model.imageCount} photo${model.imageCount === 1 ? "" : "s"}`, body, [["Cancel", "ghost"], ["Export", "primary", () => {
      if (!dirIn.value.trim()) { toast("Choose a folder", true); return false; }
      try {
        cmdQuiet(`export --outdir ${q(dirIn.value.trim())} --format ${fmt.value} --quality ${Math.max(1, Math.min(100, +quality.value || 90))}` +
                 (+edge.value > 0 ? ` --long-edge ${Math.round(+edge.value)}` : ""));
      } catch (e) { toast(e.message, true); return false; }
    }]]);
  }

  function settingsDialog() {
    const m = model;
    const body = h("div", "body");
    const cpu = h("input");
    cpu.type = "range"; cpu.min = 10; cpu.max = 100; cpu.step = 5; cpu.value = m.settingsCpuPercent;
    const cpuVal = h("span", "mono", cpu.value + "%");
    cpu.oninput = () => { cpuVal.textContent = cpu.value + "%"; };
    const cpuLine = h("div", "line");
    cpuLine.append(cpu, cpuVal);
    const edge = h("select", "input");
    for (const e of [1024, 1600, 2048, 2560, 3200]) edge.append(new Option(e + " px", e, false, e === m.settingsPreviewEdge));
    const gpu = h("input");
    gpu.type = "checkbox"; gpu.checked = m.settingsUseGpu; gpu.disabled = !m.gpuAvailable;
    const gpuLine = h("div", "line");
    gpuLine.append(gpu, h("span", null, m.gpuAvailable ? "Edit on the GPU" : "No GPU backend on this machine"));
    body.append(field("CPU budget", cpuLine), field("Preview size", edge), field("GPU", gpuLine));
    dialog("Settings", body, [["Cancel", "ghost"], ["Apply", "primary", () =>
      cmdQuiet(`settings set cpuPercent=${cpu.value} previewEdge=${edge.value} useGpu=${gpu.checked ? 1 : 0}`)]]);
  }

  // ── wiring ───────────────────────────────────────────────────────────────────────────
  $("act-open").onclick = openProject;
  $("act-import").onclick = importPhotos;
  $("act-settings").onclick = settingsDialog;
  $("settings").onclick = settingsDialog;
  $("add-photos").onclick = importPhotos;
  $("go-home").onclick = () => cmdQuiet("screen home");
  $("undo").onclick = $("undo2").onclick = () => cmdQuiet("undo");
  $("redo").onclick = $("redo2").onclick = () => cmdQuiet("redo");
  $("save").onclick = saveProject;
  $("export").onclick = exportDialog;
  $("new-group").onclick = () => cmdQuiet("group new");
  $("read-meta").onclick = () => cmdQuiet("metadata");
  $("rail-toggle").onclick = () => { $("rail").classList.toggle("collapsed"); setTimeout(placeRing, 220); };
  if (window.matchMedia("(max-width: 899px)").matches) $("rail").classList.add("collapsed");
  $("bypass").onclick = () => {
    const sel = model && model.nodes.find((n) => n.node === model.selectedNode);
    if (sel) cmdQuiet(`bypass ${sel.node} ${sel.bypass ? "off" : "on"}`);
  };
  document.addEventListener("keydown", (e) => {
    if (e.target.tagName === "INPUT" || e.target.tagName === "SELECT" || !model || model.screen !== "editor") return;
    const mod = e.ctrlKey || e.metaKey;
    if (mod && e.key.toLowerCase() === "z") { e.preventDefault(); cmdQuiet(e.shiftKey ? "redo" : "undo"); }
    else if (mod && e.key.toLowerCase() === "y") { e.preventDefault(); cmdQuiet("redo"); }
    else if (mod && e.key.toLowerCase() === "s") { e.preventDefault(); saveProject(); }
    else if (e.key === "ArrowRight") cmdQuiet("select next");
    else if (e.key === "ArrowLeft") cmdQuiet("select prev");
  });

  app.onStatus((s, detail) => {
    if (s === "running" || s === "starting" || s === "connecting") return;
    toast(`cosmo is ${s}${detail ? ": " + detail : ""}`, s === "failed");
  });
  app.onReady(async () => {
    try {
      controls = await app.call("controls", {});
      buildSliders();
      app.call("frame", {}).catch(() => {});
    } catch (e) { toast(e.message, true); }
  });
  app.onState("model", render);
})();
