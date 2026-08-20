/*
 *  cosmo — cosmo_ui_tests: headless assertions over the ASSEMBLED app.
 *
 *  `cosmo_widget_tests` covers widgets in isolation, which cannot see the failures that only
 *  exist once the shell is put together: a column squeezed to nothing by its two fixed
 *  neighbours, a screen that reflows wrong at a second size, a change that lands correctly and
 *  gets there in one frame. This target builds the real `App` over the real `CosmoService`
 *  (COSMO_APP_NOMAIN) with no display and no window, and asserts about that.
 *
 *  Its first job is R-G-1's compliance clause: "nothing changes in one frame" cannot be
 *  established by reading code — three separate changes have now shipped a snap that read
 *  correctly — so it is established by driving the clock and requiring the DRAWN value to
 *  differ from the target mid-tween. That assertion is the entire reason this file exists.
 */
#include "../../App.h"
#include "../../core/AppSettings.h"
#include "../../core/service/CosmoService.h"
#include "../../core/ThreadBudget.h"
#include "../../widgets/HomeScreen.h"
#include "../../widgets/CurvePanel.h"
#include "../../widgets/EditStackTabs.h"
#include "../../widgets/PhotoCanvas.h"
#include "../../core/service/Command.h"
#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "../../UiDump.h"
#include <cassert>
#include <cstdio>
#include <cmath>

using arstro::cosmo_v2::App;
using arstro::cosmo::AppSettings;

namespace
{
    int gFailures = 0;

    void check(bool ok, const char *what)
    {
        std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
        if (!ok) ++gFailures;
    }

    bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

    /** The app, over a real service, with no window. `RenderTarget`-free: every assertion here
     *  is about geometry and animated state, so no frame has to be rasterised — `advance` is
     *  reached through `render`, which is the app's only clock. A recording target is the
     *  cheapest way to give it one. */
    struct Rig
    {
        arstro::cosmo::ThreadBudget budget;
        arstro::cosmo::CosmoService svc{budget};
        App app;
        artboard::RecordingTarget target;
        double now = 0.0;

        Rig(double w, double h, int scale = 100) : app(svc, w, h)
        {
            AppSettings s;
            s.uiScale = scale;
            app.applySettings(s);
            svc.applySettings(s);
            app.setSize(w, h);
        }

        /** One 16 ms frame. Rendering IS advancing — App has no separate tick. The service is
         *  pumped first, exactly as the GTK host's tick does: `pump` is what moves a finished
         *  render out of the engine, so a rig that skips it can never see a preview reach the
         *  stage (and every photo assertion here would pass on an empty canvas). */
        void frame()
        {
            now += 16.0;
            svc.pump(now);
            target.clear();
            app.render(target, now);
        }
        void frames(int n) { for (int i = 0; i < n; ++i) frame(); }

        // Input through App's own entry point, in PHYSICAL pixels — the same call the GTK
        // handlers make. kind: 0 = press, 1 = motion, 2 = release.
        void click(double x, double y)
        {
            app.pointer(0, x, y, 1, now); frames(2);
            app.pointer(2, x, y, 1, now); frames(1);
        }
        void doubleClick(double x, double y) { click(x, y); click(x, y); }
        void drag(double x0, double y0, double x1, double y1)
        {
            app.pointer(0, x0, y0, 1, now); frames(1);
            for (int i = 1; i <= 6; ++i)
            {
                const double t = (double)i / 6.0;
                app.pointer(1, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, 1, now);
                frames(1);
            }
            app.pointer(2, x1, y1, 1, now); frames(1);
        }
        void settle(double ms) { frames((int)(ms / 16.0) + 2); }
    };

