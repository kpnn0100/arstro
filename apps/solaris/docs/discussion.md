# Solaris — design discussion (pre-implementation)

> **Status: CLOSED 2026-10-08 — the user said "implement".** Every agreed point is now an R- line in
> [`../REQUIREMENTS.md`](../REQUIREMENTS.md); the open questions were closed with the recommended
> defaults (recorded in [`PROGRESS.md`](PROGRESS.md)'s decisions log). This file is the record of
> why, not a specification.
>
> *(Original status: DISCUSSING — nothing here is implemented, and nothing here is a requirement yet.)*
> When the user says "implement", this file is the input: each agreed point becomes an `R-`/`DR-`
> entry (two tiers, per `arstro.rule` §2), the conflicts in §8 are amended in place, and a
> `arstro.solaris.implement` skill is written modelled on `arstro.interstellar.implement`.
>
> Started 2026-10-08. Newest round at the bottom of §9.

---

## 1. The brief, as the user gave it (2026-10-08)

> Cosmo and Interstellar is working now, I need you to make solaris with following brief.
> Don't implement yet, I want to discuss a little bit; you have to save the discussion and when I
> say implement it, make the skill and make the whole app refer the design of cosmo and interstellar.
>
> - Homepage to create project like Interstellar/Cosmo
> - Setting have place to config input output audio device, config sample rate, add folder for
>   quick access to sample
> - should have branching too for multi version of a song
> - In project view, left is sample/preset panel, user can drag and drop it into project
> - I need to discuss a little bit about timeline + mixer mechanism, right now i see 2 popular type:
>   + 1 is in cubase, each track is come into 1 line of mixer, this is good for handling
>     record/wave but not good for making music that loop a lot like edm
>   + 2 is like in FL studio, track mean nothing, only for organize, the actual mixer line is set
>     per sample and instruments
>   personally I like the 2 better, but it make it really hard to check what is out of mixer at the
>   end of the project, can you suggest me the better way to design the timeline and mixer

---

## 2. Timeline + mixer — the proposal: "lanes hold time, channels hold sound"

### 2.1 What is actually wrong with each model

- **Cubase** ties one timeline row to one mixer strip. Clear, but a loop-heavy song wants the same
  sound in many places and many sounds in one place, and the row ↔ strip lock fights both.
- **FL Studio** frees the rows, but pays with **two hidden indirections**: a playlist clip points
  at a Channel Rack channel, and that channel points at a mixer insert *by number*, shown in a tiny
  box on the channel. Looking at insert 7 tells you nothing about what feeds it. That — not the
  free rows — is why "what comes out of the mixer at the end" is hard.

So keep FL's free rows, remove the hidden number, and make the routing **visible from both ends**.

### 2.2 Three objects

| object | is | holds |
|---|---|---|
| **Channel** | one mixer strip — *is* the strip, no insert number in between | a source (instrument, or audio) + its effect rack + gain/pan/mute/solo + its output |
| **Lane** | a timeline row — organisation only (FL-style) | any clips, from any channels |
| **Clip** | when + what | sounds through **exactly one channel**; note clip → instrument channel, audio clip → audio channel |

Channel kinds: `instrument` · `audio` · `bus` · `master`.

### 2.3 Two defaults that give you both workflows in one model

> **Superseded in round 3:** every sample file gets its own strip, grouping is done by routing
> into a later mixer, and the "lane default channel" below is retired. Kept for the record.

- **A lane may have a default channel.** A sample dropped on the "Hats" lane plays through the
  "Hats" channel — ten hat loops, one strip. A lane whose clips all use its default channel *is* a
  Cubase track; a lane with mixed clips is an FL playlist row. Same model, different defaults.
- **Dropping a preset or sample into empty space creates a lane + a channel, linked.** You start
  Cubase-simple and break out to FL-free when you want to, not the other way round.
- **Duplicated clips are linked by default** (edit one loop, every copy changes); "Make unique"
  breaks the link. Loops are the EDM case the Cubase model is worst at.

### 2.4 Making "what goes out of the mixer" readable — derived, never stored

1. **"Fed by" on every strip.** The strip shows how many sources feed it and from where
   (`6 clips · lanes Drums, Fills · 1 send from Vox`). Click → the timeline lights those clips and
   dims the rest.
2. **Arrange by: Lane | Channel.** One toggle regroups the same clips as one row per channel — a
   Cubase view of an FL project, computed on the fly. This is the end-of-project check: each row
   is exactly what one strip plays. (Open: does a vertical drag in Channel view re-route the clip?)
3. **Selection is linked both ways.** Select a clip → its strip lights in the mixer and its path to
   master/outputs is drawn. Select a strip → its clips light on the timeline.
4. **Mix audit** — a command (`mix audit`) and a panel listing: channels with no clips, clips on
   muted channels, channels that reach no output, buses with a single input, peaks over 0 dBFS,
   identical instrument patches duplicated. Assertable from the CLI like Interstellar's `lint`.
5. **Signal-flow map** — an auto-laid-out read-only graph: channels → buses → master → outputs.
   Optional; may fold into the routing matrix (SR-ROUTE).

### 2.5 Format impact — additive, Interstellar's files keep their meaning

`#atrack` already carries gain/pan/mute/solo/out — it *is* a strip. So:

- `#atrack` **becomes the channel** (no rename; meaning unchanged for existing files).
- **New `#alane`** node: `id`, `name`, `order`, optional `chan=` (the default channel), colour.
- `#aclip` gains optional **`lane=`**. A clip with no `lane=` draws on its channel's own row — which
  is exactly what every Interstellar `.isp` means today. Non-breaking.
- Linked clips: a shared content node (`#apattern` or similar) that several `#aclip`s reference.

This amends `docs/audio-format.md` (suite level) — Interstellar must keep parsing, round-tripping
and preserving the new nodes (its R-AUD-2 rule already says it must).

### 2.6 Questions still open on this topic

- **Q-MIX-1** One clip → one channel (recommended: keeps "fed by" exact and merges simple), or allow
  FL multi-channel patterns? The drum-pattern convenience can come from a drum/sampler instrument
  with pads instead.
- **Q-MIX-2** Vertical drag in "Arrange by Channel" view: re-route the clip, or time-only?
- **Q-MIX-3** Mixer placement: a dock under the timeline (recommended — linked selection needs both
  visible), or a separate tab like Interstellar's Grade/Cut/Deliver?

---

## 3. Versions ("branching for multi version of a song") — proposal

Follow **Interstellar R-VER** exactly, so the suite has one versioning model:

- A project holds many **arrangements**; one is `main`; each other declares a **base**.
- **The sound is shared, like Interstellar's rack:** channels, instruments, racks, mixer.
- **Inherit live, override to diverge, pin/freeze to stop, rebase to reconcile.**
  - *Instrumental* = base + an override muting the Vox channel.
  - *Extended mix* = base + an arrangement override (more bars in the intro).
  - *Radio edit, delivered* = frozen, so later work on `main` cannot change it.
- Any instrument/effect parameter can be overridden per version, so a sound-design experiment is a
  version too.

**Conflicts** with SR-VCS-1..3 (project-level Nebula branching with its own commit store) — the
same model Interstellar R-VER-6 already replaced, for the same reason. Proposed: amend SR-VCS;
keep SR-MERGE-2 (merge two songs) as a separate import operation.

---

## 4. Home — proposal

Cosmo's `HomeScreen` rhythm, as Interstellar R-UI-1: recent projects as cards, newest first —
name, `bpm · key · length`, version count, a mini waveform of the last bounce. New (name, bpm,
time signature, sample rate defaulting from Settings), Open, Settings.

## 5. Settings — proposal

Cosmo's Engine Settings dialog reused (as Interstellar R-SET-1), plus Solaris rows:

- **Audio:** backend (PipeWire/Pulse/ALSA/JACK), output device, input device, sample rate,
  **buffer size with the latency shown in ms**, a test tone.
- **Sample folders:** add / remove / reorder — they become the browser's quick-access list.

Two decisions this forces:

- **Device rate vs project rate.** The project owns its sample rate (SR-IO-3). The device opens at
  the project rate when it can; otherwise the driver resamples and a badge says so. The Settings
  rate is the default for new projects and the preferred device rate.
- **The project never stores device names** (a project moves between machines). It stores
  **logical ports** ("Out 1–2", "Out 3–4", "In 1"); Settings maps them to physical device channels.
  **Amends SR-OUT-3**, which persists hardware output routing in the project.

## 6. Project view — left panel (samples / presets) — proposal

- **Sections:** Folders (from Settings) · Presets (instrument + effect, factory and user) ·
  Project (sounds already used — the resource pool).
- Click = audition. Waveform thumbnail per sample.
- **Drop targets:** a lane (clip through the lane's default channel) · empty space (new lane +
  channel) · a mixer strip (load into its instrument/sampler) · a rack slot (insert effect).
- Every drop is **one command** in the grammar — a drag is a GUI gesture over `add-clip` /
  `add-channel` / `rack add`, never a path the CLI cannot take (`arstro.rule` §1, §5).

## 7. Look — proposal

- Cosmo's tokens aliased, **one forked token: the accent**, by Interstellar's R-UI-2 recipe (rotate
  cosmo's hue, keep S/L weight). Cosmo blue, Interstellar purple-pink, Solaris = ?
  - **Q-LOOK-1** Accent: a warm "solar" orange-gold is on-name but sits next to record-red and
    solo-amber; a teal (~175°) is clear of every audio semantic. Undecided.
- Layout family: Cosmo `MenuStrip` + wordmark + version chip, left browser, centre timeline, mixer
  dock, transport — desktop window, like Cosmo/Interstellar.
- **`docs/ui-brief.md` (touch-first tablet, blue accent, left rail, bottom sheets) is superseded**
  by "refer the design of cosmo and interstellar". Proposed: retire it with a note, keep for
  reference.

---

## 8. Conflicts with the existing Solaris spec (to amend on "implement")

| existing | conflict | proposed resolution |
|---|---|---|
| SR-VCS-1..3 | project-level branching + own commit store | Interstellar R-VER model, per arrangement (§3) |
| SR-OUT-3 | hardware routing persisted in project | logical ports in project, device map in Settings (§5) |
| SR-TRACK-1, SR-CLIP-5, audio-format §2.1 | track = lane = strip | channel / lane / clip split, additive (§2.5) |
| `ui-brief.md` | touch-first, blue, rail + sheets | Cosmo/Interstellar desktop family (§7) |
| `SR-` single tier, phase tags | `arstro.rule` §2 wants two tiers | `REQUIREMENTS.md` (R-) + `docs/requirements.md` (DR-), as Interstellar |

---

## 9. Rounds

### Round 1 — 2026-10-08

- User gave the brief (§1). Claude proposed §2–§7 and listed the conflicts (§8).
- **Agreed:** nothing yet.
- **Waiting on the user:** Q-MIX-1, Q-MIX-2, Q-MIX-3, versions model (§3), Q-LOOK-1, retiring
  `ui-brief.md`.

### Round 2 — 2026-10-08

- User: *"so the better way is for each instrument and sample to have its own line in mixer, if we
  want to merge into 1 we have to do it manually right?"*
- Claude clarified the rule (proposal, not yet agreed):
  - **An instrument always has its own strip.** The instrument lives in the strip, so two
    instruments never share one; they can only meet in a bus.
  - **A sample has its own strip only if it is dropped into empty space.** Dropped on a lane that
    has a default channel, it joins that channel with no extra step. An audio channel is a strip
    that many sample clips play through, not "one sample".
  - **Joining things later is manual, and each is one action:**
    1. **Group to bus** — select strips → one action makes a bus and routes them into it. Each
       keeps its own fader and FX; the bus adds shared FX (drum-bus compression). The usual way.
    2. **Move clip to channel** — the clip's channel chip, or drag in "Arrange by Channel" view.
    3. **Merge channels** — audio channels only: B's clips move into A and B is deleted (B's FX
       rack is dropped, so it asks). Rare.
  - **Clutter control:** strips routed into a bus fold under it in the mixer (expand to see them),
    so 30 one-shots do not mean 30 visible strips.
- New open question **Q-MIX-4**: a sample dropped into empty space — new channel each time
  (proposed), or one shared "Samples" channel by default?

### Round 3 — 2026-10-08

User's direction (verbatim, condensed):

> I think it still be better to have sample got its own line by default, we can separate the mixer.
> We can add multiple mixers as the user wants their workspace organised. The 1st mixer will be for
> storing the samples of the project; the 2nd mixer is for receiving sends from the 1st mixer.
> Default 2nd mixer will be line to master. There should be a mixer matrix view to see the whole
> send picture. We can send to multiple output devices. I want solaris to handle multiple audio
> devices so the user can take advantage of their devices.

**Agreed by the user:**

- **Q-MIX-4 answered: every sample gets its own strip by default.**
- **Several mixers, user-added, for organisation.** Mixer 1 holds the sources (samples,
  instruments); Mixer 2 receives from Mixer 1; Mixer 2 feeds master by default.
- **A matrix view** shows every route and send at once.
- **Multiple audio devices at once**, any strip able to reach any device's outputs.

**Claude's refinements (proposed, not yet agreed):**

- **One strip per sample *file*, not per clip.** The same file placed 20 times = one strip. A
  recording lands in the armed strip (5 takes ≠ 5 strips). A clip can still be moved to another
  strip.
- **This retires §2.3's "lane default channel"** — no longer needed: grouping now happens by
  routing into Mixer 2, not by sharing a strip. Lanes are pure organisation, exactly FL.
- **Mixers are an ordered list, and routing only goes forward** (a strip feeds a strip in a later
  mixer, master, or a hardware port). A feedback loop then cannot be built at all — no cycle check
  to fail — and the flow always reads left to right. Two source mixers ("Drums", "Synths") before a
  "Buses" mixer work naturally.
- **Each strip has one main output + any number of sends** (own level, pre/post). Output = where
  the fader goes; sends = extra copies (reverb return, headphone cue).
- **Defaults of a new project:** Mixer 1 "Sources" (empty) → Mixer 2 "Buses" with one bus "Main"
  (the default destination of every new strip) → Master → the clock device's Out 1–2.
- **One matrix, replacing the old brief's three:** rows = every strip (grouped by mixer) and the
  hardware inputs; columns = every later strip, master, and hardware outputs grouped by device.
  Cell = `●` main output, a dB number for a send, `pre`/`post` marker. Filter by mixer; in the CLI
  `matrix print [--json]`.
- **Multiple devices — the two real problems and the design for them:**
  1. **Clock drift.** Each device has its own crystal (48 000 vs 48 003 Hz drifts into a click
     within minutes). One device is the **clock**; every other device is a **follower** with an
     adaptive resampler. On Linux, PipeWire already does this for every stream in its graph —
     recommended backend; ALSA-direct/JACK/WASAPI would need Solaris's own drift resampler.
  2. **Latency mismatch.** Devices have different output latencies; Solaris delays the faster ones
     so every output is heard at the same moment, and aligns recordings per input device.
  - Settings: enable N devices, mark one as clock, see per-device latency / drift ppm / xruns.
  - The project stores **logical ports** ("Main 1–2", "Phones", "Click"); Settings maps each to a
    device's channels. A port whose device is missing on this machine is shown **offline**, not
    fatal.
  - Offline render needs no device: **one WAV per logical output port**.
  - Use cases: Master → speakers on device A; a headphone cue mix (sends) → device B; the click →
    device B only.
- **Open:**
  - **Q-MIX-5** Forward-only routing (proposed), or free routing with a cycle check (allows bus →
    bus inside the same mixer)?
  - **Q-MIX-6** New strip's default destination: the "Main" bus in Mixer 2 (proposed), or straight
    to master until the user routes it?
  - **Q-DEV-1** PipeWire as the multi-device backend on Linux (proposed)? What about Windows
    (Interstellar already builds on MinGW64)?

### Round 4 — 2026-10-08 — "implement it"

- User: *"Now implement it. Make me some basic instruments: Drum Machine, Basic Synth with 2 OSC; and
  some basic filters: Compressor, EQ, Reverb, …; make sure the skill puts all the core of those
  instruments and basic filters in dsp."*
- Became: R-DSP (the sound is the DSP library's), R-INST-1/2, R-FX-1..5 in
  [`../REQUIREMENTS.md`](../REQUIREMENTS.md); law 1 of the `arstro.solaris.implement` skill.
- Open questions closed with the recommended defaults — listed in [`PROGRESS.md`](PROGRESS.md)'s
  decisions log, each one line to change.

### Round 5 — 2026-10-09 — the third brief

- User: *"1. make the Basic Synth a vst3 app and make the UI follow Arstro theme like Cosmo/Interstellar
  and Solaris, drum machine also. 2. Remove piano roll user can create midi track and put in an
  instrument track, so they can loop that midi in the track or move to another, there should be a midi
  list of project to drag and drop into project. 3. time bar interact with drag and drop too, not just
  click."*
- Asked, because each changes the app's shape, and answered (each the recommended reading):
  - **"Remove piano roll"** → *keep it as the editor*: MIDI clips become things made, looped and moved on
    the lanes and listed in the browser; the piano roll stays, as the editor a MIDI clip opens.
  - **A MIDI clip dragged to another track** → *a track owns its instrument*: it plays through that
    track's instrument. This amends R-LANE-1 (lanes were organisation only); a lane naming no
    instrument keeps the old meaning, and R-LANE-2's channel view stays reserved and compatible.
  - **"A vst3 app"** → *plugins with an Arstro editor*, not standalone apps.
- Decided without asking (one line each to change): the editor uses Solaris's teal (they are Solaris's
  instruments); the editor lives in the umbrella, because it needs Artboard and cosmo, while the DSP
  repo keeps the processor and controller and builds them alone with the host's generic view; the UI
  says "MIDI" for the grammar's note clips and patterns; dragging on the ruler seeks at every grid line
  (as a fader sends each step), and the loop brace is moved and resized by dragging; a song saved
  before tracks existed has its one-instrument lanes made tracks on opening.
- Became: R-TIME-6, R-LANE-3 (R-LANE-1 amended), R-CLIP-6…8, R-BROWSE-4, R-VST-7/8 in
  [`../REQUIREMENTS.md`](../REQUIREMENTS.md); ledger tasks E1–E6 in [`PROGRESS.md`](PROGRESS.md).

### Round 6 — 2026-10-15 — the fourth brief: a song made in the window

- User: *"fix following — when open a midi roll, user can press play directly in that — when create a note,
  play sound. Here come a hard part: you have to make a song yourself, try find a song that you understand
  deeply, that you can replicate that song 99%. During making that song, if the instrument is not enough,
  make that instrument; during making that song, analyze the UX, if any point that don't make sense to user
  fix it. The goal is a production ready solaris app."*
- Decided without asking (each one line to change):
  - **The song is J. S. Bach's Prelude in C major, BWV 846** (The Well-Tempered Clavier I, 1722). Public
    domain, as R-SVC-9 requires, so the result can be shared — a copyrighted song is not an option at any
    percentage. Chosen because it can honestly be replicated to the note: every bar is one five-note chord
    played in one fixed figure, so the score is a table of 35 chords and the replica can be diffed against
    it note by note — "99 %" becomes a number, not a feeling.
  - **It needs a piano, which Solaris does not have** — the DSP library has a physical piano voice
    (`src/physical`, REQ-piano-*) but only its demo app plays it. The piano becomes a library instrument
    with a registry entry (R-INST-5), so Solaris, the plugins and the CLI all get it with no Solaris code.
  - **It is made through the window, not the CLI** (the canon was the CLI's proof, R-SVC-9): every note
    entered by the roll's gestures, driven headlessly through the app's own gesture path (the test rig)
    and checked live in the real window. Every place a user would stumble is written down as it happens
    (`docs/ux-audit.md`), fixed in its own task, and the song is finished with the fixes.
  - **The roll's ▶ loops the roll's clip, with the song sounding around it** — what you hear while editing a
    pattern is that pattern in place, again and again; the song's own loop comes back when it stops. Space
    with a roll in front is the roll's ▶; elsewhere it stays the song's play/stop.
  - **A new note sounds through its track** — the instrument and the track's rack at the track's fader, as it
    will sound in the song — on its own stream, so it is heard whether or not the song plays; a click on the
    roll's keys and a note dragged to a new pitch sound too, as in every piano roll.
- Became: R-ROLL-6/7, R-INST-5, R-SVC-10, R-UX in [`../REQUIREMENTS.md`](../REQUIREMENTS.md); ledger tasks
  H1… in [`PROGRESS.md`](PROGRESS.md).

### Round 7 — 2026-10-15 — the fifth brief: an EDM track, as a test

- User: *"please make an edm song, edm require more advance sound, so it can be a test for you."*
- Decided without asking (each one line to change):
  - **Original, not a cover.** The progression, melody and arrangement are written for this track; nothing is
    taken from anyone's song. Made with `solaris-cc` alone, as the canon was — the window's test was the
    Prelude's; this one is the SOUND's.
  - **"More advanced sound" is measured, not claimed.** What a mastering engineer checks: integrated and
    short-term loudness, true peak, section contrast, low end mono and highs wide, the sidechain's pump depth,
    the grid, the key — numbers in `measure.py`, as the canon's and the Prelude's are.
  - **The gaps it shows are filled in the DSP library first** (the user's rule from round 6, "if the instrument
    is not enough, make that instrument"): the Basic Synth's unison was mono — every supersaw centred, its
    voices starting in phase — and the kit had no cymbal for a drop. Both are amendments of R-INST-1/2.
- Became: R-INST-1/2 amended, R-SVC-12 in [`../REQUIREMENTS.md`](../REQUIREMENTS.md); ledger tasks I1… in
  [`PROGRESS.md`](PROGRESS.md).

### Round 8 — 2026-10-16 — "implement all": a professional DAW

- User: *"implement all"* — every feature of [`pro-daw-analysis.md`](pro-daw-analysis.md) (P0, P1, P2).
- Decided without asking (each one line to change), recorded so nobody re-decides them:
  - **The analysis's phase order** (§4) is the build order; each phase leaves the app usable end to end.
  - **PipeWire is the low-latency driver.** JACK is not installed on the machine; PipeWire serves JACK clients
    anyway. PulseAudio stays the fallback (R-DEV-8 amended).
  - **MIDI input is ALSA's sequencer**, in the host; tests drive a fake MIDI source through the same seam.
  - **Encoders are FFmpeg's** (flac, libmp3lame, libvorbis, libopus are present), in the host — the core
    still carries no codec (law 12).
  - **Other makers' plugins run out of process** (`solaris-plughost`, one per plugin): shared-memory audio a
    block at a time, a crash or an overrun takes the plugin offline, never the song.
  - **No server and no AI model.** Collaboration is a shared (or Git) folder with a node-by-node merge;
    the assistive tools are signal processing (harmonic / percussive separation, monophonic pitch tracking,
    chroma chords) and say so.
  - **What cannot be tried on this machine is said, not claimed**: a real MIDI keyboard, a control surface, a
    second audio device — each is built against a fake through the host seam, tested there, and its live check
    written down as owed.
- Became: R-PRO (R-SAFE, R-MIDI-1…3, R-METER, R-EDM-4a, R-EXPORT, R-MIX-18…20, R-DEV-9, R-ROLL-11, R-INST-6…11,
  R-ARR, R-REC-1/2, R-EDIT, R-WARP, R-TEMPO, R-PRESET, R-VST-6, R-CTRL, R-PERF, R-FX-6…17, R-COLLECT, R-LAUNCH,
  R-VIDEO, R-COLLAB, R-ACCESS, R-ASSIST) in [`../REQUIREMENTS.md`](../REQUIREMENTS.md); ledger tasks N1… in
  [`PROGRESS.md`](PROGRESS.md).

### Round 9 — 2026-10-17 — other makers' VST3 plugins (R-VST-6, N25/N26): the design, before code

Round 8 fixed the shape: one `solaris-plughost` process per plugin, audio through shared memory a block at a time, a
plugin's crash or overrun never takes the song. The rest, decided here so the code has one answer (each a line to
change):

- **Where it lives.** Everything that loads a module, starts a process or maps memory is the HOST's (law 12):
  `apps/solaris/host/` gets the client side, `apps/solaris/plughost/` the helper executable (built only when the VST3 SDK
  is found, as the DSP library's plugins are). The core sees a seam beside the audio ones — `IPluginHost` (scan, load)
  and `IPluginInstance` (process a block, its parameters, latency, state, offline + why) — and wraps an instance as a
  library `Device`, so the engine plays it as any device (latency compensated, R-MIX-17).
- **Scanning.** `plugins scan` looks in `SOLARIS_VST3_PATH` (colon-separated) or else `~/.vst3`, `/usr/lib/vst3`,
  `/usr/local/lib/vst3`; each bundle is read by `solaris-plughost --scan <bundle>` in its own process, so a module that
  crashes while being read loses only itself (said). The list (id = the class's UID, name, vendor, instrument or
  effect, the bundle) is the machine's, kept in its data folder like the library; `plugins list` prints it; the model
  publishes it. Tests point the path at this build's own plugins (Basic Synth, Drum Machine) — never the user's.
- **A plugin in a song** is a device of type `vst3` naming its class and bundle; its parameters are the plugin's (ids,
  titles, units, steps, read from it), set and automated as numbers 0 … 1 by the parameter's id or its title made an
  address; the song keeps its state as the plugin gives it (base64). A missing bundle makes the device offline, never
  the song unopenable (R-DEV-3's rule for ports, here for plugins).
- **The pipe.** Control is lines over a socket pair (the service grammar's habit); audio is a shared-memory block per
  instance — inputs, events and parameter changes in, outputs back — handed over with an eventfd each way. The audio
  thread waits for the answer at most the block's own duration (the BUDGET); later is an overrun. Renders run their own
  instance (a second process), waiting as long as it takes, so a render stays pure (R-RENDER-1) and never disturbs
  playback.
- **Failure.** The helper dying (EOF / exit) or an overrun takes the instance OFFLINE: an effect passes its input dry,
  an instrument is silent, the model says which and why; `device reload <dv>` starts it again. A crash is tested by
  killing the helper — the real event, not a simulation.
- **Its editor** (N26) is the helper's: it opens the plugin's own view in its own X11 window (the SDK's editorhost
  pattern), placed and named by the window; Solaris's generated panel stays the always-available view.

### Round 10 — 2026-10-17 — control surfaces (R-CTRL-2, N28): the design, before code

A surface is a second MIDI device that the song TALKS BACK to: its faders are motorised, its buttons lit, its strip
of text names the channels. So, decided here (each a line to change):

- **Two protocols, one shape.** Mackie Control (the de-facto standard — most surfaces, and every "MCU mode") and HUI
  (Pro Tools' older one) are two CODECS of one surface model: eight channel strips (a fader, a pot, mute, solo, arm,
  select, a touch sense), a master fader, the transport keys, bank / channel shift; and back, a fader's position, the
  buttons' lights, the pots' rings, the text. A codec is pure — bytes in, events out; state in, bytes out — so both
  are tested as tables with no device at all. Mackie: a fader is a 14-bit pitch bend on its channel (the master's on
  channel 9), a pot a relative CC 16–23, a button a note (on 127 = pressed); back the same, plus the scribble strip
  as a SysEx. HUI: a fader is a 14-bit pair of CCs, a button a zone/port pair of CCs, a pot a relative CC 64–71; it
  wants a ping answered.
- **Its own port, in and out.** `settings set surface=mcu|hui|none` and `surfacePort=<port>` — the machine's, as the
  keyboard's inputs are. The surface's input port is NEVER the keyboard's (a fader's pitch bend is no bend wheel):
  the keyboard listens to every port but that one. MIDI OUTPUT is a new seam beside the input (`IMidiOut`, the host's
  ALSA sequencer).
- **What it moves** — always through the grammar, as a drag would: a fader → `set <ch>.gain` by the fader law (the
  one copy, FaderLaw.h), the master → `project.masterGain`, a pot → the strip's pan in steps, mute / solo → `set
  <ch>.mute|solo`, arm → `strip arm`, the transport keys → `transport …`, bank / channel → which eight strips it shows
  (the session's, in processing order). A fader being touched is not moved back by the song (the hand outranks the
  motor); the rest follows the song, whatever changed it.
- **What it shows** — sent only when it changed (a diff against what was sent): each fader where its strip's gain
  is, each light as its strip is, the strip's name on the text; an empty slot past the last strip down and dark.
- **Not yet:** a surface's display of values, its jog wheel, its function keys, more than one surface.


### Round 11 — 2026-10-17 — several audio devices (R-DEV-1/2/4/5, N32): the design, before code

Today a song plays on ONE device, the clock (`settings.output`); a port the machine's map puts on another device is
skipped. R-DEV asked for every device at once, the others held to the clock. Decided here (each a line to change):

- **Which devices.** The clock is `settings.output`, as now. A FOLLOWER is every other device that an output port of
  the playing song is mapped to (`port.<name> = <device>:<channel>`) — mapping a port to a device is what "enables"
  it (R-DEV-1); there is no second list to keep in step. Each follower gets its own stream (the clock's driver) and
  its own thread. One that cannot open is silent and said, once; the song plays on (R-DEV-3: never fatal).
- **Feeding one.** The audio thread renders as now and writes each follower's ports, at their channels, into that
  follower's ring — single producer, single consumer, made before play, no lock, no allocation. A ring that is full
  (its device stalled) drops the block and counts it.
- **Holding it to the clock — the DSP library's** (R-DSP-1). Two pieces, both pure and steppable, so minutes of two
  crystals running apart are tested in milliseconds with no device: an `AdaptiveResampler` (a windowed-sinc
  interpolator whose ratio may change every block without a click: 64 taps, a Kaiser window, a table of phases
  linearly interpolated; its delay a constant half-length) and a `FillController` (a PI loop on the ring's fill,
  smoothed, steering the ratio so the fill holds its target; its integral IS the drift, published in ppm; clamped at
  ±2000 ppm — far past any crystal). The follower's thread pulls through them a device block at a time.
- **Heard at the same moment** (R-DEV-4). Each path's latency is its device's, plus — for a follower — its ring's
  target and the resampler's half-length. The fill target is the follower's delay control: a follower faster than the
  clock path gets a larger target; a clock path faster than a follower is delayed by a delay line on the clock's
  channels (set at play; the heard position counts it). Aligned at play from what each device then reports; a target
  moved later travels through the loop, a few ppm at a time (never a jump).
- **An input on another device** — the same loop the other way: the input's ring (the host's) read through a
  resampler that holds what it has buffered at a target (a new `available()` on the input seam); a recording keeps
  its per-device alignment (its own latency, already).
- **Seen** (R-DEV-5): `transport.devices[]` while playing — each device's id, role (clock | follower | input),
  latency, drift in ppm, xruns, and why it is silent if it is. Settings › Audio shows them, and which ports are where.
- **Tests.** The DSP pieces by simulation: 48 000 against 48 003 Hz (and ±1000 ppm) for simulated minutes — the
  fill held within a few frames, the drift read to under 1 ppm, never an underrun; a sine through a changing ratio
  with no click (its distortion and noise measured). The service with fake devices paced in real time at different
  rates: a follower's port gets its sound, continuous; an impulse on a clock port and one on a follower port arrive
  at the same moment within a millisecond; the state published.
- **Not yet:** a device's own sample rate unlike the song's (the clock opens at the song's, R-DEV-7; a follower at
  another rate would be the same resampler at a far ratio — later); a follower re-aligned when its latency drifts
  after play; Windows.


### Round 12 — 2026-10-17 — a device that measures (R-FX-17, the tuner): the design, before code

Every device so far makes or changes sound; the tuner makes a NUMBER ("a meter, not a sound"). Nothing carries a
device's number up today — the meters are the strips' and the master's, measured by the Player. Decided here (each a
line to change):

- **A reading is the DSP library's, described by the registry** (law 2, as parameters are). A `DeviceType` gains
  `readings`: name, label, unit — outputs, never set. `Device::reading(i)` is its value now. The device writes it on
  the audio thread into atomics of its own, so any thread may read it without a lock or a torn value. The API
  document lists each type's readings beside its parameters.
- **The tuner's pitch: YIN** (de Cheveigné & Kawahara, 2002) — the difference function over a window, normalised by
  its running mean, the first dip under a threshold, refined by a parabola: a fundamental, not its loudest harmonic.
  A 2048-sample window every 512 (≈ 47 Hz … 2 kHz at 48 kHz). Its readings: `frequency` (Hz; 0 = no pitch), `note`
  (the nearest, a MIDI number), `cents` (−50 … +50 from it), `level` (dBFS). A `reference` parameter: A4 at 415 …
  466 Hz, 440 by default. The audio passes through untouched (a meter).
- **Up to the model with the audio thread's rules kept.** After each block the engine copies every reading of every
  device that has any into a fixed array of atomics the Player owns — sized at build, in compile order; no lock, no
  allocation. The service knows, from the compile, which slot is which device; `refreshModel` fills
  `devices[].readings` (name, unit, value) while playing. Stopped, there is nothing to read: none are published.
- **Read by a script:** `get <dv>.<reading>` answers it (0 when stopped); `set` on one is refused, naming it a
  reading. No event — a tuner's number changes forty times a second; a script reads it when it wants it.
- **Seen:** the device window shows a type's readings. The tuner's: the note's name large, and the cents as a needle
  across −50 … +50, easing to each new reading (nothing snaps — a needle that jumped forty times a second would
  flicker); the frequency small beneath. With no pitch, the note is a dash and the needle rests at the centre, faded.
- **Parts** (a commit each): (1) the DSP library — the tuner and the readings (`DeviceType::readings`,
  `Device::reading`); Solaris has the device, its readings not yet carried up. (2) The engine, the Player, the
  service: the slots, `devices[].readings`, `get`, the API. (3) The window: the readings drawn, the tuner's needle,
  shots.
- **Tests.** The DSP by sines and a sawtooth: to ±1 cent from 55 Hz to 1760 Hz; a sawtooth at its fundamental (no
  octave error); silence and noise: no pitch. L2: a tuner on a synth playing A4 reads 440 Hz, note 69, 0 cents while
  playing — through `get` and the model; nothing stopped; a `set` refused. UI: the needle caught mid-tween; shots at
  both sizes.
- **Not yet:** readings in the mixer dock or on a control surface; a tuner that hears more than one note at once.


### Round 13 — 2026-10-17 — group / VCA faders (R-MIX-19, N35): the design, before code

R-MIX-19 says one line: a VCA moves the gain of the strips assigned to it, adding its dB. A VCA carries no sound — it
is a hand on many faders at once. Decided here (each a line to change):

- **A VCA is a strip of its own kind** (`kind=vca`, R-MIX-1's "a channel is a strip" kept): a name, a colour, a
  fader, a mute and a solo — and NO audio: no clips, no rack, no output, no sends, nothing routes to it. Everything a
  strip already has — its place in the dock, its gain's address `ch_7.gain`, renaming, colour, order — it has with no
  new code; what a strip has that a VCA must not, is refused naming the reason (`device add`, `clip add --strip`,
  `route`, `send add` to or from it).
- **Membership is the member's:** `set <ch>.vca=<vca strip>` (or `none`) — a strip follows at most one VCA, and a VCA
  follows none (no nesting, for now). Any audio, instrument or bus strip may follow one. In the `.slp`: `vca=ch_7` on
  the member's `#astrip` line, a VCA its own `#astrip … kind=vca` line.
- **What it does — at the compile, so the engine stays as it is:** a member plays at its own gain PLUS the VCA's
  (dB added, so linear multiplied); a VCA muted mutes its members; a VCA soloed solos them (each as if soloed —
  `silentStrips` decides, ONCE, as now). A member whose gain is a formula or an automation gets the VCA's dB as an
  offset after its formula (a `Bind`'s offset, applied after its clamp). The member's own fader shows its OWN gain;
  the VCA's shows the VCA's.
- **Live:** the VCA's fader moved while playing moves every member's gain through the existing live messages (each
  member's own + the VCA's), never an engine swap; a member bound to a formula is the exception — its offset lives in
  the engine's bind, so the engine is swapped (as a mute in a song with keys is).
- **Seen:** `strips[].vca` (the VCA it follows, "" none) and, on a VCA, `strips[].members` (computed by the service,
  so a second front end need not); `strips[].vcaGain` on a member (the dB its VCA adds). The dock: a VCA strip shows
  its fader, M and S, a "VCA" tag and its members' names — no meter (it carries no sound), no rack, no routing; a
  member shows a small "VCA <name> +3.0" under its fader, easing as the VCA moves. The strip menu: "VCA ▸" — None and
  each VCA (one line, `set <ch>.vca=…`). "+ Line" offers "VCA".
- **Parts** (a commit each): (1) the model, the compile and the grammar — kinds, membership, refusals, the `.slp`, the
  sound (rendered: a member at its own plus the VCA's dB, mute and solo through it, a bound member offset); (2) live
  and the model's fields and the API; (3) the dock — the VCA's strip, a member's tag, the menu, shots.
- **Not yet:** a VCA following a VCA; a VCA's own gain automated or bound to a formula (refused, said); a VCA's pan.
