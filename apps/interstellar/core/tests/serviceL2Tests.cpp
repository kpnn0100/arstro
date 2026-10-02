/*
 *  interstellar_service_l2 — the REAL service, headless (R-TEST-2): a hosted CosmoService, the
 *  real project model and the real render path, with fake DECODERS only — never a fake rack.
 *
 *  Every test drives the service the way an agent does: text command lines through dispatchText,
 *  observation through the model, `output()` and the files on disk.
 */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "AppModelCodec.h"
#include "InterstellarService.h"
#include "Project.h"
#include "core/ThreadBudget.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

namespace fs = std::filesystem;
using namespace arstro;
using namespace arstro::interstellar;

namespace
{
    int gPassed = 0;

    std::string scratch(const std::string &name)
    {
        const char *base = std::getenv("INTERSTELLAR_TEST_DIR");
        const fs::path dir = fs::path(base ? base : "/tmp/interstellar-tests") / ("l2_" + name);
        fs::remove_all(dir);
        fs::create_directories(dir / "footage");
        return dir.string();
    }

    /** A flat colour per path, for the RACK (Cosmo grades it). */
    class FakeDecoder : public cosmo::IImageDecoder
    {
    public:
        cosmo::DecodedImage decodeFile(const std::string &path) override
        {
            cosmo::DecodedImage d;
            if (path.find("missing") != std::string::npos) return d;   // reads as failed
            d.width = 32;
            d.height = 18;
            d.rgba.assign((size_t)d.width * d.height * 4, 120);
            for (size_t i = 3; i < d.rgba.size(); i += 4) d.rgba[i] = 255;
            d.name = fs::path(path).filename().string();
            return d;
        }
    };

    /** How many times each file was opened for the timeline — a variant must not open its own. */
    std::map<std::string, int> gOpens;

    /** The TIMELINE's frames: 48x27, 24 fps, 96 frames; R = frame index, G = a per-file constant,
     *  so a test can read back which source frame of which file landed where. */
    class FakeFrameSource : public IFrameSource
    {
    public:
        bool open(const std::string &path, Info &out) override
        {
            if (path.find("missing") != std::string::npos || !fs::exists(path)) return false;
            gOpens[fs::path(path).filename().string()]++;
            mG = (uint8_t)(path.find("b.mp4") != std::string::npos ? 200 : 60);
            out.width = 48;
            out.height = 27;
            out.fps = 24;
            out.frames = 96;
            return true;
        }
        bool frameAt(long long f, Raster &out) override
        {
            out.allocate(48, 27);
            for (size_t i = 0; i < out.rgba.size(); i += 4)
            {
                out.rgba[i] = (uint8_t)(f & 0xff);
                out.rgba[i + 1] = mG;
                out.rgba[i + 2] = 100;
                out.rgba[i + 3] = 255;
            }
            return true;
        }

    private:
        uint8_t mG = 0;
    };

    struct CaptureWriter : public IFrameWriter
    {
        std::vector<Raster> *sink;
        explicit CaptureWriter(std::vector<Raster> *s) : sink(s) {}
        bool begin(const std::string &, int, int, double, long long) override { return true; }
        bool write(const Raster &f) override { sink->push_back(f); return true; }
        bool end() override { return true; }
    };

    struct Fixture
    {
        cosmo::ThreadBudget budget{50};
        std::vector<Raster> written;
        std::map<std::string, Raster> images;
        std::vector<std::string> events;
        std::unique_ptr<InterstellarService> svc;
        std::string dir;

        explicit Fixture(const std::string &name) : dir(scratch(name))
        {
            for (const char *f : {"a.mp4", "b.mp4", "still.png"}) std::ofstream(dir + "/footage/" + f) << "x";
            svc = make();
        }

        bool async = false;   // the GTK host's asyncPreview

        std::unique_ptr<InterstellarService> make()
        {
            InterstellarService::Host h;
            h.asyncPreview = async;
            h.rackDecoder = [](std::shared_ptr<const FrameSelector>) { return std::unique_ptr<cosmo::IImageDecoder>(new FakeDecoder()); };
            h.frameSource = [] { return std::unique_ptr<IFrameSource>(new FakeFrameSource()); };
            h.frameWriter = [this] { return std::unique_ptr<IFrameWriter>(new CaptureWriter(&written)); };
            h.writeImage = [this](const std::string &p, const Raster &r, std::string &) { images[p] = r; return true; };
            h.settingsPath = dir + "/settings.txt";
            h.presetDir = dir + "/presets";
            auto s = std::make_unique<InterstellarService>(budget, h);
            s->subscribe([this](const Event &e) { events.push_back(formatEvent(e)); });
            return s;
        }

