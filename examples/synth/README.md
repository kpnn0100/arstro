# Arstro Basic Synth

A compact, pretty synth example that links **Arstro DSP** (sound) with **Arstro Artboard**
(drawing + UI + animation). A playable one-octave keyboard drives the DSP `SynthEngine`; the
sound is visualised as a glowing oscilloscope and a spectrum, and the whole UI animates with the
Artboard animation system.

The app (`SynthApp`) is **platform-free** — it touches only Artboard's `IRenderTarget` and the DSP
engine — so the identical code runs under both adapters:

| Target | Render | Audio | Input |
|--------|--------|-------|-------|
| Web    | Canvas2D (WASM) | Web Audio (`ScriptProcessor`) | mouse + QWERTY |
| Linux  | Cairo (GTK3 window) | ALSA playback thread | mouse + QWERTY |

## What it demonstrates

The whole UI is built from the Artboard **widget set** on the `Segment` tree — a "flex" of the
framework:

- **`LineGraph`** ×2 — the oscilloscope and the spectrum (data visualisation).
- **`ProgressBar`** — the output level meter.
- **`TabView`** — TONE / FX / HELP pages.
- **`Knob`** ×3 — DRIVE / CHORUS / REVERB (vertical drag; double-click resets).
- **`ComboBox`** — the preset selector (Clean / Warm / Space).
- **`ToggleSwitch`** — animated DRIVE FX / REVERB FX bypass switches.
- **`ScrollView`** — the HELP page (clipped, scrollable text), exercising the HAL `clipRect`.
- **`KeyboardSegment`** — a one-octave keyboard with animated key-press glow.

Animation: an `AnimatedProperty` intro reveal (`Tween` + `EaseOutCubic`), the `Animator` timeline,
and per-key glow tweens.

## Controls

- **Keys** `A W S E D F T G Y H U J K` — play one octave (C..C).
- **Knobs** — drag vertically to set; double-click to reset (animated).
- **Tabs / drop-down / toggles** — click; the HELP tab scrolls.
- Click ▶ **Start audio** on the web page (browsers require a gesture before audio).

## Build & run

Web (needs Emscripten on PATH: `source ~/emsdk/emsdk_env.sh`):

```bash
./build.sh --project synth --target linux-web-server
(cd examples/synth/web && python3 -m http.server 8000)   # open http://localhost:8000
```

Native Linux (needs GTK3 + ALSA dev packages):

```bash
./build.sh --project synth --target linux-native-app
./examples/synth/build/synth_linux
```
