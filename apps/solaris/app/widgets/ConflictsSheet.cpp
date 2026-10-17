#include "ConflictsSheet.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    namespace
    {
        constexpr double kHeaderH = 60.0;  // the card's title band (the Relink sheet's)
        constexpr double kFooterH = 56.0;  // Keep All Mine · Take All Theirs · Done, under a hairline
        constexpr int kAllMineId = 900, kAllTheirsId = 901, kDoneId = 902;
        Color fade(Color c, double a) { c.a *= a; return c; }
        int mineId(const std::string &n) { return 1000 + (int)(std::hash<std::string>{}(n) % 1000000); }
        int theirsId(const std::string &n) { return 2000000 + (int)(std::hash<std::string>{}(n) % 1000000); }
        std::vector<std::string> linesOf(const std::string &text)
        {
            std::vector<std::string> out;
            std::istringstream in(text);
            std::string l;
            while (std::getline(in, l))
                if (!l.empty()) out.push_back(l);
            return out;
        }
        // a line's tokens, a quoted value kept whole
        std::vector<std::string> tokens(const std::string &line)
        {
            std::vector<std::string> out;
            std::string cur;
            bool q = false;
            for (char ch : line)
            {
                if (ch == '"') q = !q;
                if (ch == ' ' && !q)
                {
                    if (!cur.empty()) out.push_back(cur);
                    cur.clear();
                    continue;
                }
                cur += ch;
            }
            if (!cur.empty()) out.push_back(cur);
            return out;
        }

        std::string trimmed(const std::string &s)
        {
            const auto a = s.find_first_not_of(' ');
            return a == std::string::npos ? std::string() : s.substr(a);
        }

        // what tells the two sides apart: on its own line, the fields that differ (a header field whole); under it, the
        // first line that differs and how many more — so a gain changed both ways reads "gain=-2.0" against "gain=-9.0"
        void tellApart(const std::vector<std::string> &a, const std::vector<std::string> &b, std::vector<std::string> &outA, std::vector<std::string> &outB)
        {
            outA.clear();
            outB.clear();
            if (a.empty() || b.empty()) // one side has none: what there is
            {
                for (size_t i = 0; i < a.size() && i < 2; ++i) outA.push_back(trimmed(a[i]));
                for (size_t i = 0; i < b.size() && i < 2; ++i) outB.push_back(trimmed(b[i]));
                return;
            }
            if (a[0] != b[0])
            {
                if (a[0].find(" = ") != std::string::npos) { outA.push_back(a[0]); outB.push_back(b[0]); } // a header field
                else
                {
                    const auto ta = tokens(a[0]), tb = tokens(b[0]);
                    std::string da, db;
                    for (const auto &t : ta)
                        if (std::find(tb.begin(), tb.end(), t) == tb.end()) da += (da.empty() ? "" : " ") + t;
                    for (const auto &t : tb)
                        if (std::find(ta.begin(), ta.end(), t) == ta.end()) db += (db.empty() ? "" : " ") + t;
                    outA.push_back(da.empty() ? "(its line the same)" : da);
                    outB.push_back(db.empty() ? "(its line the same)" : db);
                }
            }
            size_t i = 1;
            while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
            if (i < a.size() || i < b.size())
            {
                auto under = [i](const std::vector<std::string> &v) {
                    if (i >= v.size()) return std::string("(nothing more under it)");
                    const size_t more = v.size() - i - 1;
                    return trimmed(v[i]) + (more ? "  (+" + std::to_string(more) + ")" : std::string());
                };
                outA.push_back(under(a));
                outB.push_back(under(b));
            }
        }

        void button(IRenderTarget &t, const Rect &r, const char *label, double hover, double a, bool primary)
        {
            drawRoundedRect(t, r, radius::control(), Paint::filled(fade(primary ? palette::primary() : lerpColor(palette::secondary(), palette::card(), hover), a)));
            if (primary && hover > 0.001) drawRoundedRect(t, r, radius::control(), Paint::filled(fade(palette::hoverWash(hover), a)));
            t.setFill(fade(primary ? palette::primaryForeground() : palette::secondaryForeground(), a));
            const char *family = primary ? font::sansMedium() : font::sans();
            const double px = primary ? 12.0 : 11.0;
            t.drawText(label, r.x + (r.w - t.measureText(label, px, family)) * 0.5, textfit::baseline(r.y + r.h * 0.5, px), px, family);
        }
    }

    ConflictsSheet::ConflictsSheet() { opacity.set(0.0); }

    void ConflictsSheet::show(const solaris::AppModel &m)
    {
        mShowWanted = true;
        mCloseWanted = false;
        mPlaceAll = true;
        mRows.clear();
        mScroll.reset(); // a new list: nowhere to travel from
        mCardHTarget = -1.0;
        bind(m);
    }

    void ConflictsSheet::close()
    {
        mCloseWanted = true;
        mShowWanted = false;
    }

    void ConflictsSheet::bind(const solaris::AppModel &m)
    {
        mWanted = m.sync.conflicts;
        mWaiting = m.sync.waiting;
    }

    double ConflictsSheet::rowAmount(const std::string &node) const
    {
        for (const auto &r : mRows)
            if (r.node == node) return r.a.value();
        return -1.0;
    }

    double ConflictsSheet::rowY(const std::string &node) const
    {
        for (const auto &r : mRows)
            if (r.node == node) return r.y.value();
        return -1.0;
    }

    std::vector<std::string> ConflictsSheet::shown(const std::string &node, bool mine) const
    {
        for (const auto &r : mRows)
            if (r.node == node) return mine ? r.mine : r.theirs;
        return {};
    }

    Rect ConflictsSheet::mineRect(const std::string &node) const
    {
        for (const auto &x : mMine)
            if (x.first == node) return x.second;
        return Rect{};
    }

    Rect ConflictsSheet::theirsRect(const std::string &node) const
    {
        for (const auto &x : mTheirs)
            if (x.first == node) return x.second;
        return Rect{};
    }

    Rect ConflictsSheet::cardRect() const
    {
        const double W = width.value(), H = height.value();
        const double w = std::min(kCardW, W - 32.0), h = std::min(mCardH.value(), H - 48.0);
        return Rect{std::floor((W - w) * 0.5), std::floor((H - h) * 0.5), w, h};
    }

    Rect ConflictsSheet::listRect() const
    {
        const Rect c = cardRect();
        return Rect{c.x, c.y + kHeaderH, c.w, std::max(0.0, c.h - kHeaderH - kFooterH)};
    }

    void ConflictsSheet::advance(double nowMs)
    {
        if (mShowWanted)
        {
            mOpen = true;
            mClosing = false;
            mAppear.animateTo(1.0, motion::kModalOpenMs, Easing::EaseOutCubic, nowMs);
            mShowWanted = false;
        }
        if (mCloseWanted && mOpen && !mClosing)
        {
            mClosing = true;
            mAppear.animateTo(0.0, motion::kModalCloseMs, Easing::EaseOutCubic, nowMs);
            mCloseWanted = false;
        }
        mAppear.update(nowMs);
        if (mClosing && !mAppear.isAnimating()) { mOpen = false; mClosing = false; }
        opacity.set(mOpen ? mAppear.value() : 0.0); // the sheet fades as one group

        // rows keyed by node: one asked fades in at its place, one answered fades out where it is, the rest slide
        for (size_t i = 0; i < mWanted.size(); ++i)
        {
            const auto &w = mWanted[i];
            auto it = std::find_if(mRows.begin(), mRows.end(), [&](const Row &r) { return r.node == w.node; });
            if (it == mRows.end())
            {
                mRows.emplace_back();
                it = mRows.end() - 1;
                it->node = w.node;
            }
            it->leaving = false;
            it->what = w.what;
            it->how = w.how;
            tellApart(linesOf(w.mine), linesOf(w.theirs), it->mine, it->theirs);
            it->yTarget = (double)i * kRowH;
        }
        for (auto &r : mRows)
            if (std::none_of(mWanted.begin(), mWanted.end(), [&](const solaris::SyncConflictModel &w) { return w.node == r.node; })) r.leaving = true;
        for (auto &r : mRows)
        {
            const double wantA = r.leaving ? 0.0 : 1.0;
            if (!r.placed)
            {
                r.y.set(r.yTarget);
                r.yLast = r.yTarget;
                if (mPlaceAll) r.a.set(1.0); // shown with the sheet: the sheet's own fade is enough
                else r.a.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                r.aTarget = 1.0;
                r.placed = true;
            }
            if (wantA != r.aTarget) { r.a.animateTo(wantA, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); r.aTarget = wantA; }
            if (!r.leaving && r.yTarget != r.yLast) { r.y.animateTo(r.yTarget, motion::kSelectMs, Easing::EaseOutCubic, nowMs); r.yLast = r.yTarget; }
            r.a.update(nowMs);
            r.y.update(nowMs);
        }
        mRows.erase(std::remove_if(mRows.begin(), mRows.end(), [](const Row &r) { return r.leaving && !r.a.isAnimating() && r.a.value() <= 0.001; }),
                    mRows.end());
        mPlaceAll = false;

        // the card grows and shrinks with what is asked (at least a row's room: "every change is answered" sits there)
        const double h = kHeaderH + std::clamp((double)mWanted.size(), 1.0, (double)kMaxRowsShown) * kRowH + 12.0 + kFooterH;
        if (mCardHTarget < 0.0) mCardH.set(h);
        else if (h != mCardHTarget) mCardH.animateTo(h, motion::kSelectMs, Easing::EaseOutCubic, nowMs);
        mCardHTarget = h;
        mCardH.update(nowMs);
        const double on = mWanted.empty() ? 0.35 : 1.0;
        if (mAllOnLast < 0.0) mAllOn.set(on);
        else if (on != mAllOnLast) mAllOn.animateTo(on, motion::kSelectMs, Easing::EaseOutCubic, nowMs);
        mAllOnLast = on;
        mAllOn.update(nowMs);
        const Rect l = listRect();
        mScroll.setExtent(l.y, l.h, (double)mWanted.size() * kRowH);
        mScroll.advance(nowMs);
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    bool ConflictsSheet::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mOpen || mClosing) return false;
        const Rect l = listRect();
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            int id = mAllMine.contains(local) ? kAllMineId : mAllTheirs.contains(local) ? kAllTheirsId : mDone.contains(local) ? kDoneId : -1;
            if (l.contains(local))
            {
                for (const auto &x : mMine)
                    if (x.second.contains(local)) id = mineId(x.first);
                for (const auto &x : mTheirs)
                    if (x.second.contains(local)) id = theirsId(x.first);
            }
            mHover.setHovered(id);
            return true;
        }
        case Gesture::Type::Scroll:
            if (l.contains(local)) mScroll.scrollBy(g.delta.y); // a list that cannot scroll swallows it: a modal
            return true;
        case Gesture::Type::Click:
            if (!cardRect().contains(local) || mDone.contains(local)) { close(); return true; } // the scrim closes, as cosmo's
            if (!mWanted.empty() && mAllMine.contains(local)) { send("project sync --take mine"); return true; }
            if (!mWanted.empty() && mAllTheirs.contains(local)) { send("project sync --take theirs"); return true; }
            if (l.contains(local))
            {
                for (const auto &x : mMine)
                    if (x.second.contains(local)) { send("project sync --take mine " + x.first); return true; }
                for (const auto &x : mTheirs)
                    if (x.second.contains(local)) { send("project sync --take theirs " + x.first); return true; }
            }
            return true;
        default:
            return true; // a modal swallows the rest
        }
    }

    bool ConflictsSheet::handleKey(const KeyEvent &e)
    {
        if (!isOpen()) return false;
        if (e.type == KeyEvent::Type::Down && (e.keyCode == 27 || e.keyCode == 13)) close(); // Escape, Enter: Done
        return true; // a modal owns the keyboard
    }

    void ConflictsSheet::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value();
        drawRoundedRect(t, Rect{0, 0, W, H}, 0.0, Paint::filled(surface::scrim(1.0))); // the group's opacity fades it
        const Rect c = cardRect();
        drawRoundedRect(t, c, radius::control(), Paint::filledStroked(palette::popover(), palette::border(), 1.0));
        t.setFill(palette::foreground());
        t.drawText("Conflicts", c.x + kPad, c.y + 34.0, 14.0, font::sansSemiBold());
        const size_t n = mWanted.size();
        std::string sub = n == 0 ? "every change is answered" : std::to_string(n) + " asked \xE2\x80\x94 the song keeps yours until you answer";
        if (mWaiting) sub += "  \xC2\xB7  the file changed again: merged once these are answered";
        t.setFill(palette::mutedForeground());
        const double tx = c.x + kPad + t.measureText("Conflicts", 14.0, font::sansSemiBold()) + 10.0;
        t.drawText(textfit::ellipsize(t, sub, std::max(0.0, c.right() - kPad - tx), 11.0, font::sans()), tx, c.y + 34.0, 11.0, font::sans());

        // the rows, clipped to the list and scrolled; each at its eased place, at its eased fade
        const Rect l = listRect();
        mMine.clear();
        mTheirs.clear();
        t.save();
        t.clipRect(l.x, l.y, l.w, l.h);
        double rowsA = 0.0; // the message comes in as the last answered row goes
        for (const auto &r : mRows) rowsA = std::max(rowsA, r.a.value());
        if (n == 0 && rowsA < 0.999)
        {
            t.setFill(fade(palette::mutedForeground(), 1.0 - rowsA));
            const char *msg = "Nothing is asked.";
            t.drawText(msg, c.x + (c.w - t.measureText(msg, 12.0, font::sans())) * 0.5, textfit::baseline(l.y + kRowH * 0.5, 12.0), 12.0, font::sans());
        }
        const double bx = c.right() - kPad - kButtonW, x0 = c.x + kPad;
        const double colW = std::max(0.0, (bx - 16.0 - x0 - 12.0) * 0.5);
        for (const auto &r : mRows)
        {
            const double top = l.y + r.y.value() - mScroll.value(), a = r.a.value();
            if (top + kRowH < l.y || top > l.bottom() || a <= 0.001) continue;
            // what, and how
            const std::string what = textfit::ellipsize(t, r.what, bx - 16.0 - x0, 12.0, font::sans());
            t.setFill(fade(palette::foreground(), a));
            t.drawText(what, x0, top + 20.0, 12.0, font::sans());
            const double ww = t.measureText(what, 12.0, font::sans());
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText(textfit::ellipsize(t, "  \xC2\xB7  " + r.how, std::max(0.0, bx - 16.0 - x0 - ww), 10.0, font::sans()), x0 + ww, top + 20.0, 10.0, font::sans());
            // its lines here and in the file, side by side
            auto column = [&](double x, const char *label, const std::vector<std::string> &lines, const char *none) {
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(label, x, top + 40.0, 9.0, font::sansSemiBold(), 9.0 * 0.13);
                if (lines.empty())
                {
                    t.drawText(none, x, top + 56.0, 10.0, font::sans());
                    return;
                }
                t.setFill(fade(palette::foreground(), a));
                for (size_t i = 0; i < lines.size() && i < 2; ++i)
                    t.drawText(textfit::ellipsize(t, lines[i], colW, 10.0, font::mono()), x, top + 56.0 + 14.0 * (double)i, 10.0, font::mono());
            };
            column(x0, "HERE", r.mine, "(not here)");
            column(x0 + colW + 12.0, "IN THE FILE", r.theirs, "(not in the file)");
            // Keep Mine · Take Theirs
            const Rect mine{bx, top + 14.0, kButtonW, kButtonH}, theirs{bx, top + 14.0 + kButtonH + 8.0, kButtonW, kButtonH};
            if (!r.leaving)
            {
                mMine.emplace_back(r.node, mine);
                mTheirs.emplace_back(r.node, theirs);
            }
            button(t, mine, "Keep Mine", mHover.amount(mineId(r.node)), a, false);
            button(t, theirs, "Take Theirs", mHover.amount(theirsId(r.node)), a, false);
            if (&r != &mRows.back())
            {
                t.setStroke(fade(palette::border(), a), 1.0);
                t.beginPath(); t.moveTo(x0, top + kRowH - 0.5); t.lineTo(c.right() - kPad, top + kRowH - 0.5); t.strokePath();
            }
        }
        t.restore();
        if (mScroll.scrollable()) // the bar only when there is more to see (R6)
        {
            const double frac = l.h / std::max(l.h, mScroll.contentH()), th = std::max(18.0, l.h * frac);
            const double ty = l.y + (l.h - th) * (mScroll.value() / std::max(1.0, mScroll.maxScroll()));
            drawRoundedRect(t, Rect{c.right() - 6.0, ty, 3.0, th}, radius::pill(), Paint::filled(palette::whiteAlpha(0.18)));
        }

        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(c.x, c.bottom() - kFooterH); t.lineTo(c.right(), c.bottom() - kFooterH); t.strokePath();
        // Keep All Mine · Take All Theirs (secondary, left; dimmed with nothing asked) · Done (primary, right)
        mDone = Rect{c.right() - kPad - 88.0, c.bottom() - kFooterH + (kFooterH - 30.0) * 0.5, 88.0, 30.0};
        mAllMine = Rect{c.x + kPad, mDone.y, 128.0, 30.0};
        mAllTheirs = Rect{mAllMine.right() + 8.0, mDone.y, 136.0, 30.0};
        button(t, mAllMine, "Keep All Mine", mHover.amount(kAllMineId), mAllOn.value(), false);
        button(t, mAllTheirs, "Take All Theirs", mHover.amount(kAllTheirsId), mAllOn.value(), false);
        button(t, mDone, "Done", mHover.amount(kDoneId), 1.0, true);
    }
}
}
