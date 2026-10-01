# Interstellar — Requirements (as-built tier)

**What the code contractually does today.** `DR-<AREA>-<n>`, descriptive present, `file:line`
anchors, each citing the `R-` tag it implements. An entry with a dead anchor is a defect
(cosmo's D-1), and a behaviour with no entry does not ship.

**There is no code.** The first build's as-built tier is withdrawn with the build it described
(git history, `69b91eb`); carrying it forward would be a document describing a seam that no longer
exists, which is exactly the failure this tier exists to prevent.

## Conformance

Rung **0** of `arstro.rule` §5 — spec only. The ladder, and what each rung will add here:

| rung | means | lands with |
|---|---|---|
| 0 | spec only | ← **here** |
| 1 | core split out; `Command`/`Event`/model with one text codec | P3 |
| 2 | registered with the root `ctest`; L2 headless service tests | P2 |
| 3 | a real CLI that is the whole app without a window | P3 |
| 4 | **generated API document, committed, drift-tested** | P3 — and **no app in the suite has this** |
| 5 | control socket + the GUI/headless equivalence test | post-P7 |

## Entries

### DR-RACK-1 The rack is a real hosted CosmoService (R-RACK-1)
`Rack` (`core/Rack.h:1`) owns a `cosmo::CosmoService` constructed with Interstellar's
`ThreadBudget` by reference, and reaches it **only** through `cosmo::Command`s. A colour write is
the `Select` + `Set` pair `cosmo-cc` sends (`Rack::setParam`, `core/Rack.cpp`), so the CLI path and
any future GUI path are one path. `interstellar_core` links `cosmo_core` deliberately — a real
authority is worth a link dependency, and avoiding one is what let colour be a fake in two builds.

### DR-RACK-2 A colour edit reaches a real `.cmp` (R-RACK-2) — THE GATE
Guarded by `test_a_colour_edit_reaches_a_real_cmp`, which reads the saved file back with code that
shares **nothing** with the writer, and by `test_a_second_service_sees_the_edit_the_first_one_made`,
which opens the saved file in a second `CosmoService` and reads `exposure 0.450, temp 5200` back.
From a shell, on a **video** source:
```
interstellar-cc rack open look.cmp : rack set 3 exposure=0.35 : rack set 3 temp=5200 : rack save
→ the .cmp's third #image carries   exposure=0.35   temp=5200
```

### DR-RACK-3 A video source is graded on an extracted frame, with no change to Cosmo (R-RACK-3)
`VideoFrameDecoder` (`host/VideoFrameDecoder.cpp:1`) is an `IImageDecoder` installed through
`CosmoService::setDecoderFactory`: a video path — `…/clip.mp4#t=2.0` — becomes one extracted frame
via `FrameSourceFFmpeg`; anything else delegates to Cosmo's own `NativeImageDecoder`. Cosmo then
holds an ordinary image slot. `rack add a.png b.png clip.mp4#t=2.0` → `3 images in the rack`.
**Limit, filed as D-1:** Cosmo opened *by itself* has no such decoder and shows the video node as
`kind=failed` — its grade is preserved in the file, its pixels are not displayable there.

### DR-RACK-4 Grouping stacks through Cosmo's own composeParams (R-RACK-4)
`Rack::effectiveParams` returns `AppModel::params` (Cosmo's `effectiveEditParams`) after a
`Select`; `getParam` reads `ownParams` back through Cosmo's own serializer rather than a field
switch. Guarded by `test_a_group_offset_stacks_onto_its_members`: a group's +0.5 EV appears in its
member's effective params while the member's own value stays 0.

### DR-RACK-5 The load is pumped against the wall clock (R-RACK-1)
`Rack::pumpUntilLoaded` (`core/Rack.cpp`) sleeps 1 ms per 16 ms tick against a wall-clock deadline
and keeps its simulated clock **monotonic** as a member — cosmo's own `pumpUntilIdle` pattern, and
D-56's lesson. The first version spun simulated time and reported `0 images` for a project that was
loading fine, because 7 500 ticks elapsed before a worker opened the first file. A timeout is now
an error naming the path, never a silently empty rack.
