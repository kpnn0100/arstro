/*
 *  solaris_ui — HomeScreen: the launcher (R-HOME-1, R-UI-1).
 *
 *  Interstellar's Home, which is cosmo's rhythm on the 8/16/32 grid: a 300 px sidebar on
 *  `leftRailBg` with the wordmark, a two-line tagline, a rule, the actions (New song primary, Open
 *  song… outline), Settings and the version at the foot; a 60 px header band with "Recent Songs"
 *  and the count; a grid of cards at a 220 px minimum width. Interstellar's class is not reused —
 *  it is Interstellar's launcher (its covers are film frames) — its structure is.
 *
 *  The wordmark is `solaris.` with the dot in the accent at the MEASURED end of the word, tracked
 *  −0.03 × size, sized to fit (R5). A card is the song's name, `bpm · length · strips`, over a plate
 *  of bars drawn from the song's name — a song has no picture, and a blank plate would read as
 *  "not loaded". A song whose file is gone is drawn dimmed and SAYS "missing".
 *
 *  Motion: the column count comes from the window width — the classic snap — so every card's
 *  geometry eases toward its slot (260 ms); `cardLive(i)` / `cardTarget(i)` expose both halves for a
 *  test. The grid scrolls (EasedScroll). EMPTY says what to do: "No songs yet — start one." and
 *  offers the button right there.
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "../../../interstellar/app/widgets/EasedScroll.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_ui
{
    class HomeScreen : public artboard::Segment
    {
    public:
        static constexpr double kSidebarW = 300.0;
        static constexpr double kPad = 32.0;
        static constexpr double kHeaderH = 60.0;
        static constexpr double kGap = 16.0;
        static constexpr double kMinCard = 220.0;
        static constexpr double kMetaH = 46.0;
        static constexpr double kActionH = 34.0;

        HomeScreen();
        void bind(const solaris::AppModel &m);
        void layout();

        // geometry a test aims at (local coords)
        artboard::Rect actionRect(int i) const;   // 0 = New, 1 = Open
        artboard::Rect settingsRect() const;
        artboard::Rect gridViewport() const;
        artboard::Rect cardLive(int i) const;
        artboard::Rect cardTarget(int i) const;
        artboard::Rect emptyButtonRect() const;
        int columns() const { return mCols; }
        int cardCount() const { return (int)mCards.size(); }
        double wordmarkSize() const { return mWordPx; }

        std::function<void()> onNewSong, onOpenSong, onSettings;
        std::function<void(const std::string &path)> onOpenRecent, onForgetRecent;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Card
        {
            solaris::RecentModel info;
            artboard::Rect target{0, 0, 0, 0};
            artboard::Rect lastTarget{-1, -1, -1, -1};
            artboard::AnimatedProperty ax{0.0}, ay{0.0}, aw{0.0}, ah{0.0}, alpha{1.0};
            bool placed = false, fadeIn = false;
            artboard::Rect live() const { return artboard::Rect{ax.value(), ay.value(), aw.value(), ah.value()}; }
        };
        struct Grid { int cols; double cardW, cardH; };
        Grid grid() const;
        double gridTop() const { return kHeaderH + 24.0; }
        double gridLeft() const { return kSidebarW + kPad; }
        artboard::Rect toScreen(const artboard::Rect &content) const;
        int hoverAt(const artboard::Point &p) const;
        void paintCard(artboard::IRenderTarget &t, const Card &c, const artboard::Rect &s, double hover) const;

        std::vector<Card> mCards;
        int mCols = 1;
        double mNowMs = 0.0;
        bool mHasModel = false;
        interstellar_v1::EasedScroll mScroll;
        cosmo_v2::HoverFade mHover;
        mutable double mWordPx = 46.0;
    };
}
}
