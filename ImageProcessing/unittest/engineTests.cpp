/*
 *  Unit tests for the EditEngine facade: image slots, the flat parameter API,
 *  preview vs full render, and histogram access. (main() is in imageTests.cpp.)
 */
#include "MiniTest.h"
#include "image_processing.h"
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
