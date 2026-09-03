/*
 *  Unit tests for the ImageProcessing core (base types + processors + analysis).
 *  Dependency-free MiniTest harness. main() lives here; engineTests.cpp registers
 *  additional cases into the shared registry.
 */
#include "MiniTest.h"
#include "image_processing.h"
#include <cmath>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

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

// ── ColorMixer: a per-hue effect must not fire on a pixel that has no hue ──────────────
//
// Hue is computed by dividing channel DIFFERENCES by chroma (ColorSpace.cpp), so as chroma
// goes to zero the hue of a pixel is decided by its last bit of noise: neighbouring pixels in
// a flat grey area read as red, green and blue at random. The Lum channel is additive
// (`l += y*0.5`), so before the chroma weight each of those pixels took a DIFFERENT full-strength
// lift and a smooth grey turned into speckle — reported as "random noise particles got lit up".
//
// The measurement, rather than an opinion about it: take a flat grey with a little noise, apply a
// lum curve that lifts hard at one hue, and compare the spread of the output with the spread of
// the input. Amplifying the noise is the defect; leaving it alone is the fix.
TEST(ColorMixer_lum_curve_does_not_amplify_noise_in_a_grey)
{
    auto spread = [](const Image &im) {                    // stddev of luminance, in linear light
        double sum = 0, sum2 = 0;
        const int n = im.width() * im.height();
        for (int y = 0; y < im.height(); ++y)
            for (int x = 0; x < im.width(); ++x)
            {
                const double l = 0.2126 * im.at(x, y, 0) + 0.7152 * im.at(x, y, 1) + 0.0722 * im.at(x, y, 2);
                sum += l; sum2 += l * l;
            }
        const double mean = sum / n;
        const double var = sum2 / n - mean * mean;
        return var > 0 ? std::sqrt(var) : 0.0;
    };

    // A flat grey with ±1/255 of per-channel noise: chroma is tiny, hue is meaningless, and the
    // pattern is deterministic so the numbers below are reproducible.
    Image grey(48, 48, 3, ColorSpace::LinearSRGB);
    unsigned seed = 12345u;   // a plain LCG: reproducible, and genuinely varying per pixel
    auto lsb = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return (int)((seed >> 16) % 3u) - 1;   // -1, 0 or +1
    };
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 48; ++x)
            for (int c = 0; c < 3; ++c)
                grey.at(x, y, c) = (Pixel)0.18 + (Pixel)lsb() * (Pixel)(1.0 / 255.0);

    // A curve that SWINGS with hue: +1 at red, -1 at cyan. That is what turns "each pixel read a
    // different random hue" into "each pixel got a different lift" — a flat curve cannot show the
    // defect at all, because it lifts every hue by the same amount.
    ColorMixer lift;
    lift.setCurve(ColorMixer::Lum, {{0.f, 1.f}, {180.f, -1.f}});
    const Image out = lift.apply(grey);

    const double before = spread(grey), after = spread(out);
    std::printf("      grey spread %.5f -> %.5f\n", before, after);
    // The grey still moves as a whole — a constant curve is a constant offset — but it must not
    // FAN OUT. 1.5x leaves room for the HSL round trip; the unweighted code multiplies it by ~40.
    CHECK(after < before * 1.5);
    // ...and it is genuinely still grey, not tinted by whichever hue each pixel happened to read.
    CHECK(satOf(out) < 0.05);

    // The other half of the contract: a pixel that HAS a hue still gets the full lift.
    const Image colour = pxHsl(0, (Pixel)0.8, (Pixel)0.4);    // at the curve's +1 peak
    Pixel h0, s0, l0, h1, s1, l1;
    color::rgbToHsl(colour.at(0, 0, 0), colour.at(0, 0, 1), colour.at(0, 0, 2), h0, s0, l0);
    const Image lifted = lift.apply(colour);
    color::rgbToHsl(lifted.at(0, 0, 0), lifted.at(0, 0, 1), lifted.at(0, 0, 2), h1, s1, l1);
    CHECK(l1 - l0 > 0.4);   // ~+0.5, undiminished by the weight

    // And the weight is a ramp, not a switch: a pastel gets part of the lift, between the two.
    const Image pastel = pxHsl(0, (Pixel)0.03, (Pixel)0.4);   // chroma 0.024: mid-ramp
    Pixel ph, ps, pl0, qh, qs, pl1;
    color::rgbToHsl(pastel.at(0, 0, 0), pastel.at(0, 0, 1), pastel.at(0, 0, 2), ph, ps, pl0);
    const Image plifted = lift.apply(pastel);
    color::rgbToHsl(plifted.at(0, 0, 0), plifted.at(0, 0, 1), plifted.at(0, 0, 2), qh, qs, pl1);
    CHECK(pl1 - pl0 > 0.02);
    CHECK(pl1 - pl0 < 0.45);
}


