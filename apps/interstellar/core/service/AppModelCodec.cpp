#include "AppModelCodec.h"
#include "engine/EditParamsIO.h"
#include <cstdlib>
#include <sstream>

namespace arstro
{
namespace interstellar
{
    const char *screenName(Screen s)
    {
        switch (s)
        {
            case Screen::Home: return "home";
            case Screen::Loading: return "loading";
            case Screen::Edit: return "edit";
        }
        return "unknown";
    }

    const char *provenanceName(Provenance p)
    {
        switch (p)
        {
            case Provenance::Local: return "local";
            case Provenance::Inherited: return "inherited";
            case Provenance::Overridden: return "overridden";
            case Provenance::Dangling: return "dangling";
        }
        return "unknown";
    }

    namespace
    {
        std::string baseName(const std::string &p)
        {
            const auto s = p.find_last_of('/');
            return s == std::string::npos ? p : p.substr(s + 1);
        }

        /** EditParams as an object of EditParamsIO keys, in the codec's own order, so the keys an
         *  agent reads here are exactly the keys it writes with `set <bind>.<filter>.<key>`. */
        Json paramsToJson(const EditParams &p)
        {
            Json o = Json::object(), masks = Json::array();
            std::istringstream in(serializeParams(p));
            std::string line;
            while (std::getline(in, line))
            {
                const auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
                if (k == "mask") { masks.push(Json::string(v)); continue; }
                char *end = nullptr;
                const double d = std::strtod(v.c_str(), &end);
                if (!v.empty() && end && *end == '\0') o.set(k, Json::number(d));
                else o.set(k, Json::string(v));
            }
            o.set("masks", masks);
            return o;
        }
    }

