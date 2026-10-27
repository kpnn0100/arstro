/*
 * Solaris — device/panel.js: DevicePanel (widgets/DevicePanel.{h,cpp}, R-UI-5, R-WIN-1…3, R-PRESET-1) — one device's
 * parameters, GENERATED from the model's registry: the content of its float window (window kind `device`, key
 * `dev:<dv>`; the window layer mounts this with ctx.window = {key, id, …}).
 *
 * HEADER (kHeaderH 35.75, a 1 px border under it): "<owner> · <dv> · <type>" in 10 px mono, muted, ellipsized before
 *   Presets; at the right, each 55.25 × 19.5 at y 8.125, 6.5 apart, radius control, 10 px sans centred:
 *     Presets   secondary → popover on hover   a menu of its type's presets (`preset load <dv> "<name>"`) and
 *               "Save as preset…" (the rename field → `preset save <dv> "<name>"`). A plugin: "Editor" in its place
 *               (`device editor <dv>[ --close]`), lit primary while open.
 *     On/Bypassed  primary ↔ secondary, eased kSelectMs (whoever changed it)   `set <dv>.bypass=true|false`
 *     Remove    an effect: hover wash, its text easing to destructive         `device remove <dv>`
 *     Piano Roll  an instrument (71.5 wide), in Remove's place: the strip's pattern; a menu when it plays several; with
 *               none `clip add --strip <ch>` and the pattern it makes is opened when the model brings it.
 * BODY (from bodyTop, scrolled, eased kScrollMs): a row per parameter (kRowStep 22.75, from 6.5), grouped by the name's
 *   prefix under a section header (kSectionH 26: 9 px SemiBold, tracked .13 em, muted, upper case):
 *     a number   cosmo's SliderRow: label 10 px sans muted (86 wide) · the slider (track 35 % of 28, pill, secondary;
 *                fill primary; thumb white r 4.5, 6.5 on hover) · the value 10 px mono muted, right-aligned in kValueW 91
 *                (ParamFormat's readout — kit/format.js paramfmt — or what decides it: `auto au_1`, `= ch_2.gain`).
 *                A log-tapered parameter moves in ratios, an integer in whole steps. Drag: `set <dv>.<p>=<storeText>`
 *                through vm.note each move, the last through vm.cmd on release; a double-click: the default (vm.cmd).
 *                While a drag is in flight the model does not re-seed the sliders.
 *     a choice   a box (secondary) after the label column, its name centred, a chevron each side: a click on the left
 *                half steps back, the right half forward (wrapping) — `set <dv>.<p>=<name>`.
 *   The row the model changed LAST (device.lastChanged) is lit (primary 12 % + a 2 px bar), eased kCrossFadeMs.
 *   A right-click on a row: the shared parameter menu (mixer/parammenu.js). View › Show IDs fades each row's address in
 *   over its label (vm.view.idsAmount). The body's scroll bar shows only when there is somewhere to scroll.
 * NOT PORTED: the analog face (AnalogFace, R-UI-12 — an instrument shows its list here), the readings band (R-FX-17),
 *   a plugin's offline band, the sample drop outline.
 */
import { effect, computed, jsonEqual, untracked } from "../core/signal.js";
import { own } from "../core/dom.js";
import { paramfmt } from "../kit/format.js";
import { FONT, ellipsize, measure } from "../kit/text.js";
import { useCss } from "../kit/css.js";
import { bindingText, openParamMenu, paramTarget } from "../mixer/parammenu.js";

const kHeaderH = 35.75, kValueW = 91, kRowStep = 22.75, kSectionH = 26;
const PADX = 9.75, LABEL_W = 86, GAP = 8.125, ROW_H = 20, TRACK_H = 28 * 0.35;
const BTN_W = 55.25, ROLL_W = 71.5, BTN_H = 19.5, BTN_Y = 8.125;
const K = { sans: 0.3418, mono: 0.36 }; // kit/text.js textAt's ascent rule

/** A click as the native's Click gesture: a press and its release on the same element (the window layer may raise —
 *  re-insert — the window between the two, which loses the DOM's own `click`). */
