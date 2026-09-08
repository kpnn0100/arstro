# Interstellar — Detailed Design

> **Proposed, not as-built.** Every signature below is a *specification* for the first
> implementation, written before the code as `arstro.rule` §3 requires. When a class lands, this
> section is rewritten to its real signatures and constants, and [`requirements.md`](requirements.md)
> gains its `DR-` entry with `file:line` anchors. A constant in this document the source does not
> have is a defect with an id.

Contents: [1. Service](#1-the-service) · [2. Command](#2-command) · [3. Event](#3-event) ·
[4. AppModel](#4-appmodel) · [5. Project & Timeline](#5-project--timeline) ·
[6. ParamRegistry](#6-paramregistry) · [7. Automation](#7-automation) ·
[8. BindingGraph](#8-bindinggraph) · [9. Evaluator](#9-evaluator) · [10. Composite](#10-composite)
· [11. FrameCache](#11-framecache) · [12. RackEmbed](#12-rackembed) ·
[13. Playback & RenderJob](#13-playback--renderjob) · [14. Widgets](#14-widgets)

---

## 1. The service

```cpp
namespace arstro { namespace interstellar {

class InterstellarService
{
public:
    struct Hooks                             // everything the core may not do itself (R-SVC-7)
    {
        std::function<std::unique_ptr<IFrameSource>()> makeFrameSource;   // one per worker
        std::function<std::unique_ptr<IFrameWriter>(const std::string &)> makeFrameWriter;
        std::function<void()> workerInit;      // pins nested OpenMP — PER THREAD (D-12)
        std::function<bool(const std::string &, bool, std::vector<uint8_t> &, int &, int &)>
            stillLoader;                       // the rack's SourceLoader (R-MEM-2's seam)
        std::string configDir;
    };

    explicit InterstellarService(Hooks hooks, ThreadBudget &budget);

    bool dispatch(const Command &c);           // the ONE way in (R-SVC-2)
    void pump(double nowMs);                   // drains workers, acquires frames, emits events
    const AppModel &model() const;             // the ONE way out (R-SVC-3)
    void setEventSink(std::function<void(const Event &)> sink);
    bool takeFrame(RenderService::Frame &out); // the SERVICE polls; one owner only (D-21)

private:
    Hooks       mHooks;
    ThreadBudget &mBudget;                     // a REFERENCE: one budget, three consumers
    Project     mProject;
    Timeline    mTimeline;
    ParamRegistry mRegistry;
    Automation  mAutomation;
    BindingGraph mBindings;
    Evaluator   mEvaluator;
    Composite   mComposite;
    FrameCache  mCache;
    RackEmbed   mRack;                         // owns the hosted CosmoService (R-COSMO)
    Playback    mPlayback;
    std::unique_ptr<RenderJob> mJob;
    AppModel    mModel;
};
}}
```

**`ThreadBudget &`, not a copy or an owned instance.** The host constructs one budget and hands the
same reference to this service and, through `RackEmbed`, to the hosted Cosmo service. Cosmo shipped
the alternative twice: two owners each converting the user's percentage independently gave 13 of 24
cores at a 25% budget and 17 of 16 on a 16-core box (D-11). Interstellar has three consumers.

**`pump(nowMs)` does exactly six things, in this order**, and the order is the D-13 lesson
(*announce before you mutate*):

```
1  drain the rack: mRack.pump(nowMs)         → re-publish cosmo::Events as rack.*
2  drain video decode workers                → completed frames into mCache
3  acquire a finished colour frame           → mModel.frameSeq++, Event{FrameReady}
4  advance playback                          → maybe a new playhead, maybe a dropped frame
5  step the render job, if any               → one frame's worth of work, then yield
6  if anything changed: mModel.revision++    → emit the accumulated events
```

---

## 2. Command

One kind per behaviour, a flat text grammar, a generated parser/formatter (R-SVC-2). The grammar is
[`project-format.md`](project-format.md) §8.

```cpp
struct Command
{
    enum class Kind
    {
        None,
        // project
        ProjectNew, ProjectOpen, ProjectSave, ProjectClose,
        // the rack — every one of these becomes a cosmo::Command (R-COSMO-3)
        RackImport, RackNew, RackPin, RackUnpin, RackAdd, RackGroupNew, RackGroupUngroup,
        RackRename, RackDuplicate, RackFrame, RackSelect,
        // the address space — the workhorse (R-PARAM-1)
        Set,                 // fields: address=value, routed by ParamRegistry::owner
        Get,                 // name = address
        Eval,                // name = address, ms = t, flag = explain
        // the cut
        TrackAdd, TrackSet, ClipAdd, ClipTrim, ClipSplit, ClipMove, ClipDelete, ClipRoll,
        ClipSlip, TransitionAdd, MarkerAdd,
        // automation + bindings
        AutoNew, AutoPoint, AutoPointDelete, AutoLinkAdd, AutoUnlink, AutoFlatten,
        BindSet, BindDelete,
        // transport + delivery
        Playhead, Play, Pause, Stop, Render, RenderCancel, ExportStill,
        // vcs
        Branch, Rebase, Merge,
        // service
        SettingsSet, StatePrint, UiDump, Wait, Api, Quit
    };

    Kind kind = Kind::None;
    std::string path, name;
    int    index = -1;
    double ms    = 0.0;                     // a time, where one is needed
    bool   flag  = false;
    std::vector<std::string> paths;
    std::vector<std::pair<std::string, std::string>> fields;

    bool valid() const { return kind != Kind::None; }
    std::string field(const std::string &key, const std::string &fallback = {}) const;
};

Command     parseCommand(const std::string &line, std::string &err);
std::string formatCommand(const Command &c);
const std::vector<std::string> &commandNames();
/** Every command's argument hints — GENERATED beside the names, not hand-written.
 *  Cosmo's --help proves why: its names come from commandNames() and are always right,
 *  while the hints beside them are hand-maintained and missing for 8 of 30. */
const std::vector<CommandSpec> &commandSpecs();
```

**Note what needs no command of its own**: every scalar, curve, mixer band, grade wheel, mask field,
geometry value and opacity — because `Set` plus the address space reaches all of them, and the
registry says which service owns each. That economy is what keeps the enum readable while the
parameter count runs into the hundreds.

**Rejection is loud** (R-SVC-6). `parseCommand` returns which fields it consumed; an unmatched key
is an error naming the key and, for an address, naming the nearest candidates. Cosmo's D-59 —
`set exposre=1.2` returning success and emitting `params.changed exposre` — is the bug this
signature exists to make impossible.

---

## 3. Event

```cpp
struct Event
{
    enum class Kind
    {
        Info, Error,
        ProjectOpening, ProjectOpened, ProjectSaved, ProjectClosed,
        RackEvent,            // a re-published cosmo::Event; text = its formatted form
        RackPinned, RackConflict,
        TimelineChanged,      // a = node, text = what
        ParamChanged,         // text = address, ms = resolved value
        AutomationChanged, BindingChanged,
        BindingBroken,        // text = address + why — emitted ONCE per render (R-BIND-8)
        LintFinding,          // text = address, ms = t, b = severity
        PlayheadChanged,      // ms = t, a = frame
        FrameReady,           // a = layer, b = width, c = level, ms = render ms
        FrameDropped,         // a = frame, b = consecutive drops
        RenderProgress,       // a = done, b = total, ms = fps
        RenderFinished,       // a = frames, b = failures
        CommandRejected       // text = the line + why; ALSO lands in AppModel::lastError
    };

    Kind kind = Kind::Info;
    std::string text;
    int a = 0, b = 0, c = 0;
    double ms = 0.0;
};

const char *eventName(Event::Kind k);   // stable dotted name: "frame.ready", "rack.event"
std::string formatEvent(const Event &e);
```

```
[evt] project.opening name=cut-a tracks=3 clips=42
[evt] rack.event      cosmo: load.progress done=6 total=18 name=DSC01.MOV
[evt] param.changed   gr1.basic.exposure=0.200
[evt] frame.ready     layer=0 3840x2160 level=1 ms=38.4
[evt] lint.finding    al_ex at=4.000 step=0.500EV fadeIn=0
[evt] render.progress done=1200 total=4428 fps=17.9
```

**A line shape is an interface.** Once this document or an `expect` step quotes one, fields are
appended at the end and never reordered or renamed.

---

## 4. AppModel

```cpp
struct AppModel
{
    unsigned revision = 0;

    Screen screen = Screen::Home;
    std::string projectPath, projectName, branch;
    bool dirty = false;

    /** THE RACK, BY VALUE (R-COSMO-4). Not mirrored, not projected field-by-field: the
     *  Cosmo model IS the rack's state, and a same-shaped Interstellar copy beside it
     *  would be the second copy R-G-3 forbids. Interstellar adds only what Cosmo has no
     *  concept of: a bind name per node, and whether a clip references it. */
    cosmo::AppModel rack;
    struct RackNodeExtra { int node; std::string bindName; bool referenced; };
    std::vector<RackNodeExtra> rackExtra;
    bool rackPinned = false;
    std::vector<std::string> rackConflicts;   // node ids + fields, from a rebase (R-COSMO-6)

    std::vector<TrackModel> tracks;
    std::vector<ClipModel>  clips;
    std::vector<TransitionModel> transitions;
    std::vector<MarkerModel> markers;

    std::vector<AutoClipModel> autoClips;
    std::vector<AutoLinkModel> autoLinks;
    std::vector<BindingModel>  bindings;      // target, expr, deps, broken

    /** The resolved value of every address the CURRENT frame used (R-EVAL-3). This is
     *  what makes automation and bindings inspectable at all — a number the renderer
     *  used and nobody can read back is the R-CPU-4 mistake, and this app computes
     *  hundreds of them per frame. */
    std::vector<std::pair<std::string, double>> resolved;

    double playhead = 0.0;                    // seconds — the SERVICE's value; the view eases to it
    long long playheadFrame = 0;
    double duration = 0.0;
    bool playing = false;

    /** Frame METADATA only, never pixels (R-SVC-3 rule 1): a 4K frame is 33 MB. */
    int frameLayer = -1, frameWidth = 0, frameHeight = 0;
    unsigned frameSeq = 0;
    int frameLevel = 0, frameLevelEdge = 0;   // MACHINE-DEPENDENT → excluded from the stable dump
    int droppedFrames = 0;                    //      "                    "
    double msPerMegapixel = 0.0;              //      "                    "

    RenderModel   render;                     // active, done, total, fps, outPath, failures
    std::vector<LintFinding> lint;
    BudgetModel   budget;                     // percent, cores, engine/decode/job threads, peak
    CacheModel    cache;                      // residentBytes, entries, hits, misses, evictions
    Settings      settings;
    std::string   lastError;
};
```

**The stable-dump exclusion list is part of the contract** (R-SVC-9). Excluded:
`frameSeq`, `frameLevel`, `frameLevelEdge`, `droppedFrames`, `msPerMegapixel`, every measured `ms`,
`budget.peak*`, and every `cache.*` counter — each is a property of *when and where* the dump was
taken rather than of the state. Two services given the same commands on a fast and a slow box are in
the same *state* while showing different proxy levels, which is the entire point of the proxy
mechanism. **Excluding a field to make a test pass is falsifying the test**, so this list changes
only with a line of justification.

---

## 5. Project & Timeline

```cpp
class Project
{
public:
    bool load(const std::string &path, std::string &err);   // through nebula
    bool save(const std::string &path, std::string &err);   // CANONICAL text (R-FMT-4)
    /** parse -> serialize is a FIXED POINT. This is the P1 gate and the cheapest test
     *  in the project: load, save to a string, load that, save again, compare bytes. */
    static bool roundTripsExactly(const std::string &text);

    const std::string &bindName(NodeId) const;
    /** Renaming rewrites EVERY expression that references the old name, atomically, and
     *  re-validates the binding graph before committing (R-PARAM-2). A rename that leaves
     *  a stale name in an expression is worse than one that refuses. */
    bool rename(NodeId, const std::string &newName, std::string &err);
};

class Timeline
{
public:
    static constexpr double kSnapThresholdPx = 8.0;   // view-supplied px → seconds by the view

    struct Clip { NodeId id; NodeId track; std::string src;  // "rack:<cosmoNode>"
                  double at, in, out, speed; Fit fit;
                  double opacity; Blend blend; Geom geom; };

    bool add(const Clip &c, std::string &err);
    bool trim(NodeId clip, double newIn, double newOut, std::string &err);
    bool split(NodeId clip, double at, std::string &err);   // → two clips, ids assigned, never reused
    bool move(NodeId clip, double at, NodeId track, std::string &err);
    bool roll(NodeId a, NodeId b, double dt, std::string &err);
    bool slip(NodeId clip, double dt, std::string &err);
    bool remove(NodeId clip, bool ripple, std::string &err);

    /** Which clips are visible at `frame`, bottom track first — the composite order.
     *  Computed from an index rebuilt on mutation, not scanned per frame: a 4 000-clip
     *  timeline scanned per frame at 24 fps is 96 000 comparisons a second for an answer
     *  that changes only when the user cuts. */
    void activeClipsAt(long long frame, std::vector<ActiveClip> &out) const;

    /** REJECTS a colour field rather than ignoring it (R-CUT-2): ignoring it would
     *  silently discard the user's edit, which is the worst of the three options. */
    static bool validateClipFields(const FieldList &, std::string &err);
};
```

---

## 6. ParamRegistry

The spine of R-PARAM, R-AUTO, R-BIND and R-SVC-10 — and the router that decides which service owns
a write.

```cpp
class ParamRegistry
{
public:
    enum class Type  { Float, Int, Bool, Enum, Curve, Colour, Point, Ref };
    enum class Owner { Interstellar, Cosmo };

    struct Entry
    {
        std::string pattern;          // "rack.<obj>.basic.exposure", "clip.<obj>.geom.scale"
        Type   type   = Type::Float;
        std::string unit;             // "ev" "%" "px" "deg" "K" "frames" "s" ""
        double min = 0, max = 0, def = 0;
        bool   automatable = true;
        bool   bindable    = true;
        bool   readOnly    = false;
        Owner  owner = Owner::Interstellar;
        std::string cosmoKey;         // the EditParamsIO key, when owner == Cosmo
    };

    struct Resolved { const Entry *entry; NodeId object; int component; std::string cosmoKey; };

    /** Resolve a dotted address. On failure fills `err` WITH THE NEAREST CANDIDATES —
     *  `gr1.basic.exposer` is a typo a human makes weekly and the address space is too
     *  large to eyeball (R-PARAM-5). */
    bool resolve(const std::string &address, Resolved &out, std::string &err) const;

    const std::vector<Entry> &all() const;    // feeds ApiDoc and every UI panel builder
};
```

**Generated, from the same tables the codec uses** (R-PARAM-3). Cosmo's leaf names come from
`EditParamsIO`'s own key list, so a preset, a `.cmp`, a `cosmo-cc set` line and an Interstellar
expression all spell `exposure` identically (R-PARAM-4). A hand-written registry would be wrong
within a week and an agent reading the API document would build scripts on parameters that do not
exist.

---

## 7. Automation

```cpp
struct AutoClip                                   // the reusable SHAPE (R-AUTO-1)
{
    NodeId id; std::string name;
    double dur = 1.0;
    enum class Interp { Linear, Bezier, Hold, Step } interp = Interp::Bezier;
    struct Point { double t, value; Easing ease; };
    std::vector<Point> points;                    // sorted by t, unique t

    /** Analytic, never a resampled table: the same shape must give the same number at
     *  24 fps, at 48 fps, in a proxy and in a master. A lookup table would make the
     *  frame rate a parameter of the picture. Clamps outside [0, dur]. */
    double value(double localT) const;
};

struct AutoLink                                   // applying a shape to an address (R-AUTO-2)
{
    NodeId id; std::string name;
    NodeId clip;                                  // the AutoClip
    std::string target;                           // the address
    double at = 0, dur = 0;                       // dur == 0 → the shape's own dur
    bool   haveFromTo = true;
    double from = 0, to = 1, scale = 1, offset = 0;
    enum class Mode { Absolute, Add, Multiply } mode = Mode::Absolute;
    NodeId scope = kNoNode;                       // a clip → `at` is CLIP-LOCAL (R-AUTO-7)
    int fadeIn = 0, fadeOut = 0;                  // frames (R-AUTO-5)
};

class Automation
{
public:
    bool addLink(const AutoLink &l, std::string &err);
    /** REFUSES an overlap on one address (R-AUTO-4): two producers for one value is a
     *  picture that depends on evaluation order, and a renderer whose output depends on
     *  evaluation order cannot be tested. */
    bool wouldOverlap(const std::string &address, double at, double dur, NodeId ignore) const;

    /** value(address, t) → {active, value} after time mapping, value mapping, mode and
     *  fade. `staticValue` is what `Add`/`Multiply` compose onto and what a fade ramps
     *  from — never another link's output (there is never a second link). */
    struct Sample { bool active; double value; };
    Sample sample(const std::string &address, double t, double staticValue) const;

    /** Lanes: the MIXER's projection — one row per automated address, its links in time
     *  (R-AUTO-3). A view of the links, not a stored structure (R-G-3). */
    std::vector<Lane> lanes(NodeId object) const;

    /** Every link whose first mapped value differs from the static value with fadeIn == 0
     *  (R-AUTO-5). A hard change the user asked for is legitimate; one they did not notice
     *  is a defect they will blame on the renderer. */
    std::vector<LintFinding> lintBoundaries(const Evaluator &) const;
};
```

---

## 8. BindingGraph

```cpp
class BindingGraph
{
public:
    /** Compiles the expression, folds it, collects its dependencies, and REFUSES a cycle
     *  naming both ends (R-BIND-4). At EDIT time, not at render time: a bad expression
     *  must be refused while the user is looking at it, not on frame 4 800 of 12 000. */
    bool set(const std::string &target, const std::string &expr, std::string &err);
    bool remove(const std::string &target);

    /** Topological order, recomputed on change and not per frame. */
    const std::vector<const Binding *> &evaluationOrder() const;

    /** The Gene Scope: two lookups, one for a dotted address, one for a built-in
     *  (`t` `frame` `fps` `dur` `project.*` `<clip>.local` `<clip>.progress`). */
    gene::Scope makeScope(const ResolvedValues &, double t) const;

    struct Binding { std::string target, expr; gene::NodePtr ast;
                     std::vector<std::string> deps;   // DERIVED, never stored (R-G-3)
                     bool broken = false; };
};
```

**Interstellar exposes neither `raw{}` nor Gene's colour type nor `emitCpp`** — there is no code
generation here, an unevaluatable escape would make a project unrenderable, and a colour parameter
is bound component-wise so colour arithmetic cannot arise (R-BIND-2).

---

## 9. Evaluator

```cpp
class Evaluator
{
public:
    /** Steps 1-4 of binding.md §6 — time, automation, bindings, rack composition.
     *  A PURE function of (project, t). No global state, no cache that survives a
     *  parameter change, no dependence on evaluation history: that purity is what
     *  R-NFR-1 rests on, and therefore what golden-frame tests rest on. */
    ResolvedValues resolve(double t) const;

    /** One address, for `eval --at` and for the resolved-value table test (R-AUTO-9).
     *  `--explain` fills `trace` with each input and each intermediate — an expression
     *  whose value a user cannot account for is a support problem, and a cut accumulates
     *  hundreds of them. */
    bool value(const std::string &address, double t, double &out,
               std::vector<std::string> *trace = nullptr) const;

    /** Step 4: the per-layer EditParams a frame renders with — the rack node's own
     *  resolved values composed up its ancestors, weighted by each ancestor's opacity
     *  and skipping bypassed ones. `composeParams` is COSMO's function, unchanged. */
    EditParams effectiveParamsFor(NodeId rackNode, const ResolvedValues &) const;

    /** The frame-cache key's parameter component: a hash over EVERY value step 4 produced
     *  for this layer. Hashing only the EditParams struct would miss a change to a shape
     *  a link maps into it — which is the exact stale-frame bug the key exists to prevent
     *  (R-PLAY-3). */
    uint64_t paramHash(NodeId rackNode, const ResolvedValues &) const;
};
```

---

## 10. Composite

```cpp
class Composite
{
public:
    enum class Blend { Normal, Multiply, Screen, Overlay, Add, Subtract, Difference };
    enum class Fit   { Contain, Cover, Stretch, None };

    /** Steps 6-8. Layers arrive already GRADED (step 5) — a composite is a composite of
     *  graded frames and never of raw ones (R-COMP-1). */
    void compose(const std::vector<Layer> &bottomToTop, const OutputSpec &, Frame &out) const;

    /** geom.crop → fit → scale/rotate about anchor → translate, into the output raster.
     *  Colour ran BEFORE this (R-EVAL-2), so a reframe can never change a pixel's colour
     *  by changing which pixels the colour stages saw. That is also why a source's
     *  xform.crop and a clip's geom.crop are different addresses (R-COMP-3). */
    void placeLayer(const Frame &graded, const Geom &, Fit, const OutputSpec &, Frame &out) const;

    /** A transition is a two-layer mix, resolved here rather than in the timeline: a
     *  dissolve is a LINEAR alpha ramp, because a dissolve eased in time reads as a
     *  luminance bump in the middle (Cosmo's photo dissolve is Linear for the same
     *  reason). */
    void resolveTransition(const Transition &, double t, const Frame &a, const Frame &b,
                           Frame &out) const;
};
```

---

## 11. FrameCache

```cpp
class FrameCache
{
public:
    struct Key { int layer; long long sourceFrame; uint64_t paramHash; int level; };

    void setCapBytes(size_t bytes);            // BYTES, not entries: a 4K frame is 33 MB
    bool get(const Key &, Frame &out);
    void put(const Key &, Frame &&);
    /** Invalidate by RANGE and by layer, so a cut at 40 s does not throw away the frames
     *  around 4 s. A whole-cache flush on every edit turns a scrub into a re-render. */
    void invalidate(int layer, long long fromFrame, long long toFrame);

    size_t residentBytes() const; size_t entries() const;
    unsigned hits() const; unsigned misses() const; unsigned evictions() const;
};
```

Every counter is published in the model (R-NFR-5). "Memory is bounded" and "the cache works" are
both claims this project has learned not to make without a number a front end can read
(R-CPU-4, R-MEM-4).

---

## 12. RackEmbed

The class the whole design turns on. Small on purpose.

```cpp
class RackEmbed
{
public:
    /** Cosmo's real surface, checked against the source rather than invented:
     *      explicit cosmo::CosmoService(ThreadBudget &budget)
     *      setDecoderFactory(ProjectLoader::DecoderFactory)
     *      setWorkerInit(std::function<void()>)      // the per-thread OpenMP pin (D-12)
     *      setImageWriter(ImageWriter)
     *      subscribe(EventSink)  applySettings(const AppSettings &)
     *      dispatch(Command)  dispatchText(line, err)  pump(nowMs)  model()  takeFrame(Frame &)
     *  The budget arrives BY REFERENCE, which is R-COSMO-8's core already satisfied. */
    RackEmbed(ThreadBudget &budget,
              cosmo::ProjectLoader::DecoderFactory decoder,
              std::function<void()> workerInit);

    bool import(const std::string &cmpPath, const std::string &branch, std::string &err);
    bool createEmpty(std::string &err);
    bool pin(const std::string &commit, std::string &err);
    bool unpin(std::string &err);
    bool isPinned() const;

    /** A colour edit from ANY Interstellar front end. Becomes exactly the pair of
     *  cosmo::Commands `cosmo-cc` would send — a Select of the node, then a Set of the
     *  key — so the GUI, the CLI and the socket share one path and there is no
     *  privileged route (R-COSMO-3, R-G-4). Refused when pinned, naming the commit
     *  (R-COSMO-5): a pin that yields is not a pin. */
    bool setColour(NodeId rackNode, const std::string &cosmoKey, const std::string &value,
                   std::string &err);

    bool save(std::string &err);          // dispatches cosmo::Command{ProjectSave}
    void pump(double nowMs);              // drains Cosmo and RE-PUBLISHES its events as rack.*
    const cosmo::AppModel &rackModel() const;
    /** Step 5's render path, and the ONE place Interstellar reaches past the service seam.
     *  Named, narrow and counted — because Cosmo's equivalent (`CosmoService::session()`) is
     *  documented in its own header as "transitional… every use is a line still to delete",
     *  and its ledger counts them. Interstellar must not grow a second such list: this is
     *  the only member allowed to use it, and the pixel-cap plumbing that would otherwise
     *  need it is a P0.2 change in Cosmo instead. */
    arstro::RenderService &renderServiceForFrames();

private:
    cosmo::CosmoService mCosmo;           // a REAL service — no window, no widgets (R-COSMO-9)
    std::function<void(const Event &)> mSink;
};
```

Five properties are requirements, not implementation choices:

1. **Commands only.** No `EditSession` call, no `EditParams` mutation, no read of Cosmo's private
   state. The single exception is `renderServiceForFrames()`, because step 5 needs the engine — and
   it is named so a `grep` finds every use. Cosmo's own `session()` accessor is the cautionary
   precedent: its header calls it transitional and its ledger counts the remaining call sites
   (105 against 7 dispatches). One named member is a seam; a hundred unnamed ones are the absence
   of one.
2. **Events re-published, never swallowed.** An edit visible in one channel and invisible in another
   is the failure R-SVC-5 exists to prevent.
3. **The Cosmo model is exposed by reference and stored by value in `AppModel`** — never copied
   field by field (R-COSMO-4).
4. **The budget, the decoder and the memory caps are Interstellar's**, injected (R-COSMO-8).
5. **Interstellar never writes `.cmp` bytes.** Saving the rack is a `cosmo::Command`, so there is
   one writer and one format authority.

---

## 13. Playback & RenderJob

```cpp
class Playback
{
public:
    static constexpr double kMinLevelHoldMs = 500.0;   // don't thrash the proxy level
    static constexpr int    kMaxConsecutiveDrops = 8;  // beyond this, drop the level instead

    void play(); void pause(); void seek(double t);
    void advance(double nowMs);           // the clock; picks the level; drops rather than stalls

    /** Read-ahead bounded BY BYTES, not by frame count — a 4K frame is 33 MB and a
     *  20-frame lookahead is 660 MB. */
    void setReadAheadBytes(size_t);

    int  droppedFrames() const; int level() const;    // both PUBLISHED (R-NFR-4)
};

class RenderJob
{
public:
    struct Spec { double from, to; OutputSpec out; bool lint; };

    void start(const Spec &, std::unique_ptr<IFrameWriter>);
    /** ONE frame's work per call, then yield — so a render never blocks the UI thread and
     *  a cancel is honoured within a frame. Cancel and resume are at a FRAME boundary,
     *  which is also what makes a resumable render meaningful. */
    bool step();
    void cancel();
    Progress progress() const;            // done, total, measured fps, ETA
};
```

**`step()` renders one frame per call rather than looping.** Cosmo's batch exporter does the same
thing for the same reason (R-EXPORT-6: one image per main-loop step), and it is what makes progress
events honest instead of arriving in a burst at the end.

---

## 14. Widgets

Conventions are `arstro.design.rule` §5 and Cosmo's `widgets/` — one class per file, filename ==
class name, a block comment saying what it is and why, `layout()` non-virtual and called every
frame, callbacks as public `std::function`s named `onVerb`, a setter that never fires its own
callback, published geometry a test can aim at. Only what is *specific to Interstellar* is below.

### 14.1 `Monitor` (R-UI-2)

```cpp
class Monitor : public artboard::Segment
{
public:
    void setFrame(RenderService::Frame &&f);      // registers the image with the target
    void setLevel(int level, int levelEdge);      // a coarse frame is UPSCALED into the same rect
    artboard::Rect frameRect() const;             // published: a shot and a test aim here
    std::function<void(double, double)> onPick;   // normalised — the white-balance picker
};
```

Two traps, both already paid for elsewhere. **A new image every frame must be registered with the
persistent target and released**, or the target grows without bound — Cosmo's target persists across
frames for exactly this reason. And **a level change is not a content change**: switching proxy
level must not cross-dissolve the two frames (design-rule gotcha 10 — "a zoom is not a content
change"), because the two are the same picture at two resolutions and dissolving them reads as a
double exposure. Set both views instead.

### 14.2 `TimelineView` (R-UI-3)

```cpp
class TimelineView : public artboard::Segment
{
public:
    static constexpr double kTrackH   = 48.0;
    static constexpr double kRulerH   = 22.75;    // Cosmo's Breadcrumb height — same rhythm
    static constexpr double kMinClipW = 6.0;      // below this a clip is a tick, still hittable

    void setPixelsPerSecond(double);              // an ANIMATED property: a zoom must ease
    double pixelsPerSecond() const;               //   … read the LIVE value, never the target
    artboard::Rect clipRect(NodeId) const;        // published geometry

    std::function<void(NodeId, double, NodeId)> onClipMoved;   // clip, at, track
    std::function<void(NodeId, double, double)>  onClipTrimmed;
    std::function<void(double)> onScrub;
};
```

- **A drag must never teleport**: on `Down`, store `grab = clipStart − pointerTime`; on `Drag`, add
  it back (design-rule §5).
- **A gesture in flight outranks the model**: `setClips()` early-returns while a drag is live, or the
  model's refresh every few tens of ms flattens the clip being dragged.
- **Snapping is a view concern** (playhead, clip edges, markers) and the *snapped* value is what the
  command carries — the service must never receive an unsnapped value and re-derive it.
- **The zoom is animated and every geometric read goes through the eased value, re-derived per
  frame.** Deriving once zooms the ruler and leaves the clips behind — gotcha 19, and the UI-scale
  defect it comes from was written by someone who had just read the rule.
- **No lanes here, and no colour here.** Putting an automation lane under a clip is what makes people
  believe the automation belongs to the clip, and R-AUTO-1 says it does not.

### 14.3 `LaneStack` (R-UI-4)

```cpp
class LaneStack : public artboard::Segment
{
public:
    static constexpr double kLaneH        = 34.0;
    static constexpr double kLaneHExpanded = 96.0;   // an eased height, not a swap
    void setLanes(std::vector<Lane>);
    artboard::Rect linkRect(NodeId link) const;
    std::function<void(NodeId, double)> onLinkMoved;
    std::function<void(NodeId, int, double, double)> onPointMoved;   // link, point, t, value
};
```

- **It shares the timeline's time axis**, and shares it by *deriving from the same animated
  `pixelsPerSecond` and scroll offset* — two copies of one fact drift, and the symptom is a lane
  that disagrees with the cut above it by three pixels (gotcha 15).
- **Expanding a lane eases its height**; it does not swap between two heights.
- **The discontinuity at a link boundary is drawn** (R-AUTO-5), because a lint finding nobody can
  see is a lint finding that does not work.
- **It clips *and* scrolls**, clamped both ends, one viewport rectangle shared by the measure, the
  row placement, the visibility test, the paint clip and the scrollbar (R6, and the "unreachable
  last row" failure it comes from).

### 14.4 `ExpressionEditor` (R-UI-5)

```cpp
class ExpressionEditor : public artboard::Segment
{
public:
    void setAddresses(const std::vector<std::string> &);   // from ParamRegistry::all()
    void setResolved(double v);                            // the live value, beside the field
    std::function<bool(const std::string &, std::string &)> validate;   // → BindingGraph::set
    std::function<void(const std::string &)> onCommit;
};
```

Completion, inline validation and the live resolved value are requirements rather than niceties: the
address space is too large to type from memory, and a cycle or a typo must be refused while the user
is looking at it. `validate` returns `bool` **and** an error string — a callback that must report
success returns `bool` so a two-hop channel cannot drop the failure silently.

### 14.5 The states that get skipped, and must not be

Every widget above ships with **empty** and **loading** drawn and shot (R-UI-7,
`arstro.design.rule` §7):

| widget | empty | loading |
|---|---|---|
| `Monitor` | "no clip at the playhead" — not a black frame, which reads as a bug | "decoding" + the level it is waiting for |
| `TimelineView` | "drag a source here" with the source bin highlighted | the ruler drawn, tracks greyed, no clips yet |
| `LaneStack` | "no automation on gr1 — pick a parameter to automate" | — |
| `SourceBin` | "the rack is empty — add footage" | Cosmo's own per-entry spinner cells |
| `BindingInspector` | "no bindings" | — |

A list empty because nothing matched and a list empty because it has not loaded look identical, and
they ask the user for different things. Say which, in words, next to the control.
