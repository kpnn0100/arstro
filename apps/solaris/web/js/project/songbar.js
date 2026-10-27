/*
 * Solaris — project/songbar.js: SongBar (widgets/SongBar.{h,cpp}, R-UI-3, R-TIME-4, R-PLAY-3) — the song screen's top
 * bar, 29.25 high, on one canvas with cosmo's MenuStrip (ui/menustrip.js) placed after Settings.
 *
 * Left → right: the `solaris.` wordmark (13 px, the dot in the accent — it IS Home: A.requestHome()), Settings
 * (A.openSettings()), the menus (File · Edit · Song · View — project/menus.js songMenus), the VERSION chip (R-VER: its
 * label cross-fades, its width eases, an accent ring while a version is open; its menu sends `version …` lines), the
 * song's name with the unsaved dot; Record, Play/Stop (cross-faded), the position bar.beat.tick in mono, the tempo (a
 * control, R-UI-13: dragged it sends one `set project.bpm=<n>` — or `tempo move <tp> --bpm <n>` — per step; a
 * double-click types one in); the MIDI-learn pill (R-CTRL-1), CPU (R-PERF-1), the master meter (up 40 ms, down
 * meterFall) and Save.
 *
 * Every number is SongBar.cpp's; every line is the native's (through project/menus.js's actions where App owns it).
 */
import { effect } from "../core/signal.js";
import { own } from "../core/dom.js";
import { Anim, Ease, frames, motion } from "../kit/anim.js";
import { canvasIn, color, colorsVersion, fillRR, strokeRR, line, text, withAlpha, primaryAlpha, whiteAlpha, hoverWash, lerpColor, brighten } from "../kit/draw.js";
import { FONT, measure, ellipsize, baseline } from "../kit/text.js";
import { glyph } from "../kit/glyphs.js";
import { g as printfG } from "../kit/format.js";
import { useCss } from "../kit/css.js";
import { a11yNode, A11Y_ORDER } from "../kit/a11y.js";
import { appActions, songMenus, menusKey } from "./menus.js";
import { menuStrip } from "../ui/menustrip.js";

export const kHeight = 29.25;
const kBpmStepPx = 3.25, kNameMin = 120;
const kPlayW = 30, kBtnH = 21, kMeterW = 60;
const kChipPad = 8, kChevron = 14;
const RADIUS = 2, PILL = 9999, HOVER_LIFT = 0.14;
const kHome = 0, kPlay = 1, kSave = 2, kSettings = 3, kBpm = 4, kRecord = 5, kLearn = 6, kVersion = 7, kNone = -1;

