/*
 *  arstro-android-shell — SampleSheet (M2.5): the android_theme token reference screen.
 *
 *  Lays out the whole design system on one surface — colour role swatches, the type ramp, the
 *  radius scale, the icon set, and the adaptive-icon masks (circle/squircle + a themed icon) — so
 *  it can be rendered to a golden PNG (light + dark) and eyeballed. It reads ONLY the theme tokens
 *  (AndroidColors / Type / Shape / IconMask), so it doubles as a living check that they compose.
 *
 *  Uses the free `emitIconPath` (not IconDrawable::render) so every glyph stays in the current
 *  transform rather than resetting to an absolute one.
 */
#pragma once
#include "artboard/artboard.h"
#include "theme/ThemeMode.h"
#include "theme/AndroidColors.h"
#include "theme/Type.h"
#include "theme/Shape.h"
#include "theme/IconDrawable.h"
#include "theme/IconMask.h"
#include "theme/icons/GeneratedIcons.h"

namespace arstro
{
namespace androidshell
{
    class SampleSheet : public artboard::Segment
    {
    public:
        ThemeMode mode = ThemeMode::Dark;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override
        {
            const AndroidColors &c = colors(mode);
            const double W = width.value();
            const double H = height.value();

            // page
            artboard::drawRoundedRect(t, {0, 0, W, H}, 0.0, artboard::Paint::filled(c.surface));

            auto text = [&](const char *s, double x, double y, double size,
                            const char *fam, const artboard::Color &col) {
                t.setFill(col);
                t.drawText(s, x, y, size, fam, 0.0);
            };

            double y = 44;
            text("android_theme", 32, y, 28, type::kMedium, c.onSurface);
            y += 22;
            text(mode == ThemeMode::Dark ? "dark scheme" : "light scheme", 32, y, 14,
                 type::kRegular, c.onSurfaceVariant);
            y += 26;

            // ---- colour role swatches ----
            struct Role { const char *n; artboard::Color col, on; };
            const Role roles[] = {
                {"primary", c.primary, c.onPrimary},
                {"primaryContainer", c.primaryContainer, c.onPrimaryContainer},
                {"secondaryContainer", c.secondaryContainer, c.onSecondaryContainer},
                {"surfaceContainer", c.surfaceContainer, c.onSurface},
                {"surfaceContainerHigh", c.surfaceContainerHigh, c.onSurface},
                {"error", c.error, c.onPrimary},
                {"outline", c.outline, c.surface},
                {"onSurfaceVariant", c.onSurfaceVariant, c.surface},
            };
            double x = 32;
            for (const Role &r : roles)
            {
                artboard::drawRoundedRect(t, {x, y, 112, 56}, shape::kRadiusMD, artboard::Paint::filled(r.col));
                text(r.n, x + 8, y + 33, 10, type::kRegular, r.on);
                x += 120;
                if (x > W - 120) { x = 32; y += 68; }
            }
            y += 88;

            // ---- type ramp ----
            text("Type ramp", 32, y, 14, type::kMedium, c.onSurfaceVariant);
            y += 26;
            struct Ramp { const char *n; artboard::TextStyle s; };
            const Ramp ramp[] = {
                {"Display 36", type::displaySmall}, {"Headline 24", type::headlineSmall},
                {"Title 22", type::titleLarge}, {"Body 14", type::bodyMedium}, {"Label 11", type::labelSmall}};
            for (const Ramp &r : ramp)
            {
                y += r.s.sizePx;
                text(r.n, 32, y, r.s.sizePx, r.s.fontFamily.c_str(), c.onSurface);
                y += 12;
            }
            y += 16;

            // ---- radius scale ----
            text("Radius scale (4/8/12/16/28)", 32, y, 14, type::kMedium, c.onSurfaceVariant);
            y += 26;
            const double radii[] = {shape::kRadiusXS, shape::kRadiusSM, shape::kRadiusMD,
                                    shape::kRadiusLG, shape::kRadiusXL};
            x = 32;
            for (double r : radii)
            {
                artboard::drawRoundedRect(t, {x, y, 64, 64}, r,
                                          artboard::Paint::filled(c.surfaceContainerHigh));
                x += 76;
            }
            y += 88;

            // ---- icons ----
            text("Icons", 32, y, 14, type::kMedium, c.onSurfaceVariant);
            y += 26;
            const icons::IconPath *glyphs[] = {&icons::kCheck, &icons::kClose, &icons::kChevronRight,
                                               &icons::kAdd, &icons::kBackArrow, &icons::kDot};
            x = 32;
            for (const icons::IconPath *g : glyphs)
            {
                emitIconPath(t, *g, {x, y, 32, 32});
                if (g->stroke) { t.setStroke(c.onSurface, 2.0); t.strokePath(); }
                else { t.setFill(c.onSurface); t.fillPath(); }
                x += 44;
            }
            y += 52;

            // ---- adaptive-icon masks + themed icon ----
            text("Masks: circle / squircle / themed", 32, y, 14, type::kMedium, c.onSurfaceVariant);
            y += 26;
            fillMask(t, {32, y, 48, 48}, MaskShape::Circle, c.primaryContainer);
            fillMask(t, {96, y, 48, 48}, MaskShape::Squircle, c.primaryContainer);
            drawThemedIcon(t, icons::kCheck, {160, y, 48, 48}, MaskShape::Circle,
                           c.primaryContainer, c.onPrimaryContainer, 12.0);
            drawThemedIcon(t, icons::kAdd, {224, y, 48, 48}, MaskShape::Squircle,
                           c.primaryContainer, c.onPrimaryContainer, 12.0);
        }
    };

} // namespace androidshell
} // namespace arstro