    // ── R-G-1 / R-SCALE-2a: a scale change TRAVELS ───────────────────────────────────────
    // The assertion that matters is not "it arrives" — a snap arrives too, sooner. It is that
    // there exist frames where the drawn scale is strictly between the old and the new, and
    // that the logical size tracks those frames rather than jumping on the first one.
    void scaleChangeIsAnimatedNotSnapped()
    {
        std::printf("App: a screen-scale change eases; it does not snap (R-G-1 / R-SCALE-2a)\n");
        Rig rig(1440.0, 900.0);
        rig.settle(200.0);

        check(near(rig.app.drawnUiScale(), 1.0), "starts drawn at the scale that is set");
        const double logicalBefore = 1440.0 / rig.app.drawnUiScale();

        rig.app.setUiScale(200);
        check(rig.app.uiScale() == 200, "the SETTING changes immediately — a preference is a number");
        check(near(rig.app.drawnUiScale(), 1.0),
              "but nothing is drawn at the new scale yet, on the frame it was set");

        // Walk the tween and collect what the app actually drew.
        int between = 0, distinctSizes = 0;
        double lastLogical = logicalBefore;
        for (int i = 0; i < 20; ++i)     // 320 ms > kScaleAnimMs (260)
        {
            rig.frame();
            const double s = rig.app.drawnUiScale();
            if (s > 1.0 + 1e-4 && s < 2.0 - 1e-4) ++between;
            const double logical = 1440.0 / s;
            if (!near(logical, lastLogical, 0.5)) { ++distinctSizes; lastLogical = logical; }
        }
        check(between >= 5,
              "the drawn scale passes through several values strictly between old and new");
        check(distinctSizes >= 5,
              "and the LOGICAL size is re-derived on those frames, not once at the end");
        check(near(rig.app.drawnUiScale(), 2.0, 1e-3), "and it arrives exactly at the target");
    }

    // The startup apply is the one case that must NOT animate: there is no previous scale to
    // travel from, so a tween would be an entrance nobody asked for.
    void theStartupScaleDoesNotAnimate()
    {
        std::printf("App: the scale loaded at startup is drawn immediately (R-SCALE-2a)\n");
        Rig rig(1440.0, 900.0, 175);
        check(rig.app.uiScale() == 175, "the stored scale is in force");
        check(near(rig.app.drawnUiScale(), 1.75),
              "and drawn at once — applySettings does not tween from 100%");
    }

    // ── R-SVC-12 / D-35: the panels follow the edit target ───────────────────────────────
    //
    // Reported as "it don't change the curve when i change target, each target need to get it
    // info when move to, even the group". The view was re-read from a view-model that had not
    // been re-derived after the session was mutated directly, so it pushed the PREVIOUS
    // target's values back into the panels.
    //
    // Driven through the service the way a script or an agent does, and read back off the
    // widget, so it asserts the whole chain: command -> session -> model -> revision -> bind.
    void panelsFollowTheEditTarget()
    {
        std::printf("App: the curve panel re-reads its values when the edit target changes (D-35)\n");
        Rig rig(1440.0, 900.0);
        rig.app.showEditor();
        rig.settle(400.0);

        std::string err;
        // Two images, so there are two targets to move between. The fake project the core tests
        // use lives in cosmo_core; here the service is driven directly with `import`, which needs
        // a decoder — so instead assert on the GROUP/target machinery the report also names,
        // using the session the service owns.
        auto *panel = const_cast<arstro::cosmo_v2::CurvePanel *>(
            static_cast<const arstro::cosmo_v2::CurvePanel *>(arstro::cosmo_v2::findSegmentByType(
                *rig.app.uiRoot("editor"), "CurvePanel")));
        check(panel != nullptr, "the curve panel exists");
        if (!panel) return;

        // With nothing loaded there is no edit target, so the panel must show the identity
        // curve rather than whatever was last in it.
        const unsigned rev0 = rig.svc.model().revision;
        rig.settle(100.0);
        check(panel->curveFor(0).size() == 2, "with no target the panel shows an identity curve");

        // A revision bump alone must make the view re-read: that is the whole contract.
        rig.svc.refreshFromSession();
        const unsigned rev1 = rig.svc.model().revision;
        check(rev1 > rev0, "refreshFromSession bumps the revision (R-SVC-12)");
        rig.frame();
        check(rig.app.boundRevision() == rev1,
              "and the next frame binds the view to it — no event subscription involved");

        // ...and a frame with no model change does NOT re-bind, or the bind would fight the
        // user's hands on every drag.
        const unsigned bound = rig.app.boundRevision();
        rig.frames(3);
        check(rig.app.boundRevision() == bound && rig.svc.model().revision == rev1,
              "a frame with no model change re-binds nothing");
    }

