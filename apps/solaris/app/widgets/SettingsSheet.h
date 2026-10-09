/*
 *  solaris_ui — SettingsSheet: the machine's settings, in cosmo's modal style (R-SET-1, amended).
 *
 *  Cosmo's SettingsDialog's LOOK — the scrim, the popover card, the row labels tracked +0.06, the
 *  chips (filled accent when chosen, secondary + border otherwise), the eased hover wash, the Done
 *  button, the 150/120 ms fade — in SECTIONS as cosmo's and Interstellar's Engine Settings are
 *  (R-SET-3): **Audio** — Output (the clock device), Input, Sample rate, Buffer (the latency it
 *  costs, said beside it); **Playback** — Metronome, Click level; **New songs** — Tempo, Meter;
 *  **Sample folders** — a list with a remove button per folder and "Add folder…"; **Interface** —
 *  Reduced motion. Not cosmo's class: its rows are an image engine's.
 *
 *  Every control is a command line (`onCommand`): `settings set output=<id>`, `folder add <path>`,
 *  `folder remove <path>` — the sheet holds no setting of its own; it draws `AppModel::settings` and
 *  `devices` every frame, so a change made from a shell shows here too. A chip's "chosen" fill is
 *  EASED (200 ms) — a setting arriving from the model is still a visible change (design rule §1).
 *
 *  Chips and paths are MEASURED (they size to their text), so their boxes are computed in the paint
 *  — measurement is only valid during a render (design rule R5) — and kept for the next hit test.
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
    class SettingsSheet : public artboard::Segment
    {
    public:
        enum Row { kOutput = 0, kInput, kRate, kBuffer, kMetronome, kClickLevel, kNewBpm, kNewSig, kMotion, kRows };
        static constexpr double kCardW = 520.0;
        static constexpr double kPad = 24.0;
        static constexpr double kChipH = 26.0;

        SettingsSheet();
        void bind(const solaris::AppModel &m);
        void show();
        void close();
        bool isOpen() const { return mOpen && !mClosing; }
        /** The LIVE eased appearance (0 shut … 1 open) — what a test reads to tell a fade from a cut. */
        double appearAmount() const { return mAppear.value(); }

        std::function<void(const std::string &line)> onCommand;
        std::function<void()> onAddFolder;

        // geometry, from the last paint (a test renders a frame first)
        artboard::Rect chipRect(int row, int chip) const;
        artboard::Rect doneRect() const { return mDone; }
        artboard::Rect addFolderRect() const { return mAddFolder; }
        artboard::Rect folderRemoveRect(int i) const { return i >= 0 && i < (int)mRemove.size() ? mRemove[(size_t)i] : artboard::Rect{}; }
        int chipCount(int row) const { return row >= 0 && row < kRows ? (int)mChips[(size_t)row].size() : 0; }
        double chosenAmount(int row, int chip) const;
        /** Scroll (eased) so a rect from the last paint is inside the card. */
        void revealRect(const artboard::Rect &r);

        void advance(double nowMs) override;
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleKey(const artboard::KeyEvent &e) override;

    protected:
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &) const override { return mOpen && !mClosing; }

    private:
        struct Chip
        {
            std::string label, value;
        };
        int chosen(int row) const;
        std::string current(int row) const; // the row's setting, as its chips' values spell it
        int hoverId(const artboard::Point &p) const;
        artboard::Rect cardRect() const;

        std::vector<Chip> mChips[kRows];
        std::vector<std::string> mFolders;
        std::string mOutput, mInput;
        int mRate = 48000, mBuffer = 256;
        double mLatencyMs = 0;
        solaris::SettingsModel mSettings;

        bool mOpen = false, mClosing = false, mShowWanted = false, mCloseWanted = false;
        double mNowMs = 0.0;
        artboard::AnimatedProperty mAppear{0.0};
        std::vector<artboard::AnimatedProperty> mChosen[kRows];
        cosmo_v2::HoverFade mHover;
        mutable interstellar_v1::EasedScroll mScroll; // its extent is measured in the paint
        // measured in the last paint
        mutable std::vector<artboard::Rect> mChipRects[kRows];
        mutable std::vector<artboard::Rect> mRemove;
        mutable artboard::Rect mDone, mAddFolder, mCard;
        mutable double mContentH = 0.0;
    };
}
}
