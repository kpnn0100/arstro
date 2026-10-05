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
                          .set("sharesMedia", r.sharesMedia)
                          .set("mediaBitDepth", r.mediaBitDepth)
                          .set("input", r.input)
                          .set("lut", r.lut)
                          .set("timecode", r.timecode)
                          .set("reel", r.reel));
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
                         .set("overrides", t.overrides)
                         .set("hasSound", t.hasSound));
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
                           .set("audio", c.audio).set("media", c.media)
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
        j.set("playbackEdge", m.playbackEdge);
        j.set("playbackRate", m.playbackRate);
        {
            Json anims = Json::array();
            for (const auto &a : m.anims)
            {
                Json ks = Json::array();
                for (const auto &k : a.keys)
                    ks.push(Json::object().set("t", k.t).set("v", k.v).set("in", k.in).set("out", k.out)
                                .set("speedIn", k.speedIn).set("inflIn", k.inflIn).set("speedOut", k.speedOut).set("inflOut", k.inflOut).set("shape", k.shape));
                anims.push(Json::object().set("id", a.id).set("node", a.node).set("nodeBind", a.nodeBind).set("owner", a.owner)
                               .set("key", a.key).set("address", a.address).set("clock", a.clock).set("now", a.now).set("value", a.value)
                               .set("min", a.min).set("max", a.max).set("shape", a.shape).set("shapeNow", a.shapeNow).set("keys", ks));
            }
            j.set("anims", anims);
            j.set("keyClipboardCount", m.keyClipboardCount);
            j.set("keyClipboardCurves", m.keyClipboardCurves);
        }
        if (!st)
        {
            j.set("playbackFromCache", m.playbackFromCache);
            j.set("previewCacheFrames", m.previewCacheFrames);
            j.set("previewCacheTotal", m.previewCacheTotal);
            j.set("previewCacheBuilding", m.previewCacheBuilding);
            j.set("previewCacheSegmentSeconds", m.previewCacheSegmentSeconds);
            Json segs = Json::array();
            for (int s : m.previewCacheSegments) segs.push(Json::integer(s));
            j.set("previewCacheSegments", segs);
        }
        {
            Json fx = Json::array();
            for (const auto &e : m.effects)
            {
                Json ps = Json::array();
                for (const auto &p : e.params)
                    ps.push(Json::object().set("key", p.key).set("label", p.label).set("unit", p.unit).set("value", p.value)
                                .set("def", p.def).set("min", p.min).set("max", p.max));
                fx.push(Json::object().set("id", e.id).set("node", e.node).set("nodeBind", e.nodeBind).set("type", e.type)
                            .set("label", e.label).set("family", e.family).set("order", e.order).set("enabled", e.enabled)
                            .set("mix", e.mix).set("params", ps).set("file", e.file).set("fileKey", e.fileKey));
            }
            j.set("effects", fx);
            Json types = Json::array();
            for (const auto &t : m.effectTypes) types.push(Json::object().set("type", t.type).set("label", t.label).set("family", t.family));
            j.set("effectTypes", types);
            Json ins = Json::array(), outs = Json::array();
            for (const auto &c : m.colourInputs) ins.push(Json::object().set("id", c.id).set("label", c.label));
            for (const auto &c : m.colourOutputs) outs.push(Json::object().set("id", c.id).set("label", c.label));
            j.set("colourInputs", ins);
            j.set("colourOutputs", outs);
        }
        j.set("workingSpace", m.workingSpace);
        j.set("shuttle", m.shuttle);
        j.set("markIn", m.markIn);
        j.set("markOut", m.markOut);
        j.set("sourceView", m.sourceView);
        j.set("sourceIn", m.sourceIn);
        j.set("sourceOut", m.sourceOut);
        j.set("sourcePlayhead", m.sourcePlayhead);
        j.set("sourceDuration", m.sourceDuration);
        j.set("targetTrack", m.targetTrack);
        j.set("soundPlaying", m.soundPlaying);
        j.set("meterPeakL", m.meterPeakL);
        j.set("meterPeakR", m.meterPeakR);
        j.set("meterRmsL", m.meterRmsL);
        j.set("meterRmsR", m.meterRmsR);
        j.set("meterClip", m.meterClip);
        j.set("peaksEpoch", (long long)m.peaksEpoch);
        j.set("hasClipClipboard", m.hasClipClipboard);
        j.set("clipClipboardFrom", m.clipClipboardFrom);
        Json settings = Json::object();
        settings.set("cpuPercent", m.settings.cpuPercent);
        settings.set("threads", m.settings.threads);
        settings.set("previewEdge", m.settings.previewEdge);
        settings.set("useGpu", m.settings.useGpu);
        settings.set("hardwareVideo", m.settings.hardwareVideo);
        settings.set("previewCache", m.settings.previewCache);
        settings.set("keyLaneHeight", m.settings.keyLaneHeight);
        settings.set("uiScale", m.settings.uiScale);
        if (!st)
        {
            settings.set("gpuAvailable", m.settings.gpuAvailable);
            settings.set("gpuInUse", m.settings.gpuInUse);
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
            {"rack[].timecode", "string", "The file's first frame as source timecode (\"10:00:00:00\"), once opened; empty = it carries none (R-XCH-5)."},
            {"rack[].reel", "string", "The reel/tape name the file carries, once opened; empty = none (R-XCH-5)."},
            {"rack[].lut", "string", "Its input LUT (.cube) as the project names it; empty = none (`set <bind>.lut=`, R-COLOR-5)."},
            {"rack[].input", "string", "What the source IS: its input colour transform into the working space (`set <bind>.input=`, R-COLOR-2)."},
            {"rack[].mediaBitDepth", "integer", "Bits per component the source carries once opened (8, 10, 12…); 0 = not yet opened. Frames reach the preview as 8-bit (R-UI-15)."},
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
            {"timelines[].hasSound", "boolean", "Something on it sounds (an unmuted #aclip that decodes): a video render carries the mix (R-AUD-9)."},
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
            {"clips[].media", "string", "Audio clips: the file, resolved — the key of its waveform envelope (R-AUD-7)."},
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
            {"playbackEdge", "integer", "The long edge playback grades at now — stepped down when the read-ahead falls behind, up with headroom; 0 = not playing (R-PLAY-2)."},
            {"playbackRate", "number", "Frames the read-ahead finished per second over the last half second; 0 = not playing."},
            {"anims", "array", "Every animated parameter — its curve (R-ANIM-1)."},
            {"keyClipboardCount", "integer", "Keyframes `key copy` holds; 0 = none (R-ANIM-7)."},
            {"keyClipboardCurves", "integer", "…from how many properties (`key paste --to` needs exactly one)."},
            {"anims[].id", "string", "The curve's id, `an_<n>`."},
            {"anims[].node", "string", "The #rackobj, #effect or #clip it animates."},
            {"anims[].nodeBind", "string", "That node's name (an effect: its id)."},
            {"anims[].owner", "string", "rack | effect | clip."},
            {"anims[].key", "string", "The parameter, as an address spells it after the node."},
            {"anims[].address", "string", "The whole address: what `key add|remove|set|clear` and `set` take."},
            {"anims[].clock", "string", "source (the footage's own time — rack and effects) | clip (the clip's own footage time)."},
            {"anims[].now", "number", "The current time on that clock: the source's reference frame; the playhead inside the clip."},
            {"anims[].value", "number", "The curve's value now."},
            {"anims[].min", "number", "The parameter's lowest value, for the graph."},
            {"anims[].max", "number", "Its highest."},
            {"anims[].keys", "array", "Its keyframes, by time (R-ANIM-2)."},
            {"anims[].keys[].t", "number", "Time on the curve's clock, s."},
            {"anims[].keys[].v", "number", "Value, in the address's units."},
            {"anims[].keys[].in", "string", "Incoming side: linear | bezier | hold."},
            {"anims[].keys[].out", "string", "Outgoing side: linear | bezier | hold (hold keeps the value to the next key)."},
            {"anims[].keys[].speedIn", "number", "Incoming speed of a bezier side, units per second."},
            {"anims[].keys[].inflIn", "number", "Incoming influence, % of the segment."},
            {"anims[].keys[].speedOut", "number", "Outgoing speed, units per second."},
            {"anims[].keys[].inflOut", "number", "Outgoing influence, % of the segment."},
            {"anims[].keys[].shape", "string", "A shape key's value in the address's own syntax; empty for a number (R-ANIM-6)."},
            {"anims[].shape", "bool", "The curve animates a shape — a tone curve, a colour wheel, a crop (R-ANIM-6)."},
            {"anims[].shapeNow", "string", "A shape curve's value now."},
            {"playbackFromCache", "bool", "The frame on the monitor while playing was decoded from the preview cache (R-PLAY-1).", true},
            {"previewCacheFrames", "integer", "Frames of the current timeline in the preview cache AND current — an edit drops the ones it changed (R-PLAY-1).", true},
            {"previewCacheTotal", "integer", "Frames in the current timeline.", true},
            {"previewCacheBuilding", "bool", "A segment is being built now.", true},
            {"previewCacheSegmentSeconds", "number", "Seconds one previewCacheSegments entry covers.", true},
            {"previewCacheSegments", "array", "Per segment of the current timeline: 0 not cached · 1 cached and current · 2 stale · 3 building — the timeline's cache bar.", true},
            {"effects", "array", "Every plugin of every rack node's image-processing stack, by node then order (R-FX-5)."},
            {"effects[].id", "string", "The plugin's id, `ef_<n>` — stable for its life, the root of its addresses."},
            {"effects[].node", "string", "The #rackobj whose stack it is in."},
            {"effects[].nodeBind", "string", "That node's bind name."},
            {"effects[].type", "string", "The plugin type (`blur.gaussian` …)."},
            {"effects[].label", "string", "Its name for a person."},
            {"effects[].family", "string", "Its menu family (Blur)."},
            {"effects[].order", "integer", "Position in the stack; 0 = first after Cosmo."},
            {"effects[].enabled", "bool", "On or off."},
            {"effects[].mix", "number", "Output over input, 0..1."},
            {"effects[].fileKey", "string", "The parameter that names its file — `path` for a LUT (`set ef_3.path=<file>`); empty = the plugin takes no file."},
            {"effects[].file", "string", "The file a file-taking plugin reads — a LUT's .cube (`set ef_3.path=`); empty = none chosen (R-COLOR-5)."},
            {"effects[].params", "array", "Its parameters, from the catalog, with the value now."},
            {"effects[].params[].key", "string", "The parameter's key — the address is `<effect id>.<key>`."},
            {"effects[].params[].label", "string", "Its name for a person."},
            {"effects[].params[].unit", "string", "px (source pixels — scales with the proxy), deg, or empty for 0..1."},
            {"effects[].params[].value", "number", "The value now."},
            {"effects[].params[].def", "number", "The default a new effect starts with."},
            {"effects[].params[].min", "number", "The lowest value `set` accepts."},
            {"effects[].params[].max", "number", "The highest value `set` accepts."},
            {"effectTypes", "array", "The plugin catalog — what `effect add --type` accepts."},
            {"effectTypes[].type", "string", "The type to name in `effect add --type`."},
            {"effectTypes[].label", "string", "Its name for a person."},
            {"effectTypes[].family", "string", "Its menu family."},
            {"shuttle", "number", "Playback rate: 0 paused, ±1, ±2, ±4 — `shuttle forward|back|stop` (J/K/L, R-EDT-2)."},
            {"markIn", "number", "The timeline's In, seconds; -1 = unset (`mark in`, R-EDT-1)."},
            {"markOut", "number", "The timeline's Out, seconds; -1 = unset."},
            {"sourceView", "string", "The rack source the viewer shows (`source view <node>`); empty = the timeline (R-EDT-1)."},
            {"sourceIn", "number", "The source viewer's In, source seconds; -1 = unset (`mark in --source`)."},
            {"sourceOut", "number", "The source viewer's Out, source seconds; -1 = unset."},
            {"sourcePlayhead", "number", "The source viewer's playhead, source seconds."},
            {"sourceDuration", "number", "The viewed source's length, seconds; 0 = a still or not yet opened."},
            {"targetTrack", "string", "The video track Insert and Overwrite place on (`edit target`)."},
            {"soundPlaying", "boolean", "Playback is heard and the audio clock drives the playhead (R-AUD-6) — false without a sound output or with nothing to hear."},
            {"meterPeakL", "number", "The master's peak the listener hears now, left, linear (1 = full scale; R-AUD-8)."},
            {"meterPeakR", "number", "…right."},
            {"meterRmsL", "number", "The master's RMS now, left, linear."},
            {"meterRmsR", "number", "…right."},
            {"meterClip", "boolean", "The sum passed full scale in the last 3 s of playback."},
            {"peaksEpoch", "integer", "Rises when a waveform envelope lands; `AppHooks::audioPeaks(media)` has it (R-AUD-7)."},
            {"workingSpace", "string", "The project's working space: rec709 (Cosmo's own, display-referred) or acescct (`colour working`, R-COLOR-3)."},
            {"colourInputs", "array", "The source spaces `set <bind>.input=` accepts, in menu order (R-COLOR-2)."},
            {"colourInputs[].id", "string", "What to dispatch."},
            {"colourInputs[].label", "string", "Its name for a person."},
            {"colourOutputs", "array", "The output transforms `render --output` accepts (R-COLOR-4)."},
            {"colourOutputs[].id", "string", "What to dispatch."},
            {"colourOutputs[].label", "string", "Its name for a person."},
            {"hasClipClipboard", "bool", "`clip copy` has filled the clip clipboard (R-TL-6)."},
            {"clipClipboardFrom", "string", "The name of the clip it was copied from."},
            {"settings", "group", "Engine settings (R-SET)."},
            {"settings.cpuPercent", "integer", "Share of the machine's cores the app may schedule — the rack's decode and the frame path alike."},
            {"settings.threads", "integer", "Engine worker threads; 0 = auto (from cpuPercent)."},
            {"settings.previewEdge", "integer", "Cap on the monitor's render long edge, px; 0 = full. Renders are unaffected."},
            {"settings.keyLaneHeight", "integer", "The Cut key lane's height, px, 80..600 — dragged at its top edge (R-ANIM-8)."},
            {"settings.previewCache", "bool", "A window builds the graded preview cache of the current timeline when idle (R-PLAY-1)."},
            {"settings.useGpu", "bool", "GPU opt-in for the grade step (only where a backend exists)."},
            {"settings.hardwareVideo", "bool", "H.264/H.265 encode on the GPU's video unit (VA-API) for renders and the preview cache; falls back to software, said (R-PLAY-3)."},
            {"settings.uiScale", "integer", "Percent of the design size the window draws at."},
            {"settings.gpuAvailable", "bool", "A GPU backend exists on this machine.", true},
            {"settings.gpuInUse", "bool", "The last frame graded for a render, a still or the synchronous monitor ran on the GPU (R-GPU-1); false when the backend declined (a stage it has not ported).", true},
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
