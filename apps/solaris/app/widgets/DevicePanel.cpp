#include "DevicePanel.h"
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

        std::string number(double v, int decimals)
        {
            char b[32];
            std::snprintf(b, sizeof b, "%.*f", decimals, v);
            std::string s = b;
            if (s == "-0" || s == "-0.0" || s == "-0.00") s = s.substr(1);
            return s;
        }
        /** The readout: engineering units, as few digits as say it. */
        std::string formatValue(const solaris::ParamModel &p, double v)
        {
            const double a = std::fabs(v);
            if (p.unit == "Hz") return a >= 1000.0 ? number(v / 1000.0, a >= 10000.0 ? 1 : 2) + " kHz" : number(v, a >= 100.0 ? 0 : 1) + " Hz";
            if (p.unit == "ms") return number(v, a >= 100.0 ? 0 : 1) + " ms";
            if (p.unit == "dB") return number(v, 1) + " dB";
            if (p.unit == "st" || p.unit == "ct" || p.unit == "oct")
                return (v > 0 ? "+" : "") + number(v, p.integer ? 0 : 1) + " " + p.unit;
            if (p.unit.empty() && !p.integer && p.min >= 0.0 && p.max <= 1.0) return number(v * 100.0, 0) + " %";
            return number(v, p.integer ? 0 : 2) + (p.unit.empty() ? "" : " " + p.unit);
        }
        /** What decides a bound parameter, said in the value column (R-WIN-2). */
        std::string bindingText(const std::string &formula)
        {
            std::string body = formula.size() > 1 ? formula.substr(1) : std::string();
            const auto a = body.find_first_not_of(' '), b = body.find_last_not_of(' ');
            body = a == std::string::npos ? std::string() : body.substr(a, b - a + 1);
            bool name = !body.empty();
            for (char c : body) name = name && (std::isalnum((unsigned char)c) || c == '_' || c == '.');
            if (name && body.rfind("au_", 0) == 0 && body.find('.') == std::string::npos) return "auto " + body; // an automation
            // a link, or a formula — cut to the value column (SliderRow right-aligns at ~6 px a character):
            // the whole formula is one `get` away, and its own menu shows it
            std::string out = "= " + body;
            constexpr size_t kChars = 14;
            if (out.size() > kChars) out = out.substr(0, kChars - 1) + "\xE2\x80\xA6";
            return out;
        }
        /** The text a `set` stores: enough digits that a drag lands where it was let go. */
        std::string storeText(double v, bool integer)
        {
            if (integer) return number(std::round(v), 0);
            char b[32];
            std::snprintf(b, sizeof b, "%.6g", v);
            return b;
        }
        std::string sectionOf(const std::string &name)
        {
            const auto dot = name.find('.');
            std::string s = dot == std::string::npos ? std::string() : name.substr(0, dot);
            for (auto &c : s) c = (char)std::toupper((unsigned char)c);
            return s;
        }
        bool logTaper(const solaris::ParamModel &p) { return p.logScale && p.min > 0.0 && p.max > p.min; }
        double toPos(const solaris::ParamModel &p, double v)
        {
            if (!logTaper(p)) return v;
            return std::log(std::max(v, p.min) / p.min) / std::log(p.max / p.min);
        }
        double fromPos(const solaris::ParamModel &p, double s)
        {
            double v = logTaper(p) ? p.min * std::pow(p.max / p.min, std::clamp(s, 0.0, 1.0)) : s;
            if (p.integer) v = std::round(v);
            return std::clamp(v, p.min, p.max);
        }
    }

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
        if (mOwner.empty()) mOwner = "Master";
        if (mRollPending)
            for (const auto &p : m.patterns)
                if (p.strip == mStrip && !mStrip.empty())
                {
                    mRollPending = false;
                    if (onOpenPattern) onOpenPattern(p.id);
                    break;
                }
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
        const double bodyH = std::max(0.0, H - kHeaderH);
        mScroll.setExtent(kHeaderH, bodyH, mBody->contentH);
        mBody->x.set(0.0);
        mBody->y.set(kHeaderH);
        mBody->width.set(W);
        mBody->height.set(bodyH);
        mBody->scroll = mScroll.value();
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
        mScroll.advance(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    int DevicePanel::rowCount() const { return (int)mBody->rows.size(); }

    std::string DevicePanel::rowParam(int i) const
    {
        return i >= 0 && i < rowCount() ? mBody->rows[(size_t)i].spec.name : std::string();
    }

    Rect DevicePanel::rowRect(int i) const
    {
        if (i < 0 || i >= rowCount()) return Rect{};
        return Rect{space::padX(), kHeaderH + mBody->rows[(size_t)i].top - mScroll.value(), width.value() - 2.0 * space::padX(),
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
        for (const auto &r : mBody->rows)
            if (r.spec.name == param) return r.lit.value();
        return 0.0;
    }

    void DevicePanel::reveal(const std::string &param)
    {
        for (const auto &r : mBody->rows)
            if (r.spec.name == param) mScroll.reveal(r.top, cosmo_v2::SliderRow::kRowHeight);
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
        if (d->sample.empty()) return "no sample \xE2\x80\x94 drop one here";
        const auto slash = d->sample.find_last_of('/');
        return "sample " + (slash == std::string::npos ? d->sample : d->sample.substr(slash + 1));
    }

    Rect DevicePanel::rollRect() const { return Rect{width.value() - space::padX() - 71.5, 8.125, 71.5, 19.5}; } // u(22): "Piano Roll" fits
    Rect DevicePanel::bypassRect() const
    {
        const solaris::DeviceModel *d = model();
        const Rect r = d && d->instrument ? rollRect() : removeRect(); // an instrument has no Remove; it has its notes
        return Rect{r.x - 6.5 - 55.25, r.y, 55.25, 19.5};
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
        if (!onMenu || i < 0 || i >= rowCount()) return;
        const ParamBody::Row &r = mBody->rows[(size_t)i];
        const std::string address = mDevice + "." + r.spec.name;
        std::vector<cosmo_v2::ContextMenu::Item> items;
        if (r.spec.choices.empty())
        {
            if (r.spec.formula.empty() || bindingText(r.spec.formula).rfind("auto ", 0) != 0)
                items.push_back({"Create Automation", [this, address] { if (onCommand) onCommand("auto create " + address); }});
            const std::string current = r.spec.formula.empty() ? std::string("=") : r.spec.formula;
            items.push_back({"Formula\xE2\x80\xA6", [this, address, current, world] {
                                 if (!onRename) return;
                                 onRename(current, world, [this, address](const std::string &typed) {
                                     std::string f = typed;
                                     if (f.empty()) return;
                                     if (f[0] != '=') f = "=" + f;
                                     if (onCommand) onCommand("set " + address + "=" + q(f));
                                 });
                             }});
            if (!r.spec.formula.empty()) items.push_back({"Clear Binding", [this, address] { if (onCommand) onCommand("bind clear " + address); }});
        }
        const std::string def = r.spec.choices.empty() ? storeText(r.spec.def, r.spec.integer)
                                                        : r.spec.choices[(size_t)std::clamp((int)std::lround(r.spec.def), 0, (int)r.spec.choices.size() - 1)];
        items.push_back({"Reset to Default", [this, address, def] { if (onCommand) onCommand("set " + address + "=" + def); }});
        onMenu(items, world);
    }

    bool DevicePanel::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            int h = -1;
            if (removeRect().contains(local)) h = 1;
            else if (bypassRect().contains(local)) h = 2;
            if (rollRect().contains(local)) h = 3;
            mHover.setHovered(h);
            return true;
        }
        case Gesture::Type::Click:
        {
            const solaris::DeviceModel *d = model();
            if (!d) return true;
            if (!d->instrument && removeRect().contains(local)) { if (onCommand) onCommand("device remove " + d->id); return true; }
            if (bypassRect().contains(local)) { if (onCommand) onCommand("set " + d->id + ".bypass=" + (d->bypass ? "false" : "true")); return true; }
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
        const Rect by = bypassRect();
        t.setFill(palette::mutedForeground());
        // a sampler names its sound where the others name their type (R-EDM-8)
        const std::string what = d->takesSample ? sampleText() : d->id + " \xC2\xB7 " + d->type;
        t.drawText(textfit::ellipsize(t, mOwner + " \xC2\xB7 " + what, std::max(0.0, by.x - 2.0 * space::padX()), 10.0, font::mono()),
                   space::padX(), textfit::baseline(by.y + by.h * 0.5, 10.0), 10.0, font::mono());
        // On / Bypassed: the fill eases whoever changed it
        const double b = mBypass.value();
        drawRoundedRect(t, by, radius::control(), Paint::filled(lerpColor(palette::primary(), palette::secondary(), b)));
        const std::string bl = b > 0.5 ? "Bypassed" : "On";
        t.setFill(lerpColor(palette::primaryForeground(), palette::secondaryForeground(), b));
        t.drawText(bl, by.x + (by.w - t.measureText(bl, 10.0, font::sans())) * 0.5, textfit::baseline(by.y + by.h * 0.5, 10.0), 10.0, font::sans());
        if (d->instrument)
        {
            const Rect rr = rollRect();
            drawRoundedRect(t, rr, radius::control(), Paint::filled(lerpColor(palette::secondary(), palette::popover(), mHover.amount(3))));
            t.setFill(palette::secondaryForeground());
            t.drawText("Piano Roll", rr.x + (rr.w - t.measureText("Piano Roll", 10.0, font::sans())) * 0.5, textfit::baseline(rr.y + rr.h * 0.5, 10.0), 10.0,
                       font::sans());
        }
        else
        {
            const Rect rm = removeRect();
            drawRoundedRect(t, rm, radius::control(), Paint::filled(palette::hoverWash(0.6 + mHover.amount(1))));
            t.setFill(lerpColor(palette::secondaryForeground(), palette::destructive(), mHover.amount(1)));
            t.drawText("Remove", rm.x + (rm.w - t.measureText("Remove", 10.0, font::sans())) * 0.5, textfit::baseline(rm.y + rm.h * 0.5, 10.0), 10.0, font::sans());
        }
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, kHeaderH - 0.5); t.lineTo(W, kHeaderH - 0.5); t.strokePath();
        // a sample hovering over a sampler: the window outlined in the accent, eased (R-EDM-8)
        if (mDrop.value() > 0.001)
            drawRoundedRect(t, Rect{1.0, 1.0, W - 2.0, height.value() - 2.0}, radius::control(),
                            Paint::stroked(palette::primaryAlpha(mDrop.value()), 2.0));
    }
}
}