// ── ColorMixer: whether a pixel BELONGS to a colour is a question about its neighbours ──
//
// R-MIXER-1 fixed the speckle by refusing the noise pixel, which is only half an answer. The
// grain inside a red flower is red grain — it just does not READ as red — so the flower moved
// and the grain did not, leaving it peppered. The same hole swallows BOKEH: a defocused green
// is a smeared green, its chroma is below the floor, and a per-pixel gate leaves it behind
// while the in-focus green moves.
//
// Both halves are measured here against the SAME image with `spread == 0`, which is exactly the
// pre-R-MIXER-5 behaviour — so this test fails without the fix by construction rather than by
// assertion, and the third block guards the thing the naive implementation gets wrong.
TEST(ColorMixer_spread_treats_noise_and_bokeh_as_part_of_their_colour)
{
    const int N = 512;   // 0.4% of 512 is ~2 px of sigma at full spread: a spread, not a smear
    auto lumOf = [](const Image &im, int x, int y) {
        return 0.2126 * im.at(x, y, 0) + 0.7152 * im.at(x, y, 1) + 0.0722 * im.at(x, y, 2);
    };

    // A curve that lifts RED hard and does nothing anywhere else, so every number below is
    // "how much did this pixel get treated as red".
    auto liftRed = [](float spread) {
        ColorMixer m;
        m.setCurve(ColorMixer::Lum, {{0.f, 1.f}, {60.f, 0.f}, {300.f, 0.f}});
        m.setSpread(spread);
        return m;
    };

    // ── 1. noise inside a colour ──
    // A field of saturated red with one pixel in every 8x8 knocked to neutral grey of the same
    // luminance: chroma noise, which is what a high-ISO red actually looks like up close.
    Image red(N, N, 3, ColorSpace::LinearSRGB);
    const Image redPx = pxHsl(0, (Pixel)0.8, (Pixel)0.4);
    const Image greyPx = solidLinear(1, 1, 3, (Pixel)0.13);   // ~the same luminance, no chroma
    std::vector<std::pair<int, int>> noisy;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
        {
            const bool dead = (x % 8 == 3) && (y % 8 == 5);
            for (int c = 0; c < 3; ++c)
                red.at(x, y, c) = dead ? greyPx.at(0, 0, c) : redPx.at(0, 0, c);
            if (dead) noisy.push_back({x, y});
        }

    auto meanLift = [&](const Image &before, const Image &after,
                        const std::vector<std::pair<int, int>> &pts) {
        double s = 0;
        for (auto &p : pts) s += lumOf(after, p.first, p.second) - lumOf(before, p.first, p.second);
        return pts.empty() ? 0.0 : s / (double)pts.size();
    };

    ColorMixer off = liftRed(0.f), on = liftRed(1.f);
    const Image redOff = off.apply(red), redOn = on.apply(red);
    const double noiseOff = meanLift(red, redOff, noisy);
    const double noiseOn = meanLift(red, redOn, noisy);
    std::printf("      noise pixel lift  spread 0: %+.4f   spread 1: %+.4f\n", noiseOff, noiseOn);
    CHECK(noiseOff < 0.01);          // the defect: the grain stays where it was...
    CHECK(noiseOn > 0.10);           // ...and now it comes along with its neighbours

    // ── 2. bokeh ──
    // A saturated red disc smeared into a neutral background — the chroma falls off with the
    // blur, so the fringe reads as neutral to a per-pixel gate even though it is plainly red.
    Image bokeh(N, N, 3, ColorSpace::LinearSRGB);
    const Image discPx = pxHsl(0, (Pixel)0.8, (Pixel)0.4);   // the SAME hue the curve lifts
    const double cx = N * 0.5, cy = N * 0.5, R = N * 0.22, soft = N * 0.05;
    std::vector<std::pair<int, int>> fringe;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
        {
            const double d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
            double t = (d - R) / soft;                       // 0 at the core edge, 1 outside
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            const double k = 1.0 - t * t * (3.0 - 2.0 * t);  // coverage of the defocused disc
            for (int c = 0; c < 3; ++c)
                bokeh.at(x, y, c) = (Pixel)(greyPx.at(0, 0, c) + (discPx.at(0, 0, c) - greyPx.at(0, 0, c)) * k);
            // The band where the disc has faded to a HINT of colour — chroma 0.002..0.010
            // against a floor of 0.010, so the per-pixel gate declines it outright. Chosen by
            // the disc's own coverage rather than by a radius: it is the chroma that decides
            // whether the gate fires, so that is what the band has to be picked on.
            if (k > 0.003 && k < 0.016) fringe.push_back({x, y});
        }

    const Image bokehOff = off.apply(bokeh), bokehOn = on.apply(bokeh);
    const double fringeOff = meanLift(bokeh, bokehOff, fringe);
    const double fringeOn = meanLift(bokeh, bokehOn, fringe);
    std::printf("      bokeh fringe lift spread 0: %+.4f   spread 1: %+.4f\n", fringeOff, fringeOn);
    CHECK(fringeOff < 0.02);                     // the defect: the bokeh is left behind...
    CHECK(fringeOn > fringeOff * 3.0 + 0.02);    // ...and now it joins the colour it came from

    // ── 3. R-MIXER-6: the spread may only ADD reach ──
    // A red square SMALLER than the blur radius, on grey. Taking the softened value outright
    // would dilute it — the square would move LESS than it does today, which is a regression for
    // every project that already exists. Larger-magnitude-wins is what forbids that.
    Image small(N, N, 3, ColorSpace::LinearSRGB);
    std::vector<std::pair<int, int>> square, outside;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
        {
            const bool in = x >= 250 && x < 254 && y >= 250 && y < 254;   // 4x4, sigma is ~2
            for (int c = 0; c < 3; ++c)
                small.at(x, y, c) = in ? redPx.at(0, 0, c) : greyPx.at(0, 0, c);
            if (in) square.push_back({x, y});
            if (x >= 258 && x < 262 && y >= 250 && y < 254) outside.push_back({x, y});
        }
    const Image smallOff = off.apply(small), smallOn = on.apply(small);
    const double sqOff = meanLift(small, smallOff, square);
    const double sqOn = meanLift(small, smallOn, square);
    std::printf("      small square lift spread 0: %+.4f   spread 1: %+.4f\n", sqOff, sqOn);
    CHECK(sqOn >= sqOff - 1e-6);   // never less than it moves today — the whole point of R-MIXER-6
    // ...and the grey just outside picks a little of it up, which is the spread doing its job.
    CHECK(meanLift(small, smallOn, outside) > meanLift(small, smallOff, outside));

    // ── 4. and R-MIXER-1 still holds: a spread of nothing is still nothing ──
    // The flat grey that started all of this. The spread averages a plane that is zero
    // everywhere, so it cannot resurrect the speckle it was built to prevent.
    Image flat(N, N, 3, ColorSpace::LinearSRGB);
    unsigned seed = 7u;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
            for (int c = 0; c < 3; ++c)
            {
                seed = seed * 1664525u + 1013904223u;
                flat.at(x, y, c) = (Pixel)0.18 + (Pixel)((int)((seed >> 16) % 3u) - 1) * (Pixel)(1.0 / 255.0);
            }
    ColorMixer swing;
    swing.setCurve(ColorMixer::Lum, {{0.f, 1.f}, {180.f, -1.f}});
    swing.setSpread(1.f);
    CHECK(satOf(swing.apply(flat)) < 0.05);
}

