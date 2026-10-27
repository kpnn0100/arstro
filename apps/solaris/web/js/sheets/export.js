/*
 * Solaris — sheets/export.js: ExportSheet (app/widgets/ExportSheet.{h,cpp}) — File › Export…, ONE `render` line (R-EXPORT-1).
 *
 * Cosmo's modal look: the scrim, a 520-wide card (60 header · 8 rows of 46 · 12 · 56 footer), faded in 150 ms /
 * out 120 ms EaseOutCubic (motion::kModalOpenMs / kModalCloseMs) as ONE group. Rows: Format, Sample rate, Bit depth,
 * Quality, Loudness, Ceiling (combos 220×28 — cosmo's ComboBox), Stems, With the video (toggles). A row that does not
 * apply greys to 0.35, eased 200 ms EaseOutCubic (motion::kSelectMs), and takes no input. Export… closes the sheet,
 * asks ctx.pick to name the file (the format's extension) and sends `render --out <file> <flags>` (App.cpp:115-116).
 * Cancel, the scrim, Escape (an open list first) close it.
 * NOT PORTED: the a11y nodes (R-ACCESS-1); the combos' label cross-fade (SettingCombo).
 */
import { effect } from "../core/signal.js";
import { own } from "../core/dom.js";
import { comboBox, toggleSwitch } from "../ui/controls.js";
import { useCss } from "../kit/css.js";

const K = { FORMAT: 0, RATE: 1, BITS: 2, QUALITY: 3, LOUDNESS: 4, CEILING: 5, STEMS: 6, VIDEO: 7, ROWS: 8 };
const CARD_W = 520, PAD = 24, ROW_H = 46, COMBO_W = 220, CONTROL_H = 28, HEADER_H = 60, FOOTER_H = 56, DIMMED = 0.35;
const LABELS = ["Format", "Sample rate", "Bit depth", "Quality", "Loudness", "Ceiling", "Stems", "With the video"];
const NOTES = ["the file's kind — its extension follows", "the song rendered at it: no resampler", "16 and 24 are dithered (TPDF)",
  "how much a lossy file keeps", "the whole mix gained to an integrated loudness", "… held so its true peak stays under this",
  "a file per strip beside the mix, gained alike", "the song's picture and the mix in one file"];
const lossy = (f) => f === "mp3" || f === "ogg" || f === "opus";
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };

