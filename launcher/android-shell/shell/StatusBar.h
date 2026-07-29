/*
 *  arstro-android-shell — StatusBar (M3.2): the top status-bar surface (plan §2.3).
 *
 *  Draws from a plain `StatusBarData` snapshot (clock string + service readings + tint), so the
 *  surface is deterministic for golden tests: the shell fills the snapshot from SystemServices on
 *  a timer (runtime), or a test fills it with fixed values. Height 24, padding 16.
 *    left  : clock (Roboto Medium 14) + notification dots (stub)
 *    right : wifi fan · battery glyph + percent · bt/dnd/airplane conditional icons (right-aligned)
 *  Battery + wifi are parametric so they are drawn programmatically; the conditional icons come
 *  from the M2.3 codegen. `darkIcons` picks onSurface (dark) vs white glyphs (over wallpaper).
 */
#pragma once
#include "artboard/artboard.h"
#include "ShellState.h"
#include "theme/ThemeMode.h"
#include "theme/AndroidColors.h"
#include "theme/Type.h"
#include "theme/IconDrawable.h"
#include "theme/icons/GeneratedIcons.h"

#include <cmath>
#include <string>

namespace arstro
{
namespace androidshell
{
    struct StatusBarData
    {
        std::string clock = "12:30";  // preformatted (locale 12/24h done by the caller)
        int wifiBars = 3;             // 0..4; -1 = wifi disabled (draw dimmed, no signal)
        int batteryPercent = 72;
        bool charging = false;
        bool bluetooth = false;       // connected
        bool dnd = false;
        bool airplane = false;
        int notifCount = 2;           // notification dots (stub)
        bool darkIcons = false;       // dark glyphs on a light bar, else white over wallpaper
    };

    class StatusBar : public artboard::Segment
    {
    public:
        StatusBarData data;
        ThemeMode mode = ThemeMode::Dark;
        bool scrim = true;  // subtle top scrim so white glyphs read over a bright wallpaper

        // Dual-shade trigger (M3.3): a top-edge drag drives one shade's expansion Observable.
        // Left half -> notifications, right half -> quick settings (Android dual shade). The panels
        // themselves are M4/M5; here we only move the fraction the bands own.
        void setState(ShellState *s) { mState = s; }
        static constexpr double kShadeDragRef = 600.0;  // drag px for a fully-open shade

    protected:
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &) override
        {
            using T = artboard::Gesture::Type;
            if (!mState) return false;
            if (g.type == T::DragStart)
            {
                mDragging = true;
                mLeftHalf = g.start.x < width.value() * 0.5;
                return true;
            }
            if (g.type == T::Drag && mDragging)
            {
                const double dy = g.pos.y - g.start.y;  // downward pull
                double frac = dy / kShadeDragRef;
                frac = frac < 0.0 ? 0.0 : (frac > 1.0 ? 1.0 : frac);
                (mLeftHalf ? mState->notificationsExpansion : mState->quickSettingsExpansion).set(frac);
                return true;
            }
            if (g.type == T::Up || g.type == T::Drop)
            {
                mDragging = false;
                return true;
            }
            return false;
        }

        void onPaint(artboard::IRenderTarget &t) const override
        {
            const AndroidColors &c = colors(mode);
            const double W = width.value(), H = height.value();
            const artboard::Color fg = data.darkIcons ? c.onSurface : artboard::Color::rgba(255, 255, 255);
            const artboard::Color dim{fg.r, fg.g, fg.b, 0.30};

            // Over-wallpaper scrim (white-glyph mode): a faint dark wash so glyphs stay legible.
            if (!data.darkIcons && scrim)
                artboard::drawRoundedRect(t, {0, 0, W, H}, 0.0,
                                          artboard::Paint::filled(artboard::Color::rgba(0, 0, 0, 46)));

            // ---- left: clock + notification dots ----
            const double baseline = H * 0.5 + 5.0;
            t.setFill(fg);
            t.drawText(data.clock, 16.0, baseline, 14.0, type::kMedium, 0.0);
            const double clockW = t.measureText(data.clock, 14.0, type::kMedium, 0.0);
            double dotX = 16.0 + clockW + 12.0;
            for (int i = 0; i < data.notifCount && i < 4; ++i)
            {
                artboard::drawCircle(t, dotX, H * 0.5, 2.0, artboard::Paint::filled(fg));
                dotX += 8.0;
            }

            // ---- right cluster, laid out right-to-left ----
            double rx = W - 16.0;
            auto icon = [&](const icons::IconPath &g) {
                const double s = 17.0;
                rx -= s;
                emitIconPath(t, g, {rx, (H - s) / 2.0, s, s});
                if (g.stroke) { t.setStroke(fg, 1.8); t.strokePath(); }
                else { t.setFill(fg); t.fillPath(); }
                rx -= 6.0;
            };
            if (data.airplane) icon(icons::kAirplane);
            if (data.dnd) icon(icons::kDndMoon);
            if (data.bluetooth) icon(icons::kBluetooth);

            rx = drawBattery(t, rx, H, fg);
            rx -= 6.0;
            rx = drawWifi(t, rx, H, fg, dim);
        }

