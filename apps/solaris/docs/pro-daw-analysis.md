# Solaris — what it still needs to be a professional DAW

*Analysis, 2026-10-16 (ledger M3), at the user's request: "do a full analysis, suggest me feature that need to be
implemented so this will be a professional DAW".*

**How this was made.** Every requirement area's status in `REQUIREMENTS.md`; the device registry (`docs/API.md` ›
Devices); the host, engine and service code (what drivers, threads, file types and inputs exist); the two songs made
in the app (the Prelude, Perihelion) and what they ran into; and the everyday expectations a working producer brings
from Ableton Live, FL Studio, Bitwig, Logic, Cubase and Reaper. Every gap below was checked against the code, not
assumed.

---

## 1. Where Solaris stands

Solaris is already strong where most young DAWs are weak:

| area | what is there |
|---|---|
| Architecture | One service behind every front end; every gesture is one command line; a generated, drift-tested API; a CLI that is the whole app; a control socket and an equivalence test. An agent can make a song (the Prelude, Perihelion). |
| Mixer | A strip per source, mixer pages, buses, sends (pre/post), a routing matrix, forward-only routing, side-chain keys, latency compensation, linear fades, balance pan. |
| Automation | Any number bound to a formula; curves with linear / hold / smooth / bezier points; links between parameters; `eval --explain`; loops and a one-gesture pump. |
| Editing | Piano roll (selection, copy and paste, quantize, velocity lane), step grid, MIDI clips that loop by their end, linked duplicates, Shift-drag and Ctrl+C / Ctrl+V for clips, a history tree. |
| Sound | Basic Synth (stereo unison), Piano (physical model), Drum Machine (12 pads), Sampler; Compressor, EQ, Reverb, Delay, Chorus, Drive, Filter, Limiter. Our instruments also ship as VST3 plugins. |
| Output | A pure render (byte-identical however it is chopped), stems, WAV. |
| The window | Settings in pages, rebindable keys, cosmo's design rules (nothing snaps), the resize cursor, windows inside the song. |

What it cannot do yet decides whether a producer can finish a commercial track in it. That is the list below.

---

## 2. The gaps, by how soon a professional hits them

Effort: **S** about a day of work · **M** a few days · **L** a week or two · **XL** more, usually DSP and host work
together. Ids in brackets are requirements that already reserve the item.

### P0 — blockers: a producer cannot finish a release without these

| # | feature | why it blocks | effort |
|---|---|---|---|
| 1 | **Autosave and crash recovery.** A snapshot beside the song every few minutes and on every risky action; on the next launch, an offer to restore. | Nothing is kept if the app or the machine dies. A pro will not trust a DAW with hours of work without it. | S–M |
| 2 | **A MIDI keyboard.** Play an instrument live through its strip, record into a pattern (overdub or replace), with count-in and quantize on input [R-EDM-10]. | Today every note is drawn or typed. Most producers play their ideas in. | L (host MIDI input via ALSA/PipeWire, a live note path in the engine) |
| 3 | **Audio recording.** Arm a strip, record from an input port, monitor the input, sample-aligned, with punch in/out and loop takes [R-EDM-11, R-REC]. | Vocals, guitars, resampling: impossible today. | L |
| 4 | **Low-latency audio.** JACK and PipeWire natively beside PulseAudio; buffers down to 64 frames; xruns counted and shown [R-DEV]. | PulseAudio alone is too slow to play or record live, and dropouts go unnoticed. | M–L |
| 5 | **Other makers' plugins.** Host VST3 instruments and effects, scanned, each in its own process so a crash takes the plugin offline and never the song; its latency compensated; its parameters automatable; its own editor window [R-EDM-17, R-VST-6]. | Every producer's sound lives partly in third-party plugins (Serum, Vital, FabFilter, Valhalla…). | XL |
| 6 | **Warp and tempo.** Time-stretch audio to the song's tempo (a sample's own BPM detected), pitch without tempo; then a tempo map and tempo automation [R-EDM-13, R-EDM-14, R-CLIP-5]. | A loop at another tempo cannot be used today; neither can a tempo change. | XL (DSP: a phase vocoder or élastique-class stretcher) |
| 7 | **MIDI files.** Import and export `.mid`, patterns and tempo. | The common language with every other tool, with collaborators and with sample packs. | S–M |
| 8 | **Mastering meters and a true-peak limiter.** LUFS (integrated, short-term, momentary), true peak, a spectrum analyser, stereo correlation; a true-peak limiter in the DSP library [R-EDM-15; noted while mastering Perihelion]. | Streaming services judge loudness in LUFS. Today the master shows a block peak only (R-PLAY-3 is partial). | M (meters), M–L (limiter) |
| 9 | **Export formats.** FLAC, MP3 (and OGG), bit depth and sample rate on export, dither, normalise to a LUFS target [R-EDM-16]. | WAV alone means a second tool for every delivery. | S–M (FFmpeg is already linked) |

