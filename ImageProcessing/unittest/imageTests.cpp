/*
 *  Unit tests for the ImageProcessing core (base types + processors + analysis).
 *  Dependency-free MiniTest harness. main() lives here; engineTests.cpp registers
 *  additional cases into the shared registry.
 */
#include "MiniTest.h"
#include "image_processing.h"
#include <cmath>

using namespace arstro;

// Helper: a solid linear-light image of a given value.
static Image solidLinear(int w, int h, int ch, Pixel v)
{
    Image img(w, h, ch, ColorSpace::LinearSRGB);
    const size_t n = (size_t)w * h * ch;
    for (size_t i = 0; i < n; ++i)
        img.data()[i] = v;
    return img;
}

// ── Image buffer ──
TEST(Image_alloc_access_clone_resize)
{
    Image img(3, 2, 4, ColorSpace::LinearSRGB);
    CHECK(img.width() == 3 && img.height() == 2 && img.channels() == 4);
    CHECK(img.pixelCount() == 6);
    CHECK(!img.empty());
    img.at(2, 1, 0) = (Pixel)0.5;
    CHECK_NEAR(img.at(2, 1, 0), 0.5, 1e-9);
    CHECK_NEAR(img.row(1)[2 * 4 + 0], 0.5, 1e-9);

    Image c = img.clone();
    CHECK(c.width() == 3 && c.channels() == 4);
    CHECK_NEAR(c.at(2, 1, 0), 0.5, 1e-9);

    Image r;
    r.resizeLike(img);
    CHECK(r.width() == 3 && r.height() == 2 && r.channels() == 4);
    r.resizeLike(img);  // second call hits the reuse (no-realloc) path
    CHECK(r.width() == 3);
}

TEST(Image_sampleBilinear)
{
    Image img(2, 2, 1, ColorSpace::LinearSRGB);
    img.at(0, 0, 0) = 0; img.at(1, 0, 0) = 1;
    img.at(0, 1, 0) = 0; img.at(1, 1, 0) = 1;
    CHECK_NEAR(img.sampleBilinear(0.5f, 0.0f, 0), 0.5, 1e-5);
    CHECK_NEAR(img.sampleBilinear(-3.0f, -3.0f, 0), 0.0, 1e-6);  // clamps to (0,0)
    CHECK_NEAR(img.sampleBilinear(9.0f, 9.0f, 0), 1.0, 1e-6);    // clamps to (1,1)

    Image empty;
    CHECK_NEAR(empty.sampleBilinear(0, 0, 0), 0.0, 1e-9);
}

// ── ColorSpace ──
TEST(ColorSpace_roundtrip_and_luminance)
{
    for (double v = 0.0; v <= 1.0; v += 0.1)
    {
        Pixel enc = color::srgbEncode((Pixel)v);
        Pixel back = color::srgbDecode(enc);
        CHECK_NEAR(back, v, 1e-3);
    }
    // clamping branches
    CHECK_NEAR(color::srgbEncode((Pixel)-1), 0.0, 1e-9);
    CHECK_NEAR(color::srgbEncode((Pixel)2), 1.0, 1e-9);
    CHECK_NEAR(color::srgbDecode((Pixel)-1), 0.0, 1e-9);
    CHECK_NEAR(color::srgbDecode((Pixel)2), 1.0, 1e-9);
    // linear segment near zero
    CHECK_NEAR(color::srgbEncode((Pixel)0.001), 0.001 * 12.92, 1e-6);
    CHECK_NEAR(color::srgbDecode((Pixel)0.02), 0.02 / 12.92, 1e-6);

    CHECK_NEAR(color::luminance(1, 1, 1), 1.0, 1e-6);
    CHECK_NEAR(color::luminance(1, 0, 0), 0.2126, 1e-6);

    Image img = solidLinear(2, 2, 3, (Pixel)0.5);
    color::encodeInPlace(img);
    CHECK(img.space() == ColorSpace::EncodedSRGB);
    CHECK_NEAR(img.at(0, 0, 0), color::srgbEncode((Pixel)0.5), 1e-6);
    color::encodeInPlace(img);  // already encoded -> no-op branch
    color::decodeInPlace(img);
    CHECK(img.space() == ColorSpace::LinearSRGB);
    CHECK_NEAR(img.at(0, 0, 0), 0.5, 1e-4);
    color::decodeInPlace(img);  // already linear -> no-op branch
}

// ── Exposure ──
TEST(Exposure_plus1EV_doubles_linear)
{
    Image img = solidLinear(2, 2, 3, (Pixel)0.25);
    Exposure e;
    e.setExposureEv(1.0);
    Image out = e.apply(img);
    CHECK_NEAR(out.at(0, 0, 0), 0.5, 1e-5);
    CHECK_NEAR(out.at(1, 1, 2), 0.5, 1e-5);

    e.setExposureEv(-1.0);
    out = e.apply(img);
    CHECK_NEAR(out.at(0, 0, 0), 0.125, 1e-5);

    e.setExposureEv(0.0);
    out = e.apply(img);
    CHECK_NEAR(out.at(0, 0, 0), 0.25, 1e-6);  // identity
}