    private:
        // Battery: outline body + top cap; level fill rising from the bottom, or a bolt when charging.
        double drawBattery(artboard::IRenderTarget &t, double rx, double H, const artboard::Color &fg) const
        {
            const double bw = 9.0, cap = 2.0, bh = 14.0;
            const double left = rx - bw;
            const double top = (H - (bh + cap)) / 2.0;  // top of cap
            // cap
            artboard::drawRoundedRect(t, {left + (bw - 4.0) / 2.0, top, 4.0, cap}, 1.0,
                                      artboard::Paint::filled(fg));
            const artboard::Rect body{left, top + cap, bw, bh};
            artboard::drawRoundedRect(t, body, 2.5, artboard::Paint::stroked(fg, 1.2));
            const double inset = 1.7;
            const double innerH = bh - 2 * inset;
            if (data.charging)
            {
                // a small lightning bolt centred in the body
                const double cx = body.x + bw / 2.0, cy = body.y + bh / 2.0;
                t.beginPath();
                t.moveTo(cx + 1.6, cy - 4.5);
                t.lineTo(cx - 2.2, cy + 0.6);
                t.lineTo(cx - 0.2, cy + 0.6);
                t.lineTo(cx - 1.6, cy + 4.5);
                t.lineTo(cx + 2.2, cy - 0.6);
                t.lineTo(cx + 0.2, cy - 0.6);
                t.closePath();
                t.setFill(fg);
                t.fillPath();
            }
            else
            {
                const double frac = std::fmin(std::fmax(data.batteryPercent / 100.0, 0.0), 1.0);
                const double fh = innerH * frac;
                if (fh > 0.5)
                    artboard::drawRoundedRect(t, {body.x + inset, body.y + inset + (innerH - fh),
                                                  bw - 2 * inset, fh},
                                              1.0, artboard::Paint::filled(fg));
            }
            // percent text to the left of the glyph
            char buf[8];
            std::snprintf(buf, sizeof buf, "%d%%", data.batteryPercent);
            const double tw = t.measureText(buf, 12.0, type::kMedium, 0.0);
            t.setFill(fg);
            t.drawText(buf, left - 4.0 - tw, H * 0.5 + 4.0, 12.0, type::kMedium, 0.0);
            return left - 4.0 - tw;
        }

        // Wifi fan: apex dot + three arcs over the top; `bars` (0..4) lit, the rest dimmed.
        // bars < 0 = disabled -> everything dimmed.
        double drawWifi(artboard::IRenderTarget &t, double rx, double H, const artboard::Color &fg,
                        const artboard::Color &dim) const
        {
            const double w = 17.0;
            const double cx = rx - w / 2.0;
            const double cy = H / 2.0 + 5.0;  // apex near the baseline
            const int bars = data.wifiBars;
            auto lit = [&](int need) { return bars >= need ? fg : dim; };

            artboard::drawCircle(t, cx, cy - 1.0, 1.6, artboard::Paint::filled(lit(1)));
            const double radii[] = {4.0, 7.0, 10.0};
            for (int k = 0; k < 3; ++k)
                strokeArc(t, cx, cy - 1.0, radii[k], 45.0, 135.0, lit(k + 2), 1.7);
            return rx - w;
        }

        // Stroke a circular arc from deg0..deg1 (measured up-positive), as a short polyline.
        static void strokeArc(artboard::IRenderTarget &t, double cx, double cy, double r,
                              double deg0, double deg1, const artboard::Color &col, double width)
        {
            const int seg = 10;
            t.beginPath();
            for (int i = 0; i <= seg; ++i)
            {
                const double th = (deg0 + (deg1 - deg0) * i / seg) * 3.14159265358979 / 180.0;
                const double x = cx + r * std::cos(th);
                const double y = cy - r * std::sin(th);
                if (i == 0) t.moveTo(x, y);
                else t.lineTo(x, y);
            }
            t.setStroke(col, width);
            t.strokePath();
        }

        ShellState *mState = nullptr;
        bool mDragging = false;
        bool mLeftHalf = true;
    };

} // namespace androidshell
} // namespace arstro
