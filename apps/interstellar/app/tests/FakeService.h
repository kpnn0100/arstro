/*
 *  interstellar_v1 tests — FakeService: a realistic AppModel behind a fake AppHooks.
 *
 *  The front end is built without the service (the point of the hooks), so its harnesses need a
 *  stand-in that looks like a real production: a rack of 8 nodes in 2 groups (a video source, a
 *  reference still, an OFFLINE source, one still decoding), three versions main → social30 →
 *  delivery (pinned + frozen, with dangling deltas), tracks and clips covering every Provenance, a
 *  transition or two, markers, a render queue in every state, and recents for Home.
 *
 *  `dispatch` RECORDS every line (tests assert on the exact text) and applies the handful a test
 *  needs to see the model move — selection, playhead, play/pause, version open, clip move/select/
 *  delete, weight, bypass, project close/open, render — bumping `revision` as a real service would.
 *  `refuseNext` makes the next line fail with a message (the refused state). `renderFrame` paints a
 *  gradient frame whose light follows the selected node's exposure, so a grade edit visibly lands;
 *  `thumbnail` paints a gradient keyed by the path.
 */
#pragma once
#include "AppHooks.h"
#include <functional>
#include <map>
#include <set>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace istest
{
    using namespace arstro::interstellar;

    inline std::vector<std::string> splitArgs(const std::string &line)
    {
        std::vector<std::string> out;
        std::string cur;
        bool q = false, have = false;
        for (size_t i = 0; i < line.size(); ++i)
        {
            const char c = line[i];
            if (q)
            {
                if (c == '\\' && i + 1 < line.size()) { cur += line[++i]; continue; }
                if (c == '"') { q = false; continue; }
                cur += c;
            }
            else if (c == '"') { q = true; have = true; }
            else if (c == ' ') { if (have || !cur.empty()) out.push_back(cur); cur.clear(); have = false; }
            else cur += c;
        }
        if (have || !cur.empty()) out.push_back(cur);
        return out;
    }

    class FakeService
    {
    public:
        AppModel m;
        std::vector<std::string> lines;
        bool refuseNext = false;
        std::string refuseMessage = "Refused: the version is pinned \xE2\x80\x94 colour is read-only at @a41c9e2";
        bool frameFails = false;
        int frames = 0;
        // the Grade monitor's source path and Copy Frame (R-UI-3, R-RACK-3, R-UI-11)
        std::vector<std::pair<std::string, double>> sourceCalls;
        std::vector<std::string> copies;
        bool copyFails = false;

        arstro::interstellar_v1::AppHooks hooks()
        {
            arstro::interstellar_v1::AppHooks h;
            h.model = [this]() -> const AppModel & { return m; };
            h.dispatch = [this](const std::string &l, std::string &err) { return dispatch(l, err); };
            h.renderFrame = [this](double t, int edge, Raster &out) { return renderFrame(t, edge, out); };
            h.thumbnail = [this](const std::string &p, double t, int edge, Raster &out) { return thumbnail(p, t, edge, out); };
            h.renderSource = [this](const std::string &b, double t, int edge, Raster &out) { return renderSource(b, t, edge, out); };
            h.copyFrame = [this](const std::string &b, std::string &err) {
                if (copyFails) { err = "Nothing to copy"; return false; }
                copies.push_back(b);
                return true;
            };
            return h;
        }

        // ── models ──────────────────────────────────────────────────────────────────────

        static long long kNow() { return 1790000000LL; }   // a fixed "now" for relative dates

        void home()
        {
            m = AppModel{};
            m.revision = 1;
            m.screen = Screen::Home;
            auto rec = [](const char *name, const char *path, int n, long long bytes, long long ago) {
                RecentModel r;
                r.name = name; r.path = path; r.coverPath = std::string(path) + ".cover";
                r.sourceCount = n; r.sizeBytes = bytes; r.lastOpened = kNow() - ago;
                return r;
            };
            m.recents = {
                rec("Night Ferry \xE2\x80\x94 Day 3", "/home/editor/Projects/night-ferry/night-ferry-day3.isp", 14, 48318382080LL, 2 * 3600),
                rec("Kodak 250D test reel", "/home/editor/Projects/kodak-250d/test-reel.isp", 6, 8589934592LL, 26 * 3600),
                rec("Lisbon interviews (selects, colour pass two, director's notes applied)", "/home/editor/Projects/lisbon/interviews.isp", 22, 120259084288LL, 3 * 86400),
                rec("Harbour drone", "/home/editor/Projects/harbour/drone.isp", 3, 2147483648LL, 40 * 60),
                rec("Studio B pickups", "/home/editor/Projects/studio-b/pickups.isp", 9, 16106127360LL, 12 * 86400),
                rec("Desert run proxy", "/home/editor/Projects/desert/run.isp", 31, 734003200LL, 40 * 86400),
            };
        }

        void homeEmpty() { home(); m.recents.clear(); }
        void homeLoading() { home(); m.revision = 0; m.recents.clear(); }

        void loading()
        {
            home();
            m.revision = 2;
            m.screen = Screen::Loading;
            m.projectName = "Night Ferry \xE2\x80\x94 Day 3";
        }

        static EffectModel effect(const char *id, const char *node, const char *bind, const std::string &type, int order, bool enabled, double mix)
        {
            EffectModel e;
            e.id = id; e.node = node; e.nodeBind = bind; e.type = type; e.order = order; e.enabled = enabled; e.mix = mix;
            e.family = "Blur";
            if (type == "blur.gaussian") { e.label = "Gaussian Blur"; e.params = {{"radius", "Radius", "px", 8.0, 8.0, 0.0, 200.0}}; }
            else if (type == "blur.box") { e.label = "Box Blur"; e.params = {{"radius", "Radius", "px", 8.0, 8.0, 0.0, 200.0}}; }
            else if (type == "blur.directional")
            {
                e.label = "Directional Blur";
                e.params = {{"length", "Length", "px", 30.0, 30.0, 0.0, 400.0}, {"angle", "Angle", "deg", 0.0, 0.0, -180.0, 180.0}};
            }
            else if (type == "lut.cube") { e.label = "LUT"; e.family = "Colour"; e.fileKey = "path"; }
            else
            {
                e.label = type == "blur.zoom" ? "Zoom Blur" : "Spin Blur";
                e.params = {{type == "blur.zoom" ? "amount" : "angle", type == "blur.zoom" ? "Amount" : "Angle", type == "blur.zoom" ? "" : "deg", 0.2, 0.2, 0.0, 1.0},
                            {"centerX", "Centre X", "", 0.5, 0.5, 0.0, 1.0}, {"centerY", "Centre Y", "", 0.5, 0.5, 0.0, 1.0}};
            }
            return e;
        }

        static RackNodeModel node(int id, const char *ro, const char *bind, const char *name, int parent, int depth, bool group)
        {
            RackNodeModel n;
            n.node = id; n.rackObj = ro; n.bindName = bind; n.cosmoName = name; n.parent = parent; n.depth = depth; n.group = group;
            return n;
        }
        static ClipModel clip(const char *id, const char *track, const char *src, const char *srcName, double at, double in, double out,
                              Provenance p, bool audio = false)
        {
            ClipModel c;
            c.id = id; c.name = id; c.track = track; c.src = src; c.srcName = srcName;
            c.at = at; c.in = in; c.out = out; c.duration = out - in; c.provenance = p; c.audio = audio;
            return c;
        }

        void edit()
        {
            home();
            m.revision = 10;
            m.screen = Screen::Edit;
            m.projectPath = "/home/editor/Projects/night-ferry/night-ferry-day3.isp";
            m.projectName = "Night Ferry \xE2\x80\x94 Day 3";
            m.dirty = true;
            m.fps = 24.0;
            m.width = 3840; m.height = 2160;

            m.rack.clear();
            m.rack.push_back(node(1, "ro1", "gr1", "Day exteriors", -1, 0, true));
            auto n = node(2, "ro2", "s_day01", "A001_C003 harbour wide", 0, 1, false);
            n.video = true; n.media = "/footage/A001_C003.mov"; n.frame = 2.5; n.usedBy = 2; n.mediaDuration = 12.0; n.mediaFps = 24.0; m.rack.push_back(n);
            n = node(3, "ro3", "s_day02", "A001_C007 deck mid", 0, 1, false);
            n.video = true; n.media = "/footage/A001_C007.mov"; n.frame = 1.0; n.usedBy = 1; n.overridden = true; n.weight = 0.8; n.mediaDuration = 8.0; n.mediaFps = 50.0; m.rack.push_back(n);
            m.rack.push_back(node(4, "ro4", "gr2", "Night interiors", -1, 0, true));
            n = node(5, "ro5", "s_nite01", "B002_C011 cabin close", 3, 1, false);
            n.video = true; n.media = "/footage/B002_C011.mov"; n.usedBy = 1; n.bypass = true; m.rack.push_back(n);
            n = node(6, "ro6", "s_still01", "ref_grey_card.tif", 3, 1, false);
            n.media = "/footage/ref_grey_card.tif"; n.usedBy = 0; m.rack.push_back(n);
            n = node(7, "ro7", "s_off01", "B002_C014 corridor", 3, 1, false);
            n.video = true; n.media = "/footage/B002_C014.mov"; n.failed = true; n.usedBy = 1; m.rack.push_back(n);
            n = node(8, "ro8", "s_drone01", "DJI_0042 approach", -1, 0, false);
            n.video = true; n.media = "/footage/DJI_0042.mp4"; n.pending = true; n.usedBy = 1; m.rack.push_back(n);
            selectRack(1);

            // R-FX-5: s_day01 carries a plugin stack; the catalog is the service's
            m.effectTypes = {{"blur.gaussian", "Gaussian Blur", "Blur"}, {"blur.box", "Box Blur", "Blur"},
                             {"blur.directional", "Directional Blur", "Blur"}, {"blur.zoom", "Zoom Blur", "Blur"},
                             {"blur.spin", "Spin Blur", "Blur"}};
            // R-COLOR: the service's lists (abridged), Rec.709 everywhere until a test changes it
            m.colourInputs = {{"rec709", "Rec.709"}, {"srgb", "sRGB"}, {"linear", "Linear (Rec.709)"}, {"logc3", "ARRI LogC3"},
                              {"slog3", "Sony S-Log3"}, {"vlog", "Panasonic V-Log"}};
            m.colourOutputs = {{"rec709", "Rec.709"}, {"pq", "HDR PQ"}, {"hlg", "HDR HLG"}};
            m.workingSpace = "rec709";
            m.effects.clear();
            m.effects.push_back(effect("ef_1", "ro2", "s_day01", "blur.gaussian", 0, true, 0.8));
            m.effects.push_back(effect("ef_2", "ro2", "s_day01", "blur.directional", 1, false, 1.0));

            TimelineModel tl;
            m.timelines.clear();
            tl.id = "main"; tl.name = "main"; tl.depth = 0; tl.overrides = 0;
            m.timelines.push_back(tl);
            tl = TimelineModel{}; tl.id = "social30"; tl.name = "Social 30s"; tl.base = "main"; tl.depth = 1; tl.overrides = 6; tl.danglingDeltas = 1;
            tl.hasSound = true;   // its audio lanes sound (R-AUD-9)
            m.timelines.push_back(tl);
            tl = TimelineModel{}; tl.id = "delivery"; tl.name = "Delivery 30s"; tl.base = "social30"; tl.depth = 2; tl.colourPinned = true;
            tl.pinCommit = "a41c9e2d71"; tl.cutFrozen = true; tl.overrides = 3; tl.danglingDeltas = 2;
            m.timelines.push_back(tl);
            m.currentTimeline = "social30";

            TrackModel tk;
            m.tracks.clear();
            tk = TrackModel{}; tk.id = "v2"; tk.name = "V2 titles"; tk.order = 2; tk.provenance = Provenance::Local; m.tracks.push_back(tk);
            tk = TrackModel{}; tk.id = "v1"; tk.name = "V1 picture"; tk.order = 1; tk.provenance = Provenance::Inherited; m.tracks.push_back(tk);
            tk = TrackModel{}; tk.id = "a1"; tk.name = "A1 dialogue"; tk.audio = true; tk.order = 1; tk.gain = -3; tk.provenance = Provenance::Inherited; m.tracks.push_back(tk);
            tk = TrackModel{}; tk.id = "a2"; tk.name = "A2 music bed"; tk.audio = true; tk.order = 2; tk.gain = -12; tk.mute = true; tk.provenance = Provenance::Overridden; m.tracks.push_back(tk);

            m.clips = {
                clip("c1", "v1", "ro2", "s_day01", 0.0, 2.0, 6.5, Provenance::Inherited),
                clip("c2", "v1", "ro3", "s_day02", 4.5, 1.0, 4.0, Provenance::Overridden),
                clip("c3", "v1", "ro5", "s_nite01", 7.5, 0.0, 2.5, Provenance::Inherited),
                clip("c4", "v1", "ro7", "s_off01", 10.0, 0.5, 2.5, Provenance::Inherited),
                clip("c5", "v1", "ro8", "s_drone01", 12.0, 3.0, 6.5, Provenance::Local),
                clip("c6", "v2", "ro6", "s_still01", 2.5, 0.0, 1.5, Provenance::Dangling),
                clip("c7", "v2", "ro2", "s_day01", 9.0, 7.0, 9.0, Provenance::Local),
                clip("a1", "a1", "/audio/dialogue_day3.wav", "dialogue_day3", 0.0, 0.0, 9.0, Provenance::Inherited, true),
                clip("a2", "a1", "/audio/dialogue_day3b.wav", "dialogue_day3b", 9.5, 0.0, 6.0, Provenance::Inherited, true),
                clip("a3", "a2", "/audio/bed_v4.wav", "bed_v4", 0.0, 0.0, 15.5, Provenance::Overridden, true),
            };
            m.clips[3].offline = true;
            m.clips[7].gain = -3; m.clips[8].gain = -3; m.clips[9].gain = -12;
            m.clips[0].duration = 4.5;
            m.transitions = {TransitionModel{"t1", "c2", "c3", "dissolve", 0.5}, TransitionModel{"t2", "c4", "c5", "dip", 0.5}};
            m.markers = {MarkerModel{"m1", "pickup", 6.0, ""}, MarkerModel{"m2", "music hit", 12.0, ""}};
            m.selectedClip = "c2";
            m.duration = 15.5;
            m.playhead = 5.25;
            m.playheadFrame = 126;
            m.frameSeq = 1;
            m.frameWidth = 3840; m.frameHeight = 2160;

            RenderJobModel j;
            m.renders.clear();
            j = RenderJobModel{}; j.id = "r1"; j.timeline = "delivery"; j.timelineName = "Delivery 30s"; j.outPath = "/home/editor/Projects/night-ferry/renders/delivery.mov";
            j.format = "prores"; j.spec = "ProRes HQ 10-bit \xC2\xB7 3840\xC3\x97" "2160 \xC2\xB7 24 fps"; j.done = 372; j.total = 372; j.state = "done"; m.renders.push_back(j);
            j = RenderJobModel{}; j.id = "r2"; j.timeline = "social30"; j.timelineName = "Social 30s"; j.outPath = "/home/editor/Projects/night-ferry/renders/social30.mp4";
            j.format = "h264"; j.spec = "H.264 \xC2\xB7 q18 \xC2\xB7 medium \xC2\xB7 1920\xC3\x97" "1080 \xC2\xB7 30 fps"; j.done = 148; j.total = 372; j.state = "running"; m.renders.push_back(j);
            j = RenderJobModel{}; j.id = "r3"; j.timeline = "main"; j.timelineName = "main"; j.outPath = "/home/editor/Projects/night-ferry/renders/main-png/";
            j.format = "png-seq"; j.spec = "PNG sequence \xC2\xB7 3840\xC3\x97" "2160 \xC2\xB7 24 fps"; j.done = 0; j.total = 372; j.state = "queued"; m.renders.push_back(j);
            j = RenderJobModel{}; j.id = "r4"; j.timeline = "main"; j.timelineName = "main"; j.outPath = "/media/usb-stick/night-ferry/main.mov";
            j.format = "prores"; j.done = 61; j.total = 372; j.state = "failed"; j.error = "encoder: no space left on device"; m.renders.push_back(j);
        }

        void editNewProject()
        {
            edit();
            m.projectName = "Untitled interstellar";
            m.dirty = false;
            m.rack.clear();
            m.selectedRack = -1;
            m.hasGradeTarget = false;
            m.clips.clear();
            m.transitions.clear();
            m.markers.clear();
            m.renders.clear();
            m.timelines.resize(1);
            m.timelines[0].danglingDeltas = 0;
            m.currentTimeline = "main";
            m.duration = 0.0;
            m.playhead = 0.0;
            m.selectedClip.clear();
        }

        void selectRack(int i)
        {
            m.selectedRack = i;
            m.hasGradeTarget = i >= 0 && i < (int)m.rack.size();
            arstro::EditParams own;
            if (i == 1)
            {
                own.exposure = 0.35f; own.contrast = 12; own.highlights = -24; own.shadows = 18; own.whites = 6; own.blacks = -8;
                own.temp = 5900; own.tint = 4.5f; own.vibrance = 10; own.saturation = -6; own.clarity = 8; own.sharpenAmount = 40;
                own.curve = {arstro::CurvePoint{0, 0}, arstro::CurvePoint{0.25f, 0.21f}, arstro::CurvePoint{0.5f, 0.56f}, arstro::CurvePoint{1, 1}};
                own.grade[0] = arstro::GradeWheel{210, 18, -4};
                own.grade[2] = arstro::GradeWheel{38, 10, 3};
            }
            else if (i == 2) { own.exposure = -0.2f; own.contrast = 6; own.temp = 6200; }
            m.gradeOwnParams = own;
            arstro::EditParams eff = own;   // the group adds a little on top: the green stacked reach
            if (i == 1 || i == 2) { eff.exposure += 0.15f; eff.contrast += 8; eff.saturation += 6; }
            m.gradeParams = eff;
            m.sourceWidth = 3840; m.sourceHeight = 2160;
        }

        /** R-ANIM: a curve as the service would publish it — owner and clock from the node, keys sorted. */
        AnimModel *animFor(const std::string &address, bool create)
        {
            const auto dot = address.find('.');
            if (dot == std::string::npos) return nullptr;
            const std::string name = address.substr(0, dot), key = address.substr(dot + 1);
            std::string node, owner, bind = name;
            double now = 0, lo = -1000, hi = 1000;
            for (const auto &r : m.rack) if (r.bindName == name) { node = r.rackObj; owner = "rack"; now = r.frame; lo = -100; hi = 100; }
            if (owner == "rack" && key == "basic.exposure") { lo = -5; hi = 5; }   // the service's own range, EV
            for (const auto &e : m.effects) if (e.id == name) { node = e.id; owner = "effect"; lo = 0; hi = 100; }
            for (const auto &c : m.clips)
                if (c.name == name || c.id == name)
                {
                    node = c.id; owner = "clip"; bind = c.name.empty() ? c.id : c.name;
                    now = std::clamp(c.in + (m.playhead - c.at) * c.speed, c.in, c.out);
                    if (key == "opacity") { lo = 0; hi = 1; }
                }
            if (node.empty()) return nullptr;
            for (auto &x : m.anims) if (x.node == node && x.key == key) { x.now = now; return &x; }
            if (!create) return nullptr;
            AnimModel x;
            x.id = "an_" + std::to_string(m.anims.size() + 1);
            x.node = node; x.nodeBind = bind; x.owner = owner; x.key = key; x.address = bind + "." + key;
            x.clock = owner == "clip" ? "clip" : "source";
            x.now = now; x.min = lo; x.max = hi;
            m.anims.push_back(x);
            return &m.anims.back();
        }
        void animateShape(const std::string &address, std::vector<std::pair<double, std::string>> keys)
        {
            AnimModel *a = animFor(address, true);
            a->shape = true;
            for (const auto &k : keys) { KeyframeModel km; km.t = k.first; km.shape = k.second; a->keys.push_back(km); }
            std::sort(a->keys.begin(), a->keys.end(), [](const KeyframeModel &x, const KeyframeModel &y) { return x.t < y.t; });
            a->shapeNow = a->keys.front().shape;
        }
        void animate(const std::string &address, std::vector<std::pair<double, double>> keys)
        {
            AnimModel *a = animFor(address, true);
            for (const auto &k : keys) { KeyframeModel km; km.t = k.first; km.v = k.second; a->keys.push_back(km); }
            std::sort(a->keys.begin(), a->keys.end(), [](const KeyframeModel &x, const KeyframeModel &y) { return x.t < y.t; });
        }

        // ── hooks ───────────────────────────────────────────────────────────────────────

        bool dispatch(const std::string &line, std::string &err)
        {
            lines.push_back(line);
            if (refuseNext) { refuseNext = false; err = refuseMessage; return false; }
            const auto a = splitArgs(line);
            if (a.empty()) return false;
            bool changed = true;
            if (a[0] == "rack" && a.size() >= 3 && a[1] == "select")
            {
                for (int i = 0; i < (int)m.rack.size(); ++i) if (m.rack[i].bindName == a[2]) selectRack(i);
                ++m.frameSeq;
            }
            else if (a[0] == "rack" && a.size() >= 3 && a[1] == "group" && a[2] == "new") groupSelection();
            else if (a[0] == "track" && a.size() >= 2 && a[1] == "add")
            {
                TrackModel tk;
                bool audio = false;
                for (size_t k = 2; k + 1 < a.size(); k += 2) if (a[k] == "--kind") audio = a[k + 1] == "audio";
                int n = 1;
                for (const auto &x : m.tracks) n += x.audio == audio;
                tk.id = std::string(audio ? "a" : "v") + std::to_string(n + 10);   // fresh, as the service's would be
                tk.name = std::string(audio ? "A" : "V") + std::to_string(n);
                tk.audio = audio;
                tk.order = n;
                m.tracks.push_back(tk);
            }
            else if (a[0] == "clip" && a.size() >= 3 && a[1] == "copy") { m.hasClipClipboard = true; m.clipClipboardFrom = a[2]; }
            else if (a[0] == "rack" && a.size() >= 5 && a[1] == "frame")
            {
                for (auto &n : m.rack) if (n.bindName == a[2]) n.frame = std::atof(a[4].c_str());
            }
            else if (a[0] == "key" && a.size() >= 4 && a[1] == "copy")
            {
                // "--keys a@t,b@t": count them, and the properties they came from
                const std::string &list = a[3];
                std::set<std::string> props;
                int n = 0;
                size_t from = 0;
                while (from < list.size())
                {
                    const size_t comma = std::min(list.find(',', from), list.size());
                    const std::string item = list.substr(from, comma - from);
                    props.insert(item.substr(0, item.rfind('@')));
                    ++n;
                    from = comma + 1;
                }
                m.keyClipboardCount = n;
                m.keyClipboardCurves = (int)props.size();
            }
            else if (a[0] == "key" && a.size() >= 3 && (a[1] == "add" || a[1] == "remove" || a[1] == "set"))
            {
                auto flag = [&](const std::string &f, double &v) {
                    for (size_t k = 3; k + 1 < a.size(); ++k) if (a[k] == "--" + f) { v = std::atof(a[k + 1].c_str()); return true; }
                    return false;
                };
                AnimModel *an = animFor(a[2], a[1] == "add");
                if (!an) { err = "key: no curve"; return false; }
                double at = an->now, v = 0.5;
                flag("at", at);
                auto it = std::find_if(an->keys.begin(), an->keys.end(), [&](const KeyframeModel &k) { return std::fabs(k.t - at) < 5e-4; });
                if (a[1] == "add")
                {
                    if (it == an->keys.end()) { KeyframeModel km; km.t = at; km.v = flag("value", v) ? v : 0.5; an->keys.push_back(km); }
                }
                else if (a[1] == "remove") { if (it != an->keys.end()) an->keys.erase(it); }
                else if (it != an->keys.end())
                {
                    double x = 0;
                    if (flag("to", x)) it->t = x;
                    if (flag("value", x)) it->v = x;
                    if (flag("speed-out", x)) { it->speedOut = x; it->out = "bezier"; }
                    if (flag("influence-out", x)) { it->inflOut = x; it->out = "bezier"; }
                    if (flag("speed-in", x)) { it->speedIn = x; it->in = "bezier"; }
                    if (flag("influence-in", x)) { it->inflIn = x; it->in = "bezier"; }
                }
                std::sort(an->keys.begin(), an->keys.end(), [](const KeyframeModel &p, const KeyframeModel &q) { return p.t < q.t; });
                if (an->keys.empty()) m.anims.erase(m.anims.begin() + (an - &m.anims[0]));
            }
            else if (a[0] == "rack" && a.size() >= 3 && a[1] == "duplicate")
            {
                // the service's behaviour: same file, own name, at the top of the rack, selected
                for (int i = 0; i < (int)m.rack.size(); ++i)
                    if (m.rack[(size_t)i].bindName == a[2] && !m.rack[(size_t)i].group)
                    {
                        RackNodeModel v = m.rack[(size_t)i];
                        v.bindName += "_v"; v.cosmoName += " (variant)"; v.rackObj += "_v"; v.node += 100;
                        v.parent = -1; v.depth = 0; v.usedBy = 0; v.overridden = false; v.selected = false;
                        m.rack[(size_t)i].sharesMedia = 1; v.sharesMedia = 1;
                        m.rack.push_back(v);
                        selectRack((int)m.rack.size() - 1);
                        break;
                    }
                ++m.frameSeq;
            }
            else if (a[0] == "rack" && a.size() >= 5 && a[1] == "frame" && a[3] == "--at")
            {
                for (auto &n : m.rack) if (n.bindName == a[2]) n.frame = std::stod(a[4]);
                ++m.frameSeq;
            }
            else if (a[0] == "playhead" && a.size() >= 2)
            {
                if (a[1] != "prev-cut" && a[1] != "next-cut") m.playhead = std::clamp(std::stod(a[1]), 0.0, m.duration);
                ++m.frameSeq;
            }
            else if (a[0] == "play") m.playing = true;
            else if (a[0] == "pause") m.playing = false;
            else if (a[0] == "timeline" && a.size() >= 3 && a[1] == "open") { m.currentTimeline = a[2]; ++m.frameSeq; }
            else if (a[0] == "clip" && a.size() >= 3 && a[1] == "select") m.selectedClip = a[2];
            else if (a[0] == "clip" && a.size() >= 5 && a[1] == "move")
            {
                for (auto &c : m.clips)
                    if (c.id == a[2])
                        for (size_t k = 3; k + 1 < a.size(); k += 2)
                        {
                            if (a[k] == "--at") c.at = std::stod(a[k + 1]);
                            if (a[k] == "--track") c.track = a[k + 1];
                        }
            }
            else if (a[0] == "clip" && a.size() >= 3 && a[1] == "delete")
                m.clips.erase(std::remove_if(m.clips.begin(), m.clips.end(), [&](const ClipModel &c) { return c.id == a[2]; }), m.clips.end());
            else if (a[0] == "effect" && a.size() >= 3 && a[1] == "add")
            {
                std::string type, node;
                for (size_t k = 3; k + 1 < a.size(); ++k) if (a[k] == "--type") type = a[k + 1];
                for (const auto &n : m.rack) if (n.bindName == a[2]) node = n.rackObj;
                int order = 0;
                for (const auto &e : m.effects) order += e.node == node;
                m.effects.push_back(effect(("ef_" + std::to_string(m.effects.size() + 10)).c_str(), node.c_str(), a[2].c_str(), type, order, true, 1.0));
            }
            else if (a[0] == "effect" && a.size() >= 3 && a[1] == "remove")
            {
                m.effects.erase(std::remove_if(m.effects.begin(), m.effects.end(), [&](const EffectModel &e) { return e.id == a[2]; }), m.effects.end());
                std::map<std::string, int> next;   // the service keeps each stack's order dense
                for (auto &e : m.effects) e.order = next[e.node]++;
            }
            else if (a[0] == "set" && a.size() >= 2)
            {
                for (size_t k = 1; k < a.size(); ++k)
                {
                    const auto eq = a[k].find('='), dot = a[k].find('.');
                    if (eq == std::string::npos || dot == std::string::npos) continue;
                    const std::string bind = a[k].substr(0, dot), key = a[k].substr(dot + 1, eq - dot - 1), val = a[k].substr(eq + 1);
                    for (auto &e : m.effects)
                        if (e.id == bind)
                        {
                            if (key == "enabled") e.enabled = val == "1";
                            else if (key == "mix") e.mix = std::stod(val);
                            else if (!e.fileKey.empty() && key == e.fileKey) e.file = val == "none" ? std::string() : val;
                            else for (auto &p : e.params) if (p.key == key) p.value = std::stod(val);
                        }
                    for (auto &n : m.rack)
                        if (n.bindName == bind)
                        {
                            if (key == "weight") n.weight = std::stod(val);
                            else if (key == "bypass") n.bypass = val == "1";
                            else if (key == "input") n.input = val;
                            else if (key == "lut") n.lut = val == "none" ? std::string() : val;
                            else if (key == "basic.exposure" && m.selectedRack >= 0 && m.rack[m.selectedRack].bindName == bind)
                            {
                                m.gradeOwnParams.exposure = (float)std::stod(val);
                                m.gradeParams.exposure = m.gradeOwnParams.exposure + 0.15f;
                                ++m.frameSeq;
                            }
                        }
                }
            }
            else if (a[0] == "colour" && a.size() >= 3 && a[1] == "working") m.workingSpace = a[2];
            else if (a[0] == "project" && a.size() >= 2 && a[1] == "close") { home(); }
            else if (a[0] == "project" && a.size() >= 2 && a[1] == "save") m.dirty = false;
            else if (a[0] == "render")
            {
                RenderJobModel j;
                j.id = "r" + std::to_string(m.renders.size() + 1);
                for (size_t k = 1; k + 1 < a.size(); k += 2)
                {
                    if (a[k] == "--timeline") j.timeline = a[k + 1];
                    if (a[k] == "--out") j.outPath = a[k + 1];
                    if (a[k] == "--format") j.format = a[k + 1];
                }
                for (const auto &tl : m.timelines) if (tl.id == j.timeline) j.timelineName = tl.name;
                j.total = 372; j.state = "queued";
                j.spec = j.format == "prores" ? "ProRes 422 10-bit \xC2\xB7 3840\xC3\x97" "2160 \xC2\xB7 24 fps"
                                              : "H.264 \xC2\xB7 q18 \xC2\xB7 medium \xC2\xB7 3840\xC3\x97" "2160 \xC2\xB7 24 fps";
                m.renders.push_back(j);
            }
            else
                changed = false;   // recorded, not modelled: a real service would act on it
            if (changed) ++m.revision;
            return true;
        }

        static void gradient(Raster &out, int w, int h, double phase, double light, double hueShift)
        {
            out.allocate(w, h, 255);
            const double sunX = 0.25 + 0.5 * std::fmod(phase, 1.0), sunY = 0.42;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const double u = (double)x / w, v = (double)y / h;
                    double r, g, b;
                    if (v < 0.58)
                    {   // sky: dusk violet to amber at the horizon
                        const double k = v / 0.58;
                        r = 0.18 + 0.75 * k; g = 0.16 + 0.42 * k; b = 0.42 - 0.12 * k;
                    }
                    else
                    {   // water: deep teal with a glint band
                        const double k = (v - 0.58) / 0.42;
                        r = 0.10 - 0.06 * k; g = 0.22 - 0.10 * k; b = 0.30 - 0.12 * k;
                        r += 0.25 * std::exp(-std::pow((u - sunX) * 9.0, 2.0)) * (1.0 - k);
                        g += 0.16 * std::exp(-std::pow((u - sunX) * 9.0, 2.0)) * (1.0 - k);
                    }
                    const double d = std::hypot((u - sunX) * 1.78, v - sunY);
                    const double sun = std::exp(-d * d * 260.0);
                    r += sun * 0.9; g += sun * 0.7; b += sun * 0.35;
                    // hue shift (thumbnails differ per source)
                    const double rr = r * (1 - hueShift) + b * hueShift, bb = b * (1 - hueShift) + r * hueShift;
                    uint8_t *p = &out.rgba[((size_t)y * w + x) * 4];
                    p[0] = (uint8_t)std::clamp(rr * light * 255.0, 0.0, 255.0);
                    p[1] = (uint8_t)std::clamp(g * light * 255.0, 0.0, 255.0);
                    p[2] = (uint8_t)std::clamp(bb * light * 255.0, 0.0, 255.0);
                    p[3] = 255;
                }
        }

        /** `rack group new`: the selection (else the Grade target) moves into a new group placed where
         *  its first member was, and the group becomes the selection — the service's behaviour. */
        void groupSelection()
        {
            const auto old = m.rack;
            std::vector<std::string> order;
            std::map<std::string, std::string> parentOf;
            std::set<std::string> members;
            for (int i = 0; i < (int)old.size(); ++i)
            {
                parentOf[old[i].rackObj] = old[i].parent >= 0 ? old[(size_t)old[i].parent].rackObj : std::string();
                if (old[i].selected || i == m.selectedRack) members.insert(old[i].rackObj);
            }
            if (members.empty()) return;
            RackNodeModel g;
            g.group = true;
            g.node = 900 + (int)old.size();
            g.rackObj = "ro_g" + std::to_string(g.node);
            g.bindName = "group" + std::to_string(g.node);
            g.cosmoName = "Group";
            bool placed = false;
            for (const auto &n : old)
            {
                if (!placed && members.count(n.rackObj)) { order.push_back(g.rackObj); parentOf[g.rackObj] = parentOf[n.rackObj]; placed = true; }
                order.push_back(n.rackObj);
            }
            for (const auto &k : members) parentOf[k] = g.rackObj;
            std::map<std::string, RackNodeModel> byKey;
            for (const auto &n : old) byKey[n.rackObj] = n;
            byKey[g.rackObj] = g;
            m.rack.clear();
            std::function<void(const std::string &, int, int)> flatten = [&](const std::string &parentKey, int parentIdx, int depth) {
                for (const auto &k : order)
                    if (parentOf[k] == parentKey)
                    {
                        RackNodeModel n = byKey[k];
                        n.parent = parentIdx; n.depth = depth; n.selected = false;
                        m.rack.push_back(n);
                        flatten(k, (int)m.rack.size() - 1, depth + 1);
                    }
            };
            flatten(std::string(), -1, 0);
            for (int i = 0; i < (int)m.rack.size(); ++i) if (m.rack[(size_t)i].rackObj == g.rackObj) selectRack(i);
            ++m.frameSeq;
        }

        bool renderFrame(double t, int edge, Raster &out)
        {
            ++frames;
            if (frameFails) return false;
            const int w = std::min(edge, 640), h = w * 9 / 16;
            const double light = std::pow(2.0, (double)m.gradeParams.exposure * 0.6);
            gradient(out, w, h, t / std::max(1.0, m.duration), light, m.currentTimeline == "main" ? 0.0 : 0.08);
            return true;
        }

        /** One source graded alone: the light follows the exposure, the sun follows t (t < 0 = the
         *  node's reference frame) — so a seek visibly moves the picture. */
        bool renderSource(const std::string &bind, double t, int edge, Raster &out)
        {
            sourceCalls.emplace_back(bind, t);
            if (frameFails) return false;
            double at = t;
            if (at < 0) for (const auto &n : m.rack) if (n.bindName == bind) at = n.frame;
            const int w = std::min(edge, 640), h = w * 9 / 16;
            const double light = std::pow(2.0, (double)m.gradeParams.exposure * 0.6);
            gradient(out, w, h, at / 12.0, light, 0.04);
            return true;
        }

        int thumbnails = 0;
        bool thumbnail(const std::string &path, double t, int, Raster &out)
        {
            ++thumbnails;
            unsigned hsh = 2166136261u;
            for (char c : path) hsh = (hsh ^ (unsigned char)c) * 16777619u;
            // the sun travels with t, so a strip of frames across a source reads as footage
            gradient(out, 160, 90, (hsh % 100) / 100.0 + t * 0.06, 0.85 + (hsh % 7) * 0.04, (hsh % 5) * 0.12);
            return true;
        }
    };
}
