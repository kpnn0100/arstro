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


### Round 14 — 2026-10-17 — per-strip tools (R-MIX-20, N36): the design, before code

R-MIX-20 names four: a phase invert, a width, a mono check on the master, and solo-safe on buses. Decided (each a line
to change):

- **Phase and width are a strip's own** (`set <ch>.phase=on|off`, `set <ch>.width=0 … 200` %), stored on its
  `#atrack` line when not their defaults (`phase=invert`, `width=150`). The engine applies them to the strip's own
  sound after its rack and BEFORE its pre-fader sends and its fader — an insert at the head of the strip's own
  controls: width by mid and side (`s × width`, 0 mono, 100 as it is, 200 the side doubled), then the polarity
  (both channels × −1). At their defaults the samples pass untouched (no arithmetic at all). A VCA has neither.
- **Live** (part 2): a width dragged while playing reaches the engine as a message, ramped across the block so a drag
  never steps; a phase toggled likewise. No rebuild.
- **The mono check is the listener's, not the song's:** `transport mono on|off` folds what the speakers get to mono
  (L = R = (L + R)/2, every output's pairs) while it is on — the Player's, after the engine; a render or an export
  never hears it, a song does not keep it. `transport.mono` says it. (The master's own width stays the master
  limiter's or an imager's business.)
- **Solo-safe:** `set <bus>.soloSafe=on|off` (`soloSafe=true` on its line) — a bus a solo never silences (a reverb
  return kept while a vocal is soloed, even when the vocal does not feed it); refused on a line that is not a bus
  (R-MIX-20: "on buses"). Decided in `silentStrips`, once, with the rest; its mute still mutes it.
- **Seen** (part 3): a strip's menu — Invert Phase (a check), Width ▸ (0 · 50 · 100 · 150 · 200 %), and on a bus
  Solo-safe (a check); its card says Ø when inverted and its width when not 100 % (each faded in); a solo-safe bus's S
  shows a ring; the master card a "Mono" toggle, lit while on.
- **Parts** (a commit each): (1) the model, the compile, the engine and the grammar — phase, width, solo-safe — and
  the mono check; (2) live (no rebuild) and the model's fields and the API; (3) the dock.
- **Not yet:** a width automated or bound to a formula (refused, said); polarity per channel.

### Round 15 — 2026-10-17 — versions of a song (R-VER-1…4, N37): the design, before code

R-VER asks for Interstellar's model applied to a song: a version is *its base, resolved live, with its deltas on top*;
inherit live, override to diverge, freeze to stop, rebase to reconcile; every version renders by name. Decided (each a
line to change):

- **What a version can change: any field of any node, and which nodes there are.** The song's nodes are the `.slp`'s
  own — the header (as the node `project`: bpm, masterGain, …), every `#a…` line with an `id`, a rack (`#arack
  track=<ch>`, the node `rack:<ch>`) and a formula (`#abind`, the node `bind:<address>`). A version's deltas, nested in
  its node at the end of the file:

  ```
  #aversion id=vr_1 name=Instrumental
    #vset node=ch_4 mute=true                 ; a field overridden — the rest of ch_4 still inherits
  #aversion id=vr_2 name=Extended base=vr_1
    #vset node=ac_1 at=16.0
    #vunset node=ch_2 phase                   ; a field the base has, back to its default here
    #vdrop node=ac_7                          ; the base's node is not in this version
    #vbody node=pt_1                          ; a node's id-less lines — a pattern's notes — are ONE field
      #note pitch=36 at=0.0 length=0.25 vel=110
    #vorder node=rack:ch_2 children=dv_1,dv_9 ; which devices, in what order
    #aclip id=ac_12 track=ch_2 lane=ln_2 pattern=pt_1 at=0.0 length=8.0   ; a node only this version has
  ```

  No `base` = main (the song itself, the root; `main` is not a node and cannot be renamed, frozen or deleted). A
  version's base is main or an EARLIER version, so no chain can loop (the forward-only rule again). Interstellar's
  versions nest nothing — its deltas are flat lines carrying `timeline=` — because only a timeline's clips vary there; a
  Solaris version spans every kind of node, so its deltas live INSIDE its node, where an older build keeps them verbatim
  as an unknown node and opens the song as main, losing nothing. Same verbs, same meanings; the spelling differs for that
  reason (law 16).
- **Edits are recorded, never copied — for every command at once.** While a version is open the song on screen IS its
  resolution; a command edits it as it edits any song, and when the command lands its deltas are worked out again:
  everything the version now differs from its base's resolution in, field by field (`#vset`/`#vunset`), node by node
  (`#vdrop`, a node of its own), a pattern's notes or an automation's points as one (`#vbody`), a rack's order
  (`#vorder`). So no command needs version code, a field edited back to the base's value inherits again, and editing an
  inherited clip stores ONE `#vset`, not a copy (a copy-on-edit mutant must fail). Ids are never reused across the
  document: a node a version adds can never collide with one the base adds later.
- **Inherit live.** A base edit reaches every version that does not override that field, the moment it lands. A delta
  whose node the base no longer has is **dangling**: ignored, counted (`versions[].dangling`), never silently deleted —
  `version rebase` names and prunes them. A version the base has broken (a clip of its own on a strip the base deleted)
  is reported with the reason (`versions[].problem`) and cannot be opened or rendered until fixed (Not yet: a repair).
- **Freeze to stop — one lever.** `version freeze <vr>`: the version keeps a snapshot of its resolution (in its node,
  `#vsnapshot`, the whole song) and stops inheriting; it is read-only — an edit while it is open is refused, naming it.
  `version thaw` drops the snapshot: its deltas are live again and the base's work since arrives. `version rebase` on a
  frozen version advances its snapshot to the base's present state with its deltas on top. Interstellar has two levers
  (a pinned colour, a frozen cut) because its colour lives in another file, the rack; a Solaris song is one document, so
  one lever stops it all (Not yet: freezing the sound alone).
- **The grammar, Interstellar's verbs:** `version new <name> [--base <vr>] [--open]` (the UI's "New version…" is ONE
  line), `version list` (the tree), `version open <vr|main>`, `version diff <vr>`, `version freeze|thaw <vr>`,
  `version rebase <vr> [--dry-run]`, `version delete <vr>` (refused while another is based on it; deleting the open one
  opens its base), `revert <address|node>` (drops the open version's override there: it inherits again; refused on main
  or where there is none), `set <vr>.name="…"`, and `render --version <vr|name>` (every version renders by name; a
  plain render is the open one). A version is named by id or name.
