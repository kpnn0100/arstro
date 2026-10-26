/*
 * Interstellar — inspector.js: the right column, by the page's tab (the window's ClipInspector,
 * RackTree + GradeInspector, OutputSpec + RenderQueue, in miniature):
 *
 *   Cut      the rack's sources (place one at the playhead: `clip add --src … --at …`) and the
 *            selected clip (rename, speed, opacity: `set <clip>.<field>=…`)
 *   Grade    the rack (select the Grade target: `rack select`; add media: `rack add`) and the
 *            target's basic controls - each slider's range and neutral are the CORE's address table
 *            (`api --json`), never a copy; a drag sends `set <bind>.basic.<key>=…` as notifies
 *   Deliver  a render of a named timeline (`render --timeline … --out … --format …`) and the queue
 *
 * Every value shown is the model's; an input that is being edited keeps the user's text until it is
 * committed (then the model's comes back).
 */
import { keyed, bindText, bindClass, bindEffect } from "../core/dom.js";
import { computed, effect } from "../core/signal.js";
import { h, icon, ICONS } from "./util.js";

const BASIC = ["exposure", "contrast", "highlights", "shadows", "whites", "blacks", "temp", "tint", "vibrance", "saturation"];
const FORMATS = ["h264", "h265", "prores", "dnxhr", "png-seq", "dcp", "imf"];

function section(title, ...body) { return h("section.panel", h("h3.panel-title", title), ...body); }

/** A number field that shows the model's value unless it is being typed into. */
function numberField(get, commit, { step = "any", min, max } = {}) {
  const el = h("input.field.mono", { type: "number", step, min, max });
  bindEffect(el, () => { const v = get(); if (document.activeElement !== el) el.value = v === null || v === undefined ? "" : String(+Number(v).toFixed(4)); });
  el.addEventListener("change", () => { if (el.value !== "") commit(Number(el.value)); });
  el.addEventListener("keydown", (e) => { if (e.key === "Enter") el.blur(); });
  return el;
}

function cutPanel(vm) {
  const sources = h("div.list");
  keyed(sources, () => vm.rack.value.filter((r) => !r.group), (r) => r.node, (r) => h("div.row",
    h("span.row-name"), h("span.row-meta.mono"),
    h("button.tbtn.row-act", { title: "Place at the playhead (clip add --src … --at …)", onclick: () => vm.place(r.bind) }, icon(ICONS.plus))), (el, r) => {
    el.querySelector(".row-name").textContent = r.name;
    el.querySelector(".row-meta").textContent = r.bind + (r.pending ? " · loading" : r.failed ? " · failed" : r.offlineWhy ? " · offline" : "");
    el.classList.toggle("bad", r.failed || !!r.offlineWhy);
  });
  const noSources = h("p.hint");
  bindText(noSources, () => (vm.rack.value.some((r) => !r.group) ? "" : "The rack is empty — add media on the Grade tab (rack add <file>)."));

  const name = h("input.field", { spellcheck: "false" });
  bindEffect(name, () => { const c = vm.selectedClip.value; if (document.activeElement !== name) name.value = c ? c.name : ""; });
  name.addEventListener("change", () => vm.renameClip(name.value.trim()));
  name.addEventListener("keydown", (e) => { if (e.key === "Enter") name.blur(); });
  const facts = h("div.facts.mono");
  bindText(facts, () => {
    const c = vm.selectedClip.value;
    if (!c) return "";
    return `source  ${c.srcName || c.src}${c.offline ? "  (offline)" : ""}\nat      ${c.at.toFixed(3)} s\nin–out  ${c.in.toFixed(3)} – ${c.out.toFixed(3)} s\nlength  ${c.duration.toFixed(3)} s`;
  });
  const speed = numberField(() => vm.selectedClip.value && vm.selectedClip.value.speed, (v) => vm.setClip("speed", v), { step: 0.05, min: 0.1, max: 8 });
  const opacity = h("input.slider", { type: "range", min: 0, max: 1, step: 0.01 });
  const opacityText = h("span.slider-val.mono");
  bindEffect(opacity, () => { const c = vm.selectedClip.value; if (c && document.activeElement !== opacity) opacity.value = String(c.opacity); });
  bindText(opacityText, () => { const c = vm.selectedClip.value; return c ? Math.round(c.opacity * 100) + "%" : ""; });
  opacity.addEventListener("input", () => vm.setClip("opacity", Number(opacity.value), { drag: true }));
  opacity.addEventListener("change", () => vm.setClip("opacity", Number(opacity.value)));
  const clipBox = h("div.clip-box.when",
    h("label.lbl", "Name"), name,
    facts,
    h("div.kv", h("label.lbl", "Speed"), speed, h("span.unit", "×")),
    h("div.kv", h("label.lbl", "Opacity"), opacity, opacityText));
  bindClass(clipBox, "on", () => !!vm.selectedClip.value);
  const noClip = h("p.hint.when", "Select a clip on the timeline.");
  bindClass(noClip, "on", () => !vm.selectedClip.value);
  return h("div.tab-panel", section("Sources", sources, noSources), section("Clip", noClip, clipBox));
}

