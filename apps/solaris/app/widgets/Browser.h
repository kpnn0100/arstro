/*
 *  solaris_ui — Browser: the left panel — samples, instruments and effects, the song's sounds
 *  (R-BROWSE-1, R-BROWSE-3).
 *
 *  Three tabs, a highlight that SLIDES between them (220 ms): **Samples** lists the sample folders
 *  from Settings, and a folder clicked is browsed (`browse <path>` — the host lists it) with a row
 *  back up; **Instruments** lists the DSP registry, instruments then effects; **Song** lists the
 *  sounds the song already plays. A row an item can be dropped from — an audio file, an instrument,
 *  an effect — is DRAGGED out: the browser keeps the gesture (Down captured) and reports the
 *  pointer's position and the drop to its owner, which knows what lies under it (`onDragMove`,
 *  `onDrop`). A double-click places the item at the playhead (`onActivate`). Dragging draws nothing
 *  here: the ghost is the owner's, drawn over everything.
 *
 *  The list TRAVELS (§1): rows are Interstellar's `AnimatedRows`, keyed by what they show — a row
 *  that arrives fades in, one that goes fades out, the rest ease to their new places — and a tab or
 *  a folder changed starts a new generation of keys, so the whole list CROSS-FADES (the old one
 *  fading where it was scrolled to). The empty-state words are a row of the same list.
 *
 *  The list scrolls (EasedScroll, one viewport rectangle). States, each in words: no sample folders
 *  yet ("Add the folders you keep samples in — Settings"); a folder that lists nothing; a song with
 *  no sounds yet.
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "../../../interstellar/app/widgets/AnimatedRows.h"
#include "../../../interstellar/app/widgets/EasedScroll.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_ui
{
    class Browser : public artboard::Segment
    {
    public:
        static constexpr double kWidth = 234.0;   // space::u(72): Interstellar's left column
        static constexpr double kTabsH = 29.25;
        enum Tab { kSamples = 0, kInstruments, kSong, kTabs };

        struct Item
        {
            std::string kind;   // folder | dir | up | audio | instrument | effect | header | empty (label + note: the words)
            std::string label, value, note;
        };

        Browser();
        void bind(const solaris::AppModel &m);
        void layout();
        void setTab(int tab);
        int tab() const { return mTab; }

        // geometry a test aims at (local)
        artboard::Rect tabRect(int t) const;
        artboard::Rect rowRect(int i) const;
        int rowCount() const { return (int)mRows.size(); }
        const Item &row(int i) const { return mRows[(size_t)i]; }
        double tabHighlightX() const { return mTabX.value(); } // LIVE, for the slide test
        /** Row i's LIVE opacity — a test tells a cross-fade from a cut. */
        double rowAlpha(int i) const;

        std::function<void(const std::string &line)> onCommand;
        std::function<void()> onOpenSettings;
        std::function<void(const Item &, artboard::Point world)> onDragMove, onDrop;
        std::function<void()> onDragCancel;
        std::function<void(const Item &)> onActivate;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        void rebuild();
        void navigate(); // a new list (another tab or folder): a new generation of keys — it cross-fades
        double rowY(int i) const; // row i's LIVE top in the list, before scrolling
        int rowAt(const artboard::Point &p) const;
        double listTop() const { return kTabsH + 1.0; }
        static bool draggable(const Item &i) { return i.kind == "audio" || i.kind == "instrument" || i.kind == "effect"; }

        int mTab = kSamples;
        std::string mPath;                        // the folder being browsed ("" = the folder list)
        std::vector<std::string> mFolders;
        std::vector<solaris::BrowserEntry> mEntries;
        std::vector<solaris::DeviceTypeModel> mTypes;
        std::vector<std::pair<std::string, std::string>> mSongSounds; // label, src
        std::vector<Item> mRows;                      // the list as it is: hit-testing, row()
        interstellar_v1::AnimatedRows<Item> mMotion;  // the list as drawn: eased, ghosts fading
        int mGen = 0;                                 // bumped by navigate()
        double mGhostScroll = 0.0;                    // where the list a navigation left was scrolled to
        double mNowMs = 0.0;
        bool mTabInit = false;
        artboard::AnimatedProperty mTabX{0.0};
        interstellar_v1::EasedScroll mScroll;
        cosmo_v2::HoverFade mHover;
        int mPressRow = -1;
        bool mDragging = false;
        Item mDragItem;
    };
}
}