TEST(Exposure_passes_alpha_through)
{
    Image img(1, 1, 4, ColorSpace::LinearSRGB);
    img.at(0, 0, 0) = (Pixel)0.25; img.at(0, 0, 1) = (Pixel)0.25;
    img.at(0, 0, 2) = (Pixel)0.25; img.at(0, 0, 3) = (Pixel)0.7;
    Exposure e; e.setExposureEv(1.0);
    Image out = e.apply(img);
    CHECK_NEAR(out.at(0, 0, 0), 0.5, 1e-5);
    CHECK_NEAR(out.at(0, 0, 3), 0.7, 1e-6);  // alpha untouched
}

// ── Contrast ──
TEST(Contrast_pivot_fixed_and_slope)
{
    Contrast c; c.setContrast(100.0);  // slope 2
    Image mid = solidLinear(1, 1, 3, (Pixel)0.5);
    Image out = c.apply(mid);
    CHECK_NEAR(out.at(0, 0, 0), 0.82, 1e-5);  // (0.5-0.18)*2+0.18

    Image piv = solidLinear(1, 1, 3, (Pixel)0.18);
    out = c.apply(piv);
    CHECK_NEAR(out.at(0, 0, 0), 0.18, 1e-6);  // pivot unchanged

    c.setContrast(0.0);
    out = c.apply(mid);
    CHECK_NEAR(out.at(0, 0, 0), 0.5, 1e-6);  // identity
}

// ── property accessors / bypass ──
TEST(ImageProcessor_property_and_bypass)
{
    Exposure e;
    e.setName("exp");
    CHECK(e.name() == "exp");
    e.setExposureEv(2.0);
    CHECK_NEAR(e.getProperty(Exposure::exposureEvID), 2.0, 1e-9);
    CHECK_NEAR(e.getPropertyTargetValue(Exposure::exposureEvID), 2.0, 1e-9);

    e.setBypass(true);
    CHECK(e.isBypassed());
    Image img = solidLinear(1, 1, 3, (Pixel)0.25);
    Image out = e.apply(img);
    CHECK_NEAR(out.at(0, 0, 0), 0.25, 1e-9);  // bypass copies input
    CHECK(out.space() == ColorSpace::LinearSRGB);
}

// ── ImageBlock ──
TEST(ImageBlock_identity_skip_and_chain)
{
    Image img = solidLinear(1, 1, 3, (Pixel)0.5);

    ImageBlock empty;
    Image o = empty.apply(img);
    CHECK_NEAR(o.at(0, 0, 0), 0.5, 1e-9);  // empty chain = identity

    Exposure e; e.setExposureEv(1.0); e.setBypass(true);  // skipped
    Contrast c; c.setContrast(100.0);                     // slope 2
    ImageBlock blk; blk.add(&e); blk.add(&c);
    blk.prepare();
    o = blk.apply(img);
    CHECK_NEAR(o.at(0, 0, 0), 0.82, 1e-5);  // only contrast applied
    blk.remove(&c);
    CHECK(blk.size() == 1);
    blk.add(nullptr);  // ignored
    CHECK(blk.size() == 1);
}

TEST(ImageBlock_three_stage_pingpong)
{
    Image img = solidLinear(1, 1, 3, (Pixel)0.25);
    Exposure e1; e1.setExposureEv(1.0);  // x2
    Contrast c; c.setContrast(0.0);      // identity
    Exposure e2; e2.setExposureEv(1.0);  // x2
    ImageBlock blk; blk.add(&e1); blk.add(&c); blk.add(&e2);
    Image o = blk.apply(img);
    CHECK_NEAR(o.at(0, 0, 0), 1.0, 1e-5);  // 0.25 -> 0.5 -> 0.5 -> 1.0
}

// ── Histogram ──
TEST(Histogram_solid_gray_bins)
{
    Image img = solidLinear(4, 4, 3, (Pixel)0.5);
    HistogramData h = Histogram::compute(img);
    uint32_t sum = 0; int nz = 0;
    for (int i = 0; i < HistogramData::kBins; ++i)
    {
        sum += h.r[i];
        if (h.r[i]) ++nz;
    }
    CHECK(sum == 16);
    CHECK(nz == 1);
    CHECK(h.maxCount == 16);

    Image emptyImg;
    HistogramData he = Histogram::compute(emptyImg);
    CHECK(he.maxCount == 0);
}