### P1 — expected in any serious session

| area | features | effort |
|---|---|---|
| **Audio editing on the lanes** | Split at the playhead or the pointer; fades and crossfades drawn on the clip; a clip-gain handle; reverse; consolidate / bounce in place; chop a sample to slices or pads [R-EDM-12]. | M–L |
| **CPU and scale** | A CPU meter (per strip too); freeze / bounce a strip to audio [R-EDM-18]; a multi-core engine — today one thread renders every strip, so a large song will drop out where other DAWs would not. | L |
| **MIDI depth** | Pitch bend, mod wheel and CC lanes (automation from a controller); note probability, humanise, swing and groove templates; scale lock and chord tools in the roll; MIDI effects (arpeggiator, chord, note repeat); glide / legato. | L |
| **Arrangement** | Markers and sections (Intro, Build, Drop) to jump to and loop; insert or delete time across all lanes; lane groups and folders; an overview strip of the whole song. | M |
| **Mixer depth** | Group / VCA faders; a phase-invert, a width and a mono check per strip; RMS and peak-hold meters with a clip latch [R-PLAY-3]; solo-safe. | M |
| **A real side-chain in one gesture** | On a strip, "Duck under ▸ Kick": one command that inserts a compressor keyed from the chosen strip with musical defaults. The pump (R-AUTO-13) gives the feel; this gives the real thing, following the kick as it is played. | S–M |
| **More sound** | Instruments: a wavetable synth, an FM synth, a multi-sample instrument (SFZ import), a granular sampler, an 808 with glide. Effects: multiband compressor, gate / expander, transient shaper, de-esser, saturator / bitcrusher, phaser / flanger, stereo imager, convolution reverb (impulse responses), pitch shifter, vocoder, a tuner. All in the DSP library, with no Solaris code (R-DSP). | XL in total, each M |
| **Presets and templates** | Device presets saved, named and browsed (R-BROWSE's preset half waits on this); song templates; a sample library with tags, favourites and search [R-EDM-20]. | M |
| **Hardware control** | MIDI learn for any address; Mackie / HUI control surfaces; transport from a keyboard's buttons. | M |
| **Several audio devices** | A strip played on another device, clocked to the first [R-EDM-19, R-DEV P2]. | L |

### P2 — what sets a DAW apart once the rest is there

- **Versions of a song** — arrangements sharing their sound (R-VER, specified, not built).
- **Collect and save** — gather every sample into the song's folder; a relink window for missing files (the model already marks clips offline).
- **Comping** — takes stacked on a lane, the best parts chosen.
- **A clip launcher** — a session grid for live performance and sketching (Ableton's Session view).
- **A video lane** — scoring to picture, synced.
- **Collaboration** — a song shared and synced; comments on the timeline.
- **Accessibility** — UI scale, high contrast, screen-reader names for controls; localisation.
- **Assistive tools** — stem separation of an audio clip, audio-to-MIDI, chord detection.

---

## 3. Things noticed while making this analysis

- **An automation row is drawn over its whole range.** A pump on a gain (−120 … 12 dB) is an 18 dB dip, about a
  seventh of the row's height, so it is hard to see and edit. The row could zoom to the values the curve uses, or
  draw a level in dB on a fader's taper.
- **The master shows a block peak only.** Without RMS, a peak hold and a clip latch, gain staging is guesswork
  (R-PLAY-3's open half).
- **Live checks owed.** Shift+wheel (D-5), typing in a field (D-9) and the resize cursor have each been tested
  through the App but not driven in the real window, because this GTK build ignores synthetic pointer events.

---

## 4. A recommended order

Each phase leaves the app usable end to end for a larger group of producers.

1. **Finish a beat-made track safely and deliver it:** autosave and recovery (1) → MIDI file import / export (7) →
   mastering meters and the true-peak limiter (8) → FLAC / MP3 export (9) → the one-gesture side-chain.
2. **Play it in:** low-latency audio (4) → a MIDI keyboard, played and recorded (2) → MIDI depth (pitch bend, CC,
   scale lock) → arrangement markers.
3. **Work with audio:** audio recording (3) → audio editing on the lanes → warp and tempo (6).
4. **Open the ecosystem:** presets and templates → hosting other makers' plugins (5) → MIDI learn and control
   surfaces → a multi-core engine and freeze.
5. **Grow the sound and the craft:** the instruments and effects above → mixer groups / VCA → versions → the P2
   list.

The existing EDM backlog (R-EDM-10 … 20) maps onto phases 1–4 and keeps its ids. A phase starts with its own
requirement round, as every brief does.
