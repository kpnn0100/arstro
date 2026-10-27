/*
 * Interstellar — cut/keymenus.js: the menus App.cpp fills for the ANIMATION section (R-ANIM-7, 9, 10).
 *
 *   openKeyContext(vm, ctx, lane, address, t, x, y)   App::openKeyContext (App.cpp:671-725): a key's ease
 *                                                     (Linear, Ease, Ease In, Ease Out, Hold), Edit in Grade
 *                                                     at This Key (a source's key), Speed & Influence… (not a
 *                                                     shape: a prompt with only what changed sent), Copy Key(s),
 *                                                     Delete Key
 *   openKeyPlotContext(vm, ctx, lane, t, x, y)        App::openKeyPlotContext (:813-827): paste the copied keys
 *                                                     at the playhead, here, or onto the front property
 *   openAnimateMenu(vm, ctx, lane, rect, group?)      App::openAnimateMenu (:735-771): what the chosen clip (or
 *                                                     Grade's source) does not animate yet, by group; a pick
 *                                                     marks it (`key mark`) and chooses its lane
 *   openKeyRowContext(vm, ctx, lane, address, x, y)   App::openKeyRowContext (:773-785): Show / Hide Curve, the
 *                                                     mode, Remove Animation (`key clear`)
 *   appendModeItems(vm, items, address, mode, fixed)  App::appendModeItems (:787-797)
 *   openMarkMenu(vm, ctx, address, x, y)              App::openMarkMenu (:799-811) - FOR GRADE'S INSPECTOR: the
 *                                                     right-click on a marked parameter's diamond. Its mode
 *                                                     (Offset / Fixed, the current one bulleted) and Remove
 *                                                     Animation. `address` is the anim's address
 *                                                     ("s_day01.basic.exposure"); x, y in ROOT logical px.
 *
 * `lane` is the key lane's handle (cut/keylane.js: {select(a), closeCurve(), selected(), now(), rowOf(a),
 * row(i), graph}) - null when the menu is opened from outside the Cut tab. x, y in ROOT logical px.
 * Every item is one command line, the App's own.
 */
import { q, num } from "../kit/cmd.js";
import { animOf, mark, clipProps, sourceProps } from "./key/keystate.js";

const run = (vm, l) => vm.cmd(l);

export function openKeyContext(vm, ctx, lane, address, t, x, y) {
  const m = vm.model.peek();
  if (!m) return;
  const a = (m.anims || []).find((z) => z.address === address);
  if (!a) return;
  const k = (a.keys || []).find((z) => Math.abs(z.t - t) < 5e-4);
  if (!k) return;
  const head = "key set " + q(address) + " --at " + num(k.t), items = [];
  for (const [label, e] of [["Linear", "linear"], ["Ease", "ease"], ["Ease In", "ease-in"], ["Ease Out", "ease-out"], ["Hold", "hold"]])
    items.push({ label, action: () => run(vm, head + " --ease " + e) });
  if (a.owner === "rack")
    for (const r of m.rack || [])
      if (r.rackObj === a.node)
        items.push({ label: "Edit in Grade at This Key", action: () => {
          // Grade stands on the key's source frame: what it shows there is what the key holds
          run(vm, "rack select " + q(r.bindName));
          run(vm, "rack frame " + q(r.bindName) + " --at " + num(k.t));
          if (vm.setTab) vm.setTab(0);
        } });
  if (!a.shape)
    items.push({ label: "Speed & Influence…", action: async () => {
      const was = [num(k.v), num(k.speedIn || 0), num(k.inflIn ?? 33.333), num(k.speedOut || 0), num(k.inflOut ?? 33.333)];
      const labels = ["Value", "Incoming speed /s", "Incoming influence %", "Outgoing speed /s", "Outgoing influence %"];
      const v = await ctx.prompt({ title: "Keyframe at " + num(k.t) + " s", message: address, ok: "Apply",
                                   fields: labels.map((label, i) => ({ label, value: was[i] })) });
      if (!Array.isArray(v) || v.length < 5) return;
      // only what was changed: a speed given makes its side a bezier, so an untouched linear side is not sent
      const flag = ["value", "speed-in", "influence-in", "speed-out", "influence-out"];
      let line = head;
      for (let i = 0; i < 5; i++) if (String(v[i]).trim() !== was[i]) line += " --" + flag[i] + " " + String(v[i]).trim();
      if (line !== head) run(vm, line);
    } });
  const g = lane && lane.graph;
  if (g && g.shown()) {
    const list = g.selectionList() || address + "@" + num(k.t), n = Math.max(1, g.selectionCount());
    items.push({ label: n > 1 ? "Copy " + n + " Keys  (Ctrl+C)" : "Copy Key  (Ctrl+C)", action: () => run(vm, "key copy --keys " + q(list)) });
  }
  items.push({ label: "Delete Key", action: () => run(vm, "key remove " + q(address) + " --at " + num(k.t)) });
  ctx.menu.open(items, x, y);
}

