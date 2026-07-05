#include "Theme.h"

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    Theme makeCosmoV2Theme()
    {
        Theme th = Theme::basicTheme();
        const Color primary = palette::primary();
        const Color secondary = palette::secondary();
        const Color card = palette::card();
        const Color background = palette::background();
        const Color mutedFg = palette::mutedForeground();
        const Color border = palette::border();
        const Color white = palette::white();

        // SliderRow's track/fill/thumb (App.tsx SliderRow: 3px tall pill track,
        // bg-[#252525]; accent fill anchored at the zero-crossing -- Slider's
        // built-in zero-crossing range fill covers exactly this bipolar-range
        // behavior); 9x9 white thumb with a white border.
        th.slider.track = {Paint::filled(secondary), radius::pill()};
        th.slider.rangeFill = {Paint::filled(primary), radius::pill()};
        th.slider.thumb = {Paint::filledStroked(white, white, 1.0), radius::pill()};
        th.slider.thumbRadius = 4.5;

        // Tab strip: idle tabs show the strip's own background through (no
        // separate fill, square corners); the active tab fills with the card
        // surface and welds into the panel below (TabView's built-in
        // active-tab-extends-down behavior), topped with a 1.5px accent bar.
        th.tab.tabIdle = {Paint::filled(background), 0.0};
        th.tab.tabActive = {Paint::filled(card), 0.0};
        th.tab.label = {mutedFg, 10.0, font::sansMedium()};
        th.tab.labelActive = {primary, 10.0, font::sansMedium()};
        th.tab.activeIndicatorColor = primary;
        th.tab.activeIndicatorHeight = 1.5;

        // Hue-remap / settings toggles: flat pill track, white thumb, accent
        // only when on (the one non-interactive-state exception: "on" itself
        // IS the interactive state).
        th.toggle.trackOff = {Paint::filled(secondary), radius::pill()};
        th.toggle.trackOn = {Paint::filled(primary), radius::pill()};
        th.toggle.thumb = {Paint::filled(white), radius::pill()};

        // Outline buttons (aspect-ratio chips, flip/auto, mask-mode chips):
        // transparent body, hairline border, muted label; "pressed" approximates
        // the design's hover/active treatment (accent border+tint) since Artboard
        // has no hover state to key off of.
        th.button.idle = {Paint::filledStroked(Color{0, 0, 0, 0}, border, 1.0), radius::control()};
        th.button.pressed = {Paint::filledStroked(Color{primary.r, primary.g, primary.b, 0.10}, primary, 1.0),
                              radius::control()};
        th.button.label = {mutedFg, 10.0, font::sans()};

        // Scrollbars: Figma hides them by default and shows a subtle thumb only
        // on hover (::-webkit-scrollbar-thumb rgba(255,255,255,.12)). Artboard
        // has no hover state, so the closest fidelity is an always-present but
        // very faint thumb rather than either "always visible" or "never visible".
        th.scroll.viewport = {Paint::filled(Color{0, 0, 0, 0}), 0.0};
        th.scroll.track = {Paint::filled(Color{0, 0, 0, 0}), 0.0};
        th.scroll.thumb = {Paint::filled(palette::whiteAlpha(0.12)), radius::control()};

        th.label = {palette::foreground(), 11.0, font::sans()};
        return th;
    }

    const Theme &sharedTheme()
    {
        static const Theme th = makeCosmoV2Theme();
        return th;
    }
}
}
