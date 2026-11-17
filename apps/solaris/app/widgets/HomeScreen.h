/*
 *  solaris_ui — HomeScreen: the launcher (R-HOME-1, R-UI-1).
 *
 *  Interstellar's Home, which is cosmo's rhythm on the 8/16/32 grid: a 300 px sidebar on
 *  `leftRailBg` with the wordmark, a two-line tagline, a rule, the actions (New song primary, Open
 *  song… outline), Settings and the version at the foot; a 60 px header band with "Recent songs"
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
#include "../../../cosmo/widgets/BrandMark.h"
#include "../Theme.h"
#include "../A11y.h"
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
        /** Where the "Recent songs" header begins (local) — the grid's left edge: its clock icon (R-FAM-7), then its words
         *  at `headingTextLeft`, then the count. */
        double headingLeft() const { return kSidebarW + kPad; }
        static constexpr double kHeadIcon = 13.0, kHeadIconGap = 8.0;
        double headingTextLeft() const { return headingLeft() + kHeadIcon + kHeadIconGap; }
        artboard::Rect headingIconRect() const { return artboard::Rect{headingLeft(), (kHeaderH - kHeadIcon) * 0.5, kHeadIcon, kHeadIcon}; }
        /** R-FAM-7: the dashed "New song" card that ends the grid while there are songs — where it is drawn now (LIVE:
         *  it eases to its slot when a card comes or goes) and its LIVE fade (0 with no songs: the empty state's
         *  sentence and button stand there instead). */
        artboard::Rect newCardLive() const;
        double newCardAmount() const { return mNew.alpha.value(); }
        /** The version Home shows — the build's (`SOLARIS_VERSION`, app/CMakeLists.txt), never a literal here. */
        static std::string versionLabel();
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
        artboard::Rect actionRect(int i) const;   // 0 = New, 1 = Open, 2 = From template… (R-PRESET-2)
        /** R-ACCESS-1: New song, Open song…, From template…, Settings, and each recent song in view. */
        void accessibles(std::vector<A11yNode> &out);
        artboard::Rect settingsRect() const;
        artboard::Rect gridViewport() const;
        artboard::Rect cardLive(int i) const;
        artboard::Rect cardTarget(int i) const;
        artboard::Rect emptyButtonRect() const;
        int columns() const { return mCols; }
        int cardCount() const { return (int)mCards.size(); }
        double wordmarkSize() const { return mWordPx; }

        std::function<void()> onNewSong, onOpenSong, onSettings;
        std::function<void(artboard::Point world)> onNewFromTemplate; // R-PRESET-2: "From template…" — a menu there
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
        Card mNew;                 // R-FAM-7: the dashed "New song" card — a slot after the last song's (no `info`)
        bool mNewWanted = false;   // … there while there are songs
        void place(Card &c, int slot, const Grid &g);
        int mCols = 1;
        double mNowMs = 0.0;
        // R-BRAND-2: the app's logo above the wordmark. It shows up once when Home first appears
        // (the first advance), then rests — Home is a place to choose, not a loading screen.
        mutable ::arstro::cosmo_v2::brand::Player mMark{::arstro::cosmo_v2::brand::Mark::Solaris};
        bool mHasModel = false;
        interstellar_v1::EasedScroll mScroll;
        cosmo_v2::HoverFade mHover;
        mutable double mWordPx = 46.0;
    };
}
}
