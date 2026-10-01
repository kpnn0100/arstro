/*
 *  interstellar_v1 — HomeScreen: the launcher (R-UI-1, ui-brief §2).
 *
 *  Cosmo's HomeScreen rhythm on its coarser 8/16/32 grid — a 300 px sidebar on `leftRailBg` with
 *  the wordmark, a two-line tagline, a rule and the actions (New project primary, Open project…
 *  outline), Settings and the version at the foot; a 60 px header band with "Recent Projects" and
 *  the count; a grid of cards at a 220 px minimum width. Cosmo's class itself is not reused: it is
 *  cosmo's launcher (its wordmark, Import Catalog, its search), not a library widget.
 *
 *  The wordmark is `interstellar.` with the dot in the accent at the MEASURED end of the word,
 *  tracked −0.03 × size, and SIZED TO FIT the sidebar (46 px is cosmo's size for five letters;
 *  twelve need less) — R5's "sized-to-fit", measured with the real font.
 *
 *  A card is name, footage count · size, and last opened, under a 16:9 cover (the first frame of
 *  the project's first source, through the optional thumbnail hook; a plate with a film glyph
 *  otherwise). The name is ellipsized against the card's width, measured.
 *
 *  Motion: the column count comes from the window width, the classic place this repo shipped a
 *  snap — so every card's geometry is EASED toward its slot (260 ms), cover and chrome drawn from
 *  the same live rect; `cardLive(i)` and `cardTarget(i)` expose both halves for a test. The grid
 *  scrolls (EasedScroll). States: LOADING draws skeleton cards at the real card geometry, so
 *  nothing moves when they fill (they cross-fade); EMPTY says "No projects yet — open some footage
 *  to start one." rather than showing a blank grid.
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "EasedScroll.h"
#include "ImageSlot.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    class HomeScreen : public artboard::Segment
    {
    public:
        static constexpr double kSidebarW = 300.0;
        static constexpr double kPad = 32.0;
        static constexpr double kHeaderH = 60.0;
        static constexpr double kGap = 16.0;
        static constexpr double kMinCard = 220.0;
        static constexpr double kMetaH = 46.0;     // cosmo's projectcard::kMetaH
        static constexpr double kActionH = 34.0;
        static constexpr int kSkeletonCount = 6;

        HomeScreen();

        void bind(const interstellar::AppModel &m);
        void setLoading(bool on) { mLoading = on; }
        void setNowUnix(long long now) { mNowUnix = now; }
        void layout();

        // geometry a test aims at (local coords)
        artboard::Rect actionRect(int i) const;   // 0 = New, 1 = Open
        artboard::Rect settingsRect() const;
        artboard::Rect gridViewport() const;
        artboard::Rect cardLive(int i) const;     // drawn now (eased, scrolled)
        artboard::Rect cardTarget(int i) const;   // where the layout wants it (scrolled)
        artboard::Rect newCardLive() const;
        artboard::Rect emptyChipRect() const;
        int columns() const { return mCols; }
        int cardCount() const { return (int)mCards.size(); }
        double skeletonAmount() const { return mSkeleton.value(); }
        double wordmarkSize() const { return mWordPx; }
        const EasedScroll &scroll() const { return mScroll; }

        std::function<void()> onNewProject, onOpenProject, onSettings;
        std::function<void(int recentIndex)> onOpenRecent;
        std::function<bool(const std::string &, double, int, interstellar::Raster &)> thumbnail;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Card
        {
            interstellar::RecentModel info;
            artboard::Rect target{0, 0, 0, 0};          // content coords
            artboard::Rect lastTarget{-1, -1, -1, -1};
            artboard::AnimatedProperty ax{0.0}, ay{0.0}, aw{0.0}, ah{0.0};
            bool placed = false;
            ImageSlot cover;
            std::string coverKey;
            artboard::AnimatedProperty alpha{1.0};
            bool fadeIn = false;   // a card that joined after the first bind: fades in at its slot
            artboard::Rect live() const { return artboard::Rect{ax.value(), ay.value(), aw.value(), ah.value()}; }
        };
        struct Grid { int cols; double cardW, cardH; };
        Grid grid() const;
        double gridTop() const { return kHeaderH + 24.0; }
        double gridLeft() const { return kSidebarW + kPad; }
        artboard::Rect toScreen(const artboard::Rect &content) const;
        int hoverAt(const artboard::Point &p) const;
        std::string relativeDate(long long when) const;

        std::vector<Card> mCards;
        Card mNewCard;
        int mCols = 1;
        bool mLoading = false, mLoadingApplied = false, mInit = false;
        artboard::AnimatedProperty mSkeleton{0.0};
        long long mNowUnix = 0;
        double mNowMs = 0.0;
        EasedScroll mScroll;
        cosmo_v2::HoverFade mHover;
        mutable double mWordPx = 46.0;   // the fitted wordmark size from the last paint
        bool mHasModel = false;
    };
}
}
