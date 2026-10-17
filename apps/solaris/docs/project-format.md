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
tracks     = on             ; R-LANE-3: lanes may be instruments' tracks (absent = a song from before them)
launchQuantize = bar        ; R-LAUNCH-1: where a launch lands — bar | beat | none (written only when not bar)
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

**Recording (R-REC-1).** An audio strip may name its INPUT, an input port, and how it is monitored:
`#atrack id=ch_3 name=Vox kind=audio … input=prt_2 monitor=off` — both written only when set (`monitor` is `auto`
unless set: heard while armed and stopped or recording). An input port is `#aport id=prt_2 name=Mic dir=in
channels=1`; `port add` makes one.

**Frozen (R-PERF-3).** A strip may be FROZEN: `#atrack id=ch_2 name=Lead kind=instrument … frozen="Frozen/Lead
ch_2.wav"` — the file (relative to the song) it plays from the start instead of its own work; its clips, notes and
devices are kept, idle. Written only when set; `strip freeze <ch>` writes the file and the key, `strip unfreeze <ch>`
drops the key (the file stays). A file that cannot be read leaves the strip silent, and says so.

**VCAs (R-MIX-19).** A VCA is a strip of its own kind — `#atrack id=ch_4 name=Synths kind=vca mixer=mx_1 order=2
gain=-6.0` — with no output, no rack, no clips and no sends (each refused when written by hand); a strip that follows
it says so: `#atrack id=ch_2 name=Lead kind=instrument … vca=ch_4` (written only when set). A member plays at its own
gain plus its VCA's; the VCA's mute and solo are its members'. A VCA follows no VCA.

**Per-strip tools (R-MIX-20).** `phase=invert` (its polarity inverted, after its rack), `width=150` (its stereo width in %,
0 … 200 — 100, the default, is not written) and, on a bus, `soloSafe=true` (a solo never silences it) — each on the
strip's `#atrack` line, only when set. The mono check is the listener's and never in the file.

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
A device whose type plays a recording (the `sampler`, R-EDM-8; the `convolution` reverb's impulse response,
R-FX-14) names it with `sample=` — a path stored
as a clip's `src` is (relative to the song when inside its folder), written right after `type`:
`#aeffect id=dv_4 type=sampler sample="samples/vox chop.wav" mode=one-shot`. A multi-sample instrument's `sample=` is
its DEFINITION, an .sfz (R-INST-9): `#aeffect id=dv_5 type=multisample sample="kits/upright/piano.sfz"` — the sounds it
names lie beside it, as the SFZ writes them.
`track=master` is the master's rack. A key the registry does not know is kept and reported by
`audit`; a value outside its range is clamped and counted.
Another maker's plugin (R-VST-6) is `type=vst3`: its class id (`plugin=`, the UID), its `name`, its `kind`
(`instrument` | `effect` — so a strip stays what it was where the bundle is missing) and its `bundle` (absolute, where
the scan found it), written right after `type`; its parameters as `p<id>=<0 … 1>` (only those the song set); and
`state=` LAST — the plugin's own state, base64, asked of it whenever the song is written. Opened, a plugin is given its
state, then the values the song sets. A bundle this machine does not have leaves the device offline, the song open:
`#aeffect id=dv_5 type=vst3 plugin=9FEB6596B6B044A4A46AC1C4145CD7D7 name="Arstro Basic Synth" kind=instrument
bundle=/home/me/.vst3/ArstroBasicSynth.vst3 p0=0.6 state=YXJzdHJv…`.

## 7. Lanes — `#alane`

```
#alane id=ln_1 name=Drums order=0 colour=3
```

Organisation only (R-LANE-1) — unless it names an instrument strip, `track=ch_2`: then it is that
instrument's TRACK (R-LANE-3) and every clip on it plays through it (a note clip of another strip or an
audio clip there is refused). A track with no `name` shows its strip's. A file with no `tracks = on`
header line predates tracks: Solaris makes each lane whose clips all play through one instrument that
instrument's track on opening, and writes the line when it saves.

```
#alane id=ln_2 name="" track=ch_4 order=1
```

**Groups (R-ARR-3).** `#alanegroup id=lg_1 name=Drums folded=on` (`folded` only when on) is written before the lanes;
a lane in it says `group=lg_1`. A group's lanes are side by side in the lane order — a song read with them scattered
is gathered at each group's first lane, and said so. Folded, the group is one row on the timeline.

## 8. Patterns — `#apattern` / `#note`

```
#apattern id=pt_1 name="Beat A" length=4.0
  #note pitch=36 at=0.0 length=0.25 vel=110
  #note pitch=42 at=0.5 length=0.25 vel=80
```

