# Interstellar — the `.isp` project format and the parameter address space

Normative for **R-FMT**, **R-PARAM** and the text grammar the CLI, the socket, the journal and the
`--script` files all share. If this document and the code disagree, one of them is a defect with an
id — not untidiness (D-1's lesson).

Contents: [1. Shape](#1-the-shape-of-the-file) · [2. Node types](#2-node-types) ·
[3. Canonical serialization](#3-canonical-serialization) · [4. Parameter addresses](#4-parameter-addresses)
· [5. The registry](#5-the-parameter-registry) · [6. Time](#6-time) ·
[7. Validation](#7-validation-what-is-rejected) · [8. The command grammar](#8-the-command-grammar)
· [9. A worked example](#9-a-worked-example)

---

## 1. The shape of the file

One fact per line. A `#type ...` header opens a node; indented `key = value` lines are its fields;
children reference parents by id. Nebula's rules apply
([shared-core.md §2](../../../docs/shared-core.md#2-the-text-project-format)): stable ids, order as
data, references not copies, canonical formatting, unknown keys preserved.

```
arstro-project = 1
app            = interstellar
id             = prj_mv01
name           = "Japan MV — cut A"
fps            = 24
width          = 3840
height         = 2160
par            = 1.0                  ; pixel aspect ratio
colorspace     = rec709
duration       = 184.500              ; seconds; derived, written for readers that want it
```

Every node header carries `id=` (stable, never reused) and `name=` (the **bind name**, R-PARAM-2).
The id is the merge anchor; the name is what an expression spells.

---

## 2. Node types

Nine, and no more in v1. There is deliberately **no `grade`, no `keyframe` and no `lut` node** —
colour is the rack's (R-COSMO-2) and animation is `autoclip` + `autolink` (R-AUTO-1).

### 2.1 `#embed` — the rack, and the audio bed

```
#embed id=emb_rack  name=rack   target=cosmo:prj_look77   path="japan18.cmp"
  as          = rack
  branch      = main
  writeBranch = main            ; where Interstellar's colour edits commit (R-COSMO-5)
#embed id=emb_song  name=song   target=solaris:prj_song50  path="song50.slp"
  as     = audio
  branch = main@8f2c1ab         ; pinned → read-only (R-VCS-6)
  track  = trk_a0
  offset = 0.000
```

| field | type | notes |
|---|---|---|
| `as` | enum `rack \| audio` | exactly one `as=rack` per project (R-COSMO-1) |
| `target` | `<app>:<projectId>` | resolved through the project registry / pool |
| `path` | string | a hint for a human and for a relink; the id is the identity |
| `branch` | `<name>` or `<name>@<commit>` | `@commit` = pinned = read-only |
| `writeBranch` | `<name>` | `as=rack` only; default = `branch`; illegal on a pin |
| `track`, `offset` | id, seconds | `as=audio` placement |

**A rack node is referenced by clips as `rack:<cosmoNodeId>`**, with the Cosmo node's own bind name
carried in the model for display and for expressions. Interstellar stores no copy of the Cosmo
tree — it reads it from the embedded service (R-COSMO-4).

### 2.2 `#track`

```
#track id=trk_v0 name=v0 kind=video order=0  mute=false lock=false
  opacity = 1.0
  blend   = normal
#track id=trk_a0 name=a0 kind=audio order=10 mute=false gain=0.0
```

`order` is z-order for video (higher composites over lower) and lane order for audio. Track-level
`opacity`/`blend` compose onto the track's clips (R-COMP-4).

### 2.3 `#clip`

```
#clip id=clp_a name=clp_a track=trk_v0 order=0
  src   = rack:cn_41            ; a node in the embedded Cosmo project (R-COSMO-2)
  at    = 0.000                 ; timeline position of the clip's first frame
  in    = 12.400                ; source in-point
  out   = 16.600                ; source out-point (exclusive)
  speed = 1.0
  fit   = contain               ; contain | cover | stretch | none  (R-COMP-5)
  opacity = 1.0
  blend   = normal
  geom.x = 0  geom.y = 0  geom.scale = 1.0  geom.rotation = 0
  geom.anchor.x = 0.5  geom.anchor.y = 0.5
  geom.crop.x = 0  geom.crop.y = 0  geom.crop.w = 1  geom.crop.h = 1
```

`out - in` divided by `speed` gives the clip's timeline length; `at` plus that length gives its end.
**No colour field of any name is accepted here** (R-CUT-2) — a `grade`, `curve`, `lut`, `exposure`
or `EditParams` key is a validation *error*, not an ignored key, because ignoring it would silently
discard an edit.

### 2.4 `#transition`

```
#transition id=tr_ab name=tr_ab track=trk_v0 between=clp_a,clp_b
  kind   = dissolve      ; dissolve | dip
  dur    = 0.500
  easing = linear        ; a dissolve is a LINEAR alpha ramp — see design.md §6
  color  = #000000       ; kind=dip only
```

### 2.5 `#autoclip` — the reusable shape (R-AUTO-1)

```
#autoclip id=ac_push name=ac_push  dur=2.000  interp=bezier
  0.000 = 0.0  ease=easeInOut
  0.750 = 0.9  ease=easeOutCubic
  2.000 = 1.0  ease=linear
```

Breakpoints are `<localTime> = <value>` with an optional easing **to the next** point. `dur` is the
shape's natural length; a link may time-scale it. Values are in the shape's own unit space (0..1 by
convention, not by rule) — the link maps them (R-AUTO-2). `interp = linear | bezier | hold | step`.

### 2.6 `#autolink` — applying a shape to an address (R-AUTO-2)

```
#autolink id=al_1 name=al_1  clip=ac_push  target=gr1.basic.exposure
  at   = 4.000            ; timeline seconds, or clip-local when scope= is set
  dur  = 2.000            ; time-scale of the shape; default = the shape's own dur
  from = 0.0              ; value mapping: shape 0 → 0.0 EV
  to   = 0.8              ;                shape 1 → 0.8 EV
  mode = absolute         ; absolute | add | multiply
  fadeIn  = 0             ; frames; ramps from the static value (R-AUTO-5)
  fadeOut = 0
#autolink id=al_2 name=al_2  clip=ac_push  target=clp_a.geom.scale
  at=4.000 dur=2.000 from=1.0 to=1.08 mode=absolute scope=clp_a
```

Those two links are the requirement *"multi param can use the same automation"*: one shape,
two addresses, two value ranges, one set of breakpoints to move.

`scope=<clipId>` puts `at` in the clip's local time so the link travels with the cut (R-AUTO-7).
`scale`/`offset` may be given instead of `from`/`to` for a pure affine mapping.

### 2.7 `#bind` — a parameter driven by a calculation (R-BIND-1)

```
#bind id=bn_1 target=gr1.opacity            expr="clamp(gr1.basic.exposure / 2 + 0.5, 0, 1)"
#bind id=bn_2 target=clp_b.geom.scale       expr="1 + ac_push.value * 0.08"
#bind id=bn_3 target=gr2.basic.exposure     expr="gr1.basic.exposure - 0.4"
```

The expression is stored as text and compiled once (Gene, R-BIND-2). `expr` is the only field; the
dependency list is derived, never stored — a stored dependency list is a second copy of a fact the
expression already carries (R-G-3).

### 2.8 `#marker`

```
#marker id=mk_1 name=chorus  at=48.000  color=#4F7EF7  note="chorus in"
```

### 2.9a `#rackobj` — Interstellar's own data about a rack node (R-RACK-6)

```
#rackobj id=ro_1 name=gr1 node=cn_1 opacity=1.0
```

| field | meaning |
|---|---|
| `node` | the Cosmo node id this names |
| `name` | the **bind name** — an expression spells this. Cosmo's own name may contain a space or a dot and is therefore not a legal address (R-PARAM-2) |
| `opacity` | the **grade weight**: how strongly this node's own parameter offsets apply to its descendants — a continuous `bypass`, and what `bind gr1.opacity = …` addresses |

It is Interstellar's data *about* a rack node, not colour data belonging to it, which is why it
lives here and not in the `.cmp`.

### 2.9 `#settings` — project-scoped preferences that belong to the project, not the machine

```
#settings
  proxyEdge      = 1280        ; playback proxy long edge
  cpuPercent     = 50          ; the ONE budget (R-NFR-3)
  cacheBytes     = 2147483648
  lintOnRender   = true
```

Machine-scoped preferences (window size, last directory, GPU opt-in) live in the config dir, not in
the project — a project that moves between machines must not carry one machine's thread count.

---

## 3. Canonical serialization

Because a commit is content-hashed and a diff must be empty when nothing changed (R-FMT-4):

1. **Node order** — by node type in the order of §2, then by id, lexicographically. `order=` fields
   carry user-visible ordering, so file order never has to.
2. **Field order** — the order declared in this document, then unknown keys, in the order they were
   read.
3. **Numbers** — the shortest decimal that reads back bit-identically; times to 3 decimal places;
   an integral value keeps one `.0` where the field is a float. (Gene's `num()` already implements
   exactly this rule for the same reason: the file is read by people and must still be exact.)
4. **Strings** — double-quoted with `\"`, `\\`, `\n` escapes; quoted only when the value contains a
   space, a quote, a `;` or a leading/trailing space.
5. **Comments** — `;` to end of line, **preserved** against the node they follow.
6. **Unknown keys** — preserved verbatim, in place, so a newer file survives an older build.
7. **Parse → serialize is a fixed point.** The round-trip test is P1's gate and it is the cheapest
   test in the project.

---

## 4. Parameter addresses

The grammar (R-PARAM-1):

```
address   := object [ "." filter ] "." param [ "." component ]
object    := a bind name — a rack group, a rack source, a track, a clip, an autoclip,
             a mask, or the reserved object `project`
filter    := basic | detail | mixer | curve | grade | xform | mask.<i>      (rack objects)
           | geom                                                          (clips, tracks)
param     := a leaf name — Cosmo's own EditParamsIO key, or an Interstellar one
component := x | y | w | h | r | g | b | a | <hue> | y@<x>
```

Object-level parameters have **no filter segment** — `gr1.opacity`, `clp_a.speed`, `v0.blend` —
which is why the filter is optional in the grammar rather than a separate rule. That is the exact
shape the user asked for: `gr1.opacity` and `gr1.basic.exposure` in one address space.

### 4.1 Rack objects (a Cosmo group or source)

Filter names are **Cosmo's panels** and leaf names are **Cosmo's own keys** (R-PARAM-4) — the same
spelling a `.cmp`, a `.apf` preset and a `cosmo-cc set` line use. The full list comes from
`EditParamsIO`; the grouping is:

| filter | leaf parameters |
|---|---|
| `basic` | `exposure` `contrast` `highlights` `shadows` `whites` `blacks` `temp` `tint` `vibrance` `saturation` `texture` `clarity` `dehaze` |
| `detail` | `sharpenAmount` `sharpenRadius` `sharpenMasking` `nrLuminance` `nrColor` `grainAmount` `grainSize` `lensDistortion` `lensCA` `lensVignette` |
| `mixer` | `hue.<h>` `sat.<h>` `lum.<h>` (per-hue curve values) · `spread` |
| `curve` | `master` `r` `g` `b` (whole curves) · `master.y@<x>` (a sampled component) · `log` |
| `grade` | `shadows.hue/sat/lum` `midtones.hue/sat/lum` `highlights.hue/sat/lum` `balance` · `remap.src/range/dst/strength` `remap.enable` |
| `xform` | `crop.x/y/w/h` `rotation` `quarterTurns` |
| `mask.<i>` | `feather` `inverted` `type` · geometry (`cx cy rx ry` \| `x0 y0 x1 y1`) · `adjust.<leaf>` for every `LocalAdjust` field |
| *(object level)* | `bypass` `opacity`¹ |

¹ A rack node's `opacity` is Interstellar's, not Cosmo's: it is the weight with which that node's
*own* parameter offsets are applied to its descendants — a continuous version of Cosmo's `bypass`,
and the thing the user's `gr1.opacity` example is asking for. `bypass` stays as the boolean.
See [design.md](design.md) §4.3 for why this is a rack-object parameter and not a clip parameter.

### 4.2 Timeline objects

| object | filter | parameters |
|---|---|---|
| `clip` | *(object level)* | `opacity` `speed` `at` `in` `out` `blend` `fit` `src` |
| `clip` | `geom` | `x` `y` `scale` `rotation` `anchor.x` `anchor.y` `crop.x` `crop.y` `crop.w` `crop.h` |
| `track` | *(object level)* | `opacity` `blend` `mute` `gain` |
| `autoclip` | *(object level)* | `value` (read-only: the shape's output at *t*) · `dur` |
| `project` | *(object level)* | `playhead` `fps` `width` `height` `duration` (read-only) |

`at`, `in`, `out`, `src`, `blend`, `fit`, `mute`, `kind` and every bind name are **not
automatable** — their change is structural rather than continuous (R-AUTO-6), and the registry says
so, so no UI offers a lane that cannot exist.

---

## 5. The parameter registry

One generated table (R-PARAM-3). Each row:

| column | meaning |
|---|---|
| `address pattern` | `<objectType>.<filter>.<param>` with `<i>`/`<h>`/`<x>` placeholders |
| `type` | `float · int · bool · enum · curve · colour · point · ref` |
| `unit` | `ev · % · px · deg · K · frames · s · none` — a unit a UI can label and a script can trust |
| `min` / `max` / `default` | the same numbers the panel's slider uses |
| `automatable` | may be an `autolink` target |
| `bindable` | may be a `#bind` target |
| `readonly` | derived; never a target |
| `source` | `cosmo` or `interstellar` — which service owns the write |

It is generated from the same tables the parser and the codec use, so it cannot describe a parameter
the app does not have and cannot omit one it does. `interstellar-cc api --json` prints it; a test
regenerates and diffs it (R-SVC-10).

**Why generated is not a preference.** Cosmo's `--help` takes its command names from
`commandNames()` and is always right, while the argument hints beside them are hand-maintained and
missing for 8 of 30. With several hundred addresses, the hand-maintained half would be wrong within
a week and an agent reading it would build scripts on parameters that do not exist.

---

## 6. Time

- **`fps` is the project's, and frames are the authority** (R-CUT-5). A time field is serialized as
  decimal seconds rounded to the nearest frame boundary at 3 decimal places and re-derived as a
  frame index on read; the pair `(seconds, fps)` must round-trip to the same integer frame.
- **Clip-local time** is `(t - clip.at) * clip.speed + clip.in`, clamped to `[in, out)`.
- **A source frame index** is `floor(localTime * sourceFps)` — nearest-neighbour sampling, stated
  because R-CUT-6 forbids pretending it is interpolation.
- **Automation time** is timeline seconds, or clip-local seconds under `scope=` (R-AUTO-7).
- **`t` in an expression** is timeline seconds; `frame` is the integer frame index (R-BIND-2).

---

## 7. Validation: what is rejected

An unknown *key* is preserved (R-FMT-4). An unknown or wrong *fact* is rejected, naming it
(R-SVC-6). The list is normative because every entry is a way a project could otherwise be silently
wrong:

| rejected | why |
|---|---|
| a colour field on a `#clip` | R-CUT-2 — ignoring it discards a user's edit |
| two `#embed … as=rack` | R-COSMO-1 — two colour authorities |
| a colour command against a **pinned** rack | R-COSMO-5 — a pin that yields is not a pin |
| two `#autolink`s overlapping on one address | R-AUTO-4 — two producers for one value |
| a `#bind` on an address that also has a link | R-BIND-5 — same |
| a `#bind` cycle | R-BIND-4 — printed with both ends of the cycle |
| an address that does not resolve | R-PARAM-5 — with the nearest candidates suggested |
| a bind name that is duplicated, reserved, or malformed | R-PARAM-2 |
| a `#clip` whose `in >= out`, or `speed = 0` | a clip with no frames is not a clip |
| a `#transition` whose `dur` exceeds either neighbour | it would consume a clip |
| a non-finite number anywhere | Cosmo's D-36: **repaired** to the default, with a count reported, never refused — one stray `nan` must not make a project unopenable |

The two behaviours in that last row are deliberately different: a *structural* error refuses, a
*numeric* corruption repairs and reports.

---

## 8. The command grammar

Flat and line-oriented, because every consumer is a shell, a file or a socket. One parser, one
formatter, generated from the `Command` struct (R-SVC-2). This is the grammar `interstellar-cc`
accepts as arguments, `run --script` reads from a file, and `attach` sends over the socket.

```
project new <path.isp> --fps 24 --res 3840x2160 [--colorspace rec709]
project open <path.isp> [--branch <name>]
project save [<path.isp>]
project close

rack import <path.cmp> [--branch main] [--write-branch main]   ; create the as=rack embed
rack new    [--name rack]                                      ; an empty Cosmo project
rack pin    <commit> | rack unpin
rack add    <media...>            ; add sources to the rack (video or still)
rack group  new "<name>" | rack group ungroup <node> | rack rename <node> "<name>"
rack duplicate <node>             ; a source VARIANT (R-RACK-3)
rack frame  <node> --at <t>       ; which frame is this video source's grading reference
rack select <node>

set <address>=<value> [<address>=<value> ...]   ; routes to whichever service owns it (R-PARAM-3)
get <address>
eval <address> --at <t> [--explain]

track add --kind video|audio [--name v1] [--order n]
track set <track> <address>=<value>...
clip add --track <track> --src rack:<node> --in <t> --out <t> --at <t> [--name clp_a]
clip trim <clip> --in <t> | --out <t>       ; clip split <clip> --at <t>
clip move <clip> --at <t> [--track <track>] ; clip delete <clip> [--ripple]
clip roll <clipA>,<clipB> --by <dt>         ; clip slip <clip> --by <dt>
transition add --between <clipA>,<clipB> --kind dissolve --dur 0.5

auto new  <name> --dur <t> --points 0=0,1=1 [--ease easeInOut] [--interp bezier]
          ; --points times are NORMALISED (fractions of --dur); the FILE stores seconds (R-AUTO-1a)
auto point <autoclip> --at <t> --value <v> [--ease <e>]   ; auto point delete <autoclip> --at <t>
auto link <autoclip> -> <address> --at <t> [--dur <t>] [--from <v> --to <v>]
          [--mode absolute|add|multiply] [--scope <clip>] [--fade-in <frames>]
auto unlink <autolink>      ; auto lanes [<object>]      ; auto flatten <autolink>
bind <address> = <expression>     ; bind list      ; bind delete <address>

playhead <t> | playhead +<dt> | playhead next-cut | playhead prev-cut
play | pause | stop
render --out <path> [--range a:b] [--branch <name>] [--format prores|h264|png-seq] [--lint]
export-still --out <path.png> [--at <t>]
branch <name> --base main | rebase --status | merge <a.isp> <b.isp> --into <c.isp> --policy concatenate
settings set proxyEdge=1280 cpuPercent=50
state print [--json] [--stable]      ; ui dump [--json] [--root cut|grade|mix]
wait <condition> --timeout 120s      ; expect <event-prefix>      ; api [--json]      ; quit
```

Note what needs **no** command of its own: every scalar, curve, mixer band, grade wheel, mask field,
geometry value and opacity is reachable through `set <address>=<value>`, because the registry names
them all. That is the same economy Cosmo's `Command.h` header argues for, extended by the address
space.

---

## 9. A worked example

The user's scenario, complete: two groups graded in Cosmo, a two-shot cut, one automation shape
driving two parameters, and one binding.

```
arstro-project = 1
app   = interstellar
id    = prj_mv01
name  = "Japan MV — cut A"
fps   = 24   width = 3840   height = 2160   par = 1.0   colorspace = rec709

#embed id=emb_rack name=rack target=cosmo:prj_look77 path="japan18.cmp"
  as = rack   branch = main   writeBranch = main
#embed id=emb_song name=song target=solaris:prj_song50 path="song50.slp"
  as = audio  branch = main   track = trk_a0  offset = 0.000

#track id=trk_v0 name=v0 kind=video order=0  opacity=1.0 blend=normal
#track id=trk_v1 name=v1 kind=video order=1  opacity=1.0 blend=normal
#track id=trk_a0 name=a0 kind=audio order=10 gain=0.0

; ── the cut. Both clips reference RACK NODES; neither carries any colour. ──
#clip id=clp_a name=clp_a track=trk_v0 order=0
  src=rack:cn_41  at=0.000  in=12.400 out=16.600 speed=1.0 fit=contain
  opacity=1.0 blend=normal
  geom.scale=1.0 geom.crop.w=1 geom.crop.h=1
#clip id=clp_b name=clp_b track=trk_v0 order=1
  src=rack:cn_58  at=4.200  in=88.000 out=91.100 speed=1.0 fit=contain
  opacity=1.0 blend=normal
#transition id=tr_ab name=tr_ab track=trk_v0 between=clp_a,clp_b kind=dissolve dur=0.500 easing=linear

; ── one shape … ──
#autoclip id=ac_push name=ac_push dur=2.000 interp=bezier
  0.000 = 0.0  ease=easeInOut
  2.000 = 1.0  ease=linear

; ── … driving two different parameters, in two different unit ranges (R-AUTO-2) ──
#autolink id=al_ex name=al_ex clip=ac_push target=gr1.basic.exposure
  at=2.000 dur=2.000 from=0.0 to=0.8 mode=absolute
#autolink id=al_sc name=al_sc clip=ac_push target=clp_a.geom.scale
  at=2.000 dur=2.000 from=1.0 to=1.08 mode=absolute scope=clp_a

; ── and one binding: the group's own weight follows its own exposure (the user's example) ──
#bind id=bn_op target=gr1.opacity expr="clamp(gr1.basic.exposure / 2 + 0.5, 0, 1)"

#marker id=mk_1 name=chorus at=48.000 color=#4F7EF7 note="chorus in"

#settings
  proxyEdge = 1280   cpuPercent = 50   cacheBytes = 2147483648   lintOnRender = true
```

`gr1` and the rack nodes `cn_41` / `cn_58` are **not in this file**. They are in `japan18.cmp`, the
Cosmo project the rack embed names, and they are edited — from Cosmo *or* from Interstellar —
through the one Cosmo service that owns them (R-COSMO-3). That absence is the whole design.
