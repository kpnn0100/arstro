#include "DevicePanel.h"
#include "AnalogFace.h"
#include "ParamMenu.h"
#include "ParamFormat.h"
#include "../../../interstellar/app/widgets/Glyphs.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include "../../../cosmo/widgets/SliderRow.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;
    namespace glyph = interstellar_v1::glyph;

    namespace
    {
        constexpr double kRowStep = 22.75;     // space::u(7): a parameter row
        constexpr double kSectionH = 26.0;     // space::u(8): a section's header
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string q(const std::string &s) { return s.find_first_of(" \t\"") == std::string::npos && !s.empty() ? s : "\"" + s + "\""; }

        std::string sectionOf(const std::string &name)
        {
            const auto dot = name.find('.');
            std::string s = dot == std::string::npos ? std::string() : name.substr(0, dot);
            for (auto &c : s) c = (char)std::toupper((unsigned char)c);
            return s;
        }
    }
    using paramfmt::formatValue;
    using paramfmt::storeText;
    using paramfmt::logTaper;
    using paramfmt::toPos;
    using paramfmt::fromPos;

    /** The scrolled body: the rows (SliderRows as children), section headers, choice rows and the light. */
    class ParamBody : public Segment
    {
    public:
        struct Row
        {
            solaris::ParamModel spec;                    // spec.formula: what decides it ("" = its number)
            std::shared_ptr<cosmo_v2::SliderRow> slider; // null for a choice
            int choice = 0;
            double top = 0.0;
            AnimatedProperty lit{0.0};
            bool litLast = false, litPlaced = false;
        };
        std::vector<Row> rows;
        std::vector<std::pair<std::string, double>> sections;
        double contentH = 0.0, scroll = 0.0;
        std::string lastChanged;

        ParamBody() { clipToBounds = true; }
        Rect rowBox(const Row &r) const { return Rect{0.0, r.top - scroll - 1.0, width.value(), kRowStep}; }
        Rect choiceBox(const Row &r) const
        {
            const double x = space::padX() + cosmo_v2::SliderRow::kLabelWidth + 8.125;
            return Rect{x, r.top - scroll, std::max(0.0, width.value() - x - space::padX()), cosmo_v2::SliderRow::kRowHeight};
        }
        void advance(double nowMs) override
        {
            for (auto &r : rows)
            {
                const bool on = r.spec.name == lastChanged;
                if (!r.litPlaced) { r.lit.set(on ? 1.0 : 0.0); r.litLast = on; r.litPlaced = true; }
                if (on != r.litLast) { r.lit.animateTo(on ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); r.litLast = on; }
                r.lit.update(nowMs);
            }
            Segment::advance(nowMs);
        }

    protected:
        void onPaint(IRenderTarget &t) const override
        {
            const double W = width.value(), H = height.value();
            // the last change, lit — drawn first, so the row's controls sit on it
            for (const auto &r : rows)
            {
                const double l = r.lit.value();
                if (l <= 0.001) continue;
                const Rect b = rowBox(r);
                drawRoundedRect(t, b, 0.0, Paint::filled(palette::primaryAlpha(0.12 * l)));
                drawRoundedRect(t, Rect{0.0, b.y + 3.0, 2.0, b.h - 6.0}, radius::pill(), Paint::filled(palette::primaryAlpha(l)));
            }
            for (const auto &s : sections)
            {
                const double y = s.second - scroll;
                if (y + kSectionH < 0 || y > H) continue;
                t.setFill(palette::mutedForeground());
                t.drawText(s.first, space::padX(), textfit::baseline(y + kSectionH * 0.6, 9.0), 9.0, font::sansSemiBold(), 0.13 * 9.0);
            }
            for (const auto &r : rows)
            {
                if (r.slider) continue;
                const Rect b = choiceBox(r);
                if (b.bottom() < 0 || b.y > H) continue;
                const double cy = b.y + b.h * 0.5;
                t.setFill(palette::mutedForeground());
                t.drawText(textfit::ellipsize(t, r.spec.label, cosmo_v2::SliderRow::kLabelWidth - 4.0, 10.0, font::sans()), space::padX(),
                           textfit::baseline(cy, 10.0), 10.0, font::sans());
                drawRoundedRect(t, b, radius::control(), Paint::filled(palette::secondary()));
                const std::string name = r.choice >= 0 && r.choice < (int)r.spec.choices.size() ? r.spec.choices[(size_t)r.choice] : "?";
                const std::string shown = textfit::ellipsize(t, name, b.w - 30.0, 10.0, font::sans());
                t.setFill(palette::foreground());
                t.drawText(shown, b.x + (b.w - t.measureText(shown, 10.0, font::sans())) * 0.5, textfit::baseline(cy, 10.0), 10.0, font::sans());
                const Color ic = palette::mutedForeground();
                glyph::line(t, b.x + 10.0, cy - 3.5, b.x + 6.5, cy, ic, 1.2), glyph::line(t, b.x + 6.5, cy, b.x + 10.0, cy + 3.5, ic, 1.2);
                glyph::line(t, b.right() - 10.0, cy - 3.5, b.right() - 6.5, cy, ic, 1.2), glyph::line(t, b.right() - 6.5, cy, b.right() - 10.0, cy + 3.5, ic, 1.2);
            }
            // the body's own scroll bar, only when there is somewhere to scroll (R6)
            if (contentH > H + 0.5)
            {
                const double th = std::max(18.0, H * H / contentH), ty = (H - th) * (scroll / (contentH - H));
                drawRoundedRect(t, Rect{W - 5.0, ty, 3.0, th}, radius::pill(), Paint::filled(palette::whiteAlpha(0.18)));
            }
        }
        bool handleGesture(const Gesture &g, const Point &local) override
        {
            if (g.type != Gesture::Type::Click) return g.type == Gesture::Type::Down || g.type == Gesture::Type::Move;
            // a choice row: the left half steps back, the right half forward
            for (auto &r : rows)
            {
                if (r.slider || r.spec.choices.empty()) continue;
                const Rect b = choiceBox(r);
                if (!b.contains(local)) continue;
                const int n = (int)r.spec.choices.size();
                const int next = ((r.choice + (local.x < b.x + b.w * 0.5 ? -1 : 1)) % n + n) % n;
                if (owner && owner->onCommand) owner->onCommand("set " + owner->device() + "." + r.spec.name + "=" + r.spec.choices[(size_t)next]);
                return true;
            }
            return true;
        }

    public:
        DevicePanel *owner = nullptr;
    };

    /** R-UI-11: over the rows, each row's full address in the label column, faded in by Show IDs (eased). A
     *  child after the sliders, so it covers their labels; it takes no input. */
    class ParamIds : public Segment
    {
    public:
        DevicePanel *owner = nullptr;
        ParamBody *body = nullptr;

    protected:
        bool hitTestSelf(const Point &) const override { return false; }
        void onPaint(IRenderTarget &t) const override
        {
            const double a = owner ? owner->idsShown() : 0.0;
            if (a <= 0.001 || !body) return;
            const double H = height.value(), px = 8.5;
            for (const auto &r : body->rows)
            {
                const double y = r.top - body->scroll;
                if (y + kRowStep < 0.0 || y > H) continue;
                const std::string address = owner->device() + "." + r.spec.name;
                const double w = std::min(std::max(cosmo_v2::SliderRow::kLabelWidth, t.measureText(address, px, font::mono()) + 8.0),
                                          std::max(0.0, width.value() - 2.0 * space::padX() - DevicePanel::kValueW));
                const Rect b{space::padX() - 3.0, y + 2.0, w, cosmo_v2::SliderRow::kRowHeight - 4.0};
                drawRoundedRect(t, b, radius::control(), Paint::filledStroked(fade(palette::popover(), 0.97 * a), palette::primaryAlpha(0.45 * a), 1.0));
                t.setFill(fade(palette::primary(), a));
                t.drawText(textfit::ellipsize(t, address, b.w - 8.0, px, font::mono()), b.x + 4.0, textfit::baseline(b.y + b.h * 0.5, px), px, font::mono());
            }
        }
    };

    DevicePanel::DevicePanel(std::string deviceId) : mDevice(std::move(deviceId))
    {
        clipToBounds = true;
        mBody = std::make_shared<ParamBody>();
        mBody->owner = this;
        addChild(mBody);
    }

    const solaris::DeviceModel *DevicePanel::model() const
    {
        for (const auto &s : mModel.strips)
            for (const auto &d : s.devices)
                if (d.id == mDevice) return &d;
        for (const auto &d : mModel.masterDevices)
            if (d.id == mDevice) return &d;
        return nullptr;
    }

    void DevicePanel::build(const solaris::DeviceModel &d)
    {
        if (d.instrument && analogFace && d.plugin.empty()) // a plugin's face is its own editor (N26): here, its list
        {
            // R-UI-12: an instrument's face — the same generated parameters, as a hardware panel
            mFace = std::make_shared<AnalogFace>(*this);
            mFace->build(d);
            addChild(mFace);
            mBody->visible = false;
            mBuilt = true;
            return;
        }
        ParamBody &b = *mBody;
        double y = 6.5;
        std::string section = "\x01";
        for (const auto &p : d.params)
        {
            const std::string s = sectionOf(p.name);
            if (s != section)
            {
                section = s;
                if (!s.empty())
                {
                    b.sections.emplace_back(s, y);
                    y += kSectionH;
                }
            }
            ParamBody::Row r;
            r.spec = p;
            r.top = y;
            if (p.choices.empty())
            {
                const bool log = logTaper(p);
                r.slider = std::make_shared<cosmo_v2::SliderRow>(p.label, log ? 0.0 : p.min, log ? 1.0 : p.max, toPos(p, p.def)); // double-click: the default
                r.slider->setValueWidth(kValueW);
                const std::string dev = d.id, name = p.name;
                ParamBody *body = mBody.get();
                r.slider->formatValue = [body, name](double s) {
                    for (const auto &row : body->rows)
                        if (row.spec.name == name)
                            return row.spec.formula.empty() ? formatValue(row.spec, fromPos(row.spec, s)) : bindingText(row.spec.formula);
                    return std::string();
                };
                const solaris::ParamModel spec = p;
                r.slider->onChange = [this, spec, dev](double s) {
                    if (onCommand) onCommand("set " + dev + "." + spec.name + "=" + storeText(fromPos(spec, s), spec.integer));
                };
                r.slider->setValue(toPos(p, p.value));
                b.addChild(r.slider);
            }
            r.choice = (int)std::lround(p.value);
            b.rows.push_back(std::move(r));
            y += kRowStep;
        }
        b.contentH = y + 6.5;
        mIds = std::make_shared<ParamIds>(); // after the sliders: it draws over their labels
        mIds->owner = this;
        mIds->body = mBody.get();
        b.addChild(mIds);
        mBuilt = true;
    }

    void DevicePanel::bind(const solaris::AppModel &m, bool interacting)
    {
        mModel = m;
        mInteracting = interacting;
        const solaris::DeviceModel *d = model();
        mPresent = d != nullptr;
        if (!d || !mBuilt) return;               // built in `advance`, once the model has it
        mOwner.clear();
        mStrip.clear();
        for (const auto &s : m.strips)
            for (const auto &x : s.devices)
                if (x.id == mDevice) { mOwner = s.name; mStrip = s.id; }
        if (mOwner.empty()) mOwner = say("Master"); // words: shown where drawn
        if (mRollPending)
            for (const auto &p : m.patterns)
                if (p.strip == mStrip && !mStrip.empty())
                {
                    mRollPending = false;
                    if (onOpenPattern) onOpenPattern(p.id);
                    break;
                }
        if (mFace) mFace->bind(*d, m.bindings);
        mBody->lastChanged = d->lastChanged;
        for (auto &r : mBody->rows)
            for (const auto &p : d->params)
                if (p.name == r.spec.name)
                {
                    r.spec.formula = p.formula;
                    r.spec.value = p.value;
                    r.choice = (int)std::lround(p.value);
                    if (r.slider && !interacting) r.slider->setValue(toPos(p, p.value)); // a drag in flight outranks the model
                }
    }

    void DevicePanel::layout()
    {
        const double W = width.value(), H = height.value();
        const double bodyH = std::max(0.0, H - bodyTop());
        if (mFace)
        {
            // the face flows its modules into the width; the panel scrolls it (R6)
            mScroll.setExtent(bodyTop(), bodyH, mFace->layoutFor(W));
            mFace->x.set(0.0);
            mFace->y.set(bodyTop());
            mFace->width.set(W);
            mFace->height.set(bodyH);
            mFace->scroll = mScroll.value();
            return;
        }
        mScroll.setExtent(bodyTop(), bodyH, mBody->contentH);
        mBody->x.set(0.0);
        mBody->y.set(bodyTop());
        mBody->width.set(W);
        mBody->height.set(bodyH);
        mBody->scroll = mScroll.value();
        if (mIds)
        {
            mIds->x.set(0.0);
            mIds->y.set(0.0);
            mIds->width.set(W);
            mIds->height.set(bodyH);
        }
        for (auto &r : mBody->rows)
        {
            if (!r.slider) continue;
            const double y = r.top - mBody->scroll;
            r.slider->visible = y + cosmo_v2::SliderRow::kRowHeight > 0.0 && y < bodyH; // cull what is scrolled out
            r.slider->x.set(space::padX());
            r.slider->y.set(y);
            r.slider->width.set(std::max(0.0, W - 2.0 * space::padX()));
            r.slider->layout();
        }
    }

    void DevicePanel::advance(double nowMs)
    {
        if (!mBuilt)
            if (const solaris::DeviceModel *d = model())
            {
                build(*d);
                bind(mModel, mInteracting);
            }
        const solaris::DeviceModel *d = model();
        const bool by = d && d->bypass;
        if (!mBypassInit) { mBypass.set(by ? 1.0 : 0.0); mBypassLast = by; mBypassInit = true; }
        else if (by != mBypassLast) { mBypass.animateTo(by ? 1.0 : 0.0, motion::kSelectMs, Easing::EaseOutCubic, nowMs); mBypassLast = by; }
        mBypass.update(nowMs);
        if (mDropWant != mDropLast)
        {
            mDrop.animateTo(mDropWant ? 1.0 : 0.0, motion::kHoverMs, Easing::EaseOutCubic, nowMs);
            mDropLast = mDropWant;
        }
        mDrop.update(nowMs);
        // R-VST-6 (N26): a plugin's offline band and its Editor light, eased whoever changed them
        const bool off = d && d->offline, ed = d && d->editor;
        if (!mPluginInit && d)
        {
            mOffline.set(off ? 1.0 : 0.0);
            mEditorLit.set(ed ? 1.0 : 0.0);
            mOfflineLast = off;
            mEditorLast = ed;
            mPluginInit = true;
        }
        if (off != mOfflineLast) { mOffline.animateTo(off ? 1.0 : 0.0, motion::kSelectMs, Easing::EaseOutCubic, nowMs); mOfflineLast = off; }
        if (ed != mEditorLast) { mEditorLit.animateTo(ed ? 1.0 : 0.0, motion::kSelectMs, Easing::EaseOutCubic, nowMs); mEditorLast = ed; }
        mOffline.update(nowMs);
        mEditorLit.update(nowMs);
        // R-FX-17: a measuring device's needle and its pitched light — a value catching up to a source it does not
        // control, eased however often the readings move
        if (d && !d->readings.empty())
        {
            double cents = 0.0, hz = 0.0;
            for (const auto &rd : d->readings)
            {
                if (rd.name == "cents") cents = rd.value;
                if (rd.name == "frequency") hz = rd.value;
            }
            const bool pitched = hz > 0.0;
            const double target = pitched ? std::clamp(cents, -50.0, 50.0) : 0.0;
            if (!mReadInit)
            {
                mNeedle.set(target);
                mPitched.set(pitched ? 1.0 : 0.0);
                mNeedleLast = target;
                mPitchedLast = pitched;
                mReadInit = true;
            }
            if (target != mNeedleLast) { mNeedle.animateTo(target, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs); mNeedleLast = target; }
            if (pitched != mPitchedLast) { mPitched.animateTo(pitched ? 1.0 : 0.0, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs); mPitchedLast = pitched; }
        }
        mNeedle.update(nowMs);
        mPitched.update(nowMs);
        mScroll.advance(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    void DevicePanel::accessibles(std::vector<A11yNode> &out)
    {
        const solaris::DeviceModel *d = model();
        if (!d) return;
        for (int i = 0; i < rowCount(); ++i)
        {
            const Rect r = rowRect(i);
            if (r.w <= 0 || r.bottom() <= bodyTop() || r.y >= height.value()) continue; // scrolled out
            const std::string param = rowParam(i);
            const solaris::ParamModel *spec = nullptr;
            for (const auto &p : d->params)
                if (p.name == param) spec = &p;
            if (!spec) continue;
            A11yNode n = a11yNode("device." + mDevice + "." + param, spec->choices.empty() ? "slider" : "combo", spec->label.empty() ? param : spec->label,
                                  worldRect(*this, r), readout(param));
            const solaris::ParamModel p = *spec;
            n.step = [this, p](int dir, bool fine) {
                double v = p.value;
                if (!p.choices.empty()) // a choice: the next one, by its name
                {
                    const int c = std::clamp((int)std::lround(v) + dir, 0, (int)p.choices.size() - 1);
                    if (onCommand) onCommand("set " + mDevice + "." + p.name + "=" + p.choices[(size_t)c]);
                    return;
                }
                if (p.integer) v += dir;
                else if (p.logScale && p.min > 0) v *= std::pow(p.max / p.min, dir * (fine ? 0.001 : 0.01));
                else v += dir * (p.max - p.min) * (fine ? 0.001 : 0.01);
                v = std::clamp(v, p.min, p.max);
                char b[32];
                std::snprintf(b, sizeof b, "%.6g", v);
                if (onCommand) onCommand("set " + mDevice + "." + p.name + "=" + b);
            };
            if (!p.choices.empty()) n.activate = [step = n.step]() { step(+1, false); }; // a combo: Enter takes the next choice
            out.push_back(n);
        }
    }

    int DevicePanel::rowCount() const { return mFace ? mFace->controlCount() : (int)mBody->rows.size(); }

    std::string DevicePanel::rowParam(int i) const
    {
        if (mFace) return mFace->controlParam(i);
        return i >= 0 && i < rowCount() ? mBody->rows[(size_t)i].spec.name : std::string();
    }

    Rect DevicePanel::rowRect(int i) const
    {
        if (mFace)
        {
            Rect r = mFace->controlRect(i); // a knob's cell or a switch's row, in the panel's coordinates
            r.y += bodyTop();
            return r;
        }
        if (i < 0 || i >= rowCount()) return Rect{};
        return Rect{space::padX(), bodyTop() + mBody->rows[(size_t)i].top - mScroll.value(), width.value() - 2.0 * space::padX(),
                    cosmo_v2::SliderRow::kRowHeight};
    }

    cosmo_v2::SliderRow *DevicePanel::slider(const std::string &param) const
    {
        for (const auto &r : mBody->rows)
            if (r.spec.name == param) return r.slider.get();
        return nullptr;
    }

    std::string DevicePanel::readout(const std::string &param) const
    {
        if (mFace) return mFace->valueText(param);
        for (const auto &r : mBody->rows)
            if (r.spec.name == param)
            {
                if (!r.spec.formula.empty()) return bindingText(r.spec.formula);
                if (!r.spec.choices.empty()) return r.choice >= 0 && r.choice < (int)r.spec.choices.size() ? r.spec.choices[(size_t)r.choice] : "";
                return formatValue(r.spec, r.spec.value);
            }
        return std::string();
    }

    double DevicePanel::litAmount(const std::string &param) const
    {
        if (mFace) return mFace->litAmount(param);
        for (const auto &r : mBody->rows)
            if (r.spec.name == param) return r.lit.value();
        return 0.0;
    }

    void DevicePanel::reveal(const std::string &param)
    {
        if (mFace)
        {
            const int i = mFace->controlIndex(param);
            if (i >= 0) mScroll.reveal(mFace->contentTop(i), AnalogFace::kCellH);
            return;
        }
        for (const auto &r : mBody->rows)
            if (r.spec.name == param) mScroll.reveal(r.top, cosmo_v2::SliderRow::kRowHeight);
    }

    void DevicePanel::revealGroup(const std::string &prefix)
    {
        if (mFace)
        {
            // the module, at the top
            const double top = mFace->sectionTop(prefix);
            if (top >= 0.0) mScroll.reveal(std::max(0.0, top - AnalogFace::kPad), std::max(0.0, height.value() - bodyTop()));
            return;
        }
        // the group's section header, at the top: the group's first row, and the header just above it
        for (const auto &r : mBody->rows)
            if (r.spec.name.rfind(prefix + ".", 0) == 0)
            {
                double top = r.top;
                for (const auto &s : mBody->sections)
                    if (s.second <= r.top) top = s.second;
                mScroll.reveal(top, std::max(0.0, height.value() - bodyTop()));
                return;
            }
    }

    double DevicePanel::readingsH() const
    {
        const solaris::DeviceModel *d = model();
        return d && !d->readings.empty() ? kReadingsH : 0.0;
    }

    Rect DevicePanel::readingsRect() const
    {
        return Rect{0.0, kHeaderH + band + mOffline.value() * kOfflineH, width.value(), readingsH()};
    }

    namespace
    {
        const solaris::ReadingModel *readingOf(const solaris::DeviceModel *d, const char *name)
        {
            if (d)
                for (const auto &r : d->readings)
                    if (r.name == name) return &r;
            return nullptr;
        }
        std::string fixed(double v, int places, bool sign = false)
        {
            char b[48];
            std::snprintf(b, sizeof b, sign ? "%+.*f" : "%.*f", places, v);
            std::string s = b;
            if (!s.empty() && s[0] == '-') s = "\xE2\x88\x92" + s.substr(1); // a true minus
            return s;
        }
    }

    std::string DevicePanel::noteText() const
    {
        const auto *hz = readingOf(model(), "frequency"), *note = readingOf(model(), "note");
        if (!hz || hz->value <= 0.0 || !note) return "\xE2\x80\x94"; // no pitch: a dash
        static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        const int n = (int)std::lround(note->value);
        return std::string(names[((n % 12) + 12) % 12]) + std::to_string(n / 12 - 1);
    }

    std::string DevicePanel::readingsText() const
    {
        const solaris::DeviceModel *d = model();
        if (!d) return {};
        const auto *hz = readingOf(d, "frequency");
        std::string s;
        auto add = [&](const std::string &part) { s += (s.empty() ? "" : " \xC2\xB7 ") + part; };
        if (hz && hz->value <= 0.0) add(tr("no pitch"));
        for (const auto &r : d->readings)
        {
            if (r.name == "note") continue;                                   // the large text says it
            if ((r.name == "frequency" || r.name == "cents") && hz && hz->value <= 0.0) continue;
            if (r.name == "frequency") add(fixed(r.value, 1) + " Hz");
            else if (r.name == "cents")
            {
                const double c = std::round(r.value) + 0.0; // + 0.0: no "−0"
                add((c == 0.0 ? std::string("0") : fixed(c, 0, true)) + " ct");
            }
            else add(fixed(r.value, 1) + (r.unit.empty() ? "" : " " + r.unit));
        }
        return s;
    }

    Rect DevicePanel::removeRect() const { return Rect{width.value() - space::padX() - 55.25, 8.125, 55.25, 19.5}; }
    bool DevicePanel::takesSample() const
    {
        const solaris::DeviceModel *d = model();
        return d && d->takesSample;
    }

    std::string DevicePanel::sampleText() const
    {
        const solaris::DeviceModel *d = model();
        if (!d || !d->takesSample) return std::string();
        if (d->sample.empty()) return d->takesDefinition ? tr("no instrument \xE2\x80\x94 drop an .sfz here") : tr("no sample \xE2\x80\x94 drop one here");
        const auto slash = d->sample.find_last_of('/');
        return fillIn(d->takesDefinition ? tr("instrument {1}") : tr("sample {1}"), slash == std::string::npos ? d->sample : d->sample.substr(slash + 1));
    }

    Rect DevicePanel::rollRect() const { return Rect{width.value() - space::padX() - 71.5, 8.125, 71.5, 19.5}; } // u(22): "Piano Roll" fits
    Rect DevicePanel::bypassRect() const
    {
        const solaris::DeviceModel *d = model();
        const Rect r = d && d->instrument ? rollRect() : removeRect(); // an instrument has no Remove; it has its notes
        return Rect{r.x - 6.5 - 55.25, r.y, 55.25, 19.5};
    }

    Rect DevicePanel::presetsRect() const
    {
        const Rect b = bypassRect();
        return Rect{b.x - 6.5 - 55.25, b.y, 55.25, 19.5};
    }

    bool DevicePanel::isPlugin() const
    {
        const solaris::DeviceModel *d = model();
        return d && !d->plugin.empty();
    }
    Rect DevicePanel::editorRect() const { return presetsRect(); } // a plugin's presets are its own: its editor stands there
    Rect DevicePanel::reloadRect() const
    {
        return Rect{width.value() - space::padX() - 55.25, kHeaderH + band + (kOfflineH - 19.5) * 0.5, 55.25, 19.5};
    }

    bool DevicePanel::contextClick(Point world)
    {
        const Point local = toLocal(world);
        if (!localBounds().contains(local)) return false;
        for (int i = 0; i < rowCount(); ++i)
            if (rowRect(i).contains(local)) { openParamMenu(i, world); return true; }
        return false;
    }

    void DevicePanel::openParamMenu(int i, Point world)
    {
        if (i < 0 || i >= rowCount()) return;
        if (mFace)
        {
            const int k = mFace->controlIndex(mFace->controlParam(i));
            const solaris::DeviceModel *d = model();
            if (!d || k < 0) return;
            for (const auto &p : d->params)
                if (p.name == mFace->controlParam(i)) openParamMenuFor(p, (int)std::lround(p.value), world);
            return;
        }
        const ParamBody::Row &r = mBody->rows[(size_t)i];
        openParamMenuFor(r.spec, r.choice, world);
    }

    void DevicePanel::openParamMenuFor(const solaris::ParamModel &spec, int choice, Point world)
    {
        if (!onMenu) return;
        // the shared menu (R-WIN-3, R-UI-11): this row or knob says what it is, ParamMenu says what can be done
        ParamTarget p;
        p.address = mDevice + "." + spec.name;
        p.formula = spec.formula;
        p.bindable = spec.choices.empty();
        p.controls = controlsFor(mModel, p.address); // R-CTRL-1
        if (p.bindable)
        {
            double v = spec.value;
            for (const auto &b : mModel.bindings) // a bound value: what it evaluates to now (R-MIX-16)
                if (b.address == p.address) v = b.live;
            p.value = formatValue(spec, v);
            p.reset = storeText(spec.def, spec.integer);
        }
        else
        {
            const int n = (int)spec.choices.size();
            p.value = choice >= 0 && choice < n ? spec.choices[(size_t)choice] : std::string();
            p.reset = spec.choices[(size_t)std::clamp((int)std::lround(spec.def), 0, n - 1)];
        }
        PropertyHooks h;
        h.command = [this](const std::string &l) { return onCommand ? onCommand(l) : false; };
        h.rename = [this](const std::string &cur, Point at, std::function<void(const std::string &)> done) { if (onRename) onRename(cur, at, std::move(done)); };
        h.copy = [this](const std::string &text) { if (onCopy) onCopy(text); };
        onMenu(paramMenuItems(p, h, world), world);
    }

    bool DevicePanel::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            int h = -1;
            if (!songControls) { mHover.setHovered(-1); return true; }
            if (removeRect().contains(local)) h = 1;
            else if (bypassRect().contains(local)) h = 2;
            else if (presetsRect().contains(local)) h = 4;
            if (mOffline.value() > 0.5 && reloadRect().contains(local)) h = 5;
            if (rollRect().contains(local)) h = 3;
            mHover.setHovered(h);
            return true;
        }
        case Gesture::Type::Click:
        {
            const solaris::DeviceModel *d = model();
            if (!d || !songControls) return true;
            if (!d->plugin.empty() && d->offline && reloadRect().contains(local)) { if (onCommand) onCommand("device reload " + d->id); return true; }
            if (!d->plugin.empty() && editorRect().contains(local))
            {
                // its own editor, in a window its process makes — or closed, when it is open (N26)
                if (onCommand) onCommand("device editor " + d->id + (d->editor ? " --close" : ""));
                return true;
            }
            if (!d->instrument && removeRect().contains(local)) { if (onCommand) onCommand("device remove " + d->id); return true; }
            if (bypassRect().contains(local)) { if (onCommand) onCommand("set " + d->id + ".bypass=" + (d->bypass ? "false" : "true")); return true; }
            if (presetsRect().contains(local) && onMenu)
            {
                // R-PRESET-1: its type's presets — each ONE `preset load` — and its values saved as one, named
                std::vector<cosmo_v2::ContextMenu::Item> items;
                const std::string dv = d->id;
                for (const auto &x : mModel.presets)
                    if (x.type == d->type)
                    {
                        const std::string name = x.name;
                        items.push_back({name + (x.factory ? "  \xC2\xB7 factory" : ""), [this, dv, name] { if (onCommand) onCommand("preset load " + dv + " \"" + name + "\""); }});
                    }
                const Point world = g.pos;
                items.push_back({say("Save as preset\xE2\x80\xA6"), [this, dv, world] {
                                     if (onRename) onRename("", world, [this, dv](const std::string &n) { if (onCommand && !n.empty()) onCommand("preset save " + dv + " \"" + n + "\""); });
                                 }});
                onMenu(std::move(items), g.pos);
                return true;
            }
            if (d->instrument && rollRect().contains(local) && !mStrip.empty())
            {
                // the strip's pattern; a menu when it plays several; a new clip (and pattern) when it has none
                std::vector<cosmo_v2::ContextMenu::Item> items;
                for (const auto &p : mModel.patterns)
                    if (p.strip == mStrip)
                    {
                        const std::string id = p.id;
                        items.push_back({p.name.empty() ? id : p.name, [this, id] { if (onOpenPattern) onOpenPattern(id); }});
                    }
                if (items.size() == 1) items[0].action();
                else if (items.size() > 1 && onMenu) onMenu(std::move(items), g.pos);
                else if (items.empty() && onCommand && onCommand("clip add --strip " + mStrip)) mRollPending = true;
                return true;
            }
            return true;
        }
        case Gesture::Type::RightClick:
            for (int i = 0; i < rowCount(); ++i)
                if (rowRect(i).contains(local)) { openParamMenu(i, g.pos); return true; }
            return true;
        case Gesture::Type::Scroll:
            mScroll.scrollBy(g.delta.y);
            return true;
        default:
            return true; // the panel is a surface: nothing falls through it
        }
    }

    void DevicePanel::onPaint(IRenderTarget &t) const
    {
        const double W = width.value();
        const solaris::DeviceModel *d = model();
        if (!d) return;
        if (!songControls)
        {
            // a plugin's own editor (R-VST-7): the instrument named as a window title, the suite's mark at the right
            const double cy = kHeaderH * 0.5;
            t.setFill(palette::foreground());
            const double mark = t.measureText("arstro", 10.0, font::mono());
            t.drawText(textfit::ellipsize(t, d->label, std::max(0.0, W - mark - 4.0 * space::padX()), 13.0, font::sansSemiBold()),
                       space::padX(), textfit::baseline(cy, 13.0), 13.0, font::sansSemiBold());
            t.setFill(palette::mutedForeground());
            t.drawText("arstro", W - space::padX() - mark - 7.0, textfit::baseline(cy, 10.0), 10.0, font::mono());
            drawCircle(t, W - space::padX() - 2.5, textfit::baseline(cy, 10.0) - 2.0, 2.0, Paint::filled(palette::primary())); // the accent dot
            t.setStroke(palette::border(), 1.0);
            t.beginPath(); t.moveTo(0, kHeaderH - 0.5); t.lineTo(W, kHeaderH - 0.5); t.strokePath();
            return;
        }
        const Rect by = bypassRect(), pr = presetsRect();
        const bool plugin = !d->plugin.empty();
        t.setFill(palette::mutedForeground());
        // a sampler names its sound where the others name their type (R-EDM-8); a plugin, itself (R-VST-6)
        const std::string what = d->takesSample ? sampleText() : d->id + " \xC2\xB7 " + (plugin ? d->label : d->type);
        t.drawText(textfit::ellipsize(t, ::arstro::solaris_ui::shown(mOwner) + " \xC2\xB7 " + what, std::max(0.0, pr.x - 2.0 * space::padX()), 10.0, font::mono()),
                   space::padX(), textfit::baseline(by.y + by.h * 0.5, 10.0), 10.0, font::mono());
        if (plugin)
        {
            // N26: Editor — its own window; lit while it is open
            const double e = mEditorLit.value();
            drawRoundedRect(t, pr, radius::control(),
                            Paint::filled(lerpColor(lerpColor(palette::secondary(), palette::popover(), mHover.amount(4)), palette::primary(), e)));
            t.setFill(lerpColor(palette::secondaryForeground(), palette::primaryForeground(), e));
            const std::string ed = textfit::ellipsize(t, tr("Editor"), pr.w - 4.0, 10.0, font::sans());
            t.drawText(ed, pr.x + (pr.w - t.measureText(ed, 10.0, font::sans())) * 0.5, textfit::baseline(pr.y + pr.h * 0.5, 10.0), 10.0, font::sans());
        }
        else
        {
            // R-PRESET-1: Presets — a menu of its type's, and Save as…
            drawRoundedRect(t, pr, radius::control(), Paint::filled(lerpColor(palette::secondary(), palette::popover(), mHover.amount(4))));
            t.setFill(palette::secondaryForeground());
            const std::string ps = textfit::ellipsize(t, tr("Presets"), pr.w - 4.0, 10.0, font::sans());
            t.drawText(ps, pr.x + (pr.w - t.measureText(ps, 10.0, font::sans())) * 0.5, textfit::baseline(pr.y + pr.h * 0.5, 10.0), 10.0, font::sans());
        }
        // On / Bypassed: the fill eases whoever changed it
        const double b = mBypass.value();
        drawRoundedRect(t, by, radius::control(), Paint::filled(lerpColor(palette::primary(), palette::secondary(), b)));
        const std::string bl = textfit::ellipsize(t, b > 0.5 ? tr("Bypassed") : tr("On"), by.w - 4.0, 10.0, font::sans());
        t.setFill(lerpColor(palette::primaryForeground(), palette::secondaryForeground(), b));
        t.drawText(bl, by.x + (by.w - t.measureText(bl, 10.0, font::sans())) * 0.5, textfit::baseline(by.y + by.h * 0.5, 10.0), 10.0, font::sans());
        if (d->instrument)
        {
            const Rect rr = rollRect();
            drawRoundedRect(t, rr, radius::control(), Paint::filled(lerpColor(palette::secondary(), palette::popover(), mHover.amount(3))));
            t.setFill(palette::secondaryForeground());
            const std::string roll = textfit::ellipsize(t, tr("Piano Roll"), rr.w - 4.0, 10.0, font::sans());
            t.drawText(roll, rr.x + (rr.w - t.measureText(roll, 10.0, font::sans())) * 0.5, textfit::baseline(rr.y + rr.h * 0.5, 10.0), 10.0, font::sans());
        }
        else
        {
            const Rect rm = removeRect();
            drawRoundedRect(t, rm, radius::control(), Paint::filled(palette::hoverWash(0.6 + mHover.amount(1))));
            t.setFill(lerpColor(palette::secondaryForeground(), palette::destructive(), mHover.amount(1)));
            const std::string rem = textfit::ellipsize(t, tr("Remove"), rm.w - 4.0, 10.0, font::sans());
            t.drawText(rem, rm.x + (rm.w - t.measureText(rem, 10.0, font::sans())) * 0.5, textfit::baseline(rm.y + rm.h * 0.5, 10.0), 10.0, font::sans());
        }
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, kHeaderH - 0.5); t.lineTo(W, kHeaderH - 0.5); t.strokePath();
        // R-VST-6 (N26): a plugin not playing — the band slides open under the header: why, and Reload
        const double off = mOffline.value();
        if (off > 0.001)
        {
            const double top = kHeaderH + band, bh = off * kOfflineH;
            t.save();
            t.clipRect(0, top, W, bh);
            drawRoundedRect(t, Rect{0, top, W, kOfflineH}, 0.0, Paint::filled(lerpColor(palette::card(), palette::destructive(), 0.16)));
            const Rect rl = reloadRect();
            const double cy = top + bh - kOfflineH * 0.5; // the content slides in with the band's bottom edge
            const double shift = cy - (rl.y + rl.h * 0.5);
            drawRoundedRect(t, Rect{rl.x, rl.y + shift, rl.w, rl.h}, radius::control(),
                            Paint::filled(lerpColor(palette::secondary(), palette::popover(), mHover.amount(5))));
            t.setFill(palette::secondaryForeground());
            const std::string rl2 = textfit::ellipsize(t, tr("Reload"), rl.w - 4.0, 10.0, font::sans());
            t.drawText(rl2, rl.x + (rl.w - t.measureText(rl2, 10.0, font::sans())) * 0.5, textfit::baseline(cy, 10.0), 10.0, font::sans());
            t.setFill(palette::destructive());
            const std::string why = fillIn(tr("Offline \xE2\x80\x94 {1}"), d->why.empty() ? tr("not playing") : d->why); // the service's reason, as it is
            t.drawText(textfit::ellipsize(t, why, std::max(0.0, rl.x - 2.0 * space::padX()), 10.0, font::sans()), space::padX(),
                       textfit::baseline(cy, 10.0), 10.0, font::sans());
            t.restore();
        }
        // R-FX-17 (Round 12): a device that measures — its band: the note large, a needle across −50 … +50 cents, the rest
        if (readingsH() > 0.0)
        {
            const Rect band = readingsRect();
            drawRoundedRect(t, band, 0.0, Paint::filled(surface::screenBg()));
            const double p = mPitched.value(), noteW = space::u(22); // u(22): "A#4" at 26 px with room
            // the note: a display read across a room — 26 px, off the editor's ramp on purpose, as the analog face's screens
            t.setFill(lerpColor(palette::mutedForeground(), palette::foreground(), p));
            t.drawText(noteText(), space::padX(), textfit::baseline(band.y + band.h * 0.42, 26.0), 26.0, font::sansSemiBold());
            // the needle's track, its ticks at −50, −25, 0, +25, +50
            const double x0 = space::padX() + noteW, x1 = std::max(x0, W - space::padX()), cy = band.y + band.h * 0.42;
            auto at = [&](double ct) { return x0 + (x1 - x0) * (ct + 50.0) / 100.0; };
            t.setStroke(surface::knobTrack(), 1.0);
            t.beginPath(); t.moveTo(x0, cy); t.lineTo(x1, cy); t.strokePath();
            for (double ct : {-50.0, -25.0, 0.0, 25.0, 50.0})
            {
                const double half = ct == 0.0 ? space::u(3) : space::u(1.5);
                t.beginPath(); t.moveTo(at(ct), cy - half); t.lineTo(at(ct), cy + half); t.strokePath();
            }
            // the needle: the accent, greener the nearer the centre (in tune) — from the EASED value, so never a jump
            const double n = mNeedle.value(), inTune = std::clamp(1.0 - std::fabs(n) / 5.0, 0.0, 1.0);
            const artboard::Color nc = lerpColor(palette::primary(), palette::success(), inTune);
            t.setStroke(artboard::Color{nc.r, nc.g, nc.b, nc.a * (0.25 + 0.75 * p)}, 2.0);
            t.beginPath(); t.moveTo(at(n), cy - space::u(4)); t.lineTo(at(n), cy + space::u(4)); t.strokePath();
            // the rest, small
            t.setFill(palette::mutedForeground());
            t.drawText(textfit::ellipsize(t, readingsText(), std::max(0.0, W - 2.0 * space::padX()), 10.0, font::mono()), space::padX(),
                       textfit::baseline(band.y + band.h - space::u(3.5), 10.0), 10.0, font::mono());
            t.setStroke(palette::border(), 1.0);
            t.beginPath(); t.moveTo(0, band.y + band.h - 0.5); t.lineTo(W, band.y + band.h - 0.5); t.strokePath();
        }
        // a sample hovering over a sampler: the window outlined in the accent, eased (R-EDM-8)
        if (mDrop.value() > 0.001)
            drawRoundedRect(t, Rect{1.0, 1.0, W - 2.0, height.value() - 2.0}, radius::control(),
                            Paint::stroked(palette::primaryAlpha(mDrop.value()), 2.0));
    }
}
}
