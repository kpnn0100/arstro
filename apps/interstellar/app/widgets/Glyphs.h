/*
 *  interstellar_v1 — Glyphs: the few line icons cosmo's `icon::` set has no word for.
 *
 *  Cosmo's lucide set (`apps/cosmo/widgets/Icons.h`) is reused wherever it has the glyph — folder,
 *  image, save, close, chevrons, ban, check, refresh. These are the timeline vocabulary it lacks
 *  (a film frame, a lock, play/pause/skip, a speaker, a warning triangle, a plus/minus), drawn in
 *  the same manner: strokes fitted to a box, colour and stroke width supplied by the caller, no
 *  colour of their own. Header-only: stateless drawing helpers, like cosmo's DashedLine.h.
 */
#pragma once
#include "../Theme.h"
#include <cmath>

namespace arstro
{
namespace interstellar_v1
{
namespace glyph
{
    using artboard::Color;
    using artboard::IRenderTarget;
    using artboard::Rect;

    inline void line(IRenderTarget &t, double x0, double y0, double x1, double y1, const Color &c, double sw)
    {
        t.setStroke(c, sw);
        t.beginPath(); t.moveTo(x0, y0); t.lineTo(x1, y1); t.strokePath();
    }

    /** A film frame: a rounded body with a sprocket row top and bottom. */
    inline void film(IRenderTarget &t, const Rect &b, const Color &c, double sw = 1.1)
    {
        artboard::drawRoundedRect(t, Rect{b.x + 0.5, b.y + b.h * 0.12, b.w - 1.0, b.h * 0.76}, 1.0, artboard::Paint::stroked(c, sw));
        const int n = 3;
        for (int i = 0; i < n; ++i)
        {
            const double x = b.x + b.w * (0.22 + 0.28 * i);
            line(t, x, b.y + b.h * 0.12, x, b.y + b.h * 0.26, c, sw);
            line(t, x, b.y + b.h * 0.74, x, b.y + b.h * 0.88, c, sw);
        }
    }

    /** A padlock: shackle arc over a body. */
    inline void lock(IRenderTarget &t, const Rect &b, const Color &c, double sw = 1.1)
    {
        const double bw = b.w * 0.76, bh = b.h * 0.48;
        const double bx = b.x + (b.w - bw) * 0.5, by = b.y + b.h - bh - 0.5;
        artboard::drawRoundedRect(t, Rect{bx, by, bw, bh}, 1.0, artboard::Paint::filled(c));
        const double r = bw * 0.32, cx = b.x + b.w * 0.5, top = by - r * 1.15;
        t.setStroke(c, sw);
        t.beginPath();
        t.moveTo(cx - r, by);
        t.lineTo(cx - r, top + r);
        t.cubicTo(cx - r, top - r * 0.35, cx + r, top - r * 0.35, cx + r, top + r);
        t.lineTo(cx + r, by);
        t.strokePath();
    }

    inline void play(IRenderTarget &t, const Rect &b, const Color &c)
    {
        t.setFill(c);
        t.beginPath();
        t.moveTo(b.x + b.w * 0.24, b.y + b.h * 0.14);
        t.lineTo(b.x + b.w * 0.86, b.y + b.h * 0.5);
        t.lineTo(b.x + b.w * 0.24, b.y + b.h * 0.86);
        t.closePath();
        t.fillPath();
    }

    inline void pause(IRenderTarget &t, const Rect &b, const Color &c)
    {
        const double w = b.w * 0.22;
        artboard::drawRoundedRect(t, Rect{b.x + b.w * 0.22, b.y + b.h * 0.16, w, b.h * 0.68}, 1.0, artboard::Paint::filled(c));
        artboard::drawRoundedRect(t, Rect{b.x + b.w * 0.56, b.y + b.h * 0.16, w, b.h * 0.68}, 1.0, artboard::Paint::filled(c));
    }

