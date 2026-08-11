#include "Icons.h"

namespace arstro
{
namespace cosmo_v2
{
namespace icon
{
    using artboard::IRenderTarget;
    using artboard::Rect;
    using artboard::Color;

    namespace
    {
        // Map fractional (0..1) coordinates within `box` to an absolute point,
        // so every icon is authored in a normalized 0..1 square regardless of
        // the pixel size it's actually drawn at.
        struct Frac
        {
            const Rect &box;
            double x(double fx) const { return box.x + fx * box.w; }
            double y(double fy) const { return box.y + fy * box.h; }
        };
    }

    void chevronRight(IRenderTarget &t, const Rect &box, const Color &c, double strokeWidth)
    {
        Frac f{box};
        t.setStroke(c, strokeWidth);
        t.beginPath();
        t.moveTo(f.x(0.32), f.y(0.18));
        t.lineTo(f.x(0.68), f.y(0.5));
        t.lineTo(f.x(0.32), f.y(0.82));
        t.strokePath();
    }

    void chevronDown(IRenderTarget &t, const Rect &box, const Color &c, double strokeWidth)
    {
        Frac f{box};
        t.setStroke(c, strokeWidth);
        t.beginPath();
        t.moveTo(f.x(0.18), f.y(0.36));
        t.lineTo(f.x(0.5), f.y(0.68));
        t.lineTo(f.x(0.82), f.y(0.36));
        t.strokePath();
    }

    void panelLeft(IRenderTarget &t, const Rect &box, const Color &c, double strokeWidth)
    {
        Frac f{box};
        t.setStroke(c, strokeWidth);
        // Outer rounded frame.
        t.beginPath();
        const double r = box.w * 0.12;
        const double x0 = f.x(0.08), y0 = f.y(0.08), x1 = f.x(0.92), y1 = f.y(0.92);
        t.moveTo(x0 + r, y0);
        t.lineTo(x1 - r, y0);
        t.quadTo(x1, y0, x1, y0 + r);
        t.lineTo(x1, y1 - r);
        t.quadTo(x1, y1, x1 - r, y1);
        t.lineTo(x0 + r, y1);
        t.quadTo(x0, y1, x0, y1 - r);
        t.lineTo(x0, y0 + r);
        t.quadTo(x0, y0, x0 + r, y0);
        t.closePath();
        t.strokePath();
        // Vertical divider ~35% in from the left (the "panel" being toggled).
        t.beginPath();
        t.moveTo(f.x(0.38), f.y(0.08));
        t.lineTo(f.x(0.38), f.y(0.92));
        t.strokePath();
    }

    void save(IRenderTarget &t, const Rect &box, const Color &c, double strokeWidth)
    {
        Frac f{box};
        t.setStroke(c, strokeWidth);
        // Document body with a folded top-right corner.
        t.beginPath();
        const double r = box.w * 0.1;
        t.moveTo(f.x(0.15), f.y(0.1));
        t.lineTo(f.x(0.68), f.y(0.1));
        t.lineTo(f.x(0.85), f.y(0.27));
        t.lineTo(f.x(0.85), f.y(0.9) - r);
        t.quadTo(f.x(0.85), f.y(0.9), f.x(0.85) - r, f.y(0.9));
        t.lineTo(f.x(0.15) + r, f.y(0.9));
        t.quadTo(f.x(0.15), f.y(0.9), f.x(0.15), f.y(0.9) - r);
        t.closePath();
        t.strokePath();
        // Label slot line.
        t.beginPath();
        t.moveTo(f.x(0.3), f.y(0.35));
        t.lineTo(f.x(0.7), f.y(0.35));
        t.strokePath();
        // Bottom shutter rect.
        t.beginPath();
        t.moveTo(f.x(0.32), f.y(0.55));
        t.lineTo(f.x(0.68), f.y(0.55));
        t.lineTo(f.x(0.68), f.y(0.78));
        t.lineTo(f.x(0.32), f.y(0.78));
        t.closePath();
        t.strokePath();
    }

    void upload(IRenderTarget &t, const Rect &box, const Color &c, double strokeWidth)
    {
        Frac f{box};
        t.setStroke(c, strokeWidth);
        t.beginPath();
        t.moveTo(f.x(0.5), f.y(0.78));
        t.lineTo(f.x(0.5), f.y(0.2));
        t.moveTo(f.x(0.28), f.y(0.42));
        t.lineTo(f.x(0.5), f.y(0.2));
        t.lineTo(f.x(0.72), f.y(0.42));
        t.strokePath();
        t.beginPath();
        t.moveTo(f.x(0.18), f.y(0.88));
        t.lineTo(f.x(0.82), f.y(0.88));
        t.strokePath();
    }

