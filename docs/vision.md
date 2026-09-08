# The Arstro Creative Suite — Vision

> Status: **concept / specification**. This document describes the intended product and its
> shared design. No implementation is committed for Interstellar or Solaris yet; both start
> **CLI-first**, with an Artboard UI to follow. Cosmo already exists and is the reference app.

## 1. One-paragraph overview

Arstro is a suite of three non-destructive creative tools that share one foundation:

- **Cosmo** — a photo editor (colour / tone / mood on stills). *Exists today.*
- **Interstellar** — a professional video editor focused on **colour blending and edit**.
- **Solaris** — a **Digital Audio Workstation** (DAW).

The three apps are different front-ends over the **same shared substrate**: a common
**text-based, mergeable project model** with a built-in **branch + auto-rebase** version-control
engine, a **content-addressed resource pool**, and **cross-app embedding**. Because all three
speak the same project model, a project made in one app can be **embedded** into another and
kept **live**: when the source changes, the embedding updates automatically (if there is no
conflict). That is what turns three separate tools into one workflow.

## 2. The core idea, in the user's words

> When shooting an MV, the user edits it in **Cosmo** to lock the tone and the mood as static
> images, then embeds that project into **Interstellar** for the video edit. The user makes the
> music in **Solaris** and embeds it directly into Interstellar. They **fork a version from the
> main branch** to cut the MV / add sound effects. Branches **auto-rebase**: every new commit on
> a base branch propagates to the feature branches, so when the original music or grade changes
> and there is no conflict, the MV updates too. Project files are **text-based and easy to
> merge** — e.g. merge two music projects into one big song, or merge two edits into one big
> video.

This document makes that idea concrete. The shared machinery lives in
[shared-core.md](shared-core.md) (the **Nebula** core); the two new apps are detailed in
[interstellar/README.md](../apps/interstellar/README.md) and
[solaris/README.md](../apps/solaris/README.md).

## 3. The shared foundation

Arstro already layers its libraries by function. The suite adds one more shared library —
**Nebula** — that carries the project model and version control every app needs.

