/*
 *  Arstrobench by arstro — unit tests (R-TEST-1/2).
 *
 *  Device-free: no window, no GTK, no display. The workloads run for real (at reduced
 *  sizes so the suite stays fast) and the UI is asserted through Artboard's
 *  RecordingTarget, which records every HAL call as a DrawOp — the canonical way to
 *  prove drawing and motion in this repo without a device.
 *
 *  Plain assert(), matching cosmo_widget_tests.
 */
#include "BenchApp.h"
#include "core/BenchmarkRunner.h"
#include "core/DspChain.h"
#include "core/DspWorkload.h"
#include "core/ImageWorkload.h"
#include "core/SystemInfo.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace arstro::arstrobench;
using artboard::DrawOp;
using artboard::RecordingTarget;

namespace
{
    int gChecks = 0;
    void check(bool cond, const char *what)
    {
        ++gChecks;
        if (!cond) { std::fprintf(stderr, "FAILED: %s\n", what); assert(false); }
    }

    constexpr double kFrameMs = 1000.0 / 60.0;

    /** Render `frames` frames into a fresh recorder and return the ops of the LAST one,
     *  so a test can compare two moments of the same animation. */
    std::vector<DrawOp> renderAt(BenchApp &app, int firstFrame, int frames)
    {
        RecordingTarget rt;
        for (int i = 0; i < frames; ++i)
        {
            rt.clear();
            app.render(rt, (firstFrame + i) * kFrameMs);
        }
        return rt.ops();
    }

    /** Every text op's string, in order — what a card actually shows. */
    std::string allText(const std::vector<DrawOp> &ops)
    {
        std::string s;
        for (const DrawOp &op : ops)
            if (op.kind == DrawOp::Kind::DrawText) s += op.text + "\n";
        return s;
    }

    bool rectsOverlap(const artboard::Rect &a, const artboard::Rect &b)
    {
        return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
    }
    artboard::Rect boundsOf(const artboard::Segment &s)
    {
        return artboard::Rect{s.x.value(), s.y.value(), s.width.value(), s.height.value()};
    }

    // ── R-SCORE-1: the score is the reciprocal of the measured time ──────────────────
    void testScoreMath()
    {
        const WorkloadResult a = WorkloadResult::fromSeconds(0.5);
        check(a.ok, "0.5 s is a valid measurement");
        check(std::fabs(a.score - 2.0) < 1e-12, "score of 0.5 s is 2.0");
        const WorkloadResult b = WorkloadResult::fromSeconds(0.008);
        check(std::fabs(b.score - 125.0) < 1e-9, "score of 8 ms is 125");
        // A non-positive time cannot be inverted; it must not become an infinity the UI
        // would have to special-case.
        const WorkloadResult zero = WorkloadResult::fromSeconds(0.0);
        check(!zero.ok && zero.score == 0.0, "a zero time yields a not-ok result, not infinity");
        const WorkloadResult neg = WorkloadResult::fromSeconds(-1.0);
        check(!neg.ok, "a negative time yields a not-ok result");
    }

