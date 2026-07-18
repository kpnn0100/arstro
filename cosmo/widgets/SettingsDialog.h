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
#include "HoverFade.h"
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
        std::function<void(bool)> onUseGpu;      // GPU acceleration on/off (R-GPU)

        /** Open, seeded with the current engine values so the right chips read selected.
         *  `gpuAvailable` false → the GPU row is shown disabled ("unavailable"). */
        void show(int previewEdge, int threads, bool useGpu, bool gpuAvailable);
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
        // Fills `rects` with the chip boxes for row `row` (0 = quality, 1 = threads, 2 = GPU).
        void chipRects(int row, std::vector<artboard::Rect> &rects) const;
        void hitTargets(const artboard::Point &p, int &row, int &chip, bool &done) const;
        static constexpr int kRows = 3;
        int rowChipCount(int row) const { return row == 0 ? (int)kEdges.size() : row == 1 ? (int)kThreads.size() : 2; }
        // Flat HoverFade ids: rows laid out contiguously, then Done.
        int chipId(int row, int chip) const { int b = 0; for (int r = 0; r < row; ++r) b += rowChipCount(r); return b + chip; }
        int doneId() const { return rowChipCount(0) + rowChipCount(1) + rowChipCount(2); }

        static const std::vector<int> kEdges;    // preview long-edge options
        static const std::vector<int> kThreads;  // thread-count options (0 = auto)

        artboard::Color mAccent;
        bool mOpen = false;
        bool mClosing = false;
        double mLastMs = 0.0;
        artboard::AnimatedProperty mAppear{0.0};
        int mEdge = 1600;         // current selection (px)
        int mThreadCount = 0;     // current selection (0 = auto)
        bool mUseGpu = false;     // GPU acceleration on/off (R-GPU)
        bool mGpuAvailable = false;  // a platform GPU backend exists (else the row is disabled)
        HoverFade mHover;         // per-chip / Done hover cross-fade (R-G-3)
    };
}
}
