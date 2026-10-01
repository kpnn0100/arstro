# Interstellar — the `.isp` project format

Normative for `R-FMT`, `R-VER`, `R-TL` and the command grammar. One fact per line; a `#type id=…`
header opens a node; canonical formatting so "no change" is literally no diff; unknown keys
preserved verbatim so a newer file opens in an older build without loss.

**Colour is not in this file.** A clip names a rack node and the colour is that node's, in the
`.cmp` the rack embed points at (R-RACK-1). The one colour-shaped thing here is a version's
*override*, and an override is a delta, not a value (§3.3).

---

## 1. Header

```
arstro-project = 1
app        = interstellar
id         = prj_mv01
name       = "Japan MV"
fps        = 24
width      = 3840
height     = 2160
par        = 1.0
colorspace = rec709
timebase   = seconds        ; see ../../../docs/audio-format.md §1
sampleRate = 48000
masterGain = 0.0
current    = tl_1           ; which timeline the EDITOR last had open — presentation, not render
```

`current` is deliberately *not* what a render uses: a delivery that depended on which tab was open
is a delivery nobody can reproduce (R-RENDER-1).

---

## 2. The rack — one per project

```
#rack id=rk_1 path="japan18.cmp" branch=main writeBranch=main
```

The hosted Cosmo project. Exactly one per project: two would be two colour authorities.

```
#rackobj id=ro_1 name=gr1 node=cn_1 kind=group weight=1.0
#rackobj id=ro_2 name=s_day01 node=cn_41 kind=source weight=1.0
  media="footage/DSC01.MOV" frame=4.250
```

Interstellar's own data *about* a Cosmo node — never colour:

| field | why it is here and not in the `.cmp` |
|---|---|
| `name` | the **bind name**. Cosmo names a node after its file or after what the user typed, so `"Tokyo Night"` and `"DSC01.MOV"` are both normal and neither is addressable |
| `media` | the source file. The timeline decodes frame N from it, and Interstellar is the party that added it |
| `frame` | which frame Cosmo grades (R-RACK-3) |
| `weight` | the **grade weight**, a continuous `bypass`. Cosmo has no concept of it |

---

## 3. Timelines — a timeline is a version

### 3.1 The node

```
#timeline id=tl_1 name=main                                  order=0
#timeline id=tl_2 name=social30 base=tl_1 colour=follow      order=1
#timeline id=tl_3 name=delivery base=tl_1 colour=pin@8f2c1ab cut=frozen order=2
```

| field | meaning |
|---|---|
| `base` | the timeline this one is a version of. Absent = a root |
| `colour` | `follow` (default) or `pin@<commit>` — freeze the base's grade for a delivery |
| `cut` | `follow` (default) or `frozen` — stop inheriting the base's arrangement |

### 3.2 One inheritance model, for both halves

**A version is "my base, resolved live, with my deltas on top."** Colour and arrangement behave
*identically*, which is the whole reason the model is learnable:

```
resolve(timeline) =
    resolve(base)                    ← recursively, unless this one is pinned/frozen
      minus every #tldrop
      with  every #tlset applied
      plus  every node declared here outright
```

- **inherit live** — a regrade or a re-cut on the base reaches every open version immediately;
- **override to diverge** — a `#tlset` or a `#tlgrade` always wins over the base;
- **pin / freeze to stop** — a delivery stops inheriting, deliberately and visibly;
- **rebase to reconcile or advance** — prune deltas whose target the base deleted, and move a pin
  forward.

> **AMENDED (writing this document, 2026-10-01).** `R-VER-2` first said *colour inherits live and
> arrangement inherits by delta, replayed on demand*, with a rationale that a cut "must not change
> under the editor's hands". Writing the resolution rules showed that to be two models where one
> does: a delta resolved live against its base **is** inheritance, and calling the same mechanism by
> two names bought nothing but a second set of rules to learn. The editorial worry it was protecting
> against is real and is answered by `cut=frozen` — which is now the same lever as `colour=pin`,
> rather than a different one. R-VER-2 is amended to match; this is the conflict rule working at the
> moment it is supposed to (`arstro.rule` §2).

