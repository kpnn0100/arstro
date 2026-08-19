#include "BaseCatalog.h"
#include <algorithm>
#include <map>

namespace genesis
{
    const std::vector<SignalDef> &commonSignals()
    {
        static const std::vector<SignalDef> s = {
            {"attach", "", "", "", "First frame after the component is built.", false},
            {"resize", "", "", "", "The component's width or height changed.", false},
            {"hoverEnter", "onHoverChanged", "", "", "The pointer came to rest over the component.", false},
            {"hoverExit", "onHoverChanged", "", "", "The pointer left.", false},
            {"focusGained", "onFocusChanged", "", "", "The component took keyboard focus.", false},
            {"focusLost", "onFocusChanged", "", "", "The component lost keyboard focus.", false},
        };
        return s;
    }

    const std::vector<BaseRead> &commonReads()
    {
        static const std::vector<BaseRead> r = {
            {"hover", "hoverAmount()", "Animated hover factor, 0..1."},
            {"focused", "(hasFocus() ? 1.0 : 0.0)", "1 while focused."},
            {"enabled", "(enabled ? 1.0 : 0.0)", "1 while enabled."},
            {"w", "width.value()", "The component's width."},
            {"h", "height.value()", "The component's height."},
        };
        return r;
    }

    namespace
    {
        BaseDef make(std::string name, std::string cppClass, std::string summary, std::string mustDraw,
                     std::vector<SignalDef> signals, std::vector<BaseRead> reads)
        {
            BaseDef d;
            d.name = std::move(name);
            d.cppClass = std::move(cppClass);
            d.summary = std::move(summary);
            d.mustDraw = std::move(mustDraw);
            d.signals = std::move(signals);
            for (const auto &s : commonSignals())
                d.signals.push_back(s);
            d.reads = std::move(reads);
            for (const auto &r : commonReads())
                d.reads.push_back(r);
            return d;
        }
    }

    const std::vector<BaseDef> &bases()
    {
        static const std::vector<BaseDef> all = {
            make("VisualLoop", "artboard::VisualLoop",
                 "An indeterminate, looping visual: a spinner, a busy pulse, a loading screen.",
                 "A busy visual with no known end. Draw an entry (loopStart) and an exit (loopEnd).",
                 {
                     {"loopStart", "onLoopStart", "", "", "The loop began. Fade/scale your visual in here.", true},
                     {"cycle", "onCycle", "int", "index", "One full cycle completed (counts from 1).", false},
                     {"loopEnd", "onLoopEnd", "", "", "The loop ended. Fade it out here.", true},
                 },
                 {
                     {"phase", "cyclePhase()", "Progress through the current cycle, 0..1."},
                     {"cycle", "(double)cycleCount()", "Completed cycles since start."},
                     {"elapsed", "elapsedMs()", "Milliseconds since start()."},
                     {"running", "(running() ? 1.0 : 0.0)", "1 while looping."},
                 }),
            make("ProgressIndicator", "artboard::ProgressIndicator",
                 "A determinate progress visual: a bar, a ring, a meter.",
                 "A 0..1 readout. Draw the empty, partial, and full states, plus the indeterminate sweep.",
                 {
                     {"valueChanged", "onValueChanged", "double", "v", "The value changed (already clamped to 0..1).", true},
                     {"complete", "onComplete", "", "", "The value first reached 1.", true},
                     {"indeterminate", "onIndeterminate", "", "", "Switched to unknown-progress mode.", false},
                     {"determinate", "onDeterminate", "", "", "Switched back to known progress.", false},
                 },
                 {
                     {"value", "value()", "The committed value, 0..1."},
                     {"display", "displayValue()", "The spring-smoothed level to draw."},
                     {"phase", "phase()", "Indeterminate sweep position, 0..1."},
                     {"indeterminate", "(indeterminate() ? 1.0 : 0.0)", "1 in indeterminate mode."},
                 }),
            make("Button", "artboard::Button",
                 "A press/click control.",
                 "Idle, hover, pressed, and disabled states. Press must give immediate animated feedback.",
                 {
                     {"pressDown", "onPressDown", "", "", "The pointer went down on the button.", true},
                     {"release", "onRelease", "", "", "The press completed into a click.", true},
                     {"cancel", "onCancel", "", "", "The press was abandoned (dragged off).", true},
                     {"clicked", "onClicked", "", "", "The button fired (pointer or keyboard).", false},
                 },
                 {}),
            make("Slider", "artboard::Slider",
                 "A horizontal ranged control (track + fill + thumb).",
                 "Track, fill, and thumb across idle/hover/dragging, at both ends of the range.",
                 {
                     {"dragStart", "onDragStart", "", "", "A drag began.", true},
                     {"valueChanged", "onValueChanged", "double", "v", "The value changed by user interaction.", true},
                     {"dragEnd", "onDragEnd", "", "", "The drag ended.", true},
                 },
                 {
                     {"value", "value()", "The current value."},
                     {"display", "displayValue()", "The spring-smoothed displayed value."},
                     {"norm", "genesisNorm(displayValue(), minimum(), maximum())", "Displayed value mapped to 0..1."},
                     {"min", "minimum()", "Range minimum."},
                     {"max", "maximum()", "Range maximum."},
                 }),
            make("Checkbox", "artboard::Checkbox",
                 "A boolean toggle.",
                 "Unchecked, checked, hover, and the transition between the two.",
                 {
                     {"checkedChanged", "onCheckedChanged", "bool", "on", "The user toggled it.", true},
                 },
                 {
                     {"checked", "(checked() ? 1.0 : 0.0)", "1 while checked."},
                 }),
        };
        return all;
    }

