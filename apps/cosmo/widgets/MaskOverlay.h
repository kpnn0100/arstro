/*
 *  cosmo_v2 by arstro — MaskOverlay: the interactive layer drawn over the photo for
 *  positioning the SELECTED local-adjustment mask (R-MASK). Ported from cosmo's
 *  MaskOverlay: it draws the mask geometry + draggable handles and maps pointer
 *  drags into the mask's normalised framed-image coordinates (the same space the
 *  engine's maskCoverage uses), reporting edits via onChange. The UI never touches
 *  pixels — it only edits MaskParams geometry.
 *    - Radial: drag the centre to move, the edge handles to resize.
 *    - Linear: drag the two endpoints (the 0% and 100% gradient lines).
 *    - Brush: drag anywhere to paint coverage dabs.
 *    - Draw (R-MASK-6): click empty canvas to place a point of a closed outline, drag a
 *      point to move it, Alt-drag a point to pull its tangent handles (the same gesture
 *      the tone curve uses for the same thing), drag a handle to bend one side alone, and
 *      double-click a point to remove it. The outline is ALWAYS closed — there is no
 *      "finish the path" step to forget, and the engine closes it too, so what is drawn is
 *      what renders. The polygon comes from `maskPathPolygon`, the engine's own flattener,
 *      because a drawn outline that is not the rendered outline is worse than no mask.
 *  Inactive (no mask selected / Mask tab closed) it is fully click-through, so it
 *  never steals the zoom/pan drag or the Before/Split/After pill.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "../../../core/ImageProcessing/src/engine/EditParams.h"
#include "../../../core/ImageProcessing/src/engine/MaskStack.h"
#include <functional>

namespace arstro
{
namespace cosmo_v2
{
    class MaskOverlay : public artboard::Segment
    {
    public:
        explicit MaskOverlay(const artboard::Color &accent);

        /** Fired when the selected mask's geometry/dabs change (drag/paint). */
        std::function<void(const arstro::MaskParams &)> onChange;

        /** The photo's display rect in this overlay's local space (from ImageView,
         *  including the current zoom/pan — R-MASK-3). */
        void setFittedRect(const artboard::Rect &localFitted) { mFitted = localFitted; }
        void setMask(const arstro::MaskParams &m, bool active)
        {
            // Visibility always applies; the GEOMETRY does not while a handle is being
            // dragged — see updateMask.
            if (mDrag < 0) mMask = m;
            mActive = active;
        }
        /** Keep the overlay's working copy in sync WITHOUT changing visibility, so a
         *  later drag writes back the current mask instead of a stale one.
         *
         *  Ignored while a handle is in flight: R-SVC-12 re-seeds every panel from the model
         *  whenever a frame lands, which during a drag is every few tens of milliseconds, so
         *  this would replace the geometry the user is dragging with the last value the model
         *  happened to have. Same rule as `CurvePanel::setCurves` — a gesture in flight
         *  outranks the model — and the same bug it was fixed for. */
        void updateMask(const arstro::MaskParams &m) { if (mDrag < 0) mMask = m; }
        void setBrushRadius(double normRadius) { mBrushRadius = normRadius; }
        bool active() const { return mActive; }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override;

        /** The overlay's coordinate mapping. Protected rather than private so a test can grab a
         *  handle exactly where it is drawn and then drag past the image edge — R-MASK-5's whole
         *  point is what happens outside 0..1, which is unassertable from outside this mapping. */
        artboard::Point normToLocal(float nx, float ny) const;
        void localToNorm(const artboard::Point &p, float &nx, float &ny) const;

        /** How many points the drawn outline has, and where one is on screen. Protected for the
         *  same reason `normToLocal` is: the tests grab a point where it is DRAWN, and a path is
         *  the one mask whose geometry is a variable-length list, so "the second point" cannot
         *  be expressed in fitted-rect arithmetic alone. */
        int pathPointCount() const { return (int)mMask.path.size(); }
        artboard::Point pathPointAt(int i) const;

    private:
        /** `mDrag` is one int for every mask type, which is why these are offsets rather than
         *  an enum: 0..2 are the radial/linear handles, 99 is a brush stroke, and a path adds
         *  three families because a point, its in-handle and its out-handle are three different
         *  things to be dragging. Kept as arithmetic on one field so `applyDrag`'s signature —
         *  and the Down/Drag/Up flow around it — stays the same for all four types. */
        static constexpr int kDragPoint = 1000;   // + index: move point i
        static constexpr int kDragIn = 2000;      // + index: bend the segment BEFORE point i
        static constexpr int kDragOut = 3000;     // + index: bend the segment AFTER point i

        int pickHandle(const artboard::Point &local) const;  // -1 none
        int pickPathHandle(const artboard::Point &local) const;
        void applyDrag(int handle, const artboard::Point &local);
        void applyPathDrag(int handle, const artboard::Point &local);

        artboard::Color mAccent;
        arstro::MaskParams mMask;
        bool mActive = false;
        artboard::Rect mFitted{0, 0, 1, 1};
        int mDrag = -1;            // 0..2 radial, 0..1 linear, 99 = brush paint, kDrag* = path
        double mBrushRadius = 0.08;
        bool mDragAlt = false;     // the modifier at the PRESS, which is what decides a
                                   // point-move from a handle-pull for the whole gesture
    };
}
}
