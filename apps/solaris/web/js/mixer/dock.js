/*
 * Solaris — mixer/dock.js: MixerDock (app/widgets/MixerDock.{h,cpp}) on one <canvas>, painted per frame as the native.
 *
 * PORTED: the tab bar (a tab per mixer, "+", Matrix; the highlight slides, select ms EaseOutCubic; the chevron folds
 * the dock: vm.toggleDock; the top edge resizes it: vm.dockResize), the page of strip cards (Geo exactly — geoOf,
 * armRow), each card's colour bar, name, sub line, rack (Generator / Input + Effects chips: a chip opens the device —
 * vm.openDevice), sends (two, "+N in the matrix"), pan, fader + L/R meters (peak, RMS core, held line, clip light —
 * paintFader exactly), dB readout, M / S / R, "→ out"; the master pinned right (its rack, Mono, fader, LUFS chip, out).
 * Eased as the native eases: fader / pan catch up (catchUp ms), mute / solo / arm / dim (select ms), colour cross-fade
 * (crossFade ms), meters rise in 40 ms and fall in meterFall ms. A dragged fader / pan / send follows the pointer.
 * COMMANDS (exactly MixerDock.cpp's): `set <ch>.gain=<dB .1>` · `set project.masterGain=<dB>` · `set <ch>.pan=<.2>` ·
 * `set <sd>.gain=<dB>` (vm.note while dragging, vm.cmd on release) · `set <ch>.mute|solo=true|false` ·
 * `strip arm <ch>|none` · `meters clear` · `transport mono on|off` · `mixer add`.
 * NOT PORTED (yet): the Matrix page (its tab shows an empty page), VCA cards, freeze pill, sidechain key labels, bus
 * group headers / folding, the context menus (route, send, + Effect, + Line, ParamMenu), bound-control tags, Show IDs,
 * chip drag-reorder, card grow / shrink, hover washes, a11y nodes.
 */
import { effect, untracked } from "../core/signal.js";
import { own } from "../core/dom.js";
import { Anim, Ease, frames, motion } from "../kit/anim.js";
import { canvasIn, color, colorsVersion, rgba, whiteAlpha, track, lerpColor, fillRR, strokeRR, line, text } from "../kit/draw.js";
import { FONT, measure, ellipsize, baseline } from "../kit/text.js";
import { useCss } from "../kit/css.js";
import { faderPos, faderDb, meterLevel, num, dbText, rnd } from "./law.js";

const kTabsH = 29.25, kStripW = 84.5, kMasterW = 97.5, kMinBodyH = 370.5, kAddLineW = 78.0;
const kFxSlots = 3, kTitleH = 9.75, kChipStep = 22.75, kChipH = 19.5, kSendH = 16.25, kResizeH = 4.875;
const kMeterAttackMs = 40.0, PADX = 9.75;
const R_HAIR = 1, R_CTRL = 2, R_PILL = 9999;

const fade = (c, a) => [c[0], c[1], c[2], c[3] * a];
const C = (n) => color(n);

function geoOf(x, w, by, bh) {
  const g = { titles: [], fx: [], sends: [] };
  g.titles[0] = { x: x + 6.5, y: by + 38.0, w: w - 13.0, h: kTitleH };
  g.gen = { x: x + 6.5, y: by + 49.5, w: w - 13.0, h: kChipH };
  g.titles[1] = { x: x + 6.5, y: by + 73.0, w: w - 13.0, h: kTitleH };
  for (let k = 0; k < kFxSlots; ++k) g.fx[k] = { x: x + 6.5, y: by + 84.5 + k * kChipStep, w: w - 13.0, h: kChipH };
  for (let k = 0; k < 2; ++k) g.sends[k] = { x: x + 6.5, y: by + 154.0 + k * kSendH, w: w - 13.0, h: kSendH };
  g.pan = { x: x + 9.75, y: by + 192.0, w: w - 19.5, h: 13.0 };
  g.zone = { x: x + 6.5, y: by + 214.0, w: w - 13.0, h: Math.max(40.0, bh - 214.0 - 78.0) };
  const zb = g.zone.y + g.zone.h;
  g.readoutY = zb + 14.0;
  const bw = (w - 13.0 - 3.25) * 0.5;
  g.mute = { x: x + 6.5, y: zb + 22.0, w: bw, h: 19.5 };
  g.solo = { x: g.mute.x + bw + 3.25, y: g.mute.y, w: bw, h: 19.5 };
  g.out = { x: x + 6.5, y: zb + 45.5, w: w - 13.0, h: 19.5 };
  g.clip = { x: g.zone.x + g.zone.w * 0.62, y: g.zone.y + 0.5, w: 8.0, h: 3.25 };
  g.arm = null;
  return g;
}
function armRow(g) {
  const x = g.mute.x, all = g.solo.x + g.solo.w - x, bw = (all - 6.5) / 3.0;
  g.mute.w = bw;
  g.solo = { x: x + bw + 3.25, y: g.mute.y, w: bw, h: g.mute.h };
  g.arm = { x: x + 2.0 * (bw + 3.25), y: g.mute.y, w: bw, h: g.mute.h };
}
function rackOf(d) {
  const r = { gen: d.length > 0 && !!d[0].instrument };
  r.effects = d.length - (r.gen ? 1 : 0);
  r.more = r.effects + 1 > kFxSlots;
  r.shown = r.more ? kFxSlots : r.effects + 1;
  return r;
}
const armable = (s) => s && (s.kind === "audio" || s.kind === "instrument");
const inR = (r, p) => r && p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;

