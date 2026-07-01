#include "EditEngine.h"
#include "../base/ColorSpace.h"
#include "../base/Parallel.h"
#include "MaskStack.h"
#include <algorithm>

namespace arstro
{
    EditEngine::EditEngine() { buildPipeline(); }

    void EditEngine::buildPipeline()
    {
        mChainPre.clear(); mChainMid.clear(); mChainPost.clear();
        // Canonical order (the hard contract), split at the tone-curve and mixer taps.
        mChainPre.add(&mCrop);            // geometry + pre-tone, up to WhiteBalance
        mChainPre.add(&mRotate);
        mChainPre.add(&mLens);
        mChainPre.add(&mNoiseReduction);
        mChainPre.add(&mExposure);
        mChainPre.add(&mContrast);
        mChainPre.add(&mToneRegions);
        mChainPre.add(&mWhiteBalance);    // <- tap: luma entering the tone curve
        mChainMid.add(&mToneCurve);
        mChainMid.add(&mTexture);
        mChainMid.add(&mClarity);
        mChainMid.add(&mVibrance);        // <- tap: hue entering the colour mixer
        mChainPost.add(&mColorMixer);
        mChainPost.add(&mColorGrading);
        mChainPost.add(&mDehaze);
        mChainPost.add(&mSharpen);
        mChainPost.add(&mGrain);
    }

    Image EditEngine::fromEncodedBytes(const uint8_t *rgba, int w, int h, int channels)
    {
        Image img(w, h, channels, ColorSpace::EncodedSRGB);
        const size_t n = (size_t)w * h * channels;
        Pixel *d = img.data();
        for (size_t i = 0; i < n; ++i)
            d[i] = (Pixel)rgba[i] / (Pixel)255;
        color::decodeInPlace(img);
        return img;
    }

    int EditEngine::addImage(const uint8_t *rgba, int width, int height, int channels)
    {
        if (!rgba || width <= 0 || height <= 0 || channels < 1)
            return -1;
        Slot s;
        s.source = fromEncodedBytes(rgba, width, height, channels);
        mSlots.push_back(std::move(s));
        const int slot = (int)mSlots.size() - 1;
        if (mCurrent < 0)
            selectImage(slot);
        return slot;
    }

    void EditEngine::selectImage(int slot)
    {
        if (slot < 0 || slot >= (int)mSlots.size())
            return;
        mCurrent = slot;
        mProxySlot = -1;
        applyParams(mSlots[mCurrent].params);
    }

    const EditParams &EditEngine::currentParams() const
    {
        static const EditParams kEmpty;
        return mCurrent >= 0 ? mSlots[mCurrent].params : kEmpty;
    }

    void EditEngine::setCurrentParams(const EditParams &p)
    {
        if (mCurrent < 0)
            return;
        mSlots[mCurrent].params = p;
        applyParams(p);
    }