TEST(Histogram_two_tone_and_readouts)
{
    Image img(2, 1, 3, ColorSpace::LinearSRGB);
    img.at(0, 0, 0) = 0; img.at(0, 0, 1) = 0; img.at(0, 0, 2) = 0;  // black
    img.at(1, 0, 0) = 1; img.at(1, 0, 1) = 1; img.at(1, 0, 2) = 1;  // white
    HistogramData h = Histogram::compute(img);
    CHECK(h.r[0] == 1);
    CHECK(h.r[HistogramData::kBins - 1] == 1);
    CHECK(h.lum[0] == 1 && h.lum[HistogramData::kBins - 1] == 1);

    float lin[4][HistogramData::kBins];
    Histogram::toLinear(h, lin);
    CHECK_NEAR(lin[0][0], 1.0, 1e-6);
    CHECK_NEAR(lin[0][128], 0.0, 1e-6);

    float lg[4][HistogramData::kBins];
    Histogram::toLog(h, lg);
    CHECK_NEAR(lg[0][0], 1.0, 1e-6);
    CHECK_NEAR(lg[0][128], 0.0, 1e-6);
    CHECK_NEAR(lg[3][HistogramData::kBins - 1], 1.0, 1e-6);
}

TEST(Histogram_grayscale_single_channel)
{
    Image img = solidLinear(2, 2, 1, (Pixel)0.5);  // 1-channel path
    HistogramData h = Histogram::compute(img);
    uint32_t sum = 0;
    for (int i = 0; i < HistogramData::kBins; ++i) sum += h.r[i];
    CHECK(sum == 4);
}

// ── Sources ──
TEST(SolidImageSource_generate_and_apply)
{
    SolidImageSource s;
    s.setSize(3, 2);
    s.setChannels(4);
    s.setColor((Pixel)0.1, (Pixel)0.2, (Pixel)0.3, (Pixel)0.5);
    Image img = s.generate();
    CHECK(img.width() == 3 && img.height() == 2 && img.channels() == 4);
    CHECK_NEAR(img.at(2, 1, 0), 0.1, 1e-6);
    CHECK_NEAR(img.at(0, 0, 3), 0.5, 1e-6);

    Image out = s.apply(Image());  // ImageSource::process ignores input
    CHECK(out.width() == 3 && out.channels() == 4);
}

TEST(FileImageSource_encoded_bytes)
{
    uint8_t px[4] = {255, 0, 0, 255};  // opaque red, gamma sRGB
    FileImageSource f;
    f.setEncodedBytes(px, 1, 1, 4);
    Image im = f.generate();
    CHECK(im.space() == ColorSpace::LinearSRGB);
    CHECK_NEAR(im.at(0, 0, 0), 1.0, 1e-4);  // 255 -> linear 1.0
    CHECK_NEAR(im.at(0, 0, 1), 0.0, 1e-4);
    CHECK_NEAR(im.at(0, 0, 3), 1.0, 1e-4);  // alpha not gamma-managed

    FileImageSource f2;
    f2.setImage(im);
    CHECK(f2.image().width() == 1);

    FileImageSource f3;
    f3.setEncodedBytes(nullptr, 1, 1, 4);  // invalid -> empty
    CHECK(f3.image().empty());
}

// ── ImageConfig + config notify ──
TEST(ImageConfig_settings_and_notify)
{
    Exposure live;  // a live processor so notifyConfigChanged iterates a body
    ImageConfig &cfg = ImageConfig::instance();
    cfg.setMaxPreviewEdge(1024);
    CHECK(cfg.maxPreviewEdge() == 1024);
    cfg.setMaxPreviewEdge(0);  // clamps to >= 1
    CHECK(cfg.maxPreviewEdge() == 1);
    cfg.setClampToUnit(false);
    CHECK(!cfg.clampToUnit());
    cfg.setClampToUnit(true);
    cfg.setWorkingSpace(ColorSpace::LinearSRGB);  // -> notifyConfigChanged
    CHECK(cfg.workingSpace() == ColorSpace::LinearSRGB);
    (void)live;
}

// ── VideoProcessor ──
TEST(VideoProcessor_per_frame)
{
    Image img = solidLinear(1, 1, 3, (Pixel)0.25);
    Exposure e; e.setExposureEv(1.0);
    VideoProcessor v(&e);
    Image f = v.processFrame(img);
    CHECK_NEAR(f.at(0, 0, 0), 0.5, 1e-5);
    CHECK(v.frameIndex() == 1);
    v.enableSmoothing(true);
    v.reset();
    CHECK(v.frameIndex() == 0);

    VideoProcessor v2;  // null processor -> passthrough clone
    Image f2 = v2.processFrame(img);
    CHECK_NEAR(f2.at(0, 0, 0), 0.25, 1e-9);

    VideoProcessor v3;
    v3.setProcessor(&e);
    CHECK(v3.processFrame(img).width() == 1);
}

TEST(ImageProcessor_polymorphic_delete)
{
    // Deleting through a base pointer exercises the virtual destructor.
    ImageProcessor *p = new Exposure();
    p->setProperty(Exposure::exposureEvID, (Pixel)1.0);
    Image out = p->apply(solidLinear(1, 1, 3, (Pixel)0.25));
    CHECK_NEAR(out.at(0, 0, 0), 0.5, 1e-5);
    delete p;
}

