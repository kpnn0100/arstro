#include "PresetBar.h"
#include "../CosmoTheme.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace { constexpr double kGap = 6.0; const char *kLabels[3] = {"SAVE", "IMPORT", "EXPORT"}; }

    PresetBar::PresetBar(const Color &accent) : mAccent(accent) {}

    Rect PresetBar::btnRect(int i) const
    {
        const double w = (width.value() - 2 * kGap) / 3.0;
        return Rect{i * (w + kGap), 0.0, w, height.value()};
    }

    void PresetBar::onPaint(IRenderTarget &t) const
    {
        for (int i = 0; i < 3; ++i)
        {
            const Rect r = btnRect(i);
            const bool pressed = mPressed == i;
            drawRoundedRect(t, r, radius::control(),
                            Paint::filledStroked(pressed ? palette::line() : palette::surface(),
                                                 palette::line(), 1.0));
            t.setFill(palette::ink());
            const double tw = (double)std::string(kLabels[i]).size() * 6.4;  // rough centring
            const double ty = r.y + r.h * 0.5 + 4.0;
            t.drawText(kLabels[i], r.x + (r.w - tw) * 0.5, ty, 11.0);
        }
    }

    bool PresetBar::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::Click)
        {
            for (int i = 0; i < 3; ++i)
                if (btnRect(i).contains(local))
                {
                    if (i == 0 && onSave) onSave();
                    else if (i == 1 && onImport) onImport();
                    else if (i == 2 && onExport) onExport();
                    return true;
                }
        }
        return Segment::handleGesture(g, local);
    }
}
}
