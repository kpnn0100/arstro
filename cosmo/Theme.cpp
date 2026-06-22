#include "CosmoTheme.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    Theme makeCosmoTheme(const Color &accent)
    {
        Theme th = Theme::basicTheme();
        const Color bg = palette::bg(), panel = palette::panel();
        const Color border = palette::border(), ink = palette::ink();
        const Color muted = palette::muted(), surface = palette::surface();

        th.knob.dial = {Paint::filled(bg), 999.0};
        th.knob.trackColor = surface;
        th.knob.valueColor = accent;
        th.knob.indicatorColor = accent;
        th.knob.label = {muted, 9.0};

        th.toggle.trackOff = {Paint::filledStroked(surface, border, 1.0), 999.0};
        th.toggle.trackOn = {Paint::filledStroked(accent, accent, 1.0), 999.0};
        th.toggle.thumb = {Paint::filledStroked(ink, border, 1.0), 999.0};

        th.combo.field = {Paint::filledStroked(panel, border, 1.0), 6.0};
        th.combo.popup = {Paint::filledStroked(panel, accent, 1.0), 6.0};
        th.combo.rowSelected = {Paint::filled(Color::hex(0x2a3a4a)), 0.0};
        th.combo.text = {ink, 13.0};
        th.combo.caretColor = accent;

        th.slider.track = {Paint::filledStroked(surface, border, 1.0), 5.0};
        th.slider.rangeFill = {Paint::filled(accent), 5.0};
        th.slider.thumb = {Paint::filledStroked(accent, accent, 2.0), 999.0};
        th.slider.thumbRadius = 6.0;

        th.tab.tabIdle = {Paint::filledStroked(panel, border, 1.0), 6.0};
        th.tab.tabActive = {Paint::filledStroked(surface, accent, 1.0), 6.0};
        th.tab.label = {ink, 12.0};
        return th;
    }
}
}
