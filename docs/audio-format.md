# The Arstro audio project format

> **Status: specified, partially implemented.** Interstellar implements the sample-clip subset
> (§4) and MIXES it — clip gain and fades, track gain, pan, mute and solo summed to a stereo master
> that its renders carry (Interstellar R-AUD-9, DR-AUD-2); buses and sends sum straight to the master
> there. Solaris will implement the rest. **This document is the contract between them**, which is
> why it lives at suite level rather than inside either app — a format two apps read is not one
> app's property.

Audio in this suite has two very different consumers. **Interstellar** needs to place a bed under a
cut, stage its gain, and deliver it: it does not mix, record or process. **Solaris** is a DAW and
needs instruments, racks, automation, buses and sends. Designing for only the first produces a
format Solaris has to migrate away from; designing only for the second produces one Interstellar
cannot implement. So this schema is the full shape, and **Interstellar implements a subset while
parsing, round-tripping and preserving the rest** — a Solaris project opens in Interstellar without
loss, and an Interstellar project opens in Solaris as a very simple song.

It shares the host project's text grammar (`#type id=… key=value`, one fact per line, canonical
formatting, unknown keys preserved), so an audio section is just more nodes in a `.isp` or a `.slp`
and needs no second parser.

---

## 1. Time, and the one thing both apps must agree on

The hardest interop question is not the nodes, it is **what a time means**. Interstellar thinks in
frames; Solaris thinks in beats. Neither may guess.

So a project states its clock, and **every time field carries its unit in the project header, never
per node**:

```
timebase   = seconds          ; seconds | beats
fps        = 24               ; present when timebase = seconds
bpm        = 120              ; present when timebase = beats
sig        = 4/4
ppq        = 960              ; ticks per quarter note, when beats
sampleRate = 48000
```

- **`timebase = seconds`** — Interstellar's. Times are decimal seconds on a frame boundary
  (R-TL-5). Solaris opening such a project reads it as a fixed-tempo song and may add `bpm`.
- **`timebase = beats`** — Solaris's. Times are beats; seconds are derived from `bpm`/`ppq`.
  Interstellar opening such a project converts to seconds **at load** using the stated tempo and
  says so, rather than carrying two clocks it cannot keep in step.

A project with audio **must** state `sampleRate`, because a render that resamples silently is a
render nobody can reproduce.

---

## 2. Nodes

### 2.1 `#atrack` — a lane

```
#atrack id=atr_1 name=bed kind=audio order=0 gain=0.0 pan=0.0 mute=false solo=false
#atrack id=atr_2 name=bass kind=instrument order=1 gain=-3.0
#atrack id=atr_3 name=drumbus kind=bus order=10
```

| field | type | meaning |
|---|---|---|
| `kind` | `audio \| instrument \| bus` | `audio` plays sample clips; `instrument` is driven by note clips; `bus` receives sends |
| `gain` | dB | −∞…+12; `0.0` is unity. **dB, not a linear factor** — a fader is logarithmic and storing the linear value makes every file unreadable by a person |
| `pan` | −1…+1 | −1 hard left |
| `order` | int | lane order, and the bus sum order |
| `out` | id | the track or bus this feeds; absent = master. Solaris also accepts `master` and a `#aport` id |
| `mixer` | id | Solaris: the `#amixer` page this strip lives on; absent = the first (§2.9) |

> **AMENDED (Solaris, 2026-10-08):** an `#atrack` **is a mixer strip** — it always carried gain, pan,
> mute, solo and `out`, which is what a strip is. Solaris separates the strip from the timeline row
> it used to also be: rows are `#alane` (§2.7) and a clip names both (`track=` what it sounds
> through, `lane=` where it is drawn). A clip with no `lane` is drawn on its track's own row — which
> is exactly what every existing `.isp` means, so nothing an Interstellar file says changes. Solaris
> also requires a route to go forward: to a strip on a later `#amixer`, `master`, or a port.

**Interstellar implements `kind=audio` only** and refuses to *render* an `instrument` track rather
than silently dropping it — a silent track is indistinguishable from a bug.

### 2.2 `#aclip` — a region on a track

Two forms, told apart by whether `src` is present.

**Sample clip** (Interstellar + Solaris):
```
#aclip id=ac_1 name=bed_a track=atr_1 src=res:9c1f… at=0.000 in=0.000 out=184.500
  gain=0.0 fadeIn=0.250 fadeOut=1.000 loop=false
```

**Note clip** (Solaris):
```
#aclip id=ac_2 name=bassline track=atr_2 at=0.000 length=8.000
  #note pitch=36 at=0.0 length=0.5 vel=100
  #note pitch=43 at=2.0 length=1.0 vel=90
```

| field | meaning |
|---|---|
| `at` | position on the timeline, in the project's timebase |
| `in` / `out` | the source range (sample clips) |
| `length` | the clip's extent (note clips) |
| `gain`, `fadeIn`, `fadeOut` | clip-level staging, in dB and timebase units |
| `loop` | repeat the source range to fill `length` |
| `lane` | Solaris: the `#alane` it is drawn on; absent = its track's own row |
| `pattern` | Solaris: a note clip plays this `#apattern` (§2.8) instead of inline notes |

**`in`/`out` are seconds into the source in every timebase** — a file has no tempo, and a source
range measured in beats would play a different part of the sample after a tempo change.

Fades are **linear in amplitude** over the stated duration, and pan is a **balance with unity at
centre** (the side panned away from falls on a quarter cosine).
**AMENDED (Solaris, 2026-10-08):** this paragraph said fades were *linear in dB*. Interstellar's
`AudioMix` (`apps/interstellar/render/AudioMix.cpp`) has always faded linearly in amplitude, and it
is the shipped implementation of this contract; a document that disagrees with the only code that
implements it is the document's defect. Solaris matches the code (Solaris R-MIX-11), so a project
sounds the same in both apps.