function tap(e, fn) {
  let down = false;
  e.addEventListener("pointerdown", (ev) => { down = ev.button === 0; });
  e.addEventListener("pointerup", (ev) => { if (down && ev.button === 0) { down = false; fn(ev); } });
  e.addEventListener("pointerleave", () => { down = false; });
}
function el(tag, cls, parent) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (parent) parent.appendChild(e);
  return e;
}
/** Place `e`'s text so its alphabetic baseline lands at `base` (drawText's). */
function atBase(e, base, px, mono) { e.style.top = base - px * (0.5 + (mono ? K.mono : K.sans)) + "px"; e.style.fontSize = px + "px"; e.style.lineHeight = px + "px"; }
const centreBase = (cy, px) => cy + px * 0.35; // TextFit::baseline
const sectionOf = (name) => { const i = name.indexOf("."); return i < 0 ? "" : name.slice(0, i).replace(/[a-z]/g, (c) => c.toUpperCase()); };

function findDevice(m, dv) {
  if (!m) return null;
  for (const s of m.strips || []) for (const d of s.devices || []) if (d.id === dv) return { d, strip: s };
  for (const d of m.masterDevices || []) if (d.id === dv) return { d, strip: null };
  return null;
}

export function mount(host, vm, ctx) {
  useCss("device/panel.css");
  const win = ctx.window || {};
  let dv = win.id || String(win.key || "").replace(/^dev:/, "");
  if (!dv) { const w = (vm.view.windows.peek() || []).filter((x) => x.kind === "device").pop(); dv = w ? w.id : ""; }

  const root = el("div", "dp", host);
  const head = el("div", "dp-head", root);
  const what = el("div", "dp-what", head);
  atBase(what, centreBase(BTN_Y + BTN_H / 2, 10), 10, true);
  const bPresets = el("button", "dp-btn dp-presets", head);
  const bBypass = el("button", "dp-btn dp-bypass", head);
  const bEnd = el("button", "dp-btn", head); // Remove, or an instrument's Piano Roll
  for (const b of [bPresets, bBypass, bEnd]) { b.type = "button"; b.tabIndex = -1; }
  const body = el("div", "dp-body", root);
  body.style.top = kHeaderH + "px";
  const content = el("div", "dp-content", body);
  const ids = el("div", "dp-ids", content);
  const bar = el("div", "dp-bar", body);

  const model = computed(() => vm.model.value, jsonEqual);
  const found = computed(() => findDevice(model.value, dv), jsonEqual);
  const shape = computed(() => { const f = found.value; return f ? f.d.params.map((p) => p.name + "|" + (p.choices || []).length).join(",") : ""; });

  let rows = [], contentH = 0, scroll = 0, dragging = null, rollPending = false;
  const W = () => ctx.box.peek().w, bodyH = () => Math.max(0, ctx.box.peek().h - kHeaderH);
  const at = (e) => (ctx.screenPoint ? ctx.screenPoint(e) : { x: e.clientX, y: e.clientY });

  // ---- the rows: built when the device's parameter list is first (or newly) known ------------------------------------
  function build() {
    for (const r of rows) r.el.remove();
    for (const s of content.querySelectorAll(".dp-sec")) s.remove();
    ids.textContent = "";
    rows = [];
    const f = untracked(() => found.value);
    if (!f) { contentH = 0; return; }
    let y = 6.5, section = "\x01";
    for (const p of f.d.params) {
      const s = sectionOf(p.name);
      if (s !== section) {
        section = s;
        if (s) {
          const h = el("div", "dp-sec", content);
          h.textContent = s;
          atBase(h, centreBase(y + kSectionH * 0.6, 9), 9, false);
          y += kSectionH;
        }
      }
      rows.push(makeRow(p, y));
      y += kRowStep;
    }
    contentH = y + 6.5;
    content.appendChild(ids); // over the rows' labels
  }

  function makeRow(p0, top) {
    const r = { spec: { ...p0 }, top, choice: Math.round(p0.value), u: 0 };
    const row = (r.el = el("div", "dp-row", content));
    row.style.top = top - 1 + "px";
    el("div", "dp-lit", row);
    const label = el("div", "dp-label", row);
    atBase(label, centreBase(1 + ROW_H / 2, 10), 10, false);
    r.label = label;
    r.id = el("div", "dp-id", ids);
    r.id.style.top = top + 2 + "px";
    r.idText = el("span", "", r.id);
    if ((p0.choices || []).length) {
      row.classList.add("dp-choicerow");
      const box = (r.box = el("div", "dp-choice", row));
      box.style.top = "1px";
      box.insertAdjacentHTML("beforeend",
        '<svg class="dp-chev dp-chev-l" width="12" height="20"><polyline points="10,6.5 6.5,10 10,13.5"/></svg>' +
        '<svg class="dp-chev dp-chev-r" width="12" height="20"><polyline points="2,6.5 5.5,10 2,13.5"/></svg>');
      r.name = el("div", "dp-choicename", box);
      atBase(r.name, centreBase(ROW_H / 2, 10), 10, false);
      tap(box, (e) => {
        const b = box.getBoundingClientRect();
        const n = r.spec.choices.length;
        const next = (((r.choice + (e.clientX < b.left + b.width / 2 ? -1 : 1)) % n) + n) % n;
        vm.cmd("set " + dv + "." + r.spec.name + "=" + r.spec.choices[next]);
      });
    } else {
      const track = (r.track = el("div", "dp-track", row));
      track.style.top = 1 + (ROW_H - TRACK_H) / 2 + "px";
      track.style.height = TRACK_H + "px";
      r.fill = el("div", "dp-fill", track);
      r.thumb = el("div", "dp-thumb", track);
      r.val = el("div", "dp-val", row);
      atBase(r.val, centreBase(1 + ROW_H / 2, 10), 10, true);
      const hit = el("div", "dp-hit", row); // the slider's 28 px height, over the track
      const log = () => paramfmt.logTaper(r.spec);
      const valueAt = (e) => {
        const b = track.getBoundingClientRect();
        const u = Math.min(1, Math.max(0, b.width > 0 ? (e.clientX - b.left) / b.width : 0));
        const s = log() ? u : r.spec.min + u * (r.spec.max - r.spec.min);
        return { u, v: paramfmt.fromPos(r.spec, s) };
      };
      const line = (v) => "set " + dv + "." + r.spec.name + "=" + paramfmt.storeText(v, !!r.spec.integer);
      hit.addEventListener("pointerdown", (e) => {
        if (e.button !== 0) return;
        e.preventDefault();
        hit.setPointerCapture(e.pointerId);
        dragging = r;
        row.classList.add("dp-active");
        const { u, v } = valueAt(e);
        r.u = u; r.dragV = v; r.sent = line(v);
        paint(r);
        vm.note(r.sent);
      });
      hit.addEventListener("pointermove", (e) => {
        if (dragging !== r) return;
        const { u, v } = valueAt(e);
        r.u = u; r.dragV = v;
        paint(r);
        const l = line(v);
        if (l !== r.sent) { r.sent = l; vm.note(l); }
      });
      const end = (e) => {
        if (dragging !== r) return;
        dragging = null;
        row.classList.remove("dp-active");
        const v = r.dragV;
        r.dragV = undefined;
        if (e.type !== "lostpointercapture") vm.cmd(line(v)); // the release: the one line the song keeps
        seed();
      };
      hit.addEventListener("pointerup", end);
      hit.addEventListener("pointercancel", end);
      hit.addEventListener("dblclick", () => vm.cmd(line(r.spec.def))); // SliderRow: a double-click, the default
    }
    row.addEventListener("contextmenu", (e) => {
      e.preventDefault();
      e.stopPropagation();
      const m = untracked(() => model.value);
      openParamMenu(vm, ctx, paramTarget(m, dv, r.spec, r.choice), at(e));
    });
    return r;
  }

  function paint(r) {
    const sp = r.spec;
    if (r.box) {
      const n = (sp.choices || []).length;
      r.name.textContent = r.choice >= 0 && r.choice < n ? sp.choices[r.choice] : "?";
      return;
    }
    const u = r.dragV !== undefined ? r.u : paramfmt.toUnit(sp, sp.value);
    r.fill.style.width = u * 100 + "%";
    r.thumb.style.left = u * 100 + "%";
    const v = r.dragV !== undefined ? r.dragV : sp.value;
    r.val.textContent = sp.formula ? bindingText(vm, sp.formula) : paramfmt.formatValue(sp, v);
  }

  // ---- the model → the rows (a drag in flight outranks it) -----------------------------------------------------------
  function seed() {
    const f = untracked(() => found.value);
    if (!f) return;
    const byName = new Map(f.d.params.map((p) => [p.name, p]));
    for (const r of rows) {
      const p = byName.get(r.spec.name);
      if (!p) continue;
      if (dragging === r) { r.spec.formula = p.formula; continue; }
      Object.assign(r.spec, p);
      r.choice = Math.round(p.value);
      paint(r);
      r.el.classList.toggle("dp-on", f.d.lastChanged === r.spec.name);
    }
  }

  function layout() {
    const w = W(), h = bodyH();
    const avail = Math.max(0, w - 2 * PADX - kValueW);
    for (const r of rows) {
      r.label.textContent = ellipsize(r.spec.label, LABEL_W - 4, 10, FONT.sans);
      const address = dv + "." + r.spec.name;
      const iw = Math.min(Math.max(LABEL_W, measure(address, 8.5, FONT.mono) + 8), avail);
      r.id.style.width = iw + "px";
      r.idText.textContent = ellipsize(address, iw - 8, 8.5, FONT.mono);
      if (r.box) {
        const bw = Math.max(0, w - (PADX + LABEL_W + GAP) - PADX);
        r.box.style.width = bw + "px";
        r.name.style.maxWidth = Math.max(0, bw - 30) + "px";
      }
    }
    const max = Math.max(0, contentH - h);
    scroll = Math.min(max, Math.max(0, scroll));
    content.style.transform = `translateY(${-scroll}px)`;
    const show = contentH > h + 0.5;
    bar.style.display = show ? "" : "none";
    if (show) {
      const th = Math.max(18, (h * h) / contentH);
      bar.style.height = th + "px";
      bar.style.transform = `translateY(${(h - th) * (max > 0 ? scroll / max : 0)}px)`;
    }
  }

  function header() {
    const f = found.value;
    const w = ctx.box.value.w;
    if (!f) return;
    const d = f.d, plugin = !!d.plugin, inst = !!d.instrument;
    const endW = inst ? ROLL_W : BTN_W;
    const endX = w - PADX - endW, byX = endX - 6.5 - BTN_W, prX = byX - 6.5 - BTN_W;
    const place = (b, x, bw) => { b.style.left = x + "px"; b.style.top = BTN_Y + "px"; b.style.width = bw + "px"; b.style.height = BTN_H + "px"; };
    place(bEnd, endX, endW); place(bBypass, byX, BTN_W); place(bPresets, prX, BTN_W);
    const fit = (b, s, bw) => { b.textContent = ellipsize(s, bw - 4, 10, FONT.sans); };
    fit(bPresets, plugin ? vm.tr("Editor") : vm.tr("Presets"), BTN_W);
    bPresets.classList.toggle("dp-editor", plugin);
    bPresets.classList.toggle("dp-lit-on", plugin && !!d.editor);
    fit(bBypass, d.bypass ? vm.tr("Bypassed") : vm.tr("On"), BTN_W);
    bBypass.classList.toggle("dp-off", !!d.bypass);
    fit(bEnd, inst ? vm.tr("Piano Roll") : vm.tr("Remove"), endW);
    bEnd.className = "dp-btn " + (inst ? "dp-roll" : "dp-remove");
    const owner = f.strip ? f.strip.name : vm.shown(vm.say("Master"));
    let w2;
    if (d.takesSample) {
      const sl = (d.sample || "").split("/").pop();
      w2 = !d.sample ? (d.takesDefinition ? vm.tr("no instrument — drop an .sfz here") : vm.tr("no sample — drop one here"))
        : vm.fillIn(d.takesDefinition ? vm.tr("instrument {1}") : vm.tr("sample {1}"), sl);
    } else w2 = d.id + " · " + (plugin ? d.label : d.type);
    what.style.left = PADX + "px";
    what.textContent = ellipsize(owner + " · " + w2, Math.max(0, prX - 2 * PADX), 10, FONT.mono);
  }

  // ---- the header's clicks -------------------------------------------------------------------------------------------
  const dev = () => { const f = untracked(() => found.value); return f ? f.d : null; };
  tap(bBypass, () => { const d = dev(); if (d) vm.cmd("set " + d.id + ".bypass=" + (d.bypass ? "false" : "true")); });
  tap(bEnd, (e) => {
    const d = dev(); if (!d) return;
    if (!d.instrument) { vm.cmd("device remove " + d.id); return; }
    const f = untracked(() => found.value), m = untracked(() => model.value);
    const strip = f && f.strip ? f.strip.id : "";
    if (!strip) return;
    const pats = (m.patterns || []).filter((p) => p.strip === strip);
    if (pats.length === 1) vm.openRoll(pats[0].id);
    else if (pats.length > 1) { const p = at(e); ctx.menu.open(pats.map((x) => ({ label: x.name || x.id, action: () => vm.openRoll(x.id) })), p.x, p.y); }
    else vm.cmd("clip add --strip " + strip).then((ok) => { if (ok) rollPending = true; });
  });
  tap(bPresets, (e) => {
    const d = dev(); if (!d) return;
    if (d.plugin) { vm.cmd("device editor " + d.id + (d.editor ? " --close" : "")); return; }
    const m = untracked(() => model.value), p = at(e);
    const items = (m.presets || []).filter((x) => x.type === d.type).map((x) => ({
      label: x.name + (x.factory ? "  · factory" : ""),
      action: () => vm.cmd("preset load " + d.id + ' "' + x.name + '"'),
    }));
    items.push({
      label: vm.say("Save as preset…"),
      action: () => ctx.menu.openRename("", p.x, p.y, (n) => { if (n) vm.cmd("preset save " + d.id + ' "' + n + '"'); }),
    });
    ctx.menu.open(items, p.x, p.y);
  });

  // ---- the wheel: the body scrolls (eased by the stylesheet, kScrollMs) ----------------------------------------------
  body.addEventListener("wheel", (e) => {
    const max = Math.max(0, contentH - bodyH());
    if (max <= 0.5) return;
    e.preventDefault();
    const dy = e.deltaMode === 1 ? e.deltaY * 16 : e.deltaMode === 2 ? e.deltaY * bodyH() : e.deltaY;
    scroll = Math.min(max, Math.max(0, scroll + dy));
    layout();
  }, { passive: false });
  root.addEventListener("contextmenu", (e) => e.preventDefault()); // the panel is a surface: nothing falls through

  // ---- effects -------------------------------------------------------------------------------------------------------
  const offs = [];
  offs.push(effect(() => { shape.value; build(); layout(); seed(); }));
  offs.push(effect(() => { found.value; seed(); }));
  offs.push(effect(() => { ctx.box.value; vm.lang && vm.lang.value; layout(); }));
  offs.push(effect(() => { header(); }));
  offs.push(effect(() => {
    // "Piano Roll" with no pattern: open the one `clip add` made, once the model has it
    const m = model.value, f = found.value;
    if (!rollPending || !m || !f || !f.strip) return;
    const p = (m.patterns || []).find((x) => x.strip === f.strip.id);
    if (p) { rollPending = false; vm.openRoll(p.id); }
  }));
  offs.push(effect(() => { const a = vm.view.idsAmount ? +vm.view.idsAmount.value || 0 : 0; ids.style.opacity = a; ids.style.display = a > 0.001 ? "" : "none"; }));
  requestAnimationFrame(() => root.classList.add("dp-anim")); // the first placement does not travel

  own(host, () => { for (const o of offs) try { o(); } catch (_) {} root.remove(); });
}