        bool run(const std::string &line, std::string *err = nullptr)
        {
            std::string e;
            const bool ok = svc->dispatchText(line, e);
            if (ok) assert(svc->pumpUntilIdle(30000));
            if (err) *err = e;
            return ok;
        }
        void must(const std::string &line)
        {
            std::string e;
            if (!run(line, &e)) { std::fprintf(stderr, "  FAILED: %s\n  → %s\n", line.c_str(), e.c_str()); assert(false); }
        }
        std::string out(const std::string &line)
        {
            must(line);
            return svc->output();
        }
        std::string path(const std::string &f) const { return dir + "/" + f; }

        /** A project with three sources, a cut of two clips on main. */
        void standard()
        {
            must("project new \"" + path("mv.isp") + "\" --fps 24 --res 48x27");
            must("rack add \"" + path("footage/a.mp4") + "\" \"" + path("footage/b.mp4") + "#t=1.0\" \"" + path("footage/still.png") + "\"");
            must("track add --kind video");
            must("clip add --track v0 --src a --in 0 --out 2 --at 0 --name shotA");
            must("clip add --track v0 --src b --in 0 --out 2 --at 2 --name shotB");
        }
    };

    bool has(const std::string &hay, const std::string &needle) { return hay.find(needle) != std::string::npos; }

    double evalValue(Fixture &f, const std::string &line)
    {
        const std::string o = f.out(line);
        const auto eq = o.find('=');
        assert(eq != std::string::npos);
        return std::atof(o.c_str() + eq + 1);
    }

    /** Read `key` of the n-th #image straight out of the .cmp — no Interstellar code. */
    bool cmpValue(const std::string &cmp, int image, const std::string &key, double &out)
    {
        std::ifstream f(cmp);
        std::string line;
        int seen = -1;
        while (std::getline(f, line))
        {
            if (line == "#image") { ++seen; continue; }
            if (line == "#group" || line.rfind("#hnode", 0) == 0) continue;   // own params precede history
            if (seen == image && line.rfind(key + "=", 0) == 0)
            {
                out = std::atof(line.c_str() + key.size() + 1);
                return true;
            }
        }
        return false;
    }