### 2.3 `#note` — MIDI as plain text

`pitch` (0–127), `at`, `length`, `vel` (1–127). One line per note, so a part is diffable and
mergeable note by note, and a semantic merge can union two takes by start time.

### 2.4 `#arack` / `#aeffect` — the processor chain

```
#arack track=atr_2
  #aeffect id=fx_1 type=synth  osc=saw cutoff=0.4 res=0.2 env.a=0.01 env.r=0.3
  #aeffect id=fx_2 type=eq     low=+2 mid=-1 high=+1
  #aeffect id=fx_3 type=reverb mix=0.18 size=0.7
```

Order is file order within the `#arack` — the one place in this schema where it is, because a
signal chain *is* a sequence and giving it an `order` key as well would be two copies of one fact.
Parameters are `type`-specific and are **named by the DSP library's own parameter paths**, so a
preset, a project and a `solaris param` command all spell `cutoff` identically.

**Interstellar preserves these verbatim and renders none of them.**

### 2.5 `#aauto` — a parameter curve

```
#aauto id=au_1 node=fx_1 param=cutoff interp=linear
  0.000 = 0.40
  4.000 = 0.80
  8.000 = 0.40
```

Breakpoints are `<time> = <value>` in the project's timebase. The same shape as Interstellar's video
automation, deliberately: one breakpoint syntax in the suite.

(**AMENDED (Solaris R-AUTO, 2026-10-09):** nothing wrote the sketch above. Solaris writes an automation
as `#aauto id=au_1 name=… unit=… min=… max=… from=<address>` with `#point at=… value=… [shape=hold|smooth|bezier]`
children (a bezier point carries Interstellar's handle fields, `speedIn= inflIn= speedOut= inflOut=`, per
beat and in % — R-AUTO-10, Solaris `docs/project-format.md` §8a) — the same child-node form as `#note` — and a separate `#abind address=… formula="=au_1"`
decides what it drives (an automation alone moves nothing). Solaris still READS the sketch and writes it
back in the new form; Interstellar keeps its own `#anim`/`#key` (its R-ANIM).)

### 2.6 `#asend` — routing

```
#asend id=sd_1 from=atr_1 to=atr_3 gain=-12.0 pre=false
```

`pre` selects pre- or post-fader. Absent sends mean a track goes to `out` (or master) only.
In Solaris `to` may also be `master` or a `#aport` id (a headphone cue straight to a port).

### 2.7 `#alane` — a timeline row (Solaris)

```
#alane id=ln_1 name=Drums order=0 colour=3
```

Organisation only: it holds any clips, from any strips. Where a clip is drawn, never what it sounds
through.

### 2.8 `#apattern` — notes shared by clips (Solaris)

```
#apattern id=pt_1 name="Beat A" length=4.0
  #note pitch=36 at=0.0 length=0.25 vel=110
#aclip id=ac_2 track=atr_2 lane=ln_2 pattern=pt_1 at=0.0 length=16.0
```

Clips naming one pattern are linked: edit the notes once, every copy changes. A clip longer than
its pattern loops it. A note clip with inline `#note` children (§2.2) is still valid.

### 2.9 `#amixer` and `#aport` (Solaris)

```
#amixer id=mx_1 name=Sources order=0
#aport  id=prt_1 name=Main dir=out channels=2 order=0
```

A mixer is a page of strips; its `order` is the processing order. A port is a **logical** output or
input — the project never names a device; the machine's settings map a port to a device's
channels. The header's `masterOut` names the port(s) the master feeds.

---

## 3. The master

There is no `#amaster` node: the master is the implicit sum of every track whose `out` is absent,
and its own gain is a project field (`masterGain`). A node for it would be a node that cannot be
deleted, which is a node that should not exist.

---

## 4. What Interstellar implements

| | Interstellar | Solaris |
|---|---|---|
| `#atrack kind=audio`, `gain`, `pan`, `mute`, `order` | **yes** | yes |
| `#aclip` sample form, `at`/`in`/`out`/`gain`/`fade` | **yes** | yes |
| master sum, `masterGain` | **yes** | yes |
| `#atrack kind=instrument \| bus`, `#asend` | parsed, preserved, **refused at render** | yes |
| `#aclip` note form, `#note` | parsed, preserved, not rendered | yes |
| `#arack`, `#aeffect`, `#aauto` | parsed, preserved, not rendered | yes |
| `#alane`, `#apattern`, `#amixer`, `#aport`; `lane=`/`pattern=`/`mixer=` keys | parsed, preserved (unknown to it) | yes |

**"Parsed, preserved, refused" is the contract**, and the three words are different: a node
Interstellar does not understand still round-trips byte-identically (so a Solaris project is not
damaged by being opened), and a node it cannot *render* makes the render refuse with the node named,
rather than producing a mix that is quietly missing a part.

---

## 5. Why not reuse an existing format

Considered and rejected: **OMF/AAF** (binary, enormous, and the interchange problems they solve are
not ours), **a DAW's native format** (none is specified, all are versioned against one application),
and **a second file beside the project** (two files that must agree about time is the drift this
suite exists to avoid — the audio lives in the same document as the cut it belongs to).

What this format is *for* is being the seam between two apps in one suite that already share a text
grammar, a resource pool and a merge engine. That is a much smaller problem than interchange, and
it is solved by agreeing on a clock and a dozen node types.
