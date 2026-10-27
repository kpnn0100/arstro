/*
 * Solaris — sheets/settings.js: the machine's settings (app/widgets/SettingsSheet.{h,cpp}; R-SET-1, R-SET-4..6, R-CTRL-1/2,
 * R-DEV-1/5, R-ACCESS-1/2).
 *
 * THE CHROME (SettingsSheet::onPaint / advance): the scrim and a 720×520 popover card (min(720, W − 32) × min(520, H − 48),
 * centred), fading in motion::kModalOpenMs and out kModalCloseMs, both EaseOutCubic, as ONE group; "Settings" 14 px
 * semibold and "this machine · never a song's"; a RAIL of the sections (164 wide, 30 px items 2 apart) whose highlight
 * (primary @ .16 and a 2 px accent) SLIDES to the chosen one (kSlideMs); the pages CROSS-FADE (kCrossFadeMs) and each
 * scrolls on its own (eased); Done under a hairline. A click on the scrim, Done, Escape or Enter closes it.
 *
 * THE PAGES: a setting is a 56 px row — its name (12) and note (10, muted) on the left, its control on the right: a
 * combo (sheets/settings/controls.js) or a toggle. Every control is ONE command line, the native's exactly:
 *   settings set <q(key=value)>          a combo's pick (q: quoted when it holds a space, a tab or a quote, or is empty)
 *   settings set <key>=on|off            a toggle
 *   settings set <q(port.<name>=<device>:<ch>)>   a port's device (R-DEV-1, the app's context menu)
 *   settings set <q(key.<id>=<combos>)>  a key chip re-bound (R-SET-4); Reset: settings set key.<id>=default …
 *   map remove <cc>                      a mapped control forgotten (R-CTRL-1)
 *   folder remove <q(path)> · folder add <path>   (the folder picker, as App's onAddFolder)
 * WEB-ONLY: the UI scale is the PAGE's (vm.setUiScale — per page, like Cosmo's), never `settings set uiScale`.
 */
import { signal, computed, effect, untracked, jsonEqual } from "../core/signal.js";
import { h, own } from "../core/dom.js";
import { useCss } from "../kit/css.js";
import { FONT, measure, ellipsize, ellipsizeFront, textAt } from "../kit/text.js";
import { comboOf, keyCode } from "../kit/keys.js";
import { UI_SCALES } from "../vm/app.js";
import { settingCombo, settingToggle, closeAllCombos, anyComboOpen, caretSvg } from "./settings/controls.js";

const CARD_W = 720, CARD_H = 520, RAIL_W = 164, PAD = 24, ROW_H = 56, COMBO_W = 220, CONTROL_H = 28;
const RAIL_ITEM_H = 30, HEADER_H = 60, FOOTER_H = 56, PAGE_TOP = 34, FOLDER_ROW_H = 26, CHIP_H = 26;
const KEY_ROW_GAP = 4, ROW_LABEL_GAP = 13, AREA_GAP = 22;

const LABELS = { output: "Output", input: "Input", sampleRate: "Sample rate", bufferSize: "Buffer", metronome: "Metronome",
  metronomeLevel: "Click level", auditionLevel: "Preview level", newBpm: "Tempo", newSig: "Meter", reducedMotion: "Reduced motion",
  autosave: "Autosave", driver: "Driver", midiInputs: "MIDI input", recordQuantize: "Record quantize", countIn: "Count-in",
  surface: "Control surface", surfacePort: "Surface port", uiScale: "UI scale", highContrast: "High contrast", language: "Language" };
const NOTES = { output: "the clock — every other device follows it", input: "where a recording comes from", sampleRate: "new songs start here",
  metronome: "clicks while playing — never in a render", metronomeLevel: "how loud the click is", auditionLevel: "a sample heard from the browser",
  newBpm: "a new song starts at this tempo", newSig: "… and in this meter",
  reducedMotion: "every tween collapses — the system's own setting counts too",
  autosave: "an unsaved song is kept beside it — recovered if Solaris stops",
  uiScale: "the whole window, drawn larger or smaller — it zooms there", highContrast: "white on black, solid borders, brighter quiet text",
  driver: "PipeWire for small buffers and counted xruns — PulseAudio if it will not open", midiInputs: "the keyboard an armed strip plays",
  recordQuantize: "where a recorded note's start lands", countIn: "the click before a recording, the song waiting",
  surface: "faders, pans, buttons and the transport on a Mackie Control or HUI surface",
  surfacePort: "its own port, in and out — the keyboard does not listen there",
  language: "the window's words — Automatic is the system's, when Solaris has it" };