    void download(IRenderTarget &t, const Rect &box, const Color &c, double strokeWidth)
    {
        Frac f{box};
        t.setStroke(c, strokeWidth);
        t.beginPath();
        t.moveTo(f.x(0.5), f.y(0.2));
        t.lineTo(f.x(0.5), f.y(0.68));
        t.moveTo(f.x(0.28), f.y(0.46));
        t.lineTo(f.x(0.5), f.y(0.68));
        t.lineTo(f.x(0.72), f.y(0.46));
        t.strokePath();
        t.beginPath();
        t.moveTo(f.x(0.18), f.y(0.88));
        t.lineTo(f.x(0.82), f.y(0.88));
        t.strokePath();
    }

    void refreshCw(IRenderTarget &t, const Rect &box, const Color &c, double strokeWidth)
    {
        Frac f{box};
        t.setStroke(c, strokeWidth);
        const double cx = f.x(0.5), cy = f.y(0.5);
        const double r = box.w * 0.32;
        // Top arc (opens toward bottom-left) with an arrowhead at its end.
        t.beginPath();
        t.moveTo(cx - r, cy);
        t.quadTo(cx - r, cy - r, cx, cy - r);
        t.quadTo(cx + r * 0.4, cy - r, cx + r, cy - r * 0.4);
        t.strokePath();
        t.beginPath();
        t.moveTo(cx + r * 0.55, cy - r * 0.85);
        t.lineTo(cx + r, cy - r * 0.4);
        t.lineTo(cx + r * 0.45, cy - r * 0.15);
        t.strokePath();
        // Bottom arc (mirrored) with an arrowhead at its end.
        t.beginPath();
        t.moveTo(cx + r, cy);
        t.quadTo(cx + r, cy + r, cx, cy + r);
        t.quadTo(cx - r * 0.4, cy + r, cx - r, cy + r * 0.4);
        t.strokePath();
        t.beginPath();
        t.moveTo(cx - r * 0.55, cy + r * 0.85);
        t.lineTo(cx - r, cy + r * 0.4);
        t.lineTo(cx - r * 0.45, cy + r * 0.15);
        t.strokePath();
    }

    void trash2(IRenderTarget &t, const Rect &box, const Color &c, double strokeWidth)
    {
        Frac f{box};
        t.setStroke(c, strokeWidth);
        // Lid.
        t.beginPath();
        t.moveTo(f.x(0.15), f.y(0.28));
        t.lineTo(f.x(0.85), f.y(0.28));
        t.strokePath();
        // Handle.
        t.beginPath();
        t.moveTo(f.x(0.38), f.y(0.28));
        t.lineTo(f.x(0.4), f.y(0.14));
        t.lineTo(f.x(0.6), f.y(0.14));
        t.lineTo(f.x(0.62), f.y(0.28));
        t.strokePath();
        // Body.
        t.beginPath();
        t.moveTo(f.x(0.22), f.y(0.28));
        t.lineTo(f.x(0.28), f.y(0.88));
        t.lineTo(f.x(0.72), f.y(0.88));
        t.lineTo(f.x(0.78), f.y(0.28));
        t.strokePath();
        // Ridges.
        t.beginPath();
        t.moveTo(f.x(0.4), f.y(0.4));
        t.lineTo(f.x(0.42), f.y(0.76));
        t.moveTo(f.x(0.6), f.y(0.4));
        t.lineTo(f.x(0.58), f.y(0.76));
        t.strokePath();
    }

    void rotateCcw(IRenderTarget &t, const Rect &box, const Color &c, double strokeWidth)
    {
        Frac f{box};
        t.setStroke(c, strokeWidth);
        const double cx = f.x(0.52), cy = f.y(0.52);
        const double r = box.w * 0.34;
        // ~300 degree arc open toward the top-right, counter-clockwise, via three
        // quad segments (Artboard's path API has no direct arc primitive).
        t.beginPath();
        t.moveTo(cx + r, cy - r * 0.2);
        t.quadTo(cx + r, cy + r, cx, cy + r);
        t.quadTo(cx - r, cy + r, cx - r, cy);
        t.quadTo(cx - r, cy - r, cx, cy - r);
        t.strokePath();
        // Arrowhead at the open (top) end, pointing counter-clockwise (left-up).
        t.beginPath();
        t.moveTo(cx - r * 0.15, cy - r * 1.35);
        t.lineTo(cx, cy - r);
        t.lineTo(cx + r * 0.4, cy - r * 1.2);
        t.strokePath();
    }