// Helper: a 1x1 RGB image from linear values.
static Image px1(Pixel r, Pixel g, Pixel b)
{
    Image img(1, 1, 3, ColorSpace::LinearSRGB);
    img.at(0, 0, 0) = r; img.at(0, 0, 1) = g; img.at(0, 0, 2) = b;
    return img;
}
static Image pxHsl(Pixel h, Pixel s, Pixel l)
{
    Pixel r, g, b; color::hslToRgb(h, s, l, r, g, b);
    return px1(r, g, b);
}
static Pixel satOf(const Image &im)
{
    Pixel h, s, l; color::rgbToHsl(im.at(0, 0, 0), im.at(0, 0, 1), im.at(0, 0, 2), h, s, l);
    return s;
}
static Pixel hueOf(const Image &im)
{
    Pixel h, s, l; color::rgbToHsl(im.at(0, 0, 0), im.at(0, 0, 1), im.at(0, 0, 2), h, s, l);
    return h;
}

// ── ColorSpace HSL / Kelvin ──
TEST(ColorSpace_hsl_kelvin_huedelta)
{
    Pixel h, s, l, r, g, b;
    color::rgbToHsl(1, 0, 0, h, s, l); CHECK_NEAR(h, 0, 1e-3); CHECK_NEAR(s, 1, 1e-3);
    color::rgbToHsl(0, 1, 0, h, s, l); CHECK_NEAR(h, 120, 1e-2);
    color::rgbToHsl(0, 0, 1, h, s, l); CHECK_NEAR(h, 240, 1e-2);
    color::rgbToHsl((Pixel)0.5, (Pixel)0.5, (Pixel)0.5, h, s, l); CHECK_NEAR(s, 0, 1e-6);
    color::hslToRgb(0, 1, (Pixel)0.5, r, g, b); CHECK_NEAR(r, 1, 1e-3); CHECK_NEAR(g, 0, 1e-3);
    color::hslToRgb(120, 0, (Pixel)0.5, r, g, b); CHECK_NEAR(r, 0.5, 1e-3);  // s=0 -> gray

    Pixel gr, gg, gb;
    color::kelvinToRgbGain(6500, 0, gr, gg, gb);
    CHECK_NEAR(gr, 1, 1e-3); CHECK_NEAR(gg, 1, 1e-3); CHECK_NEAR(gb, 1, 1e-3);
    color::kelvinToRgbGain(9000, 0, gr, gg, gb);
    CHECK(gr > gb);  // warmer -> more red than blue
    CHECK_NEAR(color::luminance(gr, gg, gb), 1.0, 1e-3);  // luminance-preserving

    CHECK_NEAR(color::hueDelta(350, 10), 20, 1e-3);
    CHECK_NEAR(color::hueDelta(10, 350), -20, 1e-3);
}

// ── ToneRegions ──
TEST(ToneRegions_lift_and_identity)
{
    ToneRegions tr;
    Image dark = solidLinear(1, 1, 3, (Pixel)0.1);
    Image id = tr.apply(dark);
    CHECK_NEAR(id.at(0, 0, 0), 0.1, 1e-6);  // all sliders 0 -> identity

    tr.setShadows(100.0);
    Image out = tr.apply(dark);
    CHECK(out.at(0, 0, 0) > 0.3);  // shadows lift brightens the dark pixel
}

// ── WhiteBalance ──
TEST(WhiteBalance_identity_and_warm)
{
    WhiteBalance wb;
    Image gray = solidLinear(1, 1, 3, (Pixel)0.5);
    Image id = wb.apply(gray);
    CHECK_NEAR(id.at(0, 0, 0), 0.5, 1e-4);  // 6500K = identity

    wb.setTemperature(9000.0);
    Image warm = wb.apply(gray);
    CHECK(warm.at(0, 0, 0) > warm.at(0, 0, 2));  // red boosted over blue
}

// ── Vibrance ──
TEST(Vibrance_weights_by_saturation)
{
    Image lowS = pxHsl(30, (Pixel)0.2, (Pixel)0.5);
    Image highS = pxHsl(30, (Pixel)0.8, (Pixel)0.5);
    const Pixel lo0 = satOf(lowS), hi0 = satOf(highS);

    Vibrance v; v.setVibrance(50.0);
    const Pixel dLo = satOf(v.apply(lowS)) - lo0;
    const Pixel dHi = satOf(v.apply(highS)) - hi0;
    CHECK(dLo > dHi);  // vibrance lifts low-saturation pixels more

    Vibrance id;  // 0/0 -> identity
    CHECK_NEAR(satOf(id.apply(lowS)), lo0, 1e-4);
}

// ── ToneCurve ──
TEST(ToneCurve_identity_and_brighten)
{
    ToneCurve c;  // default identity
    Image mid = solidLinear(1, 1, 3, (Pixel)0.18);
    CHECK_NEAR(c.apply(mid).at(0, 0, 0), 0.18, 2e-3);

    c.setPoints({{0.f, 0.f}, {0.5f, 0.75f}, {1.f, 1.f}});  // lifts mids
    CHECK(c.apply(mid).at(0, 0, 0) > 0.18);

    c.setLogScale(false);  // linear domain still runs; identity points -> identity
    ToneCurve lin; lin.setLogScale(false);
    CHECK_NEAR(lin.apply(mid).at(0, 0, 0), 0.18, 2e-3);
}

