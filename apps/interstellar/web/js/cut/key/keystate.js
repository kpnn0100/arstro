/*
 * Interstellar — cut/key/keystate.js: what a property's animation state is, what it is called, and
 * the lines its controls dispatch (R-ANIM-3, R-ANIM-9, R-ANIM-10).
 *
 * A port of app/widgets/KeyState.h (namespace keys) and app/widgets/ColourKeys.h (colourKeys):
 *
 *   COLOUR_KEYS                   ColourKeys.h:405-425 - the colour keys a curve can drive, in Grade's
 *                                 order and words: {key, label, group}
 *   animOf(m, node, key)          KeyState.h:196-201
 *   sourceNow(m, rackObj)         :205-212 - "now" on a source clock (its reference frame)
 *   keyAt(anim, t)                :214-219 - a key within 0.5 ms of t
 *   state(m, node, key, now)      :221-226 - 0 no curve · 1 animated, no key here · 2 a key here
 *   toggle(address, state, now)   :229-232 - the diamond's line: `key add|remove <a> --at <now>`
 *   mark(address)                 :236     - `key mark <a>`
 *   Prop = {node, key, address, label, object, group, objectOrder, groupOrder, labelOrder, fixedOnly}
 *   colourProp / effectProp / clipProp / describe(m, anim) / sourceProps(m, rackObj) / clipProps(m, clip)
 *   CLIP_KEYS / CLIP_LABELS       :249-259 - a clip's own properties, in an editor's order, and its speed
 *
 * Model field names: apps/interstellar/model/AppModelCodec.cpp (anims[], effects[], clips[], rack[]).
 */
import { q, num } from "../../kit/cmd.js";

/** ColourKeys.h:407-422 (R-ANIM-6: the shapes after the numbers - keyed like numbers, fixed only). */
export const COLOUR_KEYS = [
  ["basic.exposure", "Exposure", "Tone"], ["basic.contrast", "Contrast", "Tone"], ["basic.highlights", "Highlights", "Tone"],
  ["basic.shadows", "Shadows", "Tone"], ["basic.whites", "Whites", "Tone"], ["basic.blacks", "Blacks", "Tone"],
  ["basic.temp", "Temperature", "Colour"], ["basic.tint", "Tint", "Colour"], ["basic.vibrance", "Vibrance", "Colour"],
  ["basic.saturation", "Saturation", "Colour"], ["basic.texture", "Texture", "Presence"], ["basic.clarity", "Clarity", "Presence"],
  ["basic.dehaze", "Dehaze", "Presence"], ["basic.grainAmount", "Grain Amount", "Effects"], ["basic.grainSize", "Grain Size", "Effects"],
  ["detail.sharpenAmount", "Sharpen Amount", "Sharpening"], ["detail.sharpenRadius", "Sharpen Radius", "Sharpening"],
  ["detail.sharpenMasking", "Sharpen Masking", "Sharpening"], ["detail.nrLuminance", "NR Luminance", "Noise Reduction"],
  ["detail.nrColor", "NR Colour", "Noise Reduction"], ["detail.lensDistortion", "Distortion", "Lens"], ["detail.lensCA", "Defringe", "Lens"],
  ["detail.lensVignette", "Vignette", "Lens"],
  ["curve.curve", "Tone Curve", "Curve"], ["curve.curveR", "Red Curve", "Curve"], ["curve.curveG", "Green Curve", "Curve"],
  ["curve.curveB", "Blue Curve", "Curve"], ["mixer.mixer0", "Mixer Hue", "Mixer"], ["mixer.mixer1", "Mixer Saturation", "Mixer"],
  ["mixer.mixer2", "Mixer Luminance", "Mixer"], ["grade.grade0", "Shadows Wheel", "Wheels"], ["grade.grade1", "Midtones Wheel", "Wheels"],
  ["grade.grade2", "Highlights Wheel", "Wheels"], ["xform.crop", "Crop", "Xform"],
].map(([key, label, group]) => ({ key, label, group }));

export function animOf(m, node, key) {
  for (const a of (m && m.anims) || []) if (a.node === node && a.key === key) return a;
  return null;
}

export function sourceNow(m, rackObj) {
  const rack = (m && m.rack) || [];
  for (const r of rack) if (r.rackObj === rackObj && r.media) return r.frame;
  if (m.selectedRack >= 0 && m.selectedRack < rack.length && rack[m.selectedRack].media) return rack[m.selectedRack].frame;
  return 0;
}

export function keyAt(a, t) {
  for (const k of a.keys || []) if (Math.abs(k.t - t) < 5e-4) return true;
  return false;
}

