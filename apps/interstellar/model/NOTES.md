# interstellar_model — as built

The `.isp` document and timelines-as-versions, as plain C++17. It needs nothing at runtime. The
only include from outside this directory is `../core/service/AppModel.h`, used for `NodeId` and
`Provenance`; that header pulls in the header-only `engine/EditParams.h`.

Normative sources: `docs/project-format.md` (all sections), `REQUIREMENTS.md` (R-VER, R-TL,
R-RACK-6/7, R-FX, R-AUD) and `../../docs/audio-format.md` §1, §2 and §4.

| file | what it is |
|---|---|
| `Project.{h,cpp}` | The document: typed nodes, `RawNode`, parse/serialize, validation, ids, names, `rename`. |
| `Schema.h` | **Internal.** One field table per node type. The parser, serializer, `#tlset` application, `setField` validation and thaw's diff all read it, so the five cannot disagree about what fields a node has. |
| `Versions.{h,cpp}` | `resolve`, derived-aware `setField`/`setFields`/`dropNode`, colour deltas, pin, freeze/thaw, `rebase`, `diff`. |
| `Arrange.{h,cpp}` | The cut operations in `namespace arrange`. Each one goes through `setFields` / `dropNode` / a local add. |
| `tests/modelTests.cpp` | `interstellar_model_tests` (`add_test(interstellar_model)`). |

Build: `cmake -S apps/interstellar/model -B <dir> && cmake --build <dir> && ctest --test-dir <dir>`.
The directory also works through `add_subdirectory`, because `project()` and `enable_testing()` are
guarded. Both modes were checked.

---

## Public API

### Project.h (namespace `arstro::interstellar`)

```cpp
using Fields = std::vector<std::pair<std::string, std::string>>;   // ordered key/value text
struct Notes { std::string inlineComment; std::vector<std::string> after; };

struct RackRef  { id, path, branch="main", writeBranch, unknown, notes };          // #rack (see "Deviations")
struct RackObj  { id, name, node, kind="source", weight=1, media, frame=0, unknown, notes };
struct Timeline { id, name, base, colour="follow", cut="follow", order, unknown, notes;
                  pinned(), pinCommit(), frozen() };
struct TlDrop   { timeline, node, unknown, notes };
struct TlSet    { timeline, node, Fields fields /* overrides, read order */, notes };
struct TlGrade  { timeline, node, vector<pair<string,double>> deltas, notes };
struct Geom     { x, y, scale, rotation, anchorX, anchorY, cropX, cropY, cropW, cropH };
struct Track    { id, name, timeline, kind="video", order, mute, opacity, blend, gain, from, … };
struct Clip     { id, name, track, timeline, order, src, at, in, out, speed, fit, opacity, blend,
                  geom, from, …; duration(), end() };
struct Transition { id, name, track, timeline, clipA, clipB, kind="dissolve", dur=0.5, from, … };
struct Marker   { id, name, timeline, at, note, from, … };
struct ATrack   { id, name, timeline, kind="audio", order, gain, pan, mute, solo, out, from, … };
struct AClip    { id, name, track, timeline, src, at, in, out, gain, fadeIn, fadeOut, loop, from, … };
struct Fx       { id, node, clip, type, radius, strength=0.5, shutter=180, at, … };
struct RawChild { type /* "" = breakpoint/field line */, fields, text };
struct RawNode  { type, id, fields, children, lines /* exact text, re-emitted */, why };
enum class NodeKind { None, Rack, RackObj, Timeline, Track, Clip, Transition, Marker, ATrack, AClip, Fx, Raw };

class Project {
  // header: formatVersion, app, id, name, fps, width, height, par, colorspace, timebase,
  //         sampleRate, masterGain, current, headerUnknown, preamble, headerNotes
  // nodes:  hasRack + rack, rackObjs, timelines, drops, sets, grades, tracks, clips, transitions,
  //         markers, audioTracks, audioClips, effects, raw
  bool parse(const std::string &text, std::string &err, int *repaired = nullptr);
  std::string serialize() const;
  bool load(const std::string &path, std::string &err, int *repaired = nullptr);
  bool save(const std::string &path, std::string &err) const;    // <path>.tmp then rename
  static bool roundTripsExactly(const std::string &text, std::string &err);
  bool validate(std::string &err) const;                          // §7, for documents built in code
  std::vector<std::string> unrenderable() const;
  // lookup by id: timeline() rackObj() track() clip() transition() marker() audioTrack()
  //               audioClip() fx() rawNode() tlset(tl,n) tldrop(tl,n) tlgrade(tl,n)
  NodeKind kindOf(id) const;  NodeId idForRef(idOrName) const;  NodeId ownerOf(id) const;
  NodeId clipTimeline(c) / transitionTimeline(t) / audioClipTimeline(a) const;
  std::vector<NodeId> chain(tl) const;  bool derivesFrom(tl, ancestor) const;
  std::vector<std::string> bindNames() const;
  NodeId freshId(prefix);  std::string freshName(base) const;
  static bool nameIsLegal(name, why);  bool nameIsTaken(name) const;
  bool rename(id, newName, err);  static bool fieldIsColour(key);
};
std::string canonicalNumber(double);  std::string canonicalTime(double);
std::string quoteIfNeeded(const std::string &);  bool parseNumber(const std::string &, double &);
```

