#include "RelinkSheet.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    namespace
    {
        constexpr double kHeaderH = 60.0;  // the card's title band (the Export sheet's)
        constexpr double kFooterH = 56.0;  // Search a Folder… · Done, under a hairline
        constexpr double kLocateW = 84.0;
        constexpr int kSearchId = 900, kDoneId = 901;
        Color fade(Color c, double a) { c.a *= a; return c; }
        int locateId(const std::string &path) { return 1000 + (int)(std::hash<std::string>{}(path) % 1000000); }
    }

    RelinkSheet::RelinkSheet() { opacity.set(0.0); }

    void RelinkSheet::show(const solaris::AppModel &m)
    {
        mShowWanted = true;
        mCloseWanted = false;
        mPlaceAll = true;
        mRows.clear();
        mScroll.reset(); // a new list: nowhere to travel from
        mCardHTarget = -1.0;
        bind(m);
    }

    void RelinkSheet::close()
    {
        mCloseWanted = true;
        mShowWanted = false;
    }

    void RelinkSheet::bind(const solaris::AppModel &m) { mWanted = m.missing; }

    double RelinkSheet::rowAmount(const std::string &path) const
    {
        for (const auto &r : mRows)
            if (r.path == path) return r.a.value();
        return -1.0;
    }

    double RelinkSheet::rowY(const std::string &path) const
    {
        for (const auto &r : mRows)
            if (r.path == path) return r.y.value();
        return -1.0;
    }

    Rect RelinkSheet::locateRect(const std::string &path) const
    {
        for (const auto &l : mLocate)
            if (l.first == path) return l.second;
        return Rect{};
    }

    void RelinkSheet::accessibles(std::vector<A11yNode> &out)
    {
        for (const auto &l : mLocate) // as the last paint placed them: the rows in view, none leaving
            for (const auto &r : mRows)
                if (r.path == l.first)
                    out.push_back(a11yNode("relink." + r.path, "button", fillIn(tr("Locate {1}"), r.name), l.second, r.folder.empty() ? r.users : r.folder + " \xC2\xB7 " + r.users));
        out.push_back(a11yNode("relink.search", "button", tr("Search a Folder\xE2\x80\xA6"), mSearch));
        out.push_back(a11yNode("relink.done", "button", tr("Done"), mDone));
    }

    Rect RelinkSheet::cardRect() const
    {
        const double W = width.value(), H = height.value();
        const double w = std::min(kCardW, W - 32.0), h = std::min(mCardH.value(), H - 48.0);
        return Rect{std::floor((W - w) * 0.5), std::floor((H - h) * 0.5), w, h};
    }

    Rect RelinkSheet::listRect() const
    {
        const Rect c = cardRect();
        return Rect{c.x, c.y + kHeaderH, c.w, std::max(0.0, c.h - kHeaderH - kFooterH)};
    }

    void RelinkSheet::advance(double nowMs)
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

        // rows keyed by path: one arriving fades in at its place, one found fades out where it is, the rest slide
        for (size_t i = 0; i < mWanted.size(); ++i)
        {
            const auto &w = mWanted[i];
            auto it = std::find_if(mRows.begin(), mRows.end(), [&](const Row &r) { return r.path == w.path; });
            if (it == mRows.end())
            {
                mRows.emplace_back();
                it = mRows.end() - 1;
                it->path = w.path;
                const auto slash = w.path.find_last_of('/');
                it->name = slash == std::string::npos ? w.path : w.path.substr(slash + 1);
                it->folder = slash == std::string::npos ? std::string() : w.path.substr(0, slash);
            }
            it->leaving = false;
            it->users.clear();
            for (const auto &u : w.users) it->users += (it->users.empty() ? "" : ", ") + u;
            it->yTarget = (double)i * kRowH;
        }
        for (auto &r : mRows)
            if (std::none_of(mWanted.begin(), mWanted.end(), [&](const solaris::MissingModel &w) { return w.path == r.path; })) r.leaving = true;
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

        // the card grows and shrinks with what is missing (at least a row's room: "nothing is missing" sits there)
        const double h = kHeaderH + std::clamp((double)mWanted.size(), 1.0, (double)kMaxRowsShown) * kRowH + 12.0 + kFooterH;
        if (mCardHTarget < 0.0) mCardH.set(h);
        else if (h != mCardHTarget) mCardH.animateTo(h, motion::kSelectMs, Easing::EaseOutCubic, nowMs);
        mCardHTarget = h;
        mCardH.update(nowMs);
        const Rect l = listRect();
        mScroll.setExtent(l.y, l.h, (double)mWanted.size() * kRowH);
        mScroll.advance(nowMs);
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    bool RelinkSheet::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mOpen || mClosing) return false;
        const Rect l = listRect();
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            int id = mSearch.contains(local) ? kSearchId : mDone.contains(local) ? kDoneId : -1;
            if (l.contains(local))
                for (const auto &x : mLocate)
                    if (x.second.contains(local)) id = locateId(x.first);
            mHover.setHovered(id);
            return true;
        }
        case Gesture::Type::Scroll:
            if (l.contains(local)) mScroll.scrollBy(g.delta.y); // a list that cannot scroll swallows it: a modal
            return true;
        case Gesture::Type::Click:
            if (!cardRect().contains(local) || mDone.contains(local)) { close(); return true; } // the scrim closes, as cosmo's
            if (mSearch.contains(local)) { if (onSearchFolder) onSearchFolder(); return true; }
            if (l.contains(local))
                for (const auto &x : mLocate)
                    if (x.second.contains(local)) { if (onLocate) onLocate(x.first); return true; }
            return true;
        default:
            return true; // a modal swallows the rest
        }
    }

    bool RelinkSheet::handleKey(const KeyEvent &e)
    {
        if (!isOpen()) return false;
        if (e.type == KeyEvent::Type::Down && (e.keyCode == 27 || e.keyCode == 13)) close(); // Escape, Enter: Done
        return true; // a modal owns the keyboard
    }

    void RelinkSheet::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value();
        drawRoundedRect(t, Rect{0, 0, W, H}, 0.0, Paint::filled(surface::scrim(1.0))); // the group's opacity fades it
        const Rect c = cardRect();
        drawRoundedRect(t, c, radius::control(), Paint::filledStroked(palette::popover(), palette::border(), 1.0));
        t.setFill(palette::foreground());
        const std::string title = tr("Relink");
        t.drawText(title, c.x + kPad, c.y + 34.0, 14.0, font::sansSemiBold());
        const size_t n = mWanted.size();
        const std::string sub = n == 0 ? tr("every file the song names is found") : fillIn(trn((long)n, "{1} file is missing", "{1} files are missing"), std::to_string(n));
        t.setFill(palette::mutedForeground());
        const double tx = c.x + kPad + t.measureText(title, 14.0, font::sansSemiBold()) + 10.0;
        t.drawText(textfit::ellipsize(t, sub, std::max(0.0, c.right() - kPad - tx), 11.0, font::sans()), tx, c.y + 34.0, 11.0, font::sans());

        // the rows, clipped to the list and scrolled; each at its eased place, at its eased fade
        const Rect l = listRect();
        mLocate.clear();
        t.save();
        t.clipRect(l.x, l.y, l.w, l.h);
        if (n == 0 && mRows.empty())
        {
            t.setFill(palette::mutedForeground());
            const std::string msg = tr("Nothing is missing.");
            t.drawText(msg, c.x + (c.w - t.measureText(msg, 12.0, font::sans())) * 0.5, textfit::baseline(l.y + kRowH * 0.5, 12.0), 12.0, font::sans());
        }
        for (const auto &r : mRows)
        {
            const double top = l.y + r.y.value() - mScroll.value(), a = r.a.value();
            if (top + kRowH < l.y || top > l.bottom() || a <= 0.001) continue;
            const Rect loc{c.right() - kPad - kLocateW, top + (kRowH - 28.0) * 0.5, kLocateW, 28.0};
            if (!r.leaving) mLocate.emplace_back(r.path, loc);
            const double room = std::max(0.0, loc.x - 12.0 - (c.x + kPad));
            t.setFill(fade(palette::foreground(), a));
            t.drawText(textfit::ellipsize(t, r.name, room, 12.0, font::sans()), c.x + kPad, top + 19.0, 12.0, font::sans());
            const std::string where = (r.folder.empty() ? std::string() : r.folder + "  \xC2\xB7  ") + r.users;
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText(textfit::ellipsize(t, where, room, 10.0, font::mono()), c.x + kPad, top + 35.0, 10.0, font::mono());
            const double hv = mHover.amount(locateId(r.path));
            drawRoundedRect(t, loc, radius::control(), Paint::filled(fade(lerpColor(palette::secondary(), palette::card(), hv), a)));
            t.setFill(fade(palette::secondaryForeground(), a));
            const std::string lbl = textfit::ellipsize(t, tr("Locate\xE2\x80\xA6"), loc.w - 8.0, 11.0, font::sans());
            t.drawText(lbl, loc.x + (loc.w - t.measureText(lbl, 11.0, font::sans())) * 0.5, textfit::baseline(loc.y + loc.h * 0.5, 11.0), 11.0, font::sans());
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
        // Search a Folder… (secondary, left) · Done (primary, right)
        mDone = Rect{c.right() - kPad - 88.0, c.bottom() - kFooterH + (kFooterH - 30.0) * 0.5, 88.0, 30.0};
        mSearch = Rect{c.x + kPad, mDone.y, 150.0, 30.0};
        drawRoundedRect(t, mSearch, radius::control(), Paint::filled(lerpColor(palette::secondary(), palette::card(), mHover.amount(kSearchId))));
        t.setFill(palette::secondaryForeground());
        const std::string s = textfit::ellipsize(t, tr("Search a Folder\xE2\x80\xA6"), mSearch.w - 12.0, 12.0, font::sans());
        t.drawText(s, mSearch.x + (mSearch.w - t.measureText(s, 12.0, font::sans())) * 0.5, textfit::baseline(mSearch.y + mSearch.h * 0.5, 12.0), 12.0, font::sans());
        drawRoundedRect(t, mDone, radius::control(), Paint::filled(palette::primary()));
        const double hv = mHover.amount(kDoneId);
        if (hv > 0.001) drawRoundedRect(t, mDone, radius::control(), Paint::filled(palette::hoverWash(hv)));
        t.setFill(palette::primaryForeground());
        const std::string done = textfit::ellipsize(t, tr("Done"), mDone.w - 12.0, 12.0, font::sansMedium());
        t.drawText(done, mDone.x + (mDone.w - t.measureText(done, 12.0, font::sansMedium())) * 0.5, textfit::baseline(mDone.y + mDone.h * 0.5, 12.0), 12.0,
                   font::sansMedium());
    }
}
}
