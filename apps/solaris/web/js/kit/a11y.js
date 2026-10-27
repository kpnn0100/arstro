/*
 * Solaris — kit/a11y.js: the accessible list (app/A11y.h, R-ACCESS-1) — what the keyboard walks.
 *
 * Every widget lists its controls as NODES: a stable key, a role, a name, a value, where it is (ROOT logical px — add
 * ctx.box.peek().x / .y to a local rect), whether it is on, and what it does. Activating a node is the CLICK it stands
 * for (the shell clicks its centre, through the same DOM a pointer uses — so no widget learns a second way to act),
 * unless the node says otherwise (`activate`). A value's `step(dir, fine)` is its keyboard step (ONE line each).
 *
 *   a11yNode(key, role, name, bounds, value?) -> node   role: button | toggle | slider | combo | tab | menu | menuitem | listitem
 *   vm.a11y.register(order, fn, { screen }) -> unregister
 *        fn() -> node[] in reading order; `order` places the widget in the App's reading order (App::accessibles):
 *        A11Y_ORDER.bar 10 (its menus' titles after `bar.settings` are the bar's own nodes too), browser 20,
 *        timeline 30 (the session grid in its place), dock 40, windows 50; home 10 on the home screen.
 *        `screen`: "home" | "project" — listed only while that screen shows (default: always).
 *   vm.a11y.modal(fn) -> unregister   a modal's nodes ALONE while it is registered (the newest wins): a sheet, a menu,
 *        a dialog. Return [] while a text field is being typed in (its keys are its own).
 *   vm.a11y.nodes(screen) -> node[]   what the shell's Tab walks (enabled nodes with a size).
 */
export const A11Y_ORDER = Object.freeze({ home: 10, bar: 10, browser: 20, timeline: 30, session: 30, dock: 40, windows: 50 });

export function a11yNode(key, role, name, bounds, value = "") {
  return { key, role, name, value, bounds: { x: bounds.x, y: bounds.y, w: bounds.w, h: bounds.h }, enabled: true, checked: false, activate: null, step: null };
}

export function createA11y() {
  let seq = 0;
  const owners = new Map();   // id -> {order, seq, fn, screen}
  const modals = [];          // [{id, fn}], newest last
  return {
    register(order, fn, { screen } = {}) {
      const id = ++seq;
      owners.set(id, { order, seq: id, fn, screen });
      return () => owners.delete(id);
    },
    modal(fn) {
      const id = ++seq;
      modals.push({ id, fn });
      return () => { const i = modals.findIndex((m) => m.id === id); if (i >= 0) modals.splice(i, 1); };
    },
    nodes(screen) {
      const call = (fn) => { try { return fn() || []; } catch (e) { console.error(e); return []; } };
      if (modals.length) return call(modals[modals.length - 1].fn);
      const list = [...owners.values()].filter((o) => !o.screen || o.screen === screen).sort((a, b) => a.order - b.order || a.seq - b.seq);
      return list.flatMap((o) => call(o.fn));
    },
  };
}
