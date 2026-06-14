/*
 *  Native integration test: drive ScopeApp into a RecordingTarget and assert it
 *  links DSP + Artboard correctly — audio renders and a waveform is drawn.
 *  (CTest target `scope_smoke`; no browser/audio device needed.)
 */
#include "ScopeApp.h"
#include "../../Artboard/include/artboard/artboard.h"
#include <cstdio>
#include <vector>
#include <cmath>

using namespace arstro::examples;
using artboard::RecordingTarget;
using artboard::DrawOp;

static int gFail = 0;
static void check(bool ok, const char *what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++gFail;
}

int main()
{
    ScopeApp app(640, 360);
    app.setSampleRate(48000);
    app.noteOn(69, 0.9); // A4

    // Render some audio so the scope ring fills with a non-trivial signal.
    const int frames = 256, ch = 2;
    std::vector<float> buf(frames * ch);
    bool finite = true, nonzero = false;
    for (int b = 0; b < 8; ++b)
    {
        app.renderAudio(buf.data(), frames);
        for (float s : buf) { if (!std::isfinite(s)) finite = false; if (std::fabs(s) > 1e-4) nonzero = true; }
    }
    check(finite, "audio render finite");
    check(nonzero, "synth produced sound");
    check(app.waveformPoints() == 640, "ring spans the board width");

    // Draw two frames into a recorder; assert the scene was emitted.
    RecordingTarget t;
    app.render(t, 0.0);
    t.clear();
    app.render(t, 1000.0); // mid-sweep
    check(t.count(DrawOp::Kind::DrawText) >= 1, "title text drawn");
    check(t.count(DrawOp::Kind::LineTo) > 100, "waveform polyline drawn");
    check(t.count(DrawOp::Kind::StrokePath) >= 2, "waveform + playhead stroked");
    check(t.count(DrawOp::Kind::FillPath) >= 1, "background filled");

    std::printf("\n%s\n", gFail == 0 ? "scope_smoke OK" : "scope_smoke FAILED");
    return gFail == 0 ? 0 : 1;
}
