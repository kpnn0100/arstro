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
        constexpr double kMetaH = 46.0;     // card meta band (name + row)
        constexpr double kActionH = 34.0;
        constexpr double kSearchW = 168.0, kSearchH = 26.0;
        constexpr double kScrollGlideMs = 180.0;  // grid-scroll ease (R-G-1)

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
    double HomeScreen::gridTop() const { return kHeaderH + 24.0; }

    Rect HomeScreen::actionRect(int i) const
    {
        // Below the logo block + divider; three stacked full-width buttons.
        const double top = 183.0 + i * (kActionH + 6.0);
        return Rect{kPad, top, kSidebarW - 2 * kPad, kActionH};
    }

    Rect HomeScreen::bottomLinkRect(int i) const
    {
        const double h = height.value();
        const double blockTop = h - 137.0;      // divider + links + version + pb-8
        return Rect{kPad, blockTop + 16.0 + i * 20.0, kSidebarW - 2 * kPad, 18.0};
    }

    Rect HomeScreen::searchRect() const
    {
        return Rect{width.value() - kPad - kSearchW, (kHeaderH - kSearchH) / 2, kSearchW, kSearchH};
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
            if (c.thumb)
            {
                c.thumb->visible = true;
                c.thumb->x.set(c.rect.x);
                c.thumb->y.set(c.rect.y - scrollY());   // grid-clip local space (eased scroll)
                c.thumb->width.set(cardW);
                c.thumb->height.set(thumbH);
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
            const Rect s{gridLeft + c.rect.x, gy + c.rect.y - scrollY(), c.rect.w, c.rect.h};
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
        // reserved bottom links (Settings / What's New / Help) are inert (R-HOME-8)
        for (int i = 0; i < 3; ++i) if (bottomLinkRect(i).contains(local)) return true;

        // grid cards (screen space = grid origin + content rect - eased scroll)
        const double gridLeft = contentX() + kPad, gy = gridTop();
        for (auto &c : mCards)
        {
            if (!c.shown) continue;
            const Rect s{gridLeft + c.rect.x, gy + c.rect.y - scrollY(), c.rect.w, c.rect.h};
            if (s.contains(local))
            {
                // Remember the 16:9 cover (thumbnail) rect as the fly-to-centre start.
                mLastOpenRect = Rect{s.x, s.y, s.w, s.w * 9.0 / 16.0};
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

        // ── sidebar: wordmark + tagline ──
        t.setFill(palette::foreground());
        t.drawText("cosmo", kPad, 96.0, 46.0, font::sansSemiBold(), -0.045 * 46.0);
        const double wm = estimateTextWidth("cosmo", 46.0) - 0.045 * 46.0 * 5;
        t.setFill(palette::primary());
        t.drawText(".", kPad + wm + 2.0, 96.0, 46.0, font::sansSemiBold());
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
        const double blockTop = H - 137.0;
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
        t.drawText("cosmo v1.0.0", kPad, blockTop + 92.0, 10.0, font::mono());

        // ── right header ──
        iconClock(t, Rect{contentX() + kPad, kHeaderH / 2 - 6.5, 13.0, 13.0}, palette::mutedForeground(), 1.2);
        t.setFill(palette::foreground());
        t.drawText("Recent Projects", contentX() + kPad + 20.0, kHeaderH / 2 + 4.0, 13.0, font::sansSemiBold());
        const double titleW = estimateTextWidth("Recent Projects", 13.0);
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
            const Rect s = cardScreen(c.rect);
            const double th = s.w * 9.0 / 16.0;
            const double hv = mHover.amount(hoverId(Region::Card, ci));
            // card border + meta background (thumbnail itself is the ImageView child);
            // hover brightens the surface + lifts the border toward the accent (R-G-1).
            const Color cardBg = brighten(palette::folderChipBg(), 0.06 * hv);
            const Color cardBorder = lerpColor(palette::border(), palette::primaryAlpha(0.7), hv);
            drawRoundedRect(t, s, radius::control(), Paint::filledStroked(cardBg, cardBorder, 1.0 + 0.5 * hv));
            // thumbnail placeholder fill (shows if no cover decoded yet)
            drawRoundedRect(t, Rect{s.x, s.y, s.w, th}, 0.0, Paint::filled(Color::hex(0x111111)));
            if (c.info.edited)
            {
                const double bw = estimateTextWidth("Edited", 8.0) + 10.0;
                const Rect badge{s.x + s.w - bw - 8.0, s.y + 8.0, bw, 14.0};
                drawRoundedRect(t, badge, 1.0, Paint::filledStroked(Color{0, 0, 0, 0.6}, Color{palette::primary().r, palette::primary().g, palette::primary().b, 0.3}, 1.0));
                t.setFill(palette::primary());
                t.drawText("Edited", badge.x + 5.0, badge.y + 10.0, 8.0, font::sansSemiBold());
            }
            // meta
            const double mx = s.x + 12.0;
            double my = s.y + th + 18.0;
            t.setFill(palette::foreground());
            t.drawText(c.info.name, mx, my, 11.0, font::sansMedium());
            my += 15.0;
            t.setFill(palette::mutedForeground());
            std::string metaLeft = c.info.photos;
            if (!c.info.size.empty()) metaLeft += "  \xC2\xB7  " + c.info.size;
            t.drawText(metaLeft, mx, my, 10.0, font::sans());
            t.setFill(Color{palette::mutedForeground().r, palette::mutedForeground().g, palette::mutedForeground().b, 0.6});
            t.drawText(c.info.date, s.x + s.w - 12.0 - estimateTextWidth(c.info.date, 9.0), my, 9.0, font::sans());
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
