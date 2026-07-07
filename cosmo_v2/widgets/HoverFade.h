/*
 *  cosmo_v2 by arstro — HoverFade: per-item hover fade for self-drawn widgets.
 *
 *  A self-drawn widget (menu, list, tab strip, dialog…) paints many clickable
 *  sub-regions in ONE Segment, so the framework's per-Segment hoverAmount()
 *  can't tell them apart. HoverFade gives each sub-region its OWN eased 0..1
 *  amount: the hovered item eases toward 1 and every other toward 0, so moving
 *  the pointer between items fades the old one OUT while the new one fades IN
 *  (a cross-fade) instead of the highlight jumping. Items are addressed by a
 *  small integer id (0..N-1). Honors artboard::reducedMotion() (snaps to the
 *  end state). Call advance(nowMs) once per frame; read amount(id) when drawing.
 *
 *  Child-Segment controls (Button/PillButton/IconButton/…) do NOT need this —
 *  they already cross-fade via their own hoverAmount(). This is only for the
 *  many-regions-in-one-Segment case.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <algorithm>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class HoverFade
    {
    public:
        /** Set the item under the pointer (-1 = none). */
        void setHovered(int id) { mTarget = id; }
        void clear() { mTarget = -1; }
        int hovered() const { return mTarget; }

        /** Ease every item's amount toward its target (1 for the hovered id, 0
         *  otherwise) over ~durMs. Call once per frame. */
        void advance(double nowMs, double durMs = artboard::interaction::kHoverMs)
        {
            if (mTarget >= 0 && mTarget >= (int)mAmt.size())
                mAmt.resize(mTarget + 1, 0.0);

            const bool rm = artboard::reducedMotion();
            double step;
            if (rm || durMs <= 0.0)
                step = 1.0;  // reduced motion / no duration: land immediately
            else
            {
                const double dt = (mLastMs < 0.0) ? 0.0 : (nowMs - mLastMs);
                step = std::min(1.0, std::max(0.0, dt / durMs));  // fraction of the fade this frame
            }
            mLastMs = nowMs;

            for (int i = 0; i < (int)mAmt.size(); ++i)
            {
                const double tgt = (i == mTarget) ? 1.0 : 0.0;
                if (mAmt[i] < tgt) mAmt[i] = std::min(tgt, mAmt[i] + step);
                else if (mAmt[i] > tgt) mAmt[i] = std::max(tgt, mAmt[i] - step);
            }
        }

        /** Eased hover amount in [0,1] for item id (smoothstep for a soft in/out). */
        double amount(int id) const
        {
            if (id < 0 || id >= (int)mAmt.size()) return 0.0;
            const double t = mAmt[id];
            return t * t * (3.0 - 2.0 * t);
        }

    private:
        int mTarget = -1;
        double mLastMs = -1.0;
        std::vector<double> mAmt;  // raw (pre-ease) fade amount per item id
    };
}
}
