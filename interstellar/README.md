# Interstellar — professional video editor (colour blending & edit)

> Status: **concept / specification — CLI-first**. No code yet; UI comes later on Artboard. This
> README is the design brief. Interstellar is part of the [Arstro suite](../docs/vision.md) and
> is built on the shared [Nebula](../docs/shared-core.md) project/VCS/resource core.

## 1. What it is

Interstellar is a non-linear video editor whose distinguishing focus is **colour blending** — not
just per-clip grading, but the way colour is *composited and interpolated across layers and across
cuts*. It shares Cosmo's colour engine (`ImageProcessing::EditParams`) applied **per frame**, adds
a **timeline** with keyframing and transitions, and treats the score as a **live embedded Solaris
project**. Every edit is non-destructive; the project is a text [Nebula](../docs/shared-core.md)
document; branches auto-rebase; sources are referenced from the shared resource pool.

## 2. Why it shares the Cosmo colour engine

A grade authored in Cosmo and a grade authored in Interstellar are **the same data**
(`EditParams`: exposure/contrast/tone/curve/mixer/grade/…). That is what lets the director lock a
look on stills in Cosmo and have Interstellar reproduce it exactly on moving footage. Interstellar
adds only what *time* requires: sampling a clip's grade per frame, keyframing any parameter over
time, and blending between two grades across a cut.

- **Per-frame colour** = ImageProcessing renders each frame as an image with that frame's effective
  `EditParams`.
