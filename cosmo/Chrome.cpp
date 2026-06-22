#include "Chrome.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    void drawPanelChrome(IRenderTarget &t, double w, double h,
                         const Color &accent, const std::string &title)
    {
        drawRoundedRect(t, Rect{0, 0, w, h}, 10.0,
                        Paint::filledStroked(Color::hex(0x14161c),
                                             Color{accent.r, accent.g, accent.b, 0.32}, 1.0));
        drawRoundedRect(t, Rect{0, 0, w, 3.0}, 0.0, Paint::filled(accent));

        // header sheen: a vertical accent glow fading down from under the top stripe
        t.beginPath();
        t.moveTo(1, 3); t.lineTo(w - 1, 3); t.lineTo(w - 1, 36); t.lineTo(1, 36); t.closePath();
        t.setLinearFill(0, 3, 0, 36,
                        Color{accent.r, accent.g, accent.b, 0.12},
                        Color{accent.r, accent.g, accent.b, 0.0});
        t.fillPath();

        // faux-bold title (overdraw with sub-pixel offsets)
        t.setFill(accent);
        for (double ox : {0.0, 0.5})
            for (double oy : {0.0, 0.5})
                t.drawText(title, 14.0 + ox, 21.0 + oy, 13.0);
    }
}
}
