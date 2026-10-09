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
