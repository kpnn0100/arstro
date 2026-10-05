/*
 *  interstellar_model — Project: the `.isp` document as typed data (R-FMT, R-VER, R-TL, R-AUD).
 *
 *  Normative: `docs/project-format.md` (every node, §6 canonical form, §7 validation) and the
 *  suite's `docs/audio-format.md` (§4: which audio nodes Interstellar implements).
 *
 *  Four rules shape this file, and none of them is tidiness:
 *
 *  1. **There is no colour here.** A clip names a `#rackobj` and the colour is that rack node's, in
 *     the `.cmp` (R-RACK-1). A colour key on a `#clip` is REFUSED, naming the key, because ignoring
 *     it would silently discard a user's edit (R-TL-2). The one colour-shaped node is `#tlgrade`,
 *     and it is a delta on the rack's value, never a value.
 *  2. **parse -> serialize is a byte-exact fixed point.** Node order, field order, number spelling
 *     and quoting are all defined, so "no change" is literally no diff and a content hash means
 *     something. Unknown keys and comments survive in place.
 *  3. **What this build cannot render, it still carries.** Solaris's audio nodes (instrument and
 *     bus tracks, note clips, racks, automation, sends) and any node type from a newer build are
 *     kept as a `RawNode` and written back VERBATIM — a Solaris project is not damaged by being
 *     opened here. They are listed by `unrenderable()` so a render can refuse with them named
 *     rather than produce a mix quietly missing a part ("parsed, preserved, refused").
 *  4. **A structural error refuses; a numeric corruption repairs.** Two racks, a base cycle, a clip
 *     with no frames: refused, because guessing would be worse than stopping. A stray `nan`:
 *     repaired to the field default and counted, because one bad byte must not make a project
 *     unopenable.
 *
 *  References between nodes are stored as IDS. A hand-written file may spell a reference with a
 *  bind name (`src=s_day01`); the parser resolves it to the id, so the canonical text — and every
 *  comparison in the version logic — has one spelling. Ids never change, which is what lets
 *  `rename` be a one-field edit plus a rewrite of the verbatim text nobody else normalises.
 *
 *  The version logic (resolve, deltas, freeze/thaw, rebase) is in `Versions.h`; the cut
 *  operations are in `Arrange.h`. This file is only the document.
 */