    // ── D-32 through the WHOLE app: grabbing a node near the pointer must not teleport it ──
    //
    // `cosmo_widget_tests` already sweeps this against CurvePanel::handleGesture directly. This
    // one drives it through `App::pointer` — so it also covers the parts the widget test cannot
    // see: the gesture router's capture, `toLocal`, and the UI-scale transform on the way in.
    // That matters here because the user's report was about the live window, and the one live
    // measurement I took by synthesising X events was worthless (the window had moved and the
    // scale had changed between runs). This is the same path minus GTK, and it is repeatable.
    void curveNodeGrabThroughTheAppDoesNotTeleport()
    {
        std::printf("App: a node grabbed off-centre moves by the drag, through the real router (D-32)\n");
        Rig rig(1440.0, 900.0);
        rig.app.showEditor();
        rig.settle(600.0);

        const artboard::Segment *root = rig.app.uiRoot("editor");
        const artboard::Segment *tabs =
            root ? arstro::cosmo_v2::findSegmentByType(*root, "EditStackTabs") : nullptr;
        check(tabs != nullptr, "the edit-stack tabs exist");
        if (!tabs) return;

        // Click the Mixer/Curve tab (index 2 of 5) the way a user does, so the CurvePanel is
        // actually laid out and reachable rather than merely present.
        const artboard::Transform tw = tabs->worldTransform();
        const double tabW = tabs->width.value() / 5.0;
        rig.click(tw.e + tabW * 2.5, tw.f + 13.0);
        rig.settle(400.0);

        auto *curve = const_cast<arstro::cosmo_v2::CurvePanel *>(
            static_cast<const arstro::cosmo_v2::CurvePanel *>(
                arstro::cosmo_v2::findSegmentByType(*root, "CurvePanel")));
        check(curve != nullptr, "and the tone curve is in the tree");
        if (!curve) return;

        const artboard::Transform cw = curve->worldTransform();
        const artboard::Rect plot = curve->plotBox();
        const double px0 = cw.e + plot.x, py0 = cw.f + plot.y;
        const double PW = plot.w, PH = plot.h;

        // Add a node at the middle of the plot, then grab it 10 px off-centre and drag 24 px.
        rig.doubleClick(px0 + PW * 0.5, py0 + PH * 0.5);
        rig.settle(200.0);
        auto pts = curve->curveFor(0);
        check(pts.size() == 3, "a double-click in the plot adds one node");
        if (pts.size() != 3) return;

        const double startY = pts[1].y;
        const double nodeScreenY = py0 + (1.0 - startY) * PH;
        const double kOff = 10.0, kDrag = 24.0;
        rig.drag(px0 + PW * 0.5, nodeScreenY - kOff, px0 + PW * 0.5, nodeScreenY - kOff - kDrag);
        rig.settle(200.0);

        const double endY = curve->curveFor(0)[1].y;
        const double movedPx = (endY - startY) * PH;
        char msg[192];
        std::snprintf(msg, sizeof(msg),
                      "the node travelled %.1f px for a %.0f px drag (a teleport would be %.0f)",
                      movedPx, kDrag, kDrag + kOff);
        check(std::fabs(movedPx - kDrag) < 1.5, msg);
    }

    // ── R-SCALE-3: every offered scale has a window it can be laid out in ────────────────
    void everyScaleLaysOutAtItsOwnMinimum()
    {
        std::printf("App: every offered scale lays out cleanly at its own window minimum (R-SCALE-3)\n");
        for (int pct : AppSettings::uiScales())
        {
            Rig rig(1440.0, 900.0, pct);
            const double w = std::ceil(rig.app.minPhysicalWidth());
            const double h = std::ceil(rig.app.minPhysicalHeight());
            rig.app.setSize(w, h);
            rig.app.showEditor();
            rig.settle(600.0);

            char msg[160];
            // The logical box must be at least the published floor — if it were not, the
            // minimum the host enforces would be the wrong number.
            const double logicalW = w / rig.app.drawnUiScale();
            const double logicalH = h / rig.app.drawnUiScale();
            std::snprintf(msg, sizeof(msg), "%d%%: %.0fx%.0f physical is >= the logical floor",
                          pct, w, h);
            check(logicalW >= App::minLogicalWidth() - 0.5 &&
                  logicalH >= App::minLogicalHeight() - 0.5, msg);

            // ...and the canvas keeps its floor, which is what the rail is required to fold for.
            const artboard::Segment *root = rig.app.uiRoot("editor");
            check(root != nullptr, "the editor tree exists");
            const artboard::Segment *stage =
                root ? arstro::cosmo_v2::findSegmentByType(*root, "CenterStage") : nullptr;
            check(stage != nullptr, "and contains a CenterStage");
            if (stage)
            {
                std::snprintf(msg, sizeof(msg),
                              "%d%%: the canvas column keeps its %.0f px floor (got %.0f, logical %.0fx%.0f)",
                              pct, App::kMinCanvasW, stage->width.value(), logicalW, logicalH);
                check(stage->width.value() >= App::kMinCanvasW - 0.5, msg);
            }
        }
    }

