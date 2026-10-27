/*
 * Interstellar — edit/nameprompt.js: the one-field (or several-field) modal. Ports
 * app/widgets/NamePrompt.{h,cpp}; edit/topbar.js registers it as the vm provider "prompt", which is
 * what ctx.prompt calls (New version…, Save preset, a keyframe's Speed & Influence…).
 *
 *   const np = namePrompt(layer)        layer = the shell's modal layer (root logical px)
 *   np.ask({title, message, value, ok, fields?: [{label, value}]}) -> Promise<string | string[] | null>
 *
 * The card is 360 wide, 168 high (with fields: 64 + 34 a row + 16 + 28 + 16), centred 24 px above
 * the middle; a scrim at surface::scrim(0.55); appear 150 ms in / 120 ms out EaseOutCubic with an
 * 8 px rise; Cancel (outline, hover wash) and the confirm button (primary, brightened 0.14 on hover)
 * 88 × 28. Esc cancels, Enter confirms, Tab moves between fields, a click outside cancels, and the
 * dialog CLOSES BEFORE its answer is delivered (design rule §5). A single name is trimmed; an empty
 * one keeps the dialog open (NamePrompt.cpp:118-127). The field is a real <input> (caret, selection,
 * clipboard, a phone's keyboard) dressed as the native TextBox: input fill, border → ring on focus,
 * Roboto 12, caret primary, selection primaryAlpha(0.38).
 */
import { h, own } from "../core/dom.js";
import { useCss } from "../kit/css.js";
import { tok } from "../kit/tokens.js";
import { width as measure, ellipsize, baseline } from "../kit/textfit.js";
import { Painter, canvasSvg, col, fade, brighten, hoverWash, Anim, HoverFade, frameLoop, localPoint, inRect, Ease, F } from "../kit/paint.js";

const CARD_W = 360, CARD_H = 168, PAD = 16, FIELD_H = 28, BTN_H = 28, BTN_W = 88, ROW_H = 34, LABEL_W = 150, MAX_FIELDS = 5;
const HOVER_FILL_LIFT = 0.14;