    Json modelToJson(const AppModel &m, const ModelDumpOptions &opt)
    {
        const bool st = opt.stable;
        Json j = Json::object();
        if (!st) j.set("revision", m.revision);
        j.set("screen", screenName(m.screen));
        if (!st)
        {
            Json rec = Json::array();
            for (const auto &r : m.recents)
                rec.push(Json::object()
                             .set("name", r.name)
                             .set("path", r.path)
                             .set("coverPath", r.coverPath)
                             .set("sourceCount", r.sourceCount)
                             .set("sizeBytes", r.sizeBytes)
                             .set("lastOpened", r.lastOpened));
            j.set("recents", rec);
        }

        j.set("projectPath", st ? baseName(m.projectPath) : m.projectPath);
        j.set("projectName", m.projectName);
        j.set("dirty", m.dirty);
        j.set("fps", m.fps);
        j.set("width", m.width);
        j.set("height", m.height);

        Json rack = Json::array();
        for (const auto &r : m.rack)
            rack.push(Json::object()
                          .set("node", r.node)
                          .set("rackObj", r.rackObj)
                          .set("bindName", r.bindName)
                          .set("cosmoName", r.cosmoName)
                          .set("parent", r.parent)
                          .set("depth", r.depth)
                          .set("group", r.group)
                          .set("bypass", r.bypass)
                          .set("pending", r.pending)
                          .set("failed", r.failed)
                          .set("weight", r.weight)
                          .set("media", st ? baseName(r.media) : r.media)
                          .set("frame", r.frame)
                          .set("video", r.video)
                          .set("usedBy", r.usedBy)
                          .set("overridden", r.overridden)
                          .set("selected", r.selected)
                          .set("mediaDuration", r.mediaDuration)
                          .set("mediaFps", r.mediaFps)
                          .set("sharesMedia", r.sharesMedia));
        j.set("rack", rack);
        j.set("selectedRack", m.selectedRack);
        j.set("hasGradeTarget", m.hasGradeTarget);
        if (opt.params)
        {
            j.set("gradeParams", paramsToJson(m.gradeParams));
            j.set("gradeOwnParams", paramsToJson(m.gradeOwnParams));
        }
        j.set("sourceWidth", m.sourceWidth);
        j.set("sourceHeight", m.sourceHeight);

        Json tls = Json::array();
        for (const auto &t : m.timelines)
            tls.push(Json::object()
                         .set("id", t.id)
                         .set("name", t.name)
                         .set("base", t.base)
                         .set("depth", t.depth)
                         .set("colourPinned", t.colourPinned)
                         .set("pinCommit", t.pinCommit)
                         .set("cutFrozen", t.cutFrozen)
                         .set("danglingDeltas", t.danglingDeltas)
                         .set("overrides", t.overrides));
        j.set("timelines", tls);
        j.set("currentTimeline", m.currentTimeline);

        Json tracks = Json::array();
        for (const auto &t : m.tracks)
            tracks.push(Json::object()
                            .set("id", t.id)
                            .set("name", t.name)
                            .set("audio", t.audio)
                            .set("order", t.order)
                            .set("mute", t.mute)
                            .set("opacity", t.opacity)
                            .set("gain", t.gain)
                            .set("provenance", provenanceName(t.provenance)));
        j.set("tracks", tracks);

        Json clips = Json::array();
        for (const auto &c : m.clips)
            clips.push(Json::object()
                           .set("id", c.id)
                           .set("name", c.name)
                           .set("track", c.track)
                           .set("src", c.audio && st ? baseName(c.src) : c.src)
                           .set("srcName", c.srcName)
                           .set("at", c.at)
                           .set("in", c.in)
                           .set("out", c.out)
                           .set("speed", c.speed)
                           .set("duration", c.duration)
                           .set("opacity", c.opacity)
                           .set("gain", c.gain)
                           .set("audio", c.audio)
                           .set("offline", c.offline)
                           .set("provenance", provenanceName(c.provenance)));
        j.set("clips", clips);

        Json trs = Json::array();
        for (const auto &t : m.transitions)
            trs.push(Json::object().set("id", t.id).set("clipA", t.clipA).set("clipB", t.clipB).set("kind", t.kind).set("dur", t.dur));
        j.set("transitions", trs);

        Json mks = Json::array();
        for (const auto &k : m.markers)
            mks.push(Json::object().set("id", k.id).set("name", k.name).set("at", k.at).set("note", k.note));
        j.set("markers", mks);

        j.set("selectedClip", m.selectedClip);
        j.set("duration", m.duration);
        j.set("playhead", m.playhead);
        j.set("playheadFrame", m.playheadFrame);
        j.set("playing", m.playing);
        if (!st)
        {
            j.set("frameSeq", m.frameSeq);
            j.set("frameWidth", m.frameWidth);
            j.set("frameHeight", m.frameHeight);
        }

        Json renders = Json::array();
        for (const auto &r : m.renders)
            renders.push(Json::object()
                             .set("id", r.id)
                             .set("timeline", r.timeline)
                             .set("timelineName", r.timelineName)
                             .set("outPath", st ? baseName(r.outPath) : r.outPath)
                             .set("format", r.format)
                             .set("done", r.done)
                             .set("total", r.total)
                             .set("state", r.state)
                             .set("error", r.error)
                             .set("width", r.width)
                             .set("height", r.height)
                             .set("fps", r.fps)
                             .set("spec", r.spec));
        j.set("renders", renders);
        j.set("canUndo", m.canUndo);
        j.set("canRedo", m.canRedo);
        j.set("undoLabel", m.undoLabel);
        j.set("redoLabel", m.redoLabel);
        j.set("hasGradeClipboard", m.hasGradeClipboard);
        j.set("gradeClipboardFrom", m.gradeClipboardFrom);
        j.set("hasClipClipboard", m.hasClipClipboard);
        j.set("clipClipboardFrom", m.clipClipboardFrom);
        Json settings = Json::object();
        settings.set("cpuPercent", m.settings.cpuPercent);
        settings.set("threads", m.settings.threads);
        settings.set("previewEdge", m.settings.previewEdge);
        settings.set("useGpu", m.settings.useGpu);
        settings.set("uiScale", m.settings.uiScale);
        if (!st)
        {
            settings.set("gpuAvailable", m.settings.gpuAvailable);
            settings.set("cores", m.settings.cores);
            settings.set("engineThreads", m.settings.engineThreads);
            settings.set("decodeWorkers", m.settings.decodeWorkers);
        }
        j.set("settings", settings);
        Json presets = Json::array();
        for (const auto &p : m.presets) presets.push(Json::object().set("name", p.name).set("folder", p.folder));
        j.set("presets", presets);
        j.set("lastError", m.lastError);
        return j;
    }