    void test(const char *name, const std::function<void()> &fn)
    {
        fn();
        ++gPassed;
        std::printf("[PASS] %s\n", name);
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    test("a colour edit on a root timeline writes THROUGH to the .cmp (R-RACK-2)", [] {
        Fixture f("through");
        f.standard();
        f.must("set a.basic.exposure=0.35 a.basic.temp=5600");
        f.must("project save");
        double e = 0, t = 0;
        assert(cmpValue(f.path("mv.cmp"), 0, "exposure", e) && std::fabs(e - 0.35) < 1e-6);
        assert(cmpValue(f.path("mv.cmp"), 0, "temp", t) && std::fabs(t - 5600) < 1e-3);
        // And a second service — the closest thing to opening it in Cosmo — reads it back.
        Fixture g("through_b");
        g.svc = g.make();
        g.must("project open \"" + f.path("mv.isp") + "\"");
        assert(std::fabs(evalValue(g, "get a.basic.exposure") - 0.35) < 1e-6);
    });

    test("a version's colour is an OVERRIDE: a delta that keeps receiving the base (R-VER-2)", [] {
        Fixture f("override");
        f.standard();
        f.must("set a.basic.exposure=0.5");
        f.must("timeline new social30 --base main");
        f.must("timeline open social30");
        f.must("set a.basic.exposure=0.8");
        assert(std::fabs(evalValue(f, "eval a.basic.exposure") - 0.8) < 1e-6);
        assert(std::fabs(evalValue(f, "eval a.basic.exposure --timeline main") - 0.5) < 1e-6);
        // The rack is untouched: the override lives in the .isp, not the .cmp (R-G-3).
        f.must("project save");
        double onDisk = 0;
        assert(cmpValue(f.path("mv.cmp"), 0, "exposure", onDisk) && std::fabs(onDisk - 0.5) < 1e-6);
        std::ifstream isp(f.path("mv.isp"));
        std::stringstream ss;
        ss << isp.rdbuf();
        assert(has(ss.str(), "#tlgrade") && has(ss.str(), "exposure=0.3"));
        // A base regrade arrives live, under the override.
        f.must("timeline open main");
        f.must("set a.basic.exposure=0.6");
        assert(std::fabs(evalValue(f, "eval a.basic.exposure --timeline social30") - 0.9) < 1e-6);
        // Revert = inherit again.
        f.must("revert a.basic.exposure --timeline social30");
        assert(std::fabs(evalValue(f, "eval a.basic.exposure --timeline social30") - 0.6) < 1e-6);
    });

    test("a pin freezes the base's colour, refuses colour edits, and rebase advances it (R-VER-3, R-VER-4)", [] {
        Fixture f("pin");
        f.standard();
        f.must("set a.basic.exposure=0.5");
        f.must("timeline new delivery --base main");
        f.must("timeline pin delivery");
        f.must("set a.basic.exposure=0.1");   // on main, after the pin
        assert(std::fabs(evalValue(f, "eval a.basic.exposure --timeline delivery") - 0.5) < 1e-6);
        f.must("timeline open delivery");
        std::string err;
        assert(!f.run("set a.basic.exposure=0.7", &err));
        assert(has(err, "pinned at") && has(err, "R-VER-3"));
        const std::string rebased = f.out("timeline rebase delivery");
        assert(has(rebased, "pin "));
        assert(std::fabs(evalValue(f, "eval a.basic.exposure --timeline delivery") - 0.1) < 1e-6);
        assert(fs::exists(f.path("mv.pins")));
    });

    test("a non-scalar override on a version is refused with the way forward (R-VER-2, R-RACK-5)", [] {
        Fixture f("curve");
        f.standard();
        f.must("timeline new social30 --base main");
        f.must("timeline open social30");
        std::string err;
        assert(!f.run("set a.curve.curve=0,0;0.5,0.6;1,1", &err));
        assert(has(err, "rack duplicate a"));
        f.must("timeline open main");
        f.must("set a.curve.curve=0,0;0.5,0.6;1,1");   // on the base it writes through
    });

    test("an unknown address, key or filter is refused, naming the nearest (R-SVC-3)", [] {
        Fixture f("refuse");
        f.standard();
        std::string err;
        assert(!f.run("set aa.basic.exposure=1", &err) && has(err, "did you mean"));
        assert(!f.run("set a.basic.exposur=1", &err) && has(err, "exposure"));
        assert(!f.run("set a.detail.exposure=1", &err) && has(err, "under basic"));
        assert(!f.run("set shotA.opacty=0.5", &err) && has(err, "opacity"));
        assert(!f.run("set a.weight=2", &err) && has(err, "0..1"));
    });

    test("an inherited clip edited in a version records a #tlset, never a copy (R-G-3)", [] {
        Fixture f("tlset");
        f.standard();
        const size_t clips = f.svc->project().clips.size();
        f.must("timeline new social30 --base main");
        f.must("timeline open social30");
        f.must("set shotA.opacity=0.5");
        assert(f.svc->project().clips.size() == clips);
        assert(f.svc->project().tlset(f.svc->project().idForRef("social30"), f.svc->project().idForRef("shotA")));
        assert(has(f.out("eval shotA.opacity --explain"), "overrides it"));
        f.must("timeline open main");
        assert(has(f.out("get shotA.opacity"), "=1"));
    });

    test("clip trim takes SOURCE points, and a head trim keeps the frames where they were (R-TL-3)", [] {
        Fixture f("trim");
        f.standard();
        f.must("clip trim shotB --in 0.5");          // shotB: at 2, in 0 → in 0.5, at 2.5
        assert(std::fabs(evalValue(f, "get shotB.in") - 0.5) < 1e-9);
        assert(std::fabs(evalValue(f, "get shotB.at") - 2.5) < 1e-9);
        f.must("clip trim shotA --out 1.5");         // shotA: in 0, out 2 → out 1.5
        assert(std::fabs(evalValue(f, "get shotA.out") - 1.5) < 1e-9);
        assert(std::fabs(evalValue(f, "get shotA.at")) < 1e-9);
    });

    test("choosing a reference frame reloads nothing and the Grade monitor shows it (R-RACK-3)", [] {
        Fixture f("refframe");
        f.standard();
        f.must("rack select a");
        std::string err;
        assert(f.svc->dispatchText("rack frame a --at 1.5", err));
        assert(!f.svc->busy());                       // no rack reload in flight
        for (const auto &r : f.svc->model().rack) assert(!r.pending);
        assert(std::fabs(f.svc->project().rackObj(f.svc->project().idForRef("a"))->frame - 1.5) < 1e-9);
        // Past the cut, the monitor shows the Grade target's reference frame: FakeFrameSource's R
        // channel is the source frame index, 1.5 s × 24 = 36.
        Raster r;
        assert(f.svc->renderFrame(10.0, 0, r));
        assert(r.rgba[0] == 36);
    });

    test("a render NAMES its timeline; without one it is refused (R-RENDER-1)", [] {
        Fixture f("rname");
        f.standard();
        std::string err;
        assert(!f.run("render --out x.mp4", &err) && has(err, "R-RENDER-1"));
        f.must("render --timeline main --out \"" + f.path("out.mp4") + "\"");
        assert(f.written.size() == 96);   // 4 s at 24 fps
        assert(f.svc->model().renders.size() == 1 && f.svc->model().renders[0].state == "done");
        assert(f.svc->model().renders[0].timelineName == "main");
    });

    test("a render is a pure function: same timeline, same bytes, any order (R-RENDER-2, R-VOL-7)", [] {
        Fixture f("pure");
        f.standard();
        f.must("set a.basic.exposure=0.4");
        Raster x, y, z;
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, x));
        assert(f.svc->renderTimelineFrame("tl_1", 3.5, 0, z));   // a different source in between
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, y));
        assert(x.rgba == y.rgba && x.width == 48 && x.height == 27);
        f.must("render --timeline main --out \"" + f.path("one.mp4") + "\"");
        const auto first = f.written;
        f.written.clear();
        f.must("render --timeline main --out \"" + f.path("two.mp4") + "\"");
        assert(first.size() == f.written.size());
        for (size_t i = 0; i < first.size(); ++i) assert(first[i].rgba == f.written[i].rgba);
    });

    test("the grade reaches the pixels, and weight 0 is the ungraded frame (R-RACK-4)", [] {
        Fixture f("weight");
        f.standard();
        Raster ungraded, graded, weighted;
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, ungraded));
        f.must("set a.basic.exposure=1.0");
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, graded));
        long long su = 0, sg = 0;
        for (size_t i = 1; i < ungraded.rgba.size(); i += 4) { su += ungraded.rgba[i]; sg += graded.rgba[i]; }
        assert(sg > su);   // +1 EV is brighter
        f.must("set a.weight=0");
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, weighted));
        assert(weighted.rgba == ungraded.rgba);
    });

    test("a transition holds the outgoing clip and mixes both (R-TL-4)", [] {
        Fixture f("dissolve");
        f.standard();
        f.must("transition add --between shotA,shotB --dur 0.5");
        Raster mid;
        assert(f.svc->renderTimelineFrame("tl_1", 2.25, 0, mid));
        // G is 60 for a.mp4 and 200 for b.mp4: mid-dissolve sits strictly between.
        const int g = mid.rgba[(size_t)(13 * 48 + 24) * 4 + 1];
        assert(g > 90 && g < 170);
        // R is the SOURCE frame: shotA is held past its out (frame 47 is its last; 2.25 s -> 54).
        Raster before;
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, before));
        assert(before.rgba[1] == 60);
    });

    test("a reopened project binds every #rackobj to its Cosmo node again", [] {
        Fixture f("reopen");
        f.standard();
        f.must("set b.basic.contrast=25");
        f.must("rack group new Day --nodes a,b");
        f.must("set day.basic.exposure=0.2");
        f.must("project save");
        f.svc = f.make();
        f.must("project open \"" + f.path("mv.isp") + "\"");
        assert(std::fabs(evalValue(f, "get b.basic.contrast") - 25) < 1e-6);
        assert(std::fabs(evalValue(f, "get day.basic.exposure") - 0.2) < 1e-6);
        // The group's offset stacks onto its member through Cosmo's own composeParams.
        assert(std::fabs(evalValue(f, "eval a.basic.exposure") - 0.2) < 1e-6);
        for (const auto &r : f.svc->model().rack) assert(!r.bindName.empty() && !r.failed);
    });

    test("offline media leaves the project openable and reads as missing (R-RACK-7)", [] {
        Fixture f("offline");
        f.standard();
        f.must("project save");
        fs::remove(f.path("footage/still.png"));
        std::ofstream(f.path("footage/missing.png")) << "x";
        // Point the still at a file the decoder refuses.
        {
            Project p;
            std::string err;
            assert(p.load(f.path("mv.isp"), err));
            p.rackObj(p.idForRef("still"))->media = "footage/missing.png";
            assert(p.save(f.path("mv.isp"), err));
        }
        f.svc = f.make();
        f.must("project open \"" + f.path("mv.isp") + "\"");
        assert(f.svc->model().screen == Screen::Edit);
        assert(has(f.out("lint"), "offline   still"));
        bool offline = false;
        for (const auto &r : f.svc->model().rack) offline = offline || (r.bindName == "still" && r.failed);
        assert(offline);
        Raster r;
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, r));
    });

    test("a save is refused while a source is offline, so Cosmo cannot delete it (D-2)", [] {
        Fixture f("d2");
        f.standard();
        f.must("set a.basic.exposure=0.3");
        f.must("project save");
        std::ofstream(f.path("footage/missing.mp4")) << "x";
        f.must("rack add \"" + f.path("footage/missing.mp4") + "\"");   // FakeDecoder fails it: offline
        std::string err;
        assert(!f.run("project save", &err));
        assert(has(err, "NOT the rack") && has(err, "missing") && has(err, "D-2"));
        assert(!f.run("rack add \"" + f.path("footage/still.png") + "\"", &err) && has(err, "D-2"));
        assert(!f.run("timeline pin main", &err));
        // The .isp did save; the .cmp on disk still holds every source it held.
        double e = 0;
        assert(cmpValue(f.path("mv.cmp"), 0, "exposure", e) && std::fabs(e - 0.3) < 1e-6);
    });

    test("undo and redo span the rack and the project, and a drag is one step (R-EDIT-1)", [] {
        Fixture f("undo");
        f.standard();
        f.must("set a.basic.exposure=0.2");
        f.must("set a.basic.exposure=0.3");     // within half a second: the same drag
        f.must("set a.basic.exposure=0.4");
        f.must("clip move shotB --at 2.5");
        assert(f.svc->model().canUndo && f.svc->model().undoLabel.find("clip move") == 0);
        f.must("undo");                          // the cut
        assert(std::fabs(evalValue(f, "get shotB.at") - 2.0) < 1e-9);
        f.must("undo");                          // the whole exposure drag, written back THROUGH Cosmo
        assert(std::fabs(evalValue(f, "get a.basic.exposure")) < 1e-6);
        f.must("project save");
        double onDisk = 1;
        assert(cmpValue(f.path("mv.cmp"), 0, "exposure", onDisk) && std::fabs(onDisk) < 1e-6);
        f.must("redo");
        assert(std::fabs(evalValue(f, "get a.basic.exposure") - 0.4) < 1e-6);
        assert(f.svc->model().canRedo && f.svc->model().redoLabel.find("clip move") == 0);
        // A version's override is a project edit, undone like one.
        f.must("timeline new v2 --base main");
        f.must("timeline open v2");
        f.must("set a.basic.exposure=0.9");
        f.must("undo");
        assert(std::fabs(evalValue(f, "eval a.basic.exposure") - 0.4) < 1e-6);
        // A change to the rack's node set starts history over — as in Cosmo.
        f.must("rack group new Pair --nodes a,b");
        assert(!f.svc->model().canUndo && !f.svc->model().canRedo);
        std::string err;
        assert(!f.run("undo", &err) && err.find("nothing to undo") != std::string::npos);
    });

    test("engine settings: one CPU budget, persisted, and preview quality caps the monitor (R-SET)", [] {
        Fixture f("settings");
        f.standard();
        f.must("settings set cpuPercent=25 previewEdge=1000");
        assert(f.budget.percent() == 25);
        assert(f.svc->model().settings.cpuPercent == 25 && f.svc->model().settings.engineThreads == f.budget.engineThreads());
        std::string err;
        assert(!f.run("settings set cpuPrecent=50", &err) && err.find("cpuPercent") != std::string::npos);
        assert(!f.run("settings set uiScale=33", &err) && err.find("uiScale is one of") != std::string::npos);
        // Persisted: a new service on the same settings file comes up at 25 %.
        f.svc = f.make();
        assert(f.svc->model().settings.cpuPercent == 25 && f.budget.percent() == 25);
        // Preview quality caps the MONITOR's render edge; a render is unaffected.
        f.must("project new \"" + f.path("big.isp") + "\" --res 640x360");
        f.must("rack add \"" + f.path("footage/a.mp4") + "\"");
        f.must("track add --kind video");
        f.must("clip add --track v0 --src a --in 0 --out 2 --at 0");
        f.must("settings set previewEdge=256");
        Raster mon, full;
        assert(f.svc->renderFrame(1.0, 0, mon));
        assert(f.svc->renderTimelineFrame(f.svc->model().currentTimeline, 1.0, 0, full));
        assert(mon.width == 256 && mon.height == 144);
        assert(full.width == 640 && full.height == 360);
        f.must("settings set previewEdge=0");          // full
        assert(f.svc->renderFrame(1.0, 0, mon) && mon.width == 640);
    });

    test("grade copy / paste, ungroup and presets work like Cosmo's Develop and Preset menus (R-EDIT-2, R-EDIT-3)", [] {
        Fixture f("develop");
        f.standard();
        f.must("set a.basic.exposure=0.7 a.basic.contrast=15");
        f.must("grade copy a");
        assert(f.svc->model().hasGradeClipboard && f.svc->model().gradeClipboardFrom == "a");
        f.must("grade paste b");
        assert(std::fabs(evalValue(f, "get b.basic.exposure") - 0.7) < 1e-6);
        assert(std::fabs(evalValue(f, "get b.basic.contrast") - 15) < 1e-6);
        f.must("undo");                              // a paste is one undoable edit
        assert(std::fabs(evalValue(f, "get b.basic.exposure")) < 1e-6);
        // Presets: save a's grade, apply it to b, see it in the library listing.
        f.must("preset save Warm --node a");
        bool listed = false;
        for (const auto &p : f.svc->model().presets) listed = listed || p.name == "Warm";
        assert(listed);
        f.must("preset apply Warm --node b");
        assert(std::fabs(evalValue(f, "get b.basic.exposure") - 0.7) < 1e-6);
        // Import copies an .apf into the library.
        std::filesystem::copy_file(f.path("presets/Warm.apf"), f.path("Cool.apf"));
        f.must("preset import \"" + f.path("Cool.apf") + "\"");
        listed = false;
        for (const auto &p : f.svc->model().presets) listed = listed || p.name == "Cool";
        assert(listed);
        // Ungroup: the group goes, its member keeps its own grade.
        f.must("rack group new Pair --nodes a,b");
        f.must("set pair.basic.exposure=0.2");
        assert(std::fabs(evalValue(f, "eval a.basic.exposure") - 0.9) < 1e-6);
        f.must("rack ungroup pair");
        assert(std::fabs(evalValue(f, "eval a.basic.exposure") - 0.7) < 1e-6);
        assert(f.svc->project().idForRef("pair").empty());
        // On a version, a paste would be a second copy of colour — refused with the way forward.
        f.must("timeline new v2 --base main");
        f.must("timeline open v2");
        std::string err;
        assert(!f.run("grade paste b", &err) && err.find("version") != std::string::npos);
    });

    test("Save As into another folder keeps every source online", [] {
        Fixture f("saveas");
        f.standard();
        f.must("project save");
        std::filesystem::create_directories(f.path("elsewhere/deeper"));
        f.must("project save \"" + f.path("elsewhere/deeper/copy.isp") + "\"");
        f.svc = f.make();
        f.must("project open \"" + f.path("elsewhere/deeper/copy.isp") + "\"");
        for (const auto &r : f.svc->model().rack) assert(!r.failed);
        Raster r;
        bool any = false;
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, r, &any) && any);
    });

    test("the async monitor answers at once and delivers the synchronous pixels (D-5)", [] {
        Fixture f("asyncmon");
        f.standard();
        f.must("set a.basic.exposure=0.4");
        f.must("project save");
        Raster sync;
        assert(f.svc->renderFrame(1.0, 0, sync));          // the synchronous answer, for reference
        f.async = true;
        f.svc = f.make();
        f.must("project open \"" + f.path("mv.isp") + "\"");
        Raster r;
        const unsigned seq0 = f.svc->model().frameSeq;
        assert(!f.svc->renderFrame(1.0, 0, r));            // nothing finished yet: returns at once
        for (int i = 0; i < 400 && f.svc->model().frameSeq == seq0; ++i)
        {
            f.svc->pump(1e6 + i * 16.0);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        assert(f.svc->model().frameSeq != seq0);           // the landing raised frameSeq
        assert(f.svc->renderFrame(1.0, 0, r));
        assert(r.rgba == sync.rgba);                       // same plan, same pixels, any thread
        // A new request while a frame is shown: the old frame answers until the new one lands.
        Raster stale;
        assert(f.svc->renderFrame(3.0, 0, stale) && stale.rgba == sync.rgba);
    });

    test("a group's weight fades the group's own contribution (D-7, R-RACK-4)", [] {
        Fixture f("groupweight");
        f.standard();
        f.must("set a.basic.exposure=0.3");
        f.must("rack group new G --nodes a");
        f.must("set g.basic.exposure=1.2");
        Raster on, off, half, zero;
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, on));
        f.must("set g.bypass=1");
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, off));
        f.must("set g.bypass=0");
        f.must("set g.weight=0");
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, zero));
        assert(zero.rgba == off.rgba);                     // weight 0 ≡ Cosmo's bypass of the group
        assert(zero.rgba != on.rgba);
        f.must("set g.weight=0.5");
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, half));
        const size_t px = (size_t)(13 * 48 + 24) * 4 + 1;  // G channel, mid frame
        assert(half.rgba[px] > std::min(off.rgba[px], on.rgba[px]) && half.rgba[px] < std::max(off.rgba[px], on.rgba[px]));
    });

    test("Shift selects a range, Ctrl toggles, Group Selection groups it — like Cosmo (R-RACK-8)", [] {
        Fixture f("select");
        f.standard();                                   // a, b, still
        auto sel = [&] {
            std::string out;
            for (const auto &r : f.svc->model().rack)
                if (r.selected) out += r.bindName + " ";
            return out;
        };
        f.must("rack select a");
        f.must("rack select still --range");
        assert(sel() == "a b still ");
        f.must("rack select b --add");                  // Ctrl-click takes b out
        assert(sel() == "a still ");
        f.must("rack select b --add");                  // …and back in
        assert(sel() == "a still b " || sel() == "a b still ");
        f.must("rack group new");                       // no name, no --nodes: the selection
        int grouped = 0;
        for (const auto &r : f.svc->model().rack)
            if (!r.group && r.parent >= 0) ++grouped;
        assert(grouped == 3);
        assert(!f.svc->project().idForRef("group").empty());
    });

    test("a source leaves the rack only when no clip uses it (R-RACK-8)", [] {
        Fixture f("remove");
        f.standard();
        std::string err;
        assert(!f.run("rack remove a", &err) && err.find("shotA") != std::string::npos);
        f.must("rack remove still");
        assert(f.svc->project().idForRef("still").empty());
        for (const auto &r : f.svc->model().rack) assert(r.bindName != "still");
        f.must("project save");
        std::ifstream cmp(f.path("mv.cmp"));
        std::stringstream ss;
        ss << cmp.rdbuf();
        assert(ss.str().find("still.png") == std::string::npos);   // gone from Cosmo's project too
    });

    test("a source previews at any time, the monitor captures at full size, the length reaches the model (R-RACK-3, R-UI-3, R-UI-11)", [] {
        Fixture f("capture");
        f.standard();
        f.must("set a.basic.exposure=0.5");
        f.must("rack select a");
        for (const auto &r : f.svc->model().rack)
            if (r.bindName == "a") { assert(std::fabs(r.mediaDuration - 4.0) < 1e-9); assert(std::fabs(r.mediaFps - 24.0) < 1e-9); }   // 96 frames / 24
        // The slider's live preview: a source at ANY time, graded — FakeFrameSource's R is the frame.
        Raster p0, p1;
        assert(f.svc->renderSourceFrame("a", 1.5, 0, p0) && p0.width == 48);
        assert(f.svc->renderSourceFrame("a", 2.5, 0, p1));
        Raster u0;
        f.must("set a.weight=0");                          // ungraded, to read the frame index
        assert(f.svc->renderSourceFrame("a", 1.5, 0, u0) && u0.rgba[0] == 36);
        f.must("set a.weight=1");
        assert(p0.rgba != p1.rgba);
        // capture: Grade's frame (the source's reference frame) and the cut at the playhead.
        f.must("rack frame a --at 2.0");
        Raster cap, ref;
        assert(f.svc->captureFrame("a", cap) && f.svc->renderSourceFrame("a", -1.0, 0, ref) && cap.rgba == ref.rgba);
        f.must("playhead 3.0");
        Raster capCut, cut;
        assert(f.svc->captureFrame("", capCut) && f.svc->renderTimelineFrame("tl_1", 3.0, 0, cut) && capCut.rgba == cut.rgba);
        f.must("capture --source a --out \"" + f.path("grade.png") + "\"");
        assert(f.images.count(f.path("grade.png")) && f.images[f.path("grade.png")].rgba == cap.rgba);
        std::string err;
        assert(!f.run("capture --source nobody --out x.png", &err));
    });

    test("a variant shares its file, is its own object, and is selected once made (R-RACK-5)", [] {
        Fixture f("variant");
        f.standard();
        f.must("set a.basic.exposure=0.5");
        Raster before;
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, before));   // a.mp4 is open for the cut now
        const int opens = gOpens["a.mp4"];
        f.must("rack duplicate a");
        const auto &m = f.svc->model();
        int ia = -1, iv = -1, ib = -1;
        for (int i = 0; i < (int)m.rack.size(); ++i)
        {
            if (m.rack[(size_t)i].bindName == "a") ia = i;
            if (m.rack[(size_t)i].bindName == "a_v") iv = i;
            if (m.rack[(size_t)i].bindName == "b") ib = i;
        }
        assert(ia >= 0 && iv >= 0 && ib >= 0);
        assert(m.selectedRack == iv && m.rack[(size_t)iv].selected);           // selected once made
        assert(m.rack[(size_t)iv].media == m.rack[(size_t)ia].media);           // the SAME file
        assert(m.rack[(size_t)ia].sharesMedia == 1 && m.rack[(size_t)iv].sharesMedia == 1 && m.rack[(size_t)ib].sharesMedia == 0);
        assert(std::fabs(evalValue(f, "eval a_v.basic.exposure") - 0.5) < 1e-6);   // starts with the original's grade
        // its own object: a different grade, its own clip, its own pixels — on the same decoder
        f.must("set a_v.basic.exposure=-1");
        assert(std::fabs(evalValue(f, "eval a.basic.exposure") - 0.5) < 1e-6);
        f.must("clip add --track v0 --src a_v --in 0 --out 1 --at 4 --name shotV");
        Raster orig, var;
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, orig) && f.svc->renderTimelineFrame("tl_1", 4.0, 0, var));
        assert(orig.rgba == before.rgba && orig.rgba != var.rgba);
        assert(gOpens["a.mp4"] == opens);                                       // no second decoder for the variant
        // the flag that was never honoured is gone (D-8): refused, not ignored
        std::string err;
        assert(!f.run("rack add \"" + f.path("footage/still.png") + "\" --group gr1", &err) && has(err, "group"));
    });

    test("the same script on two services dumps the same stable state", [] {
        Fixture a("equiv_a"), b("equiv_b");
        for (Fixture *f : {&a, &b})
        {
            f->standard();
            f->must("set a.basic.exposure=0.3");
            f->must("timeline new v2 --base main");
            f->must("timeline open v2");
            f->must("set shotB.at=2.5");
        }
        ModelDumpOptions o;
        o.json = true;
        o.stable = true;
        std::string da = formatModel(a.svc->model(), o), db = formatModel(b.svc->model(), o);
        // The two scratch directories differ by name only; stable dumps hold file NAMES.
        assert(da == db);
    });

    std::printf("interstellar_service_l2: %d passed\n", gPassed);
    return 0;
}