A pattern is the notes a note clip plays (R-CLIP-2); clips that share a pattern are linked
(R-CLIP-3). `at` is beats from the pattern's start; `vel` 1–127. Notes are kept sorted by
(`at`, `pitch`), which is also the merge key a future semantic merge unions by.

**R-ROLL-11.** A note's `chance=` (0 … 1) is written only below 1: the odds it sounds in a pass,
rolled per clip and pass by a hash (`noteSounds`). A pattern's `scale="<key> <scale>"` (a key in
sharps, a scale of `scaleKinds()` — `C major`, `A# minor-pentatonic`) is written only when set:
notes placed or moved in pitch land on it; a scale not known is refused by `validate`.

```
#apattern id=pt_2 name=Lead length=4.0 scale="D dorian"
  #note pitch=62 at=0.0 length=0.5 vel=100
  #note pitch=69 at=0.5 length=0.25 vel=90 chance=0.5
```

**Expression lanes (R-MIDI-3)** follow its notes, sorted by (`lane`, `at`):

```
  #expr lane=bend at=0.0 value=0
  #expr lane=bend at=2.0 value=1
  #expr lane=cc1 at=1.0 value=0.5
```

`lane` is `bend` (the pitch wheel, −1 … +1) or `cc0` … `cc127` (a controller, 0 … 1; `cc1` the mod wheel; `mod` is
read as `cc1`). A lane is linear from point to point and held before its first and after its last; the clip that
plays it returns it to rest (0) where it ends. A lane we do not know, or a value out of its range, is refused.

A suite file whose note clip carries inline `#note` children (audio-format §2.2) is read as a
pattern named after the clip, and written back as one — the only normalisation the parser makes.

## 8a. Automation — `#aauto` / `#point`, and formulas — `#abind` (R-AUTO)

```
#aauto id=au_1 name="Bass · Cutoff" unit=Hz min=20.0 max=20000.0 from=dv_2.filter.cutoff
  #point at=0.0 value=600.0
  #point at=8.0 value=2400.0 shape=smooth
  #point at=12.0 value=900.0 shape=bezier speedIn=-150.0 inflIn=40.0 speedOut=0.0 inflOut=33.333
  #point at=16.0 value=600.0

#abind address=dv_2.filter.cutoff formula="=au_1"
#abind address=ch_3.pan formula="=0.25 * sin(beat * pi)"
```