// The spread's blur is `fastBlurPlane`, not the exact kernel, because sigma is a fraction of
// the image (R-MIXER-7) and an O(pixels x sigma) kernel would make a full-resolution export pay
// for its own size twice. What a WEIGHT plane needs from a blur is a radius, smoothness and its
// total weight back — not the precise shape of a Gaussian tail — so that is what is asserted.
// A three-box cascade is measurably boxy at small sigma (the box widths are integers and there
// are only two of them to choose from), which is why it delegates to the exact kernel below
// sigma 4 — the regime where the exact kernel is also the cheap one. Both regimes are covered
// here, and the effective-radius bound is the one that matters to a weight plane.
TEST(Spatial_fastBlurPlane_approximates_the_gaussian_at_constant_cost)
{
    const int w = 129, h = 65;
    std::vector<Pixel> impulse((size_t)w * h, (Pixel)0);
    impulse[(size_t)(h / 2) * w + w / 2] = (Pixel)1;

    // Effective sigma straight out of the blurred impulse: the second moment of a separable
    // 2D kernel is 2*sigma^2, so this measures the radius the caller actually got.
    auto effectiveSigma = [&](const std::vector<Pixel> &pl) {
        double mass = 0, m2 = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const double v = pl[(size_t)y * w + x];
                const double dx = x - w / 2, dy = y - h / 2;
                mass += v; m2 += v * (dx * dx + dy * dy);
            }
        return mass > 0 ? std::sqrt(m2 / mass / 2.0) : 0.0;
    };

    for (float sigma : {1.0f, 3.0f, 6.0f, 16.0f})
    {
        std::vector<Pixel> exact, fast;
        spatial::gaussianBlurPlane(impulse, exact, w, h, sigma);
        spatial::fastBlurPlane(impulse, fast, w, h, sigma);

        double peak = 0, worst = 0, sumE = 0, sumF = 0;
        for (size_t i = 0; i < impulse.size(); ++i)
        {
            peak = std::max(peak, (double)exact[i]);
            worst = std::max(worst, std::fabs((double)exact[i] - (double)fast[i]));
            sumE += exact[i]; sumF += fast[i];
        }
        const double se = effectiveSigma(fast);
        std::printf("      sigma %.0f: effective %.2f  worst diff %.5f of peak %.5f  mass %.4f vs %.4f\n",
                    sigma, se, worst, peak, sumE, sumF);
        CHECK(std::fabs(se - sigma) < sigma * 0.07);   // the radius asked for, to within 7%
        CHECK(std::fabs(sumE - sumF) < 0.02);          // and the plane's total weight back
        // Shape: exact below the crossover, and a cascade above it that is boxier in the tail
        // than a Gaussian by a tenth of its own peak — which a weight plane does not care about.
        CHECK(worst < peak * (sigma < 4.f ? 1e-4 : 0.12));
    }

    // In place, and a step edge stays monotone (a box cascade with the wrong radii rings).
    std::vector<Pixel> step((size_t)w * h, (Pixel)0);
    for (int y = 0; y < h; ++y)
        for (int x = w / 2; x < w; ++x) step[(size_t)y * w + x] = (Pixel)1;
    spatial::fastBlurPlane(step, step, w, h, 4.f);
    bool monotone = true;
    for (int x = 1; x < w; ++x)
        if (step[(size_t)(h / 2) * w + x] < step[(size_t)(h / 2) * w + x - 1] - 1e-6f) monotone = false;
    CHECK(monotone);
    CHECK(step[(size_t)(h / 2) * w + 0] < 0.01f);      // clamped edges, not wrapped
    CHECK(step[(size_t)(h / 2) * w + w - 1] > 0.99f);
}


// ── Segmenter + Detection: a mask that finds its own subject (R-AISEG) ────────────────
//
// The scene is synthetic and deliberately adversarial rather than easy. A neutral-green ground,
// a FACE and two HANDS in one skin tone, and — the point of the whole fixture — a TERRACOTTA
// PATCH in another. Both tones sit inside deepgaze's published skin range (hue 0..60, sat >=
// 0.23, val >= 0.20), so a range gate alone takes the patch exactly as confidently as it takes
// the face. Only the back-projection can tell them apart, and only in one direction: it scores
// the patch DOWN in proportion to how much SMALLER it is than the dominant skin-coloured region
// (R-AISEG-23), which is why it is 2.5% of this frame against the face's 11%. A patch bigger
// than the face would win, and the requirement says so.
// Every assertion below is "the right region and not the wrong one"; a detector that only ever
// said yes would pass a one-region test.
static Image sceneForSegmentation(int n)
{
    auto put = [](Image &im, int x, int y, int r8, int g8, int b8) {
        // Written display-referred and decoded on the way in, because that is the direction a
        // real photo arrives from and because the detector's thresholds are stated in those units.
        im.at(x, y, 0) = color::srgbDecode((Pixel)(r8 / 255.0));
        im.at(x, y, 1) = color::srgbDecode((Pixel)(g8 / 255.0));
        im.at(x, y, 2) = color::srgbDecode((Pixel)(b8 / 255.0));
    };
    auto inEllipse = [](float nx, float ny, float cx, float cy, float rx, float ry) {
        const float dx = (nx - cx) / rx, dy = (ny - cy) / ry;
        return dx * dx + dy * dy <= 1.f;
    };
    Image im(n, n, 3, ColorSpace::LinearSRGB);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            const float ny = (y + 0.5f) / n, nx = (x + 0.5f) / n;
            if (inEllipse(nx, ny, 0.38f, 0.34f, 0.17f, 0.21f))        // the face
                put(im, x, y, 226, 178, 148);
            else if (inEllipse(nx, ny, 0.22f, 0.76f, 0.065f, 0.055f)  // left hand
                     || inEllipse(nx, ny, 0.54f, 0.79f, 0.065f, 0.055f))   // right hand
                put(im, x, y, 226, 178, 148);
            else if (nx > 0.90f && ny < 0.25f)                        // the terracotta patch
                put(im, x, y, 172, 96, 70);
            else                                                      // ground
                put(im, x, y, 82, 118, 84);
        }
    return im;
}