| Layer | Module | Role | Used by |
|-------|--------|------|---------|
| UI | **Artboard** | platform-free 2D drawing + UI framework (HAL) | all apps (when UIs land) |
| Media engine | **ImageProcessing** | non-destructive image/colour pipeline (`EditParams`) | Cosmo, **Interstellar** (per-frame colour) |
| Media engine | **DigitalSignalProcessing** | synth + effects/reverb/EQ + rack (`SynthEngine`, `RackEngine`) | Pulsar, **Solaris** |
| Media engine | *VideoProcessing* (thin) | temporal layer over ImageProcessing: frames, timeline sampling, blend/composite over time. (**AMENDED 2026-09-08:** this lands as `interstellar_core`'s evaluator + compositor, **not** a new shared library — there is no second consumer yet, and `ImageProcessing/src/video/VideoProcessor.h` is a 49-line stub with no time model. It is promoted to a library when something else needs it.) | **Interstellar** |
| Expression | **Gene** (promoted) | the parse-once binding language that lets any parameter be a calculation over other parameters. Lives in `genesis_core` today; promoted to a shared library and aliased back (**added 2026-09-08**) | Genesis, **Interstellar** |
| **Project / VCS / resource** | **Nebula** (new) | the shared **text project model**, **branch + auto-rebase**, **semantic merge**, **cross-app embedding**, **content-addressed resources** | Cosmo, Interstellar, Solaris |

The key insight: colour is already shared (Cosmo and Interstellar both grade with
`ImageProcessing::EditParams`), and audio is already shared (Solaris builds on the same DSP as
Pulsar). **Nebula** is the missing piece that makes the *projects themselves* shareable,
versioned, and composable across apps.

## 4. Four shared principles

These are the same in all three apps (specified in [shared-core.md](shared-core.md)):

1. **Text-based, mergeable projects.** A project is a plain-text file (or a small text tree) of
   stable-id nodes with `key=value` fields — never a binary blob and never raw pixels/samples.
   The format is designed so both a line-based 3-way merge and a structural (semantic) merge
   work, so two projects can be combined and two branches reconciled cleanly.

2. **Branch + auto-rebase ("living branches").** Every project is its own tiny repository with a
   `main` and any number of feature branches. A feature branch declares a **base**; whenever the
   base advances, the feature branch **automatically rebases** onto the new base tip using the
   semantic merge. Clean → the branch silently moves forward with the base's new work folded in;
   conflict → the rebase pauses and flags exactly the nodes that clash. "Every new commit on the
   base branch reaches the feature branch too."

3. **Cross-app embedding + propagation.** A project can **embed** another project as a node —
   a Cosmo look inside an Interstellar clip, a Solaris song inside an Interstellar timeline. The
   embed tracks a *branch* of the source (not a frozen copy). The same auto-rebase machinery
   applies across the boundary: when the embedded source's branch advances, the host re-resolves
   the embed; no conflict → the host updates automatically; conflict → the host branch flags it.

4. **Content-addressed resources.** Heavy media (footage, stills, audio, MIDI, LUTs, presets)
   lives in a shared **resource pool**, referenced by `(path, content-hash)` and never copied
   into the project text. This keeps projects small and textual, makes moves/renames detectable,
   and lets all three apps reference the exact same source asset.

## 5. The end-to-end MV workflow (concrete walkthrough)

The story in §2, step by step, showing which mechanism does the work:

1. **Shoot.** Footage + stills land in the shared **resource pool** (referenced by hash).
2. **Grade in Cosmo — the video sources themselves, beside the stills.** The director opens the
   footage *and* the behind-the-scenes photographs in one Cosmo project, groups them, and grades
   each source separately with Cosmo's grouping and stacking. A video source is graded on an
   extracted **reference frame**, so Cosmo needs no concept of time. The Cosmo project (`.cmp`) is
   the **rack**.
3. **Start the edit in Interstellar.** A new Interstellar project **embeds that Cosmo project**
   (branch `main`) as its `as=rack` — **live and writable**. Interstellar renders each frame with
   the source's effective params from the rack (the same `ImageProcessing` engine), and a colour
   edit made *in Interstellar* is an edit to the Cosmo project: no import, no export, no
   re-synchronisation.

   > **AMENDED 2026-09-08.** Step 2 used to say the director grades "representative frames" and
   > step 3 that Interstellar embeds the result "as its colour look" — read-only, one-directional.
   > The requirement is two-directional (*"all edits in interstellar can apply back to cosmo"*), so
   > Interstellar now **hosts a real `CosmoService`** and the Cosmo project is the single colour
   > authority for both apps. Interstellar's timeline carries **no colour at all**. See
   > [../apps/interstellar/REQUIREMENTS.md](../apps/interstellar/REQUIREMENTS.md) `R-COSMO`.
4. **Score in Solaris.** The composer builds the track in Solaris (a text project of tracks /
   clips / MIDI / rack params). Interstellar **embeds the Solaris project** (branch `main`) as
   the audio bed.
5. **Fork to cut the MV.** The editor forks Interstellar `main` → feature branch `mv-cut-v1`,
   cuts clips, adds SFX, tweaks transitions. `mv-cut-v1` is a **living branch** based on `main`.
6. **The source changes.** The composer revises the song on Solaris `main`; the director nudges
   the grade on Cosmo `main`. Both embeds in Interstellar follow `main`, so the embed pointers
   advance and Interstellar's `mv-cut-v1` **auto-rebases**:
   - no conflict → the MV's audio and colour **update automatically** on the cut branch;
   - conflict (e.g. a cut lands on a bar the composer deleted) → `mv-cut-v1` flags the exact
     clip/region for the editor to resolve, without losing their cuts.
7. **Combine.** Two editors' cuts (`mv-cut-A`, `mv-cut-B`) **merge into one** longer video; two
   of the composer's sketches **merge into one** bigger song — because the project text is
   structured for union/overlay/concatenate merges (see [shared-core.md](shared-core.md) §7).
8. **Deliver.** Interstellar renders the timeline (colour per frame + the embedded Solaris
   mixdown) to a master; nothing along the way mutated a source asset.

## 6. How the apps relate

```
        resource pool  (footage · stills · audio · MIDI · LUTs · presets — content-addressed)
                 ▲              ▲                     ▲
                 │              │                     │
   ┌─────────────┴───┐  ┌───────┴────────┐    ┌───────┴────────┐
   │ Cosmo (photo)   │  │ Solaris (DAW)  │    │  Interstellar  │
   │ ImageProcessing │  │ DSP engine     │    │  (video edit)  │
   └─────────┬───────┘  └───────┬────────┘    │  timeline +    │
             │ as=rack  ⇄       │ as=audio  ► │  automation +  │
             │ (WRITABLE:       │ (read-only  │  bindings      │
             │  colour lives    │  in v1)     │                │
             │  ONLY here)      │             │ hosts a real   │
             └──────────────────┴────────────►│ CosmoService   │
                                               └───────┬────────┘
                     Nebula: text projects · branch+auto-rebase · semantic merge · embeds
```

- **Cosmo ⇄ Interstellar**: the Cosmo project *is* Interstellar's colour authority, embedded live
  and writable (`as=rack`). Interstellar cuts and animates; Cosmo grades; the `.cmp` is the one
  place a source's colour exists.
- **Solaris → Interstellar**: a song / stems become the audio bed of the timeline.
- **Cosmo ↔ Solaris**: not directly embedded, but both feed Interstellar and share Nebula.
- All three: same project semantics, same VCS, same resource pool.

## 7. Why CLI-first

Every app is specified **CLI-first** so the shared model, versioning, embedding, and rendering
can be built and tested headlessly (scriptable, CI-able, no UI dependency) before an Artboard UI
is layered on — exactly how Cosmo's UI-free `cosmo_core` sits under its GTK front-end. The CLI is
not a throwaway: it is the automation surface (batch renders, pipeline hooks, merge tooling) that
survives after the UI ships.

## 8. Documents

- [shared-core.md](shared-core.md) — **Nebula**: the text project format, the branch + auto-rebase
  engine, semantic merge, cross-app embedding + propagation, and the resource pool.
- [../apps/interstellar/README.md](../apps/interstellar/README.md) — the video editor.
- [../apps/solaris/README.md](../apps/solaris/README.md) — the DAW.
- [../apps/cosmo/docs/](../apps/cosmo/docs/) — the existing Cosmo app (reference for the app shape).
