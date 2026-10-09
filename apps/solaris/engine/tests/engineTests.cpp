// solaris_engine tests — the mix, measured on rendered samples (R-MIX-1/4/5/6/11, R-DSP-5,
// R-RENDER-1, R-PLAY-3).
//
// A constant-valued "sample" makes a mix law readable to the bit: a 0.5 region through a strip
// panned +0.5 must come out at exactly 0.5·cos(π/4) on the left and 0.5 on the right.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../Engine.h"
#include "../MixLaws.h"
#include "device/Device.h"
#include <atomic>
#include <cassert>
#include <algorithm>
#include <cstdlib>
#include <new>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace arstro::solaris::engine;

// A counting allocator for this binary: off except inside the window a test opens.
static std::atomic<long> gAllocs{0};
static std::atomic<bool> gCounting{false};
void *operator new(size_t n)
{
    if (gCounting.load(std::memory_order_relaxed)) gAllocs.fetch_add(1, std::memory_order_relaxed);
    if (void *p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }

namespace
{
    int passed = 0;
    void pass(const char *name)
    {
        std::printf("[PASS] %s\n", name);
        ++passed;
    }
    bool contains(const std::string &s, const std::string &part) { return s.find(part) != std::string::npos; }

    std::shared_ptr<const Pcm> constant(float v, long long frames, int channels = 2)
    {
        auto p = std::make_shared<Pcm>();
        p->channels = channels;
        p->frames = frames;
        p->samples.assign((size_t)(frames * channels), v);
        return p;
    }
    std::shared_ptr<const Pcm> ramp(long long frames)
    {
        auto p = std::make_shared<Pcm>();
        p->channels = 1;
        p->frames = frames;
        for (long long i = 0; i < frames; ++i) p->samples.push_back((float)i / (float)frames);
        return p;
    }

    // Sources: strip 0 (audio) → strip 1 (bus "Main") → master → port 0 "Main"; port 1 "Phones".
    MixGraph basic()
    {
        MixGraph g;
        g.sampleRate = 48000;
        g.ports = {Port{"prt_1", "Main", 2}, Port{"prt_2", "Phones", 2}};
        g.masterPorts = {0};
        Strip a;
        a.id = "ch_2";
        a.kind = Strip::Audio;
        a.out = Target{Target::Strip, 1};
        Strip main;
        main.id = "ch_1";
        main.kind = Strip::Bus;
        main.out = Target{Target::Master, -1};
        g.strips = {a, main};
        return g;
    }

    PortBuffers renderAll(Engine &e, int frames, int chunk)
    {
        PortBuffers all, part;
        all.ports.resize(e.graph().ports.size());
        for (size_t p = 0; p < all.ports.size(); ++p) all.ports[p].assign(e.graph().ports[p].channels, std::vector<float>());
        for (int done = 0; done < frames; done += chunk)
        {
            const int n = std::min(chunk, frames - done);
            e.render(n, part);
            for (size_t p = 0; p < all.ports.size(); ++p)
                for (size_t c = 0; c < all.ports[p].size(); ++c)
                    all.ports[p][c].insert(all.ports[p][c].end(), part.ports[p][c].begin(), part.ports[p][c].end());
        }
        return all;
    }
}

static void test_a_region_plays_exactly_where_it_is()
{
    MixGraph g = basic();
    Region r;
    r.pcm = constant(0.5f, 1000);
    r.start = 100;
    r.frames = 1000;
    r.srcFrames = 1000;
    g.strips[0].regions = {r};
    Engine e;
    std::string err;
    assert(e.build(g, err));
    PortBuffers out;
    e.render(1500, out);
    const auto &L = out.ports[0][0], &R = out.ports[0][1];
    assert(L[99] == 0.0f && L[100] == 0.5f && L[1099] == 0.5f && L[1100] == 0.0f);
    assert(R[500] == 0.5f);
    assert(out.ports[1][0][500] == 0.0f);                          // Phones gets nothing: nothing routes there
    assert(e.position() == 1500);
    pass("a region sounds from its first sample to its last, through strip → Main → master → port");
}

static void test_pan_and_fade_laws_are_interstellars()
{
    MixGraph g = basic();
    Region r;
    r.pcm = constant(0.5f, 48000);
    r.frames = 1000;
    r.srcFrames = 48000;
    r.fadeIn = 100;
    r.fadeOut = 200;
    g.strips[0].regions = {r};
    g.strips[0].pan = 0.5;
    g.strips[0].gain = dbToLinear(-6.0);
    Engine e;
    std::string err;
    assert(e.build(g, err));
    PortBuffers out;
    e.render(1000, out);
    const double k = dbToLinear(-6.0);
    // (M2) balance: the right (panned toward) is unity, the left falls on a quarter cosine
    assert(std::fabs(out.ports[0][1][500] - 0.5 * k) < 1e-7);
    assert(std::fabs(out.ports[0][0][500] - 0.5 * k * std::cos(0.5 * M_PI / 2)) < 1e-7);
    // (M3) linear-amplitude fades
    assert(std::fabs(out.ports[0][1][50] - 0.5 * k * 0.5) < 1e-7);
    assert(std::fabs(out.ports[0][1][900] - 0.5 * k * 0.5) < 1e-7);  // 100 samples from the end of a 200-sample fade
    assert(out.ports[0][1][0] == 0.0f);
    double gl, gr;
    balancePan(0.0, gl, gr);
    assert(gl == 1.0 && gr == 1.0);                                  // unity at centre, not −3 dB
    balancePan(-1.0, gl, gr);
    assert(gl == 1.0 && std::fabs(gr) < 1e-12);
    pass("pan and fades follow Interstellar's laws exactly: balance, unity at centre; linear amplitude (R-MIX-11)");
}

static void test_loops_offsets_and_offline_media()
{
    MixGraph g = basic();
    Region r;
    r.pcm = ramp(100);
    r.frames = 350;
    r.srcOffset = 0;
    r.srcFrames = 100;
    r.loop = true;
    Region gone;                                                   // offline media: silent, not fatal
    gone.start = 0;
    gone.frames = 350;
    gone.srcFrames = 350;
    Region off;                                                    // a source range that starts 40 frames in
    off.pcm = ramp(100);
    off.start = 400;
    off.frames = 100;
    off.srcOffset = 40;
    off.srcFrames = 60;
    g.strips[0].regions = {r, gone, off};
    Engine e;
    std::string err;
    assert(e.build(g, err));
    PortBuffers out;
    e.render(600, out);
    const auto &L = out.ports[0][0];
    assert(L[0] == 0.0f && L[100] == 0.0f && L[250] == L[50] && L[349] == L[49]);
    assert(L[350] == 0.0f);
    assert(std::fabs(L[400] - 0.40f) < 1e-7 && std::fabs(L[459] - 0.99f) < 1e-7);
    assert(L[460] == 0.0f);                                        // past the source span: silence
    pass("a looped region repeats its span; an offset reads from inside the file; offline media is silent");
}

static void test_mute_sends_and_ports()
{
    MixGraph g = basic();
    Region r;
    r.pcm = constant(0.25f, 1000);
    r.frames = 1000;
    r.srcFrames = 1000;
    g.strips[0].regions = {r};
    g.strips[0].gain = 0.0;                                        // the fader all the way down…
    g.strips[0].sends = {Send{Target{Target::Strip, 1}, 1.0, true},     // …a PRE send still feeds Main
                         Send{Target{Target::Port, 1}, 1.0, false}};    // a POST send to Phones gets nothing
    Engine e;
    std::string err;
    assert(e.build(g, err));
    PortBuffers out;
    e.render(500, out);
    assert(out.ports[0][0][10] == 0.25f);
    assert(out.ports[1][0][10] == 0.0f);
    // the post send at unity with the fader up, straight to a port (a headphone cue)
    g.strips[0].gain = 1.0;
    g.strips[0].sends = {Send{Target{Target::Port, 1}, dbToLinear(-6.0), false}};
    assert(e.build(g, err));
    e.render(500, out);
    assert(std::fabs(out.ports[1][0][10] - 0.25 * dbToLinear(-6.0)) < 1e-7 && out.ports[0][0][10] == 0.25f);
    // a silent strip (muted, or silenced by a solo) sends nothing anywhere
    g.strips[0].silent = true;
    assert(e.build(g, err));
    e.render(500, out);
    assert(out.ports[0][0][10] == 0.0f && out.ports[1][0][10] == 0.0f);
    // a mono port gets the average of L and R; master gain scales the master
    g.strips[0].silent = false;
    g.strips[0].sends.clear();
    g.strips[0].pan = 1.0;
    g.ports[0].channels = 1;
    g.masterGain = 0.5;
    assert(e.build(g, err));
    e.render(500, out);
    assert(out.ports[0].size() == 1 && std::fabs(out.ports[0][0][10] - 0.5 * (0.0 + 0.25) * 0.5) < 1e-7);
    pass("mute silences everything; a pre send survives the fader, a post send does not; a send can go straight to a port");
}

static void test_a_backward_graph_and_unknown_devices_are_refused()
{
    std::string err;
    Engine e;
    MixGraph g = basic();
    g.strips[1].out = Target{Target::Strip, 0};
    assert(!e.build(g, err) && contains(err, "not LATER") && contains(err, "R-MIX-4"));
    g = basic();
    g.strips[0].sends = {Send{Target{Target::Port, 7}, 1, false}};
    assert(!e.build(g, err) && contains(err, "port index 7"));
    g = basic();
    g.masterPorts = {3};
    assert(!e.build(g, err) && contains(err, "master feeds port index 3"));
    g = basic();
    g.strips[0].rack = {DeviceDesc{"dv_1", "flanger", {}, false}};
    assert(!e.build(g, err) && contains(err, "unknown device type `flanger`"));
    g.strips[0].rack = {DeviceDesc{"dv_1", "eq", {{"peak9.gain", 3.0}}, false}};
    assert(!e.build(g, err) && contains(err, "no parameter `peak9.gain`"));
    g = basic();
    g.strips[0].kind = Strip::Instrument;
    g.strips[0].rack = {DeviceDesc{"dv_1", "eq", {}, false}};
    assert(!e.build(g, err) && contains(err, "no instrument first"));
    pass("a backward route, a missing port, an unknown device or parameter are refused at build");
}

static void test_notes_start_on_their_exact_sample()
{
    auto renderNoteAt = [](long long at) {
        MixGraph g = basic();
        g.strips[0].kind = Strip::Instrument;
        g.strips[0].rack = {DeviceDesc{"dv_1", "synth", {{"amp.attack", 0.0}}, false}};
        g.strips[0].notes = {NoteEvent{at, 60, 120, true}, NoteEvent{at + 9000, 60, 0, false}};
        Engine e;
        std::string err;
        assert(e.build(g, err));
        PortBuffers out;
        e.render(20000, out);
        return out.ports[0][0];
    };
    const auto a = renderNoteAt(1000), b = renderNoteAt(1001);
    for (int t = 0; t < 1000; ++t) assert(a[t] == 0.0f);
    bool sounding = false;
    for (int t = 1000; t < 1100; ++t) sounding = sounding || a[t] != 0.0f;
    assert(sounding);
    // a note one sample later is the same sound one sample later — to the bit (R-DSP-5)
    for (int t = 0; t + 1 < 20000; ++t) assert(b[t + 1] == a[t]);
    pass("a note starts on its exact sample: moved by one sample, the render shifts by exactly one sample (R-DSP-5)");
}

static void test_render_is_deterministic_however_it_is_chopped()
{
    auto graph = [] {
        MixGraph g = basic();
        Strip drums;
        drums.id = "ch_3";
        drums.kind = Strip::Instrument;
        drums.rack = {DeviceDesc{"dv_1", "drums", {}, false}, DeviceDesc{"dv_2", "compressor", {{"threshold", -24.0}}, false}};
        for (int k = 0; k < 8; ++k)
            drums.notes.push_back(NoteEvent{k * 6000LL, k % 2 ? 38 : 36, 110, true});
        drums.out = Target{Target::Strip, 2};
        Strip synth;
        synth.id = "ch_4";
        synth.kind = Strip::Instrument;
        synth.rack = {DeviceDesc{"dv_3", "synth", {{"osc1.voices", 3.0}, {"noise", 0.2}}, false}, DeviceDesc{"dv_4", "chorus", {}, false}};
        synth.notes = {NoteEvent{3000, 57, 90, true}, NoteEvent{30000, 57, 0, false}};
        synth.sends = {Send{Target{Target::Strip, 3}, 0.5, false}};
        synth.out = Target{Target::Strip, 2};
        Strip verb;                                                // a reverb bus on the later mixer
        verb.id = "ch_5";
        verb.kind = Strip::Bus;
        verb.rack = {DeviceDesc{"dv_5", "reverb", {{"mix", 1.0}}, false}};
        verb.out = Target{Target::Master, -1};
        Strip mainBus = g.strips[1];
        g.strips = {g.strips[0], drums, synth, mainBus, verb};
        // re-point: the drums and the synth feed Main (index 3), the synth sends to the reverb (4)
        g.strips[1].out = Target{Target::Strip, 3};
        g.strips[2].out = Target{Target::Strip, 3};
        g.strips[2].sends = {Send{Target{Target::Strip, 4}, 0.5, false}};
        g.strips[0].out = Target{Target::Strip, 3};
        g.masterRack = {DeviceDesc{"dv_6", "eq", {{"lowcut.on", 1.0}}, false}};
        return g;
    };
    Engine a, b;
    std::string err;
    assert(a.build(graph(), err));
    assert(b.build(graph(), err));
    const auto x = renderAll(a, 48000, 128), y = renderAll(b, 48000, 77);
    assert(x.ports[0][0] == y.ports[0][0] && x.ports[0][1] == y.ports[0][1]);
    float peak = 0;
    for (float v : x.ports[0][0]) peak = std::max(peak, std::fabs(v));
    assert(peak > 0.05f);
    pass("deterministic: two engines, chunks of 128 and of 77, byte-identical over a second of drums+synth+reverb (R-RENDER-1)");
}

static void test_meters_seek_capture_and_live_params()
{
    MixGraph g = basic();
    Region r;
    r.pcm = constant(0.5f, 48000);
    r.frames = 48000;
    r.srcFrames = 48000;
    g.strips[0].regions = {r};
    g.strips[0].rack = {DeviceDesc{"dv_1", "eq", {}, false}};
    Engine e;
    std::string err;
    assert(e.build(g, err));
    e.captureStrips({0, 1});
    e.captureMaster(true);
    PortBuffers out;
    e.render(1000, out);
    const Meter &m = e.stripMeters()[0];
    assert(std::fabs(m.peak[0] - 0.5f) < 1e-6 && std::fabs(m.rms[1] - 0.5f) < 1e-5 && m.maxPeak[0] >= m.peak[0]);
    assert(std::fabs(e.masterMeter().peak[0] - 0.5f) < 1e-6);
    assert(e.captured(0).size() == 2 && e.captured(0)[0][500] == out.ports[0][0][500]);
    assert(e.captured(1)[1][500] == out.ports[0][1][500] && e.capturedMaster()[0][500] == out.ports[0][0][500]);
    e.clearPeaks();
    assert(e.stripMeters()[0].maxPeak[0] == 0.0f);
    assert(e.setDeviceParam(0, 0, "peak2.gain", 6.0));
    assert(!e.setDeviceParam(0, 0, "nope", 1.0) && !e.setDeviceParam(0, 5, "peak2.gain", 1.0) && !e.setDeviceParam(9, 0, "x", 1));
    assert(!e.setMasterDeviceParam(0, "x", 1));
    e.seek(-5);
    assert(e.position() == 0);
    e.seek(40000);
    e.render(100, out);
    assert(e.position() == 40100 && out.ports[0][0][50] != 0.0f);
    pass("meters report peak and RMS; seek moves; a strip can be captured as a stem; params change live");
}

static void test_live_render_allocates_nothing_and_takes_live_edits()
{
    MixGraph g = basic();
    Strip drums;
    drums.id = "ch_3";
    drums.kind = Strip::Instrument;
    drums.rack = {DeviceDesc{"dv_1", "drums", {}, false}, DeviceDesc{"dv_2", "reverb", {}, false}};
    for (int k = 0; k < 16; ++k) drums.notes.push_back(NoteEvent{k * 3000LL, 36 + (k % 3) * 2, 110, true});
    drums.out = Target{Target::Strip, 2};
    Strip synth;
    synth.id = "ch_4";
    synth.kind = Strip::Instrument;
    synth.rack = {DeviceDesc{"dv_3", "synth", {{"noise", 0.3}}, false}, DeviceDesc{"dv_9", "limiter", {}, false}};
    synth.notes = {NoteEvent{100, 60, 100, true}, NoteEvent{20000, 60, 0, false}};
    synth.out = Target{Target::Strip, 2};
    g.strips = {g.strips[0], drums, synth, g.strips[1]};
    g.masterRack = {DeviceDesc{"dv_10", "limiter", {}, false}}; // R-MIX-17: delay lines on the live path too
    g.strips[0].out = Target{Target::Strip, 3};
    g.strips[1].out = Target{Target::Strip, 3};
    g.strips[2].out = Target{Target::Strip, 3};
    Engine e;
    std::string err;
    assert(e.build(g, err));
    PortBuffers out;
    e.prepare(out, 256);
    gAllocs = 0;
    gCounting = true;
    for (int k = 0; k < 200; ++k) e.render(256, out);  // a second of playback in device-sized blocks
    e.setStripGain(1, 0.5);
    e.setStripPan(2, -0.5);
    e.setStripSilent(0, true);
    e.setDeviceBypass(1, 1, true);
    e.setDeviceBypass(2, 1, true);                          // a bypassed limiter runs its delay instead
    e.setMasterGain(0.8);
    e.setDeviceParam(2, 0, "filter.cutoff", 900.0);
    for (int k = 0; k < 20; ++k) e.render(256, out);
    gCounting = false;
    if (gAllocs.load()) std::printf("    %ld allocations on the live path\n", gAllocs.load());
    assert(gAllocs.load() == 0);
    // the edits took: silence the drums and the synth, and the port goes quiet
    e.setStripSilent(1, true);
    e.setStripSilent(2, true);
    e.render(256, out);
    e.render(256, out);
    float peak = 0;
    for (float v : out.ports[0][0]) peak = std::max(peak, std::fabs(v));
    assert(peak < 1e-3f);                                   // only the reverb bypassed… and the strips silenced
    pass("live: after prepare, rendering and every live edit allocate nothing (R-PLAY-2)");
}

static void test_bindings_drive_gain_and_parameters_at_control_rate()
{
    // a quiet constant (below the compressor's threshold, so it only applies its makeup) through a
    // strip whose gain follows a curve 0 → −20 dB over 4 beats, into Main whose compressor's makeup
    // a formula steps to +12 dB from beat 2: =12 * clamp(floor(beat / 2), 0, 1)
    auto op = [](ExprOp::Kind k, double num = 0, int index = 0, ExprOp::Func fn = ExprOp::Sin) {
        ExprOp o;
        o.kind = k;
        o.num = num;
        o.index = index;
        o.fn = fn;
        return o;
    };
    auto graph = [&]() {
        MixGraph g = basic();
        Region r;
        r.pcm = constant(0.05f, 96000);
        r.frames = r.srcFrames = 96000;
        g.strips[0].regions = {r};
        g.strips[1].rack = {DeviceDesc{"dv_1", "compressor", {}, false}};
        g.clock = Clock{24000, 4, 120, 48000};
        Curve c;
        CurvePoint a0, a1;
        a1.t = 96000;
        a1.v = -20.0;
        c.keys = curveKeys({a0, a1});
        g.curves = {c};
        Bind gain;
        gain.kind = Bind::StripGain;
        gain.strip = 0;
        gain.lo = -120;
        gain.hi = 12;
        gain.expr.ops = {op(ExprOp::Var, 0, kClockSlots)};
        Bind mk;
        mk.kind = Bind::DeviceParam;
        mk.strip = 1;
        mk.index = 0;
        mk.param = arstro::DeviceRegistry::find("compressor")->paramIndex("makeup");
        mk.lo = 0;
        mk.hi = 24;
        mk.expr.ops = {op(ExprOp::Num, 12), op(ExprOp::Var, 0, 0), op(ExprOp::Num, 2), op(ExprOp::Div), op(ExprOp::Fn, 0, 0, ExprOp::Floor),
                       op(ExprOp::Num, 0), op(ExprOp::Num, 1), op(ExprOp::Fn, 0, 0, ExprOp::Clamp), op(ExprOp::Mul)};
        g.binds = {gain, mk};
        return g;
    };
    Engine a, b, c;
    std::string err;
    assert(a.build(graph(), err) && b.build(graph(), err) && c.build(graph(), err));
    const PortBuffers ra = renderAll(a, 96000, 77), rb = renderAll(b, 96000, 128), rc = renderAll(c, 96000, 1000);
    assert(ra.ports[0][0] == rb.ports[0][0] && rb.ports[0][0] == rc.ports[0][0]); // however time is chopped (R-AUTO-7)
    auto dbAt = [&](long long s) { return 20.0 * std::log10(ra.ports[0][0][(size_t)s] / 0.05); };
    if (std::fabs(dbAt(24000) + 5.0) > 0.1 || std::fabs(dbAt(72000) + 3.0) > 0.2)
        std::printf("    beat 1: %.3f dB (want −5), beat 3: %.3f dB (want −15 + 12)\n", dbAt(24000), dbAt(72000));
    assert(std::fabs(dbAt(0)) < 0.1);
    assert(std::fabs(dbAt(24000) + 5.0) < 0.1);           // a quarter of the way down the curve
    assert(std::fabs(dbAt(72000) + 3.0) < 0.2);           // three quarters, plus the makeup the formula stepped in
    assert(a.bindValues().size() == 2 && a.bindValues()[1] == 12.0);
    // no zipper: the gain moves by at most one control period's share of the curve per sample
    double worst = 0;
    for (size_t i = 1; i < 40000; ++i) worst = std::max(worst, std::fabs((double)ra.ports[0][0][i] - ra.ports[0][0][i - 1]));
    assert(worst < 1e-5);
    // and evaluating allocates nothing once prepared (R-PLAY-2)
    Engine d;
    assert(d.build(graph(), err));
    PortBuffers out;
    d.prepare(out, 256);
    gAllocs = 0;
    gCounting = true;
    for (int k = 0; k < 100; ++k) d.render(256, out);
    gCounting = false;
    assert(gAllocs.load() == 0);
    pass("bindings: a curve drives a gain and a formula a device parameter, every 64 samples, ramped, byte-identical however chopped (R-AUTO-7)");
}

static void test_curves_are_interstellars_keyframes()
{
    // R-AUTO-10 (law 16): a curve IS Interstellar's keyframe model, evaluated by its Anim.h. Linear and
    // hold are exactly what they were; smooth is Ease, whose Bézier is exactly the smoothstep R-AUTO-4
    // drew; a bezier segment is the cubic of After Effects' handles — checked here against the formula
    // solved independently (Newton on the time component), not against Anim.h itself.
    auto curve = [](std::vector<CurvePoint> pts, double lo = -HUGE_VAL, double hi = HUGE_VAL) {
        Curve c;
        c.keys = curveKeys(pts);
        c.lo = lo;
        c.hi = hi;
        return c;
    };
    auto pt = [](double t, double v, CurvePoint::Shape s) {
        CurvePoint p;
        p.t = t;
        p.v = v;
        p.shape = s;
        return p;
    };
    const Curve lin = curve({pt(0, 0, CurvePoint::Linear), pt(1000, 10, CurvePoint::Linear)});
    const Curve hold = curve({pt(0, 3, CurvePoint::Hold), pt(1000, 10, CurvePoint::Linear)});
    const Curve smooth = curve({pt(0, 0, CurvePoint::Smooth), pt(1000, 10, CurvePoint::Linear)});
    for (long long s : {-5LL, 0LL, 1LL, 250LL, 333LL, 500LL, 999LL, 1000LL, 4000LL})
    {
        const double f = std::clamp((double)s / 1000.0, 0.0, 1.0);
        assert(lin.valueAt(s) == 0.0 + (10.0 - 0.0) * f);            // bit for bit the old lerp
        assert(hold.valueAt(s) == (s >= 1000 ? 10.0 : 3.0));
        assert(std::fabs(smooth.valueAt(s) - 10.0 * f * f * (3.0 - 2.0 * f)) < 1e-9);
    }
    // a bezier point between two linear ones: (0, 0) → (1000, 10, out 0.03/sample at 60 %) → (3000, 0)
    CurvePoint b = pt(1000, 10, CurvePoint::Bezier);
    b.speedIn = 0.02;
    b.inflIn = 50.0;
    b.speedOut = 0.03;
    b.inflOut = 60.0;
    const Curve bz = curve({pt(0, 0, CurvePoint::Linear), b, pt(3000, 0, CurvePoint::Linear)});
    auto cubic = [](double tA, double vA, double soA, double ioA, double tB, double vB, double siB, double iiB, double t) {
        // P0…P3 of the segment, After Effects' handles (a linear side: the segment's slope at ⅓)
        const double dt = tB - tA;
        const double x1 = tA + ioA * dt, y1 = vA + soA * ioA * dt, x2 = tB - iiB * dt, y2 = vB - siB * iiB * dt;
        auto bez = [](double p0, double p1, double p2, double p3, double u) {
            const double m = 1 - u;
            return m * m * m * p0 + 3 * m * m * u * p1 + 3 * m * u * u * p2 + u * u * u * p3;
        };
        double u = (t - tA) / dt;
        for (int i = 0; i < 60; ++i)
        {
            const double m = 1 - u, x = bez(tA, x1, x2, tB, u);
            const double dx = 3 * m * m * (x1 - tA) + 6 * m * u * (x2 - x1) + 3 * u * u * (tB - x2);
            u = std::clamp(u - (x - t) / dx, 0.0, 1.0);
        }
        return bez(vA, y1, y2, vB, u);
    };
    bool bends = false;
    for (long long s : {100LL, 400LL, 700LL, 950LL, 1050LL, 1500LL, 2200LL, 2900LL})
    {
        const double want = s < 1000 ? cubic(0, 0, 10.0 / 1000.0, 1.0 / 3.0, 1000, 10, 0.02, 0.5, (double)s)
                                     : cubic(1000, 10, 0.03, 0.6, 3000, 0, -10.0 / 2000.0, 1.0 / 3.0, (double)s);
        if (std::fabs(bz.valueAt(s) - want) > 1e-9) std::printf("    sample %lld: %.12f, the formula %.12f\n", s, bz.valueAt(s), want);
        assert(std::fabs(bz.valueAt(s) - want) < 1e-9);
        const double straight = s < 1000 ? 10.0 * s / 1000.0 : 10.0 - 10.0 * (s - 1000) / 2000.0;
        bends |= std::fabs(want - straight) > 0.5;                       // a curve, not the line
    }
    assert(bends);
    // a handle's overshoot stops at the automation's range
    const Curve capped = curve({pt(0, 0, CurvePoint::Linear), b, pt(3000, 0, CurvePoint::Linear)}, 0.0, 10.0);
    assert(bz.valueAt(1100) > 10.0 && capped.valueAt(1100) == 10.0);
    // evaluating allocates nothing (the audio thread calls it every 64 samples)
    gAllocs = 0;
    gCounting = true;
    double sink = 0;
    for (long long s = 0; s < 3000; s += 64) sink += bz.valueAt(s) + smooth.valueAt(s);
    gCounting = false;
    assert(gAllocs.load() == 0 && std::isfinite(sink));
    pass("curves are Interstellar's keyframes: linear and hold exact, smooth = Ease = smoothstep, bezier = the handles' cubic to 1e-9, clamped to the range, no allocation (R-AUTO-10)");
}

// R-MIX-17: a click and a quiet tone (under every limiter's ceiling, so a limiter is EXACTLY its delay),
// on the left only or the right only — two strips summed into one bus stay told apart, bit for bit.
static std::shared_ptr<const Pcm> clickTone(bool left, bool right, long long frames = 6000)
{
    auto p = std::make_shared<Pcm>();
    p->channels = 2;
    p->frames = frames;
    for (long long n = 0; n < frames; ++n)
    {
        const float v = n == 0 ? 0.5f : (float)(0.25 * std::sin(0.03 * (double)n));
        p->samples.push_back(left ? v : 0.0f);
        p->samples.push_back(right ? v : 0.0f);
    }
    return p;
}

static Region regionOf(std::shared_ptr<const Pcm> pcm, long long start)
{
    Region r;
    r.pcm = pcm;
    r.start = start;
    r.frames = pcm->frames;
    r.srcFrames = pcm->frames;
    return r;
}

static void test_latency_is_compensated_everywhere_signals_meet()
{
    // Twins: ch_a plays the sound on the LEFT through a limiter (96 samples of lookahead at 48 kHz),
    // ch_b the same sound on the RIGHT with nothing; both into the bus Main → master → port. With the
    // twin delayed to match, Main's left and right are the same samples — the click at 1000 + 96.
    auto twins = [](bool viaSends) {
        MixGraph g;
        g.sampleRate = 48000;
        g.ports = {Port{"prt_1", "Main", 2}, Port{"prt_2", "Phones", 2}};
        g.masterPorts = {0};
        Strip a, b, main;
        a.id = "ch_a";
        a.regions = {regionOf(clickTone(true, false), 1000)};
        a.rack = {DeviceDesc{"dv_1", "limiter", {}, false}};
        b.id = "ch_b";
        b.regions = {regionOf(clickTone(false, true), 1000)};
        main.id = "ch_main";
        main.kind = Strip::Bus;
        main.out = Target{Target::Master, -1};
        if (viaSends)
        {
            a.out = b.out = Target{Target::None, -1};
            a.sends = b.sends = {Send{Target{Target::Strip, 2}, 1.0, false}};
        }
        else a.out = b.out = Target{Target::Strip, 2};
        g.strips = {a, b, main};
        return g;
    };
    auto aligned = [](const PortBuffers &o, long long at) {
        const auto &L = o.ports[0][0], &R = o.ports[0][1];
        bool same = L == R, before = true;
        for (long long i = 0; i < at; ++i) before &= L[(size_t)i] == 0.0f;
        return same && before && L[(size_t)at] == 0.5f;
    };
    std::string err;
    for (bool viaSends : {false, true})
    {
        Engine e;
        assert(e.build(twins(viaSends), err));
        assert(e.outputLatency() == 96 && e.stripLatency(0) == 96 && e.stripLatency(1) == 0 && e.stripLatency(2) == 96);
        const auto o = renderAll(e, 6000, 128);
        assert(aligned(o, 1000 + 96));
    }
    // the same however time is chopped
    {
        Engine x, y;
        assert(x.build(twins(false), err) && y.build(twins(false), err));
        const auto ox = renderAll(x, 6000, 128), oy = renderAll(y, 6000, 77);
        assert(ox.ports[0][0] == oy.ports[0][0] && ox.ports[0][1] == oy.ports[0][1]);
    }
    // bypassed, the limiter still takes its 96 samples: the timing never jumps with a bypass
    {
        MixGraph g = twins(false);
        g.strips[0].rack[0].bypass = true;
        Engine e;
        assert(e.build(g, err) && e.outputLatency() == 96);
        assert(aligned(renderAll(e, 6000, 128), 1000 + 96));
    }
    // a master limiter: everything out is 96 late — a strip sent straight to a port (a cue) too, so the
    // ports meet; the render trims outputLatency() and a beat-0 hit lands on sample 0 (serviceTests)
    {
        MixGraph g = basic();
        g.strips[0].regions = {regionOf(clickTone(true, true), 0)};
        g.strips[0].sends = {Send{Target{Target::Port, 1}, 1.0, false}};
        g.masterRack = {DeviceDesc{"dv_m", "limiter", {}, false}};
        Engine e;
        assert(e.build(g, err) && e.outputLatency() == 96 && e.stripLatency(0) == 0);
        e.captureMaster(true);
        PortBuffers o;
        e.render(1000, o);
        assert(o.ports[0][0][95] == 0.0f && o.ports[0][0][96] == 0.5f);         // Main, through the master
        assert(o.ports[1][0][95] == 0.0f && o.ports[1][0][96] == 0.5f);         // Phones, straight from the strip
        assert(e.capturedMaster()[0][96] == 0.5f && o.ports[0][0] == o.ports[1][0]);
    }
    // a key: K keys S's compressor. A limiter on K delays S's audio to meet its key (S's output exactly
    // 96 later than with none); a limiter in S's own rack BEFORE the compressor delays the key to meet
    // the audio there (the same 96)
    {
        auto keyed = [](bool limitKey, bool limitBefore) {
            MixGraph g = basic();
            auto burst = std::make_shared<Pcm>();
            burst->channels = 1;
            burst->frames = 2000;
            for (long long n = 0; n < 2000; ++n) burst->samples.push_back(n % 500 < 60 ? 0.8f : 0.0f);
            Strip k, s;
            k.id = "ch_k";
            k.regions = {regionOf(burst, 2000)};
            k.out = Target{Target::None, -1};
            if (limitKey) k.rack = {DeviceDesc{"dv_l", "limiter", {{"ceiling", 0.0}}, false}};
            k.sends = {Send{Target{Target::Strip, 1}, 1.0, false, true}};
            s.id = "ch_s";
            s.regions = {regionOf(clickTone(true, true, 8000), 0)};
            s.rack = {DeviceDesc{"dv_c", "compressor", {{"sidechain", 1.0}, {"threshold", -30.0}, {"ratio", 8.0}, {"attack", 0.5}}, false}};
            if (limitBefore) s.rack.insert(s.rack.begin(), DeviceDesc{"dv_p", "limiter", {}, false});
            s.out = Target{Target::Strip, 2};
            g.strips = {k, s, g.strips[1]};
            return g;
        };
        auto strip = [&](const MixGraph &g) {
            Engine e;
            assert(e.build(g, err));
            e.captureStrips({1});
            std::vector<float> out;
            PortBuffers o;
            for (int done = 0; done < 8000; done += 128)
            {
                e.render(128, o);
                out.insert(out.end(), e.captured(0)[0].begin(), e.captured(0)[0].end());
            }
            return std::make_pair(out, e.stripLatency(1));
        };
        const auto plain = strip(keyed(false, false)), late = strip(keyed(true, false)), behind = strip(keyed(false, true));
        assert(plain.second == 0 && late.second == 96 && behind.second == 96);
        bool ducks = false, shifted = true;
        for (size_t i = 0; i + 96 < plain.first.size(); ++i)
        {
            shifted &= late.first[i + 96] == plain.first[i] && behind.first[i + 96] == plain.first[i];
            if (i > 2100 && i < 2400) ducks |= std::fabs(plain.first[i]) < 0.5 * 0.25 && std::fabs(plain.first[i]) > 0.0;
        }
        assert(shifted && ducks); // and the key does duck: the test is not one of two silences
    }
    pass("latency compensated (R-MIX-17): a limiter's twin is delayed to meet it at a bus, by output or by send, bypassed or not, chunked any way; the ports meet; keys meet the audio");
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_a_region_plays_exactly_where_it_is();
    test_pan_and_fade_laws_are_interstellars();
    test_loops_offsets_and_offline_media();
    test_mute_sends_and_ports();
    test_a_backward_graph_and_unknown_devices_are_refused();
    test_notes_start_on_their_exact_sample();
    test_render_is_deterministic_however_it_is_chopped();
    test_meters_seek_capture_and_live_params();
    test_live_render_allocates_nothing_and_takes_live_edits();
    test_bindings_drive_gain_and_parameters_at_control_rate();
    test_curves_are_interstellars_keyframes();
    test_latency_is_compensated_everywhere_signals_meet();
    std::printf("\n%d passed, 0 failed\n", passed);
    return 0;
}