const TITLES = ["Audio", "Playback", "New songs", "Sample folders", "Interface", "Keyboard", "MIDI"];
const AUDIO = 0, FOLDERS = 3, KEYBOARD = 5, MIDI = 6;
const PAGE_ROWS = [
  ["output", "input", "sampleRate", "bufferSize", "driver", "midiInputs"],
  ["metronome", "metronomeLevel", "auditionLevel", "recordQuantize", "countIn"],
  ["newBpm", "newSig"],
  [],
  ["language", "uiScale", "highContrast", "reducedMotion", "autosave"],
  [],
  ["surface", "surfacePort"],
];
const TOGGLES = new Set(["metronome", "reducedMotion", "highContrast"]);

/** SettingsSheet.cpp's q(): quoted when it holds a space, a tab or a quote, or is empty — no escaping. */
const q = (s) => (s !== "" && !/[ \t"]/.test(s) ? s : '"' + s + '"');
const num = (v) => String(+v);

export function mount(host, vm, ctx) {
  useCss("sheets/settings.css");
  const T = (en, ...a) => (a.length ? vm.fillIn(vm.tr(en), ...a) : vm.tr(en));
  const Tn = (n, one, other) => vm.fillIn(vm.trn(n, one, other), n);
  const send = (line) => vm.cmd(line);

  const root = h("div.ss-root.ss-still");              // the first placement: the rail and the pages do not travel
  const scrim = h("div.ss-scrim");
  const card = h("div.ss-card");
  const popLayer = h("div.ss-pop-layer");
  root.append(scrim, card, popLayer);
  host.append(root);
  const rootBox = () => {
    const r = root.getBoundingClientRect();
    const sc = root.offsetWidth ? r.width / root.offsetWidth : 1;
    return { rect: r, sc, H: root.offsetHeight };
  };

  // ------------------------------------------------------------------ the card's own paint
  const title = h("span.ss-title"), sub = h("span.ss-sub");
  const rail = h("div.ss-rail"), railHi = h("div.ss-rail-hi"), railBar = h("div.ss-rail-bar");
  const vline = h("div.ss-vline"), hline = h("div.ss-hline");
  const done = h("div.ss-done"), doneText = h("span");
  done.append(doneText);
  rail.append(railHi, railBar);
  card.append(title, sub, rail, vline, hline, done);
  const railItems = TITLES.map((_, s) => {
    const it = h("div.ss-rail-item");
    it.style.top = s * (RAIL_ITEM_H + 2) + "px";
    const t = h("span");
    it.append(t);
    it.addEventListener("click", (e) => { e.stopPropagation(); closeAllCombos(); showSection(s); });
    rail.append(it);
    return { it, t };
  });
  done.addEventListener("click", (e) => { e.stopPropagation(); vm.closeSheet(); });
  scrim.addEventListener("click", () => { closeAllCombos(); vm.closeSheet(); });
  card.addEventListener("click", () => { closeAllCombos(); listen.value = null; });

  // ------------------------------------------------------------------ the pages
  const nv = vm.nativeView && vm.nativeView.peek();
  const first = nv && Number.isInteger(nv.settingsSection) && nv.settingsSection >= 0 && nv.settingsSection < TITLES.length ? nv.settingsSection : AUDIO;
  const section = signal(first);
  const listen = signal(null, (a, b) => JSON.stringify(a) === JSON.stringify(b));   // {action, slot} a key chip listening
  const pages = TITLES.map((_, s) => {
    const el = h("div.ss-page"), inner = h("div.ss-page-in"), texts = h("div.ss-texts"), dyn = h("div.ss-dyn");
    const bar = h("div.ss-bar");
    inner.append(texts, dyn);
    el.append(inner, bar);
    card.append(el);
    const p = { el, inner, texts, dyn, bar, scroll: 0, contentH: 0, h: 0, w: 0 };
    el.addEventListener("wheel", (e) => {
      if (s !== section.peek()) return;
      e.preventDefault();
      closeAllCombos();
      const max = Math.max(0, p.contentH - p.h);
      p.scroll = Math.max(0, Math.min(max, p.scroll + e.deltaY));
      inner.style.transform = `translateY(${-p.scroll}px)`;
      placeBar(p);
    }, { passive: false });
    return p;
  });
  // EasedScroll::drawBar: a 3 px thumb, white @ .14, only while the page scrolls
  function placeBar(p) {
    const max = Math.max(0, p.contentH - p.h);
    p.bar.style.display = max > 0.5 && p.h > 0 ? "" : "none";
    if (!(max > 0.5)) return;
    const th = Math.max(18, (p.h * p.h) / p.contentH);
    Object.assign(p.bar.style, { height: th + "px", transform: `translateY(${(p.h - th) * (p.scroll / max)}px)` });
  }
  function showSection(s) {
    if (s === section.peek()) return;
    closeAllCombos();
    listen.value = null;
    section.value = s;
  }
  own(host, effect(() => {
    const s = section.value;
    pages.forEach((p, i) => p.el.classList.toggle("on", i === s));
    railItems.forEach((r, i) => r.it.classList.toggle("on", i === s));
    railHi.style.transform = railBar.style.transform = `translateY(${s * (RAIL_ITEM_H + 2)}px)`;
  }));

  // the fixed rows' controls — made once, so a knob or a label eases when the model moves it
  const controls = {};
  const chips = {};      // key → [{label, value}] what each combo shows / sends
  PAGE_ROWS.forEach((rows, s) => rows.forEach((key, i) => {
    let c;
    if (TOGGLES.has(key)) c = settingToggle({ onChange: (on) => send(`settings set ${key}=${on ? "on" : "off"}`) });
    else c = settingCombo({ w: COMBO_W, h: CONTROL_H, layer: popLayer, rootBox, onPick: (j) => {
      const ch = (chips[key] || [])[j];
      if (!ch) return;
      if (key === "uiScale") vm.setUiScale(+ch.value);                    // WEB-ONLY: the page's own scale
      else send("settings set " + q(key + "=" + ch.value));
    } });
    c.el.classList.add("ss-control");
    c.el.style.top = PAGE_TOP + i * ROW_H + (ROW_H - (TOGGLES.has(key) ? 22 : CONTROL_H)) * 0.5 + "px";
    pages[s].inner.append(c.el);
    controls[key] = c;
  }));
  own(host, () => { for (const k in controls) if (controls[k].dispose) controls[k].dispose(); });

  // ------------------------------------------------------------------ the model, as the sheet binds it
  const data = computed(() => {
    const m = vm.model.value;
    if (!m) return null;
    const tr = m.transport || {};
    return { settings: m.settings || {}, surface: m.surface || {}, devices: m.devices || [], midiDevices: m.midiDevices || [],
             ports: m.ports || [], playing: !!tr.playing, tdevices: tr.playing ? tr.devices || [] : [] };
  }, jsonEqual);

  function current(key, s) {
    switch (key) {
      case "metronome": case "reducedMotion": case "highContrast": return s[key] ? "on" : "off";
      case "metronomeLevel": case "auditionLevel": case "newBpm": return num(s[key] ?? 0);
      case "autosave": return s.autosave > 0 ? String(s.autosave) : "off";
      case "uiScale": return String(vm.view.uiScale.value);
      case "sampleRate": case "bufferSize": case "countIn": return String(s[key] ?? 0);
      default: return s[key] == null ? "" : String(s[key]);
    }
  }
  function chipsOf(key, d) {
    const s = d.settings, say = T;
    switch (key) {
      case "output": case "input": {
        const dir = key === "output" ? "out" : "in";
        return [{ label: say("System default"), value: "" }, ...d.devices.filter((x) => x.dir === dir).map((x) => ({ label: x.name, value: x.id }))];
      }
      case "sampleRate": return [44100, 48000, 88200, 96000].map((r) => ({ label: `${r / 1000} kHz`, value: String(r) }));
      case "bufferSize": return [64, 128, 256, 512, 1024, 2048].map((b) => ({ label: say("{1} samples", b), value: String(b) }));
      case "metronomeLevel": case "auditionLevel": return [-18, -12, -6, 0].map((v) => ({ label: `${v} dB`, value: String(v) }));
      case "newBpm": return [100, 120, 124, 126, 128, 140, 150, 174].map((b) => ({ label: `${b} bpm`, value: String(b) }));
      case "newSig": return ["4/4", "3/4", "6/8", "7/8"].map((m) => ({ label: m, value: m }));
      case "driver": return [{ label: "PulseAudio", value: "pulse" }, { label: "PipeWire — " + say("low latency"), value: "pipewire" }];
      case "midiInputs": return [{ label: say("Every MIDI input"), value: "all" }, { label: say("None"), value: "none" },
        ...d.midiDevices.map((x) => ({ label: x.name, value: x.id }))];
      case "surface": return [{ label: say("None"), value: "none" }, { label: "Mackie Control", value: "mcu" }, { label: "HUI", value: "hui" }];
      case "surfacePort": return [{ label: say("None"), value: "" }, ...d.midiDevices.map((x) => ({ label: x.name, value: x.id }))];
      case "recordQuantize": return [{ label: say("Off"), value: "off" }, ...["1/4", "1/8", "1/16", "1/32"].map((v) => ({ label: v, value: v }))];
      case "countIn": return [{ label: say("None"), value: "0" }, ...[1, 2, 4].map((b) => ({ label: Tn(b, "{1} bar", "{1} bars"), value: String(b) }))];
      case "uiScale": return UI_SCALES.map((u) => ({ label: `${u} %`, value: String(u) }));
      case "autosave": return [{ label: say("Off"), value: "off" }, { label: say("Every 30 s"), value: "30" }, { label: say("Every minute"), value: "60" },
        { label: say("Every 2 minutes"), value: "120" }, { label: say("Every 5 minutes"), value: "300" }];
      case "language": return [{ label: say("Automatic"), value: "auto" }, ...(s.languages || []).map((l) => ({ label: l.name, value: l.code }))];
      default: return [];
    }
  }
  function rowNote(key, d) {
    if (key === "bufferSize") return T("{1} ms of latency", (+d.settings.latencyMs || 0).toFixed(1));
    if (key === "surface" && d.surface.protocol && d.surface.protocol !== "none")
      return d.surface.connected ? T("connected · strips {1}–{2}", (d.surface.bank || 0) + 1, (d.surface.bank || 0) + 8)
                                 : T("not connected — {1}", d.surface.why || "");
    return NOTES[key] ? T(NOTES[key]) : "";
  }

  // ------------------------------------------------------------------ layout + paint (the box, the language, the model)
  const txt = (parent, s, x, base, px, fam, cls) => {
    const e = h("span" + (cls ? "." + cls : ""));
    e.textContent = s;
    textAt(e, x, base, px, fam);
    parent.append(e);
    return e;
  };
  const hrule = (parent, y, w, cls = "ss-rule") => { const e = h("div." + cls); Object.assign(e.style, { top: y + "px", width: w + "px" }); parent.append(e); return e; };
  const xBtn = (parent, x, y, onClick) => {
    const b = h("div.ss-x");
    Object.assign(b.style, { left: x + "px", top: y + "px" });
    b.addEventListener("click", (e) => { e.stopPropagation(); closeAllCombos(); onClick(); });
    parent.append(b);
    return b;
  };
  const micro = (parent, head, rest, y) => {
    const up = vm.upper(head);
    txt(parent, up, 0, y, 9, FONT.semibold, "ss-micro").style.letterSpacing = 0.13 * 9 + "px";
    txt(parent, rest, measure(up, 9, FONT.semibold, 0.13 * 9) + 10, y, 10, FONT.sans, "ss-muted");
  };

  own(host, effect(() => {
    const box = ctx.box.value, d = data.value, sec = section.value, lis = listen.value;
    vm.view.uiScale.value;
    vm.tr("Settings");                                       // the language drawn: words re-drawn when it changes
    untracked(() => paint(box, d, sec, lis));
  }));

  function paint(box, d, sec, lis) {
    const W = box.w, H = box.h;
    const cw = Math.min(CARD_W, W - 32), ch = Math.min(CARD_H, H - 48);
    Object.assign(card.style, { left: Math.floor((W - cw) * 0.5) + "px", top: Math.floor((H - ch) * 0.5) + "px", width: cw + "px", height: ch + "px" });
    title.textContent = T("Settings");
    textAt(title, PAD, 34, 14, FONT.semibold);
    sub.textContent = T("this machine · never a song's");
    textAt(sub, PAD + measure(title.textContent, 14, FONT.semibold) + 10, 34, 10, FONT.sans);
    Object.assign(rail.style, { left: "12px", top: HEADER_H + "px", width: RAIL_W + "px" });
    railItems.forEach((r, s) => { r.t.textContent = ellipsize(T(TITLES[s]), RAIL_W - 20, 12, FONT.sans); textAt(r.t, 14, RAIL_ITEM_H * 0.5 + 12 * 0.35, 12, FONT.sans); });
    const sx = 12 + RAIL_W + 12;
    Object.assign(vline.style, { left: sx + "px", top: HEADER_H - 4 + "px", height: Math.max(0, ch - FOOTER_H - (HEADER_H - 4)) + "px" });
    Object.assign(hline.style, { top: ch - FOOTER_H + "px" });
    Object.assign(done.style, { left: cw - PAD - 88 + "px", top: ch - FOOTER_H + (FOOTER_H - 30) * 0.5 + "px" });
    doneText.textContent = ellipsize(T("Done"), 88 - 12, 12, FONT.medium);
    textAt(doneText, (88 - measure(doneText.textContent, 12, FONT.medium)) * 0.5, 15 + 12 * 0.35, 12, FONT.medium);
    const px = 12 + RAIL_W + 24, pw = Math.max(0, cw - PAD - px), ph = Math.max(0, ch - FOOTER_H - 8 - HEADER_H);
    pages.forEach((p, s) => {
      Object.assign(p.el.style, { left: px + "px", top: HEADER_H + "px", width: pw + "px", height: ph + "px" });
      p.w = pw - 8; p.h = ph;
      p.texts.replaceChildren();
      p.dyn.replaceChildren();
      txt(p.texts, T(TITLES[s]), 0, 16, 14, FONT.semibold);
      if (!d) return;
      paintRows(p, s, d);
      if (s === AUDIO) paintPorts(p, d);
      else if (s === MIDI) paintMidi(p, d);
      else if (s === FOLDERS) paintFolders(p, d);
      else if (s === KEYBOARD) paintKeyboard(p, d, lis);
      const max = Math.max(0, p.contentH - p.h);
      if (p.scroll > max) { p.scroll = max; p.inner.style.transform = `translateY(${-p.scroll}px)`; }
      placeBar(p);
    });
  }

  function paintRows(p, s, d) {
    const rows = PAGE_ROWS[s];
    rows.forEach((key, i) => {
      const top = PAGE_TOP + i * ROW_H, tog = TOGGLES.has(key);
      const room = p.w - (tog ? 40 : COMBO_W) - 20;
      txt(p.texts, ellipsize(T(LABELS[key]), room, 12, FONT.sans), 0, top + 23, 12, FONT.sans);
      txt(p.texts, ellipsize(rowNote(key, d), room, 10, FONT.sans), 0, top + 39, 10, FONT.sans, "ss-muted");
      if (i + 1 < rows.length) hrule(p.texts, top + ROW_H, p.w);
      const c = controls[key];
      c.el.style.left = p.w + 8 - 8 - (tog ? 40 : COMBO_W) + "px";
      const cur = current(key, d.settings);
      if (tog) { c.set(cur === "on"); return; }
      // a value the list lacks is an entry of its own, so the setting is never invisible (R-SET-6)
      const list = chipsOf(key, d);
      let sel = list.findIndex((x) => x.value === cur);
      if (sel < 0) {
        let label = cur;
        if (key === "output" || key === "input") label += " " + T("(not connected)");
        else if (key === "newBpm") label += " bpm " + T("(set elsewhere)");
        else if (key === "metronomeLevel" || key === "auditionLevel") label += " dB " + T("(set elsewhere)");
        else label += " " + T("(set elsewhere)");
        list.push({ label, value: cur });
        sel = list.length - 1;
      }
      chips[key] = list;
      c.set(list.map((x) => x.label), sel);
    });
    p.contentH = PAGE_TOP + rows.length * ROW_H + 8;
  }

  // R-DEV-1/5: under the devices, the open song's ports — each on its device, and how that device is
  function paintPorts(p, d) {
    const s = d.settings, w = p.w;
    let fy = PAGE_TOP + PAGE_ROWS[AUDIO].length * ROW_H + 14;
    micro(p.dyn, T("Ports"), T("where this song's ports play on this machine"), fy);
    fy += 8;
    if (!d.ports.length) {
      txt(p.dyn, ellipsize(T("No song open — its ports are listed here."), w, 11, FONT.sans), 0, fy + FOLDER_ROW_H * 0.5 + 11 * 0.35, 11, FONT.sans, "ss-quiet");
      fy += FOLDER_ROW_H;
    }
    d.ports.forEach((pm, i) => {
      let device = "", channel = 0;
      for (const kv of s.ports || []) {
        const eq = kv.indexOf("=");
        if (eq < 0 || kv.slice(0, eq) !== pm.name) continue;
        const v = kv.slice(eq + 1), colon = v.lastIndexOf(":");
        device = colon < 0 ? v : v.slice(0, colon);
        channel = colon < 0 ? 0 : parseInt(v.slice(colon + 1), 10) || 0;
      }
      const out = pm.dir !== "in";
      const devName = (id) => { const x = d.devices.find((y) => y.id === id); return x ? x.name : id + " " + T("(not connected)"); };
      const label = device ? devName(device) : out ? T("Clock device") : T("Default input");
      const own_ = out ? !device || device === s.output : !device || device === s.input;
      let note = "";
      if (d.playing && d.tdevices.length) {
        let dv = null;
        for (const x of d.tdevices) if ((own_ && out && x.role === "clock") || (!own_ && x.id === device)) dv = x;
        const ms = (v) => (+v || 0).toFixed(1) + " ms";
        if (dv && dv.problem) note = T("silent — {1}", dv.problem);
        else if (dv) {
          const drift = ((dv.driftPpm >= 0 ? "+" : "") + (+dv.driftPpm || 0).toFixed(2)) + " ppm";
          note = dv.role === "clock" ? T("the clock") + " · " + ms(dv.latencyMs) : T("held to the clock") + " · " + ms(dv.latencyMs) + " · " + drift;
          if (dv.xruns >= 0) note += " · " + Tn(dv.xruns, "{1} xrun", "{1} xruns");
        }
      }
      if (!note) {
        if (out) note = own_ ? T("plays on the clock device") : T("plays on {1} from channel {2}, held to the clock", device ? label : "", channel + 1);
        else note = own_ ? T("records beside the clock") : T("records from {1}, held to the clock", device ? label : "");
      }
      const room = w - COMBO_W - 20;
      const name = ellipsize(pm.name, room * 0.6, 12, FONT.sans);
      txt(p.dyn, name, 0, fy + 23, 12, FONT.sans);
      txt(p.dyn, out ? vm.trc("port", "out") : vm.trc("port", "in"), measure(name, 12, FONT.sans) + 8, fy + 23, 10, FONT.sans, "ss-muted");
      txt(p.dyn, ellipsize(note, room, 10, FONT.sans), 0, fy + 39, 10, FONT.sans, "ss-muted");
      const f = h("div.ss-combo.ss-portfield");
      Object.assign(f.style, { left: w - COMBO_W + "px", top: fy + (ROW_H - CONTROL_H) * 0.5 + "px", width: COMBO_W + "px", height: CONTROL_H + "px" });
      const ft = h("span.ss-combo-text");
      ft.textContent = ellipsize(label, COMBO_W - 36, 11, FONT.sans);
      textAt(ft, 10, CONTROL_H * 0.5 + 11 * 0.35, 11, FONT.sans);
      f.append(ft, caretSvg(COMBO_W - 22, (CONTROL_H - 10) * 0.5));
      f.addEventListener("click", (e) => {
        e.stopPropagation();
        closeAllCombos();
        // R-DEV-1: the clock (or the default input) and every device of its direction — a pick is one line
        const line = (v) => send("settings set " + q("port." + pm.name + "=" + v));
        const items = [{ label: (out ? T("Clock device") : T("Default input")) + (device ? "" : "  · " + T("now")), action: () => line("") }];
        for (const x of d.devices) if (x.dir === (out ? "out" : "in"))
          items.push({ label: x.name + (x.id === device ? "  · " + T("now") : ""), action: () => line(x.id + ":" + channel) });
        const r = f.getBoundingClientRect(), b = rootBox();
        ctx.menu.open(items, (r.left - b.rect.left) / b.sc, (r.bottom - b.rect.top) / b.sc + 2);
      });
      p.dyn.append(f);
      if (i + 1 < d.ports.length) hrule(p.dyn, fy + ROW_H, w);
      fy += ROW_H;
    });
    p.contentH = fy + 12;
  }

  // R-CTRL-1: each MIDI control and the number it moves — one the open song lacks dimmed, saying so; × forgets it
  function paintMidi(p, d) {
    const w = p.w, maps = d.settings.mappings || [];
    let fy = PAGE_TOP + PAGE_ROWS[MIDI].length * ROW_H + 14;
    micro(p.dyn, T("Mapped controls"), T("controls and what they move"), fy);
    fy += 8;
    if (!maps.length) {
      txt(p.dyn, ellipsize(T("None yet — right-click a number › Learn MIDI CC, and move a control."), w, 11, FONT.sans), 0,
          fy + FOLDER_ROW_H * 0.5 + 11 * 0.35, 11, FONT.sans, "ss-quiet");
      fy += FOLDER_ROW_H;
    }
    maps.forEach((mp, i) => {
      const base = fy + FOLDER_ROW_H * 0.5 + 10 * 0.35, rmx = w - 22;
      txt(p.dyn, "CC " + mp.cc, 0, base, 10, FONT.mono);
      const what = mp.address + (mp.inert ? "  · " + T("not in this song") : "");
      txt(p.dyn, ellipsize(what, Math.max(0, rmx - 8 - 72), 10, FONT.mono), 72, base, 10, FONT.mono, mp.inert ? "ss-muted" : "");
      xBtn(p.dyn, rmx, fy + (FOLDER_ROW_H - 18) * 0.5, () => send("map remove " + mp.cc));
      if (i + 1 < maps.length) hrule(p.dyn, fy + FOLDER_ROW_H, w);
      fy += FOLDER_ROW_H;
    });
    p.contentH = fy + 12;
  }

  function paintFolders(p, d) {
    const w = p.w, folders = d.settings.folders || [];
    txt(p.dyn, T("the browser lists these first"), measure(T(TITLES[FOLDERS]), 14, FONT.semibold) + 10, 16, 10, FONT.sans, "ss-muted");
    let fy = PAGE_TOP;
    if (!folders.length) {
      txt(p.dyn, ellipsize(T("None yet — add the folders you keep samples in."), w, 11, FONT.sans), 0, fy + FOLDER_ROW_H * 0.5 + 11 * 0.35, 11, FONT.sans, "ss-quiet");
      fy += FOLDER_ROW_H;
    }
    folders.forEach((f, i) => {
      if (i + 1 < folders.length) hrule(p.dyn, fy + FOLDER_ROW_H, w);
      txt(p.dyn, ellipsizeFront(f, w - 34, 10, FONT.mono), 0, fy + FOLDER_ROW_H * 0.5 + 10 * 0.35, 10, FONT.mono);
      xBtn(p.dyn, w - 22, fy + (FOLDER_ROW_H - 18) * 0.5, () => send("folder remove " + q(f)));
      fy += FOLDER_ROW_H;
    });
    const lbl = T("Add folder…"), bw = measure(lbl, 11, FONT.sans) + 40;
    const add = h("div.ss-addchip");
    Object.assign(add.style, { left: "0px", top: fy + 8 + "px", width: bw + "px", height: CHIP_H + "px" });
    add.append(h("span.ss-plus"));
    txt(add, lbl, 28, CHIP_H * 0.5 + 11 * 0.35, 11, FONT.sans);
    add.addEventListener("click", async (e) => {
      e.stopPropagation();
      closeAllCombos();
      const path = await ctx.pick({ mode: "folder", title: T("Add folder…") });
      if (path) send("folder add " + vm.q(path));
    });
    p.dyn.append(add);
    p.contentH = fy + 8 + CHIP_H + 12;
  }

  // R-SET-4: an area's micro-label, then an action a line — its label, its keys as chips, "+" at the right edge
  function paintKeyboard(p, d, lis) {
    const w = p.w, keys = d.settings.keys || [];
    const changed = keys.some((k) => JSON.stringify(k.keys) !== JSON.stringify(k.defaults));
    const rl = T("Reset to defaults"), rw = measure(rl, 11, FONT.sans) + 22;
    const reset = h("div.ss-reset" + (changed ? ".live" : ""));
    Object.assign(reset.style, { left: w - rw + "px", top: "1px", width: rw + "px", height: "22px" });
    txt(reset, rl, 11, 11 + 11 * 0.35, 11, FONT.sans);
    reset.addEventListener("click", (e) => {
      e.stopPropagation();
      listen.value = null;
      let line = "";
      for (const k of keys) if (JSON.stringify(k.keys) !== JSON.stringify(k.defaults)) line += " key." + k.id + "=default";
      if (line) send("settings set" + line);                 // ONE line, every changed action
    });
    p.dyn.append(reset);
    const nx = measure(T(TITLES[KEYBOARD]), 14, FONT.semibold) + 10;
    txt(p.dyn, ellipsize(T("click a key and press the new one · Backspace removes it"), Math.max(0, w - rw - nx - 10), 10, FONT.sans), nx, 16, 10, FONT.sans, "ss-quiet");
    const listening = T("Press a key…"), wListen = measure(listening, 11, FONT.sans) + 18;
    let y = PAGE_TOP, area = "";
    keys.forEach((k) => {
      if (k.area !== area) {
        if (area) y += AREA_GAP - KEY_ROW_GAP;
        area = k.area;
        txt(p.dyn, vm.upper(area), 0, y + 4, 9, FONT.semibold, "ss-micro").style.letterSpacing = 0.13 * 9 + "px";
        y += ROW_LABEL_GAP;
      }
      const top = y;
      y += CHIP_H + KEY_ROW_GAP;
      const addX = w - CHIP_H;
      const slots = (k.keys || []).map((c, i) => ({ combo: c, slot: i }));
      const lisHere = lis && lis.action === k.id;
      if (lisHere && lis.slot >= slots.length) slots.push({ combo: "", slot: slots.length });
      const widths = slots.map((x) => (lisHere && lis.slot === x.slot ? wListen : measure(x.combo, 11, FONT.sans) + 18));
      const total = widths.reduce((a, b) => a + b + 6, 0);
      let x = addX - total;
      slots.forEach((sl, i) => {
        const on = lisHere && lis.slot === sl.slot;
        const c = h("div.ss-keychip" + (on ? ".listen" : ""));
        Object.assign(c.style, { left: x + "px", top: top + "px", width: widths[i] + "px", height: CHIP_H + "px" });
        const s = on ? listening : sl.combo;
        txt(c, s, (widths[i] - measure(s, 11, FONT.sans)) * 0.5, CHIP_H * 0.5 + 11 * 0.35, 11, FONT.sans);
        c.addEventListener("click", (e) => { e.stopPropagation(); closeAllCombos(); listen.value = { action: k.id, slot: sl.slot }; });
        p.dyn.append(c);
        x += widths[i] + 6;
      });
      const add = h("div.ss-keyadd");
      Object.assign(add.style, { left: addX + "px", top: top + "px" });
      add.append(h("span.ss-plus"));
      add.addEventListener("click", (e) => { e.stopPropagation(); closeAllCombos(); listen.value = { action: k.id, slot: (k.keys || []).length }; });
      p.dyn.append(add);
      if (JSON.stringify(k.keys) !== JSON.stringify(k.defaults)) {
        const dot = h("div.ss-dot");
        dot.style.top = top + CHIP_H * 0.5 - 2.5 + "px";
        p.dyn.append(dot);
      }
      txt(p.dyn, ellipsize(k.label, Math.max(0, addX - total - 12 - 12), 11, FONT.sans), 12, top + CHIP_H * 0.5 + 11 * 0.35, 11, FONT.sans);
    });
    p.contentH = y + 8;
  }

  // ------------------------------------------------------------------ the keys (SettingsSheet::handleKey)
  const onKey = (e) => {
    if (!ctx.open.peek() || e.defaultPrevented) return;
    const lis = listen.peek();
    if (lis) {
      e.preventDefault();
      const s = data.peek() && data.peek().settings;
      const km = s && (s.keys || []).find((k) => k.id === lis.action);
      if (!km) { listen.value = null; return; }
      const bare = !e.ctrlKey && !e.metaKey && !e.altKey && !e.shiftKey, code = keyCode(e);
      const keys = (km.keys || []).slice(), slot = Math.max(0, lis.slot);
      if (bare && code === 27) { listen.value = null; return; }
      if (bare && code === 8) {
        if (slot >= keys.length) { listen.value = null; return; }
        keys.splice(slot, 1);
      } else {
        const combo = comboOf(e);
        if (!combo) return;                                   // a key no combo can name: still listening
        if (slot < keys.length) keys[slot] = combo; else keys.push(combo);
      }
      listen.value = null;
      send("settings set " + q("key." + km.id + "=" + (keys.length ? keys.join(", ") : "none")));
      return;
    }
    if (e.key === "Escape" && anyComboOpen()) { e.preventDefault(); closeAllCombos(); return; }
    if (e.key === "Escape" || e.key === "Enter") { e.preventDefault(); vm.closeSheet(); }
  };
  window.addEventListener("keydown", onKey);
  own(host, () => window.removeEventListener("keydown", onKey));

  // ------------------------------------------------------------------ open / close: the whole sheet fades as one group
  requestAnimationFrame(() => requestAnimationFrame(() => { root.classList.remove("ss-still"); if (ctx.open.peek()) root.classList.add("shown"); }));
  own(host, effect(() => {
    if (!ctx.open.value) { closeAllCombos(); untracked(() => { listen.value = null; }); root.classList.remove("shown"); }
  }));
}