### Versions.h

The functions match the contract exactly, with two additions: `setFields` and `deltaId`.

```cpp
struct Dangling { NodeId delta; NodeId target; std::string kind; std::string why; };
std::string deltaId(kind, timeline, node);                    // "tlset:tl_2:clp_5"
struct ResolvedTimeline { timeline, tracks, clips, transitions, markers, audioTracks, audioClips,
                          std::map<NodeId, Provenance> provenance, std::vector<Dangling> dangling; };
bool resolve(const Project &, const NodeId &timeline, ResolvedTimeline &, std::string &err);
std::vector<std::pair<std::string,double>> gradeDeltas(const Project &, tl, rackObj);
bool newTimeline(Project &, name, base, NodeId &outId, err);
bool setField(Project &, tl, node, key, value, err);
bool setFields(Project &, tl, node, const Fields &kv, err);   // added: atomic multi-key edit
bool dropNode(Project &, tl, node, err);
bool setGrade(Project &, tl, rackObj, key, double delta, err);
bool clearGrade(Project &, tl, rackObj, key /* "" = all */, err);
bool pinColour(Project &, tl, commit, err);  bool unpinColour(Project &, tl, err);
bool freezeCut(Project &, tl, err);          bool thawCut(Project &, tl, err);
struct RebaseReport { std::vector<Dangling> dangling; int pruned = 0; std::string message; };
RebaseReport rebase(Project &, tl, bool prune, bool dryRun);
std::string diff(const Project &, tl);
```

### Arrange.h (namespace `arstro::interstellar::arrange`)

```cpp
enum class Edge { Head, Tail };
bool addTrack(Project &, tl, kind /* video|audio */, name /* "" = v0,v1… */, NodeId &outId, err);
bool addClip(Project &, tl, track, src, double in, double out, double at, name, NodeId &outId, err);
bool trim(Project &, tl, clip, Edge, double t /* new edge, timeline s */, err);
bool split(Project &, tl, clip, double t, NodeId &outRight, err);
bool move(Project &, tl, clip, double at, track /* "" = same */, err);
bool roll(Project &, tl, left, right, double t, err);
bool slip(Project &, tl, clip, double delta /* SOURCE seconds */, err);
bool remove(Project &, tl, clip, bool ripple, err);
bool addTransition(Project &, tl, a, b, kind /* dissolve|dip */, double dur, NodeId &outId, err);
```

