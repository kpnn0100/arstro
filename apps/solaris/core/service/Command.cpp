#include "Command.h"
#include <algorithm>

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    std::string Command::arg(size_t i, const std::string &fallback) const { return i < args.size() ? args[i] : fallback; }

    bool Command::has(const std::string &name) const
    {
        for (const auto &kv : flags)
            if (kv.first == name) return true;
        return false;
    }

    std::string Command::flag(const std::string &name, const std::string &fallback) const
    {
        for (const auto &kv : flags)
            if (kv.first == name) return kv.second;
        return fallback;
    }

    const std::vector<CommandSpec> &commandSpecs()
    {
        // The ONE grammar table (R-API-1). Order is the order the API document prints.
        static const std::vector<CommandSpec> specs = {
            {K::ProjectNew, "project new", "<path.slp>", 1, 1, {"bpm=<n>", "sig=<n/d>", "rate=<hz>", "name=<text>", "template=<name>"},
             "Create a song and open it: Mixer 1 \"Sources\", Mixer 2 \"Buses\" with the bus \"Main\" → master, "
             "and the output port \"Main\" (R-MIX-3). --template <name>: the template's song instead (R-PRESET-2) — its "
             "tempo, meter and rate unless --bpm, --sig, --rate say; yours of a name before the factory's.", "R-HOME-1"},
            {K::ProjectOpen, "project open", "<path.slp>", 1, 1, {},
             "Open a song. If it has an autosave newer than it (unsaved edits from a session that never saved), says "
             "`recovery: …` and the model's `recovery` names it (R-SAFE-2).", "R-HOME-1"},
            {K::ProjectSave, "project save", "[path.slp]", 0, 1, {}, "Write the .slp (a path saves it there).", "R-FMT-2"},
            {K::ProjectClose, "project close", "", 0, 0, {}, "Close the song and return Home (its autosave is dropped: closing is choosing).", "R-UI-1"},
            {K::ProjectSync, "project sync", "[<node>]", 0, 1, {"take=<theirs|mine>"},
             "The song's file, looked at now (the window looks once a second — R-COLLAB-1): changed by someone else, and nothing "
             "unsaved here, the song becomes the file's; with edits here, the two are merged node by node — one side's change "
             "taken, the same new id on both renumbered here with its references — as ONE edit, and a node changed on both, or "
             "changed on one and removed on the other, is a CONFLICT, kept as here and listed (`sync.conflicts`). --take "
             "theirs|mine answers one (<node>) or all; until every one is answered a save is refused, and a further change to the "
             "file waits. A save is also refused while the file has changed since this window read or wrote it.",
             "R-COLLAB-1"},
            {K::ProjectRecover, "project recover", "<path.slp>", 1, 1, {},
             "Open the song from its autosave (`<path>.autosave`, written while it had unsaved edits — R-SAFE-1): the "
             "song as it was, unsaved, saved to <path> by `project save`.", "R-SAFE-2"},
            {K::ProjectDiscardRecovery, "project discard-recovery", "<path.slp>", 1, 1, {},
             "Delete a song's autosave — the saved song is the one kept.", "R-SAFE-2"},

            {K::Set, "set", "<address>=<value> …", 1, -1, {},
             "Write addresses: project.bpm, <strip>.gain, <clip>.at, <device>.<param> (any DSP registry "
             "parameter, in its unit), … — `api` lists every one. An unknown address or parameter is refused. "
             "A value starting with `=` is a FORMULA that drives a number (R-AUTO); a plain number clears it.",
             "R-SVC-3", true},
            {K::Get, "get", "<address>", 1, 1, {}, "Print an address's stored value.", "R-SVC-1"},
            {K::Undo, "undo", "", 0, 0, {},
             "Undo the last edit (prints what): back to the history node's parent. Consecutive `set`s of the same "
             "addresses — a fader dragged — are ONE step. A machine setting, the transport and a save are not edits; a new "
             "or opened song starts a new history.",
             "R-EDM-1"},
            {K::Redo, "redo", "", 0, 0, {},
             "Redo: forward to the NEWEST branch below the current node (prints what). An edit after an undo no longer "
             "clears it — it starts another branch, and the old ones stay (R-HIST-1).", "R-EDM-1"},
            {K::HistoryPrint, "history", "", 0, 0, {"json"},
             "The song's history as a tree: one node a line — `*` at the current one, its id, its parent's (`-` at the "
             "root), what made it. --json: the same as `history[]` in the model.", "R-HIST-2"},
            {K::HistoryJump, "history jump", "<id>", 1, 1, {},
             "Make history node <id> the song (prints what made it) — as undo does, it moves through the tree and is "
             "no node itself; the song is unsaved and the player hears it.", "R-HIST-2"},

            {K::MixerAdd, "mixer add", "[name]", 0, 1, {}, "Add a mixer page after the last one.", "R-MIX-3"},
            {K::MixerDelete, "mixer delete", "<mx>", 1, 1, {}, "Delete an empty mixer. Refused while strips live on it.", "R-MIX-3"},
            {K::MixerMove, "mixer move", "<mx>", 1, 1, {"to=<index>"},
             "Move a mixer to position <index> (0 = first). Refused if it would make any route point backward.", "R-MIX-4"},

            {K::StripAdd, "strip add", "", 0, 0,
             {"kind=<audio|instrument|bus|vca>", "name=<text>", "mixer=<mx>", "instrument=<type>", "plugin=<id|name>", "sample=<file>", "out=<target>"},
             "Add a strip. Default mixer: the first for audio/instrument, the second for a bus; default output: "
             "\"Main\" — the bus that leaves the nearest later mixer with buses, else the one leaving its own mixer "
             "(the new strip is then placed BEFORE it, so the route goes forward), else master. An instrument strip "
             "gets its instrument (default synth) as its first device; --sample <file> gives a sampler its sound (R-EDM-8); "
             "--plugin <id|name> makes it another maker's instrument plugin, from `plugins list` (R-VST-6). --kind vca: a "
             "VCA — a fader, a mute and a solo for the strips that follow it (`set <ch>.vca=<vca>`), their gain its dB "
             "added; no sound of its own: no output, rack, clips or sends (R-MIX-19).", "R-MIX-1"},
            {K::StripDelete, "strip delete", "<ch>", 1, 1, {"with-clips"},
             "Delete a strip and its rack. Refused while clips (or slots of the session grid) play through it or strips "
             "route to it, unless --with-clips (its clips and slots go too).", "R-MIX-1"},
            {K::StripMove, "strip move", "<ch>", 1, 1, {"mixer=<mx>", "order=<n>"},
             "Move a strip to another mixer (at its end) and/or to position --order <n> on it (0 = first); its mixer's "
             "strips renumber in their new order, as `mixer move` renumbers mixers. Refused if a route would point backward.", "R-MIX-3"},
            {K::StripFreeze, "strip freeze", "<ch>", 1, 1, {},
             "Render a strip's OWN sound — its clips or notes through its instrument and rack, at unity, before its fader, to "
             "the song's end and its tail — beside the song (Frozen/<strip> <id>.wav, 32-bit float) and play that instead: "
             "its clips, notes and devices kept but idle; its fader, pan, sends and output as ever. Only a strip nothing "
             "feeds; the song saved (R-PERF-3).", "R-PERF-3"},
            {K::StripUnfreeze, "strip unfreeze", "<ch>", 1, 1, {}, "Play a frozen strip's own work again (the file stays) (R-PERF-3).", "R-PERF-3"},
            {K::StripBounce, "strip bounce", "<ch>", 1, 1, {},
             "Render a strip's own sound as a freeze does (Bounces/<strip> <n>.wav) and put it as an audio clip at beat 0 on "
             "a NEW strip; the strip itself untouched. Prints the clip, then `made:` (R-PERF-3).", "R-PERF-3"},
            {K::StripRelink, "strip relink", "<ch>", 1, 1, {"to=<ch>"},
             "Move EVERY clip playing through <ch> to strip --to, one edit. Refused across kinds: audio clips "
             "need an audio strip, note clips an instrument strip; a bus plays no clips.", "R-MIX-14"},
            {K::StripArm, "strip arm", "<ch|none>", 1, 1, {"input=<port>"},
             "Arm an instrument strip for the MIDI keyboard (R-MIDI-2): what is played sounds through it at once — "
             "through the song while it plays, through an input monitor (the song's mix without its clips) while "
             "stopped — the sustain pedal held; `none` disarms. Not an edit: the song does not keep it. Opens the "
             "MIDI inputs `settings midiInputs` names. Or arm an AUDIO line for its input (R-REC-1): heard through it as "
             "`<ch>.monitor` says, recorded by `transport record`; --input <port> sets its input first (that part an edit).",
             "R-MIDI-2"},
            {K::PortAdd, "port add", "<name>", 1, 1, {"dir=<in|out>", "channels=<1|2>"},
             "A logical port (R-DEV-3): an output the master or a strip can feed, or an INPUT (--dir in) an audio line "
             "records (R-REC-1); this machine's settings say which device it is (`settings set port.<name>=<device>`). "
             "Prints its id.", "R-REC-1"},
            {K::StripDuck, "strip duck", "<ch>", 1, 1, {"under=<key-strip>", "amount=<dB>"},
             "A side-chain in one line: a Compressor at the end of <ch>'s rack, its Sidechain on, keyed by a pre-fader "
             "sidechain send from --under — attack 1 ms, release half a beat, threshold −30 dB and the ratio that makes "
             "a full-scale key cut --amount dB (default 8, 1 … 24). The key must come EARLIER in processing order "
             "(R-MIX-4). One edit, one undo; prints the compressor, then `made:`.", "R-MIX-18", false, {"under"}},
            {K::Route, "route", "<ch>", 1, 1, {"to=<ch|master|port>"},
             "Set a strip's main output: a strip LATER in processing order (a later mixer, or after it on its own), "
             "master, or an output port.", "R-MIX-4"},

            {K::SendAdd, "send add", "<ch>", 1, 1, {"to=<ch|master|port>", "gain=<dB>", "pre", "sidechain"},
             "Add a send (post-fader unless --pre). Same forward-only rule as `route`. With --sidechain it is a "
             "KEY: it feeds the target's compressors' detectors (their Sidechain switch), not its input, and goes "
             "to a strip (R-MIX-15).", "R-MIX-5"},
            {K::SendDelete, "send delete", "<sd>", 1, 1, {}, "Remove a send.", "R-MIX-5"},

            {K::DeviceAdd, "device add", "<ch|master>", 1, 1, {"type=<registry type>", "plugin=<id|name>", "at=<index>", "preset=<name>", "sample=<file>"},
             "Insert a DSP registry device into a rack (default: at the end). An instrument goes only first on an "
             "instrument strip. --plugin <id|name> (instead of --type) inserts another maker's effect plugin, from "
             "`plugins list`: it plays in a process of its own, its parameters are p<id>, numbers 0 … 1 (R-VST-6). "
             "--sample <file> gives a device that plays a recording its file in the same line — a convolution "
             "reverb's impulse response (R-FX-14); `<dv>.sample` changes it after.", "R-FX-5"},
            {K::DeviceRemove, "device remove", "<dv>", 1, 1, {}, "Remove a device from its rack. An instrument strip keeps its instrument.", "R-FX-5"},
            {K::DeviceMove, "device move", "<dv>", 1, 1, {"to=<index>"}, "Move a device within its rack.", "R-FX-5"},

            {K::LaneAdd, "lane add", "[name]", 0, 1, {"strip=<ch>"},
             "Add a timeline lane at the bottom. With --strip <instrument>: that instrument's TRACK — what is put on it plays "
             "through it, and it shows the strip's name until given its own (R-LANE-3).", "R-LANE-1"},
            {K::LaneDelete, "lane delete", "<ln>", 1, 1, {"with-clips"},
             "Delete a lane. Refused while clips are drawn on it, unless --with-clips.", "R-LANE-1"},
            {K::LaneMove, "lane move", "<ln|lg>", 1, 1, {"to=<index>"},
             "Move a lane to row <index> (0 = the top); the lanes renumber in their new order. A lane moved out of its group's "
             "run leaves it, one moved into another group's run joins it; a group (lg_n) moves whole, its first lane to <index> "
             "(R-ARR-3).", "R-LANE-1"},
            {K::LaneGroup, "lane group", "<ln> …", 1, -1, {"name=<text>"},
             "Gather lanes into a NEW named group (R-ARR-3): side by side at the first one's place, in their order; a lane "
             "already in another group leaves it. Folded (`set <lg>.folded=on`) the group is one row showing all its clips. "
             "Prints its id.", "R-ARR-3"},
            {K::LaneUngroup, "lane ungroup", "<lg>", 1, 1, {}, "Let a group's lanes go; the group is gone, the lanes stay where they are.", "R-ARR-3"},

            {K::ClipAdd, "clip add", "", 0, 0,
             {"src=<file>", "strip=<ch>", "instrument=<type>", "plugin=<id|name>", "sample=<file>", "pattern=<pt>", "lane=<ln|new>", "at=<beats>", "length=<beats>", "in=<s>", "out=<s>", "warp", "preset=<name>", "auto=<au>"},
             "Place a clip. With --src: an audio clip; a file the song has not used gets its own new strip on the "
             "first mixer (R-MIX-2), a used one reuses its strip. With --strip <instrument>: a note clip of "
             "--pattern (a new empty 4-beat pattern if none). With --instrument <type>: the same on a NEW instrument "
             "strip of that type — what dropping an instrument does (R-BROWSE-3); --plugin <id|name> the same with another "
             "maker's instrument plugin (R-VST-6). With no --lane it goes on the lane "
             "its strip's clips are on (the newest one's), a new lane only for a strip with none; --lane new asks for "
             "a new one (R-SVC-8). On an instrument's TRACK (--lane <ln>) a note clip plays through that instrument — "
             "--strip may be left out, --pattern places that pattern; audio and --instrument are refused there (R-LANE-3). "
             "--warp (with --src): it follows the song's tempo, its own detected now (R-WARP-1; refused when none is heard). "
             "--auto <au>: a clip of that automation (R-AUTO-14) — on its track (made if it has none), or --lane <a plain "
             "lane | new>; --length 0 follows the automation's, longer repeats it. "
             "Prints the clip, then `made:` and whatever else it made.", "R-MIX-2"},
            {K::ClipMove, "clip move", "<ac>", 1, 1, {"at=<beats>", "lane=<ln>", "strip=<ch>"},
             "Move a clip in time, to another lane (where it is drawn), and/or another strip (what it sounds through). "
             "Onto an instrument's TRACK a note clip is re-routed to that instrument (and says so); a clip on a track "
             "told --strip <another> goes to that one's track; audio is refused on a track (R-LANE-3).", "R-LANE-2"},
            {K::ClipDuplicate, "clip duplicate", "<ac>", 1, 1, {"at=<beats>", "count=<n>", "lane=<ln>"},
             "A copy right after it (or at --at); --count N makes N copies end to end; --lane puts them on another lane "
             "(onto an instrument's TRACK a note clip's copy plays through that instrument; audio is refused there, as a "
             "move is — R-CLIP-8). A note clip's copy plays the SAME pattern — linked. Prints the first copy, then "
             "`made:` the rest. Shift-drag and Ctrl+V in the window are this line (R-CLIP-9/10).", "R-CLIP-3"},
            {K::ClipUnique, "clip unique", "<ac>", 1, 1, {},
             "Give a note clip its own copy of its pattern — or an automation clip its own curve: a new automation `of` its "
             "automation (\"<name> 2\", a copy of its points), still driving what that one drives (R-AUTO-14). Prints the new pattern "
             "or automation.", "R-CLIP-3"},
            {K::ClipDelete, "clip delete", "<ac>", 1, 1, {}, "Remove a clip.", "R-LANE-1"},

            {K::PatternNew, "pattern new", "", 0, 0, {"name=<text>", "length=<beats>"}, "Create an empty pattern.", "R-CLIP-2"},
            {K::NoteAdd, "note add", "<pt>", 1, 1,
             {"pitch=<pitch>", "at=<beats>", "length=<beats>", "vel=<1-127>", "chance=<0-1>", "chord=<name>", "inversion=<n>", "octave=<n>"},
             "Add a note to a pattern — every clip of it changes. A pitch is a number, a name (C4 = 60, F#3, Bb2) or a "
             "kit's pad (kick). --chord <Cm7> adds a chord instead (its root in --octave, default 4; --inversion n "
             "raises its n lowest notes an octave) as ONE edit — the qualities are under Notation. --chance: the odds it "
             "sounds in a pass (1). A pattern locked to a scale (`pattern scale`) puts each note on the scale's nearest "
             "pitch, a tie down — a major chord on any degree becomes that degree's own triad.", "R-CLIP-2"},
            {K::NoteDelete, "note delete", "<pt>", 1, 1, {"pitch=<pitch>", "at=<beats>"}, "Remove the note at that pitch and time.", "R-CLIP-2"},
            {K::NotePreview, "note preview", "<ch> <pitch>", 2, 2, {"vel=<1-127>", "length=<beats>"},
             "Hear one note through instrument strip <ch> — its instrument and its rack, at its fader and pan, as it "
             "sounds in the song — on a stream of its own, whether the song plays or not (the piano roll's sound "
             "when a note is made, a key pressed). A pitch is a number, a name or the strip's kit's pad; --length "
             "beats (default 0.5), --vel (default 100). A preview started while one rings takes over with a short "
             "fade. Not an edit, never in a render.", "R-ROLL-7"},
            {K::NoteMove, "note move", "<pt>", 1, 1, {"pitch=<pitch>", "at=<beats>", "to-pitch=<pitch>", "to-at=<beats>", "length=<beats>", "vel=<1-127>", "chance=<0-1>"},
             "Change the note at --pitch / --at: move it (--to-pitch, --to-at), resize it (--length), set its velocity (--vel) or "
             "its chance (--chance) — one gesture, one line. Refused onto another note. In a pattern locked to a scale, "
             "--to-pitch goes on to the scale's next pitch the way it moved.", "R-ROLL-2"},
            {K::PatternQuantize, "pattern quantize", "<pt>", 1, 1, {"grid=<beats>", "swing=<0-0.75>", "groove=<name>", "strength=<0-1>", "notes=<\"pitch@beat …\">"},
             "Move every note's start onto the grid (default a sixteenth, 0.25), delaying every second grid step by "
             "--swing of a step — or onto a --groove (R-ROLL-11: a template's steps, offsets and accents, under Notation; "
             "it carries its own grid and swing). --strength moves each start (and velocity) that part of the way (1). "
             "--notes: only those notes (the roll's selection). Notes that land on one another merge (the louder stays).", "R-ROLL-4"},
            {K::ClipsMove, "clips move", "\"<ac> …\"", 1, -1, {"by=<beats>"},
             "Move several clips in time as ONE edit (R-ARR-3 — a folded group's clips dragged together; any selection): each "
             "--by beats (negative = earlier). Refused whole, naming the first, when a clip is not the song's or would start "
             "before beat 0.", "R-ARR-3", false, {"by"}},
            {K::ClipsDuplicate, "clips duplicate", "\"<ac> …\"", 1, -1, {"by=<beats>"},
             "Copies of several clips as ONE edit (R-CLIP-11 — a selection Shift-dragged, duplicated, pasted): each on its own "
             "lane, --by beats later (default: the clips' span, so the copies follow them). Linked as `clip duplicate`'s copies "
             "are. Prints the copies, in the order named. Refused whole, naming the first, when a clip is not the song's or a "
             "copy would start before beat 0.", "R-CLIP-11"},
            {K::ClipsDelete, "clips delete", "\"<ac> …\"", 1, -1, {},
             "Remove several clips as ONE edit (R-CLIP-11 — a selection deleted). Refused whole, naming the first, when a clip "
             "is not the song's.", "R-CLIP-11"},
            {K::NotesAdd, "notes add", "<pt> \"<note> …\"", 2, -1, {"length=<beats>", "vel=<1-127>"},
             "Many notes in ONE edit (one undo step): each <note> is <pitch>@<beat>[:<length>[:<vel>[:<chance>]]] — "
             "C4@0:0.5:90, kick@1, E4@2::70, hat@1.5:::0.5. --length / --vel are the defaults (0.25, 100). A bad token is "
             "refused by name and nothing lands. Prints how many. A note past the pattern's end grows the pattern to the end "
             "of that bar, and a clip that played it whole with it (R-ROLL-9). A scale-locked pattern puts each on its "
             "scale (R-ROLL-11).", "R-SVC-8"},
            {K::NotesMove, "notes move", "<pt> \"<pitch@beat> …\"", 2, -1, {"by=<beats>", "semi=<n>"},
             "Move several notes as ONE edit (one undo step) — the piano roll's selection dragged, or moved by the arrow "
             "keys: each named by <pitch>@<beat>, all moved --by beats (default 0) and --semi semitones (default 0). "
             "Refused whole, naming the first, when a name matches no note, or a note would land on one not moved, "
             "below beat 0 or outside 0–127.", "R-ROLL-8"},
            {K::NotesDelete, "notes delete", "<pt> \"<pitch@beat> …\"", 2, -1, {},
             "Remove several notes as ONE edit — the piano roll's selection deleted. Refused whole, naming the first, "
             "when a name matches no note.", "R-ROLL-8"},
            {K::PatternSteps, "pattern steps", "<pt> \"<steps>\"", 2, -1, {"pitch=<pitch>", "step=<beats>", "vel=<1-127>", "at=<beats>", "length=<beats>"},
             "A step row: x = a note, X = an accent (velocity 127), . or - = a rest, one per --step (default 0.25) from "
             "--at (default 0); spaces and | are ignored. REPLACES that pitch's notes in the span; --length defaults to "
             "a step. One edit.", "R-SVC-8"},
            {K::PatternDuplicate, "pattern duplicate", "<pt>", 1, 1, {"name=<text>"},
             "A new pattern with the same notes and length (named \"<name> copy\"). Prints its id.", "R-SVC-8"},
            {K::PatternClear, "pattern clear", "<pt>", 1, 1, {"pitch=<pitch>"}, "Remove every note (or only --pitch's).", "R-SVC-8"},
            {K::PatternDelete, "pattern delete", "<pt>", 1, 1, {},
             "Delete a pattern. Refused while a clip plays it (naming the clips).", "R-SVC-8"},
            {K::ExprAdd, "expr add", "<pt>", 1, 1, {"lane=<bend|mod|cc<N>>", "at=<beats>", "value=<v>"},
             "A point on one of a pattern's expression lanes (R-MIDI-3): the pitch wheel (`bend`, −1 … +1) or a controller "
             "(`cc0` … `cc127`, 0 … 1; `mod` is `cc1`) — linear to the next point, held before the first and after the last; "
             "a point already at that beat is moved to the value. The instruments play `bend` and `cc1` (their wheels); every "
             "lane goes out in a MIDI file.", "R-MIDI-3", false, {"lane", "at", "value"}},
            {K::ExprDelete, "expr delete", "<pt>", 1, 1, {"lane=<lane>", "at=<beats>"}, "Remove the lane's point at --at.", "R-MIDI-3", false, {"lane", "at"}},
            {K::ExprClear, "expr clear", "<pt>", 1, 1, {"lane=<lane>"}, "Remove a lane's points (--lane), or every lane's.", "R-MIDI-3"},
            {K::ExprSet, "expr set", "<pt> \"<beat>:<value> …\"", 2, -1, {"lane=<bend|mod|cc<N>>"},
             "Many points of one expression lane in ONE edit (R-ROLL-13 — the roll's Alt+wheel on Bend or Mod): each "
             "<beat>:<value> added, or a point already at that beat moved to the value, as `expr add` does. Refused whole, "
             "naming the first, when a token is not <beat>:<value>, a beat is below 0 or named twice, or a value is outside "
             "the lane's range. A point past the pattern's end grows it (R-ROLL-9). Prints how many.", "R-ROLL-13", false, {"lane"}},
            {K::MarkerAdd, "marker add", "<name>", 1, 1, {"at=<beats>", "length=<beats>", "note=<text>"},
             "A named place in the song (R-ARR-1 — Interstellar's marker, in beats): with --length a SECTION, else a point whose "
             "section runs to the next marker (or the song's end). On the ruler; `transport seek <mk>` goes there, `transport "
             "loop <mk>` loops its section. Prints its id.", "R-ARR-1", false, {"at"}},
            {K::MarkerRename, "marker rename", "<mk> <name>", 2, 2, {"note=<text>"}, "Rename a marker (and set its --note).", "R-ARR-1"},
            {K::MarkerMove, "marker move", "<mk>", 1, 1, {"at=<beats>", "length=<beats>"},
             "Move a marker (--at) and/or set its section's --length (0: a point again).", "R-ARR-1"},
            {K::MarkerDelete, "marker delete", "<mk>", 1, 1, {}, "Delete a marker.", "R-ARR-1"},
            {K::TempoAdd, "tempo add", "", 0, 0, {"at=<beats>", "bpm=<20-999>", "ramp"},
             "Change the song's tempo (R-TEMPO-1): --bpm from --at (beats after 0; the start's tempo is `project.bpm`) on — "
             "or, with --ramp, reached there gliding from the tempo before. Every beat-to-time conversion follows the map: "
             "the render, playback, the metronome, recording, warped clips, the MIDI file. Prints its id.", "R-TEMPO-1", false,
             {"at", "bpm"}},
            {K::TempoMove, "tempo move", "<tp>", 1, 1, {"at=<beats>", "bpm=<20-999>", "ramp=<true|false>"},
             "Move a tempo change (--at), change its --bpm, or whether it is reached gliding (--ramp true|false).", "R-TEMPO-1"},
            {K::TempoDelete, "tempo delete", "<tp>", 1, 1, {}, "Delete a tempo change: the tempo before it holds on.", "R-TEMPO-1"},
            {K::VersionNew, "version new", "<name>", 1, 1, {"base=<version>", "open"},
             "Make a version of the song (R-VER-1): its base resolved live with its changes on top — of main, or of --base "
             "(an earlier version, by id or name). It stores only what it changes; with --open it is opened at once (the "
             "UI's New Version is this one line). Prints its id.", "R-VER-1"},
            {K::VersionList, "version list", "", 0, 0, {},
             "The versions as a tree under main: each one's changes, frozen, dangling changes, and why one does not stand.", "R-VER-1"},
            {K::VersionOpen, "version open", "<version|main>", 1, 1, {},
             "Open a version (or main): the song on screen and in your ears becomes it; an edit then changes only it — "
             "worked out as what it changes from its base (R-VER-2). A step of the history, not an unsaved change: a song "
             "always opens on main. One its base broke is refused, naming why.", "R-VER-1"},
            {K::VersionDiff, "version diff", "<version>", 1, 1, {},
             "What a version changes from its base, one line each — #vset/#vunset (fields), #vdrop, #vbody (a node's "
             "notes, points or takes as one), #vorder (a rack's devices), then the nodes only it has.", "R-VER-2"},
            {K::VersionFreeze, "version freeze", "<version>", 1, 1, {},
             "Stop a version inheriting (R-VER-3): it keeps its resolution as a snapshot; while open, an edit is refused.", "R-VER-3"},
            {K::VersionThaw, "version thaw", "<version>", 1, 1, {},
             "Inherit live again: its changes on its base as it is now.", "R-VER-3"},
            {K::VersionRebase, "version rebase", "<version>", 1, 1, {"dry-run"},
             "Reconcile (R-VER-3): name and prune the changes whose node its base no longer has; a frozen version's "
             "snapshot advanced to its base now, its changes on top. --dry-run names them and changes nothing.", "R-VER-3"},
            {K::VersionDelete, "version delete", "<version>", 1, 1, {},
             "Delete a version — refused while another is based on it; the open one gives way to its base.", "R-VER-1"},
            {K::Revert, "revert", "<address|node>", 1, 1, {},
             "Drop the open version's change at an address (`ch_2.mute`, `dv_1.filter.cutoff`, `project.bpm`) or every "
             "change to a node, so it inherits from its base again. Refused on main and where there is none.", "R-VER-2"},
            {K::ProjectFiles, "project files", "", 0, 0, {"missing"},
             "Every file the song names — clips and their takes, a device's sample or .sfz, a frozen strip's render — in "
             "main and every version: as named, in the song's folder / outside / missing, and what uses it (a version's "
             "ids prefixed `vr_n:`). --missing: only those that cannot be read.", "R-COLLECT-1"},
            {K::ProjectCollect, "project collect", "", 0, 0, {"dry-run", "save"},
             "Copy every file the song names from outside its folder into <folder>/Samples/ — an .sfz with its sounds "
             "into Samples/<its name>/, their layout kept; never over a different file (\" 2\", \" 3\"…), the same bytes "
             "reused — and point every reference at the copy, in main and every version, as ONE edit (one undo; the "
             "copies stay). A missing file is named and left. --dry-run says what it would do; --save saves after.", "R-COLLECT-1"},
            {K::ProjectRelink, "project relink", "[<file>]", 0, 1, {"to=<file>", "folder=<dir>", "dry-run"},
             "Point every reference to <file> at --to <file>, in main and every version, ONE edit; or with --folder, look "
             "for every MISSING file by its name under <dir> (the first by path order; several found, said) and relink "
             "each the same way. --dry-run says what it would do.", "R-COLLECT-1"},
            {K::ClipRelink, "clip relink", "<ac> <file>", 2, 2, {"take=<n>"},
             "Point one audio clip's file (or its --take n's) at another — an edit of the song as open (on a version, "
             "that version's change).", "R-COLLECT-1"},
            {K::SceneAdd, "scene add", "[<name>]", 0, 1, {"at=<index>"},
             "A row of the session grid (R-LAUNCH-1): slots launched together. --at places it (0 the top). Prints its id.", "R-LAUNCH-1"},
            {K::SceneDelete, "scene delete", "<sc>", 1, 1, {}, "Delete a scene and the slots in it.", "R-LAUNCH-1"},
            {K::SceneMove, "scene move", "<sc>", 1, 1, {"to=<index>"}, "Move a scene to row --to (the others renumbered).", "R-LAUNCH-1", false, {"to"}},
            {K::SlotAdd, "slot add", "<sc> <strip>", 2, 2, {"pattern=<pt>", "clip=<ac>", "src=<file>", "length=<beats>"},
             "A cell of the grid: what <strip> plays when it is launched in scene <sc> — a pattern (an instrument strip's, "
             "shared as a clip's is), a file (an audio strip's), or --clip <ac>: that clip's pattern or file and length. "
             "--length: beats a loop lasts (default the pattern's, or the file span's). One a cell. Prints its id.", "R-LAUNCH-1"},
            {K::SlotDelete, "slot delete", "<sl>", 1, 1, {}, "Delete a slot.", "R-LAUNCH-1"},
            {K::Launch, "launch", "<sl|sc>", 1, 1, {},
             "Launch a slot — or every slot of a scene — on the next quantum (`project.launchQuantize`: a bar, a beat, or at "
             "once): its strip plays it, looped, instead of the arrangement, until another launch, a stop or `launch back`. "
             "Live, as the keyboard is: heard, never rendered, until recorded. Stopped, the transport starts and it plays at "
             "once. A strip with no slot in the scene keeps what it plays. Not an edit.", "R-LAUNCH-1"},
            {K::LaunchStop, "launch stop", "<strip|all>", 1, 1, {},
             "On the next quantum the strip (or every strip in the session) plays nothing — the session's, silent.", "R-LAUNCH-1"},
            {K::LaunchBack, "launch back", "[<strip>]", 0, 1, {},
             "On the next quantum the strip (or every strip in the session) follows the arrangement again.", "R-LAUNCH-1"},
            {K::VideoSet, "video set", "<file>", 1, 1, {"offset=<seconds>"},
             "The song's picture (R-VIDEO-1) — added, or another in its place: read by the host (its length, size, frame "
             "rate; a file that is no video is refused); its first frame shows --offset seconds into the song (the "
             "song's clock; negative: it began before). One a song. Prints its id.", "R-VIDEO-1"},
            {K::VideoRemove, "video remove", "", 0, 0, {}, "The song has no picture again.", "R-VIDEO-1"},
            {K::PresetSave, "preset save", "<dv> <name>", 2, 2, {"replace"},
             "Save a device's set values (and a sampler's sound) as a preset of its type in your presets folder "
             "(R-PRESET-1) — a name you have is refused without --replace. The machine's, not the song's: no undo.", "R-PRESET-1"},
            {K::PresetLoad, "preset load", "<dv> <name>", 2, 2, {},
             "Set a device to a preset of its type — every value back to its default, then the preset's, ONE edit; yours "
             "of a name before the factory's (R-PRESET-1). `device add --preset` and `clip add --instrument --preset` drop "
             "one in.", "R-PRESET-1"},
            {K::PresetList, "preset list", "[type]", 0, 1, {}, "The presets, yours and the factory's, by type (R-PRESET-1).", "R-PRESET-1"},
            {K::TemplateSave, "template save", "<name>", 1, 1, {"replace"},
             "Keep the open song as a template in your templates folder (R-PRESET-2) — as it is, every file it names made "
             "absolute; a name you have refused without --replace. The machine's, not the song's: no undo. "
             "`project new <path> --template <name>` starts a song from it.", "R-PRESET-2"},
            {K::TemplateList, "template list", "", 0, 0, {}, "The song templates, yours and the factory's (R-PRESET-2).", "R-PRESET-2"},
            {K::TemplateDelete, "template delete", "<name>", 1, 1, {}, "Delete a template of yours (a factory's is refused) — no undo (R-PRESET-2).", "R-PRESET-2"},
            {K::PresetDelete, "preset delete", "<type> <name>", 2, 2, {},
             "Delete a preset of yours (a factory's is refused) — the machine's, no undo (R-PRESET-1).", "R-PRESET-1"},
            {K::TimeInsert, "time insert", "", 0, 0, {"at=<beats>", "length=<beats>"},
             "Open a gap in the whole song (R-ARR-2), ONE edit: every clip, automation point, marker and the loop at or after --at "
             "moves --length beats later; a section, a looping automation's span or the loop that holds --at grows by it. A clip "
             "that starts before --at and crosses it stays (it is named — split it to open the gap through it, R-EDIT-1). The "
             "loop is the session's: undo leaves it.", "R-ARR-2", false, {"at", "length"}},
            {K::TimeDelete, "time delete", "", 0, 0, {"from=<beats>", "to=<beats>"},
             "Take a span out of the whole song (R-ARR-2), ONE edit: what lies wholly inside it goes (clips, automation points, "
             "markers), what follows moves earlier by its length; a section, a looping span or the loop that crosses it shrinks. "
             "A clip that repeats its material (a note clip, an audio clip set to loop) and starts before --from is shortened by "
             "the overlap; any other clip crossing --from or --to is refused, naming it — split it first (R-EDIT-1) or move it.",
             "R-ARR-2", false, {"from", "to"}},
            {K::PatternScale, "pattern scale", "<pt> <key> <scale> | off", 2, 3, {},
             "Lock a pattern to a scale (R-ROLL-11) — `pattern scale pt_1 D dorian`: a note placed (`note add`, its --chord, "
             "`notes add`) lands on the scale's nearest pitch, a tie going down (so a major chord stamped on any degree is "
             "that degree's own triad); a note moved in pitch (`note move --to-pitch`, `notes move --semi`) goes on to the "
             "scale's next pitch the way it moved. A move in time, an import, a step row, a recording and `pattern "
             "transpose` keep their pitches; notes already written stay. `off` unlocks it. The scales are under Notation. "
             "Prints its notes.", "R-ROLL-11"},
            {K::NotesSet, "notes set", "<pt> \"<pitch@beat> …\"", 2, -1, {"vel=<1-127>", "chance=<0-1>", "length=<beats>", "vel-by=<±n>", "chance-by=<±x>"},
             "Set several notes' --vel, --chance and/or --length as ONE edit — the piano roll's selection. --vel-by / --chance-by "
             "move each note's own velocity / chance by that much instead, kept within 1–127 / 0–1 (R-ROLL-13: the roll's "
             "Alt+wheel). Refused whole, naming the first, when a name matches no note.", "R-ROLL-11"},
            {K::PatternHumanise, "pattern humanise", "<pt>", 1, 1, {"timing=<beats>", "vel=<0-127>", "seed=<n>", "notes=<\"pitch@beat …\">"},
             "Loosen the notes (R-ROLL-11): each start moved within ± --timing beats and each velocity within ± --vel (with "
             "neither: 0.02 and 8), a triangular spread from a seed — the same --seed on the same notes writes the same notes "
             "(by default a hash of the notes; printed). A note never goes before beat 0, nor onto another of its pitch (it "
             "keeps its time). --notes: only those notes (the roll's selection).", "R-ROLL-11"},
            {K::PatternLegato, "pattern legato", "<pt>", 1, 1, {"notes=<\"pitch@beat …\">"},
             "End every note where the next start begins — a chord's notes together, the last at the pattern's end; an "
             "overlap is shortened as a gap is filled (R-ROLL-11). --notes: only those notes, the next start among them.", "R-ROLL-11"},
            {K::PatternArpeggiate, "pattern arpeggiate", "<pt>", 1, 1,
             {"rate=<beats>", "mode=<up|down|updown|random>", "octaves=<1-4>", "gate=<0.05-1>", "seed=<n>", "notes=<\"pitch@beat …\">"},
             "A MIDI effect written as notes (R-ROLL-11): on every --rate step of the beat grid (0.25) where notes are held, the "
             "next pitch of what is held there — its pitches over --octaves (1), up, down, updown (the turns not repeated) or "
             "a seeded random (--seed) — --gate of a step long (1), at the held note's velocity and chance; it starts again "
             "from the first after a rest. Replaces the notes it arpeggiated (every note, or --notes); a step on another note "
             "of its pitch replaces that one. Prints how many.", "R-ROLL-11"},
            {K::PatternImport, "pattern import", "<file.mid>", 1, 1, {"track=<n>", "at=<beats>", "lane=<ln>", "instrument=<type>", "name=<text>"},
             "Make patterns of a Standard MIDI File (format 0/1/2; ticks or SMPTE time): one per track with notes, or "
             "--track n's alone; whole bars long; named after the track. With --lane <an instrument's track> or "
             "--instrument <type>: ONE pattern (every track merged, or --track's) and a clip of it at --at, by `clip "
             "add`'s rules — what dropping a .mid does. Prints the (first) pattern; `made:` the rest.", "R-MIDI-1"},
            {K::PatternExport, "pattern export", "<pt> <file.mid>", 2, 2, {},
             "Write a pattern as a MIDI file (format 1, 960 ticks a beat — Solaris's own, so it imports back exact; "
             "channel 10 when a kit plays it).", "R-MIDI-1"},
            {K::ProjectExportMidi, "project export-midi", "<file.mid>", 1, 1, {},
             "Write the song's notes as a MIDI file: a track per instrument strip, every note clip's pattern where the "
             "clip plays it (repeated to fill it), the song's tempo and meter.", "R-MIDI-1"},
            {K::PatternTranspose, "pattern transpose", "<pt>", 1, 1, {"semi=<n>"},
             "Move every note by --semi semitones (negative = down). Refused if a note would leave 0–127.", "R-SVC-8"},
            {K::PatternPrint, "pattern print", "<pt>", 1, 1, {},
             "Its notes, one per line in the `notes add` notation (pitch as a name or pad), the number after `#`.", "R-SVC-8"},

            {K::AutoCreate, "auto create", "<address>", 1, 1, {},
             "Automate a number: a new automation named \"<owner> · <parameter>\", ranged as it, as long as the song in whole "
             "bars and holding its value, resting at it before its clips (`.rest`) — its TRACK (a lane under its property's "
             "strip's track, else last) and ONE clip of it from beat 0 that follows its length — and the address bound to "
             "`=au_n`. Prints the id, `made:` the lane and the clip. Copy the clip as any clip (`clip duplicate`, Shift-drag, "
             "Ctrl+C/V): the copies are linked (R-AUTO-14).", "R-AUTO-5"},
            {K::AutoAdd, "auto add", "", 0, 0, {"name=<text>", "min=<v>", "max=<v>", "unit=<text>", "length=<beats>", "rest=<v>"},
             "An automation from nothing (default range 0…1, a bar long, resting at its bottom) and no clip — `clip add --auto` "
             "places it. It moves nothing until a formula reads it. Prints the id.", "R-AUTO-4"},
            {K::AutoDelete, "auto delete", "<au>", 1, 1, {"unbind"},
             "Delete an automation — its copies, its clips and its tracks with it. Refused while a formula reads it, unless "
             "--unbind (those bindings are cleared).", "R-AUTO-4"},
            {K::AutoPointAdd, "auto point add", "<au>", 1, 1, {"at=<beats>", "value=<v>", "shape=<linear|hold|smooth|bezier>"},
             "Add a point at --at beats of the automation's OWN time (one at the same beat is replaced); one past its end grows "
             "it to the next whole beat (R-AUTO-14). Its shape governs the segment after it (a bezier point's handles start "
             "flat: speed 0, influence 33.333 %).", "R-AUTO-6"},
            {K::AutoPointMove, "auto point move", "<au>", 1, 1, {"at=<beats>", "to=<beats>", "value=<v>"},
             "Move the point at --at to another beat and/or value.", "R-AUTO-6"},
            {K::AutoPointDelete, "auto point delete", "<au>", 1, 1, {"at=<beats>"}, "Remove the point at --at.", "R-AUTO-6"},
            {K::AutoPointShape, "auto point shape", "<au>", 1, 1,
             {"at=<beats>", "shape=<linear|hold|smooth|bezier>", "speed-in=<v/beat>", "influence-in=<%>", "speed-out=<v/beat>", "influence-out=<%>"},
             "Set how the curve leaves the point at --at. A BEZIER point is Interstellar's key (R-ANIM-2): on each side a "
             "speed — the slope there, in the automation's unit per beat — and an influence, how far into the "
             "neighbouring segment it pulls (0 < x ≤ 100 %); flags not given are kept. One line per handle gesture.", "R-AUTO-10"},
            {K::BindClear, "bind clear", "<address>", 1, 1, {},
             "Clear an address's formula: it plays its own stored value again. (`set <address>=<number>` clears and sets.)", "R-AUTO-1"},
            {K::Eval, "eval", "<address>", 1, 1, {"at=<beats>", "explain"},
             "The value an address plays at a beat (default 0); --explain shows its formula and every name it reads.", "R-AUTO-8"},

            {K::Render, "render", "", 0, 0,
             {"out=<file>", "format=<wav|flac|mp3|ogg|opus>", "rate=<Hz>", "bits=<16|24|32f>", "kbps=<n>", "normalize=<LUFS>",
              "ceiling=<dBTP>", "from=<beats>", "to=<beats>", "stems=<ch,…|all>", "ports", "measure", "version=<version|main>", "video"},
             "Render offline: the master to --out; with --stems, each named strip's (`all`: every strip's) post-fader "
             "output to <out>.<ch>.<ext>; with --ports, each output port to <out>.<port>.<ext>. The tail runs until "
             "−90 dBFS or 10 s. Every file starts at --from exactly: the latency of a lookahead device (a limiter) "
             "is compensated and trimmed (R-MIX-17). The format is the extension — .wav, .flac, .mp3, .ogg (Vorbis), "
             ".opus — or --format (R-EXPORT-1). --rate renders the song AT that rate (Opus: 48000, its default; MP3 to "
             "48000); --bits 16 | 24 (WAV and FLAC; default 24) | 32f (WAV) — 16 and 24 TPDF-dithered; --kbps for the lossy "
             "ones (MP3 32…320, default 320; Ogg 45…500, 256; Opus 6…510, 192). --normalize gains every file alike to that "
             "integrated loudness, held so the master's true peak stays under --ceiling (dBTP, default −1) — said on a "
             "`normalize` line. --measure prints the written master's integrated loudness, the most of its momentary and "
             "short-term, its true peak and its lowest correlation (R-METER-1). --version renders that version (or main) "
             "whichever is open — every version renders by name (R-VER-4). --video writes the picture and the mix into ONE "
             "file — .mp4, .mov or .mkv: the picture's stream copied, where its offset puts it in the range rendered; the "
             "mix as AAC (FLAC in MKV) — no --format, --stems, --ports, --bits or --kbps with it (R-VIDEO-1).", "R-RENDER-2", false, {"out"}},

            {K::MatrixPrint, "matrix print", "", 0, 0, {"json"}, "Every route and send at once: rows = strips, columns = destinations.", "R-MIX-9"},
            {K::Audit, "audit", "", 0, 0, {}, "The mix report: unused strips, clips on muted strips, unreachable strips, single-input buses, offline media, unknown devices, clipping.", "R-MIX-10"},
            {K::StatePrint, "state print", "", 0, 0, {"json", "stable", "compact"},
             "The whole AppModel; --stable omits what changes with time. --json --compact: the SONG only (no registry, "
             "machine, recents or browser), on one line; a device lists only its non-default parameters as "
             "`name=value`, a pattern its notes in the `notes add` notation.", "R-SVC-2"},
            {K::Ls, "ls", "", 0, 0, {},
             "The song as a tree of ids and names: mixers → strips (kind, devices, output, sends), the master, lanes → "
             "clips (at, length, pattern or file), patterns (length, notes, clips), automations.", "R-SVC-8"},
            {K::Show, "show", "<id>", 1, 1, {},
             "One node's fields that differ from their defaults; a device's non-default parameters with their units "
             "(and the default); `show project` for the song.", "R-SVC-8"},
            {K::Api, "api", "", 0, 0, {"json", "md"}, "This document: every command, event, model field and device parameter.", "R-API-1"},

            {K::SettingsSet, "settings set", "<key>=<value> …", 1, -1, {},
             "The MACHINE's settings (never a song's): sampleRate (new songs, and the clock device's rate), "
             "bufferSize (frames), output / input (device ids from `devices list`; empty = the system default), "
             "port.<name>=<device>:<channel> (where a song's port plays on this machine), metronome (on|off), metronomeLevel (dB), auditionLevel (dB, the browser's preview), newBpm / newSig (what a new song starts at), reducedMotion (on|off), showIds (on|off: every object's id beside its name, R-UI-11), "
             "autosave (seconds between autosaves of an unsaved song, or off — R-SAFE-1), key.<action>=<combos> (R-SET-4: a keyboard action's keys — a comma list such as \"Ctrl+Shift+Z, Ctrl+Y\", `none`, or `default`; "
             "a combo another action of the same keyboard has is refused naming it — the actions are in this document's Keyboard section). Saved at once.",
             "R-SET-1", true},
            {K::SettingsPrint, "settings print", "", 0, 0, {"json"}, "The machine's settings.", "R-SET-1"},
            {K::FolderAdd, "folder add", "<path>", 1, 1, {}, "Add a sample folder to the browser's quick-access list.", "R-SET-1"},
            {K::FolderRemove, "folder remove", "<path>", 1, 1, {}, "Remove a sample folder from the list (the folder itself is untouched).", "R-SET-1"},
            {K::FolderMove, "folder move", "<path>", 1, 1, {"to=<index>"}, "Reorder the sample folders.", "R-SET-1"},
            {K::DevicesList, "devices list", "", 0, 0, {}, "List this machine's audio devices (ids for `settings set output=…`).", "R-DEV-1"},
            {K::Browse, "browse", "[folder]", 0, 1, {"search=<text>", "favourites"},
             "List a folder for the browser: sub-folders, audio files, songs. --search <text> (R-PRESET-3): the audio files "
             "whose name holds it or that carry a tag starting with it — in the sample folders (or <folder>) eight levels "
             "down, the first 200, and the library's tagged files; --favourites: the favourites.", "R-BROWSE-1"},
            {K::SampleTag, "sample tag", "<file> <tag>", 2, 2, {},
             "Tag a sound in the machine's library (R-PRESET-3): a word of letters, digits or dashes, kept in lower case. No undo.", "R-PRESET-3"},
            {K::SampleUntag, "sample untag", "<file> <tag>", 2, 2, {}, "Take a tag off a sound (R-PRESET-3).", "R-PRESET-3"},
            {K::SampleFavourite, "sample favourite", "<file> [on|off]", 1, 2, {}, "Mark a sound a favourite, or not (R-PRESET-3; on when not said).", "R-PRESET-3"},
            {K::PluginsScan, "plugins scan", "", 0, 0, {},
             "Find the VST3 plugins where this machine keeps them (R-VST-6: SOLARIS_VST3_PATH, else ~/.vst3, /usr/lib/vst3, "
             "/usr/local/lib/vst3) — each bundle read in a process of its own, so one that crashes loses only itself (said). "
             "Prints each found and each problem; the list is the machine's, kept.", "R-VST-6"},
            {K::PluginsList, "plugins list", "", 0, 0, {}, "The VST3 plugins the last scan found (R-VST-6).", "R-VST-6"},
            {K::DeviceReload, "device reload", "<dv>", 1, 1, {},
             "Start a plugin device's process again — after a crash or an overrun took it offline — from its last state "
             "(R-VST-6). Not an edit. Prints `playing`, or `offline: <why>`.", "R-VST-6"},
            {K::MapAdd, "map add", "<cc> <address>", 2, 2, {},
             "Bind a MIDI control — `<n>` (0–127, on any channel) or `<ch>/<n>` (on channel 1–16) — to a number a formula can "
             "drive (R-CTRL-1): moved, it sets it over its travel (a gain by the fader law, a device parameter by its taper). One "
             "address per control. The machine's setting (`map.<cc>` in its file), not an edit.", "R-CTRL-1"},
            {K::MapRemove, "map remove", "<cc>", 1, 1, {}, "Forget a MIDI control's mapping (R-CTRL-1).", "R-CTRL-1"},
            {K::MapList, "map list", "", 0, 0, {}, "Every MIDI mapping: `<cc> <address>`, `(inert)` where the open song has no such address (R-CTRL-1).", "R-CTRL-1"},
            {K::MapLearn, "map learn", "[<address>]", 0, 1, {"cancel"},
             "Wait for the next MIDI control moved and bind it — on its channel — to <address> (said: `map.learned`; its "
             "value is not taken); --cancel stops waiting (R-CTRL-1).", "R-CTRL-1"},
            {K::DeviceEditor, "device editor", "<dv>", 1, 1, {"close"},
             "Open a plugin device's OWN editor in a window of its own — the plugin's process makes it (R-VST-6) — or, with "
             "--close, close it. What is turned there reaches the song as `set <dv>.p<id>`. Not an edit. Prints `open` or `closed`.", "R-VST-6"},
            {K::RecentsRemove, "recents remove", "<path>", 1, 1, {}, "Take a song off Home's recent list (the file is untouched).", "R-HOME-1"},

            {K::TransportPlay, "transport play", "", 0, 0, {"from=<beats>", "clip=<ac>"},
             "Play on the clock device (settings output), from --from or where the transport stands. Edits while "
             "playing are heard: a gain, a pan, a mute, a solo, a device parameter at once; anything structural by a "
             "new engine swapped in at the same position. --clip plays that clip's span looped, the whole song "
             "sounding there (the piano roll's play, R-ROLL-6): it follows the clip as it is edited, and the song's "
             "own loop comes back at `transport stop`, a plain `transport play` or a `transport loop`.", "R-PLAY-1"},
            {K::TransportStop, "transport stop", "", 0, 0, {"return"},
             "Stop; the transport stays where it was heard — or, with --return, goes back to the cue: where the time bar "
             "was last put (a seek, or a play's --from or --clip; `transport.cue`). A plain stop's place is temporary: "
             "the next play starts there but the cue stays.", "R-TIME-7"},
            {K::TransportSeek, "transport seek", "<beats|mk>", 1, 1, {}, "Move the transport (playing or not) — to a beat, or to a marker (R-ARR-1).", "R-TIME-4"},
            {K::MidiDevices, "midi devices", "", 0, 0, {},
             "This machine's MIDI inputs (ALSA sequencer ports), each marked when Solaris listens to it — the names "
             "`settings set midiInputs=` takes.", "R-MIDI-2"},
            {K::MetersClear, "meters clear", "", 0, 0, {},
             "Clear the meters: every clip light and held peak, and the master's true peak and integrated loudness "
             "start again (the song is not edited; a click on a clip light).", "R-METER-2"},
            {K::TransportRecord, "transport record", "", 0, 0, {"from=<beats>", "count-in=<bars>", "overdub"},
             "Play and record the armed strip's keyboard (R-MIDI-2): at `transport stop` what was played becomes a new "
             "note clip on its track from the bar it began in — or, with --overdub, notes added to the strip's clip "
             "under the start — each note where it was HEARD, its start on the `recordQuantize` grid; one undo takes "
             "it back. --count-in (else `settings countIn`) bars of click first, the song waiting. With an AUDIO line "
             "armed (R-REC-1): its input from where recording begins to the stop, written beside the song "
             "(`Recordings/<strip> <n>.wav`) and put on the strip as a clip where it began — moved earlier by the "
             "output's and the input's latency, kept only inside `transport punch` — one undo. And what the session "
             "grid plays (R-LAUNCH-1) — with nothing armed, only that: each strip's span lands as clips on its lane, "
             "its own clips under it cut, so the song plays back what was heard.", "R-MIDI-2"},
            {K::ClipComp, "clip comp", "<ac>", 1, 1, {"take=<n>", "from=<beats>", "to=<beats>"},
             "Choose which take plays where (R-REC-2): take <n> (1-based) from --from to --to (song beats; the whole clip "
             "without them) — ONE edit, the range overwriting what it covers. Where no range says, the last take plays; the "
             "seams are crossfaded 5 ms. A loop recording makes the takes.", "R-REC-2", false, {"take"}},
            {K::ClipSplit, "clip split", "<ac>", 1, 1, {"at=<beats>"},
             "Cut an audio clip in two at --at (song beats, inside it) — ONE edit (R-EDIT-1): the first half keeps the id "
             "and the fade in, the second (printed) begins at --at with the fade out; together they play what the one did. "
             "A reversed clip's first half is its span's end; a clip with takes keeps them all, its comp divided. A looping "
             "clip is cut only where a repetition begins; a note clip not at all (its notes are its pattern's).", "R-EDIT-1",
             false, {"at"}},
            {K::ClipConsolidate, "clip consolidate", "<ac> [<ac> …]", 1, -1, {},
             "Join audio clips of one strip into ONE (R-EDIT-1): what they play — gain, fades, crossfades, reverse, comps — "
             "rendered raw (before the strip's devices, fader and pan) from the first's start to the last's end, written "
             "beside the song as Bounces/<strip> <n>.wav (32-bit float) and put in their place on the first's lane; one "
             "edit, the new clip printed. A note clip is refused (a strip's bounce is R-PERF-3).", "R-EDIT-1"},
            {K::ClipSeparate, "clip separate", "<ac>", 1, 1, {},
             "Split an audio clip's sound into what sustains and what strikes (R-ASSIST-1): what it plays, raw (as `clip "
             "consolidate` renders it — before its strip's devices, fader and pan), separated by the DSP library (REQ-analysis-2: "
             "median filtering, soft masks — the two sum back to the clip), written beside the song as Separated/<name> harmonic "
             "<n>.wav and … percussive <n>.wav (32-bit float); the clip replaced by two clips at its place, on two new lanes under "
             "its own, each on a new strip of its own (R-MIX-2) — ONE edit, the harmonic clip printed. A note clip is refused.",
             "R-ASSIST-1"},
            {K::ClipToMidi, "clip to-midi", "<ac>", 1, 1, {"instrument=<type>"},
             "An audio clip's melody as notes (R-ASSIST-1): what it plays, raw, transcribed by the DSP library (REQ-analysis-3: "
             "one note at a time — YIN's pitch, a note where one lasts 60 ms or more, split where a new attack is heard, its "
             "velocity from its loudest moment) into a pattern \"<name> notes\" on a new instrument track (--instrument <type>, "
             "the synth unless named) at the clip's place, on a new lane under its own; the notes in beats through the song's "
             "tempo map — ONE edit, the new clip printed. Refused when no note is heard (chords, noise, silence).", "R-ASSIST-1"},
            {K::ChordsDetect, "chords detect", "[<ac>]", 0, 1, {},
             "The song's chords, found (R-ASSIST-1): from an audio clip's sound (raw), or with no clip from the whole song's mix "
             "(rendered as `render` does, to the song's end) — the DSP library's chroma, beat by beat of the tempo map "
             "(REQ-analysis-4: the 24 major and minor triads; a chord changes only where the change holds) — replacing the chords "
             "that were in that span (one across its edge keeps what lies outside it); where no chord is heard, none. ONE edit; "
             "prints how many and their names, `chords.detected` says it. Refused when nothing is heard and nothing was there.",
             "R-ASSIST-1"},
            {K::ChordsClear, "chords clear", "", 0, 0, {}, "Take every chord out of the song (R-ASSIST-1) — ONE edit; refused when it has none.", "R-ASSIST-1"},
            {K::ClipSlice, "clip slice", "<ac>", 1, 1, {"every=<beats>", "transients", "sensitivity=<0-1>", "kit"},
             "Cut an audio clip into clips (R-EDIT-1): --every <beats> from its start, or --transients where a sound "
             "begins (the DSP library's onset detector, REQ-onset-1; --sensitivity 0 … 1, 0.5) — ONE edit, the clips "
             "printed. --every with --kit makes a new instrument track instead: a Sampler of the clip's file in N equal "
             "slices (mode=slices) and a note clip in the clip's place playing them in order (root + k for slice k), so "
             "it plays what the clip did; the clip stays. A looping clip is not sliced.", "R-EDIT-1"},
            {K::TransportPunch, "transport punch", "<from|off> [to]", 1, 2, {},
             "Punch in and out (R-REC-1): a recording keeps only what the song plays from <from> to <to> beats, and its "
             "clip starts at <from>; `off` keeps it all. The session's, as the loop.", "R-REC-1"},
            {K::TransportLoop, "transport loop", "<from|off|mk> [to]", 1, 2, {},
             "Loop between two beats while playing — or a marker's section (R-ARR-1: its length, else to the next marker); `off` ends it.", "R-TIME-4"},
            {K::TransportMono, "transport mono", "<on|off>", 1, 1, {},
             "The mono check: what the speakers get folded to mono (each output pair's L = R = their mean) while on — "
             "the listener's, never a render or an export, not kept in the song; `transport.mono` says it.", "R-MIX-20"},
            {K::Audition, "audition", "<file|stop>", 1, 1, {},
             "Hear a sample now, outside the song (the browser's preview): through the output device at the "
             "audition level, to its end; `stop` ends it. Not an edit, never in a render.", "R-EDM-9"},
            {K::Wait, "wait", "<seconds>", 1, 1, {}, "Let time pass (playback goes on, the model's transport and meters update) — "
             "for scripts that listen.", "R-PLAY-3"},
        };
        return specs;
    }

    const CommandSpec *specFor(Command::Kind k)
    {
        for (const auto &s : commandSpecs())
            if (s.kind == k) return &s;
        return nullptr;
    }

    namespace
    {
        std::string flagName(const std::string &f)
        {
            const auto eq = f.find('=');
            return eq == std::string::npos ? f : f.substr(0, eq);
        }
        bool flagValued(const std::string &f) { return f.find('=') != std::string::npos; }

        int editDistance(const std::string &a, const std::string &b)
        {
            std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
            for (size_t j = 0; j <= b.size(); ++j) prev[j] = (int)j;
            for (size_t i = 1; i <= a.size(); ++i)
            {
                cur[0] = (int)i;
                for (size_t j = 1; j <= b.size(); ++j)
                    cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
                std::swap(prev, cur);
            }
            return prev[b.size()];
        }
        std::string joinWords(const std::vector<std::string> &v, const char *sep)
        {
            std::string s;
            for (size_t i = 0; i < v.size(); ++i) s += (i ? sep : "") + v[i];
            return s;
        }
    }

    std::vector<std::string> tokenize(const std::string &line)
    {
        std::vector<std::string> out;
        std::string cur;
        bool inQuote = false, have = false;
        for (char c : line)
        {
            if (c == '"') { inQuote = !inQuote; have = true; continue; }
            if (!inQuote && (c == ' ' || c == '\t' || c == '\r' || c == '\n'))
            {
                if (have) { out.push_back(cur); cur.clear(); have = false; }
                continue;
            }
            cur += c;
            have = true;
        }
        if (have) out.push_back(cur);
        return out;
    }

    std::string quoteToken(const std::string &s)
    {
        if (!s.empty() && s.find_first_of(" \t\";#") == std::string::npos) return s;
        return "\"" + s + "\"";
    }

    std::vector<std::string> nearest(const std::string &word, const std::vector<std::string> &pool)
    {
        std::vector<std::pair<int, std::string>> scored;
        for (const auto &p : pool)
        {
            const int d = editDistance(word, p);
            const bool prefix = !word.empty() && p.compare(0, word.size(), word) == 0;
            if (prefix || d <= std::max<int>(2, (int)p.size() / 3)) scored.emplace_back(prefix ? 0 : d, p);
        }
        std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        std::vector<std::string> out;
        for (const auto &s : scored)
        {
            if (std::find(out.begin(), out.end(), s.second) == out.end()) out.push_back(s.second);
            if (out.size() == 3) break;
        }
        return out;
    }

    std::string usageOf(const CommandSpec &s)
    {
        std::string u = s.verb;
        if (!s.args.empty()) u += " " + s.args;
        for (const auto &f : s.flags)
        {
            const auto eq = f.find('=');
            const std::string flag = eq == std::string::npos ? "--" + f : "--" + f.substr(0, eq) + " " + f.substr(eq + 1);
            const bool must = std::find(s.required.begin(), s.required.end(), flagName(f)) != s.required.end();
            u += must ? " " + flag : " [" + flag + "]";
        }
        return u;
    }

    namespace
    {
        // R-SVC-8: `# …` may end a line — a `#` that starts a word, outside quotes (`F#3` is a pitch)
        std::string withoutComment(const std::string &line)
        {
            bool inQuote = false;
            for (size_t i = 0; i < line.size(); ++i)
            {
                if (line[i] == '"') inQuote = !inQuote;
                else if (line[i] == '#' && !inQuote && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) return line.substr(0, i);
            }
            return line;
        }
    }

    Command parseCommand(const std::string &text, std::string &err)
    {
        err.clear();
        Command c;
        const std::string line = withoutComment(text);
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#' || line[first] == ';') return c;
        const std::vector<std::string> t = tokenize(line);
        if (t.empty()) return c;

        // Longest verb match: `clip add` beats `clip`, `matrix print` beats `matrix`.
        const CommandSpec *spec = nullptr;
        size_t verbWords = 0;
        for (const auto &s : commandSpecs())
        {
            const std::vector<std::string> words = tokenize(s.verb);
            if (words.size() > t.size() || words.size() <= verbWords) continue;
            if (std::equal(words.begin(), words.end(), t.begin())) { spec = &s; verbWords = words.size(); }
        }
        if (!spec)
        {
            std::vector<std::string> verbs;
            for (const auto &s : commandSpecs()) verbs.push_back(s.verb);
            std::vector<std::string> tried = {t[0]};
            if (t.size() > 1) tried.push_back(t[0] + " " + t[1]);
            std::vector<std::string> near;
            for (const auto &w : tried)
                for (const auto &n : nearest(w, verbs))
                    if (std::find(near.begin(), near.end(), n) == near.end() && near.size() < 3) near.push_back(n);
            err = "unknown command: " + (t.size() > 1 ? t[0] + " " + t[1] : t[0]);
            if (!near.empty()) err += " (did you mean: " + joinWords(near, ", ") + "?)";
            return c;
        }

        std::vector<std::string> flagNames;
        for (const auto &f : spec->flags) flagNames.push_back(flagName(f));
        Command out;
        out.kind = spec->kind;
        for (size_t i = verbWords; i < t.size(); ++i)
        {
            const std::string &tok = t[i];
            if (tok.size() > 2 && tok.compare(0, 2, "--") == 0)
            {
                std::string name = tok.substr(2), value;
                bool inlineValue = false;
                const auto eq = name.find('=');
                if (eq != std::string::npos) { value = name.substr(eq + 1); name = name.substr(0, eq); inlineValue = true; }
                const auto it = std::find(flagNames.begin(), flagNames.end(), name);
                if (it == flagNames.end())
                {
                    err = "`" + spec->verb + "` does not take --" + name;
                    const auto near = nearest(name, flagNames);
                    if (!near.empty()) err += " (did you mean: --" + joinWords(near, ", --") + "?)";
                    else if (flagNames.empty()) err += " (it takes no flags)";
                    else err += " (it takes: --" + joinWords(flagNames, ", --") + ")";
                    return c;
                }
                const bool valued = flagValued(spec->flags[size_t(it - flagNames.begin())]);
                if (valued && !inlineValue)
                {
                    if (i + 1 >= t.size()) { err = "--" + name + " needs a value: " + usageOf(*spec); return c; }
                    value = t[++i];
                }
                else if (!valued)
                {
                    if (inlineValue) { err = "--" + name + " is a switch and takes no value"; return c; }
                    value = "1";
                }
                out.flags.emplace_back(name, value);
                continue;
            }
            if (spec->fieldArgs)
            {
                const auto eq = tok.find('=');
                if (eq == std::string::npos || eq == 0)
                {
                    err = "expected <address>=<value>, got `" + tok + "`";
                    if (!out.fields.empty() && !out.fields.back().second.empty() && out.fields.back().second[0] == '=')
                        err += " — a formula with spaces is quoted: " + out.fields.back().first + "=\"" + out.fields.back().second + " …\"";
                    return c;
                }
                out.fields.emplace_back(tok.substr(0, eq), tok.substr(eq + 1));
                out.args.push_back(tok);
                continue;
            }
            out.args.push_back(tok);
        }
        for (const auto &f : spec->flags)
            if (std::find(spec->required.begin(), spec->required.end(), flagName(f)) != spec->required.end() && !out.has(flagName(f)))
            {
                const auto eq = f.find('=');
                err = spec->verb + " needs --" + flagName(f) + (eq == std::string::npos ? std::string() : " " + f.substr(eq + 1));
                return c;
            }
        const int n = (int)out.args.size();
        if (n < spec->minArgs || (spec->maxArgs >= 0 && n > spec->maxArgs))
        {
            err = (n < spec->minArgs ? "too few arguments — usage: " : "too many arguments — usage: ") + usageOf(*spec);
            return c;
        }
        return out;
    }

    std::string formatCommand(const Command &c)
    {
        const CommandSpec *s = specFor(c.kind);
        if (!s) return std::string();
        std::string line = s->verb;
        if (s->fieldArgs)
            for (const auto &f : c.fields) line += " " + quoteToken(f.first + "=" + f.second);
        else
            for (const auto &a : c.args) line += " " + quoteToken(a);
        for (const auto &f : c.flags)
        {
            bool valued = false;
            for (const auto &sf : s->flags)
                if (flagName(sf) == f.first) valued = flagValued(sf);
            line += " --" + f.first;
            if (valued) line += " " + quoteToken(f.second);
        }
        return line;
    }
}
}