    const BaseDef *findBase(const std::string &name)
    {
        for (const auto &b : bases())
            if (b.name == name)
                return &b;
        return nullptr;
    }

    const std::vector<std::string> &easingNames()
    {
        static const std::vector<std::string> e = {
            "Linear",
            "EaseInQuad", "EaseOutQuad", "EaseInOutQuad",
            "EaseInCubic", "EaseOutCubic", "EaseInOutCubic",
            "EaseInQuart", "EaseOutQuart", "EaseInOutQuart",
            "EaseInSine", "EaseOutSine", "EaseInOutSine",
            "EaseInExpo", "EaseOutExpo", "EaseInOutExpo",
            "EaseInBack", "EaseOutBack", "EaseInOutBack",
            "EaseInElastic", "EaseOutElastic", "EaseInOutElastic",
            "EaseInBounce", "EaseOutBounce", "EaseInOutBounce",
            "Standard", "StandardDecel", "StandardAccel", "EmphasizedDecel", "EmphasizedAccel",
        };
        return e;
    }

    artboard::Easing easingFromName(const std::string &name)
    {
        // One table, indexed the same way BaseCatalog::easingNames() lists them, so the
        // editor's dropdown, the emitter's `Easing::X`, and this lookup cannot disagree.
        static const std::map<std::string, artboard::Easing> m = {
            {"Linear", artboard::Easing::Linear},
            {"EaseInQuad", artboard::Easing::EaseInQuad},
            {"EaseOutQuad", artboard::Easing::EaseOutQuad},
            {"EaseInOutQuad", artboard::Easing::EaseInOutQuad},
            {"EaseInCubic", artboard::Easing::EaseInCubic},
            {"EaseOutCubic", artboard::Easing::EaseOutCubic},
            {"EaseInOutCubic", artboard::Easing::EaseInOutCubic},
            {"EaseInQuart", artboard::Easing::EaseInQuart},
            {"EaseOutQuart", artboard::Easing::EaseOutQuart},
            {"EaseInOutQuart", artboard::Easing::EaseInOutQuart},
            {"EaseInSine", artboard::Easing::EaseInSine},
            {"EaseOutSine", artboard::Easing::EaseOutSine},
            {"EaseInOutSine", artboard::Easing::EaseInOutSine},
            {"EaseInExpo", artboard::Easing::EaseInExpo},
            {"EaseOutExpo", artboard::Easing::EaseOutExpo},
            {"EaseInOutExpo", artboard::Easing::EaseInOutExpo},
            {"EaseInBack", artboard::Easing::EaseInBack},
            {"EaseOutBack", artboard::Easing::EaseOutBack},
            {"EaseInOutBack", artboard::Easing::EaseInOutBack},
            {"EaseInElastic", artboard::Easing::EaseInElastic},
            {"EaseOutElastic", artboard::Easing::EaseOutElastic},
            {"EaseInOutElastic", artboard::Easing::EaseInOutElastic},
            {"EaseInBounce", artboard::Easing::EaseInBounce},
            {"EaseOutBounce", artboard::Easing::EaseOutBounce},
            {"EaseInOutBounce", artboard::Easing::EaseInOutBounce},
            {"Standard", artboard::Easing::Standard},
            {"StandardDecel", artboard::Easing::StandardDecel},
            {"StandardAccel", artboard::Easing::StandardAccel},
            {"EmphasizedDecel", artboard::Easing::EmphasizedDecel},
            {"EmphasizedAccel", artboard::Easing::EmphasizedAccel},
        };
        auto it = m.find(name);
        return it == m.end() ? artboard::Easing::Linear : it->second;
    }

    bool isEasingName(const std::string &name)
    {
        const auto &e = easingNames();
        return std::find(e.begin(), e.end(), name) != e.end();
    }
}
