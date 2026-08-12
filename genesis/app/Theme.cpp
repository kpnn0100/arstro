#include "Theme.h"

namespace genesis
{
namespace ui
{
    namespace
    {
        artboard::BoxStyle box(const artboard::Color &fill, const artboard::Color &stroke, double r)
        {
            artboard::BoxStyle s;
            s.paint = artboard::Paint::filledStroked(fill, stroke, 1.0);
            s.cornerRadius = r;
            return s;
        }
        artboard::TextStyle text(const artboard::Color &c, double px, const char *family)
        {
            artboard::TextStyle t;
            t.color = c;
            t.sizePx = px;
            t.fontFamily = family;
            return t;
        }

        artboard::Theme make()
        {
            using namespace palette;
            artboard::Theme t = artboard::Theme::basicTheme();

            t.button.idle = box(secondary(), border(), radius::control());
            t.button.pressed = box(primaryAlpha(0.28), primary(), radius::control());
            t.button.label = text(foreground(), type::body(), font::sansMedium());

            t.textBox.idle = box(input(), border(), radius::control());
            t.textBox.focused = box(input(), primary(), radius::control());
            t.textBox.text = text(foreground(), type::small(), font::mono());
            t.textBox.placeholder = text(mutedForeground(), type::small(), font::mono());
            t.textBox.caretColor = primary();

            t.checkbox.box = box(input(), border(), radius::hairline());
            t.checkbox.indicator.paint = artboard::Paint::filled(primary());
            t.checkbox.indicator.cornerRadius = radius::hairline();
            t.checkbox.label = text(foreground(), type::small(), font::sans());

            t.slider.track = box(input(), border(), radius::pill());
            t.slider.rangeFill.paint = artboard::Paint::filled(primary());
            t.slider.rangeFill.cornerRadius = radius::pill();
            t.slider.thumb = box(foreground(), border(), radius::pill());

            t.progress.track = box(input(), border(), radius::pill());
            t.progress.fill.paint = artboard::Paint::filled(primary());
            t.progress.fill.cornerRadius = radius::pill();

            t.combo.field = box(input(), border(), radius::control());
            t.combo.popup = box(popover(), border(), radius::control());
            t.combo.text = text(foreground(), type::small(), font::sans());
            t.combo.rowSelected.paint = artboard::Paint::filled(primaryAlpha(0.22));
            t.combo.rowSelected.cornerRadius = radius::hairline();
            t.combo.caretColor = mutedForeground();

            t.scroll.track = box(artboard::Color{0, 0, 0, 0}, artboard::Color{0, 0, 0, 0}, 0.0);
            t.scroll.thumb = box(whiteAlpha(0.16), artboard::Color{0, 0, 0, 0}, radius::pill());

            t.toggle.trackOff = box(input(), border(), radius::pill());
            t.toggle.trackOn = box(primaryAlpha(0.5), primary(), radius::pill());
            t.toggle.thumb = box(foreground(), border(), radius::pill());
            return t;
        }
    }

    const artboard::Theme &theme()
    {
        static const artboard::Theme t = make();
        return t;
    }
}
}
