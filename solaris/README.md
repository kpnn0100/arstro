# Solaris — Digital Audio Workstation

> Status: **concept / specification — CLI-first**. No code yet; UI comes later on Artboard. This
> README is the design brief. Solaris is part of the [Arstro suite](../docs/vision.md) and is
> built on the shared [Nebula](../docs/shared-core.md) project/VCS/resource core, over the
> existing `DigitalSignalProcessing` engine.

## 1. What it is

Solaris is a multi-track DAW: arrange audio and MIDI clips on tracks, drive instruments and
effects with a per-track **processing rack**, automate parameters, and mix down to a master or
stems. It is non-destructive (the project is a graph of clips + params; audio is rendered on
demand), its project is a text [Nebula](../docs/shared-core.md) document, its branches auto-rebase,
and its mixdown/stems are **embeddable in Interstellar** as a live audio bed.

## 2. Why it shares the DSP engine

Solaris is a front-end over the same `DigitalSignalProcessing` library that powers **Pulsar** (the
existing synth). It reuses:

- **`synth/`** — `SynthEngine`, `VoiceManager`, `Voice`, `ParamId`: the polyphonic instrument that
  a MIDI track drives. Pulsar is effectively "one Solaris instrument track" as a standalone app.
- **`effects/`, `reverb/`, `equalizer/`, `envelope/`, `spatial/`, `generator/`, `simpleProcessor/`**
  — the processors a track's rack is built from.
- **`apps/kitchen_sink/RackEngine`** — the reference for chaining processors into a rack; Solaris
  generalizes it to one rack per track/bus.

So the same DSP that makes a sound in Pulsar makes a track in Solaris — and, because the sound is
DSP-graph + params (not baked audio), it is text-serializable and mergeable like everything else.

## 3. Core model

Solaris's Nebula schema (node types):

- **`track`** — a lane. `kind = audio | instrument | bus`. An `instrument` track hosts a
  `SynthEngine` and is driven by `note` clips; an `audio` track plays sample clips; a `bus`
  receives sends.
- **`clip`** — a time range on a track. Audio clip: `src=res:<hash>`, `in`/`out`, `start`, `gain`,
  `fade`. Note/MIDI clip: a `start`/`length` plus a list of **`note`** children.
- **`note`** — `(pitch, start, length, velocity)` — MIDI as plain text, so a part is diffable and
  mergeable note-by-note (semantic resolver unions by start-time).
- **`rack`** — an ordered chain of processors on a track/bus (the DSP graph): each entry is an
  **`effect`**/instrument node with its `ParamId` fields.
- **`automation`** — `(node, param, [time,value,easing]…)`: a parameter curve over time; curves
  merge by time.
- **`bus` / send** — mixer routing and levels.
- **`embed`** — a live reference to another Solaris project (reuse a stem/section), or the surface
  Interstellar embeds (`as=audio`).

Tempo/meter live at the project level (`bpm`, `sig`), so clip/note times can be beats or seconds.

## 4. Project format (illustrative)

A Nebula text project (see [shared-core.md §2](../docs/shared-core.md#2-the-text-project-format)):

```
arstro-project = 1
app = solaris
id  = prj_song50
bpm = 120   sig = 4/4   sampleRate = 48000

#track id=trk_drums kind=audio      order=0
#track id=trk_bass  kind=instrument order=1
#track id=trk_bus   kind=bus        order=2

#clip id=clp_dr track=trk_drums order=0 start=0 in=0 out=8 src=res:aa10… gain=-3.0
#clip id=clp_ba track=trk_bass  order=0 start=0 length=8
  #note pitch=36 start=0.0 length=0.5 vel=100
  #note pitch=36 start=1.0 length=0.5 vel=96
  #note pitch=43 start=2.0 length=1.0 vel=90

#rack track=trk_bass
  #effect id=fx_syn type=synth   osc=saw  cutoff=0.4 res:0.2 env.a=0.01 env.r=0.3
  #effect id=fx_eq  type=eq       low=+2 mid=-1 high=+1
  #effect id=fx_rev type=reverb   mix=0.18 size=0.7
#automation node=fx_syn param=cutoff  0:0.4 4:0.8 8:0.4  easing=easeInOut
```

Merges cleanly (the point of "merge two songs into one big song"): two arrangements union their
tracks; overlapping ids (forked from a shared ancestor) merge by field; note lists merge by time;
timelines **concatenate** (song B after song A) or **overlay** (layer B's tracks onto A) per the
merge policy ([shared-core.md §6](../docs/shared-core.md#6-project-merge-combining-two-projects)).

## 5. Embedding & the MV workflow

- **Into Interstellar.** Interstellar embeds a Solaris project `as=audio`
  ([shared-core.md §5](../docs/shared-core.md#5-cross-app-embedding--propagation)); Solaris renders
  the project (or named stems) to audio for the timeline. Because the embed follows a **branch**,
  the composer's revisions on Solaris `main` **auto-propagate** into the MV cut in Interstellar
  when there is no conflict — the exact behavior the suite is built around.
- **Solaris → Solaris.** A section (chorus, a stem group) can itself be an embed reused across
  songs, or pinned at a commit for a fixed reference.
- **Stems vs mixdown.** An embed can request the full mixdown or specific tracks/buses as stems,
  so Interstellar can duck music under dialogue or place stems on separate audio tracks.

## 6. CLI surface (idea-level)

CLI-first; commands are illustrative, not final:

```
solaris new <name.slp> --bpm 120 --sig 4/4 --sr 48000
solaris add-track <proj> --kind instrument --name Bass
solaris add-clip  <proj> --track trk_bass --start 0 --length 8
solaris add-note  <proj> --clip clp_ba --pitch 36 --start 0 --len 0.5 --vel 100
solaris rack      <proj> --track trk_bass --add synth,eq,reverb
solaris param     <proj> --node fx_syn --set cutoff=0.4 osc=saw
solaris automate  <proj> --node fx_syn --param cutoff --at 0=0.4 4=0.8 8=0.4
solaris branch    <proj> chorus-v2 --base main            # living branch (auto-rebase)
solaris merge     <song-a> <song-b> --into medley.slp --policy concatenate
solaris render    <proj> --branch main --out mix.wav       # or --stems trk_drums,trk_bass
```

`render` runs the DSP graph offline (instruments + racks + automation + mixer) to a master or
stems; `branch`/`merge`/`rebase` are Nebula operations shared with the other apps.

## 7. Engine & layering

- **Audio**: `DigitalSignalProcessing` (`SynthEngine`/`VoiceManager`/`Voice`, `effects`, `reverb`,
  `equalizer`, `envelope`, `spatial`, and the `RackEngine` chaining pattern).
- **Project / VCS / embed / resources**: [Nebula](../docs/shared-core.md).
- **UI (later)**: Artboard, mirroring Pulsar's panels and Cosmo's UI-free-core split (a
  `solaris_core` under an Artboard front-end); Pulsar's oscillator/filter/env/LFO panels are the
  starting point for the instrument editor.

## 8. Roadmap

1. **CLI + core**: Nebula schema, DSP rack per track, MIDI/note clips, automation, offline
   `render` (master + stems), branch/auto-rebase/merge.
2. **Interstellar embedding** with live propagation (stems + mixdown, branch-following).
3. **Artboard UI**: arrange view, piano roll, mixer, instrument/effect editors (reusing Pulsar).
