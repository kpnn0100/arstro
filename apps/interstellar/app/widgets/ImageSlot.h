/*
 *  interstellar_v1 — ImageSlot: one raster, registered lazily with the target that draws it.
 *
 *  An image id belongs to the render target that issued it (IRenderTarget::registerImage), so a
 *  self-drawn widget that paints a picture keeps three facts together: the owned pixels, the id,
 *  and WHICH target the id is valid in. Re-uploading happens only when the pixels change; a new
 *  upload RELEASES the id it replaces (a widget that only ever registers leaks one image per
 *  change); a different target (a test's RecordingTarget, then Cairo) gets a fresh registration
 *  instead of a stale id. Home covers use it; the Monitor does the same dance with two ids so it
 *  can dissolve between frames.
 */
#pragma once
#include "../../../../core/Artboard/include/artboard/artboard.h"
#include "../../core/Raster.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    class ImageSlot
    {
    public:
        void set(const interstellar::Raster &r)
        {
            mPixels = r.rgba;
            mW = r.width;
            mH = r.height;
            mDirty = !r.empty();
        }
        bool has() const { return mW > 0 && mH > 0 && !mPixels.empty(); }
        int width() const { return mW; }
        int height() const { return mH; }

        /** The id to draw with in `t` (0 = nothing to draw). Call only during a render. */
        int ensure(artboard::IRenderTarget &t) const
        {
            if (!has()) return 0;
            if (mOwner != &t) { mId = 0; mOwner = &t; mDirty = true; }
            if (mDirty)
            {
                const int id = t.registerImage(mPixels.data(), mW, mH);
                if (mId) t.releaseImage(mId);
                mId = id;
                mDirty = false;
            }
            return mId;
        }

        /** Draw `cover`-fitted into `dst` (cropping the overflow), the way a card thumbnail reads. */
        void drawCover(artboard::IRenderTarget &t, const artboard::Rect &dst) const
        {
            const int id = ensure(t);
            if (!id || dst.w <= 0 || dst.h <= 0) return;
            const double s = std::max(dst.w / mW, dst.h / mH);
            const double w = mW * s, h = mH * s;
            t.save();
            t.clipRect(dst.x, dst.y, dst.w, dst.h);
            t.drawImage(id, artboard::Rect{dst.x + (dst.w - w) * 0.5, dst.y + (dst.h - h) * 0.5, w, h});
            t.restore();
        }

    private:
        std::vector<uint8_t> mPixels;
        int mW = 0, mH = 0;
        mutable bool mDirty = false;
        mutable int mId = 0;
        mutable artboard::IRenderTarget *mOwner = nullptr;
    };
}
}