    namespace
    {
        void flatten(const Json &j, const std::string &path, std::string &out)
        {
            switch (j.type())
            {
                case Json::Type::Object:
                    for (const auto &kv : j.members()) flatten(kv.second, path.empty() ? kv.first : path + "." + kv.first, out);
                    break;
                case Json::Type::Array:
                    out += path + ".count=" + std::to_string(j.items().size()) + "\n";
                    for (size_t i = 0; i < j.items().size(); ++i)
                        flatten(j.items()[i], path + "[" + std::to_string(i) + "]", out);
                    break;
                default:
                {
                    std::string v = j.dump();
                    v.pop_back();   // dump()'s trailing newline
                    out += path + "=" + v + "\n";
                }
            }
        }
    }

    std::string formatModel(const AppModel &m, const ModelDumpOptions &o)
    {
        const Json j = modelToJson(m, o);
        if (o.json) return j.dump();
        std::string out;
        flatten(j, "", out);
        return out;
    }

    const std::vector<ModelFieldDoc> &appModelFields()
    {
        static const std::vector<ModelFieldDoc> f = {
            {"revision", "integer", "Rises on every change; a test asserts a command changed something.", true},
            {"screen", "enum(home|loading|edit)", "Which surface is up. Which TAB is not state — it is presentation."},
            {"recents", "array", "Home's project cards, newest first.", true},
            {"recents[].name", "string", "Project name.", true},
            {"recents[].path", "string", "The .isp path.", true},
            {"recents[].coverPath", "string", "First source's file, for the card cover; empty = none.", true},
            {"recents[].sourceCount", "integer", "Rack sources in the project.", true},
            {"recents[].sizeBytes", "integer", "Total size of the project's media.", true},
            {"recents[].lastOpened", "integer", "Unix seconds.", true},
            {"projectPath", "string", "Open project's .isp (file name only when stable)."},
            {"projectName", "string", "Open project's display name."},
            {"dirty", "bool", "Unsaved changes."},
            {"fps", "number", "Project frame rate."},
            {"width", "integer", "Delivery width."},
            {"height", "integer", "Delivery height."},
            {"rack", "array", "The hosted Cosmo project's nodes, flattened in tree order (R-RACK)."},
            {"rack[].node", "integer", "Cosmo node id."},
            {"rack[].rackObj", "string", "Interstellar's #rackobj id."},
            {"rack[].bindName", "string", "What an address spells: `<bindName>.basic.exposure`."},
            {"rack[].cosmoName", "string", "Cosmo's own name for the node (display)."},
            {"rack[].parent", "integer", "Index into rack of the parent group; -1 at the root."},
            {"rack[].depth", "integer", "Tree depth."},
            {"rack[].group", "bool", "A group: its grade stacks onto every descendant."},
            {"rack[].bypass", "bool", "Cosmo's bypass."},
            {"rack[].pending", "bool", "Pixels still decoding."},
            {"rack[].failed", "bool", "Offline: reads as missing, never as a stall (R-RACK-7)."},
            {"rack[].weight", "number", "Grade weight 0..1 (R-RACK-4)."},
            {"rack[].media", "string", "Source file (file name only when stable)."},
            {"rack[].frame", "number", "Reference frame a video is graded on, seconds (R-RACK-3)."},
            {"rack[].video", "bool", "A video source."},
            {"rack[].usedBy", "integer", "Clips referencing it in the current timeline."},
            {"rack[].overridden", "bool", "The current version carries a colour override on it."},
            {"rack[].selected", "bool", "In the selection that Group Selection groups (R-RACK-8)."},
            {"rack[].mediaDuration", "number", "Seconds of source once opened (selecting a video opens it); 0 = a still or not yet opened."},
            {"rack[].mediaFps", "number", "The source's own frame rate once opened; one ref-frame step is 1/mediaFps seconds. 0 = not yet opened."},
            {"rack[].sharesMedia", "integer", "How many OTHER sources use the same file — a variant and its original share one (R-RACK-5); 0 for a group."},
            {"selectedRack", "integer", "Index into rack of the Grade target; -1 = none."},
            {"hasGradeTarget", "bool", "gradeParams/gradeOwnParams are meaningful."},
            {"gradeParams", "object", "The target's EFFECTIVE params in the current version: stacked reach + overrides. Keys are EditParamsIO keys."},
            {"gradeOwnParams", "object", "The target's OWN params in the current version — what `set` changes."},
            {"sourceWidth", "integer", "Grade target's source width."},
            {"sourceHeight", "integer", "Grade target's source height."},
            {"timelines", "array", "Every timeline — every VERSION — base before derived (R-VER)."},
            {"timelines[].id", "string", "Timeline id."},
            {"timelines[].name", "string", "Name; render and open accept it."},
            {"timelines[].base", "string", "The timeline this is a version of; empty = a root."},
            {"timelines[].depth", "integer", "Depth in the version tree."},
            {"timelines[].colourPinned", "bool", "Colour pinned to a rack snapshot."},
            {"timelines[].pinCommit", "string", "The snapshot when pinned."},
            {"timelines[].cutFrozen", "bool", "Arrangement frozen."},
            {"timelines[].danglingDeltas", "integer", "Deltas whose target the base deleted — `rebase` reports them."},
            {"timelines[].overrides", "integer", "How far this version has diverged: #tlset + #tlgrade + #tldrop count."},
            {"currentTimeline", "string", "The editor's current timeline id. Never what a render uses."},
            {"tracks", "array", "The resolved current timeline's tracks."},
            {"tracks[].id", "string", "Track id."},
            {"tracks[].name", "string", "Track name."},
            {"tracks[].audio", "bool", "An audio track (#atrack)."},
            {"tracks[].order", "integer", "Stacking order; higher draws on top."},
            {"tracks[].mute", "bool", "Muted / hidden."},
            {"tracks[].opacity", "number", "Track opacity."},
            {"tracks[].gain", "number", "Audio track gain, dB."},
            {"tracks[].provenance", "enum(local|inherited|overridden|dangling)", "Where it came from in this version."},
            {"clips", "array", "The resolved current timeline's clips."},
            {"clips[].id", "string", "Clip id."},
            {"clips[].name", "string", "Clip name — an address prefix."},
            {"clips[].track", "string", "Track id."},
            {"clips[].src", "string", "#rackobj id (video) or media path (audio)."},
            {"clips[].srcName", "string", "The rack node's bind name."},
            {"clips[].at", "number", "Timeline position, seconds."},
            {"clips[].in", "number", "Source in-point, seconds."},
            {"clips[].out", "number", "Source out-point, seconds."},
            {"clips[].speed", "number", "Playback speed."},
            {"clips[].duration", "number", "Timeline duration, (out-in)/speed."},
            {"clips[].opacity", "number", "Clip opacity."},
            {"clips[].gain", "number", "Audio clip gain, dB."},
            {"clips[].audio", "bool", "An audio clip (#aclip)."},
            {"clips[].offline", "bool", "Its media is missing."},
            {"clips[].provenance", "enum(local|inherited|overridden|dangling)", "Where it came from in this version."},
            {"transitions", "array", "Transitions in the resolved current timeline."},
            {"transitions[].id", "string", "Transition id."},
            {"transitions[].clipA", "string", "Outgoing clip — HELD through the transition (R-TL-4)."},
            {"transitions[].clipB", "string", "Incoming clip."},
            {"transitions[].kind", "string", "dissolve | dip."},
            {"transitions[].dur", "number", "Seconds."},
            {"markers", "array", "Markers in the current timeline."},
            {"markers[].id", "string", "Marker id."},
            {"markers[].name", "string", "Marker name."},
            {"markers[].at", "number", "Seconds."},
            {"markers[].note", "string", "Free text."},
            {"selectedClip", "string", "Selected clip id; empty = none."},
            {"duration", "number", "Current timeline length, seconds."},
            {"playhead", "number", "Seconds, on a frame boundary."},
            {"playheadFrame", "integer", "Playhead as a frame index."},
            {"playing", "bool", "Playback running."},
            {"frameSeq", "integer", "Rises when a new monitor frame is ready.", true},
            {"frameWidth", "integer", "Monitor frame width.", true},
            {"frameHeight", "integer", "Monitor frame height.", true},
            {"renders", "array", "The render queue."},
            {"renders[].id", "string", "Job id."},
            {"renders[].timeline", "string", "The timeline rendered — always named (R-RENDER-1)."},
            {"renders[].timelineName", "string", "Its name."},
            {"renders[].outPath", "string", "Output path (file name only when stable)."},
            {"renders[].format", "string", "h264 | h265 | prores | dnxhr | png-seq."},
            {"renders[].done", "integer", "Frames written."},
            {"renders[].total", "integer", "Frames in the range."},
            {"renders[].state", "string", "queued | running | done | failed | cancelled."},
            {"renders[].error", "string", "Why it failed."},
            {"renders[].width", "integer", "Output frame width (R-RENDER-6)."},
            {"renders[].height", "integer", "Output frame height."},
            {"renders[].fps", "number", "Output rate the timeline is sampled at."},
            {"renders[].spec", "string", "The whole output spec in words — codec, profile/bit depth, quality and speed, size, rate."},
            {"canUndo", "bool", "`undo` has something to undo (R-EDIT-1)."},
            {"canRedo", "bool", "`redo` has something to redo."},
            {"undoLabel", "string", "What `undo` would undo, e.g. `set a.basic.exposure`."},
            {"redoLabel", "string", "What `redo` would redo."},
            {"hasGradeClipboard", "bool", "`grade copy` has filled the clipboard (R-EDIT-2)."},
            {"gradeClipboardFrom", "string", "The bind name the clipboard grade came from."},
            {"hasClipClipboard", "bool", "`clip copy` has filled the clip clipboard (R-TL-6)."},
            {"clipClipboardFrom", "string", "The name of the clip it was copied from."},
            {"settings", "group", "Engine settings (R-SET)."},
            {"settings.cpuPercent", "integer", "Share of the machine's cores the app may schedule — the rack's decode and the frame path alike."},
            {"settings.threads", "integer", "Engine worker threads; 0 = auto (from cpuPercent)."},
            {"settings.previewEdge", "integer", "Cap on the monitor's render long edge, px; 0 = full. Renders are unaffected."},
            {"settings.useGpu", "bool", "GPU opt-in for the grade step (only where a backend exists)."},
            {"settings.uiScale", "integer", "Percent of the design size the window draws at."},
            {"settings.gpuAvailable", "bool", "A GPU backend exists on this machine.", true},
            {"settings.cores", "integer", "Cores on this machine.", true},
            {"settings.engineThreads", "integer", "Engine threads the budget resolves to now.", true},
            {"settings.decodeWorkers", "integer", "Decode workers the budget resolves to now.", true},
            {"presets", "array", "The preset library (Cosmo `.apf`), depth first (R-EDIT-3)."},
            {"presets[].name", "string", "What `preset apply` spells, folders joined with `/`."},
            {"presets[].folder", "string", "Its folder, empty at the top."},
            {"lastError", "string", "The last refusal or failure, for a front end that was not listening."},
        };
        return f;
    }
}
}