### 3.3 The deltas

```
#tldrop   timeline=tl_2 node=clp_3                      ; the base's clip is not in this version
#tlset    timeline=tl_2 node=clp_5 out=4.200 at=0.000   ; field overrides on an inherited node
#tlgrade  timeline=tl_2 node=ro_2 exposure=0.4          ; a COLOUR override, per rack node
```

- `#tlset` carries only the fields it overrides — the rest keep inheriting, so a base edit to an
  untouched field still arrives.
- `#tlgrade` is the one colour-shaped node in the file, and it is **a delta applied to the rack's
  value at render time**, never a stored parameter set. The rack remains the authority; this says
  *"in this version, +0.4 on top of whatever the rack says"*.
- A delta whose `node` no longer exists in the base is **dangling** — reported by `rebase`, never
  silently dropped.

### 3.4 Why versions are per-timeline, not per-project

The first specification put Nebula branching around the whole project, which put the version
boundary around the **colour authority** — so "rebase to get the base's latest colour" could not be
expressed at all, because the base's colour was in a different repository state. One project, one
rack, many cuts of it: the thing that varies between versions is the *arrangement and the
deviations*, which is exactly what a timeline is.

---

## 4. Arrangement nodes

```
#track id=trk_1 name=v0 timeline=tl_1 kind=video order=0 opacity=1.0 blend=normal
#clip  id=clp_1 name=shotA track=trk_1 order=0 src=ro_2
  at=0.000 in=12.400 out=16.600 speed=1.0 fit=contain opacity=1.0 blend=normal
  geom.x=0.0 geom.y=0.0 geom.scale=1.0 geom.rotation=0.0
  geom.anchor.x=0.5 geom.anchor.y=0.5
  geom.crop.x=0.0 geom.crop.y=0.0 geom.crop.w=1.0 geom.crop.h=1.0
#transition id=tr_1 name=tr_ab track=trk_1 between=clp_1,clp_2 kind=dissolve dur=0.500
#marker id=mk_1 name=chorus timeline=tl_1 at=48.000 note="chorus in"
```

`src` names a **`#rackobj`**, not a file: the colour and the pixels arrive together, from one place.

**A colour field on a `#clip` is a validation ERROR**, not an ignored key — ignoring it would
silently discard a user's edit. `grade`, `curve`, `mixer`, `lut`, `exposure`, `temp`, … all refused,
naming the key.

Audio nodes (`#atrack`, `#aclip`, `#note`, `#arack`, `#aeffect`, `#aauto`, `#asend`) are the suite
schema: [`../../../docs/audio-format.md`](../../../docs/audio-format.md). They carry `timeline=` like any
other arrangement node.

---

## 5. Effects

```
#fx id=fx_1 node=ro_2 type=denoise radius=2 strength=0.6
#fx id=fx_2 node=ro_2 type=blend   radius=1 shutter=180
#fx id=fx_3 clip=clp_1 type=freeze at=2.000
```

A temporal effect attaches to a **rack node** (it belongs to the footage, like the grade) or to a
**clip** (`freeze` is editorial). `radius` is the temporal footprint the engine unions to size its
window (R-VOL-4) — it is declared data, not something the engine infers, because residency has to
be computable before the first frame is read.

---

## 6. Canonical serialization

Node order by type, then by id. Field order as declared here, then unknown keys in read order.
Numbers: the shortest decimal that reads back bit-identically, times to three places. Strings quoted
only when they contain a space, a quote, a `;` or an `=`. Comments (`;`) preserved against the node
they follow. **Parse → serialize is a byte-exact fixed point**, which is the cheapest test in the
project and the one that catches most format regressions.

---

## 7. Validation: what is refused

