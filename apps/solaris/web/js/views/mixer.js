/*
 * Solaris — mixer.js: the strips (the window's MixerDock, a compact first version): every mixer a
 * page, every strip its colour, name, meter, fader (the window's law), gain, pan, mute and solo; the
 * master last. A fader in flight sends `set <ch>.gain=<dB>` as notifies and its release one answered
 * line, exactly the lines the window's fader sends (MixerDock.cpp:802) - one undo step per drag.
 */
import { keyed, bindEffect } from "../core/dom.js";
import { trackVar } from "../vm/song.js";
import { h, faderPos, faderDb, meterLevel, num, dbText, ease } from "./util.js";

function meter(cls = "") {
  const bars = [h("i"), h("i")];
  const el = h("div.meter" + cls, ...bars.map((b) => h("span", b)));
  // a meter's own eased copy: fast attack, a 300 ms fall (Theme.h motion::kMeterFallMs)
  const lv = [0, 0], shown = [0, 0];
  let last = performance.now(), attached = false;
  const step = (now) => {
    const dt = Math.min(100, now - last);
    last = now;
    for (let i = 0; i < 2; i++) {
      shown[i] = lv[i] >= shown[i] ? lv[i] : Math.max(lv[i], shown[i] - dt / 300);
      bars[i].style.clipPath = `inset(${((1 - shown[i]) * 100).toFixed(2)}% 0 0 0)`;
    }
    if (el.isConnected) attached = true;
    if (el.isConnected || !attached) requestAnimationFrame(step); // a removed strip's meter stops
  };
  requestAnimationFrame(step);
  return { el, set: (peaks) => { lv[0] = meterLevel((peaks && peaks[0]) || 0); lv[1] = meterLevel((peaks && peaks[1]) || 0); } };
}

/** One strip: `id` is a strip id or "master"; `field` writes `<id>.gain` or `project.masterGain`. */
function stripView(vm, id) {
  const isMaster = id === "master";
  const gainAddress = isMaster ? "project.masterGain" : `${id}.gain`;
  const m = meter();
  const fader = h("input.fader", { type: "range", min: 0, max: 1, step: 0.001, "aria-label": "gain" });
  const readout = h("span.db.mono");
  let dragging = false;
  const eased = ease((v) => { if (!dragging) fader.value = String(v); }, 220);
  fader.addEventListener("input", () => {
    dragging = true;
    const db = Math.round(faderDb(Number(fader.value)) * 10) / 10;
    readout.textContent = dbText(db);
    eased.jump(Number(fader.value)); // the pointer is the animation
    vm.session.notify(`set ${gainAddress}=${num(db, 1)}`);
  });
  fader.addEventListener("change", () => {
    const db = Math.round(faderDb(Number(fader.value)) * 10) / 10;
    dragging = false;
    vm.run(`set ${gainAddress}=${num(db, 1)}`).catch(() => {});
  });
  fader.addEventListener("dblclick", () => vm.run(`set ${gainAddress}=0`).catch(() => {}));

  const pan = h("input.pan", { type: "range", min: -1, max: 1, step: 0.01, "aria-label": "pan" });
  let panning = false;
  const panEased = ease((v) => { if (!panning) pan.value = String(v); }, 220);
  pan.addEventListener("input", () => {
    panning = true;
    let v = Number(pan.value);
    if (Math.abs(v) < 0.03) v = 0; // the centre catches (MixerDock)
    panEased.jump(v);
    vm.session.notify(`set ${id}.pan=${num(v, 2)}`);
  });
  pan.addEventListener("change", () => {
    let v = Number(pan.value);
    if (Math.abs(v) < 0.03) v = 0;
    panning = false;
    vm.run(`set ${id}.pan=${num(v, 2)}`).catch(() => {});
  });
  pan.addEventListener("dblclick", () => vm.run(`set ${id}.pan=0`).catch(() => {}));

  const mute = h("button.ms.mute", { title: "Mute" }, "M");
  const solo = h("button.ms.solo", { title: "Solo" }, "S");
  const name = h("div.strip-name");
  const kind = h("div.strip-kind.mono");
  const el = h("div.strip" + (isMaster ? ".master" : ""),
    h("i.strip-colour"), name, kind,
    h("div.fader-row", m.el, h("div.fader-wrap", fader)),
    readout,
    isMaster ? h("div.pan-space") : pan,
    isMaster ? h("div.ms-row") : h("div.ms-row", mute, solo));

  const update = (s) => {
    name.textContent = s.name;
    kind.textContent = isMaster ? "out" : `${s.kind} · ${s.id}`;
    el.querySelector(".strip-colour").style.background = isMaster ? "var(--primary)" : trackVar(s.colour);
    if (!dragging) {
      eased.set(faderPos(s.gain));
      readout.textContent = dbText(s.gain);
    }
    fader.disabled = !!s.gainFormula;
    fader.title = s.gainFormula ? `bound: ${s.gainFormula}` : `${gainAddress} — ${dbText(s.gain)} dB (double-click: 0 dB)`;
    if (!isMaster) {
      if (!panning) panEased.set(s.pan);
      pan.disabled = !!s.panFormula;
      mute.classList.toggle("on", !!s.mute);
      solo.classList.toggle("on", !!s.solo);
      mute.onclick = () => vm.toggle(id, "mute", s.mute);
      solo.onclick = () => vm.toggle(id, "solo", s.solo);
      el.classList.toggle("inaudible", s.audible === false);
    }
  };
  return { el, update, meter: m };
}

export function mixer(vm) {
  const pages = h("div.mixer-pages.scroll");
  const meters = new Map(); // strip id -> meter
  keyed(pages, () => vm.mixers.value, (mx) => mx.id, (mx) => {
    const strips = h("div.mixer-strips");
    const page = h("section.mixer-page", h("header.mixer-name"), strips);
    const views = new Map();
    keyed(strips, () => { const x = vm.mixers.value.find((y) => y.id === mx.id); return x ? x.strips : []; }, (s) => s.id, (s) => {
      const v = stripView(vm, s.id);
      views.set(s.id, v);
      meters.set(s.id, v.meter);
      return v.el;
    }, (el, s) => views.get(s.id).update(s));
    return page;
  }, (el, mx) => { el.querySelector(".mixer-name").textContent = mx.name; });

  const master = stripView(vm, "master");
  bindEffect(master.el, () => {
    const s = vm.song.value;
    master.update({ name: "Master", gain: s ? s.masterGain : 0 });
  });
  bindEffect(pages, () => {
    const t = vm.transport.value;
    for (const [id, m] of meters) m.set(t.peaks ? t.peaks[id] : null);
    master.meter.set(t.masterPeak);
  });
  return h("div.mixer", pages, h("div.master-wrap", master.el));
}
