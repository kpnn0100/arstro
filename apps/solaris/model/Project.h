/*
 *  solaris_model — Project: the `.slp` document as typed data (R-FMT, R-MIX, R-LANE, R-CLIP).
 *
 *  Normative: `docs/project-format.md` (every node, §10 canonical form, §11 validation) and the
 *  suite's `docs/audio-format.md`. Four rules shape this file:
 *
 *  1. **There is no sound here.** The model knows a device's `type` and its parameter TEXT; what
 *     those mean is the DSP registry's, checked by the core when it compiles (R-DSP-2). The model
 *     links nothing but the standard library.
 *  2. **parse → serialize is a byte-exact fixed point** for canonical text (R-FMT-2). Unknown keys,
 *     unknown nodes and comments survive in place.
 *  3. **A structural error refuses; a numeric corruption repairs** (R-FMT-4). A backward route, a
 *     clip on the wrong kind of strip, a dangling reference: refused, named. A `nan`: repaired to
 *     the field's default and counted.
 *  4. **Routing only goes forward** (R-MIX-4): `validate` refuses an `out` or a send whose target
 *     is on the same or an earlier mixer, naming both ends.
 *
 *  Times are beats (rounded to the tick on parse and on write) except an audio clip's `in`/`out`,
 *  which are seconds into its file.
 */
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace solaris
{
    using Fields = std::vector<std::pair<std::string, std::string>>;

    /** Text around a node that is not data — kept, because a comment is a user's note. */
    struct Remarks
    {
        std::string inlineComment;
        std::vector<std::string> after; // whole comment lines that followed the node
    };

    struct Header
    {
        std::string id, name;
        double bpm = 120.0;
        std::string sig = "4/4";
        int sampleRate = 48000;
        double masterGain = 0.0;        // dB
        std::vector<std::string> masterOut; // port ids
        bool tracks = true;             // R-LANE-3: false = a song saved before lanes could be tracks (no `tracks` line)
        std::string launchQuantize = "bar"; // R-LAUNCH-1: where a launch, a stop or a return lands — bar | beat | none
        Fields unknown;
        std::vector<std::string> comments; // whole comment lines before the first node
    };

    struct Port
    {
        std::string id, name, dir = "out";
        int channels = 2, order = 0;
        Fields unknown;
        Remarks remarks;
    };

    struct Mixer
    {
        std::string id, name;
        int order = 0;
        Fields unknown;
        Remarks remarks;
    };

    /** A strip (R-MIX-1) — the suite's `#atrack`. */
    struct Strip
    {
        std::string id, name, kind = "audio", mixer, out; // out: "" = master, "master", a strip id, a port id
        int order = 0, colour = -1;
        double gain = 0.0, pan = 0.0;
        bool mute = false, solo = false;
        std::string input;              // R-REC-1: the input port (dir=in) it monitors and records; "" none
        std::string monitor = "auto";   // R-REC-1: auto | on | off — when its input is heard through it
        std::string frozen;             // R-PERF-3: the file it plays instead of its own work ("" not frozen), as a clip's src
        std::string vca;                // R-MIX-19: the VCA strip (kind vca) whose dB it adds ("" none)
        bool phase = false;             // R-MIX-20: its polarity inverted (both channels)
        double width = 100.0;           // … its stereo width, % (0 mono … 200 the side doubled)
        bool soloSafe = false;          // … a bus no solo silences
        Fields unknown;
        Remarks remarks;
    };

    struct Send
    {
        std::string id, from, to;
        double gain = 0.0;
        bool pre = false;
        bool sidechain = false; // R-MIX-15: into the target's KEY (its compressors' detectors), not its input
        Fields unknown;
        Remarks remarks;
    };

    /** One device in a rack: its registry type and its parameters as text, in file order. */
    struct DeviceNode
    {
        std::string id, type;
        bool bypass = false;
        Fields params;
        Remarks remarks;
        std::string sample;   // R-EDM-8: a sampler's sound — a path, stored as a clip's src is (relative to the song)
        // R-VST-6: type `vst3` — another maker's plugin: its class (UID), its bundle, what it is called and whether it is
        // an instrument (so a song still says so where the bundle is missing), and its state as the plugin gave it (base64)
        std::string plugin, bundle, pluginName, pluginKind, state;
    };

    struct Rack
    {
        std::string track; // a strip id, or "master"
        std::vector<DeviceNode> devices;
        Fields unknown;
        Remarks remarks;
    };

    struct Lane
    {
        std::string id, name;
        std::string track;              // R-LANE-3: the instrument strip this lane is the TRACK of; empty = organisation only
        std::string group;              // R-ARR-3: the lane group it is in (lg_n); empty = none
        int order = 0, colour = -1;
        Fields unknown;
        Remarks remarks;
    };

    /** R-ARR-3: a named group of lanes — organisation only, as a lane is; its lanes side by side in the lane order.
     *  Folded, they are one row on the timeline. */
    struct LaneGroup
    {
        std::string id, name;
        bool folded = false;
        Fields unknown;
        Remarks remarks;
    };

    struct Note
    {
        int pitch = 60, vel = 100;
        double at = 0.0, length = 0.25; // beats from the pattern's start
        double chance = 1.0;            // R-ROLL-11: the odds it sounds in a repetition, 0 … 1 (`noteSounds`)
        Fields unknown;
    };

    /** R-MIDI-3: a point of an expression lane — the pitch wheel (`bend`, −1 … +1) or a controller (`cc<N>`, 0 … 1;
     *  `cc1` the mod wheel) — linear to the next, held before the first and after the last. */
    struct ExprPoint
    {
        std::string lane;
        double at = 0.0, value = 0.0;   // beats from the pattern's start
        Fields unknown;
    };

    struct Pattern
    {
        std::string id, name;
        double length = 4.0;            // beats
        std::vector<Note> notes;        // sorted by (at, pitch)
        std::vector<ExprPoint> expr;    // R-MIDI-3: sorted by (lane, at)
        std::string scale;              // R-ROLL-11: "D dorian" (`parseScale`'s spelling) locks placed and moved notes; "" none
        Fields unknown;
        Remarks remarks;
    };

    /** R-MIDI-3: a lane's one spelling — `bend`, `cc0` … `cc127` (`mod` is `cc1`); false for anything else. */
    bool exprLane(const std::string &name, std::string &canonical);
    /** The values a lane takes: −1 … +1 for `bend`, 0 … 1 for a controller; its rest (0 either way). */
    void exprRange(const std::string &lane, double &lo, double &hi);
    /** A lane's value at `at` beats into its pattern: linear between points, held at the ends; `rest` when empty. */
    double exprAt(const std::vector<ExprPoint> &expr, const std::string &lane, double at, double rest = 0.0);

    /** R-ROLL-11: a scale a pattern can be locked to — its spellings (the first canonical), its pitch classes above the
     *  key, ascending, and what the API document calls it. */
    struct ScaleKind
    {
        std::vector<std::string> names;
        std::vector<int> steps;
        std::string label;
    };
    const std::vector<ScaleKind> &scaleKinds();
    /** "D dorian", "Bb minor-pentatonic" → its one spelling ("D dorian", "A# minor-pentatonic" — a key in sharps, as
     *  pitches are named) and its pitch classes, bit k for pitch class k (C = 0). False with `err` naming the words. */
    bool parseScale(const std::string &text, std::string &canonical, int &mask, std::string &err);
    /** A pitch onto a scale's mask: `dir` 0 the nearest (a tie goes DOWN — so a major chord stamped on any degree is
     *  that degree's own triad), +1 the first at or above, −1 the first at or below; within 0 … 127, the other way when
     *  the end is reached. Mask 0 (no scale) keeps the pitch. */
    int snapToScale(int pitch, int mask, int dir);
    struct Project;
    /** R-ARR-3: each group's lanes side by side — at its first lane's place, in their order — a group with no lane
     *  gone, the lanes renumbered 0…n−1. True when anything changed (a hand-written song, or after a lane edit). */
    bool gatherLaneGroups(Project &p);
    /** R-COLLAB-1: where a merge took two sides' places at one `order` — two strips of one mixer, two lanes, scenes,
     *  mixers or ports — that group made 0…n−1 again, the earlier in the file first. True when anything moved. */
    bool tidyOrders(Project &p);
    /** A pattern's scale as a mask — 0 when it has none (or one validateProject would refuse). */
    int scaleMaskOf(const Pattern &pt);
    /** R-ROLL-11: whether a note sounds in a clip's `repetition` (0 the first pass of its pattern): always at chance 1,
     *  never at 0, else a hash of the clip, the repetition, the note's pitch and tick against its chance — never a
     *  clock, so a render is pure, and two clips of one pattern vary apart. */
    bool noteSounds(const Note &n, const std::string &clip, long long repetition);

    /** R-REC-2: one pass over a loop, recorded — a file of the clip's span. */
    struct Take
    {
        std::string src;
        Fields unknown;
    };
    /** R-REC-2: where take `take` (1-based) plays — song beats [from, to). */
    struct CompRange
    {
        int take = 1;
        double from = 0.0, to = 0.0;
        Fields unknown;
    };

    struct Clip
    {
        std::string id, name, track, lane, src, pattern; // src: audio clip; pattern: note clip
        double at = 0.0;                // beats
        double in = 0.0, out = 0.0;     // seconds into the file (audio)
        double length = 0.0;            // beats; 0 = unset (audio: the file span; note: the pattern's)
        double offset = 0.0;            // R-LAUNCH-1: a note clip starts this many beats into its pattern (one cut at its start)
        double gain = 0.0;              // dB
        double fadeIn = 0.0, fadeOut = 0.0; // beats
        bool loop = false;
        bool reverse = false;           // R-EDIT-1: an audio clip's span played backwards, `out` to `in`
        bool warp = false;              // R-WARP-1: it follows the song's tempo — its span lasts its own beats
        double bpm = 0.0;               // R-WARP-1: its own tempo (0: not known)
        double pitch = 0.0;             // R-WARP-1: semitones its span is moved, its length kept
        std::vector<Take> takes;        // R-REC-2: a loop recording's passes, in the order played (1-based)
        std::vector<CompRange> comp;    // R-REC-2: which take plays where; elsewhere the LAST
        Fields unknown;
        Remarks remarks;

        bool isAudio() const { return !src.empty(); }
        /** R-REC-2: the take that plays at `beat` (1-based) — the comp's, else the last; 0 when it has no takes. */
        int takeAt(double beat) const;
    };

    /** A point of an automation (R-AUTO-4): the shape governs the segment AFTER it — a bezier point
     *  both its sides (R-AUTO-10), each shaped by Interstellar's handle (R-ANIM-2): a SPEED, the
     *  curve's slope there in the automation's unit per beat, and an INFLUENCE, how far into the
     *  neighbouring segment that slope pulls, 0.1 … 100 %. Written only for a bezier point. */
    struct AutoPoint
    {
        double at = 0.0, value = 0.0;   // beats; the automation's unit
        std::string shape = "linear";   // linear | hold | smooth | bezier
        Fields unknown;
        double speedIn = 0.0, inflIn = 33.333, speedOut = 0.0, inflOut = 33.333; // bezier: per beat; %
    };

    /** An automation — FL Studio's automation clip (R-AUTO-4). It moves nothing until a formula reads it. */
    struct Automation
    {
        std::string id, name, unit, from; // `from`: the address it was made from (a hint, never a link)
        double min = 0.0, max = 1.0;
        // R-AUTO-12: its first `loop` beats are a CYCLE, repeated from `at` for `length` beats (0: to the song's end);
        // outside that span it rests where a cycle ends. loop 0: the curve as drawn
        double loop = 0.0, at = 0.0, length = 0.0;
        std::vector<AutoPoint> points;    // sorted by `at`
        Fields unknown;
        Remarks remarks;
    };

    /** A binding (R-AUTO-1): the formula that decides an address's value. One per address. */
    struct Binding
    {
        std::string address, formula;   // formula as typed, with its leading `=`
        Fields unknown;
        Remarks remarks;
    };

    /** R-ARR-1: a named place in the song — Interstellar's `#marker` (id, name, at, note) in the song's beats, with a
     *  `length` making it a SECTION (0: a point, whose section runs to the next marker). */
    struct Marker
    {
        std::string id, name, note;
        double at = 0.0, length = 0.0;  // beats
        Fields unknown;
        Remarks remarks;
    };

    /** R-LAUNCH-1: a row of the session grid — slots launched together. */
    struct Scene
    {
        std::string id, name;
        int order = 0;
        Fields unknown;
        Remarks remarks;
    };

    /** R-LAUNCH-1: a clip waiting in a scene for a strip — a pattern (an instrument strip's) or a file (an audio
     *  strip's) — launched live, looped; its own node, so nothing that reads the arrangement has to skip it. */
    struct Slot
    {
        std::string id, name, scene, strip;
        std::string pattern;            // an instrument strip's: shared, as a clip's is (R-CLIP-3)
        std::string src;                // an audio strip's: a file, stored as a clip's src is
        double in = 0.0, out = 0.0;     // seconds into the file (audio); out 0 = its end
        double length = 0.0;            // beats a loop lasts; 0 = the pattern's (audio: the file span's)
        double gain = 0.0;              // dB
        Fields unknown;
        Remarks remarks;
    };

    /** R-VIDEO-1: the song's picture — its first frame shows `offset` seconds into the song (its clock, the tempo map's);
     *  negative, the picture began before the song. One a song. */
    struct Video
    {
        std::string id, src;            // src stored as a clip's is
        double offset = 0.0;            // seconds
        Fields unknown;
        Remarks remarks;
    };

    /** R-TEMPO-1: a change of the song's tempo — `bpm` from beat `at` on; `ramp`: reached there gliding (linearly, in
     *  beats) from the tempo before. Before the first, the header's `bpm` holds. */
    struct TempoPoint
    {
        std::string id;
        double at = 0.0, bpm = 120.0;   // beats; beats a minute
        bool ramp = false;
        Fields unknown;
        Remarks remarks;
    };

    /** A node this build does not know: kept verbatim, written back where it was read. */
    struct RawNode
    {
        std::vector<std::string> lines;
    };

    /** R-VER: a version of the song — its base resolved, with its deltas on top (docs/project-format.md §9b; the
     *  resolution and the deltas are Versions.h's). Main, the song itself, is not one. */
    struct Version
    {
        std::string id, name;
        std::string base;                // an EARLIER version's id; empty = main
        bool frozen = false;             // R-VER-3: its `#vsnapshot` is read instead of its base
        std::vector<std::string> lines;  // its deltas, its own nodes, then `#vsnapshot` and the song — canonical text,
                                         // indented as written less the block's two spaces
        Fields unknown;
        Remarks remarks;
    };

    struct Project
    {
        Header header;
        std::vector<Port> ports;
        std::vector<Mixer> mixers;
        std::vector<Strip> strips;
        std::vector<Send> sends;
        std::vector<Rack> racks;
        std::vector<Lane> lanes;
        std::vector<LaneGroup> groups;  // R-ARR-3
        std::vector<Pattern> patterns;
        std::vector<Clip> clips;
        std::vector<Automation> automations;
        std::vector<Binding> bindings;
        std::vector<Marker> markers;    // R-ARR-1: sorted by (at, id)
        std::vector<TempoPoint> tempos; // R-TEMPO-1: sorted by at, each at a beat of its own after 0
        std::vector<RawNode> raw;
        std::vector<Scene> scenes;      // R-LAUNCH-1: the session grid's rows, in creation order (`order` places them)
        std::vector<Slot> slots;        // R-LAUNCH-1: its cells
        std::vector<Video> videos;      // R-VIDEO-1: its picture — one at most
        std::vector<Version> versions;  // R-VER: in creation order, each based on main or an earlier one
        /** R-VER: the version open — its resolution is what the nodes above hold ("" = main, the song itself). Never
         *  written: a song opens on main. */
        std::string version;
        /** R-VER: while a version is open, main's own text (`songText` of the song), which an edit on a version never
         *  touches — `serializeProject` writes it in place of the nodes above. */
        std::string mainText;

        // ── lookups (nullptr = none) ──
        Port *port(const std::string &id);
        Mixer *mixer(const std::string &id);
        Strip *strip(const std::string &id);
        Send *send(const std::string &id);
        Rack *rack(const std::string &track);
        Lane *lane(const std::string &id);
        Pattern *pattern(const std::string &id);
        Clip *clip(const std::string &id);
        Automation *automation(const std::string &id);
        Marker *marker(const std::string &id);
        TempoPoint *tempo(const std::string &id);  // R-TEMPO-1
        const TempoPoint *tempo(const std::string &id) const;
        Scene *scene(const std::string &id);   // R-LAUNCH-1
        const Scene *scene(const std::string &id) const;
        Slot *slot(const std::string &id);
        const Slot *slot(const std::string &id) const;
        /** Scenes by order (the grid's rows). */
        std::vector<const Scene *> scenesInOrder() const;
        LaneGroup *group(const std::string &id);
        const LaneGroup *group(const std::string &id) const;
        const Marker *marker(const std::string &id) const;
        /** R-ARR-1: where a marker's section ends — at + length, or (a point) the next marker after it, else the song's
         *  end (`songEnd`, beats); never before its own start. */
        double sectionEnd(const Marker &m, double songEnd) const;
        Binding *binding(const std::string &address);
        const Automation *automation(const std::string &id) const;
        const Binding *binding(const std::string &address) const;
        DeviceNode *device(const std::string &id, Rack **owner = nullptr);
        const Strip *strip(const std::string &id) const;
        const Mixer *mixer(const std::string &id) const;
        const Port *port(const std::string &id) const;
        const Lane *lane(const std::string &id) const;
        const Pattern *pattern(const std::string &id) const;
        const Clip *clip(const std::string &id) const;
        const Rack *rack(const std::string &track) const;

        /** The mixer a strip lives on, resolved (absent = the first by order). */
        const Mixer *mixerOf(const Strip &s) const;
        /** Its processing rank: (mixer order, strip order) — forward-only routing makes this topological. */
        std::pair<int, int> rankOf(const Strip &s) const;
        /** Mixers by order. */
        std::vector<const Mixer *> mixersInOrder() const;
        /** Strips in processing order. */
        std::vector<const Strip *> stripsInOrder() const;

        /** The next free id with `prefix` (`ch` → `ch_7`), never reusing one in the document — main's, every
         *  version's own nodes and snapshots included (R-VER), so a node a version adds never meets one its base adds. */
        std::string nextId(const std::string &prefix) const;
        /** Every id of the song as it stands (the open version's nodes) and of its versions, for the uniqueness check. */
        std::vector<std::string> allIds() const;
        const Version *versionOf(const std::string &idOrName) const;
        Version *versionOf(const std::string &idOrName);
    };

    struct ParseReport
    {
        int repaired = 0;                 // non-finite numbers set back to their defaults
        std::vector<std::string> notes;   // what was normalised (inline notes → a pattern, …)
    };

    /** Parse `.slp` text. False with `err` on a syntax or structural error (`validate` is run). */
    bool parseProject(const std::string &text, Project &out, std::string &err, ParseReport *report = nullptr);
    /** Canonical text (§10): the song — main's, whichever version is open — then its versions (§9b). */
    std::string serializeProject(const Project &p);
    /** R-VER: the song's own nodes as they stand (the open version's resolution), no versions — what a version's
     *  deltas are worked out against and what a snapshot holds. */
    std::string songText(const Project &p);
    /** Every structural error (§11), each one sentence naming what and where. Empty = valid. */
    std::vector<std::string> validateProject(const Project &p);
    /** R-MIX-4, the one rule: may strip `from` feed strip `to` — by its output, a send or a sidechain
     *  key? Only a strip LATER IN PROCESSING ORDER: a later mixer, or later on its own (strip order,
     *  then the file's order on a tie). A total order, so no loop can form (amended, audit 2026-10-09:
     *  it was "a later mixer" for outputs and sends, which kept a bus made on Buses from feeding Main). */
    bool feedsForward(const Project &p, const Strip &from, const Strip &to);
    /** Everything `s` may route or send to: the strips after it in processing order, then
     *  "master", then the output ports — what a matrix column or a route picker offers. */
    std::vector<std::string> targetsOf(const Project &p, const Strip &s);
    /** Where a sidechain KEY may go (R-MIX-15): the strips of `targetsOf` — a key goes to a strip. */
    std::vector<std::string> keyTargetsOf(const Project &p, const Strip &s);
    /** R-MIX-9: the matrix's columns, ONCE for every face — every strip some strip may reach or does
     *  reach (processing order), then "master", then the output ports: every route has a cell. */
    std::vector<std::string> matrixColumnsOf(const Project &p);

    /** R-LANE-3: the name a lane shows — its own, else (a track with none) its instrument strip's, so a renamed
     *  instrument renames its tracks. */
    std::string laneTitle(const Project &p, const Lane &l);

    /** A fresh project: R-MIX-3's defaults — Mixer 1 "Sources", Mixer 2 "Buses" with bus "Main",
     *  output port "Main" fed by the master. */
    Project newProject(const std::string &id, const std::string &name, double bpm, const std::string &sig, int sampleRate);
}
}
