#include "AnalogFace.h"
#include "DevicePanel.h"
#include "ParamFormat.h"
#include "ParamMenu.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;
    using paramfmt::formatValue;
    using paramfmt::fromUnit;
    using paramfmt::storeText;
    using paramfmt::toUnit;

    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kArc0 = 0.75 * kPi;   // a dial's minimum: 135° (lower left), screen angles clockwise from +x
        constexpr double kArcSpan = 1.5 * kPi; // … its travel: 270°
        Color fade(Color c, double a) { c.a *= a; return c; }

        std::string suffixOf(const std::string &name)
        {
            const auto dot = name.find('.');
            return dot == std::string::npos ? name : name.substr(dot + 1);
        }
        std::string prefixOf(const std::string &name)
        {
            const auto dot = name.find('.');
            return dot == std::string::npos ? std::string() : name.substr(0, dot);
        }
        std::string upper(std::string s)
        {
            for (auto &c : s) c = (char)std::toupper((unsigned char)c);
            return s;
        }
        std::string lower(std::string s)
        {
            for (auto &c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        }
        /** An arc as a polyline (the render target has none): `from` → `to` radians, screen-clockwise. */
        void arc(IRenderTarget &t, double cx, double cy, double r, double from, double to, const Color &c, double w)
        {
            if (std::fabs(to - from) < 1e-4) return;
            const int n = std::max(2, (int)std::ceil(std::fabs(to - from) / (kPi / 48.0)));
            t.setStroke(c, w);
            t.beginPath();
            for (int k = 0; k <= n; ++k)
            {
                const double a = from + (to - from) * k / n;
                if (k == 0) t.moveTo(cx + r * std::cos(a), cy + r * std::sin(a));
                else t.lineTo(cx + r * std::cos(a), cy + r * std::sin(a));
            }
            t.strokePath();
        }
        /** One cycle of a wave by its choice's name, at phase p ∈ [0,1): −1 … 1. Unknown: a sine. */
        double waveAt(const std::string &name, double p)
        {
            p -= std::floor(p);
            if (name == "saw") return 2.0 * p - 1.0;
            if (name == "square") return p < 0.5 ? 1.0 : -1.0;
            if (name == "triangle") return 1.0 - 4.0 * std::fabs(p - 0.5);
            return std::sin(2.0 * kPi * p);
        }
        /** A choice's key face: its shape when the face knows the name (a wave, a filter), else nothing. */
        bool drawGlyph(IRenderTarget &t, const std::string &choice, const Rect &r, const Color &c)
        {
            const std::string n = lower(choice);
            const double x0 = r.x, x1 = r.right(), y0 = r.y, y1 = r.bottom(), cy = (y0 + y1) * 0.5, h = (y1 - y0) * 0.5;
            t.setStroke(c, 1.3);
            t.beginPath();
            if (n == "sine" || n == "saw" || n == "square" || n == "triangle")
            {
                const int steps = 24;
                for (int k = 0; k <= steps; ++k)
                {
                    const double p = (double)k / steps;
                    double v = waveAt(n, p);
                    if (n == "saw" && k == steps) v = 1.0;
                    const double x = x0 + p * (x1 - x0), y = cy - v * h;
                    if (k == 0) t.moveTo(x, y);
                    else if (n == "square" && (k == steps / 2)) { t.lineTo(x, cy - h); t.lineTo(x, cy + h); }
                    else t.lineTo(x, y);
                }
                if (n == "saw") t.lineTo(x1, cy + h);
                t.strokePath();
                return true;
            }
            if (n == "lowpass" || n == "highpass" || n == "bandpass" || n == "notch")
            {
                const int steps = 24;
                for (int k = 0; k <= steps; ++k)
                {
                    const double p = (double)k / steps, w = std::pow(2.0, (p - 0.5) * 6.0); // three octaves either side
                    const double db = std::clamp(AnalogFace::responseDb(n, w, 0.35), -24.0, 12.0);
                    const double x = x0 + p * (x1 - x0), y = y0 + (12.0 - db) / 36.0 * (y1 - y0);
                    if (k == 0) t.moveTo(x, y);
                    else t.lineTo(x, y);
                }
                t.strokePath();
                return true;
            }
            return false;
        }
    }

    double AnalogFace::responseDb(const std::string &mode, double w, double res)
    {
        // StateVariableFilter (S4)/(S5): the analog prototype, s = j·w, D = s² + k·s + 1, k = 1/Q
        const double q = 0.7071 * std::pow(2.0, 5.5 * std::clamp(res, 0.0, 1.0)), k = 1.0 / q;
        const double re = 1.0 - w * w, im = k * w, d = std::sqrt(re * re + im * im);
        double mag = 1.0 / d;                                  // lowpass
        if (mode == "highpass") mag = w * w / d;
        else if (mode == "bandpass") mag = w / d;              // the band output: its peak gain is Q
        else if (mode == "notch") mag = std::fabs(re) / d;
        return 20.0 * std::log10(std::max(mag, 1e-6));
    }

    AnalogFace::AnalogFace(DevicePanel &owner) : mOwner(owner) { clipToBounds = true; }

    void AnalogFace::build(const solaris::DeviceModel &d)
    {
        // modules in the registry's order of first appearance, the ungrouped last
        std::vector<std::string> order;
        for (const auto &p : d.params)
            if (std::find(order.begin(), order.end(), prefixOf(p.name)) == order.end()) order.push_back(prefixOf(p.name));
        const auto none = std::find(order.begin(), order.end(), std::string());
        if (none != order.end()) { order.erase(none); order.push_back(std::string()); }
        for (const auto &prefix : order)
        {
            Section s;
            s.prefix = prefix;
            s.title = prefix.empty() ? std::string("Main") : upper(prefix);
            for (const auto &g : d.groups)
                if (g.prefix == prefix) s.title = g.label; // the registry's title (REQ-device-10)
            for (const auto &p : d.params)
                if (prefixOf(p.name) == prefix)
                {
                    Control c;
                    c.spec = p;
                    c.section = (int)mSections.size();
                    c.knob = p.choices.empty();
                    s.controls.push_back((int)mControls.size());
                    (c.knob ? s.knobs : s.switches)++;
                    mControls.push_back(std::move(c));
                }
            mSections.push_back(std::move(s));
        }
        // a module's screen, by what its parameters say it is
        for (int si = 0; si < sectionCount(); ++si)
        {
            Section &s = mSections[(size_t)si];
            const int wave = find(s.prefix, "wave", si);
            if (wave >= 0 && !mControls[(size_t)wave].knob) s.screen = Screen::Wave;
            else if (find(s.prefix, "cutoff", si) >= 0 && find(s.prefix, "res", si) >= 0) s.screen = Screen::Filter;
            else if (find(s.prefix, "attack", si) >= 0 && find(s.prefix, "decay", si) >= 0 && find(s.prefix, "sustain", si) >= 0 &&
                     find(s.prefix, "release", si) >= 0)
                s.screen = Screen::Envelope;
            s.cols = s.knobs <= kMaxCols ? std::max(1, s.knobs) : std::min(kMaxCols, (s.knobs + 1) / 2);
            s.rows = s.knobs == 0 ? 0 : (s.knobs + s.cols - 1) / s.cols;
            double inner = s.knobs > 0 ? s.cols * kCellW : 0.0;
            for (int i : s.controls)
                if (!mControls[(size_t)i].knob)
                {
                    const double n = (double)std::max<size_t>(1, mControls[(size_t)i].spec.choices.size());
                    inner = std::max(inner, n * kKeyMinW + (n - 1.0) * kKeyGap);
                }
            if (s.screen != Screen::None) inner = std::max(inner, 2.0 * kCellW);
            s.naturalW = inner + 2.0 * kPad;
            s.naturalH = kTitleH + (s.screen != Screen::None ? kScreenH + kGap : 0.0) + s.switches * kSwitchH + s.rows * kCellH + kPad;
        }
        bind(d, {});
    }

    int AnalogFace::find(const std::string &prefix, const std::string &suffix, int si) const
    {
        for (int i : mSections[(size_t)si].controls)
            if (mControls[(size_t)i].spec.name == (prefix.empty() ? suffix : prefix + "." + suffix)) return i;
        return -1;
    }

    void AnalogFace::bind(const solaris::DeviceModel &d, const std::vector<solaris::BindingModel> &bindings)
    {
        mLastChanged = d.lastChanged;
        for (int i = 0; i < controlCount(); ++i)
        {
            Control &c = mControls[(size_t)i];
            for (const auto &p : d.params)
                if (p.name == c.spec.name)
                {
                    c.spec.value = p.value;
                    c.spec.formula = p.formula;
                    c.spec.text = p.text;
                }
            c.bound = !c.spec.formula.empty();
            double v = c.spec.value;
            if (c.bound)
                for (const auto &b : bindings) // a bound value: what it evaluates to now (R-MIX-16)
                    if (b.address == mOwner.device() + "." + c.spec.name) v = b.live;
            if (i == mDrag) continue; // a gesture in flight outranks the model
            if (c.knob) c.turn = toUnit(c.spec, v);
            else c.choice = std::clamp((int)std::lround(c.spec.value), 0, std::max(0, (int)c.spec.choices.size() - 1));
        }
    }

    double AnalogFace::layoutFor(double width)
    {
        mWidth = width;
        // modules flow into rows; a row shares its spare width (in proportion) and its height
        const double avail = std::max(0.0, width - 2.0 * kPad);
        double y = kPad;
        size_t i = 0;
        while (i < mSections.size())
        {
            size_t j = i;
            double used = 0.0;
            while (j < mSections.size())
            {
                const double w = mSections[j].naturalW + (j > i ? kGap : 0.0);
                if (j > i && used + w > avail + 1e-6) break;
                used += w;
                ++j;
            }
            double natural = 0.0, rowH = 0.0;
            for (size_t k = i; k < j; ++k)
            {
                natural += mSections[k].naturalW;
                rowH = std::max(rowH, mSections[k].naturalH);
            }
            const double spare = std::max(0.0, avail - (natural + (double)(j - i - 1) * kGap));
            double x = kPad;
            for (size_t k = i; k < j; ++k)
            {
                Section &s = mSections[k];
                const double w = j - i == 1 ? std::max(s.naturalW, avail) : s.naturalW + spare * s.naturalW / std::max(1.0, natural);
                s.box = Rect{x, y, std::min(w, std::max(s.naturalW, avail)), rowH};
                x += s.box.w + kGap;
                // inside: the screen, the switches, then the knobs centred in what is left
                double cy = y + kTitleH + (s.screen != Screen::None ? kScreenH + kGap : 0.0);
                const double innerX = s.box.x + kPad, innerW = s.box.w - 2.0 * kPad;
                for (int ci : s.controls)
                    if (!mControls[(size_t)ci].knob)
                    {
                        mControls[(size_t)ci].cell = Rect{innerX, cy, innerW, kSwitchH};
                        cy += kSwitchH;
                    }
                const double gridH = s.rows * kCellH, room = s.box.bottom() - kPad - cy;
                cy += std::max(0.0, (room - gridH) * 0.5);
                const double cw = s.cols > 0 ? innerW / s.cols : innerW;
                int n = 0;
                for (int ci : s.controls)
                    if (mControls[(size_t)ci].knob)
                    {
                        mControls[(size_t)ci].cell = Rect{innerX + (n % s.cols) * cw, cy + (n / s.cols) * kCellH, cw, kCellH};
                        ++n;
                    }
            }
            y += rowH + kGap;
            i = j;
        }
        mContentH = mSections.empty() ? 0.0 : y - kGap + kPad;
        return mContentH;
    }

    void AnalogFace::advance(double nowMs)
    {
        mNowMs = nowMs;
        for (int i = 0; i < controlCount(); ++i)
        {
            Control &c = mControls[(size_t)i];
            const bool on = c.spec.name == mLastChanged;
            if (!c.placed)
            {
                // nowhere to travel from: placed
                c.shown.set(c.turn);
                c.turnLast = c.turn;
                c.light.set((double)c.choice);
                c.choiceLast = c.choice;
                c.lit.set(on ? 1.0 : 0.0);
                c.litLast = on;
                c.placed = true;
            }
            if (i != mDrag && c.turn != c.turnLast)
            {
                c.shown.animateTo(c.turn, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs); // a value it does not control
                c.turnLast = c.turn;
            }
            if (c.choice != c.choiceLast)
            {
                c.light.animateTo((double)c.choice, motion::kSlideMs, Easing::EaseOutCubic, nowMs);
                c.choiceLast = c.choice;
            }
            if (on != c.litLast)
            {
                c.lit.animateTo(on ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                c.litLast = on;
            }
            c.shown.update(nowMs);
            c.light.update(nowMs);
            c.lit.update(nowMs);
        }
        if (!isHovered() && mDrag < 0) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    // ── geometry ────────────────────────────────────────────────────────────────────────────

    int AnalogFace::controlIndex(const std::string &param) const
    {
        for (int i = 0; i < controlCount(); ++i)
            if (mControls[(size_t)i].spec.name == param) return i;
        return -1;
    }

    std::string AnalogFace::controlParam(int i) const { return i >= 0 && i < controlCount() ? mControls[(size_t)i].spec.name : std::string(); }

    Rect AnalogFace::controlRect(int i) const
    {
        if (i < 0 || i >= controlCount()) return Rect{};
        Rect r = mControls[(size_t)i].cell;
        r.y -= scroll;
        return r;
    }

    Rect AnalogFace::dialRect(int i) const
    {
        const Rect c = controlRect(i);
        if (c.w <= 0) return c;
        if (!mControls[(size_t)i].knob)
        {
            const Rect k0 = keyRect(i, 0), kn = keyRect(i, (int)mControls[(size_t)i].spec.choices.size() - 1);
            return Rect{k0.x, k0.y, kn.right() - k0.x, k0.h};
        }
        return Rect{c.x + (c.w - kDial) * 0.5, c.y + 3.25, kDial, kDial};
    }

    Rect AnalogFace::keyRect(int i, int choice) const
    {
        if (i < 0 || i >= controlCount() || mControls[(size_t)i].knob) return Rect{};
        const Rect c = controlRect(i);
        const double n = (double)std::max<size_t>(1, mControls[(size_t)i].spec.choices.size());
        const double kw = std::clamp((c.w - (n - 1.0) * kKeyGap) / n, kKeyMinW * 0.5, kKeyMaxW);
        const double total = n * kw + (n - 1.0) * kKeyGap, x0 = c.x + (c.w - total) * 0.5;
        return Rect{x0 + choice * (kw + kKeyGap), c.y + 3.25, kw, kKeysH};
    }

    double AnalogFace::contentTop(int i) const { return i >= 0 && i < controlCount() ? mControls[(size_t)i].cell.y : 0.0; }

    Rect AnalogFace::sectionRect(int s) const
    {
        Rect r = mSections[(size_t)s].box;
        r.y -= scroll;
        return r;
    }

    double AnalogFace::sectionTop(const std::string &prefix) const
    {
        for (const auto &s : mSections)
            if (s.prefix == prefix) return s.box.y;
        return -1.0;
    }

    int AnalogFace::controlAt(const Point &local) const
    {
        for (int i = 0; i < controlCount(); ++i)
        {
            const Rect r = mControls[(size_t)i].knob ? controlRect(i) : dialRect(i);
            if (r.contains(local)) return i;
        }
        return -1;
    }

    double AnalogFace::turnLive(const std::string &param) const
    {
        const int i = controlIndex(param);
        return i >= 0 ? mControls[(size_t)i].shown.value() : 0.0;
    }

    double AnalogFace::turnTarget(const std::string &param) const
    {
        const int i = controlIndex(param);
        return i >= 0 ? mControls[(size_t)i].turn : 0.0;
    }

    double AnalogFace::lightLive(const std::string &param) const
    {
        const int i = controlIndex(param);
        return i >= 0 ? mControls[(size_t)i].light.value() : 0.0;
    }

    double AnalogFace::litAmount(const std::string &param) const
    {
        const int i = controlIndex(param);
        return i >= 0 ? mControls[(size_t)i].lit.value() : 0.0;
    }

    double AnalogFace::liveValue(int i) const
    {
        const Control &c = mControls[(size_t)i];
        if (!c.knob) return c.light.value();
        return fromUnit(c.spec, c.shown.value());
    }

    std::string AnalogFace::valueText(const std::string &param) const
    {
        const int i = controlIndex(param);
        if (i < 0) return std::string();
        const Control &c = mControls[(size_t)i];
        if (c.bound) return bindingText(c.spec.formula);
        if (!c.knob) return c.choice >= 0 && c.choice < (int)c.spec.choices.size() ? c.spec.choices[(size_t)c.choice] : std::string();
        return formatValue(c.spec, fromUnit(c.spec, c.shown.value()));
    }

    // ── gestures: one `set` per step, as a slider's ─────────────────────────────────────────

    void AnalogFace::send(const Control &c, const std::string &value) const
    {
        if (mOwner.onCommand) mOwner.onCommand("set " + mOwner.device() + "." + c.spec.name + "=" + value);
    }

    bool AnalogFace::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
            if (mDrag < 0) mHover.setHovered(controlAt(local));
            return true;
        case Gesture::Type::Down:
        {
            const int i = controlAt(local);
            mPress = i >= 0 && mControls[(size_t)i].knob && !mControls[(size_t)i].bound ? i : -1; // a bound knob is its formula's
            if (mPress >= 0)
            {
                mPressY = local.y;
                mPressTurn = mControls[(size_t)mPress].shown.value(); // where it is DRAWN: a drag never jumps
                mSent.clear();
            }
            return true;
        }
        case Gesture::Type::DragStart:
            if (mPress >= 0) mDrag = mPress;
            [[fallthrough]];
        case Gesture::Type::Drag:
        {
            if (mDrag < 0) return true;
            Control &c = mControls[(size_t)mDrag];
            // up turns it up; Shift is five times finer; the knob is the pointer's (direct manipulation)
            const double d = (mPressY - local.y) / kTurnPx * (g.shift ? 0.2 : 1.0);
            const double v = fromUnit(c.spec, std::clamp(mPressTurn + d, 0.0, 1.0));
            const double turn = toUnit(c.spec, v);
            c.shown.set(turn);
            c.turn = c.turnLast = turn;
            const std::string text = storeText(v, c.spec.integer);
            if (text != mSent)
            {
                mSent = text;
                send(c, text);
            }
            return true;
        }
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
            mDrag = -1;
            mPress = -1;
            return true;
        case Gesture::Type::Click:
        {
            // a switch's key: that choice, one line
            const int i = controlAt(local);
            if (i < 0 || mControls[(size_t)i].knob) return true;
            const Control &c = mControls[(size_t)i];
            for (int k = 0; k < (int)c.spec.choices.size(); ++k)
                if (keyRect(i, k).contains(local) && k != c.choice) send(c, c.spec.choices[(size_t)k]);
            return true;
        }
        case Gesture::Type::DoubleClick:
        {
            const int i = controlAt(local);
            if (i >= 0 && mControls[(size_t)i].knob && !mControls[(size_t)i].bound)
                send(mControls[(size_t)i], storeText(mControls[(size_t)i].spec.def, mControls[(size_t)i].spec.integer)); // the default
            return true;
        }
        case Gesture::Type::RightClick:
        {
            const int i = controlAt(local);
            if (i >= 0) mOwner.openParamMenuFor(mControls[(size_t)i].spec, mControls[(size_t)i].choice, g.pos); // R-WIN-3
            return true;
        }
        default:
            return false; // the wheel scrolls the panel
        }
    }

    // ── paint ───────────────────────────────────────────────────────────────────────────────

    void AnalogFace::paintKnob(IRenderTarget &t, const Control &c, int i, const Rect &cell) const
    {
        const double hv = mHover.amount(i), lit = c.lit.value(), turn = c.shown.value();
        const bool active = i == mDrag;
        const double cx = cell.x + cell.w * 0.5, cy = cell.y + 3.25 + kDial * 0.5, R = kDial * 0.5;
        // the last change: a halo
        if (lit > 0.001) drawCircle(t, cx, cy, R + 2.5, Paint::stroked(palette::primaryAlpha(0.55 * lit), 2.0));
        // the track, and the value's arc in the accent — from the minimum, or from zero when the range spans it
        arc(t, cx, cy, R - 1.5, kArc0, kArc0 + kArcSpan, surface::knobTrack(), 3.0);
        double origin = 0.0;
        if (c.spec.min < 0.0 && c.spec.max > 0.0) origin = toUnit(c.spec, 0.0);
        const Color arcC = c.bound ? palette::primaryAlpha(0.55) : palette::primary();
        arc(t, cx, cy, R - 1.5, kArc0 + kArcSpan * std::min(origin, turn), kArc0 + kArcSpan * std::max(origin, turn), arcC, 3.0);
        // detents: a whole-number range of a few steps clicks
        if (c.spec.integer && c.spec.max - c.spec.min >= 1.0 && c.spec.max - c.spec.min <= 12.0)
        {
            const int n = (int)std::lround(c.spec.max - c.spec.min);
            for (int k = 0; k <= n; ++k)
            {
                const double a = kArc0 + kArcSpan * (double)k / n;
                const bool here = std::fabs((double)k / n - turn) < 0.5 / n;
                drawCircle(t, cx + (R + 3.5) * std::cos(a), cy + (R + 3.5) * std::sin(a), 1.1,
                           Paint::filled(here ? palette::primary() : palette::whiteAlpha(0.28)));
            }
        }
        // the cap, and its pointer
        const double capR = R - 6.0;
        // a cap lit from above, as a moulded knob is — between two of the theme's greys, brighter under the pointer
        const Color capTop = lerpColor(lerpColor(surface::knobCap(), palette::foreground(), 0.10), palette::foreground(), 0.06 * hv);
        t.setRadialFill(cx - 0.35 * capR, cy - 0.45 * capR, 1.6 * capR, capTop, surface::moduleBg());
        t.beginPath();
        for (int k = 0; k <= 48; ++k)
        {
            const double a = 2.0 * kPi * k / 48.0;
            if (k == 0) t.moveTo(cx + capR * std::cos(a), cy + capR * std::sin(a));
            else t.lineTo(cx + capR * std::cos(a), cy + capR * std::sin(a));
        }
        t.closePath();
        t.fillPath();
        drawCircle(t, cx, cy, capR, Paint::stroked(palette::whiteAlpha(active ? 0.24 : 0.09), 1.0));
        const double a = kArc0 + kArcSpan * turn;
        t.setStroke(palette::foreground(), 2.0);
        t.beginPath();
        t.moveTo(cx + 0.3 * capR * std::cos(a), cy + 0.3 * capR * std::sin(a));
        t.lineTo(cx + 0.92 * capR * std::cos(a), cy + 0.92 * capR * std::sin(a));
        t.strokePath();
        // its label and its value, or its binding
        const double ly = cell.y + 3.25 + kDial + 13.0;
        const std::string label = textfit::ellipsize(t, c.spec.label, cell.w - 4.0, 10.0, font::sans());
        t.setFill(lerpColor(palette::mutedForeground(), palette::foreground(), std::max(hv, active ? 1.0 : 0.0)));
        t.drawText(label, cx - t.measureText(label, 10.0, font::sans()) * 0.5, ly, 10.0, font::sans());
        const std::string value = textfit::ellipsize(t, c.bound ? bindingText(c.spec.formula) : formatValue(c.spec, fromUnit(c.spec, turn)), cell.w - 4.0, 9.0, font::mono());
        t.setFill(c.bound ? palette::primary() : active ? palette::foreground() : palette::mutedForeground());
        t.drawText(value, cx - t.measureText(value, 9.0, font::mono()) * 0.5, ly + 12.0, 9.0, font::mono());
    }

    void AnalogFace::paintSwitch(IRenderTarget &t, const Control &c, int i, const Rect &cell) const
    {
        const int n = (int)c.spec.choices.size();
        if (n <= 0) return;
        const double light = c.light.value(), hv = mHover.amount(i);
        for (int k = 0; k < n; ++k)
            drawRoundedRect(t, keyRect(i, k), radius::control(), Paint::filled(lerpColor(surface::knobCap(), palette::popover(), 0.5 * hv)));
        // the light slides from key to key (design rule §1)
        const Rect a = keyRect(i, 0), b = keyRect(i, std::min(n - 1, 1));
        const double step = n > 1 ? b.x - a.x : 0.0;
        const Rect lr{a.x + light * step, a.y, a.w, a.h};
        drawRoundedRect(t, lr, radius::control(), Paint::filledStroked(palette::primaryAlpha(0.18), palette::primary(), 1.0));
        for (int k = 0; k < n; ++k)
        {
            const Rect kr = keyRect(i, k);
            const double on = std::max(0.0, 1.0 - std::fabs(light - k));
            const Color ink = lerpColor(palette::mutedForeground(), palette::primary(), on);
            const Rect inner{kr.x + 6.5, kr.y + 5.5, kr.w - 13.0, kr.h - 11.0};
            if (!drawGlyph(t, c.spec.choices[(size_t)k], inner, ink))
            {
                const std::string s = textfit::ellipsize(t, c.spec.choices[(size_t)k], kr.w - 4.0, 9.0, font::sans());
                t.setFill(lerpColor(palette::mutedForeground(), palette::foreground(), on));
                t.drawText(s, kr.x + (kr.w - t.measureText(s, 9.0, font::sans())) * 0.5, textfit::baseline(kr.y + kr.h * 0.5, 9.0), 9.0, font::sans());
            }
        }
        // its label, and the chosen name
        const std::string chosen = c.choice >= 0 && c.choice < n ? c.spec.choices[(size_t)c.choice] : std::string();
        const std::string label = textfit::ellipsize(t, c.spec.label + " \xC2\xB7 " + chosen, cell.w, 10.0, font::sans());
        t.setFill(lerpColor(palette::mutedForeground(), palette::foreground(), hv));
        t.drawText(label, cell.x + (cell.w - t.measureText(label, 10.0, font::sans())) * 0.5, a.bottom() + 13.0, 10.0, font::sans());
    }

    void AnalogFace::paintScreen(IRenderTarget &t, const Section &s, int si, const Rect &r) const
    {
        drawRoundedRect(t, r, radius::control(), Paint::filledStroked(surface::screenBg(), palette::whiteAlpha(0.06), 1.0));
        t.save();
        t.clipRect(r.x + 1.0, r.y + 1.0, r.w - 2.0, r.h - 2.0);
        t.setStroke(surface::screenGrid(), 1.0);
        for (int k = 1; k < 4; ++k)
        {
            t.beginPath(); t.moveTo(r.x + r.w * k / 4.0, r.y); t.lineTo(r.x + r.w * k / 4.0, r.bottom()); t.strokePath();
        }
        t.beginPath(); t.moveTo(r.x, r.y + r.h * 0.5); t.lineTo(r.right(), r.y + r.h * 0.5); t.strokePath();
        const Rect in{r.x + 4.0, r.y + 5.0, r.w - 8.0, r.h - 10.0};
        auto value = [&](const char *suffix, double fallback) {
            const int i = find(s.prefix, suffix, si);
            return i >= 0 ? liveValue(i) : fallback;
        };
        auto curve = [&](const std::vector<Point> &pts, bool fill) {
            if (pts.size() < 2) return;
            if (fill)
            {
                t.setFill(palette::primaryAlpha(0.12));
                t.beginPath();
                t.moveTo(pts.front().x, in.bottom());
                for (const auto &p : pts) t.lineTo(p.x, p.y);
                t.lineTo(pts.back().x, in.bottom());
                t.closePath();
                t.fillPath();
            }
            t.setStroke(palette::primary(), 1.5);
            t.beginPath();
            t.moveTo(pts[0].x, pts[0].y);
            for (size_t k = 1; k < pts.size(); ++k) t.lineTo(pts[k].x, pts[k].y);
            t.strokePath();
        };
        if (s.screen == Screen::Wave)
        {
            // the oscillator: its wave (the switch's light morphs between shapes), its level, its pitch, its voices
            const int wi = find(s.prefix, "wave", si);
            const Control &w = mControls[(size_t)wi];
            const double light = w.light.value();
            const int a = std::clamp((int)std::floor(light), 0, (int)w.spec.choices.size() - 1), b = std::min(a + 1, (int)w.spec.choices.size() - 1);
            const double f = light - a, level = std::clamp(value("level", 1.0), 0.0, 1.0);
            const double cycles = std::clamp(2.0 * std::pow(2.0, value("octave", 0.0) * 0.5 + value("semi", 0.0) / 24.0), 1.0, 6.0);
            const int voices = std::max(1, (int)std::lround(value("voices", 1.0)));
            const double spread = value("detune", 0.0) / 100.0;
            for (int v = voices - 1; v >= 0; --v)
            {
                // a unison voice drifts by the detune: drawn behind the first, fainter
                const double off = voices > 1 ? (v - (voices - 1) * 0.5) * spread * 0.18 : 0.0;
                std::vector<Point> pts;
                for (int k = 0; k <= 160; ++k)
                {
                    const double p = (double)k / 160.0, ph = p * cycles + off;
                    const double y = (1.0 - f) * waveAt(w.spec.choices[(size_t)a], ph) + f * waveAt(w.spec.choices[(size_t)b], ph);
                    pts.push_back(Point{in.x + p * in.w, in.y + in.h * 0.5 - y * level * in.h * 0.45});
                }
                if (v == 0) curve(pts, false);
                else
                {
                    t.setStroke(palette::primaryAlpha(0.3), 1.0);
                    t.beginPath();
                    t.moveTo(pts[0].x, pts[0].y);
                    for (size_t k = 1; k < pts.size(); ++k) t.lineTo(pts[k].x, pts[k].y);
                    t.strokePath();
                }
            }
        }
        else if (s.screen == Screen::Filter)
        {
            // the filter: the DSP's own prototype (responseDb), its mode morphing as the switch's light slides;
            // where the envelope sweeps the cutoff, marked under it
            const int mi = find(s.prefix, "mode", si);
            const double fc = std::max(1.0, value("cutoff", 1000.0)), res = value("res", 0.0);
            constexpr double kLo = 20.0, kHi = 20000.0, kTop = 32.0, kBottom = -30.0;
            auto xOf = [&](double hz) { return in.x + in.w * std::log(hz / kLo) / std::log(kHi / kLo); };
            auto yOf = [&](double db) { return in.y + (kTop - std::clamp(db, kBottom, kTop)) / (kTop - kBottom) * in.h; };
            t.setStroke(palette::whiteAlpha(0.08), 1.0);
            t.beginPath(); t.moveTo(in.x, yOf(0.0)); t.lineTo(in.right(), yOf(0.0)); t.strokePath();
            std::vector<Point> pts;
            for (int k = 0; k <= 160; ++k)
            {
                const double hz = kLo * std::pow(kHi / kLo, (double)k / 160.0);
                double db = responseDb("lowpass", hz / fc, res);
                if (mi >= 0)
                {
                    const Control &m = mControls[(size_t)mi];
                    const double light = m.light.value();
                    const int a = std::clamp((int)std::floor(light), 0, (int)m.spec.choices.size() - 1), b = std::min(a + 1, (int)m.spec.choices.size() - 1);
                    const double f = light - a;
                    db = (1.0 - f) * responseDb(m.spec.choices[(size_t)a], hz / fc, res) + f * responseDb(m.spec.choices[(size_t)b], hz / fc, res);
                }
                pts.push_back(Point{xOf(hz), yOf(db)});
            }
            t.setStroke(palette::whiteAlpha(0.14), 1.0);
            t.beginPath(); t.moveTo(xOf(fc), in.y); t.lineTo(xOf(fc), in.bottom()); t.strokePath();
            curve(pts, true);
            if (find(s.prefix, "env", si) >= 0)
            {
                const double to = std::clamp(fc * std::pow(2.0, value("env", 0.0)), kLo, kHi), yb = in.bottom() - 2.0;
                t.setStroke(palette::primaryAlpha(0.6), 1.5);
                t.beginPath(); t.moveTo(xOf(fc), yb); t.lineTo(xOf(to), yb); t.strokePath();
                drawCircle(t, xOf(to), yb, 2.0, Paint::filled(palette::primary()));
            }
        }
        else if (s.screen == Screen::Envelope)
        {
            // the envelope: linear stages, as the library's ADSR; a stage's width grows with the root of its time
            const double A = std::max(0.0, value("attack", 0.0)), D = std::max(0.0, value("decay", 0.0)), S = std::clamp(value("sustain", 1.0), 0.0, 1.0),
                         Rl = std::max(0.0, value("release", 0.0));
            const double wa = std::sqrt(A + 1.0), wd = std::sqrt(D + 1.0), wr = std::sqrt(Rl + 1.0), hold = in.w * 0.22;
            const double scale = (in.w - hold) / std::max(1e-6, wa + wd + wr);
            const double x0 = in.x, x1 = x0 + wa * scale, x2 = x1 + wd * scale, x3 = x2 + hold, x4 = x3 + wr * scale;
            const double top = in.y + 2.0, bottom = in.bottom(), sy = bottom - S * (bottom - top);
            curve({Point{x0, bottom}, Point{x1, top}, Point{x2, sy}, Point{x3, sy}, Point{x4, bottom}}, true);
            t.setStroke(palette::whiteAlpha(0.12), 1.0);
            t.beginPath(); t.moveTo(x3, in.y); t.lineTo(x3, in.bottom()); t.strokePath(); // the note-off
        }
        t.restore();
    }

    void AnalogFace::onPaint(IRenderTarget &t) const
    {
        const double H = height.value();
        for (int si = 0; si < sectionCount(); ++si)
        {
            const Section &s = mSections[(size_t)si];
            const Rect box = sectionRect(si);
            if (box.bottom() < 0.0 || box.y > H) continue;
            drawRoundedRect(t, box, radius::control(), Paint::filledStroked(surface::moduleBg(), palette::border(), 1.0));
            // the module's title, a faceplate's: tracked capitals, a rule running on from it
            const std::string title = textfit::ellipsize(t, upper(s.title), box.w - 2.0 * kPad, 9.0, font::sansSemiBold());
            const double ty = box.y + kTitleH * 0.5;
            t.setFill(palette::mutedForeground());
            t.drawText(title, box.x + kPad, textfit::baseline(ty, 9.0), 9.0, font::sansSemiBold(), 0.13 * 9.0);
            const double tx = box.x + kPad + t.measureText(title, 9.0, font::sansSemiBold()) + (double)title.size() * 0.13 * 9.0 + 6.5;
            if (tx < box.right() - kPad)
            {
                t.setStroke(palette::whiteAlpha(0.1), 1.0);
                t.beginPath(); t.moveTo(tx, ty); t.lineTo(box.right() - kPad, ty); t.strokePath();
            }
            if (s.screen != Screen::None) paintScreen(t, s, si, Rect{box.x + kPad, box.y + kTitleH, box.w - 2.0 * kPad, kScreenH});
            for (int i : s.controls)
            {
                const Control &c = mControls[(size_t)i];
                const Rect cell = controlRect(i);
                if (cell.bottom() < 0.0 || cell.y > H) continue;
                if (c.knob) paintKnob(t, c, i, cell);
                else paintSwitch(t, c, i, cell);
            }
        }
        // R-UI-11: Show IDs — each address over its label
        const double ids = mOwner.idsShown();
        if (ids > 0.001)
            for (int i = 0; i < controlCount(); ++i)
            {
                const Rect cell = controlRect(i);
                if (cell.bottom() < 0.0 || cell.y > H) continue;
                const std::string address = mOwner.device() + "." + mControls[(size_t)i].spec.name;
                const double px = 8.0, w = std::min(cell.w + 8.0, t.measureText(address, px, font::mono()) + 8.0);
                const double y = mControls[(size_t)i].knob ? cell.y + 3.25 + kDial + 3.0 : cell.y + 3.25 + kKeysH + 3.0;
                const Rect b{cell.x + (cell.w - w) * 0.5, y, w, 13.0};
                drawRoundedRect(t, b, radius::control(), Paint::filledStroked(fade(palette::popover(), 0.97 * ids), palette::primaryAlpha(0.45 * ids), 1.0));
                t.setFill(fade(palette::primary(), ids));
                t.drawText(textfit::ellipsize(t, address, b.w - 6.0, px, font::mono()), b.x + 3.0, textfit::baseline(b.y + b.h * 0.5, px), px, font::mono());
            }
        // the face's own scroll bar, only when there is somewhere to scroll (R6)
        if (mContentH > H + 0.5)
        {
            const double th = std::max(18.0, H * H / mContentH), ty = (H - th) * (scroll / (mContentH - H));
            drawRoundedRect(t, Rect{width.value() - 5.0, ty, 3.0, th}, radius::pill(), Paint::filled(palette::whiteAlpha(0.18)));
        }
    }
}
}
