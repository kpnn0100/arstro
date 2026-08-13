/*
 *  arstro-android-shell — Launcher (M6): the home surface (plan §2.6).
 *
 *  Wallpaper + scrim, a 5-column home grid of app icons + labels, a bottom dock, a page-dot
 *  indicator, and a swipe-up all-apps drawer with a live search field. App icons are the M2.6
 *  fallback — a tonal circle (hue hashed from the name) with the first letter — until GtkIconTheme
 *  pixmaps are wired (M6.2 refinement). Reads an AppList; a swipe-up / Click opens the drawer.
 */
#pragma once
#include "artboard/artboard.h"
#include "AppList.h"
#include "theme/AndroidColors.h"
#include "theme/Type.h"
#include "theme/Shape.h"

#include <cstdint>
#include <string>

namespace arstro
{
namespace androidshell
{
    class Launcher : public artboard::Segment
    {
    public:
        AppList *apps = nullptr;
        ThemeMode mode = ThemeMode::Dark;
        bool drawerOpen = false;      // all-apps drawer (M6.4)
        double drawerFraction = 0.0;  // 0 home .. 1 drawer (animated)
        std::string searchQuery;
        double scrollOffset = 0.0;

    protected:
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override
        {
            using T = artboard::Gesture::Type;
            // swipe up on home -> open drawer; swipe down in drawer -> close
            if (g.type == T::Drag)
            {
                const double dy = g.pos.y - g.start.y;
                if (!drawerOpen && dy < -40) { drawerOpen = true; }
                else if (drawerOpen && dy > 40 && scrollOffset <= 0) { drawerOpen = false; }
                return true;
            }
            if (g.type == T::Click && apps)
            {
                const int idx = iconAt(local);
                if (idx >= 0) { apps->launch((size_t)idx); return true; }
            }
            return false;
        }

        void advance(double nowMs) override
        {
            // ease the drawer fraction toward its target (drawer open/close)
            const double target = drawerOpen ? 1.0 : 0.0;
            drawerFraction += (target - drawerFraction) * 0.25;
            if (std::fabs(target - drawerFraction) < 0.001) drawerFraction = target;
            Segment::advance(nowMs);
        }

        void onPaint(artboard::IRenderTarget &t) const override
        {
            const AndroidColors &c = colors(mode);
            const double W = width.value(), H = height.value();

            // wallpaper: a soft diagonal gradient (no image asset yet) + legibility scrim
            t.setLinearFill(0, 0, W, H, c.primaryContainer, artboard::Color::hex(0x101018));
            t.beginPath();
            t.moveTo(0, 0); t.lineTo(W, 0); t.lineTo(W, H); t.lineTo(0, H); t.closePath();
            t.fillPath();
            artboard::drawRoundedRect(t, {0, 0, W, H}, 0.0,
                                      artboard::Paint::filled(artboard::Color{0, 0, 0, 0.12}));

            mIconRects.clear();
            const double f = drawerFraction;

            // home grid (fades/scales out as the drawer opens)
            if (f < 0.999)
            {
                drawHomeGrid(t, c, W, H, 1.0 - f);
                drawDock(t, c, W, H, 1.0 - f);
                drawPageDots(t, c, W, H, 1.0 - f);
            }
            // drawer sheet slides up
            if (f > 0.001)
                drawDrawer(t, c, W, H, f);
        }

    private:
        static uint32_t hashName(const std::string &s)
        {
            uint32_t h = 2166136261u;
            for (char ch : s) { h ^= (uint8_t)ch; h *= 16777619u; }
            return h;
        }
        static artboard::Color tonal(const std::string &name)
        {
            // map the name hash to a pleasant tonal hue (fixed S/L), as an app-icon fallback bg.
            const double hue = (hashName(name) % 360) / 360.0;
            // simple HSV->RGB at S=0.5 V=0.75
            const double s = 0.5, v = 0.78;
            const double i = hue * 6.0;
            const int ii = (int)i; const double fr = i - ii;
            const double p = v * (1 - s), q = v * (1 - s * fr), tt = v * (1 - s * (1 - fr));
            double r, g, b;
            switch (ii % 6)
            {
            case 0: r = v; g = tt; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = tt; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = tt; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
            }
            return artboard::Color{r, g, b, 1.0};
        }

