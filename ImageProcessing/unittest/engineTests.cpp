/*
 *  Unit tests for the EditEngine facade: image slots, the flat parameter API,
 *  preview vs full render, and histogram access. (main() is in imageTests.cpp.)
 */
#include "MiniTest.h"
#include "image_processing.h"
#include <chrono>
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
    CHECK_NEAR(q.curve[1].first, 0.5, 1e-4);
    CHECK_NEAR(q.curve[1].second, 0.6, 1e-4);
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

// ── .apf generic preset envelope + selective image mapping ──
TEST(Apf_selective_save_and_apply)
{
    EditParams p;
    p.exposure = 1.2f; p.contrast = 30.f; p.temp = 7200.f; p.vibrance = 40.f;
    p.curve = {{0.f, 0.05f}, {1.f, 0.95f}};
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
    CHECK(out.curve.size() == 2 && out.curve[1].second == 1.f);  // curve NOT applied (default identity)
    CHECK_NEAR(out.temp, 6500.0, 1e-3);                          // color not in the file at all

    // apply "curve" too -> curve now changes
    CHECK(applyApfToEditParams(rt, {"basic", "curve"}, out));
    CHECK(out.curve.size() == 2);
    CHECK_NEAR(out.curve[0].second, 0.05, 1e-4);

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
