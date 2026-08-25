#include "CropOverlay.h"
#include "../Theme.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        /** How close counts as on a handle. The SAME number the curve, mixer and split-seam
         *  editors use, so "how close is close" is one value across the app. */
        double grabPx() { return metrics::anchorHitRadius(); }
        constexpr double kAppearMs = 180.0;   // the fade in/out with the Xform tab (R-G-1)
        constexpr double kHandleLen = 18.0;   // corner bracket arm length
        constexpr double kHandleW = 2.0;      // ...and its thickness
        constexpr double kDimAlpha = 0.62;    // how dark the discarded area goes
    }

    CropOverlay::CropOverlay()
    {
        // Nothing to clip: everything is drawn inside the fitted rect by construction, and
        // clipping would cost a layer for no gain.
        inputTransparent = false;
    }

    Rect CropOverlay::boxPx() const
    {
        return Rect{mFitted.x + mCrop.x * mFitted.w, mFitted.y + mCrop.y * mFitted.h,
                    mCrop.w * mFitted.w, mCrop.h * mFitted.h};
    }

    double CropOverlay::nxOf(double localX) const
    {
        return mFitted.w > 0.0 ? (localX - mFitted.x) / mFitted.w : 0.0;
    }
    double CropOverlay::nyOf(double localY) const
    {
        return mFitted.h > 0.0 ? (localY - mFitted.y) / mFitted.h : 0.0;
    }

    crop::Part CropOverlay::partAt(const Point &local) const
    {
        if (!mActive || mFitted.w <= 0.0 || mFitted.h <= 0.0) return crop::Part::None;
        return crop::partAt(boxPx(), local, grabPx());
    }

    void CropOverlay::emitChange(bool live)
    {
        if (onChange) onChange(mCrop.x, mCrop.y, mCrop.w, mCrop.h, live);
    }

    void CropOverlay::advance(double nowMs)
    {
        // R-G-1: appearing with the tab is a visible change, so it fades rather than flips.
        if (mAppearWanted != mActive)
        {
            mAppearWanted = mActive;
            mAppear.animateTo(mActive ? 1.0 : 0.0, kAppearMs, Easing::EaseOutCubic, nowMs);
        }
        mAppear.update(nowMs);
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    bool CropOverlay::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mActive) return Segment::handleGesture(g, local);

        if (g.type == Gesture::Type::Move)
        {
            // Hover feedback per part, so a corner lights up before it is grabbed — the only
            // affordance available without a cursor the HAL does not have.
            mHoverPart = partAt(local);
            if (mHoverPart == crop::Part::None) mHover.clear();
            else mHover.setHovered((int)mHoverPart);
            return false;   // never consume a Move: the pan and the seam want them too
        }

        // Claim a part ONLY when nothing is claimed yet. `DragStart` arrives after the drag
        // threshold, so the pointer has already left the spot it pressed — re-running the hit
        // test there silently changed a CORNER grab into an EDGE grab (and an edge grab into a
        // move), which then resized the wrong axis or nothing at all. Same principle as D-50:
        // a gesture in flight outranks a fresh hit test.
        if ((g.type == Gesture::Type::Down || g.type == Gesture::Type::DragStart) &&
            mPart == crop::Part::None)
        {
            const crop::Part p = partAt(local);
            if (p == crop::Part::None) return Segment::handleGesture(g, local);
            mPart = p;
            // D-32's lesson: remember where inside the handle the pointer landed and add it
            // back on every move, so a corner grabbed 10 px off-centre resizes BY the drag
            // rather than jumping the corner under the pointer.
            const Rect box = boxPx();
            double ax = 0.0, ay = 0.0;
            switch (p)
            {
            case crop::Part::Move:        ax = box.x; ay = box.y; break;
            case crop::Part::Left:        ax = box.x; ay = local.y; break;
            case crop::Part::Right:       ax = box.x + box.w; ay = local.y; break;
            case crop::Part::Top:         ax = local.x; ay = box.y; break;
            case crop::Part::Bottom:      ax = local.x; ay = box.y + box.h; break;
            case crop::Part::TopLeft:     ax = box.x; ay = box.y; break;
            case crop::Part::TopRight:    ax = box.x + box.w; ay = box.y; break;
            case crop::Part::BottomRight: ax = box.x + box.w; ay = box.y + box.h; break;
            case crop::Part::BottomLeft:  ax = box.x; ay = box.y + box.h; break;
            default: break;
            }
            mGrabDX = local.x - ax;
            mGrabDY = local.y - ay;
            mHover.setHovered((int)p);
            return true;
        }

        if (mPart != crop::Part::None)
        {
            switch (g.type)
            {
            case Gesture::Type::Drag:
            case Gesture::Type::DragStart:
            {
                const double nx = nxOf(local.x - mGrabDX), ny = nyOf(local.y - mGrabDY);
                if (mPart == crop::Part::Move)
                {
                    // R-CROP-3: a MOVE is never constrained by the ratio — the lock is about
                    // shape, and this is the gesture the user reported missing.
                    mCrop = crop::moveTo(mCrop, nx, ny);
                }
                else
                {
                    mCrop = crop::resizeBy(mCrop, mPart, nx, ny, normRatio());
                }
                emitChange(/*live=*/true);
                return true;
            }
            case Gesture::Type::Up:
            case Gesture::Type::Drop:
                mPart = crop::Part::None;
                mGrabDX = mGrabDY = 0.0;      // nothing leaks into the next gesture
                emitChange(/*live=*/false);   // the settled value, so history records one entry
                return true;
            default:
                break;
            }
        }
        return Segment::handleGesture(g, local);
    }

    void CropOverlay::onPaint(IRenderTarget &t) const
    {
        const double a = mAppear.value();
        if (a <= 0.001 || mFitted.w <= 0.0 || mFitted.h <= 0.0) return;
        const Rect box = boxPx();

        // 1. Dim what will be discarded — four rectangles around the box rather than one big
        //    one with a hole, because the HAL has no even-odd fill and two stacked alphas over
        //    the kept area would read as a different exposure.
        const Color dim = palette::whiteAlpha(0.0);
        (void)dim;
        const Paint shade = Paint::filled(Color{0.0, 0.0, 0.0, (float)(kDimAlpha * a)});
        const double fx = mFitted.x, fy = mFitted.y, fw = mFitted.w, fh = mFitted.h;
        drawRoundedRect(t, Rect{fx, fy, fw, box.y - fy}, 0.0, shade);                       // above
        drawRoundedRect(t, Rect{fx, box.y + box.h, fw, (fy + fh) - (box.y + box.h)}, 0.0, shade);  // below
        drawRoundedRect(t, Rect{fx, box.y, box.x - fx, box.h}, 0.0, shade);                 // left
        drawRoundedRect(t, Rect{box.x + box.w, box.y, (fx + fw) - (box.x + box.w), box.h}, 0.0, shade);  // right

        // 2. Thirds grid over what is KEPT — the classic framing aid, and the reason a crop box
        //    is easier to judge than four numbers.
        {
            Paint line = Paint::filled(palette::whiteAlpha(0.18 * a));
            for (int i = 1; i <= 2; ++i)
            {
                const double x = box.x + box.w * i / 3.0;
                const double y = box.y + box.h * i / 3.0;
                drawRoundedRect(t, Rect{x - 0.5, box.y, 1.0, box.h}, 0.0, line);
                drawRoundedRect(t, Rect{box.x, y - 0.5, box.w, 1.0}, 0.0, line);
            }
        }

        // 3. The border, then the corner brackets. Brackets rather than dots because they read
        //    as "this corner is a handle" at a glance and do not hide the photo under them.
        {
            Paint border = Paint::filled(palette::whiteAlpha(0.85 * a));
            drawRoundedRect(t, Rect{box.x, box.y - 0.5, box.w, 1.0}, 0.0, border);
            drawRoundedRect(t, Rect{box.x, box.y + box.h - 0.5, box.w, 1.0}, 0.0, border);
            drawRoundedRect(t, Rect{box.x - 0.5, box.y, 1.0, box.h}, 0.0, border);
            drawRoundedRect(t, Rect{box.x + box.w - 0.5, box.y, 1.0, box.h}, 0.0, border);
        }
        {
            const double L = std::min(kHandleLen, std::min(box.w, box.h) * 0.4);
            struct Corner { double x, y, sx, sy; crop::Part part; };
            const Corner corners[4] = {
                {box.x, box.y, 1.0, 1.0, crop::Part::TopLeft},
                {box.x + box.w, box.y, -1.0, 1.0, crop::Part::TopRight},
                {box.x + box.w, box.y + box.h, -1.0, -1.0, crop::Part::BottomRight},
                {box.x, box.y + box.h, 1.0, -1.0, crop::Part::BottomLeft}};
            for (const Corner &c : corners)
            {
                // Brightened from the EASED hover amount every frame, not set once when the
                // pointer arrived — R-G-1 clause (d).
                const double hv = mHover.amount((int)c.part);
                Paint p = Paint::filled(palette::whiteAlpha((0.85 + 0.15 * hv) * a));
                const double w = kHandleW + hv;
                const double ax = c.sx > 0 ? c.x : c.x - L;
                const double ay = c.sy > 0 ? c.y : c.y - w;
                drawRoundedRect(t, Rect{ax, c.sy > 0 ? c.y : c.y - w, L, w}, 0.0, p);
                drawRoundedRect(t, Rect{c.sx > 0 ? c.x : c.x - w, c.sy > 0 ? c.y : c.y - L, w, L}, 0.0, p);
                (void)ay;
            }
        }
    }
}
}