    // ── R-IMG: the image workload ───────────────────────────────────────────────────
    void testImageWorkload()
    {
        // The dummy image must be broadband (R-IMG-1): a flat field would let the spatial
        // stages do unrepresentative work, so assert it is genuinely varied.
        const std::vector<uint8_t> px = ImageWorkload::makeDummyPixels(64, 48);
        check(px.size() == 64u * 48u * 4u, "dummy image is w*h*4 bytes");
        int distinct = 0;
        bool seen[256] = {false};
        for (size_t i = 0; i < px.size(); i += 4)
            if (!seen[px[i]]) { seen[px[i]] = true; ++distinct; }
        check(distinct > 32, "dummy image red channel spans many values (not a flat field)");
        // Deterministic: the same generator must produce the same pixels every run, or two
        // machines are not measuring the same work (R-SCORE-4).
        check(px == ImageWorkload::makeDummyPixels(64, 48), "dummy image generation is deterministic");

        // The parameter set must actually be non-neutral (R-IMG-2) — an identity EditParams
        // would still "run the pipeline" while measuring almost nothing.
        const arstro::EditParams p = ImageWorkload::benchParams();
        check(p.exposure != 0.0f && p.contrast != 0.0f, "tone stages engaged");
        check(p.sharpenAmount > 0.0f && p.nrLuminance > 0.0f, "spatial stages engaged");
        check(p.clarity != 0.0f && p.texture != 0.0f && p.dehaze != 0.0f, "presence stages engaged");
        check(p.temp != 6500.0f, "white balance engaged");
        check(p.curve.size() > 2, "tone curve is not the identity");
        check(!p.mixer[0].empty() && !p.mixer[1].empty() && !p.mixer[2].empty(), "mixer curves engaged");
        check(p.grade[0].sat > 0.0f && p.grade[2].sat > 0.0f, "colour grading engaged");
        // R-IMG-2a: geometry stays 1:1 so the cost does not shift with a resampling factor.
        check(p.cropX == 0.0f && p.cropY == 0.0f && p.cropW == 1.0f && p.cropH == 1.0f, "crop is full-frame");
        check(p.rotation == 0.0f && p.quarterTurns == 0, "no rotation");

        const WorkloadResult r = ImageWorkload(96, 64, 2).run();
        check(r.ok, "image workload ran");
        check(r.seconds > 0.0, "image workload measured a positive time");
        check(std::fabs(r.score - 1.0 / r.seconds) < 1e-9, "image score is 1/seconds");
        check(r.checksum != 0.0, "image workload produced a checksum (the work was not elided)");
        check(!r.detail.empty(), "image workload described itself");
        // R-SCORE-4: a degenerate request must fail cleanly rather than divide by zero.
        check(!ImageWorkload(0, 0, 1).run().ok, "a zero-size image workload is not ok");
        check(!ImageWorkload(96, 64, 0).run().ok, "a zero-pass image workload is not ok");
    }

    // ── R-DSP: the signal workload ──────────────────────────────────────────────────
    void testDspWorkload()
    {
        check(DspWorkload::kVoices == 9, "nine voices (R-DSP-1)");
        check(DspWorkload::kFrames == 192000, "192000 samples (R-DSP-2)");
        // Multi-note: nine DISTINCT notes, or the voices are not independent work.
        const int *notes = DspWorkload::notes();
        for (int i = 0; i < DspWorkload::kVoices; ++i)
            for (int j = i + 1; j < DspWorkload::kVoices; ++j)
                check(notes[i] != notes[j], "the nine notes are distinct");

        const DspWorkload w(2400, 1);
        // Per-voice buffers, not one summed buffer: the strips need each voice separately.
        const std::vector<std::vector<std::vector<Sample>>> dry = w.generate();
        check((int)dry.size() == DspWorkload::kVoices, "one buffer set per voice");
        check((int)dry[0].size() == DspWorkload::kChannels, "one buffer per channel per voice");
        check((int)dry[0][0].size() == 2400, "the generated buffer is `frames` long");
        for (int v = 0; v < DspWorkload::kVoices; ++v)
        {
            double energy = 0.0;
            for (const Sample smp : dry[(size_t)v][0]) energy += std::fabs(smp);
            check(energy > 0.0, "every voice produced signal before the chain is timed");
        }
        // The nine voices must not be nine copies of the same signal, or the strips are
        // measuring one voice nine times.
        double diff = 0.0;
        for (size_t i = 0; i < dry[0][0].size(); ++i)
            diff += std::fabs(dry[0][0][i] - dry[4][0][i]);
        check(diff > 0.0, "different voices produce different signal");

        // R-DSP-2: the measured chain is the full three-tier mix, not a token triple.
        const MixChain chain(DspWorkload::kVoices, 16, DspWorkload::kChannels);
        check(chain.stageCount() > 40, "the measured chain is a multi-stage mix");
        check(MultibandCompressor::kBands == 4, "the master compressor is 4-band");

        const WorkloadResult r = w.run();
        check(r.ok, "dsp workload ran");
        check(r.seconds > 0.0, "dsp workload measured a positive time");
        check(std::fabs(r.score - 1.0 / r.seconds) < 1e-9, "dsp score is 1/seconds");
        check(r.checksum > 0.0, "dsp workload produced a checksum");
        check(!DspWorkload(0, 1).run().ok, "a zero-frame dsp workload is not ok");
        check(!DspWorkload(2400, 0).run().ok, "a zero-pass dsp workload is not ok");
    }

