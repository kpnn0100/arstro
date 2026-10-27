/*
 * Interstellar — deliver/output.js: the Deliver tab's right column, what to render, as what, where.
 * Ports app/widgets/OutputSpec.{h,cpp} and App.cpp's preset / burn-in / captions / Save Preset wiring.
 *
 * TIMELINE a radio list of every version (indented by depth, "editing" tagged, 6 rows then it scrolls),
 * a dangling note under it; FORMAT the codec (H.264 · H.265 · ProRes · DNxHR · PNG · DCP · IMF), then the
 * rows that codec has (Profile, Quality, Speed, Depth, the PNG / DCP / IMF notes) which COLLAPSE eased
 * (height a, opacity a²; motion-cross-fade), then Colour (a DCP's cross-fades to "DCI X'Y'Z' 12-bit");
 * the Preset pill and Save Preset… ride the FORMAT header; SIZE & RATE Full · ½ · ¼ (a DCP's Container
 * cross-faded in its place) and the rate stepper; RANGE Whole · In–Out, the marks row, the captions pill
 * on the header, the audio sentence; OUTPUT the burn-ins pill on the header, the path, Render, and the
 * two-line summary. The line Render sends is OutputSpec::renderLine, defaults left out.
 *
 * Segmented controls are a Painter port of cosmo's SegmentedControl (tray r2, pad 2, gap 2, ONE primary
 * highlight sliding 220 ms EaseOutCubic, labels white when active, lerp(muted, white, 0.55 hv) idle).
 */
import { own } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { useCss } from "../kit/css.js";
import { tok } from "../kit/tokens.js";
import { q, seconds, timecode } from "../kit/cmd.js";
import { width as measure, ellipsize, baseline, fontEpoch } from "../kit/textfit.js";
import { EasedScroll, wheelPx } from "../kit/easedscroll.js";
import { glyph } from "../kit/glyphs.js";
import { Painter, canvasSvg, col, css, fade, lerp, brighten, whiteA, primaryA, hoverWash, Anim, HoverFade, frameLoop, localPoint, inRect, Ease, F } from "../kit/paint.js";

const PAD_X = 9.75, NOTE_H = 20, SEG_H = 24, GAP = 6.5, LABEL_W = 58.5, FIELD_H = 26, BUTTON_H = 30, LINE_H = 19.5;   // OutputSpec.cpp:17-24
const ROW_H = 24.375, MAX_ROWS = 6, SECTION_H = 27.95;                                                              // OutputSpec.h, SectionHeader.h
const ENC = new TextEncoder();
const estW = (s, px) => ENC.encode(s).length * px * 0.6;
const CODECS = ["h264", "h265", "prores", "dnxhr", "png-seq", "dcp", "imf"];
const CONTAINERS = ["2k-flat", "2k-scope", "4k-flat", "4k-scope"], CONTAINER_LABELS = ["2K Flat", "2K Scope", "4K Flat", "4K Scope"];
const CONTAINER_W = [1998, 2048, 3996, 4096], CONTAINER_H = [1080, 858, 2160, 1716];
const PRORES = ["proxy", "lt", "standard", "hq", "4444"], DNX = ["lb", "sq", "hq", "hqx", "444"], CRF = [28, 23, 18, 12], SPEEDS = ["fast", "medium", "slow"];
const OUTPUTS = ["rec709", "rec709-2.4", "srgb", "p3d65", "pq", "hlg"], OUTPUT_LABELS = ["Rec.709", "Rec.709 2.4", "sRGB", "P3-D65", "HDR PQ", "HDR HLG"];
const RATES = [["Project", "", 0], ["23.976", "24000/1001", 24000 / 1001], ["24", "24", 24], ["25", "25", 25], ["29.97", "30000/1001", 30000 / 1001],
  ["30", "30", 30], ["50", "50", 50], ["59.94", "60000/1001", 60000 / 1001], ["60", "60", 60]];
const R = { ProresProfile: 0, DnxProfile: 1, Quality: 2, Speed: 3, Depth: 4, PngNote: 5, InOut: 6, Container: 7, DcpNote: 8, ImfNote: 9 };
const N_ROWS = 10;
const BURN_ITEMS = ["tc@bl", "srctc@br", "clip@tl", "source@tr", ""];
const BURN_NAMES = ["Timecode, bottom left", "Source timecode, bottom right", "Clip name, top left", "Source name, top right", "Text…, top centre"];
const CAP_WAYS = ["burn", "track", "sidecar"], CAP_NAMES = ["Burn into the picture", "Subtitle track (MP4, MOV, MKV)", "Sidecar .srt beside the file"];
const CAP_ONE = ["Captions: burn", "Captions: track", "Captions: .srt"];
const rateText = (fps) => { let r = fps.toFixed(3); r = r.replace(/0+$/, ""); return r.replace(/\.$/, ""); };