// Mean over a normalised rectangle — "how much of this region did the detector take".
static double meanOver(const std::vector<Pixel> &cov, int n, float x0, float y0, float x1, float y1)
{
    double sum = 0; int count = 0;
    for (int y = (int)(y0 * n); y < (int)(y1 * n); ++y)
        for (int x = (int)(x0 * n); x < (int)(x1 * n); ++x)
        { sum += cov[(size_t)y * n + x]; ++count; }
    return count ? sum / count : 0.0;
}

// R-AISEG-22: Skin, and only Skin. The other four are WITHDRAWN, not deleted — their enum
// values are what project files store, so what has to be true is that they parse, that the
// built-in says it cannot answer for them, and that asking anyway gives an empty plane rather
// than a wrong one.
TEST(Segmenter_answers_for_skin_and_declines_the_withdrawn_subjects)
{
    CHECK(segment::builtinHandles(SemanticSubject::Skin));
    for (SemanticSubject s : {SemanticSubject::Sky, SemanticSubject::Foliage,
                              SemanticSubject::Water, SemanticSubject::Hair,
                              SemanticSubject::Person})
        CHECK(!segment::builtinHandles(s));

    const int n = 256;
    const Image scene = sceneForSegmentation(n);
    std::vector<Pixel> score;
    segment::builtinScore(scene, SemanticSubject::Sky, score);
    CHECK(score.size() == (size_t)n * n && "a declined subject still gets a sized plane");
    double total = 0;
    for (Pixel v : score) total += v;
    CHECK(total == 0.0 && "and every value in it is zero, not a guess");
}

// The assertion the whole rework rests on (R-AISEG-23). Both tones are inside deepgaze's range
// gate, so step 1 alone cannot separate them; the histogram normalises against its busiest
// colour, which is the face, and the wall's own mode is scored down against it.
TEST(Segmenter_backprojection_scores_a_wall_down_against_a_larger_face)
{
    const int n = 256;
    const Image scene = sceneForSegmentation(n);
    std::vector<Pixel> score;
    segment::builtinScore(scene, SemanticSubject::Skin, score);

    const double face = meanOver(score, n, 0.32f, 0.28f, 0.44f, 0.40f);
    const double hand = meanOver(score, n, 0.20f, 0.74f, 0.24f, 0.78f);
    const double wall = meanOver(score, n, 0.93f, 0.04f, 0.99f, 0.21f);
    const double ground = meanOver(score, n, 0.05f, 0.90f, 0.35f, 0.98f);
    std::printf("      score: face %.3f  hand %.3f  wall %.3f  ground %.3f\n",
                face, hand, wall, ground);
    CHECK(face > 0.85);
    CHECK(hand > 0.85 && "a hand is the same tone as the face and must score with it");
    CHECK(ground < 0.02 && "the gate refuses green outright");
    // Not "the patch is zero" — it is skin-coloured and this is a colour detector. What must be
    // true is that it is far enough below the face for the threshold in `scoreToCoverage` to
    // separate them. A fixed hue band scored the two IDENTICALLY, and that is the improvement.
    CHECK(wall < face * 0.5);

    std::vector<Pixel> cov = score;
    segment::scoreToCoverage(cov, n, n, 0.5f);
    std::printf("      coverage @0.5: face %.3f  wall %.3f\n",
                meanOver(cov, n, 0.32f, 0.28f, 0.44f, 0.40f),
                meanOver(cov, n, 0.93f, 0.04f, 0.99f, 0.21f));
    CHECK(meanOver(cov, n, 0.32f, 0.28f, 0.44f, 0.40f) > 0.95);
    CHECK(meanOver(cov, n, 0.93f, 0.04f, 0.99f, 0.21f) < 0.10);
}

// R-AISEG-23: too few seeds is an ANSWER — "there is no skin here" — and not a licence to build
// a colour model out of stray pixels and then find it everywhere. This is the failure mode that
// makes a detector confident and wrong, so it is asserted rather than assumed.
TEST(Segmenter_declines_a_photo_with_no_skin_in_it)
{
    const int n = 128;
    Image green(n, n, 3, ColorSpace::LinearSRGB);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            green.at(x, y, 0) = color::srgbDecode((Pixel)(60 / 255.0));
            green.at(x, y, 1) = color::srgbDecode((Pixel)(140 / 255.0));
            green.at(x, y, 2) = color::srgbDecode((Pixel)(60 / 255.0));
        }
    std::vector<Pixel> score;
    segment::builtinScore(green, SemanticSubject::Skin, score);
    double total = 0;
    for (Pixel v : score) total += v;
    CHECK(total == 0.0);
}