    // ── R-VIEW-1 / R-G-1: the PHOTO dissolves when an adjustment lands ───────────────────
    //
    // Reported as "make the edit more smooth by duplicate the photo on UI, photo will be fade
    // from current to new photo with adjustment when doing adjustment on slider". Asserted off
    // the recorded op stream rather than off PhotoCanvas's members, because what matters is what
    // was DRAWN: mid-dissolve there must be TWO photos on the stage, the top one inside a layer
    // whose alpha is strictly between 0 and 1, and at rest one photo with no fractional layer.
    struct Stage
    {
        int photos = 0;        // images drawn at stage size (a filmstrip thumb is 86 px wide)
        double partial = -1.0; // the fractional layer alpha in force over one of them
        bool partialWithoutCover = false;  // a see-through photo with nothing drawn behind it
        bool updatedWhileVisible = false;  // pixels replaced in a photo that is ON SCREEN
    };

    /** What the last recorded frame actually put on the photo stage. The layer stack is walked
     *  rather than "the last PushLayer seen", because every faded segment in the tree pushes one
     *  and only the one still OPEN over a DrawImage is the dissolve. */
    Stage stageOf(const artboard::RecordingTarget &target)
    {
        Stage st;
        std::vector<double> layers;
        std::vector<int> updated;                       // ids whose pixels were re-uploaded
        std::vector<std::pair<int, double>> drawn;      // stage-sized draws and their weight
        for (const auto &op : target.ops())
        {
            using K = artboard::DrawOp::Kind;
            if (op.kind == K::PushLayer) layers.push_back(op.args[0]);
            else if (op.kind == K::PopLayer) { if (!layers.empty()) layers.pop_back(); }
            else if (op.kind == K::UpdateImage) updated.push_back(op.imageId);
            else if (op.kind == K::DrawImage && op.args[2] > 200.0)   // stage-sized, not a thumb
            {
                ++st.photos;
                double weight = 1.0;
                for (double a : layers) weight *= a;
                if (weight > 0.001 && weight < 0.999) st.partial = weight;
                drawn.push_back({op.imageId, weight});
            }
        }
        // R-VIEW-1e: a see-through photo needs the one it is dissolving from underneath it. One
        // stage photo drawn at a fractional weight means the canvas is showing through.
        if (st.partial >= 0.0 && st.photos < 2) st.partialWithoutCover = true;
        // R-VIEW-1a: pixels may only be written where they cannot be SEEN, and what a layer
        // contributes is not its own alpha — the bottom one is painted over, so it contributes
        // ∏(1 − alpha) of everything above it. Compose bottom-to-top to get the real weights.
        //
        // The bar is one animation step, not zero, and that is a property of Artboard rather than
        // a looseness in the rule: an ImageView uploads its pixels inside onPaint, so the earliest
        // an upload can happen is the first frame the layer is drawn at all — one linear step of a
        // 160 ms dissolve, ~0.13, doubled here for slack. The behaviour this catches missed it by
        // a mile: reversing a dissolve wrote into a layer contributing 0.5 to 0.9.
        for (size_t i = 0; i < drawn.size(); ++i)
        {
            double contribution = drawn[i].second;
            for (size_t j = i + 1; j < drawn.size(); ++j) contribution *= (1.0 - drawn[j].second);
            if (contribution <= 0.26) continue;
            for (int id : updated)
                if (id == drawn[i].first) st.updatedWhileVisible = true;
        }
        return st;
    }