export function mount(host, vm, ctx) {
  useCss("mixer/dock.css");
  host.classList.add("sl-dock");
  const cv = canvasIn(host, ctx);
  const M = motion();
  const lives = new Map();
  let model = null, mixers = [], tab = 0, nowMs = performance.now();
  let lufsLast = -144, monoInit = false, monoLast = false;
  const lufs = new Anim(-70), lufsOn = new Anim(0), mono = new Anim(0), hiX = new Anim(0), hiW = new Anim(0), pageA = new Anim(1);
  let hiInit = false, hiXLast = 0, hiWLast = 0, scrollX = 0, scrollY = 0;
  let hits = [];
  let press = null, dragging = false, lastSent = "";

  const stripOf = (id) => (model && model.strips ? model.strips.find((s) => s.id === id) : null);
  const onMatrix = () => tab === mixers.length;
  const W = () => cv.w || ctx.box.peek().w, H = () => cv.h || ctx.box.peek().h;
  const stripsRight = () => Math.max(0, W() - kMasterW);
  const bodyH = () => Math.max(H() - kTabsH, kMinBodyH);
  const bodyTop = () => kTabsH - scrollY;
  const labelOf = (t) => {
    if (!t || t === "master") return vm.tr("Master");
    const s = stripOf(t);
    if (s) return s.name;
    for (const p of (model && model.ports) || []) if (p.id === t) return vm.fillIn(vm.tr("{1} port"), p.name);
    return t;
  };

  function liveFor(id) {
    let l = lives.get(id);
    if (!l) {
      l = { fader: new Anim(0), pan: new Anim(0), mute: new Anim(0), solo: new Anim(0), dim: new Anim(0), colT: new Anim(1), arm: new Anim(0),
            meter: [new Anim(0), new Anim(0)], rms: [new Anim(0), new Anim(0)], hold: [new Anim(0), new Anim(0)], clip: new Anim(0),
            faderLast: 0, panLast: 0, meterLast: [0, 0], rmsLast: [0, 0], holdLast: [0, 0], clipLast: false,
            muteLast: false, soloLast: false, dimLast: false, armLast: false, placed: false, colFrom: null, colTo: null, colLast: -2 };
      lives.set(id, l);
    }
    return l;
  }
  const all = (l) => [l.fader, l.pan, l.mute, l.solo, l.dim, l.colT, l.arm, l.clip, ...l.meter, ...l.rms, ...l.hold];

  // MixerDock::advance's `drive`: each value the model sets, eased
  function drive(id, gain, pan, mute, solo, audible, peak, colour, rms, hold, clip, armed) {
    const l = liveFor(id);
    const draggingFader = dragging && press && press.part === "fader" && press.id === id;
    const draggingPan = dragging && press && press.part === "pan" && press.id === id;
    const f = faderPos(gain);
    const col = colour >= 0 ? track(colour) : C("secondary-foreground");
    if (!l.placed) {
      l.fader.set(f); l.faderLast = f; l.pan.set(pan); l.panLast = pan;
      l.mute.set(mute ? 1 : 0); l.muteLast = mute; l.solo.set(solo ? 1 : 0); l.soloLast = solo;
      l.arm.set(armed ? 1 : 0); l.armLast = armed; l.dim.set(audible ? 0 : 1); l.dimLast = !audible;
      l.colFrom = l.colTo = col; l.colT.set(1); l.colLast = colour; l.placed = true;
    }
    if (!draggingFader && f !== l.faderLast) { l.fader.animateTo(f, M.catchUp, Ease.EaseOutCubic, nowMs); l.faderLast = f; }
    if (!draggingPan && pan !== l.panLast) { l.pan.animateTo(pan, M.catchUp, Ease.EaseOutCubic, nowMs); l.panLast = pan; }
    if (mute !== l.muteLast) { l.mute.animateTo(mute ? 1 : 0, M.select, Ease.EaseOutCubic, nowMs); l.muteLast = mute; }
    if (solo !== l.soloLast) { l.solo.animateTo(solo ? 1 : 0, M.select, Ease.EaseOutCubic, nowMs); l.soloLast = solo; }
    if (armed !== l.armLast) { l.arm.animateTo(armed ? 1 : 0, M.select, Ease.EaseOutCubic, nowMs); l.armLast = armed; }
    if (audible === l.dimLast) { l.dim.animateTo(audible ? 0 : 1, M.select, Ease.EaseOutCubic, nowMs); l.dimLast = !audible; }
    if (colour !== l.colLast) {
      l.colFrom = lerpColor(l.colFrom, l.colTo, l.colT.value); l.colTo = col;
      l.colT.set(0); l.colT.animateTo(1, M.crossFade, Ease.EaseOutCubic, nowMs); l.colLast = colour;
    }
    for (let c = 0; c < 2; ++c) {
      const m = meterLevel(peak ? peak[c] : 0), r = meterLevel(rms ? rms[c] : 0), hd = meterLevel(hold ? hold[c] : 0);
      if (m !== l.meterLast[c]) { l.meter[c].animateTo(m, m > l.meter[c].value ? kMeterAttackMs : M.meterFall, Ease.EaseOutCubic, nowMs); l.meterLast[c] = m; }
      if (r !== l.rmsLast[c]) { l.rms[c].animateTo(r, r > l.rms[c].value ? kMeterAttackMs : M.meterFall, Ease.EaseOutCubic, nowMs); l.rmsLast[c] = r; }
      if (hd !== l.holdLast[c]) { l.hold[c].animateTo(hd, hd > l.hold[c].value ? kMeterAttackMs : M.meterFall, Ease.EaseOutCubic, nowMs); l.holdLast[c] = hd; }
    }
    if (!!clip !== l.clipLast) { l.clip.animateTo(clip ? 1 : 0, clip ? kMeterAttackMs : M.select, Ease.EaseOutCubic, nowMs); l.clipLast = !!clip; }
  }

  function bind(m) {
    model = m;
    if (!m) return;
    mixers = m.mixers || [];
    if (tab > mixers.length) tab = mixers.length;
    const armedId = m.armed || "";
    for (const s of m.strips || [])
      drive(s.id, s.gain ?? 0, s.pan ?? 0, !!s.mute, !!s.solo, s.audible !== false, s.peak, s.colour ?? -1, s.rms, s.hold, s.clip, armedId === s.id);
    const tr = m.transport || {};
    drive("master", m.masterGain ?? 0, 0, false, false, true, tr.masterPeak, -1, tr.masterRms, tr.masterHold, tr.masterClip, false);
    const lu = m.loudness && Number.isFinite(m.loudness.integrated) ? m.loudness.integrated : -144;
    if (lu !== lufsLast) {
      const on = lu > -143;
      if (on) lufs.animateTo(Math.max(-70, lu), M.catchUp, Ease.EaseOutCubic, nowMs);
      lufsOn.animateTo(on ? 1 : 0, M.crossFade, Ease.EaseOutCubic, nowMs);
      lufsLast = lu;
    }
    if (!monoInit) { mono.set(tr.mono ? 1 : 0); monoLast = !!tr.mono; monoInit = true; }
    if (!!tr.mono !== monoLast) { mono.animateTo(tr.mono ? 1 : 0, M.select, Ease.EaseOutCubic, nowMs); monoLast = !!tr.mono; }
  }

  // ── tabs ──────────────────────────────────────────────────────────────────────────────────
  function tabsLayout() {
    const out = [];
    let x = PADX;
    for (let t = 0; t < mixers.length; ++t) {
      const label = mixers[t].name;
      const w = measure(label, 10, FONT.medium) + 2 * PADX;
      out.push({ kind: "tab", t, label, x, w });
      x += w + 3.25;
    }
    out.push({ kind: "add", x, w: kTabsH - 8 });
    x += kTabsH - 8 + 13.0;
    const ml = vm.tr("Matrix");
    out.push({ kind: "tab", t: mixers.length, label: ml, x, w: measure(ml, 10, FONT.medium) + 2 * PADX });
    return out;
  }
  function setTab(t) {
    if (t === tab) return;
    tab = t; scrollX = 0;
    pageA.set(0); pageA.animateTo(1, M.crossFade, Ease.EaseOutCubic, performance.now());
    frames(step);
  }

  // ── paint ─────────────────────────────────────────────────────────────────────────────────
  function paintFader(g, z, l, a, meters = true) {
    const trackX = z.x + z.w * 0.36, top = z.y + 6.0, bottom = z.y + z.h - 6.0, span = bottom - top;
    for (const db of [6, 0, -6, -12, -24, -48]) {
      const y = Math.round(bottom - faderPos(db) * span) + 0.5;
      line(g, trackX - 9, y, trackX - 4, y, fade(whiteAlpha(db === 0 ? 0.3 : 0.12), a), 1);
    }
    fillRR(g, trackX - 2, top, 4, span, R_PILL, fade(C("input"), a));
    const y12 = 48 / 66, y3 = 57 / 66;
    const zoneColour = (v) => (v >= y3 ? C("meter-high") : v >= y12 ? C("meter-mid") : C("meter-low"));
    for (let c = 0; c < 2 && meters; ++c) {
      const mx = z.x + z.w * 0.62 + c * 5.0;
      fillRR(g, mx, top, 3, span, R_HAIR, fade(whiteAlpha(0.05), a));
      const bar = (lv, alpha) => {
        if (lv <= 0.001) return;
        const seg = (from, to, col) => {
          const f = Math.min(lv, to);
          if (f <= from) return;
          fillRR(g, mx, bottom - f * span, 3, (f - from) * span, R_HAIR, fade(col, a * alpha));
        };
        seg(0, y12, C("meter-low")); seg(y12, y3, C("meter-mid")); seg(y3, 1, C("meter-high"));
      };
      bar(l.meter[c].value, 0.45);
      bar(l.rms[c].value, 1.0);
      const hv = l.hold[c].value;
      if (hv > 0.001) fillRR(g, mx, Math.round(bottom - hv * span) - 0.5, 3, 1.5, 0, fade(zoneColour(hv), a));
    }
    if (meters) fillRR(g, z.x + z.w * 0.62, z.y + 0.5, 8, 3.25, R_HAIR, fade(lerpColor(whiteAlpha(0.08), C("meter-high"), l.clip.value), a));
    const ty = bottom - l.fader.value * span;
    fillRR(g, trackX - 10, ty - 4.5, 20, 9, R_CTRL, fade(C("secondary"), a));
    strokeRR(g, trackX - 10, ty - 4.5, 20, 9, R_CTRL, fade(C("foreground"), 0.8 * a), 1);
    line(g, trackX - 6, Math.round(ty) + 0.5, trackX + 6, Math.round(ty) + 0.5, fade(C("foreground"), a), 1);
  }
  function arrowText(g, words, x, cy, maxW, px, fam, c) {
    const ay = Math.round(cy) + 0.5;
    line(g, x, ay, x + 7, ay, c, 1.1); line(g, x + 4.5, ay - 2.5, x + 7, ay, c, 1.1); line(g, x + 4.5, ay + 2.5, x + 7, ay, c, 1.1);
    text(g, ellipsize(words, Math.max(0, maxW - 11), px, fam), x + 11, baseline(cy, px), px, fam, c);
  }
  function centred(g, s, r, px, fam, c) { text(g, s, r.x + (r.w - measure(s, px, fam)) * 0.5, baseline(r.y + r.h * 0.5, px), px, fam, c); }
  function toggle(g, r, label, on, onCol, a) {
    fillRR(g, r.x, r.y, r.w, r.h, R_CTRL, fade(lerpColor(C("secondary"), onCol, on), a));
    centred(g, label, r, 10, FONT.medium, fade(lerpColor(C("secondary-foreground"), C("background"), on), a));
  }
  function inputNames(id) {
    if (id === "master") return (model.strips || []).filter((s) => s.out === "master" || !s.out).map((s) => s.name);
    const s = stripOf(id);
    return s ? (s.fromStrips || []).map((f) => stripOf(f)).filter(Boolean).map((o) => o.name) : [];
  }
  function generatorText(id) {
    const s = id === "master" ? null : stripOf(id);
    if (s && s.devices && s.devices.length && s.devices[0].instrument) return s.devices[0].label;
    if (!s || s.kind === "bus") {
      const n = inputNames(id);
      return n.length ? n.join(", ") : id === "master" ? vm.tr("the mix") : vm.tr("nothing yet");
    }
    return vm.tr("Audio clips");
  }
  function paintRack(g, id, devs, x, w, a, col) {
    const geo = geoOf(x, w, bodyTop(), bodyH()), rv = rackOf(devs), px = 9.0, trk = 0.13 * px;
    const s = id === "master" ? null : stripOf(id);
    const titles = [!s || s.kind === "bus" ? vm.tr("Input") : vm.tr("Generator"), vm.tr("Effects")];
    for (let k = 0; k < 2; ++k) {
      const r = geo.titles[k];
      text(g, ellipsize(vm.upper(titles[k]), r.w, px, FONT.semibold, trk), r.x, baseline(r.y + r.h * 0.5, px), px, FONT.semibold, fade(C("muted-foreground"), a), trk);
    }
    const r = geo.gen;
    if (rv.gen) {
      const d = devs[0];
      fillRR(g, r.x, r.y, r.w, r.h, R_CTRL, fade(C("secondary"), a));
      fillRR(g, r.x, r.y + 3, 2, r.h - 6, R_PILL, fade(col, a));
      text(g, ellipsize(d.label, r.w - 11.5, 10, FONT.medium), r.x + 6.5, baseline(r.y + r.h * 0.5, 10), 10, FONT.medium, fade(d.bypass ? C("muted-foreground") : C("foreground"), a));
      hits.push({ r, part: "chip", id, dv: d.id });
    } else {
      strokeRR(g, r.x, r.y, r.w, r.h, R_CTRL, fade(whiteAlpha(0.08), a), 1);
      const input = id === "master" || (s && s.kind === "bus");
      const names = input ? inputNames(id) : [];
      const mc = fade(C("muted-foreground"), a);
      if (!names.length) text(g, ellipsize(generatorText(id), r.w - 10, 10, FONT.sans), r.x + 5, baseline(r.y + r.h * 0.5, 10), 10, FONT.sans, mc);
      else {
        const more = names.length > 1 ? "+" + (names.length - 1) : "";
        const mw = more ? measure(more, 10, FONT.mono) + 4 : 0;
        text(g, ellipsize(names[0], r.w - 10 - mw, 10, FONT.sans), r.x + 5, baseline(r.y + r.h * 0.5, 10), 10, FONT.sans, mc);
        if (more) text(g, more, r.x + r.w - 5 - (mw - 4), baseline(r.y + r.h * 0.5, 10), 10, FONT.mono, mc);
      }
    }
    // the effects: every one and "+ Effect", or the first, "+N more" and "+ Effect"
    const gen = rv.gen ? 1 : 0;
    for (let j = 0; j < rv.shown; ++j) {
      const fr = geo.fx[j];
      const add = j === rv.shown - 1, more = rv.more && j === 1;
      if (add) {
        strokeRR(g, fr.x, fr.y, fr.w, fr.h, R_CTRL, fade(whiteAlpha(0.14), a), 1);
        text(g, ellipsize(vm.tr("+ Effect"), fr.w - 10, 10, FONT.sans), fr.x + 5, baseline(fr.y + fr.h * 0.5, 10), 10, FONT.sans, fade(C("muted-foreground"), a));
        continue;
      }
      fillRR(g, fr.x, fr.y, fr.w, fr.h, R_CTRL, fade(C("secondary"), a));
      if (more) {
        text(g, ellipsize(vm.fillIn(vm.tr("+{1} more"), String(rv.effects - 1)), fr.w - 10, 10, FONT.sans), fr.x + 5, baseline(fr.y + fr.h * 0.5, 10), 10, FONT.sans, fade(C("muted-foreground"), a));
        continue;
      }
      const d = devs[gen + j];
      text(g, ellipsize(d.label, fr.w - 10, 10, FONT.sans), fr.x + 5, baseline(fr.y + fr.h * 0.5, 10), 10, FONT.sans, fade(d.bypass ? C("muted-foreground") : C("foreground"), a));
      hits.push({ r: fr, part: "chip", id, dv: d.id });
    }
  }
  function paintCard(g, s, x, w, a) {
    const l = lives.get(s.id);
    if (!l) return;
    const by = bodyTop(), bh = bodyH(), geo = geoOf(x, w, by, bh);
    if (armable(s)) armRow(geo);
    g.save();
    g.beginPath(); g.rect(x, kTabsH, w, H() - kTabsH); g.clip();
    fillRR(g, x, by, w, bh, 0, fade(C("card"), a));
    line(g, x + w - 0.5, by, x + w - 0.5, by + bh, fade(C("border"), a), 1);
    const ca = a * (1 - 0.55 * l.dim.value);
    const col = lerpColor(l.colFrom, l.colTo, l.colT.value);
    fillRR(g, x + 6.5, by + 5, w - 13, 3, R_PILL, fade(col, ca));
    text(g, ellipsize(s.name, w - 13, 11, FONT.medium), x + 6.5, by + 21, 11, FONT.medium, fade(C("foreground"), ca));
    const kw = s.kind === "instrument" || s.kind === "audio" || s.kind === "bus" ? vm.tr(s.kind) : s.kind;
    let sub = kw;
    if (s.kind === "audio" || s.kind === "instrument") sub += " · " + vm.fillIn(vm.trn(s.clipCount || 0, "{1} clip", "{1} clips"), String(s.clipCount || 0));
    else sub += " · " + vm.fillIn(vm.tr("fed by {1}"), String((s.fromStrips || []).length));
    text(g, ellipsize(sub, Math.max(0, w - 13), 9, FONT.sans), x + 6.5, by + 33, 9, FONT.sans, fade(C("muted-foreground"), ca));
    paintRack(g, s.id, s.devices || [], x, w, ca, col);
    // sends
    const sends = s.sends || [], ns = sends.length;
    for (let k = 0; k < Math.min(ns, 2); ++k) {
      const r = geo.sends[k];
      if (k === 1 && ns > 2) {
        text(g, ellipsize(vm.fillIn(vm.tr("+{1} in the matrix"), String(ns - 1)), r.w - 6, 9, FONT.sans), r.x + 3, baseline(r.y + r.h * 0.5, 9), 9, FONT.sans, fade(C("muted-foreground"), ca));
        hits.push({ r, part: "sendMore", id: s.id });
        continue;
      }
      const sd = sends[k];
      const gain = num(sd.gain, 1) + (sd.pre ? " P" : "");
      const gw = measure(gain, 9, FONT.mono), cyS = r.y + r.h * 0.5;
      arrowText(g, labelOf(sd.to), r.x + 3, cyS, r.w - gw - 10, 9, FONT.sans, fade(C("secondary-foreground"), ca));
      text(g, gain, r.x + r.w - 3 - gw, baseline(cyS, 9), 9, FONT.mono, fade(C("muted-foreground"), ca));
      hits.push({ r, part: "send", id: s.id, sd: sd.id, gain: sd.gain });
    }
    // pan: from the centre
    {
      const r = geo.pan, cy = r.y + r.h * 0.5, cx = r.x + r.w * 0.5, px = cx + l.pan.value * r.w * 0.5;
      fillRR(g, r.x, cy - 1.5, r.w, 3, R_PILL, fade(C("input"), ca));
      fillRR(g, Math.min(cx, px), cy - 1.5, Math.abs(px - cx), 3, R_PILL, fade(C("primary"), ca));
      line(g, Math.round(cx) + 0.5, cy - 4, Math.round(cx) + 0.5, cy + 4, fade(whiteAlpha(0.3), ca), 1);
      g.beginPath(); g.arc(px, cy, 4, 0, Math.PI * 2); g.fillStyle = rgba(fade(C("foreground"), ca)); g.fill();
      hits.push({ r, part: "pan", id: s.id });
    }
    paintFader(g, geo.zone, l, ca);
    hits.push({ r: { x: geo.clip.x - 4, y: geo.clip.y - 3.25, w: geo.clip.w + 8, h: 9.75 }, part: "clip", id: s.id });
    hits.push({ r: geo.zone, part: "fader", id: s.id });
    const rd = dbText(faderDb(l.fader.value));
    text(g, rd, x + (w - measure(rd, 10, FONT.mono)) * 0.5, geo.readoutY, 10, FONT.mono, fade(C("secondary-foreground"), ca));
    toggle(g, geo.mute, "M", l.mute.value, whiteAlpha(0.82), a);
    toggle(g, geo.solo, "S", l.solo.value, C("solo"), a);
    hits.push({ r: geo.mute, part: "mute", id: s.id }, { r: geo.solo, part: "solo", id: s.id });
    if (geo.arm) { toggle(g, geo.arm, "R", l.arm.value, C("destructive"), a); hits.push({ r: geo.arm, part: "arm", id: s.id }); }
    {
      const r = geo.out;
      fillRR(g, r.x, r.y, r.w, r.h, R_CTRL, fade(C("input"), ca));
      arrowText(g, labelOf(s.out), r.x + 5, r.y + r.h * 0.5, r.w - 10, 10, FONT.sans, fade(C("secondary-foreground"), ca));
    }
    g.restore();
  }
  function paintMaster(g, a) {
    const l = lives.get("master");
    if (!l || a <= 0.001) return;
    const x = stripsRight(), w = kMasterW, by = bodyTop(), bh = bodyH(), geo = geoOf(x, w, by, bh);
    g.save();
    g.beginPath(); g.rect(x, kTabsH, w, H() - kTabsH); g.clip();
    fillRR(g, x, by, w, bh, 0, fade(C("popover"), a));
    line(g, x + 0.5, by, x + 0.5, by + bh, fade(C("border"), a), 1);
    text(g, ellipsize(vm.tr("Master"), w - 13, 11, FONT.medium), x + 6.5, by + 21, 11, FONT.medium, fade(C("foreground"), a));
    const md = model.masterDevices || [], nd = md.length;
    text(g, ellipsize(nd ? vm.fillIn(vm.trn(nd, "{1} device", "{1} devices"), String(nd)) : vm.tr("the mix"), w - 13, 9, FONT.sans), x + 6.5, by + 33, 9, FONT.sans, fade(C("muted-foreground"), a));
    paintRack(g, "master", md, x, w, a, C("secondary-foreground"));
    {
      const mr = { x: x + 9.75, y: by + 192 - 3.25, w: w - 19.5, h: 19.5 }, on = mono.value; // the master's pan row (monoRect)
      fillRR(g, mr.x, mr.y, mr.w, mr.h, R_CTRL, fade(lerpColor(C("secondary"), C("primary"), on), a));
      centred(g, ellipsize(vm.tr("Mono"), mr.w - 4, 9, FONT.medium), mr, 9, FONT.medium, fade(lerpColor(C("secondary-foreground"), C("primary-foreground"), on), a));
      hits.push({ r: mr, part: "mono", id: "master" });
    }
    paintFader(g, geo.zone, l, a);
    hits.push({ r: { x: geo.clip.x - 4, y: geo.clip.y - 3.25, w: geo.clip.w + 8, h: 9.75 }, part: "clip", id: "master" });
    hits.push({ r: geo.zone, part: "fader", id: "master" });
    const rd = dbText(faderDb(l.fader.value));
    text(g, rd, x + (w - measure(rd, 10, FONT.mono)) * 0.5, geo.readoutY, 10, FONT.mono, fade(C("secondary-foreground"), a));
    {
      const lr = { x: geo.mute.x, y: geo.mute.y, w: geo.solo.x + geo.solo.w - geo.mute.x, h: geo.mute.h };
      fillRR(g, lr.x, lr.y, lr.w, lr.h, R_CTRL, fade(C("secondary"), a));
      const on = lufsOn.value;
      if (on > 0.001) centred(g, ellipsize(lufs.value.toFixed(1) + " LUFS", lr.w - 8, 10, FONT.mono), lr, 10, FONT.mono, fade(C("foreground"), a * on));
      if (on < 0.999) centred(g, ellipsize("— LUFS", lr.w - 8, 10, FONT.mono), lr, 10, FONT.mono, fade(C("muted-foreground"), a * (1 - on)));
      hits.push({ r: lr, part: "loudness", id: "master" });
    }
    const outs = (model.masterOut || []).map(labelOf).join(", ");
    arrowText(g, outs || vm.tr("no port"), x + 6.5, geo.out.y + geo.out.h * 0.5, w - 13, 10, FONT.sans, fade(C("muted-foreground"), a));
    g.restore();
  }
  function paintPage(g, a) {
    const mx = mixers[tab];
    if (!mx) return;
    const cards = (model.strips || []).filter((s) => s.mixer === mx.id);
    g.save();
    g.beginPath(); g.rect(0, kTabsH, stripsRight(), H() - kTabsH); g.clip();
    let cx = -scrollX;
    for (const s of cards) {
      if (cx + kStripW > 0 && cx < stripsRight()) paintCard(g, s, cx, kStripW, a);
      cx += kStripW;
    }
    const add = { x: cx + 6.5, y: bodyTop() + 6.5, w: kAddLineW - 13, h: 26 };
    if (add.x < stripsRight()) {
      strokeRR(g, add.x, add.y, add.w, add.h, R_CTRL, fade(whiteAlpha(0.14), a), 1);
      centred(g, ellipsize(vm.tr("+ Line"), add.w - 6, 10, FONT.sans), add, 10, FONT.sans, fade(C("secondary-foreground"), a));
    }
    if (!cards.length) {
      const tx = add.x + add.w + PADX * 2, mw = Math.max(0, stripsRight() - tx - 13);
      text(g, ellipsize(vm.tr("No strips on this mixer."), mw, 11, FONT.sans), tx, kTabsH + 34, 11, FONT.sans, fade(C("muted-foreground"), a));
      text(g, ellipsize(vm.tr("\"+ Line\" adds one here; a strip moved to this mixer shows up too."), mw, 10, FONT.sans), tx, kTabsH + 52, 10, FONT.sans, fade(C("muted-foreground"), 0.75 * a));
    }
    g.restore();
    return cards.length * kStripW + kAddLineW;
  }
  function paint() {
    const g = cv.begin(), w = W(), h = H();
    hits = [];
    fillRR(g, 0, 0, w, h, 0, C("stage-bg"));
    if (!model) return;
    if (!onMatrix()) paintPage(g, pageA.value);
    paintMaster(g, onMatrix() ? 0 : 1);
    // the tab bar
    fillRR(g, 0, 0, w, kTabsH, 0, C("header-bg"));
    line(g, 0, 0.5, w, 0.5, C("border"), 1);
    line(g, 0, kTabsH - 0.5, w, kTabsH - 0.5, C("border"), 1);
    const tabs = tabsLayout();
    const cur = tabs.find((t) => t.kind === "tab" && t.t === tab);
    if (cur) {
      if (!hiInit) { hiX.set(cur.x); hiW.set(cur.w); hiXLast = cur.x; hiWLast = cur.w; hiInit = true; }
      if (cur.x !== hiXLast) { hiX.animateTo(cur.x, M.select, Ease.EaseOutCubic, nowMs); hiXLast = cur.x; frames(step); }
      if (cur.w !== hiWLast) { hiW.animateTo(cur.w, M.select, Ease.EaseOutCubic, nowMs); hiWLast = cur.w; frames(step); }
    }
    if (hiW.value > 0.5) fillRR(g, hiX.value, 4, hiW.value, kTabsH - 8, R_HAIR, C("secondary"));
    for (const tb of tabs) {
      const r = { x: tb.x, y: 4, w: tb.w, h: kTabsH - 8 };
      if (tb.kind === "add") {
        const c = C("muted-foreground");
        line(g, r.x + r.w * 0.5, r.y + 6, r.x + r.w * 0.5, r.y + r.h - 6, c, 1.3);
        line(g, r.x + 6, r.y + r.h * 0.5, r.x + r.w - 6, r.y + r.h * 0.5, c, 1.3);
        hits.push({ r, part: "addMixer" });
        continue;
      }
      const on = tb.t === tab;
      text(g, tb.label, tb.x + PADX, baseline(kTabsH * 0.5, 10), 10, on ? FONT.medium : FONT.sans, on ? C("foreground") : C("muted-foreground"));
      hits.push({ r, part: "tab", index: tb.t });
    }
    {
      const r = { x: w - 6.5 - 21.125, y: 4, w: 21.125, h: kTabsH - 8 };
      const low = h < kTabsH + 40, c = C("muted-foreground"), cx = r.x + r.w * 0.5, cy = r.y + r.h * 0.5, d = low ? -1 : 1;
      line(g, cx - 4, cy - 2 * d, cx, cy + 2 * d, c, 1.3);
      line(g, cx, cy + 2 * d, cx + 4, cy - 2 * d, c, 1.3);
      hits.push({ r, part: "toggle" });
    }
    hits.push({ r: { x: 0, y: 0, w, h: kResizeH }, part: "resize" });
  }

  function step(now) {
    nowMs = now;
    let moving = false;
    for (const l of lives.values()) for (const p of all(l)) moving = p.update(now) || moving;
    for (const p of [lufs, lufsOn, mono, hiX, hiW, pageA]) moving = p.update(now) || moving;
    paint();
    return moving || dragging;
  }

  // ── input ─────────────────────────────────────────────────────────────────────────────────
  const localOf = (e) => {
    const r = host.getBoundingClientRect(), k = r.width / Math.max(1, host.clientWidth || W());
    return { x: (e.clientX - r.left) / (k || 1), y: (e.clientY - r.top) / (k || 1) };
  };
  const hitAt = (p) => { for (let i = hits.length - 1; i >= 0; --i) if (inR(hits[i].r, p)) return hits[i]; return null; };
  const send = (line) => vm.cmd(line);
  const faderAt = (id) => { const l = lives.get(id); return l ? l.fader.value : 0; };

  function dragTo(p, e) {
    const h = press;
    if (h.part === "resize") { vm.dockResize(h.startH - (e.clientY - h.cy) / (h.k || 1)); return; }
    if (h.part === "fader") {
      const z = h.r, pos = Math.min(1, Math.max(0, (z.y + z.h - 6 - p.y) / Math.max(1, z.h - 12) - h.grab));
      const l = liveFor(h.id);
      l.fader.set(pos);
      const db = rnd(faderDb(pos), 0.1);
      l.faderLast = faderPos(db);
      const ln = h.id === "master" ? "set project.masterGain=" + num(db, 1) : "set " + h.id + ".gain=" + num(db, 1);
      if (ln !== lastSent) { lastSent = ln; vm.note(ln); }
    } else if (h.part === "pan") {
      const r = h.r;
      let v = Math.min(1, Math.max(-1, (p.x - r.x) / Math.max(1, r.w) * 2 - 1));
      if (Math.abs(v) < 0.03) v = 0;
      v = rnd(v, 0.01);
      const l = liveFor(h.id);
      l.pan.set(v); l.panLast = v;
      const ln = "set " + h.id + ".pan=" + num(v, 2);
      if (ln !== lastSent) { lastSent = ln; vm.note(ln); }
    } else if (h.part === "send") {
      const gain = Math.min(12, Math.max(-60, rnd(h.gain + (p.x - h.p.x) * 0.1, 0.1)));
      const ln = "set " + h.sd + ".gain=" + num(gain, 1);
      if (ln !== lastSent) { lastSent = ln; vm.note(ln); }
    }
    frames(step);
  }
  function click(h) {
    switch (h.part) {
      case "tab": setTab(h.index); break;
      case "addMixer": send("mixer add"); break;
      case "toggle": vm.toggleDock(); break;
      case "clip": send("meters clear"); break;
      case "loudness": vm.openLoudness(); break;
      case "mono": send("transport mono " + (model.transport && model.transport.mono ? "off" : "on")); break;
      case "mute": { const s = stripOf(h.id); if (s) send("set " + h.id + ".mute=" + (s.mute ? "false" : "true")); break; }
      case "solo": { const s = stripOf(h.id); if (s) send("set " + h.id + ".solo=" + (s.solo ? "false" : "true")); break; }
      case "arm": send(model.armed === h.id ? "strip arm none" : "strip arm " + h.id); break;
      case "chip": vm.openDevice(h.dv); break;
      case "sendMore": setTab(mixers.length); break;
      default: break;
    }
  }
  const onDown = (e) => {
    if (e.button !== 0) return;
    const p = localOf(e), h = hitAt(p);
    if (!h) return;
    press = { ...h, p, cy: e.clientY, k: host.getBoundingClientRect().height / Math.max(1, host.clientHeight || H()) };
    dragging = false; lastSent = "";
    if (h.part === "resize") press.startH = vm.view.dockHeight.peek();
    if (h.part === "fader") {
      const z = h.r, pos = Math.min(1, Math.max(0, (z.y + z.h - 6 - p.y) / Math.max(1, z.h - 12)));
      press.grab = Math.abs(pos - faderAt(h.id)) < 0.06 ? pos - faderAt(h.id) : 0;
    }
    cv.el.setPointerCapture(e.pointerId);
    e.preventDefault();
  };
  const onMove = (e) => {
    if (!press) return;
    const p = localOf(e);
    if (!dragging && Math.hypot(p.x - press.p.x, p.y - press.p.y) < 3) return;
    dragging = true;
    dragTo(p, e);
  };
  const onUp = (e) => {
    if (!press) return;
    const h = press;
    if (dragging) { if (lastSent) vm.cmd(lastSent); } // the release lands where it was let go: ONE awaited line
    else click(h);
    press = null; dragging = false; lastSent = "";
    try { cv.el.releasePointerCapture(e.pointerId); } catch (_) {}
    frames(step);
  };
  const onWheel = (e) => {
    if (!model || onMatrix()) return;
    const content = (model.strips || []).filter((s) => mixers[tab] && s.mixer === mixers[tab].id).length * kStripW + kAddLineW;
    const maxX = Math.max(0, content - stripsRight()), maxY = Math.max(0, bodyH() - (H() - kTabsH));
    const dx = e.shiftKey ? e.deltaY : e.deltaX, dy = e.shiftKey ? 0 : e.deltaY;
    const nx = Math.min(maxX, Math.max(0, scrollX + dx)), ny = Math.min(maxY, Math.max(0, scrollY + dy));
    if (nx === scrollX && ny === scrollY) return;
    scrollX = nx; scrollY = ny;
    e.preventDefault();
    frames(step);
  };
  cv.el.addEventListener("pointerdown", onDown);
  cv.el.addEventListener("pointermove", onMove);
  cv.el.addEventListener("pointerup", onUp);
  cv.el.addEventListener("pointercancel", onUp);
  cv.el.addEventListener("wheel", onWheel, { passive: false });
  cv.onResize(() => frames(step));

  own(host, effect(() => {
    const m = vm.model.value;
    void ctx.box.value; void colorsVersion.value; void vm.lang.value;
    untracked(() => { nowMs = performance.now(); bind(m); frames(step); });
  }));
  own(host, () => {
    cv.el.removeEventListener("pointerdown", onDown);
    cv.el.removeEventListener("pointermove", onMove);
    cv.el.removeEventListener("pointerup", onUp);
    cv.el.removeEventListener("pointercancel", onUp);
    cv.el.removeEventListener("wheel", onWheel);
  });
}
