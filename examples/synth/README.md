# Arstro Synth

A full-function compact synth whose UI follows the supplied React design: a **536×240 module**
with a status bar, five colour-coded pages, a bottom nav, draggable knobs, and animated
per-page visualisers — drawn entirely with **Arstro Artboard** and sounded by **Arstro DSP**.

`SynthApp` is **platform-free** (only `IRenderTarget` + the DSP engine), drawn immediate-mode in a
fixed design space that scales to fill the host surface. It runs under both adapters:

| Target | Render | Audio | Input |
|--------|--------|-------|-------|
| Web    | Canvas2D (WASM) | Web Audio (`ScriptProcessor`) | mouse + keyboard |
| Linux  | Cairo (GTK3 window) | ALSA playback thread | mouse + keyboard |

## Pages (← → to switch)

- **HOME** — patch info, live L/R output meters (from real audio peak), active-voice count, master
  `vol`/`pan` knobs.
- **OSC** — animated waveform visualiser; `SIN/SAW/SQR/TRI` selector and `lvl`/`det`/`spd`/`voc`
  knobs → oscillator level, detune, stereo spread, unison voice count.
- **ENV** — ADSR curve + animated playhead; `atk`/`dec`/`sus`/`rel` knobs → the voice envelope.
- **FX** — five effects (`CMP`/`DRV`/`CHR`/`DLY`/`RVB`), each with its own animated visualiser and
  3 knobs → compressor, overdrive, chorus, delay (repeater) and reverb. Click the selected tab
  again to **bypass** it.
- **SET** — device/settings readout.

The `SIN/SAW/SQR/TRI` selector picks a real oscillator waveform (sine, band-limited saw/square,
triangle) — it changes both the visualiser and the sound. Every knob drag pushes a live parameter
into the DSP `SynthEngine`; the value pops up large while you drag. Page changes slide in
(Artboard `Animator`/`Tween`).

## Controls

- **Keys** `A W S E D F T G Y H U J K` — play one octave (C..C).
- **← / →** — switch pages. **↑ / ↓** — move selection on FX / SET.
- **Drag** a knob vertically to change it.


## Build & run

Web (needs Emscripten: `source ~/emsdk/emsdk_env.sh`):

```bash
./build.sh --project synth --target linux-web-server
(cd examples/synth/web && python3 -m http.server 8000)   # open http://localhost:8000
```

Native Linux (needs GTK3 + ALSA dev packages):

```bash
./build.sh --project synth --target linux-native-app
./examples/synth/build/synth_linux
```
