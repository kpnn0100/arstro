#include "Chrome.h"

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    void drawPanelChrome(IRenderTarget &t, double w, double h,
                         const Color &accent, const std::string &title)
    {
        const Color surface = Color::hex(0x141824);
        const Color hairline = Color{1, 1, 1, 0.07};
        const Color titleInk = Color::hex(0xdfe4ee);

        // Quiet chassis: one neutral surface + a single hairline. No accent-tinted
        // border, no full-width colour stripe, no header sheen.
        drawRoundedRect(t, Rect{0, 0, w, h}, 12.0, Paint::filledStroked(surface, hairline, 1.0));

        // molded top-edge highlight (a physical panel, not a glow)
        t.beginPath(); t.moveTo(13.0, 1.0); t.lineTo(w - 13.0, 1.0);
        t.setStroke(Color{1, 1, 1, 0.05}, 1.0); t.strokePath();

        // title: real hierarchy via size + neutral ink (no faux-bold overdraw)
        t.setFill(titleInk);
        t.drawText(title, 15.0, 23.0, 12.5);

        // short accent tab under the title — the section's identity, quietly
        drawRoundedRect(t, Rect{15.0, 30.0, 20.0, 2.0}, 1.0, Paint::filled(accent));
    }
}
}