    /** Skip to a cut: a bar and a triangle, pointing left (dir = -1) or right (dir = +1). */
    inline void skip(IRenderTarget &t, const Rect &b, const Color &c, int dir)
    {
        const double cx = b.x + b.w * 0.5;
        const double barX = dir < 0 ? b.x + b.w * 0.2 : b.x + b.w * 0.8;
        line(t, barX, b.y + b.h * 0.2, barX, b.y + b.h * 0.8, c, 1.4);
        t.setFill(c);
        t.beginPath();
        if (dir < 0)
        {
            t.moveTo(b.x + b.w * 0.82, b.y + b.h * 0.2);
            t.lineTo(cx - b.w * 0.2, b.y + b.h * 0.5);
            t.lineTo(b.x + b.w * 0.82, b.y + b.h * 0.8);
        }
        else
        {
            t.moveTo(b.x + b.w * 0.18, b.y + b.h * 0.2);
            t.lineTo(cx + b.w * 0.2, b.y + b.h * 0.5);
            t.lineTo(b.x + b.w * 0.18, b.y + b.h * 0.8);
        }
        t.closePath();
        t.fillPath();
    }

    inline void chevronLeft(IRenderTarget &t, const Rect &b, const Color &c, double sw = 1.3)
    {
        t.setStroke(c, sw);
        t.beginPath();
        t.moveTo(b.x + b.w * 0.62, b.y + b.h * 0.22);
        t.lineTo(b.x + b.w * 0.36, b.y + b.h * 0.5);
        t.lineTo(b.x + b.w * 0.62, b.y + b.h * 0.78);
        t.strokePath();
    }
    inline void chevronRightSmall(IRenderTarget &t, const Rect &b, const Color &c, double sw = 1.3)
    {
        t.setStroke(c, sw);
        t.beginPath();
        t.moveTo(b.x + b.w * 0.38, b.y + b.h * 0.22);
        t.lineTo(b.x + b.w * 0.64, b.y + b.h * 0.5);
        t.lineTo(b.x + b.w * 0.38, b.y + b.h * 0.78);
        t.strokePath();
    }
    inline void caretDown(IRenderTarget &t, const Rect &b, const Color &c, double sw = 1.2)
    {
        t.setStroke(c, sw);
        t.beginPath();
        t.moveTo(b.x + b.w * 0.2, b.y + b.h * 0.38);
        t.lineTo(b.x + b.w * 0.5, b.y + b.h * 0.66);
        t.lineTo(b.x + b.w * 0.8, b.y + b.h * 0.38);
        t.strokePath();
    }

    /** A camera body with a lens — "capture this frame" (R-UI-11). */
    inline void camera(IRenderTarget &t, const Rect &b, const Color &c, double sw = 1.2)
    {
        const double x = b.x, y = b.y + b.h * 0.22, w = b.w, h = b.h * 0.66;
        t.setStroke(c, sw);
        t.beginPath();
        t.moveTo(x + w * 0.08, y + h * 0.18);
        t.lineTo(x + w * 0.30, y + h * 0.18);
        t.lineTo(x + w * 0.38, y);
        t.lineTo(x + w * 0.62, y);
        t.lineTo(x + w * 0.70, y + h * 0.18);
        t.lineTo(x + w * 0.92, y + h * 0.18);
        t.lineTo(x + w * 0.92, y + h);
        t.lineTo(x + w * 0.08, y + h);
        t.closePath();
        t.strokePath();
        // the lens: a circle as four cubics (k = 0.5523 — the standard quarter-circle handle)
        const double cx = x + w * 0.5, cy = y + h * 0.58, r = std::min(w, h) * 0.24, k = 0.5523 * r;
        t.beginPath();
        t.moveTo(cx + r, cy);
        t.cubicTo(cx + r, cy + k, cx + k, cy + r, cx, cy + r);
        t.cubicTo(cx - k, cy + r, cx - r, cy + k, cx - r, cy);
        t.cubicTo(cx - r, cy - k, cx - k, cy - r, cx, cy - r);
        t.cubicTo(cx + k, cy - r, cx + r, cy - k, cx + r, cy);
        t.closePath();
        t.strokePath();
    }

