/*
 *  interstellar_service_tests — L1: the tables the API document is generated from.
 *
 *  Grammar (parse, refuse with candidates, round-trip), the model codec (every key documented,
 *  every documented key written, stable mode drops what it must), and the address registry
 *  (every key Cosmo's codec writes is addressable). No service, no rack — these are the contracts
 *  the service and the API document stand on, so they fail first and loudest.
 */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "ApiDoc.h"
#include "AppModelCodec.h"
#include "Command.h"
#include "Event.h"
#include "ParamRegistry.h"
#include "engine/EditParamsIO.h"
#include <cstdio>
#include <functional>
#include <set>
#include <sstream>
#include <string>

using namespace arstro;
using namespace arstro::interstellar;

static int gPassed = 0;
static void test(const char *name, const std::function<void()> &fn)
{
    fn();
    ++gPassed;
    std::printf("  ok  %s\n", name);
}

static bool contains(const std::string &hay, const std::string &needle) { return hay.find(needle) != std::string::npos; }

static void collectPaths(const Json &j, const std::string &path, std::set<std::string> &out,
                         const std::set<std::string> &opaque)
{
    if (!path.empty() && path.back() != ']') out.insert(path);   // `clips[]` is the element, not a key
    if (opaque.count(path)) return;   // documented as a whole object (EditParams keys are addresses)
    if (j.type() == Json::Type::Object)
        for (const auto &kv : j.members()) collectPaths(kv.second, path.empty() ? kv.first : path + "." + kv.first, out, opaque);
    else if (j.type() == Json::Type::Array)
        for (const auto &it : j.items()) collectPaths(it, path + "[]", out, opaque);
}

