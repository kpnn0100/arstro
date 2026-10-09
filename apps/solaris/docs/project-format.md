# Solaris — the `.slp` project format

Normative for `R-FMT`, `R-MIX`, `R-LANE` and the command grammar's nouns. The suite grammar
(Interstellar's `.isp`): one fact per line, a `#type id=…` line opens a node, canonical formatting
so "no change" is literally no diff, unknown keys and nodes preserved so a newer file opens in an
older build without loss. The audio nodes are the suite schema's
([`../../../docs/audio-format.md`](../../../docs/audio-format.md)); this file says how Solaris uses
them and adds the nodes that schema reserved for it.

---

## 1. Header

```
arstro-project = 1
app        = solaris
id         = prj_1
name       = "Night Drive"
timebase   = beats
bpm        = 128.0
sig        = 4/4
ppq        = 960
sampleRate = 48000
masterGain = 0.0            ; dB
masterOut  = prt_1          ; the output port(s) the master feeds, comma-separated
```

`timebase` is always `beats` in a Solaris project (audio-format §1). Every time field below is in
beats **except an audio clip's `in`/`out`, which are seconds into the file** — a file has no tempo,
and a source range that moved when the tempo changed would play a different part of the sample.

---

## 2. Output and input ports — `#aport`

```
#aport id=prt_1 name=Main   dir=out channels=2 order=0
#aport id=prt_2 name=Phones dir=out channels=2 order=1
```

A **logical** port (R-DEV-3). The project never names a device: Settings maps `Main` to a device's
channels on this machine. A port no device serves is offline, not an error.

## 3. Mixers — `#amixer`

```
#amixer id=mx_1 name=Sources order=0
#amixer id=mx_2 name=Buses   order=1
```

A page of strips (R-MIX-3). `order` is the **processing order** as well as the tab order, because
routing only goes forward (R-MIX-4).

## 4. Strips — `#atrack`

```
#atrack id=ch_1 name=kick  kind=audio      mixer=mx_1 order=0 gain=0.0 pan=0.0 mute=false solo=false out=ch_3 colour=2
#atrack id=ch_2 name=Lead  kind=instrument mixer=mx_1 order=1 out=ch_3
#atrack id=ch_3 name=Main  kind=bus        mixer=mx_2 order=0
```

The suite's `#atrack` **is a strip** (R-MIX-1): it already carried gain, pan, mute, solo and `out`.
Solaris adds two keys, both optional, so an Interstellar `.isp` keeps its meaning:

| field | meaning |
|---|---|
| `mixer` | the `#amixer` it lives on. Absent = the first mixer |
| `out` | a strip id on a **later** mixer, `master`, or a port id. Absent = master |
| `colour` | an index into the suite's track colours |

A route to a strip on the same or an earlier mixer is **refused**, naming both ends (R-MIX-4).

## 5. Sends — `#asend`

```
#asend id=sd_1 from=ch_2 to=ch_4  gain=-12.0 pre=false
#asend id=sd_2 from=ch_2 to=prt_2 gain=0.0   pre=true     ; a headphone cue straight to a port
#asend id=sd_3 from=ch_2 to=ch_3  gain=0.0   pre=true sidechain=true ; the kick keys the bass's compressor
```

Same forward-only rule as `out`. `sidechain=true` (written only when true; absent = false) makes the
send a KEY (R-MIX-15): it feeds the target strip's compressors' detectors, not its input, and may go
to any STRIP later in processing order, its own mixer included — never to the master or a port.

## 6. Racks — `#arack` / `#aeffect`

```
#arack track=ch_2
  #aeffect id=dv_1 type=synth osc1.wave=saw osc2.wave=square osc2.semi=7.0 filter.cutoff=1200.0
  #aeffect id=dv_2 type=reverb mix=0.2 bypass=true
#arack track=master
  #aeffect id=dv_3 type=compressor threshold=-12.0 ratio=4.0
```

Order is file order (the one place in the schema where it is, because a chain is a sequence).
`type` and every parameter key are **the DSP registry's** (R-DSP-2); values are in the registry's
units (R-DSP-3); a choice is written by name. An instrument strip's first device is its instrument.
A device whose type plays a recording (the `sampler`, R-EDM-8) names it with `sample=` — a path stored
as a clip's `src` is (relative to the song when inside its folder), written right after `type`:
`#aeffect id=dv_4 type=sampler sample="samples/vox chop.wav" mode=one-shot`.
`track=master` is the master's rack. A key the registry does not know is kept and reported by
`audit`; a value outside its range is clamped and counted.

## 7. Lanes — `#alane`

```
#alane id=ln_1 name=Drums order=0 colour=3
```

Organisation only (R-LANE-1). A clip's lane decides where it is DRAWN, never what it sounds through.

## 8. Patterns — `#apattern` / `#note`

```
#apattern id=pt_1 name="Beat A" length=4.0
  #note pitch=36 at=0.0 length=0.25 vel=110
  #note pitch=42 at=0.5 length=0.25 vel=80
```

A pattern is the notes a note clip plays (R-CLIP-2); clips that share a pattern are linked
(R-CLIP-3). `at` is beats from the pattern's start; `vel` 1–127. Notes are kept sorted by
(`at`, `pitch`), which is also the merge key a future semantic merge unions by.

A suite file whose note clip carries inline `#note` children (audio-format §2.2) is read as a
pattern named after the clip, and written back as one — the only normalisation the parser makes.

## 8a. Automation — `#aauto` / `#point`, and formulas — `#abind` (R-AUTO)

```
#aauto id=au_1 name="Bass · Cutoff" unit=Hz min=20.0 max=20000.0 from=dv_2.filter.cutoff
  #point at=0.0 value=600.0
  #point at=8.0 value=2400.0 shape=smooth

#abind address=dv_2.filter.cutoff formula="=au_1"
#abind address=ch_3.pan formula="=0.25 * sin(beat * pi)"
```

An automation is a curve that moves nothing until a formula reads it (R-AUTO-4): `at` in beats, `value`
in its unit, `shape` (`linear` when absent | `hold` | `smooth`) governs the segment AFTER the point.
`from` is the address it was made from — a hint, never a link. A binding (R-AUTO-1) decides one
address's value: `formula` as typed, with its `=`. One per address; the stored value under it (the
strip's `gain=`, the device's parameter) stays, and plays again when the binding is cleared. An
`#aauto` written in the suite schema's earlier sketch (indented `<beats> = <value>` lines, `node=`,
`param=`, `interp=`) is read and written back in this form — the second normalisation the parser
makes.

