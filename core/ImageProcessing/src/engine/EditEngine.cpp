#include "EditEngine.h"
#include "../base/ColorSpace.h"
#include "../base/Parallel.h"
#include "MaskStack.h"
#include <algorithm>
#include <array>

namespace arstro
{
    EditEngine::EditEngine() { buildPipeline(); mAccel = createComputeAccelerator(); }

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
        // The input is 8-bit, so sRGB→linear has only 256 possible results: precompute
        // a lookup table once and gather, instead of a divide + pow per pixel (the
        // dominant per-image load cost). Colour channels use the sRGB LUT; alpha is a
        // plain /255. Output is already linear, so decodeInPlace is not needed.
        static const std::array<Pixel, 256> kSrgbToLinear = [] {
            std::array<Pixel, 256> t{};
            for (int i = 0; i < 256; ++i) t[i] = color::srgbDecode((Pixel)i / (Pixel)255);
            return t;
        }();
        static const std::array<Pixel, 256> kByteToUnit = [] {
            std::array<Pixel, 256> t{};
            for (int i = 0; i < 256; ++i) t[i] = (Pixel)i / (Pixel)255;
            return t;
        }();

        Image img(w, h, channels, ColorSpace::LinearSRGB);
        Pixel *d = img.data();
        const int rowN = w * channels;
        const int colorCh = channels >= 3 ? 3 : channels;  // matches decodeInPlace's colour-channel rule
        par::parallelFor(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
            {
                const uint8_t *s = rgba + (size_t)y * rowN;
                Pixel *o = d + (size_t)y * rowN;
                for (int x = 0; x < rowN; x += channels)
                {
                    for (int c = 0; c < colorCh; ++c) o[x + c] = kSrgbToLinear[s[x + c]];
                    for (int c = colorCh; c < channels; ++c) o[x + c] = kByteToUnit[s[x + c]];  // alpha
                }
            }
        });
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
        // R-MEM-1: the new source joins the capped pool immediately, so a load of 120
        // photos evicts as it goes instead of accumulating 387 MB per photo to the end.
        touchSourceLRU(slot);
        if (mCurrent < 0)
            selectImage(slot);
        return slot;
    }

    bool EditEngine::supplySource(int slot, const uint8_t *rgba, int width, int height, int channels)
    {
        // R-MEM-2: eviction is a cache decision, never a data decision. A cold slot gets
        // its pixels back here, from the caller that re-decoded the original file, and
        // then renders exactly as it would have if it had never been evicted.
        if (slot < 0 || slot >= (int)mSlots.size() || mSlots[slot].released ||
            !rgba || width <= 0 || height <= 0 || channels < 1)
            return false;
        mSlots[slot].source = fromEncodedBytes(rgba, width, height, channels);
        ++mRehydrations;
        touchSourceLRU(slot);
        return true;
    }

    bool EditEngine::slotNeedsSource(int slot) const
    {
        if (slot < 0 || slot >= (int)mSlots.size()) return false;
        const Slot &s = mSlots[slot];
        if (s.released) return false;   // removed from the session; nothing to bring back
        if (!s.source.empty()) return false;
        // A proxy already at the current preview size is enough to render a preview from,
        // so a slot is only COLD when neither is present. renderFull is the other caller
        // and it asks for the source explicitly.
        return s.proxy.empty() || s.proxyEdge != mPreviewMaxEdge;
    }

    bool EditEngine::slotHasSource(int slot) const
    {
        return slot >= 0 && slot < (int)mSlots.size() && !mSlots[slot].released &&
               !mSlots[slot].source.empty();
    }

    void EditEngine::setMemoryCaps(size_t sourceBytes, size_t proxyBytes)
    {
        // A cap of 0 would evict the slot being rendered on the next touch, so both keep a
        // floor of one frame's worth. The floors are deliberately not "unbounded": a host
        // that asks for a tiny cap gets a tiny cache, not a disabled one.
        mSourceCap = sourceBytes;
        mProxyCap = proxyBytes;
        if (mCurrent >= 0) { touchSourceLRU(mCurrent); touchProxyLRU(mCurrent); }
    }

    size_t EditEngine::residentSourceBytes() const
    {
        size_t n = 0;
        for (const Slot &s : mSlots) n += imageBytes(s.source);
        return n;
    }

    size_t EditEngine::residentProxyBytes() const
    {
        size_t n = 0;
        for (const Slot &s : mSlots) n += imageBytes(s.proxy);
        return n;
    }

    void EditEngine::selectImage(int slot)
    {
        // A slot with no source is still selectable: it may hold a usable proxy, or it may
        // be cold and about to be re-decoded (R-MEM-2). Refusing on an empty source was what
        // made an evicted slot unreachable rather than merely slow. A RELEASED slot is a
        // different thing and stays unselectable, which is the contract releaseImage has
        // always had.
        if (slot < 0 || slot >= (int)mSlots.size() || mSlots[slot].released)
            return;
        // Each slot keeps its own preview proxy (see ensurePreviewProxy + the Slot
        // struct), so switching images does NOT invalidate/re-downscale anything —
        // the proxy is reused if still cached. Interactive same-slot renders also
        // reuse it (a slot's source pixels are never mutated in place).
        mCurrent = slot;
        applyParams(mSlots[mCurrent].params);
    }

    void EditEngine::touchProxyLRU(int slot)
    {
        auto it = std::find(mProxyLRU.begin(), mProxyLRU.end(), slot);
        if (it != mProxyLRU.end()) mProxyLRU.erase(it);
        mProxyLRU.insert(mProxyLRU.begin(), slot);  // most-recent first
        // R-MEM-1: evict from the cold end until the pool is under its BYTE cap. The slot
        // being rendered is SKIPPED, never evicted — a render reading an image dropped to
        // satisfy a number is a crash, not a saving — and skipping means leaving it in the
        // list: popping it would quietly stop tracking it, so it could never be evicted
        // again once the selection moved on.
        size_t bytes = residentProxyBytes();
        for (int i = (int)mProxyLRU.size() - 1; i >= 0 && bytes > mProxyCap; --i)
        {
            const int old = mProxyLRU[i];
            if (old == mCurrent) continue;
            if (old < 0 || old >= (int)mSlots.size() || mSlots[old].proxy.empty())
            {
                mProxyLRU.erase(mProxyLRU.begin() + i);   // stale entry, nothing to free
                continue;
            }
            bytes -= imageBytes(mSlots[old].proxy);
            mSlots[old].proxy = Image{};
            mSlots[old].proxyEdge = -1;
            mProxyLRU.erase(mProxyLRU.begin() + i);
        }
    }

    void EditEngine::touchSourceLRU(int slot)
    {
        auto it = std::find(mSourceLRU.begin(), mSourceLRU.end(), slot);
        if (it != mSourceLRU.end()) mSourceLRU.erase(it);
        mSourceLRU.insert(mSourceLRU.begin(), slot);
        // The pool that actually mattered: a source is ~14x its proxy, and one per slot
        // held for the life of the project is what made a 120-photo catalog ask for 54 GB.
        // A slot evicted here is COLD, not gone — RenderService re-decodes it on demand.
        size_t bytes = residentSourceBytes();
        for (int i = (int)mSourceLRU.size() - 1; i >= 0 && bytes > mSourceCap; --i)
        {
            const int old = mSourceLRU[i];
            if (old == mCurrent) continue;   // skipped, and deliberately left in the list
            if (old < 0 || old >= (int)mSlots.size() || mSlots[old].source.empty())
            {
                mSourceLRU.erase(mSourceLRU.begin() + i);
                continue;
            }
            bytes -= imageBytes(mSlots[old].source);
            mSlots[old].source = Image{};
            mSourceLRU.erase(mSourceLRU.begin() + i);
        }
    }

    void EditEngine::dropProxy(int slot)
    {
        auto it = std::find(mProxyLRU.begin(), mProxyLRU.end(), slot);
        if (it != mProxyLRU.end()) mProxyLRU.erase(it);
        if (slot >= 0 && slot < (int)mSlots.size()) { mSlots[slot].proxy = Image{}; mSlots[slot].proxyEdge = -1; }
    }

    void EditEngine::dropSource(int slot)
    {
        auto it = std::find(mSourceLRU.begin(), mSourceLRU.end(), slot);
        if (it != mSourceLRU.end()) mSourceLRU.erase(it);
        if (slot >= 0 && slot < (int)mSlots.size()) mSlots[slot].source = Image{};
    }

    void EditEngine::releaseImage(int slot)
    {
        if (slot < 0 || slot >= (int)mSlots.size())
            return;
        mSlots[slot].params = EditParams{};
        mSlots[slot].released = true;   // gone from the session, not merely evicted
        dropSource(slot);
        dropProxy(slot);
        if (mCurrent == slot) mCurrent = -1;
    }

    void EditEngine::clearImages()
    {
        mSlots.clear();
        mProxyLRU.clear();
        mSourceLRU.clear();
        mCurrent = -1;
        mRehydrations = 0;
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
        for (int ch = 0; ch < 3; ++ch)
            mToneCurve.setChannelPoints(ch, p.curveChannel[ch]);
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

    void EditEngine::setCurvePoints(const std::vector<CurvePoint> &pts)
    {
        if (auto *p = cur()) { p->curve = pts; mToneCurve.setPoints(pts); }
    }
    void EditEngine::setCurveChannelPoints(int ch, const std::vector<CurvePoint> &pts)
    {
        if (ch < 0 || ch >= 3) return;
        if (auto *p = cur()) { p->curveChannel[ch] = pts; mToneCurve.setChannelPoints(ch, pts); }
    }
    void EditEngine::setCurveLogScale(bool log)
    {
        if (auto *p = cur()) { p->curveLog = log; mToneCurve.setLogScale(log); }
    }

    void EditEngine::setMixerCurve(MixerChannel c, const std::vector<CurvePoint> &points)
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
        // Row-independent box downscale — parallelised over target rows, reading
        // source rows via row pointers (the dominant per-image + per-switch cost).
        par::parallelFor(th, [&](int r0, int r1) {
            for (int ty = r0; ty < r1; ++ty)
            {
                const int y0 = (int)((double)ty * sh / th);
                int y1 = (int)((double)(ty + 1) * sh / th); if (y1 <= y0) y1 = y0 + 1;
                Pixel *o = out.row(ty);
                for (int tx = 0; tx < tw; ++tx)
                {
                    const int x0 = (int)((double)tx * sw / tw);
                    int x1 = (int)((double)(tx + 1) * sw / tw); if (x1 <= x0) x1 = x0 + 1;
                    const double inv = 1.0 / ((double)(y1 - y0) * (x1 - x0));
                    for (int c = 0; c < ch; ++c)
                    {
                        double sum = 0.0;
                        for (int y = y0; y < y1; ++y)
                        {
                            const Pixel *s = src.row(y);
                            for (int x = x0; x < x1; ++x) sum += s[x * ch + c];
                        }
                        o[tx * ch + c] = (Pixel)(sum * inv);
                    }
                }
            }
        });
        return out;
    }

    bool EditEngine::ensurePreviewProxy()
    {
        Slot &s = mSlots[mCurrent];
        if (!s.proxy.empty() && s.proxyEdge == mPreviewMaxEdge)  // cached at this preview size -> reuse
        {
            touchProxyLRU(mCurrent);
            return true;
        }
        // Cold: the proxy is missing or stale AND the source it would be built from has
        // been evicted (R-MEM-2). Say so rather than downscaling an empty image — the
        // caller re-decodes and comes back.
        if (s.source.empty()) return false;
        s.proxy = downscaleLinear(s.source, mPreviewMaxEdge);
        s.proxyEdge = mPreviewMaxEdge;
        touchProxyLRU(mCurrent);
        // Building the proxy is also the moment the source stops being the only copy, so
        // it goes to the back of its own pool rather than staying pinned by having been
        // used most recently.
        touchSourceLRU(mCurrent);
        return true;
    }

    PreviewBuffer EditEngine::renderInto(const Image &linearSource, const EditParams &params, std::vector<uint8_t> &outBytes)
    {
        Image processed;

        // GPU-accelerated path: only when the user opted in AND a backend is
        // available AND it accepts the job. The CPU path below is the guaranteed
        // fallback and the correctness reference (R-GPU-2) — a backend that returns
        // true must match it. `params` carries the full edit (incl. masks); the CPU
        // chains are already configured from the same params (setCurrentParams).
        bool accelerated = false;
        if (mPreferGpu && mAccel && mAccel->available())
        {
            ComputeResult r;
            if (mAccel->process(linearSource, params, r))
            {
                processed = std::move(r.processed);
                mPreCurveHist = r.preCurveHist;
                mPreMixerHue = r.preMixerHue;
                mLastHistogram = r.finalHist;
                accelerated = true;
            }
        }

        if (!accelerated)
        {
            // CPU reference path: run the three segments, tapping histograms at the
            // boundaries. Histograms + encode are parallelised (Histogram.cpp /
            // ColorSpace.cpp) since they run on every preview render.
            Image preCurve, preMixer;
            mChainPre.apply(linearSource, preCurve);
            mPreCurveHist = Histogram::compute(preCurve);   // luma entering the tone curve
            mChainMid.apply(preCurve, preMixer);
            mPreMixerHue = Histogram::computeHue(preMixer);  // hue entering the colour mixer
            mChainPost.apply(preMixer, processed);
            applyMaskStack(processed, mMasks);  // local adjustments on the framed image (linear)
            color::encodeInPlace(processed);
            mLastHistogram = Histogram::compute(processed);
        }
        mLastAccelerated = accelerated;

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
        // An empty buffer here means COLD, not failed (R-MEM-2): the slot's pixels were
        // evicted and the caller must re-decode. RenderService does that through its
        // SourceLoader and renders on the second attempt.
        if (!ensurePreviewProxy()) return PreviewBuffer{};
        return renderInto(mSlots[mCurrent].proxy, mSlots[mCurrent].params, mPreviewOut);
    }

    PreviewBuffer EditEngine::renderFull()
    {
        if (mCurrent < 0) return PreviewBuffer{};
        // Full resolution is the one thing a proxy cannot stand in for, so an evicted
        // source is always cold here — export re-decodes rather than exporting a preview.
        if (mSlots[mCurrent].source.empty()) return PreviewBuffer{};
        touchSourceLRU(mCurrent);
        return renderInto(mSlots[mCurrent].source, mSlots[mCurrent].params, mFullOut);
    }

    PreviewBuffer EditEngine::renderImage(const Image &linearSrc, const EditParams &p, int maxEdge)
    {
        if (linearSrc.empty()) return PreviewBuffer{};
        if (maxEdge < 1) maxEdge = 1;
        applyParams(p);
        Image src = downscaleLinear(linearSrc, maxEdge);
        return renderInto(src, p, mFullOut);  // engine-owned; consume before the next render
    }
}