// R-AISEG-23: the model is fitted to the pixels of THIS photograph, so a photograph taken
// under a strong cast is not half-refused by a rule written for daylight. The invariant is what
// is asserted — the same face under a heavy tungsten cast must still come back as the same
// region — because that is the promise a photographer relies on, and a fixed band can only keep
// it for as long as its author guessed the lighting right.
TEST(Segmenter_is_not_thrown_by_a_heavy_colour_cast)
{
    const int n = 256;
    const Image plain = sceneForSegmentation(n);
    Image cast = plain.clone();
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            cast.at(x, y, 1) = (Pixel)((double)cast.at(x, y, 1) * 0.62);   // tungsten: G and B
            cast.at(x, y, 2) = (Pixel)((double)cast.at(x, y, 2) * 0.32);   // pulled right down
        }

    std::vector<Pixel> a, b;
    segment::builtinScore(plain, SemanticSubject::Skin, a);
    segment::builtinScore(cast, SemanticSubject::Skin, b);
    segment::scoreToCoverage(a, n, n, 0.5f);
    segment::scoreToCoverage(b, n, n, 0.5f);
    const double faceA = meanOver(a, n, 0.32f, 0.28f, 0.44f, 0.40f);
    const double faceB = meanOver(b, n, 0.32f, 0.28f, 0.44f, 0.40f);
    std::printf("      face under daylight %.3f, under tungsten %.3f\n", faceA, faceB);
    CHECK(faceA > 0.95);
    CHECK(faceB > 0.95);
}

