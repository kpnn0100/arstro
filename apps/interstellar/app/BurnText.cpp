#include "BurnText.h"
#include "Theme.h"
#include "../../cosmo/EmbeddedFonts.h"
#include "adapter/native/CairoTarget.h"
#include <algorithm>
#include <cairo.h>
#include <cmath>
#include <mutex>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    bool drawOverlayText(interstellar::Raster &frame, const std::vector<interstellar::OverlayText> &items)
    {
        static std::once_flag fonts;
        std::call_once(fonts, [] { cosmo_v2::registerEmbeddedFonts(); });
        if (frame.empty()) return false;
        for (const auto &it : items)
        {
            if (it.text.empty()) continue;
            const char *family = it.mono ? font::mono() : font::sans();
            const double pad = std::round(it.px * 0.45);
            // measure on a scratch surface (a real metric, design rule R5)
            double tw = 0;
            {
                cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 4, 4);
                cairo_t *cr = cairo_create(s);
                CairoTarget t(cr);
                tw = t.measureText(it.text, it.px, family);
                cairo_destroy(cr);
                cairo_surface_destroy(s);
            }
            const int bw = std::max(1, (int)std::ceil(tw + 2 * pad)), bh = std::max(1, (int)std::ceil(it.px * 1.5));
            cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bw, bh);
            cairo_t *cr = cairo_create(s);
            {
                CairoTarget t(cr);
                if (it.box) drawRoundedRect(t, Rect{0, 0, (double)bw, (double)bh}, std::min(4.0, bh * 0.2), Paint::filled(Color(0, 0, 0, 0.62)));
                t.setFill(Color(1, 1, 1, 1));
                t.drawText(it.text, pad, bh * 0.5 + it.px * 0.35, it.px, family);
            }
            cairo_surface_flush(s);
            // where the plate goes: its anchor, clamped inside the frame
            int x0 = (int)std::lround(it.align == 0 ? it.x : it.align == 1 ? it.x - bw * 0.5 : it.x - bw);
            int y0 = (int)std::lround(it.valign == 0 ? it.y : it.y - bh);
            x0 = std::clamp(x0, 0, std::max(0, frame.width - bw));
            y0 = std::clamp(y0, 0, std::max(0, frame.height - bh));
            const unsigned char *px = cairo_image_surface_get_data(s);
            const int stride = cairo_image_surface_get_stride(s);
            for (int y = 0; y < bh && y0 + y < frame.height; ++y)
                for (int x = 0; x < bw && x0 + x < frame.width; ++x)
                {
                    // Cairo's ARGB32 is premultiplied, B G R A in memory on little-endian machines
                    const unsigned char *p = px + (size_t)y * stride + (size_t)x * 4;
                    const double a = p[3] / 255.0;
                    if (a <= 0.0) continue;
                    const double src[3] = {p[2] / 255.0 * it.white, p[1] / 255.0 * it.white, p[0] / 255.0 * it.white};   // premultiplied r g b, at the text's white
                    const size_t i = ((size_t)(y0 + y) * frame.width + (x0 + x)) * 4;
                    for (int c = 0; c < 3; ++c)
                    {
                        if (frame.deep())
                        {
                            const double d = frame.rgba16[i + c] / 65535.0;
                            frame.rgba16[i + c] = (uint16_t)std::lround(std::clamp(src[c] + d * (1.0 - a), 0.0, 1.0) * 65535.0);
                        }
                        else
                        {
                            const double d = frame.rgba[i + c] / 255.0;
                            frame.rgba[i + c] = (uint8_t)std::lround(std::clamp(src[c] + d * (1.0 - a), 0.0, 1.0) * 255.0);
                        }
                    }
                }
            cairo_destroy(cr);
            cairo_surface_destroy(s);
        }
        return true;
    }
}
}
