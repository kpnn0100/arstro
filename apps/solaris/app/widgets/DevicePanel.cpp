#include "DevicePanel.h"
#include "../../../interstellar/app/widgets/FadePage.h"
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
        constexpr double kValueW = 52.0;       // space::u(16): "12.5 kHz" in mono 10

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
    }

    /** One device's rows: SliderRows as children, section headers and choice rows drawn here. */
    class ParamPage : public interstellar_v1::FadePage
    {
    public:
        struct Row
        {
            solaris::ParamModel spec;
            std::shared_ptr<cosmo_v2::SliderRow> slider; // null for a choice
            int choice = 0;
            double top = 0.0;
        };
        std::vector<Row> rows;
        std::vector<std::pair<std::string, double>> sections; // header, top
        double contentH = 0.0, scroll = 0.0;

        bool logTaper(const solaris::ParamModel &p) const { return p.logScale && p.min > 0.0 && p.max > p.min; }
        double toPos(const solaris::ParamModel &p, double v) const
        {
            if (!logTaper(p)) return v;
            return std::log(std::max(v, p.min) / p.min) / std::log(p.max / p.min);
        }
        double fromPos(const solaris::ParamModel &p, double s) const
        {
            double v = logTaper(p) ? p.min * std::pow(p.max / p.min, std::clamp(s, 0.0, 1.0)) : s;
            if (p.integer) v = std::round(v);
            return std::clamp(v, p.min, p.max);
        }
        Rect choiceBox(const Row &r, double w) const
        {
            const double x = space::padX() + cosmo_v2::SliderRow::kLabelWidth + 8.125;
            return Rect{x, r.top - scroll, std::max(0.0, w - x - space::padX()), cosmo_v2::SliderRow::kRowHeight};
        }

    protected:
        void onPaint(IRenderTarget &t) const override
        {
            const double W = width.value();
            for (const auto &s : sections)
            {
                const double y = s.second - scroll;
                if (y + kSectionH < 0 || y > height.value()) continue;
                t.setFill(palette::mutedForeground());
                t.drawText(s.first, space::padX(), textfit::baseline(y + kSectionH * 0.6, 9.0), 9.0, font::sansSemiBold(), 0.13 * 9.0);
            }
            for (const auto &r : rows)
            {
                if (r.slider) continue;
                const Rect b = choiceBox(r, W);
                if (b.bottom() < 0 || b.y > height.value()) continue;
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
        }
    };

    DevicePanel::DevicePanel()
    {
        clipToBounds = true;
        opacity.set(0.0);
    }

    const solaris::DeviceModel *DevicePanel::find(const std::string &id) const
    {
        for (const auto &s : mModel.strips)
            for (const auto &d : s.devices)
                if (d.id == id) return &d;
        for (const auto &d : mModel.masterDevices)
            if (d.id == id) return &d;
        return nullptr;
    }

    std::string DevicePanel::ownerOf(const std::string &id) const
    {
        for (const auto &s : mModel.strips)
            for (const auto &d : s.devices)
                if (d.id == id) return s.name;
        return "Master";
    }

    ParamPage *DevicePanel::page(const std::string &id) const
    {
        const auto it = mPages.find(id);
        return it == mPages.end() ? nullptr : it->second.get();
    }

    ParamPage &DevicePanel::build(const solaris::DeviceModel &d)
    {
        auto pg = std::make_shared<ParamPage>();
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
                    pg->sections.emplace_back(s, y);
                    y += kSectionH;
                }
            }
            ParamPage::Row r;
            r.spec = p;
            r.top = y;
            if (p.choices.empty())
            {
                const bool log = pg->logTaper(p);
                const double lo = log ? 0.0 : p.min, hi = log ? 1.0 : p.max;
                r.slider = std::make_shared<cosmo_v2::SliderRow>(p.label, lo, hi, pg->toPos(p, p.def)); // double-click: the registry's default
                r.slider->setValueWidth(kValueW);
                ParamPage *raw = pg.get();
                const std::string dev = d.id;
                const solaris::ParamModel spec = p;
                r.slider->formatValue = [raw, spec](double s) { return formatValue(spec, raw->fromPos(spec, s)); };
                r.slider->onChange = [this, raw, spec, dev](double s) {
                    if (onCommand) onCommand("set " + dev + "." + spec.name + "=" + storeText(raw->fromPos(spec, s), spec.integer));
                };
                r.slider->setValue(pg->toPos(p, p.value));
                pg->addChild(r.slider);
            }
            r.choice = (int)std::lround(p.value);
            pg->rows.push_back(r);
            y += kRowStep;
        }
        pg->contentH = y + 6.5;
        pg->setShownImmediate(false);
        addChild(pg);
        mPages[d.id] = pg;
        return *pg;
    }

    void DevicePanel::show(const std::string &id)
    {
        if (id == mDevice && mWanted) return;
        mDevice = id;
        mWanted = true;
    }

    void DevicePanel::hide() { mWanted = false; }

    void DevicePanel::bind(const solaris::AppModel &m, bool interacting)
    {
        mModel = m;
        mInteracting = interacting;
        const solaris::DeviceModel *d = find(mDevice);
        if (!d) { mWanted = false; return; } // removed while open (by anyone): the panel goes with it
        ParamPage *pg = page(mDevice);
        if (!pg || interacting) return;      // not built yet (the next advance builds it from this model); a drag outranks the model
        for (auto &r : pg->rows)
            for (const auto &p : d->params)
                if (p.name == r.spec.name)
                {
                    r.spec.value = p.value;
                    r.choice = (int)std::lround(p.value);
                    if (r.slider) r.slider->setValue(pg->toPos(p, p.value));
                }
    }

    void DevicePanel::layout()
    {
        const double W = width.value(), H = height.value();
        const double bodyH = std::max(0.0, H - kHeaderH);
        const ParamPage *cur = page(mDevice);
        mScroll.setExtent(kHeaderH, bodyH, cur ? cur->contentH : 0.0);
        for (auto &kv : mPages)
        {
            ParamPage &pg = *kv.second;
            pg.x.set(0.0);
            pg.y.set(kHeaderH);
            pg.width.set(W);
            pg.height.set(bodyH);
            pg.scroll = kv.first == mDevice ? mScroll.value() : pg.scroll; // a page fading out stays where it was
            for (auto &r : pg.rows)
            {
                if (!r.slider) continue;
                const double y = r.top - pg.scroll;
                r.slider->visible = y + cosmo_v2::SliderRow::kRowHeight > 0.0 && y < bodyH; // cull what is scrolled out
                r.slider->x.set(space::padX());
                r.slider->y.set(y);
                r.slider->width.set(std::max(0.0, W - 2.0 * space::padX()));
                r.slider->layout();
            }
        }
    }

    void DevicePanel::advance(double nowMs)
    {
        mNowMs = nowMs;
        // a device shown for the first time: its rows, built once from the model as it is now, then kept
        if (mWanted && !page(mDevice))
            if (const solaris::DeviceModel *d = find(mDevice)) build(*d);
        if (mWanted != mApplied)
        {
            mAppear.animateTo(mWanted ? 1.0 : 0.0, mWanted ? motion::kModalOpenMs : motion::kModalCloseMs, Easing::EaseOutCubic, nowMs);
            if (mWanted && mAppear.value() <= 0.001)
                for (auto &kv : mPages) kv.second->setShownImmediate(kv.first == mDevice); // from closed: the panel's fade carries it
            mApplied = mWanted;
        }
        if (mWanted)
            for (auto &kv : mPages) kv.second->setShown(kv.first == mDevice); // another device: the pages cross-fade
        mAppear.update(nowMs);
        opacity.set(mAppear.value());

        const solaris::DeviceModel *d = find(mDevice);
        const bool by = d && d->bypass;
        if (!mBypassInit) { mBypass.set(by ? 1.0 : 0.0); mBypassLast = by; mBypassInit = true; }
        else if (by != mBypassLast) { mBypass.animateTo(by ? 1.0 : 0.0, motion::kSelectMs, Easing::EaseOutCubic, nowMs); mBypassLast = by; }
        mBypass.update(nowMs);
        mScroll.advance(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    int DevicePanel::rowCount() const
    {
        const ParamPage *pg = page(mDevice);
        return pg ? (int)pg->rows.size() : 0;
    }

    std::string DevicePanel::rowParam(int i) const
    {
        const ParamPage *pg = page(mDevice);
        return pg && i >= 0 && i < (int)pg->rows.size() ? pg->rows[(size_t)i].spec.name : std::string();
    }

    Rect DevicePanel::rowRect(int i) const
    {
        const ParamPage *pg = page(mDevice);
        if (!pg || i < 0 || i >= (int)pg->rows.size()) return Rect{};
        return Rect{space::padX(), kHeaderH + pg->rows[(size_t)i].top - mScroll.value(), width.value() - 2.0 * space::padX(),
                    cosmo_v2::SliderRow::kRowHeight};
    }

    cosmo_v2::SliderRow *DevicePanel::slider(const std::string &param) const
    {
        const ParamPage *pg = page(mDevice);
        if (!pg) return nullptr;
        for (const auto &r : pg->rows)
            if (r.spec.name == param) return r.slider.get();
        return nullptr;
    }

    void DevicePanel::reveal(const std::string &param)
    {
        const ParamPage *pg = page(mDevice);
        if (!pg) return;
        for (const auto &r : pg->rows)
            if (r.spec.name == param) mScroll.reveal(r.top, cosmo_v2::SliderRow::kRowHeight);
    }

    double DevicePanel::pageAmount(const std::string &id) const
    {
        const ParamPage *pg = page(id);
        return pg ? pg->fadeValue() : 0.0;
    }

    Rect DevicePanel::closeRect() const { return Rect{width.value() - space::padX() - 19.5, 14.625, 19.5, 19.5}; }
    Rect DevicePanel::removeRect() const
    {
        const Rect c = closeRect();
        return Rect{c.x - 6.5 - 55.25, c.y, 55.25, 19.5}; // space::u(17)
    }
    Rect DevicePanel::bypassRect() const
    {
        const solaris::DeviceModel *d = find(mDevice);
        const Rect r = d && d->instrument ? closeRect() : removeRect(); // an instrument cannot be removed: no button
        return Rect{r.x - 6.5 - 55.25, r.y, 55.25, 19.5};
    }

    bool DevicePanel::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            int h = -1;
            if (closeRect().contains(local)) h = 0;
            else if (removeRect().contains(local)) h = 1;
            else if (bypassRect().contains(local)) h = 2;
            mHover.setHovered(h);
            return true;
        }
        case Gesture::Type::Click:
        {
            const solaris::DeviceModel *d = find(mDevice);
            if (!d) return true;
            if (closeRect().contains(local)) { hide(); return true; }
            if (!d->instrument && removeRect().contains(local)) { if (onCommand) onCommand("device remove " + d->id); return true; }
            if (bypassRect().contains(local)) { if (onCommand) onCommand("set " + d->id + ".bypass=" + (d->bypass ? "false" : "true")); return true; }
            // a choice row: the left half steps back, the right half forward
            ParamPage *pg = page(mDevice);
            if (!pg || local.y < kHeaderH) return true;
            const Point pl{local.x, local.y - kHeaderH};
            for (auto &r : pg->rows)
            {
                if (r.slider || r.spec.choices.empty()) continue;
                const Rect b = pg->choiceBox(r, width.value());
                if (!b.contains(pl)) continue;
                const int n = (int)r.spec.choices.size();
                const int next = ((r.choice + (pl.x < b.x + b.w * 0.5 ? -1 : 1)) % n + n) % n;
                if (onCommand) onCommand("set " + d->id + "." + r.spec.name + "=" + r.spec.choices[(size_t)next]);
                return true;
            }
            return true;
        }
        case Gesture::Type::Scroll:
            mScroll.scrollBy(g.delta.y);
            return true;
        default:
            return true; // the panel is a surface: nothing falls through it to the strips below
        }
    }

    void DevicePanel::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value();
        drawRoundedRect(t, Rect{0.5, 0.5, W - 1.0, H - 1.0}, radius::control(), Paint::filledStroked(palette::popover(), palette::border(), 1.0));
        const solaris::DeviceModel *d = find(mDevice);
        if (!d) return;
        const Rect by = bypassRect();
        const double titleW = std::max(0.0, by.x - 2.0 * space::padX());
        t.setFill(palette::foreground());
        t.drawText(textfit::ellipsize(t, d->label, titleW, 12.0, font::sansMedium()), space::padX(), 21.0, 12.0, font::sansMedium());
        t.setFill(palette::mutedForeground());
        t.drawText(textfit::ellipsize(t, ownerOf(d->id) + " \xC2\xB7 " + d->id, titleW, 10.0, font::mono()), space::padX(), 36.0, 10.0, font::mono());

        // On / Bypassed: the fill eases whoever changed it
        const double b = mBypass.value();
        const Color chip = lerpColor(palette::primary(), palette::secondary(), b);
        drawRoundedRect(t, by, radius::control(), Paint::filled(chip));
        const std::string bl = b > 0.5 ? "Bypassed" : "On";
        t.setFill(lerpColor(palette::primaryForeground(), palette::secondaryForeground(), b));
        t.drawText(bl, by.x + (by.w - t.measureText(bl, 10.0, font::sans())) * 0.5, textfit::baseline(by.y + by.h * 0.5, 10.0), 10.0, font::sans());
        if (!d->instrument)
        {
            const Rect rm = removeRect();
            drawRoundedRect(t, rm, radius::control(), Paint::filled(palette::hoverWash(0.6 + mHover.amount(1))));
            t.setFill(lerpColor(palette::secondaryForeground(), palette::destructive(), mHover.amount(1)));
            t.drawText("Remove", rm.x + (rm.w - t.measureText("Remove", 10.0, font::sans())) * 0.5, textfit::baseline(rm.y + rm.h * 0.5, 10.0), 10.0, font::sans());
        }
        const Rect c = closeRect();
        if (mHover.amount(0) > 0.001) drawRoundedRect(t, c, radius::control(), Paint::filled(palette::hoverWash(mHover.amount(0))));
        const Color xc = lerpColor(palette::mutedForeground(), palette::foreground(), mHover.amount(0));
        glyph::line(t, c.x + 6.0, c.y + 6.0, c.right() - 6.0, c.bottom() - 6.0, xc, 1.3);
        glyph::line(t, c.right() - 6.0, c.y + 6.0, c.x + 6.0, c.bottom() - 6.0, xc, 1.3);
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, kHeaderH - 0.5); t.lineTo(W, kHeaderH - 0.5); t.strokePath();
        mScroll.drawBar(t, W - 5.0);
    }
}
}
