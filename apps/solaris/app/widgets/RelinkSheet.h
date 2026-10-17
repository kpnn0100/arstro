/*
 *  solaris_ui — RelinkSheet: File › Relink… — the files the song names that cannot be read (R-COLLECT-1).
 *
 *  Cosmo's modal look, as the Export sheet's (the scrim, the card, the 150/120 ms fade). A row per file in the model's
 *  `missing[]`: its name, the folder it was in and what uses it (a version's ids prefixed `vr_n:`), and Locate… — the
 *  host's file picker, then ONE `project relink <file> --to <picked>`. Search a Folder… — the host's folder picker, then
 *  ONE `project relink --folder <dir>`, every missing file looked for there by name. The sheet holds nothing: it draws
 *  the model, so a file a shell relinked leaves it too.
 *
 *  Nothing snaps: rows are keyed by path — a found one fades out where it is and the rest slide up into its place; one
 *  that goes missing while it is open fades in. The list clips and scrolls (eased, the wheel's) when it is longer than
 *  the card; with nothing missing the card says so.
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
    class RelinkSheet : public artboard::Segment
    {
    public:
        static constexpr double kCardW = 560.0;
        static constexpr double kPad = 24.0;
        static constexpr double kRowH = 48.0;
        static constexpr int kMaxRowsShown = 7; // the card grows to this many, then the list scrolls

        RelinkSheet();
        /** Open it on the song as it is: its rows placed, not faded in one by one. */
        void show(const solaris::AppModel &m);
        void close();
        /** What is missing now — rows arrive and leave, eased (call on every model change). */
        void bind(const solaris::AppModel &m);
        bool isOpen() const { return mOpen && !mClosing; }
        double appearAmount() const { return mAppear.value(); } // LIVE: 0 shut … 1 open

        /** Locate… on a row (the file as the song names it); Search a Folder…. The App asks the host, then sends ONE line. */
        std::function<void(const std::string &path)> onLocate;
        std::function<void()> onSearchFolder;

        // what a test reaches (window coordinates, from the last paint) and the LIVE eased values it catches mid-way
        int rowCount() const { return (int)mRows.size(); }           // rows drawn, a leaving one too
        double rowAmount(const std::string &path) const;             // 0 … 1: its fade (−1: no such row)
        double rowY(const std::string &path) const;                  // its eased place in the list (content px)
        artboard::Rect locateRect(const std::string &path) const;
        artboard::Rect searchRect() const { return mSearch; }
        artboard::Rect doneRect() const { return mDone; }
        double cardHeight() const { return mCardH.value(); }         // LIVE: it grows and shrinks with its rows, eased
        double scrollOffset() const { return mScroll.value(); }
        bool scrollable() const { return mScroll.scrollable(); }

        void advance(double nowMs) override;
        bool handleKey(const artboard::KeyEvent &e) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &) const override { return mOpen && !mClosing; }

    private:
        struct Row
        {
            std::string path, name, folder, users;
            artboard::AnimatedProperty a{0.0}, y{0.0};
            double aTarget = -1.0, yTarget = 0.0, yLast = -1.0; // what each last eased toward
            bool leaving = false, placed = false;
        };
        artboard::Rect cardRect() const;
        artboard::Rect listRect() const;
        int idOf(const std::string &path) const;
        std::vector<Row> mRows;
        std::vector<solaris::MissingModel> mWanted;
        bool mPlaceAll = false;                       // the next advance places the rows at once (just shown)
        bool mOpen = false, mClosing = false, mShowWanted = false, mCloseWanted = false;
        artboard::AnimatedProperty mAppear{0.0};
        artboard::AnimatedProperty mCardH{0.0};      // the card grows and shrinks with its rows, eased
        double mCardHTarget = -1.0;
        interstellar_v1::EasedScroll mScroll;
        mutable artboard::Rect mSearch, mDone;
        mutable std::vector<std::pair<std::string, artboard::Rect>> mLocate;
        cosmo_v2::HoverFade mHover;
    };
}
}