TEST(ToneCurve_per_channel_independent)
{
    Image mid = solidLinear(1, 1, 3, (Pixel)0.18);  // neutral grey

    // A per-channel R curve lifts ONLY red; green/blue stay put (identity channels).
    ToneCurve r;
    r.setChannelPoints(0, {{0.f, 0.f}, {0.5f, 0.9f}, {1.f, 1.f}});  // 0 = R
    Image ro = r.apply(mid);
    CHECK(ro.at(0, 0, 0) > 0.18 + 1e-3);   // R lifted
    CHECK_NEAR(ro.at(0, 0, 1), 0.18, 2e-3); // G unchanged
    CHECK_NEAR(ro.at(0, 0, 2), 0.18, 2e-3); // B unchanged

    // Master applies to every channel; a channel curve stacks on top: out_c = chan_c(master(x)).
    ToneCurve mc;
    mc.setPoints({{0.f, 0.f}, {0.5f, 0.75f}, {1.f, 1.f}});          // master lifts all channels
    mc.setChannelPoints(2, {{0.f, 0.f}, {0.5f, 0.9f}, {1.f, 1.f}}); // B lifted further
    Image mo = mc.apply(mid);
    CHECK(mo.at(0, 0, 0) > 0.18);                        // master lifted R
    CHECK_NEAR(mo.at(0, 0, 0), mo.at(0, 0, 1), 2e-3);    // R == G (both master-only)
    CHECK(mo.at(0, 0, 2) > mo.at(0, 0, 1) + 1e-3);       // B got master AND its own channel lift

    // Out-of-range channel index is ignored (no crash, no change).
    ToneCurve oob;
    oob.setChannelPoints(3, {{0.f, 1.f}, {1.f, 0.f}});
    oob.setChannelPoints(-1, {{0.f, 1.f}, {1.f, 0.f}});
    CHECK_NEAR(oob.apply(mid).at(0, 0, 0), 0.18, 2e-3);
}

// ── ColorMixer (cyclic per-hue curves) ──
TEST(ColorMixer_hue_curve_localized_and_cyclic)
{
    Image blue = pxHsl(240, (Pixel)0.8, (Pixel)0.5);
    Image red = pxHsl(0, (Pixel)0.8, (Pixel)0.5);

    // identity: no curves -> unchanged
    ColorMixer id;
    CHECK_NEAR(satOf(id.apply(blue)), 0.8, 1e-3);

    // localized Sat curve: desaturate only around red (hue 0), leave blue alone
    ColorMixer m;
    m.setCurve(ColorMixer::Sat, {{0.f, -1.f}, {60.f, 0.f}, {180.f, 0.f}, {300.f, 0.f}});
    CHECK_NEAR(satOf(m.apply(blue)), 0.8, 0.03);  // blue (240) unchanged
    CHECK(satOf(m.apply(red)) < 0.2);             // red desaturated

    // single point -> constant curve affecting ALL hues
    ColorMixer mc;
    mc.setCurve(ColorMixer::Sat, {{0.f, -1.f}});
    CHECK(satOf(mc.apply(blue)) < 0.2);           // even blue desaturated

    // Hue-shift curve: y=1 bends the hue by +180deg (full any-to-any range)
    ColorMixer mh;
    mh.setCurve(ColorMixer::Hue, {{0.f, 1.f}, {120.f, 0.f}, {240.f, 0.f}});
    CHECK(hueOf(mh.apply(pxHsl(0, (Pixel)0.9, (Pixel)0.5))) > 30.0);
}

// ── ColorGrading ──
TEST(ColorGrading_hue_remap_and_identity)
{
    Image green = pxHsl(120, (Pixel)0.9, (Pixel)0.5);
    ColorGrading id;  // disabled -> identity
    CHECK_NEAR(hueOf(id.apply(green)), 120.0, 2.0);

    Image red = pxHsl(0, (Pixel)0.9, (Pixel)0.5);
    ColorGrading cg;
    cg.setHueRemapEnabled(true);
    cg.setHueRemap(/*src*/ 0, /*range*/ 40, /*dst*/ 30, /*strength*/ 1.0);
    Image out = cg.apply(red);
    CHECK_NEAR(hueOf(out), 30.0, 6.0);  // red remapped toward orange
}

