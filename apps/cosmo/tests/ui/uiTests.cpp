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
#include "../../widgets/CropOverlay.h"
#include "../../widgets/XformPanel.h"
#include "../../widgets/RightColumn.h"
#include "../../core/service/Command.h"
#include "../../core/decode/ImageDecoder.h"
#include <chrono>
#include <cstdio>
#include <fstream>
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
        void drag(double x0, double y0, double x1, double y1) { dragMod(x0, y0, x1, y1, false); }
        /** As `drag`, with the Alt modifier held for the WHOLE gesture — press, every move and
         *  the release — which is what GTK reports while the key is down. Separate from `drag`
         *  because the modifier has to be on the PRESS: CurvePanel decides there whether a
         *  node drag pulls tangent handles or moves the node. */
        void dragMod(double x0, double y0, double x1, double y1, bool alt)
        {
            app.pointer(0, x0, y0, 1, now, alt); frames(1);
            for (int i = 1; i <= 6; ++i)
            {
                const double t = (double)i / 6.0;
                app.pointer(1, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, 1, now, alt);
                frames(1);
            }
            app.pointer(2, x1, y1, 1, now, alt); frames(1);
        }
        void settle(double ms) { frames((int)(ms / 16.0) + 2); }

        /** Put a real (fake-decoded) photo in the editor.
         *
         *  Most assertions in this file are about geometry and motion, which need no pixels —
         *  but anything that edits needs a SELECTED SLOT, because `set` is rejected outright
         *  with "nothing selected to edit" when there is none. Three of the features tested
         *  below (the tone curve's round trip, the split seam, the crop box) are edits, so
         *  they were silently asserting against a service that had refused every command.
         *
         *  A fake decoder rather than a real file: the point is a slot with params, not pixels,
         *  and a repo that needs a RAW file on disk to run its UI tests does not run them. */
        bool loadFakePhoto(int images = 2)
        {
            struct FakeDecoder : arstro::cosmo::IImageDecoder
            {
                arstro::cosmo::DecodedImage decodeFile(const std::string &path) override
                {
                    arstro::cosmo::DecodedImage d;
                    d.width = 96; d.height = 64;                 // 3:2, so aspect maths is testable
                    d.rgba.assign((size_t)96 * 64 * 4, 150);
                    d.name = path;
                    return d;
                }
            };
            svc.setDecoderFactory([] {
                return std::unique_ptr<arstro::cosmo::IImageDecoder>(new FakeDecoder());
            });
            const std::string path = "/tmp/cosmo_ui_fake.cmp";
            {
                std::ofstream f(path, std::ios::trunc);
                f << "cosmoworkspace=1\n";
                for (int i = 0; i < images; ++i)
                    f << "#image\nparent=-1\npath=/fake/ui" << i << ".raf\n";
            }
            std::string err;
            if (!svc.dispatchText("project open " + path, err)) return false;
            for (int i = 0; i < 400 && svc.model().load.active; ++i)
            {
                frames(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            settle(300.0);
            if (svc.model().imageCount != images) return false;
            if (svc.model().nodes.empty()) return false;
            return svc.dispatchText("select " + std::to_string(svc.model().nodes.front().node), err);
        }
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

    // ── The reported regression: "alt+drag used to show a bezier curve, now it can't" ────
    //
    // `cosmo_widget_tests::curveAltDragMakesSmoothSpline` already drives CurvePanel's gesture
    // entry directly and passes, so the widget's own arithmetic is fine. What that test cannot
    // see is everything between the window and the widget, and everything after: the gesture
    // router, the Command the panel emits, the service's round trip, and — the new part —
    // R-SVC-12's re-bind, which re-seeds every panel from the model whenever the model's
    // revision moves. A frame landing mid-drag bumps that revision, so if the handles do not
    // survive the trip through EditParams they are wiped between one move and the next, which
    // is exactly what "it can't any more" looks like from the outside.
    void curveAltDragSurvivesTheRoundTripThroughTheModel()
    {
        std::printf("App: alt+drag pulls tangent handles, and the model keeps them\n");
        Rig rig(1440.0, 900.0);
        check(rig.loadFakePhoto(), "a photo is loaded and selected, so `set` is not refused");
        rig.app.showEditor();
        rig.settle(600.0);

        const artboard::Segment *root = rig.app.uiRoot("editor");
        const artboard::Segment *tabs =
            root ? arstro::cosmo_v2::findSegmentByType(*root, "EditStackTabs") : nullptr;
        check(tabs != nullptr, "the edit-stack tabs exist");
        if (!tabs) return;
        const artboard::Transform tw = tabs->worldTransform();
        const double tabW = tabs->width.value() / 5.0;
        rig.click(tw.e + tabW * 2.5, tw.f + 13.0);          // the Mixer/Curve tab
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

        rig.doubleClick(px0 + PW * 0.5, py0 + PH * 0.5);
        rig.settle(200.0);
        check(curve->curveFor(0).size() == 3, "a double-click adds an interior node");
        if (curve->curveFor(0).size() != 3) return;
        check(!curve->curveFor(0)[1].smooth, "which starts as a CORNER, handles ignored");

        // Alt+drag it. The node sits on the identity curve at the plot's centre.
        const double nodeY = py0 + (1.0 - curve->curveFor(0)[1].y) * PH;
        rig.dragMod(px0 + PW * 0.5, nodeY, px0 + PW * 0.5 + 26.0, nodeY - 18.0, /*alt=*/true);
        rig.settle(200.0);

        const arstro::CurvePoint &n = curve->curveFor(0)[1];
        check(n.smooth, "alt+drag makes the node SMOOTH");
        const bool pulled = (n.ox != 0.0f || n.oy != 0.0f) && (n.ix != 0.0f || n.iy != 0.0f);
        check(pulled, "and pulls tangent handles out of it");
        // Symmetric: the in-handle mirrors the out-handle, which is what makes it a spline
        // rather than two independent tangents (Alt on a HANDLE is what breaks symmetry).
        check(near(n.ix, -n.ox, 1e-4) && near(n.iy, -n.oy, 1e-4),
              "symmetrically, so the curve through the node is smooth");

        // ...and the model agrees. This is the half the widget test cannot reach: if the
        // Command dropped the handles, or EditParams could not carry them, the panel would be
        // re-seeded flat on the next frame and the bezier would vanish under the pointer.
        const arstro::EditParams *p = rig.svc.session().curParams();
        check(p != nullptr, "there is a params set to inspect");
        bool modelHasSmooth = false;
        if (p)
            for (const arstro::CurvePoint &cp : p->curve)
                if (cp.smooth && (cp.ox != 0.0f || cp.oy != 0.0f)) modelHasSmooth = true;
        check(modelHasSmooth, "and the SERVICE's params carry the smooth node with its handles");

        // Finally: a re-bind must not undo it. Force the revision to move the way an arriving
        // frame does, let the app re-read the model, and check the handles are still there.
        rig.settle(400.0);
        const arstro::CurvePoint &after = curve->curveFor(0)[1];
        check(after.smooth && (after.ox != 0.0f || after.oy != 0.0f),
              "and a model re-bind does not flatten it again");
    }

    // ── R-VIEW-3: the split seam is grabbable, slides, clamps, and re-centres eased ──────
    void splitSeamCanBeDraggedAndRecentres()
    {
        std::printf("App: the split seam slides, clamps, and re-centres with easing (R-VIEW-3)\n");
        Rig rig(1440.0, 900.0);
        check(rig.loadFakePhoto(), "a photo is loaded");
        rig.app.showEditor();
        rig.settle(600.0);

        const artboard::Segment *root = rig.app.uiRoot("editor");
        auto *canvas = const_cast<arstro::cosmo_v2::PhotoCanvas *>(
            static_cast<const arstro::cosmo_v2::PhotoCanvas *>(
                arstro::cosmo_v2::findSegmentByType(*root, "PhotoCanvas")));
        check(canvas != nullptr, "the photo canvas exists");
        if (!canvas) return;

        const artboard::Transform cwT = canvas->worldTransform();
        const double CW = canvas->width.value(), CH = canvas->height.value();
        const double midY = cwT.f + CH * 0.5;

        // Before Split mode is on, the seam must NOT claim a press — otherwise it would
        // swallow the pan, which is the one thing a hit test rather than a mode has to get
        // right.
        check(!canvas->onSeam(artboard::Point{CW * 0.5, CH * 0.5}),
              "with the seam hidden, a press at the middle is not a seam grab");

        // Turn Split on via the pill, the way a user does: it is the middle of three
        // segments, at the bottom-centre of the canvas.
        const artboard::Segment *pill =
            arstro::cosmo_v2::findSegmentByType(*canvas, "SegmentedControl");
        check(pill != nullptr, "the Before/Split/After pill exists");
        if (!pill) return;
        const artboard::Transform pw = pill->worldTransform();
        rig.click(pw.e + pill->width.value() * 0.5, pw.f + pill->height.value() * 0.5);
        rig.settle(400.0);
        check(canvas->onSeam(artboard::Point{CW * 0.5, CH * 0.5}),
              "in Split mode the middle IS a seam grab");
        const double startSeam = canvas->seamX();
        check(near(startSeam, CW * 0.5, 1.0), "and the seam starts centred");

        // Grab it 9 px off-centre and drag 200 px right. D-32's lesson: it must move BY the
        // drag, not jump to the pointer — a 9 px teleport is exactly the bug that cost a day
        // on the curve editor.
        const double kOff = 9.0, kDrag = 200.0;
        rig.drag(cwT.e + startSeam + kOff, midY, cwT.e + startSeam + kOff + kDrag, midY);
        rig.settle(200.0);
        const double moved = canvas->seamX() - startSeam;
        char msg[192];
        std::snprintf(msg, sizeof(msg),
                      "the seam travelled %.1f px for a %.0f px drag (a teleport would be %.0f)",
                      moved, kDrag, kDrag + kOff);
        check(std::fabs(moved - kDrag) < 2.0, msg);

        // The clipped "before" half must follow the seam, or the seam is a decoration.
        const artboard::Segment *clip = nullptr;
        for (const auto &ch : canvas->children())
            if (ch && ch->clipToBounds && ch->width.value() > 1.0 &&
                std::fabs(ch->width.value() - canvas->seamX()) < 2.0)
                clip = ch.get();
        check(clip != nullptr, "and the clipped before-half is as wide as the seam position");

        // Drag far past the right edge: it must clamp INSIDE the canvas, because a seam at
        // the very edge has nothing left to compare and nothing left to grab.
        rig.drag(cwT.e + canvas->seamX(), midY, cwT.e + CW + 400.0, midY);
        rig.settle(200.0);
        check(canvas->seamX() < CW, "dragged past the edge, the seam stays inside the canvas");
        check(canvas->seamX() > CW - 40.0, "but does go most of the way");
        // ...and the same at the left.
        rig.drag(cwT.e + canvas->seamX(), midY, cwT.e - 400.0, midY);
        rig.settle(200.0);
        check(canvas->seamX() > 0.0, "and the same at the left edge");
        check(canvas->seamX() < 40.0, "having travelled most of the way there");

        // Double-click re-centres — and EASES, because that is the app moving it rather than
        // the user. R-G-1: sample mid-tween and require the drawn value to differ from both
        // ends. A snap would pass an "it arrives" test, which is why this checks travel.
        const double before = canvas->seamX();
        rig.app.pointer(0, cwT.e + before, midY, 1, rig.now); rig.frames(1);
        rig.app.pointer(2, cwT.e + before, midY, 1, rig.now); rig.frames(1);
        rig.app.pointer(0, cwT.e + before, midY, 1, rig.now); rig.frames(1);
        rig.app.pointer(2, cwT.e + before, midY, 1, rig.now);
        int between = 0;
        for (int i = 0; i < 30; ++i)
        {
            rig.frame();
            const double x = canvas->seamX();
            if (x > before + 2.0 && x < CW * 0.5 - 2.0) ++between;
        }
        std::snprintf(msg, sizeof(msg), "the re-centre travelled (%d intermediate frames)", between);
        check(between >= 2, msg);
        rig.settle(400.0);
        check(near(canvas->seamX(), CW * 0.5, 1.5), "and it lands back in the middle");
    }

    // ── R-CROP-5/3: the crop box on the photo — move the region, and keep a locked shape ──
    //
    // This is the feature the user reported as missing outright: "when a ratio is locked user
    // can freely choose region". There was no crop box at all (PARITY #3), so the answer was
    // not a bug fix but the missing half of the feature — and the assertion that matters most
    // is the plain one: dragging inside the box MOVES it, with a ratio locked, without
    // changing its shape.
    void cropBoxMovesTheRegionAndKeepsALockedRatio()
    {
        std::printf("App: the crop box moves the region and keeps a locked ratio (R-CROP-3/5)\n");
        Rig rig(1440.0, 900.0);
        check(rig.loadFakePhoto(), "a photo is loaded");
        rig.app.showEditor();
        rig.settle(600.0);

        const artboard::Segment *root = rig.app.uiRoot("editor");
        auto *canvas = const_cast<arstro::cosmo_v2::PhotoCanvas *>(
            static_cast<const arstro::cosmo_v2::PhotoCanvas *>(
                arstro::cosmo_v2::findSegmentByType(*root, "PhotoCanvas")));
        check(canvas != nullptr, "the photo canvas exists");
        if (!canvas) return;
        auto overlay = canvas->cropOverlay();
        check(overlay != nullptr, "and it carries a crop overlay");
        if (!overlay) return;

        // Inactive until the Xform tab is open — an overlay that eats presses it has no use for
        // is how four panels once made the whole editor unclickable.
        check(!overlay->active(), "the crop box is inactive while another tab is showing");
        check(overlay->partAt(artboard::Point{100, 100}) == arstro::cosmo_v2::crop::Part::None,
              "so it hit-tests as nothing and the press falls through to the pan");

        // Open Xform — the last of five tabs.
        const artboard::Segment *tabs = arstro::cosmo_v2::findSegmentByType(*root, "EditStackTabs");
        check(tabs != nullptr, "the tabs exist");
        if (!tabs) return;
        const artboard::Transform tw = tabs->worldTransform();
        const double tabW = tabs->width.value() / 5.0;
        rig.click(tw.e + tabW * 4.5, tw.f + 13.0);
        rig.settle(400.0);
        check(overlay->active(), "opening Xform activates the crop box");

        // Lock 16:9 through the panel, so the lock arrives the way a user sets it.
        auto *xf = const_cast<arstro::cosmo_v2::XformPanel *>(
            static_cast<const arstro::cosmo_v2::XformPanel *>(
                arstro::cosmo_v2::findSegmentByType(*root, "XformPanel")));
        check(xf != nullptr, "the Xform panel is in the tree");
        if (!xf) return;
        const artboard::Transform xw = xf->worldTransform();
        const artboard::Rect chip = xf->aspectChipRect(3);   // 16:9
        rig.click(xw.e + chip.x + chip.w * 0.5, xw.f + chip.y + chip.h * 0.5);
        rig.settle(400.0);
        check(near(xf->lockedRatio(), 16.0 / 9.0, 1e-6), "16:9 is locked");

        const artboard::Rect before = overlay->cropRect();
        check(before.w < 1.0 || before.h < 1.0, "and the crop is no longer the whole photo");
        // The box really is 16:9 in PIXELS on this 96x64 (3:2) photo — the R-CROP-1 fix.
        const double pxRatio = (before.w * 96.0) / (before.h * 64.0);
        char msg[192];
        std::snprintf(msg, sizeof(msg), "the box is %.4f in pixels (16:9 = %.4f)", pxRatio, 16.0 / 9.0);
        check(near(pxRatio, 16.0 / 9.0, 1e-2), msg);

        // ── THE REPORTED GAP: drag INSIDE the box to move the region. ──
        const artboard::Transform cw = canvas->worldTransform();
        const artboard::Rect fitted = canvas->photoFittedRect();
        auto pxOf = [&](double nx, double ny) {
            return artboard::Point{cw.e + fitted.x + nx * fitted.w, cw.f + fitted.y + ny * fitted.h};
        };
        const artboard::Point centre = pxOf(before.x + before.w * 0.5, before.y + before.h * 0.5);
        check(overlay->partAt(artboard::Point{centre.x - cw.e, centre.y - cw.f}) ==
                  arstro::cosmo_v2::crop::Part::Move,
              "the interior of the box hit-tests as a MOVE");
        rig.drag(centre.x, centre.y, centre.x, centre.y - fitted.h * 0.15);
        rig.settle(200.0);
        const artboard::Rect moved = overlay->cropRect();
        check(moved.y < before.y - 1e-4, "dragging inside MOVES the region up");
        check(near(moved.w, before.w, 1e-4) && near(moved.h, before.h, 1e-4),
              "and a move never changes the shape, even with a ratio locked");
        check(near(moved.x, before.x, 1e-4), "nor drifts sideways on a vertical drag");

        // ...and the model got it, so the box edits through the same Command path a slider does.
        const arstro::EditParams *mp = rig.svc.session().curParams();
        check(mp != nullptr, "there are params to check");
        if (mp) check(near(mp->cropY, moved.y, 1e-3), "and the SERVICE has the moved crop");

        // ── Resizing a CORNER keeps the locked shape. ──
        const artboard::Rect pre = overlay->cropRect();
        // Aimed a few px INSIDE the corner, which is where a user presses: the grab band
        // reaches 13 px in, and a full-width crop's corner sits exactly on the photo's edge —
        // a press dead on it is one pixel outside the canvas and lands in the right column.
        const artboard::Point br = pxOf(pre.x + pre.w, pre.y + pre.h);
        const artboard::Point brIn{br.x - 4.0, br.y - 4.0};
        check(overlay->partAt(artboard::Point{brIn.x - cw.e, brIn.y - cw.f}) ==
                  arstro::cosmo_v2::crop::Part::BottomRight, "the bottom-right corner is a corner");
        rig.drag(brIn.x, brIn.y, brIn.x - fitted.w * 0.12, brIn.y);
        rig.settle(200.0);
        const artboard::Rect resized = overlay->cropRect();
        check(resized.w < pre.w - 1e-4, "dragging the corner resizes");
        const double r2 = (resized.w * 96.0) / (resized.h * 64.0);
        std::snprintf(msg, sizeof(msg), "and the ratio held at %.4f", r2);
        check(near(r2, 16.0 / 9.0, 2e-2), msg);

        // ── Free unlocks WITHOUT resetting — the other reported bug, end to end. ──
        const artboard::Rect kept = overlay->cropRect();
        const artboard::Rect freeChip = xf->aspectChipRect(arstro::cosmo_v2::XformPanel::kAspectFree);
        rig.click(xw.e + freeChip.x + freeChip.w * 0.5, xw.f + freeChip.y + freeChip.h * 0.5);
        rig.settle(400.0);
        check(near(xf->lockedRatio(), 0.0), "Free unlocks the ratio");
        const artboard::Rect afterFree = overlay->cropRect();
        check(near(afterFree.x, kept.x, 1e-4) && near(afterFree.y, kept.y, 1e-4) &&
                  near(afterFree.w, kept.w, 1e-4) && near(afterFree.h, kept.h, 1e-4),
              "and leaves the crop EXACTLY where it was (it used to reset to the whole photo)");

        // ...and now an edge drag changes one axis only, which is what free means.
        const artboard::Rect f0 = overlay->cropRect();
        const artboard::Point re0 = pxOf(f0.x + f0.w, f0.y + f0.h * 0.5);
        const artboard::Point rightEdge{re0.x - 4.0, re0.y};
        rig.drag(rightEdge.x, rightEdge.y, rightEdge.x - fitted.w * 0.1, rightEdge.y);
        rig.settle(200.0);
        const artboard::Rect f1 = overlay->cropRect();
        check(f1.w < f0.w - 1e-4, "free: the dragged edge moves");
        check(near(f1.h, f0.h, 1e-4), "and the other axis is left alone");
    }

    // ── D-51, through the real app: drag a locked corner OFF the photo ───────────────────
    //
    // The geometry is unit-tested, but the report was about the live window, and the path from
    // a pointer past the canvas edge to a normalised coordinate goes through the fitted rect
    // and the gesture router. So this drags well outside and asserts the shape survived.
    void cropLockedRatioHoldsWhenDraggedOffThePhoto()
    {
        std::printf("App: a locked ratio holds when the corner is dragged off the photo (D-51)\n");
        Rig rig(1440.0, 900.0);
        check(rig.loadFakePhoto(), "a photo is loaded");
        rig.app.showEditor();
        rig.settle(600.0);

        const artboard::Segment *root = rig.app.uiRoot("editor");
        auto *canvas = const_cast<arstro::cosmo_v2::PhotoCanvas *>(
            static_cast<const arstro::cosmo_v2::PhotoCanvas *>(
                arstro::cosmo_v2::findSegmentByType(*root, "PhotoCanvas")));
        const artboard::Segment *tabs = arstro::cosmo_v2::findSegmentByType(*root, "EditStackTabs");
        if (!canvas || !tabs) { check(false, "canvas and tabs exist"); return; }
        const artboard::Transform tw = tabs->worldTransform();
        rig.click(tw.e + tabs->width.value() / 5.0 * 4.5, tw.f + 13.0);   // Xform
        rig.settle(400.0);

        auto *xf = const_cast<arstro::cosmo_v2::XformPanel *>(
            static_cast<const arstro::cosmo_v2::XformPanel *>(
                arstro::cosmo_v2::findSegmentByType(*root, "XformPanel")));
        if (!xf) { check(false, "the Xform panel exists"); return; }
        const artboard::Transform xw = xf->worldTransform();

        // 1:1 — a square is the easiest shape to see break, and the reported symptom was one
        // axis running while the other stopped.
        const artboard::Rect chip = xf->aspectChipRect(1);
        rig.click(xw.e + chip.x + chip.w * 0.5, xw.f + chip.y + chip.h * 0.5);
        rig.settle(400.0);
        check(near(xf->lockedRatio(), 1.0, 1e-6), "1:1 is locked");

        auto overlay = canvas->cropOverlay();
        const artboard::Transform cw = canvas->worldTransform();
        const artboard::Rect fitted = canvas->photoFittedRect();
        auto pxOf = [&](double nx, double ny) {
            return artboard::Point{cw.e + fitted.x + nx * fitted.w, cw.f + fitted.y + ny * fitted.h};
        };
        // Shrink it first, so there is somewhere to grow FROM and the corner is not already on
        // the frame edge.
        {
            const artboard::Rect r = overlay->cropRect();
            const artboard::Point br = pxOf(r.x + r.w, r.y + r.h);
            rig.drag(br.x - 4.0, br.y - 4.0, pxOf(r.x + r.w * 0.5, r.y + r.h * 0.5).x,
                     pxOf(r.x + r.w * 0.5, r.y + r.h * 0.5).y);
            rig.settle(200.0);
        }
        const artboard::Rect before = overlay->cropRect();
        const double pxRatioOf = [&](const artboard::Rect &r) {
            return (r.w * 96.0) / (r.h * 64.0);
        }(before);
        char msg[224];
        std::snprintf(msg, sizeof(msg), "the box starts square in pixels: %.4f", pxRatioOf);
        check(near(pxRatioOf, 1.0, 2e-2), msg);

        // Now drag the bottom-right corner FAR past the bottom-right of the photo — hundreds of
        // px outside the canvas, which is what a user does when they want "as big as possible".
        const artboard::Point br = pxOf(before.x + before.w, before.y + before.h);
        rig.drag(br.x - 4.0, br.y - 4.0, br.x + 900.0, br.y + 700.0);
        rig.settle(300.0);

        const artboard::Rect after = overlay->cropRect();
        const double ratioAfter = (after.w * 96.0) / (after.h * 64.0);
        std::snprintf(msg, sizeof(msg),
                      "after dragging %.0f px off the photo the ratio is %.4f (was 1.0), box %.3f,%.3f %.3fx%.3f",
                      900.0, ratioAfter, after.x, after.y, after.w, after.h);
        check(near(ratioAfter, 1.0, 2e-2), msg);
        check(after.w > before.w + 1e-4, "and it did grow");
        check(after.x + after.w <= 1.0 + 1e-6 && after.y + after.h <= 1.0 + 1e-6,
              "and stayed inside the photo");
        check(near(after.x, before.x, 1e-4) && near(after.y, before.y, 1e-4),
              "and the un-dragged corner did not move");
    }

    // ── R-CROP-7 / R-G-1: entering and leaving the crop ZOOMS, it does not cut ───────────
    //
    // Reported as "choose another tab make it suddenly crop and when click back to xform it
    // suddenly expand". The assertion that matters is not "it arrives" — a snap arrives too,
    // sooner. It is that there exist frames where the RENDERED FRAMING is strictly between the
    // crop and the whole photo, which is R-G-1's own compliance clause.
    void leavingAndEnteringTheCropZoomsRatherThanCutting()
    {
        std::printf("App: entering/leaving the crop zooms, it does not cut (R-CROP-7, D-52)\n");
        Rig rig(1440.0, 900.0);
        check(rig.loadFakePhoto(), "a photo is loaded");
        rig.app.showEditor();
        rig.settle(600.0);

        const artboard::Segment *root = rig.app.uiRoot("editor");
        const artboard::Segment *tabs = arstro::cosmo_v2::findSegmentByType(*root, "EditStackTabs");
        auto *canvas = const_cast<arstro::cosmo_v2::PhotoCanvas *>(
            static_cast<const arstro::cosmo_v2::PhotoCanvas *>(
                arstro::cosmo_v2::findSegmentByType(*root, "PhotoCanvas")));
        if (!tabs || !canvas) { check(false, "the tabs and canvas exist"); return; }
        const artboard::Transform tw = tabs->worldTransform();
        const double tabW = tabs->width.value() / 5.0;
        auto clickTab = [&](int i) { rig.click(tw.e + tabW * (i + 0.5), tw.f + 13.0); };

        // Xform on, and a real crop set, so there is something to zoom BETWEEN. Without a crop
        // the framing never changes and the test would pass on a snap.
        clickTab(4);
        rig.settle(500.0);
        std::string err;
        check(rig.svc.dispatchText("set crop=0.25,0.25,0.5,0.5", err), "a crop is applied");
        rig.settle(400.0);
        const arstro::EditParams *p = rig.svc.session().curParams();
        check(p != nullptr && near(p->cropW, 0.5, 1e-4), "and the slot really has it");

        auto framingNow = [&] { return rig.svc.session().cropPreviewAmount(); };
        check(near(framingNow(), 1.0, 1e-3),
              "with Xform open the photo is rendered UNCROPPED (amount 1)");

        // ── LEAVE the tab: the framing must travel from 1 back to 0, not jump. ──
        clickTab(0);
        int between = 0, frames = 0;
        double last = framingNow();
        bool monotonic = true;
        for (int i = 0; i < 40; ++i)
        {
            rig.frame();
            ++frames;
            const double t = framingNow();
            if (t > 1e-3 && t < 1.0 - 1e-3) ++between;
            if (t > last + 1e-6) monotonic = false;    // it must only ever decrease
            last = t;
            if (t <= 1e-6) break;
        }
        char msg[192];
        std::snprintf(msg, sizeof(msg), "leaving Xform travelled through %d intermediate framings",
                      between);
        check(between >= 3, msg);
        check(monotonic, "and only ever in one direction");
        rig.settle(500.0);
        check(near(framingNow(), 0.0, 1e-3), "landing on the real crop");

        // ── COME BACK: the same in reverse. ──
        clickTab(4);
        between = 0;
        last = framingNow();
        monotonic = true;
        for (int i = 0; i < 40; ++i)
        {
            rig.frame();
            const double t = framingNow();
            if (t > 1e-3 && t < 1.0 - 1e-3) ++between;
            if (t < last - 1e-6) monotonic = false;
            last = t;
            if (t >= 1.0 - 1e-6) break;
        }
        std::snprintf(msg, sizeof(msg), "returning travelled through %d intermediate framings",
                      between);
        check(between >= 3, msg);
        check(monotonic, "and only ever in one direction");
        rig.settle(500.0);
        check(near(framingNow(), 1.0, 1e-3), "landing on the whole photo again");

        // ── The box converges onto the crop rather than sitting in the wrong place. ──
        // Mid-transition the box covers MORE of the photo than at rest, because the photo is
        // still zoomed in; at rest it is exactly the crop.
        auto overlay = canvas->cropOverlay();
        check(overlay->settled(), "at rest the overlay reports a settled framing");
        clickTab(0);
        rig.frames(3);
        check(!overlay->settled(), "and not while the zoom is running");
        check(overlay->partAt(artboard::Point{10.0, 10.0}) == arstro::cosmo_v2::crop::Part::None,
              "so it refuses gestures mid-flight, when the mapping is moving");
        rig.settle(600.0);
    }

    // ── R-HOME-1c: closing the app asks about unsaved work ───────────────────────────────
    void closingTheAppAsksAboutUnsavedWork()
    {
        std::printf("App: closing asks about unsaved changes, and a clean project just goes (R-HOME-1c)\n");
        Rig rig(1440.0, 900.0);
        check(rig.loadFakePhoto(), "a photo is loaded");
        rig.app.showEditor();
        rig.settle(400.0);

        int approvals = 0;
        rig.app.onQuitApproved = [&approvals] { ++approvals; };

        // A CLEAN project must go immediately — a prompt with nothing to lose is a prompt nobody
        // reads, and one that appears anyway teaches people to dismiss it.
        rig.svc.session().markClean();
        rig.app.requestQuit();
        check(approvals == 1, "a clean project closes with no question");

        // Now dirty it the way a user does, through a real edit.
        std::string err;
        check(rig.svc.dispatchText("set exposure=0.8", err), "an edit lands");
        rig.settle(300.0);
        check(rig.svc.session().isDirty(), "and the session is dirty");

        approvals = 0;
        rig.app.requestQuit();
        rig.settle(300.0);
        check(approvals == 0, "a dirty project does NOT close on its own");

        // The modal is up, and it is the one the wordmark route uses.
        const artboard::Segment *root = rig.app.uiRoot("editor");
        const artboard::Segment *dlg =
            root ? arstro::cosmo_v2::findSegmentByType(*root, "ConfirmDialog") : nullptr;
        check(dlg != nullptr, "the confirm dialog is in the tree");

        // Cancel keeps the app: nothing is approved and the session stays dirty. Answered the
        // way a user can now answer it — Escape.
        artboard::KeyEvent esc;
        esc.type = artboard::KeyEvent::Type::Down;
        esc.keyCode = 0x1B;
        check(rig.app.key(esc), "Escape is consumed by the modal");
        rig.settle(300.0);
        check(approvals == 0, "cancelling keeps the app open");
        check(rig.svc.session().isDirty(), "and does not quietly mark the work clean");

        // Discard: approved, and the dirty flag cleared so a second ask cannot appear.
        rig.app.requestQuit();
        rig.settle(200.0);
        check(rig.app.confirmDialog()->confirmDestructive(), "the dialog offers a Discard");
        rig.settle(300.0);
        check(approvals == 1, "discarding closes the app");
        check(!rig.svc.session().isDirty(), "and clears the dirty flag");
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

namespace
{
    // ── T3.1: an idle window stops asking to be repainted ────────────────────────────
    //
    // The host used to queue a draw unconditionally every 16 ms, so the whole window was
    // re-rendered in software Cairo sixty times a second forever, at rest, with nothing
    // moving. On a desktop that is invisible; on a small board it is the core the render
    // engine needs.
    //
    // The rule this must not break is R-G-1: nothing a user can see may change in one
    // frame. R-G-1 is about CHANGE, not about repainting — so the two things to prove are
    // that an app at rest eventually goes quiet, and that anything in flight keeps every
    // frame it would have had.
    void anIdleAppStopsAskingToBeRepainted()
    {
        std::printf("\n-- T3.1 an idle app goes quiet, a moving one does not --\n");
        Rig rig(1440.0, 900.0);

        // Fresh app: activity has just happened, so it must want to draw.
        check(rig.app.needsRedraw(rig.now), "a just-built app wants a frame");

        // Let it sit. Nothing has been clicked, no project is loading, nothing animates.
        rig.settle(3000.0);
        check(!rig.app.needsRedraw(rig.now),
              "and after three seconds of nothing at all, it stops asking");

        // A click must wake it immediately — this is the half that would show up as a dead
        // UI rather than as wasted CPU, so it matters more than the saving does.
        //
        // On the EDITOR screen, deliberately: a press on the home grid can land on a real
        // project card and start an open transition, and a transition in flight legitimately
        // keeps asking for frames — so testing the wake/settle cycle there would be testing
        // the developer's recents file. (That the recents file reached a unit test at all is
        // its own problem; cosmo_ui now runs with a sandboxed XDG_CONFIG_HOME.)
        rig.app.showEditor();
        rig.settle(3000.0);
        check(!rig.app.needsRedraw(rig.now), "quiet on the editor screen");
        rig.app.pointer(0, 700.0, 450.0, 1, rig.now);
        check(rig.app.needsRedraw(rig.now), "a press wakes it on the same frame");
        rig.app.pointer(2, 700.0, 450.0, 1, rig.now);
        rig.settle(3000.0);
        check(!rig.app.needsRedraw(rig.now), "then it settles again");

        // A wheel and a key are the other two input paths; both must wake it.
        rig.app.wheel(700.0, 450.0, -1.0, false);
        check(rig.app.needsRedraw(rig.now), "a wheel wakes it");
        rig.settle(3000.0);
        artboard::KeyEvent k;
        k.type = artboard::KeyEvent::Type::Down;
        k.keyCode = 0x25;                       // Left; the code does not matter here
        rig.app.key(k);
        check(rig.app.needsRedraw(rig.now), "a key wakes it");

        // AN ANIMATION IN FLIGHT KEEPS EVERY FRAME. A UI-scale change is the longest and
        // most structural tween in the app (R-SCALE-2a), so it is the strictest case: the
        // window must be requested on every single frame while the transform is moving, or
        // the tween would visibly step.
        rig.settle(3000.0);
        check(!rig.app.needsRedraw(rig.now), "quiet before the tween");
        rig.app.setUiScale(200);
        int askedEveryFrame = 0, framesWhileMoving = 0;
        for (int i = 0; i < 60; ++i)
        {
            const bool moving = rig.app.drawnUiScale() < 2.0 - 1e-4;
            if (!moving) break;
            ++framesWhileMoving;
            if (rig.app.needsRedraw(rig.now)) ++askedEveryFrame;
            rig.frame();
        }
        std::printf("      (%d frames while the scale tween ran, %d of them requested)\n",
                    framesWhileMoving, askedEveryFrame);
        check(framesWhileMoving > 4, "the scale change really did take several frames");
        check(askedEveryFrame == framesWhileMoving,
              "and every one of them was requested — R-G-1 is untouched by drawing on demand");
    }

    // ── R-INFO: right-click ▸ Image Information puts the file's metadata on screen ────────
    void rightClickShowsTheImageInformation()
    {
        std::printf("App: right-click offers Image Information, and it opens the metadata panel (R-INFO)\n");
        Rig rig(1440.0, 900.0);
        // A decoder that also answers `readMetadata` — the whole point of the panel is that the
        // rows come from the FILE through the seam, so a rig with pixels but no metadata would
        // be testing an empty dialog.
        struct MetaDecoder : arstro::cosmo::IImageDecoder
        {
            arstro::cosmo::DecodedImage decodeFile(const std::string &path) override
            {
                arstro::cosmo::DecodedImage d;
                d.width = 96; d.height = 64;
                d.rgba.assign((size_t)96 * 64 * 4, 150);
                d.name = path;
                return d;
            }
            arstro::cosmo::ImageMetadata readMetadata(const std::string &) override
            {
                arstro::cosmo::ImageMetadata m;
                // More rows than the card can show, deliberately: the list must scroll, and a
                // dialog sized to its content is a dialog that runs off a 720p screen.
                m.add("Camera make", "ARSTRO");
                m.add("Camera model", "Test Cam 1");
                m.add("Lens", "24-70mm f/2.8");
                m.add("ISO", "ISO 400");
                m.add("Shutter", "1/250 s");
                m.add("Aperture", "f/2.8");
                m.add("Focal length", "50 mm");
                m.add("Exposure bias", "-0.30 EV");
                m.add("Taken", "2026:08:27 10:30:00");
                m.add("Orientation", "Normal");
                m.add("Software", "cosmo test");
                m.add("Dimensions", "6000 x 4000");
                m.add("Resolution", "24.0 MP");
                m.add("Artist", "A Photographer");
                m.add("Copyright", "(c) 2026");
                m.add("As-shot WB", "R 2.104  G 1.000  B 1.548");
                return m;
            }
        };
        check(rig.loadFakePhoto(), "a photo is loaded");
        rig.svc.setDecoderFactory([] {
            return std::unique_ptr<arstro::cosmo::IImageDecoder>(new MetaDecoder());
        });
        rig.app.showEditor();
        rig.settle(400.0);

        // Right-click the middle of the stage, exactly as a user does. Button 2 is Right at
        // App::pointer's entry (the GTK host passes the same number).
        const artboard::Segment *root = rig.app.uiRoot("editor");
        auto *photo = arstro::cosmo_v2::findSegmentByType(*root, "PhotoCanvas");
        check(photo != nullptr, "the photo canvas is in the tree");
        if (!photo) return;
        const artboard::Transform pw = photo->worldTransform();
        const double cx = pw.e + photo->width.value() * 0.5;
        const double cy = pw.f + photo->height.value() * 0.5;
        rig.app.pointer(0, cx, cy, 2, rig.now);
        rig.app.pointer(2, cx, cy, 2, rig.now);
        rig.settle(300.0);

        auto *menu = arstro::cosmo_v2::findSegmentByType(*root, "ContextMenu");
        check(menu != nullptr, "and the right-click opened a menu");

        // Item 3 of the photo-menu order: Add Photo / Group / Ungroup / Image Information.
        // (No cell was clicked, so the per-cell items — bypass, rename, delete — are absent.)
        // kPadY = 4, kItemH = 24, both private to ContextMenu, so the row centre is derived
        // here the same way the widget lays it out.
        rig.click(cx + 20.0, cy + 4.0 + 3 * 24.0 + 12.0);
        rig.settle(300.0);

        auto info = rig.app.infoDialog();
        check(info->isOpen(), "clicking it opens the image-information panel");
        check(info->rowCount() >= 16, "with the rows the service produced");
        check(info->valueOf("ISO") == "ISO 400", "and the values the file carries");
        check(info->valueOf("Path").find(".raf") != std::string::npos,
              "including what the caller already knew");

        // R-G-1: the list eases. A wheel must not teleport the rows — asserted the only way
        // that can tell an eased list from a snapping one, by looking mid-tween.
        const double before = info->scrollOffset();
        rig.app.wheel(cx, cy, -60.0, false);
        rig.frames(2);
        const double mid = info->scrollOffset();
        rig.settle(400.0);
        const double after = info->scrollOffset();
        check(after > before, "the wheel scrolls the list");
        check(mid > before && mid < after, "and gets there over several frames, not in one");

        // Escape closes it, as it does every modal here.
        artboard::KeyEvent esc;
        esc.type = artboard::KeyEvent::Type::Down;
        esc.keyCode = 0x1B;
        check(rig.app.key(esc), "Escape is consumed by the modal");
        rig.settle(300.0);
        check(!info->isOpen(), "and closes it");
    }
}

int main()
{
    std::printf("cosmo assembled-app UI tests\n\n");
    anIdleAppStopsAskingToBeRepainted();
    panelsFollowTheEditTarget();
    curveNodeGrabThroughTheAppDoesNotTeleport();
    curveAltDragSurvivesTheRoundTripThroughTheModel();
    splitSeamCanBeDraggedAndRecentres();
    cropBoxMovesTheRegionAndKeepsALockedRatio();
    cropLockedRatioHoldsWhenDraggedOffThePhoto();
    leavingAndEnteringTheCropZoomsRatherThanCutting();
    closingTheAppAsksAboutUnsavedWork();
    rightClickShowsTheImageInformation();
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