Every function in Versions and Arrange accepts a timeline or node as either an **id** or a **bind
name**. A refused call changes nothing. The multi-step operations (split, roll, ripple remove)
save a copy of the document first and put it back if a later step fails.

---

## Deviations from the contract, and why

1. **`struct Rack` is named `struct RackRef`.** The integrator asked for this:
   `interstellar::Rack` is already the hosted CosmoService class in `core/Rack.h`, and the service
   links both libraries into one namespace. The member is still `Project::rack` (and `hasRack`),
   and `NodeKind::Rack` keeps its name. `canonicalNumber(double)` stays as specified, because the
   integrator will make it the only one in the namespace: it writes the shortest decimal that
   reads back bit-identically, and never writes `-0`.
2. **`#clip`, `#transition` and `#aclip` may carry an optional `timeline=`.** The contract says
   clips belong to the timeline of their track. The contract also says a split of an inherited
   clip creates a **local** right half on the same track, and that track is inherited. Without
   the field, that half would belong to the base and show up in every sibling version. So the
   field is written only when it differs from the track's timeline. `addClip` and
   `addTransition` on an inherited track use it too.
3. **Arrangement nodes may carry an optional `from=`.** Only `freezeCut` writes it. Ids and bind
   names are unique across the document, so a frozen copy cannot reuse its origin's id or name.
   `from` is how `thawCut` matches each copy back to the base node it came from.
4. **`setFields` was added** (an atomic multi-key `setField`). A head trim changes `in` and `at`,
   and those must be validated and applied as one edit, never as two where the first can stand
   alone.
5. **Arrange is in `namespace arrange`**, because `move` and `remove` are standard-library names.
   The contract named the operations but not their signatures; the signatures above are mine.
6. **Deltas have no id**, matching the doc's examples. A delta's identity is
   `(type, timeline, node)`. A second delta with the same identity is refused, because one version
   cannot say two things about one node. For a delta, `Dangling::delta` is `deltaId(kind, tl,
   node)`.
7. **Unknown node types** (for example a future `#bind`) are kept as `RawNode` like the Solaris
   nodes. They sort after all known types and are listed by `unrenderable()` as "unknown node
   type".

## Decisions made inside the contract

**Text and canonical form**
- **`roundTripsExactly(text)` is strict:** it is true only if `serialize(parse(text)) == text`.
  The tests call it on `p.serialize()` after every operation.
- **Numbers:**
  - Numbers are written with `std::to_chars`, so they do not depend on the locale. This matters
    because a GTK host calls `setlocale`, and `printf` would then write `4,25`.
  - Fixed notation is used for `1e-6 ≤ |v| < 1e15`. Outside that range the number is written as
    the shortest scientific form.
  - A whole number keeps `.0`, and `-0` is written as `0.0`.
  - Times always have 3 decimals. Integer fields are written as integers, and booleans as
    `true`/`false`.
  - Parsing is whole-string and finite-only, and accepts a leading `+`.
- **Header:** keys are padded to 14 columns (`arstro-project` is the longest). Every known key is
  always written except `current`, which is written only when set. Unknown header keys follow in
  the order they were read.
- **Node layout follows the spec's own examples:**
  - `#clip` takes five lines: `id name track [timeline] order src` / `at in out speed fit
    opacity blend` / `geom.x/y/scale/rotation` / `geom.anchor.*` / `geom.crop.* [from]`.
  - A source `#rackobj` puts `media frame` on a second line.
  - A sample `#aclip` puts `gain fadeIn fadeOut loop` on a second line.
  - Unknown keys go at the end of the node's last line.
- **Optional fields are written only when they carry meaning.** Every such rule was chosen so
  that nothing in the file is ever dropped:
  - `name` is written only when non-empty. A node without one is addressed by its id.
  - `mute` is written only when true.
  - `opacity` and `blend` are written for video tracks, and for audio tracks only when not the
    default. `gain` is the reverse: written for audio tracks, and for video only when not 0.
  - `colour` and `cut` are written on every derived timeline, and on a root only when not the
    default.
  - `frame` is written when there is media or when it is non-zero.
  - An `#fx` type parameter is written for its own type, or when it is not the default.