function gradePanel(vm) {
  const tree = h("div.list.tree");
  keyed(tree, () => vm.rack.value, (r) => r.node, (r) => h("button.row", { onclick: () => vm.rackSelect(r.bind) },
    h("span.row-name"), h("span.row-meta.mono")), (el, r) => {
    el.style.paddingLeft = `${8 + r.depth * 13}px`;   // space::indent — one tree level
    el.querySelector(".row-name").textContent = (r.group ? "▸ " : "") + r.name;
    el.querySelector(".row-meta").textContent = r.bind + (r.bypass ? " · bypass" : "") + (r.pending ? " · loading" : r.failed ? " · failed" : "");
    el.classList.toggle("selected", r.selected);
    el.classList.toggle("bad", r.failed || !!r.offlineWhy);
  });
  const path = h("input.field.mono", { placeholder: "/path/on/this/machine/clip.mov", spellcheck: "false" });
  const add = () => { const p = path.value.trim(); if (p) { vm.rackAdd(p.split(/\s*,\s*/)); path.value = ""; } };
  path.addEventListener("keydown", (e) => { if (e.key === "Enter") add(); });
  const addRow = h("div.add-row", path, h("button.pill", { onclick: add, title: "rack add <media…> (comma-separated)" }, "Add"));

  // the sliders: the core's address table for `<bind>.basic.*` (range, neutral, unit), the model's value
  const specs = computed(() => {
    const by = new Map();
    for (const a of vm.addresses.value) {
      const m = /^<bind>\.basic\.(\w+)$/.exec(a.address || "");
      if (m && BASIC.includes(m[1])) by.set(m[1], a);
    }
    return BASIC.filter((k) => by.has(k)).map((k) => {
      const a = by.get(k);
      const [lo, hi] = String(a.range || "").split("..").map(Number);
      return { key: k, address: "basic." + k, min: Number.isFinite(lo) ? lo : -100, max: Number.isFinite(hi) ? hi : 100,
               neutral: a.neutral || 0, unit: a.unit || "", doc: a.doc || "" };
    });
  });
  const sliders = h("div.sliders");
  keyed(sliders, () => specs.value, (s) => s.key, (s) => {
    const input = h("input.slider", { type: "range", min: s.min, max: s.max, step: (s.max - s.min) / 400, title: `${s.doc} Double-click: back to ${s.neutral}.` });
    const val = h("span.slider-val.mono");
    const row = h("div.kv.slider-row", h("label.lbl", s.key), input, val);
    const value = () => {
      const p = vm.gradeParams.value;
      const v = p[s.key] !== undefined ? p[s.key] : p[s.address];
      return typeof v === "number" ? v : s.neutral;
    };
    bindEffect(input, () => { const v = value(); if (document.activeElement !== input) input.value = String(v); val.textContent = (+v.toFixed(2)) + (s.unit && s.unit !== "0..1" ? " " + s.unit : ""); });
    input.addEventListener("input", () => { val.textContent = String(+Number(input.value).toFixed(2)); vm.setGrade(s.address, Number(input.value), { drag: true }); });
    input.addEventListener("change", () => { vm.setGrade(s.address, Number(input.value)); input.blur(); });
    input.addEventListener("dblclick", () => vm.setGrade(s.address, s.neutral));
    return row;
  });
  const target = h("p.hint");
  bindText(target, () => { const g = vm.gradeTarget.value; return g ? `Grading ${g.name} (${g.bind})` : "Select a source or a group in the rack to grade it."; });
  bindClass(sliders, "off", () => !vm.gradeTarget.value);
  return h("div.tab-panel", section("Rack", tree, addRow), section("Grade", target, sliders));
}