#pragma once
#include "Anim.h"
#include "../core/service/AppModel.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace interstellar
{
    /** Ordered `key = value` text. Ordered because the file is read by people and diffed by
     *  machines; a map would re-sort a user's keys and turn "no change" into a diff. */
    using Fields = std::vector<std::pair<std::string, std::string>>;

    /** The text around a node that is not data. Kept because a comment is a user's note to
     *  themselves and a format that eats them is a format people stop annotating. */
    struct Notes
    {
        std::string inlineComment;          // "; …" written on the node's first line
        std::vector<std::string> after;     // whole comment lines that followed the node
    };

    // ── §2 the rack ───────────────────────────────────────────────────────────────────────────

    /** The `#rack` node: WHERE the hosted Cosmo project is. At most one — two would be two colour
     *  authorities. Named RackRef, not Rack, because `interstellar::Rack` is the hosted
     *  CosmoService itself (core/Rack.h) and the service links both libraries into one namespace:
     *  this is the reference, that is the thing. */
    struct RackRef
    {
        NodeId id;
        std::string path, branch = "main", writeBranch;
        Fields unknown;
        Notes notes;
    };

    /** Interstellar's own data ABOUT a Cosmo node — never colour (project-format §2). */
    struct RackObj
    {
        NodeId id, name;                    // name = the bind name an address spells (R-RACK-6)
        std::string node;                   // the Cosmo node id
        std::string kind = "source";        // group | source
        double weight = 1.0;                // the grade weight, a continuous bypass (R-RACK-4)
        std::string media;                  // the source file the timeline decodes
        double frame = 0.0;                 // the reference frame Cosmo grades, seconds (R-RACK-3)
        std::string input = "rec709";       // what the media IS — its input colour transform (R-COLOR-2)
        std::string lut;                    // an input LUT (.cube) after the input transform; "" = none (R-COLOR-5)
        std::string proxy;                  // R-MEDIA-2: its proxy file (the monitor's, when the project uses proxies); "" = none
        double proxyScale = 1.0;            // the proxy's width over the original's (plugins are sized in ORIGINAL pixels)
        std::string cosmoPath;              // R-MEDIA-3: the file Cosmo's slot was added with, once relinked away from it ("" = media)
        Fields unknown;
        Notes notes;
    };

    // ── §3 timelines and their deltas ─────────────────────────────────────────────────────────

    /** A timeline is a VERSION: "my base, resolved live, with my deltas on top" (R-VER-2). */
    struct Timeline
    {
        NodeId id, name, base;              // base "" = a root
        std::string colour = "follow";      // follow | pin@<commit>
        std::string cut = "follow";         // follow | frozen
        int order = 0;
        Fields unknown;
        Notes notes;

        bool pinned() const { return colour.rfind("pin@", 0) == 0; }
        std::string pinCommit() const { return pinned() ? colour.substr(4) : std::string(); }
        bool frozen() const { return cut == "frozen"; }
    };

    /** "The base's node is not in this version." Keyed by (timeline, node) — no id of its own,
     *  because a second drop of the same node would say nothing new. */
    struct TlDrop
    {
        NodeId timeline, node;
        Fields unknown;
        Notes notes;
    };

    /** Field overrides on an inherited node. Carries ONLY the overridden fields, in order, so an
     *  untouched field keeps inheriting and a later base edit to it still arrives (§3.3). */
    struct TlSet
    {
        NodeId timeline, node;
        Fields fields;
        Notes notes;
    };

    /** A COLOUR override, per rack node: deltas applied to the rack's value at render time. The
     *  rack stays the authority; this says "+0.4 on top of whatever the rack says". */
    struct TlGrade
    {
        NodeId timeline, node;
        std::vector<std::pair<std::string, double>> deltas;
        Notes notes;
    };

    // ── §4 arrangement ────────────────────────────────────────────────────────────────────────

    struct Geom
    {
        double x = 0, y = 0, scale = 1.0, rotation = 0;
        double anchorX = 0.5, anchorY = 0.5;
        double cropX = 0, cropY = 0, cropW = 1.0, cropH = 1.0;
    };

    /*  `from` (on every arrangement node) is written only by `freezeCut`: it names the base node
     *  a frozen version's local copy was materialised from, so `thawCut` can turn the copy back
     *  into a delta against that node. Absent on everything else. */

    struct Track
    {
        NodeId id, name, timeline;
        std::string kind = "video";         // video | audio
        int order = 0;
        bool mute = false;
        double opacity = 1.0;
        std::string blend = "normal";
        double gain = 0.0;                  // dB, audio tracks
        NodeId from;
        Fields unknown;
        Notes notes;
        bool audio() const { return kind == "audio"; }
    };

    /*  `timeline` on a clip, transition or audio clip is normally ABSENT: such a node belongs to
     *  its track's timeline. It is written only when a version adds a node of its own on a track
     *  it INHERITS (the right half of a split, a clip added to the base's `v0`) — without it, that
     *  node would belong to the base and appear in every sibling version too. */

    struct Clip
    {
        NodeId id, name, track, timeline;
        int order = 0;
        NodeId src;                         // a #rackobj id: colour and pixels from one place — or a #timeline (R-EDT-4)
        double at = 0, in = 0, out = 0, speed = 1.0;
        int angle = 0;                      // R-EDT-5: a placed timeline shows only its angle-th video track (0 = all of it)
        std::string fit = "contain";
        double opacity = 1.0;
        std::string blend = "normal";
        Geom geom;
        NodeId from;
        Fields unknown;
        Notes notes;
        /** Timeline seconds. Speed divides — a 2x clip occupies half the time. */
        double duration() const;
        double end() const { return at + duration(); }
    };

    struct Transition
    {
        NodeId id, name, track, timeline;
        NodeId clipA, clipB;                // between=clipA,clipB — outgoing, incoming
        std::string kind = "dissolve";      // dissolve | dip
        double dur = 0.5;
        NodeId from;
        Fields unknown;
        Notes notes;
    };

    struct Marker
    {
        NodeId id, name, timeline;
        double at = 0;
        std::string note;
        NodeId from;
        Fields unknown;
        Notes notes;
    };

    /** `#atrack kind=audio` — the only audio lane Interstellar renders (audio-format §2.1). */
    struct ATrack
    {
        NodeId id, name, timeline;
        std::string kind = "audio";
        int order = 0;
        double gain = 0.0, pan = 0.0;       // dB; -1..+1
        bool mute = false, solo = false;
        NodeId out;                         // the bus this feeds; "" = master
        NodeId from;
        Fields unknown;
        Notes notes;
    };

    /** `#aclip` SAMPLE form — told apart from the note form by `src` (audio-format §2.2). */
    struct AClip
    {
        NodeId id, name, track, timeline;
        std::string src;                    // res:<hash> or a path: audio is referenced, never embedded
        double at = 0, in = 0, out = 0;
        double gain = 0.0, fadeIn = 0.0, fadeOut = 0.0;
        bool loop = false;
        NodeId from;
        Fields unknown;
        Notes notes;
    };

    // ── §5 effects ────────────────────────────────────────────────────────────────────────────

    /** A temporal effect on a rack node (it belongs to the footage) or on a clip (`freeze` is
     *  editorial). `radius` is declared, not inferred: residency must be computable before the
     *  first frame is read (R-VOL-4). Parameters of a type this build does not know are kept as
     *  unknown keys. */
    struct Fx
    {
        NodeId id, node, clip;
        std::string type;                   // denoise | blend | freeze
        int radius = 0;
        double strength = 0.5;              // denoise
        double shutter = 180.0;             // blend, degrees
        double at = 0.0;                    // freeze, clip-local seconds
        Fields unknown;
        Notes notes;
    };

    /*  An image-processing plugin on a rack node (R-FX-5): `#effect id=ef_3 node=ro_2 type=blur.gaussian
     *  order=0 enabled=1 mix=1.0 radius=12.0`. The schema knows the fixed fields; the plugin's own
     *  parameters are the node's open key=value fields — `unknown`, which the parser already keeps
     *  in file order — and the service's plugin catalog validates them. So a new plugin type needs no
     *  format change, and a file written by a newer build keeps every parameter it carried. */
    struct Effect
    {
        NodeId id, node;
        std::string type;
        int order = 0;                      // position in the node's stack (after Cosmo, which is first)
        bool enabled = true;
        double mix = 1.0;                   // 0 = the effect's input, 1 = its output
        Fields unknown;                     // the plugin's parameters
        Notes notes;
    };

    /** R-ANIM-1: a curve — one animated parameter. `node` is the #rackobj, #effect or #clip it
     *  animates (by id, so a rename never orphans it); `key` is the parameter as an address spells it
     *  after the node ("basic.exposure", "radius", "opacity"). A rack node's and an effect's curve
     *  runs in SOURCE time, a clip's in CLIP time. Its keyframes are the #key nodes naming it. */
    struct Anim
    {
        NodeId id, node;
        std::string key;
        Fields unknown;
        Notes notes;
    };

    /** One keyframe of a curve (R-ANIM-2): its time on the curve's clock, its value in the
     *  address's units, and each side's interpolation — speed in units per second, influence in %. */
    struct AnimKey
    {
        NodeId anim;
        double t = 0, v = 0;
        std::string in = "linear", out = "linear";   // linear | bezier | hold
        double speedIn = 0, inflIn = 33.333, speedOut = 0, inflOut = 33.333;
        std::string shape;                           // R-ANIM-6: a shape key's value ("x,y;…", "h,s,l", "x,y,w,h"); "" = a number
        Fields unknown;
        Notes notes;
    };

    // ── parsed, preserved, refused at render ──────────────────────────────────────────────────

    struct RawChild
    {
        std::string type;                   // "note", "aeffect", … ; "" = a breakpoint or field line
        Fields fields;
        std::string text;                   // the line, trimmed — a breakpoint is `0.000 = 0.40`
    };

    /** A node this build parses and preserves but does not implement. `lines` is the exact text
     *  it was read from and is what `serialize` writes; `fields`/`children` are a read-only view
     *  for inspection. */
    struct RawNode
    {
        std::string type;
        NodeId id;                          // "" for an id-less node such as #arack
        Fields fields;
        std::vector<RawChild> children;
        std::vector<std::string> lines;
        std::string why;                    // why it cannot be rendered, for unrenderable()
    };

    /** What an id names. */
    enum class NodeKind { None, Rack, RackObj, Timeline, Track, Clip, Transition, Marker, ATrack, AClip, Fx, Effect, Anim, Raw };
    const char *nodeKindName(NodeKind k);

    class Project
    {
    public:
        // ── header (project-format §1) ──
        int formatVersion = 1;
        std::string app = "interstellar";
        std::string id, name;
        double fps = 24.0;
        int width = 3840, height = 2160;
        double par = 1.0;
        std::string colorspace = "rec709";
        std::string timebase = "seconds";
        int sampleRate = 48000;
        double masterGain = 0.0;
        bool proxies = false;                // R-MEDIA-2: the monitor decodes proxies where a source has one; renders never do
        /** Which timeline the EDITOR last had open — presentation. A render never reads it
         *  (R-RENDER-1). */
        NodeId current;
        Fields headerUnknown;
        std::vector<std::string> preamble;              // comment lines before the first key
        std::map<std::string, Notes> headerNotes;       // per header key

        // ── nodes ──
        bool hasRack = false;
        RackRef rack;
        std::vector<RackObj> rackObjs;
        std::vector<Timeline> timelines;
        std::vector<TlDrop> drops;
        std::vector<TlSet> sets;
        std::vector<TlGrade> grades;
        std::vector<Track> tracks;
        std::vector<Clip> clips;
        std::vector<Transition> transitions;
        std::vector<Marker> markers;
        std::vector<ATrack> audioTracks;
        std::vector<AClip> audioClips;
        std::vector<Fx> effects;
        std::vector<Effect> imageEffects;            // R-FX-5: the rack's plugin stacks
        std::vector<Anim> anims;                     // R-ANIM-1: the curves
        std::vector<AnimKey> animKeys;               // their keyframes
        std::vector<RawNode> raw;

        // ── text I/O ──
        /** Parse `text`. False + `err` (with a line number where there is one) on a structural
         *  error; a non-finite or unreadable number is REPAIRED to the field default and counted
         *  in `*repaired`. */
        bool parse(const std::string &text, std::string &err, int *repaired = nullptr);
        std::string serialize() const;
        bool load(const std::string &path, std::string &err, int *repaired = nullptr);
        /** Written to `<path>.tmp` and renamed over `path`, so a crash mid-save leaves the old
         *  file rather than half a new one. */
        bool save(const std::string &path, std::string &err) const;
        /** True when `serialize(parse(text)) == text` byte for byte. The cheapest test in the
         *  project; public so any front end can assert it. `err` names the first differing line. */
        static bool roundTripsExactly(const std::string &text, std::string &err);

        /** The structural rules of project-format §7, for a document built or edited in code.
         *  `parse` runs the same checks. */
        bool validate(std::string &err) const;

        /** Every node preserved but not renderable — "#atrack atr_2 (kind=instrument)", … — so a
         *  render can refuse with them NAMED. Empty for a pure Interstellar project. */
        std::vector<std::string> unrenderable() const;

        // ── lookup, by id ──
        Timeline *timeline(const NodeId &);         const Timeline *timeline(const NodeId &) const;
        RackObj *rackObj(const NodeId &);           const RackObj *rackObj(const NodeId &) const;
        Track *track(const NodeId &);               const Track *track(const NodeId &) const;
        Clip *clip(const NodeId &);                 const Clip *clip(const NodeId &) const;
        Transition *transition(const NodeId &);     const Transition *transition(const NodeId &) const;
        Marker *marker(const NodeId &);             const Marker *marker(const NodeId &) const;
        ATrack *audioTrack(const NodeId &);         const ATrack *audioTrack(const NodeId &) const;
        AClip *audioClip(const NodeId &);           const AClip *audioClip(const NodeId &) const;
        Fx *fx(const NodeId &);                     const Fx *fx(const NodeId &) const;
        Effect *effect(const NodeId &);             const Effect *effect(const NodeId &) const;
        Anim *anim(const NodeId &);                 const Anim *anim(const NodeId &) const;
        /** The curve animating `key` of `node`, or null (R-ANIM-1). */
        const Anim *animOf(const NodeId &node, const std::string &key) const;
        /** A curve's keyframes, sorted by time, as the evaluator takes them (model/Anim.h). */
        std::vector<anim::Key> keysOf(const NodeId &animId) const;
        /** A SHAPE curve's keys (R-ANIM-6), sorted by time; empty when the curve is numeric. */
        std::vector<anim::ShapeKey> shapeKeysOf(const NodeId &animId) const;
        /** Drop every curve of `node` and their keyframes (the node is gone). */
        void dropAnimsOf(const NodeId &node);
        /** Drop one curve and its keyframes. */
        void dropAnim(const NodeId &animId);
        RawNode *rawNode(const NodeId &);           const RawNode *rawNode(const NodeId &) const;
        TlSet *tlset(const NodeId &tl, const NodeId &node);
        const TlSet *tlset(const NodeId &tl, const NodeId &node) const;
        TlDrop *tldrop(const NodeId &tl, const NodeId &node);
        const TlDrop *tldrop(const NodeId &tl, const NodeId &node) const;
        TlGrade *tlgrade(const NodeId &tl, const NodeId &node);
        const TlGrade *tlgrade(const NodeId &tl, const NodeId &node) const;

        NodeKind kindOf(const NodeId &id) const;
        /** An id, or a bind name, to the id it names; "" when it names nothing. The user's
         *  language is names and everyone else's is ids; this is the one place they meet. */
        NodeId idForRef(const std::string &idOrName) const;
        /** The timeline that DECLARES an arrangement node — its own `timeline`, or its track's.
         *  "" for a node that is not an arrangement node, or whose owner cannot be found. */
        NodeId ownerOf(const NodeId &id) const;
        NodeId clipTimeline(const Clip &c) const;
        NodeId transitionTimeline(const Transition &t) const;
        NodeId audioClipTimeline(const AClip &a) const;
        /** `tl`, its base, its base's base, … Stops (without repeating) at a cycle. */
        std::vector<NodeId> chain(const NodeId &tl) const;
        bool derivesFrom(const NodeId &tl, const NodeId &ancestor) const;
        /** The rack's bind names, sorted — what a refused `src` lists. */
        std::vector<std::string> bindNames() const;

        // ── identity ──
        /** `prefix` + a number, unique in this document and never handed out twice: the counter
         *  only rises, and it starts above every id AND every reference already in the file, so a
         *  dangling delta can never silently re-attach to a new node that reused its target id. */
        NodeId freshId(const std::string &prefix);
        /** `base`, or `base2`, `base3`, … — the first one that is legal and free. */
        std::string freshName(const std::string &base) const;
        /** `[A-Za-z_][A-Za-z0-9_]{0,31}`, and not a reserved word. */
        static bool nameIsLegal(const std::string &name, std::string &why);
        /** Taken by any node's name or id. */
        bool nameIsTaken(const std::string &name) const;
        /** Give node `id` a new bind name and rewrite EVERY reference spelled with the old one,
         *  atomically — including the verbatim text of preserved nodes, which nothing else
         *  normalises. Refused (and nothing changed) if the name is illegal or taken. */
        bool rename(const NodeId &id, const std::string &newName, std::string &err);
        /** A colour key: refused on a #clip and in a #tlset (R-TL-2). Case-insensitive, and any
         *  dotted extension of a colour key (`grade.lift`) counts. */
        static bool fieldIsColour(const std::string &key);

    private:
        std::map<std::string, long long> mNext;
        bool validateIdsAndNames(std::string &err) const;
        bool validateRefs(std::string &err) const;
        void normalizeRefs();
        void canonicalizeDeltas(int *repaired);
    };

    /** Canonical number text: the shortest decimal that reads back bit-identically, with `.0`
     *  kept on an integral value so a float field stays legible as one. Locale-free — a GTK host
     *  calls setlocale(), and a German locale would otherwise write `4,25`. */
    std::string canonicalNumber(double v);
    /** Times: exactly three decimals. */
    std::string canonicalTime(double v);
    /** Quote only when the value would not survive unquoted: empty, whitespace, `"`, `;`, `=`. */
    std::string quoteIfNeeded(const std::string &v);
    /** Locale-free, whole-string, finite-only. A leading `+` is accepted. */
    bool parseNumber(const std::string &text, double &out);
}
}
