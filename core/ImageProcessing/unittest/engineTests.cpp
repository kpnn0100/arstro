/*
 *  Unit tests for the EditEngine facade: image slots, the flat parameter API,
 *  preview vs full render, and histogram access. (main() is in imageTests.cpp.)
 */
#include "MiniTest.h"
#include "image_processing.h"
#include "compute/GlesComputeBackend.h"  // createGlesComputeAccelerator (ARSTRO_GLES_COMPUTE)
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using namespace arstro;

// Helper: a solid gamma-sRGB RGBA8 buffer (what the app's decoder hands in).
static std::vector<uint8_t> solidRGBA8(int w, int h, uint8_t v)
{
    std::vector<uint8_t> b((size_t)w * h * 4);
    for (size_t i = 0; i < b.size(); i += 4)
    {
        b[i] = v; b[i + 1] = v; b[i + 2] = v; b[i + 3] = 255;
    }
    return b;
}

TEST(Engine_no_image_is_safe)
{
    EditEngine eng;
    CHECK(eng.imageCount() == 0);
    CHECK(!eng.hasImage());
    eng.setExposure(2.0f);  // no-op without an image
    eng.resetAll();
    eng.selectImage(5);     // out of range -> no-op
    PreviewBuffer pb = eng.renderPreview();
    CHECK(pb.rgba == nullptr && pb.width == 0);
    PreviewBuffer pf = eng.renderFull();
    CHECK(pf.rgba == nullptr);

    CHECK(eng.addImage(nullptr, 4, 4) < 0);  // invalid input
}

TEST(Engine_identity_roundtrip)
{
    EditEngine eng;
    auto bytes = solidRGBA8(4, 4, 128);
    int slot = eng.addImage(bytes.data(), 4, 4, 4);
    CHECK(slot == 0);
    CHECK(eng.currentSlot() == 0 && eng.hasImage());

    PreviewBuffer pb = eng.renderPreview();
    CHECK(pb.width == 4 && pb.height == 4);
    // identity edit: 128 in -> ~128 out (decode->linear->encode round trip)
    CHECK(std::abs((int)pb.rgba[0] - 128) <= 1);
    CHECK(std::abs((int)pb.rgba[3] - 255) <= 1);

    // render again -> exercises the scratch-buffer reuse path
    PreviewBuffer pb2 = eng.renderPreview();
    CHECK(pb2.width == 4);
}

TEST(Engine_exposure_brightens)
{
    EditEngine eng;
    auto bytes = solidRGBA8(8, 8, 128);
    eng.addImage(bytes.data(), 8, 8, 4);
    eng.setExposure(1.0f);
    PreviewBuffer pb = eng.renderPreview();
    CHECK((int)pb.rgba[0] > 150);  // +1 EV pushes mid-gray well up

    eng.resetAll();
    pb = eng.renderPreview();
    CHECK(std::abs((int)pb.rgba[0] - 128) <= 1);  // back to identity
}

TEST(Engine_contrast_and_bypass)
{
    EditEngine eng;
    auto bytes = solidRGBA8(4, 4, 200);
    eng.addImage(bytes.data(), 4, 4, 4);

    eng.setContrast(80.0f);
    PreviewBuffer pb = eng.renderPreview();
    int contrasted = pb.rgba[0];

    eng.setBypass(true);
    pb = eng.renderPreview();
    CHECK(std::abs((int)pb.rgba[0] - 200) <= 1);  // bypass shows the original

    eng.setBypass(false);
    pb = eng.renderPreview();
    CHECK((int)pb.rgba[0] == contrasted);  // re-applies
}

TEST(Engine_histogram_after_render)
{
    EditEngine eng;
    auto bytes = solidRGBA8(4, 4, 128);
    eng.addImage(bytes.data(), 4, 4, 4);
    eng.renderPreview();
    const HistogramData &h = eng.histogram();
    uint32_t sum = 0;
    for (int i = 0; i < HistogramData::kBins; ++i) sum += h.r[i];
    CHECK(sum == 16);  // 4x4 pixels
}

// The pre-curve / pre-mixer histogram taps reflect the image ENTERING those stages:
// a downstream change must not move them; an upstream change must.
static std::vector<uint8_t> variedRGBA8b(int w, int h)
{
    std::vector<uint8_t> b((size_t)w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            uint8_t *p = b.data() + ((size_t)y * w + x) * 4;
            p[0] = (uint8_t)(20 + x * 200 / (w - 1)); p[1] = (uint8_t)(200 - y * 180 / (h - 1)); p[2] = 90; p[3] = 255;
        }
    return b;
}
TEST(Engine_pre_stage_histogram_taps)
{
    EditEngine eng;
    auto bytes = variedRGBA8b(20, 20);
    eng.addImage(bytes.data(), 20, 20, 4);
    eng.setPreviewSize(4096);
    eng.renderPreview();
    const HistogramData preCurve0 = eng.preCurveHistogram();
    const HueHistogram preMixer0 = eng.preMixerHue();

    // the colour mixer is DOWNSTREAM of both taps -> neither moves
    eng.setMixerCurve(EditEngine::MixerHue, {{0.f, 1.f}, {180.f, 1.f}, {359.f, 1.f}});
    eng.renderPreview();
    CHECK(eng.preMixerHue().bins == preMixer0.bins);
    CHECK(eng.preCurveHistogram().lum == preCurve0.lum);
    // the tone curve is DOWNSTREAM of the pre-curve tap -> the luma tap stays put
    eng.setCurvePoints({{0.f, 1.f}, {1.f, 0.f}});  // invert
    eng.renderPreview();
    CHECK(eng.preCurveHistogram().lum == preCurve0.lum);

    // but an UPSTREAM change (exposure) moves the pre-curve luma tap
    eng.resetAll();
    eng.renderPreview();
    const HistogramData base = eng.preCurveHistogram();
    eng.setExposure(2.0f);
    eng.renderPreview();
    CHECK(!(eng.preCurveHistogram().lum == base.lum));
}

TEST(Engine_multi_image_independent_params)
{
    EditEngine eng;
    auto a = solidRGBA8(4, 4, 128);
    auto b = solidRGBA8(4, 4, 128);
    int s0 = eng.addImage(a.data(), 4, 4, 4);
    int s1 = eng.addImage(b.data(), 4, 4, 4);
    CHECK(eng.imageCount() == 2);
    CHECK(s0 == 0 && s1 == 1);

    eng.selectImage(s1);
    eng.setExposure(1.0f);
    PreviewBuffer pb = eng.renderPreview();
    int bright = pb.rgba[0];
    CHECK(bright > 150);

    eng.selectImage(s0);  // slot 0 still default
    pb = eng.renderPreview();
    CHECK(std::abs((int)pb.rgba[0] - 128) <= 1);

    eng.selectImage(s1);  // slot 1 keeps its exposure
    pb = eng.renderPreview();
    CHECK((int)pb.rgba[0] == bright);
}

TEST(Engine_release_image_frees_and_keeps_indices_stable)
{
    EditEngine eng;
    auto a = solidRGBA8(4, 4, 64);
    auto b = solidRGBA8(4, 4, 200);
    int s0 = eng.addImage(a.data(), 4, 4, 4);
    int s1 = eng.addImage(b.data(), 4, 4, 4);
    CHECK(s0 == 0 && s1 == 1);
    CHECK(eng.currentSlot() == s0);  // the first-added image auto-selects

    eng.releaseImage(s0);
    CHECK(eng.imageCount() == 2);    // the index stays valid; nothing shifts
    CHECK(eng.currentSlot() == -1);  // the released slot was current -> now none

    eng.selectImage(s0);             // a released slot can never be selected again
    CHECK(eng.currentSlot() == -1);

    eng.selectImage(s1);             // the other slot is untouched by the release
    CHECK(eng.currentSlot() == s1);
    PreviewBuffer pb = eng.renderPreview();
    CHECK(pb.rgba != nullptr && (int)pb.rgba[0] > 150);

    eng.releaseImage(999);           // out of range -> no-op, no crash
}

TEST(Engine_preview_downscale_and_full)
{
    EditEngine eng;
    auto bytes = solidRGBA8(100, 50, 128);
    eng.addImage(bytes.data(), 100, 50, 4);

    eng.setPreviewSize(10);
    PreviewBuffer pv = eng.renderPreview();
    CHECK(pv.width == 10 && pv.height == 5);  // long edge capped at 10

    eng.setPreviewSize(0);  // clamps to >= 1
    PreviewBuffer full = eng.renderFull();
    CHECK(full.width == 100 && full.height == 50);  // full path is unscaled
}

// A varied test image: brightness gradient (x) + colour variation (y), so every
// tonal region and hue band is exercised (a flat gray would make many ops no-op).
static std::vector<uint8_t> variedRGBA8(int w, int h)
{
    std::vector<uint8_t> b((size_t)w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            uint8_t *p = b.data() + ((size_t)y * w + x) * 4;
            p[0] = (uint8_t)(30 + x * 210 / (w > 1 ? w - 1 : 1));   // dark..bright
            p[1] = (uint8_t)(30 + y * 190 / (h > 1 ? h - 1 : 1));   // colour variation
            p[2] = 120;
            p[3] = 255;
        }
    return b;
}