- **What open is.** `version open` is a step of the history (undo goes back to the version you came from) but does not
  mark the song unsaved: the file does not say which version is open — a song always opens on main.
- **Seen** (part 3): the song bar's version chip beside the song's name — "main", or the version's name, with ▾; its
  menu lists the versions as a tree (the open one ticked; frozen ones with a snowflake, broken ones dimmed with why),
  then New Version…, Freeze / Thaw, Rebase, Rename…, Delete. Opening a version changes the song on screen through the
  same eased values as any edit — clips that differ slide or fade, a muted strip's fill fades.
- **Parts** (a commit each): (1) the model — the format, resolution, the deltas worked out from an edit, freeze, rebase,
  dangling, broken, ids never reused; (2) the service — the grammar, the edit hook, a frozen version refusing edits,
  render by name, history, the model's fields, the API; (3) the song bar.
- **Not yet:** a version's own tempo map or markers edited separately from the song's (they are nodes like any other,
  so they version already — the "not yet" is a UI for comparing two versions); per-note inheritance inside one pattern;
  merging two versions; a repair for a broken version.

### Round 16 — 2026-10-17 — collect and save; relink (R-COLLECT-1, N38): the design, before code

R-COLLECT-1: `project collect` copies every file the song uses into its folder and rewrites the paths; a relink window
for missing files (`clip relink <ac> <file>`, or a folder searched). Decided (each a line to change):

- **What a song uses** — every audio clip's file and its takes', a device's sample (a sampler's sound, a convolution's
  response), an instrument's definition (an `.sfz`) AND every sound it names (beside it, as the compile reads them), a
  frozen strip's render — in main and in EVERY version (their own nodes, their overrides, their snapshots): a version
  you will render later must find its files too. `project files [--missing]` lists them, one line each: the path as the
  song names it, whether it is in the song's folder, outside it, or missing, and what uses it (ids; a version's
  prefixed by its id). Read-only.
- **Collect** — `project collect [--dry-run] [--save]`: every file outside the song's folder copied into
  `<folder>/Samples/` — an `.sfz` with its sounds into `Samples/<its name>/`, their layout kept (a sound named outside
  its `.sfz`'s folder is not collected, said) — never over a different file (a name taken gets " 2", " 3"…; the same
  bytes already there are reused); then every reference to it rewritten, in main and every version, as ONE edit (one
  undo — the copies stay; they are only files). Files already in the folder (Recordings, Frozen, Samples, anything
  under it) are left; a missing file is named and stays missing (relink it first). `--save` saves after — the UI's
  "Collect and Save" is ONE line. `--dry-run` says what it would copy and changes nothing. The service copies the
  files itself, as it writes the song.
- **Relink** — `project relink <file> --to <new>`: every reference to that file, in main and every version, pointed at
  the new one, ONE edit; `project relink --folder <dir> [--dry-run]`: every MISSING file looked for by its name under
  `<dir>` (searched down; the first by path order, several matches said) and relinked the same way, ONE edit;
  `clip relink <ac> <file> [--take <n>]`: one clip's file (or one take's) — an edit of the open song, so on a version
  it is that version's change. A device's sample already has its line (`set <dv>.sample=<file>`, R-EDM-8).
- **Missing, published** — `missing[]` in the model: each file the song names that cannot be read — the path as
  named, and what uses it — so a front end draws the relink window from the model, and an agent sees it in `state`.
