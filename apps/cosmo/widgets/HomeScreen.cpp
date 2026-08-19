#include "HomeScreen.h"
#include "Icons.h"
#include "TextMetrics.h"
#include "../Theme.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kSidebarW = 300.0;
        constexpr double kPad = 32.0;       // px-8
        constexpr double kHeaderH = 60.0;   // recent-projects header band
        constexpr double kGap = 16.0;       // grid gap-4
        constexpr double kMinCard = 220.0;
        constexpr double kMetaH = projectcard::kMetaH;  // card meta band (name + row), shared with the loading screen
        constexpr double kActionH = 34.0;
        // ── the sidebar's two anchored blocks, in named parts ──
        // Everything below used to be spelled as 183.0 / 137.0 / 92.0 at the point of use, which
        // is why nobody noticed the two blocks could collide: the numbers gave no clue what they
        // were made of. minSidebarHeight() sums these, so the window minimum follows the layout
        // instead of being a guess that happened to work at the size someone tested.
        constexpr double kLogoDividerY = 159.0;      // the rule under the wordmark + tagline
        constexpr double kActionsTop = 183.0;        // first action button (kLogoDividerY + 24)
        constexpr double kActionGap = 6.0;
        constexpr int kActionCount = 3;              // New Project / Open Project / Import Catalog
        constexpr double kLinkFirstY = 16.0;         // below the bottom divider
        constexpr double kLinkStride = 20.0;
        constexpr double kLinkH = 18.0;
        constexpr int kLinkCount = 3;                // Settings / What's New / Help & Documentation
        constexpr double kVersionY = 92.0;           // baseline, below the divider
        constexpr double kBottomPad = 24.0;          // breathing room under the version line
        /** Divider to window bottom: the links, then the version baseline, then the pad. */
        constexpr double kBottomBlockH = kVersionY + kBottomPad + 21.0;   // = 137, as drawn
        constexpr double kBlockGap = 32.0;           // minimum air between actions and the links
        constexpr double gridTopConst() { return kHeaderH + 24.0; }
        constexpr double kSearchW = 168.0, kSearchH = 26.0;
        /** The narrowest the search field may be squeezed to before the header gives up the
         *  long title instead. Below ~72 px the glyph plus two or three characters is all that
         *  is left, and a field you cannot read your own query in is not a field. */
        constexpr double kSearchMinW = 72.0;
        constexpr double kHeaderGap = 12.0;   // air between the title block and the field
        constexpr double kScrollGlideMs = 180.0;  // grid-scroll ease (R-G-1)
        /** Grid reflow when the column count changes. Longer than the scroll glide: a card
         *  moving AND resizing is a bigger change than the same card sliding, and reading it
         *  as one motion needs the extra time. */
        constexpr double kReflowMs = 260.0;

        std::string lower(std::string s)
        {
            for (char &c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        }

        // ── inline lucide-ish icons (stroke, fit to box) ──
        void iconPlus(IRenderTarget &t, const Rect &b, const Color &c, double sw)
        {
            const double cx = b.x + b.w / 2, cy = b.y + b.h / 2, r = b.w * 0.42;
            t.setStroke(c, sw);
            t.beginPath(); t.moveTo(cx - r, cy); t.lineTo(cx + r, cy); t.strokePath();
            t.beginPath(); t.moveTo(cx, cy - r); t.lineTo(cx, cy + r); t.strokePath();
        }
        void iconFolder(IRenderTarget &t, const Rect &b, const Color &c, double sw)
        {
            t.setStroke(c, sw);
            t.beginPath();
            t.moveTo(b.x + 1, b.y + b.h * 0.30);
            t.lineTo(b.x + b.w * 0.42, b.y + b.h * 0.30);
            t.lineTo(b.x + b.w * 0.52, b.y + b.h * 0.15);
            t.lineTo(b.x + b.w - 1, b.y + b.h * 0.15);
            t.lineTo(b.x + b.w - 1, b.y + b.h - 1);
            t.lineTo(b.x + 1, b.y + b.h - 1);
            t.closePath(); t.strokePath();
        }
        void iconClock(IRenderTarget &t, const Rect &b, const Color &c, double sw)
        {
            const double cx = b.x + b.w / 2, cy = b.y + b.h / 2, r = b.w * 0.42;
            drawCircle(t, cx, cy, r, Paint::stroked(c, sw));
            t.setStroke(c, sw);
            t.beginPath(); t.moveTo(cx, cy); t.lineTo(cx, cy - r * 0.6); t.strokePath();
            t.beginPath(); t.moveTo(cx, cy); t.lineTo(cx + r * 0.5, cy); t.strokePath();
        }
        void iconSearch(IRenderTarget &t, const Rect &b, const Color &c, double sw)
        {
            const double r = b.w * 0.30, cx = b.x + r + 1, cy = b.y + r + 1;
            drawCircle(t, cx, cy, r, Paint::stroked(c, sw));
            t.setStroke(c, sw);
            t.beginPath(); t.moveTo(cx + r * 0.8, cy + r * 0.8); t.lineTo(b.x + b.w - 1, b.y + b.h - 1); t.strokePath();
        }
        void iconGear(IRenderTarget &t, const Rect &b, const Color &c, double sw)
        {
            const double cx = b.x + b.w / 2, cy = b.y + b.h / 2;
            drawCircle(t, cx, cy, b.w * 0.22, Paint::stroked(c, sw));
            drawCircle(t, cx, cy, b.w * 0.42, Paint::stroked(c, sw));
        }
        void iconSpark(IRenderTarget &t, const Rect &b, const Color &c, double sw)
        {
            const double cx = b.x + b.w / 2, cy = b.y + b.h / 2, r = b.w * 0.42;
            t.setStroke(c, sw);
            t.beginPath(); t.moveTo(cx, cy - r); t.lineTo(cx, cy + r); t.strokePath();
            t.beginPath(); t.moveTo(cx - r, cy); t.lineTo(cx + r, cy); t.strokePath();
            t.beginPath(); t.moveTo(cx - r * 0.6, cy - r * 0.6); t.lineTo(cx + r * 0.6, cy + r * 0.6); t.strokePath();
            t.beginPath(); t.moveTo(cx - r * 0.6, cy + r * 0.6); t.lineTo(cx + r * 0.6, cy - r * 0.6); t.strokePath();
        }
        void iconHelp(IRenderTarget &t, const Rect &b, const Color &c, double sw)
        {
            const double cx = b.x + b.w / 2, cy = b.y + b.h / 2, r = b.w * 0.42;
            drawCircle(t, cx, cy, r, Paint::stroked(c, sw));
            t.setFill(c);
            t.drawText("?", cx - 2.4, cy + 3.5, 9.0, font::sansMedium());
        }

        void dashedRoundRect(IRenderTarget &t, const Rect &r, const Color &c, double sw, double dash = 5.0)
        {
            t.setStroke(c, sw);
            auto edge = [&](double x0, double y0, double x1, double y1) {
                const double len = std::hypot(x1 - x0, y1 - y0);
                const double ux = (x1 - x0) / len, uy = (y1 - y0) / len;
                for (double d = 0; d < len; d += dash * 2)
                {
                    const double e = std::min(d + dash, len);
                    t.beginPath(); t.moveTo(x0 + ux * d, y0 + uy * d); t.lineTo(x0 + ux * e, y0 + uy * e); t.strokePath();
                }
            };
            edge(r.x, r.y, r.x + r.w, r.y);
            edge(r.x + r.w, r.y, r.x + r.w, r.y + r.h);
            edge(r.x + r.w, r.y + r.h, r.x, r.y + r.h);
            edge(r.x, r.y + r.h, r.x, r.y);
        }
    }

    HomeScreen::HomeScreen()
    {
        clipToBounds = true;

        // Thumbnails live under a clip child so they never spill over the header
        // when the grid is scrolled; it is input-transparent so card clicks reach us.
        mGridClip = std::make_shared<Segment>();
        mGridClip->clipToBounds = true;
        mGridClip->inputTransparent = true;
        addChild(mGridClip);

        TextBoxStyle st;
        st.idle = {Paint::filledStroked(palette::input(), palette::border(), 1.0), radius::control()};
        st.focused = {Paint::filledStroked(palette::input(), palette::ring(), 1.0), radius::control()};
        st.text = {palette::foreground(), 11.0, font::sans()};
        st.placeholder = {palette::mutedForeground(), 11.0, font::sans()};
        st.caretColor = palette::primary();
        mSearch = std::make_shared<TextBox>(st);
        mSearch->placeholder = "Search\xE2\x80\xA6";
        addChild(mSearch);
    }

    double HomeScreen::contentX() const { return kSidebarW; }
    double HomeScreen::gridTop() const { return gridTopConst(); }

    Rect HomeScreen::actionRect(int i) const
    {
        // Below the logo block + divider; three stacked full-width buttons.
        const double top = kActionsTop + i * (kActionH + kActionGap);
        return Rect{kPad, top, kSidebarW - 2 * kPad, kActionH};
    }

    double HomeScreen::minSidebarHeight()
    {
        // The two blocks are anchored to OPPOSITE edges — actions to the top, links to the
        // bottom — so the window's minimum is their sum plus air. Summed here, once, so the
        // host cannot hold a stale copy of it (the old 400 px minimum let the "Settings /
        // What's New / Help & Documentation" block overlap the action buttons).
        const double actionsBottom = kActionsTop + kActionCount * kActionH
                                     + (kActionCount - 1) * kActionGap;
        return actionsBottom + kBlockGap + kBottomBlockH;
    }

    double HomeScreen::minContentWidth()
    {
        // Sidebar + the grid's left pad + one card at kMinCard + the right pad. Below this the
        // grid would have to render a card narrower than its own minimum.
        const double grid = kSidebarW + kPad + kMinCard + kPad;
        // ...and the header row has its own floor: the SHORT title plus its air plus the
        // field's floor. Summed rather than assumed to be smaller, so a future change to the
        // sidebar or the title cannot silently make the header the binding constraint without
        // the minimum moving with it.
        const double header = kSidebarW + kPad + (20.0 + estimateTextWidth("Recent", 13.0) + 8.0
                                                  + estimateTextWidth("00", 10.0))
                            + kHeaderGap + kSearchMinW + kPad;
        return std::max(grid, header);
    }

    double HomeScreen::minContentHeight()
    {
        // The grid's own need: the header, one card row at the minimum card width, and a pad.
        const double oneRow = gridTopConst() + kMinCard * 9.0 / 16.0 + kMetaH + kPad;
        return std::max(minSidebarHeight(), oneRow);
    }

    Rect HomeScreen::bottomLinkRect(int i) const
    {
        const double h = height.value();
        const double blockTop = h - kBottomBlockH;
        return Rect{kPad, blockTop + kLinkFirstY + i * kLinkStride, kSidebarW - 2 * kPad, kLinkH};
    }

    // ── the header row shares one line between a title and a field (R5/R6) ──────────────
    // At the minimum window width the title ran straight under the search box: the field was
    // pinned to the right at a fixed 168 px and the title was drawn from the left with no
    // knowledge of it, so "Recent Projects" simply overlapped it. Two lines cannot both be
    // fixed on one row. The title is the more important of the two, so it is measured first
    // and the field takes what is left — and when even the floor does not fit, the title gives
    // up its long form rather than the field disappearing.
    std::string HomeScreen::headerTitle() const
    {
        const double room = width.value() - kPad - kSearchMinW - kHeaderGap - (contentX() + kPad);
        return titleBlockW("Recent Projects") <= room ? "Recent Projects" : "Recent";
    }

    double HomeScreen::titleBlockW(const std::string &title) const
    {
        // Clock glyph + gap, the title, then the count — the same three pieces onPaint draws,
        // measured with the same estimator it uses, because a box measured one way and drawn
        // another is how the overlap got in.
        return 20.0 + estimateTextWidth(title, 13.0) + 8.0
             + estimateTextWidth(std::to_string(visibleCount()), 10.0);
    }

    Rect HomeScreen::searchRect() const
    {
        const double titleEnd = contentX() + kPad + titleBlockW(headerTitle());
        const double x = std::max(titleEnd + kHeaderGap, width.value() - kPad - kSearchW);
        const double w = std::max(kSearchMinW, width.value() - kPad - x);
        return Rect{x, (kHeaderH - kSearchH) / 2, w, kSearchH};
    }

    void HomeScreen::setRecents(const std::vector<CardInfo> &cards)
    {
        // Grow the reusable thumbnail pool (Segment has no removeChild).
        while (mThumbPool.size() < cards.size())
        {
            auto iv = std::make_shared<ImageView>();
            iv->setFit(ImageView::Fit::Cover);
            iv->inputTransparent = true;
            iv->visible = false;
            mGridClip->addChild(iv);
            mThumbPool.push_back(iv);
        }
        mCards.clear();
        for (size_t i = 0; i < cards.size(); ++i)
        {
            Card c; c.info = cards[i];
            c.thumb = mThumbPool[i];
            c.thumb->clearImage();
            c.thumb->visible = true;
            mCards.push_back(std::move(c));
        }
        for (size_t i = cards.size(); i < mThumbPool.size(); ++i) mThumbPool[i]->visible = false;
        mScrollY = 0.0;  // new list starts at the top (snap, not a glide)
        mScrollYAnim.set(0.0); mScrollIssued = 0.0;
        applyFilter();
        layout();
    }

    void HomeScreen::setThumbnail(int recentIndex, const uint8_t *rgba, int w, int h)
    {
        for (auto &c : mCards)
            if (c.info.recentIndex == recentIndex && c.thumb) { c.thumb->setImage(rgba, w, h); break; }
    }

    int HomeScreen::visibleCount() const
    {
        int n = 0; for (const auto &c : mCards) if (c.shown) ++n; return n;
    }

    void HomeScreen::applyFilter()
    {
        const std::string q = lower(mSearch ? mSearch->text : std::string());
        for (auto &c : mCards)
            c.shown = q.empty() || lower(c.info.name).find(q) != std::string::npos;
    }

    void HomeScreen::relayoutGrid()
    {
        const double gridLeft = contentX() + kPad;
        const double availW = std::max(kMinCard, width.value() - kPad - gridLeft);
        int cols = (int)std::floor((availW + kGap) / (kMinCard + kGap));
        if (cols < 1) cols = 1;
        const double cardW = (availW - (cols - 1) * kGap) / cols;
        const double thumbH = cardW * 9.0 / 16.0;
        const double cardH = thumbH + kMetaH;

        int slot = 0;
        auto place = [&](Rect &out) {
            const int r = slot / cols, cIdx = slot % cols;
            out = Rect{cIdx * (cardW + kGap), r * (cardH + kGap), cardW, cardH};  // content coords
            ++slot;
        };
        for (auto &c : mCards)
        {
            if (!c.shown) { if (c.thumb) c.thumb->visible = false; continue; }
            place(c.rect);
            // R-G-1: the column count changes DISCRETELY as the window resizes — 4 across
            // becomes 3 at one particular width — so every card's size and position jumps at
            // that instant. Easing the live geometry toward the new target makes the reflow
            // travel. The first placement must not animate, or opening Home would fly every
            // card in from the origin; `placed` is that distinction, per card, because a card
            // added by a later refreshHome() is also new.
            if (!c.placed || artboard::reducedMotion())
            {
                c.ax.set(c.rect.x); c.ay.set(c.rect.y); c.aw.set(c.rect.w); c.ah.set(c.rect.h);
                c.placed = true;
            }
            else
            {
                c.ax.animateTo(c.rect.x, kReflowMs, Easing::EaseOutCubic, mNowMs);
                c.ay.animateTo(c.rect.y, kReflowMs, Easing::EaseOutCubic, mNowMs);
                c.aw.animateTo(c.rect.w, kReflowMs, Easing::EaseOutCubic, mNowMs);
                c.ah.animateTo(c.rect.h, kReflowMs, Easing::EaseOutCubic, mNowMs);
            }
            if (c.thumb)
            {
                // The thumbnail rides the SAME eased values as the chrome, or the two would
                // separate mid-reflow and the image would slide inside its own card.
                const Rect lv = c.live();
                c.thumb->visible = true;
                c.thumb->x.set(lv.x);
                c.thumb->y.set(lv.y - scrollY());   // grid-clip local space (eased scroll)
                c.thumb->width.set(lv.w);
                c.thumb->height.set(lv.w * 9.0 / 16.0);
            }
        }
        place(mNewCardRect);  // trailing New-Project card
        const int rows = (slot + cols - 1) / cols;
        mContentH = rows * cardH + std::max(0, rows - 1) * kGap;

        const double viewH = height.value() - gridTop();
        const double maxScroll = std::max(0.0, mContentH - viewH + kPad);
        if (mScrollY > maxScroll) mScrollY = maxScroll;
        if (mScrollY < 0) mScrollY = 0;
    }

    void HomeScreen::scrollBy(double delta)
    {
        mScrollY -= delta * 60.0;   // wheel up (positive) scrolls content up
        layout();
    }

    void HomeScreen::layout()
    {
        const double gridLeft = contentX() + kPad, gy = gridTop();
        const double availW = std::max(0.0, width.value() - kPad - gridLeft);
        mGridClip->x.set(gridLeft); mGridClip->y.set(gy);
        mGridClip->width.set(availW); mGridClip->height.set(std::max(0.0, height.value() - gy));
        relayoutGrid();
        const Rect sr = searchRect();
        mSearch->x.set(sr.x); mSearch->y.set(sr.y);
        mSearch->width.set(sr.w); mSearch->height.set(sr.h);
    }

    void HomeScreen::advance(double nowMs)
    {
        if (mSearch && mSearch->text != mLastSearch)
        {
            mLastSearch = mSearch->text;
            applyFilter();
            layout();
        }

        // Hover feedback (R-G-3): each region cross-fades independently. isHovered()
        // is false while the pointer is over the search child, so it clears then too.
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);

        // R-G-1: drive the reflow, and re-place each thumbnail on this frame's eased values so
        // the image and its chrome stay one object while the grid rearranges.
        mNowMs = nowMs;
        bool reflowing = false;
        for (auto &c : mCards)
        {
            c.ax.update(nowMs); c.ay.update(nowMs); c.aw.update(nowMs); c.ah.update(nowMs);
            if (c.ax.isAnimating() || c.ay.isAnimating() || c.aw.isAnimating() || c.ah.isAnimating())
                reflowing = true;
            if (c.shown && c.thumb)
            {
                const Rect lv = c.live();
                c.thumb->x.set(lv.x);
                c.thumb->y.set(lv.y - scrollY());
                c.thumb->width.set(lv.w);
                c.thumb->height.set(lv.w * 9.0 / 16.0);
            }
        }
        (void)reflowing;

        // Ease the grid scroll toward its target so the wheel glides instead of snapping.
        if (mScrollY != mScrollIssued)
        {
            mScrollYAnim.animateTo(mScrollY, kScrollGlideMs, Easing::EaseOutCubic, nowMs);
            mScrollIssued = mScrollY;
        }
        mScrollYAnim.update(nowMs);
        for (auto &cd : mCards)  // keep the thumbnail children tracking the eased scroll
            if (cd.shown && cd.thumb) cd.thumb->y.set(cd.rect.y - scrollY());

        Segment::advance(nowMs);
    }

    void HomeScreen::regionAt(const Point &local, Region &kind, int &index) const
    {
        kind = Region::None; index = -1;
        for (int i = 0; i < 3; ++i)
            if (actionRect(i).contains(local)) { kind = Region::Action; index = i; return; }
        for (int i = 0; i < 3; ++i)
            if (bottomLinkRect(i).contains(local)) { kind = Region::Link; index = i; return; }
        // grid cards (screen space = grid origin + content rect - eased scroll)
        const double gridLeft = contentX() + kPad, gy = gridTop();
        for (int i = 0; i < (int)mCards.size(); ++i)
        {
            const Card &c = mCards[i];
            if (!c.shown) continue;
            const Rect lv = c.live();   // R-G-1: the eased rect, not the target
            const Rect s{gridLeft + lv.x, gy + lv.y - scrollY(), lv.w, lv.h};
            if (s.contains(local)) { kind = Region::Card; index = i; return; }
        }
        const Rect ns{gridLeft + mNewCardRect.x, gy + mNewCardRect.y - scrollY(), mNewCardRect.w, mNewCardRect.h};
        if (ns.contains(local)) { kind = Region::NewCard; index = -1; return; }
    }

    bool HomeScreen::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::Move)  // track the hovered region (cross-fades in advance)
        {
            Region k; int idx; regionAt(local, k, idx);
            mHover.setHovered(hoverId(k, idx));
            return true;
        }
        if (g.type != Gesture::Type::Click) return Segment::handleGesture(g, local);

        for (int i = 0; i < 3; ++i)
            if (actionRect(i).contains(local))
            {
                if (i == 0 && onNewProject) onNewProject();
                else if (i == 1 && onOpenProject) onOpenProject();
                else if (i == 2 && onImportCatalog) onImportCatalog();
                return true;
            }
        // Sidebar links: Settings is live (R-SETTINGS-5, amending R-HOME-8) so the
        // preferences that decide how a project LOADS can be set before opening one --
        // this is the only screen available at that point. What's New / Help remain
        // reserved: they consume the click so it cannot fall through to the grid behind.
        for (int i = 0; i < 3; ++i)
            if (bottomLinkRect(i).contains(local))
            {
                if (i == 0 && onSettings) onSettings();
                return true;
            }

        // grid cards (screen space = grid origin + content rect - eased scroll)
        const double gridLeft = contentX() + kPad, gy = gridTop();
        for (auto &c : mCards)
        {
            if (!c.shown) continue;
            const Rect lv = c.live();   // R-G-1: the eased rect, not the target
            const Rect s{gridLeft + lv.x, gy + lv.y - scrollY(), lv.w, lv.h};
            if (s.contains(local))
            {
                // Remember the FULL card rect + its info as the fly-to-centre start, so
                // the loading screen shows the same whole item (R-LOADING).
                mLastOpenRect = s;
                mLastOpenCard = {c.info.name, c.info.photos, c.info.size, c.info.date, c.info.edited};
                if (onOpenRecent) onOpenRecent(c.info.recentIndex);
                return true;
            }
        }
        const Rect ns{gridLeft + mNewCardRect.x, gy + mNewCardRect.y - scrollY(), mNewCardRect.w, mNewCardRect.h};
        if (ns.contains(local)) { if (onNewProject) onNewProject(); return true; }

        return Segment::handleGesture(g, local);
    }

    void HomeScreen::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value();

        // ── backgrounds ──
        drawRoundedRect(t, Rect{0, 0, W, H}, 0.0, Paint::filled(palette::background()));
        drawRoundedRect(t, Rect{0, 0, kSidebarW, H}, 0.0, Paint::filled(palette::leftRailBg()));
        // sidebar right border + header bottom border
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(kSidebarW, 0); t.lineTo(kSidebarW, H); t.strokePath();
        t.beginPath(); t.moveTo(kSidebarW, kHeaderH); t.lineTo(W, kHeaderH); t.strokePath();

        // ── sidebar: wordmark + tagline ── (wordmark suppressed while the App flies
        //    its own copy back into place on return, so it reads as one element)
        if (!mWordmarkHidden)
        {
            // Same spacing formula as the top-bar wordmark (spacing -0.03*size; the
            // dot sits at kPad + estimateTextWidth) so the two "cosmo." read identical.
            const double sp = -0.03 * 46.0;
            t.setFill(palette::foreground());
            t.drawText("cosmo", kPad, 96.0, 46.0, font::sansSemiBold(), sp);
            t.setFill(palette::primary());
            t.drawText(".", kPad + estimateTextWidth("cosmo", 46.0), 96.0, 46.0, font::sansSemiBold(), sp);
        }
        t.setFill(palette::mutedForeground());
        t.drawText("Develop, grade, and export", kPad, 118.0, 11.0, font::sans());
        t.drawText("your photography.", kPad, 133.0, 11.0, font::sans());
        // divider
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(kPad, 159.0); t.lineTo(kSidebarW - kPad, 159.0); t.strokePath();

        // ── sidebar: actions ──
        struct Act { const char *label; int icon; };  // icon: 0 plus 1 folder 2 upload
        const Act acts[3] = {{"New Project", 0}, {"Open Project\xE2\x80\xA6", 1}, {"Import Catalog\xE2\x80\xA6", 2}};
        for (int i = 0; i < 3; ++i)
        {
            const Rect r = actionRect(i);
            const bool primary = (i == 0);
            const double hv = mHover.amount(hoverId(Region::Action, i));
            // Hover: brighten the primary fill; pull the outline chip's border toward the
            // accent + a faint wash; give the ghost "Import" a wash. Eased so it never pops.
            if (primary)
                drawRoundedRect(t, r, radius::control(), Paint::filled(brighten(palette::primary(), interaction::kHoverFillLift * hv)));
            else if (i == 1)
                drawRoundedRect(t, r, radius::control(),
                                Paint::filledStroked(palette::primaryAlpha(0.10 * hv),
                                                     lerpColor(palette::border(), palette::primary(), 0.5 * hv), 1.0));
            else if (hv > 0.0)
                drawRoundedRect(t, r, radius::control(), Paint::filled(palette::hoverWash(hv)));
            const Color fg = primary ? palette::primaryForeground()
                                     : lerpColor(palette::mutedForeground(), palette::foreground(), 0.6 * hv);
            const Rect ib{r.x + 14.0, r.y + (r.h - 13.0) / 2, 13.0, 13.0};
            if (acts[i].icon == 0) iconPlus(t, ib, fg, 1.6);
            else if (acts[i].icon == 1) iconFolder(t, ib, fg, 1.2);
            else icon::upload(t, ib, fg, 1.2);
            t.setFill(fg);
            t.drawText(acts[i].label, r.x + 36.0, r.y + r.h / 2 + 4.0, 12.0, primary ? font::sansSemiBold() : font::sans());
        }

        // ── sidebar: bottom reserved links + version ──
        const double blockTop = H - kBottomBlockH;
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(kPad, blockTop); t.lineTo(kSidebarW - kPad, blockTop); t.strokePath();
        const char *links[3] = {"Settings", "What's New", "Help & Documentation"};
        for (int i = 0; i < 3; ++i)
        {
            const Rect r = bottomLinkRect(i);
            const double hv = mHover.amount(hoverId(Region::Link, i));
            if (hv > 0.0)  // faint hover row + lift the icon/label toward foreground (R-G-1)
                drawRoundedRect(t, Rect{r.x - 6.0, r.y - 1.0, r.w + 12.0, r.h + 2.0}, radius::control(), Paint::filled(palette::hoverWash(hv)));
            const Color lc = lerpColor(palette::mutedForeground(), palette::foreground(), 0.7 * hv);
            const Rect ib{r.x, r.y + (r.h - 11.0) / 2, 11.0, 11.0};
            if (i == 0) iconGear(t, ib, lc, 1.1);
            else if (i == 1) iconSpark(t, ib, lc, 1.1);
            else iconHelp(t, ib, lc, 1.1);
            t.setFill(lc);
            t.drawText(links[i], r.x + 18.0, r.y + r.h / 2 + 4.0, 11.0, font::sans());
        }
        t.setFill(Color{palette::mutedForeground().r, palette::mutedForeground().g, palette::mutedForeground().b, 0.4});
        t.drawText("cosmo v1.0.0", kPad, blockTop + kVersionY, 10.0, font::mono());

        // ── right header ──
        iconClock(t, Rect{contentX() + kPad, kHeaderH / 2 - 6.5, 13.0, 13.0}, palette::mutedForeground(), 1.2);
        t.setFill(palette::foreground());
        const std::string title = headerTitle();
        t.drawText(title, contentX() + kPad + 20.0, kHeaderH / 2 + 4.0, 13.0, font::sansSemiBold());
        const double titleW = estimateTextWidth(title, 13.0);
        t.setFill(Color{palette::mutedForeground().r, palette::mutedForeground().g, palette::mutedForeground().b, 0.6});
        t.drawText(std::to_string(visibleCount()), contentX() + kPad + 20.0 + titleW + 8.0, kHeaderH / 2 + 4.0, 10.0, font::sans());
        // search chrome frame (the TextBox child draws itself); draw the search glyph
        const Rect sr = searchRect();
        iconSearch(t, Rect{sr.x + 8.0, sr.y + (sr.h - 11.0) / 2, 11.0, 11.0}, Color{palette::mutedForeground().r, palette::mutedForeground().g, palette::mutedForeground().b, 0.5}, 1.2);

        // ── grid (clipped to its viewport) ──
        const double gridLeft = contentX() + kPad, gy = gridTop();
        const double availW = W - kPad - gridLeft;
        t.save();
        t.clipRect(gridLeft, gy, availW, H - gy);

        if (visibleCount() == 0 && !mCards.empty())
        {
            iconSearch(t, Rect{(W + kSidebarW) / 2 - 12, gy + 40, 24, 24}, Color{palette::mutedForeground().r, palette::mutedForeground().g, palette::mutedForeground().b, 0.4}, 1.4);
            t.setFill(Color{palette::mutedForeground().r, palette::mutedForeground().g, palette::mutedForeground().b, 0.6});
            const std::string msg = "No projects match \"" + mSearch->text + "\"";
            t.drawText(msg, (W + kSidebarW) / 2 - estimateTextWidth(msg, 12.0) / 2, gy + 80, 12.0, font::sans());
        }

        auto cardScreen = [&](const Rect &content) {
            return Rect{gridLeft + content.x, gy + content.y - scrollY(), content.w, content.h};
        };
        for (int ci = 0; ci < (int)mCards.size(); ++ci)
        {
            const Card &c = mCards[ci];
            if (!c.shown) continue;
            const Rect s = cardScreen(c.live());
            const double hv = mHover.amount(hoverId(Region::Card, ci));
            // Shared card chrome (thumbnail itself is the ImageView child, drawn over it).
            drawProjectCardChrome(t, s, {c.info.name, c.info.photos, c.info.size, c.info.date, c.info.edited}, hv);
        }
        // trailing New-Project card (dashed)
        {
            const Rect s = cardScreen(mNewCardRect);
            const double th = s.w * 9.0 / 16.0;
            const Rect box{s.x, s.y, s.w, th};
            const double hv = mHover.amount(hoverId(Region::NewCard, -1));
            // hover pulls the dashed border + glyph toward the accent/foreground (R-G-1)
            const Color dash = lerpColor(Color{palette::border().r, palette::border().g, palette::border().b, 0.6},
                                         palette::primary(), 0.5 * hv);
            dashedRoundRect(t, box, dash, 1.0 + 0.4 * hv);
            const Color nc = lerpColor(palette::mutedForeground(), palette::foreground(), 0.6 * hv);
            const double cxp = box.x + box.w / 2;
            iconPlus(t, Rect{cxp - 10, box.y + box.h / 2 - 16, 20, 20}, nc, 1.4);
            t.setFill(nc);
            t.drawText("New Project", cxp - estimateTextWidth("New Project", 11.0) / 2, box.y + box.h / 2 + 18.0, 11.0, font::sans());
        }
        t.restore();
    }
}
}
