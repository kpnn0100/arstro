/*
 *  interstellar_core — Project: the `.isp` document. Nine node types, stable ids, bind names,
 *  and a CANONICAL text form (R-FMT).
 *
 *  Two rules shape this file and neither is tidiness:
 *
 *  1. **There is no colour here.** No EditParams, no curve, no mixer, no LUT reference. A clip
 *     names a node in the embedded Cosmo project and the colour is that node's (R-COSMO-2).
 *     `validateClipFields` REJECTS a colour key rather than ignoring it, because Nebula's
 *     unknown-key tolerance is for keys from the future and a `grade=` on a clip is a user's
 *     edit this model cannot honour — ignoring it would discard their work silently (R-CUT-2).
 *  2. **parse -> serialize is a fixed point, byte for byte** (R-FMT-4). A commit is content
 *     hashed and "no change" must be literally no diff, so node order, field order and number
 *     formatting are all defined rather than incidental. Unknown keys are preserved in place.
 *
 *  A node is identified by its `id` (the merge anchor, never reused) and named by its `name`
 *  (the bind name an expression spells — R-PARAM-2).
 */
#pragma once
#include <map>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
    using NodeId = std::string;

    enum class Blend { Normal, Multiply, Screen, Overlay, Add, Subtract, Difference };
    enum class Fit { Contain, Cover, Stretch, None };
    enum class Interp { Linear, Bezier, Hold, Step };
    enum class AutoMode { Absolute, Add, Multiply };
    enum class Ease { Linear, EaseIn, EaseOut, EaseInOut };

    const char *blendName(Blend b);
    const char *fitName(Fit f);
    const char *interpName(Interp i);
    const char *autoModeName(AutoMode m);
    const char *easeName(Ease e);
    bool parseBlend(const std::string &s, Blend &out);
    bool parseFit(const std::string &s, Fit &out);
    bool parseInterp(const std::string &s, Interp &out);
    bool parseAutoMode(const std::string &s, AutoMode &out);
    bool parseEase(const std::string &s, Ease &out);
    /** The eased 0..1 fraction. Shared by the automation sampler and the transition resolver so
     *  one curve family serves both — two implementations of "easeInOut" would drift. */
    double applyEase(Ease e, double t);

    /** Fields a node carried that this build does not know. Preserved verbatim and in order, so
     *  a newer file opens in an older build without losing data (R-FMT-4). */
    using UnknownFields = std::vector<std::pair<std::string, std::string>>;

    struct Geom
    {
        double x = 0, y = 0, scale = 1.0, rotation = 0;
        double anchorX = 0.5, anchorY = 0.5;
        double cropX = 0, cropY = 0, cropW = 1, cropH = 1;
    };

    struct Track
    {
        NodeId id, name;
        bool audio = false;
        int order = 0;
        bool mute = false, lock = false;
        double opacity = 1.0, gain = 0.0;
        Blend blend = Blend::Normal;
        UnknownFields unknown;
    };

    struct Clip
    {
        NodeId id, name, track;
        int order = 0;
        std::string src;            // "rack:<cosmoNode>" (R-COSMO-2)
        double at = 0, in = 0, out = 0, speed = 1.0;
        Fit fit = Fit::Contain;
        double opacity = 1.0;
        Blend blend = Blend::Normal;
        Geom geom;
        UnknownFields unknown;
        /** Timeline length in seconds. `speed` divides, so a 2x clip occupies half the time. */
        double duration() const { return speed != 0.0 ? (out - in) / speed : 0.0; }
        double end() const { return at + duration(); }
    };

    struct Transition
    {
        NodeId id, name, track, clipA, clipB;
        std::string kind = "dissolve";   // dissolve | dip
        double dur = 0.5;
        Ease easing = Ease::Linear;      // a dissolve is a LINEAR alpha ramp — design.md §6
        std::string color = "#000000";
        UnknownFields unknown;
    };

    struct AutoPoint { double t = 0, value = 0; Ease ease = Ease::Linear; };

    /** The reusable SHAPE. It knows nothing about which parameters use it, which is the whole
     *  reason one shape can drive several (R-AUTO-1). */
    struct AutoClip
    {
        NodeId id, name;
        double dur = 1.0;
        Interp interp = Interp::Bezier;
        std::vector<AutoPoint> points;   // sorted by t, unique t
        UnknownFields unknown;
        /** Analytic, never a resampled table: the same shape must give the same number at 24 and
         *  at 48 fps, in a proxy and in a master. Clamps outside [0, dur]. */
        double value(double localT) const;
    };

    /** Applying a shape to an address: where in time, into what range, and how it meets the
     *  static value (R-AUTO-2). */
    struct AutoLink
    {
        NodeId id, name, clip;
        std::string target;              // the parameter address
        double at = 0, dur = 0;          // dur == 0 -> the shape's own dur
        bool haveFromTo = true;
        double from = 0, to = 1, scale = 1, offset = 0;
        AutoMode mode = AutoMode::Absolute;
        NodeId scope;                    // a clip id -> `at` is CLIP-LOCAL (R-AUTO-7)
        int fadeIn = 0, fadeOut = 0;     // frames
        UnknownFields unknown;
    };

    struct Binding
    {
        NodeId id;
        std::string target, expr;        // dependencies are DERIVED, never stored (R-G-3)
        UnknownFields unknown;
    };

    struct Marker
    {
        NodeId id, name;
        double at = 0;
        std::string color = "#4F7EF7", note;
        UnknownFields unknown;
    };

    struct Embed
    {
        NodeId id, name;
        std::string role;                // "rack" | "audio"
        std::string target, path, branch = "main", writeBranch, track;
        double offset = 0;
        UnknownFields unknown;
        /** `branch=main@<commit>` is a PIN, and a pin is read-only everywhere (R-VCS-6). */
        bool pinned() const { return branch.find('@') != std::string::npos; }
        std::string pinCommit() const
        {
            const auto at = branch.find('@');
            return at == std::string::npos ? std::string() : branch.substr(at + 1);
        }
    };

    /** Interstellar's OWN parameters for a node of the embedded Cosmo rack.
     *
     *  Added 2026-09-11, during implementation, because two things the specification assumed
     *  turned out to need somewhere to live and the `.cmp` is not it (the rack owns COLOUR, and
     *  only colour — R-COSMO-2):
     *
     *  * **the bind name.** Cosmo names a node after its file or after what the user typed for a
     *    group, so `"Tokyo Night"` and `"DSC01.MOV"` are both normal — and neither is a legal
     *    address (R-PARAM-2). An expression needs `gr1`, so the bind name is Interstellar's and
     *    lives here.
     *  * **the grade weight** (`<name>.opacity`): the weight with which this node's own
     *    parameter offsets apply to its descendants — a continuous `bypass`. It is what the
     *    user's `bind gr1.opacity = …` example addresses, it is composited by Interstellar in
     *    step 4, and Cosmo has no concept of it. */
    struct RackObj
    {
        NodeId id, name;
        std::string node;          // the Cosmo node id this names
        double opacity = 1.0;      // the grade weight — see above
        UnknownFields unknown;
    };

    struct ProjectSettings
    {
        int proxyEdge = 1280;
        int cpuPercent = 50;
        long long cacheBytes = 2147483648LL;
        bool lintOnRender = true;
        UnknownFields unknown;
    };

    class Project
    {
    public:
        // ── header ──
        std::string id, name;
        double fps = 24.0;
        int width = 3840, height = 2160;
        double par = 1.0;
        std::string colorspace = "rec709";
        UnknownFields headerUnknown;

        std::vector<Track> tracks;
        std::vector<Clip> clips;
        std::vector<Transition> transitions;
        std::vector<AutoClip> autoClips;
        std::vector<AutoLink> autoLinks;
        std::vector<Binding> bindings;
        std::vector<Marker> markers;
        std::vector<Embed> embeds;
        std::vector<RackObj> rackObjs;
        ProjectSettings settings;

        // ── text I/O (R-FMT) ──
        /** Parse `text`. False + `err` on a structural error; a non-finite number is REPAIRED to
         *  the field default and counted in `repaired` instead of refusing the file — one stray
         *  `nan` must not make a project unopenable (cosmo's D-36). The two behaviours differ on
         *  purpose: a structural error refuses, a numeric corruption repairs and reports. */
        bool parse(const std::string &text, std::string &err, int *repaired = nullptr);
        std::string serialize() const;
        bool load(const std::string &path, std::string &err, int *repaired = nullptr);
        bool save(const std::string &path, std::string &err) const;

        /** The P1 gate, and the cheapest test in the project: parse -> serialize is a fixed
         *  point. Kept here rather than in the suite so any front end can assert it. */
        static bool roundTripsExactly(const std::string &text, std::string &err);

        // ── lookup. Ids are unique; a bind name is unique too (R-PARAM-2). ──
        Track *track(const NodeId &);           const Track *track(const NodeId &) const;
        Clip *clip(const NodeId &);             const Clip *clip(const NodeId &) const;
        AutoClip *autoClip(const NodeId &);     const AutoClip *autoClip(const NodeId &) const;
        AutoLink *autoLink(const NodeId &);     const AutoLink *autoLink(const NodeId &) const;
        RackObj *rackObj(const NodeId &);       const RackObj *rackObj(const NodeId &) const;
        /** The RackObj naming a given Cosmo node, or nullptr. */
        const RackObj *rackObjForNode(const std::string &cosmoNode) const;
        /** Give `cosmoNode` a bind name if it has none yet, deriving one from `hint`, and return
         *  it. Idempotent: the name is stable once assigned, because an expression spells it. */
        RackObj &ensureRackObj(const std::string &cosmoNode, const std::string &hint);
        const Embed *rackEmbed() const;         Embed *rackEmbed();
        /** Resolve a BIND NAME to a node id, over every node type. The name is the user's
         *  language and the id is everyone else's. */
        bool idForName(const std::string &name, NodeId &out) const;
        bool nameIsTaken(const std::string &name) const;

        /** A fresh id with `prefix`, unique in this project and never reused within it
         *  (R-FMT-5). */
        NodeId freshId(const std::string &prefix);
        /** A bind name derived from `base`, unique, and legal per R-PARAM-2. */
        std::string freshName(const std::string &base) const;
        /** `[A-Za-z_][A-Za-z0-9_]{0,31}`, and not a reserved word. */
        static bool nameIsLegal(const std::string &name, std::string &why);

        /** Rename a node and REWRITE every expression that references it, atomically — a rename
         *  that leaves a stale name in an expression is worse than one that refuses
         *  (R-PARAM-2). Also rewrites automation targets. */
        bool rename(const NodeId &id, const std::string &newName, std::string &err);

        /** Project length: the furthest clip end, and at least one frame. */
        double duration() const;
        long long frameAt(double seconds) const;
        double secondsAt(long long frame) const;

        /** Reject a colour field on a clip, naming it (R-CUT-2). Public so the command layer and
         *  the parser share one answer. */
        static bool fieldIsColour(const std::string &key);

    private:
        long long mNextId = 1;
    };

    /** Canonical number text: the shortest decimal that reads back bit-identically, with a `.0`
     *  kept on an integral float so a field's type is legible. Times use 3 decimal places.
     *  One function, because two would drift and "no change is no diff" rests on it. */
    std::string canonicalNumber(double v);
    std::string canonicalTime(double v);
    /** Quote only when the value would not survive unquoted. */
    std::string quoteIfNeeded(const std::string &v);
}
}
