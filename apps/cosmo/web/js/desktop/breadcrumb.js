/*
 * Cosmo by arstro — desktop/breadcrumb.js: the path bar between the photo and the filmstrip.
 *
 * Ports widgets/Breadcrumb.cpp and App::bindViewToModel's crumbs (App.cpp:659-668): the names
 * from "All Photos" down to the group the filmstrip shows, plus the edited image's file name as
 * a trailing crumb when an image (not a group) is edited. 22.75 high, leftRailBg, hairlines at
 * 0 and 22.75; crumbs from x 9.75, each estimate(name, 10) wide, a 9 px chevron (stroke 1.0,
 * mutedFg @ 0.4) 3.25 after it and the next crumb 3.25 after that; 10 px Roboto on baseline
 * 14.875; the last crumb foreground @ 0.8 and inert, the others mutedFg -> foreground on hover
 * (HoverFade 120 ms). Hit-tested by x over the whole height.
 *
 * The shown group is this page's view state (vm.view.group via vm.group / vm.breadcrumb, built
 * from the model's parent ids); a click on a crumb navigates there (vm.openGroup).
 *
 * Deviation: the native's navigateToGroup also clears the selection; the grammar has no
 * "select none", so the selection (shared) is left as it is.
 */
import { computed } from "../core/signal.js";
import { h, bindEffect } from "../core/dom.js";
import { icon } from "../ui/icons.js";
import { ROOT } from "../vm/editor.js";
import { text, est, gestures, local, CRUMB_H } from "./editor.js";

export function mountBreadcrumb(center, vm, ed) {
  const bar = h("div.bc", {},
    h("div.bc-line.top"), h("div.bc-line.bottom"));
  const row = h("div.bc-row");
  bar.append(row);
  center.append(bar);

  const crumbs = computed(() => {
    const list = [{ name: "All Photos", group: ROOT }, ...vm.breadcrumb.value.map((n) => ({ name: n.name, group: n.node }))];
    // currentSourcePath(): the photo of the current slot (a still-loading selection keeps it).
    const slot = vm.currentSlot.value;
    const t = slot >= 0 ? vm.nodes.value.find((n) => n.slot === slot && n.kind === "image") : null;
    if (vm.editGroup.value < 0 && t && t.name) list.push({ name: t.name, group: null });
    return list;
  }, (a, b) => JSON.stringify(a) === JSON.stringify(b));

  let spans = [];
  bindEffect(bar, () => {
    const list = crumbs.value;
    row.replaceChildren();
    spans = [];
    let x = 9.75;
    list.forEach((c, i) => {
      const w = est(c.name, 10);
      const last = i === list.length - 1;
      const t = text(c.name, { x, y: CRUMB_H / 2 + 10 * 0.35, size: 10, cls: last ? "bc-last" : "bc-crumb" });
      row.append(t);
      spans.push({ x, w, group: c.group, el: t, last });
      x += w + 3.25;
      if (!last) {
        const chev = h("span.bc-chev", { style: { left: x + "px", top: CRUMB_H / 2 - 4.5 + "px" } }, icon("chevronRight", 9, 1.0));
        row.append(chev);
        x += 9 + 3.25;
      }
    });
  });

  const crumbAt = (p) => spans.find((s) => !s.last && p.x >= s.x && p.x <= s.x + s.w) || null;
  bar.addEventListener("pointermove", (e) => {
    const s = crumbAt(local(bar, e));
    for (const c of spans) c.el.classList.toggle("hover", c === s);
  });
  bar.addEventListener("pointerleave", () => { for (const c of spans) c.el.classList.remove("hover"); });
  gestures(bar, {
    click: (e, p) => {
      const s = crumbAt(p);
      if (s && s.group !== null) vm.openGroup(s.group);
    },
  });
}