static AppModel populatedModel()
{
    AppModel m;
    m.screen = Screen::Edit;
    m.recents.push_back({"Japan MV", "/home/u/japan.isp", "/home/u/a.mov", 6, 1234, 1700000000});
    m.projectPath = "/home/u/japan.isp";
    m.projectName = "Japan MV";
    RackNodeModel g; g.node = 1; g.rackObj = "ro_1"; g.bindName = "gr1"; g.cosmoName = "Day"; g.group = true;
    RackNodeModel s; s.node = 2; s.rackObj = "ro_2"; s.bindName = "s_day01"; s.parent = 0; s.depth = 1;
    s.media = "/home/u/footage/DSC01.MOV"; s.video = true; s.frame = 2.0;
    m.rack = {g, s};
    m.selectedRack = 1;
    m.hasGradeTarget = true;
    m.timelines.push_back({"tl_1", "main", "", 0, false, "", false, 0, 0});
    m.timelines.push_back({"tl_2", "social30", "tl_1", 1, true, "8f2c1ab", true, 1, 3});
    m.currentTimeline = "tl_2";
    TrackModel t; t.id = "trk_1"; t.name = "v0"; m.tracks.push_back(t);
    ClipModel c; c.id = "clp_1"; c.name = "shotA"; c.track = "trk_1"; c.src = "ro_2"; c.out = 4; c.duration = 4;
    m.clips.push_back(c);
    m.transitions.push_back({"tr_1", "clp_1", "clp_2", "dissolve", 0.5});
    m.markers.push_back({"mk_1", "chorus", 48, "chorus in"});
    RenderJobModel r; r.id = "r1"; r.timeline = "tl_2"; r.timelineName = "social30"; r.outPath = "/tmp/out/s.mp4";
    r.format = "h264"; r.state = "queued";
    m.renders.push_back(r);
    {
        EffectModel e;
        e.id = "ef_1"; e.node = "ro_1"; e.nodeBind = "a"; e.type = "blur.gaussian"; e.label = "Gaussian Blur"; e.family = "Blur";
        e.params.push_back(EffectParamModel{"radius", "Radius", "px", 8.0, 8.0, 0.0, 200.0});
        e.file = "looks/teal.cube";
        e.fileKey = "path";
        m.effects.push_back(e);
        m.effectTypes.push_back({"blur.gaussian", "Gaussian Blur", "Blur"});
        m.colourInputs.push_back({"slog3", "Sony S-Log3"});
        m.colourOutputs.push_back({"pq", "HDR PQ"});
        m.workingSpace = "acescct";
        AnimModel an;
        an.id = "an_1";
        an.node = "ro_1";
        an.nodeBind = "a";
        an.owner = "rack";
        an.key = "basic.exposure";
        an.address = "a.basic.exposure";
        an.clock = "source";
        an.keys.push_back(KeyframeModel{});
        m.anims.push_back(an);
    }
    m.presets.push_back({"Film/Warm fade", "Film"});
    return m;
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // a failed assert must not swallow the lines before it
    std::printf("interstellar_service_tests\n");

    test("every spec row parses from its own usage verb, and verbs are unique", [] {
        std::set<std::string> verbs;
        for (const auto &s : commandSpecs())
        {
            assert(verbs.insert(s.verb).second);
            std::string line = s.verb;
            for (int i = 0; i < s.minArgs; ++i) line += s.fieldArgs ? " a.b=1" : " x";
            std::string err;
            const Command c = parseCommand(line, err);
            if (!err.empty()) std::printf("    %s → %s\n", line.c_str(), err.c_str());
            assert(err.empty() && c.kind == s.kind);
        }
    });

    test("the longest verb wins and flags land by name", [] {
        std::string err;
        Command c = parseCommand("audio clip add --track a1 --src bed.wav --at 0 --gain=-3", err);
        assert(err.empty() && c.kind == Command::Kind::AudioClipAdd);
        assert(c.flag("track") == "a1" && c.flag("src") == "bed.wav" && c.flag("gain") == "-3");
        c = parseCommand("timeline rebase tl_2 --dry-run", err);
        assert(err.empty() && c.kind == Command::Kind::TimelineRebase && c.arg(0) == "tl_2" && c.has("dry-run"));
        c = parseCommand("rack group new \"Day interiors\" --nodes s1,s2", err);
        assert(err.empty() && c.kind == Command::Kind::RackGroupNew && c.arg(0) == "Day interiors");
    });

    test("set carries address=value pairs, curves and frame selectors intact", [] {
        std::string err;
        Command c = parseCommand("set s_day01.curve.curve=0,0;0.5,0.62;1,1 s_day01.basic.exposure=0.35", err);
        assert(err.empty() && c.kind == Command::Kind::Set && c.fields.size() == 2);
        assert(c.fields[0].first == "s_day01.curve.curve" && c.fields[0].second == "0,0;0.5,0.62;1,1");
        c = parseCommand("rack add a.png \"/media/my clip.mp4#t=2.0\"", err);
        assert(err.empty() && c.args.size() == 2 && c.args[1] == "/media/my clip.mp4#t=2.0");
        c = parseCommand("set exposure", err);
        assert(!err.empty() && contains(err, "<address>=<value>"));
    });

    test("comments are whole lines only", [] {
        std::string err;
        Command c = parseCommand("   # a comment", err);
        assert(!c.valid() && err.empty());
        c = parseCommand("; also a comment", err);
        assert(!c.valid() && err.empty());
        c = parseCommand("", err);
        assert(!c.valid() && err.empty());
    });

    test("an unknown verb is refused with the nearest candidates (R-SVC-3)", [] {
        std::string err;
        Command c = parseCommand("timline pin tl_2", err);
        assert(!c.valid() && contains(err, "unknown command") && contains(err, "timeline pin"));
        c = parseCommand("rendr --timeline main", err);
        assert(!c.valid() && contains(err, "render"));
    });

    test("an unknown flag is refused, naming it and the candidates", [] {
        std::string err;
        Command c = parseCommand("render --timline main --out x.mp4", err);
        assert(!c.valid() && contains(err, "--timline") && contains(err, "--timeline"));
        c = parseCommand("play --fast", err);
        assert(!c.valid() && contains(err, "takes no flags"));
        c = parseCommand("timeline pin", err);
        assert(!c.valid() && contains(err, "usage: timeline pin <tl>"));
        c = parseCommand("timeline rebase tl_2 --dry-run=1", err);
        assert(!c.valid() && contains(err, "switch"));
    });

    test("format → parse is a fixed point for every row", [] {
        for (const auto &s : commandSpecs())
        {
            Command c;
            c.kind = s.kind;
            for (int i = 0; i < s.minArgs; ++i)
            {
                if (s.fieldArgs) { c.fields.emplace_back("my clip.at", "1.5"); c.args.push_back("my clip.at=1.5"); }
                else c.args.push_back(i == 0 ? "with space" : "y");
            }
            for (const auto &f : s.flags)
            {
                const auto eq = f.find('=');
                c.flags.emplace_back(eq == std::string::npos ? f : f.substr(0, eq), eq == std::string::npos ? "1" : "v 1");
            }
            std::string err;
            const std::string line = formatCommand(c);
            const Command back = parseCommand(line, err);
            if (!err.empty()) std::printf("    %s → %s\n", line.c_str(), err.c_str());
            assert(err.empty());
            assert(formatCommand(back) == line);
        }
    });

    test("events format as one log line with declared fields", [] {
        Event e(Event::Kind::ParamsChanged);
        e.with("address", "s_day01.basic.exposure").with("value", 0.35).with("target", "rack");
        assert(formatEvent(e) == "[evt] params.changed address=s_day01.basic.exposure value=0.35 target=rack");
        Event q(Event::Kind::Error);
        q.with("why", "no such node");
        assert(formatEvent(q) == "[evt] error why=\"no such node\"");
        std::set<std::string> names;
        for (const auto &s : eventSpecs()) assert(names.insert(s.name).second);
        assert(canonicalNumber(0.1) == "0.1" && canonicalNumber(-0.0) == "0.0" && canonicalNumber(24) == "24.0" && canonicalNumber(1.0 / 3) == "0.3333333333333333");
    });

    test("every model key is documented and every documented key is written (R-API-1)", [] {
        const AppModel m = populatedModel();
        ModelDumpOptions o;
        o.json = true;
        std::set<std::string> opaque, documented, written;
        for (const auto &f : appModelFields())
        {
            documented.insert(f.path);
            if (f.type == "object") opaque.insert(f.path);
        }
        collectPaths(modelToJson(m, o), "", written, opaque);
        for (const auto &w : written)
            if (!documented.count(w)) { std::printf("    undocumented: %s\n", w.c_str()); assert(false); }
        for (const auto &d : documented)
            if (!written.count(d)) { std::printf("    never written: %s\n", d.c_str()); assert(false); }
    });

    test("--stable drops machine fields and reduces paths to names", [] {
        const AppModel m = populatedModel();
        ModelDumpOptions o;
        o.json = true;
        o.stable = true;
        const std::string s = formatModel(m, o);
        assert(!contains(s, "\"revision\"") && !contains(s, "\"recents\"") && !contains(s, "\"frameSeq\""));
        assert(contains(s, "\"projectPath\": \"japan.isp\"") && contains(s, "\"media\": \"DSC01.MOV\""));
        assert(!contains(s, "/home/u"));
        for (const auto &f : appModelFields())
            if (f.machineDependent && f.path.find('.') == std::string::npos && f.path.find('[') == std::string::npos)
                assert(!contains(s, "\"" + f.path + "\""));
        o.json = false;
        const std::string flat = formatModel(m, o);
        assert(contains(flat, "rack[1].bindName=\"s_day01\"") && contains(flat, "clips.count=1"));
    });

    test("grade params dump under the keys `set` writes", [] {
        AppModel m = populatedModel();
        m.gradeOwnParams.exposure = 0.35f;
        ModelDumpOptions o;
        o.json = true;
        const std::string s = formatModel(m, o);
        assert(contains(s, "\"exposure\": 0.3499999940395355") || contains(s, "\"exposure\": 0.35"));
        assert(contains(s, "\"curve\": \"0,0;1,1\""));
    });

    test("every key Cosmo's codec writes is addressable, under exactly one filter", [] {
        std::istringstream in(serializeParams(EditParams{}));
        std::string line;
        int n = 0;
        while (std::getline(in, line))
        {
            const std::string k = line.substr(0, line.find('='));
            if (k == "mask") continue;
            const ParamDef *d = cosmoKey(k);
            if (!d) std::printf("    unaddressable cosmo key: %s\n", k.c_str());
            assert(d && d->pattern == "<bind>." + d->filter + "." + k);
            ++n;
        }
        assert(n == 44);   // a new key fails the loop above first; this catches a codec that stopped writing one
        std::set<std::string> patterns;
        for (const auto &d : paramDefs()) assert(patterns.insert(d.pattern).second);
    });

    test("the API document names every command, event, model key and address", [] {
        const std::string j = apiJson(), md = apiMarkdown();
        for (const auto &s : commandSpecs()) assert(contains(j, "\"verb\": \"" + s.verb + "\"") && contains(md, "`" + s.verb));
        for (const auto &e : eventSpecs()) assert(contains(j, "\"name\": \"" + e.name + "\"") && contains(md, e.name));
        for (const auto &f : appModelFields()) assert(contains(j, "\"path\": \"" + f.path + "\""));
        for (const auto &d : paramDefs()) assert(contains(j, "\"address\": \"" + d.pattern + "\""));
        assert(apiJson() == j && apiMarkdown() == md);   // deterministic
    });

    std::printf("interstellar_service_tests: %d passed\n", gPassed);
    return 0;
}
