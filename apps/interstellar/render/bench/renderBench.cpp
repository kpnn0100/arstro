/*
 *  interstellar_render_bench — the timing harness behind every number in NOTES.md.
 *
 *  It exists so a measurement is a command, not a claim: the first build only ever timed the
 *  identity grade (a memcpy) and reported it as the cost of grading. Every figure here is a median
 *  over repeated runs after a warm-up, on synthetic frames, Release, with the thread count printed.
 *
 *      interstellar_render_bench [composite|grade|stages|all] [--frames N]
 *
 *  `composite` times a 2-layer 1920x1080 composite three ways: the PREVIOUS algorithm (copied
 *  below verbatim apart from type names — full-raster scan, per-pixel inverse map, nearest, serial),
 *  the new one forced to one thread, and the new one on every core.
 *  `grade` times GradeEngine::render on a non-identity grade at full 1080p and at a 1280 proxy edge,
 *  plus the identity path, splits the full-resolution cost into ingest / engine / copy, and times
 *  the slot sequence (clearImages/addImage/applyParams/renderFull) it replaced, checking the two
 *  are byte-identical.
 *  `stages` times the five point stages the grade uses one by one, and for Exposure compares the
 *  PointProcessor loop (one virtual call per pixel) with a direct call and with an inlined loop —
 *  the evidence for or against per-pixel dispatch being the cost.
 *  `loop-grade-full|loop-grade-proxy|loop-slot-full|loop-slot-proxy` run N bare frames with no
 *  timing or output, for `perf stat` / `perf record`.
 */
#include "Composite.h"
#include "GradeEngine.h"
#include "base/Image.h"
#include "base/Parallel.h"
#include "color/Vibrance.h"
#include "color/WhiteBalance.h"
#include "engine/EditEngine.h"
#include "tone/Contrast.h"
#include "tone/Exposure.h"
#include "tone/ToneCurve.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace arstro::interstellar;
using namespace arstro::interstellar::render;

namespace
{
    using Clock = std::chrono::steady_clock;

    double medianMs(int runs, const std::function<void()> &fn)
    {
        fn();   // warm-up: first-touch page faults and pool start-up are not the steady state
        std::vector<double> t;
        for (int i = 0; i < runs; ++i)
        {
            const auto a = Clock::now();
            fn();
            t.push_back(std::chrono::duration<double, std::milli>(Clock::now() - a).count());
        }
        std::sort(t.begin(), t.end());
        return t[t.size() / 2];
    }