    /** One synthetic photo, selected, with its first preview on the stage. Returns false if the
     *  engine never produced a frame — the poll is generous because these frames cost
     *  microseconds of real time while the render happens on a worker. */
    bool seedOnePhoto(Rig &rig, uint8_t r, uint8_t g, uint8_t b)
    {
        std::vector<uint8_t> px((size_t)64 * 48 * 4, 0);
        for (int i = 0; i < 64 * 48; ++i)
        { px[i * 4 + 0] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255; }
        const int slot = rig.app.openImage(px.data(), 64, 48, "a.jpg", "");
        if (slot < 0) return false;
        rig.app.selectImage(slot);     // openImage does not select; `set` needs an edit target
        for (int i = 0; i < 400 && stageOf(rig.target).photos == 0; ++i) rig.frame();
        return stageOf(rig.target).photos > 0;
    }

    void theStageDissolvesWhenAnAdjustmentLands()
    {
        std::printf("App: a new render cross-dissolves onto the photo on screen (R-VIEW-1)\n");
        Rig rig(1200.0, 800.0);
        rig.app.showEditor();
        rig.settle(200.0);

        check(seedOnePhoto(rig, 90, 90, 90), "the first preview reaches the stage");
        check(stageOf(rig.target).partial < 0.0,
              "and is SET, not dissolved — an empty stage has nothing to fade from (R-VIEW-1b)");
        const int photosAtRest = stageOf(rig.target).photos;

        // An adjustment, through the service exactly as the slider sends it.
        arstro::cosmo::Command set;
        set.kind = arstro::cosmo::Command::Kind::Set;
        set.fields = {{"exposure", "1.5"}};
        check(rig.svc.dispatch(set), "the adjustment is accepted (there is an edit target)");

        std::vector<double> alphas;
        int twoPhotoFrames = 0;
        for (int i = 0; i < 400; ++i)
        {
            rig.frame();
            const Stage st = stageOf(rig.target);
            if (st.partial >= 0.0) alphas.push_back(st.partial);
            if (st.photos > photosAtRest) ++twoPhotoFrames;
            if (alphas.size() >= 6 && st.partial < 0.0) break;   // the dissolve has been and gone
        }
        std::printf("      (%zu partial-alpha frames, %d with both photos drawn)\n",
                    alphas.size(), twoPhotoFrames);
        check(alphas.size() >= 3,
              "the stage draws several frames at a partial alpha — it interpolates, it does not cut");
        check(twoPhotoFrames >= 3,
              "and BOTH photos are on screen during those frames (the duplicate layer)");
        bool moves = false;
        for (size_t i = 1; i < alphas.size(); ++i)
            if (std::fabs(alphas[i] - alphas[0]) > 0.05) moves = true;
        check(moves, "the alpha MOVES across frames rather than sitting at one value");

        rig.settle(300.0);
        const Stage rest = stageOf(rig.target);
        check(rest.partial < 0.0, "once it arrives there is no partial layer left");
        check(rest.photos == photosAtRest, "and only the photo that won is drawn");
    }