export function openKeyPlotContext(vm, ctx, lane, t, x, y) {
  const m = vm.model.peek();
  if (!m || !(m.keyClipboardCount > 0)) return;
  const now = lane ? lane.now() : -1, items = [];
  if (now >= 0) items.push({ label: "Paste Keys at Playhead  (Ctrl+V)", action: () => run(vm, "key paste --at " + num(now)) });
  items.push({ label: "Paste Keys Here", action: () => run(vm, "key paste --at " + num(t)) });
  const front = lane ? lane.selected() : "";
  if (m.keyClipboardCurves === 1 && front && now >= 0)
    items.push({ label: "Paste onto " + front, action: () => run(vm, "key paste --to " + q(front) + " --at " + num(now)) });
  ctx.menu.open(items, x, y);
}

export function openAnimateMenu(vm, ctx, lane, at, group = "") {
  const m = vm.model.peek();
  if (!m) return;
  let all = [];
  for (const c of m.clips || []) if (c.id === m.selectedClip && !c.audio && !c.nested) all = clipProps(m, c);
  if (!all.length && m.selectedRack >= 0 && m.rack && m.selectedRack < m.rack.length) all = sourceProps(m, m.rack[m.selectedRack].rackObj);
  const groups = [], props = new Map();
  for (const p of all) {
    if (animOf(m, p.node, p.key)) continue;
    if (!props.has(p.group)) { groups.push(p.group); props.set(p.group, []); }
    props.get(p.group).push([p.label, p.address]);
  }
  const animate = (address) => { run(vm, mark(address)); if (lane) lane.select(address); };
  const items = [];
  if (group) for (const [label, a] of props.get(group) || []) items.push({ label, action: () => animate(a) });
  else
    for (const g of groups) {
      if (g === "Clip") for (const [label, a] of props.get(g)) items.push({ label, action: () => animate(a) });
      else items.push({ label: g + "  ›", action: () => setTimeout(() => openAnimateMenu(vm, ctx, lane, at, g), 0) });
    }
  if (!items.length) items.push({ label: all.length ? "Everything here is animated" : "Choose a clip or a source first", action: () => {} });
  ctx.menu.open(items, at.x, at.y + at.h);
}

export function appendModeItems(vm, items, address, mode, fixedOnly) {
  // R-ANIM-10: the mode, the current one bulleted; switching keeps the picture (`key mode`)
  if (fixedOnly) { items.push({ label: "• Fixed (a shape or a clip's own)", action: () => {} }); return; }
  const tick = "• ", none = "   ";
  items.push({ label: (mode === "offset" ? tick : none) + "Offset — rides on Grade's value",
               action: () => { if (mode !== "offset") run(vm, "key mode " + q(address) + " offset"); } });
  items.push({ label: (mode === "fixed" ? tick : none) + "Fixed — the keys are the value",
               action: () => { if (mode !== "fixed") run(vm, "key mode " + q(address) + " fixed"); } });
}

export function openKeyRowContext(vm, ctx, lane, address, x, y) {
  const items = [];
  if (lane.selected() === address) items.push({ label: "Hide Curve", action: () => lane.closeCurve() });
  else items.push({ label: "Show Curve", action: () => lane.select(address) });
  const i = lane.rowOf(address);
  if (i >= 0) appendModeItems(vm, items, address, lane.row(i).mode, lane.row(i).prop.fixedOnly);
  // the inverse of marking: the curve goes, the value it has now stays (key clear)
  items.push({ label: "Remove Animation", action: () => run(vm, "key clear " + q(address)) });
  ctx.menu.open(items, x, y);
}

/** Grade's mark on a marked parameter (R-ANIM-9): its mode, or unmark it. x, y in ROOT px. */
export function openMarkMenu(vm, ctx, address, x, y) {
  const m = vm.model.peek();
  let mode = "offset";
  for (const a of (m && m.anims) || []) if (a.address === address) mode = a.mode;
  const items = [];
  appendModeItems(vm, items, address, mode, false);
  items.push({ label: "Remove Animation", action: () => run(vm, "key clear " + q(address)) });
  ctx.menu.open(items, x, y);
}
