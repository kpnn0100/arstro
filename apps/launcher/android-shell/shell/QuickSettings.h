/*
 *  arstro-android-shell — QuickSettings (M5): the right dual-shade surface (plan §2.5).
 *
 *  Right-anchored panel driven by `ShellState.quickSettingsExpansion` (M3.3). Header (big clock +
 *  battery + gear + power), brightness + volume sliders, and a 4-column tile grid whose states
 *  come from SystemServices. Tiles render active (primaryContainer) / inactive
 *  (surfaceContainerHighest) / unavailable (38% opacity). A tile toggle animates its corner radius
 *  from 16 → full-pill (the M3-Expressive shape morph); here the radius is derived from live state
 *  so a golden captures both looks.
 */
#pragma once
#include "artboard/artboard.h"
#include "ShellState.h"
#include "theme/AndroidColors.h"
#include "theme/Type.h"
#include "theme/Shape.h"
#include "theme/IconDrawable.h"
#include "theme/icons/GeneratedIcons.h"
#include "system/SystemServices.h"

#include <string>
#include <vector>

namespace arstro
{
namespace androidshell
{
    class QuickSettings : public artboard::Segment
    {
    public:
        ShellState *state = nullptr;
        SystemServices *services = nullptr;
        ThemeMode mode = ThemeMode::Dark;
        std::string clockText = "12:30";

        double expansion() const { return state ? state->quickSettingsExpansion.get() : 1.0; }

    protected:
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override
        {
            using T = artboard::Gesture::Type;
            if (!services) return false;
            if (g.type == T::Click)
            {
                const int idx = tileAt(local);
                if (idx >= 0) { toggleTile(idx); return true; }
            }
            return false;
        }

        void onPaint(artboard::IRenderTarget &t) const override
        {
            const AndroidColors &c = colors(mode);
            const double W = width.value(), H = height.value();
            const double e = expansion();
            if (e <= 0.001) return;

            artboard::drawRoundedRect(t, {0, 0, W, H}, 0.0,
                                      artboard::Paint::filled(artboard::Color{0, 0, 0, 0.32 * e}));

            const double margin = 12.0;
            const double pw = W - 2 * margin < 420.0 ? W - 2 * margin : 420.0;
            const double px = W - margin - pw;  // right-anchored
            const double ph = H - 2 * margin < 560.0 ? H - 2 * margin : 560.0;
            const double py = margin - (1.0 - e) * (ph + margin);

            artboard::drawElevation(t, {px, py, pw, ph}, shape::kRadiusXL, 6.0);
            artboard::drawRoundedRect(t, {px, py, pw, ph}, shape::kRadiusXL,
                                      artboard::Paint::filled(c.surfaceContainerLow));
            t.save();
            artboard::drawRoundedRect(t, {px, py, pw, ph}, shape::kRadiusXL, artboard::Paint{});
            t.clipPath();

            double y = py + 20;
            // header: big clock + gear + power
            t.setFill(c.onSurface);
            t.drawText(clockText, px + 20, y + 24, 28.0, type::kRegular, 0.0);
            drawHeaderIcon(t, c, icons::kGear, px + pw - 92, y);
            drawHeaderIcon(t, c, icons::kPower, px + pw - 52, y);
            y += 56;

            // sliders
            y = drawSlider(t, c, icons::kBrightness, px + 16, y, pw - 32,
                           services ? services->brightness() : 0.6);
            y = drawSlider(t, c, icons::kBrightness, px + 16, y, pw - 32,
                           services ? services->volume() : 0.4);
            y += 8;

            // tile grid (4 cols)
            const double gap = 8.0;
            const double tileW = (pw - 32 - gap * 3) / 4.0;
            const double tileH = 64.0;
            const auto tiles = tileModels();
            mTileRects.clear();
            for (size_t i = 0; i < tiles.size(); ++i)
            {
                const double col = i % 4, row = i / 4;
                const artboard::Rect r{px + 16 + col * (tileW + gap), y + row * (tileH + gap), tileW, tileH};
                mTileRects.push_back(r);
                drawTile(t, c, r, tiles[i]);
            }
            t.restore();
        }

    private:
        struct Tile { const icons::IconPath *icon; std::string label; bool active; bool available; };