TEST(Engine_full_catalog_setters_route)
{
    EditEngine eng;
    auto bytes = variedRGBA8(16, 16);
    eng.addImage(bytes.data(), 16, 16, 4);
    PreviewBuffer base = eng.renderPreview();
    const std::vector<uint8_t> b0(base.rgba, base.rgba + (size_t)base.width * base.height * 4);

    // Each setter should change the rendered output; resetAll restores it.
    auto changes = [&](auto fn) {
        eng.resetAll();
        fn();
        PreviewBuffer pb = eng.renderPreview();
        std::vector<uint8_t> cur(pb.rgba, pb.rgba + (size_t)pb.width * pb.height * 4);
        return cur != b0;
    };

    CHECK(changes([&] { eng.setShadows(80.f); }));
    CHECK(changes([&] { eng.setHighlights(-80.f); }));
    CHECK(changes([&] { eng.setTemperature(9000.f); }));
    CHECK(changes([&] { eng.setVibrance(100.f); eng.setSaturation(100.f); }));
    CHECK(changes([&] { eng.setDehaze(100.f); }));
    CHECK(changes([&] { eng.setGrainAmount(100.f); eng.setGrainSize(30.f); }));
    CHECK(changes([&] { eng.setCurvePoints({{0.f, 0.2f}, {1.f, 1.f}}); }));
    CHECK(changes([&] { eng.setMixerCurve(EditEngine::MixerSat, {{0.f, -1.f}, {180.f, -1.f}}); }));
    CHECK(changes([&] { eng.setGradeSaturation(EditEngine::Shadows, 100.f);
                        eng.setGradeHue(EditEngine::Shadows, 30.f); }));
    CHECK(changes([&] { eng.setTexture(100.f); }));
    CHECK(changes([&] { eng.setClarity(100.f); }));
    CHECK(changes([&] { eng.setSharpenAmount(120.f); eng.setSharpenRadius(1.5f); }));
    CHECK(changes([&] { eng.setNoiseColor(100.f); }));
    CHECK(changes([&] { eng.setNoiseLuminance(100.f); }));
    CHECK(changes([&] { eng.setLensVignette(-100.f); }));
    CHECK(changes([&] { eng.setLensDistortion(100.f); }));
    CHECK(changes([&] { eng.setLensCA(100.f); }));

    eng.resetAll();
    PreviewBuffer back = eng.renderPreview();
    std::vector<uint8_t> backBytes(back.rgba, back.rgba + (size_t)back.width * back.height * 4);
    CHECK(backBytes == b0);  // resetAll restores the original render exactly
}

TEST(Engine_transform_changes_dimensions)
{
    EditEngine eng;
    auto bytes = solidRGBA8(40, 20, 128);
    eng.addImage(bytes.data(), 40, 20, 4);
    eng.setPreviewSize(4096);  // no downscale for this small image

    eng.setCrop(0.f, 0.f, 0.5f, 1.f);
    PreviewBuffer cropped = eng.renderFull();
    CHECK(cropped.width == 20 && cropped.height == 20);

    eng.resetCrop();
    eng.setQuarterTurns(1);
    PreviewBuffer turned = eng.renderFull();
    CHECK(turned.width == 20 && turned.height == 40);  // dims swap
}

TEST(Engine_every_setter_runs)
{
    EditEngine eng;
    auto bytes = variedRGBA8(12, 12);
    eng.addImage(bytes.data(), 12, 12, 4);

    eng.setExposure(0.5f); eng.setContrast(20.f);
    eng.setHighlights(-30.f); eng.setShadows(30.f); eng.setWhites(10.f); eng.setBlacks(-10.f);
    eng.setTemperature(7200.f); eng.setTint(20.f);
    eng.setVibrance(40.f); eng.setSaturation(15.f);
    eng.setDehaze(25.f); eng.setGrainAmount(20.f); eng.setGrainSize(40.f);
    eng.setCurvePoints({{0.f, 0.05f}, {0.5f, 0.55f}, {1.f, 1.f}}); eng.setCurveLogScale(false);
    eng.setCurveLogScale(true);
    eng.setMixerCurve(EditEngine::MixerHue, {{0.f, 0.1f}, {120.f, 0.f}, {240.f, -0.1f}});
    eng.setMixerCurve(EditEngine::MixerSat, {{0.f, -0.1f}, {180.f, 0.1f}});
    eng.setMixerCurve(EditEngine::MixerLum, {{0.f, 0.05f}, {180.f, -0.05f}});
    for (int r = 0; r < 3; ++r)
    {
        eng.setGradeHue((EditEngine::GradeRegion)r, 30.f * r);
        eng.setGradeSaturation((EditEngine::GradeRegion)r, 20.f);
        eng.setGradeLuminance((EditEngine::GradeRegion)r, 5.f);
    }
    eng.setGradeBalance(15.f);
    eng.setHueRemapEnabled(true);
    eng.setHueRemap(20.f, 30.f, 50.f, 0.8f);
    eng.setCrop(0.05f, 0.05f, 0.9f, 0.9f);
    eng.setRotation(2.5f);
    eng.setQuarterTurns(1);

    PreviewBuffer pv = eng.renderPreview();
    CHECK(pv.rgba != nullptr && pv.width > 0);
    PreviewBuffer full = eng.renderFull();
    CHECK(full.rgba != nullptr && full.width > 0);
}

TEST(Engine_rgb_input)
{
    EditEngine eng;
    std::vector<uint8_t> rgb((size_t)2 * 2 * 3, 128);
    int slot = eng.addImage(rgb.data(), 2, 2, 3);
    CHECK(slot == 0);
    PreviewBuffer pb = eng.renderPreview();
    CHECK(pb.width == 2);
    CHECK((int)pb.rgba[3] == 255);  // synthesized opaque alpha
}

// ── EditParams: the UI-independent edit description ──
TEST(EditParams_roundtrip)
{
    EditEngine eng;
    auto bytes = solidRGBA8(8, 8, 128);
    eng.addImage(bytes.data(), 8, 8, 4);

    EditParams p;
    p.exposure = 0.7f; p.contrast = 15.f; p.temp = 7000.f;
    p.curve = {{0.f, 0.1f}, {1.f, 1.f}};
    p.mixer[1] = {{0.f, 0.5f}, {180.f, 0.5f}};
    p.grade[0] = {30.f, 40.f, 5.f};
    p.cropW = 0.8f; p.rotation = 2.f;
    eng.setCurrentParams(p);

    const EditParams &q = eng.currentParams();
    CHECK_NEAR(q.exposure, 0.7, 1e-6);
    CHECK_NEAR(q.temp, 7000.0, 1e-3);
    CHECK(q.grade[0].hue == 30.f);
    CHECK_NEAR(q.cropW, 0.8, 1e-6);
    PreviewBuffer pb = eng.renderPreview();  // cropW 0.8 of 8px -> width ~6
    CHECK(pb.rgba != nullptr && pb.width > 0);
}

// ── renderImage: the seam a video editor reuses (no slot machinery) ──
TEST(Engine_renderImage_reuse)
{
    EditEngine eng;
    std::vector<uint8_t> bytes((size_t)100 * 50 * 4, 128);
    Image img = EditEngine::fromEncodedBytes(bytes.data(), 100, 50, 4);

    EditParams p; p.exposure = 1.0f;
    PreviewBuffer pb = eng.renderImage(img, p, 40);  // long edge -> 40
    CHECK(pb.width == 40 && pb.height == 20);
    CHECK((int)pb.rgba[0] > 150);  // +1 EV brightens mid-gray

    EditParams id;  // identity, on the same engine, no slot selected
    PreviewBuffer pb2 = eng.renderImage(img, id, 40);
    CHECK(std::abs((int)pb2.rgba[0] - 128) <= 2);
}

// ── hardware acceleration: parallel output == serial output ──
TEST(Parallel_matches_serial)
{
    EditEngine eng;
    auto bytes = variedRGBA8(80, 60);
    Image img = EditEngine::fromEncodedBytes(bytes.data(), 80, 60, 4);
    EditParams p; p.exposure = 0.5f; p.contrast = 20.f; p.vibrance = 30.f;

    par::setThreads(1);
    PreviewBuffer a = eng.renderImage(img, p, 80);
    std::vector<uint8_t> serial(a.rgba, a.rgba + (size_t)a.width * a.height * 4);
    par::setThreads(8);
    PreviewBuffer b = eng.renderImage(img, p, 80);
    std::vector<uint8_t> parallel(b.rgba, b.rgba + (size_t)b.width * b.height * 4);
    par::setThreads(0);  // back to auto

    CHECK(serial == parallel);  // row-parallel must be byte-identical to serial
}