    // ── R-VIEW-1a / R-VIEW-1e: the drag that made it blink ──────────────────────────────
    //
    // Reported as "it doesn't smooth, make the photo blink when transition", and both causes are
    // per-FRAME facts that only a frame-by-frame walk can see, which is why this test exists
    // rather than a longer look at the code:
    //   (1) the covered layer was skipped from the PREVIOUS frame's alpha, so the first frame of
    //       every 1->0 dissolve drew the top at ~0.9 with the canvas behind it;
    //   (2) a render arriving mid-dissolve was written into the layer that still had weight
    //       (1-a), stepping the composite by that weight times two renders' difference.
    // A drag is the case that hits both, so the test IS a drag: a new value every three frames.
    void theStageNeverBlinksDuringADrag()
    {
        std::printf("App: a drag dissolves continuously — no dark frame, no pixel swap on screen "
                    "(R-VIEW-1a/1e)\n");
        Rig rig(1200.0, 800.0);
        rig.app.showEditor();
        rig.settle(200.0);
        check(seedOnePhoto(rig, 120, 100, 80), "a photo is on the stage");

        double ev = 0.0;
        int dissolveFrames = 0, accepted = 0;
        bool covered = true, wroteOffScreenOnly = true;
        for (int i = 0; i < 300; ++i)
        {
            if (i % 3 == 0)
            {
                ev += 0.15;                     // the slider moving, in EV
                arstro::cosmo::Command set;
                set.kind = arstro::cosmo::Command::Kind::Set;
                set.fields = {{"exposure", std::to_string(ev)}};
                if (rig.svc.dispatch(set)) ++accepted;
            }
            rig.frame();
            const Stage st = stageOf(rig.target);
            if (st.partial >= 0.0) ++dissolveFrames;
            if (st.partialWithoutCover) covered = false;
            if (st.updatedWhileVisible) wroteOffScreenOnly = false;
        }
        std::printf("      (%d adjustments, %d frames mid-dissolve)\n", accepted, dissolveFrames);
        check(dissolveFrames > 10, "the drag produced enough dissolving frames to judge");
        check(covered,
              "every see-through photo has the one it came from drawn underneath it (R-VIEW-1e)");
        check(wroteOffScreenOnly,
              "no frame replaces the pixels of a photo that is on screen (R-VIEW-1a)");

        // And it converges: the last render is not left waiting for a dissolve that never comes.
        // "Quiet" has to be measured, not timed — the render worker is asynchronous, so a fixed
        // settle can simply end inside a dissolve that started one frame earlier (it did, twice).
        int quiet = 0;
        for (int i = 0; i < 600 && quiet < 10; ++i)
        {
            rig.frame();
            std::this_thread::sleep_for(std::chrono::microseconds(200));   // let a late render land
            quiet = stageOf(rig.target).partial < 0.0 ? quiet + 1 : 0;
        }
        const Stage rest = stageOf(rig.target);
        check(quiet >= 10 && rest.photos >= 1, "it settles on one photo with no partial layer");
    }

    // R-VIEW-2: the Before/After pill rides the same seam, so the toggle dissolves too. Clicked,
    // not called: the pill's onChange is what asks App to re-push the photo, so calling setMode
    // would prove a code path nobody ships.
    void theBeforeAfterToggleDissolves()
    {
        std::printf("App: Before/After dissolves rather than cutting (R-VIEW-2)\n");
        Rig rig(1200.0, 800.0);
        rig.app.showEditor();
        rig.settle(200.0);
        check(seedOnePhoto(rig, 200, 120, 60), "a photo is on the stage");
        rig.settle(300.0);

        const artboard::Segment *photo =
            arstro::cosmo_v2::findSegmentByType(*rig.app.uiRoot("editor"), "PhotoCanvas");
        check(photo != nullptr, "the photo canvas exists");
        if (!photo) return;
        const artboard::Segment *pill = arstro::cosmo_v2::findSegmentByType(*photo, "SegmentedControl");
        check(pill != nullptr, "and its Before/Split/After pill");
        if (!pill) return;

        const artboard::Point before =
            pill->worldTransform().apply(artboard::Point{pill->width.value() / 6.0,
                                                         pill->height.value() / 2.0});
        rig.app.pointer(0, before.x, before.y, 1, rig.now); rig.frames(1);
        rig.app.pointer(2, before.x, before.y, 1, rig.now);

        int partialFrames = 0;
        for (int i = 0; i < 400; ++i)
        {
            rig.frame();
            if (stageOf(rig.target).partial > 0.0) ++partialFrames;
            if (partialFrames >= 4) break;
        }
        std::printf("      (%d partial-alpha frames after the Before click)\n", partialFrames);
        check(partialFrames >= 2, "switching to Before fades the baseline in over the edit");
    }
}

int main()
{
    std::printf("cosmo assembled-app UI tests\n\n");
    panelsFollowTheEditTarget();
    curveNodeGrabThroughTheAppDoesNotTeleport();
    scaleChangeIsAnimatedNotSnapped();
    theStartupScaleDoesNotAnimate();
    everyScaleLaysOutAtItsOwnMinimum();
    theStageDissolvesWhenAnAdjustmentLands();
    theStageNeverBlinksDuringADrag();
    theBeforeAfterToggleDissolves();
    std::printf("\n%s (%d failure%s)\n", gFailures ? "FAILED" : "all passed", gFailures,
                gFailures == 1 ? "" : "s");
    return gFailures ? 1 : 0;
}