export function mount(host, vm, ctx) {
  useCss("css/deliver/output.css");
  host.classList.add("os");
  const svg = canvasSvg();
  host.append(svg);
  const pathEl = document.createElement("input");
  pathEl.className = "os-path";
  pathEl.placeholder = "output path";
  pathEl.spellcheck = false;
  host.append(pathEl);
  const p = new Painter(svg);
  const loop = frameLoop(step);
  own(host, () => loop.kill());

  // ── state (OutputSpec.h members) ──
  let W = 0, H = 0, timelines = [], current = "", timeline = "", projectDir = "", projW = 0, projH = 0, projFps = 24, duration = 0, playhead = 0;
  let presets = [], inT = 0, outT = -1, rateIndex = 0, preset = "", presetSnap = [], lastDefault = "", containerChosen = false;
  const burnOn = [false, false, false, false, false], capOn = [false, false, false];
  let burnText = "", fixtureApplied = false, contentH = 0, now0 = 0;
  const seg = {};
  function mkSeg(name, labels, sel, onChange) { seg[name] = { labels, sel, pos: new Anim(sel), act: labels.map((_, i) => new Anim(i === sel ? 1 : 0)), onChange }; }
  function setSel(name, i, immediate = false) {
    const s = seg[name];
    if (i < 0 || i >= s.labels.length) return;
    const changed = i !== s.sel;
    s.sel = i;
    if (immediate) { s.pos.set(i); s.act.forEach((a, k) => a.set(k === i ? 1 : 0)); }
    else {
      const t = performance.now();
      s.act.forEach((a, k) => a.animateTo(k === i ? 1 : 0, 120, Ease.EaseOutCubic, t));
      if (changed) s.pos.animateTo(i, 220, Ease.EaseOutCubic, t);
    }
    if (s.onChange) s.onChange(i);
    loop.kick();
  }
  const format = () => CODECS[Math.max(0, Math.min(6, seg.codec.sel))];
  mkSeg("codec", ["H.264", "H.265", "ProRes", "DNxHR", "PNG", "DCP", "IMF"], 0, (i) => {
    if (seg.colour.sel >= 4 && (i === 0 || i === 4)) setSel("colour", 0);   // an 8-bit codec cannot carry HDR
    refreshDefaultPath();
  });
  mkSeg("prores", ["Proxy", "LT", "422", "HQ", "4444"], 2);
  mkSeg("dnx", ["LB", "SQ", "HQ", "HQX", "444"], 2, (i) => { if (i < 3 && seg.colour.sel >= 4) setSel("colour", 0); });
  mkSeg("quality", ["Draft", "Good", "High", "Master"], 2);
  mkSeg("speed", ["Fast", "Medium", "Slow"], 1);
  mkSeg("depth", ["8-bit", "10-bit"], 0, (i) => { if (i === 0 && seg.colour.sel >= 4) setSel("colour", 0); });
  mkSeg("colour", ["709", "2.4", "sRGB", "P3", "PQ", "HLG"], 0, (i) => {
    if (i < 4) return;   // HDR needs 10 bits: H.265 10-bit unless the codec already is a 10-bit one
    const f = format();
    if (f === "h264" || f === "png-seq") setSel("codec", 1);
    if (format() === "h265") setSel("depth", 1);
    if (format() === "dnxhr" && seg.dnx.sel < 3) setSel("dnx", 3);
  });
  mkSeg("size", ["Full", "½", "¼"], 0);
  mkSeg("container", CONTAINER_LABELS.slice(), 0, () => { containerChosen = true; });
  mkSeg("range", ["Whole", "In – Out"], 0);

  const rows = Array.from({ length: N_ROWS }, () => ({ amt: new Anim(0), applied: false, init: false }));
  const presetAmt = new Anim(0), capAvail = new Anim(0), noteAmt = new Anim(0);
  let presetApplied = false, capInit = false, capApplied = false, noteInit = false, noteApplied = false, noteCount = 0;
  const hover = new HoverFade(), sel = new HoverFade();
  const listScroll = new EasedScroll(() => loop.kick()), colScroll = new EasedScroll(() => loop.kick());

  // ── the line (OutputSpec::renderLine and friends) ──
  function outputSize() {
    let w = projW, h = projH;
    if (format() === "dcp" && w > 0 && h > 0) {
      const k = Math.max(0, Math.min(3, seg.container.sel)), s = Math.min(1, Math.min(CONTAINER_W[k] / w, CONTAINER_H[k] / h));
      return [Math.min(CONTAINER_W[k], Math.round(projW * s)), Math.min(CONTAINER_H[k], Math.round(projH * s))];
    }
    const div = seg.size.sel === 1 ? 2 : seg.size.sel === 2 ? 4 : 1;
    if (div === 1 || w <= 0 || h <= 0) return [w, h];
    const longEdge = Math.max(w, h), edge = Math.floor(longEdge / div), s = edge / longEdge;
    return [Math.max(1, Math.round(projW * s)), Math.max(1, Math.round(projH * s))];
  }
  const outputFps = () => (rateIndex === 0 ? projFps : RATES[rateIndex][2]);
  const rateLabel = () => (rateIndex === 0 ? "Project · " + rateText(projFps) + " fps" : RATES[rateIndex][0] + " fps");
  const controlState = () => [seg.codec.sel, seg.prores.sel, seg.dnx.sel, seg.quality.sel, seg.speed.sel, seg.depth.sel, seg.colour.sel, seg.size.sel, rateIndex].join(",");
  function setPreset(name) { preset = name; presetSnap = controlState(); loop.kick(); }
  const tlOf = (id) => timelines.find((t) => t.id === id) || null;
  const timelineCaptions = () => { const t = tlOf(timeline); return t ? t.captions | 0 : 0; };
  function burnFlag() {
    const parts = [];
    for (let b = 0; b < 5; b++) {
      if (!burnOn[b]) continue;
      if (b === 4 && (!burnText || /[,@"]/.test(burnText))) continue;   // the grammar's own separators
      parts.push(b === 4 ? "text=" + burnText + "@tc" : BURN_ITEMS[b]);
    }
    return parts.length ? " --burnin " + q(parts.join(",")) : "";
  }
  function captionsFlag() {
    if (timelineCaptions() === 0) return "";
    const ways = CAP_WAYS.filter((_, k) => capOn[k] && !(k === 1 && format() === "png-seq"));
    return ways.length ? " --captions " + ways.join(",") : "";
  }
  const rangeArg = () => { const fps = projFps > 0 ? projFps : 24; return " --range " + seconds(inT, fps) + ":" + seconds(outT >= 0 ? outT : duration, fps); };
  function renderLine() {
    const f = format(), path = pathEl.value;
    if (preset) return "render --timeline " + q(timeline) + " --out " + q(path) + " --preset " + q(preset) + burnFlag() + captionsFlag() + (seg.range.sel === 1 ? rangeArg() : "");
    let line = "render --timeline " + q(timeline) + " --out " + q(path) + " --format " + f;
    if (f === "prores" && seg.prores.sel !== 2) line += " --profile " + PRORES[seg.prores.sel];
    if (f === "dnxhr" && seg.dnx.sel !== 2) line += " --profile " + DNX[seg.dnx.sel];
    if (f === "h264" || f === "h265") {
      if (seg.quality.sel !== 2) line += " --quality " + CRF[seg.quality.sel];
      if (seg.speed.sel !== 1) line += " --speed " + SPEEDS[seg.speed.sel];
    }
    if (f === "h265" && seg.depth.sel === 1) line += " --bits 10";
    if (f !== "dcp" && seg.colour.sel !== 0) line += " --output " + OUTPUTS[seg.colour.sel];
    if (f === "dcp") line += " --container " + CONTAINERS[seg.container.sel];
    else if (seg.size.sel !== 0) { const [w, h] = outputSize(); line += " --res " + w + "x" + h; }
    if (rateIndex !== 0) line += " --fps " + RATES[rateIndex][1];
    if (seg.range.sel === 1) line += rangeArg();
    return line + burnFlag() + captionsFlag();
  }
  function specFlags() {
    const line = renderLine(), at = line.indexOf(" --format ");
    let flags = at < 0 ? "" : line.slice(at + 1);
    for (const o of [" --range ", " --burnin ", " --captions "]) { const k = flags.indexOf(o); if (k >= 0) flags = flags.slice(0, k); }
    return flags;
  }
  function audioSentence() {
    const t = tlOf(timeline), f = format();
    if (!t || !t.hasSound) return "No sound on this timeline: the render is picture only";
    if (f === "png-seq") return "A PNG sequence carries no sound: render a video for the mix";
    if (f === "dcp") return "Sound: the master mix on L/R of 5.1, 24-bit PCM 48 kHz";
    if (f === "imf") return "Sound: the master mix, 24-bit PCM 48 kHz stereo";
    return "Sound: the master mix, " + (f === "prores" || f === "dnxhr" ? "24-bit PCM" : "AAC") + " 48 kHz stereo";
  }
  function summaryLines() {
    const [w, h] = outputSize(), fps = outputFps(), ranged = seg.range.sel === 1;
    const a = ranged ? inT : 0, b = ranged && outT >= 0 ? outT : duration;
    const frames = Math.max(0, Math.round(b * fps) - Math.round(a * fps));
    let l2 = ranged ? timecode(a, projFps) + "–" + timecode(b, projFps) : "whole timeline";
    let tail = " · " + frames + " frames";
    if (format() === "dcp") tail += " · in " + CONTAINER_LABELS[seg.container.sel] + " · X'Y'Z'";
    else if (seg.colour.sel !== 0) tail += " · " + OUTPUT_LABELS[seg.colour.sel];
    // native: "W×H · R fps · <span> · N frames[ · …]" split at the LAST " · ": l1 = frame + last part, l2 = the middle
    const all = w + "×" + h + " · " + rateText(fps) + " fps · " + l2 + tail;
    const cut = all.lastIndexOf(" · "), rate = all.indexOf(" fps · ");
    if (rate >= 0 && cut > rate) return [all.slice(0, rate + 4) + all.slice(cut), all.slice(rate + 7, cut)];
    return [all, ""];
  }
  function defaultPath() {
    if (!timeline) return "";
    const dir = projectDir ? projectDir + "/renders" : "renders", f = format();
    if (f === "png-seq") return dir + "/" + timeline + "-png/";
    if (f === "dcp" || f === "imf") return dir + "/" + timeline + (f === "dcp" ? "_DCP" : "_IMF");
    if (f === "prores" || f === "dnxhr") return dir + "/" + timeline + (f === "dnxhr" ? "-dnxhr" : "") + ".mov";
    return dir + "/" + timeline + (f === "h265" ? "-hevc" : "") + ".mp4";
  }
  function refreshDefaultPath() {
    const d = defaultPath();
    if (!pathEl.value || pathEl.value === lastDefault) pathEl.value = d;
    lastDefault = d;
  }
  function rowWanted(r) {
    const f = format();
    switch (r) {
      case R.ProresProfile: return f === "prores";
      case R.DnxProfile: return f === "dnxhr";
      case R.Quality: case R.Speed: return f === "h264" || f === "h265";
      case R.Depth: return f === "h265";
      case R.PngNote: return f === "png-seq";
      case R.Container: case R.DcpNote: return f === "dcp";
      case R.ImfNote: return f === "imf";
      case R.InOut: return seg.range.sel === 1;
      default: return false;
    }
  }

  // ── the fixture's view state: {format, flags} (renderShots.cpp) ──
  function applyFixture(d) {
    const i = CODECS.indexOf(d.format);
    if (i >= 0) setSel("codec", i, true);
    const fl = String(d.flags || ""), arg = (k) => { const m = new RegExp("--" + k + " (\\S+)").exec(fl); return m ? m[1] : null; };
    let v;
    if ((v = arg("profile")) !== null) { if (PRORES.includes(v)) setSel("prores", PRORES.indexOf(v), true); if (DNX.includes(v)) setSel("dnx", DNX.indexOf(v), true); }
    if ((v = arg("quality")) !== null && CRF.includes(+v)) setSel("quality", CRF.indexOf(+v), true);
    if ((v = arg("speed")) !== null && SPEEDS.includes(v)) setSel("speed", SPEEDS.indexOf(v), true);
    if (arg("bits") === "10") setSel("depth", 1, true);
    if ((v = arg("output")) !== null && OUTPUTS.includes(v)) setSel("colour", OUTPUTS.indexOf(v), true);
    if ((v = arg("container")) !== null && CONTAINERS.includes(v)) { setSel("container", CONTAINERS.indexOf(v), true); containerChosen = true; }
    if ((v = arg("res")) !== null) { const w = +v.split("x")[0]; setSel("size", w * 4 <= Math.max(projW, projH) + 2 ? 2 : w * 2 <= Math.max(projW, projH) + 2 ? 1 : 0, true); }
    if ((v = arg("fps")) !== null) { const k = RATES.findIndex((r) => r[1] === v); if (k > 0) rateIndex = k; }
    refreshDefaultPath();
  }

  own(host, effect(() => {                           // OutputSpec::bind
    const m = vm.model.value;
    if (!m) return;
    timelines = m.timelines || [];
    current = m.currentTimeline || "";
    projW = m.width | 0; projH = m.height | 0;
    projFps = m.fps > 0 ? m.fps : 24;
    presets = m.renderPresets || [];
    if (!containerChosen && projW > 0 && projH > 0) {
      const k = (projW >= 3996 || projH >= 2160 ? 2 : 0) + (projW / projH >= 2 ? 1 : 0);
      if (seg.container.sel !== k) { setSel("container", k, true); containerChosen = false; }
    }
    duration = m.duration || 0;
    playhead = m.playhead || 0;
    const path = m.projectPath || "", slash = path.lastIndexOf("/");
    projectDir = slash < 0 ? "" : path.slice(0, slash);
    if (!timelines.some((t) => t.id === timeline)) timeline = current;
    sel.setHovered(timelines.findIndex((t) => t.id === timeline));
    refreshDefaultPath();
    const fx = vm.view && vm.view.fixture ? vm.view.fixture.value : null;
    if (fx && fx.deliver && !fixtureApplied) { fixtureApplied = true; applyFixture(fx.deliver); }
    loop.kick();
  }));
  own(host, effect(() => { const b = ctx.box.value; W = b.w; H = b.h; fontEpoch.value; loop.kick(); }));

  // ── layout: one top-to-bottom pass from the LIVE row amounts (OutputSpec::layout) ──
  let L = {}, hits = [];
  function layout() {
    const w = W, cw = Math.max(0, w - 2 * PAD_X - LABEL_W), dim = 1 - 0.55 * presetAmt.value;
    let y = -colScroll.value;
    hits = [];
    const segs = [];
    const placeSeg = (name, x, yy, ww, a) => { segs.push({ name, x, y: yy, w: Math.max(0, ww), a }); if (a > 0.001) hits.push({ kind: "seg", name, r: { x, y: yy, w: ww, h: SEG_H } }); };
    const pills = [];
    const placePill = (id, label, x, yy, ww, hh, a) => { pills.push({ id, label, r: { x, y: yy, w: ww, h: hh }, a }); if (a > 0.001) hits.push({ kind: "pill", id, r: { x, y: yy, w: ww, h: hh } }); };
    L = { segs, pills, rowY: [] };
    L.hdrTimeline = y; y += SECTION_H;
    L.listY = y; L.listH = Math.min(timelines.length, MAX_ROWS) * ROW_H; y += L.listH;
    L.noteY = y; y += NOTE_H;
    L.hdrFormat = y;
    {
      const ph = Math.min(SEG_H, SECTION_H - 4), py = y + (SECTION_H - ph) * 0.5, saveW = 92, presetW = Math.max(90, Math.min(190, w - 2 * PAD_X - saveW - GAP - 64));
      placePill("save", "Save Preset…", w - PAD_X - saveW, py, saveW, ph, preset ? 0.45 : 1);
      placePill("preset", "Preset: " + (preset || "Custom"), w - PAD_X - saveW - GAP - presetW, py, presetW, ph, 1);
    }
    y += SECTION_H;
    placeSeg("codec", PAD_X, y, w - 2 * PAD_X, dim); y += SEG_H + GAP;
    const optional = (r, name) => { const a = rows[r].amt.value; L.rowY[r] = y; if (name) placeSeg(name, PAD_X + LABEL_W, y, cw, a * a * dim); y += (SEG_H + GAP) * a; };
    optional(R.ProresProfile, "prores"); optional(R.DnxProfile, "dnx"); optional(R.Quality, "quality"); optional(R.Speed, "speed"); optional(R.Depth, "depth");
    for (const r of [R.PngNote, R.DcpNote, R.ImfNote]) { const a = rows[r].amt.value; L.rowY[r] = y; y += (LINE_H + GAP) * a; }
    const pa = rows[R.Container].amt.value, keep = (1 - pa) * (1 - pa);
    L.colourY = y; placeSeg("colour", PAD_X + LABEL_W, y, cw, dim * keep); y += SEG_H + GAP;
    L.hdrSize = y; y += SECTION_H;
    L.sizeY = y; placeSeg("size", PAD_X + LABEL_W, y, cw, dim * keep); placeSeg("container", PAD_X + LABEL_W, y, cw, dim * pa * pa); y += SEG_H + GAP;
    L.rateY = y; y += SEG_H + GAP;
    if (true) { hits.push({ kind: "step", dir: -1, r: { x: PAD_X + LABEL_W, y: L.rateY, w: 26, h: SEG_H } }); hits.push({ kind: "step", dir: 1, r: { x: w - PAD_X - 26, y: L.rateY, w: 26, h: SEG_H } }); }
    L.hdrRange = y;
    {
      const n = capOn.filter(Boolean).length, only = capOn.lastIndexOf(true);
      const label = timelineCaptions() === 0 ? "No captions" : n === 0 ? "Captions: off" : n === 1 ? CAP_ONE[only] : "Captions: " + n + " ways";
      const ph = Math.min(SEG_H, SECTION_H - 4);
      placePill("cap", label, w - PAD_X - 100, y + (SECTION_H - ph) * 0.5, 100, ph, 0.45 + 0.55 * capAvail.value);
    }
    y += SECTION_H;
    L.rangeY = y; placeSeg("range", PAD_X + LABEL_W, y, cw, 1); y += SEG_H + GAP;
    {
      const a = rows[R.InOut].amt.value, bw = Math.max(0, (cw - GAP) * 0.5);
      L.rowY[R.InOut] = y;
      placePill("in", "In  " + timecode(inT, projFps), PAD_X + LABEL_W, y, bw, SEG_H, a);
      placePill("out", "Out  " + timecode(outT >= 0 ? outT : duration, projFps), PAD_X + LABEL_W + bw + GAP, y, bw, SEG_H, a);
      y += (SEG_H + GAP) * a;
    }
    L.audioY = y; y += LINE_H + GAP;
    L.hdrOutput = y;
    {
      const n = burnOn.filter((on, b) => on && (b !== 4 || burnText)).length;
      const ph = Math.min(SEG_H, SECTION_H - 4);
      placePill("burn", n === 0 ? "No burn-ins" : n + (n === 1 ? " burn-in" : " burn-ins"), w - PAD_X - 92, y + (SECTION_H - ph) * 0.5, 92, ph, 1);
    }
    y += SECTION_H;
    L.pathR = { x: PAD_X, y, w: Math.max(0, w - 2 * PAD_X), h: FIELD_H }; y += FIELD_H + GAP * 2;
    L.renderR = { x: PAD_X, y, w: Math.max(0, w - 2 * PAD_X), h: BUTTON_H }; hits.push({ kind: "render", r: L.renderR }); y += BUTTON_H + GAP;
    L.summaryY = y; y += 2 * LINE_H * 0.75 + GAP;
    contentH = y + colScroll.value;
    for (let i = 0; i < timelines.length; i++) {
      const r = { x: 0, y: L.listY + i * ROW_H - listScroll.value, w, h: ROW_H };
      if (r.y + r.h > Math.max(0, L.listY) && r.y < L.listY + L.listH) hits.push({ kind: "tl", i, r });
    }
    // the path field rides the layout (a real input), culled when scrolled out
    const pr = L.pathR, vis = pr.y + pr.h > 0 && pr.y < H;
    pathEl.style.display = vis ? "" : "none";
    Object.assign(pathEl.style, { left: pr.x + "px", top: pr.y + "px", width: pr.w + "px", height: pr.h + "px" });
  }

  function advance(now) {
    let moving = false;
    if (preset && controlState() !== presetSnap) setPreset("");   // a control touched: Custom again
    const xf = tok.ms("motion-cross-fade");
    if (!preset === presetApplied) { presetAmt.animateTo(preset ? 1 : 0, xf, Ease.EaseOutCubic, now); presetApplied = !!preset; }
    const ca = timelineCaptions() > 0;
    if (!capInit) { capAvail.set(ca ? 1 : 0); capApplied = ca; capInit = true; }
    else if (ca !== capApplied) { capAvail.animateTo(ca ? 1 : 0, xf, Ease.EaseOutCubic, now); capApplied = ca; }
    for (let r = 0; r < N_ROWS; r++) {
      const row = rows[r], want = rowWanted(r);
      if (!row.init) { row.amt.set(want ? 1 : 0); row.applied = want; row.init = true; }
      else if (want !== row.applied) { row.amt.animateTo(want ? 1 : 0, xf, Ease.EaseOutCubic, now); row.applied = want; }
      moving = row.amt.update(now) || moving;
    }
    const t = tlOf(timeline), dangling = t ? t.danglingDeltas | 0 : 0, nw = dangling > 0;
    if (nw) noteCount = dangling;
    if (!noteInit) { noteAmt.set(nw ? 1 : 0); noteApplied = nw; noteInit = true; }
    if (nw !== noteApplied) { noteAmt.animateTo(nw ? 1 : 0, xf, Ease.EaseOutCubic, now); noteApplied = nw; }
    moving = presetAmt.update(now) || moving;
    moving = capAvail.update(now) || moving;
    moving = noteAmt.update(now) || moving;
    for (const k in seg) { moving = seg[k].pos.update(now) || moving; for (const a of seg[k].act) moving = a.update(now) || moving; }
    moving = hover.advance(now) || moving;
    moving = sel.advance(now, tok.ms("motion-select")) || moving;
    layout();
    listScroll.setExtent(L.listY, L.listH, timelines.length * ROW_H);
    colScroll.setExtent(0, H, contentH);
    return moving || listScroll.moving || colScroll.moving;
  }

  // ── paint (OutputSpec::onPaint) ──
  function sectionHeader(x, y, w, label, trailW = 0) {           // cosmo SectionHeader.h drawSectionHeader
    const base = y + 11.375 + 9 * 0.85;
    p.text(label, x, base, 9, F.semibold, col("muted-foreground"), 0.13 * 9);
    const lineY = base - 9 * 0.35, labelW = estW(label, 9) + 9 * label.length * 0.13, rs = x + labelW + 6.5, re = x + w - (trailW ? trailW + 6.5 : 0);
    if (re > rs) p.line(rs, lineY, re, lineY, col("border"), 1);
  }
  function corners(x, y, w, h, tl, tr, br, bl) {
    const k = 0.5522847498307936, c = (r) => Math.max(0, Math.min(r, w / 2, h / 2));
    tl = c(tl); tr = c(tr); br = c(br); bl = c(bl);
    return `M${x + tl} ${y}L${x + w - tr} ${y}C${x + w - tr + tr * k} ${y} ${x + w} ${y + tr - tr * k} ${x + w} ${y + tr}` +
      `L${x + w} ${y + h - br}C${x + w} ${y + h - br + br * k} ${x + w - br + br * k} ${y + h} ${x + w - br} ${y + h}` +
      `L${x + bl} ${y + h}C${x + bl - bl * k} ${y + h} ${x} ${y + h - bl + bl * k} ${x} ${y + h - bl}` +
      `L${x} ${y + tl}C${x} ${y + tl - tl * k} ${x + tl - tl * k} ${y} ${x + tl} ${y}Z`;
  }
  function paintSeg(sg) {                          // cosmo SegmentedControl
    if (sg.a <= 0.001 || sg.y + SEG_H < 0 || sg.y > H) return;
    const s = seg[sg.name], n = s.labels.length, pad = 2, gap = 2, segW = (sg.w - 2 * pad - gap * (n - 1)) / n, ih = SEG_H - 2 * pad;
    p.group(sg.a);
    p.rect(sg.x, sg.y, sg.w, SEG_H, 2, { fill: col("segmented-bg"), stroke: col("border"), sw: 1 });
    const pos = Math.min(n - 1, Math.max(0, s.pos.value)), a = Math.floor(pos), b = Math.min(n - 1, a + 1), f = pos - a;
    const radii = (i) => { const first = i <= 0, last = i >= n - 1; return [first ? 2 : 1, last ? 2 : 1, last ? 2 : 1, first ? 2 : 1]; };
    const ra = radii(a), rb = radii(b), m = (i) => ra[i] + (rb[i] - ra[i]) * f;
    if (segW > 0) p.path(corners(sg.x + pad + pos * (segW + gap), sg.y + pad, segW, ih, m(0), m(1), m(2), m(3)), { fill: col("primary") });
    s.labels.forEach((label, i) => {
      const hv = hover.amount("seg:" + sg.name + ":" + i), idle = lerp(col("muted-foreground"), col("white"), 0.55 * hv);
      const c = lerp(idle, col("white"), s.act[i].value);
      p.text(label, sg.x + pad + i * (segW + gap) + (segW - estW(label, 10)) * 0.5, sg.y + pad + ih * 0.5 + 10 * 0.35, 10, F.sans, c);
    });
    p.restore();
  }
  function paintPill(pl) {                          // cosmo PillButton, styled by OutputSpec.cpp:95-103
    if (pl.a <= 0.001 || pl.r.y + pl.r.h < 0 || pl.r.y > H) return;
    const hv = hover.amount("pill:" + pl.id), r = pl.r;
    p.group(pl.a);
    p.rect(r.x, r.y, r.w, r.h, tok.px("radius-control"), { fill: brighten(col("secondary"), 0.14 * hv), stroke: lerp(col("border"), col("white"), 0.5 * hv), sw: 1 });
    const label = ellipsize(pl.label, r.w - 8, 10, F.sans), tw = measure(label, 10, F.sans);
    p.text(label, r.x + (r.w - tw) * 0.5, baseline(r.y + r.h * 0.5, 10), 10, F.sans, lerp(col("foreground"), col("white"), 0.55 * hv));
    p.restore();
  }
  function paint() {
    const fg = col("foreground"), muted = col("muted-foreground"), border = col("border"), primary = col("primary"), w = W;
    p.begin();
    p.rect(0, 0, W, H, 0, { fill: col("card") });
    p.line(0.5, 0, 0.5, H, border, 1);
    sectionHeader(PAD_X, L.hdrTimeline, w - 2 * PAD_X, "TIMELINE");
    // the version list, a radio per row
    p.clip(0, Math.max(0, L.listY), w, Math.max(0, L.listY + L.listH - Math.max(0, L.listY)));
    timelines.forEach((tl, i) => {
      if (!listScroll.bandVisible(i * ROW_H, ROW_H)) return;
      const r = { x: 0, y: L.listY + i * ROW_H - listScroll.value, w, h: ROW_H }, s = sel.amount(i), hv = hover.amount("tl:" + i);
      if (s > 0.001) p.rect(r.x + 4, r.y + 1, r.w - 8, r.h - 2, tok.px("radius-control"), { fill: primaryA(0.14 * s) });
      if (hv > 0.001) p.rect(r.x + 4, r.y + 1, r.w - 8, r.h - 2, tok.px("radius-control"), { fill: hoverWash(hv) });
      const cy = r.y + r.h * 0.5, rx = PAD_X + 6 + (tl.depth | 0) * (tok.px("space-indent") || 13);
      p.circle(rx, cy, 5, { stroke: lerp(muted, primary, s), sw: 1, fill: "none" });
      if (s > 0.001) p.circle(rx, cy, 2.5 * s, { fill: primary });
      let x = rx + 12;
      if (tl.colourPinned || tl.cutFrozen) { p.add(glyph.lock({ x, y: cy - 5, w: 8, h: 10 }, css(muted))); x += 12; }
      const tag = tl.id === current ? "editing" : "", tagW = tag ? measure(tag, 9, F.sans) + 8 : 0;
      p.text(ellipsize(tl.name || tl.id, w - PAD_X - tagW - x, 11, F.sans), x, baseline(cy, 11), 11, F.sans, fg);
      if (tag) p.text(tag, w - PAD_X - tagW + 8, baseline(cy, 9), 9, F.sans, muted);
    });
    p.restore();
    const lb = listScroll.bar(w - 2);
    if (lb) p.rect(lb.x, lb.y, lb.w, lb.h, tok.px("radius-pill"), { fill: whiteA(0.14) });
    if (!timelines.length) p.text("No versions to render.", PAD_X, L.listY + 14, 11, F.sans, muted);
    const na = noteAmt.value;
    if (na > 0.001) {
      const cy = L.noteY + NOTE_H * 0.5, dc = fade(col("destructive"), na);
      p.add(glyph.warn({ x: PAD_X, y: cy - 5, w: 10, h: 10 }, css(dc)));
      p.text(ellipsize(noteCount + " dangling — rebase before delivering?", w - 2 * PAD_X - 16, 9.5, F.sans), PAD_X + 16, baseline(cy, 9.5), 9.5, F.sans, dc);
    }
    const label = (s, y, a) => { if (a > 0.001) p.text(s, PAD_X, baseline(y + SEG_H * 0.5, 10), 10, F.sans, fade(muted, a)); };
    const sq = (a) => a * a, ra = (r) => rows[r].amt.value;
    sectionHeader(PAD_X, L.hdrFormat, w - 2 * PAD_X, "FORMAT", w - 2 * PAD_X - (L.pills.find((x) => x.id === "preset").r.x - PAD_X));
    label("Profile", L.rowY[R.ProresProfile], sq(ra(R.ProresProfile)));
    label("Profile", L.rowY[R.DnxProfile], sq(ra(R.DnxProfile)));
    label("Quality", L.rowY[R.Quality], sq(ra(R.Quality)));
    label("Speed", L.rowY[R.Speed], sq(ra(R.Speed)));
    label("Depth", L.rowY[R.Depth], sq(ra(R.Depth)));
    label("Colour", L.colourY, 1);
    const note = (r, s) => { const a = ra(r); if (a > 0.001) p.text(ellipsize(s, w - 2 * PAD_X, 10, F.sans), PAD_X, baseline(L.rowY[r] + LINE_H * 0.5, 10), 10, F.sans, fade(muted, a)); };
    note(R.PngNote, "8-bit RGBA, one lossless PNG per frame");
    note(R.DcpNote, "Not validated here — check it before a cinema");
    note(R.ImfNote, "Not validated here — check it with Photon");
    const pa = ra(R.Container);
    if (pa > 0.001) p.text("DCI X'Y'Z' 12-bit", PAD_X + LABEL_W + 8, baseline(L.colourY + SEG_H * 0.5, 10), 10, F.sans, fade(fg, pa * pa));
    sectionHeader(PAD_X, L.hdrSize, w - 2 * PAD_X, "SIZE & RATE");
    label("Size", L.sizeY, (1 - pa) * (1 - pa));
    label("Container", L.sizeY, pa * pa);
    label("Rate", L.rateY, 1);
    {
      const r = { x: PAD_X + LABEL_W, y: L.rateY, w: Math.max(0, w - 2 * PAD_X - LABEL_W), h: SEG_H };
      p.rect(r.x, r.y, r.w, r.h, tok.px("radius-control"), { fill: col("segmented-bg"), stroke: border, sw: 1 });
      [-1, 1].forEach((dir, k) => {
        const b = dir < 0 ? { x: r.x, y: r.y, w: 26, h: SEG_H } : { x: w - PAD_X - 26, y: r.y, w: 26, h: SEG_H };
        const live = dir < 0 ? rateIndex > 0 : rateIndex < RATES.length - 1, hv = hover.amount("step:" + dir);
        if (hv > 0.001 && live) p.rect(b.x + 2, b.y + 2, b.w - 4, b.h - 4, tok.px("radius-hairline"), { fill: hoverWash(hv) });
        const gc = live ? lerp(muted, fg, hv) : fade(muted, 0.35), gr = { x: b.x + 7, y: b.y + 6, w: 12, h: 12 };
        p.add(k === 0 ? glyph.chevronLeft(gr, css(gc)) : glyph.chevronRightSmall(gr, css(gc)));
      });
      const v = rateLabel();
      p.text(v, r.x + (r.w - measure(v, 10, F.mono)) * 0.5, baseline(r.y + r.h * 0.5, 10), 10, F.mono, fg);
    }
    sectionHeader(PAD_X, L.hdrRange, w - 2 * PAD_X, "RANGE", 100);
    label("Range", L.rangeY, 1);
    if (ra(R.InOut) > 0.001) label("Marks", L.rowY[R.InOut], ra(R.InOut));
    {
      const cy = L.audioY + LINE_H * 0.5;
      p.add(glyph.speaker({ x: PAD_X, y: cy - 5, w: 10, h: 10 }, css(fade(muted, 0.7)), 1));
      p.text(ellipsize(audioSentence(), w - 2 * PAD_X - 16, 9.5, F.sans), PAD_X + 16, baseline(cy, 9.5), 9.5, F.sans, muted);
    }
    sectionHeader(PAD_X, L.hdrOutput, w - 2 * PAD_X, "OUTPUT", 92);
    for (const sg of L.segs) paintSeg(sg);
    for (const pl of L.pills) paintPill(pl);
    {
      // Render: primary, 12 SemiBold primary-foreground; dimmed while it has nothing to render
      const r = L.renderR, live = !!timeline && !!pathEl.value, hv = hover.amount("render");
      p.group(live ? 1 : 0.45);
      p.rect(r.x, r.y, r.w, r.h, tok.px("radius-control"), { fill: brighten(primary, 0.14 * hv) });
      const tw = measure("Render", 12, F.semibold);
      p.text("Render", r.x + (r.w - tw) * 0.5, baseline(r.y + r.h * 0.5, 12), 12, F.semibold, col("primary-foreground"));
      p.restore();
    }
    {
      const [l1, l2] = summaryLines();
      p.text(ellipsize(l1, w - 2 * PAD_X, 9.5, F.mono), PAD_X, baseline(L.summaryY + LINE_H * 0.375, 9.5), 9.5, F.mono, muted);
      if (l2) p.text(ellipsize(l2, w - 2 * PAD_X, 9.5, F.mono), PAD_X, baseline(L.summaryY + LINE_H * 1.125, 9.5), 9.5, F.mono, muted);
    }
    const cb = colScroll.bar(w - 2);
    if (cb) p.rect(cb.x, cb.y, cb.w, cb.h, tok.px("radius-pill"), { fill: whiteA(0.14) });
    p.end();
  }

  function step(now) {
    if (!(W > 0) || !(H > 0)) return false;
    now0 = now;
    const more = advance(now);
    paint();
    return more;
  }

  // ── input ──
  function hitAt(pt) {
    for (let k = hits.length - 1; k >= 0; k--) if (inRect(pt, hits[k].r)) return hits[k];
    return null;
  }
  function hoverId(hit, pt) {
    if (!hit) return -1;
    if (hit.kind === "tl") return "tl:" + hit.i;
    if (hit.kind === "pill") return "pill:" + hit.id;
    if (hit.kind === "step") return "step:" + hit.dir;
    if (hit.kind === "render") return "render";
    const s = seg[hit.name], n = s.labels.length, segW = (hit.r.w - 4 - 2 * (n - 1)) / n, i = Math.max(0, Math.min(n - 1, Math.floor((pt.x - hit.r.x - 2) / (segW + 2))));
    return "seg:" + hit.name + ":" + i;
  }
  const worldRect = (r) => { const b = ctx.box.value; return { x: b.x + r.x, y: b.y + r.y, w: r.w, h: r.h }; };
  const mark = (on) => (on ? "•  " : "     ");
  function openMenu(items, r) { const wr = worldRect(r); ctx.menu.open(items, wr.x, wr.y + wr.h); }
  function pillClick(id, r) {
    if (id === "preset") {                          // App.cpp onPresetMenu
      const items = [{ label: mark(!preset) + "Custom", action: () => setPreset("") }];
      for (const pr of presets) { const n = typeof pr === "string" ? pr : pr.name; items.push({ label: mark(preset === n) + n, action: () => setPreset(n) }); }
      openMenu(items, r);
    } else if (id === "save") {                     // App.cpp onSavePreset
      const flags = specFlags();
      Promise.resolve(ctx.prompt({ title: "Save render preset", message: "These output settings, by name — in every project.", value: "", ok: "Save" }))
        .then((name) => { if (typeof name === "string" && name.trim()) vm.cmd("render preset save " + q(name.trim()) + " " + flags); });
    } else if (id === "burn") {                     // App.cpp onBurnMenu
      const items = BURN_NAMES.map((nm, b) => {
        const on = burnOn[b] && (b !== 4 || !!burnText);
        if (b === 4) return { label: on ? "•  Text: " + burnText : mark(false) + nm, action: () => askBurnText() };
        return { label: mark(on) + nm, action: () => { burnOn[b] = !on; loop.kick(); } };
      });
      openMenu(items, r);
    } else if (id === "cap") {                      // App.cpp onCaptionsMenu
      const items = timelineCaptions() === 0
        ? [{ label: "No captions on this timeline — File › Import Captions…", action: () => {} }]
        : CAP_NAMES.map((nm, k) => ({ label: mark(capOn[k]) + nm, action: () => { capOn[k] = !capOn[k]; loop.kick(); } }));
      openMenu(items, r);
    } else if (id === "in") { inT = playheadNow(); if (outT >= 0 && outT <= inT) outT = -1; loop.kick(); }
    else if (id === "out") { outT = playheadNow(); if (outT <= inT) inT = 0; loop.kick(); }
  }
  function askBurnText() {
    Promise.resolve(ctx.prompt({ title: "Burn in text", message: "Words over every frame of this render, top centre (empty: none).", value: burnText, ok: "Burn in" }))
      .then((t) => { if (typeof t === "string") { burnText = t; burnOn[4] = !!t; loop.kick(); } });
  }
  const playheadNow = () => { const v = vm.playhead && vm.playhead.peek ? vm.playhead.peek() : playhead; return typeof v === "number" ? v : playhead; };

  host.addEventListener("pointermove", (e) => {
    const pt = localPoint(host, e);
    hover.setHovered(hoverId(hitAt(pt), pt));
    loop.kick();
  });
  host.addEventListener("pointerleave", () => { hover.clear(); loop.kick(); });
  host.addEventListener("click", (e) => {
    if (e.target === pathEl) return;
    const pt = localPoint(host, e), hit = hitAt(pt);
    if (!hit) return;
    if (hit.kind === "tl") {
      const tl = timelines[hit.i];
      if (tl && tl.id !== timeline) { timeline = tl.id; sel.setHovered(hit.i); refreshDefaultPath(); }
    } else if (hit.kind === "step") rateIndex = Math.max(0, Math.min(RATES.length - 1, rateIndex + hit.dir));
    else if (hit.kind === "seg") { const id = hoverId(hit, pt); setSel(hit.name, +id.split(":")[2]); }
    else if (hit.kind === "pill") pillClick(hit.id, hit.r);
    else if (hit.kind === "render") { if (timeline && pathEl.value) vm.cmd(renderLine()); }
    loop.kick();
  });
  pathEl.addEventListener("input", () => loop.kick());
  pathEl.addEventListener("keydown", (e) => e.stopPropagation());
  host.addEventListener("wheel", (e) => {
    const pt = localPoint(host, e), d = wheelPx(e);
    const inList = pt.y >= L.listY && pt.y < L.listY + L.listH;
    if ((inList && listScroll.scrollBy(d)) || colScroll.scrollBy(d)) { e.preventDefault(); loop.kick(); }
  }, { passive: false });
  loop.kick();
}
