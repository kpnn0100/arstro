/*
 *  cosmo_v2 by arstro — SettingsDialog: a modal engine-settings picker (R-SETTINGS).
 *  Two rows of selectable chips — Preview quality (base preview render long-edge:
 *  speed vs. detail) and CPU threads (worker count for the multicore engine) — plus
 *  a Done button. Both map straight onto the RenderService via EditSession /
 *  par::setThreads. Same modal chrome + fade as PresetDialog (R-G-1). Reference:
 *  cosmo/panels/SettingsPanel.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class SettingsDialog : public artboard::Segment
    {
    public:
        explicit SettingsDialog(const artboard::Color &accent);

        std::function<void(int)> onPreviewEdge;  // preview render long-edge in px
        std::function<void(int)> onThreads;      // engine worker threads (0 = auto)

        /** Open, seeded with the current engine values so the right chips read selected. */
        void show(int previewEdge, int threads);
        bool isOpen() const { return mOpen && !mClosing; }

    protected:
        void advance(double nowMs) override;
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return mOpen && !mClosing; }

    private:
        void beginClose();
        artboard::Rect cardRect() const;
        artboard::Rect doneRect() const;
        // Fills `rects` with the chip boxes for row `row` (0 = quality, 1 = threads).
        void chipRects(int row, std::vector<artboard::Rect> &rects) const;
        void hitTargets(const artboard::Point &p, int &row, int &chip, bool &done) const;

        static const std::vector<int> kEdges;    // preview long-edge options
        static const std::vector<int> kThreads;  // thread-count options (0 = auto)

        artboard::Color mAccent;
        bool mOpen = false;
        bool mClosing = false;
        double mLastMs = 0.0;
        artboard::AnimatedProperty mAppear{0.0};
        int mEdge = 1600;     // current selection (px)
        int mThreadCount = 0; // current selection (0 = auto)
        int mHoverRow = -1, mHoverChip = -1;        // hovered chip (row, index) under the pointer
        bool mHoverDone = false;                    // Done button hovered
        bool mHoverPrev = false;
        artboard::AnimatedProperty mHoverAmt{0.0};  // hover fade (R-G-1)
    };
}
}
