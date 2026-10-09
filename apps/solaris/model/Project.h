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
        int order = 0, colour = -1;
        Fields unknown;
        Remarks remarks;
    };

    struct Note
    {
        int pitch = 60, vel = 100;
        double at = 0.0, length = 0.25; // beats from the pattern's start
        Fields unknown;
    };

    struct Pattern
    {
        std::string id, name;
        double length = 4.0;            // beats
        std::vector<Note> notes;        // sorted by (at, pitch)
        Fields unknown;
        Remarks remarks;
    };

    struct Clip
    {
        std::string id, name, track, lane, src, pattern; // src: audio clip; pattern: note clip
        double at = 0.0;                // beats
        double in = 0.0, out = 0.0;     // seconds into the file (audio)
        double length = 0.0;            // beats; 0 = unset (audio: the file span; note: the pattern's)
        double gain = 0.0;              // dB
        double fadeIn = 0.0, fadeOut = 0.0; // beats
        bool loop = false;
        Fields unknown;
        Remarks remarks;

        bool isAudio() const { return !src.empty(); }
    };

    /** A point of an automation (R-AUTO-4): the shape governs the segment AFTER it. */
    struct AutoPoint
    {
        double at = 0.0, value = 0.0;   // beats; the automation's unit
        std::string shape = "linear";   // linear | hold | smooth
        Fields unknown;
    };

    /** An automation — FL Studio's automation clip (R-AUTO-4). It moves nothing until a formula reads it. */
    struct Automation
    {
        std::string id, name, unit, from; // `from`: the address it was made from (a hint, never a link)
        double min = 0.0, max = 1.0;
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

    /** A node this build does not know: kept verbatim, written back where it was read. */
    struct RawNode
    {
        std::vector<std::string> lines;
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
        std::vector<Pattern> patterns;
        std::vector<Clip> clips;
        std::vector<Automation> automations;
        std::vector<Binding> bindings;
        std::vector<RawNode> raw;

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

        /** The next free id with `prefix` (`ch` → `ch_7`), never reusing one in the document. */
        std::string nextId(const std::string &prefix) const;
        /** Every id in the document, for the uniqueness check and nextId. */
        std::vector<std::string> allIds() const;
    };

    struct ParseReport
    {
        int repaired = 0;                 // non-finite numbers set back to their defaults
        std::vector<std::string> notes;   // what was normalised (inline notes → a pattern, …)
    };

    /** Parse `.slp` text. False with `err` on a syntax or structural error (`validate` is run). */
    bool parseProject(const std::string &text, Project &out, std::string &err, ParseReport *report = nullptr);
    /** Canonical text (§10). */
    std::string serializeProject(const Project &p);
    /** Every structural error (§11), each one sentence naming what and where. Empty = valid. */
    std::vector<std::string> validateProject(const Project &p);
    /** R-MIX-4, the one rule: may strip `from` feed strip `to`? Only a strip on a LATER mixer. */
    bool feedsForward(const Project &p, const Strip &from, const Strip &to);
    /** Everything `s` may route or send to: strips on later mixers (processing order), then
     *  "master", then the output ports — what a matrix column or a route picker offers. */
    std::vector<std::string> targetsOf(const Project &p, const Strip &s);
    /** R-MIX-4 amended (R-MIX-15): a sidechain KEY may go to any strip LATER in processing order, the
     *  same mixer included — it is heard by a detector, never played, so no loop can form. */
    bool keysForward(const Project &p, const Strip &from, const Strip &to);
    std::vector<std::string> keyTargetsOf(const Project &p, const Strip &s);

    /** A fresh project: R-MIX-3's defaults — Mixer 1 "Sources", Mixer 2 "Buses" with bus "Main",
     *  output port "Main" fed by the master. */
    Project newProject(const std::string &id, const std::string &name, double bpm, const std::string &sig, int sampleRate);
}
}
