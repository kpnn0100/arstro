/*
 *  Cosmo by arstro — MaskOverlay: the interactive layer drawn over the photo for
 *  positioning the SELECTED local-adjustment mask. It draws the mask geometry and
 *  draggable handles and maps pointer drags into the mask's normalised framed-image
 *  coordinates (the same space the engine's maskCoverage uses), reporting edits via
 *  onChange. The UI never touches pixels — it only edits MaskParams geometry.
 *    - Radial: drag the centre to move, the edge handles to resize.
 *    - Linear: drag the two endpoints (the 0% and 100% gradient lines).
 *    - Brush: drag anywhere to paint coverage dabs.
 *  Inactive (no mask selected) it is fully click-through.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../../ImageProcessing/src/engine/EditParams.h"
#include <functional>

namespace arstro
{
namespace cosmo
{
    class MaskOverlay : public artboard::Segment
    {
    public:
        explicit MaskOverlay(const artboard::Color &accent);

        /** Fired when the selected mask's geometry/dabs change (drag/paint). */
        std::function<void(const arstro::MaskParams &)> onChange;

        /** The photo's display rect in this overlay's local space (from ImageView). */
        void setFittedRect(const artboard::Rect &localFitted) { mFitted = localFitted; }
        void setMask(const arstro::MaskParams &m, bool active) { mMask = m; mActive = active; }
        /** Keep the overlay's working copy in sync with panel edits (adjust/feather/
         *  invert) WITHOUT changing visibility, so a later drag writes back the full,
         *  current mask instead of a stale one. */
        void updateMask(const arstro::MaskParams &m) { mMask = m; }
        void setBrushRadius(double normRadius) { mBrushRadius = normRadius; }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override;

    private:
        artboard::Point normToLocal(float nx, float ny) const;
        void localToNorm(const artboard::Point &p, float &nx, float &ny) const;
        int pickHandle(const artboard::Point &local) const;  // -1 none
        void applyDrag(int handle, const artboard::Point &local);

        artboard::Color mAccent;
        arstro::MaskParams mMask;
        bool mActive = false;
        artboard::Rect mFitted{0, 0, 1, 1};
        int mDrag = -1;            // 0..2 radial, 0..1 linear, 99 = brush paint
        double mBrushRadius = 0.08;
    };
}
}
