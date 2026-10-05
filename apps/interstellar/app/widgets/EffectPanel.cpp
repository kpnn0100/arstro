#include "EffectPanel.h"
#include "CommandLine.h"
#include "Glyphs.h"
#include "TextFit.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kPadX = 9.75;
        constexpr double kRowGap = 6.5;
        constexpr double kBodyPad = 3.25;
        Color fade(Color c, double a) { c.a *= a; return c; }
    }

    EffectPanel::EffectPanel() { clipToBounds = true; }

    EffectPanel::Section &EffectPanel::sectionFor(const interstellar::EffectModel &e)
    {
        auto it = mSections.find(e.id);
        if (it != mSections.end()) return it->second;
        Section &s = mSections[e.id];
        s.type = e.type;
        const std::string id = e.id;
        auto make = [&](const std::string &key, const std::string &label, double min, double max, bool percent) {
            Row r;
            r.key = key;
            r.percent = percent;
            r.slider = std::make_shared<cosmo_v2::SliderRow>(label, percent ? min * 100.0 : min, percent ? max * 100.0 : max, 0.0);
            r.slider->onChange = [this, id, key, percent](double v) {
                if (onCommand) onCommand("set " + cmd::quote(id + "." + key + "=" + cmd::num(percent ? v / 100.0 : v)));
            };
            r.slider->visible = false;
            addChild(r.slider);
            s.rows.push_back(r);
        };
        make("mix", "Mix", 0.0, 1.0, true);
        for (const auto &p : e.params) make(p.key, p.label + (p.unit == "px" ? " (px)" : p.unit == "deg" ? " (\xC2\xB0)" : ""), p.min, p.max, p.unit.empty());
        if (!e.fileKey.empty())
        {
            s.fileKey = e.fileKey;
            auto b = std::make_shared<cosmo_v2::PillButton>("Choose a .cube\xE2\x80\xA6");
            b->idleBox = {Paint::filledStroked(palette::secondary(), palette::border(), 1.0), radius::control()};
            b->activeBox = b->idleBox;
            b->idleText = {palette::foreground(), 10.0, font::sans()};
            b->activeText = b->idleText;
            b->hoverEmphasis = palette::white();
            const std::string key = e.fileKey;
            b->onClick = [this, id, key] { if (onChooseFile) onChooseFile(id, key); };
            b->visible = false;
            addChild(b);
            s.fileButton = b;
        }
        return s;
    }

    void EffectPanel::bind(const interstellar::AppModel &m, const std::string &id, bool interacting)
    {
        const interstellar::EffectModel *sel = nullptr;
        for (const auto &x : m.effects) if (x.id == id) sel = &x;
        mId = sel ? sel->id : std::string();
        mNode = sel ? sel->node : std::string();
        mShown.clear();
        if (sel)
        {
            std::vector<const interstellar::EffectModel *> stack;
            for (const auto &x : m.effects) if (x.node == mNode) stack.push_back(&x);
            std::stable_sort(stack.begin(), stack.end(), [](auto *a, auto *b) { return a->order < b->order; });
            for (const auto *e : stack)
            {
                Section &s = sectionFor(*e);
                s.label = e->label;
                mShown.push_back(e->id);
                if (s.fileButton && s.file != e->file)
                {
                    s.file = e->file;
                    const auto slash = s.file.find_last_of('/');
                    s.fileButton->setLabel(s.file.empty() ? std::string("Choose a .cube\xE2\x80\xA6")
                                                          : "LUT  \xC2\xB7  " + (slash == std::string::npos ? s.file : s.file.substr(slash + 1)));
                }
                if (interacting) continue;   // a gesture in flight outranks the model
                for (auto &r : s.rows)
                {
                    double v = 0.0;
                    if (r.key == "mix") v = e->mix;
                    else for (const auto &p : e->params) if (p.key == r.key) v = p.value;
                    r.slider->setValue(r.percent ? v * 100.0 : v);
                }
            }
        }
        // a newly selected effect: its section opens and comes into view
        if (!mId.empty() && mId != mLastSelected)
        {
            mSections[mId].open = true;
            mRevealPending = true;
        }
        mLastSelected = mId;
        for (auto &kv : mSections)
        {
            const bool shown = std::find(mShown.begin(), mShown.end(), kv.first) != mShown.end();
            if (!shown)
            {
                for (auto &r : kv.second.rows) r.slider->visible = false;
                if (kv.second.fileButton) kv.second.fileButton->visible = false;
            }
        }
    }

    std::shared_ptr<cosmo_v2::SliderRow> EffectPanel::sliderOf(const std::string &effectId, const std::string &key) const
    {
        const auto it = mSections.find(effectId);
        if (it == mSections.end()) return nullptr;
        for (const auto &r : it->second.rows) if (r.key == key) return r.slider;
        return nullptr;
    }

    std::shared_ptr<cosmo_v2::PillButton> EffectPanel::fileButtonOf(const std::string &effectId) const
    {
        const auto it = mSections.find(effectId);
        return it == mSections.end() ? nullptr : it->second.fileButton;
    }

    void EffectPanel::setOpen(const std::string &effectId, bool open)
    {
        auto it = mSections.find(effectId);
        if (it != mSections.end()) it->second.open = open;
    }
    bool EffectPanel::isOpen(const std::string &effectId) const
    {
        const auto it = mSections.find(effectId);
        return it != mSections.end() && it->second.open;
    }
    double EffectPanel::openAmount(const std::string &effectId) const
    {
        const auto it = mSections.find(effectId);
        return it == mSections.end() ? 0.0 : it->second.amount.value();
    }

    double EffectPanel::bodyH(const Section &s) const
    {
        const double full = kBodyPad + (s.rows.size() + (s.fileButton ? 1 : 0)) * (cosmo_v2::SliderRow::kRowHeight + kRowGap);
        return full * s.amount.value();
    }

    double EffectPanel::sectionTop(int i) const
    {
        double y = 0.0;
        for (int k = 0; k < i; ++k) y += kHeaderH + bodyH(mSections.at(mShown[(size_t)k]));
        return y;
    }

    double EffectPanel::contentH() const { return sectionTop((int)mShown.size()); }

    Rect EffectPanel::headerRect(int i) const
    {
        return Rect{0.0, sectionTop(i) - mScroll.value(), width.value(), kHeaderH};
    }

    void EffectPanel::layout()
    {
        const double w = width.value(), h = height.value();
        mScroll.setExtent(0.0, h, contentH());
        for (int i = 0; i < (int)mShown.size(); ++i)
        {
            const Section &s = mSections.at(mShown[(size_t)i]);
            const double top = headerRect(i).bottom() + kBodyPad, body = bodyH(s), a = s.amount.value();
            double y = top;
            for (const auto &r : s.rows)
            {
                r.slider->x.set(kPadX);
                r.slider->y.set(y);
                r.slider->width.set(std::max(0.0, w - 2 * kPadX));
                r.slider->height.set(cosmo_v2::SliderRow::kRowHeight);
                r.slider->layout();
                r.slider->opacity.set(a);
                // inside the section's eased height and the panel — culled otherwise (no input either)
                const bool inside = y + cosmo_v2::SliderRow::kRowHeight <= top - kBodyPad + body + 0.5;
                r.slider->visible = a > 0.02 && inside && y + cosmo_v2::SliderRow::kRowHeight > 0.0 && y < h;
                y += cosmo_v2::SliderRow::kRowHeight + kRowGap;
            }
            if (s.fileButton)
            {
                // the file row, under the sliders, by the same eased rules
                const double rh = cosmo_v2::SliderRow::kRowHeight;
                s.fileButton->x.set(kPadX);
                s.fileButton->y.set(y);
                s.fileButton->width.set(std::max(0.0, w - 2 * kPadX));
                s.fileButton->height.set(rh);
                s.fileButton->opacity.set(a);
                const bool inside = y + rh <= top - kBodyPad + body + 0.5;
                s.fileButton->visible = a > 0.02 && inside && y + rh > 0.0 && y < h;
            }
        }
    }

    void EffectPanel::advance(double nowMs)
    {
        for (auto &kv : mSections)
        {
            Section &s = kv.second;
            if (!s.placed) { s.amount.set(s.open ? 1.0 : 0.0); s.openApplied = s.open; s.placed = true; }   // first placement
            if (s.open != s.openApplied)
            {
                s.amount.animateTo(s.open ? 1.0 : 0.0, motion::kSlideMs, Easing::EaseOutCubic, nowMs);
                s.openApplied = s.open;
            }
            s.amount.update(nowMs);
        }
        if (mRevealPending && !mId.empty())
        {
            // reveal the selected section as it will stand OPEN (its header and its whole body)
            for (int i = 0; i < (int)mShown.size(); ++i)
                if (mShown[(size_t)i] == mId)
                {
                    const Section &s = mSections.at(mId);
                    const double full = kBodyPad + (s.rows.size() + (s.fileButton ? 1 : 0)) * (cosmo_v2::SliderRow::kRowHeight + kRowGap);
                    mScroll.setExtent(0.0, height.value(), contentH() + full);
                    mScroll.reveal(sectionTop(i), kHeaderH + full);
                }
            mRevealPending = false;
        }
        mScroll.advance(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        layout();
        Segment::advance(nowMs);
    }

    bool EffectPanel::handleGesture(const Gesture &g, const Point &local)
    {
        auto headerAt = [&](const Point &p) {
            for (int i = 0; i < (int)mShown.size(); ++i)
                if (headerRect(i).contains(p)) return i;
            return -1;
        };
        switch (g.type)
        {
            case Gesture::Type::Move: mHover.setHovered(headerAt(local)); return true;
            case Gesture::Type::Scroll: return mScroll.scrollBy(g.delta.y);
            case Gesture::Type::Click:
            {
                const int i = headerAt(local);
                if (i >= 0) { Section &s = mSections.at(mShown[(size_t)i]); s.open = !s.open; }
                return true;
            }
            default: return Segment::handleGesture(g, local);
        }
    }

    void EffectPanel::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::card()));
        // D-12: clipToBounds clips the CHILDREN, not this paint — a scrolled header must not draw
        // over the plugin list above
        t.save();
        t.clipRect(0, 0, w, h);
        for (int i = 0; i < (int)mShown.size(); ++i)
        {
            const Rect r = headerRect(i);
            if (r.bottom() < 0 || r.y > h) continue;
            const Section &s = mSections.at(mShown[(size_t)i]);
            const bool sel = mShown[(size_t)i] == mId;
            if (const double hv = mHover.amount(i); hv > 0.001) drawRoundedRect(t, r, 0.0, Paint::filled(palette::hoverWash(hv)));
            glyph::line(t, kPadX, r.y + 0.5, w - kPadX, r.y + 0.5, palette::border(), 1.0);
            // the disclosure arrow turns with the section's own eased amount
            const double open = s.amount.value(), cx = kPadX + 4.0, cy = r.y + r.h * 0.5;
            const double ang = (1.0 - open) * -1.5707963;
            auto pt = [&](double x, double y) { return Point{cx + x * std::cos(ang) - y * std::sin(ang), cy + x * std::sin(ang) + y * std::cos(ang)}; };
            const Point p0 = pt(-3.5, -1.75), p1 = pt(3.5, -1.75), p2 = pt(0.0, 2.75);
            t.beginPath(); t.moveTo(p0.x, p0.y); t.lineTo(p1.x, p1.y); t.lineTo(p2.x, p2.y); t.closePath();
            t.setFill(sel ? palette::foreground() : palette::mutedForeground());
            t.fillPath();
            std::string title = s.label;
            for (char &c : title) c = (char)std::toupper((unsigned char)c);
            const double idW = t.measureText(mShown[(size_t)i], 9.0, font::mono());
            t.setFill(sel ? palette::foreground() : palette::mutedForeground());
            t.drawText(textfit::ellipsize(t, title, w - 2 * kPadX - 14.0 - idW - 8.0, 9.0, font::sansSemiBold(), 0.13 * 9.0), kPadX + 14.0,
                       textfit::baseline(cy, 9.0), 9.0, font::sansSemiBold(), 0.13 * 9.0);
            t.setFill(fade(palette::mutedForeground(), 0.8));
            t.drawText(mShown[(size_t)i], w - kPadX - idW, textfit::baseline(cy, 9.0), 9.0, font::mono());
        }
        t.restore();
        mScroll.drawBar(t, w);
    }
}
}