    void EditEngine::applyParams(const EditParams &p)
    {
        mCrop.setRect(p.cropX, p.cropY, p.cropW, p.cropH);
        mRotate.setAngle(p.rotation);
        mRotate.setQuarterTurns(p.quarterTurns);
        mExposure.setExposureEv(p.exposure);
        mContrast.setContrast(p.contrast);
        mToneRegions.setHighlights(p.highlights);
        mToneRegions.setShadows(p.shadows);
        mToneRegions.setWhites(p.whites);
        mToneRegions.setBlacks(p.blacks);
        mWhiteBalance.setTemperature(p.temp);
        mWhiteBalance.setTint(p.tint);
        mToneCurve.setLogScale(p.curveLog);
        mToneCurve.setPoints(p.curve);
        mTexture.setAmount(p.texture);
        mClarity.setAmount(p.clarity);
        mVibrance.setVibrance(p.vibrance);
        mVibrance.setSaturation(p.saturation);
        mColorMixer.setCurve(ColorMixer::Hue, p.mixer[0]);
        mColorMixer.setCurve(ColorMixer::Sat, p.mixer[1]);
        mColorMixer.setCurve(ColorMixer::Lum, p.mixer[2]);
        for (int r = 0; r < 3; ++r)
        {
            mColorGrading.setGradeHue((ColorGrading::Region)r, p.grade[r].hue);
            mColorGrading.setGradeSaturation((ColorGrading::Region)r, p.grade[r].sat);
            mColorGrading.setGradeLuminance((ColorGrading::Region)r, p.grade[r].lum);
        }
        mColorGrading.setBalance(p.balance);
        mColorGrading.setHueRemapEnabled(p.remapEnable);
        mColorGrading.setHueRemap(p.remapSrc, p.remapRange, p.remapDst, p.remapStrength);
        mDehaze.setAmount(p.dehaze);
        mGrain.setAmount(p.grainAmount);
        mGrain.setSize(p.grainSize);
        mLens.setDistortion(p.lensDistortion);
        mLens.setChromaticAberration(p.lensCA);
        mLens.setVignette(p.lensVignette);
        mNoiseReduction.setLuminance(p.nrLuminance);
        mNoiseReduction.setColor(p.nrColor);
        mSharpen.setAmount(p.sharpenAmount);
        mSharpen.setRadius(p.sharpenRadius);
        mSharpen.setMasking(p.sharpenMasking);
        mMasks = p.masks;  // local adjustments applied post-pipeline (see renderInto)
    }

    void EditEngine::setPreviewSize(int maxEdge)
    {
        if (maxEdge < 1) maxEdge = 1;
        mPreviewMaxEdge = maxEdge;
    }

    // ── parameter setters ──
    void EditEngine::setExposure(float v) { if (auto *p = cur()) { p->exposure = v; mExposure.setExposureEv(v); } }
    void EditEngine::setContrast(float v) { if (auto *p = cur()) { p->contrast = v; mContrast.setContrast(v); } }
    void EditEngine::setHighlights(float v) { if (auto *p = cur()) { p->highlights = v; mToneRegions.setHighlights(v); } }
    void EditEngine::setShadows(float v) { if (auto *p = cur()) { p->shadows = v; mToneRegions.setShadows(v); } }
    void EditEngine::setWhites(float v) { if (auto *p = cur()) { p->whites = v; mToneRegions.setWhites(v); } }
    void EditEngine::setBlacks(float v) { if (auto *p = cur()) { p->blacks = v; mToneRegions.setBlacks(v); } }
    void EditEngine::setTemperature(float v) { if (auto *p = cur()) { p->temp = v; mWhiteBalance.setTemperature(v); } }
    void EditEngine::setTint(float v) { if (auto *p = cur()) { p->tint = v; mWhiteBalance.setTint(v); } }
    void EditEngine::setVibrance(float v) { if (auto *p = cur()) { p->vibrance = v; mVibrance.setVibrance(v); } }
    void EditEngine::setSaturation(float v) { if (auto *p = cur()) { p->saturation = v; mVibrance.setSaturation(v); } }
    void EditEngine::setTexture(float v) { if (auto *p = cur()) { p->texture = v; mTexture.setAmount(v); } }
    void EditEngine::setClarity(float v) { if (auto *p = cur()) { p->clarity = v; mClarity.setAmount(v); } }
    void EditEngine::setDehaze(float v) { if (auto *p = cur()) { p->dehaze = v; mDehaze.setAmount(v); } }
    void EditEngine::setGrainAmount(float v) { if (auto *p = cur()) { p->grainAmount = v; mGrain.setAmount(v); } }
    void EditEngine::setGrainSize(float v) { if (auto *p = cur()) { p->grainSize = v; mGrain.setSize(v); } }

    void EditEngine::setSharpenAmount(float v) { if (auto *p = cur()) { p->sharpenAmount = v; mSharpen.setAmount(v); } }
    void EditEngine::setSharpenRadius(float px) { if (auto *p = cur()) { p->sharpenRadius = px; mSharpen.setRadius(px); } }
    void EditEngine::setSharpenMasking(float v) { if (auto *p = cur()) { p->sharpenMasking = v; mSharpen.setMasking(v); } }
    void EditEngine::setNoiseLuminance(float v) { if (auto *p = cur()) { p->nrLuminance = v; mNoiseReduction.setLuminance(v); } }
    void EditEngine::setNoiseColor(float v) { if (auto *p = cur()) { p->nrColor = v; mNoiseReduction.setColor(v); } }