export function state(m, node, key, now) {
  const a = animOf(m, node, key);
  if (!a) return 0;
  return keyAt(a, now) ? 2 : 1;
}

/** The line a diamond click dispatches (KeyState.h:229-232). */
export const toggle = (address, st, now) => (st === 2 ? "key remove " : "key add ") + q(address) + " --at " + num(now);
/** Grade's mark (R-ANIM-9): an offset - nothing on screen changes (KeyState.h:236). */
export const mark = (address) => "key mark " + q(address);

// ── names (R-ANIM-9) ──
export const CLIP_KEYS = ["opacity", "geom.x", "geom.y", "geom.scale", "geom.rotation", "speed"];
export const CLIP_LABELS = ["Opacity", "Position X", "Position Y", "Scale", "Rotation", "Speed"];
const GROUPS = ["Tone", "Colour", "Presence", "Effects", "Sharpening", "Noise Reduction", "Lens", "Curve", "Mixer", "Wheels", "Xform"];

function blank() {
  return { node: "", key: "", address: "", label: "", object: "", group: "", objectOrder: 0, groupOrder: 0, labelOrder: 0, fixedOnly: false };
}

export function rackIndex(m, rackObj) {
  const rack = (m && m.rack) || [];
  for (let i = 0; i < rack.length; i++) if (rack[i].rackObj === rackObj) return i;
  return 9999;
}

/** Where Grade shows a colour key, in Grade's order (1 = Tone …; KeyState.h:269-275). */
export function panelOrder(group) {
  const i = GROUPS.indexOf(group);
  return i < 0 ? 50 : i + 1;
}

export function colourProp(m, rackObj, bind, key) {
  const p = blank();
  p.node = rackObj;
  p.key = key;
  p.address = bind + "." + key;
  p.object = bind;
  p.objectOrder = rackIndex(m, rackObj);
  p.label = key;
  p.group = "Grade";
  p.groupOrder = 50;
  COLOUR_KEYS.forEach((ck, i) => {
    if (key !== ck.key) return;
    p.label = ck.label;
    p.group = ck.group;
    p.groupOrder = panelOrder(p.group);
    p.labelOrder = i;
    p.fixedOnly = p.groupOrder >= panelOrder("Curve");   // shapes
  });
  return p;
}

export function effectProp(m, e, key) {
  const p = blank();
  p.node = e.id;
  p.key = key;
  p.address = e.id + "." + key;
  p.object = e.nodeBind;
  p.objectOrder = rackIndex(m, e.node);
  p.group = e.label;
  p.groupOrder = 100 + (e.order || 0);
  p.label = key === "mix" ? "Mix" : key;
  (e.params || []).forEach((pm, i) => { if (pm.key === key) { p.label = pm.label; p.labelOrder = i + 1; } });
  return p;
}

export function clipProp(m, c, key) {
  const p = blank();
  p.node = c.id;
  p.key = key;
  p.object = c.name ? c.name : c.id;
  p.address = p.object + "." + key;
  p.objectOrder = rackIndex(m, c.src);
  p.group = "Clip";
  p.groupOrder = 0;
  p.label = key;
  p.fixedOnly = true;
  CLIP_KEYS.forEach((k, i) => { if (key === k) { p.label = CLIP_LABELS[i]; p.labelOrder = i; } });
  return p;
}

/** What a curve is called, and where it sorts (by object, then Grade's order) - KeyState.h:338-350. */
export function describe(m, a) {
  if (a.owner === "clip") for (const c of m.clips || []) if (c.id === a.node) return clipProp(m, c, a.key);
  if (a.owner === "effect") for (const e of m.effects || []) if (e.id === a.node) return effectProp(m, e, a.key);
  const p = colourProp(m, a.node, a.nodeBind, a.key);
  p.address = a.address ? a.address : p.address;
  p.fixedOnly = p.fixedOnly || !!a.shape;
  return p;
}

/** Every property of a source that can be marked: its colour keys and its effects (:353-370). */
export function sourceProps(m, rackObj) {
  const out = [];
  let bind = "";
  for (const r of m.rack || []) if (r.rackObj === rackObj) bind = r.bindName;
  if (!bind) return out;
  for (const ck of COLOUR_KEYS) out.push(colourProp(m, rackObj, bind, ck.key));
  for (const e of m.effects || []) {
    if (e.node !== rackObj) continue;
    out.push(effectProp(m, e, "mix"));
    for (const pm of e.params || []) out.push(effectProp(m, e, pm.key));
  }
  return out;
}

/** …and a clip's: its own, then its source's (:373-382). */
export function clipProps(m, c) {
  return [...CLIP_KEYS.map((k) => clipProp(m, c, k)), ...sourceProps(m, c.src)];
}
