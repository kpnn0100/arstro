/*
 *  cosmo_v2 by arstro — SettingsDialog: a modal engine-settings picker (R-SETTINGS).
 *  Five rows of selectable chips — Screen scale (how large the whole shell is drawn,
 *  R-SCALE), Preview quality (base preview render long-edge: speed vs. detail), CPU
 *  threads (worker count for the multicore engine), CPU limit (the share of the
 *  machine cosmo may schedule, R-CPU) and GPU acceleration — plus a Done button. They map straight onto the RenderService via EditSession /
 *  par::setThreads, or onto AppSettings::cpuPercent. Same modal chrome + fade as
 *  PresetDialog (R-G-1). Opened from the editor's Settings menu AND from the home
 *  screen's sidebar link (R-SETTINGS-5) — one dialog, not one per screen.
 *
 *  CPU limit sits directly under CPU threads because it is what "Auto" resolves to
 *  (R-CPU-2): reading them apart would make Auto look like it still meant "every core".
 *  Reference: cosmo/panels/SettingsPanel.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
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

        /** R-SCALE-1: percent of the design size (75/90/100/125). FIRST row, because it
         *  changes the window every other row is read in — a user who cannot read the dialog
         *  should not have to find the fix at the bottom of it. */
        std::function<void(int)> onUiScale;
        std::function<void(int)> onPreviewEdge;  // preview render long-edge in px
        std::function<void(int)> onThreads;      // engine worker threads (0 = auto)
        std::function<void(int)> onCpuPercent;   // share of the machine cosmo may use (R-CPU-3)
        std::function<void(bool)> onUseGpu;      // GPU acceleration on/off (R-GPU)

        /** Open, seeded with the current engine values so the right chips read selected.
         *  `gpuAvailable` false → the GPU row is shown disabled ("unavailable"). */
        /** `maxScale` is the largest screen scale this DISPLAY can give a window for; anything
         *  above it is drawn disabled with the reason, the way the GPU row shows "unavailable"
         *  (R-SCALE-3). ONE number suffices because the constraint is monotonic — the logical
         *  minimum is fixed, so a bigger scale always needs a bigger window. */
        void show(int uiScale, int maxScale, int previewEdge, int threads, int cpuPercent,
                  bool useGpu, bool gpuAvailable);
        bool isOpen() const { return mOpen && !mClosing; }
        /** Public (unlike the rest of the Segment overrides) because the HOME screen
         *  drives this dialog directly: Home is not part of the editor tree that would
         *  otherwise advance it (R-SETTINGS-5) -- the same reason HomeScreen::advance is
         *  public. It must keep ticking while shut, so show() has a current nowMs. */
        void advance(double nowMs) override;
        /** Also public for Home: the dialog is drawn entirely in the overlay pass, so
         *  this call IS the dialog. */
        void onOverlay(artboard::IRenderTarget &t) const override;

    protected:
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return mOpen && !mClosing; }

    private:
        void beginClose();
        artboard::Rect cardRect() const;
        artboard::Rect doneRect() const;
        // Named once, so inserting a row is not five coordinated edits to five literals —
        // which is what the row indices were before Screen scale was added.
        enum Row { kRowScale = 0, kRowQuality, kRowThreads, kRowCpu, kRowGpu, kRows };
        /** Fills `rects` with the chip boxes for row `row`, WRAPPING at the card's inner
         *  width; `lines` (optional) receives how many lines it took. */
        void chipRects(int row, std::vector<artboard::Rect> &rects, int *lines = nullptr) const;
        /** The one chip walk. Returns the number of lines row `row` needs; when `out` is
         *  non-null also fills it with the chips' boxes placed from (`left`, `top`). Splitting
         *  it this way is what keeps `cardRect` and `chipRects` from calling each other. */
        int layoutChips(int row, double left, double top, std::vector<artboard::Rect> *out) const;
        int rowLines(int row) const;              // lines of chips row `row` needs
        double rowBlockH(int row) const;          // label + gap + those lines
        double blockTop(const artboard::Rect &card, int row) const;
        /** The text on chip `i` of `row` — ONE mapping, read both by chipRects (which
         *  sizes each chip to its text) and by the paint. Two copies would drift and
         *  mis-place every chip after the first in a row. */
        static std::string chipLabel(int row, int i);
        void hitTargets(const artboard::Point &p, int &row, int &chip, bool &done) const;
        int rowChipCount(int row) const
        {
            return row == kRowScale   ? (int)kScales.size()
                 : row == kRowQuality ? (int)kEdges.size()
                 : row == kRowThreads ? (int)kThreads.size()
                 : row == kRowCpu     ? (int)kCpuPercents.size()
                                      : 2;
        }
        // Flat HoverFade ids: rows laid out contiguously, then Done.
        int chipId(int row, int chip) const { int b = 0; for (int r = 0; r < row; ++r) b += rowChipCount(r); return b + chip; }
        int doneId() const { int b = 0; for (int r = 0; r < kRows; ++r) b += rowChipCount(r); return b; }

        static const std::vector<int> &kScales;      // UI scale options, % (== AppSettings::uiScales)
        static const std::vector<int> kEdges;        // preview long-edge options
        static const std::vector<int> kThreads;      // thread-count options (0 = auto)
        static const std::vector<int> kCpuPercents;  // CPU budget options, % of cores (R-CPU-3)

        artboard::Color mAccent;
        bool mOpen = false;
        bool mClosing = false;
        double mLastMs = 0.0;
        artboard::AnimatedProperty mAppear{0.0};
        int mUiScale = 100;       // current selection (percent, R-SCALE-1)
        int mMaxScale = 10000;     // largest scale this display can honour (R-SCALE-3)
        int mEdge = 1600;         // current selection (px)
        int mThreadCount = 0;     // current selection (0 = auto)
        int mCpuPercent = 50;     // current selection (% of cores, R-CPU-1)
        bool mUseGpu = false;     // GPU acceleration on/off (R-GPU)
        bool mGpuAvailable = false;  // a platform GPU backend exists (else the row is disabled)
        HoverFade mHover;         // per-chip / Done hover cross-fade (R-G-3)
    };
}
}
