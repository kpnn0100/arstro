/*
 * Interstellar — home/loading.js: Screen::Loading, a project is opening. Ports
 * app/widgets/LoadingView.{h,cpp}.
 *
 * The small wordmark where the Edit top bar will put it, the project's name, "Opening the rack and
 * its versions…" and an INDETERMINATE bar: a segment (32 % of a 240 px track) sweeping ping-pong
 * with an ease-in-out over motion::kPulseMs (1400 ms) - a timing driven by phase, not a tween
 * (LoadingView.cpp:17-25); reduced motion parks it in the middle. The name is the model's
 * projectName once the service has one, else what this page asked to open (App.cpp:45, 535, 1300).
 * The view arrives and leaves through the shell's screen cross-fade (260 ms).
 */
import { own } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { reducedMotion } from "../core/motion.js";
import { tok, SHELL } from "../kit/tokens.js";
import { width as measure, ellipsize, baseline, fontEpoch } from "../kit/textfit.js";
import { Painter, canvasSvg, col, frameLoop, Ease, F } from "../kit/paint.js";

const BAR_W = 240, SEG_FRAC = 0.32;                 // LoadingView.cpp:13-14

export function mount(host, vm, ctx) {
  const svg = canvasSvg();
  host.append(svg);
  const p = new Painter(svg);
  let W = 0, H = 0, name = "";
  const loop = frameLoop(step);
  own(host, () => loop.kill());

  own(host, effect(() => {
    const m = vm.model.value;
    const n = m && m.screen !== "home" && m.projectName ? m.projectName : "";
    name = n || vm.view.openingName.value;
    if (vm.screen.value === "loading") loop.kick();
  }));
  own(host, effect(() => { const b = ctx.box.value; W = b.w; H = b.h; fontEpoch.value; loop.kick(); }));

  function sweep(now) {                             // LoadingView.cpp:17-25
    if (reducedMotion()) return 0.5;
    const period = tok.ms("motion-pulse");
    const phase = (now % period) / period;
    const tri = phase < 0.5 ? phase * 2 : 2 - phase * 2;
    return Ease.EaseInOutCubic(tri);
  }

  function step(now) {
    if (!(W > 0) || !(H > 0)) return false;
    const fg = col("foreground");
    p.begin();
    p.rect(0, 0, W, H, 0, { fill: col("background") });
    // the small wordmark, top-left, where the Edit top bar will put it
    const sp = -0.03 * 13, base = baseline(SHELL.topBarH * 0.5, 13);
    p.text("interstellar", 9.75, base, 13, F.semibold, fg, sp);
    p.text(".", 9.75 + measure("interstellar", 13, F.semibold, sp), base, 13, F.semibold, col("primary"), sp);

    const cy = H * 0.45;
    const nm = ellipsize(name || "Opening project", W - 64, 14, F.semibold);
    p.text(nm, (W - measure(nm, 14, F.semibold)) * 0.5, cy, 14, F.semibold, fg);
    const s = "Opening the rack and its versions…";
    p.text(s, (W - measure(s, 11, F.sans)) * 0.5, cy + 22, 11, F.sans, col("muted-foreground"));

    // indeterminate: a segment sweeping the track
    const bx = (W - BAR_W) * 0.5, by = cy + 40, pill = tok.px("radius-pill");
    p.rect(bx, by, BAR_W, 3, pill, { fill: col("secondary") });
    const segW = BAR_W * SEG_FRAC;
    p.rect(bx + (BAR_W - segW) * sweep(now), by, segW, 3, pill, { fill: col("primary") });
    p.end();
    // the sweep runs only while this screen is the one shown (or fading)
    return vm.screen.peek() === "loading" && !reducedMotion();
  }
  loop.kick();
}