// ── Dehaze ──
TEST(Dehaze_identity_and_contrast)
{
    Dehaze d;
    Image flat = solidLinear(1, 1, 3, (Pixel)0.5);
    CHECK_NEAR(d.apply(flat).at(0, 0, 0), 0.5, 1e-6);  // amount 0 -> identity

    Image grad(2, 1, 3, ColorSpace::LinearSRGB);
    for (int c = 0; c < 3; ++c) { grad.at(0, 0, c) = (Pixel)0.45; grad.at(1, 0, c) = (Pixel)0.55; }
    Dehaze d2; d2.setAmount(100.0);
    Image out = d2.apply(grad);
    const double inSpread = 0.55 - 0.45;
    const double outSpread = out.at(1, 0, 0) - out.at(0, 0, 0);
    CHECK(outSpread > inSpread);  // dehaze increases local contrast
}

// ── Grain ──
TEST(Grain_identity_deterministic)
{
    Image gray = solidLinear(8, 8, 3, (Pixel)0.5);
    Grain g0;
    Image id = g0.apply(gray);
    CHECK_NEAR(id.at(0, 0, 0), 0.5, 1e-6);  // amount 0 -> identity

    Grain g; g.setAmount(100.0); g.setSize(20.0);
    Image a = g.apply(gray);
    Image b = g.apply(gray);
    bool varies = false, same = true;
    for (size_t i = 0; i < gray.pixelCount() * 3; ++i)
    {
        if (std::fabs(a.data()[i] - 0.5) > 1e-4) varies = true;
        if (std::fabs(a.data()[i] - b.data()[i]) > 1e-9) same = false;
    }
    CHECK(varies);  // grain adds variation
    CHECK(same);    // deterministic for a fixed seed
}

// ── Crop ──
TEST(Crop_dims_and_origin)
{
    Image img(4, 4, 3, ColorSpace::LinearSRGB);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            for (int c = 0; c < 3; ++c) img.at(x, y, c) = (Pixel)(x + y) / 8;
    Crop crop; crop.setRect(0, 0, (Pixel)0.5, (Pixel)0.5);
    Image out = crop.apply(img);
    CHECK(out.width() == 2 && out.height() == 2);
    CHECK_NEAR(out.at(0, 0, 0), img.at(0, 0, 0), 1e-9);

    Crop full;  // default full frame -> same dims
    CHECK(full.apply(img).width() == 4);
}

// ── Rotate ──
TEST(Rotate_quarter_turns_and_identity)
{
    Image img(2, 1, 3, ColorSpace::LinearSRGB);
    img.at(0, 0, 0) = 1; img.at(0, 0, 1) = 0; img.at(0, 0, 2) = 0;  // red left
    img.at(1, 0, 0) = 0; img.at(1, 0, 1) = 1; img.at(1, 0, 2) = 0;  // green right

    Rotate r0;
    CHECK_NEAR(r0.apply(img).at(0, 0, 0), 1.0, 1e-9);  // qt 0, angle 0 -> identity

    Rotate r1; r1.setQuarterTurns(1);
    Image out = r1.apply(img);
    CHECK(out.width() == 1 && out.height() == 2);  // dims swap
    CHECK_NEAR(out.at(0, 0, 0), 1.0, 1e-9);  // red on top
    CHECK_NEAR(out.at(0, 1, 1), 1.0, 1e-9);  // green on bottom

    // four 90-degree turns return to the original
    Image acc = img.clone();
    Rotate rr; rr.setQuarterTurns(1);
    for (int i = 0; i < 4; ++i) acc = rr.apply(acc);
    CHECK(acc.width() == 2 && acc.height() == 1);
    CHECK_NEAR(acc.at(0, 0, 0), 1.0, 1e-9);
    CHECK_NEAR(acc.at(1, 0, 1), 1.0, 1e-9);

    Rotate ra; ra.setAngle(0.0);  // angle 0 with qt 0 is identity
    CHECK_NEAR(ra.apply(img).at(1, 0, 1), 1.0, 1e-9);
}

// ── edge cases / branch coverage ──
TEST(Processors_grayscale_passthrough)
{
    Image g = solidLinear(2, 2, 1, (Pixel)0.4);  // single-channel
    WhiteBalance wb; wb.setTemperature(9000.0);
    CHECK_NEAR(wb.apply(g).at(0, 0, 0), 0.4, 1e-6);
    Vibrance v; v.setSaturation(100.0);
    CHECK_NEAR(v.apply(g).at(0, 0, 0), 0.4, 1e-6);
    ColorMixer m; m.setCurve(ColorMixer::Sat, {{0.f, 0.5f}, {180.f, 0.5f}});
    CHECK_NEAR(m.apply(g).at(0, 0, 0), 0.4, 1e-6);
    ColorGrading cg; cg.setGradeSaturation(ColorGrading::Shadows, 80.0);
    CHECK_NEAR(cg.apply(g).at(0, 0, 0), 0.4, 1e-6);
}

