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
        const Color line = palette::line(), ink = palette::ink();
        const Color muted = palette::muted(), surface = palette::surface();

        // Knob (kept styled for completeness; cosmo now edits with sliders).
        th.knob.dial = {Paint::filled(surface), radius::pill()};
        th.knob.trackColor = line;
        th.knob.valueColor = accent;
        th.knob.indicatorColor = ink;
        th.knob.label = {muted, 9.0};

        // Toggle: flat track, light thumb; accent only when on.
        th.toggle.trackOff = {Paint::filled(surface), radius::pill()};
        th.toggle.trackOn = {Paint::filled(accent), radius::pill()};
        th.toggle.thumb = {Paint::filled(ink), radius::pill()};

        // ComboBox: flat field, neutral caret (accent reserved for interaction state).
        th.combo.field = {Paint::filledStroked(surface, line, 1.0), radius::control()};
        th.combo.popup = {Paint::filledStroked(panel, line, 1.0), radius::control()};
        th.combo.rowSelected = {Paint::filled(surface), 0.0};
        th.combo.text = {ink, 12.0};
        th.combo.caretColor = muted;

        // Slider: thin neutral track, accent fill, clean light thumb.
        th.slider.track = {Paint::filled(surface), radius::pill()};
        th.slider.rangeFill = {Paint::filled(accent), radius::pill()};
        th.slider.thumb = {Paint::filledStroked(ink, line, 1.0), radius::pill()};
        th.slider.thumbRadius = 6.0;

        // Tabs: active tab is the SAME surface as the panel below (united); idle tabs
        // recede to the app background. No outlines — the connected surface reads it.
        th.tab.tabIdle = {Paint::filled(bg), radius::panel()};
        th.tab.tabActive = {Paint::filled(panel), radius::panel()};
        th.tab.label = {ink, 12.0};

        // LineGraph (histogram channels handled in HistogramPanel; keep sane defaults).
        th.graph.background = {Paint::filled(bg), radius::control()};
        th.graph.gridColor = line;
        th.graph.lineColor = accent;
        th.graph.fillColor = Color{accent.r, accent.g, accent.b, 0.16};
        return th;
    }
}
}
