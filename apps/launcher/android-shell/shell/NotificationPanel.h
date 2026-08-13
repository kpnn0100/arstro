/*
 *  arstro-android-shell — NotificationPanel (M4.2): the left dual-shade surface (plan §2.4).
 *
 *  Set as the notifications-shade surface root. Renders from the NotificationStore behind a scrim,
 *  as a rounded panel that slides down with `ShellState.notificationsExpansion` (driven by the
 *  M3.3 top-edge drag). Cards show app/timestamp/title/body/actions; empty state shows a bell.
 *  Cards are drawn directly (with an internal scroll offset) for a deterministic golden; the AB-4
 *  kinetic ScrollView + swipe-dismiss physics are layered in M4.3.
 */
#pragma once
#include "artboard/artboard.h"
#include "ShellState.h"
#include "theme/AndroidColors.h"
#include "theme/Type.h"
#include "theme/Shape.h"
#include "theme/IconDrawable.h"
#include "theme/IconMask.h"
#include "theme/icons/GeneratedIcons.h"
#include "notifyd/NotificationStore.h"

#include <cmath>
#include <string>

namespace arstro
{
namespace androidshell
{
    class NotificationPanel : public artboard::Segment
    {
    public:
        NotificationStore *store = nullptr;
        ShellState *state = nullptr;
        ThemeMode mode = ThemeMode::Dark;
        std::string dateText = "Wed, Jul 29";
        double scrollOffset = 0.0;
        bool headsUp = false;        // render only the newest card, floating at top (no scrim/header)
        double swipeX = 0.0;         // horizontal dismiss offset of the front card (M4.3)

        double expansion() const
        {
            return state ? state->notificationsExpansion.get() : 1.0;
        }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override
        {
            const AndroidColors &c = colors(mode);
            const double W = width.value(), H = height.value();

            // Heads-up: a single floating card near the top, no scrim/header (plan §2.4). Slides
            // out horizontally by swipeX (swipe-to-dismiss, M4.3), fading as it goes.
            if (headsUp)
            {
                if (!store || store->count() == 0) return;
                const double cw = W - 24 < 420 ? W - 24 : 420;
                const double cx = (W - cw) / 2.0 + swipeX;
                const double alpha = 1.0 - std::fmin(std::fabs(swipeX) / (cw * 0.6), 1.0);
                if (alpha <= 0.01) return;
                t.pushLayer(alpha);
                artboard::drawElevation(t, {cx, 12, cw, 84}, shape::kRadiusXL, 8.0);
                drawCard(t, c, {cx, 12, cw, 84}, store->items().back());
                t.popLayer();
                return;
            }

            const double e = expansion();
            if (e <= 0.001) return;

            // scrim
            artboard::drawRoundedRect(t, {0, 0, W, H}, 0.0,
                                      artboard::Paint::filled(artboard::Color{0, 0, 0, 0.32 * e}));

            // panel geometry: width 420 (or W-24), left margin 12, slides from above.
            const double margin = 12.0;
            const double pw = W - 2 * margin < 420.0 ? W - 2 * margin : 420.0;
            const double px = margin;
            const int n = store ? store->count() : 0;
            const double contentH = 64.0 /*header*/ + (n == 0 ? 120.0 : n * 108.0) + 24.0;
            const double ph = contentH < H - 2 * margin ? contentH : H - 2 * margin;
            const double py = margin - (1.0 - e) * (ph + margin);  // slides down into place

            artboard::drawElevation(t, {px, py, pw, ph}, shape::kRadiusXL, 6.0);
            artboard::drawRoundedRect(t, {px, py, pw, ph}, shape::kRadiusXL,
                                      artboard::Paint::filled(c.surfaceContainerLow));

            // clip content to the panel
            t.save();
            artboard::drawRoundedRect(t, {px, py, pw, ph}, shape::kRadiusXL, artboard::Paint{});
            t.clipPath();

            // header: date + Clear-all pill (when dismissible)
            double y = py + 16;
            t.setFill(c.onSurface);
            t.drawText(dateText, px + 16, y + 20, 14.0, type::kMedium, 0.0);
            if (store && store->dismissibleCount() > 0)
            {
                const double pillW = 84, pillH = 32, pillX = px + pw - 16 - pillW;
                artboard::drawRoundedRect(t, {pillX, y + 4, pillW, pillH}, shape::radiusFull(pillH),
                                          artboard::Paint::filled(c.secondaryContainer));
                t.setFill(c.onSecondaryContainer);
                t.drawText("Clear all", pillX + 14, y + 4 + 21, 13.0, type::kMedium, 0.0);
            }
            y += 48;

            // cards or empty state
            if (n == 0)
            {
                const double cxp = px + pw / 2.0;
                emitIconPath(t, icons::kBell, {cxp - 20, y + 20, 40, 40});
                t.setStroke(c.onSurfaceVariant, 2.0);
                t.strokePath();
                t.setFill(c.onSurfaceVariant);
                const char *msg = "No notifications";
                const double tw = t.measureText(msg, 14.0, type::kRegular, 0.0);
                t.drawText(msg, cxp - tw / 2.0, y + 84, 14.0, type::kRegular, 0.0);
            }
            else
            {
                double cy = y - scrollOffset;
                for (const Notification &nf : store->items())
                {
                    drawCard(t, c, {px + 8, cy, pw - 16, 96}, nf);
                    cy += 108;
                }
            }
            t.restore();
        }

    private:
        void drawCard(artboard::IRenderTarget &t, const AndroidColors &c, const artboard::Rect &r,
                      const Notification &nf) const
        {
            artboard::drawRoundedRect(t, r, shape::kRadiusXL,
                                      artboard::Paint::filled(c.surfaceContainerHigh));
            // app icon chip
            fillMask(t, {r.x + 14, r.y + 14, 24, 24}, MaskShape::Circle, c.primaryContainer);
            // app name + timestamp
            t.setFill(c.onSurfaceVariant);
            t.drawText(nf.appName.empty() ? "App" : nf.appName, r.x + 48, r.y + 26, 12.0, type::kMedium, 0.0);
            t.drawText("now", r.x + r.w - 44, r.y + 26, 12.0, type::kRegular, 0.0);
            // title + body
            t.setFill(c.onSurface);
            t.drawText(nf.summary, r.x + 16, r.y + 52, 14.0, type::kMedium, 0.0);
            t.setFill(c.onSurfaceVariant);
            t.drawText(nf.body, r.x + 16, r.y + 74, 14.0, type::kRegular, 0.0);
            // urgency accent bar for critical
            if (nf.urgency >= 2)
                artboard::drawRoundedRect(t, {r.x, r.y + 12, 4, r.h - 24}, 2.0,
                                          artboard::Paint::filled(c.error));
            // progress bar
            if (nf.hasProgress)
            {
                const double bw = r.w - 32;
                artboard::drawRoundedRect(t, {r.x + 16, r.y + r.h - 14, bw, 4}, 2.0,
                                          artboard::Paint::filled(c.surfaceContainerHighest));
                artboard::drawRoundedRect(t, {r.x + 16, r.y + r.h - 14, bw * nf.progress / 100.0, 4},
                                          2.0, artboard::Paint::filled(c.primary));
            }
        }
    };

} // namespace androidshell
} // namespace arstro