        std::vector<Tile> tileModels() const
        {
            const bool wifi = services && services->wifi().enabled;
            const bool bt = services && services->bluetooth().powered;
            const bool dnd = services && services->doNotDisturb();
            const bool air = services && services->airplaneMode();
            const bool dark = services ? services->darkTheme() : true;
            const bool night = services && services->nightLight();
            return {
                {&icons::kWifi, "Wi‑Fi", wifi, true},
                {&icons::kBluetooth, "Bluetooth", bt, true},
                {&icons::kDndMoon, "Do Not Disturb", dnd, true},
                {&icons::kAirplane, "Airplane", air, true},
                {&icons::kDarktheme, "Dark", dark, true},
                {&icons::kDndMoon, "Night Light", night, true},
                {&icons::kRotate, "Auto‑rotate", false, false},
                {&icons::kScreenshot, "Screenshot", false, true},
            };
        }

        void toggleTile(int idx) const
        {
            if (!services) return;
            switch (idx)
            {
            case 0: services->setWifiEnabled(!services->wifi().enabled); break;
            case 1: services->setBluetoothPowered(!services->bluetooth().powered); break;
            case 2: services->setDoNotDisturb(!services->doNotDisturb()); break;
            case 3: services->setAirplaneMode(!services->airplaneMode()); break;
            case 4: services->setDarkTheme(!services->darkTheme()); break;
            case 5: services->setNightLight(!services->nightLight()); break;
            case 7: services->takeScreenshot(); break;
            default: break;  // 6 auto-rotate unavailable on desktop
            }
        }

        int tileAt(const artboard::Point &p) const
        {
            for (size_t i = 0; i < mTileRects.size(); ++i)
                if (mTileRects[i].contains(p)) return (int)i;
            return -1;
        }

        void drawHeaderIcon(artboard::IRenderTarget &t, const AndroidColors &c,
                            const icons::IconPath &g, double x, double y) const
        {
            artboard::drawCircle(t, x + 16, y + 16, 18, artboard::Paint::filled(c.surfaceContainerHigh));
            emitIconPath(t, g, {x + 6, y + 6, 20, 20});
            t.setStroke(c.onSurface, 1.8); t.strokePath();
        }

        double drawSlider(artboard::IRenderTarget &t, const AndroidColors &c, const icons::IconPath &g,
                          double x, double y, double w, double v) const
        {
            const double h = 44.0;
            artboard::drawRoundedRect(t, {x, y, w, h}, shape::radiusFull(h),
                                      artboard::Paint::filled(c.surfaceContainerHighest));
            const double fillW = (w) * (v < 0 ? 0 : (v > 1 ? 1 : v));
            if (fillW > h)
                artboard::drawRoundedRect(t, {x, y, fillW, h}, shape::radiusFull(h),
                                          artboard::Paint::filled(c.primary));
            emitIconPath(t, g, {x + 12, y + 12, 20, 20});
            t.setStroke(c.onPrimary, 1.8); t.strokePath();
            return y + h + 8;
        }

        void drawTile(artboard::IRenderTarget &t, const AndroidColors &c, const artboard::Rect &r,
                      const Tile &tile) const
        {
            const double alpha = tile.available ? 1.0 : 0.38;
            const artboard::Color bg = tile.active ? c.primaryContainer : c.surfaceContainerHighest;
            const artboard::Color fg = tile.active ? c.onPrimaryContainer : c.onSurface;
            // shape morph: active tiles are full-pill, inactive are radius 16.
            const double radius = tile.active ? shape::radiusFull(r.h) : shape::kRadiusLG;
            t.pushLayer(alpha);
            artboard::drawRoundedRect(t, r, radius, artboard::Paint::filled(bg));
            emitIconPath(t, *tile.icon, {r.x + 12, r.y + 12, 22, 22});
            if (tile.icon->stroke) { t.setStroke(fg, 1.8); t.strokePath(); }
            else { t.setFill(fg); t.fillPath(); }
            t.setFill(fg);
            t.drawText(tile.label, r.x + 10, r.y + r.h - 10, 11.0, type::kMedium, 0.0);
            t.popLayer();
        }

        mutable std::vector<artboard::Rect> mTileRects;
    };

} // namespace androidshell
} // namespace arstro