// ── RenderService: engine on its own thread; UI submits + polls, never blocks ──
TEST(RenderService_async_and_full)
{
    RenderService svc;
    auto bytes = variedRGBA8(40, 30);
    CHECK(svc.addImage(bytes.data(), 40, 30, 4) == 0);
    CHECK(svc.addImage(bytes.data(), 40, 30, 4) == 1);  // sequential slot ids

    EditParams p; p.exposure = 1.0f;
    svc.setPreviewSize(64);
    svc.render(0, p);

    RenderService::Frame f;
    bool got = false;
    for (int i = 0; i < 400 && !got; ++i)  // poll up to ~2s; render is async
    {
        if (svc.tryAcquire(f)) got = true;
        else std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(got);
    CHECK(f.width > 0 && f.height > 0);
    CHECK((int)f.rgba.size() == f.width * f.height * 4);

    RenderService::Frame full;
    CHECK(svc.renderFull(0, p, full));   // blocking full-res
    CHECK(full.width == 40 && full.height == 30);
    // destructor joins the worker cleanly (test would hang otherwise)
}

// ── EditParams serialization round-trip (session save/load) ──
TEST(EditParamsIO_roundtrip)
{
    EditParams p;
    p.exposure = 0.8f; p.contrast = -25.f; p.shadows = 40.f; p.temp = 7200.f; p.vibrance = 33.f;
    p.dehaze = 50.f; p.grainAmount = 12.f; p.grainSize = 60.f;
    p.texture = 22.f; p.clarity = -18.f;
    p.sharpenAmount = 80.f; p.sharpenRadius = 1.4f; p.sharpenMasking = 35.f;
    p.nrLuminance = 25.f; p.nrColor = 40.f;
    p.lensDistortion = -12.f; p.lensCA = 30.f; p.lensVignette = -45.f;
    p.curve = {{0.f, 0.05f}, {0.5f, 0.6f}, {1.f, 0.95f}};
    p.curveLog = false;
    p.mixer[0] = {{0.f, 0.3f}, {180.f, -0.2f}};
    p.mixer[2] = {{120.f, 0.5f}};
    p.grade[1] = {210.f, 35.f, -8.f};
    p.balance = 15.f;
    p.remapEnable = true; p.remapSrc = 12.f; p.remapRange = 44.f; p.remapDst = 200.f; p.remapStrength = 0.7f;
    p.cropX = 0.1f; p.cropY = 0.05f; p.cropW = 0.8f; p.cropH = 0.9f;
    p.rotation = -3.5f; p.quarterTurns = 3;

    const std::string text = serializeParams(p);
    EditParams q;
    CHECK(deserializeParams(text, q));

    CHECK_NEAR(q.exposure, 0.8, 1e-4);
    CHECK_NEAR(q.contrast, -25.0, 1e-4);
    CHECK_NEAR(q.temp, 7200.0, 1e-2);
    CHECK_NEAR(q.dehaze, 50.0, 1e-4);
    CHECK(q.curve.size() == 3);
    CHECK_NEAR(q.curve[1].x, 0.5, 1e-4);
    CHECK_NEAR(q.curve[1].y, 0.6, 1e-4);
    CHECK(!q.curveLog);
    CHECK(q.mixer[0].size() == 2 && q.mixer[2].size() == 1);
    CHECK_NEAR(q.grade[1].hue, 210.0, 1e-3);
    CHECK_NEAR(q.grade[1].lum, -8.0, 1e-4);
    CHECK(q.remapEnable);
    CHECK_NEAR(q.remapStrength, 0.7, 1e-4);
    CHECK_NEAR(q.cropW, 0.8, 1e-4);
    CHECK(q.quarterTurns == 3);
    CHECK_NEAR(q.texture, 22.0, 1e-4);
    CHECK_NEAR(q.clarity, -18.0, 1e-4);
    CHECK_NEAR(q.sharpenAmount, 80.0, 1e-4);
    CHECK_NEAR(q.sharpenRadius, 1.4, 1e-4);
    CHECK_NEAR(q.nrColor, 40.0, 1e-4);
    CHECK_NEAR(q.lensCA, 30.0, 1e-4);
    CHECK_NEAR(q.lensVignette, -45.0, 1e-4);

    // empty/garbage tolerated -> defaults kept
    EditParams d;
    deserializeParams("nonsense\n=bad\nfoo=1\n", d);
    CHECK_NEAR(d.exposure, 0.0, 1e-9);
    CHECK_NEAR(d.temp, 6500.0, 1e-3);
}

// A mixer curve persists its BEZIER CONTROL points (not a sampled polyline): a
// smooth point's tangent handles + smooth flag survive the round-trip, and the
// control-point count is unchanged (no explosion into samples, no collapse to
// corners). This is the "save by bezier point" fix.
TEST(EditParamsIO_mixer_bezier_roundtrip)
{
    EditParams p;
    CurvePoint a; a.x = 0.f;   a.y = 0.f;                          // corner
    CurvePoint m; m.x = 120.f; m.y = 0.5f; m.smooth = true;        // smooth w/ handles
                  m.ix = -40.f; m.iy = 0.2f; m.ox = 40.f; m.oy = -0.1f;
    CurvePoint z; z.x = 240.f; z.y = -0.3f;                        // corner
    p.mixer[0] = {a, m, z};

    EditParams q;
    CHECK(deserializeParams(serializeParams(p), q));

    CHECK(q.mixer[0].size() == 3);          // control points preserved, not resampled
    CHECK(!q.mixer[0][0].smooth);
    CHECK(!q.mixer[0][2].smooth);
    const CurvePoint &qm = q.mixer[0][1];
    CHECK(qm.smooth);
    CHECK_NEAR(qm.x, 120.0, 1e-4);
    CHECK_NEAR(qm.y, 0.5, 1e-4);
    CHECK_NEAR(qm.ix, -40.0, 1e-4);
    CHECK_NEAR(qm.iy, 0.2, 1e-4);
    CHECK_NEAR(qm.ox, 40.0, 1e-4);
    CHECK_NEAR(qm.oy, -0.1, 1e-4);

    // a bare "x,y" (pre-bezier / corner) token still parses -> corner point
    EditParams legacy;
    CHECK(deserializeParams("mixer0=0,0.1;180,-0.1\n", legacy));
    CHECK(legacy.mixer[0].size() == 2);
    CHECK(!legacy.mixer[0][0].smooth);
    CHECK_NEAR(legacy.mixer[0][1].x, 180.0, 1e-4);
    CHECK_NEAR(legacy.mixer[0][1].y, -0.1, 1e-4);
}

TEST(EditParamsIO_curve_channel_roundtrip)
{
    EditParams p;
    p.curve = {{0.f, 0.f}, {0.4f, 0.6f}, {1.f, 1.f}};      // RGB master
    p.curveChannel[0] = {{0.f, 0.f}, {0.5f, 0.9f}, {1.f, 1.f}};  // R
    p.curveChannel[2] = {{0.f, 0.1f}, {1.f, 0.95f}};             // B

    EditParams q;
    CHECK(deserializeParams(serializeParams(p), q));
    CHECK(q.curve.size() == 3);
    CHECK(q.curveChannel[0].size() == 3);
    CHECK_NEAR(q.curveChannel[0][1].x, 0.5, 1e-4);
    CHECK_NEAR(q.curveChannel[0][1].y, 0.9, 1e-4);
    CHECK(q.curveChannel[1].size() == 2);                 // G left as default identity, round-trips
    CHECK_NEAR(q.curveChannel[2][0].y, 0.1, 1e-4);

    // A legacy file without curveR/G/B keys leaves the channels at their identity default.
    EditParams legacy;
    CHECK(deserializeParams("curve=0,0;1,1\n", legacy));
    CHECK(legacy.curveChannel[0].size() == 2);
    CHECK(legacy.curveChannel[0][0].x == 0.f && legacy.curveChannel[0][1].x == 1.f);
}

// ── composeParams: group settings stacked onto a member (cosmo group tree) ──
TEST(ComposeParams_additive_scalars)
{
    EditParams member;  member.exposure = 0.5f; member.contrast = 10.f; member.temp = 5000.f;
    EditParams group;   group.exposure = 1.0f;  group.contrast = 5.f;   group.temp = 8000.f;  // +1500 K warm

    EditParams e = composeParams(member, group);
    CHECK_NEAR(e.exposure, 1.5, 1e-5);          // 0.5 + 1.0  (the headline example)
    CHECK_NEAR(e.contrast, 15.0, 1e-4);         // 10 + 5
    CHECK_NEAR(e.temp, 6500.0, 1e-3);           // 5000 + (8000 - 6500)

    // A neutral group is a no-op.
    EditParams neutral;
    EditParams same = composeParams(member, neutral);
    CHECK_NEAR(same.exposure, 0.5, 1e-6);
    CHECK_NEAR(same.temp, 5000.0, 1e-3);
    CHECK(same.curve == member.curve);
}

TEST(ComposeParams_recursive_fold_and_curve_masks)
{
    // Nested groups fold: member + g1 + g2 sums exposure (recursive stacking).
    EditParams member; member.exposure = 0.25f;
    EditParams g1; g1.exposure = 1.0f;
    EditParams g2; g2.exposure = 0.5f;
    EditParams e = composeParams(composeParams(member, g1), g2);
    CHECK_NEAR(e.exposure, 1.75, 1e-5);

    // Tone curve stacks additively in Y: identity member + a mid-lift group -> lifted mid.
    EditParams m2;  // identity curve
    EditParams grp; grp.curve = {{0.f, 0.f}, {0.5f, 0.7f}, {1.f, 1.f}};
    EditParams ec = composeParams(m2, grp);
    CHECK(ec.curve.size() == 33);               // resampled for render
    CHECK_NEAR(ec.curve[16].x, 0.5, 1e-4);
    CHECK_NEAR(ec.curve[16].y, 0.7, 1e-3);      // 0.5 + (0.7 - 0.5)

    // With BOTH curves non-identity the final is the SUM of the two curves' adjustments:
    //   final(x) = item(x) + group(x) - x. Item lifts mid to 0.6 (+0.1), group to 0.7 (+0.2)
    //   -> final(0.5) = 0.6 + 0.7 - 0.5 = 0.8 (the two lifts add). (NOT literal 0.6+0.7,
    //   which would double the identity, and NOT composition group(item(0.5))=0.76.)
    EditParams itemC; itemC.curve = {{0.f, 0.f}, {0.5f, 0.6f}, {1.f, 1.f}};
    EditParams grpC;  grpC.curve = {{0.f, 0.f}, {0.5f, 0.7f}, {1.f, 1.f}};
    CHECK_NEAR(composeParams(itemC, grpC).curve[16].y, 0.8, 2e-3);

    // Masks concatenate: group masks apply to every member.
    EditParams mm; MaskParams a; a.type = MaskParams::Radial; mm.masks = {a};
    EditParams gg; MaskParams b; b.type = MaskParams::Linear; gg.masks = {b};
    EditParams em = composeParams(mm, gg);
    CHECK(em.masks.size() == 2);

    // Crop is NOT stacked -- framing stays per-item.
    EditParams mc; mc.cropX = 0.1f; mc.cropY = 0.1f; mc.cropW = 0.8f; mc.cropH = 0.8f;
    EditParams gc; gc.cropX = 0.f; gc.cropY = 0.f; gc.cropW = 0.5f; gc.cropH = 0.5f;
    EditParams ex = composeParams(mc, gc);
    CHECK_NEAR(ex.cropW, 0.8, 1e-6);            // member's crop kept, group's ignored
    CHECK_NEAR(ex.cropX, 0.1, 1e-6);
}

TEST(EditEngine_per_channel_tone_curve)
{
    std::vector<uint8_t> g((size_t)4 * 4 * 4, 0);          // solid mid-grey RGBA8
    for (int i = 0; i < 16; ++i) { g[i * 4] = 128; g[i * 4 + 1] = 128; g[i * 4 + 2] = 128; g[i * 4 + 3] = 255; }

    EditEngine eng; eng.setComputeAccelerator(nullptr);   // CPU reference path
    eng.addImage(g.data(), 4, 4, 4); eng.selectImage(0);
    eng.setCurveChannelPoints(0, {{0.f, 0.f}, {0.5f, 0.9f}, {1.f, 1.f}});  // lift ONLY red

    PreviewBuffer b = eng.renderFull();
    const uint8_t *px = b.rgba;                            // RGBA8
    CHECK(px[0] > px[1] + 40);   // R lifted well above G
    CHECK(px[1] == px[2]);       // G == B, untouched
}

// ── .apf generic preset envelope + selective image mapping ──
TEST(Apf_selective_save_and_apply)
{
    EditParams p;
    p.exposure = 1.2f; p.contrast = 30.f; p.temp = 7200.f; p.vibrance = 40.f;
    p.curve = {{0.f, 0.05f}, {1.f, 0.95f}};
    p.curveChannel[0] = {{0.f, 0.f}, {0.5f, 0.9f}, {1.f, 1.f}};  // a per-channel R curve rides in "curve"
    p.grade[1] = {210.f, 25.f, -6.f};
    MaskParams m; m.type = MaskParams::Radial; m.adjust.exposure = 0.8f; p.masks = {m};

    // save ONLY basic + curve
    apf::Document doc = editParamsToApf(p, {"basic", "curve"}, "Look A");
    CHECK(doc.engine == std::string("image"));
    CHECK(doc.name == std::string("Look A"));
    CHECK(doc.has("basic") && doc.has("curve"));
    CHECK(!doc.has("color") && !doc.has("masks"));  // unticked categories omitted

    // round-trip the envelope through text
    apf::Document rt;
    CHECK(apf::parse(apf::serialize(doc), rt));
    CHECK(rt.engine == std::string("image"));
    auto present = apfPresentImageCategories(rt);
    CHECK(present.size() == 2 && present[0] == std::string("basic"));

    // apply only "basic" onto a fresh params -> basic changes, curve untouched (default)
    EditParams out;  // defaults
    CHECK(applyApfToEditParams(rt, {"basic"}, out));
    CHECK_NEAR(out.exposure, 1.2, 1e-4);
    CHECK_NEAR(out.contrast, 30.0, 1e-4);
    CHECK(out.curve.size() == 2 && out.curve[1].y == 1.f);  // curve NOT applied (default identity)
    CHECK_NEAR(out.temp, 6500.0, 1e-3);                          // color not in the file at all

    // apply "curve" too -> master + per-channel curves now change
    CHECK(applyApfToEditParams(rt, {"basic", "curve"}, out));
    CHECK(out.curve.size() == 2);
    CHECK_NEAR(out.curve[0].y, 0.05, 1e-4);
    CHECK(out.curveChannel[0].size() == 3);            // R curve carried in the "curve" category
    CHECK_NEAR(out.curveChannel[0][1].y, 0.9, 1e-4);
    CHECK(out.curveChannel[1].size() == 2);            // G/B stay identity

    // a full save carries masks + grade; apply restores them
    apf::Document full = editParamsToApf(p, apfImageCategories(), "Full");
    EditParams out2;
    CHECK(applyApfToEditParams(full, apfImageCategories(), out2));
    CHECK(out2.masks.size() == 1);
    CHECK_NEAR(out2.masks[0].adjust.exposure, 0.8, 1e-4);
    CHECK_NEAR(out2.grade[1].hue, 210.0, 1e-3);
}

TEST(Apf_rejects_other_engine)
{
    apf::Document audio; audio.engine = "audio"; audio.category("basic").set("exposure", "1.0");
    EditParams out;
    CHECK(!applyApfToEditParams(audio, {"basic"}, out));  // cross-engine rejected
    CHECK_NEAR(out.exposure, 0.0, 1e-9);                  // nothing applied
    // a non-apf blob fails to parse
    apf::Document d;
    CHECK(!apf::parse("not a preset\nx=1\n", d));
}

// ── Local adjustments (masks) ──
TEST(MaskCoverage_shapes)
{
    MaskParams radial;  // centred ellipse, default feather
    radial.type = MaskParams::Radial; radial.cx = 0.5f; radial.cy = 0.5f; radial.rx = 0.3f; radial.ry = 0.3f;
    CHECK(maskCoverage(radial, 0.5f, 0.5f) > 0.99f);   // centre fully covered
    CHECK(maskCoverage(radial, 0.95f, 0.5f) < 0.01f);  // far outside not covered
    MaskParams inv = radial; inv.inverted = true;
    CHECK(maskCoverage(inv, 0.5f, 0.5f) < 0.01f);      // inversion flips it

    MaskParams lin;  // vertical gradient 0 at y0=0.3 -> 1 at y1=0.7
    lin.type = MaskParams::Linear; lin.x0 = 0.5f; lin.y0 = 0.3f; lin.x1 = 0.5f; lin.y1 = 0.7f;
    CHECK(maskCoverage(lin, 0.5f, 0.2f) < 0.01f);
    CHECK(maskCoverage(lin, 0.5f, 0.8f) > 0.99f);
    CHECK(maskCoverage(lin, 0.5f, 0.5f) > 0.3f && maskCoverage(lin, 0.5f, 0.5f) < 0.7f);

    MaskParams brush;
    brush.type = MaskParams::Brush; brush.feather = 0.5f;
    brush.dabs.push_back({0.25f, 0.25f, 0.1f, 1.f});
    CHECK(maskCoverage(brush, 0.25f, 0.25f) > 0.9f);   // dab centre
    CHECK(maskCoverage(brush, 0.75f, 0.75f) < 0.01f);  // away from any dab
}

TEST(MaskStack_blends_local_adjustment)
{
    EditEngine eng;
    auto bytes = solidRGBA8(32, 32, 100);  // uniform mid-gray
    eng.addImage(bytes.data(), 32, 32, 4);
    eng.setPreviewSize(4096);  // no downscale

    PreviewBuffer base = eng.renderFull();
    const int cx = base.width / 2, cy = base.height / 2;
    const int center0 = base.rgba[((size_t)cy * base.width + cx) * 4];
    const int corner0 = base.rgba[0];

    MaskParams m;  // centred radial, big exposure lift
    m.type = MaskParams::Radial; m.cx = 0.5f; m.cy = 0.5f; m.rx = 0.25f; m.ry = 0.25f; m.feather = 0.3f;
    m.adjust.exposure = 2.0f;
    eng.setMasks({m});

    PreviewBuffer out = eng.renderFull();
    const int center1 = out.rgba[((size_t)cy * out.width + cx) * 4];
    const int corner1 = out.rgba[0];
    CHECK(center1 > center0 + 20);                 // mask centre brightened
    CHECK(std::abs(corner1 - corner0) <= 2);       // outside the mask unchanged

    eng.setMasks({});  // clearing masks restores the base render
    PreviewBuffer back = eng.renderFull();
    CHECK(std::abs((int)back.rgba[((size_t)cy * back.width + cx) * 4] - center0) <= 1);
}

TEST(EditParamsIO_masks_roundtrip)
{
    EditParams p;
    MaskParams r; r.type = MaskParams::Radial; r.cx = 0.4f; r.cy = 0.6f; r.rx = 0.2f; r.ry = 0.35f;
    r.feather = 0.7f; r.inverted = true; r.adjust.exposure = 1.5f; r.adjust.clarity = 40.f;
    MaskParams b; b.type = MaskParams::Brush; b.adjust.temp = -30.f;
    b.dabs.push_back({0.1f, 0.2f, 0.08f, 0.5f});
    b.dabs.push_back({0.3f, 0.4f, 0.06f, 1.0f});
    p.masks = {r, b};

    EditParams q;
    CHECK(deserializeParams(serializeParams(p), q));
    CHECK(q.masks.size() == 2);
    CHECK(q.masks[0].type == MaskParams::Radial);
    CHECK(q.masks[0].inverted);
    CHECK_NEAR(q.masks[0].cy, 0.6, 1e-4);
    CHECK_NEAR(q.masks[0].adjust.exposure, 1.5, 1e-4);
    CHECK_NEAR(q.masks[0].adjust.clarity, 40.0, 1e-4);
    CHECK(q.masks[1].type == MaskParams::Brush);
    CHECK(q.masks[1].dabs.size() == 2);
    CHECK_NEAR(q.masks[1].dabs[1].radius, 0.06, 1e-4);
    CHECK_NEAR(q.masks[1].adjust.temp, -30.0, 1e-4);
}

// ── GPU compute-backend seam (R-GPU): selection, fallback, availability ──
// A test double for IComputeBackend (the compute-side analogue of Artboard's
// RecordingTarget): fills the result with a constant sentinel so an accelerated
// render is byte-distinguishable from the CPU reference path.
namespace
{
    struct MockBackend : IComputeBackend
    {
        bool avail = true;
        bool accept = true;   // what process() returns
        int calls = 0;
        float sentinel = 0.5f;
        const char *name() const override { return "Mock"; }
        Kind kind() const override { return Kind::Gpu; }
        bool available() const override { return avail; }
        bool process(const Image &src, const EditParams &, ComputeResult &out) override
        {
            ++calls;
            if (!accept) return false;   // decline -> engine falls back to the CPU reference
            out.processed = Image(src.width(), src.height(), src.channels());
            Pixel *d = out.processed.data();
            const size_t n = (size_t)src.width() * src.height() * src.channels();
            for (size_t i = 0; i < n; ++i) d[i] = sentinel;
            return true;
        }
    };
}

TEST(EditEngine_gpu_backend_selection_and_fallback)
{
    auto bytes = variedRGBA8b(16, 16);
    EditParams p; p.exposure = 0.4f; p.contrast = 12.f;

    // Baseline: force CPU-only (a default engine may install a real platform
    // backend, e.g. OpenGL — the mock path below tests selection independently).
    EditEngine base;
    base.setComputeAccelerator(nullptr);
    base.addImage(bytes.data(), 16, 16, 4);
    base.selectImage(0);
    base.setPreviewSize(4096);
    base.setCurrentParams(p);
    CHECK(!base.gpuAvailable());
    PreviewBuffer cpb = base.renderFull();
    CHECK(cpb.rgba != nullptr);
    const std::vector<uint8_t> cpu(cpb.rgba, cpb.rgba + (size_t)cpb.width * cpb.height * 4);

    // Engine with an injected mock accelerator.
    EditEngine eng;
    eng.addImage(bytes.data(), 16, 16, 4);
    eng.selectImage(0);
    eng.setPreviewSize(4096);
    eng.setCurrentParams(p);
    auto mockOwned = std::make_unique<MockBackend>();
    MockBackend *mock = mockOwned.get();
    eng.setComputeAccelerator(std::move(mockOwned));
    CHECK(eng.gpuAvailable());

    // (1) prefer OFF -> CPU path; mock not called; output == CPU baseline.
    eng.setPreferGpu(false);
    {
        PreviewBuffer b = eng.renderFull();
        const std::vector<uint8_t> out(b.rgba, b.rgba + (size_t)b.width * b.height * 4);
        CHECK(mock->calls == 0);
        CHECK(out == cpu);
    }

    // (2) prefer ON + available -> mock USED; output is the sentinel (0.5 -> 128).
    eng.setPreferGpu(true);
    {
        PreviewBuffer b = eng.renderFull();
        CHECK(mock->calls == 1);
        CHECK((int)b.rgba[0] == 128);
        CHECK((int)b.rgba[1] == 128);
        CHECK((int)b.rgba[2] == 128);
    }

    // (3) backend DECLINES the job -> CPU fallback == baseline (process was called).
    mock->accept = false;
    {
        PreviewBuffer b = eng.renderFull();
        const std::vector<uint8_t> out(b.rgba, b.rgba + (size_t)b.width * b.height * 4);
        CHECK(mock->calls == 2);
        CHECK(out == cpu);
    }

    // (4) backend UNAVAILABLE -> gpuAvailable() false; process not called; CPU output.
    mock->accept = true;
    mock->avail = false;
    CHECK(!eng.gpuAvailable());
    {
        PreviewBuffer b = eng.renderFull();
        const std::vector<uint8_t> out(b.rgba, b.rgba + (size_t)b.width * b.height * 4);
        CHECK(mock->calls == 2);   // available()==false short-circuits before process()
        CHECK(out == cpu);
    }

    // The platform factory returns the GPU backend where built, else nullptr.
#ifdef ARSTRO_GL_COMPUTE
    CHECK(createComputeAccelerator() != nullptr);
#else
    CHECK(createComputeAccelerator() == nullptr);
#endif
}

// Real GPU backend (OpenGL 4.3 compute): on a host with a GL compute device
// (e.g. the AMD/Mesa GPU, or llvmpipe software), the ported subset — exposure,
// contrast, white balance — must match the CPU within a small tolerance, and an
// edit outside the subset must decline to the (identical) CPU path. Skips cleanly
// where no GPU backend is available.
TEST(EditEngine_gl_backend_matches_cpu)
{
    auto bytes = variedRGBA8b(24, 18);
    EditParams p; p.exposure = 0.7f; p.contrast = 20.f; p.temp = 5200.f; p.tint = 8.f;

    // CPU reference (forced no accelerator).
    EditEngine cpu; cpu.setComputeAccelerator(nullptr);
    cpu.addImage(bytes.data(), 24, 18, 4); cpu.selectImage(0); cpu.setPreviewSize(4096);
    cpu.setCurrentParams(p);
    PreviewBuffer cb = cpu.renderFull();
    const std::vector<uint8_t> cref(cb.rgba, cb.rgba + (size_t)cb.width * cb.height * 4);

    // GPU-preferred engine (default factory installs the platform backend).
    EditEngine gpu;
    gpu.addImage(bytes.data(), 24, 18, 4); gpu.selectImage(0); gpu.setPreviewSize(4096);
    gpu.setPreferGpu(true);
    if (!gpu.gpuAvailable()) { CHECK(true); return; }  // CPU-only host: nothing to verify

    gpu.setCurrentParams(p);
    PreviewBuffer gb = gpu.renderFull();
    const std::vector<uint8_t> g(gb.rgba, gb.rgba + (size_t)gb.width * gb.height * 4);
    CHECK(g.size() == cref.size());
    int maxd = 0;
    for (size_t i = 0; i < g.size(); ++i) { int d = (int)g[i] - (int)cref[i]; if (d < 0) d = -d; if (d > maxd) maxd = d; }
    CHECK(maxd <= 2);  // GPU vs CPU float, after sRGB encode + round to 8-bit

    // An edit OUTSIDE the ported subset (saturation) must decline -> exact CPU output.
    EditParams q = p; q.saturation = 40.f;
    EditEngine cpu2; cpu2.setComputeAccelerator(nullptr);
    cpu2.addImage(bytes.data(), 24, 18, 4); cpu2.selectImage(0); cpu2.setPreviewSize(4096);
    cpu2.setCurrentParams(q);
    PreviewBuffer c2 = cpu2.renderFull();
    const std::vector<uint8_t> c2ref(c2.rgba, c2.rgba + (size_t)c2.width * c2.height * 4);
    gpu.setCurrentParams(q);
    PreviewBuffer g2 = gpu.renderFull();
    const std::vector<uint8_t> g2b(g2.rgba, g2.rgba + (size_t)g2.width * g2.height * 4);
    CHECK(g2b == c2ref);

    // A per-channel tone curve is also outside the ported subset -> decline -> exact CPU.
    EditParams r = p; r.curveChannel[0] = {{0.f, 0.f}, {0.5f, 0.9f}, {1.f, 1.f}};
    EditEngine cpu3; cpu3.setComputeAccelerator(nullptr);
    cpu3.addImage(bytes.data(), 24, 18, 4); cpu3.selectImage(0); cpu3.setPreviewSize(4096);
    cpu3.setCurrentParams(r);
    PreviewBuffer c3 = cpu3.renderFull();
    const std::vector<uint8_t> c3ref(c3.rgba, c3.rgba + (size_t)c3.width * c3.height * 4);
    gpu.setCurrentParams(r);
    PreviewBuffer g3 = gpu.renderFull();
    const std::vector<uint8_t> g3b(g3.rgba, g3.rgba + (size_t)g3.width * g3.height * 4);
    CHECK(g3b == c3ref);
}

// The OpenGL ES 3.1 backend (Android's GPU path), here exercised on desktop Mesa GLES.
// Same contract as EditEngine_gl_backend_matches_cpu: the ported subset (exposure/
// contrast/white balance + sRGB encode) matches the CPU reference within tolerance, and
// an edit outside the subset declines -> exact CPU output. Skips cleanly when the GLES
// backend isn't built (desktop default) or no ES 3.1 device is present.
TEST(EditEngine_gles_backend_matches_cpu)
{
#ifdef ARSTRO_GLES_COMPUTE
    auto bytes = variedRGBA8b(24, 18);
    EditParams p; p.exposure = 0.7f; p.contrast = 20.f; p.temp = 5200.f; p.tint = 8.f;

    // CPU reference (forced no accelerator).
    EditEngine cpu; cpu.setComputeAccelerator(nullptr);
    cpu.addImage(bytes.data(), 24, 18, 4); cpu.selectImage(0); cpu.setPreviewSize(4096);
    cpu.setCurrentParams(p);
    PreviewBuffer cb = cpu.renderFull();
    const std::vector<uint8_t> cref(cb.rgba, cb.rgba + (size_t)cb.width * cb.height * 4);

    // GLES-preferred engine (inject the GLES backend directly — the default factory
    // prefers desktop GL when both are built).
    EditEngine gpu;
    gpu.setComputeAccelerator(createGlesComputeAccelerator());
    gpu.addImage(bytes.data(), 24, 18, 4); gpu.selectImage(0); gpu.setPreviewSize(4096);
    gpu.setPreferGpu(true);
    if (!gpu.gpuAvailable()) { CHECK(true); return; }  // no ES 3.1 device: nothing to verify

    gpu.setCurrentParams(p);
    PreviewBuffer gb = gpu.renderFull();
    const std::vector<uint8_t> g(gb.rgba, gb.rgba + (size_t)gb.width * gb.height * 4);
    CHECK(g.size() == cref.size());
    int maxd = 0;
    for (size_t i = 0; i < g.size(); ++i) { int d = (int)g[i] - (int)cref[i]; if (d < 0) d = -d; if (d > maxd) maxd = d; }
    CHECK(maxd <= 2);  // GPU vs CPU float, after sRGB encode + round to 8-bit

    // An edit OUTSIDE the ported subset (saturation) must decline -> exact CPU output.
    EditParams q = p; q.saturation = 40.f;
    EditEngine cpu2; cpu2.setComputeAccelerator(nullptr);
    cpu2.addImage(bytes.data(), 24, 18, 4); cpu2.selectImage(0); cpu2.setPreviewSize(4096);
    cpu2.setCurrentParams(q);
    PreviewBuffer c2 = cpu2.renderFull();
    const std::vector<uint8_t> c2ref(c2.rgba, c2.rgba + (size_t)c2.width * c2.height * 4);
    gpu.setCurrentParams(q);
    PreviewBuffer g2 = gpu.renderFull();
    const std::vector<uint8_t> g2b(g2.rgba, g2.rgba + (size_t)g2.width * g2.height * 4);
    CHECK(g2b == c2ref);
#else
    CHECK(true);  // GLES backend not built in this configuration
#endif
}

// The cosmo path: RenderService runs the engine (and thus the GPU backend) on its
// OWN worker thread, so the GL context must init off the main thread. Verify a
// GPU-preferred service frame matches a CPU service frame within tolerance.
TEST(RenderService_gpu_worker_matches_cpu)
{
    auto bytes = variedRGBA8b(20, 16);
    EditParams p; p.exposure = 0.5f; p.temp = 5000.f;

    auto renderSvc = [&](bool gpu, RenderService::Frame &out) -> bool {
        RenderService svc;
        const int slot = svc.addImage(bytes.data(), 20, 16, 4);
        svc.setPreviewSize(4096);
        if (gpu && !svc.gpuAvailable()) return false;  // CPU-only host
        svc.setPreferGpu(gpu);
        svc.render(slot, p);
        for (int i = 0; i < 500; ++i) {
            if (svc.tryAcquire(out)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    };

    RenderService::Frame cpuF, gpuF;
    CHECK(renderSvc(false, cpuF));
    if (!renderSvc(true, gpuF)) { CHECK(true); return; }  // no GPU backend -> skip
    CHECK(cpuF.width == gpuF.width && cpuF.height == gpuF.height);
    CHECK(cpuF.rgba.size() == gpuF.rgba.size());
    int maxd = 0;
    for (size_t i = 0; i < cpuF.rgba.size(); ++i) { int d = (int)gpuF.rgba[i] - (int)cpuF.rgba[i]; if (d < 0) d = -d; if (d > maxd) maxd = d; }
    CHECK(maxd <= 2);
}

// ── R-PREVIEW-6 / D-45: a stage at its default value is DROPPED, not run ──────────
//
// The whole edit pipeline used to run at default parameters, and 132 of a 193 ms
// preview render went to seventeen stages reproducing the buffer they were handed.
// Two things have to be true for the skip to be correct, and only one of them is a
// performance claim:
//
//   * every processor answers isIdentity() truthfully at its defaults, and stops
//     answering it the moment a parameter moves;
//   * a chain of identity stages returns its input BIT-IDENTICALLY. This is the
//     assertion that fails without the fix, and it fails for a real reason rather
//     than a timing one: several stages are only APPROXIMATELY identity at their
//     neutral value. Contrast computes `(x - pivot) * 1 + pivot`, which is not x in
//     float, and ToneCurve at `curveLog` (the default) round-trips every channel
//     through srgbEncode -> a 1024-entry LUT -> srgbDecode. Skipping is therefore
//     the more accurate answer as well as the free one.

// A gradient with values that are not exactly representable, so an approximate
// identity shows up as a bit difference rather than surviving by luck.
static Image variedLinear(int w, int h)
{
    Image img(w, h, 4, ColorSpace::LinearSRGB);
    Pixel *d = img.data();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            Pixel *p = d + ((size_t)y * w + x) * 4;
            p[0] = (Pixel)((x * 7 + y * 3) % 251) / (Pixel)251;
            p[1] = (Pixel)((x * 3 + y * 11) % 241) / (Pixel)241;
            p[2] = (Pixel)((x * 13 + y * 5) % 233) / (Pixel)233;
            p[3] = (Pixel)1;
        }
    return img;
}

TEST(Identity_stages_report_themselves_and_stop_when_moved)
{
    Exposure exposure;      CHECK(exposure.isIdentity());
    exposure.setExposureEv(0.5f);           CHECK(!exposure.isIdentity());
    exposure.setExposureEv(0.0f);           CHECK(exposure.isIdentity());

    Contrast contrast;      CHECK(contrast.isIdentity());
    contrast.setContrast(10.f);             CHECK(!contrast.isIdentity());

    ToneRegions regions;    CHECK(regions.isIdentity());
    regions.setBlacks(-5.f);                CHECK(!regions.isIdentity());

    // 6500 K is the working white, so the default is identity but 5000 K is not.
    WhiteBalance wb;        CHECK(wb.isIdentity());
    wb.setTemperature(5000.f);              CHECK(!wb.isIdentity());

    Vibrance vib;           CHECK(vib.isIdentity());
    vib.setSaturation(20.f);                CHECK(!vib.isIdentity());

    ColorGrading grade;     CHECK(grade.isIdentity());
    // Hue alone does nothing while saturation is zero — the wheel is still identity.
    grade.setGradeHue(ColorGrading::Midtones, 210.f);         CHECK(grade.isIdentity());
    grade.setGradeSaturation(ColorGrading::Midtones, 30.f);   CHECK(!grade.isIdentity());

    Dehaze dehaze;          CHECK(dehaze.isIdentity());
    dehaze.setAmount(25.f);                 CHECK(!dehaze.isIdentity());

    Grain grain;            CHECK(grain.isIdentity());
    grain.setAmount(30.f);                  CHECK(!grain.isIdentity());

    Texture texture;        CHECK(texture.isIdentity());
    texture.setAmount(15.f);                CHECK(!texture.isIdentity());

    Clarity clarity;        CHECK(clarity.isIdentity());
    clarity.setAmount(15.f);                CHECK(!clarity.isIdentity());

    Sharpen sharpen;        CHECK(sharpen.isIdentity());
    sharpen.setRadius(2.f);                 CHECK(sharpen.isIdentity());  // radius alone: nothing
    sharpen.setAmount(40.f);                CHECK(!sharpen.isIdentity());

    NoiseReduction nr;      CHECK(nr.isIdentity());
    nr.setLuminance(20.f);                  CHECK(!nr.isIdentity());

    Crop crop;              CHECK(crop.isIdentity());
    crop.setRect(0.1f, 0.1f, 0.8f, 0.8f);   CHECK(!crop.isIdentity());
    crop.reset();                           CHECK(crop.isIdentity());

    Rotate rotate;          CHECK(rotate.isIdentity());
    rotate.setAngle(1.5f);                  CHECK(!rotate.isIdentity());
    rotate.setAngle(0.f);                   CHECK(rotate.isIdentity());
    rotate.setQuarterTurns(1);              CHECK(!rotate.isIdentity());

    LensCorrection lens;    CHECK(lens.isIdentity());
    lens.setVignette(30.f);                 CHECK(!lens.isIdentity());

    // The two LUT-driven stages carry a cached flag instead of properties.
    ToneCurve curve;        CHECK(curve.isIdentity());
    curve.setPoints({CurvePoint{0.f, 0.f}, CurvePoint{0.5f, 0.7f}, CurvePoint{1.f, 1.f}});
    CHECK(!curve.isIdentity());
    curve.setPoints({CurvePoint{0.f, 0.f}, CurvePoint{1.f, 1.f}});
    CHECK(curve.isIdentity());              // back to a straight line
    curve.setChannelPoints(1, {CurvePoint{0.f, 0.1f}, CurvePoint{1.f, 1.f}});
    CHECK(!curve.isIdentity());             // a channel curve counts too

    ColorMixer mixer;       CHECK(mixer.isIdentity());
    mixer.setCurve(ColorMixer::Sat, {CurvePoint{0.f, 0.4f}, CurvePoint{360.f, 0.4f}});
    CHECK(!mixer.isIdentity());
    mixer.setCurve(ColorMixer::Sat, {});
    CHECK(mixer.isIdentity());
}

TEST(A_chain_of_identity_stages_returns_its_input_bit_for_bit)
{
    // The full pipeline order from EditEngine.h, every stage at its constructed
    // default. Without the identity skip this is only APPROXIMATELY the input —
    // ToneCurve's log round trip and Contrast's `(x-p)*1+p` both move low bits — so
    // an exact comparison is what makes the test fail on the unfixed code.
    Crop crop; Rotate rotate; LensCorrection lens; NoiseReduction nr;
    Exposure exposure; Contrast contrast; ToneRegions regions; WhiteBalance wb;
    ToneCurve curve; Texture texture; Clarity clarity; Vibrance vib;
    ColorMixer mixer; ColorGrading grade; Dehaze dehaze; Sharpen sharpen; Grain grain;

    ImageBlock chain;
    chain.add(&crop); chain.add(&rotate); chain.add(&lens); chain.add(&nr);
    chain.add(&exposure); chain.add(&contrast); chain.add(&regions); chain.add(&wb);
    chain.add(&curve); chain.add(&texture); chain.add(&clarity); chain.add(&vib);
    chain.add(&mixer); chain.add(&grade); chain.add(&dehaze); chain.add(&sharpen);
    chain.add(&grain);
    CHECK(chain.size() == 17);
    CHECK(chain.isIdentity());   // a chain of no-ops is itself a no-op

    const Image in = variedLinear(37, 23);
    Image out;
    chain.apply(in, out);

    CHECK(out.width() == in.width() && out.height() == in.height());
    CHECK(out.channels() == in.channels());
    const size_t n = in.pixelCount() * (size_t)in.channels();
    size_t differing = 0;
    for (size_t i = 0; i < n; ++i)
        if (out.data()[i] != in.data()[i])
            ++differing;
    CHECK(differing == 0);

    // ...and one stage moving off its default is enough to make the chain run again,
    // so the skip cannot be hiding a real edit.
    exposure.setExposureEv(1.0f);
    CHECK(!chain.isIdentity());
    Image lifted;
    chain.apply(in, lifted);
    CHECK_NEAR(lifted.at(5, 5, 0), in.at(5, 5, 0) * (Pixel)2, 1e-4);
}

// ── R-PREVIEW-6: the table-driven transfer functions must be accurate ────────────
//
// srgbEncode/srgbDecode became 4096-entry LUTs with linear interpolation because
// `std::pow(double, 1/2.4)` per colour channel per pixel was ~39 ms of a 1600 px
// render (encodeInPlace + three histogram taps + ToneCurve's log round trip). The
// speed is not what needs guarding — a table is obviously faster. What needs guarding
// is that the shortcut is invisible, so this asserts the error against the closed
// form that IS the specification, at a bound far below one 8-bit step.
TEST(Srgb_tables_match_the_closed_form_far_below_one_8bit_step)
{
    // One 8-bit step is 1/255 = 3.92e-3. The bound is set two orders of magnitude
    // under that, which is what the 4096-knot spacing actually delivers; a regression
    // that coarsened the table or dropped the interpolation would break this long
    // before anything became visible.
    const Pixel kBound = (Pixel)5e-5;

    Pixel worstEnc = 0, worstDec = 0, atEnc = 0, atDec = 0;
    // Dense sweep, deliberately NOT on the knot spacing — sampling only at knots would
    // report zero error and prove nothing about the interpolation between them.
    const int kSamples = 200003;   // prime, so it never lands on a 4096-grid point
    for (int i = 0; i <= kSamples; ++i)
    {
        const Pixel x = (Pixel)i / (Pixel)kSamples;
        const Pixel de = std::fabs(color::srgbEncode(x) - color::srgbEncodeExact(x));
        const Pixel dd = std::fabs(color::srgbDecode(x) - color::srgbDecodeExact(x));
        if (de > worstEnc) { worstEnc = de; atEnc = x; }
        if (dd > worstDec) { worstDec = dd; atDec = x; }
    }
    printf("    srgbEncode worst |err| = %.3e at x=%.6f\n", (double)worstEnc, (double)atEnc);
    printf("    srgbDecode worst |err| = %.3e at x=%.6f\n", (double)worstDec, (double)atDec);
    CHECK(worstEnc < kBound);
    CHECK(worstDec < kBound);

    // The endpoints and the two knees stay exact — they are where a table is most
    // tempting to get wrong, and where clipping behaviour is visible.
    CHECK(color::srgbEncode((Pixel)0) == (Pixel)0);
    CHECK(color::srgbEncode((Pixel)1) == (Pixel)1);
    CHECK(color::srgbDecode((Pixel)0) == (Pixel)0);
    CHECK(color::srgbDecode((Pixel)1) == (Pixel)1);
    CHECK(color::srgbEncode((Pixel)-0.5) == (Pixel)0);   // out of range clamps, not wraps
    CHECK(color::srgbEncode((Pixel)1.5) == (Pixel)1);
    CHECK(color::srgbDecode((Pixel)-0.5) == (Pixel)0);
    CHECK(color::srgbDecode((Pixel)1.5) == (Pixel)1);

    // Below its knee the encode is exactly linear (12.92x), and the knee sits at LUT
    // index ~12.8 — so the first cells interpolate a straight line and are error-free.
    CHECK_NEAR(color::srgbEncode((Pixel)0.001), (Pixel)0.01292, 1e-6);

    // Round-tripping must return the value, which is the property every caller relies
    // on: ToneCurve encodes, looks up, and decodes on every pixel at the default
    // `curveLog` domain, so a biased pair of tables would tint the whole image.
    for (int i = 0; i <= 1000; ++i)
    {
        const Pixel x = (Pixel)i / (Pixel)1000;
        CHECK_NEAR(color::srgbDecode(color::srgbEncode(x)), x, 3e-4);
    }
}

// ── R-PREVIEW-6: the intermediate histogram taps are opt-in, and turning them off
//    changes nothing but the cost ────────────────────────────────────────────────
TEST(Intermediate_histogram_taps_are_optional_and_pixel_neutral)
{
    auto bytes = variedRGBA8b(64, 48);
    EditParams p;
    p.exposure = 0.4f;                 // a real edit, so stages actually run
    p.curve = {CurvePoint{0.f, 0.f}, CurvePoint{0.5f, 0.6f}, CurvePoint{1.f, 1.f}};

    auto renderWith = [&](bool wantPreCurve, bool wantHue, std::vector<uint8_t> &out,
                          HistogramData &finalHist) {
        EditEngine eng;
        eng.addImage(bytes.data(), 64, 48, 4);
        eng.setPreviewSize(4096);       // no downscale; compare like for like
        eng.setWantIntermediateHistograms(wantPreCurve, wantHue);
        CHECK(eng.wantsPreCurveHistogram() == wantPreCurve);
        CHECK(eng.wantsPreMixerHue() == wantHue);
        eng.applyParams(p);
        const PreviewBuffer pb = eng.renderPreview();
        CHECK(pb.rgba != nullptr);
        out.assign(pb.rgba, pb.rgba + (size_t)pb.width * pb.height * 4);
        finalHist = eng.histogram();
    };

    std::vector<uint8_t> withTaps, withoutTaps;
    HistogramData histWith, histWithout;
    renderWith(true, true, withTaps, histWith);
    renderWith(false, false, withoutTaps, histWithout);

    // The whole point: the taps are OBSERVATION, so switching them off may not move a
    // single output pixel, and may not touch the final histogram either.
    CHECK(withTaps.size() == withoutTaps.size());
    CHECK(withTaps == withoutTaps);
    for (int b = 0; b < HistogramData::kBins; ++b)
        CHECK(histWith.lum[b] == histWithout.lum[b]);

    // Default is ON, so a caller that never asks keeps the old behaviour.
    EditEngine fresh;
    CHECK(fresh.wantsPreCurveHistogram() && fresh.wantsPreMixerHue());

    // With a tap off, its accessor holds the last value computed while it was on —
    // so a panel that opens has something to draw before the next frame lands.
    EditEngine eng;
    eng.addImage(bytes.data(), 64, 48, 4);
    eng.setPreviewSize(4096);
    eng.applyParams(p);
    eng.renderPreview();                                  // both taps on
    const HistogramData warm = eng.preCurveHistogram();
    long long warmTotal = 0;
    for (int b = 0; b < HistogramData::kBins; ++b) warmTotal += warm.lum[b];
    CHECK(warmTotal > 0);                                 // it really was computed
    eng.setWantIntermediateHistograms(false, false);
    eng.renderPreview();                                  // taps off
    long long stillTotal = 0;
    for (int b = 0; b < HistogramData::kBins; ++b) stillTotal += eng.preCurveHistogram().lum[b];
    CHECK(stillTotal == warmTotal);                       // retained, not cleared
}

// ── R-PREVIEW-6 / R-MEM-1: a preview render is allocation-free at steady state, and a
//    FULL-RESOLUTION render does not park its scratch afterwards ──────────────────
TEST(Render_reuses_its_working_buffers_but_full_res_releases_them)
{
    // Repeated preview renders must not grow: the three working Images and the three
    // chains' ping-pong scratch are members, so after the first render the pipeline
    // reuses capacity instead of faulting ~82 MB of fresh pages every slider move.
    auto bytes = variedRGBA8b(96, 64);
    EditEngine eng;
    const int slot = eng.addImage(bytes.data(), 96, 64, 4);
    CHECK(slot == 0);
    eng.setPreviewSize(4096);
    EditParams p; p.exposure = 0.25f; p.clarity = 12.f;   // spatial + point stages both run
    eng.applyParams(p);

    const PreviewBuffer a = eng.renderPreview();
    CHECK(a.rgba != nullptr && a.width == 96 && a.height == 64);
    std::vector<uint8_t> first(a.rgba, a.rgba + (size_t)a.width * a.height * 4);

    // Rendering again with identical params must produce identical pixels — a reused
    // buffer that was not fully rewritten would show up here as stale rows.
    for (int i = 0; i < 4; ++i)
    {
        const PreviewBuffer b = eng.renderPreview();
        CHECK(b.rgba != nullptr && b.width == a.width && b.height == a.height);
        std::vector<uint8_t> again(b.rgba, b.rgba + (size_t)b.width * b.height * 4);
        CHECK(again == first);
    }

    // And a full-resolution render still returns valid pixels after releasing its
    // scratch — the release must not take the OUTPUT buffer with it.
    const PreviewBuffer f = eng.renderFull();
    CHECK(f.rgba != nullptr && f.width == 96 && f.height == 64);
    long long sum = 0;
    for (size_t i = 0; i < (size_t)f.width * f.height * 4; ++i) sum += f.rgba[i];
    CHECK(sum > 0);

    // A preview render straight after a full-res one must still work: the buffers it
    // reuses were just released, so this is the path that would crash if the release
    // left a dangling size behind.
    const PreviewBuffer c = eng.renderPreview();
    CHECK(c.rgba != nullptr && c.width == a.width && c.height == a.height);
    std::vector<uint8_t> afterFull(c.rgba, c.rgba + (size_t)c.width * c.height * 4);
    CHECK(afterFull == first);
}

// ── R-PREVIEW-2: the preview pyramid ─────────────────────────────────────────────
//
// A slot keeps four preview levels (previewEdge, /2, /4, /8) so a live gesture can
// render whichever one fits R-PREVIEW-1's 33 ms budget and refine upward when the
// gesture settles. The properties that matter, and would each break something real:
//
//   * every level exists after ingest, so an interactive gesture never DISCOVERS that
//     a level is missing — building one mid-drag is the stall the budget exists to
//     prevent;
//   * a coarse level renders the same EDIT, just fewer pixels — the params are the
//     slot's, not a reduced set, so refinement converges rather than changing look;
//   * the whole pyramid costs ~1.33x one proxy in bytes, which is what lets R-MEM-1's
//     caps absorb it;
//   * a level that has been evicted is rebuilt from a FINER resident level by halving,
//     never from a coarser one by upscaling.
TEST(Preview_pyramid_exists_after_ingest_and_every_level_renders)
{
    auto bytes = variedRGBA8b(400, 300);
    EditEngine eng;
    eng.setPreviewSize(320);                       // level edges: 320 / 160 / 80 / 40
    const int slot = eng.addImagePreviewOnly(bytes.data(), 400, 300, 4);
    CHECK(slot == 0);
    CHECK(EditEngine::previewLevels() == 4);

    CHECK(eng.previewLevelEdge(0) == 320);
    CHECK(eng.previewLevelEdge(1) == 160);
    CHECK(eng.previewLevelEdge(2) == 80);
    CHECK(eng.previewLevelEdge(3) == 40);
    // Out of range clamps rather than reading past the pyramid.
    CHECK(eng.previewLevelEdge(-5) == 320);
    CHECK(eng.previewLevelEdge(99) == 40);

    // Ingest built ALL of them — this is the property R-PREVIEW-1 rests on.
    for (int l = 0; l < EditEngine::previewLevels(); ++l)
        CHECK(eng.previewLevelResident(l));

    // Each level renders, at its own size, and dimensions halve as the level rises.
    int prevW = 1 << 30;
    for (int l = 0; l < EditEngine::previewLevels(); ++l)
    {
        eng.setPreviewLevel(l);
        CHECK(eng.previewLevel() == l);
        const PreviewBuffer pb = eng.renderPreview();
        CHECK(pb.rgba != nullptr);
        CHECK(pb.width > 0 && pb.height > 0);
        CHECK(pb.width <= eng.previewLevelEdge(l));
        CHECK(pb.width < prevW);
        prevW = pb.width;
    }
    // setPreviewLevel clamps, it does not read out of bounds.
    eng.setPreviewLevel(99);
    CHECK(eng.previewLevel() == EditEngine::previewLevels() - 1);
    eng.setPreviewLevel(-1);
    CHECK(eng.previewLevel() == 0);
}

TEST(Preview_pyramid_renders_the_same_edit_at_every_level)
{
    // Refinement only converges if a coarse level is the SAME edit at fewer pixels. If a
    // level rendered different params the photo would visibly change as it sharpened,
    // which is the thing R-PREVIEW-3 promises does not happen.
    auto bytes = variedRGBA8b(240, 160);
    EditEngine eng;
    eng.setPreviewSize(240);
    eng.addImagePreviewOnly(bytes.data(), 240, 160, 4);
    EditParams p;
    p.exposure = 1.0f;                 // +1 EV: exactly doubles linear values
    p.temp = 6500.f;                   // keep WB neutral so the check is unambiguous
    eng.applyParams(p);

    // Mean brightness must agree across levels: a box downscale preserves the mean, and
    // every stage here is resolution-independent, so the levels are the same picture.
    double means[4] = {0, 0, 0, 0};
    for (int l = 0; l < EditEngine::previewLevels(); ++l)
    {
        eng.setPreviewLevel(l);
        const PreviewBuffer pb = eng.renderPreview();
        CHECK(pb.rgba != nullptr);
        long long sum = 0;
        const size_t n = (size_t)pb.width * pb.height;
        for (size_t i = 0; i < n; ++i)
            sum += pb.rgba[i * 4] + pb.rgba[i * 4 + 1] + pb.rgba[i * 4 + 2];
        means[l] = (double)sum / (double)(n * 3);
    }
    for (int l = 1; l < EditEngine::previewLevels(); ++l)
        CHECK_NEAR(means[l], means[0], 4.0);   // 8-bit units; downscaling shifts a mean a little
}

TEST(Preview_pyramid_costs_about_a_third_extra_in_bytes)
{
    // 1 + 1/4 + 1/16 + 1/64 = 1.328. R-PREVIEW-5 leans on this: if a pyramid cost 4x a
    // proxy, R-MEM-1's caps could not absorb it and the browse cache from D-44 would
    // shrink by the same factor.
    auto bytes = variedRGBA8b(512, 512);
    EditEngine eng;
    eng.setPreviewSize(512);
    eng.addImagePreviewOnly(bytes.data(), 512, 512, 4);

    const size_t total = eng.residentProxyBytes();
    const size_t levelZero = (size_t)512 * 512 * 4 * sizeof(Pixel);
    const double ratio = (double)total / (double)levelZero;
    printf("    pyramid / level-0 bytes = %.3f (ideal 1.328)\n", ratio);
    CHECK(ratio > 1.2 && ratio < 1.45);
}

TEST(An_evicted_level_is_rebuilt_by_halving_a_finer_one_not_upscaling_a_coarser_one)
{
    // The pyramid is one cache entry, so eviction takes all of it — but a preview SIZE
    // change leaves level 0 valid and the rest stale, and that is the path where "rebuild
    // from a finer level" matters. Rendering coarse must then still work without a source.
    auto bytes = variedRGBA8b(300, 200);
    EditEngine eng;
    eng.setPreviewSize(256);
    eng.addImagePreviewOnly(bytes.data(), 300, 200, 4);   // preview-only: NO source kept

    // Coarse render straight after ingest.
    eng.setPreviewLevel(3);
    const PreviewBuffer coarse = eng.renderPreview();
    CHECK(coarse.rgba != nullptr);
    const int coarseW = coarse.width;

    // Change the preview size: level 0 must be rebuilt, and there is no source to do it
    // from, so the engine must report COLD rather than render an empty image.
    eng.setPreviewSize(200);
    eng.setPreviewLevel(0);
    const PreviewBuffer cold = eng.renderPreview();
    CHECK(cold.rgba == nullptr);          // cold, not wrong — the caller re-decodes

    // Give the pixels back; now every level is available again at the new size.
    eng.setPreviewSize(256);
    CHECK(eng.supplySource(0, bytes.data(), 300, 200, 4));
    eng.setPreviewLevel(3);
    const PreviewBuffer again = eng.renderPreview();
    CHECK(again.rgba != nullptr);
    CHECK(again.width == coarseW);        // same level, same size as before
    for (int l = 0; l < EditEngine::previewLevels(); ++l)
        CHECK(eng.previewLevelResident(l));
}

// ── D-36 / D-47a: a NaN may make a pixel wrong; it may not kill the process ───────
//
// Every one of these calls segfaulted before the fix, and the crash the user reported was
// this exact line reached from a render worker: `sampleTf` indexing its 4096-entry table
// with `(int)NaN`, which is INT_MIN in practice.
//
// The bug is older than the table. `ToneCurve::sampleLut` had it first (D-36), guarded by
// `if (d < 0) d = 0; if (d > 1) d = 1;` — and **every comparison with NaN is false**, so the
// clamp was a no-op for exactly the value that needed clamping. Turning srgbEncode/Decode
// into tables gave the same mistake two more sites and made them reachable on every pixel
// of every frame instead of only when a tone curve was in use.
TEST(A_NaN_cannot_index_a_lookup_table_out_of_bounds)
{
    const float nan = std::nanf("");

    // The transfer functions: NaN and both infinities, in both directions.
    CHECK(color::srgbEncode(nan) == 0.f);
    CHECK(color::srgbDecode(nan) == 0.f);
    CHECK(color::srgbEncode(INFINITY) == 1.f);
    CHECK(color::srgbEncode(-INFINITY) == 0.f);
    CHECK(color::srgbDecode(INFINITY) == 1.f);
    CHECK(color::srgbDecode(-INFINITY) == 0.f);
    // ...and still exact where it matters, so the guard did not cost accuracy.
    CHECK(color::srgbEncode(0.f) == 0.f);
    CHECK(color::srgbEncode(1.f) == 1.f);
    CHECK_NEAR(color::srgbEncode(0.5f), color::srgbEncodeExact(0.5f), 1e-5);

    // The whole pipeline, fed a NaN parameter directly — the shape a hand-edited preset or
    // a fuzzed socket client produces, and the shape `set exposure=250` produces via 2^250.
    auto bytes = variedRGBA8b(40, 28);
    for (float poison : {std::nanf(""), INFINITY, -INFINITY, 1e39f, 250.f})
    {
        EditEngine eng;
        eng.addImage(bytes.data(), 40, 28, 4);
        eng.setPreviewSize(4096);
        EditParams p;
        p.exposure = poison;
        p.curve = {CurvePoint{0.f, 0.f}, CurvePoint{0.5f, 0.6f}, CurvePoint{1.f, 1.f}};
        p.mixer[1] = {CurvePoint{0.f, 0.3f}, CurvePoint{360.f, 0.3f}};
        p.vibrance = 20.f;                       // forces the HSL round trip
        eng.applyParams(p);
        const PreviewBuffer pb = eng.renderPreview();
        CHECK(pb.rgba != nullptr);                // it renders...
        CHECK(pb.width == 40 && pb.height == 28); // ...at the right size...
        bool allBytesDefined = true;              // ...and every byte is a real value
        for (size_t i = 0; i < (size_t)pb.width * pb.height * 4; ++i)
            if (pb.rgba[i] > 255) allBytesDefined = false;
        CHECK(allBytesDefined);
    }

    // A NaN through every other float-to-index site the pipeline has.
    ToneCurve tc;
    tc.setPoints({CurvePoint{0.f, 0.f}, CurvePoint{0.4f, 0.5f}, CurvePoint{1.f, 1.f}});
    ColorMixer cm;
    cm.setCurve(ColorMixer::Sat, {CurvePoint{0.f, 0.5f}, CurvePoint{360.f, 0.5f}});
    Image poisoned(8, 4, 4, ColorSpace::LinearSRGB);
    for (size_t i = 0; i < poisoned.pixelCount() * 4; ++i) poisoned.data()[i] = nan;
    Image out;
    tc.apply(poisoned, out);        // would have died in sampleLut
    cm.apply(poisoned, out);        // would have returned NaN from sampleCyclic
    Image enc = poisoned.clone();
    color::encodeInPlace(enc);      // would have died in sampleTf
    CHECK(out.width() == 8 && enc.width() == 8);

    // clamp01 is the last line of defence before the 8-bit pack, where a NaN cast is
    // undefined. It must produce a DEFINITE value.
    CHECK(clamp01(nan) == 0.f);
    CHECK(clamp01(-1.f) == 0.f);
    CHECK(clamp01(2.f) == 1.f);
    CHECK(clamp01(0.25f) == 0.25f);
}

// ── D-36: a non-finite parameter is refused on the way in, and repaired in a file ──
TEST(Non_finite_parameters_are_refused_or_neutralised)
{
    // Strict: the name of the offending field, so a rejection is debuggable.
    EditParams p;
    CHECK(firstNonFiniteParam(p) == nullptr);
    p.exposure = std::nanf("");
    CHECK(firstNonFiniteParam(p) != nullptr);
    CHECK(std::string(firstNonFiniteParam(p)) == "exposure");
    p.exposure = 0.f;
    p.lensVignette = INFINITY;
    CHECK(std::string(firstNonFiniteParam(p)) == "lensVignette");

    // Masks and curve points count too — a NaN mask radius is as fatal as a NaN exposure,
    // and both arrive by the same route (a hand-edited file).
    EditParams q;
    MaskParams m;
    m.rx = std::nanf("");
    q.masks.push_back(m);
    CHECK(firstNonFiniteParam(q) != nullptr);
    EditParams r;
    r.curve = {CurvePoint{0.f, 0.f}, CurvePoint{std::nanf(""), 0.5f}, CurvePoint{1.f, 1.f}};
    CHECK(firstNonFiniteParam(r) != nullptr);

    // Lenient: repaired to the NEUTRAL value, and counted.
    EditParams bad;
    bad.exposure = std::nanf("");
    bad.temp = INFINITY;              // neutral is 6500, not 0 — the walk must know that
    bad.contrast = -INFINITY;
    bad.cropW = std::nanf("");        // neutral is 1, not 0
    const int fixed = sanitizeParams(bad);
    CHECK(fixed == 4);
    CHECK(firstNonFiniteParam(bad) == nullptr);
    const EditParams neutral;
    CHECK(bad.exposure == neutral.exposure);
    CHECK(bad.temp == neutral.temp);          // 6500 restored, not zeroed
    CHECK(bad.contrast == neutral.contrast);
    CHECK(bad.cropW == neutral.cropW);        // 1 restored, not zeroed
    // A clean set is left completely alone.
    EditParams good; good.exposure = 1.25f; good.temp = 5200.f;
    CHECK(sanitizeParams(good) == 0);
    CHECK(good.exposure == 1.25f && good.temp == 5200.f);

    // The field list is walked, not the struct's memory, so a new EditParams field would be
    // silently unguarded. This is the assertion that fails when one is added — 58 is the 42
    // named scalars plus the 16 coordinates of a DEFAULT params' curves (the master curve's
    // two points and the three channel curves' two each). Masks and mixer curves are empty
    // by default, so they add nothing here; they are covered by the assertions above.
    printf("    %d scalars guarded\n", guardedParamScalarCount());
    CHECK(guardedParamScalarCount() == 58);
}
