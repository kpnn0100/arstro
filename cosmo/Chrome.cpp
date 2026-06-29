#include "Chrome.h"
#include "CosmoTheme.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    void drawPanelChrome(IRenderTarget &t, double w, double h, const std::string &title)
    {
        // Elevated body with a hairline edge (one radius scale).
        drawRoundedRect(t, Rect{0, 0, w, h}, radius::panel(),
                        Paint::filledStroked(palette::panel(), palette::line(), 1.0));

        // Quiet title (faux-bold via a half-pixel overdraw — weight, not size/accent).
        t.setFill(palette::ink());
        for (double ox : {0.0, 0.4})
            t.drawText(title, 14.0 + ox, 21.0, 11.0);

        // Hairline divider under the header.
        t.beginPath();
        t.moveTo(12.0, 31.0);
        t.lineTo(w - 12.0, 31.0);
        t.setStroke(palette::line(), 1.0);
        t.strokePath();
    }
}
}
