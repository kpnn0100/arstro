#include "HomeScreen.h"
#include "CommandLine.h"
#include "Glyphs.h"
#include "TextFit.h"
#include "../../../cosmo/widgets/Icons.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        // the sidebar's anchored blocks, named (cosmo's numbers, cosmo's reasons)
        constexpr double kWordBaseline = 96.0;
        constexpr double kLogoDividerY = 159.0;
        constexpr double kActionsTop = 183.0;
        constexpr double kActionGap = 6.0;
        constexpr double kBottomBlockH = 92.0;   // divider → settings link → version line → pad
        constexpr double kWordMaxPx = 46.0;
        constexpr double kWordMinPx = 24.0;
        constexpr double kCoverEdge = 480;
        const char *kWord = "interstellar";
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string sizeLabel(long long b)
        {
            char buf[32];
            if (b >= (1LL << 30)) std::snprintf(buf, sizeof buf, "%.1f GB", b / double(1LL << 30));
            else if (b >= (1LL << 20)) std::snprintf(buf, sizeof buf, "%.0f MB", b / double(1LL << 20));
            else std::snprintf(buf, sizeof buf, "%.0f KB", b / 1024.0);
            return buf;
        }
        void dashedRect(IRenderTarget &t, const Rect &r, const Color &c, double sw)
        {
            t.setStroke(c, sw);
            auto edge = [&](double x0, double y0, double x1, double y1) {
                const double len = std::hypot(x1 - x0, y1 - y0);
                if (len <= 0) return;
                const double ux = (x1 - x0) / len, uy = (y1 - y0) / len;
                for (double d = 0; d < len; d += 10.0)
                {
                    const double e = std::min(d + 5.0, len);
                    t.beginPath(); t.moveTo(x0 + ux * d, y0 + uy * d); t.lineTo(x0 + ux * e, y0 + uy * e); t.strokePath();
                }
            };
            edge(r.x, r.y, r.right(), r.y);
            edge(r.right(), r.y, r.right(), r.bottom());
            edge(r.right(), r.bottom(), r.x, r.bottom());
            edge(r.x, r.bottom(), r.x, r.y);
        }
    }

    HomeScreen::HomeScreen() { clipToBounds = true; }

    void HomeScreen::bind(const interstellar::AppModel &m)
    {
        // newest first (R-UI-1)
        std::vector<interstellar::RecentModel> rec = m.recents;
        std::stable_sort(rec.begin(), rec.end(), [](const auto &a, const auto &b) { return a.lastOpened > b.lastOpened; });
        // keep existing cards (and their eased geometry) by path; Segment-free, so a vector is fine
        std::vector<Card> next;
        next.reserve(rec.size());
        for (const auto &r : rec)
        {
            Card c;
            bool known = false;
            for (auto &old : mCards) if (old.info.path == r.path) { c = old; known = true; break; }
            if (!known && mHasModel && !mCards.empty()) { c.alpha.set(0.0); c.fadeIn = true; }
            c.info = r;
            if (c.coverKey != r.coverPath)
            {
                c.coverKey = r.coverPath;
                interstellar::Raster ras;
                if (!r.coverPath.empty() && thumbnail && thumbnail(r.coverPath, 0.0, (int)kCoverEdge, ras) && !ras.empty())
                {
                    c.cover.set(ras);
                    if (c.coverRetry) { c.coverAlpha.set(0.0); c.coverFade = true; }   // late: fade in
                }
                else c.cover = ImageSlot{};
                c.coverRetry = false;
            }
            next.push_back(std::move(c));
        }
        mCards = std::move(next);
        mHasModel = true;
    }

    HomeScreen::Grid HomeScreen::grid() const
    {
        const double availW = std::max(kMinCard, width.value() - kPad - gridLeft());
        int cols = (int)std::floor((availW + kGap) / (kMinCard + kGap));
        cols = std::max(1, cols);
        const double cardW = (availW - (cols - 1) * kGap) / cols;
        return Grid{cols, cardW, cardW * 9.0 / 16.0 + kMetaH};
    }

    Rect HomeScreen::toScreen(const Rect &c) const { return Rect{gridLeft() + c.x, gridTop() + c.y - mScroll.value(), c.w, c.h}; }
    Rect HomeScreen::gridViewport() const { return Rect{gridLeft(), gridTop(), std::max(0.0, width.value() - kPad - gridLeft()), std::max(0.0, height.value() - gridTop())}; }
    Rect HomeScreen::cardLive(int i) const { return i >= 0 && i < (int)mCards.size() ? toScreen(mCards[i].live()) : Rect{0, 0, 0, 0}; }
    Rect HomeScreen::cardTarget(int i) const { return i >= 0 && i < (int)mCards.size() ? toScreen(mCards[i].target) : Rect{0, 0, 0, 0}; }
    Rect HomeScreen::newCardLive() const { return toScreen(mNewCard.live()); }

    Rect HomeScreen::actionRect(int i) const
    {
        return Rect{kPad, kActionsTop + i * (kActionH + kActionGap), kSidebarW - 2 * kPad, kActionH};
    }
    Rect HomeScreen::settingsRect() const
    {
        return Rect{kPad, height.value() - kBottomBlockH + 16.0, kSidebarW - 2 * kPad, 18.0};
    }
    Rect HomeScreen::emptyChipRect() const
    {
        const Rect vp = gridViewport();
        return Rect{vp.x + (vp.w - 140.0) * 0.5, vp.y + 96.0, 140.0, 30.0};
    }

    void HomeScreen::layout()
    {
        const Grid g = grid();
        mCols = g.cols;
        int slot = 0;
        auto place = [&](Card &c) {
            const int r = slot / g.cols, k = slot % g.cols;
            c.target = Rect{k * (g.cardW + kGap), r * (g.cardH + kGap), g.cardW, g.cardH};
            ++slot;
            // A target that moved re-aims the tween — only then, since layout() runs every frame.
            // The FIRST placement sets: there is nowhere to fly in from (R-G-1a's lesson).
            const bool changed = c.target.x != c.lastTarget.x || c.target.y != c.lastTarget.y ||
                                 c.target.w != c.lastTarget.w || c.target.h != c.lastTarget.h;
            if (!changed) return;
            if (!c.placed || reducedMotion())
            {
                c.ax.set(c.target.x); c.ay.set(c.target.y); c.aw.set(c.target.w); c.ah.set(c.target.h);
                c.placed = true;
            }
            else
            {
                c.ax.animateTo(c.target.x, motion::kReflowMs, Easing::EaseOutCubic, mNowMs);
                c.ay.animateTo(c.target.y, motion::kReflowMs, Easing::EaseOutCubic, mNowMs);
                c.aw.animateTo(c.target.w, motion::kReflowMs, Easing::EaseOutCubic, mNowMs);
                c.ah.animateTo(c.target.h, motion::kReflowMs, Easing::EaseOutCubic, mNowMs);
            }
            c.lastTarget = c.target;
        };
        for (auto &c : mCards) place(c);
        place(mNewCard);
        const int rows = (slot + g.cols - 1) / g.cols;
        const double contentH = rows * g.cardH + std::max(0, rows - 1) * kGap + kPad;
        mScroll.setExtent(gridTop(), std::max(0.0, height.value() - gridTop()), contentH);
    }

    void HomeScreen::advance(double nowMs)
    {
        mNowMs = nowMs;
        if (!mInit) { mSkeleton.set(mLoading ? 1.0 : 0.0); mLoadingApplied = mLoading; mInit = true; }
        if (mLoading != mLoadingApplied)
        {
            mSkeleton.animateTo(mLoading ? 1.0 : 0.0, motion::kScrollMs, Easing::EaseOutCubic, nowMs);
            mLoadingApplied = mLoading;
        }
        mSkeleton.update(nowMs);
        for (auto &c : mCards)
        {
            if (c.coverFade) { c.coverAlpha.animateTo(1.0, motion::kScrollMs, Easing::EaseOutCubic, nowMs); c.coverFade = false; }
            c.coverAlpha.update(nowMs);
        }
        for (auto &c : mCards)
        {
            if (c.fadeIn) { c.alpha.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); c.fadeIn = false; }
            c.ax.update(nowMs); c.ay.update(nowMs); c.aw.update(nowMs); c.ah.update(nowMs); c.alpha.update(nowMs);
        }
        mNewCard.ax.update(nowMs); mNewCard.ay.update(nowMs); mNewCard.aw.update(nowMs); mNewCard.ah.update(nowMs);
        mScroll.advance(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    int HomeScreen::hoverAt(const Point &p) const
    {
        for (int i = 0; i < 2; ++i) if (actionRect(i).contains(p)) return i;
        if (settingsRect().contains(p)) return 2;
        if (!gridViewport().contains(p) || mLoading) return -1;
        if (mCards.empty()) return emptyChipRect().contains(p) ? 9 : -1;
        for (int i = 0; i < (int)mCards.size(); ++i) if (cardLive(i).contains(p)) return 10 + i;
        if (newCardLive().contains(p)) return 3;
        return -1;
    }

    bool HomeScreen::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
            mHover.setHovered(hoverAt(local));
            return true;
        case Gesture::Type::Down:
            return true;
        case Gesture::Type::Scroll:
            if (gridViewport().contains(local)) return mScroll.scrollBy(g.delta.y);
            return false;
        case Gesture::Type::Click:
        {
            const int h = hoverAt(local);
            if (h == 0 || h == 3 || h == 9) { if (onNewProject) onNewProject(); }
            else if (h == 1) { if (onOpenProject) onOpenProject(); }
            else if (h == 2) { if (onSettings) onSettings(); }
            else if (h >= 10) { if (onOpenRecent) onOpenRecent(h - 10); }
            return true;
        }
        default:
            break;
        }
        return Segment::handleGesture(g, local);
    }

    std::string HomeScreen::relativeDate(long long when) const
    {
        if (when <= 0) return "never";
        const long long d = mNowUnix - when;
        if (d < 60) return "just now";
        if (d < 3600) return std::to_string(d / 60) + "m ago";
        if (d < 86400) return std::to_string(d / 3600) + "h ago";
        if (d < 7 * 86400) return std::to_string(d / 86400) + "d ago";
        std::time_t tt = (std::time_t)when;
        std::tm tmv{};
#ifdef _WIN32
        gmtime_s(&tmv, &tt);  // MSVC/MinGW-ucrt param order: (dest, source)
#else
        gmtime_r(&tt, &tmv);
#endif
        char buf[16];
        std::strftime(buf, sizeof buf, "%b %e", &tmv);
        return buf;
    }

    void HomeScreen::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value();
        drawRoundedRect(t, Rect{0, 0, W, H}, 0.0, Paint::filled(palette::background()));
        drawRoundedRect(t, Rect{0, 0, kSidebarW, H}, 0.0, Paint::filled(palette::leftRailBg()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(kSidebarW, 0); t.lineTo(kSidebarW, H); t.strokePath();
        t.beginPath(); t.moveTo(kSidebarW, kHeaderH); t.lineTo(W, kHeaderH); t.strokePath();

        // ── wordmark: sized to fit, the dot at the MEASURED end of the word ──
        const double room = kSidebarW - 2 * kPad;
        const double px = textfit::fitSize(t, std::string(kWord) + ".", room, kWordMaxPx, kWordMinPx, font::sansSemiBold(), -0.03);
        mWordPx = px;
        const double sp = -0.03 * px;
        t.setFill(palette::foreground());
        t.drawText(kWord, kPad, kWordBaseline, px, font::sansSemiBold(), sp);
        t.setFill(palette::primary());
        t.drawText(".", kPad + t.measureText(kWord, px, font::sansSemiBold(), sp), kWordBaseline, px, font::sansSemiBold(), sp);
        t.setFill(palette::mutedForeground());
        t.drawText("Group, grade, cut and deliver", kPad, 118.0, 11.0, font::sans());
        t.drawText("the colour of a production.", kPad, 133.0, 11.0, font::sans());
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(kPad, kLogoDividerY); t.lineTo(kSidebarW - kPad, kLogoDividerY); t.strokePath();

        // ── actions ──
        for (int i = 0; i < 2; ++i)
        {
            const Rect r = actionRect(i);
            const double hv = mHover.amount(i);
            const bool primary = i == 0;
            if (primary)
                drawRoundedRect(t, r, radius::control(), Paint::filled(brighten(palette::primary(), interaction::kHoverFillLift * hv)));
            else
                drawRoundedRect(t, r, radius::control(), Paint::filledStroked(palette::primaryAlpha(0.10 * hv),
                                                                               lerpColor(palette::border(), palette::primary(), 0.5 * hv), 1.0));
            const Color fg = primary ? palette::primaryForeground() : lerpColor(palette::mutedForeground(), palette::foreground(), 0.6 + 0.4 * hv);
            const Rect ib{r.x + 14.0, r.y + (r.h - 13.0) * 0.5, 13.0, 13.0};
            if (primary) glyph::plus(t, ib, fg, 1.6);
            else cosmo_v2::icon::folder(t, ib, fg, 1.2);
            t.setFill(fg);
            t.drawText(primary ? "New project" : "Open project\xE2\x80\xA6", r.x + 36.0, textfit::baseline(r.y + r.h * 0.5, 12.0), 12.0,
                       primary ? font::sansSemiBold() : font::sans());
        }

        // ── foot: settings + version ──
        const double blockTop = H - kBottomBlockH;
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(kPad, blockTop); t.lineTo(kSidebarW - kPad, blockTop); t.strokePath();
        {
            const Rect r = settingsRect();
            const double hv = mHover.amount(2);
            if (hv > 0.001) drawRoundedRect(t, Rect{r.x - 6.0, r.y - 1.0, r.w + 12.0, r.h + 2.0}, radius::control(), Paint::filled(palette::hoverWash(hv)));
            const Color lc = lerpColor(palette::mutedForeground(), palette::foreground(), 0.7 * hv);
            const Rect ib{r.x, r.y + (r.h - 11.0) * 0.5, 11.0, 11.0};
            drawCircle(t, ib.x + 5.5, ib.y + 5.5, 2.4, Paint::stroked(lc, 1.1));
            drawCircle(t, ib.x + 5.5, ib.y + 5.5, 4.6, Paint::stroked(lc, 1.1));
            t.setFill(lc);
            t.drawText("Settings", r.x + 18.0, textfit::baseline(r.y + r.h * 0.5, 11.0), 11.0, font::sans());
        }
        t.setFill(fade(palette::mutedForeground(), 0.4));
        t.drawText("interstellar v0.1.0", kPad, blockTop + 60.0, 10.0, font::mono());

        // ── header ──
        const double hx = gridLeft();
        const Rect clock{hx, kHeaderH * 0.5 - 6.5, 13.0, 13.0};
        drawCircle(t, clock.x + 6.5, clock.y + 6.5, 5.5, Paint::stroked(palette::mutedForeground(), 1.2));
        glyph::line(t, clock.x + 6.5, clock.y + 6.5, clock.x + 6.5, clock.y + 3.2, palette::mutedForeground(), 1.2);
        glyph::line(t, clock.x + 6.5, clock.y + 6.5, clock.x + 9.2, clock.y + 6.5, palette::mutedForeground(), 1.2);
        const std::string title = "Recent Projects";
        t.setFill(palette::foreground());
        t.drawText(title, hx + 20.0, textfit::baseline(kHeaderH * 0.5, 13.0), 13.0, font::sansSemiBold());
        if (!mLoading)
        {
            t.setFill(fade(palette::mutedForeground(), 0.6));
            t.drawText(std::to_string(mCards.size()), hx + 20.0 + t.measureText(title, 13.0, font::sansSemiBold()) + 8.0,
                       textfit::baseline(kHeaderH * 0.5, 10.0), 10.0, font::sans());
        }

        // ── grid, clipped to its viewport ──
        const Rect vp = gridViewport();
        t.save();
        t.clipRect(vp.x, vp.y, vp.w, vp.h);
        const double sk = mSkeleton.value();
        const double realAll = 1.0 - sk;
        const double real = realAll;
        const Grid g = grid();
        if (sk > 0.001)
        {
            // skeleton cards at the REAL geometry, so nothing moves when they fill
            const double breathe = 0.75 + 0.25 * std::sin(mNowMs / motion::kPulseMs * 6.28318530718);
            for (int i = 0; i < kSkeletonCount; ++i)
            {
                const int r = i / g.cols, k = i % g.cols;
                const Rect c = toScreen(Rect{k * (g.cardW + kGap), r * (g.cardH + kGap), g.cardW, g.cardH});
                if (c.y > H) break;
                const double th = c.w * 9.0 / 16.0;
                const double a = sk * breathe;
                drawRoundedRect(t, c, radius::control(), Paint::filledStroked(fade(palette::folderChipBg(), sk), fade(palette::border(), sk), 1.0));
                drawRoundedRect(t, Rect{c.x, c.y, c.w, th}, 0.0, Paint::filled(fade(surface::skeleton(), a)));
                drawRoundedRect(t, Rect{c.x + 12.0, c.y + th + 12.0, c.w * 0.55, 8.0}, radius::control(), Paint::filled(fade(surface::skeleton(), a)));
                drawRoundedRect(t, Rect{c.x + 12.0, c.y + th + 27.0, c.w * 0.35, 7.0}, radius::control(), Paint::filled(fade(surface::skeleton(), a * 0.8)));
            }
        }
        if (real > 0.001)
        {
            if (mCards.empty() && mHasModel)
            {
                const std::string s = "No projects yet \xE2\x80\x94 open some footage to start one.";
                const std::string se = textfit::ellipsize(t, s, vp.w - 16.0, 13.0, font::sans());
                t.setFill(fade(palette::mutedForeground(), real));
                t.drawText(se, vp.x + (vp.w - t.measureText(se, 13.0, font::sans())) * 0.5, vp.y + 72.0, 13.0, font::sans());
                const Rect chip = emptyChipRect();
                const double hv = mHover.amount(9);
                drawRoundedRect(t, chip, radius::control(), Paint::filledStroked(fade(palette::primaryAlpha(0.10 + 0.08 * hv), real),
                                                                                  fade(lerpColor(palette::border(), palette::primary(), 0.6 + 0.4 * hv), real), 1.0));
                const std::string cl = "New project";
                t.setFill(fade(palette::foreground(), real));
                t.drawText(cl, chip.x + (chip.w - t.measureText(cl, 12.0, font::sans())) * 0.5, textfit::baseline(chip.y + chip.h * 0.5, 12.0), 12.0, font::sans());
            }
            for (int i = 0; i < (int)mCards.size(); ++i)
            {
                const Card &c = mCards[i];
                const Rect s = toScreen(c.live());
                if (s.bottom() < vp.y || s.y > vp.bottom()) continue;   // culled, same test as hover
                if (c.alpha.value() <= 0.001) continue;
                const double real = realAll * c.alpha.value();
                const double hv = mHover.amount(10 + i);
                const double th = s.w * 9.0 / 16.0;
                const Color bg = brighten(palette::folderChipBg(), 0.06 * hv);
                const Color bd = lerpColor(palette::border(), palette::primaryAlpha(0.7), hv);
                drawRoundedRect(t, s, radius::control(), Paint::filledStroked(fade(bg, real), fade(bd, real), 1.0 + 0.5 * hv));
                const Rect cover{s.x + 1.0, s.y + 1.0, s.w - 2.0, th - 1.0};
                drawRoundedRect(t, cover, 0.0, Paint::filled(fade(surface::thumbPlaceholder(), real)));
                if (c.cover.has())
                {
                    t.pushLayer(real * c.coverAlpha.value());
                    c.cover.drawCover(t, cover);
                    t.popLayer();
                }
                else
                    glyph::film(t, Rect{cover.x + cover.w * 0.5 - 13.0, cover.y + cover.h * 0.5 - 10.0, 26.0, 20.0}, fade(palette::mutedForeground(), 0.5 * real), 1.2);
                // meta band: name (ellipsized against the card), footage · size, last opened
                const double mx = s.x + 12.0;
                const std::string date = relativeDate(c.info.lastOpened);
                const double dw = t.measureText(date, 9.0, font::sans());
                t.setFill(fade(palette::foreground(), real));
                t.drawText(textfit::ellipsize(t, c.info.name, s.w - 24.0, 11.0, font::sansMedium()), mx, s.y + th + 18.0, 11.0, font::sansMedium());
                std::string meta = std::to_string(c.info.sourceCount) + (c.info.sourceCount == 1 ? " source" : " sources");
                if (c.info.sizeBytes > 0) meta += "  \xC2\xB7  " + sizeLabel(c.info.sizeBytes);
                t.setFill(fade(palette::mutedForeground(), real));
                t.drawText(textfit::ellipsize(t, meta, s.w - 24.0 - dw - 8.0, 10.0, font::sans()), mx, s.y + th + 33.0, 10.0, font::sans());
                t.setFill(fade(palette::mutedForeground(), 0.6 * real));
                t.drawText(date, s.right() - 12.0 - dw, s.y + th + 33.0, 9.0, font::sans());
            }
            if (!mCards.empty())
            {
                const Rect s = newCardLive();
                const double th = s.w * 9.0 / 16.0;
                const Rect box{s.x, s.y, s.w, th};
                const double hv = mHover.amount(3);
                dashedRect(t, box, fade(lerpColor(palette::whiteAlpha(0.22), palette::primary(), 0.5 * hv), real), 1.0 + 0.4 * hv);
                const Color nc = fade(lerpColor(palette::mutedForeground(), palette::foreground(), 0.6 * hv), real);
                glyph::plus(t, Rect{box.x + box.w * 0.5 - 10.0, box.y + box.h * 0.5 - 16.0, 20.0, 20.0}, nc, 1.4);
                t.setFill(nc);
                t.drawText("New project", box.x + (box.w - t.measureText("New project", 11.0, font::sans())) * 0.5, box.y + box.h * 0.5 + 18.0, 11.0, font::sans());
            }
        }
        t.restore();
        mScroll.drawBar(t, W - 6.0);
    }
}
}
