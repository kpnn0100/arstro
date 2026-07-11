# Solaris — Definition Questionnaire

Answer these and they become the Solaris **requirements** (`requirements.md`). This is the
elicitation checklist that pins down every decision the requirements depend on.

**How to use**
- Each question lists options; **★ marks my recommended default** for a first, shippable CLI DAW.
- Write your pick (or your own answer) on the `→ Answer:` line. "★" alone = accept the default.
- 🔒 = **blocking**: needed before I can write requirements / before M0 (contract-freeze). The rest
  can be deferred (answer "defer" and I'll spec the ★ default and mark it revisable).
- Fast path: reply "**all ★ except: …**" and list only your overrides.

Legend: 🔒 blocking · ★ recommended default · ◦ option

---

## 1. Product scope & positioning 🔒

- 🔒 **Q1.1 What is v1's job?** What must the first Solaris do end-to-end?
  - ◦ (a) ★ Arrange audio + MIDI on tracks, per-track effect rack + one synth instrument, basic
    mixer, offline render to WAV (master + stems), full Nebula project/VCS/merge/embed.
  - ◦ (b) Smaller: MIDI-only (synth tracks) + render, no audio-sample clips yet.
  - ◦ (c) Larger: also audio recording / real-time playback.
  → Answer: 

- 🔒 **Q1.2 Real-time playback in v1, or offline-render only?** (Real-time needs an audio device
  backend + a real-time-safe engine; offline is deterministic and CI-friendly.)
  - ◦ (a) ★ Offline render only for v1 (write WAV; "preview" = render a range). Real-time later.
  - ◦ (b) Real-time playback from the start.
  → Answer: 

- **Q1.3 Audio *recording* (capture input) in scope?**  ◦ (a) ★ No (v1) ◦ (b) Yes
  → Answer: 

- **Q1.4 Primary user & use?** (Shapes defaults — e.g. scoring for the MV workflow vs general music
  production.)  ◦ (a) ★ Scoring/beds for the Interstellar MV workflow ◦ (b) General song production
  ◦ (c) Both
  → Answer: 

---

## 2. Time & tempo model 🔒

- 🔒 **Q2.1 Authoritative time unit?**
  - ◦ (a) ★ Beats (bpm + PPQ ticks); seconds derived — tempo-relative, quantize/MIDI-friendly, and
    stable under tempo edits.
  - ◦ (b) Seconds (absolute); beats derived.
  - ◦ (c) Store both explicitly.
  → Answer: 

- **Q2.2 PPQ resolution** (ticks per quarter note), if beats.  ◦ (a) ★ 960 ◦ (b) 480 ◦ (c) other
  → Answer: 

- **Q2.3 Tempo changes within a song?**  ◦ (a) ★ Single global tempo in v1 ◦ (b) Tempo map (changes
  over time)
  → Answer: 

- **Q2.4 Time signature / meter?**  ◦ (a) ★ Single global (e.g. 4/4) ◦ (b) Meter map
  → Answer: 

- **Q2.5 Scheduling accuracy?**  ◦ (a) ★ Sample-accurate note/automation events ◦ (b) Block-quantized
  (cheaper, coarser)
  → Answer: 

---

## 3. Tracks 🔒

- 🔒 **Q3.1 Track kinds for v1?**  ◦ (a) ★ `audio`, `instrument`, `bus` ◦ (b) also `folder`/group
  tracks ◦ (c) also MIDI-only (no instrument) tracks
  → Answer: 

- **Q3.2 One instrument per instrument-track, or a full chain?**  ◦ (a) ★ One synth at the head of
  the track's rack ◦ (b) Multiple instruments layered per track
  → Answer: 

- **Q3.3 Per-track controls in v1?** (gain, pan, mute, solo, phase, input gain…)  ◦ (a) ★ gain, pan,
  mute, solo ◦ (b) + more (list)
  → Answer: 

- **Q3.4 Max tracks (soft limit / target)?**  ◦ (a) ★ no hard cap; target ~64 ◦ (b) other
  → Answer: 

---

## 4. Clips 🔒

- 🔒 **Q4.1 Clip types?**  ◦ (a) ★ audio clips (sample refs) + note/MIDI clips ◦ (b) note-only v1
  → Answer: 

- **Q4.2 Audio clip edits in v1?** (trim in/out, start, gain, fade in/out, loop, time-stretch,
  pitch-shift)  ◦ (a) ★ trim, start, gain, fades, loop ◦ (b) + time-stretch/pitch (needs DSP work)
  → Answer: 

- **Q4.3 Do clips overlap on a track, or are tracks monophonic-in-time?**  ◦ (a) ★ audio: no overlap
  (one clip at a time); note clips: may overlap ◦ (b) free overlap with a mix policy
  → Answer: 

- **Q4.4 Clip-local vs track-level effects?**  ◦ (a) ★ effects live on the track rack (clips carry
  only gain/fade) ◦ (b) per-clip effect racks too
  → Answer: 

---

## 5. Notes / MIDI 🔒

- 🔒 **Q5.1 Note attributes stored?**  ◦ (a) ★ pitch (0–127), start, length, velocity ◦ (b) +
  per-note pitch-bend/expression/microtuning
  → Answer: 

- **Q5.2 Continuous controllers / pitch-bend / mod-wheel in v1?**  ◦ (a) ★ No (use automation
  instead) ◦ (b) Yes, as MIDI CC lanes
  → Answer: 

- **Q5.3 Standard MIDI File import/export (.mid)?**  ◦ (a) ★ Not v1 (notes authored in `.slp`/CLI)
  ◦ (b) Import ◦ (c) Import + export
  → Answer: 

- **Q5.4 Note merge policy** (two branches editing the same note clip)?  ◦ (a) ★ union by
  (start,pitch); identical note = no conflict; same note edited differently = field conflict
  ◦ (b) other
  → Answer: 

---

## 6. Instruments & the processor rack 🔒

- 🔒 **Q6.1 v1 instrument(s)?**  ◦ (a) ★ the existing DSP `SynthEngine` (Pulsar's synth) as the one
  instrument ◦ (b) + a sampler (play audio samples chromatically — needs new DSP)
  → Answer: 

- 🔒 **Q6.2 v1 effect set** (from DSP: reverb, EQ, envelope, chorus/overdrive/compressor per
  `ParamId`, spatial…)?  ◦ (a) ★ expose whatever the promoted DSP registry already has (reverb, EQ,
  the ESP32 synth FX chain) ◦ (b) a curated subset (list) ◦ (c) + new effects (list)
  → Answer: 

- **Q6.3 Rack ordering/reorder + bypass per node?**  ◦ (a) ★ ordered chain, add/remove/reorder,
  per-node bypass ◦ (b) fixed order
  → Answer: 

- **Q6.4 Third-party plugins (VST/AU/LV2) ever?**  ◦ (a) ★ Out of scope (Arstro-DSP only) ◦ (b) Later
  ◦ (c) v1
  → Answer: 

---

## 7. Parameters & param paths (DSP seam) 🔒

- 🔒 **Q7.1 Param-path string form?** (Referenced by the text project + automation.)
  - ◦ (a) ★ `<nodeId>/<paramName>` (e.g. `fx_syn/cutoff`), names from the DSP registry descriptors.
  - ◦ (b) numeric (`<nodeId>/<paramIdHex>`) — compact but opaque.
  → Answer: 

- 🔒 **Q7.2 Param value domain in the file?**  ◦ (a) ★ normalized 0..1 + the registry maps to real
  units ◦ (b) real units (Hz/dB/ms) stored directly ◦ (c) both (store real, normalize on apply)
  → Answer: 

- **Q7.3 Param versioning** (registry evolves)?  ◦ (a) ★ a `paramVersion` in the file + a migration
  hook ◦ (b) none (breaking)
  → Answer: 

---

## 8. Automation 🔒

- 🔒 **Q8.1 Automation in v1?**  ◦ (a) ★ Yes — param curves over time (breakpoints + easing) ◦ (b)
  defer (static params only in v1)
  → Answer: 

- **Q8.2 Curve shape between breakpoints?**  ◦ (a) ★ per-segment easing (linear + ease in/out)
  ◦ (b) linear only ◦ (c) bezier handles (like Cosmo's mixer curve)
  → Answer: 

- **Q8.3 What's automatable?**  ◦ (a) ★ any registry param + track gain/pan ◦ (b) a fixed subset
  → Answer: 

---

## 9. Mixer / routing 🔒

- 🔒 **Q9.1 Routing graph for v1?**  ◦ (a) ★ track → (optional bus) → master; sends to buses
  ◦ (b) track → master only (no buses yet) ◦ (c) arbitrary graph
  → Answer: 

- **Q9.2 Channel format?**  ◦ (a) ★ stereo master (mono/stereo tracks summed to stereo) ◦ (b) mono
  ◦ (c) surround/multichannel
  → Answer: 

- **Q9.3 Sends pre/post fader, master FX chain?**  ◦ (a) ★ post-fader sends + a master rack ◦ (b)
  simpler (no sends v1)
  → Answer: 

---

## 10. Rendering & export 🔒

- 🔒 **Q10.1 Render outputs?**  ◦ (a) ★ master mixdown + selectable stems (per track/bus) ◦ (b)
  master only
  → Answer: 

- **Q10.2 Export formats/bit depth?**  ◦ (a) ★ WAV, 24-bit + 32-bit float, project sample rate
  ◦ (b) + FLAC ◦ (c) other
  → Answer: 

- **Q10.3 Render range?**  ◦ (a) ★ whole song or an arbitrary [start,end] range ◦ (b) whole only
  → Answer: 

- **Q10.4 Tail handling** (reverb/delay ring-out past the last event)?  ◦ (a) ★ render until silence
  (threshold) or a max tail ◦ (b) hard cut at range end
  → Answer: 

---

## 11. Audio file I/O & sample rate 🔒

- 🔒 **Q11.1 WAV I/O dependency?**  ◦ (a) ★ vendored `dr_wav` (no system dep — matches the vendored
  LibRaw precedent) ◦ (b) libsndfile (system dep; already used by `apps/wav_demo`)
  → Answer: 

- **Q11.2 Input formats to decode (source clips)?**  ◦ (a) ★ WAV (v1) ◦ (b) + FLAC ◦ (c) + MP3/AAC
  (needs a codec dep)
  → Answer: 

- 🔒 **Q11.3 Project sample rate + mismatched sources?**  ◦ (a) ★ one project rate (e.g. 48 kHz);
  resample source clips whose rate differs (needs a resampler) ◦ (b) require all sources at the
  project rate (no resampler) v1
  → Answer: 

---

## 12. Project, versioning & history (Nebula) 🔒

- 🔒 **Q12.1 Branch + auto-rebase in v1?**  ◦ (a) ★ Yes — living branches per the suite vision (this
  is a headline feature) ◦ (b) defer VCS; just load/save a `.slp` in v1
  → Answer: 

- 🔒 **Q12.2 Nebula backing store?**  ◦ (a) ★ self-contained text store (own commit/branch objects)
  ◦ (b) real git repo + custom semantic merge drivers
  → Answer: 

- **Q12.3 History granularity — what is a "commit"?**  ◦ (a) ★ explicit user commits (+ coalesced
  auto-snapshots like Cosmo's history) ◦ (b) every edit is a commit ◦ (c) manual only
  → Answer: 

- **Q12.4 Undo model** — same tree as Cosmo (branching per-project history)?  ◦ (a) ★ Yes, project-
  level branching history (reuse the Nebula/Cosmo model) ◦ (b) linear undo
  → Answer: 

---

## 13. Embedding & propagation 🔒

- 🔒 **Q13.1 What Solaris exposes to Interstellar** when embedded `as=audio`?  ◦ (a) ★ full mixdown
  **and** named stems, following a branch ◦ (b) mixdown only
  → Answer: 

- **Q13.2 Solaris embedding *other* projects?**  ◦ (a) ★ Solaris→Solaris (reuse a section/stem)
  ◦ (b) none in v1 ◦ (c) also embed non-audio (unlikely)
  → Answer: 

- **Q13.3 Propagation trigger** — how does the embedded render refresh?  ◦ (a) ★ on the source
  branch advancing (auto-rebase), re-render the artifact lazily/cached ◦ (b) manual "update embed"
  → Answer: 

---

## 14. Merging two projects 🔒

- 🔒 **Q14.1 "Merge two songs into one" default policy?**  ◦ (a) ★ choose per-merge:
  **concatenate** (B after A) or **overlay** (B's tracks stacked at t=0) ◦ (b) concatenate only
  ◦ (c) overlay only
  → Answer: 

- **Q14.2 Tempo/meter conflict on merge** (A=120, B=140)?  ◦ (a) ★ keep A's; time-convert B's
  beats ◦ (b) keep both via a tempo map ◦ (c) flag as conflict
  → Answer: 

- **Q14.3 Track-name/id collisions on merge?**  ◦ (a) ★ id-namespace both (no collision); dedupe
  identical source assets by hash ◦ (b) other
  → Answer: 

---

## 15. Determinism & identity 🔒

- 🔒 **Q15.1 Reproducibility requirement?**  ◦ (a) ★ per-machine reproducible (same machine → byte-
  identical render; enables golden tests + content-hash commits) ◦ (b) cross-machine byte-identical
  (stricter: constrains SIMD/denormal/float) ◦ (c) not required
  → Answer: 

- 🔒 **Q15.2 `AudioConfig` (currently a global singleton) — keep or make per-render?**  ◦ (a) ★
  make render config passable (a render is a pure function of project+config) ◦ (b) keep singleton
  (one global rate per process; simplest)
  → Answer: 

- **Q15.3 Fixed block size for offline render?**  ◦ (a) ★ yes, a defined block size (e.g. 512)
  ◦ (b) whole-song single pass
  → Answer: 

---

## 16. CLI surface & UX 🔒

- 🔒 **Q16.1 CLI shape?**  ◦ (a) ★ subcommands over a project file (`solaris <verb> <proj> …`), as
  sketched in the README ◦ (b) an interactive REPL/shell ◦ (c) a script/DSL file the CLI executes
  → Answer: 

- **Q16.2 Output contract for scripting?**  ◦ (a) ★ human text + a `--json` mode; non-zero exit on
  error; conflicts machine-readable ◦ (b) human only
  → Answer: 

- **Q16.3 Editing granularity via CLI** — is the CLI a full editor or a batch/scripting tool?
  ◦ (a) ★ full enough to build a song headlessly (add/edit/remove every node) ◦ (b) mostly render/
  merge/branch; authoring mainly by editing the `.slp` text
  → Answer: 

---

## 17. File format specifics 🔒

- 🔒 **Q17.1 Extension + `app=` tag?**  ◦ (a) ★ `.slp`, `app=solaris`, shares the Nebula grammar with
  `.cmp`/`.isp` ◦ (b) other
  → Answer: 

- **Q17.2 Embed the tempo map/markers/arrangement in the same file?**  ◦ (a) ★ yes, one text file
  ◦ (b) split
  → Answer: 

- **Q17.3 Human-editable by hand expected?**  ◦ (a) ★ yes — keep it legible/commentable (it's a
  design goal) ◦ (b) tool-only
  → Answer: 

---

## 18. Performance & limits (targets, not hard reqs)

- **Q18.1 Offline render speed target?**  ◦ (a) ★ faster-than-real-time on a laptop for a ~64-track
  song ◦ (b) other
  → Answer: 

- **Q18.2 Multicore render?**  ◦ (a) ★ nice-to-have (parallelize independent tracks) ◦ (b) v1
  requirement ◦ (c) single-threaded v1
  → Answer: 

- **Q18.3 Max song length / polyphony targets?**  → Answer: 

---

## 19. Non-goals (say what v1 will NOT do)

- **Q19.1 Confirm out-of-scope for v1** (tick to confirm out): ◦ real-time playback ◦ recording
  ◦ VST/AU/LV2 ◦ time-stretch/pitch ◦ video ◦ notation/score view ◦ surround ◦ MP3/AAC ◦ MIDI
  hardware I/O
  → Answer (list anything that should instead be IN): 

---

## 20. Platform & build

- **Q20.1 v1 platforms?**  ◦ (a) ★ Linux CLI (matches the repo) ◦ (b) + macOS/Windows CLI
  → Answer: 

- **Q20.2 `solaris_core` UI-free like `cosmo_core`?**  ◦ (a) ★ yes — a UI-free core the CLI (and
  later an Artboard UI) both drive ◦ (b) other
  → Answer: 

- **Q20.3 Build integration?**  ◦ (a) ★ a `solaris` project in `build.sh` + top-level CMake, tests
  in `ctest` (like cosmo) ◦ (b) other
  → Answer: 

---

## What happens next

Answer the 🔒 questions (the ★ defaults are a coherent, shippable set — "all ★ except: …" is a
valid answer). From your answers I will write:
1. `solaris/docs/requirements.md` — the numbered functional + non-functional requirements.
2. Any needed updates to [prerequisites.md](prerequisites.md) (its Part A/E decisions get resolved)
   and the [Nebula contracts](../../docs/shared-core.md) they touch.

See also: [../README.md](../README.md) (design brief) · [prerequisites.md](prerequisites.md)
(what to define/implement) · [../../docs/shared-core.md](../../docs/shared-core.md) (Nebula).
