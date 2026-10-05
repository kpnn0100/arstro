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
#include "ColourTransform.h"
#include "Lut.h"
#include "InterstellarService.h"
#include "Project.h"
#include "Versions.h"
#include "core/ThreadBudget.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <functional>
#include <map>
#include <set>
#include <mutex>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
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
        explicit FakeDecoder(std::shared_ptr<const FrameSelector> sel = nullptr) : mSel(std::move(sel)) {}
        std::shared_ptr<const FrameSelector> mSel;
        cosmo::DecodedImage decodeFile(const std::string &path) override
        {
            cosmo::DecodedImage d;
            if (path.find("missing") != std::string::npos) return d;   // reads as failed
            // R-MEDIA-3: as the real seam does — the file the .isp names now; a file that is not there fails
            std::string file;
            double t = 0;
            splitFrameSelector(path, file, t);
            if (mSel) file = mSel->fileFor(path, file);
            if (!fs::exists(file) && !seq::firstFrameExists(file)) return d;
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

    /** R-PLAY-1: "video files" the capture writer finished under a `.cache/` directory, readable back
     *  through the fake source — lossless, so a cached frame can be compared with a graded one. The
     *  file itself is touched on disk, as a real writer would leave it. */
    std::mutex gMemMu;
    std::map<std::string, std::vector<Raster>> gMemFiles;
    int gCacheSegmentsWritten = 0;

    /** The TIMELINE's frames: 48x27, 24 fps, 96 frames; R = frame index, G = a per-file constant,
     *  so a test can read back which source frame of which file landed where. */
    class FakeFrameSource : public IFrameSource
    {
    public:
        bool open(const std::string &path, Info &out) override
        {
            {
                std::lock_guard<std::mutex> l(gMemMu);
                const auto m = gMemFiles.find(path);
                if (m != gMemFiles.end())
                {
                    mMem = m->second;
                    out.width = mMem.front().width;
                    out.height = mMem.front().height;
                    out.fps = 24;
                    out.frames = (long long)mMem.size();
                    return true;
                }
            }
            if (path.find("missing") != std::string::npos || (!fs::exists(path) && !seq::firstFrameExists(path))) return false;   // a DNG sequence: its frames
            gOpens[fs::path(path).filename().string()]++;
            mG = (uint8_t)(path.find("b.mp4") != std::string::npos ? 200 : 60);
            mEdge = path.find("edge") != std::string::npos;   // a hard vertical edge: what a blur changes
            mRamp = path.find("ramp") != std::string::npos;   // R-COLOR-1: a ramp finer than 8 bits
            mMove = path.find("move.mp4") != std::string::npos;   // R-CLR-2: a 7×7 square drifting right and down
            if (path.find("a.mp4") != std::string::npos) { out.timecode = "01:00:10:00"; out.reel = "A001"; }   // R-XCH-5
            if (path.find("b.mp4") != std::string::npos) out.timecode = "01:00:11:00";   // R-EDT-5: one second after a
            out.width = 48;
            out.height = 27;
            out.fps = 24;
            out.frames = 96;
            return true;
        }
        /** The ramp at 16 bits: 48 steps of 12/65535 — about two 8-bit codes from end to end. */
        bool frameAtDeep(long long f, Raster &out) override
        {
            if (!mRamp) return IFrameSource::frameAtDeep(f, out);
            out.allocate16(48, 27, 65535);
            for (int y = 0; y < 27; ++y)
                for (int x = 0; x < 48; ++x)
                {
                    uint16_t *p = &out.rgba16[((size_t)y * 48 + x) * 4];
                    p[0] = p[1] = p[2] = (uint16_t)(30000 + x * 12);
                }
            return true;
        }
        bool frameAt(long long f, Raster &out) override
        {
            if (mRamp)
            {
                Raster deep;
                frameAtDeep(f, deep);
                toShallow(deep, out);
                return true;
            }
            if (!mMem.empty())
            {
                if (f < 0 || f >= (long long)mMem.size()) return false;
                out = mMem[(size_t)f];
                return true;
            }
            out.allocate(48, 27);
            if (mMove)
            {
                // frame f: the square's top-left at (4 + f, 6 + f / 2), on a dark ground
                for (size_t i = 0; i < out.rgba.size(); i += 4) { out.rgba[i] = out.rgba[i + 1] = out.rgba[i + 2] = 20; out.rgba[i + 3] = 255; }
                const int x0 = 4 + (int)f, y0 = 6 + (int)f / 2;
                for (int y = y0; y < y0 + 7 && y < 27; ++y)
                    for (int x = x0; x < x0 + 7 && x < 48; ++x) { uint8_t *p = &out.rgba[((size_t)y * 48 + x) * 4]; p[0] = p[1] = p[2] = 230; }
                return true;
            }
            for (size_t i = 0; i < out.rgba.size(); i += 4)
            {
                if (mEdge)
                {
                    const bool left = (i / 4) % 48 < 24;
                    out.rgba[i] = out.rgba[i + 1] = out.rgba[i + 2] = left ? 255 : 0;
                    out.rgba[i + 3] = 255;
                    continue;
                }
                out.rgba[i] = (uint8_t)(f & 0xff);
                out.rgba[i + 1] = mG;
                out.rgba[i + 2] = 100;
                out.rgba[i + 3] = 255;
            }
            return true;
        }

    private:
        uint8_t mG = 0;
        bool mEdge = false, mRamp = false, mMove = false;
        std::vector<Raster> mMem;
    };

    /** R-AUD-5 (amended): every file's sound is a constant — a.mp4 0.1, b.mp4 0.2, music.wav 0.3 —
     *  for 4 s, so a sum, a gain, a mute or a solo reads straight off the samples. A still is silent. */
    class FakeAudioSource : public IAudioSource
    {
    public:
        bool open(const std::string &path, int rate, Info &out) override
        {
            if (path.find("still") != std::string::npos || !fs::exists(path)) return false;
            mRate = rate;
            mLevel = path.find("music") != std::string::npos ? 0.3f : path.find("b.mp4") != std::string::npos ? 0.2f : 0.1f;
            out.rate = rate;
            out.duration = 4.0;
            return true;
        }
        bool read(long long start, int frames, float *st) override
        {
            for (int k = 0; k < frames; ++k)
            {
                const long long f = start + k;
                st[k * 2] = st[k * 2 + 1] = f >= 0 && f < 4LL * mRate ? mLevel : 0.0f;
            }
            return true;
        }
        int mRate = 48000;
        float mLevel = 0.1f;
    };
    std::vector<float> gAudio;   // what the last render wrote as sound

    /** R-AUD-6: a sound output that takes samples at the rate a device would (real time), so the
     *  audio clock is a real clock; it keeps what it was given, and says when it was flushed. */
    struct FakeAudioOut : public IAudioOut
    {
        static std::mutex mu;
        static std::vector<float> heard;
        static int flushes, opens;
        static bool refuse;
        int rate = 48000;
        bool open(int r, std::string &err) override
        {
            if (refuse) { err = "no sound server (test)"; return false; }
            rate = r;
            std::lock_guard<std::mutex> l(mu);
            ++opens;
            return true;
        }
        bool write(const float *st, int frames) override
        {
            {
                std::lock_guard<std::mutex> l(mu);
                heard.insert(heard.end(), st, st + (size_t)frames * 2);
            }
            std::this_thread::sleep_for(std::chrono::microseconds((long long)(frames * 1e6 / rate)));
            return true;
        }
        double latency() override { return 0.05; }
        void flush() override
        {
            std::lock_guard<std::mutex> l(mu);
            ++flushes;
        }
    };
    std::mutex FakeAudioOut::mu;
    std::vector<float> FakeAudioOut::heard;
    int FakeAudioOut::flushes = 0, FakeAudioOut::opens = 0;
    bool FakeAudioOut::refuse = false;

    /** What the last render asked its writer for (R-RENDER-6). */
    struct WriterBegin { std::string path; int w = 0, h = 0; double fps = 0; long long frames = 0; EncodeSpec spec; };
    WriterBegin gBegin;
    bool gNoHardware = false;   // the capture writer plays a machine with no video unit (R-PLAY-3)

    EncodeSpec gProxySpec;   // R-MEDIA-2: what the last proxy was encoded as
    std::vector<std::vector<OverlayText>> gBurns;   // R-DLV-1/2: what each frame was asked to carry
    struct CaptureWriter : public IFrameWriter
    {
        std::vector<Raster> *sink;
        explicit CaptureWriter(std::vector<Raster> *s) : sink(s) {}
        bool begin(const std::string &p, int w, int h, double fps, long long n, const EncodeSpec &spec) override
        {
            mPath = p;
            mCacheFile = p.find(".cache/") != std::string::npos || p.find(".proxies/") != std::string::npos;   // R-PLAY-1, R-MEDIA-2: read back
            mFrames.clear();
            if (p.find(".proxies/") != std::string::npos) gProxySpec = spec;
            if (mCacheFile) { mCacheSpec = spec; mCacheW = w; mCacheH = h; return true; }   // not a render: gBegin is the render's
            gBegin = WriterBegin{p, w, h, fps, n, spec};
            gAudio.clear();
            mNote = spec.hardware && gNoHardware ? "hardware video unavailable (test) \xe2\x80\x94 encoded in software" : "";
            return true;
        }
        bool write(const Raster &f) override
        {
            if (mCacheFile) { mFrames.push_back(f); return f.width == mCacheW && f.height == mCacheH; }
            sink->push_back(f);
            return true;
        }
        bool end() override
        {
            if (!mCacheFile) return true;
            std::ofstream(mPath).put('\0');
            std::lock_guard<std::mutex> l(gMemMu);
            gMemFiles[mPath] = std::move(mFrames);
            if (mPath.find(".cache/") != std::string::npos) ++gCacheSegmentsWritten;
            return true;
        }
        bool writeAudio(const float *st, int frames) override
        {
            if (!mCacheFile) gAudio.insert(gAudio.end(), st, st + (size_t)frames * 2);
            return true;
        }
        std::string note() const override { return mNote; }
        std::string mNote, mPath;
        bool mCacheFile = false;
        EncodeSpec mCacheSpec;
        int mCacheW = 0, mCacheH = 0;
        std::vector<Raster> mFrames;
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
            for (const char *f : {"a.mp4", "b.mp4", "still.png", "edge.mp4", "ramp.mp4", "music.wav", "move.mp4"}) std::ofstream(dir + "/footage/" + f) << "x";
            svc = make();
        }

        bool async = false;   // the GTK host's asyncPreview
        bool sound = false;   // a sound output (R-AUD-6)
        bool noText = false;  // a host that cannot draw text (R-DLV-2)
        double now = 1e12;    // a pump clock ahead of the service's own (it only moves forward)

        std::unique_ptr<InterstellarService> make()
        {
            InterstellarService::Host h;
            h.asyncPreview = async;
            h.rackDecoder = [](std::shared_ptr<const FrameSelector> sel) { return std::unique_ptr<cosmo::IImageDecoder>(new FakeDecoder(sel)); };
            h.frameSource = [] { return std::unique_ptr<IFrameSource>(new FakeFrameSource()); };
            h.frameWriter = [this] { return std::unique_ptr<IFrameWriter>(new CaptureWriter(&written)); };
            h.audioSource = [] { return std::unique_ptr<IAudioSource>(new FakeAudioSource()); };
            if (sound) h.audioOut = [] { return std::unique_ptr<IAudioOut>(new FakeAudioOut()); };
            h.writeImage = [this](const std::string &p, const Raster &r, std::string &) { images[p] = r; return true; };
            if (!noText)
                h.drawText = [](Raster &frame, const std::vector<OverlayText> &items) {
                    gBurns.push_back(items);
                    // a mark where it drew, so a test can see the text is laid over the finished frame
                    if (!frame.deep() && !frame.rgba.empty()) { frame.rgba[0] = 255; frame.rgba[1] = 0; frame.rgba[2] = 255; }
                    return true;
                };
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
        // INTERSTELLAR_TEST_ONLY=<text>: only the tests whose name holds it (a mutant's quick run)
        const char *only = std::getenv("INTERSTELLAR_TEST_ONLY");
        if (only && *only && !std::strstr(name, only)) return;
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
        // 48x27 is odd: a video codec needs an even frame, so the render asks for 46x26 (same aspect)
        f.must("render --timeline main --res 46x26 --out \"" + f.path("out.mp4") + "\"");
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
        f.must("render --timeline main --res 46x26 --out \"" + f.path("one.mp4") + "\"");
        const auto first = f.written;
        f.written.clear();
        f.must("render --timeline main --res 46x26 --out \"" + f.path("two.mp4") + "\"");
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

    test("a render carries its whole output spec and refuses what the codec cannot honour (R-RENDER-6)", [] {
        Fixture f("rspec");
        f.standard();
        std::string err;
        auto refused = [&](const std::string &flags, const std::string &why) {
            const bool r = !f.run("render --timeline main " + flags, &err) && has(err, why);
            if (!r) std::fprintf(stderr, "  expected a refusal naming '%s' for: %s\n  got: %s\n", why.c_str(), flags.c_str(), err.c_str());
            return r;
        };
        // every flag checked against the codec, before anything is queued
        assert(refused("--out x.mp4", "even dimensions"));
        assert(refused("--format prores --res 46x26 --out x.mp4", ".mov"));
        assert(refused("--format h264 --res 46x26 --out x.mov --profile hq", "--profile applies to ProRes and DNxHR"));
        assert(refused("--format prores --res 46x26 --out x.mov --quality 20", "quality is its --profile"));
        assert(refused("--format prores --res 46x26 --out x.mov --profile ultra", "proxy, lt, standard, hq or 4444"));
        assert(refused("--format h264 --res 46x26 --out x.mp4 --bits 10", "H.265 offers 10-bit"));
        assert(refused("--format dnxhr --res 46x26 --out x.mov --profile hq --bits 10", "hqx and 444 are 10-bit"));
        assert(refused("--format h265 --res 46x26 --out x.mp4 --quality 60", "0 (best)"));
        assert(refused("--format h265 --res 46x26 --out x.mp4 --speed warp", "ultrafast"));
        assert(refused("--res 96x54 --out x.mp4", "never upscales"));
        assert(refused("--res 24x24 --out x.mp4", "aspect"));
        assert(refused("--fps 0 --res 46x26 --out x.mp4", "--fps"));
        assert(refused("--format png-seq --out frames --quality 10", "lossless"));
        assert(f.svc->model().renders.empty());
        // a whole spec reaches the writer, and the queue row says all of it
        f.must("render --timeline main --format h265 --bits 10 --quality 22 --speed slow --res 24x14 --fps 12 --out \"" + f.path("o.mkv") + "\"");
        assert(gBegin.spec.codec == "h265" && gBegin.spec.bitDepth == 10 && gBegin.spec.quality == 22 && gBegin.spec.speed == "slow");
        assert(gBegin.w == 24 && gBegin.h == 14 && std::fabs(gBegin.fps - 12.0) < 1e-9);
        assert(f.written.size() == 48);                                   // 4 s sampled at 12 fps
        assert(f.written[0].width == 24 && f.written[0].height == 14);    // rendered AT the output size
        const auto &r = f.svc->model().renders.back();
        std::printf("    queue row: %s\n", r.spec.c_str());
        assert(r.width == 24 && r.height == 14 && std::fabs(r.fps - 12.0) < 1e-9);
        assert(has(r.spec, "H.265 10-bit") && has(r.spec, "q22") && has(r.spec, "slow") && has(r.spec, "24\xC3\x97" "14") && has(r.spec, "12 fps"));
        // sampled at the output rate: frame k of a 12 fps render IS the timeline at k/12 s
        Raster at1, w12;
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 24, at1));
        assert(f.written[12].deep());                                     // a 10-bit render is a deep one (R-COLOR-1)…
        toShallow(f.written[12], w12);                                    // …the same picture within a code value
        assert(w12.rgba.size() == at1.rgba.size());
        for (size_t i = 0; i < at1.rgba.size(); ++i) assert(std::abs(at1.rgba[i] - w12.rgba[i]) <= 1);
        // intermediates: profile and depth from the profile
        f.must("render --timeline main --format dnxhr --profile hqx --res 46x26 --out \"" + f.path("o.mov") + "\"");
        assert(gBegin.spec.codec == "dnxhr" && gBegin.spec.profile == "hqx" && gBegin.spec.bitDepth == 10);
        f.must("render --timeline main --format prores --res 46x26 --out \"" + f.path("p.mov") + "\"");
        assert(gBegin.spec.profile == "standard" && gBegin.spec.bitDepth == 10 && has(f.svc->model().renders.back().spec, "ProRes 422 10-bit"));
        f.must("render --timeline main --res 46x26 --fps 24000/1001 --out \"" + f.path("ntsc.mp4") + "\"");
        assert(std::fabs(gBegin.fps - 24000.0 / 1001.0) < 1e-12 && has(f.svc->model().renders.back().spec, "23.976 fps"));
        // R-PLAY-3: the video unit is a setting for H.264/H.265, overridable per render, and said
        assert(!gBegin.spec.hardware);                                    // off by default
        assert(refused("--format prores --res 46x26 --out x.mov --encoder hardware", "no hardware encoder"));
        assert(refused("--res 46x26 --out x.mp4 --encoder gpu", "software or hardware"));
        f.must("settings set hardwareVideo=1");
        f.must("render --timeline main --res 46x26 --out \"" + f.path("hw.mp4") + "\"");
        assert(gBegin.spec.hardware && has(f.svc->model().renders.back().spec, "hardware"));
        f.must("render --timeline main --format prores --res 46x26 --out \"" + f.path("hw.mov") + "\"");
        assert(!gBegin.spec.hardware);                                    // an intermediate is never sent to the video unit
        f.must("render --timeline main --res 46x26 --encoder software --out \"" + f.path("sw.mp4") + "\"");
        assert(!gBegin.spec.hardware);
        gNoHardware = true;                                               // no video unit: the render still finishes, and says so
        f.must("render --timeline main --res 46x26 --out \"" + f.path("fb.mp4") + "\"");
        gNoHardware = false;
        assert(f.svc->model().renders.back().state == "done" && has(f.svc->model().renders.back().spec, "encoded in software"));
        f.svc = f.make();                                                 // persisted
        assert(f.svc->model().settings.hardwareVideo);
    });

    test("a 10-bit delivery carries more than 8 bits: decoded, graded, composited and encoded deep (R-COLOR-1)", [] {
        Fixture f("deep");
        // 48x26 (codecs want even sizes) from a 48x27 source: the composite resamples, at full size
        f.must("project new \"" + f.path("mv.isp") + "\" --fps 24 --res 48x26");
        f.must("rack add \"" + f.path("footage/ramp.mp4") + "\"");
        f.must("track add --kind video");
        f.must("clip add --track v0 --src ramp --in 0 --out 1 --at 0 --name shot");
        f.must("set ramp.basic.exposure=0.8");
        f.must("set ramp.basic.contrast=15");
        f.must("set shot.opacity=0.9");               // the composite mixes, not copies
        auto levels = [](const Raster &r) {
            std::set<int> v;
            for (int x = 0; x < r.width; ++x)
                v.insert(r.deep() ? r.rgba16[((size_t)13 * r.width + x) * 4] : r.rgba[((size_t)13 * r.width + x) * 4] * 257);
            return (int)v.size();
        };
        // ProRes is 10-bit: every frame reaches the encoder at 16 bits, and the ramp's steps survive
        f.must("render --timeline main --format prores --out \"" + f.path("p.mov") + "\"");
        assert(f.svc->model().renders.back().state == "done" && f.written.size() == 24);
        assert(f.written[0].deep() && f.written[0].width == 48 && f.written[0].height == 26);
        const int deepLevels = levels(f.written[0]);
        // H.264 here is 8-bit: the same timeline, a handful of codes
        f.written.clear();
        f.must("render --timeline main --format h264 --out \"" + f.path("h.mp4") + "\"");
        assert(!f.written[0].deep());
        const int shallowLevels = levels(f.written[0]);
        std::printf("    row levels: %d at 16-bit vs %d at 8-bit\n", deepLevels, shallowLevels);
        assert(deepLevels >= 40 && shallowLevels <= 6);
        // the two are the same picture, the deep one unrounded
        f.written.clear();
        f.must("render --timeline main --format h265 --bits 10 --out \"" + f.path("h.mkv") + "\"");
        Raster back, eight;
        toShallow(f.written[0], back);
        assert(f.svc->renderTimelineFrame(f.svc->model().timelines[0].id, 0.0, 0, eight) && !eight.deep());
        for (size_t i = 0; i < eight.rgba.size(); ++i) assert(std::abs(eight.rgba[i] - back.rgba[i]) <= 1);
    });

    test("a source says what it is, the project where it is graded, a render what it delivers (R-COLOR-2..4)", [] {
        Fixture f("colour");
        f.standard();
        std::string err;
        const std::string tl = f.svc->model().timelines[0].id;
        Raster plain;
        assert(f.svc->renderTimelineFrame(tl, 0.5, 0, plain));               // warms the frame cache too
        // the address: a source's interpretation, refused for a group or a space it does not know
        assert(!f.run("set a.input=slog4", &err) && has(err, "slog3"));
        f.must("rack group new gr --nodes a");
        assert(!f.run("set gr.input=slog3", &err) && has(err, "group"));
        f.must("set a.input=slog3");
        assert(has(f.out("get a.input"), "a.input=slog3"));
        int ia = -1;
        for (int i = 0; i < (int)f.svc->model().rack.size(); ++i) if (f.svc->model().rack[(size_t)i].bindName == "a") ia = i;
        assert(ia >= 0 && f.svc->model().rack[(size_t)ia].input == "slog3");
        assert(f.svc->model().colourInputs.size() == render::colour::inputs().size() && f.svc->model().workingSpace == "rec709");
        // the frame is the input transform of the plain one, exactly (identity grade) — not the cached one
        Raster logged, want = plain;
        assert(f.svc->renderTimelineFrame(tl, 0.5, 0, logged));
        render::colour::Transform::input("slog3", "rec709").apply(want);
        assert(logged.rgba == want.rgba && logged.rgba != plain.rgba);
        // saved on the #rackobj, read back, and undone like any edit
        f.must("project save");
        f.svc = f.make();
        f.must("project open \"" + f.path("mv.isp") + "\"");
        assert(has(f.out("get a.input"), "a.input=slog3"));
        f.must("set a.input=vlog");
        f.must("undo");
        assert(has(f.out("get a.input"), "a.input=slog3"));
        f.must("set a.input=rec709");
        Raster back;
        assert(f.svc->renderTimelineFrame(tl, 0.5, 0, back) && back.rgba == plain.rgba);

        // ACEScct: a Rec.709 source is converted in and the monitor's view brings it back
        assert(!f.run("colour working aces", &err) && has(err, "acescct"));
        f.must("colour working acescct");
        assert(f.svc->model().workingSpace == "acescct");
        Raster viewed;
        assert(f.svc->renderTimelineFrame(tl, 0.5, 0, viewed) && viewed.rgba.size() == plain.rgba.size());
        int off = 0;
        for (size_t i = 0; i < plain.rgba.size(); ++i) off = std::max(off, std::abs(viewed.rgba[i] - plain.rgba[i]));
        assert(off <= 2);
        f.must("undo");
        assert(f.svc->model().workingSpace == "rec709");
        f.must("colour working acescct");

        // a render's output: PQ, 10-bit, its pixels the PQ transform of the working picture
        assert(!f.run("render --timeline main --format h264 --output pq --res 46x26 --out \"" + f.path("x.mp4") + "\"", &err) && has(err, "10 bits"));
        assert(!f.run("render --timeline main --format h265 --output srgb --peak 1000 --res 46x26 --out \"" + f.path("x.mp4") + "\"", &err) && has(err, "PQ's"));
        assert(!f.run("render --timeline main --format h265 --output pq --peak 50 --res 46x26 --out \"" + f.path("x.mp4") + "\"", &err) && has(err, "400"));
        f.must("settings set hardwareVideo=1");
        f.written.clear();
        f.must("render --timeline main --format h265 --output pq --peak 1000 --range 0:0.5 --res 46x26 --out \"" + f.path("pq.mkv") + "\"");
        assert(gBegin.spec.output == "pq" && gBegin.spec.peak == 1000.0 && gBegin.spec.bitDepth == 10 && !gBegin.spec.hardware);
        assert(has(f.svc->model().renders.back().spec, "HDR PQ 1000 cd/m"));
        assert(!f.written.empty() && f.written[0].deep());
        // the working picture (ACEScct, no view) at 16 bits, through the PQ output: what was written
        f.must("colour working rec709");
        Raster w709;
        assert(f.svc->renderTimelineFrame(tl, 0.0, 46, w709) && w709.width == 46);
        Raster expect;
        toDeep(w709, expect);
        render::colour::Transform::input("rec709", "acescct").apply(expect);
        render::colour::Transform::output("acescct", "pq", 1000.0).apply(expect);
        int worst = 0;
        for (size_t i = 0; i < expect.rgba16.size(); ++i) worst = std::max(worst, std::abs((int)expect.rgba16[i] - (int)f.written[0].rgba16[i]));
        std::printf("    PQ render vs the transforms applied by hand: worst %d / 65535\n", worst);
        assert(worst <= 2);
        f.must("settings set hardwareVideo=0");
    });

    test("a LUT goes on a source, in its stack, and comes out of its grade (R-COLOR-5, R-COLOR-6)", [] {
        Fixture f("luts");
        f.standard();
        std::string err;
        const std::string tl = f.svc->model().timelines[0].id;
        {
            std::ofstream c(f.path("invert.cube"));
            c << "TITLE \"invert\"\nLUT_3D_SIZE 2\n";
            for (int b = 0; b < 2; ++b) for (int g = 0; g < 2; ++g) for (int r = 0; r < 2; ++r) c << 1 - r << " " << 1 - g << " " << 1 - b << "\n";
            std::ofstream bad(f.path("bad.cube"));
            bad << "LUT_3D_SIZE 3\n0 0 0\n";
        }
        Raster plain;
        assert(f.svc->renderTimelineFrame(tl, 0.5, 0, plain));
        auto inverted = [&](const Raster &r) {
            for (size_t i = 0; i < r.rgba.size(); ++i)
                if (i % 4 != 3 && std::abs((255 - plain.rgba[i]) - r.rgba[i]) > 1) return false;
            return true;
        };
        // the input LUT: refused unless it reads, applied before Cosmo, saved, cleared
        assert(!f.run("set a.lut=\"" + f.path("bad.cube") + "\"", &err) && has(err, "rows, the size says 27"));
        f.must("set a.lut=\"" + f.path("invert.cube") + "\"");
        Raster lutted;
        assert(f.svc->renderTimelineFrame(tl, 0.5, 0, lutted) && inverted(lutted));
        int ia = -1;
        for (int i = 0; i < (int)f.svc->model().rack.size(); ++i) if (f.svc->model().rack[(size_t)i].bindName == "a") ia = i;
        assert(has(f.svc->model().rack[(size_t)ia].lut, "invert.cube"));
        f.must("project save");
        f.svc = f.make();
        f.must("project open \"" + f.path("mv.isp") + "\"");
        assert(has(f.out("get a.lut"), "invert.cube"));
        f.must("set a.lut=none");
        assert(has(f.out("get a.lut"), "a.lut=none"));
        Raster again;
        assert(f.svc->renderTimelineFrame(tl, 0.5, 0, again) && again.rgba == plain.rgba);   // not a cached LUT frame

        // the LUT effect: in the stack, after Cosmo, its mix the amount
        const std::string fx = f.out("effect add a --type lut.cube");
        const std::string id = fx.substr(0, fx.find_first_of(" \n"));
        assert(id.rfind("ef_", 0) == 0);
        assert(has(f.out("get " + id + ".path"), "none"));
        Raster none;
        assert(f.svc->renderTimelineFrame(tl, 0.5, 0, none) && none.rgba == plain.rgba);       // no file: the picture as it came
        assert(!f.run("set " + id + ".path=\"" + f.path("bad.cube") + "\"", &err) && has(err, "rows"));
        f.must("set " + id + ".path=\"" + f.path("invert.cube") + "\"");
        Raster fxd;
        assert(f.svc->renderTimelineFrame(tl, 0.5, 0, fxd) && inverted(fxd));
        bool fileShown = false;
        for (const auto &e : f.svc->model().effects) fileShown = fileShown || (e.id == id && has(e.file, "invert.cube"));
        assert(fileShown);
        f.must("effect remove " + id);

        // the export: a grade baked to a .cube reproduces the grade on the frame
        f.must("set a.basic.exposure=0.6 a.basic.contrast=25 a.basic.vibrance=30 a.basic.texture=60");
        Raster graded;
        assert(f.svc->renderTimelineFrame(tl, 0.5, 0, graded));
        assert(!f.run("lut export a --out \"" + f.path("x.cube") + "\" --size 1", &err) && has(err, "2 … 129"));
        f.must("rack group new gr --nodes b");
        assert(!f.run("lut export gr --out \"" + f.path("x.cube") + "\"", &err) && has(err, "group"));
        f.must("lut export a --out \"" + f.path("a.cube") + "\"");
        render::Lut baked;
        assert(render::readCube(f.path("a.cube"), baked, err) && baked.size3d == 33);
        std::ifstream in(f.path("a.cube"));
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        assert(has(text, "# Left out (not per-pixel colour): texture") && has(text, "In: Rec.709 code values"));
        Raster viaLut = plain;
        baked.apply(viaLut);
        int worst = 0;
        for (size_t i = 0; i < graded.rgba.size(); ++i) if (i % 4 != 3) worst = std::max(worst, std::abs(viaLut.rgba[i] - graded.rgba[i]));
        std::printf("    the baked LUT on the plain frame vs the grade: worst %d code values\n", worst);
        assert(worst <= 3);
    });

    test("a render carries the mix: sources' sound and files, gain, fades, mute and solo, sample-accurate (R-AUD-5 amended, R-AUD-9)", [] {
        Fixture f("mix");
        f.standard();                                   // shotA 0–2 s, shotB 2–4 s
        std::string err;
        f.must("track add --kind audio");               // a0: the camera's sound under the picture
        f.must("audio clip add --track a0 --src a --in 0 --out 2 --at 0");
        f.must("audio clip add --track a0 --src b --in 0 --out 2 --at 2");
        f.must("audio track add --name music");
        f.must("audio clip add --track music --src \"" + f.path("footage/music.wav") + "\" --at 1 --gain -6.0206 --fade 0.5");
        assert(has(f.out("get music.gain"), "music.gain=0.0"));
        // no --out: the file's own length (the fake's 4 s) — so the timeline now runs to 5 s
        f.written.clear();
        f.must("render --timeline main --format h264 --res 46x26 --out \"" + f.path("m.mp4") + "\"");
        assert(gBegin.spec.audioRate == 48000 && has(f.svc->model().renders.back().spec, "AAC 48 kHz"));
        assert(f.written.size() == 120);                                  // 5 s at 24 fps
        assert(gAudio.size() == 120 * 2000 * 2);                          // exactly 2000 frames of sound per picture frame
        auto at = [&](double t) { return gAudio[(size_t)std::llround(t * 48000) * 2]; };
        auto near = [](float a, double b) { return std::fabs(a - b) < 1e-4; };
        assert(near(at(0.5), 0.1));                                       // a alone
        assert(near(at(1.25), 0.1 + 0.3 * 0.5 * 0.5));                   // + music at -6 dB, half way through its fade-in
        assert(near(at(1.75), 0.1 + 0.15));                               // fade done
        assert(near(at(2.5), 0.2 + 0.15));                                // the cut: b's sound under b's picture
        assert(near(at(4.75), 0.15 * 0.5));                               // music alone, half way through its fade-out
        // the cut is sample-accurate: the last frame of a, the first of b
        assert(near(gAudio[(size_t)(96000 - 1) * 2], 0.25) && near(gAudio[(size_t)96000 * 2], 0.35));
        // mute the dialogue lane; then solo the music
        f.must("set a0.mute=1");
        f.must("render --timeline main --format prores --res 46x26 --out \"" + f.path("m.mov") + "\"");
        assert(has(f.svc->model().renders.back().spec, "PCM 24-bit 48 kHz"));
        assert(near(at(0.5), 0.0) && near(at(2.5), 0.15));
        f.must("set a0.mute=0");
        f.must("set music.solo=1");
        assert(has(f.out("get music.solo"), "music.solo=1"));
        f.must("render --timeline main --format h264 --res 46x26 --out \"" + f.path("s.mp4") + "\"");
        assert(near(at(0.5), 0.0) && near(at(2.5), 0.15));               // the solo silences every other lane
        f.must("set music.solo=0 music.pan=1");
        f.must("render --timeline main --format h264 --res 46x26 --range 3:4 --out \"" + f.path("p.mp4") + "\"");
        assert(gAudio.size() == 24 * 2000 * 2);                           // a range: its own samples, from 3 s
        assert(near(gAudio[0], 0.2) && near(gAudio[1], 0.2 + 0.15));     // panned right: the left keeps only b (the clip's own -6 dB stays)
        // at 29.97 a frame owns 1601.6 samples: each frame's span comes from its own number, so the
        // total is exact and nothing drifts
        f.must("render --timeline main --format h264 --res 46x26 --fps 30000/1001 --range 0:1 --out \"" + f.path("n.mp4") + "\"");
        assert(f.svc->model().renders.back().total == 30 && gAudio.size() == (size_t)std::llround(30 * 48000.0 * 1001.0 / 30000.0) * 2);
        // a PNG sequence carries no sound; a silent timeline makes a silent render
        f.must("render --timeline main --format png-seq --range 0:0.5 --out \"" + f.path("frames") + "\"");
        assert(!has(f.svc->model().renders.back().spec, "AAC"));
        assert(!f.run("audio clip add --track music --src \"" + f.path("footage/still.png") + "\" --at 0", &err) && has(err, "no sound"));
    });

    test("playback is heard: the sound is the clock, a pause stops it, a scrub sounds a grain, the meters read it, waveforms are cached (R-AUD-6..8)", [] {
        Fixture f("sound");
        f.sound = true;
        f.svc = f.make();
        f.standard();
        f.must("track add --kind audio");
        f.must("audio clip add --track a0 --src a --in 0 --out 2 --at 0");
        f.must("audio clip add --track a0 --src b --in 0 --out 2 --at 2");
        f.must("project save");
        auto pumpFor = [&](double ms) {
            const auto t0 = std::chrono::steady_clock::now();
            while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() < ms)
            {
                f.svc->pump(f.now += 10.0);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        };
        {
            std::lock_guard<std::mutex> l(FakeAudioOut::mu);
            FakeAudioOut::heard.clear();
            FakeAudioOut::flushes = 0;
        }
        // play: the playhead is what the device has played, not the wall clock
        f.must("play");
        assert(f.svc->model().soundPlaying);
        f.now += 5000.0;   // the WALL clock jumps five seconds: a wall-clock playhead would leap to the end
        pumpFor(400);
        const double t = f.svc->model().playhead;
        size_t samples;
        { std::lock_guard<std::mutex> l(FakeAudioOut::mu); samples = FakeAudioOut::heard.size() / 2; }
        std::printf("    after ~0.4 s: playhead %.3f s, %zu frames handed to the device\n", t, samples);
        assert(t > 0.15 && t < 0.8);                                          // heard time, not the leap
        assert(std::fabs(t - ((double)samples / 48000 - 0.05)) < 0.1);         // written minus latency
        // the meters read the level being heard: a's 0.1
        assert(std::fabs(f.svc->model().meterPeakL - 0.1) < 1e-4 && std::fabs(f.svc->model().meterRmsR - 0.1) < 1e-4);
        assert(!f.svc->model().meterClip);
        // an edit while playing reaches the ear without a restart: a's lane doubled
        f.must("set a0.gain=6.0206");
        pumpFor(250);
        assert(f.svc->model().soundPlaying && std::fabs(f.svc->model().meterPeakL - 0.2) < 1e-3);
        // pause: the device is flushed (what it held is not heard) and the meters fall
        int flushed;
        { std::lock_guard<std::mutex> l(FakeAudioOut::mu); flushed = FakeAudioOut::flushes; }
        f.must("pause");
        pumpFor(60);
        assert(!f.svc->model().soundPlaying && f.svc->model().meterPeakL == 0.0);
        { std::lock_guard<std::mutex> l(FakeAudioOut::mu); assert(FakeAudioOut::flushes == flushed + 1); }
        // a scrub sounds an 80 ms grain of b (doubled) where it lands, eased at both ends
        size_t before;
        { std::lock_guard<std::mutex> l(FakeAudioOut::mu); before = FakeAudioOut::heard.size(); }
        f.must("playhead 2.5");
        pumpFor(200);
        {
            std::lock_guard<std::mutex> l(FakeAudioOut::mu);
            const size_t n = (FakeAudioOut::heard.size() - before) / 2;
            assert(n == 3840);
            const float mid = FakeAudioOut::heard[before + 1920 * 2], edge = FakeAudioOut::heard[before];
            assert(std::fabs(mid - 0.4f) < 1e-4 && edge < 0.01f);
        }
        // across the cut while playing from 1.9: b, under the same doubled lane
        f.must("playhead 1.9");
        pumpFor(60);
        f.must("play");
        pumpFor(450);
        assert(f.svc->model().playhead > 2.05);
        assert(std::fabs(f.svc->model().meterPeakL - 0.4) < 1e-3);             // b, doubled
        f.must("pause");
        // the waveform envelopes, computed once and kept beside the project
        pumpFor(300);
        std::vector<float> peaks;
        double per = 0;
        const std::string a = fs::absolute(f.path("footage/a.mp4")).lexically_normal().string();
        bool got = false;
        for (const auto &c : f.svc->model().clips) if (c.audio && c.media.find("a.mp4") != std::string::npos) got = f.svc->audioPeaks(c.media, peaks, per);
        assert(got && per == 100.0 && peaks.size() == 400 && std::fabs(peaks[10] - 0.1f) < 1e-5);
        assert(f.svc->model().peaksEpoch >= 2);
        assert(fs::exists(f.path("mv.peaks")) && std::distance(fs::directory_iterator(f.path("mv.peaks")), fs::directory_iterator{}) == 2);
        (void)a;
        // no sound server: playback is picture only, as before — never a stall
        FakeAudioOut::refuse = true;
        Fixture g("nosound");
        g.sound = true;
        g.svc = g.make();
        g.standard();
        g.must("track add --kind audio");
        g.must("audio clip add --track a0 --src a --in 0 --out 2 --at 0");
        g.must("play");
        for (int i = 0; i < 20; ++i) { g.svc->pump(g.now += 10.0); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        g.svc->pump(g.now += 500.0);
        assert(!g.svc->model().soundPlaying && g.svc->model().playhead > 0.4);   // the wall clock carried it
        FakeAudioOut::refuse = false;
    });

    test("a timeline goes out as EDL, FCPXML and OTIO and comes back the same; the media's timecode and reel travel (R-XCH-1..5)", [] {
        Fixture f("xch");
        f.standard();                                   // shotA (a 0–2 s) at 0, shotB (b 1–3 s) at 2
        std::string err;
        f.must("transition add --between shotA,shotB --dur 0.5");
        f.must("track add --kind video");
        f.must("clip add --track v1 --src a --in 3 --out 4 --at 1 --name over");
        f.must("set shotB.speed=2");
        f.must("track add --kind audio");
        f.must("audio clip add --track a0 --src \"" + f.path("footage/music.wav") + "\" --at 0 --out 3 --gain -6");
        // refusals: AAF named and routed, a format it cannot guess, an out of range track
        assert(!f.run("interchange export main --out \"" + f.path("cut.aaf") + "\"", &err) && has(err, "otioconvert"));
        assert(!f.run("interchange export main --out \"" + f.path("cut.txt") + "\"", &err) && has(err, "edl, fcpxml or otio"));
        assert(!f.run("interchange export main --out \"" + f.path("cut.edl") + "\" --track 3", &err) && has(err, "1 … 2"));
        f.must("interchange export main --out \"" + f.path("cut.edl") + "\"");
        assert(has(f.svc->output(), "one video track"));
        std::ifstream e(f.path("cut.edl"));
        const std::string edl((std::istreambuf_iterator<char>(e)), std::istreambuf_iterator<char>());
        // R-XCH-5: a's reel and its timecode (01:00:10:00) — the clip from 0 s of it starts at 01:00:10:00
        assert(has(edl, "001  A001     V     C        01:00:10:00 01:00:12:00 01:00:00:00 01:00:02:00"));
        assert(has(edl, "M2   b") && has(edl, "* TO CLIP NAME: b"));
        f.must("interchange export main --out \"" + f.path("cut.fcpxml") + "\"");
        f.must("interchange export main --out \"" + f.path("cut.otio") + "\" --start 10:00:00:00");
        std::ifstream o(f.path("cut.otio"));
        const std::string otio((std::istreambuf_iterator<char>(o)), std::istreambuf_iterator<char>());
        assert(has(otio, "\"value\": 864000.0"));   // 10:00:00:00 at 24
        // back in: three new root timelines, the same cut each time
        f.must("interchange import \"" + f.path("cut.edl") + "\" --name e");
        f.must("interchange import \"" + f.path("cut.fcpxml") + "\" --name x");
        f.must("interchange import \"" + f.path("cut.otio") + "\" --name o");
        assert(has(f.svc->output(), "imported 3 clips, 1 dissolves into timeline o"));
        const Project &P = f.svc->project();
        auto clipsOf = [&](const std::string &name) {
            std::vector<std::tuple<double, double, double, double>> v;
            ResolvedTimeline R;
            std::string e2;
            for (const auto &t : P.timelines)
                if (t.name == name && resolve(P, t.id, R, e2))
                    for (const auto &c : R.clips) v.emplace_back(c.at, c.in, c.out, c.speed);
            std::sort(v.begin(), v.end());
            return v;
        };
        const auto want = clipsOf("main");
        assert(want.size() == 3 && clipsOf("x") == want && clipsOf("o") == want);
        const auto fromEdl = clipsOf("e");                  // the EDL carries V1 only
        assert(fromEdl.size() == 2 && std::get<1>(fromEdl[0]) == 0.0 && std::get<3>(fromEdl[1]) == 2.0);   // timecode taken back off
        size_t aclips = 0;
        for (const auto &a : P.audioClips) aclips += a.gain == -6.0;
        assert(aclips == 3);                                // main's, the FCPXML's and the OTIO's
        assert(f.svc->model().currentTimeline != P.timelines[0].id);   // the import is opened
        // media found by name under --media when the file it names is gone
        {
            std::ofstream edl2(f.path("moved.edl"));
            edl2 << "TITLE: moved\nFCM: NON-DROP FRAME\n\n001  b        V     C        01:00:12:00 01:00:13:00 01:00:00:00 01:00:01:00\n* FROM CLIP NAME: b\n";   // b's 1 s (its timecode starts 01:00:11:00)
        }
        f.must("interchange import \"" + f.path("moved.edl") + "\" --name m1");
        assert(has(f.svc->output(), "not placed") && has(f.svc->output(), "--media"));
        f.must("interchange import \"" + f.path("moved.edl") + "\" --name m2 --media \"" + f.path("footage") + "\"");
        assert(has(f.svc->output(), "imported 1 clips"));
        assert(!f.run("interchange import \"" + f.path("nothing.edl") + "\"", &err) && has(err, "cannot read"));
    });

    test("J/K/L shuttles both ways at 1×/2×/4×; three points place a source by Insert or Overwrite (R-EDT-1, R-EDT-2)", [] {
        Fixture f("edit3");
        f.standard();                                   // shotA (a 0–2 s) at 0, shotB (b 0–2 s) at 2
        std::string err;
        const std::string tl = f.svc->model().timelines[0].id;
        auto clips = [&] {
            std::vector<std::tuple<double, double, double, std::string>> v;
            ResolvedTimeline R;
            std::string e2;
            resolve(f.svc->project(), tl, R, e2);
            for (const auto &c : R.clips) v.emplace_back(c.at, c.in, c.out, f.svc->project().rackObj(c.src)->name);
            std::sort(v.begin(), v.end());
            return v;
        };
        // the shuttle: L 1× → 2× → 4×, J reverses, K stops; the clock runs at the rate
        f.svc->pump(f.now);                                                // the service's clock is the test's from here
        f.must("shuttle forward");
        assert(f.svc->model().playing && f.svc->model().shuttle == 1.0);
        f.must("shuttle forward");
        assert(f.svc->model().shuttle == 2.0);
        const double p0 = f.svc->model().playhead;
        f.svc->pump(f.now += 600.0);                                       // the commands' own pumps took a few ms of it
        const double moved = f.svc->model().playhead - p0;
        std::printf("    2x for ~0.5-0.6 s: the playhead moved %.3f s\n", moved);
        assert(moved > 0.9 && moved < 1.25);                               // twice the time that passed
        f.must("shuttle forward");
        assert(f.svc->model().shuttle == 4.0);
        f.must("shuttle forward");
        assert(f.svc->model().shuttle == 4.0);                             // 4× is the top
        f.must("shuttle back");
        assert(f.svc->model().shuttle == -1.0);
        f.must("shuttle back");
        assert(f.svc->model().shuttle == -2.0);
        f.svc->pump(f.now += 10.0);
        f.svc->pump(f.now += 2000.0);                                      // runs back past the start: stops AT it
        assert(!f.svc->model().playing && f.svc->model().playhead == 0.0 && f.svc->model().shuttle == 0.0);
        f.must("shuttle forward");
        f.must("shuttle stop");
        assert(!f.svc->model().playing && f.svc->model().shuttle == 0.0);
        assert(!f.run("shuttle sideways", &err) && has(err, "forward, back or stop"));

        // three-point INSERT from the viewer: b from 0 to 1.5 at the playhead (1.0) — shotA splits, all after moves
        assert(!f.run("edit insert", &err) && has(err, "source view"));
        assert(!f.run("mark in --source", &err) && has(err, "no source in the viewer"));
        f.must("source view b");
        assert(f.svc->model().sourceView == "b" && f.svc->model().sourceDuration == 4.0);
        f.must("mark in --source --at 0");
        f.must("source playhead 1.5");
        f.must("mark out --source");
        assert(f.svc->model().sourceIn == 0.0 && f.svc->model().sourceOut == 1.5);
        f.must("playhead 1");
        f.must("edit insert");
        using C = std::tuple<double, double, double, std::string>;
        auto want1 = std::vector<C>{C{0.0, 0.0, 1.0, "a"}, C{1.0, 0.0, 1.5, "b"}, C{2.5, 1.0, 2.0, "a"}, C{3.5, 0.0, 2.0, "b"}};
        assert(clips() == want1);
        assert(f.svc->model().playhead == 2.5);                            // the playhead lands at its end
        f.must("undo");
        assert(clips().size() == 2);
        f.must("redo");
        assert(clips() == want1);
        // OVERWRITE, the fourth point from the timeline's marks: source In 2, timeline In 0.5 … Out 1.0
        f.must("mark clear --source");
        f.must("mark in --source --at 2");
        f.must("mark in --at 0.5");
        f.must("mark out --at 1");
        f.must("edit overwrite");
        auto v = clips();
        assert(v.size() == 5 && v[0] == (C{0.0, 0.0, 0.5, "a"}) && v[1] == (C{0.5, 2.0, 2.5, "b"}) && v[2] == (C{1.0, 0.0, 1.5, "b"}));
        assert(f.svc->model().markIn == -1.0 && f.svc->model().markOut == -1.0);   // the timeline marks are spent
        // backtimed: the source's In and Out, the timeline's Out alone — the clip ENDS there
        f.must("mark in --source --at 3");
        f.must("mark out --source --at 3.5");
        f.must("mark out --at 6");
        f.must("edit overwrite");
        v = clips();
        assert(std::get<0>(v.back()) == 5.5 && std::get<1>(v.back()) == 3.0);
        // the target track, and its refusals
        f.must("track add --kind audio");
        assert(!f.run("edit target a0", &err) && has(err, "not a video track"));
        f.must("track add --kind video");
        f.must("edit target v1");
        assert(f.svc->model().targetTrack == f.svc->project().idForRef("v1"));
        f.must("edit insert --src a --in 0 --out 1 --at 0");
        bool onV1 = false;
        ResolvedTimeline R;
        std::string e2;
        resolve(f.svc->project(), tl, R, e2);
        for (const auto &c : R.clips) onV1 = onV1 || c.track == f.svc->project().idForRef("v1");
        assert(onV1);
    });

    test("a speed ramp: the source frame is the integral of the speed, the clip lasts what the curve says (R-EDT-3)", [] {
        Fixture f("retime");                            // (not "ramp": the fake reads any path with "ramp" as its 16-bit ramp)
        f.standard();                                   // shotA: a 0–2 s at 0, speed 1 (the fake's frame f has R = f)
        const std::string tl = f.svc->model().timelines[0].id;
        auto frameAt = [&](double t) {
            Raster r;
            assert(f.svc->renderTimelineFrame(tl, t, 0, r));
            return (int)r.rgba[0];
        };
        assert(frameAt(1.0) == 24);
        // 1× at source 0, 3× at source 2, linear: v(s) = 1 + s → τ(s) = ln(1 + s), the clip lasts ln 3
        f.must("key add shotA.speed --at 0 --value 1");
        f.must("key add shotA.speed --at 2 --value 3");
        const Clip *c = f.svc->project().clip(f.svc->project().idForRef("shotA"));
        std::printf("    ramped shotA: stored average speed %.4f (2 / ln 3 = %.4f)\n", c->speed, 2.0 / std::log(3.0));
        assert(std::fabs(c->speed - 2.0 / std::log(3.0)) < 2e-3);              // so it lasts ln 3 ≈ 1.099 s
        // the frame at t is source e^t − 1 — 0.649 s at t = 0.5, 1.718 s at t = 1
        assert(std::abs(frameAt(0.5) - (int)std::floor((std::exp(0.5) - 1.0) * 24.0)) <= 1);
        assert(std::abs(frameAt(1.0) - (int)std::floor((std::exp(1.0) - 1.0) * 24.0)) <= 1);
        // continuous: frame after frame the source only advances, by more as the speed rises
        int prev = -1, firstStep = 0, lastStep = 0;
        for (int k = 0; k < 26; ++k)
        {
            const int fr = frameAt(k / 24.0);
            assert(fr >= prev);
            if (k == 1) firstStep = fr - prev;
            if (k == 25) lastStep = fr - prev;
            prev = fr;
        }
        assert(firstStep <= 2 && lastStep >= 2);
        // one undo step takes the key away — and the length comes back with it
        f.must("undo");
        c = f.svc->project().clip(f.svc->project().idForRef("shotA"));
        assert(std::fabs(c->speed - 1.0) < 1e-6);
        f.must("redo");
        c = f.svc->project().clip(f.svc->project().idForRef("shotA"));
        assert(std::fabs(c->speed - 2.0 / std::log(3.0)) < 2e-3);
    });

    test("a timeline placed as a clip: its picture, its sound, live edits, and never inside itself (R-EDT-4)", [] {
        Fixture f("nest");
        f.standard();                                   // main: shotA a 0–2 at 0, shotB b 0–2 at 2 (frame f has R = f)
        f.must("track add --kind audio");
        f.must("audio clip add --track a0 --src a --in 0 --out 2 --at 0");   // main's own sound: 0.1 for 2 s
        f.must("timeline new reel");
        f.must("timeline open reel");
        f.must("track add --kind video --name rv0");
        f.must("clip add --track rv0 --src b --in 0 --out 2 --at 0 --name inner");
        f.must("audio track add --name music");
        f.must("audio clip add --track music --src \"" + f.path("footage/music.wav") + "\" --at 0");   // 0.3 for 4 s
        f.must("timeline open main");
        f.must("track add --kind video --name v1");
        f.must("clip add --track v1 --src reel --in 0.5 --out 3 --at 1 --name nest");   // reel 0.5–3 at main 1–3.5
        const NodeId main = f.svc->project().idForRef("main"), reel = f.svc->project().idForRef("reel");
        const ClipModel *n = nullptr;
        for (const auto &c : f.svc->model().clips) if (c.name == "nest") n = &c;
        assert(n && n->nested && n->srcName == "reel" && !n->offline && n->src == reel);
        auto placeable = [&](const std::string &name) {
            for (const auto &t : f.svc->model().timelines) if (t.name == name) return t.placeable;
            assert(false);
            return false;
        };
        assert(placeable("reel") && !placeable("main"));   // what the lane menu offers here: never itself
        auto frame = [&](const NodeId &tl, double t) {
            Raster r;
            assert(f.svc->renderTimelineFrame(tl, t, 0, r));
            return r;
        };
        auto red = [&](double t) { return (int)frame(main, t).rgba[0]; };
        assert(red(0.5) == 12);                         // before the nest: shotA
        assert(red(1.25) == 18);                        // reel at 0.75: inner, b's frame 18
        assert(red(2.25) == 42);                        // reel at 1.75, over shotB
        assert(red(3.0) == 24);                         // reel at 2.5 has no picture: the nest is clear, shotB shows
        const Raster outer = frame(main, 1.25), inner = frame(reel, 0.75);
        assert(outer.width == inner.width && outer.rgba == inner.rgba);    // the nested picture IS the reel's frame
        f.must("colour working acescct");               // … in any working space: the view applies once, outside
        assert(frame(main, 1.25).rgba == frame(reel, 0.75).rgba && frame(main, 1.25).rgba != outer.rgba);
        f.must("colour working rec709");
        // the clip's own speed plays the nested timeline faster, like footage
        f.must("set nest.speed=2.0");
        assert(red(1.25) == 24);                        // reel at 0.5 + 0.25 × 2 = 1.0
        f.must("undo");
        // no --out: the rest of the timeline — to where its last clip (or sound) ends
        f.must("clip add --track v1 --src reel --in 1 --at 6 --name nest2");
        for (const auto &c : f.svc->model().clips) if (c.name == "nest2") assert(std::fabs(c.out - 4.0) < 1e-9);
        f.must("clip delete nest2");
        // its sound joins the mix, through the clip's window: reel's music under main's a
        f.written.clear();
        f.must("render --timeline main --format h264 --res 46x26 --out \"" + f.path("n.mp4") + "\"");
        auto at = [&](double t) { return gAudio[(size_t)std::llround(t * 48000) * 2]; };
        auto near = [](float a, double b) { return std::fabs(a - b) < 1e-4; };
        std::printf("    the mix: %.3f at 0.5, %.3f at 1.5, %.3f at 3.0, %.3f at 3.75\n", at(0.5), at(1.5), at(3.0), at(3.75));
        assert(near(at(0.5), 0.1) && near(at(1.5), 0.4) && near(at(3.0), 0.3) && near(at(3.75), 0.0));
        f.must("set v1.mute=1");                        // the nest's track muted: its picture and its sound
        f.must("render --timeline main --format h264 --res 46x26 --out \"" + f.path("m.mp4") + "\"");
        assert(near(at(1.5), 0.1) && red(1.25) == 30);  // a alone; shotA's own frame shows
        f.must("set v1.mute=0");
        // the loops are refused, and a placed timeline cannot be deleted from under its clip
        std::string err;
        f.must("timeline open reel");
        assert(!f.run("clip add --track rv0 --src main --in 0 --out 1 --at 3", &err) && has(err, "inside itself"));
        assert(!placeable("main") && !placeable("reel"));  // from inside reel: main holds reel, so neither
        assert(!f.run("timeline delete reel", &err) && has(err, "main places reel as clip nest"));
        // an edit inside the nested timeline reaches the outer one — and the preview cache rebuilds
        // exactly the seconds it changed
        f.must("timeline open main");
        f.must("cache build");
        f.must("wait cache.done");
        f.must("timeline open reel");
        f.must("set inner.in=1.0 inner.out=3.0");       // inner: b 1–3 — the same reel seconds, other frames
        f.must("timeline open main");
        assert(red(1.25) == 42 && red(2.25) == 66);     // reel 0.75 → b 1.75; reel 1.75 → b 2.75
        gCacheSegmentsWritten = 0;
        f.must("cache build");
        f.must("wait cache.done");
        std::printf("    after an edit inside the nest: %d of 4 cache segments rebuilt\n", gCacheSegmentsWritten);
        assert(gCacheSegmentsWritten == 2);             // main 1–3 is what changed
        // a loop only a hand-edited file can make: named by lint, and the render does not recurse
        f.must("project save");
        const NodeId rv0 = f.svc->project().idForRef("rv0");
        std::ofstream(f.path("mv.isp"), std::ios::app) << "#clip id=clp_99 name=back track=" << rv0 << " order=9 src=" << main
                                                       << " at=2.5 in=0.0 out=1.0\n";
        f.svc = f.make();
        f.must("project open \"" + f.path("mv.isp") + "\"");
        assert(has(f.out("lint"), "timeline reel contains itself"));
        assert(red(1.25) == 42);                        // reel at 0.75: inner, unchanged
        assert(red(3.25) == 30);                        // reel at 2.75 is `back` → main, already being drawn: clear
        f.must("render --timeline main --format h264 --res 46x26 --out \"" + f.path("l.mp4") + "\"");
    });

    test("a multicam: sources lined up by timecode or in-points, one angle shown, each switch a cut (R-EDT-5)", [] {
        Fixture f("multicam");
        f.standard();                                   // a: timecode 01:00:10:00, b: 01:00:11:00; still: none
        std::string err;
        assert(!f.run("multicam new cams --sources a", &err) && has(err, "two or more"));
        assert(!f.run("multicam new cams --sources a,still", &err) && has(err, "still carries no timecode"));
        assert(!f.run("multicam new cams --sources a,b --audio still", &err) && has(err, "one of the --sources"));
        f.must("track add --kind video --name v1");
        f.must("multicam new cams --sources a,b --track v1 --at 0");
        // the multicam: a on angle 1 at 0, b on angle 2 at 1 s (its timecode is one second later); a's sound
        const Project &P = f.svc->project();
        const NodeId cams = P.idForRef("cams");
        ResolvedTimeline C;
        assert(resolve(P, cams, C, err) && C.clips.size() == 2 && C.audioClips.size() == 1);
        const auto angles = std::vector<NodeId>{C.tracks[0].id, C.tracks[1].id};
        for (const auto &c : C.clips)
            assert(c.track == angles[c.src == P.idForRef("a") ? 0 : 1] && std::fabs(c.at - (c.src == P.idForRef("a") ? 0.0 : 1.0)) < 1e-9);
        assert(std::fabs(C.audioClips[0].at) < 1e-9 && has(C.audioClips[0].src, "a.mp4"));
        auto onV1 = [&] {
            std::vector<ClipModel> v;
            for (const auto &c : f.svc->model().clips) if (c.track == P.idForRef("v1")) v.push_back(c);
            std::sort(v.begin(), v.end(), [](const ClipModel &x, const ClipModel &y) { return x.at < y.at; });
            return v;
        };
        assert(onV1().size() == 1 && onV1()[0].nested && onV1()[0].angle == 1 && std::fabs(onV1()[0].duration - 5.0) < 1e-9);
        f.must("playhead 0.5");
        const AppModel &m = f.svc->model();
        assert(m.multicamClip == onV1()[0].id && m.multicamAngle == 1 && m.multicamAngles == std::vector<std::string>({"a", "b"}));
        auto px = [&](double t) {
            Raster r;
            assert(f.svc->renderTimelineFrame(P.idForRef("main"), t, 0, r));
            return std::make_pair((int)r.rgba[0], (int)r.rgba[1]);   // (source frame, the file's green: a 60, b 200)
        };
        assert(px(1.5) == std::make_pair(36, 60));      // angle 1: a's frame 36
        // switch to angle 2 at 2 s: a cut — before it a, after it b at its own synced frame
        f.must("playhead 2.0");
        f.must("multicam angle 2");
        assert(onV1().size() == 2 && onV1()[0].angle == 1 && onV1()[1].angle == 2 && std::fabs(onV1()[1].at - 2.0) < 1e-9);
        assert(px(1.5) == std::make_pair(36, 60));
        assert(px(2.5) == std::make_pair(36, 200));     // cams 2.5 s: b began at 1 s → b's 1.5 s
        assert(m.multicamAngle == 2);
        // on a clip's first frame the angle changes without a cut
        f.must("multicam angle 1");
        assert(onV1().size() == 2 && onV1()[1].angle == 1);
        f.must("undo");                                 // each switch one step
        assert(onV1().size() == 2 && onV1()[1].angle == 2);
        f.must("undo");
        assert(onV1().size() == 1 && onV1()[0].angle == 1);
        f.must("redo");
        // the sound is the multicam's (a's, 0.1) across the cut, never doubled
        f.must("render --timeline main --format h264 --res 46x26 --out \"" + f.path("mc.mp4") + "\"");
        auto at = [&](double t) { return gAudio[(size_t)std::llround(t * 48000) * 2]; };
        std::printf("    multicam sound: %.3f at 1.5, %.3f at 2.5, %.3f at 4.5\n", at(1.5), at(2.5), at(4.5));
        assert(std::fabs(at(1.5) - 0.1) < 1e-4 && std::fabs(at(2.5) - 0.1) < 1e-4 && std::fabs(at(4.5)) < 1e-4);
        // refusals: past the last angle, an angle on footage, nothing under the playhead
        assert(!f.run("multicam angle 3", &err) && has(err, "has 2 angle(s)"));
        assert(!f.run("set shotA.angle=1", &err) && has(err, "places footage"));
        f.must("playhead 6.0");
        assert(!f.run("multicam angle 1", &err) && has(err, "no multicam clip under the playhead") && f.svc->model().multicamClip.empty());
        // by in-points: a's 1.0 s and b's 0.5 s meet — a at 0, b at 0.5; b's sound with --audio
        f.must("multicam new cams2 --sources a,b --in a=1.0,b=0.5 --audio b");
        ResolvedTimeline C2;
        assert(resolve(P, P.idForRef("cams2"), C2, err));
        for (const auto &c : C2.clips) assert(std::fabs(c.at - (c.src == P.idForRef("a") ? 0.0 : 0.5)) < 1e-9);
        assert(C2.audioClips.size() == 1 && has(C2.audioClips[0].src, "b.mp4") && std::fabs(C2.audioClips[0].at - 0.5) < 1e-9);
        assert(!f.run("multicam new cams3 --sources a,b --sync timecode --in a=1", &err) && has(err, "--sync in"));
    });

    test("camera RAW: a CinemaDNG folder is one source by its pattern; a vendor RAW is refused naming its SDK (R-MEDIA-1)", [] {
        Fixture f("raw");
        f.standard();
        std::string err;
        fs::create_directories(f.path("footage/C001"));
        for (int k = 10; k < 14; ++k) std::ofstream(f.path("footage/C001/C001_0000" + std::to_string(k) + ".dng")) << "x";
        std::ofstream(f.path("footage/C001/notes.txt")) << "x";
        f.must("rack add \"" + f.path("footage/C001") + "\"");
        const RackNodeModel *r = nullptr;
        for (const auto &n : f.svc->model().rack) if (n.bindName == "c001") r = &n;
        assert(r && has(r->media, "C001_%06d.dng") && r->video && !r->failed);
        f.must("track add --kind video --name v1");
        f.must("clip add --track v1 --src c001 --in 0 --at 6");   // no --out: the sequence's own length
        // refusals: an empty folder, a pattern with no frames, the vendor formats
        fs::create_directories(f.path("footage/empty"));
        assert(!f.run("rack add \"" + f.path("footage/empty") + "\"", &err) && has(err, "holds no numbered .dng"));
        assert(!f.run("rack add \"" + f.path("footage/C009_%06d.dng") + "\"", &err) && has(err, "no frame of"));
        std::ofstream(f.path("footage/clip.R3D")) << "x";
        assert(!f.run("rack add \"" + f.path("footage/clip.R3D") + "\"", &err) && has(err, "REDCODE RAW") && has(err, "RED R3D SDK"));
        std::ofstream(f.path("footage/clip.ari")) << "x";
        assert(!f.run("rack add \"" + f.path("footage/clip.ari") + "\"", &err) && has(err, "ARRI Image SDK"));
        // a project that already names one (made where the SDK is): offline, and saying why
        f.must("project save");
        std::ofstream(f.path("mv.isp"), std::ios::app) << "#rackobj id=ro_90 name=redclip kind=source weight=1.0 media=footage/clip.R3D frame=0.0\n";
        f.svc = f.make();
        f.must("project open \"" + f.path("mv.isp") + "\"");
        const RackNodeModel *red = nullptr;
        for (const auto &n : f.svc->model().rack) if (n.bindName == "redclip") red = &n;
        assert(red && red->failed && red->offlineWhy == "REDCODE RAW needs the RED R3D SDK — not in this build");
        assert(has(f.out("lint"), "(REDCODE RAW needs the RED R3D SDK — not in this build)"));
        for (const auto &n : f.svc->model().rack) if (n.bindName == "c001") assert(!n.failed && n.offlineWhy.empty());
    });

    test("proxies: made in the background, the monitor's while the project says so, never a render's (R-MEDIA-2)", [] {
        Fixture f("proxy");
        f.standard();                                   // shotA a 0–2, shotB b 2–4; a still in the rack
        std::string err;
        assert(!f.run("proxy make still", &err) && has(err, "needs no proxy"));
        assert(!f.run("proxy make a --edge 4", &err) && has(err, "16 … 8192"));
        f.must("set shotA.opacity=0.5");                // an edit made BEFORE the proxies exist …
        f.must("proxy make --edge 24 --codec prores");  // every video source: a and b (the still needs none)
        const AppModel &m = f.svc->model();
        assert(m.proxyJobs.size() == 2 && m.proxyJobs[0].state == "done" && m.proxyJobs[1].state == "done" && m.proxyJobs[0].total == 96);
        auto rackNode = [&](const std::string &bind) -> const RackNodeModel & {
            for (const auto &n : f.svc->model().rack) if (n.bindName == bind) return n;
            assert(false);
            return f.svc->model().rack.front();
        };
        const std::string pa = rackNode("a").proxy, pb = rackNode("b").proxy;
        assert(has(pa, "mv.proxies/a_24.mov") && has(pb, "mv.proxies/b_24.mov") && rackNode("still").proxy.empty());
        f.must("undo");                                 // … undone after: the proxies stay
        assert(!rackNode("a").proxy.empty() && has(f.out("get shotA.opacity"), "shotA.opacity=1.0"));
        {
            std::lock_guard<std::mutex> l(gMemMu);
            auto &frames = gMemFiles[pa];
            std::printf("    proxy of a: %zu frames at %dx%d (%s %s, %d-bit)\n", frames.size(), frames[0].width, frames[0].height,
                        gProxySpec.codec.c_str(), gProxySpec.profile.c_str(), gProxySpec.bitDepth);
            assert(frames.size() == 96 && frames[0].width == 24 && frames[0].height == 14);   // 48×27 → 24×13.5 → 24×14, even
            assert(gProxySpec.codec == "prores" && gProxySpec.profile == "proxy" && gProxySpec.bitDepth == 10);
            assert(frames[24].rgba[0] == 24 && frames[24].rgba[2] == 100);                    // the original's frame 24, its values
            for (const auto *fr : {&gMemFiles[pa], &gMemFiles[pb]})   // mark the proxies, so a frame says which file it came from
                for (auto &x : const_cast<std::vector<Raster> &>(*fr))
                    for (size_t i = 2; i < x.rgba.size(); i += 4) x.rgba[i] = 7;
        }
        f.must("project save");
        {
            std::ifstream in(f.path("mv.isp"));
            const std::string isp((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            assert(has(isp, "proxy=mv.proxies/a_24.mov") && has(isp, "proxyScale=0.5") && !has(isp, "proxies ="));
        }
        auto monitor = [&](double t) {
            Raster r;
            assert(f.svc->renderFrame(t, 0, r));
            return std::make_pair((int)r.rgba[((size_t)13 * r.width + 24) * 4], (int)r.rgba[((size_t)13 * r.width + 24) * 4 + 2]);
        };
        assert(monitor(1.0) == std::make_pair(24, 100));   // the switch is off: the original
        f.must("proxy use on");
        assert(f.svc->model().useProxies && monitor(1.0) == std::make_pair(24, 7) && monitor(3.0).second == 7);   // the same frame, from the proxy
        // a deliverable is never a proxy: export-still and a render decode the originals
        f.must("export-still --timeline main --out \"" + f.path("s.png") + "\" --at 1");
        const Raster &still = f.images[f.path("s.png")];
        assert(still.rgba[((size_t)13 * still.width + 24) * 4 + 2] == 100);
        f.written.clear();
        f.must("render --timeline main --format h264 --res 46x26 --out \"" + f.path("r.mp4") + "\"");
        assert(f.written.size() == 96 && f.written[24].rgba[((size_t)13 * 46 + 23) * 4 + 2] == 100);
        assert(monitor(1.0).second == 7);               // … and the monitor is back on the proxy after it
        // forgetting one proxy: that source's original, the other's proxy
        f.must("proxy remove a");
        assert(rackNode("a").proxy.empty() && monitor(1.0).second == 100 && monitor(3.0).second == 7);
        assert(!f.run("proxy remove a", &err) && has(err, "has no proxy"));
        f.must("proxy use off");
        assert(monitor(3.0).second == 100);
        // a plugin is sized in ORIGINAL pixels: a blur through a half-size proxy is as wide as on the original
        f.must("rack add \"" + f.path("footage/edge.mp4") + "\"");
        f.must("track add --kind video --name v1");
        f.must("clip add --track v1 --src edge --in 0 --out 2 --at 6");
        f.must("effect add edge --type blur.gaussian");
        f.must("set ef_1.radius=4");
        f.must("proxy make edge --edge 24");
        auto softWidth = [&] {
            Raster r;
            assert(f.svc->renderFrame(7.0, 0, r));
            int n = 0;
            for (int x = 0; x < r.width; ++x) { const int v = r.rgba[((size_t)13 * r.width + x) * 4]; n += v > 20 && v < 235; }
            return n;
        };
        const int onOriginal = softWidth();
        f.must("proxy use on");
        const int onProxy = softWidth();
        std::printf("    a 4 px blur across the edge: %d px soft on the original, %d px through the half-size proxy\n", onOriginal, onProxy);
        assert(onOriginal >= 4 && std::abs(onProxy - onOriginal) <= 2);
        f.must("proxy use off");
        f.must("project save");
        {
            std::ifstream in(f.path("mv.isp"));
            const std::string isp((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            assert(!has(isp, "a_24.mov") && has(isp, "proxy=mv.proxies/b_24.mov"));
        }
    });

    test("relink: offline media listed, found by name in a folder or pointed at one by one; grades and unsaved edits kept (R-MEDIA-3)", [] {
        Fixture f("relink");
        f.standard();                                   // shotA a 0–2, shotB b 2–4
        f.must("set a.basic.exposure=0.3");
        f.must("project save");
        Raster graded;
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, graded));
        const int before = graded.rgba[0];                // shotA's frame 24, graded
        fs::create_directories(f.path("footage/moved/deeper"));
        fs::rename(f.path("footage/a.mp4"), f.path("footage/moved/deeper/a.mp4"));
        f.svc = f.make();
        f.must("project open \"" + f.path("mv.isp") + "\"");
        auto failed = [&](const std::string &bind) {
            for (const auto &n : f.svc->model().rack) if (n.bindName == bind) return n.failed;
            assert(false);
            return false;
        };
        auto red = [&](double t) {
            Raster r;
            bool any = false;
            f.svc->renderTimelineFrame("tl_1", t, 0, r, &any);
            return any ? (int)r.rgba[0] : -1;
        };
        assert(failed("a") && !failed("b") && red(1.0) == -1);
        assert(has(f.out("media offline"), "a  ") && has(f.svc->output(), "(missing)") && !has(f.svc->output(), "b  "));
        // an edit only in memory: Cosmo cannot save while a is offline (D-2)
        f.must("set b.basic.exposure=0.4");
        std::string err;
        assert(!f.run("project save", &err) && has(err, "NOT the rack"));
        assert(!f.run("media relink --search \"" + f.path("nowhere") + "\"", &err) && has(err, "no folder"));
        assert(!f.run("media relink a \"" + f.path("footage/still.png") + "\"", &err) && has(err, "a video"));
        f.must("media relink --search \"" + f.path("footage") + "\"");
        assert(has(f.svc->output(), "a → ") && has(f.svc->output(), "moved/deeper/a.mp4"));
        assert(!failed("a") && red(1.0) == before);                                       // shotA plays again, graded as it was
        assert(evalValue(f, "get a.basic.exposure") == 0.3 && evalValue(f, "get b.basic.exposure") == 0.4);   // its grade, and the unsaved edit
        assert(has(f.out("media offline"), "no source is offline"));
        // the move is the .isp's; now the rack saves too — and a new session finds a where it is
        f.must("project save");
        {
            std::ifstream in(f.path("mv.isp"));
            const std::string isp((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            assert(has(isp, "media=footage/moved/deeper/a.mp4"));
        }
        f.svc = f.make();
        f.must("project open \"" + f.path("mv.isp") + "\"");
        assert(!failed("a") && red(1.0) == before && evalValue(f, "get a.basic.exposure") == 0.3 && evalValue(f, "get b.basic.exposure") == 0.4);
        // one by one; and refused while the rack's structure differs from its saved one
        const int beforeB = red(3.0);                    // shotB's frame 24, graded 0.4
        fs::rename(f.path("footage/b.mp4"), f.path("footage/moved/b.mp4"));
        f.svc = f.make();
        f.must("project open \"" + f.path("mv.isp") + "\"");
        assert(failed("b"));
        f.must("rack group new grp --nodes still");
        assert(!f.run("media relink b \"" + f.path("footage/moved/b.mp4") + "\"", &err) && has(err, "groups or names changed"));
        f.must("rack ungroup grp");                     // the structure as saved again
        f.must("set a.basic.exposure=0.5");             // undoable, before the relink …
        f.must("media relink b \"" + f.path("footage/moved/b.mp4") + "\"");
        assert(!failed("b") && red(3.0) == beforeB && beforeB > 24);
        f.must("undo");                                 // … undone after it: the relink stays
        assert(!failed("b") && evalValue(f, "get a.basic.exposure") == 0.3);
        {
            f.must("project save");
            std::ifstream in(f.path("mv.isp"));
            const std::string isp((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            assert(has(isp, "media=footage/moved/b.mp4"));
        }
        // a CinemaDNG clip is found by its folder
        fs::create_directories(f.path("footage/D001"));
        for (int k = 0; k < 3; ++k) std::ofstream(f.path("footage/D001/D001_00000" + std::to_string(k) + ".dng")) << "x";
        f.must("rack add \"" + f.path("footage/D001") + "\"");
        f.must("project save");
        fs::rename(f.path("footage/D001"), f.path("footage/moved/D001"));
        f.svc = f.make();
        f.must("project open \"" + f.path("mv.isp") + "\"");
        assert(failed("d001"));
        f.must("media relink --search \"" + f.path("footage") + "\"");
        assert(!failed("d001") && has(f.svc->output(), "moved/D001/D001_%06d.dng"));
    });

    test("qualifiers and windows limit a node's grade to what they select; the matte view shows the key (R-CLR-1, R-CLR-2)", [] {
        Fixture f("matte");
        f.standard();
        f.must("rack add \"" + f.path("footage/edge.mp4") + "\"");   // left half white, right half black
        f.must("track add --kind video --name v1");
        f.must("clip add --track v1 --src edge --in 0 --out 2 --at 6");
        auto px = [&](double t, int x, int y = 13) {
            Raster r;
            assert(f.svc->renderTimelineFrame("tl_1", t, 0, r) && r.width == 48);
            return (int)r.rgba[((size_t)y * 48 + x) * 4];
        };
        assert(px(6.5, 10) == 255 && px(6.5, 40) == 0);
        f.must("set edge.basic.exposure=-1");
        const int dark = px(6.5, 10);
        assert(dark < 230);                                                  // the grade reaches the white
        // a luma qualifier: the bright half only, then everything but it
        f.must("effect add edge --type qualifier.hsl");
        assert(px(6.5, 10) == dark);                                         // the defaults select everything
        f.must("set ef_1.lumLow=0.5");
        assert(px(6.5, 10) == dark);
        f.must("set ef_1.lumLow=0 ef_1.lumHigh=0.4");                         // the dark half only: white is left alone
        assert(px(6.5, 10) == 255);
        f.must("set ef_1.mix=0");                                            // a matte at mix 0 limits nothing
        assert(px(6.5, 10) == dark);
        f.must("set ef_1.mix=0.5");                                          // … at 0.5, half its limit
        assert(px(6.5, 10) > dark + 10 && px(6.5, 10) < 245);
        f.must("set ef_1.mix=1 ef_1.invert=1");
        assert(px(6.5, 10) == dark);
        f.must("effect remove ef_1");
        // a hue qualifier on a's frame (12, 60, 100) — hue ~206°: its hue is graded, another hue is not
        f.must("set a.basic.exposure=1");
        auto aPx = [&] { Raster r; assert(f.svc->renderTimelineFrame("tl_1", 0.5, 0, r)); return (int)r.rgba[((size_t)13 * 48 + 20) * 4 + 2]; };
        const int bright = aPx();
        assert(bright > 100);
        const std::string q = f.out("effect add a --type qualifier.hsl");
        const std::string qa = q.substr(0, q.size() - 1);
        f.must("set " + qa + ".hue=206 " + qa + ".hueWidth=15");
        assert(aPx() == bright);
        f.must("set " + qa + ".hue=100");
        assert(aPx() == 100);                                                // another hue: a's blue as it was
        f.must("effect remove " + qa);
        f.must("set a.basic.exposure=0");
        // a window: the left half, hard; then off to the right; then a feathered circle
        const std::string w = f.out("effect add edge --type window.shape");
        const std::string wi = w.substr(0, w.size() - 1);
        f.must("set " + wi + ".shape=1 " + wi + ".centerX=0.25 " + wi + ".width=0.5 " + wi + ".height=2 " + wi + ".feather=0");
        assert(px(6.5, 10) == dark);
        f.must("set " + wi + ".centerX=0.75");
        assert(px(6.5, 10) == 255);
        f.must("set " + wi + ".shape=0 " + wi + ".centerX=0.25 " + wi + ".centerY=0.5 " + wi + ".width=0.2 " + wi + ".height=0.6 " + wi + ".feather=0.15");
        std::vector<int> row;
        for (int x = 12; x < 24; ++x) row.push_back(px(6.5, x));
        bool between = false;
        for (size_t k = 1; k < row.size(); ++k) { assert(row[k] >= row[k - 1]); between = between || (row[k] > dark + 5 && row[k] < 250); }
        std::printf("    a feathered window across the white: %d … %d … %d\n", row.front(), row[row.size() / 2], row.back());
        assert(row.front() == dark && row.back() > 245 && between);
        // the matte view: Grade's monitor shows the key
        f.must("rack select edge");
        f.must("view matte on");
        assert(f.svc->model().matteView);
        {
            Raster r;
            assert(f.svc->renderSourceFrame("edge", 0.5, 0, r));
            const int centre = r.rgba[((size_t)13 * r.width + 12) * 4], outside = r.rgba[((size_t)13 * r.width + 40) * 4];
            std::printf("    matte view: %d at the window's centre, %d outside it (%dx%d)\n", centre, outside, r.width, r.height);
            assert(centre == 255 && outside == 0 && r.rgba[((size_t)13 * r.width + 12) * 4 + 1] == 255);
        }
        f.must("view matte off");
        // an animated window follows its keys (source time 0 → 1 s: the left of centre, then the right)
        f.must("set " + wi + ".shape=1 " + wi + ".width=0.5 " + wi + ".height=2 " + wi + ".feather=0");
        f.must("key add " + wi + ".centerX --at 0 --value 0.25");
        f.must("key add " + wi + ".centerX --at 1 --value 0.75");
        assert(px(6.0, 10) == dark && px(7.0, 10) == 255);
        f.must("key clear " + wi + ".centerX");
        f.must("effect remove " + wi);
        // a group's window limits the GROUP's contribution, not the member's own grade
        f.must("set edge.basic.exposure=0");
        f.must("rack group new grp --nodes edge");
        f.must("set grp.basic.exposure=-1");
        assert(px(6.5, 10) == dark);
        const std::string g = f.out("effect add grp --type window.shape");
        const std::string gi = g.substr(0, g.size() - 1);
        f.must("set " + gi + ".shape=1 " + gi + ".centerX=0.75 " + gi + ".width=0.5 " + gi + ".height=2 " + gi + ".feather=0");
        assert(px(6.5, 10) == 255);                                          // the group's grade only on the right
        f.must("set edge.basic.exposure=-1");
        assert(px(6.5, 10) == dark);                                         // the member's own grade is not the group's to limit
    });

    test("a tracked window follows what is under it, forward and back, keyed every frame, one undo step (R-CLR-2)", [] {
        Fixture f("track");
        f.standard();
        f.must("rack add \"" + f.path("footage/move.mp4") + "\"");
        f.must("track add --kind video --name v1");
        f.must("clip add --track v1 --src move --in 0 --out 3 --at 10");
        const std::string w = f.out("effect add move --type window.shape");
        const std::string wi = w.substr(0, w.size() - 1);
        std::string err;
        assert(!f.run("track window ef_99", &err) && has(err, "not a window"));
        // on the square at frame 0: its centre (7, 9) of 48×27
        f.must("set " + wi + ".centerX=" + std::to_string(7.5 / 48) + " " + wi + ".centerY=" + std::to_string(9.5 / 27) + " " +
               wi + ".width=" + std::to_string(11.0 / 48) + " " + wi + ".height=" + std::to_string(11.0 / 27));
        f.must("playhead 10");
        f.must("track window " + wi + " --to 1");
        const AppModel &m = f.svc->model();
        assert(m.trackJobs.size() == 1 && m.trackJobs[0].state == "done" && m.trackJobs[0].done == 24 && m.trackJobs[0].total == 24);
        auto keyAt = [&](const std::string &k, double t) {
            const Project &P = f.svc->project();
            const Anim *a = P.animOf(P.idForRef(wi), k);
            if (!a) return -1.0;
            for (const auto &key : P.keysOf(a->id)) if (std::fabs(key.t - t) < 1e-3) return key.v;
            return -1.0;
        };
        const Project &P = f.svc->project();
        const Anim *ax = P.animOf(P.idForRef(wi), "centerX");
        std::printf("    forward: %zu keys; at frame 20 the window is at (%.4f, %.4f), the square's centre (%.4f, %.4f)\n",
                    ax ? P.keysOf(ax->id).size() : 0, keyAt("centerX", 20.0 / 24), keyAt("centerY", 20.0 / 24), 27.5 / 48, 19.5 / 27);
        assert(ax && P.keysOf(ax->id).size() == 25);                                 // the start and every frame after it
        for (int fr : {0, 7, 20, 24})
        {
            assert(std::fabs(keyAt("centerX", fr / 24.0) - (4 + fr + 3.5) / 48) < 1.01 / 48);
            assert(std::fabs(keyAt("centerY", fr / 24.0) - (6 + fr / 2 + 3.5) / 27) < 1.01 / 27);
        }
        f.must("undo");                                                              // the whole track, one step
        assert(!f.svc->project().animOf(P.idForRef(wi), "centerX") || f.svc->project().keysOf(f.svc->project().animOf(P.idForRef(wi), "centerX")->id).empty());
        f.must("redo");
        assert(std::fabs(keyAt("centerX", 1.0) - 31.5 / 48) < 1.01 / 48);
        // backward, from source 1 s to 0.5 s
        f.must("key clear " + wi + ".centerX");
        f.must("key clear " + wi + ".centerY");
        f.must("set " + wi + ".centerX=" + std::to_string(31.5 / 48) + " " + wi + ".centerY=" + std::to_string(21.5 / 27));
        f.must("playhead 11");
        f.must("track window " + wi + " --back --to 0.5");
        assert(f.svc->model().trackJobs.back().state == "done" && f.svc->model().trackJobs.back().done == 12);
        assert(std::fabs(keyAt("centerX", 0.5) - 19.5 / 48) < 1.01 / 48 && std::fabs(keyAt("centerY", 0.5) - 15.5 / 27) < 1.01 / 27);
        f.must("track window " + wi + " --to 0.2");                                 // forward, to before the start: the job says why it stopped
        assert(f.svc->model().trackJobs.back().state == "failed" && has(f.svc->model().trackJobs.back().error, "nothing to track"));
        // a window on a group has no one source to follow
        f.must("rack group new grp --nodes move");
        const std::string g = f.out("effect add grp --type window.shape");
        assert(!f.run("track window " + g.substr(0, g.size() - 1), &err) && has(err, "on a group"));
    });

    test("stills: grabbed with their grade, applied to other sources, wiped against the monitor (R-CLR-4, R-CLR-5)", [] {
        Fixture f("stills");
        f.standard();                                   // shotA a 0–2, shotB b 2–4; a's reference frame is 0
        f.must("set a.basic.exposure=0.5");
        f.must("still grab a --name look1");
        const AppModel &m = f.svc->model();
        assert(m.stills.size() == 1 && m.stills[0].name == "look1" && m.stills[0].from == "a" && has(m.stills[0].file, "mv.stills/st_"));
        assert(f.images.count(m.stills[0].file) == 1);  // the picture, written
        {
            std::ifstream g(f.path("mv.stills/" + m.stills[0].id + ".grade"));
            const std::string text((std::istreambuf_iterator<char>(g)), std::istreambuf_iterator<char>());
            assert(has(text, "exposure=0.5"));            // the grade, a snapshot beside the project
        }
        // applied to b through Cosmo; one undo step
        f.must("still apply look1 b");
        assert(evalValue(f, "get b.basic.exposure") == 0.5);
        f.must("undo");
        assert(evalValue(f, "get b.basic.exposure") == 0.0);
        f.must("redo");
        f.must("undo");
        // from the playhead: the timeline's picture and the grade of the clip on top
        f.must("playhead 2.5");
        f.must("still grab");
        assert(m.stills.size() == 2 && m.stills[1].from == "b" && std::fabs(m.stills[1].at - 0.5) < 1e-9);
        // the wipe: left the picture, right the still
        auto monitor = [&](double t) { Raster r; assert(f.svc->renderFrame(t, 0, r)); return r; };
        const Raster plain = monitor(1.0), still = f.images[m.stills[0].file];
        auto at = [](const Raster &r, int x, int y) { return (int)r.rgba[((size_t)y * r.width + x) * 4]; };
        f.must("view wipe look1 --split vertical --at 0.5");
        assert(m.wipeRef == m.stills[0].id && m.wipeLabel == "look1" && m.wipeVertical && m.wipeAt == 0.5);
        Raster w = monitor(1.0);
        std::printf("    wipe: left %d (the picture %d), right %d (the still %d)\n", at(w, 10, 13), at(plain, 10, 13), at(w, 40, 13), at(still, 40, 13));
        assert(at(w, 10, 13) == at(plain, 10, 13) && at(w, 40, 13) == at(still, 40, 13) && at(plain, 40, 13) != at(still, 40, 13));
        f.must("view wipe --split horizontal --at 0.25");    // only the split moves
        w = monitor(1.0);
        assert(!m.wipeVertical && at(w, 40, 3) == at(plain, 40, 3) && at(w, 40, 20) == at(still, 40, 20));
        // a deliverable never wipes
        f.must("export-still --timeline main --out \"" + f.path("x.png") + "\" --at 1");
        assert(f.images[f.path("x.png")].rgba == plain.rgba);
        // against another version at the playhead
        f.must("timeline new alt --base main");
        f.must("timeline open alt");
        f.must("set a.basic.exposure=1.0");
        f.must("still grab a --name altlook");           // a still on a version keeps the version's grade
        {
            const std::string id = f.svc->model().stills.back().id;
            std::ifstream g(f.path("mv.stills/" + id + ".grade"));
            const std::string text((std::istreambuf_iterator<char>(g)), std::istreambuf_iterator<char>());
            assert(has(text, "exposure=1\n") || has(text, "exposure=1.0"));
            f.must("still delete altlook");
        }
        f.must("timeline open main");
        f.must("view wipe alt --split vertical --at 0.5");
        Raster alt;
        assert(f.svc->renderTimelineFrame(f.svc->project().idForRef("alt"), 1.0, 0, alt));
        w = monitor(1.0);
        assert(m.wipeLabel == "alt" && at(w, 40, 13) == at(alt, 40, 13) && at(w, 10, 13) == at(plain, 10, 13) && at(alt, 40, 13) != at(plain, 40, 13));
        {
            // Grade's monitor: the same source, graded as the other version grades it, on the right
            Raster g;
            assert(f.svc->renderSourceFrame("a", 0.5, 0, g));
            assert(at(g, 40, 13) > at(g, 10, 13) + 3);   // alt's a is brighter (exposure 1 against 0.5)
        }
        f.must("view wipe off");
        assert(monitor(1.0).rgba == plain.rgba && m.wipeRef.empty());
        // a still is a reference: undo leaves it; a version cannot take a grade
        f.must("set b.basic.contrast=10");
        f.must("still grab b --name look2");
        f.must("undo");
        assert(m.stills.size() == 3);
        std::string err;
        f.must("timeline open alt");
        assert(!f.run("still apply look1 b", &err) && has(err, "is a version"));
        f.must("timeline open main");
        f.must("view wipe look2");
        const std::string look2 = m.stills.back().id;
        assert(fs::exists(f.path("mv.stills/" + look2 + ".grade")));
        f.must("still delete look2");
        assert(m.stills.size() == 2 && m.wipeRef.empty() && !fs::exists(f.path("mv.stills/" + look2 + ".grade")));
        assert(!f.run("still apply look2", &err) && has(err, "no still named"));
        // the gallery is in the .isp
        f.must("project save");
        std::ifstream in(f.path("mv.isp"));
        const std::string isp((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        assert(has(isp, "#still id=st_1 name=look1") && has(isp, "grade=mv.stills/st_1.grade"));
        // … and a new session has them, and applies them
        f.svc = f.make();
        f.must("project open \"" + f.path("mv.isp") + "\"");
        assert(f.svc->model().stills.size() == 2 && f.svc->model().stills[0].name == "look1");
        f.must("still apply look1 b");
        assert(evalValue(f, "get b.basic.exposure") == 0.5);
    });

    test("the node graph: a serial node grades after, a parallel node adds its difference from the input by its mix (R-CLR-3)", [] {
        Fixture f("nodes");
        f.standard();                                   // shotA: a's frame 24 at t = 1 (R = 24, B = 100)
        f.must("set a.basic.exposure=0.3");
        auto blue = [&] { Raster r; assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, r)); return (int)r.rgba[((size_t)13 * 48 + 20) * 4 + 2]; };
        const int base = blue();
        // a parallel node: empty, it changes nothing
        f.must("node parallel a");
        const RackNodeModel *v = nullptr;
        for (const auto &n : f.svc->model().rack) if (n.bindName == "a_par") v = &n;
        assert(v && v->parallelOf == f.svc->project().idForRef("a") && v->parallelMix == 1.0);
        assert(blue() == base);
        // graded, its difference from the input is added to a's result
        f.must("set a_par.basic.exposure=1.0");
        const int full = blue();
        std::printf("    parallel: a alone %d, + a graded parallel node %d\n", base, full);
        assert(full > base + 10);
        f.must("set a_par.parallelMix=0.5");
        const int half = blue();
        assert(half > base && half < full);
        f.must("set a_par.parallelMix=0");
        assert(blue() == base);
        f.must("set a_par.parallelMix=1");
        std::string err;
        assert(!f.run("set a.parallelMix=0.5", &err) && has(err, "not a parallel node"));
        assert(!f.run("node serial a_par", &err) && has(err, "parallel node"));
        assert(!f.run("node parallel a_par", &err) && has(err, "parallel node itself"));
        // a serial node after a: a group around it, graded after
        f.must("node serial a --name grade2");
        int parent = -1, gi = -1;
        for (int i = 0; i < (int)f.svc->model().rack.size(); ++i)
        {
            if (f.svc->model().rack[(size_t)i].bindName == "a") parent = f.svc->model().rack[(size_t)i].parent;
            if (f.svc->model().rack[(size_t)i].bindName == "grade2") gi = i;
        }
        assert(gi >= 0 && parent == gi && f.svc->model().rack[(size_t)gi].group);
        f.must("set grade2.basic.exposure=-1.0");
        assert(blue() < full - 10);
        assert(!f.run("node parallel grade2", &err) && has(err, "is a group"));
        // remove: the serial node ungroups, the parallel node goes, a source stays
        f.must("node remove grade2");
        assert(blue() == full);
        f.must("node remove a_par");
        assert(blue() == base && !f.svc->project().rackObj(f.svc->project().idForRef("a_par")));
        assert(!f.run("node remove a", &err) && has(err, "is a source"));
    });

    test("autosave writes unsaved work beside the project every interval; a crash's autosave is offered and recovered (R-DLV-5, R-DLV-6)", [] {
        Fixture f("autosave");
        f.standard();
        f.must("project save");
        const std::string isp = f.path("mv.autosave.isp"), grades = f.path("mv.autosave.grades");
        f.must("settings set autosave=10");
        f.must("set a.basic.exposure=0.4");
        f.must("clip move shotB --at 3");
        // the first unsaved change starts the clock; nothing before the interval, then one write
        f.svc->pump(f.now += 1000);
        f.svc->pump(f.now += 5000);
        assert(!fs::exists(isp));
        f.svc->pump(f.now += 6000);
        assert(fs::exists(isp) && fs::exists(grades) && f.svc->model().autosavedAt > 0);
        {
            std::ifstream g(grades);
            const std::string text((std::istreambuf_iterator<char>(g)), std::istreambuf_iterator<char>());
            assert(has(text, "exposure=0.4"));
        }
        f.must("set b.basic.contrast=20");          // after the autosave: only in memory, then lost with the session
        f.must("still grab a --name kept");         // (and a still: the gallery comes back with the rest)
        f.must("project autosave");                 // … unless written now
        // a crash: a new session, the project never saved
        f.svc = f.make();
        f.must("project open \"" + f.path("mv.isp") + "\"");
        assert(f.svc->model().recoveryAvailable && f.svc->model().recoveryTime > 0);
        assert(evalValue(f, "get a.basic.exposure") == 0.0);   // the file's state
        f.must("project recover");
        assert(evalValue(f, "get a.basic.exposure") == 0.4 && evalValue(f, "get b.basic.contrast") == 20.0);
        assert(has(f.out("get shotB.at"), "shotB.at=3.0") && f.svc->model().stills.size() == 1);
        assert(f.svc->model().dirty && !f.svc->model().recoveryAvailable);   // recovered, not saved
        f.must("project save");                     // kept: the autosave is spent
        assert(!fs::exists(isp) && !fs::exists(grades));
        // discarded instead
        f.must("set a.basic.exposure=0.9");
        f.must("project autosave");
        f.svc = f.make();
        f.must("project open \"" + f.path("mv.isp") + "\"");
        assert(f.svc->model().recoveryAvailable);
        f.must("project recover --discard");
        assert(!fs::exists(isp) && evalValue(f, "get a.basic.exposure") == 0.4 && !f.svc->model().recoveryAvailable);
        std::string err;
        assert(!f.run("project recover", &err) && has(err, "no autosave"));
        // a deliberate close is not a crash; an autosave older than the file is not offered
        f.must("set a.basic.exposure=0.7");
        f.must("project autosave");
        f.must("project close");
        assert(!fs::exists(isp));
        f.must("project open \"" + f.path("mv.isp") + "\"");
        f.must("set a.basic.exposure=0.8");
        f.must("project autosave");
        fs::last_write_time(isp, fs::last_write_time(f.path("mv.isp")) - std::chrono::hours(1));
        f.svc = f.make();
        f.must("project open \"" + f.path("mv.isp") + "\"");
        assert(!f.svc->model().recoveryAvailable);
        assert(!f.run("settings set autosave=5", &err) && has(err, "10..3600"));
    });

    test("render presets: three built in, saved by name beside the settings, applied in one step, a given flag winning (R-DLV-3)", [] {
        Fixture f("presets");
        f.must("project new \"" + f.path("p.isp") + "\" --fps 24 --res 2880x2160");   // 4:3
        f.must("rack add \"" + f.path("footage/a.mp4") + "\"");
        f.must("track add --kind video");
        f.must("clip add --track v0 --src a --in 0 --out 1 --at 0");
        const AppModel &m = f.svc->model();
        assert(m.renderPresets.size() >= 3 && m.renderPresets[0].name == "YouTube 1080p" && m.renderPresets[0].builtIn);
        auto render = [&](const std::string &flags, const std::string &out) {
            f.written.clear();
            f.must("render --timeline main --range 0:0.125 --out \"" + f.path(out) + "\" " + flags);
        };
        // a 1080p frame on a 4:3 project: 1440×1080, its quality and speed
        render("--preset \"YouTube 1080p\"", "yt.mp4");
        std::printf("    YouTube 1080p on 2880x2160: %dx%d %s q%d %s\n", gBegin.w, gBegin.h, gBegin.spec.codec.c_str(), gBegin.spec.quality, gBegin.spec.speed.c_str());
        assert(gBegin.w == 1440 && gBegin.h == 1080 && gBegin.spec.codec == "h264" && gBegin.spec.quality == 18 && gBegin.spec.speed == "slow");
        render("--preset \"YouTube 1080p\" --quality 30", "yt2.mp4");                 // a flag given wins
        assert(gBegin.spec.quality == 30 && gBegin.w == 1440);
        render("--preset \"ProRes HQ master\"", "m.mov");
        assert(gBegin.spec.codec == "prores" && gBegin.spec.profile == "hq" && gBegin.w == 2880 && gBegin.h == 2160);
        // saved, listed, applied, kept for the next session
        f.must("render preset save \"Grade review\" --format h265 --bits 10 --quality 20 --res 960x540");
        assert(m.renderPresets.size() == 4 && m.renderPresets[3].name == "Grade review" && !m.renderPresets[3].builtIn);
        assert(has(f.out("render preset list"), "Grade review  --format h265 --bits 10 --quality 20 --res 960x540"));
        render("--preset \"Grade review\"", "g.mp4");
        assert(gBegin.spec.codec == "h265" && gBegin.spec.bitDepth == 10 && gBegin.spec.quality == 20 && gBegin.w == 720 && gBegin.h == 540);
        f.svc = f.make();
        assert(f.svc->model().renderPresets.size() == 4);
        std::string err;
        assert(!f.run("render preset save \"YouTube 1080p\" --format h264", &err) && has(err, "built in"));
        assert(!f.run("render preset save nofmt --quality 20", &err) && has(err, "--format"));
        assert(!f.run("render preset save x --format h264 --out a.mp4", &err));
        f.must("project open \"" + f.path("p.isp") + "\"");
        assert(!f.run("render --timeline main --out \"" + f.path("n.mp4") + "\" --preset nothere", &err) && has(err, "no render preset named"));
        f.must("render preset delete \"Grade review\"");
        assert(f.svc->model().renderPresets.size() == 3);
        assert(!f.run("render preset delete \"Grade review\"", &err) && has(err, "no preset named"));
    });

    test("burn-ins: record and source timecode, clip and source names and text, placed, on a render's every frame (R-DLV-2)", [] {
        Fixture f("burnin");
        f.standard();                                   // shotA a 0–2 (TC 01:00:10:00), shotB b 2–4 (TC 01:00:11:00)
        f.must("set a.basic.exposure=0.5");
        gBurns.clear();
        f.written.clear();
        f.must("render --timeline main --format h264 --res 46x26 --out \"" + f.path("b.mp4") + "\" --burnin \"tc@bl,srctc@br,clip@tl,source@tr,text=DRAFT@tc\"");
        assert(gBurns.size() == 96 && f.written.size() == 96);
        auto item = [&](int frame, int k) { return gBurns[(size_t)frame][(size_t)k]; };
        std::printf("    frame 0: %s · %s · %s · %s · %s;  frame 60: %s · %s · %s\n", item(0, 0).text.c_str(), item(0, 1).text.c_str(), item(0, 2).text.c_str(),
                    item(0, 3).text.c_str(), item(0, 4).text.c_str(), item(60, 0).text.c_str(), item(60, 1).text.c_str(), item(60, 2).text.c_str());
        assert(item(0, 0).text == "01:00:00:00" && item(24, 0).text == "01:00:01:00" && item(0, 0).mono);
        assert(item(0, 1).text == "01:00:10:00" && item(12, 1).text == "01:00:10:12");   // a's own timecode at the frame it shows
        assert(item(60, 1).text == "01:00:11:12");                                         // b's, 12 frames into shotB
        assert(item(0, 2).text == "shotA" && item(60, 2).text == "shotB" && item(0, 3).text == "a" && item(60, 3).text == "b");
        assert(item(0, 4).text == "DRAFT" && !item(0, 4).mono);
        // the places: left/right, top/bottom, inside a margin; sized to the frame
        const auto bl = item(0, 0), tr = item(0, 3), tc = item(0, 4);
        assert(bl.align == 0 && bl.valign == 1 && bl.x > 0 && bl.y > 13 && bl.y < 26);
        assert(tr.align == 2 && tr.valign == 0 && tr.x > 23 && tr.x < 46 && tr.y < 13 && tc.align == 1 && std::fabs(tc.x - 23.0) < 1e-9);
        assert(bl.px >= 10.0);
        // laid over the finished frame — after the grade
        assert(f.written[0].rgba[0] == 255 && f.written[0].rgba[1] == 0 && f.written[0].rgba[2] == 255);
        // the spec says so; refusals name the grammar
        assert(has(f.svc->model().renders.back().spec, "burn-ins"));
        std::string err;
        assert(!f.run("render --timeline main --format h264 --res 46x26 --out \"" + f.path("c.mp4") + "\" --burnin \"foo@bl\"", &err) && has(err, "tc, srctc, clip"));
        assert(!f.run("render --timeline main --format h264 --res 46x26 --out \"" + f.path("c.mp4") + "\" --burnin tc", &err) && has(err, "needs a place"));
        assert(item(0, 0).white == 1.0);
        // an HDR render's burn-in is graphics white, not the peak: 203 cd/m² (BT.2408)
        gBurns.clear();
        f.must("render --timeline main --format h265 --bits 10 --output pq --res 46x26 --out \"" + f.path("pq.mp4") + "\" --burnin tc@bl");
        assert(!gBurns.empty() && std::fabs(gBurns[0][0].white - 0.5807) < 1e-3);
        gBurns.clear();
        f.must("render --timeline main --format h265 --bits 10 --output hlg --res 46x26 --out \"" + f.path("hlg.mp4") + "\" --burnin tc@bl");
        assert(!gBurns.empty() && std::fabs(gBurns[0][0].white - 0.75) < 1e-9);
        // a still never carries one
        gBurns.clear();
        f.must("export-still --timeline main --out \"" + f.path("s.png") + "\" --at 1");
        assert(gBurns.empty());
        // a host that cannot draw text refuses rather than renders without them
        Fixture g("burnin2");
        g.noText = true;
        g.svc = g.make();
        g.standard();
        assert(!g.run("render --timeline main --format h264 --res 46x26 --out \"" + g.path("c.mp4") + "\" --burnin tc@bl", &err) && has(err, "cannot draw text"));
    });

    test("captions: an SRT onto the timeline, inherited by a version and overridden by delta, burned in, muxed and beside a render (R-DLV-1)", [] {
        Fixture f("captions");
        f.standard();                                   // shotA a 0–2, shotB b 2–4
        const std::string srt = f.path("subs.srt");
        std::ofstream(srt, std::ios::binary) << "\xEF\xBB\xBF" "1\r\n00:00:00,500 --> 00:00:01,250\r\n<i>Hello</i>\r\nworld\r\n\r\n"
                                                "2\r\n00:00:02,000 --> 00:00:03,000 X1:10\r\nSecond {\\an8}line\r\n\r\n";
        f.must("caption import \"" + srt + "\"");
        auto caps = [&] { return f.svc->model().captions; };
        assert(caps().size() == 2 && caps()[0].text == "Hello\nworld" && std::fabs(caps()[0].at - 0.5) < 1e-9 && std::fabs(caps()[0].dur - 0.75) < 1e-9);
        assert(caps()[1].text == "Second line" && caps()[1].name == "cue2" && f.svc->model().captionsShown);
        f.must("undo");                                 // one step
        assert(caps().empty());
        f.must("redo");
        assert(caps().size() == 2);
        // a version inherits them, and its edit is a delta on the base's caption
        f.must("timeline new cut2 --base main");
        f.must("timeline open cut2");
        assert(caps().size() == 2);
        f.must("set cue2.text=Zweite");
        assert(caps()[1].text == "Zweite" && has(f.out("get cue2.text"), "Zweite"));
        f.must("timeline open main");
        assert(caps()[1].text == "Second line");
        // export writes an SRT back
        f.must("caption export \"" + f.path("out.srt") + "\" --timeline cut2");
        std::ifstream e(f.path("out.srt"));
        const std::string back((std::istreambuf_iterator<char>(e)), std::istreambuf_iterator<char>());
        assert(back == "1\n00:00:00,500 --> 00:00:01,250\nHello\nworld\n\n2\n00:00:02,000 --> 00:00:03,000\nZweite\n\n");
        // a render from 0.25 s: burned in at the frame's own time, a track and an .srt in the render's seconds
        gBurns.clear();
        f.written.clear();
        f.must("render --timeline main --format h264 --res 46x26 --range 0.25:3 --out \"" + f.path("cap.mp4") + "\" --captions burn,track,sidecar");
        assert(gBurns.size() == 66);
        const auto &at12 = gBurns[12];                  // render 0.5 s = timeline 0.75 s: cue 1, two lines, the last lowest
        assert(at12.size() == 2 && at12[0].text == "world" && at12[1].text == "Hello" && at12[1].y < at12[0].y);
        assert(at12[0].align == 1 && at12[0].valign == 1 && !at12[0].mono && std::fabs(at12[0].x - 23.0) < 1e-9 && at12[0].px >= 12.0);
        assert(gBurns[0].empty() && gBurns[24].empty() && gBurns[48].size() == 1 && gBurns[48][0].text == "Second line");   // 1.25 s ends cue 1
        const auto &subs = gBegin.spec.subtitles;
        assert(subs.size() == 2 && std::fabs(subs[0].start - 0.25) < 1e-9 && std::fabs(subs[0].end - 1.0) < 1e-9);
        assert(std::fabs(subs[1].start - 1.75) < 1e-9 && std::fabs(subs[1].end - 2.75) < 1e-9 && subs[1].text == "Second line");
        std::ifstream sc(f.path("cap.srt"));
        const std::string side((std::istreambuf_iterator<char>(sc)), std::istreambuf_iterator<char>());
        assert(side == "1\n00:00:00,250 --> 00:00:01,000\nHello\nworld\n\n2\n00:00:01,750 --> 00:00:02,750\nSecond line\n\n");
        assert(has(f.svc->model().renders.back().spec, "captions burned in + track + .srt"));
        // a stream shows one cue at a time: two that start together are one, an overlap ends the earlier
        f.must("caption add --at 2 --dur 0.5 --text Over --name over1");
        f.must("caption add --at 1 --dur 0.5 --text Mid --name mid1");
        f.must("render --timeline main --format h264 --res 46x26 --out \"" + f.path("cap2.mp4") + "\" --captions track");
        const auto &s2 = gBegin.spec.subtitles;
        assert(s2.size() == 3 && std::fabs(s2[0].end - 1.0) < 1e-9 && s2[1].text == "Mid" && s2[2].text == "Second line\nOver" && std::fabs(s2[2].end - 3.0) < 1e-9);
        f.must("caption remove over1");
        f.must("caption remove mid1");
        assert(caps().size() == 2);
        // refusals name the way out
        std::string err;
        assert(!f.run("render --timeline main --format png-seq --out \"" + f.path("seq") + "\" --captions track", &err) && has(err, "sidecar writes an .srt"));
        assert(!f.run("render --timeline main --format h264 --res 46x26 --out \"" + f.path("c.mp4") + "\" --captions subtitles", &err) && has(err, "burn, track or sidecar"));
        assert(!f.run("render --timeline main --format h264 --res 46x26 --range 3.5:4 --out \"" + f.path("c.mp4") + "\" --captions track", &err) && has(err, "no captions in the range"));
        assert(!f.run("caption add --at 1 --dur 0 --text x", &err) && has(err, "more than zero"));
        std::ofstream(f.path("bad.srt")) << "1\n00:00:02,000 --> 00:00:01,000\nbackwards\n";
        assert(!f.run("caption import \"" + f.path("bad.srt") + "\"", &err) && has(err, "ends after it starts"));
        // the monitor's switch is presentation
        f.must("view captions off");
        assert(!f.svc->model().captionsShown);
    });

    test("a clip is copied and pasted whole; a drop places the rest of the source (R-TL-6, R-UI-14)", [] {
        Fixture f("clippaste");
        f.standard();
        std::string err;
        assert(!f.run("clip paste", &err) && has(err, "nothing copied"));
        // a drop: no --out = the rest of the source (FakeFrameSource: 96 frames at 24 = 4 s)
        f.must("clip add --track v0 --src a --in 1 --at 6 --name dropped");
        auto clipNamed = [&](const std::string &n) -> const ClipModel * {
            for (const auto &c : f.svc->model().clips) if (c.name == n) return &c;
            return nullptr;
        };
        const ClipModel *d = clipNamed("dropped");
        assert(d && std::fabs(d->in - 1.0) < 1e-9 && std::fabs(d->out - 4.0) < 1e-9);
        f.must("clip add --track v0 --src still --in 0 --at 12 --name held");
        assert(clipNamed("held") && std::fabs(clipNamed("held")->out - 5.0) < 1e-9);   // a still holds 5 s
        // copy carries what the clip carries; paste lands at the playhead on the copied track
        f.must("set shotA.speed=2.0");
        f.must("set shotA.opacity=0.5");
        f.must("set shotA.geom.scale=1.25");
        f.must("clip copy shotA");
        assert(f.svc->model().hasClipClipboard && f.svc->model().clipClipboardFrom == "shotA");
        f.must("clip delete shotA");                       // cut, then paste: the original is gone
        f.must("playhead 8.0");
        const size_t before = f.svc->model().clips.size();
        f.must("clip paste");
        assert(f.svc->model().clips.size() == before + 1);
        const ClipModel *p = nullptr;
        for (const auto &c : f.svc->model().clips) if (std::fabs(c.at - 8.0) < 1e-9) p = &c;
        assert(p && p->srcName == "a" && std::fabs(p->speed - 2.0) < 1e-9 && std::fabs(p->opacity - 0.5) < 1e-9);
        assert(f.svc->model().selectedClip == p->id);       // the pasted clip is the selection
        assert(f.out("get " + p->name + ".geom.scale").find("1.25") != std::string::npos);
        f.must("clip paste --at 20");                      // again, elsewhere
        f.must("undo");                                    // one paste, one step
        bool at20 = false;
        for (const auto &c : f.svc->model().clips) at20 = at20 || std::fabs(c.at - 20.0) < 1e-9;
        assert(!at20);
    });

    test("a rack node's plugin stack runs after Cosmo: add, order, on/off, mix, groups, variants, undo (R-FX-5, R-FX-6)", [] {
        Fixture f("effects");
        f.standard();
        f.must("rack add \"" + f.path("footage/edge.mp4") + "\"");
        f.must("clip add --track v0 --src edge --in 0 --out 2 --at 6 --name shotE");
        auto edgeAt = [&](int x) {
            Raster r;
            assert(f.svc->renderTimelineFrame("tl_1", 6.5, 0, r) && r.width == 48);
            return (int)r.rgba[((size_t)13 * 48 + x) * 4];
        };
        const int sharpL = edgeAt(22), sharpR = edgeAt(25);
        assert(sharpL > 200 && sharpR < 40);                                   // a hard edge, ungraded
        std::string err;
        assert(!f.run("effect add edge --type blur.nonsense", &err) && has(err, "blur.gaussian"));
        const std::string id = f.out("effect add edge --type blur.gaussian");
        assert(id == "ef_1\n");
        const auto &m = f.svc->model();
        assert(m.effects.size() == 1 && m.effects[0].nodeBind == "edge" && m.effects[0].label == "Gaussian Blur");
        assert(m.effects[0].params.size() == 1 && m.effects[0].params[0].key == "radius" && std::fabs(m.effects[0].params[0].value - 8.0) < 1e-9);
        assert(m.effectTypes.size() >= 5);
        const int blurL = edgeAt(22), blurR = edgeAt(25);
        assert(blurL < sharpL - 20 && blurR > sharpR + 20);                    // the edge softened
        assert(f.out("get ef_1.radius") == "ef_1.radius=8.0\n");
        assert(!f.run("set ef_1.radius=500", &err) && has(err, "0.0..200.0"));
        assert(!f.run("set ef_1.length=3", &err) && has(err, "no parameter"));
        f.must("set ef_1.enabled=0");
        assert(edgeAt(22) == sharpL);                                          // off: the input, exactly
        f.must("set ef_1.enabled=1");
        f.must("set ef_1.mix=0");
        assert(edgeAt(22) == sharpL);                                          // mix 0: the input
        f.must("set ef_1.mix=1");
        // a second plugin, then reorder: the stack's order is the file's `order`
        f.must("effect add edge --type blur.box");
        f.must("effect move ef_2 --to 0");
        assert(f.svc->project().effect("ef_2")->order == 0 && f.svc->project().effect("ef_1")->order == 1);
        f.must("effect remove ef_2");
        assert(f.svc->project().effect("ef_1")->order == 0);
        // a GROUP's plugins reach its members, after their own
        f.must("rack group new look --nodes edge");
        f.must("set ef_1.enabled=0");
        const std::string gid = f.out("effect add look --type blur.box");
        assert(gid == "ef_3\n" && edgeAt(22) < sharpL - 10);                    // the group's blur, on the member
        f.must("effect remove ef_3");
        assert(edgeAt(22) == sharpL);
        f.must("undo");                                                        // one step brings it back
        assert(f.svc->project().effect("ef_3") && edgeAt(22) < sharpL - 10);
        // a variant gets its own copy of the stack, fresh ids
        f.must("set ef_1.radius=4");
        f.must("rack duplicate edge");
        int copies = 0;
        for (const auto &e : f.svc->project().imageEffects)
            if (e.node != f.svc->project().effect("ef_1")->node && e.type == "blur.gaussian") { ++copies; assert(e.id != "ef_1"); }
        assert(copies == 1);
        // the file keeps it all, and a reopen reads it back
        f.must("project save");
        const std::string text = f.svc->project().serialize();
        assert(has(text, "#effect id=ef_1 ") && has(text, "radius=4.0") && has(text, "type=blur.gaussian"));
    });

    test("playback reads ahead: frames from the pool are the frames, on time, and pausing goes sharp again (R-PLAY-2)", [] {
        Fixture f("ahead");
        f.async = true;          // the GTK host's binding: a preview worker and the read-ahead pool
        f.svc = f.make();
        f.standard();
        f.must("set a.basic.exposure=0.3");
        const auto t0 = std::chrono::steady_clock::now();
        auto nowMs = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() + 1000.0; };
        f.svc->pump(nowMs());
        std::string err;
        assert(f.svc->dispatchText("play", err));
        Raster r;
        double last = -1;
        int checked = 0;
        long long hitsBefore = 0;
        while (nowMs() < 1000.0 + 1500.0 && f.svc->model().playing)
        {
            f.svc->pump(nowMs());
            const double t = f.svc->model().playhead;
            if (t != last)
            {
                last = t;
                hitsBefore = f.svc->playbackStats().hits;
                assert(f.svc->renderFrame(t, 48, r) || r.empty());
                if (f.svc->playbackStats().hits > hitsBefore && checked < 5)
                {
                    // a ring frame IS the frame: the same pixels a direct render of t gives at that edge
                    Raster direct;
                    assert(f.svc->renderTimelineFrame("tl_1", t, f.svc->playbackStats().edge, direct));
                    assert(direct.rgba == r.rgba);
                    ++checked;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const auto st = f.svc->playbackStats();
        std::printf("    played: %lld shown, %lld exact, lag %.2f frames, edge %d, pool %.1f fps\n", st.shown, st.hits,
                    st.shown ? st.lagFrames / st.shown : 0.0, st.edge, st.rate);
        assert(checked > 0 && st.hits > 0 && st.edge > 0);
        assert(st.shown > 0 && st.lagFrames / st.shown < 2.0);   // never far behind the playhead
        f.must("pause");
        assert(f.svc->playbackStats().edge == 0);                  // paused: graded at the full preview size
    });

    test("keyframes: colour, effects and clips animate on their clocks; set keys; versions add, pins freeze (R-ANIM)", [] {
        Fixture f("anim");
        f.standard();                                             // shotA = a.mp4 0–2 s at 0; shotB = b.mp4 at 2
        auto frame = [&](const std::string &tl, double t) {
            Raster r;
            assert(f.svc->renderTimelineFrame(tl, t, 0, r));
            return r;
        };
        auto fileHas = [&](const std::string &needle) {
            std::ifstream in(f.path("mv.isp"));
            std::stringstream ss;
            ss << in.rdbuf();
            return has(ss.str(), needle);
        };
        // the reference: exposure 0.5 held still, at timeline 1.0 = source time 1.0 of a
        f.must("set a.basic.exposure=0.5");
        const Raster still05 = frame("tl_1", 1.0);
        f.must("set a.basic.exposure=0");
        const Raster still0 = frame("tl_1", 1.0);
        assert(still05.rgba != still0.rgba);
        // R-ANIM-1/2: a linear curve 0 → 1 over source 0–2 s is 0.5 at source time 1
        f.must("key add a.basic.exposure --at 0 --value 0");
        std::printf("    %s", f.out("key add a.basic.exposure --at 2 --value 1").c_str());
        assert(frame("tl_1", 1.0).rgba == still05.rgba);
        // SOURCE time: a second clip of a, placed elsewhere, sees the same value at the same source frame
        f.must("clip add --track v0 --src a --in 1 --out 1.5 --at 6 --name again");
        assert(frame("tl_1", 6.0).rgba == still05.rgba);
        assert(f.svc->model().anims.size() == 1 && f.svc->model().anims[0].keys.size() == 2 && f.svc->model().anims[0].clock == "source");
        // R-ANIM-3: a set on an animated key writes a key where Grade stands (the reference frame)
        f.must("set a.frame=1.5");
        f.must("set a.basic.exposure=0.8");
        const AnimModel *am = &f.svc->model().anims[0];
        assert(am->keys.size() == 3 && std::fabs(am->keys[1].t - 1.5) < 1e-9 && std::fabs(am->keys[1].v - 0.8) < 1e-6);
        assert(has(f.out("get a.basic.exposure"), "=0.8"));
        assert(has(f.out("eval a.basic.exposure --explain"), "animated   3 keys"));
        // shaping: a speed makes that side a bezier, written to the file only when it is one
        f.must("key set a.basic.exposure --at 0 --speed-out 2");
        f.must("project save");
        assert(fileHas("#anim id=an_1 node=ro_1 key=basic.exposure") && fileHas("out=bezier speedOut=2.0 inflOut=33.333"));
        assert(frame("tl_1", 0.5).rgba != still0.rgba);
        std::string err;
        assert(!f.run("key set a.basic.exposure --at 0 --influence-out 0", &err) && has(err, "percent"));
        assert(!f.run("key add a.basic.exposure --at 1 --value 9", &err) && has(err, ".."));      // out of range
        assert(!f.run("key add shotA.at", &err) && has(err, "not its placement"));
        assert(!f.run("key remove a.basic.exposure --at 0.7", &err) && has(err, "no key at"));
        // R-ANIM-5: a version inherits the curve live, adds a scalar delta, and cannot key it
        f.must("timeline new v2 --base main");
        f.must("timeline open v2");
        assert(!f.run("key add a.basic.exposure --at 1", &err) && has(err, "R-ANIM-5"));
        f.must("set a.basic.exposure=0.9");                    // curve says 0.8 at the reference frame → +0.1
        f.must("project save");
        assert(fileHas("#tlgrade timeline=tl_2 node=ro_1 exposure=0.1"));
        f.must("timeline open main");
        // a pin freezes the curve: the base changes its curve, the pinned version does not move
        f.must("timeline new p1 --base main");
        f.must("timeline pin p1");
        const NodeId p1 = "tl_3";                              // main, v2, p1
        const Raster pinnedBefore = frame(p1, 1.0), mainBefore = frame("tl_1", 1.0);
        assert(pinnedBefore.rgba == mainBefore.rgba);
        f.must("key set a.basic.exposure --at 1.5 --value 0.2");   // shapes source time 1.0
        assert(frame("tl_1", 1.0).rgba != mainBefore.rgba);
        assert(frame(p1, 1.0).rgba == pinnedBefore.rgba);
        // an effect's parameter, in the source's time
        f.must("effect add a --type blur.gaussian");
        f.must("key add ef_1.radius --at 0 --value 0");
        f.must("key add ef_1.radius --at 2 --value 20");
        f.must("set a.frame=1");
        assert(has(f.out("get ef_1.radius"), "=10"));
        // a clip's opacity, on the clip's own footage clock: in=0, so source time = timeline time
        f.must("key add shotA.opacity --at 0 --value 0");
        f.must("key add shotA.opacity --at 2 --value 1");
        f.must("playhead 1.0");
        assert(has(f.out("get shotA.opacity"), "=0.5"));
        // the render draws it: clearing the curve leaves 0.5 still, and the frame is the same
        const Raster animated = frame("tl_1", 1.0);
        f.must("key clear shotA.opacity");
        assert(has(f.out("get shotA.opacity"), "=0.5") && frame("tl_1", 1.0).rgba == animated.rgba);
        f.must("undo");                                         // the curve back
        assert(has(f.out("get shotA.opacity"), "=0.5"));
        // a split leaves both halves animated on the same frames
        f.must("clip split shotA --at 1");
        int opacityCurves = 0;
        for (const auto &a : f.svc->model().anims) opacityCurves += a.key == "opacity";
        assert(opacityCurves == 2);
        f.must("playhead 1.5");
        bool rightAt075 = false;
        for (const auto &c : f.svc->model().clips)
            if (c.at > 0.9 && c.at < 1.1 && c.track == "trk_1") rightAt075 = has(f.out("get " + (c.name.empty() ? c.id : c.name) + ".opacity"), "=0.75");
        assert(rightAt075);
        // the last key removed: the curve goes, its value stays
        f.must("key remove ef_1.radius --at 0");
        f.must("key remove ef_1.radius --at 2");
        assert(has(f.out("get ef_1.radius"), "=20"));
        // undo brings a removed key back; a deleted clip takes its curve with it; it all saves and loads
        f.must("undo");                                         // one key back (20)…
        f.must("undo");                                         // …and the other: 0 → 20 again, 10 at source time 1
        assert(has(f.out("get ef_1.radius"), "=10"));
        f.must("clip delete again");
        f.must("project save");
        f.svc = f.make();
        f.must("project open \"" + f.path("mv.isp") + "\"");
        assert(!f.svc->model().anims.empty());
        std::printf("    %zu curves reloaded\n", f.svc->model().anims.size());
    });

    test("shapes animate: a wheel, a crop and a tone curve blend between keys; a pin freezes them (R-ANIM-6)", [] {
        Fixture f("animshape");
        f.standard();
        auto frame = [&](const std::string &tl, double t) {
            Raster r;
            assert(f.svc->renderTimelineFrame(tl, t, 0, r));
            return r;
        };
        // the still the animation must reproduce at source time 1.0 (half way): a midtone wheel
        f.must("set a.grade.grade1=60,50,20");
        const Raster still = frame("tl_1", 1.0);
        f.must("set a.grade.grade1=0,0,0");
        assert(frame("tl_1", 1.0).rgba != still.rgba);
        f.must("key add a.grade.grade1 --at 0 --value 0,0,0");
        f.must("key add a.grade.grade1 --at 2 --value 120,100,40");
        assert(frame("tl_1", 1.0).rgba == still.rgba);
        // a crop, on footage that has an edge to move (the flat fakes would not show one)
        f.must("rack add \"" + f.path("footage/edge.mp4") + "\"");
        f.must("clip add --track v0 --src edge --in 0 --out 2 --at 6 --name shotE");
        f.must("set edge.xform.crop=0.1,0,0.8,1");
        const Raster cropStill = frame("tl_1", 7.0);
        f.must("set edge.xform.crop=0,0,1,1");
        assert(frame("tl_1", 7.0).rgba != cropStill.rgba);
        f.must("key add edge.xform.crop --at 0 --value 0,0,1,1");
        f.must("key add edge.xform.crop --at 2 --value 0.2,0,0.6,1");
        assert(frame("tl_1", 7.0).rgba == cropStill.rgba);
        // the hue takes the short way round: 350° → 10° passes 0°, not 180°
        f.must("key set a.grade.grade1 --at 0 --value 350,0,0");
        f.must("key set a.grade.grade1 --at 2 --value 10,0,0");
        f.must("set a.frame=1");
        assert(has(f.out("get a.grade.grade1"), "=0,0,0"));
        // a tone curve: different point counts are resampled; a speed is refused, an ease is not
        f.must("key add a.curve.curve --at 0 --value \"0,0;1,1\"");
        f.must("key add a.curve.curve --at 2 --value \"0,0;0.5,0.8;1,1\"");
        const std::string mid = f.out("get a.curve.curve");
        std::printf("    the curve at source 1.0: %s", mid.c_str());
        assert(std::count(mid.begin(), mid.end(), ';') == 16);
        std::string err;
        assert(!f.run("key set a.curve.curve --at 0 --speed-out 2", &err) && has(err, "a shape"));
        f.must("key set a.curve.curve --at 0 --ease ease-out");
        assert(!f.run("key add a.basic.curveLog --at 0", &err));   // a switch has no curve
        // the format keeps them, a pin freezes them
        f.must("project save");
        {
            std::ifstream in(f.path("mv.isp"));
            std::stringstream ss;
            ss << in.rdbuf();
            assert(has(ss.str(), "shape=\"0,0;0.5,0.8;1,1\"") && has(ss.str(), "shape=0.2,0,0.6,1"));
        }
        f.must("timeline new p1 --base main");
        f.must("timeline pin p1");
        const Raster pinned = frame("tl_2", 1.0);
        f.must("key set a.grade.grade1 --at 2 --value 10,100,40");    // the base's wheel gains saturation and lift
        assert(frame("tl_1", 1.0).rgba != pinned.rgba && frame("tl_2", 1.0).rgba == pinned.rgba);
        // the last key removed leaves the shape as the parameter's own
        f.must("key remove edge.xform.crop --at 0");
        f.must("key remove edge.xform.crop --at 2");
        assert(has(f.out("get edge.xform.crop"), "0.2,0,0.6,1"));
    });

    test("several keys at once: shift together, copy, paste to the same or another property; the lane's height persists (R-ANIM-7, R-ANIM-8)", [] {
        Fixture f("animkeys");
        f.standard();
        f.must("key add a.basic.exposure --at 0 --value 0");
        f.must("key add a.basic.exposure --at 1 --value 1");
        f.must("key add shotA.opacity --at 0 --value 0");
        f.must("key add shotA.opacity --at 1.5 --value 1");
        auto keysOf = [&](const std::string &address) {
            std::vector<double> v;
            for (const auto &a : f.svc->model().anims)
                if (a.address == address) for (const auto &k : a.keys) v.push_back(k.t);
            return v;
        };
        // a box-selection across two curves, moved together: one command, one undo step
        f.must("key shift --keys \"a.basic.exposure@1,shotA.opacity@1.5\" --by 0.25");
        assert(keysOf("a.basic.exposure") == std::vector<double>({0.0, 1.25}) && keysOf("shotA.opacity") == std::vector<double>({0.0, 1.75}));
        std::string err;
        assert(!f.run("key shift --keys \"a.basic.exposure@0\" --by 1.25", &err) && has(err, "would meet"));
        assert(!f.run("key shift --keys \"a.basic.exposure@0.5\" --by 1", &err) && has(err, "no key at"));
        f.must("undo");
        assert(keysOf("a.basic.exposure") == std::vector<double>({0.0, 1.0}));
        // copy two keys of one property; paste them at 3 s onto the same property, then onto another clip's
        f.must("key copy --keys \"shotA.opacity@0,shotA.opacity@1.5\"");
        assert(f.svc->model().keyClipboardCount == 2 && f.svc->model().keyClipboardCurves == 1);
        f.must("key paste --at 3");
        assert(keysOf("shotA.opacity") == std::vector<double>({0.0, 1.5, 3.0, 4.5}));
        f.must("key paste --to shotB.opacity --at 0");
        assert(keysOf("shotB.opacity") == std::vector<double>({0.0, 1.5}));
        assert(!f.run("key paste --to a.curve.curve", &err) && has(err, "is a shape"));
        f.must("key copy --keys \"a.basic.exposure@0,shotA.opacity@0\"");
        assert(!f.run("key paste --to shotB.opacity", &err) && has(err, "ONE property"));
        assert(!f.run("key copy --keys \"nonsense\"", &err) && has(err, "<address>@<seconds>"));
        // R-ANIM-8: the lane's height is a setting, bounded, persisted
        f.must("settings set keyLaneHeight=220");
        assert(!f.run("settings set keyLaneHeight=20", &err) && has(err, "80..600"));
        f.svc = f.make();
        assert(f.svc->model().settings.keyLaneHeight == 220);
    });

    test("with Use GPU on, the grade runs on Cosmo's GPU backend and matches the CPU (R-GPU-1)", [] {
        Fixture f("gpu");
        f.standard();
        f.must("set a.basic.exposure=0.4");
        f.must("set a.basic.clarity=35");
        f.must("set a.basic.vibrance=30");
        f.must("set a.grade.grade1=200,40,5");
        Raster cpu, gpu;
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, cpu));
        if (!f.svc->model().settings.gpuAvailable) { std::printf("    skipped: no GPU backend on this host\n"); return; }
        f.must("settings set useGpu=1");     // the frame cache keys on the processor: this grades afresh
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, gpu));
        f.must("playhead 0");                // a command refreshes the model
        assert(gpu.width == cpu.width && gpu.height == cpu.height);
        int maxd = 0;
        for (size_t i = 0; i < cpu.rgba.size(); ++i) maxd = std::max(maxd, std::abs((int)cpu.rgba[i] - (int)gpu.rgba[i]));
        std::printf("    GPU vs CPU, max difference %d/255\n", maxd);
        assert(maxd <= 2);
        assert(f.svc->model().settings.useGpu && f.svc->model().settings.gpuInUse);   // it did run there
        // the spatial stages too (sharpening, noise reduction, dehaze): still on the GPU
        f.must("set a.detail.sharpenAmount=40");
        f.must("set a.detail.nrLuminance=30");
        f.must("set a.basic.dehaze=20");
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, gpu));
        f.must("playhead 0");
        assert(f.svc->model().settings.gpuInUse);
        f.must("set a.detail.sharpenAmount=0");
        f.must("settings set useGpu=0");
        f.must("set a.basic.contrast=5");    // a grade not cached yet, on the CPU
        assert(f.svc->renderTimelineFrame("tl_1", 1.0, 0, gpu));
        f.must("playhead 0");
        assert(!f.svc->model().settings.gpuInUse);
    });

    test("the preview cache holds the graded frames, rebuilds only what an edit changed, and playback reads it (R-PLAY-1)", [] {
        Fixture f("pcache");
        f.standard();                                             // shotA 0–2 s, shotB 2–4 s: four 1-s segments
        f.must("set a.basic.exposure=0.3");
        gCacheSegmentsWritten = 0;
        f.must("cache build");
        f.must("wait cache.done");
        const AppModel &m = f.svc->model();
        std::printf("    cached %d of %d frames in %d segments\n", m.previewCacheFrames, m.previewCacheTotal, gCacheSegmentsWritten);
        assert(m.previewCacheTotal == 96 && m.previewCacheFrames == 96 && gCacheSegmentsWritten == 4);
        assert(m.previewCacheSegments == std::vector<int>({1, 1, 1, 1}));
        assert(fs::exists(f.path("mv.cache/tl_1/index")));
        // a cached frame IS the graded frame (the fake file is lossless): frame 30 = segment 1, index 6
        {
            std::lock_guard<std::mutex> l(gMemMu);
            const std::vector<Raster> *seg1 = nullptr;
            for (const auto &kv : gMemFiles) if (has(kv.first, "mv.cache/tl_1/seg_1_")) seg1 = &kv.second;
            assert(seg1 && seg1->size() == 24);
            Raster direct;
            assert(f.svc->renderTimelineFrame("tl_1", 30 / 24.0, f.svc->cacheEdge(), direct));
            // 48×27 is stored 48×28 — encoders want even sizes; the reader crops the repeated row off
            const Raster &c6 = (*seg1)[6];
            assert(c6.width == 48 && c6.height == 28 && direct.width == 48 && direct.height == 27);
            assert(std::equal(direct.rgba.begin(), direct.rgba.end(), c6.rgba.begin()));
        }
        // nothing changed: building again writes nothing
        gCacheSegmentsWritten = 0;
        f.must("cache build");
        f.must("wait cache.done");
        assert(gCacheSegmentsWritten == 0 && f.svc->model().previewCacheFrames == 96);
        // an edit to shotB's source invalidates exactly shotB's two seconds
        f.must("set b.basic.exposure=0.5");
        f.must("cache build");
        f.must("wait cache.done");
        std::printf("    after an edit to shotB: %d segments rebuilt\n", gCacheSegmentsWritten);
        assert(gCacheSegmentsWritten == 2 && f.svc->model().previewCacheFrames == 96);
        // the index outlives the service: a new session finds every frame current and builds nothing
        f.must("project save");
        gCacheSegmentsWritten = 0;
        f.svc = f.make();
        f.must("project open \"" + f.path("mv.isp") + "\"");
        f.must("cache build");
        f.must("wait cache.done");
        assert(gCacheSegmentsWritten == 0 && f.svc->model().previewCacheFrames == 96);
        // the setting is persisted and refused anything but 0|1
        std::string err;
        assert(!f.run("settings set previewCache=2", &err) && has(err, "previewCache is 0 or 1"));
        f.must("cache clear");
        assert(!fs::exists(f.path("mv.cache/tl_1/index")));
    });

    test("playback decodes cached frames instead of grading them, and they are the frames (R-PLAY-1)", [] {
        Fixture f("pcacheplay");
        f.async = true;
        f.svc = f.make();
        f.standard();
        f.must("set a.basic.exposure=0.3");
        f.must("cache build");
        f.must("wait cache.done");
        assert(f.svc->model().previewCacheFrames == 96);
        const auto t0 = std::chrono::steady_clock::now();
        auto nowMs = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() + 100000.0; };
        f.svc->pump(nowMs());
        std::string err;
        assert(f.svc->dispatchText("play", err));
        Raster r;
        double last = -1;
        int checked = 0;
        while (nowMs() < 100000.0 + 1500.0 && f.svc->model().playing)
        {
            f.svc->pump(nowMs());
            const double t = f.svc->model().playhead;
            if (t != last)
            {
                last = t;
                const auto before = f.svc->playbackStats();
                assert(f.svc->renderFrame(t, 48, r) || r.empty());
                const auto after = f.svc->playbackStats();
                if (after.fromCache > before.fromCache && after.hits > before.hits && checked < 5)
                {
                    // the frame due, from the cache: the timeline at t graded at the cache edge
                    Raster direct;
                    assert(f.svc->renderTimelineFrame("tl_1", t, f.svc->cacheEdge(), direct));
                    assert(direct.rgba == r.rgba);
                    ++checked;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const auto st = f.svc->playbackStats();
        std::printf("    played: %lld shown, %lld from the cache\n", st.shown, st.fromCache);
        assert(checked > 0 && st.fromCache > 0 && st.fromCache * 10 >= st.shown * 8);   // nearly every frame shown was decoded, not graded
        f.must("pause");
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