    /** ✓ — a finished, successful action (the notice chip). */
    inline void check(IRenderTarget &t, const Rect &b, const Color &c, double sw = 1.3)
    {
        t.setStroke(c, sw);
        t.beginPath();
        t.moveTo(b.x + b.w * 0.12, b.y + b.h * 0.55);
        t.lineTo(b.x + b.w * 0.40, b.y + b.h * 0.82);
        t.lineTo(b.x + b.w * 0.90, b.y + b.h * 0.22);
        t.strokePath();
    }

    inline void plus(IRenderTarget &t, const Rect &b, const Color &c, double sw = 1.3)
    {
        const double cx = b.x + b.w * 0.5, cy = b.y + b.h * 0.5, r = b.w * 0.36;
        line(t, cx - r, cy, cx + r, cy, c, sw);
        line(t, cx, cy - r, cx, cy + r, c, sw);
    }
    inline void minus(IRenderTarget &t, const Rect &b, const Color &c, double sw = 1.3)
    {
        const double cx = b.x + b.w * 0.5, cy = b.y + b.h * 0.5, r = b.w * 0.36;
        line(t, cx - r, cy, cx + r, cy, c, sw);
    }

    /** A speaker — an audio track. */
    inline void speaker(IRenderTarget &t, const Rect &b, const Color &c, double sw = 1.1)
    {
        t.setFill(c);
        t.beginPath();
        t.moveTo(b.x + b.w * 0.12, b.y + b.h * 0.38);
        t.lineTo(b.x + b.w * 0.32, b.y + b.h * 0.38);
        t.lineTo(b.x + b.w * 0.56, b.y + b.h * 0.16);
        t.lineTo(b.x + b.w * 0.56, b.y + b.h * 0.84);
        t.lineTo(b.x + b.w * 0.32, b.y + b.h * 0.62);
        t.lineTo(b.x + b.w * 0.12, b.y + b.h * 0.62);
        t.closePath();
        t.fillPath();
        t.setStroke(c, sw);
        t.beginPath();
        t.moveTo(b.x + b.w * 0.70, b.y + b.h * 0.32);
        t.quadTo(b.x + b.w * 0.82, b.y + b.h * 0.5, b.x + b.w * 0.70, b.y + b.h * 0.68);
        t.strokePath();
    }

    /** A warning triangle with a bang. */
    inline void warn(IRenderTarget &t, const Rect &b, const Color &c, double sw = 1.1)
    {
        t.setStroke(c, sw);
        t.beginPath();
        t.moveTo(b.x + b.w * 0.5, b.y + b.h * 0.1);
        t.lineTo(b.x + b.w * 0.94, b.y + b.h * 0.88);
        t.lineTo(b.x + b.w * 0.06, b.y + b.h * 0.88);
        t.closePath();
        t.strokePath();
        line(t, b.x + b.w * 0.5, b.y + b.h * 0.38, b.x + b.w * 0.5, b.y + b.h * 0.6, c, sw);
        artboard::drawCircle(t, b.x + b.w * 0.5, b.y + b.h * 0.74, sw * 0.6, artboard::Paint::filled(c));
    }

    /** An indeterminate spinner arc — cosmo's per-entry "still coming" cell (R-LOADUX-2). The
     *  phase is a PERIOD (900 ms per turn), a timing rather than a tween. */
    inline void spinner(IRenderTarget &t, double cx, double cy, double r, double phaseMs, double alpha = 1.0)
    {
        artboard::drawCircle(t, cx, cy, r, artboard::Paint::stroked(palette::whiteAlpha(0.10 * alpha), 1.6));
        const double a0 = std::fmod(phaseMs, 900.0) / 900.0 * 6.28318530718;
        Color head = palette::primaryAlpha(0.85 * alpha);
        t.setStroke(head, 1.6);
        t.beginPath();
        for (int s = 0; s <= 8; ++s)
        {
            const double a = a0 + (double)s / 8.0 * 1.5707963268;
            const double px = cx + std::cos(a) * r, py = cy + std::sin(a) * r;
            if (s == 0) t.moveTo(px, py); else t.lineTo(px, py);
        }
        t.strokePath();
    }
}
}
}