- **Shared look library** = LUTs/presets are resource-pool assets, so a Cosmo preset *is* an
  Interstellar preset (see [shared-core.md §7](../docs/shared-core.md#7-the-resource-pool)).

## 3. Core model

Interstellar's Nebula schema (node types):

- **`track`** — an ordered lane. `kind = video | audio | adjustment`. Video tracks composite
  top-over-bottom; an `adjustment` track applies a grade to everything beneath it.
- **`clip`** — a time range of a source on a track: `src=res:<hash>`, `in`/`out` (source range),
  `start` (timeline position via `order`/time), `speed`, plus a non-destructive **grade** stack
  (`EditParams`) and a **`blend`** (mode + opacity).
- **`grade`** — an `EditParams` value (shared with Cosmo), attachable to a clip, a track, or an
  adjustment layer.
- **`transition`** — a timed blend between two adjacent clips/looks (dissolve, wipe, and — the
  focus — **grade interpolation**: ease `EditParams` A → B across the cut).
- **`keyframe`** — `(node, field, time, value, easing)`; any numeric parameter can be animated
  over time. Keyframe lists merge by time (semantic resolver).
- **`embed`** — a live reference to another project (Cosmo look, Solaris song); see §5.

## 4. Colour blending — the focus

The feature Interstellar is organized around, in three layers:

1. **Layer compositing.** Stacked video tracks blend with per-clip **blend modes** + opacity
   (normal/over/multiply/screen/add/…), evaluated in the ImageProcessing pipeline so the result is
   identical to grading a single composited frame. This is "colour blending" between *layers*.
2. **Grade interpolation across time.** Because a grade is `EditParams` (numeric), Interstellar can
   **ease one grade into another** — keyframed within a clip, or as a `transition` across a cut
   (shot A's look dissolving into shot B's look). This is "colour blending" across *time*.
3. **Shared looks.** Grades, LUTs, and presets are pool assets shared with Cosmo, so a mood locked
   on stills propagates to the edit, and a look tweak in Cosmo `main` flows into the cut via the
   embed + auto-rebase (§5).

Non-goals for v1 (kept explicit): GPU real-time playback, effects beyond colour/composite/transform,
and node-graph compositing — the focus is colour + cut, not a full VFX compositor.

## 5. Embedding (the workflow glue)

Interstellar is where the suite comes together. It embeds two kinds of source project as **living**
references (branch-following, auto-rebasing — see
[shared-core.md §5](../docs/shared-core.md#5-cross-app-embedding--propagation)):

- **`embed … as=look` (Cosmo project)** — the graded stills become the project's colour look; the
  look is applied to footage per frame. When the director revises the grade on Cosmo `main` and
  there is no conflict, the cut's colour updates automatically.
- **`embed … as=audio` (Solaris project)** — the song/stems become the timeline's audio bed on an
  audio track. When the composer revises the song on Solaris `main` and there is no conflict, the
  MV's audio updates automatically; a cut that depends on a removed section is flagged, not broken.

Both embeds can be **pinned** (`branch=main@<commit>`) to freeze a delivery.

## 6. Project format (illustrative)

A Nebula text project (see [shared-core.md §2](../docs/shared-core.md#2-the-text-project-format)):

```
arstro-project = 1
app = interstellar
id  = prj_mv01
fps = 24   width = 3840   height = 2160

#track id=trk_v0 kind=video order=0
#track id=trk_v1 kind=video order=1
#track id=trk_a0 kind=audio order=2

#clip id=clp_a track=trk_v0 order=0 start=0.000 in=12.40 out=16.60 src=res:9c1f…
  grade = exposure:0.2 contrast:8 temp:5400 curve:0,0;0.4,0.5;1,1
  blend = normal opacity:1.0
#clip id=clp_b track=trk_v0 order=1 start=4.200 in=88.00 out=91.10 src=res:9c1f…
  grade = exposure:-0.1 mixer:hue@30:0.2
#transition id=tr_ab between=clp_a,clp_b kind=grade-dissolve dur=0.5 easing=easeInOut

#embed id=emb_look target=cosmo:prj_look77   branch=main as=look
#embed id=emb_song target=solaris:prj_song50 branch=main as=audio track=trk_a0
```

Merges cleanly: two editors' cut branches auto-rebase over the same `main`; two separate cut
projects can be unioned into one longer video (concatenate policy,
[shared-core.md §6](../docs/shared-core.md#6-project-merge-combining-two-projects)).

## 7. CLI surface (idea-level)

CLI-first; commands are illustrative, not final:

```
interstellar new <name.isp> --fps 24 --res 3840x2160
interstellar add-track <proj> --kind video
interstellar add-clip  <proj> --track trk_v0 --src <media> --in 12.4 --out 16.6 --start 0
interstellar grade     <proj> --clip clp_a --set exposure=0.2 contrast=8 temp=5400
interstellar transition <proj> --between clp_a,clp_b --kind grade-dissolve --dur 0.5
interstellar embed     <proj> --look   cosmo:prj_look77@main
interstellar embed     <proj> --audio  solaris:prj_song50@main --track trk_a0
interstellar branch    <proj> mv-cut-v1 --base main        # living branch (auto-rebase)
interstellar rebase    <proj> --status                     # show clean / conflicting nodes
interstellar merge     <proj-a> <proj-b> --into big-cut.isp --policy concatenate
interstellar render    <proj> --branch mv-cut-v1 --out master.mov [--range a:b]
```

`render` composites tracks (colour per frame via ImageProcessing) and mixes the embedded Solaris
audio; `branch`/`rebase`/`merge` are Nebula operations shared with the other apps.

## 8. Engine & layering

- **Colour / per-frame**: `ImageProcessing` (`EditParams`, the same pipeline Cosmo uses).
- **Temporal**: a thin *VideoProcessing* layer — decode/sample source clips to frames, evaluate
  keyframes, composite/blend tracks, resolve transitions. Frame colour is delegated to
  ImageProcessing so a still and a frame grade identically.
- **Project / VCS / embed / resources**: [Nebula](../docs/shared-core.md).
- **UI (later)**: Artboard, mirroring Cosmo's split (a UI-free `interstellar_core` under a GTK/
  Artboard front-end).

## 9. Roadmap

1. **CLI + core**: Nebula schema, ImageProcessing per-frame render, timeline sampling,
   grade/transition, embed resolution, branch/auto-rebase/merge, `render`.
2. **Solaris/Cosmo embedding** end-to-end with live propagation.
3. **Artboard UI**: timeline, colour panels (reusing Cosmo's grade widgets), player.
