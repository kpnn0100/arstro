#include "BenchApp.h"
#include "Theme.h"

namespace arstro
{
namespace arstrobench
{
    using namespace artboard;

    namespace
    {
        // ── the fixed composition (R-UI-1/2). One place; the tests read these too. ──
        constexpr double kPad = 28.0;
        constexpr double kHeaderH = 66.0;
        constexpr double kGap = 20.0;
        constexpr double kCardsY = 94.0;
        constexpr double kCardH = 214.0;
        constexpr double kTotalY = 328.0;
        constexpr double kTotalH = 88.0;
        constexpr double kSystemY = 436.0;
        constexpr double kSystemH = 188.0;
        constexpr double kFooterBaseline = 660.0;
        constexpr double kButtonW = 150.0;
        constexpr double kButtonH = 34.0;
        constexpr double kStaggerMs = 70.0;
    }

    BenchApp::BenchApp(double width, double height)
        : mW(width), mH(height), mInfo(SystemInfo::query())
    {
        mRoot = std::make_shared<Segment>();
        mRoot->width.set(mW);
        mRoot->height.set(mH);

        const double contentW = mW - kPad * 2.0;
        const double cardW = (contentW - kGap) * 0.5;

        // The cards state what they WILL measure before a run, not only afterwards.
        mImageCard = std::make_shared<ScoreCard>("IMAGE PROCESSING", accent::image());
        mImageCard->setDetail(ImageWorkload().describe());
        mDspCard = std::make_shared<ScoreCard>("SIGNAL PROCESSING", accent::signal());
        mDspCard->setDetail(DspWorkload().describe());

        mImageCard->x.set(kPad);
        mImageCard->width.set(cardW);
        mImageCard->height.set(kCardH);
        mDspCard->x.set(kPad + cardW + kGap);
        mDspCard->width.set(cardW);
        mDspCard->height.set(kCardH);

        mTotal = std::make_shared<TotalCard>();
        mTotal->x.set(kPad);
        mTotal->width.set(contentW);
        mTotal->height.set(kTotalH);

        mSystem = std::make_shared<SystemPanel>();
        mSystem->x.set(kPad);
        mSystem->width.set(contentW);
        mSystem->height.set(kSystemH);
        mSystem->setInfo(mInfo);

        mRun = std::make_shared<RunButton>();
        mRun->x.set(mW - kPad - kButtonW);
        mRun->y.set((kHeaderH - kButtonH) * 0.5);
        mRun->width.set(kButtonW);
        mRun->height.set(kButtonH);
        mRun->onClick = [this] { startRun(); };

        mRoot->addChild(mImageCard);
        mRoot->addChild(mDspCard);
        mRoot->addChild(mTotal);
        mRoot->addChild(mSystem);
        mRoot->addChild(mRun);

        mRecognizer.setSink([this](const Gesture &g) { mRoot->onGesture(g); });
    }

    void BenchApp::startRun()
    {
        if (mRunner.running()) return;
        mRunner.start();
    }

    void BenchApp::pointer(int kind, double x, double y, int button, double timeMs)
    {
        const RawPointer::Kind k = kind == 0 ? RawPointer::Kind::Down
                                  : kind == 2 ? RawPointer::Kind::Up
                                              : RawPointer::Kind::Move;
        const PointerButton b = button == 2 ? PointerButton::Right : PointerButton::Left;
        mRecognizer.feed(RawPointer{k, Point{x, y}, b, timeMs});
    }

    void BenchApp::pollRunner(double nowMs)
    {
        const BenchmarkRunner::Stage stage = mRunner.stage();
        if (stage == mLastStage) return;
        mLastStage = stage;

        const BenchmarkRunner::Snapshot snap = mRunner.snapshot();
        switch (stage)
        {
        case BenchmarkRunner::Stage::Idle:
            break;
        case BenchmarkRunner::Stage::Image:
            // A fresh run clears the previous result rather than leaving a stale score
            // sitting under a spinning meter.
            mImageCard->setState(ScoreCard::State::Running, nowMs);
            mDspCard->setState(ScoreCard::State::Idle, nowMs);
            mTotal->showScore(0.0, false, nowMs);
            mRun->enabled = false;
            mRun->setLabel("Running...", nowMs);
            break;
        case BenchmarkRunner::Stage::Dsp:
            if (snap.image.ok)
            {
                mImageCard->setDetail(snap.image.detail);  // what was actually measured
                mImageCard->showScore(snap.image.score, snap.image.seconds, nowMs);
            }
            mDspCard->setState(ScoreCard::State::Running, nowMs);
            break;
        case BenchmarkRunner::Stage::Done:
            if (snap.image.ok)
            {
                mImageCard->setDetail(snap.image.detail);
                mImageCard->showScore(snap.image.score, snap.image.seconds, nowMs);
            }
            if (snap.dsp.ok)
            {
                mDspCard->setDetail(snap.dsp.detail);
                mDspCard->showScore(snap.dsp.score, snap.dsp.seconds, nowMs);
            }
            mTotal->showScore(snap.total, snap.image.ok && snap.dsp.ok, nowMs);
            mRun->enabled = true;
            mRun->setLabel("Run again", nowMs);
            break;
        }
    }

    void BenchApp::render(IRenderTarget &target, double nowMs)
    {
        if (!mEntered)
        {
            // Entrance, staggered by index (R-UI-4). Done on the first frame rather than
            // in the constructor because it needs the host's clock.
            mEntered = true;
            mImageCard->enter(kCardsY, 0.0, nowMs);
            mDspCard->enter(kCardsY, kStaggerMs, nowMs);
            mTotal->enter(kTotalY, kStaggerMs * 2.0, nowMs);
            mSystem->enter(kSystemY, kStaggerMs * 3.0, nowMs);
        }

        pollRunner(nowMs);
        mRoot->advance(nowMs);

        // ── background + header chrome (drawn by the app, under the segment tree) ──
        target.save();
        target.setTransform(Transform::identity());
        drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(palette::background()));

        target.setFill(palette::foreground());
        target.drawText("arstrobench", kPad, 41.0, 22.0, font::sansSemiBold(), -0.66);
        const double markW = target.measureText("arstrobench", 22.0, font::sansSemiBold(), -0.66);
        // The accent dot after the wordmark -- cosmo's own wordmark treatment.
        drawRoundedRect(target, Rect{kPad + markW + 3.0, 34.0, 5.0, 5.0}, radius::pill(),
                        Paint::filled(palette::primary()));
        target.setFill(palette::mutedForeground());
        target.drawText("by arstro", kPad + markW + 18.0, 41.0, 12.0, font::sans());

        // Header rule, snapped to the header's bottom edge.
        drawRoundedRect(target, Rect{0, kHeaderH, mW, 1.0}, 0.0, Paint::filled(palette::border()));

        // Methodology footer: the scoring model stated where the score is read.
        target.setFill(palette::mutedForeground());
        target.drawText("score = 1 / seconds  ·  fastest of 3 image passes and 5 signal passes  ·  "
                        "generation is never timed",
                        kPad, kFooterBaseline, 10.0, font::mono());
        target.restore();

        mRoot->render(target);
        mRoot->renderOverlay(target);
    }
}
}