    Raster texture(int w, int h, uint32_t seed)
    {
        // A gradient plus hashed noise: realistic enough that no stage can take a flat-input shortcut.
        Raster r;
        r.allocate(w, h, 255);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                uint8_t *p = &r.rgba[((size_t)y * w + x) * 4];
                const uint32_t n = (uint32_t)(x * 73856093u ^ y * 19349663u ^ seed) * 2654435761u;
                p[0] = (uint8_t)std::min(255, 20 + x * 200 / w + (int)(n >> 28));
                p[1] = (uint8_t)std::min(255, 30 + y * 180 / h + (int)((n >> 20) & 15));
                p[2] = (uint8_t)std::min(255, 60 + ((x + y) * 120 / (w + h)) + (int)((n >> 12) & 15));
            }
        return r;
    }

    // ── The previous algorithm, from 69b91eb:apps/interstellar/core/Composite.cpp ──────────────
    // Kept only to be measured against. Types renamed; the arithmetic is untouched.
    namespace legacy
    {
        void blendPixel(Blend mode, double opacity, const uint8_t *src, uint8_t *dst)
        {
            const double a = (src[3] / 255.0) * opacity;
            if (a <= 0.0) return;
            for (int c = 0; c < 3; ++c)
            {
                const double base = dst[c] / 255.0;
                const double over = src[c] / 255.0;
                const double mixed = blendChannel(mode, base, over);
                const double v = base + (mixed - base) * a;
                dst[c] = (uint8_t)std::lround(std::min(1.0, std::max(0.0, v)) * 255.0);
            }
            const double da = dst[3] / 255.0;
            dst[3] = (uint8_t)std::lround(std::min(1.0, da + a * (1.0 - da)) * 255.0);
        }

        void placeLayer(const Layer &l, Raster &out)
        {
            if (!l.src || l.src->empty() || out.empty()) return;
            const Raster &s = *l.src;
            const Geom &g = l.geom;
            const double cx = std::max(0.0, std::min(1.0, g.cropX));
            const double cy = std::max(0.0, std::min(1.0, g.cropY));
            const double cw = std::max(1e-6, std::min(1.0 - cx, g.cropW));
            const double ch = std::max(1e-6, std::min(1.0 - cy, g.cropH));
            const double srcW = s.width * cw, srcH = s.height * ch;
            double fitScale = 1.0;
            switch (l.fit)
            {
                case Fit::Contain: fitScale = std::min(out.width / srcW, out.height / srcH); break;
                case Fit::Cover: fitScale = std::max(out.width / srcW, out.height / srcH); break;
                case Fit::None: fitScale = 1.0; break;
                case Fit::Stretch: fitScale = 0.0; break;
            }
            const double sx = l.fit == Fit::Stretch ? out.width / srcW : fitScale * g.scale;
            const double sy = l.fit == Fit::Stretch ? out.height / srcH : fitScale * g.scale;
            const double drawW = srcW * sx, drawH = srcH * sy;
            const double ax = g.anchorX * drawW, ay = g.anchorY * drawH;
            const double originX = out.width * 0.5 - ax + g.x;
            const double originY = out.height * 0.5 - ay + g.y;
            const double rot = g.rotation * 3.14159265358979324 / 180.0;
            const double cosR = std::cos(-rot), sinR = std::sin(-rot);
            for (int y = 0; y < out.height; ++y)
                for (int x = 0; x < out.width; ++x)
                {
                    double dx = x + 0.5 - (originX + ax);
                    double dy = y + 0.5 - (originY + ay);
                    double ux = dx * cosR - dy * sinR + ax;
                    double uy = dx * sinR + dy * cosR + ay;
                    if (ux < 0 || uy < 0 || ux >= drawW || uy >= drawH) continue;
                    const int spx = (int)(cx * s.width + (ux / sx));
                    const int spy = (int)(cy * s.height + (uy / sy));
                    if (spx < 0 || spy < 0 || spx >= s.width || spy >= s.height) continue;
                    const uint8_t *src = &s.rgba[((size_t)spy * s.width + spx) * 4];
                    uint8_t *dst = &out.rgba[((size_t)y * out.width + x) * 4];
                    blendPixel(l.blend, l.opacity, src, dst);
                }
        }

        void compose(const std::vector<Layer> &layers, int w, int h, Raster &out)
        {
            out.allocate(w, h, 0);
            for (const auto &l : layers) legacy::placeLayer(l, out);
        }
    }

    void benchComposite(int frames)
    {
        std::printf("\n== composite: 2 layers into 1920x1080 (median of %d) ==\n", frames);
        const Raster full = texture(1920, 1080, 1), hd720 = texture(1280, 720, 2), pip = texture(1920, 1080, 3);

        Layer pipLayer;
        pipLayer.src = &pip;
        pipLayer.fit = Fit::Contain;
        pipLayer.geom.scale = 0.4;
        pipLayer.geom.rotation = 12;
        pipLayer.geom.x = 420;
        pipLayer.geom.y = -220;
        pipLayer.opacity = 0.8;
        pipLayer.blend = Blend::Screen;

        struct Scenario { const char *name; const Raster *bottom; };
        const Scenario scenarios[] = {
            {"A  1080p bottom (copy path) + rotated 0.4x Screen PiP", &full},
            {"B  720p bottom upscaled (bilinear) + rotated 0.4x Screen PiP", &hd720},
        };
        for (const auto &sc : scenarios)
        {
            Layer bottom;
            bottom.src = sc.bottom;
            bottom.fit = Fit::Contain;
            const std::vector<Layer> layers = {bottom, pipLayer};
            Raster out;
            const int legacyRuns = std::max(3, frames / 4);
            const double tOld = medianMs(legacyRuns, [&] { legacy::compose(layers, 1920, 1080, out); });
            arstro::par::setThreads(1);
            const double tNew1 = medianMs(frames, [&] { compose(layers, 1920, 1080, out); });
            arstro::par::setThreads(0);
            const double tNewN = medianMs(frames, [&] { compose(layers, 1920, 1080, out); });
            std::printf("%s\n", sc.name);
            std::printf("    previous (nearest, full scan, serial) : %8.2f ms\n", tOld);
            std::printf("    new, 1 thread  (bilinear, bbox+span)   : %8.2f ms\n", tNew1);
            std::printf("    new, %2d threads                        : %8.2f ms\n", arstro::par::threads(), tNewN);
        }

        // Where the time goes in the new path: the PiP alone, and a 50 %% dissolve of two full frames.
        Raster out;
        out.allocate(1920, 1080, 0);
        const double tPip = medianMs(frames, [&] { placeLayer(pipLayer, out); });
        Layer a, b;
        a.src = &full;
        b.src = &pip;
        a.opacity = 0.5;
        b.opacity = 0.5;
        b.dissolveWithPrevious = true;
        const double tDis = medianMs(frames, [&] { compose({a, b}, 1920, 1080, out); });
        std::printf("    the PiP layer alone, %2d threads        : %8.2f ms\n", arstro::par::threads(), tPip);
        std::printf("    50%% dissolve, two 1080p frames        : %8.2f ms\n", tDis);
    }

    arstro::EditParams nonIdentityGrade()
    {
        arstro::EditParams p;
        p.exposure = 0.5f;
        p.contrast = 20.f;
        p.temp = 5200.f;
        p.vibrance = 15.f;
        p.curve = {arstro::CurvePoint{0.f, 0.f}, arstro::CurvePoint{0.25f, 0.19f},
                   arstro::CurvePoint{0.75f, 0.83f}, arstro::CurvePoint{1.f, 1.f}};
        return p;
    }

    /** The slot sequence the contract first named (and the previous GradeEngine used): kept here
     *  as the "before" so the switch to renderImage stays a measured decision. */
    void slotSequence(arstro::EditEngine &e, const Raster &in, const arstro::EditParams &p, int edge, Raster &out)
    {
        e.clearImages();
        const int slot = e.addImage(in.rgba.data(), in.width, in.height, 4);
        e.selectImage(slot);
        e.applyParams(p);
        arstro::PreviewBuffer b;
        if (edge > 0)
        {
            e.setPreviewSize(edge);
            e.setPreviewLevel(0);
            b = e.renderPreview();
        }
        else
            b = e.renderFull();
        out.width = b.width;
        out.height = b.height;
        out.rgba.assign(b.rgba, b.rgba + (size_t)b.width * b.height * 4);
    }

    void benchGrade(int frames)
    {
        std::printf("\n== grade: exposure 0.5, contrast 20, temp 5200, S-curve, vibrance 15 (median of %d) ==\n", frames);
        const Raster in = texture(1920, 1080, 7);
        const arstro::EditParams p = nonIdentityGrade();
        GradeEngine g;
        arstro::EditEngine slotEngine;
        slotEngine.setWantIntermediateHistograms(false, false);
        Raster out;
        for (int threads : {0, 1})
        {
            arstro::par::setThreads(threads);
            const int n = threads == 1 ? std::max(3, frames / 3) : frames;
            const double tFull = medianMs(n, [&] { g.render(in, p, true, 0, out); });
            const double tProxy = medianMs(n, [&] { g.render(in, p, true, 1280, out); });
            const double sFull = medianMs(n, [&] { slotSequence(slotEngine, in, p, 0, out); });
            const double sProxy = medianMs(n, [&] { slotSequence(slotEngine, in, p, 1280, out); });
            std::printf("threads = %d\n", arstro::par::threads());
            std::printf("    GradeEngine 1920x1080 full             : %8.2f ms/frame  (%5.1f fps)\n", tFull, 1000.0 / tFull);
            std::printf("    GradeEngine 1280 proxy edge            : %8.2f ms/frame  (%5.1f fps)\n", tProxy, 1000.0 / tProxy);
            std::printf("    slot sequence, full (renderFull)       : %8.2f ms/frame  (%5.1f fps)\n", sFull, 1000.0 / sFull);
            std::printf("    slot sequence, 1280 (renderPreview)    : %8.2f ms/frame  (%5.1f fps)\n", sProxy, 1000.0 / sProxy);
        }
        arstro::par::setThreads(0);
        const double tId = medianMs(frames, [&] { g.render(in, arstro::EditParams{}, true, 0, out); });
        std::printf("    identity (short-circuit copy)          : %8.2f ms/frame\n", tId);

        // Where a full-resolution GradeEngine frame goes: ingest (8-bit -> linear float), the
        // engine, and the copy out — timed separately with the same calls render() makes.
        arstro::EditEngine e;
        e.setWantIntermediateHistograms(false, false);
        std::vector<double> ingest, engine, copy;
        std::vector<uint8_t> sink;
        for (int i = 0; i < frames + 1; ++i)
        {
            const auto t0 = Clock::now();
            const arstro::Image lin = arstro::EditEngine::fromEncodedBytes(in.rgba.data(), in.width, in.height, 4);
            const auto t1 = Clock::now();
            const arstro::PreviewBuffer b = e.renderImage(lin, p, 1920);
            const auto t2 = Clock::now();
            sink.assign(b.rgba, b.rgba + (size_t)b.width * b.height * 4);
            const auto t3 = Clock::now();
            if (i == 0) continue;
            ingest.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            engine.push_back(std::chrono::duration<double, std::milli>(t2 - t1).count());
            copy.push_back(std::chrono::duration<double, std::milli>(t3 - t2).count());
        }
        auto med = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
        std::printf("    split of a full frame, %2d threads: ingest %.2f ms | renderImage %.2f ms | copy-out %.2f ms\n",
                    arstro::par::threads(), med(ingest), med(engine), med(copy));

        for (int edge : {0, 1280})
        {
            Raster a, b;
            g.render(in, p, true, edge, a);
            slotSequence(slotEngine, in, p, edge, b);
            std::printf("    GradeEngine vs slot sequence at edge %4d: %s\n", edge,
                        a.rgba == b.rgba && a.width == b.width ? "byte-identical" : "DIFFERENT");
        }
        const double tCtor = medianMs(5, [] { arstro::EditEngine fresh; });
        std::printf("    EditEngine construction (the cost of a render after releaseScratch): %.2f ms\n", tCtor);
    }

    /** Exposure with its kernel reachable non-virtually, to separate dispatch from arithmetic. */
    struct ExposureProbe : arstro::Exposure
    {
        void direct(const arstro::Image &in, arstro::Image &out)
        {
            out.resizeLike(in);
            const int ch = in.channels(), w = in.width(), h = in.height();
            const Pixel *s = in.data();
            Pixel *d = out.data();
            arstro::par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                {
                    const Pixel *sr = s + (size_t)y * w * ch;
                    Pixel *dr = d + (size_t)y * w * ch;
                    for (int x = 0; x < w; ++x) Exposure::processPixel(sr + x * ch, dr + x * ch, ch);
                }
            });
        }
    };

    void benchStages(int frames)
    {
        std::printf("\n== stages: one point stage at a time over a 1920x1080 linear RGBA float image (median of %d) ==\n", frames);
        const Raster in8 = texture(1920, 1080, 9);
        const arstro::Image in = arstro::EditEngine::fromEncodedBytes(in8.rgba.data(), 1920, 1080, 4);
        arstro::Image out;
        arstro::Exposure ex;
        ex.setExposureEv(0.5f);
        arstro::Contrast co;
        co.setContrast(20.f);
        arstro::WhiteBalance wb;
        wb.setTemperature(5200.f);
        arstro::ToneCurve tc;
        tc.setPoints(nonIdentityGrade().curve);
        arstro::Vibrance vb;
        vb.setVibrance(15.f);
        struct S { const char *name; arstro::ImageProcessor *p; };
        const S stages[] = {{"Exposure", &ex}, {"Contrast", &co}, {"WhiteBalance", &wb}, {"ToneCurve", &tc}, {"Vibrance", &vb}};
        for (int threads : {1, 0})
        {
            arstro::par::setThreads(threads);
            std::printf("threads = %d\n", arstro::par::threads());
            double total = 0;
            for (const auto &s : stages)
            {
                const double t = medianMs(frames, [&] { s.p->apply(in, out); });
                total += t;
                std::printf("    %-14s %7.2f ms\n", s.name, t);
            }
            std::printf("    %-14s %7.2f ms\n", "sum of five", total);

            // Exposure three ways. Same memory traffic (read one image, write one image); the only
            // difference is how the per-pixel kernel is reached.
            ExposureProbe probe;
            probe.setExposureEv(0.5f);
            probe.apply(in, out);   // resolve the parameter (snap + update) once
            const float gain = std::pow(2.0f, 0.5f);
            const double tVirtual = medianMs(frames, [&] { probe.apply(in, out); });
            const double tDirect = medianMs(frames, [&] { probe.direct(in, out); });
            const double tInline = medianMs(frames, [&] {
                out.resizeLike(in);
                const int w = in.width(), h = in.height();
                const Pixel *s = in.data();
                Pixel *d = out.data();
                arstro::par::parallelFor(h, [&](int y0, int y1) {
                    for (int y = y0; y < y1; ++y)
                    {
                        const Pixel *sr = s + (size_t)y * w * 4;
                        Pixel *dr = d + (size_t)y * w * 4;
                        for (int x = 0; x < w * 4; x += 4)
                        {
                            dr[x] = sr[x] * gain;
                            dr[x + 1] = sr[x + 1] * gain;
                            dr[x + 2] = sr[x + 2] * gain;
                            dr[x + 3] = sr[x + 3];
                        }
                    }
                });
            });
            std::printf("    Exposure via PointProcessor (virtual per pixel) %7.2f ms | direct call per pixel %7.2f ms | inlined loop %7.2f ms\n",
                        tVirtual, tDirect, tInline);
        }
        arstro::par::setThreads(0);
    }
}

