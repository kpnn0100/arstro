/*
 *  Unit tests for the ImageProcessing core (base types + processors + analysis).
 *  Dependency-free MiniTest harness. main() lives here; engineTests.cpp registers
 *  additional cases into the shared registry.
 */
#include "MiniTest.h"
#include "image_processing.h"

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

MINITEST_MAIN