    void EditEngine::setLensDistortion(float v) { if (auto *p = cur()) { p->lensDistortion = v; mLens.setDistortion(v); } }
    void EditEngine::setLensCA(float v) { if (auto *p = cur()) { p->lensCA = v; mLens.setChromaticAberration(v); } }
    void EditEngine::setLensVignette(float v) { if (auto *p = cur()) { p->lensVignette = v; mLens.setVignette(v); } }

    void EditEngine::setCurvePoints(const std::vector<std::pair<float, float>> &pts)
    {
        if (auto *p = cur()) { p->curve = pts; mToneCurve.setPoints(pts); }
    }
    void EditEngine::setCurveLogScale(bool log)
    {
        if (auto *p = cur()) { p->curveLog = log; mToneCurve.setLogScale(log); }
    }

    void EditEngine::setMixerCurve(MixerChannel c, const std::vector<std::pair<float, float>> &points)
    {
        if (auto *p = cur())
        {
            p->mixer[c] = points;
            mColorMixer.setCurve((ColorMixer::Channel)c, points);
        }
    }

    void EditEngine::setGradeHue(GradeRegion r, float v) { if (auto *p = cur()) { p->grade[r].hue = v; mColorGrading.setGradeHue((ColorGrading::Region)r, v); } }
    void EditEngine::setGradeSaturation(GradeRegion r, float v) { if (auto *p = cur()) { p->grade[r].sat = v; mColorGrading.setGradeSaturation((ColorGrading::Region)r, v); } }
    void EditEngine::setGradeLuminance(GradeRegion r, float v) { if (auto *p = cur()) { p->grade[r].lum = v; mColorGrading.setGradeLuminance((ColorGrading::Region)r, v); } }
    void EditEngine::setGradeBalance(float v) { if (auto *p = cur()) { p->balance = v; mColorGrading.setBalance(v); } }
    void EditEngine::setHueRemapEnabled(bool on) { if (auto *p = cur()) { p->remapEnable = on; mColorGrading.setHueRemapEnabled(on); } }
    void EditEngine::setHueRemap(float src, float range, float dst, float strength)
    {
        if (auto *p = cur())
        {
            p->remapSrc = src; p->remapRange = range; p->remapDst = dst; p->remapStrength = strength;
            mColorGrading.setHueRemap(src, range, dst, strength);
        }
    }

    void EditEngine::setCrop(float x, float y, float w, float h)
    {
        if (auto *p = cur()) { p->cropX = x; p->cropY = y; p->cropW = w; p->cropH = h; mCrop.setRect(x, y, w, h); }
    }
    void EditEngine::resetCrop() { setCrop(0, 0, 1, 1); }
    void EditEngine::setRotation(float deg) { if (auto *p = cur()) { p->rotation = deg; mRotate.setAngle(deg); } }
    void EditEngine::setQuarterTurns(int t) { if (auto *p = cur()) { p->quarterTurns = t & 3; mRotate.setQuarterTurns(t); } }
    void EditEngine::setMasks(const std::vector<MaskParams> &masks) { if (auto *p = cur()) { p->masks = masks; mMasks = masks; } }

    void EditEngine::setBypass(bool b) { mChainPre.setBypass(b); mChainMid.setBypass(b); mChainPost.setBypass(b); }

    void EditEngine::resetAll()
    {
        if (mCurrent < 0) return;
        mSlots[mCurrent].params = EditParams{};
        applyParams(mSlots[mCurrent].params);
    }