- **Comments:**
  - Comment lines before the first header key are kept as the preamble.
  - On a header key, an inline comment and any following comment lines stay with that key.
  - On a node, an inline comment is written at the end of its first line. Inline comments from
    continuation lines are joined onto it.
  - Comment lines that follow a node are written after it.
  - Blank lines are not kept. Canonical form puts one blank line between type groups.
- **Raw nodes** are written back byte-for-byte: the header line plus every following indented
  line and comment line. A `#note` written flush-left after an `#aclip`, or an `#aeffect` after an
  `#arack`, is still treated as that node's child, so sorting cannot separate them. `rawView`
  rebuilds the read-only fields, children and id from the stored text.
- **`#tlset` overrides keep the order they were read in.** A new key is appended and an existing
  key is replaced in place. When the target exists, the values are canonicalised by its field
  types (`4.2` becomes `4.200`, a name becomes an id). A dangling `#tlset` keeps its text exactly
  as written.
- **References are stored as ids.** A bind name written in a reference (`src=s_day01`,
  `current = main`) is turned into the id at parse time. That means the first pass changes the
  text. `rename` therefore updates the name and rewrites references spelled with the old name
  inside raw text, which is the only place a name-spelled reference can survive. It also works on
  raw nodes: only the `name=` token in their stored text changes.
- **Bind names** apply to every named typed node, timelines included. A name must be legal and
  unique among names, and must not equal another node's id. So `social-30s`, the example name in
  R-RENDER-1, is not a legal timeline name. The reserved words are `project timeline rack t frame
  fps dur self true false` plus the expression function names from the previous build.
- **Colour keys:**
  - These are refused case-insensitively, as the key itself or with any dotted extension
    (`grade.lift`): grade, curve, mixer, lut, look, exposure, contrast, temp, tint, saturation,
    vibrance, highlights, shadows, whites, blacks, clarity, texture, dehaze, params. Also refused:
    `editparams`, `wb`, `colour`, `color`.
  - The check applies to `#clip`, to every `#tlset`, and to `setField` on a clip.
  - The error names the key and points to `#tlgrade`.

