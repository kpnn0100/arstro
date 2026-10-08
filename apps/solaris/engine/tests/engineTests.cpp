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
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace arstro::solaris::engine;

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
    e.captureStrip(0);
    PortBuffers out;
    e.render(1000, out);
    const Meter &m = e.stripMeters()[0];
    assert(std::fabs(m.peak[0] - 0.5f) < 1e-6 && std::fabs(m.rms[1] - 0.5f) < 1e-5 && m.maxPeak[0] >= m.peak[0]);
    assert(std::fabs(e.masterMeter().peak[0] - 0.5f) < 1e-6);
    assert(e.captured().size() == 2 && e.captured()[0][500] == out.ports[0][0][500]);
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
    std::printf("\n%d passed, 0 failed\n", passed);
    return 0;
}
