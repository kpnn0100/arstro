/*
 *  Cosmo by arstro — CropOverlay: the drag-to-crop rectangle drawn over the photo
 *  while the Transform tab is active. The engine renders the FULL (uncropped) frame
 *  in this mode so the user can drag the crop box over the whole image; the box
 *  corners/edges resize, the interior moves, and an optional aspect-ratio lock keeps
 *  the pixel ratio fixed. All coordinates are normalised to the framed image (the
 *  same x/y/w/h the engine's Crop takes); the overlay only reports the rect.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>

namespace arstro
{
namespace cosmo
{
    class CropOverlay : public artboard::Segment
    {
    public:
        explicit CropOverlay(const artboard::Color &accent);

        std::function<void(double, double, double, double)> onChange;  // x,y,w,h (0..1)

        void setFittedRect(const artboard::Rect &localFitted) { mFitted = localFitted; }
        void setCrop(double x, double y, double w, double h) { mX = x; mY = y; mW = w; mH = h; }
        void setActive(bool a) { mActive = a; }
        /** Pixel aspect ratio (w:h) to lock to, 0 = free. */
        void setAspect(double ratio) { mAspect = ratio; if (ratio > 0) applyAspect(); }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override;

    private:
        artboard::Point normToLocal(double nx, double ny) const;
        void localToNorm(const artboard::Point &p, double &nx, double &ny) const;
        int pickHandle(const artboard::Point &local) const;
        void applyAspect();
        void emit();

        artboard::Color mAccent;
        double mX = 0, mY = 0, mW = 1, mH = 1;  // normalised crop rect
        double mAspect = 0;                       // 0 = free
        bool mActive = false;
        artboard::Rect mFitted{0, 0, 1, 1};
        int mDrag = -1;          // 0..3 corners, 4..7 edges, 8 move
        double mGrabX = 0, mGrabY = 0;  // for move: pointer offset from rect origin
    };
}
}
