#include "CropOverlay.h"
#include "../CosmoTheme.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace { constexpr double kPick = 14.0, kMin = 0.04, kHandle = 5.0; }

    CropOverlay::CropOverlay(const Color &accent) : mAccent(accent) {}

    Point CropOverlay::normToLocal(double nx, double ny) const
    {
        return Point{mFitted.x + nx * mFitted.w, mFitted.y + ny * mFitted.h};
    }
    void CropOverlay::localToNorm(const Point &p, double &nx, double &ny) const
    {
        nx = mFitted.w > 0 ? (p.x - mFitted.x) / mFitted.w : 0;
        ny = mFitted.h > 0 ? (p.y - mFitted.y) / mFitted.h : 0;
        nx = std::clamp(nx, 0.0, 1.0); ny = std::clamp(ny, 0.0, 1.0);
    }

    bool CropOverlay::hitTestSelf(const Point &p) const
    {
        return mActive && p.x >= 0 && p.x <= width.value() && p.y >= 0 && p.y <= height.value();
    }

    int CropOverlay::pickHandle(const Point &local) const
    {
        const Point c[4] = {normToLocal(mX, mY), normToLocal(mX + mW, mY),
                            normToLocal(mX, mY + mH), normToLocal(mX + mW, mY + mH)};
        for (int i = 0; i < 4; ++i)
            if (std::hypot(local.x - c[i].x, local.y - c[i].y) <= kPick) return i;
        const Point eL = normToLocal(mX, mY + mH / 2), eR = normToLocal(mX + mW, mY + mH / 2);
        const Point eT = normToLocal(mX + mW / 2, mY), eB = normToLocal(mX + mW / 2, mY + mH);
        if (std::hypot(local.x - eL.x, local.y - eL.y) <= kPick) return 4;
        if (std::hypot(local.x - eR.x, local.y - eR.y) <= kPick) return 5;
        if (std::hypot(local.x - eT.x, local.y - eT.y) <= kPick) return 6;
        if (std::hypot(local.x - eB.x, local.y - eB.y) <= kPick) return 7;
        double nx, ny; localToNorm(local, nx, ny);
        if (nx >= mX && nx <= mX + mW && ny >= mY && ny <= mY + mH) return 8;
        return -1;
    }

    void CropOverlay::applyAspect()
    {
        if (mAspect <= 0 || mFitted.h <= 0) return;
        // target pixel ratio = (mW*Pw)/(mH*Ph); keep the rect centre fixed.
        const double k = mFitted.w / mFitted.h;  // px-per-norm ratio
        const double cx = mX + mW / 2, cy = mY + mH / 2;
        mH = mW * k / mAspect;
        mX = cx - mW / 2; mY = cy - mH / 2;
        mW = std::min(mW, 1.0); mH = std::min(mH, 1.0);
        mX = std::clamp(mX, 0.0, 1.0 - mW); mY = std::clamp(mY, 0.0, 1.0 - mH);
    }

    void CropOverlay::emit() { if (onChange) onChange(mX, mY, mW, mH); }

    bool CropOverlay::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mActive) return false;
        if (g.type == Gesture::Type::Down)
        {
            mDrag = pickHandle(local);
            if (mDrag == 8) { double nx, ny; localToNorm(local, nx, ny); mGrabX = nx - mX; mGrabY = ny - mY; }
            return mDrag >= 0;
        }
        if (g.type == Gesture::Type::Up || g.type == Gesture::Type::Drop) { mDrag = -1; return true; }
        if (g.type != Gesture::Type::Drag && g.type != Gesture::Type::DragStart) return false;
        if (mDrag < 0) return false;

        double nx, ny; localToNorm(local, nx, ny);
        double l = mX, r = mX + mW, tp = mY, b = mY + mH;
        switch (mDrag)
        {
        case 0: l = std::min(nx, r - kMin); tp = std::min(ny, b - kMin); break;  // TL
        case 1: r = std::max(nx, l + kMin); tp = std::min(ny, b - kMin); break;  // TR
        case 2: l = std::min(nx, r - kMin); b = std::max(ny, tp + kMin); break;  // BL
        case 3: r = std::max(nx, l + kMin); b = std::max(ny, tp + kMin); break;  // BR
        case 4: l = std::min(nx, r - kMin); break;
        case 5: r = std::max(nx, l + kMin); break;
        case 6: tp = std::min(ny, b - kMin); break;
        case 7: b = std::max(ny, tp + kMin); break;
        case 8:
            mX = std::clamp(nx - mGrabX, 0.0, 1.0 - mW);
            mY = std::clamp(ny - mGrabY, 0.0, 1.0 - mH);
            emit(); return true;
        }
        mX = l; mY = tp; mW = r - l; mH = b - tp;
        if (mAspect > 0) applyAspect();
        emit();
        return true;
    }

    void CropOverlay::onPaint(IRenderTarget &t) const
    {
        if (!mActive) return;
        const Point tl = normToLocal(mX, mY), br = normToLocal(mX + mW, mY + mH);
        const double x = tl.x, y = tl.y, w = br.x - tl.x, h = br.y - tl.y;

        // dim the area outside the crop (four bands over the fitted photo)
        const Color shade{0, 0, 0, 0.45f};
        const Rect F = mFitted;
        auto band = [&](double bx, double by, double bw, double bh) {
            if (bw > 0 && bh > 0) drawRoundedRect(t, Rect{bx, by, bw, bh}, 0.0, Paint::filled(shade));
        };
        band(F.x, F.y, F.w, y - F.y);                       // top
        band(F.x, y + h, F.w, F.y + F.h - (y + h));         // bottom
        band(F.x, y, x - F.x, h);                           // left
        band(x + w, y, F.x + F.w - (x + w), h);             // right

        t.setStroke(mAccent, 1.5);
        t.beginPath();
        t.moveTo(x, y); t.lineTo(x + w, y); t.lineTo(x + w, y + h); t.lineTo(x, y + h); t.closePath();
        t.strokePath();
        // rule-of-thirds guides
        t.setStroke(Color{1, 1, 1, 0.3f}, 1.0);
        for (int i = 1; i < 3; ++i)
        {
            double gx = x + w * i / 3.0, gy = y + h * i / 3.0;
            t.beginPath(); t.moveTo(gx, y); t.lineTo(gx, y + h); t.strokePath();
            t.beginPath(); t.moveTo(x, gy); t.lineTo(x + w, gy); t.strokePath();
        }
        const Paint dot = Paint::filledStroked(mAccent, palette::bg(), 1.5);
        for (Point p : {Point{x, y}, Point{x + w, y}, Point{x, y + h}, Point{x + w, y + h}})
            drawCircle(t, p.x, p.y, kHandle, dot);
    }
}
}