An automation is a curve that moves nothing until a formula reads it (R-AUTO-4): `at` in beats, `value`
in its unit, `shape` (`linear` when absent | `hold` | `smooth` | `bezier`) governs the segment AFTER the
point. A `bezier` point (R-AUTO-10) is Interstellar's keyframe (its R-ANIM-2, spelled as its `#key`
spells it) with both sides bezier: `speedIn` / `speedOut` are the curve's slope arriving and leaving, in
the automation's unit PER BEAT, and `inflIn` / `inflOut` how far into the segment before / after the
handle reaches, in % (0 < x ≤ 100). The four are written only for a bezier point (always all four), so a
file of linear / hold / smooth points is byte-identical to one written before bezier existed; on another
shape they are ignored. `smooth` is After Effects' Ease on the segment after the point (speed 0,
influence ⅓ at both ends — the smoothstep it always was); a bezier next point's own handle wins on its
side. The curve never leaves `min … max` (a handle's overshoot stops there).
`from` is the address it was made from — a hint, never a link. **A loop** (R-AUTO-12): `loop=<beats>` makes the
curve's first `loop` beats a cycle, repeated from `at=<beats>` (0 when absent) for `length=<beats>` (to the song's
end when absent); outside that span the value is where a cycle ends. The three are written only when it loops
(`loop` > 0), so a file that never loops is byte-identical to one written before loops existed:
`#aauto id=au_2 name="Bass · Gain (pump)" unit=dB min=-120.0 max=12.0 from=ch_3.gain loop=1.0 at=32.0 length=64.0`.
A binding (R-AUTO-1) decides one
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
| `offset` | — | beats into its pattern it starts — a clip cut at its start (a recorded launch's tail, R-LAUNCH-1); written only when set |
| `gain`, `fadeIn`, `fadeOut` | dB, beats, beats | `gain` only |
| `reverse` | `true`: the span played backwards, `out` to `in` (R-EDIT-1; written only when set) | — |
| `warp` · `bpm` · `pitch` | `true`: it lasts its span's own beats at `bpm` (its tempo), stretched to the song's · its tempo · semitones its span is moved, its length kept (R-WARP-1; each written only when set) | — |

**Tempo changes (R-TEMPO-1).** `#atempo id=tp_1 at=16.0 bpm=140.0 ramp=true` — from beat `at` on, `bpm`; with
`ramp`, reached there gliding linearly (in beats) from the tempo before. Before the first, the header's `bpm`. One to a
beat, after beat 0, in order of `at`; written after the markers.

Two audio clips on one lane playing through one strip that overlap — the later beginning inside the earlier and ending
after it — crossfade: each one's fade is played at least as long as the overlap (R-EDIT-1). Nothing of it is stored;
the fades written are the clips' own.

---

**Takes (R-REC-2).** A loop recording's clip carries its passes and its comp under it:

```
#aclip id=ac_5 track=ch_3 lane=ln_2 src="Recordings/Vox 3.wav" at=4.0 in=0.0 out=1.0 length=2.0
  #take src="Recordings/Vox 1.wav"
  #take src="Recordings/Vox 2.wav"
  #take src="Recordings/Vox 3.wav"
  #comp take=1 from=4.5 to=5.0
```

Takes are numbered from 1 in the order they were played; a `#comp` range (song beats) names the take that plays there,
the last take elsewhere; the seams are crossfaded 5 ms. `clip comp` writes them.

## 9a. Markers — `#amarker` (R-ARR-1)

```
#amarker id=mk_1 name=Intro at=0.0
#amarker id=mk_2 name=Chorus at=16.0 length=8.0 note="the big one"
```

Interstellar's `#marker` (`id`, `name`, `at`, `note`) in beats, with `length` added: a marker with a length is a
section; without one it is a point whose section runs to the next marker (or the song's end). `length` and `note`
are written only when set; `name` is required. `transport seek <mk>` / `transport loop <mk>` use them.

## 9a′. The session grid — `#ascene` / `#aslot` (R-LAUNCH-1; docs/discussion.md Round 17)

```
#ascene id=sc_1 name=Verse order=0
#ascene id=sc_2 name=Drop order=1
#aslot id=sl_1 scene=sc_1 strip=ch_2 pattern=pt_1 length=4.0
#aslot id=sl_2 name="Drum loop" scene=sc_2 strip=ch_3 src=loops/beat.wav out=2.0 gain=-3.0
```

A scene is a row (`order` places it, 0 the top); a slot is a cell — what its strip plays when launched in that scene:
a `pattern` (an instrument strip's, shared as a clip's is) or a file (`src`, an audio strip's, with `in`/`out` in
seconds as a clip's). `length` is beats a loop lasts (absent: the pattern's, or the file span's); `name` and `gain`
(dB) are written when set. One slot a cell. A slot is its own node — not a clip off the timeline — so nothing that
reads the arrangement sees it; launching it is live (never rendered) until recorded.

## 9a″. The picture — `#avideo` (R-VIDEO-1; docs/discussion.md Round 18)

```
#avideo id=vd_1 src=picture.mp4 offset=4.0
```

One a song: the video the song is scored to. `src` names its file as a clip's does (in the song's folder by its name,
else absolutely — collect gathers it); `offset` is seconds on the song's clock (the tempo map's) at which its first frame
shows — negative, it began before the song. Written after the session grid. A second `#avideo`, or one naming no file,
is refused. A file the host cannot read leaves the song open and the picture offline.

## 9b. Versions — `#aversion` (R-VER; docs/discussion.md Round 15)

```
#aversion id=vr_1 name=Instrumental
  #vset node=ch_4 mute=true
#aversion id=vr_2 name=Extended base=vr_1 frozen=true
  #vset node=ac_1 at=16.0
  #vunset node=ch_2 phase
  #vdrop node=ac_7
  #vbody node=pt_1
    #note pitch=36 at=0.0 length=0.25 vel=110
  #vorder node=rack:ch_2 children=dv_1,dv_9
  #aclip id=ac_12 track=ch_2 lane=ln_2 pattern=pt_1 at=0.0 length=8.0
  #vsnapshot
    …the whole song as this version resolved when it was frozen, its header and nodes indented four
```

Everything above this section is **main**, the song itself. A version is *its base resolved, with its deltas on
top*; `base` names an earlier version, absent = main. The nodes a version can change are the song's: the header
(`project`), every node with an `id`, a rack (`rack:<track>`) and a formula (`bind:<address>`).

| delta | means |
|---|---|
| `#vset node=<n> k=v …` | these fields of the base's node are overridden; the rest still inherit |
| `#vunset node=<n> k …` | these fields the base's node has are absent here (back to their defaults) |
| `#vdrop node=<n>` | the base's node is not in this version (its child nodes with it) |
| `#vbody node=<n>` + lines | the node's id-less lines — a pattern's notes and expression, an automation's points, a clip's takes and comp — replaced as one |
| `#vorder node=<n> children=a,b` | which child nodes (a rack's devices) and in what order |
| a node, indented two | a node only this version has (its own lines indented four) |
| `#vsnapshot` + lines | frozen (`frozen=true`): the resolution kept, read instead of the base |

A delta naming a node its base's resolution does not have is **dangling** — ignored, counted, kept until `version
rebase` prunes it. Deltas are written in the order the base's nodes come, then the version's own nodes; the nested
lines are canonical text indented. A version's deltas are worked out from the song it now is (R-VER), so the same
song is always written the same way. An older build reads `#aversion` as an unknown node — kept verbatim, the song
opening as main.

## 10. Canonical serialization

Header keys in §1's order, then unknown header keys in read order. Nodes grouped by type in this
document's order (`#aport`, `#amixer`, `#atrack`, `#asend`, `#arack`, `#alane`, `#apattern`,
`#aclip`, `#aauto`, `#abind`, `#amarker`, `#atempo`, `#ascene`, `#aslot`, then unknown nodes in read order, then `#aversion` in creation order — §9b), each type in creation order; children indented two
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
| a route, send or key to a strip that is not LATER in processing order | R-MIX-4 (amended 2026-10-09) — and it names both ends |
| a lane that is the track of no strip or of a non-instrument; a clip on a track that does not play through it | R-LANE-3 — named |
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
marker add <name> --at b [--length b] [--note t] | marker rename <mk> <name> | marker move <mk> [--at b] [--length b] | marker delete <mk>
time insert --at b --length b | time delete --from b --to b      ; the whole song, ONE edit (R-ARR-2)
lane group <ln> … [--name n] | lane ungroup <lg> | lane move <lg> --to i | clips move "<ac> …" --by b   ; R-ARR-3
port add <name> [--dir in|out] [--channels 1|2] | strip arm <ch> [--input <port>] | transport record | transport punch <a> <b>|off   ; R-REC-1
clip comp <ac> --take n [--from b --to b]   ; R-REC-2
version new <name> [--base vr] [--open] | version open <vr|main> | version list | version diff <vr>   ; R-VER (§9b)
version freeze|thaw <vr> | version rebase <vr> [--dry-run] | version delete <vr> | revert <address|node>
project files [--missing] | project collect [--dry-run] [--save] | project relink <file> --to <new> | project relink --folder <dir>   ; R-COLLECT-1
clip relink <ac> <file> [--take n]
scene add [name] [--at i] | scene delete <sc> | scene move <sc> --to i | slot add <sc> <strip> (--pattern pt | --clip ac | --src f) [--length b] | slot delete <sl>   ; R-LAUNCH-1
transport play | transport stop | transport seek <b|mk> | transport loop <a> <b> | transport loop <mk> | transport loop off
render --out <f.wav> [--from b] [--to b] [--stems ch,…] [--ports] [--bits 24|32f] [--version vr|main]
matrix print [--json] | audit | state print [--json] [--stable] | registry [--json] | api [--json|--md]
settings set <key>=<value> … | settings print | devices list | browse <folder>
```

## Autosave — `<song>.slp.autosave` (R-SAFE-1)

While a song has unsaved edits, Solaris writes it beside itself as `<song>.slp.autosave`: the same `.slp` text a save
would write, replaced atomically. A save or a close deletes it; one that outlives its song's last save is offered for
recovery when the song is opened (`project recover`).

---

## Presets — `<folder>/<type>/<name>.slpreset` (R-PRESET-1)

Not part of a song: a device's set values under a name, in the user's presets folder (`SOLARIS_PRESETS`, else
`$XDG_DATA_HOME/solaris/presets`) or the factory's shipped with the app (`apps/solaris/presets`). Lines of `key = value`;
`;` begins a comment.

```
arstro-preset = 1
type = synth
name = Warm Pad
; two detuned saws an octave apart
osc1.wave = saw
filter.cutoff = 1800.0
amp.release = 1800.0
```

`type` is the registry type it is for; every other key is a parameter of that type, its value the text the `.slp`
stores (a choice by name, a number canonical); a sampler's adds `sample = <absolute path>`. Loaded, a device takes
these values and the registry's default for every other.

## Templates — `<folder>/<name>.slp` (R-PRESET-2)

A template is a song file like any other, kept in the user's templates folder (`SOLARIS_TEMPLATES`, else
`$XDG_DATA_HOME/solaris/templates`) or the factory's shipped with the app (`apps/solaris/templates`). `template save`
writes the open song with every file it names made absolute; `project new --template` reads it back and gives it the
new song's id and name.

