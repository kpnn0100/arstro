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
