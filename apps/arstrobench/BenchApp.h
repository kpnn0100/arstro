/*
 *  Arstrobench by arstro — the app: header, two workload cards, the total, the system
 *  panel, and a methodology footer (R-UI-2).
 *
 *  It is the only class that knows both sides of the wall: it owns the BenchmarkRunner
 *  and, once per frame, moves whatever the worker has published into the widgets. The
 *  render loop never blocks on the measurement (R-G-4).
 *
 *  Platform-free: only Artboard's Segment/IRenderTarget plus the UI-free bench core.
 */
#pragma once
#include "../../core/Artboard/include/artboard/artboard.h"
#include "core/BenchmarkRunner.h"
#include "core/SystemInfo.h"
#include "widgets/GpuToggle.h"
#include "widgets/RunButton.h"
#include "widgets/ScoreCard.h"
#include "widgets/SystemPanel.h"
#include "widgets/TotalCard.h"
#include <memory>

namespace arstro
{
namespace arstrobench
{
    class BenchApp
    {
    public:
        // The window is a fixed composition sized to its content (R-UI-1).
        static constexpr double kWidth = 1000.0;
        static constexpr double kHeight = 700.0;

        BenchApp(double width = kWidth, double height = kHeight);

        void render(artboard::IRenderTarget &target, double nowMs);
        void pointer(int kind, double x, double y, int button, double timeMs);

        /** Kick off a run (no-op while one is in flight). Wired to the Run button. */
        void startRun();
        bool isRunning() const { return mRunner.running(); }

        /** Shrink the workloads — the unit tests must not spend seconds on plumbing. */
        void setWorkloads(const ImageWorkload &image, const DspWorkload &dsp)
        {
            mRunner.setWorkloads(image, dsp);
        }

        // ── inspection seams (tests, R-TEST-2) ──
        ScoreCard &imageCard() { return *mImageCard; }
        ScoreCard &dspCard() { return *mDspCard; }
        TotalCard &totalCard() { return *mTotal; }
        SystemPanel &systemPanel() { return *mSystem; }
        RunButton &runButton() { return *mRun; }
        GpuToggle &gpuToggle() { return *mGpu; }
        const SystemInfo &system() const { return mInfo; }

    private:
        void pollRunner(double nowMs);

        double mW, mH;
        bool mEntered = false;
        BenchmarkRunner::Stage mLastStage = BenchmarkRunner::Stage::Idle;

        std::shared_ptr<artboard::Segment> mRoot;
        std::shared_ptr<ScoreCard> mImageCard, mDspCard;
        std::shared_ptr<TotalCard> mTotal;
        std::shared_ptr<SystemPanel> mSystem;
        std::shared_ptr<RunButton> mRun;
        std::shared_ptr<GpuToggle> mGpu;

        artboard::GestureRecognizer mRecognizer;
        BenchmarkRunner mRunner;
        SystemInfo mInfo;
    };
}
}
