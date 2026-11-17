#include "HomeScreen.h"
#include "../../../interstellar/app/widgets/Glyphs.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include "../../../cosmo/widgets/Icons.h"
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
        // the sidebar's anchored blocks — cosmo's numbers, as Interstellar's Home uses them
        constexpr double kWordBaseline = 96.0;
        constexpr double kLogoDividerY = 159.0;
        constexpr double kActionsTop = 183.0;
        constexpr double kActionGap = 6.0;
        constexpr double kBottomBlockH = 92.0;
        constexpr double kWordMaxPx = 46.0, kWordMinPx = 24.0;
        const char *kWord = "solaris";
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string lengthLabel(double beats, double bpm)
        {
            const double s = bpm > 0 ? beats * 60.0 / bpm : 0.0;
            char buf[32];
            std::snprintf(buf, sizeof buf, "%d:%02d", (int)(s / 60.0), (int)std::fmod(s, 60.0));
            return buf;
        }
        // R-FAM-7: a dashed outline (Artboard has no dash primitive: short strokes along each edge, as cosmo's Home draws its own)
        void dashedRect(IRenderTarget &t, const Rect &r, const Color &c, double sw, double dash = 5.0)
        {
            t.setStroke(c, sw);
            auto edge = [&](double x0, double y0, double x1, double y1) {
                const double len = std::hypot(x1 - x0, y1 - y0);
                if (len <= 0.0) return;
                const double ux = (x1 - x0) / len, uy = (y1 - y0) / len;
                for (double d = 0; d < len; d += dash * 2)
                {
                    const double e = std::min(d + dash, len);
                    t.beginPath(); t.moveTo(x0 + ux * d, y0 + uy * d); t.lineTo(x0 + ux * e, y0 + uy * e); t.strokePath();
                }
            };
            edge(r.x, r.y, r.right(), r.y);
            edge(r.right(), r.y, r.right(), r.bottom());
            edge(r.right(), r.bottom(), r.x, r.bottom());
            edge(r.x, r.bottom(), r.x, r.y);
        }
        // A deterministic hash of the song's name → the heights of its plate's bars.
        unsigned hashOf(const std::string &s)
        {
            unsigned h = 2166136261u;
            for (unsigned char c : s) h = (h ^ c) * 16777619u;
            return h;
        }
    }

    HomeScreen::HomeScreen()
    {
        clipToBounds = true;
        mNew.alpha.set(0.0);
    }

    std::string HomeScreen::versionLabel() { return std::string("solaris v") + SOLARIS_VERSION; }
    Rect HomeScreen::newCardLive() const { return toScreen(mNew.live()); }

    void HomeScreen::bind(const solaris::AppModel &m)
    {
        std::vector<Card> next;
        for (const auto &r : m.recents)  // the service keeps them newest first
        {
            Card c;
            bool known = false;
            for (auto &old : mCards)
                if (old.info.path == r.path) { c = old; known = true; break; }
            if (!known && mHasModel) { c.alpha.set(0.0); c.fadeIn = true; } // a card that arrives fades in at its slot
            c.info = r;
            next.push_back(std::move(c));
        }
        mCards = std::move(next);
        mHasModel = true;
        mNewWanted = !mCards.empty(); // R-FAM-7: the "New song" card ends a grid that has songs
    }

    HomeScreen::Grid HomeScreen::grid() const
    {
        const double availW = std::max(kMinCard, width.value() - kPad - gridLeft());
        const int cols = std::max(1, (int)std::floor((availW + kGap) / (kMinCard + kGap)));
        const double cardW = (availW - (cols - 1) * kGap) / cols;
        return Grid{cols, cardW, cardW * 9.0 / 16.0 + kMetaH};
    }

    Rect HomeScreen::toScreen(const Rect &c) const { return Rect{gridLeft() + c.x, gridTop() + c.y - mScroll.value(), c.w, c.h}; }
    void HomeScreen::accessibles(std::vector<A11yNode> &out)
    {
        static const char *kActions[] = {N_("New song"), N_("Open song\xE2\x80\xA6"), N_("From template\xE2\x80\xA6")};
        for (int i = 0; i < 3; ++i) out.push_back(a11yNode("home.action." + std::to_string(i), "button", tr(kActions[i]), worldRect(*this, actionRect(i))));
        out.push_back(a11yNode("home.settings", "button", tr("Settings"), worldRect(*this, settingsRect())));
        const Rect vp = gridViewport();
        if (mNewWanted && newCardLive().bottom() >= vp.y && newCardLive().y <= vp.bottom())
            out.push_back(a11yNode("home.newCard", "button", tr("New song"), worldRect(*this, newCardLive())));
        for (int i = 0; i < (int)mCards.size(); ++i)
        {
            const Rect r = cardLive(i);
            if (r.w <= 0 || r.bottom() < vp.y || r.y > vp.bottom()) continue; // scrolled out: not reachable until in view
            const auto &c = mCards[(size_t)i].info;
            out.push_back(a11yNode("home.recent." + c.path, "listitem", c.name.empty() ? c.path : c.name, worldRect(*this, r),
                                   c.missing ? tr("missing") : c.recovery ? tr("unsaved edits to recover") : std::string()));
        }
    }

    Rect HomeScreen::gridViewport() const
    {
        return Rect{gridLeft(), gridTop(), std::max(0.0, width.value() - kPad - gridLeft()), std::max(0.0, height.value() - gridTop())};
    }
    Rect HomeScreen::cardLive(int i) const { return i >= 0 && i < (int)mCards.size() ? toScreen(mCards[i].live()) : Rect{}; }
    Rect HomeScreen::cardTarget(int i) const { return i >= 0 && i < (int)mCards.size() ? toScreen(mCards[i].target) : Rect{}; }
    Rect HomeScreen::actionRect(int i) const { return Rect{kPad, kActionsTop + i * (kActionH + kActionGap), kSidebarW - 2 * kPad, kActionH}; }
    Rect HomeScreen::settingsRect() const { return Rect{kPad, height.value() - kBottomBlockH + 16.0, kSidebarW - 2 * kPad, 18.0}; }
    Rect HomeScreen::emptyButtonRect() const
    {
        const Rect vp = gridViewport();
        return Rect{vp.x + (vp.w - 140.0) * 0.5, vp.y + 96.0, 140.0, 30.0};
    }

    void HomeScreen::layout()
    {
        const Grid g = grid();
        mCols = g.cols;
        int slot = 0;
        for (auto &c : mCards) place(c, slot++, g);
        // R-FAM-7: the "New song" card takes the slot after the last song — and keeps it while it fades out, so the last
        // song forgotten does not send it travelling
        if (mNewWanted || !mNew.placed) place(mNew, slot, g);
        if (mNewWanted) ++slot;
        const int rows = (slot + g.cols - 1) / g.cols;
        const double contentH = rows * g.cardH + std::max(0, rows - 1) * kGap + kPad;
        mScroll.setExtent(gridTop(), std::max(0.0, height.value() - gridTop()), contentH);
    }

    void HomeScreen::place(Card &c, int slot, const Grid &g)
    {
        const int r = slot / g.cols, k = slot % g.cols;
        c.target = Rect{k * (g.cardW + kGap), r * (g.cardH + kGap), g.cardW, g.cardH};
        const bool moved = c.target.x != c.lastTarget.x || c.target.y != c.lastTarget.y || c.target.w != c.lastTarget.w || c.target.h != c.lastTarget.h;
        if (!moved) return;
        if (!c.placed || reducedMotion() || c.alpha.value() <= 0.001)
        {
            // the first placement SETS: there is nowhere to travel from (nor for a card nobody can see)
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
    }

    void HomeScreen::advance(double nowMs)
    {
        mNowMs = nowMs;
        {
            // R-FAM-7: the "New song" card fades with there being songs — the first frame places it
            const double want = mNewWanted ? 1.0 : 0.0;
            if (!mNew.fadeIn) { mNew.alpha.set(want); mNew.fadeIn = true; } // (fadeIn here: "its fade has been placed")
            else if (std::fabs(mNew.alpha.value() - want) > 1e-4 && !mNew.alpha.isAnimating()) mNew.alpha.animateTo(want, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mNew.ax.update(nowMs); mNew.ay.update(nowMs); mNew.aw.update(nowMs); mNew.ah.update(nowMs); mNew.alpha.update(nowMs);
        }
        if (mMark.gone()) mMark.showUp(nowMs, false);   // R-BRAND-2: once, the first time Home is drawn
        mMark.advance(nowMs);
        for (auto &c : mCards)
        {
            if (c.fadeIn) { c.alpha.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); c.fadeIn = false; }
            c.ax.update(nowMs); c.ay.update(nowMs); c.aw.update(nowMs); c.ah.update(nowMs); c.alpha.update(nowMs);
        }
        mScroll.advance(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    int HomeScreen::hoverAt(const Point &p) const
    {
        for (int i = 0; i < 2; ++i)
            if (actionRect(i).contains(p)) return i;
        if (actionRect(2).contains(p)) return 3; // R-PRESET-2: From template… (2 is Settings')
        if (settingsRect().contains(p)) return 2;
        if (!gridViewport().contains(p)) return -1;
        if (mCards.empty()) return emptyButtonRect().contains(p) ? 9 : -1;
        if (mNewWanted && newCardLive().contains(p)) return 8; // R-FAM-7: the dashed "New song" card
        for (int i = 0; i < (int)mCards.size(); ++i)
            if (cardLive(i).contains(p)) return 10 + i;
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
            return gridViewport().contains(local) && mScroll.scrollBy(g.delta.y);
        case Gesture::Type::Click:
        {
            const int h = hoverAt(local);
            if (h == 0 || h == 9 || h == 8) { if (onNewSong) onNewSong(); }
            else if (h == 1) { if (onOpenSong) onOpenSong(); }
            else if (h == 2) { if (onSettings) onSettings(); }
            else if (h == 3) { if (onNewFromTemplate) onNewFromTemplate(g.pos); } // R-PRESET-2
            else if (h >= 10 && onOpenRecent) onOpenRecent(mCards[(size_t)(h - 10)].info.path);
            return true;
        }
        case Gesture::Type::RightClick:
        {
            const int h = hoverAt(local);
            if (h >= 10 && onForgetRecent) onForgetRecent(mCards[(size_t)(h - 10)].info.path);
            return true;
        }
        default:
            break;
        }
        return Segment::handleGesture(g, local);
    }

    void HomeScreen::paintCard(IRenderTarget &t, const Card &c, const Rect &s, double hv) const
    {
        const double a = c.alpha.value() * (c.info.missing ? 0.55 : 1.0);
        const double th = s.w * 9.0 / 16.0;
        const Color bg = brighten(palette::folderChipBg(), 0.06 * hv);
        const Color bd = lerpColor(palette::border(), palette::primaryAlpha(0.7), hv);
        drawRoundedRect(t, s, radius::control(), Paint::filledStroked(fade(bg, a), fade(bd, a), 1.0 + 0.5 * hv));
        // the plate: a song's bars, from its name — deterministic, muted, never the accent
        const Rect plate{s.x + 1.0, s.y + 1.0, s.w - 2.0, th - 1.0};
        drawRoundedRect(t, plate, 0.0, Paint::filled(fade(palette::segmentedBg(), a))); // a token (it was a raw 0x111111 — the same value)
        unsigned h = hashOf(c.info.name.empty() ? c.info.path : c.info.name);
        const int bars = 28;
        const double bw = (plate.w - 32.0) / bars;
        for (int i = 0; i < bars; ++i)
        {
            h = h * 1103515245u + 12345u;
            const double v = 0.18 + 0.82 * ((h >> 16) & 0xff) / 255.0 * (0.55 + 0.45 * std::sin(i * 0.37));
            const double bh = std::max(2.0, (plate.h - 36.0) * std::fabs(v));
            const Rect b{plate.x + 16.0 + i * bw + bw * 0.2, plate.y + (plate.h - bh) * 0.5, bw * 0.6, bh};
            drawRoundedRect(t, b, radius::control(), Paint::filled(fade(palette::whiteAlpha(0.10 + 0.05 * hv), a)));
        }
        const double mx = s.x + 12.0;
        t.setFill(fade(palette::foreground(), a));
        // a missing song has no header to read: it is named by its file, which is what the user moved
        std::string name = c.info.name;
        if (name.empty())
        {
            const auto slash = c.info.path.find_last_of('/');
            name = slash == std::string::npos ? c.info.path : c.info.path.substr(slash + 1);
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".slp") == 0) name.resize(name.size() - 4);
            if (name.empty()) name = tr("Untitled");
        }
        t.drawText(textfit::ellipsize(t, name, s.w - 24.0, 11.0, font::sansMedium()), mx, s.y + th + 18.0, 11.0, font::sansMedium());
        std::string meta;
        if (c.info.missing) meta = tr("missing \xE2\x80\x94 moved or deleted");
        else if (c.info.recovery) meta = tr("unsaved changes kept \xE2\x80\x94 open it to recover them"); // R-SAFE-2
        else
        {
            char bpm[32];
            std::snprintf(bpm, sizeof bpm, "%g bpm", c.info.bpm);
            meta = std::string(bpm) + "  \xC2\xB7  " + lengthLabel(c.info.lengthBeats, c.info.bpm) + "  \xC2\xB7  " +
                   fillIn(trn(c.info.strips, "{1} strip", "{1} strips"), std::to_string(c.info.strips));
        }
        t.setFill(fade(c.info.missing ? palette::destructive() : c.info.recovery ? palette::primary() : palette::mutedForeground(), a));
        t.drawText(textfit::ellipsize(t, meta, s.w - 24.0, 10.0, font::sans()), mx, s.y + th + 33.0, 10.0, font::sans());
    }

    void HomeScreen::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value();
        drawRoundedRect(t, Rect{0, 0, W, H}, 0.0, Paint::filled(palette::background()));
        drawRoundedRect(t, Rect{0, 0, kSidebarW, H}, 0.0, Paint::filled(palette::leftRailBg()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(kSidebarW, 0); t.lineTo(kSidebarW, H); t.strokePath();
        t.beginPath(); t.moveTo(kSidebarW, kHeaderH); t.lineTo(W, kHeaderH); t.strokePath();

        // ── the logo, above the wordmark (R-BRAND-2). Structure in the foreground colour, so
        //    it follows the theme and high contrast; the colour part is the brand's own. ──
        ::arstro::cosmo_v2::brand::draw(t, Rect{kPad - 2.0, 18.0, 36.0, 36.0}, mMark.mark(), mMark.pose(), palette::foreground());

        // ── wordmark, sized to fit; the dot at the MEASURED end of the word ──
        const double px = textfit::fitSize(t, std::string(kWord) + ".", kSidebarW - 2 * kPad, kWordMaxPx, kWordMinPx, font::sansSemiBold(), -0.03);
        mWordPx = px;
        const double sp = -0.03 * px;
        t.setFill(palette::foreground());
        t.drawText(kWord, kPad, kWordBaseline, px, font::sansSemiBold(), sp);
        t.setFill(palette::primary());
        t.drawText(".", kPad + t.measureText(kWord, px, font::sansSemiBold(), sp), kWordBaseline, px, font::sansSemiBold(), sp);
        t.setFill(palette::mutedForeground());
        const double tagW = kSidebarW - 2.0 * kPad;
        t.drawText(textfit::ellipsize(t, tr("Arrange, mix and play a song"), tagW, 11.0, font::sans()), kPad, 118.0, 11.0, font::sans());
        t.drawText(textfit::ellipsize(t, tr("on every audio device you have."), tagW, 11.0, font::sans()), kPad, 133.0, 11.0, font::sans());
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(kPad, kLogoDividerY); t.lineTo(kSidebarW - kPad, kLogoDividerY); t.strokePath();

        for (int i = 0; i < 3; ++i)
        {
            const Rect r = actionRect(i);
            const double hv = mHover.amount(i == 2 ? 3 : i);
            const bool primary = i == 0;
            if (primary) drawRoundedRect(t, r, radius::control(), Paint::filled(brighten(palette::primary(), interaction::kHoverFillLift * hv)));
            else
                drawRoundedRect(t, r, radius::control(),
                                Paint::filledStroked(palette::primaryAlpha(0.10 * hv), lerpColor(palette::border(), palette::primary(), 0.5 * hv), 1.0));
            const Color fg = primary ? palette::primaryForeground() : lerpColor(palette::mutedForeground(), palette::foreground(), 0.6 + 0.4 * hv);
            const Rect ib{r.x + 14.0, r.y + (r.h - 13.0) * 0.5, 13.0, 13.0};
            if (primary) glyph::plus(t, ib, fg, 1.6);
            else if (i == 1) cosmo_v2::icon::folder(t, ib, fg, 1.2);
            else
            {
                // R-PRESET-2: a template — two sheets, one over the other
                drawRoundedRect(t, Rect{ib.x + 3.0, ib.y, ib.w - 3.0, ib.h - 3.0}, radius::hairline(), Paint::stroked(fg, 1.1));
                drawRoundedRect(t, Rect{ib.x, ib.y + 3.0, ib.w - 3.0, ib.h - 3.0}, radius::hairline(), Paint::filledStroked(palette::background(), fg, 1.1));
            }
            t.setFill(fg);
            const char *fam = primary ? font::sansSemiBold() : font::sans();
            t.drawText(textfit::ellipsize(t, primary ? tr("New song") : i == 1 ? tr("Open song\xE2\x80\xA6") : tr("From template\xE2\x80\xA6"), r.w - 44.0, 12.0, fam),
                       r.x + 36.0, textfit::baseline(r.y + r.h * 0.5, 12.0), 12.0, fam);
        }

        const double blockTop = H - kBottomBlockH;
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(kPad, blockTop); t.lineTo(kSidebarW - kPad, blockTop); t.strokePath();
        {
            const Rect r = settingsRect();
            const double hv = mHover.amount(2);
            if (hv > 0.001) drawRoundedRect(t, Rect{r.x - 6.0, r.y - 1.0, r.w + 12.0, r.h + 2.0}, radius::control(), Paint::filled(palette::hoverWash(hv)));
            const Color lc = lerpColor(palette::mutedForeground(), palette::foreground(), 0.7 * hv);
            drawCircle(t, r.x + 5.5, r.y + 9.0, 2.4, Paint::stroked(lc, 1.1));
            drawCircle(t, r.x + 5.5, r.y + 9.0, 4.6, Paint::stroked(lc, 1.1));
            t.setFill(lc);
            t.drawText(textfit::ellipsize(t, tr("Settings"), r.w - 18.0, 11.0, font::sans()), r.x + 18.0, textfit::baseline(r.y + r.h * 0.5, 11.0), 11.0, font::sans());
        }
        t.setFill(fade(palette::mutedForeground(), 0.4));
        t.drawText(versionLabel(), kPad, blockTop + 60.0, 10.0, font::mono()); // the build's (R-FAM-7)

        // ── header ──
        // R-FAM-7: a clock, the words, the count — as cosmo's and Interstellar's Home (a clock says "recent")
        {
            const Rect ic = headingIconRect();
            const double cx = ic.x + ic.w * 0.5, cy = ic.y + ic.h * 0.5;
            const Color c = palette::mutedForeground();
            drawCircle(t, cx, cy, ic.w * 0.5 - 0.6, Paint::stroked(c, 1.2));
            t.setStroke(c, 1.2);
            t.beginPath(); t.moveTo(cx, cy - 3.4); t.lineTo(cx, cy); t.lineTo(cx + 2.6, cy + 1.5); t.strokePath();
        }
        const double hx = headingTextLeft();
        const std::string title = tr("Recent songs");
        t.setFill(palette::foreground());
        t.drawText(title, hx, textfit::baseline(kHeaderH * 0.5, 13.0), 13.0, font::sansSemiBold());
        t.setFill(fade(palette::mutedForeground(), 0.6));
        t.drawText(std::to_string(mCards.size()), hx + t.measureText(title, 13.0, font::sansSemiBold()) + 8.0,
                   textfit::baseline(kHeaderH * 0.5, 10.0), 10.0, font::sans());

        // ── the grid, clipped to its viewport ──
        const Rect vp = gridViewport();
        t.save();
        t.clipRect(vp.x, vp.y, vp.w, vp.h);
        if (mCards.empty() && mHasModel)
        {
            const std::string s = textfit::ellipsize(t, tr("No songs yet \xE2\x80\x94 start one."), vp.w - 16.0, 13.0, font::sans());
            t.setFill(palette::mutedForeground());
            t.drawText(s, vp.x + (vp.w - t.measureText(s, 13.0, font::sans())) * 0.5, vp.y + 72.0, 13.0, font::sans());
            const Rect b = emptyButtonRect();
            const double hv = mHover.amount(9);
            drawRoundedRect(t, b, radius::control(), Paint::filledStroked(palette::primaryAlpha(0.10 + 0.08 * hv),
                                                                           lerpColor(palette::border(), palette::primary(), 0.6 + 0.4 * hv), 1.0));
            t.setFill(palette::foreground());
            const std::string ns = textfit::ellipsize(t, tr("New song"), b.w - 12.0, 12.0, font::sans());
            t.drawText(ns, b.x + (b.w - t.measureText(ns, 12.0, font::sans())) * 0.5, textfit::baseline(b.y + b.h * 0.5, 12.0), 12.0, font::sans());
        }
        for (int i = 0; i < (int)mCards.size(); ++i)
        {
            const Rect s = toScreen(mCards[i].live());
            if (s.bottom() < vp.y || s.y > vp.bottom() || mCards[i].alpha.value() <= 0.001) continue; // culled, the hover test's band
            paintCard(t, mCards[i], s, mHover.amount(10 + i));
        }
        // R-FAM-7: the dashed "New song" card after the last song — an outline, a plus, the words; never a filled card
        if (const double na = mNew.alpha.value(); na > 0.001)
        {
            const Rect s = newCardLive();
            if (s.bottom() >= vp.y && s.y <= vp.bottom())
            {
                const double hv = mHover.amount(8);
                if (hv > 0.001) drawRoundedRect(t, s, radius::control(), Paint::filled(palette::primaryAlpha(0.06 * hv * na)));
                dashedRect(t, Rect{s.x + 0.5, s.y + 0.5, s.w - 1.0, s.h - 1.0}, fade(lerpColor(palette::border(), palette::primary(), 0.7 * hv), na), 1.0);
                const Color fg = fade(lerpColor(palette::mutedForeground(), palette::foreground(), hv), na);
                const std::string ns = textfit::ellipsize(t, tr("New song"), s.w - 24.0, 12.0, font::sans());
                const double tw = t.measureText(ns, 12.0, font::sans()), gx = s.x + (s.w - (13.0 + 8.0 + tw)) * 0.5, gy = s.y + s.h * 0.5;
                glyph::plus(t, Rect{gx, gy - 6.5, 13.0, 13.0}, fg, 1.4);
                t.setFill(fg);
                t.drawText(ns, gx + 21.0, textfit::baseline(gy, 12.0), 12.0, font::sans());
            }
        }
        t.restore();
        mScroll.drawBar(t, W - 6.0);
    }
}
}