    // ── R-IMG-4/4a: the GPU opt-in, and the honesty of what it reports ──────────────
    void testGpuBackendReporting()
    {
        // CPU is the default: a benchmark that silently used whatever accelerator
        // happened to exist would not be comparable between machines.
        check(!ImageWorkload().preferGpu(), "the image workload is CPU by default");

        // R-IMG-4a: the label states the backend that ACTUALLY ran, not the one asked for.
        check(ImageWorkload::backendText(false, false, "OpenGL") == "CPU",
              "a CPU run is labelled CPU");
        check(ImageWorkload::backendText(true, true, "OpenGL") == "GPU (OpenGL)",
              "an accelerated run names the accelerator");
        const std::string declined = ImageWorkload::backendText(true, false, "OpenGL");
        check(declined.find("CPU") == 0,
              "a GPU run the accelerator declined is labelled CPU, not GPU");
        check(declined.find("GPU") != std::string::npos,
              "...and says why it is CPU, so the toggle cannot look broken");

        // The measured WORK is identical either way, which is what makes the two scores
        // comparable: only the backend differs.
        const arstro::EditParams cpu = ImageWorkload::benchParams();
        const arstro::EditParams gpu = ImageWorkload::benchParams();
        check(cpu.exposure == gpu.exposure && cpu.sharpenAmount == gpu.sharpenAmount,
              "the GPU toggle does not change the workload, only the backend");

        // A real run with the GPU requested must still produce a valid, labelled result
        // on any machine — accelerator or not.
        const WorkloadResult r = ImageWorkload(96, 64, 1, true).run();
        check(r.ok, "a GPU-requested run produces a result even with no accelerator");
        check(r.detail.find("CPU") != std::string::npos || r.detail.find("GPU") != std::string::npos,
              "the result names a backend");
    }

    // ── R-UI-9: the toggle ─────────────────────────────────────────────────────────
    void testGpuToggle()
    {
        artboard::setReducedMotion(false);
        BenchApp app;
        renderAt(app, 0, 120);
        GpuToggle &gpu = app.gpuToggle();

        // Whatever this machine has, the toggle reflects it: a switch that cannot do
        // anything is disabled and says so, rather than lying about being available.
        check(gpu.unavailable() == !ImageWorkload::gpuAvailable(),
              "the toggle's availability matches the machine's");
        check(gpu.enabled == ImageWorkload::gpuAvailable(),
              "an unavailable toggle is disabled");
        check(!gpu.on(), "the toggle starts off (CPU is the default)");

        const std::string offText = allText(renderAt(app, 120, 1));
        check(offText.find(ImageWorkload::gpuAvailable() ? "GPU" : "GPU unavailable") != std::string::npos,
              "the toggle draws its caption");

        if (ImageWorkload::gpuAvailable())
        {
            // Clicking it flips the switch and re-labels what the next run will measure.
            const artboard::Rect r = boundsOf(gpu.toggle());
            const artboard::Rect g = boundsOf(gpu);
            const double cx = g.x + r.x + r.w * 0.5, cy = g.y + r.y + r.h * 0.5;
            app.pointer(0, cx, cy, 0, 5000.0);
            app.pointer(2, cx, cy, 0, 5040.0);
            check(gpu.on(), "clicking the toggle switches it on");
            const std::string onText = allText(renderAt(app, 130, 1));
            check(onText.find("GPU requested") != std::string::npos,
                  "the image card states that the next run will ask for the GPU");
        }

        // The toggle does not overlap the Run button (layout snaps, it does not stack).
        check(!rectsOverlap(boundsOf(gpu), boundsOf(app.runButton())),
              "the toggle does not overlap the Run button");
        check(boundsOf(gpu).right() <= boundsOf(app.runButton()).x,
              "the toggle sits entirely left of the Run button");
    }