- **Seen** (part 2): File › Collect and Save (ONE `project collect --save`; what it did said in a toast); File ›
  Relink… — a window listing each missing file (its name, the folder it was in, what uses it), each row's Locate… (the
  host's file picker, then ONE `project relink <file> --to <picked>`), and Search a Folder… (the host's folder picker,
  then ONE `project relink --folder <dir>`); a found row fades out, the rest slide up. A song opened with files missing
  says so in a toast that names File › Relink….
- **Parts** (a commit each): (1) the service — files listed, collect, the relinks, `missing[]`, the API; (2) the File
  menu, the Relink window, the toast on open, shots.
- **Not yet:** collecting a plugin's bundle (not media); finding a missing file by its content when its name changed.

### Round 17 — 2026-10-17 — a clip launcher (R-LAUNCH-1, N39): the design, before code

R-LAUNCH-1: a session grid (scenes × tracks) whose slots start in time with the song, beside the arrangement; launched
clips recorded into the arrangement. Decided (each a line to change):

- **The grid is the song's: scenes × strips.** A scene is a row (`#ascene id=sc_1 name=Verse order=0`); a slot is a
  clip that waits in a scene for a strip (`#aslot id=sl_1 scene=sc_1 strip=ch_2 pattern=pt_1 length=4.0`, or an audio
  slot with `src=` and `in`/`out` as a clip has, and `name`/`gain`). A slot is its own node, not a clip off the
  timeline, so nothing that reads the arrangement has to learn to skip it; a pattern slot shares its pattern as a clip
  does (R-CLIP-3). A slot of an instrument strip plays a pattern, of an audio strip a file — refused otherwise, as a
  clip is. Columns are the strips that can hold one (instrument and audio), in processing order. Versions and collect
  see slots as any node (`src=` included).
