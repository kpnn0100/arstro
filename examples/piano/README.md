# Arstro Piano

A **playable** one-octave keyboard UI: press computer keys, hear a physically-modeled piano —
hammer felt compressing a string, inharmonic partials, a shared soundboard, real damper/pedal
behavior — not a sample player. The DSP is `arstro::PianoEngine` /
[`PianoVoice`](../../DigitalSignalProcessing/src/physical/README.md) (8-voice pool sharing one
`PianoBridge`); the UI is drawn with **Arstro Artboard**.

`PianoApp` is platform-free (only `IRenderTarget` + `PianoEngine`), same shape as
[`examples/synth`](../synth/README.md)'s `SynthApp` (`render()` / `renderAudio()` / `key()`) —
read that example first if you haven't, this one is deliberately simpler (no pages/knobs, just
the keyboard).

## Controls

| Keys | Action |
|---|---|
| `A W S E D F T G Y H U J K` | One octave, C to C (white keys `A S D F G H J K`, black keys `W E T Y U`) |
| `Z` / `X` | Shift octave down / up |
| `Space` (hold) | Sustain pedal — damper stays lifted |
| `Shift` (hold) | Una corda (soft pedal) |
| `Ctrl` (hold) | Sostenuto — holds only the notes already sounding when pressed |

Same QWERTY-row layout as `examples/synth`, so the two apps share one muscle-memory mapping.

## Voicing panel (live tuning)

To the right of the keyboard is a **VOICING** panel — one slider per physics parameter, so you
can hear the model change under your fingers (it re-voices held notes too, not just the next
strike). The controls are built from `PianoEngine::tuneSpec()` metadata, so the list is the
engine's, not the UI's — add a `Tune` enumerator and it appears automatically.

| Slider | What it changes (README §) |
|---|---|
| Hammer hardness | felt stiffness `K` × — soft/round ↔ bright/percussive (§6, §11.3) |
| Felt curve (p) | hammer nonlinear exponent (§6) |
| Felt hysteresis | base load/unload asymmetry `ε_branch` (§6) |
| Felt relaxation | Stulov rate-dependent loss depth × (§6, M9.3) |
| Decay / sustain | string `T60` × — how long notes ring (§3) |
| Brightness T60 | how fast the high partials die (§3) |
| Inharmonicity | stiffness/metallic stretch × (§2) |
| Unison detune | cents between unison strings — chorus/beating (§5) |
| Bass growl | longitudinal tension coupling `κ` — phantom partials (§12) |
| Attack glide | tension-modulation `κ_t` × — attack pitch glide (§12.5) |
| Treble shimmer | duplex/aliquot drive (§8.1) |
| Master gain | output level into the soft limiter |

Multiplier (`×`) sliders scale the **per-note register default**, so the keyboard's natural
bass→treble scaling is preserved; the rest are absolute. **Reset voicing** restores every slider
to the shipped default (neutral = the model exactly as the milestones left it). Ranges are bounded
to stay clear of contact-loop instability, so even the extremes stay finite and bounded.

## Build & run

Needs GTK3 + ALSA dev packages (`gtk+-3.0`, `alsa` via pkg-config).

```bash
./build.sh --project piano --target linux-native-app
./examples/piano/build/piano_linux
```

Or via the top-level CMake umbrella build (selectable/launchable as `arstro_piano_ui` from an
IDE's CMake target picker):

```bash
cmake -S . -B build && cmake --build build --target arstro_piano_ui
./build/examples/piano/arstro_piano_ui
```

If GTK3/ALSA dev packages aren't found, the CMake target is skipped with a status message
(`ARSTRO_BUILD_PIANO_UI`, default `ON`) rather than failing the whole configure.

## Architecture notes

- **Audio**: a dedicated ALSA playback thread pulls `PianoApp::renderAudio()`, which calls
  `PianoEngine::renderBlockBytes()` (already includes the headroom + soft limiter built for
  `apps/piano_demo`) and converts the S16LE PCM back to float for ALSA.
- **Threading**: `PianoEngine` has no internal lock-free queue (unlike `SynthEngine`), so
  `key()` (GTK/UI thread) only enqueues a command into a small mutex-guarded queue;
  `renderAudio()` (ALSA thread) drains it first, so every `PianoEngine`/`PianoVoice` access
  happens on one thread. See the threading note in `PianoApp.h`.
- No web build (yet) — this app is native-only. `examples/synth`'s `web_main.cpp` +
  `web/index.html` is the pattern to follow if that's ever wanted (Web Audio
  `ScriptProcessorNode` in place of the ALSA thread).