**Validation (§7)**
- These errors refuse the document:
  - two `#rack`;
  - a base cycle, named in full: `tl_a -> tl_c -> tl_b -> tl_a`;
  - a clip with `in >= out` or `speed == 0`;
  - a `src` that names no `#rackobj`, with the error listing the rack's bind names;
  - a duplicate id (raw children's ids count);
  - a missing base or timeline;
  - a `#tlgrade` on something that is not a `#rackobj`;
  - a `#tlset` or `#tldrop` on something that is not an arrangement node;
  - a `#tlset` on an identity or shape field;
  - a bad `colour` or `cut` value;
  - a picture clip on an audio track, or a sound clip on a video track.
- **A delta's target may be missing.** It dangles. The project still opens and the dangling
  delta is reported. If a missing target were refused, any base delete would make every derived
  file impossible to open.
- **The anchor rule:**
  - A clip with no explicit `timeline` must have its track. Without it the clip would belong to
    no timeline at all.
  - A clip with an explicit `timeline` may lose its track. That is a dangling anchor, reported by
    `resolve`.
- **A transition longer than a neighbour** is refused at parse only when the transition and both
  clips are declared by one timeline. Across versions it is a `conflict` that `resolve` reports.
- **Repair rules:**
  - A non-finite or unreadable number is repaired to the field default and counted. This covers
    header, typed fields, `#tlgrade` deltas and `#tlset` values whose target exists.
  - Repaired `in`/`out`/`speed` values that would leave a clip with no frames are widened to one
    frame (`out = in + 1/fps`), and that also counts. Otherwise a single `nan` could still make the
    file unopenable.
- `current` is not validated, because it is presentation only (R-RENDER-1).
- `app` is not enforced, so a Solaris `.slp` still parses.

**Versions**
- **Resolution order within one timeline:**
  1. Explicit drops.
  2. Overrides.
  3. Consequences of the drops: inherited clips on a dropped track and transitions touching a
     dropped clip leave without being reported as dangling.
  4. Local additions.
  5. Anchor and conflict checks.

  Overrides come before consequences on purpose. If a user moves an inherited clip off a track
  and then drops the track, the clip stays.
- **Provenance is recomputed at each level.** A node that was broken in the base can be fixed by
  an override here, and a healthy one can be broken by one. Nodes with a missing anchor, or with
  no frames once their overrides are applied, **stay in the result** marked `Dangling`, so an
  editor can show and fix them. A renderer must skip them.
- **`Dangling::kind`:**
  - `tlset`, `tldrop`, `tlgrade` and `transition` can be pruned. A transition is prunable because
    a dissolve between nothing means nothing.
  - `clip`, `aclip` and `conflict` are only reported. A version's own clip is the user's content,
    and a conflict means the target still exists.
  - `dangling` lists only problems that this timeline owns. A node that arrives already broken
    from the base is marked but not listed again; its owner lists it.
- **Deriving the edit:**
  - **If this timeline owns the node, the node is edited in place.**
  - **If the node is inherited, the edit becomes a `#tlset`.** Setting a field back to the
    base's value removes that key from the `#tlset`, and an empty `#tlset` is removed.
  - `name` and `id` are refused and point to `rename`.
  - A reference value must resolve inside the timeline being edited. For example, a clip cannot
    be moved to a track that is not visible in this version.
  - The edit is refused if it leaves a clip with no frames, makes a transition longer than a
    neighbour, or makes `in` or `at` negative. These are edit-time rules, not parse rules.
- **`dropNode`:**
  - On an owned node, it erases the node and this timeline's own nodes that hang on it (clips on a
    track, transitions touching a clip, a clip's `#fx`).
  - On an inherited node, it adds a `#tldrop`, removes this timeline's `#tlset` for that node, and
    erases this timeline's own nodes that hang on it.
  - Deltas in other versions are left to dangle where they can be seen.
- **`gradeDeltas`:**
  - It walks the chain, and the nearest timeline wins for each key. Results are nearest first.
  - A zero delta still wins, because it means "nothing on top of the rack". `clearGrade` removes
    a key so it inherits again.
  - A pinned timeline contributes its own deltas and then stops the walk. The base's deltas are
    part of the colour the pin froze; the caller holds the rack snapshot for that pin.
  - Colour commands on a pinned timeline are refused, and the error names the commit.
  - Pinning a root is refused.
- **`freezeCut`:**
  - It is refused while anything dangles, because freezing would make the breakage permanent.
  - It copies the timeline's resolved arrangement, with overrides already applied, as local nodes.
    Each copy gets a fresh id, the name `<name>_<timeline>`, and `from=`.
  - Then it re-points this timeline's own nodes onto the copies, folds its `#tlset`/`#tldrop` into
    them, and re-points every version that inherits from this timeline. Without that last step, a
    freeze upstream would break every version downstream.
- **`thawCut`:**
  - It diffs each copy against the base's current resolution. Only editable fields are compared,
    with references mapped back through the copies; unknown keys are compared too.
  - A base node with no copy becomes a `#tldrop`. No drop is written for a node whose anchor is
    also dropped, because the anchor's drop already covers it.
  - A copy whose origin has since been deleted becomes plain local content.
  - It is refused if an `#fx` is attached to a copy, because effects are not versioned (see
    limits).
  - Freezing and then thawing with no edits in between returns a **byte-identical** document
    (tested).
- **`rebase`:**
  - It reports every dangling item, and with `prune` removes the prunable ones. A dry run counts
    what it would remove without changing anything.
  - It never advances a pin, but its message mentions any pin and any frozen cut.
- **`diff`:**
  - It lists what the version changes, in lines of the form `drop` / `override … (base x)` /
    `add` / `grade` / `dangling`.
  - On a frozen timeline it shows what a thaw would record, worked out on a scratch copy of the
    document.
- **`freshId(prefix)`:** each prefix has a counter that only goes up. It starts above the
  highest suffix used by any id **and any reference** in the document. So a dangling delta's target
  id is never handed out again, which would otherwise quietly re-attach the delta to an unrelated
  node.

**Arrange**
- Computed times are snapped to the file's 1 ms grid before they are checked. The value that is
  validated is then exactly the value that is written.
- `split`:
  - The left half is the original, trimmed through `setFields`. On an inherited clip that becomes
    `#tlset out`.
  - The right half is a new local clip. It is built from the resolved clip, so overrides carry
    over.
  - A transition that left the original now leaves the right half. On an inherited transition
    this is a `#tlset between=`.
  - The split is refused if either half would be shorter than a transition attached to it.
- `remove(ripple)`: every later clip on the same track, meaning one starting at or after the
  removed clip's end, shifts left by the removed clip's length. On inherited clips each shift is a
  `#tlset at=`.
- `roll`: the two clips must be adjacent, within half a millisecond.
- Reverse clips (negative speed): parsing allows them, since only `speed == 0` is refused. The cut
  operations refuse to work on them.

---

## Test output

```
$ interstellar_model_tests   # Release (-DCMAKE_BUILD_TYPE=Release; NDEBUG undefined in the suite)
interstellar_model_tests: PASS (431 checks, 19 groups)
real	0m0.006s
$ ctest --output-on-failure
1/1 Test #1: interstellar_model ...............   Passed    0.01 sec
100% tests passed, 0 tests failed out of 1
$ interstellar_model_tests   # Debug, -fsanitize=address,undefined
interstellar_model_tests: PASS (431 checks, 19 groups)
$ interstellar_model_tests   # MUTANT: setField copies an inherited clip (+#tldrop) instead of a #tlset
interstellar_model_tests: …/tests/modelTests.cpp:433: void {anonymous}::derivedEditRecordsDeltaNotCopy():
  Assertion `p.clips.size() == clips && p.tracks.size() == tracks' failed.      (exit status 134)
```

**R-TEST-3 was checked, not assumed.** In a scratch copy of the library, I changed the
inherited-node branch of `applyCanon` (the code under `setField`). The mutant made a local copy of
the inherited clip with the edit applied and added a `#tldrop` on the original: the copy-on-edit
mistake. Every group before `derivedEditRecordsDeltaNotCopy` still passed against the mutant. The
suite then aborted on that test's clip-count assertion. The mutant is not in the repository.

The asserts are live in a Release build: the suite does `#undef NDEBUG` before `<cassert>`. During
development, real failures in a `-DCMAKE_BUILD_TYPE=Release` build aborted the run.

What the 19 groups cover:

| group | what it covers |
|---|---|
| Golden round trip | A canonical document with every node type round-trips byte-exactly: raw instrument and bus tracks, a note clip with `#note` children, `#arack` with `#aeffect`, `#aauto` breakpoints, `#asend`, an unknown `#bind`, unknown keys and comments everywhere. Also checks typed and raw inspection and the `unrenderable()` names. |
| Messy input | Out-of-order, CRLF, name-spelled input becomes canonical in one pass. Also checks canonical number rules and quoting. |
| Colour refusals | Colour keys on `#clip`, in `#tlset` and through `setField` are refused, and the error names the key. |
| nan repair | `nan`/`inf` values are repaired and counted (exactly 6), including the one-frame widening. |
| Structural refusals | Every structural refusal, with the error checked for the right names. For a cycle that means both ends and the full chain. |
| Resolve | Inherited, Overridden and Local provenance. A one-field `#tlset` leaves the other fields inheriting, and a later base edit to another field arrives. `#tldrop` and its consequences. A future key inside a `#tlset`. A grandchild. |
| **Delta, not copy** | **Clip and track counts unchanged.** `#tlset` merging, setting a field back to the base value, inherited tracks and markers. |
| Owned edits and refusals | Owned edits happen in place. Refusals for unknown keys (the error lists the editable ones), `name`, `kind`, `nan`, no frames, a foreign track, a bad `src`, a transition that would consume the clip, a dropped node. |
| Split | Split of an inherited clip: `#tlset out` plus exactly one new local clip. The transition moves to the right half as a delta. Refusals. |
| Dangling and rebase | The base deletes a clip that a derived `#tlset` targets: the delta dangles and the file still round-trips. `freshId` does not reissue the dangling target's id. Rebase dry run, report, then prune. A version's own clip whose track was deleted is never pruned. A conflict is reported. |
| Colour deltas | `gradeDeltas` nearest-wins through three levels, a zero delta, `clearGrade`, pin refusal naming the commit, the pin stopping the walk, unpin. |
| Freeze and thaw | Folding, everything Local while frozen, base re-cuts not arriving, the frozen `diff`. Thaw is **byte-identical** to before. An edit made while frozen returns as a delta. |
| Freeze downstream | A grandchild's override and its own clip follow the parent's freeze and thaw, and the thaw is byte-identical. |
| Rename | `rename` rewrites every reference, including raw text. The refusals change nothing. Renaming a raw node. |
| Arrange | trim head/tail (the remaining frames stay where they were), refusals, derived trim and move with no new clips, roll keeping total length, slip, ripple remove by delta, addTransition refusal, local addTrack and addClip. |
| Frozen edits | On a frozen timeline, edits are local. |
| `diff` | `diff` text. |
| Save and load | `save` then `load`. |
| Ids and names | `freshId` never reuses an id; `freshName` legalises names. |

---

## Known limits

- **`#fx` is not versioned.** Effects are not part of `ResolvedTimeline`, and `freezeCut` does
  not copy clip-attached effects. A frozen version's copy of a clip therefore loses the clip's
  `freeze` effect. That effect is still attached to the origin clip, which is global. `thawCut`
  refuses rather than orphan an effect that was attached to a copy.
- **Freeze and thaw do not version names.** A copy's bind name is `<name>_<timeline>`. If a copy
  is renamed while frozen, the new name is lost on thaw, because a `#tlset` cannot override a
  name.
- **`freshId` has no persisted counter.** After a save and reload, an id that was deleted *and*
  is no longer referenced anywhere can be issued again. Ids that are still referenced, such as a
  dangling delta's target, are always safe.
- **No snapping to frames.** The model stores times on a 1 ms grid. It does not enforce R-TL-5
  ("frames are the authority"). Snapping cut points to `1/fps` belongs to the command layer, and
  is not done there yet.
- **`#track kind=audio` and `#atrack` both exist**, because the contract lists both. An `#aclip`
  may sit on either, or on a raw instrument track; the last case is refused at render. No arrange
  operation adds audio clips. The grammar has `audio clip add`, but it is not in this contract.
- **Lookups are linear**, and every edit resolves the timeline it targets from scratch, base chain
  included. This is fine for thousands of nodes and has not been optimised. `normalizeRefs` and
  `validateIdsAndNames` build hash indexes.
- **Overlap is not checked.** Two clips may overlap on one track, which is also true of the
  format. A conflict across versions, such as a transition outlasting a neighbour that the base
  trimmed, is reported by `resolve` but not prevented. Trimming in the base does refuse when it
  would consume one of the base's *own* transitions.
- **The `beats` timebase is not converted.** `timebase=beats` and its `bpm`/`sig`/`ppq` keys are
  kept as header data. audio-format §1 calls for converting to seconds at load; that is not done.
