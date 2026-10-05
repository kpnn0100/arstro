#include "Command.h"
#include <algorithm>

namespace arstro
{
namespace interstellar
{
    using K = Command::Kind;

    std::string Command::arg(size_t i, const std::string &fallback) const
    {
        return i < args.size() ? args[i] : fallback;
    }

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
        // The ONE grammar table (R-API-1). Order is the order `api` prints, grouped as §8 is.
        static const std::vector<CommandSpec> specs = {
            {K::ProjectNew, "project new", "<path.isp>", 1, 1, {"fps=<n>", "res=<WxH>"},
             "Create a project and its rack (an empty .cmp beside it), and open it.", "R-SCOPE-2"},
            {K::ProjectOpen, "project open", "<path.isp>", 1, 1, {},
             "Open a project: its timelines, and the hosted Cosmo project its #rack names.", "R-SCOPE-2"},
            {K::ProjectSave, "project save", "[path.isp]", 0, 1, {},
             "Save the .isp and the rack's .cmp. A path saves the .isp there.", "R-RACK-2"},
            {K::ProjectClose, "project close", "", 0, 0, {}, "Close the project and return Home (its autosave goes: closing is a decision).", "R-UI-1"},
            {K::ProjectAutosave, "project autosave", "", 0, 0, {},
             "Write the autosave now — the .isp and every rack node's grade beside the project (<stem>.autosave.*); "
             "it happens by itself every `autosave` seconds while there are unsaved changes.", "R-DLV-5"},
            {K::ProjectRecover, "project recover", "", 0, 0, {"discard"},
             "Put back an autosave newer than the project file (after a crash): the .isp and every grade, through Cosmo; "
             "the project is left unsaved so you decide. --discard removes the autosave instead.", "R-DLV-6"},

            {K::ColourWorking, "colour working", "<rec709|acescct>", 1, 1, {},
             "The project's working space: Rec.709 (display-referred, Cosmo's own — the default) or ACEScct "
             "(scene-referred; the monitor and each render then apply an output transform).", "R-COLOR-3"},
            {K::RackImport, "rack import", "<path.cmp>", 1, 1, {},
             "Point the rack at an existing Cosmo project — its groups and grades are the rack.", "R-RACK-1"},
            {K::RackAdd, "rack add", "<media…>", 1, -1, {},
             "Add photos or videos to the rack. A video is graded on a reference frame "
             "(`clip.mp4#t=2.0` picks it).", "R-RACK-3"},
            {K::RackGroupNew, "rack group new", "[name]", 0, 1, {"nodes=<a,b,…>"},
             "Group rack nodes — the named ones, else the selection; a group's grade stacks onto every "
             "descendant. No name: one is made up, as Cosmo does.", "R-RACK-4"},
            {K::RackDuplicate, "rack duplicate", "<node>", 1, 1, {"name=<bind>"},
             "Duplicate a rack node as a variant: same media, its own grade.", "R-RACK-5"},
            {K::RackFrame, "rack frame", "<node>", 1, 1, {"at=<t>"},
             "Choose which frame of a video source Cosmo grades. Changes no parameter.", "R-RACK-3"},
            {K::RackRename, "rack rename", "<node> <bind>", 2, 2, {},
             "Change a rack node's bind name (what an address spells).", "R-RACK-6"},
            {K::RackSelect, "rack select", "<node>", 1, 1, {"add", "range"},
             "Make a rack node the Grade target and the selection. --add toggles it into the selection "
             "(Ctrl-click); --range selects from the last clicked node to it (Shift-click).", "R-RACK-8"},
            {K::RackRemove, "rack remove", "<node>", 1, 1, {},
             "Take a source out of the rack (Cosmo's delete). Refused while a clip uses it.", "R-RACK-8"},

            {K::Set, "set", "<address>=<value> …", 1, -1, {},
             "Write addresses. A rack address writes THROUGH to Cosmo on a root timeline and "
             "becomes this version's override on a derived one.", "R-RACK-2", true},
            {K::Get, "get", "<address>", 1, 1, {}, "Print an address's stored value.", "R-API-2"},
            {K::Eval, "eval", "<address>", 1, 1, {"timeline=<tl>", "explain"},
             "Print an address's RESOLVED value in a timeline; --explain names every layer.", "R-API-2"},
            {K::Revert, "revert", "<address>", 1, 1, {"timeline=<tl>"},
             "Drop this version's override at an address, so it inherits from its base again.", "R-VER-2"},

            {K::TimelineNew, "timeline new", "<name>", 1, 1, {"base=<tl>"},
             "Create a timeline; with --base it is a version of that one (stores deltas only).", "R-VER-1"},
            {K::TimelineList, "timeline list", "", 0, 0, {}, "List timelines as a version tree.", "R-VER-5"},
            {K::TimelineOpen, "timeline open", "<tl>", 1, 1, {}, "Make a timeline the editor's current one.", "R-UI-4"},
            {K::TimelinePin, "timeline pin", "<tl>", 1, 1, {"commit=<c>"},
             "Stop inheriting the base's colour: freeze it at a snapshot of the rack.", "R-VER-3"},
            {K::TimelineUnpin, "timeline unpin", "<tl>", 1, 1, {}, "Inherit the base's colour live again.", "R-VER-3"},
            {K::TimelineFreeze, "timeline freeze", "<tl>", 1, 1, {},
             "Stop inheriting the base's arrangement: materialise the resolved cut.", "R-VER-3"},
            {K::TimelineThaw, "timeline thaw", "<tl>", 1, 1, {},
             "Inherit the base's arrangement live again.", "R-VER-3"},
            {K::TimelineRebase, "timeline rebase", "<tl>", 1, 1, {"dry-run"},
             "Report and prune dangling deltas; advance a pin to the rack's present state.", "R-VER-4"},
            {K::TimelineDiff, "timeline diff", "<tl>", 1, 1, {},
             "What this version changes relative to its base.", "R-VER-2"},
            {K::TimelineDelete, "timeline delete", "<tl>", 1, 1, {},
             "Delete a timeline. Refused while another version is based on it.", "R-VER-1"},

            {K::TrackAdd, "track add", "", 0, 0, {"kind=<video|audio>", "name=<n>"},
             "Add a track to the current timeline.", "R-TL-1"},
            {K::ClipAdd, "clip add", "", 0, 0,
             {"track=<trk>", "src=<rackobj|timeline>", "in=<t>", "out=<t>", "at=<t>", "name=<n>"},
             "Place a span of a rack source — or of another timeline, nested (R-EDT-4) — on a track. Without "
             "--out, the rest of the source from --in (a still: 5 s; a timeline: to its last clip's end) — what "
             "a drag from the source bin drops. A timeline that would end up inside itself is refused.", "R-TL-1"},
            {K::ClipTrim, "clip trim", "<clip>", 1, 1, {"in=<t>", "out=<t>"},
             "Set a clip's source in and/or out point (seconds into the SOURCE, like `<clip>.in`); "
             "trimming the head keeps the remaining frames where they were on the timeline.", "R-TL-3"},
            {K::ClipSplit, "clip split", "<clip>", 1, 1, {"at=<t>"}, "Cut a clip in two at a timeline time.", "R-TL-3"},
            {K::ClipMove, "clip move", "<clip>", 1, 1, {"at=<t>", "track=<trk>"},
             "Move a clip in time and/or to another track.", "R-TL-3"},
            {K::ClipDelete, "clip delete", "<clip>", 1, 1, {"ripple"},
             "Remove a clip; --ripple closes the gap.", "R-TL-3"},
            {K::ClipRoll, "clip roll", "<clip>", 1, 1, {"at=<t>"},
             "Move the cut between a clip and its right neighbour.", "R-TL-3"},
            {K::ClipSlip, "clip slip", "<clip>", 1, 1, {"by=<dt>"},
             "Shift a clip's source range without moving it on the timeline.", "R-TL-3"},
            {K::ClipSpeed, "clip speed", "<clip> <speed>", 2, 2, {}, "Set a clip's playback speed.", "R-TL-3"},
            {K::ClipSelect, "clip select", "[clip]", 0, 1, {}, "Select a clip (none clears).", "R-UI-3"},
            {K::EffectAdd, "effect add", "<node>", 1, 1, {"type=<plugin>"},
             "Add an image-processing plugin to a rack node's stack, after Cosmo and the node's other effects; "
             "prints its id (`ef_<n>`, stable for its life). Parameters are addresses: `set ef_3.radius=12`.", "R-FX-5"},
            {K::EffectRemove, "effect remove", "<effect>", 1, 1, {}, "Remove a plugin from its node's stack.", "R-FX-5"},
            {K::EffectMove, "effect move", "<effect>", 1, 1, {"to=<index>"},
             "Move a plugin to position <index> of its node's stack (0 = first after Cosmo).", "R-FX-5"},
            {K::ClipCopy, "clip copy", "<clip>", 1, 1, {},
             "Keep a clip — its source range, speed, geometry, opacity and blend — to paste.", "R-TL-6"},
            {K::ClipPaste, "clip paste", "", 0, 0, {"at=<t>", "track=<trk>"},
             "Place a new clip from the copied one (default: at the playhead, on its track).", "R-TL-6"},
            {K::TransitionAdd, "transition add", "", 0, 0, {"between=<a,b>", "kind=<dissolve|dip>", "dur=<s>"},
             "Dissolve between two adjacent clips; the outgoing clip is HELD through it.", "R-TL-4"},
            {K::MarkerAdd, "marker add", "<name>", 1, 1, {"at=<t>", "note=<text>"},
             "Drop a named marker on the current timeline.", "R-TL-1"},

            {K::FxAdd, "fx add", "", 0, 0,
             {"node=<rackobj>", "clip=<clip>", "type=<denoise|blend|freeze>", "radius=<n>",
              "strength=<0..1>", "shutter=<deg>", "at=<t>"},
             "Attach a temporal effect to a rack node (footage) or a clip (editorial).", "R-FX-2"},
            {K::FxDelete, "fx delete", "<fx>", 1, 1, {}, "Remove a temporal effect.", "R-FX-2"},

            {K::AudioTrackAdd, "audio track add", "", 0, 0, {"name=<n>"},
             "Add an audio track (suite schema `#atrack kind=audio`).", "R-AUD-2"},
            {K::AudioClipAdd, "audio clip add", "", 0, 0,
             {"track=<atrk>", "src=<file>", "at=<t>", "in=<t>", "out=<t>", "gain=<dB>", "fade=<s>"},
             "Place a sound on an audio track: a file, or a rack source's own sound (`--src <bind>`). "
             "Without --out it runs to the end of the file's sound.", "R-AUD-2"},

            {K::Undo, "undo", "", 0, 0, {},
             "Step back one edit — a grade, an override, a cut, a version change — across the rack and "
             "the project alike.", "R-EDIT-1"},
            {K::Redo, "redo", "", 0, 0, {}, "Step forward again after an undo.", "R-EDIT-1"},
            {K::GradeCopy, "grade copy", "<node>", 1, 1, {},
             "Copy a rack node's grade (its own params, masks excluded) to the clipboard.", "R-EDIT-2"},
            {K::GradePaste, "grade paste", "[node…]", 0, -1, {"all"},
             "Paste the copied grade onto rack nodes (or every source with --all). Root timeline only: "
             "it writes through to Cosmo.", "R-EDIT-2"},
            {K::RackUngroup, "rack ungroup", "<group>", 1, 1, {},
             "Dissolve a group; its members keep their own grades.", "R-RACK-4"},
            {K::SettingsSet, "settings set", "<key>=<value> …", 1, -1, {},
             "Engine settings: cpuPercent (25|50|75|100), threads (0=auto), previewEdge (px), useGpu "
             "(0|1), uiScale (%), hardwareVideo (0|1: H.264/H.265 on the GPU's video unit), previewCache (0|1: build the graded "
             "preview cache when idle), keyLaneHeight (px, 80..600: the Cut key lane). Persisted; one CPU budget for the rack and "
             "the render path.", "R-SET-1", true},
            {K::PresetApply, "preset apply", "<name>", 1, 1, {"node=<bind>"},
             "Apply a library preset to a rack source (the Grade target by default). Root timeline only.",
             "R-EDIT-3"},
            {K::PresetSave, "preset save", "<name>", 1, 1, {"node=<bind>"},
             "Save a rack source's grade to the library as <name>.apf.", "R-EDIT-3"},
            {K::PresetImport, "preset import", "<path.apf>", 1, 1, {},
             "Copy an .apf (from Cosmo or anywhere) into the library.", "R-EDIT-3"},

            {K::Playhead, "playhead", "<t>|+<dt>|-<dt>|next-cut|prev-cut", 1, 1, {},
             "Move the playhead; snapped to a frame.", "R-TL-5"},
            {K::Play, "play", "", 0, 0, {}, "Start playback of the current timeline.", "R-UI-3"},
            {K::Pause, "pause", "", 0, 0, {}, "Stop playback.", "R-UI-3"},

            {K::Render, "render", "", 0, 0,
             {"timeline=<tl>", "out=<path>", "range=<a:b>", "format=<h264|h265|prores|dnxhr|png-seq>",
              "profile=<proxy|lt|standard|hq|4444 · lb|sq|hq|hqx|444>", "res=<WxH>", "fps=<n|num/den>",
              "quality=<0..51>", "speed=<ultrafast…veryslow>", "bits=<8|10>", "encoder=<software|hardware>",
              "output=<rec709|rec709-2.4|srgb|p3d65|pq|hlg>", "peak=<400..10000>", "preset=<name>",
              "burnin=<tc@bl,clip@tl,source@tr,srctc@br,text=…@tc>"},
             "Queue a render of a NAMED timeline (no implicit current one), with its whole output spec: "
             "codec and profile, size (never above the project, same aspect), frame rate (the timeline is "
             "sampled at it), constant quality and encoder speed for H.264/H.265, bit depth for H.265, the "
             "output colour transform (HDR PQ/HLG need 10 bits; --peak is PQ's mastering peak in cd/m²). A "
             "flag the codec cannot honour is refused.", "R-RENDER-6"},
            {K::RenderCancel, "render cancel", "<job>", 1, 1, {}, "Cancel a queued or running render.", "R-RENDER-4"},
            {K::RenderPresetSave, "render preset save", "<name>", 1, 1,
             {"format=<h264|h265|prores|dnxhr|png-seq>", "profile=<p>", "res=<WxH>", "fps=<n|num/den>", "quality=<0..51>",
              "speed=<s>", "bits=<8|10>", "encoder=<software|hardware>", "output=<o>", "peak=<cd/m²>"},
             "Save a render's whole output spec by name (beside the engine settings: every project has it); `render "
             "--preset <name>` applies it, flags given beside it winning. --res is a frame to fit in at the project's aspect.",
             "R-DLV-3"},
            {K::RenderPresetDelete, "render preset delete", "<name>", 1, 1, {}, "Forget a saved render preset (the built-in ones stay).", "R-DLV-3"},
            {K::RenderPresetList, "render preset list", "", 0, 0, {},
             "The render presets: YouTube 1080p, ProRes HQ master and Review H.264 built in, then the saved ones.", "R-DLV-3"},
            {K::CacheBuild, "cache build", "", 0, 0, {},
             "Build the current timeline's graded preview cache now (a window also builds it when idle): "
             "one-second H.264 segments at the playing size, every frame checked by its plan, so only what "
             "an edit changed is rebuilt. Playback reads it; a render never does. `wait cache.done`.", "R-PLAY-1"},
            {K::CacheClear, "cache clear", "", 0, 0, {}, "Delete the current timeline's preview cache.", "R-PLAY-1"},
            {K::KeyAdd, "key add", "<address>", 1, 1, {"at=<t>", "value=<v>", "ease=<linear|ease|ease-in|ease-out|hold>"},
             "Add a keyframe to a parameter's curve (making the curve if it has none) — a rack node's colour key or an effect's "
             "parameter at a SOURCE time, a clip's opacity or geometry at a time on the clip's own footage clock. --at defaults to "
             "now on that clock (the source's reference frame; the playhead inside the clip), --value to what the parameter "
             "shows there. Root timeline only for the rack and effects: versions inherit curves (R-ANIM-5).", "R-ANIM-1"},
            {K::KeyRemove, "key remove", "<address>", 1, 1, {"at=<t>"},
             "Remove the keyframe at --at (default now). The last one takes the curve with it; its value stays.", "R-ANIM-1"},
            {K::KeySet, "key set", "<address>", 1, 1,
             {"at=<t>", "to=<t>", "value=<v>", "in=<linear|bezier|hold>", "out=<linear|bezier|hold>", "speed-in=<units/s>",
              "influence-in=<%>", "speed-out=<units/s>", "influence-out=<%>", "ease=<linear|ease|ease-in|ease-out|hold>"},
             "Shape the keyframe at --at: move it (--to), change its value, its incoming/outgoing interpolation, speed (units per "
             "second) and influence (% of the segment) — After Effects' model; giving a speed or influence makes that side a "
             "bezier. --ease applies a preset.", "R-ANIM-2"},
            {K::KeyShift, "key shift", "", 0, 0, {"keys=<address@t,…>", "by=<s>"},
             "Move several keyframes in time together, by --by seconds (one undo step) — a box-selection dragged in the "
             "graph. Refused when a key would land on another of its curve.", "R-ANIM-7"},
            {K::KeyCopy, "key copy", "", 0, 0, {"keys=<address@t,…>"},
             "Copy keyframes (their values, shapes and sides, times relative to the earliest) to the key clipboard.", "R-ANIM-7"},
            {K::KeyPaste, "key paste", "", 0, 0, {"at=<t>", "to=<address>"},
             "Paste the copied keyframes with the earliest at --at (default: now on each curve's clock) — onto the properties "
             "they came from, or onto --to when one property was copied. Same-time keys are replaced.", "R-ANIM-7"},
            {K::KeyClear, "key clear", "<address>", 1, 1, {},
             "Remove a parameter's curve; the value it had now stays as the parameter's own.", "R-ANIM-1"},
            {K::ExportStill, "export-still", "", 0, 0, {"timeline=<tl>", "out=<p.png>", "at=<t>"},
             "Write one composited frame of a named timeline.", "R-RENDER-5"},
            {K::Shuttle, "shuttle", "<forward|back|stop>", 1, 1, {},
             "J/K/L: forward plays at 1×, then 2× and 4× on each press; back the same in reverse; stop pauses. "
             "Sound is heard at 1× forward only.", "R-EDT-2"},
            {K::Mark, "mark", "<in|out|clear>", 1, 1, {"at=<t>", "source"},
             "Set the timeline's In or Out at the playhead (or --at), or clear both; --source marks the source viewer.", "R-EDT-1"},
            {K::SourceView, "source view", "<node|none>", 1, 1, {},
             "Show a rack source in the viewer, with its own playhead and In/Out, for three-point editing; none = the timeline.", "R-EDT-1"},
            {K::SourcePlayhead, "source playhead", "<t>", 1, 1, {}, "Move the source viewer's playhead (source seconds).", "R-EDT-1"},
            {K::EditTarget, "edit target", "<track>", 1, 1, {}, "The video track Insert and Overwrite place on.", "R-EDT-1"},
            {K::EditInsert, "edit insert", "", 0, 0, {"src=<node>", "in=<t>", "out=<t>", "at=<t>", "track=<trk>"},
             "Three-point INSERT: the source (the viewer's, or --src) between its In/Out at the timeline In (or the "
             "playhead) on the target track; a clip there is split and everything after moves right on every track. "
             "Any three of source In/Out and timeline In/Out decide the fourth.", "R-EDT-1"},
            {K::EditOverwrite, "edit overwrite", "", 0, 0, {"src=<node>", "in=<t>", "out=<t>", "at=<t>", "track=<trk>"},
             "Three-point OVERWRITE: as insert, but what lies on the target track in the new clip's range is cut away "
             "and nothing moves.", "R-EDT-1"},
            {K::MulticamNew, "multicam new", "<name>", 1, 1,
             {"sources=<a,b,...>", "sync=<timecode|in>", "in=<src=t,...>", "audio=<src|none>", "track=<trk>", "at=<t>"},
             "A multicam: a new timeline with one video track per source (angle 1, 2, … in the order given), lined up "
             "by their timecode (default) or by their in-points (--in a=1.5,b=0.4 — source seconds, 0 when not given), "
             "and the sound of one source (--audio, default the first; none for silence). With --track, also placed "
             "on the current timeline at --at (else the playhead), showing angle 1.", "R-EDT-5"},
            {K::ProxyMake, "proxy make", "[<source>…]", 0, -1, {"codec=<prores|h264>", "edge=<px>"},
             "Make a proxy of each named video source (none named: every one without a proxy) — ProRes Proxy (default) or "
             "H.264, its long edge --edge px (960 unless given), in <project>.proxies/ — as a background job like a render. "
             "The monitor uses it while `proxy use on`; renders never do.", "R-MEDIA-2"},
            {K::ProxyRemove, "proxy remove", "<source…>", 1, -1, {},
             "Forget a source's proxy: the monitor decodes the original again. The file stays on disk.", "R-MEDIA-2"},
            {K::ProxyUse, "proxy use", "<on|off>", 1, 1, {},
             "The project's switch: the monitor (and playback, the preview cache, capture) decodes each source's proxy where "
             "it has one — or the originals. Renders and export-still always decode the originals.", "R-MEDIA-2"},
            {K::MediaOffline, "media offline", "", 0, 0, {},
             "List the offline sources in one place: each one's bind name, the file it names, and why it is offline.", "R-MEDIA-3"},
            {K::MediaRelink, "media relink", "[<source> <file>]", 0, 2, {"search=<folder>"},
             "Point a source at its file where it is now — `<source> <file>` (a CinemaDNG folder or pattern too), or "
             "--search <folder>: every offline source found there by its file name (a sequence by its folder). The grade, "
             "its history in this session and every clip stay; Cosmo keeps the slot it had. Refused, saying why, if the "
             "rack's structure changed since it was saved (Cosmo cannot save while a source is offline, D-2).", "R-MEDIA-3"},
            {K::ViewMatte, "view matte", "<on|off>", 1, 1, {},
             "Grade's monitor shows the selected source's matte — where its grade reaches, white — instead of its picture: "
             "the key its qualifiers and windows (and its groups') make, so a key is pulled by looking at it.", "R-CLR-1"},
            {K::TrackWindow, "track window", "<effect>", 1, 1, {"to=<source seconds>", "back"},
             "Make a window follow what is under it: from the playhead's frame of its source, forward (or --back) to "
             "--to (else the source's end or start), the window's centre keyed on every frame where the patch it "
             "covers is found again. A background job, one frame per pump; one undo step.", "R-CLR-2"},
            {K::TrackCancel, "track cancel", "", 0, 0, {}, "Stop the running track; what it keyed stays (one undo step).", "R-CLR-2"},
            {K::StillGrab, "still grab", "[<source>]", 0, 1, {"name=<n>"},
             "Keep what the monitor shows as a still, with the grade it was made with: a source's graded reference frame "
             "(named), or the timeline at the playhead with the grade of the clip on top there. Saved beside the project "
             "(<project>.stills/); the grade is a snapshot to apply, never an authority.", "R-CLR-4"},
            {K::StillApply, "still apply", "<still> [<node>…]", 1, -1, {},
             "Apply a still's grade to rack nodes (else the Grade target) — through Cosmo, as grade paste writes; one undo "
             "step. On the root timeline (a version stores overrides).", "R-CLR-4"},
            {K::StillDelete, "still delete", "<still>", 1, 1, {}, "Remove a still and its files.", "R-CLR-4"},
            {K::ViewWipe, "view wipe", "[<still|timeline|off>]", 0, 1, {"split=<vertical|horizontal>", "at=<0..1>"},
             "Split the monitor between its picture and a reference — a still, or another version at the playhead — "
             "left | right (vertical) or top / bottom, the split at --at of the frame (0.5 unless moved). With no name, "
             "only the split moves. Presentation: renders and export-still never wipe.", "R-CLR-5"},
            {K::NodeSerial, "node serial", "<node>", 1, 1, {"name=<n>"},
             "Add a SERIAL node after a rack node: a new group around it, whose grade applies to its result — the rack's "
             "groups are the graph's serial chain.", "R-CLR-3"},
            {K::NodeParallel, "node parallel", "<source>", 1, 1, {"name=<n>"},
             "Add a PARALLEL node to a source: a variant grading the same input beside it, its difference from the input "
             "added to the source's result by <variant>.parallelMix (1 unless set). It starts empty, so nothing changes "
             "until it is graded.", "R-CLR-3"},
            {K::NodeRemove, "node remove", "<node>", 1, 1, {},
             "Remove a serial node (ungroups it) or a parallel node (removes the variant).", "R-CLR-3"},
            {K::MulticamAngle, "multicam angle", "<n>", 1, 1, {"clip=<clip>", "at=<t>"},
             "Switch the multicam clip under the playhead (or --at; --clip names it) to angle n FROM there: the clip "
             "is cut at that frame and the rest shows angle n — at its first frame, the whole clip does. One undo step.",
             "R-EDT-5"},
            {K::InterchangeExport, "interchange export", "<timeline>", 1, 1,
             {"format=<edl|fcpxml|otio|aaf>", "out=<path>", "track=<n>", "start=<HH:MM:SS:FF>"},
             "Write a timeline for another editor: a CMX 3600 EDL (one video track, --track; reels and source "
             "timecode), FCPXML 1.9 or OpenTimelineIO — the format from --format or the extension. The record "
             "clock starts at 01:00:00:00 unless --start. AAF is refused, naming the way to it.", "R-XCH-1"},
            {K::InterchangeImport, "interchange import", "<file>", 1, 1,
             {"format=<edl|fcpxml|otio>", "media=<dir>", "name=<timeline>", "fps=<rate>"},
             "Read an EDL, FCPXML or OTIO into a NEW root timeline: its media added to the rack (found by path, or "
             "by clip/reel name under --media), its tracks, clips, dissolves and speeds. An EDL's rate is the "
             "project's unless --fps. Media that cannot be found is listed, not placed.", "R-XCH-1"},
            {K::LutExport, "lut export", "<source>", 1, 1, {"out=<file.cube>", "size=<2..129>", "output=<rec709|rec709-2.4|srgb|p3d65|pq|hlg>"},
             "Bake a source's colour — its input transform and LUT, its grade as the open version folds it, its weight, "
             "its LUT effects — into a 3D .cube (33 points unless --size), optionally through an output transform. "
             "What is not per-pixel colour is left out and said so in the file.", "R-COLOR-6"},

            {K::Capture, "capture", "", 0, 0, {"out=<p.png>", "source=<bind>"},
             "Save what the monitor shows, at full resolution: --source names a rack source (its "
             "reference frame, graded — Grade); without it, the current timeline at the playhead.", "R-UI-11"},
            {K::StatePrint, "state print", "", 0, 0, {"json", "stable"},
             "Print the AppModel; --stable omits machine-dependent fields.", "R-API-2"},
            {K::Api, "api", "", 0, 0, {"json", "md"}, "Print this document.", "R-API-1"},
            {K::Lint, "lint", "", 0, 0, {}, "Report offline media, dangling deltas and refused fields.", "R-RACK-7"},
            {K::Wait, "wait", "<rack.loaded|render.done|frame.ready|cache.done>", 1, 1, {"timeout=<dur>"},
             "Block (pumping) until a condition holds.", "R-API-2"},
            {K::Quit, "quit", "", 0, 0, {}, "End a script or session.", "R-API-2"},
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
                    cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] != b[j - 1])});
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
            // A prefix match is a near miss however long the rest is (`time` → `timeline …`).
            const bool prefix = !word.empty() && p.compare(0, word.size(), word) == 0;
            if (prefix || d <= std::max<int>(2, (int)p.size() / 3)) scored.emplace_back(prefix ? 0 : d, p);
        }
        std::stable_sort(scored.begin(), scored.end(),
                         [](const auto &a, const auto &b) { return a.first < b.first; });
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
            u += eq == std::string::npos ? " [--" + f + "]" : " [--" + f.substr(0, eq) + " " + f.substr(eq + 1) + "]";
        }
        return u;
    }

    Command parseCommand(const std::string &line, std::string &err)
    {
        err.clear();
        Command c;

        // A comment is a WHOLE line starting with `#` or `;`. Not an inline one: `#` appears in a
        // frame selector (`clip.mp4#t=2.0`) and `;` in every curve value (`0,0;0.5,0.62;1,1`).
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#' || line[first] == ';') return c;

        const std::vector<std::string> t = tokenize(line);
        if (t.empty()) return c;

        // Longest verb match: `audio clip add` beats `audio`, `rack group new` beats `rack`.
        const CommandSpec *spec = nullptr;
        size_t verbWords = 0;
        for (const auto &s : commandSpecs())
        {
            const std::vector<std::string> words = tokenize(s.verb);
            if (words.size() > t.size() || words.size() <= verbWords) continue;
            if (std::equal(words.begin(), words.end(), t.begin()))
            {
                spec = &s;
                verbWords = words.size();
            }
        }
        if (!spec)
        {
            std::vector<std::string> verbs;
            for (const auto &s : commandSpecs()) verbs.push_back(s.verb);
            // Compare against as many words as the candidate has, so `timline pin` finds `timeline pin`.
            std::vector<std::string> tried = {t[0]};
            if (t.size() > 1) tried.push_back(t[0] + " " + t[1]);
            if (t.size() > 2) tried.push_back(t[0] + " " + t[1] + " " + t[2]);
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
                    return c;
                }
                out.fields.emplace_back(tok.substr(0, eq), tok.substr(eq + 1));
                out.args.push_back(tok);
                continue;
            }
            out.args.push_back(tok);
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
            for (const auto &spec : s->flags)
                if (flagName(spec) == f.first) valued = flagValued(spec);
            line += " --" + f.first;
            if (valued) line += " " + quoteToken(f.second);
        }
        return line;
    }
}
}