TEST(Vibrance_and_mixer_clamps)
{
    // saturation pushed past 1 (clamp) and below 0 (clamp)
    Image c = pxHsl(200, (Pixel)0.9, (Pixel)0.5);
    Vibrance up; up.setSaturation(100.0);
    CHECK(satOf(up.apply(c)) <= 1.0 + 1e-6);
    Vibrance dn; dn.setSaturation(-100.0);
    CHECK(satOf(dn.apply(c)) >= -1e-6);

    // ColorMixer: strong negative hue shift wraps below 0; saturation clamps high
    ColorMixer m;
    m.setCurve(ColorMixer::Hue, {{0.f, -1.f}, {120.f, -1.f}, {240.f, -1.f}});  // shift all -180
    m.setCurve(ColorMixer::Sat, {{0.f, 1.f}, {180.f, 1.f}});                   // 2x sat (clamps)
    Image out = m.apply(c);
    CHECK(hueOf(out) >= 0.0 && hueOf(out) < 360.0);
    CHECK(satOf(out) <= 1.0 + 1e-6);
}

TEST(ToneCurve_empty_points_resets)
{
    ToneCurve c;
    c.setPoints({});  // empty -> falls back to identity
    Image mid = solidLinear(1, 1, 3, (Pixel)0.3);
    CHECK_NEAR(c.apply(mid).at(0, 0, 0), 0.3, 3e-3);
}

TEST(Dehaze_add_haze)
{
    Image grad(2, 1, 3, ColorSpace::LinearSRGB);
    for (int c = 0; c < 3; ++c) { grad.at(0, 0, c) = (Pixel)0.3; grad.at(1, 0, c) = (Pixel)0.7; }
    Dehaze d; d.setAmount(-80.0);  // add haze -> fade toward airlight (less spread)
    Image out = d.apply(grad);
    const double inSpread = 0.7 - 0.3;
    const double outSpread = out.at(1, 0, 0) - out.at(0, 0, 0);
    CHECK(outSpread < inSpread);
}

TEST(Crop_degenerate_passthrough)
{
    Image empty;  // 0x0 -> degenerate guard -> clone passthrough
    Crop crop; crop.setRect(0, 0, (Pixel)0.5, (Pixel)0.5);
    CHECK(crop.apply(empty).empty());

    // far-edge crop where the pixel width clamps to zero -> passthrough
    Image img = solidLinear(4, 4, 3, (Pixel)0.5);
    Crop edge; edge.setRect((Pixel)1.0, 0, (Pixel)0.5, (Pixel)0.5);
    Image o = edge.apply(img);
    CHECK(!o.empty());
}

TEST(Rotate_180_270_and_angle)
{
    Image img(2, 2, 3, ColorSpace::LinearSRGB);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            for (int c = 0; c < 3; ++c) img.at(x, y, c) = (Pixel)(x + 2 * y) / 4;

    Rotate r2; r2.setQuarterTurns(2);  // 180
    Image o2 = r2.apply(img);
    CHECK(o2.width() == 2 && o2.height() == 2);
    CHECK_NEAR(o2.at(0, 0, 0), img.at(1, 1, 0), 1e-9);  // corner swapped

    Rotate r3; r3.setQuarterTurns(3);  // 270
    Image o3 = r3.apply(img);
    CHECK(o3.width() == 2 && o3.height() == 2);

    // arbitrary angle: white square rotated 45 -> corners fall outside (0), centre stays
    Image white = solidLinear(7, 7, 3, (Pixel)1.0);
    Rotate ra; ra.setAngle(45.0);
    Image oa = ra.apply(white);
    CHECK(oa.width() == 7 && oa.height() == 7);
    CHECK(oa.at(0, 0, 0) < 0.5);            // corner outside the source -> transparent
    CHECK(oa.at(3, 3, 0) > 0.5);            // centre still white
}

// ── Detail / presence / lens (spatial processors) ──

// A horizontal gray step: left half dark, right half bright (linear light).
static Image grayStep(int w, int h, Pixel lo, Pixel hi)
{
    Image img(w, h, 3, ColorSpace::LinearSRGB);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            Pixel v = x < w / 2 ? lo : hi;
            for (int c = 0; c < 3; ++c) img.at(x, y, c) = v;
        }
    return img;
}

TEST(Sharpen_identity_and_overshoot)
{
    Image step = grayStep(8, 3, (Pixel)0.2, (Pixel)0.6);

    Sharpen id;  // amount 0 -> identity
    Image i = id.apply(step);
    CHECK_NEAR(i.at(0, 1, 0), 0.2, 1e-5);
    CHECK_NEAR(i.at(7, 1, 0), 0.6, 1e-5);

    Sharpen s; s.setAmount(120.f); s.setRadius(1.f); s.setMasking(0.f);
    Image o = s.apply(step);
    const int el = 8 / 2 - 1, er = 8 / 2;  // pixels straddling the edge
    CHECK(o.at(el, 1, 0) < step.at(el, 1, 0));  // dark side dips (overshoot)
    CHECK(o.at(er, 1, 0) > step.at(er, 1, 0));  // bright side lifts

    // masking gates flat areas: with full masking the edge still sharpens, but a
    // pixel far from the edge (flat) stays put.
    Sharpen sm; sm.setAmount(120.f); sm.setRadius(1.f); sm.setMasking(100.f);
    Image om = sm.apply(step);
    CHECK(om.at(er, 1, 0) > step.at(er, 1, 0));        // edge still sharpened
    CHECK_NEAR(om.at(0, 1, 0), step.at(0, 1, 0), 1e-3);  // flat region untouched
}

