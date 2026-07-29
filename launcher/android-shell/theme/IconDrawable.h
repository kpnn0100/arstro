/*
 *  arstro-android-shell — IconDrawable (M2.3): draws a generated icon path table.
 *
 *  Replays an `icons::IconPath` scaled from its viewSize into a target rect, then fills or
 *  strokes it (per the icon's `stroke` flag) in a themed colour. Emits only Artboard HAL path
 *  primitives, so it renders identically everywhere. Callers set `rect` (where) and `color`
 *  (from an AndroidColors role) and pick an icon via `setIcon(icons::kSomething)`.
 */
#pragma once
#include "artboard/artboard.h"
#include "theme/icons/IconTypes.h"

namespace arstro
{
namespace androidshell
{
    class IconDrawable : public artboard::Drawable
    {
    public:
        IconDrawable() = default;
        explicit IconDrawable(const icons::IconPath &path) : mPath(&path) {}

        void setIcon(const icons::IconPath &path) { mPath = &path; }
        const icons::IconPath *icon() const { return mPath; }

        artboard::Rect rect{0, 0, 24, 24};                      // destination in local space
        artboard::Color color = artboard::Color::rgba(255, 255, 255);
        double strokeWidth = 2.0;                               // used only for stroke icons

    protected:
        void onDraw(artboard::IRenderTarget &t) const override
        {
            if (!mPath || mPath->count <= 0 || mPath->viewSize <= 0.0f)
                return;
            const double sx = rect.w / mPath->viewSize;
            const double sy = rect.h / mPath->viewSize;
            const auto X = [&](float x) { return rect.x + x * sx; };
            const auto Y = [&](float y) { return rect.y + y * sy; };

            t.beginPath();
            for (int i = 0; i < mPath->count; ++i)
            {
                const icons::IconOp &op = mPath->ops[i];
                switch (op.kind)
                {
                case icons::IconOp::Move: t.moveTo(X(op.v[0]), Y(op.v[1])); break;
                case icons::IconOp::Line: t.lineTo(X(op.v[0]), Y(op.v[1])); break;
                case icons::IconOp::Cubic:
                    t.cubicTo(X(op.v[0]), Y(op.v[1]), X(op.v[2]), Y(op.v[3]), X(op.v[4]), Y(op.v[5]));
                    break;
                case icons::IconOp::Close: t.closePath(); break;
                }
            }
            if (mPath->stroke)
            {
                t.setStroke(color, strokeWidth);
                t.strokePath();
            }
            else
            {
                t.setFill(color);
                t.fillPath();
            }
        }

    private:
        const icons::IconPath *mPath = nullptr;
    };

} // namespace androidshell
} // namespace arstro
