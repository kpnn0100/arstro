# Interstellar — video colour, grouped and versioned

> **Status: specified, rebuilding.** The first implementation (git `69b91eb`) is removed and its
> specification withdrawn — see [`REQUIREMENTS.md`](REQUIREMENTS.md) for why. Conformance rung 0.

## What it is

**A colour tool for video, with an editor attached.** Its subject is grouping and blending colour
across the footage of a production; its timeline exists so a graded look can be cut, versioned and
delivered.

That sentence is the correction at the centre of this rebuild. The first version made the timeline
the app and colour an embedded afterthought, and it shipped twice as a cutting tool whose colour
authority was a test fake.

## The three ideas

**1. Colour lives in Cosmo, really.** Interstellar hosts a live `cosmo::CosmoService` and drives it
with `cosmo::Command`s. A grade made here enters Cosmo's history and is written into the `.cmp`;
open that file in Cosmo and the edit is there. No import, no export, nothing to synchronise —
because there is nothing to synchronise. A video source is graded on an extracted **reference
frame**, so Cosmo needs no concept of time and no change at all.

**2. A timeline is a version.** A project holds one rack of footage and many cuts of it. A timeline
may declare a `base`, and then it is *"my base, resolved live, with my deltas on top"* — colour and
arrangement behaving identically. A regrade on `main` reaches every open version the moment it
lands; an override always wins; a delivery **pins** colour and **freezes** its cut; `rebase`
reconciles what has dangled and advances the pin.

**3. Video is a volume.** `(x, y, t)`, lazy — an interface, not storage, because ten seconds of
1080p is 6 GB in linear float. The accessor is a **window**, never a point, so the cost is visible
at the call site; a view is raw memory, resolved once per window and hoisted once per row; a
processor **declares its temporal radius**, so residency is bounded by the filter and not by the
clip. That is what makes temporal denoise expressible at all — today's seventeen stages are all
radius 0 and none of them can see a neighbouring frame.

## Surfaces

**Home** — projects. **Edit** — three tabs over one monitor: **Grade** (the rack: grouping and
colour, cosmo's own panels), **Cut** (arrangement and the version switcher), **Deliver** (render a
*named* timeline). Cosmo's design exactly, with one token changed: the accent is purple-pink
`#CF5AED`, which is cosmo's blue rotated to hue 288° at the same lightness so every alpha built on
it still works.

## Driven by an agent

Everything is a `Command`; everything observable is an `AppModel` field or an `Event`. The API
document is **generated from the same tables the parser uses, committed, and drift-tested** —
rung 4 of the suite's conformance ladder, which no Arstro app has reached.

```
interstellar-cc api --json
interstellar-cc rack import japan18.cmp
interstellar-cc set gr1.basic.exposure=0.2          # writes THROUGH to the .cmp
interstellar-cc timeline new social30 --base main
interstellar-cc render --timeline social30 --out master.mp4
```

## Documents

| question | document |
|---|---|
| what was asked for, and its status | [`REQUIREMENTS.md`](REQUIREMENTS.md) |
| what the code does today | [`docs/requirements.md`](docs/requirements.md) |
| what is in an `.isp`, and the command grammar | [`docs/project-format.md`](docs/project-format.md) |
| the audio schema Solaris will share | [`../../docs/audio-format.md`](../../docs/audio-format.md) |
| layers, seams, the volume, version resolution | [`docs/architecture.md`](docs/architecture.md) |
| the accent, the screens, the states | [`docs/ui-brief.md`](docs/ui-brief.md) |
| what is done and what is next | [`docs/PROGRESS.md`](docs/PROGRESS.md) |
| known bugs | [`docs/DEFECTS.md`](docs/DEFECTS.md) |

Suite context: [`../../docs/vision.md`](../../docs/vision.md) ·
[`../../docs/shared-core.md`](../../docs/shared-core.md) · [`../cosmo/docs/`](../cosmo/docs/)
