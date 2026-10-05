/*
 *  interstellar_render — GradeEngine implementation. See GradeEngine.h for the contract and for
 *  why it calls `renderImage` rather than the slot sequence.
 */
#include "GradeEngine.h"
#include "engine/EditEngine.h"
#include "engine/EditParamsIO.h"
#include <algorithm>
#include <string>

namespace arstro
{
namespace interstellar
{
namespace render
{
    GradeEngine::GradeEngine() { ensureEngine(); }
    GradeEngine::~GradeEngine() = default;

    void GradeEngine::setPreferGpu(bool prefer)
    {
        mPreferGpu = prefer;
        if (mEngine) mEngine->setPreferGpu(prefer);
    }

    bool GradeEngine::gpuAvailable()
    {
        ensureEngine();
        return mEngine->gpuAvailable();
    }

    void GradeEngine::ensureEngine()
    {
        if (mEngine) return;
        mEngine.reset(new arstro::EditEngine());
        mEngine->setWantIntermediateHistograms(false, false);
        mEngine->setPreferGpu(mPreferGpu);   // releaseScratch drops the engine; the opt-in survives
    }

    void GradeEngine::releaseScratch() { mEngine.reset(); }

    bool GradeEngine::isIdentity(const arstro::EditParams &p)
    {
        // One string compare per cache MISS is nothing beside a decode.
        static const std::string kDefault = arstro::serializeParams(arstro::EditParams{});
        return arstro::serializeParams(p) == kDefault;
    }

    bool GradeEngine::render(const Raster &in, const arstro::EditParams &p, bool hasParams, int longEdge,
                             Raster &out)
    {
        if (in.deep()) return renderDeep(in, p, hasParams, longEdge, out);
        if (in.empty() || in.rgba.size() < (std::size_t)in.width * in.height * 4) return false;

        mLastAccelerated = false;
        if (!hasParams || isIdentity(p))
        {
            out = in;
            return true;
        }

        ensureEngine();
        // renderImage applies `p` itself and hands `p` to an accelerator directly, so no slot and no
        // per-slot params are involved: nothing from the previous frame can leak into this one.
        const arstro::Image linear = arstro::EditEngine::fromEncodedBytes(in.rgba.data(), in.width, in.height, 4);
        const int edge = longEdge > 0 ? longEdge : std::max(in.width, in.height);
        const arstro::PreviewBuffer buf = mEngine->renderImage(linear, p, edge);
        mLastAccelerated = mEngine->lastRenderAccelerated();

        if (!buf.rgba || buf.width <= 0 || buf.height <= 0)
        {
            out = in;
            return true;
        }
        out.width = buf.width;
        out.height = buf.height;
        out.rgba.assign(buf.rgba, buf.rgba + (std::size_t)buf.width * buf.height * 4);
        return true;
    }

    bool GradeEngine::renderDeep(const Raster &in, const arstro::EditParams &p, bool hasParams, int longEdge, Raster &out)
    {
        if (in.rgba16.size() < (std::size_t)in.width * in.height * 4) return false;
        mLastAccelerated = false;
        if (!hasParams || isIdentity(p))
        {
            out = in;
            return true;
        }
        ensureEngine();
        // The same engine call as the 8-bit path; only the ingest and the read-out differ — the
        // picture is taken from the engine's float result instead of its RGBA8 bytes (R-COLOR-1).
        const arstro::Image linear = arstro::EditEngine::fromEncodedWords(in.rgba16.data(), in.width, in.height, 4);
        const int edge = longEdge > 0 ? longEdge : std::max(in.width, in.height);
        const arstro::PreviewBuffer buf = mEngine->renderImage(linear, p, edge);
        mLastAccelerated = mEngine->lastRenderAccelerated();
        const arstro::Image &img = mEngine->lastProcessed();
        if (!buf.rgba || img.empty() || img.width() != buf.width || img.height() != buf.height)
        {
            out = in;
            return true;
        }
        const int w = img.width(), h = img.height(), ch = img.channels();
        out.allocate16(w, h, 65535);
        const Pixel *s = img.data();
        uint16_t *d = out.rgba16.data();
        // clamp01 maps NaN to 0, as the engine's own 8-bit pack does
        auto q = [](Pixel v) { return (uint16_t)(arstro::clamp01(v) * (Pixel)65535 + (Pixel)0.5); };
        for (std::size_t i = 0, n = (std::size_t)w * h; i < n; ++i, s += ch, d += 4)
        {
            const Pixel r = s[0], g = ch >= 3 ? s[1] : r, b = ch >= 3 ? s[2] : r;
            d[0] = q(r);
            d[1] = q(g);
            d[2] = q(b);
            d[3] = ch >= 4 ? q(s[3]) : (uint16_t)65535;
        }
        return true;
    }
}
}
}