    static Image downscaleLinear(const Image &src, int maxEdge)
    {
        const int sw = src.width(), sh = src.height();
        const int longEdge = sw > sh ? sw : sh;
        if (longEdge <= maxEdge)
            return src.clone();
        const double scale = (double)maxEdge / (double)longEdge;
        int tw = (int)(sw * scale + 0.5); if (tw < 1) tw = 1;
        int th = (int)(sh * scale + 0.5); if (th < 1) th = 1;
        const int ch = src.channels();
        Image out(tw, th, ch, src.space());
        for (int ty = 0; ty < th; ++ty)
        {
            const int y0 = (int)((double)ty * sh / th);
            int y1 = (int)((double)(ty + 1) * sh / th); if (y1 <= y0) y1 = y0 + 1;
            for (int tx = 0; tx < tw; ++tx)
            {
                const int x0 = (int)((double)tx * sw / tw);
                int x1 = (int)((double)(tx + 1) * sw / tw); if (x1 <= x0) x1 = x0 + 1;
                for (int c = 0; c < ch; ++c)
                {
                    double sum = 0.0; int n = 0;
                    for (int y = y0; y < y1; ++y)
                        for (int x = x0; x < x1; ++x) { sum += src.at(x, y, c); ++n; }
                    out.at(tx, ty, c) = (Pixel)(sum / (n > 0 ? n : 1));
                }
            }
        }
        return out;
    }

    void EditEngine::ensurePreviewProxy()
    {
        if (mProxySlot == mCurrent && mProxyEdge == mPreviewMaxEdge)
            return;
        mPreviewProxy = downscaleLinear(mSlots[mCurrent].source, mPreviewMaxEdge);
        mProxySlot = mCurrent;
        mProxyEdge = mPreviewMaxEdge;
    }

    PreviewBuffer EditEngine::renderInto(const Image &linearSource, std::vector<uint8_t> &outBytes)
    {
        // Run the three segments, tapping histograms at the boundaries.
        Image preCurve, preMixer, processed;
        mChainPre.apply(linearSource, preCurve);
        mPreCurveHist = Histogram::compute(preCurve);   // luma entering the tone curve
        mChainMid.apply(preCurve, preMixer);
        mPreMixerHue = Histogram::computeHue(preMixer);  // hue entering the colour mixer
        mChainPost.apply(preMixer, processed);
        applyMaskStack(processed, mMasks);  // local adjustments on the framed image (linear)
        color::encodeInPlace(processed);
        mLastHistogram = Histogram::compute(processed);

        const int w = processed.width(), h = processed.height(), ch = processed.channels();
        outBytes.assign((size_t)w * h * 4, 255);
        const Pixel *s = processed.data();
        uint8_t *d = outBytes.data();
        par::parallelFor(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const Pixel *p = s + ((size_t)y * w + x) * ch;
                    uint8_t *q = d + ((size_t)y * w + x) * 4;
                    Pixel r = ch >= 1 ? p[0] : 0;
                    Pixel g = ch >= 3 ? p[1] : r;
                    Pixel b = ch >= 3 ? p[2] : r;
                    Pixel a = ch >= 4 ? p[3] : (Pixel)1;
                    q[0] = (uint8_t)(clamp01(r) * 255 + 0.5f);
                    q[1] = (uint8_t)(clamp01(g) * 255 + 0.5f);
                    q[2] = (uint8_t)(clamp01(b) * 255 + 0.5f);
                    q[3] = (uint8_t)(clamp01(a) * 255 + 0.5f);
                }
        });
        PreviewBuffer pb;
        pb.rgba = outBytes.data();
        pb.width = w; pb.height = h;
        return pb;
    }

    PreviewBuffer EditEngine::renderPreview()
    {
        if (mCurrent < 0) return PreviewBuffer{};
        ensurePreviewProxy();
        return renderInto(mPreviewProxy, mPreviewOut);
    }

    PreviewBuffer EditEngine::renderFull()
    {
        if (mCurrent < 0) return PreviewBuffer{};
        return renderInto(mSlots[mCurrent].source, mFullOut);
    }

    PreviewBuffer EditEngine::renderImage(const Image &linearSrc, const EditParams &p, int maxEdge)
    {
        if (linearSrc.empty()) return PreviewBuffer{};
        if (maxEdge < 1) maxEdge = 1;
        applyParams(p);
        Image src = downscaleLinear(linearSrc, maxEdge);
        return renderInto(src, mFullOut);  // engine-owned; consume before the next render
    }
}
