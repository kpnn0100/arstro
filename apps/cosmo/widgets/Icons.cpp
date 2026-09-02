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

    void deleteBin(IRenderTarget &t, const Rect &box, const Color &c, double strokeWidth)
    {
        // Traced from the supplied `delete-2` artwork, viewBox 24 — every number below is a
        // source coordinate divided by 24, so it can be checked against the file. The source
        // strokes at 2/24 (~0.083 of the box), which is 1.1 px at the 13 px this renders at,
        // so the default weight is unchanged from what it replaces.
        Frac f{box};
        t.setStroke(c, strokeWidth);
        // Lid: M4 7 H20
        t.beginPath();
        t.moveTo(f.x(4 / 24.0), f.y(7 / 24.0));
        t.lineTo(f.x(20 / 24.0), f.y(7 / 24.0));
        t.strokePath();
        // Handle: M9 5 C9 3.895 9.895 3 11 3 H13 C14.105 3 15 3.895 15 5 V7
        t.beginPath();
        t.moveTo(f.x(9 / 24.0), f.y(5 / 24.0));
        t.cubicTo(f.x(9 / 24.0), f.y(3.895 / 24.0), f.x(9.895 / 24.0), f.y(3 / 24.0),
                  f.x(11 / 24.0), f.y(3 / 24.0));
        t.lineTo(f.x(13 / 24.0), f.y(3 / 24.0));
        t.cubicTo(f.x(14.105 / 24.0), f.y(3 / 24.0), f.x(15 / 24.0), f.y(3.895 / 24.0),
                  f.x(15 / 24.0), f.y(5 / 24.0));
        t.lineTo(f.x(15 / 24.0), f.y(7 / 24.0));
        t.strokePath();
        // Can: M6 10 V18 C6 19.657 7.343 21 9 21 H15 C16.657 21 18 19.657 18 18 V10.
        // Straight sides with rounded bottom corners — the shape that distinguishes this from
        // the tapered lucide bin it replaces.
        t.beginPath();
        t.moveTo(f.x(6 / 24.0), f.y(10 / 24.0));
        t.lineTo(f.x(6 / 24.0), f.y(18 / 24.0));
        t.cubicTo(f.x(6 / 24.0), f.y(19.657 / 24.0), f.x(7.343 / 24.0), f.y(21 / 24.0),
                  f.x(9 / 24.0), f.y(21 / 24.0));
        t.lineTo(f.x(15 / 24.0), f.y(21 / 24.0));
        t.cubicTo(f.x(16.657 / 24.0), f.y(21 / 24.0), f.x(18 / 24.0), f.y(19.657 / 24.0),
                  f.x(18 / 24.0), f.y(18 / 24.0));
        t.lineTo(f.x(18 / 24.0), f.y(10 / 24.0));
        t.strokePath();
        // Ridges: M10 12 V17 and M14 12 V17 — parallel here, where the old glyph splayed them.
        t.beginPath();
        t.moveTo(f.x(10 / 24.0), f.y(12 / 24.0));
        t.lineTo(f.x(10 / 24.0), f.y(17 / 24.0));
        t.moveTo(f.x(14 / 24.0), f.y(12 / 24.0));
        t.lineTo(f.x(14 / 24.0), f.y(17 / 24.0));
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

    void pipette(IRenderTarget &t, const Rect &box, const Color &c, double strokeWidth)
    {
        // Traced from the supplied `color-picker-dropper-colour` artwork, viewBox 32 — the
        // numbers below are source coordinates divided by 32.
        //
        // The source is ONE filled path with a second subpath hollowing the barrel out. That
        // hollow is 1.98 source units across, which is 0.8 px at the 13 px this renders at, so
        // reproducing it as a fill would ask the rasteriser to resolve a sub-pixel hole and
        // would come out as a grey smear. It is drawn as the stroke it visually is instead, and
        // only the HEAD — where the artwork is genuinely solid, 7.6 units across — is filled.
        // That split is what keeps the glyph reading as an eyedropper rather than as a pen.
        Frac f{box};
        // Head: the rounded 45-degree bar, corners (17,8.6) (22.3,3.3) (27.7,8.7) (22.4,14).
        t.setFill(c);
        t.beginPath();
        t.moveTo(f.x(17 / 32.0), f.y(8.6 / 32.0));
        t.cubicTo(f.x(19.8 / 32.0), f.y(5.8 / 32.0), f.x(23.8 / 32.0), f.y(1.8 / 32.0),
                  f.x(26.2 / 32.0), f.y(1.8 / 32.0));   // the source's rounded cap, one cubic
        t.cubicTo(f.x(29.2 / 32.0), f.y(4.8 / 32.0), f.x(29.2 / 32.0), f.y(7.2 / 32.0),
                  f.x(27.7 / 32.0), f.y(8.7 / 32.0));
        t.lineTo(f.x(22.4 / 32.0), f.y(14 / 32.0));
        t.closePath();
        t.fillPath();

        t.setStroke(c, strokeWidth);
        // Collar: the source has TWO small rounded stubs, (14.3,7.3)..(14.3,8.7) on the upper
        // edge of the barrel and (23.7,15.3)..(23.7,16.7) on the lower one, at different points
        // along it. Transcribed literally they come out as one visible tab and one detached
        // 2 px speck, because the head is drawn over everything between them. So they are drawn
        // as the one collar they read as: perpendicular to the barrel, just below the head,
        // equal on both sides. The barrel's direction is (-11, 11.6), so the perpendicular is
        // (0.686, 0.650) and +/-4.5 units puts the upper end at (14.5, 7.7) — which is where the
        // source's own stub is, so this is the artwork's geometry and not a substitute for it.
        t.beginPath();
        t.moveTo(f.x(14.51 / 32.0), f.y(7.67 / 32.0));
        t.lineTo(f.x(20.69 / 32.0), f.y(13.53 / 32.0));
        t.strokePath();
        // Barrel: the centreline between the source's two parallel edges, (17,8.6)->(5,20.6)
        // and (17,11.4)->(9,24.6), carried down to the tip at (4.5,26.5). Straight, and stopping
        // where the source's rounded foot begins: tracing that foot's cubics as a stroke curls
        // the end back on itself and the glyph reads as a walking stick.
        t.beginPath();
        t.moveTo(f.x(17.6 / 32.0), f.y(10.6 / 32.0));
        t.lineTo(f.x(5.6 / 32.0), f.y(25.2 / 32.0));
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
