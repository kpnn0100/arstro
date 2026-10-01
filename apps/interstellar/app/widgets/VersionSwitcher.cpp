#include "VersionSwitcher.h"
#include "CommandLine.h"
#include "Glyphs.h"
#include "TextFit.h"
#include <algorithm>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kPad = 4.0;
        constexpr double kHeaderH = 24.0;
        constexpr double kDividerH = 9.0;
        constexpr double kEdge = 8.0;          // the menu never comes closer than this to the window edge
        constexpr double kGapBelowBar = 6.0;
        constexpr double kNamePx = 11.0;
        std::string shortCommit(const std::string &c) { return c.size() > 7 ? c.substr(0, 7) : c; }
        Color fade(Color c, double a) { c.a *= a; return c; }
    }

    VersionSwitcher::VersionSwitcher()
    {
        height.set(kHeight);
        width.set(232.0);
    }

    void VersionSwitcher::bind(const interstellar::AppModel &m)
    {
        // a different version: keep the old label to fade out under the new one
        if (!mShownId.empty() && m.currentTimeline != mShownId)
        {
            const int ci = currentIndex();
            if (ci >= 0) { mPrevShown = mTimelines[ci]; mHasPrev = true; mSwapPending = true; }
        }
        mTimelines = m.timelines;
        mCurrent = m.currentTimeline;
        mShownId = mCurrent;
    }

    int VersionSwitcher::currentIndex() const
    {
        for (int i = 0; i < (int)mTimelines.size(); ++i)
            if (mTimelines[i].id == mCurrent) return i;
        return -1;
    }

    VersionSwitcher::Layout VersionSwitcher::menuLayout() const
    {
        Layout L{};
        const Point o = worldTransform().apply(Point{0, 0});
        L.w = kMenuW;
        L.x = 0.0;
        if (o.x + L.x + L.w > mRootW - kEdge) L.x = mRootW - kEdge - L.w - o.x;
        if (o.x + L.x < kEdge) L.x = kEdge - o.x;
        L.y = kHeight + kGapBelowBar;
        const double avail = std::max(0.0, mRootH - (o.y + L.y) - 2.0 * kEdge);
        const double actionsH = kActionCount * kRowH;
        const double fixed = 2 * kPad + kHeaderH + kDividerH + actionsH;
        const double content = (double)mTimelines.size() * kRowH;
        L.headerH = kHeaderH;
        L.listTop = L.y + kPad + kHeaderH;
        L.listH = std::clamp(avail - fixed, std::min(content, kRowH), std::max(content, kRowH));
        if (L.listH > content) L.listH = content;
        L.dividerY = L.listTop + L.listH + kDividerH * 0.5;
        L.actionsTop = L.listTop + L.listH + kDividerH;
        L.total = fixed + L.listH;
        return L;
    }

    Rect VersionSwitcher::menuRect() const
    {
        const Layout L = menuLayout();
        return Rect{L.x, L.y, L.w, L.total};
    }

    Rect VersionSwitcher::versionRowRect(int i) const
    {
        const Layout L = menuLayout();
        return Rect{L.x, L.listTop + i * kRowH - mScroll.value(), L.w, kRowH};
    }

    Rect VersionSwitcher::actionRect(int a) const
    {
        const Layout L = menuLayout();
        return Rect{L.x, L.actionsTop + a * kRowH, L.w, kRowH};
    }

    bool VersionSwitcher::actionEnabled(int a) const
    {
        const int ci = currentIndex();
        if (ci < 0) return a == NewVersion ? !mTimelines.empty() : false;
        const auto &tl = mTimelines[ci];
        const bool derived = !tl.base.empty();
        switch (a)
        {
        case NewVersion: return true;
        case Pin: return derived || tl.colourPinned;
        case Freeze: return derived || tl.cutFrozen;
        case Rebase: return derived;
        default: return false;
        }
    }

    std::string VersionSwitcher::actionLabel(int a) const
    {
        const int ci = currentIndex();
        const interstellar::TimelineModel *tl = ci >= 0 ? &mTimelines[ci] : nullptr;
        switch (a)
        {
        case NewVersion: return "New version\xE2\x80\xA6";
        case Pin: return (tl && tl->colourPinned) ? "Unpin colour" : "Pin colour to base";
        case Freeze: return (tl && tl->cutFrozen) ? "Thaw cut" : "Freeze cut";
        case Rebase: return "Rebase onto base";
        default: return "";
        }
    }

    int VersionSwitcher::hoverIdAt(const Point &p) const
    {
        if (mWantOpen)
        {
            const Layout L = menuLayout();
            if (Rect{L.x, L.listTop, L.w, L.listH}.contains(p))
                for (int i = 0; i < (int)mTimelines.size(); ++i)
                    if (versionRowRect(i).contains(p)) return 100 + i;
            for (int a = 0; a < kActionCount; ++a)
                if (actionRect(a).contains(p)) return 10 + a;
        }
        if (prevRect().contains(p)) return 0;
        if (nextRect().contains(p)) return 2;
        if (bodyRect().contains(p)) return 1;
        return -1;
    }

    bool VersionSwitcher::hitTestSelf(const Point &p) const
    {
        // Open: capture everything, so a click outside closes the menu instead of reaching the
        // control under it. Closed: just the chrome.
        if (mWantOpen) return true;
        return localBounds().contains(p);
    }

    void VersionSwitcher::stepVersion(int dir)
    {
        const int ci = currentIndex();
        const int j = ci + dir;
        if (ci < 0 || j < 0 || j >= (int)mTimelines.size()) return;
        emit("timeline open " + cmd::quote(mTimelines[j].id));
    }

    bool VersionSwitcher::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
            mHover.setHovered(hoverIdAt(local));
            return true;
        case Gesture::Type::Down:
            return true;
        case Gesture::Type::Scroll:
            if (mWantOpen)
            {
                const Layout L = menuLayout();
                if (Rect{L.x, L.listTop, L.w, L.listH}.contains(local)) mScroll.scrollBy(g.delta.y);
                return true;   // the open menu owns the wheel
            }
            return false;
        case Gesture::Type::Click:
        {
            const int id = hoverIdAt(local);
            if (mWantOpen)
            {
                if (id >= 100)
                {
                    const int i = id - 100;
                    if (mTimelines[i].id != mCurrent) emit("timeline open " + cmd::quote(mTimelines[i].id));
                    close();
                }
                else if (id >= 10)
                {
                    const int a = id - 10;
                    if (!actionEnabled(a)) return true;
                    const int ci = currentIndex();
                    const std::string tl = ci >= 0 ? cmd::quote(mTimelines[ci].id) : std::string();
                    close();   // close BEFORE acting: the action may open a modal (design rule §5)
                    switch (a)
                    {
                    case NewVersion: if (onNewVersion) onNewVersion(); break;
                    case Pin: emit(std::string(mTimelines[ci].colourPinned ? "timeline unpin " : "timeline pin ") + tl); break;
                    case Freeze: emit(std::string(mTimelines[ci].cutFrozen ? "timeline thaw " : "timeline freeze ") + tl); break;
                    case Rebase: emit("timeline rebase " + tl); break;
                    }
                }
                else if (menuRect().contains(local))
                {
                    // inside the card but on no item (header, divider): stay open
                }
                else
                {
                    close();
                    if (id == 0) stepVersion(-1);
                    else if (id == 2) stepVersion(+1);
                }
                return true;
            }
            if (id == 0) { stepVersion(-1); return true; }
            if (id == 2) { stepVersion(+1); return true; }
            if (id == 1) { open(); return true; }
            break;
        }
        default:
            break;
        }
        return Segment::handleGesture(g, local);
    }

    void VersionSwitcher::advance(double nowMs)
    {
        if (mWantOpen != mAppliedOpen)
        {
            mOpenAmt.animateTo(mWantOpen ? 1.0 : 0.0, mWantOpen ? motion::kDropdownMs : motion::kModalCloseMs,
                               Easing::EaseOutCubic, nowMs);
            if (mWantOpen)
            {
                // Open with the current version in view (minimum travel, no yank to the middle).
                const Layout L = menuLayout();
                mScroll.setExtent(L.listTop, L.listH, (double)mTimelines.size() * kRowH);
                const int ci = currentIndex();
                if (ci >= 0) mScroll.reveal(ci * kRowH, kRowH);
            }
            mAppliedOpen = mWantOpen;
        }
        mOpenAmt.update(nowMs);
        if (mSwapPending)
        {
            mSwap.set(0.0);
            mSwap.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mSwapPending = false;
        }
        mSwap.update(nowMs);
        if (mHasPrev && !mSwap.isAnimating()) mHasPrev = false;
        const Layout L = menuLayout();
        mScroll.setExtent(L.listTop, L.listH, (double)mTimelines.size() * kRowH);
        mScroll.advance(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    void VersionSwitcher::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = kHeight;
        drawRoundedRect(t, Rect{0, 0, w, h}, radius::control(),
                        Paint::filledStroked(palette::segmentedBg(), palette::border(), 1.0));
        const int ci = currentIndex();

        // ‹ and › — dimmed at the ends of the list (disabled reads as such, not as broken).
        const bool canPrev = ci > 0, canNext = ci >= 0 && ci + 1 < (int)mTimelines.size();
        for (int k = 0; k < 2; ++k)
        {
            const Rect r = k == 0 ? prevRect() : nextRect();
            const double hv = mHover.amount(k == 0 ? 0 : 2);
            if (hv > 0.001) drawRoundedRect(t, Rect{r.x + 1, r.y + 1, r.w - 2, r.h - 2}, radius::hairline(), Paint::filled(palette::hoverWash(hv)));
            const bool en = k == 0 ? canPrev : canNext;
            Color c = en ? lerpColor(palette::mutedForeground(), palette::foreground(), 0.6 * hv) : fade(palette::mutedForeground(), 0.45);
            const Rect gb{r.x + (r.w - 10) * 0.5, r.y + (r.h - 10) * 0.5, 10, 10};
            if (k == 0) glyph::chevronLeft(t, gb, c); else glyph::chevronRightSmall(t, gb, c);
        }
        // Hairlines between the arrows and the body.
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(kArrowW, 3); t.lineTo(kArrowW, h - 3); t.strokePath();
        t.beginPath(); t.moveTo(w - kArrowW, 3); t.lineTo(w - kArrowW, h - 3); t.strokePath();

        const Rect body = bodyRect();
        const double bhv = std::max(mHover.amount(1), mOpenAmt.value());
        if (bhv > 0.001) drawRoundedRect(t, Rect{body.x + 1, body.y + 1, body.w - 2, body.h - 2}, radius::hairline(), Paint::filled(palette::hoverWash(bhv)));

        const double sw = mSwap.value();
        if (ci < 0)
        {
            paintBody(t, nullptr, 1.0);
            return;
        }
        if (mHasPrev && sw < 0.999) paintBody(t, &mPrevShown, 1.0 - sw);
        paintBody(t, &mTimelines[ci], mHasPrev ? sw : 1.0);
    }

    void VersionSwitcher::paintBody(IRenderTarget &t, const interstellar::TimelineModel *tlp, double alpha) const
    {
        const double h = kHeight;
        const Rect body = bodyRect();
        double x = body.x + 8.0;
        const double cy = h * 0.5;
        if (!tlp)
        {
            t.setFill(fade(palette::mutedForeground(), alpha));
            t.drawText("no version", x, textfit::baseline(cy, 10.0), 10.0, font::sans());
            return;
        }
        const auto &tl = *tlp;
        const bool locked = tl.colourPinned || tl.cutFrozen;
        // Right part first, measured, so the name gets what is actually left (R5). The tag gives way
        // before the name does: "@commit · frozen" → "@commit" (or "frozen") → nothing but the lock
        // — the dropdown still says all of it.
        const double caretW = 10.0;
        const std::string commit = tl.colourPinned ? "@" + shortCommit(tl.pinCommit) : std::string();
        std::vector<std::string> tags;
        if (tl.colourPinned && tl.cutFrozen) tags.push_back(commit + " \xC2\xB7 frozen");
        if (tl.colourPinned) tags.push_back(commit);
        else if (tl.cutFrozen) tags.push_back("frozen");
        tags.push_back(std::string());
        const std::string fullName = tl.name.empty() ? tl.id : tl.name;
        const double nameW = t.measureText(fullName, kNamePx, font::sansMedium());
        const double x0 = x + (locked ? 13.0 : 0.0);
        std::string tag;
        for (const auto &cand : tags)
        {
            tag = cand;
            const double tw = cand.empty() ? 0.0 : t.measureText(cand, 9.0, font::mono()) + 8.0;
            if (body.right() - 8.0 - caretW - 4.0 - tw - x0 >= std::min(nameW, 72.0)) break;
        }
        const double tagTextW = tag.empty() ? 0.0 : t.measureText(tag, 9.0, font::mono());
        const double tagW = tag.empty() ? 0.0 : tagTextW + 8.0;   // + the gap before it
        if (locked)
        {
            glyph::lock(t, Rect{x, cy - 5.5, 9.0, 11.0}, fade(palette::primary(), alpha));
            x += 13.0;
        }
        const double nameRoom = body.right() - 8.0 - caretW - 4.0 - tagW - x;
        const std::string name = textfit::ellipsize(t, fullName, nameRoom, kNamePx, font::sansMedium());
        t.setFill(fade(palette::foreground(), alpha));
        t.drawText(name, x, textfit::baseline(cy, kNamePx), kNamePx, font::sansMedium());
        if (!tag.empty())
        {
            t.setFill(fade(palette::primary(), alpha));
            t.drawText(tag, body.right() - 8.0 - caretW - 4.0 - tagTextW, textfit::baseline(cy, 9.0), 9.0, font::mono());
        }
        glyph::caretDown(t, Rect{body.right() - 8.0 - caretW, cy - 5, caretW, 10}, palette::mutedForeground());
    }

    void VersionSwitcher::onOverlay(IRenderTarget &t) const
    {
        const double a = mOpenAmt.value();
        if (a <= 0.001) return;
        const Layout L = menuLayout();
        // Height reveal + fade, both from the one eased amount.
        const double revealH = L.total * (0.92 + 0.08 * a);
        const Rect card{L.x, L.y - 4.0 * (1.0 - a), L.w, revealH};
        t.save();
        t.clipRect(card.x - 12, card.y - 2, card.w + 24, card.h + 16);
        // soft shadow, then the card
        drawRoundedRect(t, Rect{card.x, card.y + 3, card.w, card.h}, radius::control(), Paint::filled(Color{0, 0, 0, 0.35 * a}));
        drawRoundedRect(t, card, radius::control(), Paint::filledStroked(fade(palette::popover(), a), fade(palette::border(), a), 1.0));
        const double dy = card.y - L.y;   // content rides the card's entrance offset

        // header
        t.setFill(fade(palette::mutedForeground(), a));
        t.drawText("VERSIONS", L.x + 10.0, L.y + dy + kPad + kHeaderH * 0.5 + 9.0 * 0.35, 9.0, font::sansSemiBold(), 0.13 * 9.0);
        const std::string count = std::to_string(mTimelines.size());
        t.drawText(count, L.x + L.w - 10.0 - t.measureText(count, 9.0, font::mono()), L.y + dy + kPad + kHeaderH * 0.5 + 9.0 * 0.35, 9.0, font::mono());

        // the version list, in its own clipped, scrolled viewport
        t.save();
        t.clipRect(L.x, L.listTop + dy, L.w, L.listH);
        for (int i = 0; i < (int)mTimelines.size(); ++i)
        {
            if (!mScroll.bandVisible(i * kRowH, kRowH)) continue;
            const auto &tl = mTimelines[i];
            Rect r = versionRowRect(i);
            r.y += dy;
            const bool cur = tl.id == mCurrent;
            const double hv = mHover.amount(100 + i);
            if (cur) drawRoundedRect(t, Rect{r.x + 3, r.y + 1, r.w - 6, r.h - 2}, radius::control(), Paint::filled(palette::primaryAlpha(0.14 * a)));
            if (hv > 0.001) drawRoundedRect(t, Rect{r.x + 3, r.y + 1, r.w - 6, r.h - 2}, radius::control(), Paint::filled(palette::hoverWash(hv * a)));
            double x = r.x + 10.0 + tl.depth * space::indent();
            const double cy = r.y + r.h * 0.5;
            if (tl.depth > 0)
            {
                // a tree elbow, so the indent reads as "derived from" rather than as a typo
                Color ec = fade(palette::border(), a * 2.0);
                glyph::line(t, x - 8.0, cy - 7.0, x - 8.0, cy, ec, 1.0);
                glyph::line(t, x - 8.0, cy, x - 3.0, cy, ec, 1.0);
            }
            // right-side badges, measured first
            double right = r.right() - 10.0;
            auto badge = [&](const std::string &s, const Color &c, const char *fam) {
                const double bw = t.measureText(s, 9.0, fam);
                right -= bw;
                t.setFill(fade(c, a));
                t.drawText(s, right, textfit::baseline(cy, 9.0), 9.0, fam);
                right -= 7.0;
            };
            if (tl.danglingDeltas > 0) badge(std::to_string(tl.danglingDeltas) + " dangling", palette::destructive(), font::sans());
            if (tl.overrides > 0) badge("\xCE\x94" + std::to_string(tl.overrides), palette::mutedForeground(), font::mono());
            if (tl.cutFrozen) badge("frozen", palette::mutedForeground(), font::sans());
            if (tl.colourPinned) badge("@" + shortCommit(tl.pinCommit), palette::primary(), font::mono());
            if (tl.colourPinned || tl.cutFrozen)
            {
                glyph::lock(t, Rect{x, cy - 5.0, 8.0, 10.0}, fade(cur ? palette::primary() : palette::mutedForeground(), a));
                x += 12.0;
            }
            const std::string nm = textfit::ellipsize(t, tl.name.empty() ? tl.id : tl.name, right - x, kNamePx,
                                                      cur ? font::sansMedium() : font::sans());
            t.setFill(fade(cur ? palette::primary() : palette::foreground(), a));
            t.drawText(nm, x, textfit::baseline(cy, kNamePx), kNamePx, cur ? font::sansMedium() : font::sans());
        }
        t.restore();
        mScroll.drawBar(t, L.x + L.w - 2.0, a);

        // divider + actions (pinned under the list)
        t.setStroke(fade(palette::border(), a), 1.0);
        t.beginPath(); t.moveTo(L.x + 8, L.dividerY + dy); t.lineTo(L.x + L.w - 8, L.dividerY + dy); t.strokePath();
        for (int k = 0; k < kActionCount; ++k)
        {
            Rect r = actionRect(k);
            r.y += dy;
            const bool en = actionEnabled(k);
            const double hv = en ? mHover.amount(10 + k) : 0.0;
            if (hv > 0.001) drawRoundedRect(t, Rect{r.x + 3, r.y + 1, r.w - 6, r.h - 2}, radius::control(), Paint::filled(palette::hoverWash(hv * a)));
            Color c = en ? lerpColor(palette::foreground(), palette::white(), 0.3 * hv) : fade(palette::mutedForeground(), 0.7);
            std::string label = actionLabel(k);
            if (k == Rebase)
            {
                const int ci = currentIndex();
                if (ci >= 0 && mTimelines[ci].danglingDeltas > 0)
                    label += " \xC2\xB7 " + std::to_string(mTimelines[ci].danglingDeltas) + " dangling";
            }
            label = textfit::ellipsize(t, label, r.w - 20.0, kNamePx, font::sans());
            t.setFill(fade(c, a));
            t.drawText(label, r.x + 10.0, textfit::baseline(r.y + r.h * 0.5, kNamePx), kNamePx, font::sans());
        }
        t.restore();
    }
}
}