    void folder(IRenderTarget &t, const Rect &box, const Color &c, double strokeWidth)
    {
        Frac f{box};
        t.setStroke(c, strokeWidth);
        // Folder silhouette: a tab along the top-left, then the body.
        t.beginPath();
        t.moveTo(f.x(0.08), f.y(0.82));
        t.lineTo(f.x(0.08), f.y(0.2));
        t.lineTo(f.x(0.42), f.y(0.2));
        t.lineTo(f.x(0.52), f.y(0.34));
        t.lineTo(f.x(0.92), f.y(0.34));
        t.lineTo(f.x(0.92), f.y(0.82));
        t.closePath();
        t.strokePath();
    }

    void image(IRenderTarget &t, const Rect &box, const Color &c, double strokeWidth)
    {
        Frac f{box};
        t.setStroke(c, strokeWidth);
        // Frame.
        t.beginPath();
        t.moveTo(f.x(0.12), f.y(0.16));
        t.lineTo(f.x(0.88), f.y(0.16));
        t.lineTo(f.x(0.88), f.y(0.84));
        t.lineTo(f.x(0.12), f.y(0.84));
        t.closePath();
        t.strokePath();
        // Sun + the mountain ridge that reads as "photo".
        t.beginPath();
        t.moveTo(f.x(0.42), f.y(0.36));
        t.quadTo(f.x(0.42), f.y(0.28), f.x(0.34), f.y(0.28));
        t.quadTo(f.x(0.26), f.y(0.28), f.x(0.26), f.y(0.36));
        t.quadTo(f.x(0.26), f.y(0.44), f.x(0.34), f.y(0.44));
        t.quadTo(f.x(0.42), f.y(0.44), f.x(0.42), f.y(0.36));
        t.strokePath();
        t.beginPath();
        t.moveTo(f.x(0.14), f.y(0.78));
        t.lineTo(f.x(0.42), f.y(0.5));
        t.lineTo(f.x(0.62), f.y(0.7));
        t.lineTo(f.x(0.74), f.y(0.58));
        t.lineTo(f.x(0.87), f.y(0.72));
        t.strokePath();
    }

    void close(IRenderTarget &t, const Rect &box, const Color &c, double strokeWidth)
    {
        Frac f{box};
        t.setStroke(c, strokeWidth);
        t.beginPath();
        t.moveTo(f.x(0.22), f.y(0.22));
        t.lineTo(f.x(0.78), f.y(0.78));
        t.moveTo(f.x(0.78), f.y(0.22));
        t.lineTo(f.x(0.22), f.y(0.78));
        t.strokePath();
    }

    void ban(IRenderTarget &t, const Rect &box, const Color &c, double strokeWidth)
    {
        Frac f{box};
        t.setStroke(c, strokeWidth);
        const double cx = f.x(0.5), cy = f.y(0.5);
        const double r = box.w * 0.38;
        // Circle, as four quad segments (no arc primitive in the path API).
        t.beginPath();
        t.moveTo(cx + r, cy);
        t.quadTo(cx + r, cy + r, cx, cy + r);
        t.quadTo(cx - r, cy + r, cx - r, cy);
        t.quadTo(cx - r, cy - r, cx, cy - r);
        t.quadTo(cx + r, cy - r, cx + r, cy);
        t.closePath();
        t.strokePath();
        // The slash through it.
        const double d = r * 0.707;
        t.beginPath();
        t.moveTo(cx - d, cy - d);
        t.lineTo(cx + d, cy + d);
        t.strokePath();
    }

    void check(IRenderTarget &t, const Rect &box, const Color &c, double strokeWidth)
    {
        Frac f{box};
        t.setStroke(c, strokeWidth);
        t.beginPath();
        t.moveTo(f.x(0.18), f.y(0.52));
        t.lineTo(f.x(0.42), f.y(0.76));
        t.lineTo(f.x(0.84), f.y(0.26));
        t.strokePath();
    }

    void checkCircle(IRenderTarget &t, const Rect &box, const Color &c, double strokeWidth)
    {
        Frac f{box};
        const double cx = f.x(0.5), cy = f.y(0.5);
        const double r = box.w * 0.42;
        t.setStroke(c, strokeWidth);
        t.beginPath();
        t.moveTo(cx + r, cy);
        t.quadTo(cx + r, cy + r, cx, cy + r);
        t.quadTo(cx - r, cy + r, cx - r, cy);
        t.quadTo(cx - r, cy - r, cx, cy - r);
        t.quadTo(cx + r, cy - r, cx + r, cy);
        t.closePath();
        t.strokePath();
        // The tick, inset so it sits comfortably inside the ring.
        t.beginPath();
        t.moveTo(cx - r * 0.46, cy + r * 0.04);
        t.lineTo(cx - r * 0.10, cy + r * 0.42);
        t.lineTo(cx + r * 0.50, cy - r * 0.38);
        t.strokePath();
    }
}
}
}