- **Launching is live, as the keyboard is** (R-MIDI-2's precedent): heard while playing, never in a render, until it is
  recorded. A strip follows the arrangement until a slot on it is launched; from then it is the session's — playing
  that slot, looped, or silent once stopped — until `launch back` returns it to the arrangement. Every launch, stop and
  return lands on the next quantum of the song's time — `project.launchQuantize` = `bar` (the default), `beat`, or
  `none` — computed by the service ahead of what the engine has rendered, never in the past.
- **The grammar:** `scene add [<name>] [--at <i>]`, `scene delete <sc>` (its slots with it), `scene move <sc> --to
  <i>`, `set <sc>.name=`; `slot add <sc> <strip> (--pattern <pt> | --clip <ac> | --src <file>) [--length <beats>]`
  (`--clip`: a clip's pattern or file and its length), `slot delete <sl>`, `set <sl>.name|length|gain=`; `launch <sl>`,
  `launch <sc>` (its slots; a strip with no slot in it keeps what it plays), `launch stop <strip>|all`, `launch back
  [<strip>]`. The model: `scenes[]`, `slots[]`, and `transport.launched[]` — per strip in the session: the slot, queued
  or playing or stopping, the beat it starts, how far through its loop it is.
- **Heard from the same graph** (R-PLAY-1): the compile puts every slot of a strip in the graph (its notes relative to
  its start, or its region); a launch, a stop and a return are live messages to the Player with the sample they land
  on — no engine rebuilt. At a launch the strip's notes sounding end there and the slot's start on the sample; a slot
  keeps its own phase, so a song loop jumping back does not restart it.
- **Recorded into the arrangement:** while `transport record` runs, what each strip played from the session lands, on
  stop, as clips on its lane — a slot's span as a clip of its pattern (or file), looping — and the strip's own clips
  under that span give way (one inside goes; one crossing an edge is cut there), so the song plays back what was heard.
  A note clip may now start into its pattern (`offset=` beats, as an audio clip's `in`), so a clip cut at its start
  still plays its second half. ONE undo, "Record".
- **Seen** (part 4): View › Session — the grid in the timeline's place, cross-faded: a column per strip (its colour and
  name), a row per scene (its name and ▶ at the end), a slot's name in its cell; ▶ on a slot launches it, ■ under a
  column stops it, "Back to Arrangement" returns them all; a queued slot pulses until it starts; a playing one fills its
  cell with its loop's progress. Every one a command line.
- **Parts** (a commit each): (1) the model — scenes, slots, the grammar, `launchQuantize`, the fields; (2) launching,
  heard — the graph's slots, the messages, quantized, `transport.launched[]`; (3) recording launches, `offset=`; (4) the
  session view.
- **Not yet:** follow actions; launching from a MIDI controller (the map grammar will reach it: `launch` is a line);
  recording INTO a slot.

### Round 18 — 2026-10-17 — a video lane (R-VIDEO-1, N40): the design, before code

R-VIDEO-1: a video file shown in a window, synced to the transport (decoded by the host's FFmpeg), with an offset; a
render can carry it into the export. Decided (each a line to change):

- **One picture a song** (`#avideo id=vd_1 src="picture.mp4" offset=4.0`): `offset` is seconds on the song's clock (the
  tempo map's) at which the picture's first frame shows; negative, the picture began before the song. A second video is
  refused (scoring to one picture; Not yet: several). It is a node like any: a version may move it, collect gathers its
  file (`src=`, R-COLLECT-1).
- **The grammar:** `video set <file> [--offset <s>]` — adds it or puts another in its place; the host probes it (its
  length, size, frame rate) and a file that is no video is refused; `set vd_1.offset=<s>`; `video remove`.
- **The model says where the picture is** (law 13 — a front end never re-derives the tempo map): `video` — its file,
  offset, length, size, frame rate, whether it can be read, and `time`: the picture's seconds at the heard position (−1
  before it starts or after it ends), moving while playing like the playhead, held at the time bar when stopped.
- **The host decodes** (R-SVC-4 — the core carries no codec): a probe (`probeVideo`), a frame at a time scaled to fit a
  box (`videoFrame`, RGBA), and the export's mux (`muxVideo`). FFmpeg, as the audio's.
- **Into the export:** `render --out <f.mp4|.mov|.mkv> --video` — the picture's own stream copied, not re-encoded,
  shifted by the offset into the render's range; the mix beside it (AAC in MP4/MOV, FLAC in MKV). Refused without a
  video, and for an audio-only extension. The L3 check: a clip FFmpeg makes, carried through `solaris-cc`, read back by
  `ffprobe` — both streams, the picture's length and start where the offset put them.
- **Seen** (part 2): View › Show Video — a window (a device window's kind) with the frame at `video.time`, letterboxed,
  "before the picture" / "after the picture" said outside its span; the lanes gain a video row above the first lane: the
  picture's span with a thumbnail every so often (decoded lazily by the host, kept), its name; dragged sideways it sets
  the offset (ONE line on release, the pointer's while held). The Export sheet's "With the video" (shown when the song
  has one) — MP4 or MKV then.
- **Parts** (a commit each): (1) the service and the host — the node, the grammar, the probe, `video` in the model, the
  frame decoder, `render --video` and its mux, L2 with a fake host and the L3 FFmpeg round trip; (2) the window, the
  lane, the export sheet's toggle, shots.
- **Not yet:** several videos; the picture's own sound as a clip; timecode (SMPTE) display.

### Round 19 — 2026-10-17 — sharing a song through a folder (R-COLLAB-1, N41): the design, before code

R-COLLAB-1: no server — a song saved into a shared folder (a synced drive, or a Git checkout); when the file changes
under an open song, its edits and the other's are merged node by node (by id), a conflict on one node asked about.
Decided (each a line to change):

- **The base is what this window last read or wrote.** The service keeps the song's text as it was last read from the
  file or written to it — the merge's common ancestor. The file's own save becomes atomic (written beside, renamed
  over), so a collaborator's Solaris never reads half a song.
- **Noticed on the clock.** `pump` looks at the file (its size and time) at most once a second while a song is open;
  `project sync` looks now. A file that reads back as the base — this window's own save — is nothing; one that does not
  parse yet (a sync still copying it, Git's conflict markers) is waited for and, after a few tries, said, never merged.
- **Nothing of mine to keep: reload.** With no unsaved edits the song simply becomes the file's (the time bar, the
  view, the selection where they were); said once (`project.synced`, "reloaded").
- **Edits of mine: a three-way merge, node by node** (`mergeSongs(base, mine, theirs)` in the model, pure): the nodes
  are the versions' own (the header field by field, every node with an id, a rack, a binding; a node's id-less lines —
  a pattern's notes, a clip's takes — its body, ONE unit). Changed on one side only → that side; on both, the same →
  either; on both differently, or changed on one and removed on the other → a CONFLICT. The same id added on both sides
  with different nodes is not a conflict: mine is given the next free id and every reference to it among my nodes is
  rewritten (a `key=value` value, an address in a formula). Order follows theirs, my additions after their neighbours.
  The merged song lands as ONE edit (undo is mine again) and the base becomes theirs.
- **A conflict is asked, never guessed:** until answered, the node is mine; the model lists each — its id, what it is
  ("strip Bass", "pattern Lead"), how (both changed / I removed / they removed) and both sides' lines. `project sync
  --take theirs|mine <id>` answers one (`theirs` is an edit), `--take theirs|mine` all. A save with conflicts pending is
  refused (it would overwrite theirs unseen); a save when the file changed since the base merges first.
- **Versions** (`#aversion`) are nodes as any: two people's edits to one version are one node's — a conflict when both
  changed it.
- **Seen** (part 3): a toast when the song is reloaded or merged ("Merged the changes on disk — 2 to answer"); a
  Conflicts sheet (the Relink sheet's look): a row per conflict — what it is, mine and theirs side by side — Keep Mine /
  Take Theirs (ONE line each), and both for all; rows leaving as answered, eased.
- **Parts** (a commit each): (1) the merge in the model — the node table shared with the versions, `mergeSongs`, the
  id renumbering — with model tests; (2) the service — the base, the atomic save, watching, `project sync`, the model's
  `sync`, save's guard — L2 with two services on one file; (3) the window — the toast, the Conflicts sheet, shots.
- **Not yet:** live co-editing (two windows editing the same moment), comments on the timeline, merging inside a
  pattern's notes or a clip's takes (one node each), a server.

### Round 20 — 2026-10-17 — accessibility (R-ACCESS-1, N42): the design, before code

R-ACCESS-1: a UI scale setting (eased), a high-contrast theme, every control's name and value published for assistive
technology (AT-SPI through the host), all of it reachable from the keyboard. Decided (each a line to change):

- **The UI scale is cosmo's** (one suite, law 16 — cosmo R-SCALE): `settings set uiScale=<percent>`, snapped to
  cosmo's steps (75 · 90 · 100 · 125 · 150 · 175 · 200), the machine's, on Settings › Interface. The window is drawn
  at the EASED scale (260 ms, cosmo's `kScaleAnimMs`): every frame the logical size is the window's divided by the
  drawn scale and every pointer position is divided by it, so the whole shell zooms — never a cut, then a reflow.
- **High contrast is one eased amount on cosmo's tokens** (the accent's runtime slot, cosmo R-G-5, is the precedent):
  `settings set contrast=normal|high`; cosmo's palette gains `setContrast(0…1)`, which cosmo itself never calls, so its
  pixels do not move. Each neutral token blends from its value toward a high-contrast one — the background black, text
  white, muted text light grey, borders a solid 45 % white, the cards darker, the accent kept — by the amount the App
  eases over 260 ms. So a switch fades the whole UI; a token is still the only place a colour is named.
- **Every control published: the accessible list.** Each Solaris widget lists its controls (`A11yNode`: a stable
  key, a role — button, toggle, slider, combo, tab, menu item, list item, text — a name, a value, its bounds in the
  window, enabled, and what it does: activate; a value's step up and down). The App gathers them in reading order —
  the song bar, the browser, the lanes' headers, the dock, the open windows, a modal's controls ONLY while it is open
  — as `App::accessibles()`. Activating a node is the click it stands for (the same ONE command line); a value's step
  is its keyboard step (a fader ±0.5 dB, a pan ±5 %, a parameter by its registry step). Coverage first: Home, the song
  bar and its menus, the browser's tabs and rows, the dock's strips (name, fader, pan, mute, solo, arm, record), the
  modal sheets' controls, a device window's parameters; the lanes' clips and the piano roll's notes are "Not yet".
- **Reachable from the keyboard — the same list.** Tab / Shift+Tab move a focus through the focusable nodes in that
  order (when nothing else takes Tab); Enter or Space activates; the arrows step a value (Shift: a tenth); Escape lets
  go. The focus ring is drawn by the App over the node's bounds, travelling between controls (220 ms) and fading in and
  out — and the focused node's name is said in the model-free way a screen reader hears it (below).
- **AT-SPI through the host** (R-SVC-4 — the core and the App carry no OS API): the GTK host gives the window an ATK
  object whose children are the list's nodes — a role, a name, a value (AtkValue for a slider), extents (AtkComponent),
  actions (AtkAction: activate; increment / decrement) — re-read when the App says the list changed, and a focus event
  when the keyboard's focus moves. The bridge is plain ATK (no GTK), so a host test builds it from a list and reads it
  back through ATK's own calls with no display.
- **Parts** (a commit each): (1) the scale and the contrast — settings, Settings › Interface, the eased transform and
  token blend, shots; (2) the accessible list and the keyboard's focus — the widgets' nodes, Tab / Enter / arrows, the
  ring; (3) the ATK bridge in the host, its test; the live screen reader is owed (the user's desktop).
- **Not yet:** the lanes' clips and the piano roll's notes as nodes; a screen reader's own announcements of a value as
  it plays (meters); right-to-left.


### Round 21 — 2026-10-17 — localisation (R-ACCESS-2, N43): the design, before code

R-ACCESS-2: every UI string from a table; a language setting. Nothing in the suite translates yet (cosmo, Interstellar
and the rest draw English literals), so this round decides the shape for Solaris and keeps it small enough to lift into
the suite later. Decided (each a line to change):

- **What is translated: the window's own words — nothing an agent reads.** The App's labels, menus, sheets, toasts it
  composes, empty states, tooltips and the accessible names a screen reader speaks. NOT translated: the command
  grammar, the event lines, the model, the API document, `solaris-cc`'s output (an agent parses them — law 13 and
  R-API-1); the service's own words shown in the window (a refusal's text, a history entry's label, an Info line —
  they are the API's lines); the DSP registry's words (a parameter's label, its unit, its choices, a module's title —
  the names an address and the manuals use); and the user's data (songs, strips, lanes, markers, files). A command is
  never built from a translated label: a widget sends ids and values, and draws `tr(...)` of its English beside them.
- **The table: English is the key** (gettext's way, so the code reads as it does today and a missing translation falls
  back to the English): `tr("Settings")`; a context where one English word means two things, `trc("menu", "Open")`
  (the key `menu|Open`); a count, `trn(n, "{1} clip", "{1} clips")`; numbers and names into a sentence by position,
  `{1}` `{2}` (`fillIn(tr("{1} of {2}"), a, b)` — `fill` would meet `std::fill`), so a language may reorder them. One file per language,
  `lang/<code>.strings`, a line per string — `"Settings" = "Cài đặt";`, a plural's keys side by side and as many
  forms as the language's rule has — under a header (`@language vi`, `@name Tiếng Việt`, `@plural other`). Compiled
  INTO the binary (as the fonts are — the app's own words are not a thing that may differ between machines), parsed
  by a small library both the service and the window link (`solaris_strings`: no dependencies). A file that drops or
  adds a placeholder, repeats a key or has the wrong number of plural forms fails its test, not the window.
- **The languages: English, Vietnamese, and a pseudo-language.** English is the keys themselves. Vietnamese (`vi`)
  ships whole — Roboto and JetBrains Mono, embedded, already carry every Vietnamese letter (checked against the
  TTFs' character sets: none missing). `en-XA` (Android's pseudo-locale code) is generated, not written: every letter
  accented and the string lengthened by a third in brackets — `[Šéţţîñĝš ·······]` — so a string NOT from the table
  stands out and one that will not fit a longer language is seen in English's place.
- **The setting:** `settings set language=auto|en|vi|en-XA` (the machine's; default `auto` — the OS's language when
  Solaris has it, else English, as Reduced motion reads the OS's: the host tells the App, `setOsLanguage`); an unknown
  code refused naming the nearest. The model publishes `settings.language` and `settings.languages[]` (code and the
  language's own name), so any face offers the same list. Settings › Interface › Language: a combo of those names,
  ONE line.
- **Nothing snaps: a switch dissolves.** The whole window cross-fades from the old language to the new over 180 ms
  (cosmo's cross-fade): while it runs the App paints the tree in the old language, then the tree again in the new one
  inside a layer at the eased amount (Artboard's `pushLayer`) — the new language last, so every widget's hit-test
  rectangles are the new ones. Text that moves because its words grew is part of the dissolve.
- **Text fits** (design rule R5): a longer language is a width test. The pseudo-language's third more is drawn in the
  shots at both sizes; a label that does not fit ellipsizes against the room it has (the widgets' own text fitting).
- **Proved, not promised — "every UI string from a table":** a UI test paints each covered screen in `en-XA` through a
  render target that records every string drawn, and fails on any string with a letter that is neither bracketed (from
  the table) nor one the model published (the user's data, the registry's words, the service's lines) nor a number or
  unit. A source scan test lists every `tr` / `trc` / `trn` key in the window's code and fails when `vi.strings` lacks
  one or keeps one no code uses — the table cannot drift from the code, as the API document cannot.
- **Parts** (a commit each): (1) the table and the setting — `solaris_strings` (parse, look up, plural rules, the
  pseudo-language), `lang/vi.strings` embedded, `settings set language=`, the model's fields, L0 and L2 tests, the API;
  (2) the shell through the table — Home, the song bar and its menus, Settings, Export, Relink, Conflicts, the
  confirm dialogs, the toasts, the accessible names of all of them; Settings › Interface › Language; the dissolve; the
  recorded-strings test and the source scan for those files; shots in Vietnamese and `en-XA`; (3) the song view through
  the table — the browser, the lanes and the ruler, the dock, the windows (device, roll, automation, loudness, video,
  tuner, session), extended to the same tests and shots.
- **Not yet:** the registry's words and the service's lines translated; right-to-left languages; plural rules beyond
  English's one / other and a language with one form; the web face's own text; a translator's tooling beyond the scan's
  list of missing keys.

### Round 22 — 2026-10-17 — analysis tools (R-ASSIST-1, N44): the design, before code

R-ASSIST-1: harmonic / percussive separation of an audio clip into two clips; monophonic audio-to-MIDI (pitch
tracking); chord detection of a clip or the song (chroma), shown on the ruler — signal processing, no AI model.
Decided (each a line to change):

- **The analysis is the DSP library's** (R-DSP-1): three offline functions over a whole sound, as `detectOnsets` and
  `estimateTempo` already are — done once, off any audio thread, allocating, deterministic (no clock, no threads, no
  randomness: the same sound gives the same answer, R-RENDER-1). A new `src/analysis/` module composes what the library
  has — its FFT (REQ-fft-1), the tuner's YIN (REQ-tuner-1, its estimator lifted into a function both use), its onsets
  (REQ-onset-1) — and adds a short-time Fourier transform with an exact inverse (Hann windows overlapping by four: the
  sum of their squares is constant, so an untouched spectrum resynthesises the sound to rounding). Each gets a `## Math`,
  unit tests and a rendered integration test, `REQ-analysis-1…4`, its own commit and push; Solaris then bumps the pointer.
- **Separation: median filtering (Fitzgerald), soft masks.** The magnitude spectrogram filtered along time (17 frames) is
  what sustains; along frequency (17 bins) what is sharp in time. Wiener masks M_H = H²/(H² + P²), M_P = 1 − M_H, taken
  from the channels' sum so both channels split alike (the image kept), applied to each channel's spectrum and
  resynthesised. The masks sum to one, so the harmonic and the percussive sum back to the clip — a test measures it.
- **Audio to MIDI: monophonic, by YIN.** The mono sum framed (a 2048-sample window every 256 at 48 kHz, scaled with the
  rate); a frame voiced when YIN finds a period under its threshold and the frame is above −50 dBFS; its note the nearest
  semitone; a five-frame median over the notes; a note where one pitch lasts 60 ms or more, split where the library's
  onsets find a new attack on the same pitch; its velocity from its loudest frame (−60 … 0 dBFS → 1 … 127). Seconds out;
  the service puts them in beats through the song's tempo map at the clip's place. Not yet: bends, chords, polyphony.
- **Chords: chroma and templates, smoothed.** A 8192-point STFT of the mono sum; each bin from 110 Hz to 4 kHz adds its
  power to its pitch class (a triangle a semitone wide around the nearest note); the frames averaged over each beat of
  the song's tempo map (the service hands the beat times in); each beat scored against the 24 major and minor triads
  (cosine), "N" — no chord — when the best is under 0.6 or the beat is silent; a Viterbi pass with a stay bonus (a chord
  is likely to last) so a passing note does not flip it; equal beats merged into spans. The names as a musician writes
  them: C, Am, F#, Bbm.
- **The grammar — each ONE command, ONE edit (undo takes it back whole):**
  `clip separate <ac>` — the clip's sound (as it plays: its source span) split; two files written beside the song
  (`Separated/<name> harmonic.wav`, `… percussive.wav`, the host's writer, as a bounce's are); the clip replaced by two
  clips at its place, on two new lanes under its own, each with its own strip (R-MIX-2) named after it.
  `clip to-midi <ac> [--instrument <type>]` — its notes as a pattern ("<name> notes") on a new instrument track (a
  synth unless named) at the clip's place.
  `chords detect [<ac>]` — the song's chords from that clip, or from the whole song's mix (rendered as a render is):
  they replace the song's chords; `chords clear`. Saved in the song (`#achord at= length= name=` — Interstellar has no
  such node, so it is Solaris's alone), in the model (`chords[]`: id, at, length, name), an event each
  (`chords.detected n=`). **Amended in part 2:** the model's `chords[]` was already `--chord`'s vocabulary
  (R-ROLL-11) and a model field is never renamed — the song's chords are `songChords[]`.
- **The window:** a clip's menu gains "Separate Harmonic / Percussive", "To MIDI" and "Detect Chords"; the ruler's menu
  "Detect Chords" (the song) and "Clear Chords". The chords on the ruler: a band of their own under the markers', there
  only while the song has chords — the ruler grows into it and the lanes ease down (`Timeline::top()`, eased); each
  chord's name at its span, keyed by id, fading in and out, sliding when the map moves it; its accessible name its chord.
- **Long sounds:** the analysis runs inside its command, as a render, a freeze or a bounce does (a few seconds for a
  minute of sound); a progress bar and a cancel are Not yet.
- **Parts** (a commit each): (1) the DSP library's analysis module; (2) the service — the three commands, the chords in
  the song and the model, L2 tests over synthetic sound the fake host plays back (a sine with clicks, a melody, a chord
  progression); (3) the window — the menus, the ruler's chord band, shots.
- **Not yet:** polyphonic transcription, pitch bend in the notes, chord qualities beyond major and minor (sevenths,
  suspensions), key detection, more than two stems (drums, bass, vocals need a model — R-ASSIST-1 says no AI model).

### Round 23 — 2026-10-19 — automation clips (R-AUTO-14, O1–O2): the design, before code

The user, after M2 (a looping automation, `auto create --pump`, "Create Pump"): *"it is not create pump — it should be
create a clip for automation and user can copy just like a midi track"*. Asked four questions, the user chose:
**Create Pump removed**; automation clips **on lanes, like MIDI clips** (each automation its own track, the AUTOMATION
section gone); a copy **linked** like a MIDI clip's (edit one, all change; Make unique forks); the loop setting
**replaced by clips that repeat**, old songs converted. R-AUTO-12 and R-AUTO-13 are withdrawn; R-AUTO-14 replaces them.
Decided (each a line to change):

- **An automation is to its clips what a pattern is to its note clips** (R-CLIP-3, law 6). Its points are in its OWN
  beats, 0 … its `length`; a CLIP places it in the song (`#aclip auto=au_1 at= [length=]`); a clip longer than the
  automation repeats it, as a note clip repeats its pattern (R-CLIP-7); a copy (`clip duplicate`, Shift-drag, Ctrl+C /
  Ctrl+V — R-CLIP-9/10, unchanged) is a second clip of the same automation — linked. A clip's `length` 0 follows the
  automation's, as a note clip's follows its pattern's.
- **What a formula reads is still the automation** (`=au_1`, R-AUTO-1 unchanged). Its value at a beat: the clip of it
  playing there — its curve at `(beat − clip.at) mod length`; where two of its clips overlap the one that began later
  plays (the earlier is cut there); between its clips and after the last, the value the clip before ended on (FL Studio
  holds it); before its first clip — and with no clip at all — its REST value, `rest` (`#aauto rest=`). `auto create`
  sets `rest` to the property's own value, so a pump copied in from bar 9 leaves the property alone before it (the
  first clip's start would be the dip). Pure (R-RENDER-1): every value is a function of the beat, never of what played.
- **Make unique** (`clip unique <ac>` — the note clip's verb, R-CLIP-3) gives an automation clip its own curve: a new
  automation `of` the first (`#aauto id=au_2 of=au_1`, named "<name> 2", a copy of its points, length, rest, unit and
  range), the clip now plays it — and it still drives what the first drives: a formula reads the FIRST, whose value is
  whichever clip of the family plays (formulas naming a copy are refused, naming its first). A copy of a copy is a copy
  of the first (no chains). `auto delete` of the first takes its copies, clips and track with it.
- **An automation's track** is a lane (R-LANE-3's idea, its own key so an instrument's track keeps one meaning:
  `#alane auto=au_1`, showing the automation's name), made by
  `auto create` right under the property's strip's track when it has one, else last. Its clips go there by default; any
  plain lane may hold automation clips (lanes are organisation, law 5). Refused: an automation clip on an instrument's
  track or on another automation's track; an audio or note clip on an automation's track; `--strip` on an automation
  clip (it sounds through nothing).
- **The grammar:** `auto create <address>` — the automation, its track, ONE clip from beat 0 following its length (the
  song's end in whole bars), bound — prints the automation, `made:` the lane and the clip; `--pump` is gone (refused as
  any unknown flag). `auto add [--length <beats>] [--rest <v>]` — an automation and no clip (a bar long). `clip add
  --auto <au> [--at] [--length] [--lane <ln|new>]` — a clip of it. `set <au>.length=<beats>` (> 0; a point past it is
  kept, silent, and said) and `set <au>.rest=<v>` (in range). `set <au>.loop|.at` are refused, saying what replaced
  them. `auto point add|move|delete|shape <au> --at <b>` — `b` in the automation's own beats; a point at or past the
  end grows the length to the next whole beat (so a script that adds points in song beats to an automation made by
  `auto create` still lands them where it did — the EDM and canon demos). Splitting, slicing, consolidating an automation
  clip: refused (Not yet).
- **The model:** `clips[].kind` gains `automation`, `clips[].automation` its id; `lanes[].automation` the automation a
  track belongs to; `automations[]` gains `rest`, `of` (the first, for a copy) and `clips` (how many play it);
  `automations[].length` is the curve's length (**amended** — it was R-AUTO-12's span), `loop` the same number (the
  cycle a clip repeats — R-AUTO-12's meaning kept), `at` always 0 (retired). Nothing is renamed or removed.
- **Old songs:** a file without the header line `autoclips = on` predates clips; reading it adopts each automation —
  its points were in the song's beats, so it gets a track and ONE clip at beat 0, its length the last point's beat
  rounded up to whole bars (at least the song's end), `rest` its first value: it plays exactly as before. An R-AUTO-12
  loop becomes length = its cycle and a clip over its span (`at`, `length`; 0 = to the song's end), `rest` the value a
  cycle ends on — before the span it rests as it did; a span that ended mid-cycle now holds where it stopped instead of
  jumping to the cycle's end (the one difference, said in the note). Done in the model's read, so every reader — the
  window, a render of a version, a merge — sees one meaning (the `tracks = on` adoption is the precedent, R-LANE-3).
- **The engine** (law 11): a compiled curve is a set of shapes (the automation's and its copies', keys in beats ×
  samples-per-beat, as now) and its clips' spans (from, to, cycle length, shape), sorted, cut where a later one begins;
  `valueAt` finds the span by binary search — no allocation, as now. `eval`, playback and a render agree, measured.
- **Time edits** (R-ARR-2) move automation clips as clips (a clip crossing a deleted span is shortened, as a note
  clip is); an automation's own points never move.
- **The window:** the AUTOMATION section is gone; an automation's track is a row among the lanes (its name, what reads
  it, the accent stripe); its clips draw the curve — the first pass as drawn, each repeat dimmer with a seam (R-CLIP-7's
  look). In a clip: a drag on its name strip moves it (Shift: a copy), its end resizes it (repeats), a click in the curve
  adds a point, a drag moves one, Alt-drag shapes a bezier (R-AUTO-10), a double-click on a point removes it and
  elsewhere opens the automation's window, a right-click on a point sets its shape, elsewhere opens the clip's menu —
  Make unique, Length ▸ (1/4, 1/2, 1, 2 beats, 1, 2, 4 bars), Open automation, Copy ID, Delete. Editing in any repeat
  edits the shared curve (every linked clip moves with it, eased). The window's Loop combo becomes Length; it shows the
  rest value and how many clips play it. ParamMenu loses Create Pump; Create Automation is the same one line.
- **Parts** (a commit each): O1 the model, the engine, the service, the adoption, L2 and model tests, the API — the
  window's span grips, Loop combo and Create Pump removed with their tests; O2 the window — tracks among the lanes, the
  clips drawn and edited, the menus, shots.
- **Not yet:** an automation clip's own offset (a left-edge trim), splitting one, an automation clip in the session
  grid (R-LAUNCH-1), recording automation by moving a control while playing.
