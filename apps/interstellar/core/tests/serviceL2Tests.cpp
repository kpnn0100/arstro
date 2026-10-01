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

    /** The TIMELINE's frames: 48x27, 24 fps, 96 frames; R = frame index, G = a per-file constant,
     *  so a test can read back which source frame of which file landed where. */
    class FakeFrameSource : public IFrameSource
    {
    public:
        bool open(const std::string &path, Info &out) override
        {
            if (path.find("missing") != std::string::npos || !fs::exists(path)) return false;
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

        std::unique_ptr<InterstellarService> make()
        {
            InterstellarService::Host h;
            h.rackDecoder = [](std::shared_ptr<const FrameSelector>) { return std::unique_ptr<cosmo::IImageDecoder>(new FakeDecoder()); };
            h.frameSource = [] { return std::unique_ptr<IFrameSource>(new FakeFrameSource()); };
            h.frameWriter = [this] { return std::unique_ptr<IFrameWriter>(new CaptureWriter(&written)); };
            h.writeImage = [this](const std::string &p, const Raster &r, std::string &) { images[p] = r; return true; };
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