function deliverPanel(vm) {
  const tl = h("select.field");
  keyed(tl, () => vm.timelines.value, (t) => t.id, (t) => h("option", { value: t.id }), (el, t) => { el.textContent = t.name; }, { animate: false });
  bindEffect(tl, () => { const t = vm.timeline.value; if (t && !tl.dataset.touched) tl.value = t.id; });
  tl.addEventListener("change", () => { tl.dataset.touched = "1"; });
  const out = h("input.field.mono", { placeholder: "/path/on/this/machine/out.mp4", spellcheck: "false" });
  bindEffect(out, () => { const p = vm.project.value; if (p && !out.value && p.path) out.value = p.path.replace(/\.isp$/, "") + ".mp4"; });
  const fmt = h("select.field", ...FORMATS.map((f) => h("option", { value: f }, f)));
  const go = h("button.pill.primary", { onclick: () => { if (out.value.trim() && tl.value) vm.render({ out: out.value.trim(), format: fmt.value, tl: tl.value }); },
                                        title: "render --timeline … --out … --format …" }, "Render");
  const form = h("div.form", h("label.lbl", "Timeline"), tl, h("label.lbl", "Output"), out, h("label.lbl", "Format"), fmt, h("div.form-go", go));

  const queue = h("div.list");
  keyed(queue, () => vm.renders.value, (r) => r.id, (r) => h("div.render",
    h("div.render-head", h("span.row-name"), h("span.row-meta.mono"),
      h("button.tbtn.row-act", { title: "Cancel (render cancel)", onclick: () => vm.cancelRender(r.id) }, icon(ICONS.close))),
    h("div.bar", h("i")), h("div.render-err")), (el, r) => {
    el.querySelector(".row-name").textContent = r.timelineName || r.timeline;
    el.querySelector(".row-meta").textContent = `${r.format} · ${r.state}${r.total ? ` · ${r.done}/${r.total}` : ""}`;
    el.querySelector(".bar i").style.transform = `scaleX(${r.total ? Math.min(1, r.done / r.total) : 0})`;
    el.querySelector(".render-err").textContent = r.error || r.out;
    el.classList.toggle("bad", !!r.error);
    el.querySelector(".row-act").disabled = !(r.state === "queued" || r.state === "running");
  });
  const none = h("p.hint");
  bindText(none, () => (vm.renders.value.length ? "" : "No renders yet."));
  return h("div.tab-panel", section("Render", form), section("Queue", queue, none));
}

export function inspector(vm) {
  const pages = { cut: cutPanel(vm), grade: gradePanel(vm), deliver: deliverPanel(vm) };
  const box = h("aside.inspector.scroll", ...Object.values(pages));
  effect(() => {
    const tab = vm.view.tab.value;
    for (const [k, el] of Object.entries(pages)) el.classList.toggle("on", k === tab);
  });
  const err = h("div.last-error");
  bindText(err, () => vm.lastError.value || "");
  bindClass(err, "on", () => !!vm.lastError.value);
  box.append(err);
  return box;
}
