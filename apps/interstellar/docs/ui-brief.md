# Interstellar — UI brief

> The law is `.claude/skills/arstro.design.rule`, invoked and **not restated**. This file specifies
> only what is *about Interstellar*: its accent, its two screens and its three tabs.

**Interstellar is cosmo wearing one different colour.** That is the requirement, and it is also the
cheapest path: the tokens, the radii, the type ramp, the spacing ladder, the motion durations and
most of the widgets are *aliased or linked*, not rewritten.

## 1. The accent — the only forked token

```
primary           #CF5AED      ← purple-pink. THE single accent
primaryForeground #FFFFFF
ring              rgba(207, 90, 237, 0.5)
primaryAlpha(a)   rgba(207, 90, 237, a)
```

**Derived, not picked.** Cosmo's `#4F7EF7` in HSL is H 222°, S 91%, L 64%. Rotating the hue to
**288°** and easing saturation to **80%** gives `#CF5AED`: the same lightness and therefore the same
perceptual weight, so every token built on top of the accent — the focus ring, the 22% selection
wash, the segmented-control highlight — keeps working with no other number changed. Picking a
purple by eye would have forced a re-tune of all four.

Everything else is cosmo's, **aliased**: `background #141414`, `card #1C1C1C`,
`foreground #DBDBDB`, `muted`, `mutedForeground`, `border rgba(255,255,255,.072)`,
`destructive #E5534B`, `success #3FB950`, the per-surface literals, `hoverWash(t)`, all three radii,
all five font families, the 3.25 px ladder, the type ramp.

**One accent still means one accent.** `destructive` and `success` stay semantic and are not a
second and third. The timeline's playhead is `destructive` because it marks a position, not a
state — which is a domain adaptation, written down here as the rule requires.

## 2. Home

Cosmo's `HomeScreen` rhythm, on its coarser 8/16/32 grid: a wordmark, a recent-projects grid of
cards (name, footage count, size, last opened), and a sidebar with New, Open and Settings.
A card's cover is the **first frame of the project's first source**, which is the one picture that
says what a cut is.

- **Empty**: *"No projects yet — open some footage to start one."* Not a blank grid.
- **Loading**: skeleton cards at the real card geometry, so nothing moves when they fill.
- The grid reflow **animates** (260 ms): a column count derived from the window width is the
  canonical place this repo has shipped a snap.

## 3. Edit — three tabs over one monitor

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ interstellar.   « project »   [ Grade | Cut | Deliver ]   ‹ main ▾ ›      ⋯  │ 29.25
├─────────────────┬────────────────────────────────────────┬───────────────────┤
│  left column    │              MONITOR                    │   right column    │
│  (per tab)      │   outside the tab host: ONE widget,     │   (per tab)       │
│                 │   one frame, every tab                  │                   │
│                 ├────────────────────────────────────────┤                   │
│                 │  transport  ◀◀ ▶ ▶▶ ⌷  00:00:04:07     │                   │
├─────────────────┴────────────────────────────────────────┴───────────────────┤
│  deck (per tab: filmstrip · timeline · render queue)                         │
└──────────────────────────────────────────────────────────────────────────────┘
```

**The transport is Cut's and Deliver's** (R-UI-3, amended 2026-10-02): in Grade it eases away and
the monitor takes its room, showing the **Grade target alone at its reference frame** — the frame
being graded, as in cosmo — captioned `<bind> · ref <timecode>`. The transport's fourth button
(⌷, a camera, beside ▶▶) **captures the frame** — Copy Frame / Save Frame… (R-UI-11); in Grade the
same button sits left of the monitor caption.

**Ctrl + wheel zooms the monitor** about the pointer, as cosmo's photo stage does (R-UI-13): eased,
drag to pan, double-click to fit, a chip naming the magnification.

**Cosmo's menu bar sits after the wordmark** — File · Edit · Settings · Workspace · Preset (R-UI-7);
Engine Settings is cosmo's own dialog; the whole shell draws through cosmo's screen scale (R-UI-8).

**The version switcher (`‹ main ▾ ›`) is chrome**, beside the project name — because which version
you are editing is as present a fact as which project you are in (R-UI-4). A pinned or frozen
version shows a lock glyph and the commit, since that is the state people forget they are in.

### Grade
The tab that must feel exactly like cosmo, because it *is* cosmo: a **rack tree** on the left
(groups, sources, drag to regroup, grade weight, bypass) and cosmo's own `EditStackTabs` on the
right — `ParamPanel`, `MixerPanel`, `CurvePanel`, `GradePanel`, `XformPanel`, `HistogramWidget` —
linked, not reimplemented. The deck is a **filmstrip of rack sources**.

**Groups browse like cosmo** (R-UI-12): in the tree a group is shut until opened (a chevron, eased),
so grouping puts its members inside it; the filmstrip shows one level — the top or the open group —
a double-click on a folder chip drills in, and cosmo's breadcrumb in the SOURCES header
(`All sources › Day exteriors › A001_C003…`) goes back up.

New here, and only this: a **frame selector** under a video source's cell (which frame Cosmo
grades — a fast-seek slider over the whole source whose track is a strip of its frames; dragging
previews the frame, graded, in the monitor, release commits, ‹ › step one frame), a **used-by count** (zero is not an error — reference stills are legitimate), and a
**version-override badge** on any node this version has a `#tlgrade` on, with "revert to base" one
click away.

### Cut
Source bin, the **timeline**, and a clip inspector. Cuts only: no colour, no curves. A clip
inherited from the base draws plainly; one this version **overrode** carries the accent edge; one
the base has deleted under a dangling delta draws in `destructive` with the reason.

### Deliver
Render queue, output spec, and the lint report. **The timeline being rendered is named explicitly
in the queue row**, never implied by the open tab (R-RENDER-1). The output spec is the whole of R-RENDER-6 — codec and
profile or quality/speed/depth, size, rate, range — with the rows a codec lacks collapsed, and the
queue row says the spec in words.

## 4. States

`idle · hover · pressed · active · disabled · empty · loading · error/refused`, each shot.

| surface | empty | loading |
|---|---|---|
| Monitor | "no clip at the playhead" — not a black frame, which reads as a bug | "decoding", with the proxy level |
| Rack tree | "no footage yet — add some" | cosmo's own per-entry spinner cells |
| Timeline | "drag a source here", source bin highlighted | ruler drawn, tracks greyed |
| Render queue | "nothing queued" | per-job progress with a rate |

## 5. Verification

`interstellar_shots` renders every named state headlessly with `--size`, `--script`, `--tree` and
`--check` **from the first UI commit** — all four are cheap while a harness is new and each cost an
agent an investigation in cosmo. Two window sizes, mid-transition and at rest, and the live eased
value exposed wherever a test must tell a tween from a snap.