export function mount(host, vm, ctx) {
  useCss("sheets/export.css");
  const tr = (s) => vm.tr(s);
  const shown = (s) => vm.shown(s);
  const m = vm.model.peek() || {};
  const name = m.projectName || "";
  const songRate = m.sampleRate || 48000;
  const hasVideo = !!(m.video && m.video.id);

  // ---------------------------------------------------------------- the choices (ExportSheet::ExportSheet)
  const audioFormats = [{ label: "WAV", value: "wav" }, { label: "FLAC", value: "flac" }, { label: "MP3", value: "mp3" },
    { label: "Ogg Vorbis", value: "ogg" }, { label: "Opus", value: "opus" }];
  const opts = [];
  opts[K.FORMAT] = audioFormats;
  opts[K.RATE] = [{ label: vm.say("The song's — {1} kHz", String(songRate / 1000)), value: "" }, { label: "44.1 kHz", value: "44100" },
    { label: "48 kHz", value: "48000" }, { label: "88.2 kHz", value: "88200" }, { label: "96 kHz", value: "96000" }];
  opts[K.BITS] = [{ label: vm.say("16-bit"), value: "16" }, { label: vm.say("24-bit"), value: "24" }, { label: vm.say("32-bit float"), value: "32f" }];
  opts[K.QUALITY] = [{ label: "—", value: "" }];
  opts[K.LOUDNESS] = [{ label: vm.say("Off"), value: "" },
    { label: "−23 LUFS — " + tr("broadcast"), value: "-23" }, { label: "−16 LUFS — " + tr("podcasts"), value: "-16" },
    { label: "−14 LUFS — " + tr("streaming"), value: "-14" }, { label: "−9 LUFS — " + tr("loud"), value: "-9" }];
  opts[K.CEILING] = [{ label: "−0.3 dBTP", value: "-0.3" }, { label: "−1 dBTP", value: "-1" }, { label: "−2 dBTP", value: "-2" }];
  const sel = [0, 0, 1, 0, 0, 1];
  let stems = false, video = false, audioFormat = "wav";

  // ---------------------------------------------------------------- the DOM: scrim, card, header, rows, footer
  const root = el("div", "sx-sheet sx-export");
  const scrim = el("div", "sx-scrim");
  const card = el("div", "sx-card");
  const title = el("div", "sx-title");
  const file = el("div", "sx-sub sx-mono");
  const body = el("div", "sx-body");
  const foot = el("div", "sx-foot");
  const cancel = el("button", "sx-btn sx-secondary");
  const exportBtn = el("button", "sx-btn sx-primary sx-export-go");
  foot.append(cancel, exportBtn);
  card.append(title, file, body, foot);
  root.append(scrim, card);
  host.append(root);

  const rows = [];
  const combos = [];
  let toggles = [];
  for (let r = 0; r < K.ROWS; r++) {
    const row = el("div", "sx-row");
    const label = el("div", "sx-label"), note = el("div", "sx-note");
    const ctl = el("div", "sx-ctl");
    row.append(label, note, ctl);
    body.append(row);
    rows.push({ row, label, note, ctl, on: null });
    if (r === K.STEMS || r === K.VIDEO) {
      const t = toggleSwitch({ onChange: (on) => { if (r === K.STEMS) stems = on; else { video = on; videoChanged(); } refresh(); } });
      ctl.classList.add("sx-toggle");
      ctl.append(t.el);
      toggles[r] = t;
      continue;
    }
    const cb = comboBox({ w: COMBO_W, h: CONTROL_H, host: root, onChange: (i) => { sel[r] = i; if (r === K.FORMAT) formatChanged(); refresh(); } });
    ctl.append(cb.el);
    combos[r] = cb;
  }

  const value = (r) => {
    if (r === K.STEMS) return stems ? "on" : "";
    if (r === K.VIDEO) return withVideo() ? "on" : "";
    const o = opts[r][sel[r]];
    return o ? o.value : "";
  };
  const withVideo = () => hasVideo && video;
  function setOptions(r, list, select) {
    opts[r] = list;
    sel[r] = Math.max(0, list.findIndex((o) => o.value === select));
    combos[r].setOptions(list.map((o) => shown(o.label)), sel[r]);
  }
  function formatChanged() {
    const f = value(K.FORMAT);
    if (f === "opus") setOptions(K.QUALITY, [{ label: "96 kbps", value: "96" }, { label: "128 kbps", value: "128" }, { label: "192 kbps", value: "192" }, { label: "256 kbps", value: "256" }], "192");
    else if (lossy(f)) setOptions(K.QUALITY, [{ label: "128 kbps", value: "128" }, { label: "192 kbps", value: "192" }, { label: "256 kbps", value: "256" }, { label: "320 kbps", value: "320" }], f === "mp3" ? "320" : "256");
    else setOptions(K.QUALITY, [{ label: "—", value: "" }], "");
    if (f === "flac" && value(K.BITS) === "32f") setOptions(K.BITS, opts[K.BITS], "24");              // FLAC: 16 or 24
    if (f === "mp3" && parseInt(value(K.RATE), 10) > 48000) setOptions(K.RATE, opts[K.RATE], "48000"); // MP3: to 48 kHz
  }
  function videoChanged() {
    if (withVideo()) {
      const f = value(K.FORMAT);
      if (f !== "mp4" && f !== "mkv") audioFormat = f;
      setOptions(K.FORMAT, [{ label: "MP4", value: "mp4" }, { label: "MKV", value: "mkv" }], f === "mkv" ? "mkv" : "mp4");
    } else if (value(K.FORMAT) === "mp4" || value(K.FORMAT) === "mkv") setOptions(K.FORMAT, audioFormats, audioFormat);
    formatChanged();
  }
  function applies(r) {
    const f = value(K.FORMAT), v = withVideo();
    switch (r) {
      case K.RATE: return f !== "opus";
      case K.BITS: return !lossy(f) && !v;
      case K.QUALITY: return lossy(f) && !v;
      case K.CEILING: return value(K.LOUDNESS) !== "";
      case K.STEMS: return !v;
      case K.VIDEO: return hasVideo;
      default: return true;
    }
  }
  const extension = () => "." + value(K.FORMAT);
  function flags() {
    const f = value(K.FORMAT);
    let s = withVideo() ? "--video" : "--format " + f;
    if (applies(K.RATE) && value(K.RATE)) s += " --rate " + value(K.RATE);
    if (applies(K.BITS)) s += " --bits " + value(K.BITS);
    if (applies(K.QUALITY)) s += " --kbps " + value(K.QUALITY);
    if (value(K.LOUDNESS)) s += " --normalize " + value(K.LOUDNESS) + " --ceiling " + value(K.CEILING);
    if (applies(K.STEMS) && stems) s += " --stems all";
    return s;
  }
  const fileName = () => (name || tr("Untitled")) + extension();

  // the rows that apply, eased (CSS: opacity over --motion-select, EaseOutCubic); the first placement does not travel
  let placed = false;
  function refresh() {
    for (let r = 0; r < K.ROWS; r++) {
      const on = applies(r), x = rows[r];
      if (x.on === on) continue;
      x.on = on;
      x.row.classList.toggle("sx-off", !on);
    }
    file.textContent = fileName();
    if (!placed) { placed = true; requestAnimationFrame(() => root.classList.add("sx-placed")); }
  }
  for (const r of [K.FORMAT, K.RATE, K.BITS, K.LOUDNESS, K.CEILING]) setOptions(r, opts[r], r === K.BITS ? "24" : r === K.CEILING ? "-1" : "");
  formatChanged();

  // ---------------------------------------------------------------- words (redrawn when the language changes)
  own(host, effect(() => {
    vm.lang.value;
    title.textContent = tr("Export");
    for (let r = 0; r < K.ROWS; r++) { rows[r].label.textContent = tr(LABELS[r]); rows[r].note.textContent = tr(NOTES[r]); }
    for (const r of [K.FORMAT, K.RATE, K.BITS, K.QUALITY, K.LOUDNESS, K.CEILING]) combos[r].setOptions(opts[r].map((o) => shown(o.label)), sel[r]);
    cancel.textContent = tr("Cancel");
    exportBtn.textContent = tr("Export…");
    file.textContent = fileName();
  }));
  refresh();

  // ---------------------------------------------------------------- geometry (ExportSheet::cardRect) from the slot's box
  own(host, effect(() => {
    const b = ctx.box.value;
    const w = Math.min(CARD_W, b.w - 32), hh = Math.min(HEADER_H + K.ROWS * ROW_H + 12 + FOOTER_H, b.h - 48);
    Object.assign(card.style, { left: Math.floor((b.w - w) * 0.5) + "px", top: Math.floor((b.h - hh) * 0.5) + "px", width: w + "px", height: hh + "px" });
    const roomH = hh - HEADER_H - FOOTER_H;
    rows.forEach((x, r) => { x.row.style.visibility = (r + 1) * ROW_H <= roomH ? "" : "hidden"; }); // a short window: rows under the footer hidden
  }));

  // ---------------------------------------------------------------- open / close (150 in, 120 out, EaseOutCubic)
  requestAnimationFrame(() => root.classList.add("sx-in"));
  own(host, effect(() => {
    if (ctx.open.value) return;
    closeCombos();
    root.classList.remove("sx-in");
    root.classList.add("sx-out");
  }));
  function closeCombos() { for (const c of combos) if (c) c.close(); }
  const comboOpen = () => [...root.querySelectorAll(".cp-combo-pop")].some((p) => p.style.display !== "none" && p.style.opacity !== "0");
  const close = () => { closeCombos(); vm.closeSheet(); };

  scrim.addEventListener("click", close);  // the scrim closes, as cosmo's
  cancel.addEventListener("click", close);
  exportBtn.addEventListener("click", () => {
    const f = flags(), suggested = fileName();
    close();
    Promise.resolve(ctx.pick({ mode: "save", title: tr("Export the mix"), ok: tr("Export"), name: suggested })).then((p) => {
      if (p) vm.cmd("render --out " + vm.q(p) + " " + f);
    });
  });
  const onKey = (e) => {
    if (e.key !== "Escape" || !ctx.open.peek()) return;
    if (comboOpen()) closeCombos(); else close();             // Escape: the list first
    e.preventDefault();
    e.stopPropagation();
  };
  window.addEventListener("keydown", onKey, true);
  own(host, () => window.removeEventListener("keydown", onKey, true));
}
