/*
 * Interstellar — grade/menus.js: the menus App.cpp fills for the Grade deck (App::openStillContext,
 * App.cpp:963; GradeDeck::onStillActivate, App.cpp:116). Exact native command lines.
 */
import { q, num } from "../kit/cmd.js";

/** App::openStillContext: Apply Grade to <bind> · Wipe Against <name> / Stop Wipe · Delete Still. */
export function openStillContext(vm, ctx, id, x, y) {
  const m = vm.model.peek();
  if (!m) return null;
  let name = id;
  for (const st of m.stills || []) if (st.id === id) name = st.name;
  const items = [];
  const b = vm.selectedBind.peek();
  if (b) items.push({ label: "Apply Grade to " + b, action: () => vm.cmd("still apply " + q(id) + " " + q(b)) });
  if (m.wipeRef === id) items.push({ label: "Stop Wipe", action: () => vm.cmd("view wipe off") });
  else items.push({ label: "Wipe Against " + name, action: () => vm.cmd("view wipe " + q(id)) });
  items.push({ label: "Delete Still", action: () => vm.cmd("still delete " + q(id)) });
  return ctx.menu.open(items, x, y);
}

/** GradeDeck::onStillActivate (App.cpp:116): a double-click applies the still to the Grade target. */
export function activateStill(vm, id) {
  const b = vm.selectedBind.peek();
  if (b) vm.cmd("still apply " + q(id) + " " + q(b));
}

/** App::openNodeContext (App.cpp:935): a node graph node's menu - `node serial|parallel|remove`, the mix. */
export function openNodeContext(vm, ctx, n, x, y) {
  const items = [], b = q(n.bind);
  if (n.kind === "source" || n.kind === "group") items.push({ label: "Add Serial Node After", action: () => vm.cmd("node serial " + b) });
  if (n.kind === "source") items.push({ label: "Add Parallel Node", action: () => vm.cmd("node parallel " + b) });
  if (n.kind === "parallel")
    for (const pct of [100, 75, 50, 25])
      if (Math.round((n.mix ?? 1) * 100) !== pct) items.push({ label: "Mix " + pct + "%", action: () => vm.cmd("set " + b + ".parallelMix=" + num(pct / 100)) });
  if (n.kind === "parallel") items.push({ label: "Remove Parallel Node", action: () => vm.cmd("node remove " + b) });
  if (n.kind === "group") items.push({ label: "Remove Serial Node", action: () => vm.cmd("node remove " + b) });
  if (!items.length) return null;
  return ctx.menu.open(items, x, y);
}
