/*
 *  arstro kit — the small drawing vocabulary every touch view shares.
 *
 *  Header-only helpers over the render HAL: a rounded rectangle, measured and aligned text, and an
 *  icon from the generated tables. They exist so a view states WHAT it draws (a tile, a centred
 *  label) and the one copy of HOW lives here.
 */
#pragma once
#include "IconTypes.h"
#include "Theme.h"
#include <algorithm>
#include <string>

namespace arstro
{
namespace kit
{
    /** Build a rounded rectangle as the current path. The radius is clamped to half the short
     *  side, so a box thinner than 2r degrades to a capsule instead of folding over itself. */
    inline void roundedRectPath(artboard::IRenderTarget &t, const artboard::Rect &r, double radius)
    {
        const double k = 0.5522847498;  // circle-by-cubic constant
        const double rad = std::max(0.0, std::min(radius, std::min(r.w, r.h) * 0.5));
        const double x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h, c = rad * k;
        t.beginPath();
        t.moveTo(x0 + rad, y0);
        t.lineTo(x1 - rad, y0);
        t.cubicTo(x1 - rad + c, y0, x1, y0 + rad - c, x1, y0 + rad);
        t.lineTo(x1, y1 - rad);
        t.cubicTo(x1, y1 - rad + c, x1 - rad + c, y1, x1 - rad, y1);
        t.lineTo(x0 + rad, y1);
        t.cubicTo(x0 + rad - c, y1, x0, y1 - rad + c, x0, y1 - rad);
        t.lineTo(x0, y0 + rad);
        t.cubicTo(x0, y0 + rad - c, x0 + rad - c, y0, x0 + rad, y0);
        t.closePath();
    }

    inline void fillRounded(artboard::IRenderTarget &t, const artboard::Rect &r, double radius, const artboard::Color &c)
    {
        if (r.w <= 0.0 || r.h <= 0.0 || c.a <= 0.0) return;
        roundedRectPath(t, r, radius);
        t.setFill(c);
        t.fillPath();
    }

    inline artboard::Color withAlpha(artboard::Color c, double a)
    {
        c.a *= a;
        return c;
    }

    inline artboard::Color mix(const artboard::Color &a, const artboard::Color &b, double t)
    {
        return artboard::Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    }

    /** "#RRGGBB" → colour; `fallback` when the text is not one. */
    inline artboard::Color parseColor(const std::string &s, const artboard::Color &fallback)
    {
        if (s.size() != 7 || s[0] != '#') return fallback;
        unsigned v = 0;
        for (size_t i = 1; i < 7; ++i)
        {
            const char ch = s[i];
            int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
            if (d < 0) return fallback;
            v = v * 16 + (unsigned)d;
        }
        return artboard::Color::hex(v);
    }

    /** Shorten `s` with an ellipsis until it fits `maxW`. Measured, so it is right for the real font. */
    inline std::string ellipsize(artboard::IRenderTarget &t, const std::string &s, double maxW, double size, const char *family)
    {
        if (t.measureText(s, size, family) <= maxW) return s;
        std::string cut = s;
        while (!cut.empty())
        {
            cut.pop_back();
            while (!cut.empty() && ((unsigned char)cut.back() & 0xC0) == 0x80) cut.pop_back();  // whole UTF-8 chars
            const std::string trial = cut + "\xE2\x80\xA6";
            if (t.measureText(trial, size, family) <= maxW) return trial;
        }
        return "";
    }

    enum class Align { Left, Center, Right };

    /** Text on one line, vertically centred on `centerY`, aligned inside [x, x+w], ellipsized to fit. */
    inline void drawLabel(artboard::IRenderTarget &t, const std::string &text, double x, double centerY, double w,
                          double size, const char *family, const artboard::Color &color, Align align = Align::Left,
                          double tracking = 0.0)
    {
        if (color.a <= 0.0 || w <= 0.0) return;
        const std::string s = ellipsize(t, text, w, size, family);
        const double tw = t.measureText(s, size, family, tracking);
        const double tx = align == Align::Left ? x : align == Align::Center ? x + (w - tw) * 0.5 : x + w - tw;
        t.setFill(color);
        t.drawText(s, tx, type::baseline(centerY, size), size, family, tracking);
    }

    /** One generated icon, fitted into `rect`, in one colour. */
    inline void drawIcon(artboard::IRenderTarget &t, const icons::IconPath &p, const artboard::Rect &rect,
                         const artboard::Color &color, double strokeWidth = 1.75)
    {
        if (p.count <= 0 || color.a <= 0.0) return;
        const double s = rect.w / p.viewSize;  // uniform: an icon is never stretched
        t.beginPath();
        for (int i = 0; i < p.count; ++i)
        {
            const icons::IconOp &op = p.ops[i];
            const auto X = [&](float v) { return rect.x + v * s; };
            const auto Y = [&](float v) { return rect.y + v * s; };
            switch (op.kind)
            {
            case icons::IconOp::Move: t.moveTo(X(op.v[0]), Y(op.v[1])); break;
            case icons::IconOp::Line: t.lineTo(X(op.v[0]), Y(op.v[1])); break;
            case icons::IconOp::Cubic: t.cubicTo(X(op.v[0]), Y(op.v[1]), X(op.v[2]), Y(op.v[3]), X(op.v[4]), Y(op.v[5])); break;
            case icons::IconOp::Close: t.closePath(); break;
            }
        }
        if (p.stroke) { t.setStroke(color, strokeWidth * s * (24.0 / p.viewSize)); t.strokePath(); }
        else { t.setFill(color); t.fillPath(); }
    }

    /** The app tile every launcher surface draws: the bundle's own picture is drawn by the caller;
     *  this is the fallback — a tinted rounded square with the app's initial — so an app without
     *  artwork still looks deliberate. */
    inline void drawMonogram(artboard::IRenderTarget &t, const artboard::Rect &r, const std::string &name,
                             const artboard::Color &tint, double alpha = 1.0)
    {
        fillRounded(t, r, r.w * radius::icon(), withAlpha(mix(tint, artboard::Color::hex(0x101216), 0.72), alpha));
        std::string initial = name.empty() ? "?" : name.substr(0, 1);
        if (!name.empty() && ((unsigned char)name[0] & 0x80))  // keep a whole UTF-8 first character
        {
            size_t n = 1;
            while (n < name.size() && ((unsigned char)name[n] & 0xC0) == 0x80) ++n;
            initial = name.substr(0, n);
        }
        drawLabel(t, initial, r.x, r.y + r.h * 0.5, r.w, r.w * 0.42, font::sansMedium(), withAlpha(mix(tint, palette::white(), 0.35), alpha), Align::Center);
    }
}
}