namespace
{
    /** Bare loops for `perf stat` / `perf record`: no timing, no printing, just the work. */
    void perfLoop(const std::string &which, int frames)
    {
        const Raster in = texture(1920, 1080, 7);
        const arstro::EditParams p = nonIdentityGrade();
        if (which == "loop-grade-full" || which == "loop-grade-proxy")
        {
            GradeEngine g;
            Raster out;
            const int edge = which == "loop-grade-full" ? 0 : 1280;
            for (int i = 0; i < frames; ++i) g.render(in, p, true, edge, out);
            return;
        }
        if (which == "loop-slot-full" || which == "loop-slot-proxy")
        {
            arstro::EditEngine e;
            e.setWantIntermediateHistograms(false, false);
            Raster out;
            const int edge = which == "loop-slot-full" ? 0 : 1280;
            for (int i = 0; i < frames; ++i) slotSequence(e, in, p, edge, out);
        }
    }
}

int main(int argc, char **argv)
{
    std::string what = "all";
    int frames = 21;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) frames = std::max(1, std::atoi(argv[++i]));
        else what = a;
    }
    std::printf("interstellar_render_bench: hardware threads = %d, Pixel = %s\n", arstro::par::threads(),
                sizeof(Pixel) == 4 ? "float" : "double");
    if (what == "composite" || what == "all") benchComposite(frames);
    if (what == "grade" || what == "all") benchGrade(frames);
    if (what == "stages" || what == "all") benchStages(frames);
    if (what.rfind("loop-", 0) == 0) perfLoop(what, frames);
    return 0;
}
