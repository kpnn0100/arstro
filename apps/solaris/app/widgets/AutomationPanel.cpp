#include "AutomationPanel.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    namespace
    {
        constexpr double kLabelW = 97.5;  // space::u(30): "At the playhead" fits
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string q(const std::string &s) { return s.find_first_of(" \t\"") == std::string::npos && !s.empty() ? s : "\"" + s + "\""; }
        std::string number(double v, int decimals)
        {
            char b[32];
            std::snprintf(b, sizeof b, "%.*f", decimals, v);
            std::string s = b;
            if (s == "-0" || s == "-0.0" || s == "-0.00" || s == "-0.000") s = s.substr(1);
            return s;
        }
        std::string beatText(double b)
        {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", std::round(b * 960.0) / 960.0);
            return buf;
        }
        std::string shortNum(double v)
        {
            char b[32];
            std::snprintf(b, sizeof b, "%.3g", v);
            return b;
        }
    }

    AutomationPanel::AutomationPanel(std::string au) : mAu(std::move(au)) { clipToBounds = true; }

    std::string AutomationPanel::title() const { return "Automation \xE2\x80\x94 " + (mModel.name.empty() ? mAu : mModel.name); }

    std::string AutomationPanel::format(double v) const
    {
        const std::string &u = mModel.unit;
        const double a = std::fabs(v);
        if (u == "Hz") return a >= 1000.0 ? number(v / 1000.0, a >= 10000.0 ? 1 : 2) + " kHz" : number(v, a >= 100.0 ? 0 : 1) + " Hz";
        if (u == "ms") return number(v, a >= 100.0 ? 0 : 1) + " ms";
        if (u == "dB") return number(v, 1) + " dB";
        if (u.empty() && mModel.min >= 0.0 && mModel.max <= 1.0) return number(v * 100.0, 0) + " %";
        return number(v, a >= 100.0 ? 0 : 2) + (u.empty() ? "" : " " + u);
    }

    void AutomationPanel::bind(const solaris::AppModel &m)
    {
        mPresent = false;
        for (const auto &a : m.automations)
            if (a.id == mAu) { mModel = a; mPresent = true; }
        mPlaying = m.transport.playing;
        mPosition = m.transport.position;
        if (!mPresent) return; // its window closes; the rows it showed fade with it
        mNowTarget = mModel.now;
        // the rows, keyed by what they SAY: a change cross-fades its row, an insert slides the rest (§1)
        std::vector<std::pair<std::string, Row>> rows;
        auto add = [&rows](const std::string &key, Row::Kind kind, const std::string &label, const std::string &value, const std::string &extra) {
            Row r;
            r.kind = kind;
            r.label = label;
            r.value = value;
            r.extra = extra;
            rows.emplace_back(key, r);
        };
        add("name:" + mModel.name, Row::Info, "Name", mModel.name, "");
        add("id", Row::Info, "Id", mModel.id, "what a formula names");
        const std::string range = format(mModel.min) + " \xE2\x80\xA6 " + format(mModel.max);
        add("range:" + range, Row::Info, "Range", range, "");
        add("from:" + mModel.from, Row::Info, "Made from", mModel.from.empty() ? std::string("\xE2\x80\x94") : mModel.from,
            mModel.from.empty() ? "made from nothing" : "");
        add("now", Row::Now, "At the playhead", "", "");
        add("sec:points:" + std::to_string(mModel.points.size()), Row::Section, "POINTS \xC2\xB7 " + std::to_string(mModel.points.size()), "", "");
        for (const auto &p : mModel.points)
        {
            std::string shape = p.shape;
            if (p.shape == "bezier")
                shape += "  in " + shortNum(p.speedIn) + "/b " + shortNum(p.inflIn) + "%  out " + shortNum(p.speedOut) + "/b " + shortNum(p.inflOut) + "%";
            add("pt:" + beatText(p.at) + "|" + format(p.value) + "|" + shape, Row::Point, "beat " + beatText(p.at), format(p.value), shape);
        }
        add("sec:read", Row::Section, "READ BY", "", "");
        if (mModel.usedBy.empty()) add("rd:", Row::Reader, "nothing reads it", "", "it moves nothing until a formula names " + mModel.id);
        for (const auto &address : mModel.usedBy)
        {
            std::string formula;
            for (const auto &b : m.bindings)
                if (b.address == address) formula = b.formula;
            add("rd:" + address + "|" + formula, Row::Reader, address, formula, "");
        }
        mRows.sync(rows, kRowH);
        mContentH = rows.size() * kRowH + space::gap();
    }

    void AutomationPanel::layout() { mScroll.setExtent(kHeaderH, std::max(0.0, height.value() - kHeaderH), mContentH); }

    void AutomationPanel::advance(double nowMs)
    {
        // the value at the playhead: continuous while playing (the transport is the animation), eased to a jump
        if (!mNowInit)
        {
            mNow.set(mNowTarget);
            mPos.set(mPosition);
            mNowLast = mNowTarget;
            mPosLast = mPosition;
            mNowInit = mPresent;
        }
        auto follow = [&](AnimatedProperty &p, double target, double &last) {
            if (target == last) return;
            if (mPlaying) p.set(target);
            else p.animateTo(target, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
            last = target;
        };
        follow(mNow, mNowTarget, mNowLast);
        follow(mPos, mPosition, mPosLast);
        mNow.update(nowMs);
        mPos.update(nowMs);
        mRows.advance(nowMs);
        mScroll.advance(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    Rect AutomationPanel::renameRect() const { return Rect{width.value() - space::padX() - 55.25, 8.125, 55.25, 19.5}; }

    std::string AutomationPanel::text() const
    {
        std::string out;
        for (const auto &r : mRows.rows())
        {
            if (r.gone) continue;
            const Row &d = r.data;
            out += d.label + "  " + (d.kind == Row::Now ? format(mNow.value()) : d.value) + "  " + d.extra + "\n";
        }
        return out;
    }

    bool AutomationPanel::hasRow(const std::string &key) const
    {
        for (const auto &r : mRows.rows())
            if (r.key == key && !r.gone) return true;
        return false;
    }

    std::vector<std::string> AutomationPanel::rowKeys() const
    {
        std::vector<std::pair<int, std::string>> live;
        for (const auto &r : mRows.rows())
            if (!r.gone) live.emplace_back(r.index, r.key);
        std::sort(live.begin(), live.end());
        std::vector<std::string> out;
        for (const auto &x : live) out.push_back(x.second);
        return out;
    }

    double AutomationPanel::rowAlpha(const std::string &key) const
    {
        for (const auto &r : mRows.rows())
            if (r.key == key) return r.liveAlpha();
        return 0.0;
    }

    bool AutomationPanel::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
            mHover.setHovered(renameRect().contains(local) ? 0 : -1);
            return true;
        case Gesture::Type::Click:
            if (renameRect().contains(local) && mPresent && onRename)
            {
                // cosmo's field over the button; what is typed is ONE line
                const std::string au = mAu;
                onRename(mModel.name, g.pos, [this, au](const std::string &typed) {
                    if (!typed.empty() && onCommand) onCommand("set " + au + ".name=" + q(typed));
                });
            }
            return true;
        case Gesture::Type::Scroll:
            mScroll.scrollBy(g.delta.y);
            return true;
        default:
            return true; // the panel is a surface: nothing falls through it
        }
    }

    void AutomationPanel::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value(), pad = space::padX();
        // the header: what it is, and Rename
        const Rect rn = renameRect();
        t.setFill(palette::mutedForeground());
        const std::string head = mModel.id + (mModel.unit.empty() ? std::string() : " \xC2\xB7 " + mModel.unit) + " \xC2\xB7 " +
                                 std::to_string(mModel.points.size()) + (mModel.points.size() == 1 ? " point" : " points");
        t.drawText(textfit::ellipsize(t, head, std::max(0.0, rn.x - 2.0 * pad), 10.0, font::mono()), pad, textfit::baseline(rn.y + rn.h * 0.5, 10.0), 10.0,
                   font::mono());
        const double hv = mHover.amount(0);
        drawRoundedRect(t, rn, radius::control(), Paint::filled(lerpColor(palette::secondary(), palette::popover(), hv)));
        t.setFill(palette::secondaryForeground());
        t.drawText("Rename", rn.x + (rn.w - t.measureText("Rename", 10.0, font::sans())) * 0.5, textfit::baseline(rn.y + rn.h * 0.5, 10.0), 10.0, font::sans());
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, kHeaderH - 0.5); t.lineTo(W, kHeaderH - 0.5); t.strokePath();

        // the rows, eased: each where its slot is NOW, at its live opacity
        t.save();
        t.clipRect(0, kHeaderH, W, std::max(0.0, H - kHeaderH));
        const double scroll = mScroll.value();
        for (const auto &r : mRows.rows())
        {
            const double y = kHeaderH + space::gap() + r.liveY() - scroll, a = r.liveAlpha();
            if (y + kRowH < kHeaderH || y > H || a <= 0.001) continue;
            const Row &d = r.data;
            const double base = textfit::baseline(y + kRowH * 0.5, 10.0);
            switch (d.kind)
            {
            case Row::Section:
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(d.label, pad, textfit::baseline(y + kRowH * 0.62, 9.0), 9.0, font::sansSemiBold(), 0.13 * 9.0);
                break;
            case Row::Info:
            case Row::Now:
            {
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(d.label, pad, base, 10.0, font::sans());
                const bool now = d.kind == Row::Now;
                const std::string v = now ? format(mNow.value()) : d.value;
                const std::string extra = now ? "at beat " + number(mPos.value(), 2) : d.extra;
                const double vx = pad + kLabelW, room = std::max(0.0, W - vx - pad);
                const bool mono = d.label != "Name";
                t.setFill(fade(now ? palette::primary() : palette::foreground(), a));
                const std::string shown = textfit::ellipsize(t, v, room, mono ? 10.0 : 11.0, mono ? font::mono() : font::sans());
                t.drawText(shown, vx, base, mono ? 10.0 : 11.0, mono ? font::mono() : font::sans());
                if (!extra.empty())
                {
                    const double ex = vx + t.measureText(shown, mono ? 10.0 : 11.0, mono ? font::mono() : font::sans()) + space::gap();
                    t.setFill(fade(palette::mutedForeground(), a));
                    t.drawText(textfit::ellipsize(t, extra, std::max(0.0, W - pad - ex), 9.0, font::sans()), ex, textfit::baseline(y + kRowH * 0.5, 9.0), 9.0,
                               font::sans());
                }
                break;
            }
            case Row::Point:
            {
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(d.label, pad, base, 10.0, font::mono());
                t.setFill(fade(palette::foreground(), a));
                t.drawText(d.value, pad + 65.0, base, 10.0, font::mono());
                t.setFill(fade(d.extra.rfind("bezier", 0) == 0 ? palette::primary() : palette::secondaryForeground(), a));
                t.drawText(textfit::ellipsize(t, d.extra, std::max(0.0, W - pad - (pad + 149.5)), 9.0, font::mono()), pad + 149.5,
                           textfit::baseline(y + kRowH * 0.5, 9.0), 9.0, font::mono());
                break;
            }
            case Row::Reader:
            {
                const bool none = d.value.empty() && d.label == "nothing reads it";
                t.setFill(fade(none ? palette::mutedForeground() : palette::foreground(), a));
                const std::string addr = textfit::ellipsize(t, d.label, 136.5, 10.0, none ? font::sans() : font::mono());
                t.drawText(addr, pad, base, 10.0, none ? font::sans() : font::mono());
                const std::string right = none ? d.extra : d.value;
                t.setFill(fade(palette::secondaryForeground(), a));
                t.drawText(textfit::ellipsize(t, right, std::max(0.0, W - pad - (pad + 143.0)), 10.0, font::mono()), pad + 143.0, base, 10.0,
                           font::mono());
                break;
            }
            }
        }
        t.restore();
        mScroll.drawBar(t, W - 2.0);
    }
}
}