## 9. Clips — `#aclip`

```
#aclip id=ac_1 name=kick   track=ch_1 lane=ln_1 src="samples/kick.wav" at=0.0 in=0.0 out=0.512
#aclip id=ac_2 name="Beat" track=ch_2 lane=ln_2 pattern=pt_1 at=0.0 length=16.0
```

| field | audio clip | note clip |
|---|---|---|
| `track` | the audio strip it sounds through | the instrument strip |
| `lane` | where it is drawn (absent = its strip's own row, as in Interstellar) | same |
| `at` | beats | beats |
| `src` / `in` / `out` | file (relative to the project), source seconds | — |
| `pattern` | — | the `#apattern` it plays |
| `length` | beats it fills when `loop=true` | beats; longer than the pattern loops it |
| `gain`, `fadeIn`, `fadeOut` | dB, beats, beats | `gain` only |

---

## 10. Canonical serialization

Header keys in §1's order, then unknown header keys in read order. Nodes grouped by type in this
document's order (`#aport`, `#amixer`, `#atrack`, `#asend`, `#arack`, `#alane`, `#apattern`,
`#aclip`, `#aauto`, `#abind`, then unknown nodes in read order), each type in creation order; children indented two
spaces. Fields in the order the tables give, then unknown keys in read order. Booleans
`true`/`false`. Numbers: always a decimal point (`0.0`); **times are rounded to the tick (1/960
beat) and printed with the fewest decimals that read back to the same tick**; seconds to the
microsecond. Strings quoted only when they contain a space, a quote, `;`, `=` or `#`. A whole-line
comment (`;`) is kept with the node it follows. **Parse → serialize is a byte-exact fixed point** for
canonical text.

## 11. Validation — what is refused, what is repaired

| refused | why |
|---|---|
| two nodes with one id | references would be ambiguous |
| a route or send to a strip on the same or an earlier mixer | R-MIX-4 — and it names both ends |
| `out`/`to`/`track`/`mixer`/`lane`/`pattern` naming nothing | named, with the candidates |
| an audio clip on a non-audio strip, a note clip on a non-instrument strip | the clip could not sound |
| an audio clip with `in >= out` | a clip with no samples is not a clip |
| **repaired**, never refused: a non-finite number → its default; a parameter out of range → clamped | one stray byte must not make a project unopenable; both are counted and reported |

## 12. The command grammar

The authoritative list is **generated**: [`API.md`](API.md) / [`api.json`](api.json), printed by
`solaris-cc api` from the table the parser reads, committed, drift-tested. Summary:

```
project new <p.slp> [--bpm 120] [--sig 4/4] [--rate 48000] | project open <p> | project save [p] | project close
set <address>=<value> … | get <address>              ; project.bpm, ch_1.gain, dv_1.filter.cutoff, ac_1.at …
mixer add [name] | mixer delete <mx> | mixer move <mx> --to <i>
strip add --kind audio|instrument|bus [--name n] [--mixer mx] [--instrument synth|drums] [--out <target>]
strip delete <ch> | strip move <ch> --mixer <mx> | route <ch> --to <ch|master|port>
send add <ch> --to <target> [--gain dB] [--pre] | send delete <sd>
device add <ch|master> --type <type> [--at i] | device remove <dv> | device move <dv> --to <i>
lane add [name] | lane delete <ln> [--with-clips]
clip add --src <file> [--lane ln] [--at b] [--strip ch]          ; audio — a new file gets its own strip
clip add --strip <ch> [--pattern pt] [--lane ln] [--at b] [--length b]   ; notes
clip move <ac> [--at b] [--lane ln] [--strip ch] | clip duplicate <ac> [--at b] | clip unique <ac> | clip delete <ac>
pattern new [--name n] [--length b] | note add <pt> --pitch p --at b [--length b] [--vel v] | note delete <pt> --pitch p --at b
transport play | transport stop | transport seek <b> | transport loop <a> <b> | transport loop off
render --out <f.wav> [--from b] [--to b] [--stems ch,…] [--ports] [--bits 24|32f]
matrix print [--json] | audit | state print [--json] [--stable] | registry [--json] | api [--json|--md]
settings set <key>=<value> … | settings print | devices list | browse <folder>
```