TEST(NoiseReduction_identity_and_smooths)
{
    // checkerboard luminance noise on an otherwise mid-gray patch
    Image noisy(8, 8, 3, ColorSpace::LinearSRGB);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
        {
            Pixel v = ((x + y) & 1) ? (Pixel)0.35 : (Pixel)0.25;
            for (int c = 0; c < 3; ++c) noisy.at(x, y, c) = v;
        }
    auto totalVariation = [](const Image &im) {
        double tv = 0; int w = im.width(), h = im.height();
        for (int y = 0; y < h; ++y)
            for (int x = 1; x < w; ++x) tv += std::fabs(im.at(x, y, 0) - im.at(x - 1, y, 0));
        return tv;
    };

    NoiseReduction id;  // 0/0 -> identity
    CHECK_NEAR(id.apply(noisy).at(0, 0, 0), 0.25, 1e-5);

    NoiseReduction nr; nr.setLuminance(100.f);
    CHECK(totalVariation(nr.apply(noisy)) < totalVariation(noisy));  // luma smoothed

    // colour speckle: uniform-ish luma, alternating red/blue tint
    Image speckle(8, 8, 3, ColorSpace::LinearSRGB);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
        {
            bool a = (x + y) & 1;
            speckle.at(x, y, 0) = a ? (Pixel)0.5 : (Pixel)0.3;
            speckle.at(x, y, 1) = (Pixel)0.4;
            speckle.at(x, y, 2) = a ? (Pixel)0.3 : (Pixel)0.5;
        }
    NoiseReduction cnr; cnr.setColor(100.f);
    Image out = cnr.apply(speckle);
    double before = std::fabs(speckle.at(0, 0, 0) - speckle.at(0, 0, 2));
    double after = std::fabs(out.at(0, 0, 0) - out.at(0, 0, 2));
    CHECK(after < before);  // chroma spread reduced
}

TEST(Texture_and_Clarity_local_contrast)
{
    Image step = grayStep(16, 4, (Pixel)0.3, (Pixel)0.7);

    Texture tid; CHECK_NEAR(tid.apply(step).at(0, 0, 0), 0.3, 1e-5);  // 0 -> identity
    Clarity cid; CHECK_NEAR(cid.apply(step).at(0, 0, 0), 0.3, 1e-5);

    auto edgeGap = [](const Image &im) {
        int w = im.width(); return (double)im.at(w / 2, 1, 0) - im.at(w / 2 - 1, 1, 0);
    };
    const double base = edgeGap(step);

    Texture t; t.setAmount(100.f);
    CHECK(edgeGap(t.apply(step)) > base);   // adds fine contrast across the edge

    Clarity c; c.setAmount(100.f);
    CHECK(edgeGap(c.apply(step)) > base);   // adds midtone local contrast
    Clarity cn; cn.setAmount(-100.f);
    CHECK(edgeGap(cn.apply(step)) < base);  // negative softens
}

TEST(LensCorrection_identity_vignette_distortion)
{
    // varied colour gradient so geometric ops are observable
    Image g(9, 9, 3, ColorSpace::LinearSRGB);
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x)
        {
            g.at(x, y, 0) = (Pixel)x / 8;
            g.at(x, y, 1) = (Pixel)y / 8;
            g.at(x, y, 2) = (Pixel)0.5;
        }

    LensCorrection id;  // all zero -> exact identity
    Image i = id.apply(g);
    CHECK_NEAR(i.at(3, 5, 0), g.at(3, 5, 0), 1e-6);
    CHECK_NEAR(i.at(8, 0, 1), g.at(8, 0, 1), 1e-6);

    LensCorrection vig; vig.setVignette(-100.f);
    Image v = vig.apply(solidLinear(9, 9, 3, (Pixel)0.5));
    CHECK_NEAR(v.at(4, 4, 0), 0.5, 1e-4);  // centre unchanged
    CHECK(v.at(0, 0, 0) < 0.05);           // corner darkened to ~0

    LensCorrection dist; dist.setDistortion(100.f);
    Image dimg = dist.apply(g);
    bool remapped = false;
    for (int y = 0; y < 9 && !remapped; ++y)
        for (int x = 0; x < 9; ++x)
            if (std::fabs(dimg.at(x, y, 0) - g.at(x, y, 0)) > 1e-4) { remapped = true; break; }
    CHECK(remapped);  // geometry remapped somewhere in the frame

    LensCorrection ca; ca.setChromaticAberration(100.f);
    Image cao = ca.apply(g);
    CHECK(cao.at(1, 1, 0) != g.at(1, 1, 0) || cao.at(1, 1, 2) != g.at(1, 1, 2));  // R/B shifted
}

MINITEST_MAIN