        void drawAppIcon(artboard::IRenderTarget &t, const artboard::Rect &cell, const std::string &name,
                         bool label, double alpha) const
        {
            const double s = 56.0;
            const double ix = cell.x + (cell.w - s) / 2.0, iy = cell.y;
            t.pushLayer(alpha);
            artboard::Color bg = tonal(name);
            artboard::drawCircle(t, ix + s / 2, iy + s / 2, s / 2, artboard::Paint::filled(bg));
            // first letter
            std::string letter = name.empty() ? "?" : std::string(1, (char)std::toupper((unsigned char)name[0]));
            t.setFill(artboard::Color::rgba(255, 255, 255));
            const double lw = t.measureText(letter, 24.0, type::kMedium, 0.0);
            t.drawText(letter, ix + s / 2 - lw / 2, iy + s / 2 + 8, 24.0, type::kMedium, 0.0);
            mIconRects.push_back({ix, iy, s, s});
            if (label)
            {
                t.setFill(artboard::Color::rgba(255, 255, 255));
                const double tw = t.measureText(name, 12.0, type::kRegular, 0.0);
                const double clamped = tw > cell.w ? cell.w : tw;
                t.drawText(name.substr(0, 12), cell.x + (cell.w - clamped) / 2, iy + s + 16, 12.0,
                           type::kRegular, 0.0);
            }
            t.popLayer();
        }

        void drawHomeGrid(artboard::IRenderTarget &t, const AndroidColors &, double W, double H, double a) const
        {
            if (!apps) return;
            const int cols = 5;
            const double margin = 24.0, cellW = (W - 2 * margin) / cols, cellH = 96.0;
            const double top = 80.0;
            const int rows = 4;
            const int n = apps->count() < cols * rows ? apps->count() : cols * rows;
            for (int i = 0; i < n; ++i)
            {
                const int col = i % cols, row = i / cols;
                drawAppIcon(t, {margin + col * cellW, top + row * cellH, cellW, cellH},
                            apps->apps()[i].name, true, a);
            }
        }

        void drawDock(artboard::IRenderTarget &t, const AndroidColors &, double W, double H, double a) const
        {
            if (!apps) return;
            const int cols = 5;
            const double margin = 24.0, cellW = (W - 2 * margin) / cols;
            const double y = H - 120.0;
            const int base = apps->count() > 5 ? apps->count() - 5 : 0;  // last 5 as "dock"
            for (int i = 0; i < 5 && base + i < apps->count(); ++i)
                drawAppIcon(t, {margin + i * cellW, y, cellW, 70}, apps->apps()[base + i].name, false, a);
        }

        void drawPageDots(artboard::IRenderTarget &t, const AndroidColors &, double W, double H, double a) const
        {
            const double y = H - 150.0, cx = W / 2;
            t.pushLayer(a);
            artboard::drawRoundedRect(t, {cx - 12, y, 16, 4}, 2, artboard::Paint::filled(artboard::Color::rgba(255, 255, 255)));
            artboard::drawCircle(t, cx + 12, y + 2, 2, artboard::Paint::filled(artboard::Color{1, 1, 1, 0.5}));
            t.popLayer();
        }

        void drawDrawer(artboard::IRenderTarget &t, const AndroidColors &c, double W, double H, double f) const
        {
            const double top = H * (1.0 - f);  // slides up from the bottom
            const double radius = shape::kRadiusXL * (1.0 - f);  // corner flattens as it fills
            t.pushLayer(f);
            artboard::drawRoundedRect(t, {0, top, W, H - top}, radius,
                                      artboard::Paint::filled(artboard::Color{c.surface.r, c.surface.g, c.surface.b, 0.98}));
            // search field
            const double sy = top + 24;
            artboard::drawRoundedRect(t, {24, sy, W - 48, 52}, shape::radiusFull(52),
                                      artboard::Paint::filled(c.surfaceContainerHigh));
            t.setFill(c.onSurfaceVariant);
            t.drawText(searchQuery.empty() ? "Search apps" : searchQuery, 44, sy + 33, 15.0, type::kRegular, 0.0);
            // alphabetical grid
            if (apps)
            {
                const auto hits = apps->search(searchQuery);
                const int cols = 5;
                const double margin = 24.0, cellW = (W - 2 * margin) / cols, cellH = 96.0;
                const double gtop = sy + 76;
                for (size_t k = 0; k < hits.size(); ++k)
                {
                    const int col = k % cols, row = k / cols;
                    const double cy = gtop + row * cellH - scrollOffset;
                    if (cy > top - cellH && cy < H)
                        drawAppIcon(t, {margin + col * cellW, cy, cellW, cellH}, apps->apps()[hits[k]].name, true, 1.0);
                }
            }
            t.popLayer();
        }

        int iconAt(const artboard::Point &p) const
        {
            for (size_t i = 0; i < mIconRects.size(); ++i)
                if (mIconRects[i].contains(p)) return (int)i;
            return -1;
        }

        mutable std::vector<artboard::Rect> mIconRects;
    };

} // namespace androidshell
} // namespace arstro
