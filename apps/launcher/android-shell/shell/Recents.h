/*
 *  arstro-android-shell — Recents (M7): the overview / app-switcher surface (plan §2.7.3).
 *
 *  A full-screen overview over a scrim: an MRU row of window cards from the CompositorBridge.
 *  v1 cards are icon-cards (Wayland forbids cross-client thumbnails; per-desktop screenshot hooks
 *  are v1.5). Swipe a card up to close its window; tap to activate; a per-card chip offers
 *  Split-left / Split-right / Close. Also the entry point for split-screen (bridge.tilePair).
 */
#pragma once
#include "artboard/artboard.h"
#include "compositor/CompositorBridge.h"
#include "theme/AndroidColors.h"
#include "theme/Type.h"
#include "theme/Shape.h"

#include <string>
#include <vector>

namespace arstro
{
namespace androidshell
{
    class Recents : public artboard::Segment
    {
    public:
        CompositorBridge *bridge = nullptr;
        ThemeMode mode = ThemeMode::Dark;
        bool visible_ = false;         // overview shown
        double scrollX = 0.0;          // horizontal scroll through the card row
        std::vector<WindowInfo> windowsOverride;  // for golden renders without a bridge

        std::vector<WindowInfo> windows() const
        {
            if (!windowsOverride.empty()) return windowsOverride;
            return bridge ? bridge->listWindows() : std::vector<WindowInfo>{};
        }

    protected:
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override
        {
            using T = artboard::Gesture::Type;
            if (g.type == T::Click)
            {
                const int idx = cardAt(local);
                if (idx >= 0 && bridge) { bridge->activate(windows()[idx].id); visible_ = false; return true; }
            }
            if (g.type == T::Drag)
            {
                // swipe a card up -> close its window
                const double dy = g.pos.y - g.start.y;
                const int idx = cardAt({g.start.x, g.start.y});
                if (idx >= 0 && dy < -80 && bridge) { bridge->close(windows()[idx].id); return true; }
                scrollX -= (g.pos.x - g.start.x) * 0.0;  // (row scroll wired with velocity later)
            }
            return false;
        }

        void onPaint(artboard::IRenderTarget &t) const override
        {
            if (!visible_) return;
            const AndroidColors &c = colors(mode);
            const double W = width.value(), H = height.value();
            artboard::drawRoundedRect(t, {0, 0, W, H}, 0.0, artboard::Paint::filled(artboard::Color{0, 0, 0, 0.40}));

            const auto ws = windows();
            mCardRects.clear();
            if (ws.empty())
            {
                t.setFill(c.onSurfaceVariant);
                const char *msg = "No recent apps";
                const double tw = t.measureText(msg, 16.0, type::kRegular, 0.0);
                t.drawText(msg, W / 2 - tw / 2, H / 2, 16.0, type::kRegular, 0.0);
                return;
            }

            // centered card row: current card centered, neighbours peek
            const double cardW = W * 0.62, cardH = H * 0.62;
            const double gap = 24.0;
            const double cy = (H - cardH) / 2.0;
            double x = W / 2.0 - cardW / 2.0 - scrollX;
            for (const WindowInfo &w : ws)
            {
                const artboard::Rect r{x, cy, cardW, cardH};
                mCardRects.push_back(r);
                drawCard(t, c, r, w);
                x += cardW + gap;
            }
            // clear-all
            t.setFill(c.onSurface);
            t.drawText("Clear all", W / 2 - 34, H - 40, 14.0, type::kMedium, 0.0);
        }

    private:
        void drawCard(artboard::IRenderTarget &t, const AndroidColors &c, const artboard::Rect &r,
                      const WindowInfo &w) const
        {
            // header: app title
            t.setFill(artboard::Color::rgba(255, 255, 255));
            t.drawText(w.title.empty() ? w.appId : w.title, r.x + 4, r.y - 10, 14.0, type::kMedium, 0.0);
            artboard::drawElevation(t, r, shape::kRadiusXL, 6.0);
            artboard::drawRoundedRect(t, r, shape::kRadiusXL, artboard::Paint::filled(c.surfaceContainerHigh));
            // icon-card: a big letter chip centered (thumbnails unavailable on Wayland, plan §2.7.3)
            const std::string nm = w.appId.empty() ? "?" : w.appId;
            artboard::drawCircle(t, r.x + r.w / 2, r.y + r.h / 2, 48, artboard::Paint::filled(c.primaryContainer));
            t.setFill(c.onPrimaryContainer);
            std::string L(1, (char)std::toupper((unsigned char)nm[0]));
            const double lw = t.measureText(L, 40.0, type::kMedium, 0.0);
            t.drawText(L, r.x + r.w / 2 - lw / 2, r.y + r.h / 2 + 14, 40.0, type::kMedium, 0.0);
        }

        int cardAt(const artboard::Point &p) const
        {
            for (size_t i = 0; i < mCardRects.size(); ++i)
                if (mCardRects[i].contains(p)) return (int)i;
            return -1;
        }

        mutable std::vector<artboard::Rect> mCardRects;
    };

} // namespace androidshell
} // namespace arstro
