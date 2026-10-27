/*
 * Solaris — mixer/parammenu.js: ParamMenu (widgets/ParamMenu.{h,cpp}, R-WIN-3, R-MIX-16, R-UI-11, R-CTRL-1) — ONE menu for
 * every number a formula can drive: a device parameter's row or knob, a fader, a pan, a send, the master fader. The
 * widget says WHAT is under the pointer (a ParamTarget); this builds the items, so the menus cannot drift apart. Every
 * edit is ONE command line, the native's text exactly.
 *
 * EXPORTS
 *   paramMenuItems(p, hooks, at) -> [{label, action}]   ParamMenu.cpp paramMenuItems, in the native's order:
 *       Create Automation (`auto create <address>`; not when the formula IS an automation) · Formula… (the rename field,
 *       seeded with the formula or "=" → `set <address>=<q("=" + typed)>`) · Clear Binding (`bind clear <address>`, when
 *       bound) · Reset to Default (`set <address>=<reset>`) · Learn MIDI CC (`map learn <address>`) · Forget CC <n>
 *       (`map remove <n>`) · Copy Address · Copy Value · Copy as Formula ("=<address>").
 *       p     = ParamTarget {address, formula ("" = its own value plays), value (Copy Value, as shown), reset ("" none),
 *               bindable (a number: true; a choice or a switch: false), controls: [cc …]}
 *       hooks = {command(line), rename(current, at, done(typed)), copy(text)}
 *       at    = {x, y} ROOT logical px (where the rename field opens)
 *       Labels are STORED words (vm.say) when hooks.say is given, else English.
 *   openParamMenu(vm, ctx, p, at)   builds the hooks from the vm (vm.cmd) and ctx (ctx.menu.openRename, ctx.copy) and
 *                                   opens the menu at `at` (ROOT px) with ctx.menu.open — what a row's right-click does.
 *   paramTarget(m, dv, spec, choice)  DevicePanel::openParamMenuFor's target for a device parameter `spec` (a
 *                                   ParamModel of the model JSON): a bound value Copy-Values what it evaluates to now.
 *   automationOf(formula)            "au_1" for "=au_1"; "" for a link or an expression.
 *   bindingText(vm, formula, max=14) the value column's words: `auto au_1`, `= ch_2.gain`, `= <formula>` cut with "…".
 *   controlsFor(m, address)          R-CTRL-1: the MIDI controls the machine maps to `address` (settings.mappings[]).
 */
import { q, paramfmt } from "../kit/format.js";

function body(formula) {
  const b = formula && formula.length > 1 ? formula.slice(1) : "";
  return b.replace(/^ +| +$/g, "");
}

export function automationOf(formula) {
  const b = body(formula || "");
  if (!b.startsWith("au_") || b.length < 4) return "";
  return /^[A-Za-z0-9_]+$/.test(b) ? b : "";
}

export function bindingText(vm, formula, maxChars = 14) {
  const au = automationOf(formula);
  if (au) return vm ? vm.fillIn(vm.tr("auto {1}"), au) : "auto " + au;
  let out = "= " + body(formula);
  if (out.length > maxChars) out = out.slice(0, maxChars - 1) + "…";
  return out;
}

export function controlsFor(m, address) {
  const maps = (m && m.settings && m.settings.mappings) || [];
  return maps.filter((x) => x.address === address).map((x) => String(x.cc));
}

export function paramTarget(m, dv, spec, choice) {
  const address = dv + "." + spec.name;
  const choices = spec.choices || [];
  const p = { address, formula: spec.formula || "", bindable: choices.length === 0, controls: controlsFor(m, address), value: "", reset: "" };
  if (p.bindable) {
    let v = spec.value;
    for (const b of (m && m.bindings) || []) if (b.address === address && typeof b.live === "number") v = b.live; // R-MIX-16
    p.value = paramfmt.formatValue(spec, v);
    p.reset = paramfmt.storeText(spec.def, !!spec.integer);
  } else {
    const n = choices.length;
    p.value = choice >= 0 && choice < n ? choices[choice] : "";
    p.reset = n ? choices[Math.min(n - 1, Math.max(0, Math.round(spec.def)))] : "";
  }
  return p;
}

export function paramMenuItems(p, hooks, at) {
  const say = hooks.say || ((s) => s);
  const cmd = (line) => hooks.command && hooks.command(line);
  const copy = (t) => hooks.copy && hooks.copy(t);
  const address = p.address;
  const items = [];
  if (p.bindable) {
    if (!automationOf(p.formula)) items.push({ label: say("Create Automation"), action: () => cmd("auto create " + address) });
    const current = p.formula ? p.formula : "=";
    items.push({
      label: say("Formula…"),
      action: () => {
        if (!hooks.rename) return;
        hooks.rename(current, at, (typed) => {
          let f = typed || "";
          if (!f) return;
          if (f[0] !== "=") f = "=" + f;
          cmd("set " + address + "=" + q(f));
        });
      },
    });
    if (p.formula) items.push({ label: say("Clear Binding"), action: () => cmd("bind clear " + address) });
  }
  if (p.reset) {
    const def = p.reset;
    items.push({ label: say("Reset to Default"), action: () => cmd("set " + address + "=" + def) });
  }
  if (p.bindable) {
    items.push({ label: say("Learn MIDI CC"), action: () => cmd("map learn " + address) });
    for (const cc of p.controls || []) items.push({ label: "Forget CC " + cc, action: () => cmd("map remove " + cc) });
  }
  items.push({ label: say("Copy Address"), action: () => copy(address) });
  if (p.value) {
    const value = p.value;
    items.push({ label: say("Copy Value"), action: () => copy(value) });
  }
  if (p.bindable) items.push({ label: say("Copy as Formula"), action: () => copy("=" + address) });
  return items;
}

export function openParamMenu(vm, ctx, p, at) {
  const hooks = {
    say: vm.say ? (s) => vm.say(s) : null,
    command: (line) => vm.cmd(line),
    rename: (current, pt, done) => ctx.menu.openRename(current, pt.x, pt.y, done),
    copy: (text) => (ctx.copy ? ctx.copy(text) : vm.copy(text)),
  };
  ctx.menu.open(paramMenuItems(p, hooks, at), at.x, at.y);
}