export function namePrompt(layer) {
  useCss("css/edit/nameprompt.css");
  const el = h("div.np");
  const svg = canvasSvg();
  el.append(svg);
  layer.append(el);
  const p = new Painter(svg);
  const fields = [];
  for (let i = 0; i < MAX_FIELDS + 1; ++i) {
    const f = h("input.np-field", { type: "text", spellcheck: "false", autocomplete: "off" });
    f.addEventListener("keydown", onKey);
    el.append(f);
    fields.push(f);
  }
  let open = false, closing = false, multi = false, count = 0, title = "", message = "", okLabel = "OK", labels = [];
  let resolveFn = null, W = 0, H = 0, startPending = false;
  const appear = new Anim(0);
  const hover = new HoverFade();
  const loop = frameLoop(step);

  const cardH = () => (multi ? 64 + count * ROW_H + PAD + BTN_H + PAD : CARD_H);
  const cardRect = () => ({ x: (W - CARD_W) * 0.5, y: (H - cardH()) * 0.5 - 24, w: CARD_W, h: cardH() });
  function buttonRect(i) {
    const c = cardRect(), y = c.y + c.h - PAD - BTN_H, right = c.x + c.w - PAD;
    return i === 1 ? { x: right - BTN_W, y, w: BTN_W, h: BTN_H } : { x: right - 2 * BTN_W - 8, y, w: BTN_W, h: BTN_H };
  }
  const isOpen = () => open && !closing;

  function ask(o = {}) {
    if (resolveFn) finish(null);
    W = layer.offsetWidth; H = layer.offsetHeight;
    multi = Array.isArray(o.fields) && o.fields.length > 0;
    count = multi ? Math.min(o.fields.length, MAX_FIELDS) : 0;
    title = o.title || ""; message = o.message || ""; okLabel = o.ok || "OK";
    labels = multi ? o.fields.slice(0, count).map((f) => f.label || "") : [];
    fields.forEach((f, i) => {
      const shown = multi ? i < count : i === MAX_FIELDS;
      f.style.display = shown ? "block" : "none";
      f.value = multi ? (i < count ? String(o.fields[i].value ?? "") : "") : (i === MAX_FIELDS ? String(o.value ?? "") : "");
    });
    open = true; closing = false; startPending = true;
    el.classList.add("on");
    layout();
    const first = multi ? fields[0] : fields[MAX_FIELDS];
    requestAnimationFrame(() => { first.focus(); first.select(); });
    loop.kick();
    return new Promise((resolve) => { resolveFn = resolve; });
  }
  function finish(v) {
    const r = resolveFn;
    resolveFn = null;
    if (r) setTimeout(() => r(v), 0);       // after the close has started (design rule §5)
  }
  function beginClose() {
    if (!open || closing) return;
    closing = true;
    appear.animateTo(0, tok.ms("motion-modal-close") || 120, Ease.EaseOutCubic, performance.now());
    for (const f of fields) f.blur();
    loop.kick();
  }
  function confirm() {
    if (!isOpen()) return;
    if (multi) { beginClose(); finish(fields.slice(0, count).map((f) => f.value)); return; }
    const name = fields[MAX_FIELDS].value.trim();
    if (!name) return;                       // nothing to create: stay open
    beginClose();
    finish(name);
  }
  function cancel() { if (!isOpen()) return; beginClose(); finish(null); }

  function onKey(e) {
    if (!isOpen()) return;
    e.stopPropagation();                     // a modal consumes every key while it is up
    if (e.key === "Escape") { e.preventDefault(); cancel(); }
    else if (e.key === "Enter") { e.preventDefault(); confirm(); }
    else if (e.key === "Tab" && multi && count > 0) {
      e.preventDefault();
      const i = Math.max(0, fields.indexOf(e.target));
      const n = (i + 1) % count;
      fields[n].focus(); fields[n].select();
    }
  }

  function layout() {                        // NamePrompt.cpp:157-171
    const c = cardRect(), rise = 8 * (1 - appear.value);
    const place = (f, x, y, w) => Object.assign(f.style, { transform: `translate(${x}px, ${y + rise}px)`, width: Math.max(0, w) + "px", height: FIELD_H + "px" });
    place(fields[MAX_FIELDS], c.x + PAD, c.y + 72, c.w - 2 * PAD);
    for (let i = 0; i < MAX_FIELDS; ++i) place(fields[i], c.x + PAD + LABEL_W, c.y + 60 + i * ROW_H, c.w - 2 * PAD - LABEL_W);
    for (const f of fields) f.style.opacity = appear.value;
  }

  function step(now) {
    W = layer.offsetWidth; H = layer.offsetHeight;
    if (startPending) { appear.animateTo(1, tok.ms("motion-modal-open") || 150, Ease.EaseOutCubic, now); startPending = false; }
    let moving = appear.update(now);
    if (closing && !appear.animating && appear.value <= 0.001) { open = false; closing = false; el.classList.remove("on"); }
    moving = hover.advance(now) || moving;
    layout();
    paint();
    return moving;
  }

  function paint() {                         // NamePrompt.cpp:210-251
    const a = appear.value;
    p.begin();
    if (a > 0.001) {
      p.rect(0, 0, W, H, 0, { fill: tok.rgba("scrim", 0.55 * a) });
      const c = cardRect();
      c.y += 8 * (1 - a);
      const rc = tok.px("radius-control"), fg = col("foreground"), muted = col("muted-foreground");
      p.rect(c.x, c.y + 6, c.w, c.h, rc, { fill: [0, 0, 0, 0.35 * a] });
      p.rect(c.x, c.y, c.w, c.h, rc, { fill: fade(col("popover"), a), stroke: fade(col("border"), a), sw: 1 });
      p.text(ellipsize(title, c.w - 2 * PAD, 14, F.semibold), c.x + PAD, c.y + PAD + 14, 14, F.semibold, fade(fg, a));
      p.text(ellipsize(message, c.w - 2 * PAD, 12, F.sans), c.x + PAD, c.y + PAD + 38, 12, F.sans, fade(muted, a));
      if (multi) for (let i = 0; i < count; ++i)
        p.text(ellipsize(labels[i], LABEL_W - 8, 11, F.sans), c.x + PAD, baseline(c.y + 60 + i * ROW_H + FIELD_H * 0.5, 11), 11, F.sans, fade(muted, a));
      for (let i = 0; i < 2; ++i) {
        const r = buttonRect(i);
        r.y += 8 * (1 - a);
        const hv = hover.amount(i);
        if (i === 1) p.rect(r.x, r.y, r.w, r.h, rc, { fill: fade(brighten(col("primary"), HOVER_FILL_LIFT * hv), a) });
        else p.rect(r.x, r.y, r.w, r.h, rc, { fill: fade(hoverWash(hv), a), stroke: fade(col("border"), a), sw: 1 });
        const label = i === 1 ? okLabel : "Cancel", fam = i === 1 ? F.semibold : F.sans;
        p.text(label, r.x + (r.w - measure(label, 12, fam)) * 0.5, baseline(r.y + r.h * 0.5, 12), 12, fam,
               fade(i === 1 ? col("primary-foreground") : fg, a));
      }
    }
    p.end();
  }

  // a modal swallows everything while it is up; a click outside the card cancels
  el.addEventListener("pointermove", (e) => {
    const pt = localPoint(el, e);
    hover.setHovered(inRect(pt, buttonRect(0)) ? 0 : inRect(pt, buttonRect(1)) ? 1 : -1);
    loop.kick();
  });
  el.addEventListener("pointerdown", (e) => { if (e.target === el || e.target === svg) e.preventDefault(); });
  el.addEventListener("click", (e) => {
    if (!isOpen() || e.target.classList.contains("np-field")) return;
    const pt = localPoint(el, e);
    if (inRect(pt, buttonRect(0))) cancel();
    else if (inRect(pt, buttonRect(1))) confirm();
    else if (!inRect(pt, cardRect())) cancel();
  });

  return { ask, isOpen, cancel, confirm, el, destroy() { loop.kill(); el.remove(); } };
}