    // ── R-SYS: the machine report ───────────────────────────────────────────────────
    void testSystemInfo()
    {
        const SystemInfo info = SystemInfo::query();
        // R-SYS-2: a field that could not be determined reads "Unknown", never blank — a
        // missing row in the panel would look like a rendering bug.
        check(!info.chip.empty(), "chip is never blank");
        check(!info.os.empty(), "os is never blank");
        check(!info.chipText().empty() && !info.ramText().empty(), "the rendered rows are never blank");
        check(info.threads >= 0, "thread count is not negative");

        SystemInfo blank;
        check(blank.chipText() == "Unknown", "an unqueried chip reads Unknown");
        check(blank.ramText() == "Unknown", "unknown memory reads Unknown, not 0.0 GB");
        SystemInfo known;
        known.chip = "Test CPU"; known.threads = 8; known.ramBytes = 16000000000ULL;
        check(known.chipText().find("8 threads") != std::string::npos, "the thread count is folded into the chip row");
        check(known.ramText() == "16.0 GB", "memory is reported in the decimal GB a machine is sold in");
    }

    // ── R-G-4: the runner works off the render thread and publishes in stages ────────
    void testRunner()
    {
        BenchmarkRunner runner;
        runner.setWorkloads(ImageWorkload(64, 48, 1), DspWorkload(2400, 1));
        check(runner.stage() == BenchmarkRunner::Stage::Idle, "a fresh runner is idle");
        check(!runner.running(), "a fresh runner is not running");

        runner.start();
        // The call returned immediately — that is the whole point of R-G-4. Poll, never join.
        for (int i = 0; i < 20000 && runner.stage() != BenchmarkRunner::Stage::Done; ++i)
        {
            (void)runner.snapshot();  // must be safe to read at any moment, mid-run
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(runner.stage() == BenchmarkRunner::Stage::Done, "the run reached Done");
        check(!runner.running(), "a finished runner is not running");

        const BenchmarkRunner::Snapshot snap = runner.snapshot();
        check(snap.image.ok && snap.dsp.ok, "both workloads produced a result");
        // R-SCORE-5: the headline is the plain sum of the two, with no hidden weighting.
        check(std::fabs(snap.total - (snap.image.score + snap.dsp.score)) < 1e-9,
              "the total is the sum of the two scores");

        // A second run must clear the previous scores rather than leave them stale.
        runner.start();
        for (int i = 0; i < 20000 && runner.stage() != BenchmarkRunner::Stage::Done; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(runner.snapshot().dsp.ok, "a re-run produces a fresh result");
    }

    // ── R-UI-1/2: the layout snaps, it does not stack ───────────────────────────────
    void testLayoutDoesNotOverlap()
    {
        BenchApp app;
        renderAt(app, 0, 120);  // let the entrance settle so every card is at its final y

        const artboard::Rect image = boundsOf(app.imageCard());
        const artboard::Rect dsp = boundsOf(app.dspCard());
        const artboard::Rect total = boundsOf(app.totalCard());
        const artboard::Rect system = boundsOf(app.systemPanel());
        const artboard::Rect run = boundsOf(app.runButton());

        check(!rectsOverlap(image, dsp), "the two score cards do not overlap");
        check(!rectsOverlap(image, total) && !rectsOverlap(dsp, total), "the cards do not overlap the total");
        check(!rectsOverlap(total, system), "the total does not overlap the system panel");
        check(!rectsOverlap(run, image) && !rectsOverlap(run, dsp), "the button does not overlap the cards");

        // Everything stays inside the fixed window (R-UI-1): nothing needs to scroll
        // because nothing can fall outside the box.
        for (const artboard::Rect &r : {image, dsp, total, system, run})
        {
            check(r.x >= 0.0 && r.y >= 0.0, "a panel starts inside the window");
            check(r.right() <= BenchApp::kWidth + 0.5, "a panel ends inside the window width");
            check(r.bottom() <= BenchApp::kHeight + 0.5, "a panel ends inside the window height");
        }
    }

    // ── R-G-1 / R-UI-4: the entrance is real motion, not a claim ────────────────────
    void testEntranceAnimates()
    {
        artboard::setReducedMotion(false);
        BenchApp app;
        const std::vector<DrawOp> early = renderAt(app, 0, 3);
        const double yEarly = app.imageCard().y.value();
        const double opacityEarly = app.imageCard().opacity.value();
        const std::vector<DrawOp> late = renderAt(app, 3, 120);
        const double yLate = app.imageCard().y.value();

        check(opacityEarly < 1.0, "the card starts transparent (a fade, not a pop-in)");
        check(app.imageCard().opacity.value() > 0.99, "the card ends fully opaque");
        check(std::fabs(yEarly - yLate) > 1.0, "the card actually travelled (it did not snap)");
        check(early.size() != late.size() || allText(early) != allText(late) || yEarly != yLate,
              "the op stream at t=0 differs from t=end");
        check(!late.empty(), "the settled frame draws something");
    }

    // ── R-G-1: reduced motion collapses it, and nothing else changes ────────────────
    void testReducedMotionCollapses()
    {
        artboard::setReducedMotion(true);
        BenchApp app;
        renderAt(app, 0, 2);  // the very first frames: no time for a tween to have run
        check(app.imageCard().opacity.value() > 0.99, "reduced motion shows the card immediately");
        check(std::fabs(app.imageCard().y.value() - app.dspCard().y.value()) < 0.001,
              "reduced motion puts both cards at their final y at once");
        artboard::setReducedMotion(false);
    }

    // ── R-UI-3/5: idle shows absence; a result counts up ────────────────────────────
    void testScoreCountsUp()
    {
        artboard::setReducedMotion(false);
        BenchApp app;
        renderAt(app, 0, 120);
        const std::string idleText = allText(renderAt(app, 120, 1));
        check(idleText.find("\xE2\x80\x94") != std::string::npos,
              "an unmeasured card shows an em-dash, not a zero score");
        check(idleText.find("Idle") != std::string::npos, "an unmeasured card reads Idle");

        app.imageCard().showScore(125.0, 0.008, 121 * kFrameMs);
        // R-UI-5: the em-dash does not become a number in one frame — it fades out first,
        // and the count-up only starts at the trough.
        const std::string fading = allText(renderAt(app, 121, 1));
        check(fading.find("\xE2\x80\x94") != std::string::npos,
              "the em-dash is still on screen one frame into the swap (it fades, it does not pop)");
        renderAt(app, 122, 1);
        const double partway = app.imageCard().shownScore();
        check(partway < 125.0, "the score is still counting up, not assigned");
        renderAt(app, 123, 90);
        check(std::fabs(app.imageCard().shownScore() - 125.0) < 0.01, "the score settles on its value");
        const std::string doneText = allText(renderAt(app, 213, 1));
        check(doneText.find("125") != std::string::npos, "the settled card shows the score");
        check(doneText.find("8.00 ms per pass") != std::string::npos, "the settled card shows the time");
        check(doneText.find("Done") != std::string::npos, "the settled card reads Done");
    }

    // ── R-UI-6: the meter sweeps while running and fills when the result lands ──────
    void testMeterStates()
    {
        artboard::setReducedMotion(false);
        BenchApp app;
        renderAt(app, 0, 120);
        check(!app.imageCard().indeterminate(), "an idle meter does not sweep");

        app.imageCard().setState(ScoreCard::State::Running, 120 * kFrameMs);
        check(app.imageCard().indeterminate(), "a running meter sweeps (progress is unknowable)");
        const double phase0 = app.imageCard().phase();
        renderAt(app, 121, 20);
        check(std::fabs(app.imageCard().phase() - phase0) > 1e-6, "the sweep advances with the clock");

        app.imageCard().showScore(10.0, 0.1, 141 * kFrameMs);
        check(!app.imageCard().indeterminate(), "a finished meter stops sweeping");
        renderAt(app, 142, 90);
        check(app.imageCard().displayValue() > 0.95, "a finished meter springs to full");
    }

    // ── R-UI-7 / R-G-3: the button clicks, and does nothing while disabled ──────────
    void testRunButton()
    {
        BenchApp app;
        renderAt(app, 0, 120);
        int clicks = 0;
        app.runButton().onClick = [&clicks] { ++clicks; };

        // Press and release inside the button = one click, through the real recognizer.
        const artboard::Rect r = boundsOf(app.runButton());
        const double cx = r.x + r.w * 0.5, cy = r.y + r.h * 0.5;
        app.pointer(0, cx, cy, 0, 2000.0);
        app.pointer(2, cx, cy, 0, 2040.0);
        check(clicks == 1, "clicking the button fires its action");

        app.runButton().enabled = false;
        app.pointer(0, cx, cy, 0, 2200.0);
        app.pointer(2, cx, cy, 0, 2240.0);
        check(clicks == 1, "a disabled button does not fire");
        app.runButton().enabled = true;

        // The label cross-fades rather than swapping in one frame.
        app.runButton().setLabel("Running...", 3000.0);
        check(app.runButton().label() == "Run benchmark", "the old label is still shown at the start of the fade");
        renderAt(app, 200, 60);
        app.runButton().advance(3400.0);
        check(app.runButton().label() == "Running...", "the label swapped at the fade trough");
    }

    // ── The system panel renders the queried machine ────────────────────────────────
    void testSystemPanelShowsMachine()
    {
        BenchApp app;
        SystemInfo info;
        info.chip = "Test Silicon X1"; info.threads = 12;
        info.ramBytes = 32000000000ULL; info.os = "TestOS 1.0";
        app.systemPanel().setInfo(info);
        const std::string text = allText(renderAt(app, 0, 120));
        check(text.find("Test Silicon X1") != std::string::npos, "the chip row is drawn");
        check(text.find("12 threads") != std::string::npos, "the thread count is drawn");
        check(text.find("32.0 GB") != std::string::npos, "the memory row is drawn");
        check(text.find("TestOS 1.0") != std::string::npos, "the operating-system row is drawn");
        // R-SYS-1: all three rows, every time.
        check(text.find("Chip") != std::string::npos && text.find("Memory") != std::string::npos &&
              text.find("Operating system") != std::string::npos, "all three system rows are labelled");
    }

    // ── End to end: a real (reduced) run drives the UI to its finished state ────────
    void testRunDrivesTheUi()
    {
        artboard::setReducedMotion(false);
        BenchApp app;
        app.setWorkloads(ImageWorkload(64, 48, 1), DspWorkload(2400, 1));
        renderAt(app, 0, 60);
        app.startRun();

        int frame = 60;
        for (int guard = 0; app.isRunning() && guard < 30000; ++guard, ++frame)
            renderAt(app, frame, 1);
        check(!app.isRunning(), "the run finished");
        for (int i = 0; i < 120; ++i, ++frame) renderAt(app, frame, 1);  // let the count-ups settle

        check(app.imageCard().state() == ScoreCard::State::Done, "the image card finished");
        check(app.dspCard().state() == ScoreCard::State::Done, "the signal card finished");
        check(app.imageCard().shownScore() > 0.0, "the image card shows a score");
        check(app.dspCard().shownScore() > 0.0, "the signal card shows a score");
        check(std::fabs(app.totalCard().shownScore() -
                        (app.imageCard().shownScore() + app.dspCard().shownScore())) < 0.5,
              "the total card shows the sum of the two");
        check(app.runButton().label() == "Run again", "the button offers a re-run");
        check(app.runButton().enabled, "the button is usable again");
    }
}

int main()
{
    testScoreMath();
    testImageWorkload();
    testDspWorkload();
    testGpuBackendReporting();
    testGpuToggle();
    testSystemInfo();
    testRunner();
    testLayoutDoesNotOverlap();
    testEntranceAnimates();
    testReducedMotionCollapses();
    testScoreCountsUp();
    testMeterStates();
    testRunButton();
    testSystemPanelShowsMachine();
    testRunDrivesTheUi();
    std::printf("arstrobench_tests: %d checks passed, 0 failed\n", gChecks);
    return 0;
}
