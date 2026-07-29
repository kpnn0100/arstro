/*
 *  arstro-android-shell — IconMask (M2.4): adaptive-icon masking + themed icons.
 *
 *  Android app icons are clipped to a shared mask — a circle (Pixel default) or the squircle
 *  (`config_icon_mask`, plan §2.2). `drawMaskedImage` builds the mask as a path, `clipPath()`s it
 *  (AB-1), and blits a registered icon image (AB-19) inside it. `drawThemedIcon` renders an M3
 *  monochrome/"themed" icon: a filled mask-shape background + a tinted glyph on top.
 *
 *  The squircle path table (`icons::kSquircle`) is generated from assets/icons-src/squircle.svg by
 *  the M2.3 codegen (arcs flattened to cubics) — the mask is just another icon path we clip with.
 */
#pragma once
#include "artboard/artboard.h"
#include "theme/IconDrawable.h"
#include "theme/icons/GeneratedIcons.h"

namespace arstro
{
namespace androidshell
{
    enum class MaskShape { Circle, Squircle };

    // Emit the mask outline as the current path (no fill/stroke), scaled into rect, in the current
    // transform. Caller then clipPath()s or fills it.
    inline void emitMaskPath(artboard::IRenderTarget &t, const artboard::Rect &rect, MaskShape shape)
    {
        if (shape == MaskShape::Squircle)
        {
            emitIconPath(t, icons::kSquircle, rect);  // 100x100 squircle, scaled to rect
            return;
        }
        // Circle inscribed in rect (four cubic-bezier quarter arcs).
        const double cx = rect.x + rect.w / 2.0;
        const double cy = rect.y + rect.h / 2.0;
        const double r = (rect.w < rect.h ? rect.w : rect.h) / 2.0;
        const double k = 0.5522847498307936 * r;  // bezier circle constant
        t.beginPath();
        t.moveTo(cx - r, cy);
        t.cubicTo(cx - r, cy - k, cx - k, cy - r, cx, cy - r);
        t.cubicTo(cx + k, cy - r, cx + r, cy - k, cx + r, cy);
        t.cubicTo(cx + r, cy + k, cx + k, cy + r, cx, cy + r);
        t.cubicTo(cx - k, cy + r, cx - r, cy + k, cx - r, cy);
        t.closePath();
    }

    // Clip a registered image (id from registerImage) to the mask and draw it into rect (AB-1 clip
    // + AB-19 image). Scoped by save()/restore() so the clip does not leak.
    inline void drawMaskedImage(artboard::IRenderTarget &t, int imageId, const artboard::Rect &rect,
                                MaskShape shape)
    {
        t.save();
        emitMaskPath(t, rect, shape);
        t.clipPath();
        t.drawImage(imageId, rect);
        t.restore();
    }

    // Fill the mask shape with a solid colour (a themed-icon background, or a fallback icon tile).
    inline void fillMask(artboard::IRenderTarget &t, const artboard::Rect &rect, MaskShape shape,
                         const artboard::Color &color)
    {
        emitMaskPath(t, rect, shape);
        t.setFill(color);
        t.fillPath();
    }

    // An M3 themed (monochrome) icon: a filled mask-shape background in `bg`, with `glyph` tinted
    // `fg` and inset by `glyphInset` on each side. (M3 convention: bg = primaryContainer, fg =
    // onPrimaryContainer.) Emits only HAL primitives; honours the current transform.
    inline void drawThemedIcon(artboard::IRenderTarget &t, const icons::IconPath &glyph,
                               const artboard::Rect &rect, MaskShape shape,
                               const artboard::Color &bg, const artboard::Color &fg,
                               double glyphInset)
    {
        fillMask(t, rect, shape, bg);
        const artboard::Rect g{rect.x + glyphInset, rect.y + glyphInset,
                               rect.w - 2 * glyphInset, rect.h - 2 * glyphInset};
        emitIconPath(t, glyph, g);
        if (glyph.stroke)
        {
            t.setStroke(fg, 2.0);
            t.strokePath();
        }
        else
        {
            t.setFill(fg);
            t.fillPath();
        }
    }

} // namespace androidshell
} // namespace arstro
