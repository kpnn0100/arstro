# Interstellar API

> **Generated** by `interstellar-cc api --md` from the tables the parser, the event stream, the model codec and the address router run on. Do not edit: `ctest -R interstellar_api_current` fails when this file and the code disagree (R-API-1). Machine-readable twin: [`api.json`](api.json).

**How an agent drives Interstellar** (R-API-2): *discover* with `api --json`; *reach* with one command line; *observe* through `state print --json --stable` or the `[evt]` stream; *assert* with `eval`.

```
interstellar-cc --script cut.txt          # one command per line, `#` comments
interstellar-cc project open mv.isp : set s_day01.basic.exposure=0.35 : project save
```

## Commands

| usage | does | req |
|---|---|---|
| `project new <path.isp> [--fps <n>] [--res <WxH>]` | Create a project and its rack (an empty .cmp beside it), and open it. | R-SCOPE-2 |
| `project open <path.isp>` | Open a project: its timelines, and the hosted Cosmo project its #rack names. | R-SCOPE-2 |
| `project save [path.isp]` | Save the .isp and the rack's .cmp. A path saves the .isp there. | R-RACK-2 |
| `project close` | Close the project and return Home. | R-UI-1 |
| `rack import <path.cmp>` | Point the rack at an existing Cosmo project — its groups and grades are the rack. | R-RACK-1 |
| `rack add <media…> [--group <node>]` | Add photos or videos to the rack. A video is graded on a reference frame (`clip.mp4#t=2.0` picks it). | R-RACK-3 |
| `rack group new [name] [--nodes <a,b,…>]` | Group rack nodes — the named ones, else the selection; a group's grade stacks onto every descendant. No name: one is made up, as Cosmo does. | R-RACK-4 |
| `rack duplicate <node> [--name <bind>]` | Duplicate a rack node as a variant: same media, its own grade. | R-RACK-5 |
| `rack frame <node> [--at <t>]` | Choose which frame of a video source Cosmo grades. Changes no parameter. | R-RACK-3 |
| `rack rename <node> <bind>` | Change a rack node's bind name (what an address spells). | R-RACK-6 |
| `rack select <node> [--add] [--range]` | Make a rack node the Grade target and the selection. --add toggles it into the selection (Ctrl-click); --range selects from the last clicked node to it (Shift-click). | R-RACK-8 |
| `rack remove <node>` | Take a source out of the rack (Cosmo's delete). Refused while a clip uses it. | R-RACK-8 |
| `set <address>=<value> …` | Write addresses. A rack address writes THROUGH to Cosmo on a root timeline and becomes this version's override on a derived one. | R-RACK-2 |
| `get <address>` | Print an address's stored value. | R-API-2 |
| `eval <address> [--timeline <tl>] [--explain]` | Print an address's RESOLVED value in a timeline; --explain names every layer. | R-API-2 |
| `revert <address> [--timeline <tl>]` | Drop this version's override at an address, so it inherits from its base again. | R-VER-2 |
| `timeline new <name> [--base <tl>]` | Create a timeline; with --base it is a version of that one (stores deltas only). | R-VER-1 |
| `timeline list` | List timelines as a version tree. | R-VER-5 |
| `timeline open <tl>` | Make a timeline the editor's current one. | R-UI-4 |
| `timeline pin <tl> [--commit <c>]` | Stop inheriting the base's colour: freeze it at a snapshot of the rack. | R-VER-3 |
| `timeline unpin <tl>` | Inherit the base's colour live again. | R-VER-3 |
| `timeline freeze <tl>` | Stop inheriting the base's arrangement: materialise the resolved cut. | R-VER-3 |
| `timeline thaw <tl>` | Inherit the base's arrangement live again. | R-VER-3 |
| `timeline rebase <tl> [--dry-run]` | Report and prune dangling deltas; advance a pin to the rack's present state. | R-VER-4 |
| `timeline diff <tl>` | What this version changes relative to its base. | R-VER-2 |
| `timeline delete <tl>` | Delete a timeline. Refused while another version is based on it. | R-VER-1 |
| `track add [--kind <video\|audio>] [--name <n>]` | Add a track to the current timeline. | R-TL-1 |
| `clip add [--track <trk>] [--src <rackobj>] [--in <t>] [--out <t>] [--at <t>] [--name <n>]` | Place a span of a rack source on a track. | R-TL-1 |
| `clip trim <clip> [--in <t>] [--out <t>]` | Set a clip's source in and/or out point (seconds into the SOURCE, like `<clip>.in`); trimming the head keeps the remaining frames where they were on the timeline. | R-TL-3 |
| `clip split <clip> [--at <t>]` | Cut a clip in two at a timeline time. | R-TL-3 |
| `clip move <clip> [--at <t>] [--track <trk>]` | Move a clip in time and/or to another track. | R-TL-3 |
| `clip delete <clip> [--ripple]` | Remove a clip; --ripple closes the gap. | R-TL-3 |
| `clip roll <clip> [--at <t>]` | Move the cut between a clip and its right neighbour. | R-TL-3 |
| `clip slip <clip> [--by <dt>]` | Shift a clip's source range without moving it on the timeline. | R-TL-3 |
| `clip speed <clip> <speed>` | Set a clip's playback speed. | R-TL-3 |
| `clip select [clip]` | Select a clip (none clears). | R-UI-3 |
| `transition add [--between <a,b>] [--kind <dissolve\|dip>] [--dur <s>]` | Dissolve between two adjacent clips; the outgoing clip is HELD through it. | R-TL-4 |
| `marker add <name> [--at <t>] [--note <text>]` | Drop a named marker on the current timeline. | R-TL-1 |
| `fx add [--node <rackobj>] [--clip <clip>] [--type <denoise\|blend\|freeze>] [--radius <n>] [--strength <0..1>] [--shutter <deg>] [--at <t>]` | Attach a temporal effect to a rack node (footage) or a clip (editorial). | R-FX-2 |
| `fx delete <fx>` | Remove a temporal effect. | R-FX-2 |
| `audio track add [--name <n>]` | Add an audio track (suite schema `#atrack kind=audio`). | R-AUD-2 |
| `audio clip add [--track <atrk>] [--src <file>] [--at <t>] [--in <t>] [--out <t>] [--gain <dB>] [--fade <s>]` | Place an audio file on an audio track. | R-AUD-2 |
| `undo` | Step back one edit — a grade, an override, a cut, a version change — across the rack and the project alike. | R-EDIT-1 |
| `redo` | Step forward again after an undo. | R-EDIT-1 |
| `grade copy <node>` | Copy a rack node's grade (its own params, masks excluded) to the clipboard. | R-EDIT-2 |
| `grade paste [node…] [--all]` | Paste the copied grade onto rack nodes (or every source with --all). Root timeline only: it writes through to Cosmo. | R-EDIT-2 |
| `rack ungroup <group>` | Dissolve a group; its members keep their own grades. | R-RACK-4 |
| `settings set <key>=<value> …` | Engine settings: cpuPercent (25\|50\|75\|100), threads (0=auto), previewEdge (px), useGpu (0\|1), uiScale (%). Persisted; one CPU budget for the rack and the render path. | R-SET-1 |
| `preset apply <name> [--node <bind>]` | Apply a library preset to a rack source (the Grade target by default). Root timeline only. | R-EDIT-3 |
| `preset save <name> [--node <bind>]` | Save a rack source's grade to the library as <name>.apf. | R-EDIT-3 |
| `preset import <path.apf>` | Copy an .apf (from Cosmo or anywhere) into the library. | R-EDIT-3 |
| `playhead <t>\|+<dt>\|-<dt>\|next-cut\|prev-cut` | Move the playhead; snapped to a frame. | R-TL-5 |
| `play` | Start playback of the current timeline. | R-UI-3 |
| `pause` | Stop playback. | R-UI-3 |
| `render [--timeline <tl>] [--out <path>] [--range <a:b>] [--format <h264\|prores\|png-seq>]` | Queue a render of a NAMED timeline. There is no implicit current timeline. | R-RENDER-1 |
| `render cancel <job>` | Cancel a queued or running render. | R-RENDER-4 |
| `export-still [--timeline <tl>] [--out <p.png>] [--at <t>]` | Write one composited frame of a named timeline. | R-RENDER-5 |
| `state print [--json] [--stable]` | Print the AppModel; --stable omits machine-dependent fields. | R-API-2 |
| `api [--json] [--md]` | Print this document. | R-API-1 |
| `lint` | Report offline media, dangling deltas and refused fields. | R-RACK-7 |
| `wait <rack.loaded\|render.done\|frame.ready> [--timeout <dur>]` | Block (pumping) until a condition holds. | R-API-2 |
| `quit` | End a script or session. | R-API-2 |

## Addresses

`set <address>=<value>` routes by owner. A **cosmo** address writes THROUGH to the hosted Cosmo project when the current timeline is a root, and becomes the current version's override (`#tlgrade`) when it is derived — a scalar override is ADDED to the rack's value, so a later base edit still arrives; a list or wheel override replaces it. A pinned version refuses colour writes, naming the pin. A **clip** write on a derived version becomes a `#tlset`. `revert <address>` drops an override.

| address | owner | kind | unit | range | neutral | meaning |
|---|---|---|---|---|---|---|
| `<bind>.basic.exposure` | cosmo | scalar | EV | -5.0..5.0 | 0.0 | Exposure. |
| `<bind>.basic.contrast` | cosmo | scalar |  | -100.0..100.0 | 0.0 | Contrast. |
| `<bind>.basic.highlights` | cosmo | scalar |  | -100.0..100.0 | 0.0 | Highlights. |
| `<bind>.basic.shadows` | cosmo | scalar |  | -100.0..100.0 | 0.0 | Shadows. |
| `<bind>.basic.whites` | cosmo | scalar |  | -100.0..100.0 | 0.0 | Whites. |
| `<bind>.basic.blacks` | cosmo | scalar |  | -100.0..100.0 | 0.0 | Blacks. |
| `<bind>.basic.temp` | cosmo | scalar | K | 2000.0..19500.0 | 6500.0 | White balance temperature, Kelvin. Group offsets stack by their offset from 6500. |
| `<bind>.basic.tint` | cosmo | scalar |  | -150.0..150.0 | 0.0 | White balance tint. |
| `<bind>.basic.vibrance` | cosmo | scalar |  | -100.0..100.0 | 0.0 | Vibrance. |
| `<bind>.basic.saturation` | cosmo | scalar |  | -100.0..100.0 | 0.0 | Saturation. |
| `<bind>.basic.texture` | cosmo | scalar |  | -100.0..100.0 | 0.0 | Texture. |
| `<bind>.basic.clarity` | cosmo | scalar |  | -100.0..100.0 | 0.0 | Clarity. |
| `<bind>.basic.dehaze` | cosmo | scalar |  | 0.0..100.0 | 0.0 | Dehaze. |
| `<bind>.basic.grainAmount` | cosmo | scalar |  | 0.0..100.0 | 0.0 | Grain amount; seeded from (source, frame) so a render is reproducible (R-RENDER-2). |
| `<bind>.basic.grainSize` | cosmo | scalar |  | 0.0..100.0 | 0.0 | Grain size. |
| `<bind>.detail.sharpenAmount` | cosmo | scalar |  | 0.0..150.0 | 0.0 | Sharpen amount. |
| `<bind>.detail.sharpenRadius` | cosmo | scalar | px | 0.5..3.0 | 1.0 | Sharpen radius. |
| `<bind>.detail.sharpenMasking` | cosmo | scalar |  | 0.0..100.0 | 0.0 | Sharpen masking. |
| `<bind>.detail.nrLuminance` | cosmo | scalar |  | 0.0..100.0 | 0.0 | Luminance noise reduction. |
| `<bind>.detail.nrColor` | cosmo | scalar |  | 0.0..100.0 | 0.0 | Colour noise reduction. |
| `<bind>.detail.lensDistortion` | cosmo | scalar |  | -100.0..100.0 | 0.0 | Lens distortion. |
| `<bind>.detail.lensCA` | cosmo | scalar |  | 0.0..100.0 | 0.0 | Defringe. |
| `<bind>.detail.lensVignette` | cosmo | scalar |  | -100.0..100.0 | 0.0 | Vignette. |
| `<bind>.curve.curve` | cosmo | points | x,y;… |  |  | Master tone curve: control points `x,y[,ix,iy,ox,oy];…` in 0..1. |
| `<bind>.curve.curveLog` | cosmo | bool |  |  | 1.0 | Curve in log domain. |
| `<bind>.curve.curveR` | cosmo | points | x,y;… |  |  | Red channel curve. |
| `<bind>.curve.curveG` | cosmo | points | x,y;… |  |  | Green channel curve. |
| `<bind>.curve.curveB` | cosmo | points | x,y;… |  |  | Blue channel curve. |
| `<bind>.mixer.mixer0` | cosmo | points | x,y;… |  |  | Colour mixer: hue curve. |
| `<bind>.mixer.mixer1` | cosmo | points | x,y;… |  |  | Colour mixer: saturation curve. |
| `<bind>.mixer.mixer2` | cosmo | points | x,y;… |  |  | Colour mixer: luminance curve. |
| `<bind>.mixer.mixerSpread` | cosmo | scalar |  | 0.0..100.0 | 25.0 | How far the mixer's hue selection reaches into the neighbourhood. |
| `<bind>.grade.grade0` | cosmo | triple | h,s,l |  |  | Shadows wheel: hue°, sat, lum. |
| `<bind>.grade.grade1` | cosmo | triple | h,s,l |  |  | Midtones wheel: hue°, sat, lum. |
| `<bind>.grade.grade2` | cosmo | triple | h,s,l |  |  | Highlights wheel: hue°, sat, lum. |
| `<bind>.grade.balance` | cosmo | scalar |  | -100.0..100.0 | 0.0 | Wheel balance. |
| `<bind>.grade.remapEnable` | cosmo | bool |  |  | 0.0 | Hue remap on. |
| `<bind>.grade.remapSrc` | cosmo | scalar | ° | 0.0..360.0 | 0.0 | Hue remap: source hue. |
| `<bind>.grade.remapRange` | cosmo | scalar | ° | 0.0..180.0 | 30.0 | Hue remap: range. |
| `<bind>.grade.remapDst` | cosmo | scalar | ° | 0.0..360.0 | 0.0 | Hue remap: destination hue. |
| `<bind>.grade.remapStrength` | cosmo | scalar |  | 0.0..100.0 | 0.0 | Hue remap: strength. |
| `<bind>.xform.crop` | cosmo | quad | x,y,w,h |  |  | Source crop, normalised. Part of the look, one per source (R-FX-4). |
| `<bind>.xform.rotation` | cosmo | scalar | ° | -45.0..45.0 | 0.0 | Straighten. |
| `<bind>.xform.quarterTurns` | cosmo | int |  | 0.0..3.0 | 0.0 | Quarter turns clockwise. |
| `<bind>.weight` | rackobj | scalar | 0..1 | 0.0..1.0 | 1.0 | Grade weight: a continuous bypass, blending ungraded→graded (R-RACK-4). |
| `<bind>.bypass` | rackobj | bool |  |  | 0.0 | Cosmo's bypass for the node. |
| `<bind>.frame` | rackobj | scalar | s |  | 0.0 | Reference frame a video source is graded on (R-RACK-3). Same as `rack frame`. |
| `<clip>.at` | clip | scalar | s |  | 0.0 | Timeline position of the clip's first frame. |
| `<clip>.in` | clip | scalar | s |  | 0.0 | Source in-point. |
| `<clip>.out` | clip | scalar | s |  | 0.0 | Source out-point (exclusive). |
| `<clip>.speed` | clip | scalar | × | 0.1..8.0 | 1.0 | Playback speed. |
| `<clip>.opacity` | clip | scalar | 0..1 | 0.0..1.0 | 1.0 | Clip opacity. |
| `<clip>.blend` | clip | text | normal\|add\|multiply\|screen |  |  | Blend mode. |
| `<clip>.fit` | clip | text | contain\|cover\|stretch |  |  | How the source fits the frame. |
| `<clip>.geom.x` | clip | scalar | frame | -2.0..2.0 | 0.0 | Horizontal offset, in frame widths. |
| `<clip>.geom.y` | clip | scalar | frame | -2.0..2.0 | 0.0 | Vertical offset, in frame heights. |
| `<clip>.geom.scale` | clip | scalar | × | 0.01..10.0 | 1.0 | Scale. |
| `<clip>.geom.rotation` | clip | scalar | ° | -360.0..360.0 | 0.0 | Rotation about the anchor. |
| `<clip>.geom.anchor.x` | clip | scalar | 0..1 | 0.0..1.0 | 0.5 | Anchor x. |
| `<clip>.geom.anchor.y` | clip | scalar | 0..1 | 0.0..1.0 | 0.5 | Anchor y. |
| `<clip>.geom.crop.x` | clip | scalar | 0..1 | 0.0..1.0 | 0.0 | Clip reframe crop x (R-FX-4). |
| `<clip>.geom.crop.y` | clip | scalar | 0..1 | 0.0..1.0 | 0.0 | Clip reframe crop y. |
| `<clip>.geom.crop.w` | clip | scalar | 0..1 | 0.0..1.0 | 1.0 | Clip reframe crop width. |
| `<clip>.geom.crop.h` | clip | scalar | 0..1 | 0.0..1.0 | 1.0 | Clip reframe crop height. |
| `<clip>.name` | clip | text |  |  |  | Clip name (what an address spells). |
| `<track>.opacity` | track | scalar | 0..1 | 0.0..1.0 | 1.0 | Track opacity. |
| `<track>.blend` | track | text | normal\|add\|multiply\|screen |  |  | Track blend mode. |
| `<track>.mute` | track | bool |  |  | 0.0 | Mute (video: hidden). |
| `<track>.gain` | track | scalar | dB | -60.0..12.0 | 0.0 | Audio track gain. |
| `<track>.name` | track | text |  |  |  | Track name. |
| `<fx>.radius` | fx | int | frames | 0.0..8.0 | 1.0 | Temporal footprint (R-VOL-4). |
| `<fx>.strength` | fx | scalar | 0..1 | 0.0..1.0 | 0.5 | Denoise strength. |
| `<fx>.shutter` | fx | scalar | ° | 0.0..360.0 | 180.0 | Frame-blend shutter angle. |
| `<fx>.at` | fx | scalar | s |  | 0.0 | Freeze: the source time held. |
| `<aclip>.at` | aclip | scalar | s |  | 0.0 | Timeline position. |
| `<aclip>.in` | aclip | scalar | s |  | 0.0 | Source in-point. |
| `<aclip>.out` | aclip | scalar | s |  | 0.0 | Source out-point. |
| `<aclip>.gain` | aclip | scalar | dB | -60.0..12.0 | 0.0 | Clip gain. |
| `<aclip>.fade` | aclip | scalar | s | 0.0..10.0 | 0.0 | Fade in and out length. |
| `project.masterGain` | project | scalar | dB | -60.0..12.0 | 0.0 | Master bus gain. |
| `project.name` | project | text |  |  |  | Project display name. |

## Events

Each line on the stream is `[evt] <name> key=value …`.

| event | fields | meaning |
|---|---|---|
| `info` | `text` | A note worth logging; carries no state. |
| `error` | `why` | Something failed. Also set as AppModel.lastError. |
| `command.rejected` | `line`, `why` | A line was refused (R-SVC-3): unknown verb, unknown flag, refused edit. |
| `screen.changed` | `screen` | Home / Loading / Edit. |
| `project.opened` | `path`, `timelines`, `rack` | A project is open. |
| `project.saved` | `path`, `cmp` | The .isp and the rack's .cmp were written. |
| `project.closed` |  | Back to Home. |
| `rack.loaded` | `images`, `failed` | The hosted Cosmo project finished decoding. |
| `rack.changed` | `what`, `node` | A node was added, grouped, renamed, reframed. |
| `params.changed` | `address`, `value`, `target` | An address was written. target = rack \| version \| clip \| track \| project. |
| `selection.changed` | `rack`, `clip` | The Grade target or the selected clip changed. |
| `timeline.opened` | `timeline`, `name` | The editor's current timeline changed. |
| `timeline.changed` | `timeline`, `what` | A version was created, pinned, unpinned, frozen, thawed or deleted. |
| `timeline.rebased` | `timeline`, `pruned`, `dangling`, `pin` | rebase ran: deltas pruned, the dangling ones named, the pin advanced (old→new). |
| `arrange.changed` | `timeline`, `what`, `node` | A cut operation landed. |
| `playhead.moved` | `t`, `frame` | The playhead moved by command. |
| `playback.changed` | `playing` | Playback started or stopped. |
| `frame.ready` | `seq`, `width`, `height`, `ms` | A monitor frame was composited. |
| `render.queued` | `job`, `timeline`, `out` | A render was queued. |
| `render.progress` | `job`, `timeline`, `done`, `total` | Frames written so far. |
| `render.finished` | `job`, `timeline`, `frames`, `out` | A render completed. |
| `render.failed` | `job`, `timeline`, `why` | A render stopped with an error or was cancelled. |
| `lint.report` | `offline`, `dangling`, `refused` | Counts from `lint`; details follow as info. |
| `history.changed` | `did`, `label`, `canUndo`, `canRedo` | An edit was recorded, undone or redone (did = edit \| undo \| redo \| cleared). |
| `settings.changed` | `cpuPercent`, `threads`, `previewEdge`, `useGpu`, `uiScale` | Engine settings after a change, all keys. |
| `presets.changed` | `count` | The preset library was rescanned. |

## Model

`state print --json` prints these keys in this order. `--stable` omits the rows marked *machine* and reduces paths to file names, so two front ends showing the same state print the same text.

| path | type | | meaning |
|---|---|---|---|
| `revision` | integer | *machine* | Rises on every change; a test asserts a command changed something. |
| `screen` | enum(home\|loading\|edit) |  | Which surface is up. Which TAB is not state — it is presentation. |
| `recents` | array | *machine* | Home's project cards, newest first. |
| `recents[].name` | string | *machine* | Project name. |
| `recents[].path` | string | *machine* | The .isp path. |
| `recents[].coverPath` | string | *machine* | First source's file, for the card cover; empty = none. |
| `recents[].sourceCount` | integer | *machine* | Rack sources in the project. |
| `recents[].sizeBytes` | integer | *machine* | Total size of the project's media. |
| `recents[].lastOpened` | integer | *machine* | Unix seconds. |
| `projectPath` | string |  | Open project's .isp (file name only when stable). |
| `projectName` | string |  | Open project's display name. |
| `dirty` | bool |  | Unsaved changes. |
| `fps` | number |  | Project frame rate. |
| `width` | integer |  | Delivery width. |
| `height` | integer |  | Delivery height. |
| `rack` | array |  | The hosted Cosmo project's nodes, flattened in tree order (R-RACK). |
| `rack[].node` | integer |  | Cosmo node id. |
| `rack[].rackObj` | string |  | Interstellar's #rackobj id. |
| `rack[].bindName` | string |  | What an address spells: `<bindName>.basic.exposure`. |
| `rack[].cosmoName` | string |  | Cosmo's own name for the node (display). |
| `rack[].parent` | integer |  | Index into rack of the parent group; -1 at the root. |
| `rack[].depth` | integer |  | Tree depth. |
| `rack[].group` | bool |  | A group: its grade stacks onto every descendant. |
| `rack[].bypass` | bool |  | Cosmo's bypass. |
| `rack[].pending` | bool |  | Pixels still decoding. |
| `rack[].failed` | bool |  | Offline: reads as missing, never as a stall (R-RACK-7). |
| `rack[].weight` | number |  | Grade weight 0..1 (R-RACK-4). |
| `rack[].media` | string |  | Source file (file name only when stable). |
| `rack[].frame` | number |  | Reference frame a video is graded on, seconds (R-RACK-3). |
| `rack[].video` | bool |  | A video source. |
| `rack[].usedBy` | integer |  | Clips referencing it in the current timeline. |
| `rack[].overridden` | bool |  | The current version carries a colour override on it. |
| `rack[].selected` | bool |  | In the selection that Group Selection groups (R-RACK-8). |
| `selectedRack` | integer |  | Index into rack of the Grade target; -1 = none. |
| `hasGradeTarget` | bool |  | gradeParams/gradeOwnParams are meaningful. |
| `gradeParams` | object |  | The target's EFFECTIVE params in the current version: stacked reach + overrides. Keys are EditParamsIO keys. |
| `gradeOwnParams` | object |  | The target's OWN params in the current version — what `set` changes. |
| `sourceWidth` | integer |  | Grade target's source width. |
| `sourceHeight` | integer |  | Grade target's source height. |
| `timelines` | array |  | Every timeline — every VERSION — base before derived (R-VER). |
| `timelines[].id` | string |  | Timeline id. |
| `timelines[].name` | string |  | Name; render and open accept it. |
| `timelines[].base` | string |  | The timeline this is a version of; empty = a root. |
| `timelines[].depth` | integer |  | Depth in the version tree. |
| `timelines[].colourPinned` | bool |  | Colour pinned to a rack snapshot. |
| `timelines[].pinCommit` | string |  | The snapshot when pinned. |
| `timelines[].cutFrozen` | bool |  | Arrangement frozen. |
| `timelines[].danglingDeltas` | integer |  | Deltas whose target the base deleted — `rebase` reports them. |
| `timelines[].overrides` | integer |  | How far this version has diverged: #tlset + #tlgrade + #tldrop count. |
| `currentTimeline` | string |  | The editor's current timeline id. Never what a render uses. |
| `tracks` | array |  | The resolved current timeline's tracks. |
| `tracks[].id` | string |  | Track id. |
| `tracks[].name` | string |  | Track name. |
| `tracks[].audio` | bool |  | An audio track (#atrack). |
| `tracks[].order` | integer |  | Stacking order; higher draws on top. |
| `tracks[].mute` | bool |  | Muted / hidden. |
| `tracks[].opacity` | number |  | Track opacity. |
| `tracks[].gain` | number |  | Audio track gain, dB. |
| `tracks[].provenance` | enum(local\|inherited\|overridden\|dangling) |  | Where it came from in this version. |
| `clips` | array |  | The resolved current timeline's clips. |
| `clips[].id` | string |  | Clip id. |
| `clips[].name` | string |  | Clip name — an address prefix. |
| `clips[].track` | string |  | Track id. |
| `clips[].src` | string |  | #rackobj id (video) or media path (audio). |
| `clips[].srcName` | string |  | The rack node's bind name. |
| `clips[].at` | number |  | Timeline position, seconds. |
| `clips[].in` | number |  | Source in-point, seconds. |
| `clips[].out` | number |  | Source out-point, seconds. |
| `clips[].speed` | number |  | Playback speed. |
| `clips[].duration` | number |  | Timeline duration, (out-in)/speed. |
| `clips[].opacity` | number |  | Clip opacity. |
| `clips[].gain` | number |  | Audio clip gain, dB. |
| `clips[].audio` | bool |  | An audio clip (#aclip). |
| `clips[].offline` | bool |  | Its media is missing. |
| `clips[].provenance` | enum(local\|inherited\|overridden\|dangling) |  | Where it came from in this version. |
| `transitions` | array |  | Transitions in the resolved current timeline. |
| `transitions[].id` | string |  | Transition id. |
| `transitions[].clipA` | string |  | Outgoing clip — HELD through the transition (R-TL-4). |
| `transitions[].clipB` | string |  | Incoming clip. |
| `transitions[].kind` | string |  | dissolve \| dip. |
| `transitions[].dur` | number |  | Seconds. |
| `markers` | array |  | Markers in the current timeline. |
| `markers[].id` | string |  | Marker id. |
| `markers[].name` | string |  | Marker name. |
| `markers[].at` | number |  | Seconds. |
| `markers[].note` | string |  | Free text. |
| `selectedClip` | string |  | Selected clip id; empty = none. |
| `duration` | number |  | Current timeline length, seconds. |
| `playhead` | number |  | Seconds, on a frame boundary. |
| `playheadFrame` | integer |  | Playhead as a frame index. |
| `playing` | bool |  | Playback running. |
| `frameSeq` | integer | *machine* | Rises when a new monitor frame is ready. |
| `frameWidth` | integer | *machine* | Monitor frame width. |
| `frameHeight` | integer | *machine* | Monitor frame height. |
| `renders` | array |  | The render queue. |
| `renders[].id` | string |  | Job id. |
| `renders[].timeline` | string |  | The timeline rendered — always named (R-RENDER-1). |
| `renders[].timelineName` | string |  | Its name. |
| `renders[].outPath` | string |  | Output path (file name only when stable). |
| `renders[].format` | string |  | h264 \| prores \| png-seq. |
| `renders[].done` | integer |  | Frames written. |
| `renders[].total` | integer |  | Frames in the range. |
| `renders[].state` | string |  | queued \| running \| done \| failed \| cancelled. |
| `renders[].error` | string |  | Why it failed. |
| `canUndo` | bool |  | `undo` has something to undo (R-EDIT-1). |
| `canRedo` | bool |  | `redo` has something to redo. |
| `undoLabel` | string |  | What `undo` would undo, e.g. `set a.basic.exposure`. |
| `redoLabel` | string |  | What `redo` would redo. |
| `hasGradeClipboard` | bool |  | `grade copy` has filled the clipboard (R-EDIT-2). |
| `gradeClipboardFrom` | string |  | The bind name the clipboard grade came from. |
| `settings` | group |  | Engine settings (R-SET). |
| `settings.cpuPercent` | integer |  | Share of the machine's cores the app may schedule — the rack's decode and the frame path alike. |
| `settings.threads` | integer |  | Engine worker threads; 0 = auto (from cpuPercent). |
| `settings.previewEdge` | integer |  | Cap on the monitor's render long edge, px; 0 = full. Renders are unaffected. |
| `settings.useGpu` | bool |  | GPU opt-in for the grade step (only where a backend exists). |
| `settings.uiScale` | integer |  | Percent of the design size the window draws at. |
| `settings.gpuAvailable` | bool | *machine* | A GPU backend exists on this machine. |
| `settings.cores` | integer | *machine* | Cores on this machine. |
| `settings.engineThreads` | integer | *machine* | Engine threads the budget resolves to now. |
| `settings.decodeWorkers` | integer | *machine* | Decode workers the budget resolves to now. |
| `presets` | array |  | The preset library (Cosmo `.apf`), depth first (R-EDIT-3). |
| `presets[].name` | string |  | What `preset apply` spells, folders joined with `/`. |
| `presets[].folder` | string |  | Its folder, empty at the top. |
| `lastError` | string |  | The last refusal or failure, for a front end that was not listening. |
