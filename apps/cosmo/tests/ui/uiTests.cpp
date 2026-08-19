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

        /** One 16 ms frame. Rendering IS advancing — App has no separate tick. */
        void frame()
        {
            now += 16.0;
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
}

int main()
{
    std::printf("cosmo assembled-app UI tests\n\n");
    curveNodeGrabThroughTheAppDoesNotTeleport();
    scaleChangeIsAnimatedNotSnapped();
    theStartupScaleDoesNotAnimate();
    everyScaleLaysOutAtItsOwnMinimum();
    std::printf("\n%s (%d failure%s)\n", gFailures ? "FAILED" : "all passed", gFailures,
                gFailures == 1 ? "" : "s");
    return gFailures ? 1 : 0;
}