| refused | why |
|---|---|
| a colour field on a `#clip` | R-TL-2 — ignoring it discards a user's edit |
| two `#rack` nodes | two colour authorities |
| a colour command against a pinned version | a pin that yields is not a pin |
| a `#timeline` whose `base` chain contains a cycle | named, both ends |
| a `#clip` with `in >= out`, or `speed = 0` | a clip with no frames is not a clip |
| a `src` naming no `#rackobj` | with the rack's bind names listed |
| a `#transition` longer than either neighbour | it would consume a clip |
| a non-finite number | **repaired** to the field default and counted, never refused — one stray `nan` must not make a project unopenable |

The last row differs from the rest on purpose: a *structural* error refuses, a *numeric* corruption
repairs and reports.

---

## 8. The command grammar

One parser, shared by the CLI, a script file, the GUI's text dispatch and the journal. **The
authoritative list is generated**: [`API.md`](API.md) (and [`api.json`](api.json)) are printed by
`interstellar-cc api` from the same table the parser reads (`core/service/Command.cpp`), committed,
and drift-tested — so this section is a summary and the generated document wins.

```
project new <path.isp> [--fps 24] [--res WxH]   | project open <p> | project save [p] | project close
rack import <path.cmp> | rack add <media…> | rack group new <name> --nodes a,b | rack duplicate <node>
rack frame <node> --at <t> | rack rename <node> <name> | rack select <node>
set <address>=<value> …        ; routed by owner: a rack address writes THROUGH to Cosmo on a root
                               ; timeline and becomes the version's #tlgrade on a derived one
get <address> | eval <address> [--timeline <tl>] [--explain] | revert <address> [--timeline <tl>]

timeline new <name> [--base <tl>] | timeline list | timeline open <tl> | timeline delete <tl>
timeline pin <tl> [--commit <c>] | timeline unpin <tl> | timeline freeze <tl> | timeline thaw <tl>
timeline rebase <tl> [--dry-run]            ; reconcile dangling deltas, advance a pin
timeline diff <tl>                          ; what this version changes against its base

track add --kind video|audio [--name v0]
clip add --track <t> --src <rackobj> --in <t> --out <t> --at <t> [--name n]
clip trim <c> [--in <t>] [--out <t>] | clip split <c> --at <t> | clip move <c> --at <t> [--track <t>]
clip delete <c> [--ripple] | clip roll <c> --at <t> | clip slip <c> --by <dt> | clip speed <c> <s>
clip select [c] | transition add --between a,b [--kind dissolve|dip] [--dur 0.5] | marker add <n> --at <t>
fx add --node <ro>|--clip <c> --type denoise|blend|freeze [--radius 2] [--strength 0.6] | fx delete <fx>

audio track add [--name bed] | audio clip add --track <t> --src <file> --at <t> --out <t> [--gain -3]
playhead <t>|+<dt>|-<dt>|next-cut|prev-cut | play | pause
render --timeline <tl> --out <path> [--range a:b] [--format h264|prores|png-seq] | render cancel <job>
export-still --timeline <tl> --out <p.png> [--at <t>]
state print [--json] [--stable] | api [--json] [--md] | lint | wait <cond> [--timeout 120s] | quit
```

> **AMENDED (building the service, 2026-10-01).** Added what the UI and the version model needed
> and the first draft lacked: `rack select`, `clip select`, `clip speed`, `revert`, `timeline delete`,
> `marker add`, `fx delete`, `render cancel`. **Removed** `timeline diff --against` and `eval --at`:
> neither is implemented in v1 (values do not vary over time yet), and R-SVC-3 forbids accepting a
> flag that does nothing. `clip trim --in/--out` are SOURCE points — the clip's own `in`/`out`
> fields, the spelling `set <clip>.in=` uses (the service converts each to the edge's timeline time
> for the model's trim; a head trim keeps the remaining frames where they were). `audio clip add` requires `--out`: the core reads no
> audio headers. A version's colour override is **scalar**: the format stores a number added to the
> rack's value; a curve or wheel override on a version is refused, pointing at the base or at
> `rack duplicate` (R-RACK-5).