// R-AISEG-5: sensitivity is the threshold and it is the whole control. Monotone, in the
// direction the label promises — a photographer turning it up must never watch the mask shrink.
TEST(Segmenter_sensitivity_is_the_only_knob_and_it_is_monotone)
{
    const int n = 256;
    const Image scene = sceneForSegmentation(n);
    std::vector<Pixel> base;
    segment::builtinScore(scene, SemanticSubject::Skin, base);
    double last = -1;
    for (float s : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
    {
        std::vector<Pixel> cov = base;
        segment::scoreToCoverage(cov, n, n, s);
        double total = 0;
        for (Pixel v : cov) total += v;
        const double frac = total / (double)cov.size();
        std::printf("      sensitivity %.2f -> %.3f of the frame\n", s, frac);
        CHECK(frac >= last - 1e-6);
        last = frac;
    }
    // ...and it is a threshold on a REGION, not a global gain: even wide open it must not have
    // taken the whole frame, or the control would be a fader and the mask would be pointless.
    CHECK(last < 0.85);
}

// R-PREVIEW, via `kAnalysisEdge`: the SAME photo at two sizes must give the same regions, not
// two similar sets. It matters more now that the answer is STORED (R-AISEG-21): a detection run
// on a preview is the mask the exported file gets.
TEST(Detection_finds_the_same_regions_at_any_render_size)
{
    struct Run { int n; DetectionResult r; };
    Run a{256, {}}, b{1536, {}};   // 1536 forces the 1/2 analysis path; 256 does not
    a.r = detectSubjectRegions(sceneForSegmentation(a.n), SemanticSubject::Skin, 0.5f);
    b.r = detectSubjectRegions(sceneForSegmentation(b.n), SemanticSubject::Skin, 0.5f);
    std::printf("      256px: %zu regions, %.3f covered   1536px: %zu regions, %.3f covered\n",
                a.r.regions.size(), (double)a.r.coverage, b.r.regions.size(), (double)b.r.coverage);
    CHECK(a.r.regions.size() == b.r.regions.size());
    CHECK(std::fabs((double)a.r.coverage - (double)b.r.coverage) < 0.02);
}

// R-AISEG-21: the whole pipeline, end to end — what it found, that the blob rule kept the two
// small hands beside the large face, and that every loop is real geometry a mask can hold.
TEST(Detection_returns_one_loop_per_region_and_keeps_the_hands)
{
    const int n = 512;
    std::vector<std::pair<std::string, float>> seen;
    const DetectionResult r =
        detectSubjectRegions(sceneForSegmentation(n), SemanticSubject::Skin, 0.5f, nullptr,
                             [&](const char *stage, float f) { seen.push_back({stage, f}); });

    CHECK(r.handled);
    CHECK(r.by == std::string("built-in"));
    std::printf("      %zu regions, %.1f%% of the frame, by %s\n",
                r.regions.size(), (double)r.coverage * 100.0, r.by.c_str());
    for (const ContourLoop &l : r.regions)
        std::printf("        loop: %zu points, area %.4f\n", l.size(), (double)loopArea(l));
    // Face + two hands. deepgaze's own BinaryMaskAnalyser keeps only the LARGEST contour, which
    // would return one region here — this is the assertion that the rule was deliberately
    // changed (R-AISEG-23 step 4) and not merely copied.
    CHECK(r.regions.size() == 3);
    for (const ContourLoop &l : r.regions)
    {
        CHECK((int)l.size() >= detect::kMinLoopPoints);
        CHECK(loopArea(l) >= detect::kMinLoopArea);
        for (const auto &pt : l)
            CHECK(pt.first >= -0.01f && pt.first <= 1.01f && pt.second >= -0.01f && pt.second <= 1.01f);
    }
    // The face ellipse is 0.17 x 0.21 of the frame -> pi*r*r = 0.112, the two hands a further
    // 0.022. The wall is thresholded away (see the back-projection test), so ~13% is the answer.
    CHECK(r.coverage > 0.09f && r.coverage < 0.20f);

    // R-AISEG-20: every stage reported, in order, with a monotone fraction that ends at 1.
    std::string order;
    float last = -1.f;
    for (const auto &s : seen) { order += s.first + " "; CHECK(s.second >= last); last = s.second; }
    std::printf("      stages: %s\n", order.c_str());
    CHECK(order == "preparing colour regions shapes outline outline ");
    CHECK(last == 1.f);
}

// R-AISEG-22 again, one level up: a withdrawn subject reaches the detection and comes back
// `handled == false` — which is a DIFFERENT sentence from "found nothing" and the UI needs both.
TEST(Detection_reports_that_nobody_handles_a_withdrawn_subject)
{
    const DetectionResult r =
        detectSubjectRegions(sceneForSegmentation(256), SemanticSubject::Sky, 0.5f);
    CHECK(!r.handled);
    CHECK(r.regions.empty());
    CHECK(r.coverage == 0.f);
}

// R-AISEG-23 step 3, on planes small enough to check by hand: a morphology bug is invisible in
// a photograph and obvious in a 9x9 grid.
TEST(Detection_morphology_opens_specks_away_and_closes_pinholes)
{
    const int n = 11;
    auto make = [&](std::initializer_list<const char *> rows) {
        std::vector<Pixel> p((size_t)n * n, (Pixel)0);
        int y = 0;
        for (const char *row : rows)
        {
            for (int x = 0; x < n; ++x) p[(size_t)y * n + x] = (Pixel)(row[x] == '#' ? 1 : 0);
            ++y;
        }
        return p;
    };
    auto count = [&](const std::vector<Pixel> &p) {
        int c = 0;
        for (Pixel v : p) c += (float)v >= 0.5f ? 1 : 0;
        return c;
    };

    // A 7x7 block with a one-pixel pinhole, and a lone speck in the corner. The block is big
    // enough to survive an erode at radius 1 — a 5x5 one is not, which is worth knowing: the
    // opening radius has to be small relative to the smallest region worth keeping, which is
    // exactly why `kOpenFraction` is a fraction of the short edge and not a pixel count.
    std::vector<Pixel> p = make({"#..........",
                                 "...........",
                                 "..#######..",
                                 "..#######..",
                                 "..#######..",
                                 "..###.###..",
                                 "..#######..",
                                 "..#######..",
                                 "..#######..",
                                 "...........",
                                 "..........."});
    CHECK(count(p) == 49);   // 48 of the block + the speck

    // Opening = erode then dilate. The erode takes the speck, the block's one-pixel rim, and the
    // ring around the pinhole; the dilate puts the rim back and cannot put the speck back,
    // because nothing survived there to grow from. That asymmetry IS the opening.
    detect::erodePlane(p, n, n, 1);
    CHECK(count(p) == 16 && "a 5x5 core minus the 3x3 the pinhole ate, and no speck");
    detect::dilatePlane(p, n, n, 1);
    CHECK(count(p) == 48 && "the block is back, still with its pinhole, and the speck is not");

    // Closing = dilate then erode, and it is what fills the pinhole.
    detect::dilatePlane(p, n, n, 1);
    detect::erodePlane(p, n, n, 1);
    CHECK(count(p) == 49 && "48 + the filled pinhole");
    CHECK((float)p[(size_t)5 * n + 5] >= 0.5f);
    CHECK((float)p[(size_t)0 * n + 0] < 0.5f && "and the speck did not come back");
}

// R-AISEG-23 step 4: big enough relative to the largest AND not trivially small, both rules.
TEST(Detection_blob_filter_keeps_the_significant_and_drops_the_rest)
{
    const int n = 100;
    std::vector<Pixel> p((size_t)n * n, (Pixel)0);
    auto box = [&](int x0, int y0, int w, int h) {
        for (int y = y0; y < y0 + h; ++y)
            for (int x = x0; x < x0 + w; ++x) p[(size_t)y * n + x] = (Pixel)1;
    };
    box(5, 5, 40, 40);     // 1600 px, the largest
    box(60, 5, 15, 15);    // 225 px = 14% of the largest -> kept
    box(60, 60, 4, 4);     // 16 px = 1% of the largest -> dropped
    const int kept = detect::keepSignificantBlobs(p, n, n);
    int on = 0;
    for (Pixel v : p) on += (float)v >= 0.5f ? 1 : 0;
    std::printf("      kept %d blobs, %d pixels of 1841\n", kept, on);
    CHECK(kept == 2);
    CHECK(on == 1600 + 225);
}

// R-AISEG-14 as amended: the contour is thinned by SHAPE, not by sampling less of it. Both
// halves are asserted, because either alone is satisfiable by a bug — dropping every point
// makes it small, keeping every point makes it accurate.
TEST(Contour_simplify_is_small_and_still_within_its_tolerance)
{
    ContourLoop circle;
    for (int i = 0; i < 720; ++i)
    {
        const double a = i * 2.0 * 3.14159265358979 / 720.0;
        circle.push_back({(float)(0.5 + 0.3 * std::cos(a)), (float)(0.5 + 0.3 * std::sin(a))});
    }
    const float tol = 0.002f;
    const ContourLoop simple = simplifyLoop(circle, tol);
    std::printf("      %zu points -> %zu, area %.4f -> %.4f\n",
                circle.size(), simple.size(), (double)loopArea(circle), (double)loopArea(simple));
    CHECK(simple.size() < circle.size() / 8);
    CHECK(simple.size() >= 3);
    // Every ORIGINAL point is still within the tolerance of the simplified outline — which is
    // the actual promise, and is not the same as the two polygons having a similar area.
    double worst = 0;
    for (const auto &pt : circle)
    {
        double best = 1e9;
        for (size_t i = 0, j = simple.size() - 1; i < simple.size(); j = i++)
        {
            const double ax = simple[j].first, ay = simple[j].second;
            const double bx = simple[i].first, by = simple[i].second;
            const double dx = bx - ax, dy = by - ay;
            const double len2 = dx * dx + dy * dy;
            double t = len2 > 0 ? ((pt.first - ax) * dx + (pt.second - ay) * dy) / len2 : 0;
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            const double ex = pt.first - (ax + dx * t), ey = pt.second - (ay + dy * t);
            best = std::min(best, std::sqrt(ex * ex + ey * ey));
        }
        worst = std::max(worst, best);
    }
    std::printf("      worst deviation %.5f (tolerance %.5f)\n", worst, (double)tol);
    CHECK(worst <= tol * 1.001);
    // ...and a loop that is already minimal is left alone rather than destroyed.
    ContourLoop tri{{0.1f, 0.1f}, {0.9f, 0.2f}, {0.5f, 0.9f}};
    CHECK(simplifyLoop(tri, 0.5f).size() == 3);
}

TEST(Segmenter_subject_names_are_one_codec)
{
    SemanticSubject s = SemanticSubject::Hair;
    CHECK(parseSemanticSubject("sky", s) && s == SemanticSubject::Sky);
    CHECK(parseSemanticSubject("SKIN", s) && s == SemanticSubject::Skin);
    CHECK(parseSemanticSubject("3", s) && s == SemanticSubject::Water);
    CHECK(!parseSemanticSubject("skyy", s));
    CHECK(!parseSemanticSubject("", s));
    CHECK(!parseSemanticSubject("9", s));
    CHECK(s == SemanticSubject::Water && "a refusal leaves the caller's value alone");
    // R-AISEG-22: a WITHDRAWN subject still parses. It has to — a project file holds the number
    // and a script may hold the name — and the place to find out that nobody answers for it is
    // the detection, which says so, not the parser, which would say "syntax error".
    for (int i = 0; i < (int)SemanticSubject::Count; ++i)
    {
        SemanticSubject back{};
        CHECK(parseSemanticSubject(semanticSubjectName((SemanticSubject)i), back));
        CHECK((int)back == i);
    }
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

// ── R-PREVIEW-2 / T1: the parallelFor pool ───────────────────────────────────────
//
// parallelFor now keeps a persistent pool and cuts the range into many more chunks than
// there are threads, claimed by an atomic counter. The MOTIVE is big.LITTLE — equal
// static slices made every parallel pass finish at little-core speed — and that motive
// cannot be tested on a symmetric desktop. What CAN be tested, and is what would
// actually break, are the properties the contract in Parallel.h promises:
//
//   * every index is visited exactly once, whatever the schedule;
//   * the SPLIT is deterministic even though the SCHEDULE is not — the byte-identical
//     serial-vs-parallel guarantee rests on chunk boundaries coming from the chunk
//     INDEX, not from which thread happened to claim it;
//   * a nested parallelFor runs serially instead of deadlocking on an exhausted pool;
//   * setThreads() can grow and shrink the pool repeatedly without losing work.
//
// The load-balancing behaviour itself is measured by
// apps/cosmo/core/tests/fixtures/parallel_imbalance.cpp, which prints numbers rather
// than asserting on a clock — a timing assertion in a unit suite is a flake generator.

TEST(ParallelFor_visits_every_index_exactly_once)
{
    for (int t : {1, 2, 3, 4, 8, 16, 24})
    {
        par::setThreads(t);
        for (int n : {0, 1, 2, 7, 63, 64, 1066, 4099})
        {
            std::vector<std::atomic<int>> hits(n > 0 ? n : 1);
            for (auto &h : hits) h.store(0);
            par::parallelFor(n, [&](int b, int e) {
                CHECK(b >= 0 && e <= n && b <= e);
                for (int i = b; i < e; ++i) hits[i].fetch_add(1);
            });
            for (int i = 0; i < n; ++i)
                CHECK(hits[i].load() == 1);
        }
    }
    par::setThreads(0);
}

TEST(ParallelFor_result_is_identical_to_serial_at_every_thread_count)
{
    // A deliberately order-sensitive body: each output depends only on its own index, so
    // any difference between thread counts would mean a chunk boundary moved or a range
    // was visited twice. This is the property every processor in the library relies on.
    const int n = 3001;
    std::vector<double> reference(n);
    par::setThreads(1);
    par::parallelFor(n, [&](int b, int e) {
        for (int i = b; i < e; ++i) reference[i] = std::sin((double)i) * 1e6 + (double)i;
    });

    for (int t : {2, 3, 5, 8, 13, 24})
    {
        par::setThreads(t);
        std::vector<double> got(n, 0.0);
        par::parallelFor(n, [&](int b, int e) {
            for (int i = b; i < e; ++i) got[i] = std::sin((double)i) * 1e6 + (double)i;
        });
        for (int i = 0; i < n; ++i)
            CHECK(got[i] == reference[i]);   // bit-identical, not merely close
    }
    par::setThreads(0);
}

TEST(ParallelFor_nested_runs_serially_instead_of_deadlocking)
{
    // The pool is not reentrant. A nested call must degrade to running on the calling
    // thread — if it waited for a pool whose every worker is already inside the outer
    // body, this test would hang rather than fail, which is why it exists at all.
    par::setThreads(8);
    const int outer = 200, inner = 50;
    std::vector<std::atomic<int>> hits(outer * inner);
    for (auto &h : hits) h.store(0);
    par::parallelFor(outer, [&](int b, int e) {
        for (int i = b; i < e; ++i)
            par::parallelFor(inner, [&](int ib, int ie) {
                for (int j = ib; j < ie; ++j) hits[(size_t)i * inner + j].fetch_add(1);
            });
    });
    for (size_t k = 0; k < hits.size(); ++k)
        CHECK(hits[k].load() == 1);
    par::setThreads(0);
}

TEST(ParallelFor_survives_repeated_resizing_under_load)
{
    // setThreads() restarts the pool. Doing that between batches must not drop work or
    // leave a worker waiting on a generation that will never come.
    const int n = 5000;
    for (int round = 0; round < 30; ++round)
    {
        par::setThreads(1 + (round * 7) % 17);       // 1..17, jumping around
        std::vector<int> sum(n, 0);
        par::parallelFor(n, [&](int b, int e) {
            for (int i = b; i < e; ++i) sum[i] = i * 2;
        });
        for (int i = 0; i < n; i += 137)
            CHECK(sum[i] == i * 2);
    }
    par::setThreads(0);
}

// ── T1: the batch handshake, which is the part that actually broke ────────────────
//
// The first version of the pool segfaulted about one ctest run in three, and neither the
// coverage test nor the determinism test above caught it, because both do ONE batch at a
// time with plenty of slack around it. The failure needed batches to overlap: a worker
// waking late for batch N, holding N's body pointer, then claiming a chunk of batch N+1
// after `run()` had returned and N's `std::function` — a local in parallelFor's frame —
// was destroyed.
//
// So this hammers the handshake: thousands of tiny back-to-back batches, each with a
// DIFFERENT lambda capturing its own state, so a stale body pointer is a use-after-free
// rather than a harmless call to the same code. Interleaved with thread-count changes,
// which restart the pool underneath it. It is a stress test, not a proof — but it is the
// shape that reproduced the crash, and it runs clean now where it did not before.
// ...and the case the first two crash-fixes both missed: parallelFor is called from
// SEVERAL THREADS AT ONCE. That is not incidental — the render worker runs the pipeline
// while the export path renders full-resolution frames and a load converts and downscales
// freshly decoded images, and all of them go through parallelFor (19 call sites).
//
// The old implementation was safe against this by accident: it spawned its own threads per
// call, so callers could not interfere. A pool with ONE batch descriptor is not, and no
// amount of care about a single batch's lifetime fixes it — two callers simply overwrite
// each other's body pointer, chunk count and claim index. That is what segfaulted
// `cosmo_core` and `cosmo_ui` intermittently (D-47), and it is why the pool holds a LIST.
TEST(ParallelFor_is_safe_when_several_threads_call_it_at_once)
{
    par::setThreads(8);
    std::atomic<bool> bad{false};
    std::atomic<int> batches{0};
    std::vector<std::thread> callers;
    for (int c = 0; c < 6; ++c)
        callers.emplace_back([c, &bad, &batches] {
            for (int round = 0; round < 250; ++round)
            {
                // A distinct size and a distinct tag per caller per round, so a batch that
                // picked up another caller's body writes a value this one can detect.
                const int n = 50 + (c * 37 + round * 11) % 400;
                const int tag = c * 100000 + round;
                std::vector<int> out(n, -1);
                par::parallelFor(n, [&out, tag](int b, int e) {
                    for (int i = b; i < e; ++i) out[i] = tag;
                });
                for (int i = 0; i < n; ++i)
                    if (out[i] != tag) { bad.store(true); return; }
                batches.fetch_add(1);
            }
        });
    for (auto &t : callers) t.join();
    printf("    %d concurrent batches across 6 caller threads\n", batches.load());
    CHECK(!bad.load());
    CHECK(batches.load() == 6 * 250);
    par::setThreads(0);
}

// ── D-47b: setThreads() from one thread while another is inside parallelFor ──────
//
// This is the shape that actually crashed the app. `ThreadBudget::endLoad()` calls
// `par::setThreads` from whichever thread finishes a load, while the render worker is
// inside `parallelFor` — and R-LOADPERF-3 streams decoded images into a LIVE editor, so
// "a load finishes while the photographer drags a slider" is the normal case.
//
// The pool's worker set was touched outside the lock (a join may not happen while holding
// the mutex the joinee waits on, so the join was moved out and nothing replaced the
// guard). Two concurrent restarts then iterated and cleared the same
// vector<std::thread>. The lethal outcome is a worker that is never joined: it survives
// into the next batch, picks up a Batch whose owning run() has returned, and calls a
// DESTROYED std::function whose captured image pointer is freed memory. The garbage floats
// then reached srgbEncode and the process died in a LUT lookup, nowhere near the pool.
TEST(ParallelFor_survives_setThreads_racing_against_a_live_batch)
{
    std::atomic<bool> stop{false}, bad{false};
    std::atomic<long long> batches{0};

    // Two workers hammering parallelFor with per-call tags, as the render worker and the
    // export path do.
    std::vector<std::thread> workers;
    for (int c = 0; c < 3; ++c)
        workers.emplace_back([c, &stop, &bad, &batches] {
            long long round = 0;
            while (!stop.load())
            {
                const int n = 80 + (int)((c * 31 + round * 7) % 500);
                const int tag = (int)((c + 1) * 1000000 + (round % 100000));
                std::vector<int> out(n, -1);
                par::parallelFor(n, [&out, tag](int b, int e) {
                    for (int i = b; i < e; ++i) out[i] = tag;
                });
                for (int i = 0; i < n; ++i)
                    if (out[i] != tag) { bad.store(true); return; }
                batches.fetch_add(1);
                ++round;
            }
        });

    // ...and a third thread doing what endLoad()/beginLoad() do: changing the count.
    std::thread resizer([&stop] {
        int i = 0;
        while (!stop.load())
        {
            par::setThreads(1 + (i++ % 12));
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    stop.store(true);
    for (auto &t : workers) t.join();
    resizer.join();

    printf("    %lld batches while the thread count was changing underneath\n", batches.load());
    CHECK(!bad.load());
    CHECK(batches.load() > 100);   // it really did keep working, not deadlock
    par::setThreads(0);
}

TEST(ParallelFor_survives_thousands_of_overlapping_batches)
{
    for (int round = 0; round < 400; ++round)
    {
        par::setThreads(2 + (round % 15));          // 2..16, restarting the pool as it goes
        for (int batch = 0; batch < 8; ++batch)
        {
            // A fresh vector per batch, captured by reference: if a stale worker called a
            // previous batch's body it would write through a dangling reference.
            const int n = 40 + (batch * 7 + round) % 300;
            std::vector<int> out(n, -1);
            const int tag = round * 8 + batch;
            par::parallelFor(n, [&out, tag](int b, int e) {
                for (int i = b; i < e; ++i) out[i] = tag;
            });
            for (int i = 0; i < n; ++i)
                CHECK(out[i] == tag);               // every index written, by THIS batch
        }
    }
    par::setThreads(0);
}