const fade = (c, a) => withAlpha(c, c[3] * a);
const dbOf = (lin) => (lin > 0 ? 20 * Math.log10(lin) : -90);
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const contains = (r, p) => p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h;
/** a name as one token of a command line: quoted when it holds a space (the format has no escape: " becomes ') */
function quoted(v) {
  v = String(v).replace(/"/g, "'");
  return v !== "" && !/[ \t;=#]/.test(v) ? v : '"' + v + '"';
}

/** cosmo_v2::HoverFade — one hovered id; amounts ease in and out over motion.hover. */
class HoverFade {
  constructor() { this.cur = -1; this.map = new Map(); }
  setHovered(id, now) {
    if (id === this.cur) return;
    const M = motion();
    if (this.cur !== -1 && this.map.has(this.cur)) this.map.get(this.cur).animateTo(0, M.hover, Ease.EaseOutCubic, now);
    this.cur = id;
    if (id !== -1) {
      if (!this.map.has(id)) this.map.set(id, new Anim(0));
      this.map.get(id).animateTo(1, M.hover, Ease.EaseOutCubic, now);
    }
  }
  clear(now) { this.setHovered(-1, now); }
  advance(now) { let m = false; for (const a of this.map.values()) m = a.update(now) || m; return m; }
  amount(id) { const a = this.map.get(id); return a ? a.value : 0; }
}

export function mount(host, vm, ctx) {
  useCss("project/songbar.css");
  host.classList.add("sl-songbar");
  const cv = canvasIn(host, ctx);
  const A = appActions(vm, ctx);
  const tr = vm.tr, say = vm.say, shown = vm.shown;
  const M = motion();

  // ------------------------------------------------------------------ the menus (App::buildMenus), cosmo's strip
  let menus = songMenus(vm, A), menuKey = menusKey(menus);
  const shownMenus = (ms) => ms.map((mn) => ({ title: shown(mn.title), items: mn.items.map((it) => ({ label: shown(it.label), action: it.action })) }));
  const strip = menuStrip({
    menus: shownMenus(menus), height: kHeight,
    onOpenChanged: (i) => {
      host.style.zIndex = i >= 0 ? "5" : "";
      if (vm.view.menuOpen) vm.view.menuOpen.value = i >= 0;
    },
  });
  strip.el.classList.add("sl-songbar-menus");
  host.append(strip.el);

  // ------------------------------------------------------------------ bound state (SongBar::bind)
  let name = "", dirty = false, playingWanted = false, recordingWanted = false, position = 0, bpm = 120, tempoId = "";
  let beatsPerBar = 4, peak = [0, 0], learning = "", learnShown = "", cpu = 0, versions = [], version = "", chipWanted = "main";
  // eased (SongBar.h)
  const bpmShownA = new Anim(120), recLit = new Anim(0), learnA = new Anim(0), cpuShown = new Anim(0);
  const playingA = new Anim(0), dirtyDot = new Anim(0), meter = [new Anim(0), new Anim(0)];
  const chipT = new Anim(1), onVersionA = new Anim(0), chipW = new Anim(0);
  let bpmLast = -1, bpmPress = 0, bpmPressY = 0, bpmLive = 0, bpmDrag = false, bpmSent = "", cpuTarget = 0;
  let init = false, chipPlaced = false, chipFrom = "", chipTo = "", chipWTarget = -1, chipTextW = 0, wordW = 52;
  let hovered = false;
  const hover = new HoverFade();
  const W = () => ctx.box.peek().w;

  const tempoLine = (v) => (tempoId === "" ? "set project.bpm=" + v : "tempo move " + tempoId + " --bpm " + v);
  const send = (line) => vm.cmd(line);

  function bind(m) {
    m = m || {};
    const t = m.transport || {};
    name = m.projectName ? m.projectName : say("Untitled");
    dirty = !!m.dirty;
    playingWanted = !!t.playing;
    recordingWanted = !!t.recording; // R-MIDI-2
    position = +t.position || 0;
    const tempos = m.tempos || [];
    bpm = !tempos.length ? +m.bpm || 120 : +t.tempo || +m.bpm || 120; // R-TEMPO-1
    tempoId = !tempos.length ? "" : t.tempoId || "";
    beatsPerBar = Math.max(1, parseInt(m.sig, 10) || 0);
    const mp = t.masterPeak || [0, 0];
    peak = [+mp[0] || 0, +mp[1] || 0];
    learning = m.mapLearning || ""; // R-CTRL-1
    cpu = +t.cpu || 0;              // R-PERF-1
    if (learning) learnShown = learning;
    versions = m.versions || [];
    version = m.version || "";
    chipWanted = "main";
    for (const v of versions) if (v.id === version) chipWanted = v.name + (v.frozen ? " · " + say("frozen") : "");
  }

  // ------------------------------------------------------------------ geometry (SongBar::hitRect …)
  const menusX = () => hitRect(kSettings).x + hitRect(kSettings).w + 6;
  const chipX = () => menusX() + strip.contentWidth() + 14;
  const transportX = () => Math.max(W() * 0.5 - 110, chipX() + chipW.value + 10 + kNameMin + 24);
  function hitRect(which) {
    const Wd = W(), cy = kHeight * 0.5;
    switch (which) {
      case kHome: return { x: 6, y: cy - 10, w: wordW + 12, h: 20 };
      case kSettings: return { x: 12 + wordW + 12, y: cy - kBtnH * 0.5, w: 66, h: kBtnH };
      case kPlay: return { x: transportX(), y: cy - kBtnH * 0.5, w: kPlayW, h: kBtnH };
      case kRecord: return { x: transportX() - 6.5 - kPlayW, y: cy - kBtnH * 0.5, w: kPlayW, h: kBtnH };
      case kSave: return { x: Wd - 12 - 48, y: cy - kBtnH * 0.5, w: 48, h: kBtnH };
      case kBpm: { const p = hitRect(kPlay); return { x: p.x + p.w + 12 + 9 * 0.6 * 11 + 12, y: cy - kBtnH * 0.5, w: 68.25, h: kBtnH }; }
      case kVersion: return { x: chipX(), y: cy - 9, w: chipW.value, h: 18 };
      case kLearn: {
        const b = hitRect(kBpm), x = b.x + b.w + 12, right = cpuRect().x - 12;
        return { x, y: cy - kBtnH * 0.5, w: clamp(right - x, 0, 260), h: kBtnH };
      }
      default: return { x: 0, y: 0, w: 0, h: 0 };
    }
  }
  function cpuRect() { const s = hitRect(kSave); return { x: s.x - 14 - kMeterW - 12 - 58.5, y: kHeight * 0.5 - 9, w: 58.5, h: 18 }; }
  function hitAt(p) {
    for (let i = kHome; i <= kRecord; ++i) if (contains(hitRect(i), p)) return i;
    if (learnA.value > 0.5 && contains(hitRect(kLearn), p)) return kLearn;
    if (contains(hitRect(kVersion), p)) return kVersion;
    return kNone;
  }
  function placeStrip() {
    strip.el.style.left = menusX() + "px";
    strip.el.style.top = "0px";
  }

  // ------------------------------------------------------------------ SongBar::advance
  function advance(now) {
    if (!init) {
      playingA.set(playingWanted ? 1 : 0); recLit.set(recordingWanted ? 1 : 0);
      learnA.set(learning ? 1 : 0); dirtyDot.set(dirty ? 1 : 0);
      init = true;
    }
    let moving = false;
    const toward = (p, want, ms) => {
      if (Math.abs(p.value - want) > 1e-4 && !p.animating) p.animateTo(want, ms, Ease.EaseOutCubic, now);
      moving = p.update(now) || moving;
    };
    if (bpmLast < 0) { bpmShownA.set(bpm); bpmLast = bpm; } // first placement: nowhere to travel from
    else if (bpm !== bpmLast) {
      if (bpmDrag) bpmShownA.set(bpm); else bpmShownA.animateTo(bpm, M.catchUp, Ease.EaseOutCubic, now);
      bpmLast = bpm;
    }
    moving = bpmShownA.update(now) || moving;
    toward(playingA, playingWanted ? 1 : 0, M.crossFade);
    toward(recLit, recordingWanted ? 1 : 0, M.crossFade);
    toward(learnA, learning ? 1 : 0, M.crossFade);
    if (cpu !== cpuTarget) { cpuShown.animateTo(cpu, M.catchUp, Ease.EaseOutCubic, now); cpuTarget = cpu; }
    moving = cpuShown.update(now) || moving;
    toward(dirtyDot, dirty ? 1 : 0, M.crossFade);
    // R-VER: the chip
    const onVersion = version !== "" && version !== "main";
    if (!chipPlaced) { chipTo = chipWanted; chipT.set(1); onVersionA.set(onVersion ? 1 : 0); chipPlaced = true; }
    else if (chipWanted !== chipTo) { chipFrom = chipTo; chipTo = chipWanted; chipT.set(0); chipT.animateTo(1, M.crossFade, Ease.EaseOutCubic, now); }
    moving = chipT.update(now) || moving;
    toward(onVersionA, onVersion ? 1 : 0, M.crossFade);
    chipTextW = measure(shown(chipTo), 10, FONT.sans);
    if (chipTextW > 0) {
      const want = kChipPad + chipTextW + kChevron + kChipPad;
      if (chipWTarget < 0) chipW.set(want);
      else if (Math.abs(want - chipWTarget) > 0.25) chipW.animateTo(want, M.select, Ease.EaseOutCubic, now);
      chipWTarget = want;
    }
    moving = chipW.update(now) || moving;
    for (let c = 0; c < 2; ++c) {
      // −60…0 dBFS → 0…1; up in 40 ms (a meter's attack), down in meterFall (its release)
      const want = clamp((dbOf(peak[c]) + 60) / 60, 0, 1);
      const p = meter[c];
      if (Math.abs(p.value - want) > 1e-3 && p.target !== want) {
        const rising = want > p.value;
        p.animateTo(want, rising ? 40 : M.meterFall, rising ? ((t) => 1 - (1 - t) * (1 - t)) : Ease.EaseOutCubic, now);
      }
      moving = p.update(now) || moving;
    }
    if (!hovered) hover.clear(now);
    moving = hover.advance(now) || moving;
    paint();
    return moving;
  }
  const kick = () => frames(advance);

  // ------------------------------------------------------------------ SongBar::onPaint
  function paint() {
    const g = cv.begin();
    const Wd = W(), H = kHeight, cy = H * 0.5;
    const fg = color("foreground"), muted = color("muted-foreground"), border = color("border"), primary = color("primary");
    const secondary = color("secondary"), destructive = color("destructive");
    fillRR(g, 0, 0, Wd, H, 0, color("left-rail-bg"));
    line(g, 0, H - 0.5, Wd, H - 0.5, border, 1);

    // the wordmark — Home: washed on hover
    const wW = measure("solaris", 13, FONT.semibold, -0.03 * 13);
    const newWordW = wW + measure(".", 13, FONT.semibold);
    if (Math.abs(newWordW - wordW) > 0.01) { wordW = newWordW; placeStrip(); }
    const homeHv = hover.amount(kHome);
    if (homeHv > 0.001) { const r = hitRect(kHome); fillRR(g, r.x, r.y, r.w, r.h, RADIUS, hoverWash(homeHv)); }
    text(g, "solaris", 12, baseline(cy, 13), 13, FONT.semibold, fg, -0.03 * 13);
    text(g, ".", 12 + wW, baseline(cy, 13), 13, FONT.semibold, primary);

    const button = (id, label, prim) => {
      const r = hitRect(id);
      const hv = hover.amount(id);
      if (prim) fillRR(g, r.x, r.y, r.w, r.h, RADIUS, brighten(primary, HOVER_LIFT * hv));
      else if (hv > 0.001) fillRR(g, r.x, r.y, r.w, r.h, RADIUS, hoverWash(hv));
      if (label) {
        const ink = prim ? color("primary-foreground") : lerpColor(muted, fg, 0.5 + 0.5 * hv);
        const fit = ellipsize(label, r.w - 8, 11, FONT.sans);
        text(g, fit, r.x + (r.w - measure(fit, 11, FONT.sans)) * 0.5, baseline(r.y + r.h * 0.5, 11), 11, FONT.sans, ink);
      }
    };
    button(kSettings, tr("Settings"), false);

    // R-VER: the version chip
    {
      const r = hitRect(kVersion);
      if (r.w <= 0) r.w = kChipPad + chipTextW + kChevron + kChipPad;
      const hv = hover.amount(kVersion), ring = onVersionA.value, tt = chipT.value;
      fillRR(g, r.x, r.y, r.w, r.h, RADIUS, lerpColor(secondary, primaryAlpha(0.12), ring));
      strokeRR(g, r.x, r.y, r.w, r.h, RADIUS, lerpColor(border, primary, 0.7 * ring), 1);
      if (hv > 0.001) fillRR(g, r.x, r.y, r.w, r.h, RADIUS, hoverWash(hv));
      const room = Math.max(0, r.w - kChipPad - kChevron - kChipPad + 4);
      const ink = lerpColor(muted, fg, Math.max(ring, hv));
      if (tt < 0.999 && chipFrom) text(g, ellipsize(shown(chipFrom), room, 10, FONT.sans), r.x + kChipPad, baseline(cy, 10), 10, FONT.sans, fade(ink, 1 - tt));
      text(g, ellipsize(shown(chipTo), room, 10, FONT.sans), r.x + kChipPad, baseline(cy, 10), 10, FONT.sans, fade(ink, tt));
      const vx = r.x + r.w - kChipPad - 5; // the ▾
      g.beginPath(); g.moveTo(vx - 3, cy - 1.5); g.lineTo(vx, cy + 1.5); g.lineTo(vx + 3, cy - 1.5);
      g.strokeStyle = `rgba(${ink[0] * 255}, ${ink[1] * 255}, ${ink[2] * 255}, ${ink[3]})`; g.lineWidth = 1.25; g.stroke();
    }

    // the song's name, ellipsized against the space left before the transport; the unsaved dot after it
    const nameX = chipX() + Math.max(chipW.value, 0) + 10, nameMax = Math.max(0, hitRect(kRecord).x - 24 - nameX);
    const nm = ellipsize(shown(name), nameMax, 12, FONT.medium);
    text(g, nm, nameX, baseline(cy, 12), 12, FONT.medium, fg);
    if (dirtyDot.value > 0.001) {
      g.beginPath(); g.arc(nameX + measure(nm, 12, FONT.medium) + 8, cy, 3, 0, Math.PI * 2);
      const c = fade(primary, dirtyDot.value);
      g.fillStyle = `rgba(${c[0] * 255}, ${c[1] * 255}, ${c[2] * 255}, ${c[3]})`; g.fill();
    }

    // R-MIDI-2: record — a red dot; lit while recording, eased
    {
      const r = hitRect(kRecord);
      const hv = hover.amount(kRecord), lit = recLit.value;
      fillRR(g, r.x, r.y, r.w, r.h, RADIUS, lerpColor(secondary, fade(destructive, 0.3), lit));
      strokeRR(g, r.x, r.y, r.w, r.h, RADIUS, lerpColor(border, destructive, 0.4 * hv + 0.6 * lit), 1);
      const c = lerpColor(lerpColor(muted, destructive, 0.6), destructive, lit);
      g.beginPath(); g.arc(r.x + r.w * 0.5, r.y + r.h * 0.5, 4, 0, Math.PI * 2);
      g.fillStyle = `rgba(${c[0] * 255}, ${c[1] * 255}, ${c[2] * 255}, ${c[3]})`; g.fill();
    }
    // transport: play/stop cross-fade
    {
      const r = hitRect(kPlay);
      const hv = hover.amount(kPlay), p = playingA.value;
      fillRR(g, r.x, r.y, r.w, r.h, RADIUS, lerpColor(secondary, primaryAlpha(0.25), p));
      strokeRR(g, r.x, r.y, r.w, r.h, RADIUS, lerpColor(border, primary, 0.4 * hv + 0.6 * p), 1);
      const gb = { x: r.x + (r.w - 10) * 0.5, y: r.y + (r.h - 10) * 0.5, w: 10, h: 10 };
      if (p < 0.999) glyph.play(g, gb, fade(fg, 1 - p));
      if (p > 0.001) fillRR(g, gb.x + 1, gb.y + 1, 8, 8, RADIUS, fade(fg, p));
      // position: bar.beat.tick, 1-based like every DAW
      const beats = Math.max(0, position);
      const bar = Math.trunc(beats / beatsPerBar) + 1, beat = Math.trunc(beats % beatsPerBar) + 1;
      const tick = Math.trunc((beats - Math.floor(beats)) * 960);
      const buf = String(bar).padStart(3, " ") + "." + beat + "." + String(tick).padStart(3, "0");
      text(g, buf, r.x + r.w + 12, baseline(cy, 11), 11, FONT.mono, fg);
      // the tempo: a control (R-UI-13)
      const br = hitRect(kBpm);
      const bh = Math.max(hover.amount(kBpm), bpmDrag ? 1 : 0);
      if (bh > 0.001) fillRR(g, br.x, br.y, br.w, br.h, RADIUS, hoverWash(bh));
      const shownBpm = bpmDrag ? bpmLive : bpmShownA.value;
      text(g, printfG(Math.round(shownBpm * 10) / 10) + " bpm", br.x + 4, baseline(cy, 11), 11, FONT.mono, bpmDrag ? primary : lerpColor(muted, fg, bh));
    }

    // R-CTRL-1: a learn waiting — what for; a click cancels it
    if (learnA.value > 0.001) {
      const a = learnA.value, r = hitRect(kLearn), hv = hover.amount(kLearn);
      fillRR(g, r.x, r.y, r.w, r.h, RADIUS, primaryAlpha((0.14 + 0.08 * hv) * a));
      strokeRR(g, r.x, r.y, r.w, r.h, RADIUS, primaryAlpha(0.6 * a), 1);
      text(g, ellipsize(vm.fillIn(tr("MIDI learn · {1} — move a control"), learnShown), Math.max(0, r.w - 16), 10, FONT.sans),
        r.x + 8, baseline(cy, 10), 10, FONT.sans, fade(fg, a));
    }
    // R-PERF-1: the audio thread's load — muted, the destructive colour from 80 %
    {
      const r = cpuRect();
      const v = Math.max(0, cpuShown.value);
      const c = lerpColor(muted, destructive, clamp((v - 0.7) / 0.1, 0, 1));
      const pct = Math.round(v * 100) + "%";
      const pw = measure(pct, 10, FONT.mono);
      text(g, ellipsize(tr("CPU"), Math.max(0, r.w - pw - 6), 9, FONT.semibold, 0.13 * 9), r.x, r.y + 10, 9, FONT.semibold, muted, 0.13 * 9);
      text(g, pct, r.x + r.w - pw, r.y + 10, 10, FONT.mono, c);
      const trk = { x: r.x, y: r.y + r.h - 3, w: r.w, h: 2 };
      fillRR(g, trk.x, trk.y, trk.w, trk.h, PILL, whiteAlpha(0.06));
      if (v > 0) fillRR(g, trk.x, trk.y, trk.w * Math.min(1, v), trk.h, PILL, c);
    }
    // master meter, L over R
    {
      const s = hitRect(kSave);
      const m = { x: s.x - 14 - kMeterW, y: cy - 5, w: kMeterW, h: 10 };
      for (let c = 0; c < 2; ++c) {
        const trk = { x: m.x, y: m.y + c * 6, w: m.w, h: 4 };
        fillRR(g, trk.x, trk.y, trk.w, trk.h, RADIUS, whiteAlpha(0.06));
        const v = meter[c].value;
        if (trk.w * v >= 2 * RADIUS) {
          const col = v > 0.95 ? color("meter-high") : v > 0.8 ? color("meter-mid") : color("meter-low");
          fillRR(g, trk.x, trk.y, trk.w * v, trk.h, RADIUS, col);
        }
      }
    }
    button(kSave, tr("Save"), false);
  }

  // ------------------------------------------------------------------ R-VER: the chip's menu (SongBar::versionMenu)
  function versionMenu(at) {
    const items = [];
    const onMain = !version || version === "main";
    items.push({ label: "main" + (onMain ? "  · " + say("open") : ""), action: () => send("version open main") });
    const under = (base, indent) => {
      for (const v of versions) {
        if (v.base !== base) continue;
        let label = indent + v.name;
        if (v.frozen) label += "  · " + say("frozen");
        if (v.problem) label += "  · " + say("does not stand");
        if (v.id === version) label += "  · " + say("open");
        items.push({ label, action: () => send("version open " + v.id) });
        under(v.id, indent + "    ");
      }
    };
    under("main", "    ");
    const base = onMain ? "" : " --base " + version;
    items.push({ label: say("New Version…"), action: () => editText("", at, (n) => { if (n) send("version new " + quoted(n) + base + " --open"); }) });
    if (onMain) return items;
    const cur = versions.find((v) => v.id === version);
    if (!cur) return items;
    const id = cur.id, nm = cur.name;
    items.push({ label: cur.frozen ? say("Thaw") : say("Freeze"), action: () => send((cur.frozen ? "version thaw " : "version freeze ") + id) });
    items.push({ label: say("Rebase"), action: () => send("version rebase " + id) });
    items.push({ label: say("Rename…"), action: () => editText(nm, at, (n) => { if (n) send("set " + id + ".name=" + quoted(n)); }) });
    items.push({ label: say("Delete Version"), action: () => send("version delete " + id) });
    return items;
  }
  /** App's field (onEditText): the menu in rename mode */
  function editText(current, at, done) {
    if (ctx.menu && ctx.menu.openRename) ctx.menu.openRename(current, at.x, at.y, (v) => done(v == null ? "" : String(v)));
    else if (ctx.prompt) ctx.prompt({ value: current, x: at.x, y: at.y }).then((v) => done(v == null ? "" : v));
  }

  // ------------------------------------------------------------------ gestures (SongBar::handleGesture)
  const local = (e) => { const r = cv.el.getBoundingClientRect(), k = r.width / Math.max(1, W()); return { x: (e.clientX - r.left) / k, y: (e.clientY - r.top) / k }; };
  const origin = () => ctx.box.peek();
  const onMove = (e) => {
    const p = local(e);
    hovered = true;
    if (bpmDrag) {
      const step = e.shiftKey ? 0.1 : 1;
      const raw = bpmPress + ((bpmPressY - p.y) / kBpmStepPx) * step;
      const v = clamp(Math.round(raw / step) * step, 20, 999);
      const r = Math.round(v * 10) / 10;
      const buf = printfG(r);
      bpmLive = r;
      if (buf !== bpmSent && bpmLive !== bpm) { bpmSent = buf; send(tempoLine(buf)); } // one line per step that moved it
    } else hover.setHovered(hitAt(p), performance.now());
    const h = hitAt(p);
    cv.el.style.cursor = bpmDrag || h === kBpm ? "ns-resize" : h !== kNone && (h !== kLearn || learnA.value > 0.5) ? "pointer" : "";
    kick();
  };
  const onLeave = () => { hovered = false; kick(); };
  const onDown = (e) => {
    if (e.button !== 0) return;
    const p = local(e);
    if (hitAt(p) === kBpm) {
      bpmDrag = true; // the tempo, taken: the pointer's until let go
      bpmPress = bpmLive = bpm;
      bpmPressY = p.y;
      bpmSent = "";
      try { cv.el.setPointerCapture(e.pointerId); } catch {}
      kick();
    }
  };
  const onUp = () => { if (bpmDrag) { bpmDrag = false; kick(); } };
  const onClick = (e) => {
    if (e.button !== 0 || e.detail > 1) return; // a double click is not a click
    const p = local(e);
    switch (hitAt(p)) {
      case kHome: A.requestHome(); break;
      case kPlay: A.playToggle(); break;
      case kRecord: A.recordToggle(); break;
      case kSave: A.save(); break;
      case kSettings: A.openSettings(); break;
      case kLearn: send("map learn --cancel"); break; // R-CTRL-1
      case kVersion: { // R-VER: its menu drops from under the chip, not over it
        const c = hitRect(kVersion), o = origin();
        const at = { x: o.x + c.x, y: o.y + c.y + c.h + 2 };
        ctx.menu.open(versionMenu(at), at.x, at.y);
        break;
      }
      default: break;
    }
  };
  const onDbl = (e) => {
    const p = local(e);
    if (hitAt(p) !== kBpm) return;
    const o = origin();
    editText(printfG(bpm), { x: o.x + p.x, y: o.y + p.y }, (t) => { if (t) send(tempoLine(t)); }); // the service refuses what is not a tempo
  };
  cv.el.addEventListener("pointermove", onMove);
  cv.el.addEventListener("pointerleave", onLeave);
  cv.el.addEventListener("pointerdown", onDown);
  cv.el.addEventListener("pointerup", onUp);
  cv.el.addEventListener("pointercancel", onUp);
  cv.el.addEventListener("click", onClick);
  cv.el.addEventListener("dblclick", onDbl);

  // ------------------------------------------------------------------ the model, the menus, the size, the words
  own(host, effect(() => {
    const m = vm.model.value;
    ctx.box.value; vm.lang && vm.lang.value; colorsVersion.value;
    bind(m);
    const next = songMenus(vm, A), key = menusKey(next) + "\x1C" + (vm.lang ? vm.lang.peek() : "");
    if (key !== menuKey) {
      menus = next; menuKey = key;
      const sm = shownMenus(next);
      sm.forEach((mn, i) => { if (!strip.relabel(i, mn.title, mn.items.map((it) => it.label))) strip.setItems(i, mn.items); });
    }
    placeStrip();
    kick();
  }));
  cv.onResize(() => { placeStrip(); kick(); });

  // R-ACCESS-1: Home, Settings, the version, Record, Play, the tempo (a slider), Save
  if (vm.a11y) {
    own(host, vm.a11y.register(A11Y_ORDER.bar, () => {
      const o = origin();
      const at = (w) => { const r = hitRect(w); return { x: r.x + o.x, y: r.y + o.y, w: r.w, h: r.h }; };
      const out = [a11yNode("bar.home", "button", tr("Home"), at(kHome)), a11yNode("bar.settings", "button", tr("Settings"), at(kSettings))];
      if (chipW.value > 1) out.push(a11yNode("bar.version", "button", tr("Version"), at(kVersion), shown(chipTo)));
      const rec = a11yNode("bar.record", "toggle", tr("Record"), at(kRecord), recordingWanted ? tr("on") : tr("off"));
      rec.checked = recordingWanted;
      out.push(rec);
      out.push(a11yNode("bar.play", "button", playingWanted ? tr("Stop") : tr("Play"), at(kPlay)));
      const b = a11yNode("bar.tempo", "slider", tr("Tempo"), at(kBpm), printfG(Math.round(bpm * 10) / 10) + " bpm");
      b.step = (dir, fine) => {
        const step = fine ? 0.1 : 1, v = clamp(Math.round((bpm + dir * step) / step) * step, 20, 999);
        send(tempoLine(printfG(Math.round(v * 10) / 10)));
      };
      out.push(b);
      out.push(a11yNode("bar.save", "button", tr("Save"), at(kSave), dirty ? tr("unsaved") : ""));
      return out;
    }, { screen: "project" }));
  }

  own(host, () => {
    if (strip.destroy) strip.destroy();
    for (const [n, f] of [["pointermove", onMove], ["pointerleave", onLeave], ["pointerdown", onDown], ["pointerup", onUp],
      ["pointercancel", onUp], ["click", onClick], ["dblclick", onDbl]]) cv.el.removeEventListener(n, f);
    if (vm.view.menuOpen && strip.openIndex && strip.openIndex() >= 0) vm.view.menuOpen.value = false;
  });
  // a test's handles (SongBar.h)
  host.__songbar = { hitRect, cpuRect, playAmount: () => playingA.value, bpmShown: () => (bpmDrag ? bpmLive : bpmShownA.value), menus: () => strip };
}
