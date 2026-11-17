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
#include "../../widgets/MaskPanel.h"
#include "../../widgets/MaskOverlay.h"
#include "../../widgets/CurvePanel.h"
#include "../../widgets/EditStackTabs.h"
#include "../../widgets/PhotoCanvas.h"
#include "../../widgets/CropOverlay.h"
#include "../../widgets/XformPanel.h"
#include "../../widgets/RightColumn.h"
#include "../../widgets/AgentPanel.h"
#include "../../widgets/TopBar.h"
#include "../../widgets/PresetTree.h"
#include "../../widgets/ActionBar.h"
#include "../../widgets/Filmstrip.h"
#include "../../widgets/TextFit.h"
#include "../../EditControls.h"
#include "../../core/service/Command.h"
#include "../../core/decode/ImageDecoder.h"
#include "../../core/decode/Exif.h"
#include "../../ExportWriter.h"
#include <filesystem>
#include <gdk-pixbuf/gdk-pixbuf.h>
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

    /** Tab `i`'s centre, in world x, from the tabs themselves (EditStackTabs::tabCenterX). */
    double tabCenter(const artboard::Segment *tabs, int i)
    {
        return tabs->worldTransform().e + static_cast<const arstro::cosmo_v2::EditStackTabs *>(tabs)->tabCenterX(i);
    }
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
        /** `scene` paints sky over grass instead of flat grey. Flat grey is the right fixture
         *  for geometry — nothing in it distracts — but it is the wrong one for anything that
         *  has to FIND something, because a classifier handed a uniform field correctly finds
         *  nothing and the test then proves only that nothing happened. */
        bool loadFakePhoto(int images = 2, bool scene = false)
        {
            struct FakeDecoder : arstro::cosmo::IImageDecoder
            {
                bool scene = false;
                arstro::cosmo::DecodedImage decodeFile(const std::string &path) override
                {
                    arstro::cosmo::DecodedImage d;
                    d.width = 96; d.height = 64;                 // 3:2, so aspect maths is testable
                    d.rgba.assign((size_t)96 * 64 * 4, 150);
                    // A face-like blob on a green ground: a fixture with real regions in it,
                    // which several tests want and a flat grey cannot give.
                    if (scene)
                        for (int y = 0; y < 64; ++y)
                            for (int x = 0; x < 96; ++x)
                            {
                                const double dx = ((x + 0.5) / 96.0 - 0.5) / 0.24;
                                const double dy = ((y + 0.5) / 64.0 - 0.5) / 0.32;
                                const bool skin = dx * dx + dy * dy <= 1.0;
                                const size_t i = ((size_t)y * 96 + x) * 4;
                                d.rgba[i] = skin ? 226 : 82;
                                d.rgba[i + 1] = skin ? 178 : 118;
                                d.rgba[i + 2] = skin ? 148 : 84;
                            }
                    d.name = path;
                    return d;
                }
            };
            svc.setDecoderFactory([scene] {
                auto d = std::unique_ptr<FakeDecoder>(new FakeDecoder());
                d->scene = scene;
                return std::unique_ptr<arstro::cosmo::IImageDecoder>(std::move(d));
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

        // Click the Mixer/Curve tab (index 2) the way a user does, so the CurvePanel is
        // actually laid out and reachable rather than merely present.
        const artboard::Transform tw = tabs->worldTransform();
        rig.click(tabCenter(tabs, 2), tw.f + 13.0);
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
        rig.click(tabCenter(tabs, 2), tw.f + 13.0);          // the Mixer/Curve tab
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
        rig.click(tabCenter(tabs, 4), tw.f + 13.0);
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
        rig.click(tabCenter(tabs, 4), tw.f + 13.0);   // Xform
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
        auto clickTab = [&](int i) { rig.click(tabCenter(tabs, i), tw.f + 13.0); };

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
        check(rig.app.confirmDialog()->confirmDestructive(), "the dialog offers a Don't save");
        rig.settle(300.0);
        check(approvals == 1, "discarding closes the app");
        check(!rig.svc.session().isDirty(), "and clears the dirty flag");
    }

    // ── R-UX-1: one modal gate ────────────────────────────────────────────────────────────
    // Settings, the preset picker and History had no key handling, and the host guarded only the
    // confirm dialog - so Escape did nothing and Delete / Ctrl+Z reached the editor behind them.
    // For each modal: open it, press Delete-as-the-host-would (the host asks modalOpen() before
    // its accelerators - asserted directly), an arrow key (App::key's own shortcut), then Escape.
    artboard::KeyEvent keyDown(int code)
    {
        artboard::KeyEvent e;
        e.type = artboard::KeyEvent::Type::Down;
        e.keyCode = code;
        return e;
    }

    void aModalOwnsTheKeyboard()
    {
        std::printf("App: a modal owns the keyboard - Escape closes it, nothing reaches the editor (R-UX-1)\n");
        Rig rig(1440.0, 900.0);
        check(rig.loadFakePhoto(3), "photos are loaded");
        rig.app.showEditor();
        rig.settle(400.0);
        std::string err;
        check(rig.svc.dispatchText("set exposure=0.4", err), "an edit, so History has a tree to show");
        rig.settle(200.0);
        check(!rig.app.modalOpen(), "no modal is open to begin with");

        struct Case { const char *name; void (App::*open)(); };
        const Case cases[] = {{"Settings", &App::openSettingsDialog},
                              {"the preset picker", &App::presetSaveClicked},
                              {"History", &App::openHistoryView},
                              {"Export", &App::openExportDialog}};
        for (const Case &c : cases)
        {
            (rig.app.*c.open)();
            rig.settle(300.0);
            const std::string n = c.name;
            check(rig.app.modalOpen(), (n + " is open, and the gate knows it").c_str());
            const int nodeBefore = rig.svc.model().selectedNode;
            check(rig.app.key(keyDown(39)), ("behind " + n + ", the Right arrow is swallowed").c_str());
            rig.settle(100.0);
            check(rig.svc.model().selectedNode == nodeBefore, "and the selection did not walk");
            artboard::KeyEvent t;
            t.type = artboard::KeyEvent::Type::Text;
            t.text = "o";
            check(rig.app.key(t), "a typed `o` is swallowed (the host would otherwise open a file dialog)");
            check(rig.app.modalOpen(), "the modal is still open after those keys");
            check(rig.app.key(keyDown(0x1B)), "Escape is taken");
            rig.settle(400.0);
            check(!rig.app.modalOpen(), ("Escape closed " + n).c_str());
        }
        int rack = 0;
        for (const auto &nd : rig.svc.model().nodes) rack += nd.group ? 0 : 1;
        check(rack == 3, "no photo was removed along the way");

        // Enter is the primary action: Done for Settings.
        rig.app.openSettingsDialog();
        rig.settle(300.0);
        rig.app.key(keyDown(0x0D));
        rig.settle(400.0);
        check(!rig.app.modalOpen(), "Enter is Done on Settings");
    }

    // ── R-UX-3: Delete asks first ──────────────────────────────────────────────────────────
    void deleteAsksBeforeRemovingAPhoto()
    {
        std::printf("App: Delete asks before removing a photo - it cannot be undone (R-UX-3)\n");
        Rig rig(1440.0, 900.0);
        check(rig.loadFakePhoto(3), "three photos are loaded");
        rig.app.showEditor();
        rig.settle(400.0);
        // Wired as the host wires it, so the confirmed Delete is the `delete` command (R-SVC-2).
        rig.app.onCommand = [&rig](arstro::cosmo::Command c) { rig.svc.dispatch(c); };

        // The slot count never shrinks (slot id == index), so count what the user counts: the rack.
        auto photos = [&rig] {
            int n = 0;
            for (const auto &nd : rig.svc.model().nodes) n += nd.group ? 0 : 1;
            return n;
        };
        check(photos() == 3, "the rack holds three photos");
        rig.app.requestDeleteSelected();
        rig.settle(300.0);
        check(photos() == 3, "the Delete key alone removes nothing");
        check(rig.app.confirmDialog()->isOpen(), "it asks");
        rig.app.key(keyDown(0x0D));   // Enter: the destructive button is never the default
        rig.settle(300.0);
        check(photos() == 3 && !rig.app.modalOpen(), "Enter cancels - Remove is not bound to a key");

        rig.app.requestDeleteSelected();
        rig.settle(300.0);
        rig.app.key(keyDown(0x1B));
        rig.settle(300.0);
        check(photos() == 3, "Escape keeps the photo");

        rig.app.requestDeleteSelected();
        rig.settle(300.0);
        check(rig.app.confirmDialog()->confirmDestructive(), "the dialog offers a destructive Remove");
        rig.settle(300.0);
        check(photos() == 2, "Remove removes it");
        // And it really cannot be undone, which is why it asks.
        { std::string e2; rig.svc.dispatchText("undo", e2); }
        rig.settle(200.0);
        check(photos() == 2, "undo does not bring it back");
    }

    // ── R-UX-2: File > Close Project ───────────────────────────────────────────────────────
    void closingAProjectAsksFirstAndClearsTheChrome()
    {
        std::printf("App: Close Project asks about unsaved work, then leaves nothing of the project behind (R-UX-2)\n");
        Rig rig(1440.0, 900.0);
        check(rig.loadFakePhoto(2), "a project is loaded");
        rig.app.showEditor();
        rig.settle(600.0);
        rig.app.onCommand = [&rig](arstro::cosmo::Command c) { rig.svc.dispatch(c); };
        std::string err;
        check(rig.svc.dispatchText("set exposure=0.8", err), "an edit lands");
        rig.settle(300.0);
        check(rig.svc.session().isDirty(), "the project is dirty");

        rig.app.requestCloseProject();
        rig.settle(300.0);
        check(rig.app.confirmDialog()->isOpen(), "closing a dirty project asks first");
        check(rig.svc.model().imageCount == 2, "and nothing is gone while it asks");
        rig.app.key(keyDown(0x1B));
        rig.settle(300.0);
        check(rig.svc.model().imageCount == 2 && rig.svc.session().isDirty() && !rig.app.onHomeScreen(),
              "Cancel keeps the project, the edits and the editor");

        rig.app.requestCloseProject();
        rig.settle(300.0);
        check(rig.app.confirmDialog()->confirmDestructive(), "Don't save is offered");
        rig.frames(3);
        check(rig.svc.model().imageCount == 2, "the project is still there while the editor fades out (nothing empties in one frame)");
        rig.settle(3000.0);
        check(rig.app.onHomeScreen(), "it lands on Home");
        check(rig.svc.model().imageCount == 0, "the project is closed");
        check(rig.app.topBarTitle().empty(), "the top bar no longer carries the project's name");
        check(!rig.app.histogramShown(), "and the histogram is empty");
    }

    template <class T> T *widget(Rig &rig, const char *type)
    {
        const artboard::Segment *root = rig.app.uiRoot("editor");
        const artboard::Segment *s = root ? arstro::cosmo_v2::findSegmentByType(*root, type) : nullptr;
        return const_cast<T *>(static_cast<const T *>(s));
    }
    bool drew(const Rig &rig, const std::string &text)
    {
        for (const auto &op : rig.target.ops())
            if (op.kind == artboard::DrawOp::Kind::DrawText && op.text == text) return true;
        return false;
    }

    // ══ R-FAMILY-8…14: Cosmo adopts the family standard ═══════════════════════════════════
    int menuRow(const arstro::cosmo_v2::MenuStrip &ms, int menu, const std::string &prefix)
    {
        const auto &items = ms.menu(menu).items;
        for (int i = 0; i < (int)items.size(); ++i)
            if (!items[(size_t)i].separator && items[(size_t)i].label.rfind(prefix, 0) == 0) return i;
        return -1;
    }
    bool rowEnabled(const arstro::cosmo_v2::MenuStrip &ms, int menu, const std::string &prefix)
    {
        const int i = menuRow(ms, menu, prefix);
        return i >= 0 && ms.menu(menu).items[(size_t)i].enabled;
    }
    bool anyThreeDots(const Rig &rig)
    {
        for (const auto &op : rig.target.ops())
            if (op.kind == artboard::DrawOp::Kind::DrawText && op.text.find("...") != std::string::npos) return true;
        return false;
    }

    void theMenusAreTheFamilys()
    {
        std::printf("App: the menus are File · Edit · Develop · Preset · View, with shortcuts, separators and disabled rows (R-FAMILY-8)\n");
        using arstro::cosmo_v2::MenuStrip;
        using arstro::cosmo_v2::RightColumn;
        Rig rig(1440.0, 900.0);
        rig.app.showEditor();
        rig.settle(400.0);
        MenuStrip &ms = *rig.app.topBar()->menuStrip();
        const char *want[] = {"File", "Edit", "Develop", "Preset", "View"};
        bool titles = ms.menuCount() == 5;
        for (int i = 0; titles && i < 5; ++i) titles = ms.menu(i).title == want[i];
        check(titles, "the five menus, in the family's order");

        bool dots = false, padded = false, sep = false;
        for (int m = 0; m < ms.menuCount(); ++m)
            for (const auto &it : ms.menu(m).items)
            {
                dots |= it.label.find("...") != std::string::npos;
                padded |= it.label.find("(Ctrl") != std::string::npos || it.label.find("  ") != std::string::npos;
                sep |= it.separator;
            }
        check(!dots, "no label has three dots (an ellipsis is U+2026)");
        check(!padded, "no shortcut is padded into a label");
        check(sep, "groups are divided by separators");
        const int settings = menuRow(ms, 0, "Settings\xE2\x80\xA6");
        check(settings >= 0 && ms.menu(0).items[(size_t)settings].shortcut == "Ctrl+,", "Settings… is under File, Ctrl+,");
        check(menuRow(ms, 0, "Export\xE2\x80\xA6") >= 0, "File's deliverable is Export…");
        check(menuRow(ms, 1, "History\xE2\x80\xA6") >= 0 && menuRow(ms, 1, "Undo") == 0 && menuRow(ms, 1, "Redo") == 1,
              "Edit holds Undo, Redo and History…");
        check(ms.menu(1).items[0].shortcut == "Ctrl+Z" && ms.menu(1).items[1].shortcut == "Ctrl+Shift+Z" &&
                  ms.menu(0).items[(size_t)menuRow(ms, 0, "Save As")].shortcut == "Ctrl+Shift+S" &&
                  ms.menu(0).items[(size_t)menuRow(ms, 0, "Open")].shortcut == "Ctrl+O" &&
                  ms.menu(0).items[(size_t)menuRow(ms, 0, "New Project")].shortcut == "Ctrl+N",
              "the shortcuts are in the shortcut field");

        // Nothing loaded: what cannot act is disabled.
        check(!rowEnabled(ms, 0, "Export") && !rowEnabled(ms, 0, "Save") && !rowEnabled(ms, 1, "Undo") &&
                  !rowEnabled(ms, 1, "Redo") && !rowEnabled(ms, 1, "History") && !rowEnabled(ms, 2, "Paste to Selected") &&
                  !rowEnabled(ms, 2, "Ungroup") && !rowEnabled(ms, 3, "Save Preset"),
              "with no photo: Export, Save, Undo, Redo, History, Paste, Ungroup and Save Preset are disabled");
        check(rowEnabled(ms, 0, "Settings") && rowEnabled(ms, 0, "Open"), "Settings… and Open… are live");

        check(rig.loadFakePhoto(3), "photos arrive");
        rig.app.showEditor();
        rig.settle(500.0);
        check(rowEnabled(ms, 0, "Export") && rowEnabled(ms, 0, "Save") && rowEnabled(ms, 3, "Save Preset"),
              "with photos: Export…, Save and Save Preset… are live");
        check(!rowEnabled(ms, 1, "Undo") && !rowEnabled(ms, 2, "Paste to Selected"), "nothing to undo or paste yet");
        ms.menu(2).items[(size_t)menuRow(ms, 2, "Copy Settings")].action();
        rig.frames(2);
        check(rowEnabled(ms, 2, "Paste to Selected") && rowEnabled(ms, 2, "Paste to All"), "after Copy, both Pastes are live");
        std::string err;
        check(rig.svc.dispatchText("set exposure=0.6", err), "an edit lands");
        rig.settle(200.0);
        check(rowEnabled(ms, 1, "Undo") && rowEnabled(ms, 1, "History"), "after an edit, Undo and History… are live");
        const std::string undo = ms.menu(1).items[0].label;
        check(undo.size() > 5 && undo.rfind("Undo ", 0) == 0, "Undo names what it would undo");
        std::printf("      (the row reads \"%s\")\n", undo.c_str());

        // View > Show Agent selects the Agent tab; the row then reads Hide Agent and goes back.
        const int before = rig.app.agentShown() ? -1 : 0;
        check(before == 0 && menuRow(ms, 4, "Show Agent") >= 0, "View offers Show Agent");
        ms.menu(4).items[(size_t)menuRow(ms, 4, "Show Agent")].action();
        rig.settle(400.0);
        check(rig.app.agentShown(), "it selects the Agent tab of the right column");
        check(menuRow(ms, 4, "Hide Agent") >= 0, "and the row now reads Hide Agent");
        ms.menu(4).items[(size_t)menuRow(ms, 4, "Hide Agent")].action();
        rig.settle(400.0);
        check(!rig.app.agentShown() && menuRow(ms, 4, "Show Agent") >= 0, "Hide Agent returns to the tab that was open");

        // ── the keyboard, with a menu open ──
        const artboard::Transform mt = ms.worldTransform();
        auto mid = [&mt](const artboard::Rect &r) { return artboard::Point{mt.e + r.x + r.w * 0.5, mt.f + r.y + r.h * 0.5}; };
        artboard::Point p = mid(ms.titleRect(0));
        rig.click(p.x, p.y);
        rig.settle(300.0);
        check(ms.openIndex() == 0, "clicking File opens it");
        check(!anyThreeDots(rig), "nothing drawn with a menu open has three dots");
        bool sawShortcut = false;
        for (const auto &op : rig.target.ops())
            if (op.kind == artboard::DrawOp::Kind::DrawText && op.text == "Ctrl+Shift+S" && op.fontFamily == arstro::cosmo_v2::font::mono())
                sawShortcut = true;
        check(sawShortcut, "the shortcut column is drawn, in the mono face");
        const int rev = (int)rig.svc.model().selectedNode;
        check(rig.app.key(keyDown(40)), "Down is taken by the open menu");
        rig.frames(2);
        const int cur = ms.cursorItem();
        check(cur == 0, "it stands on the first row");
        const double wash = ms.itemHoverAmount(cur);
        check(wash > 0.0 && wash < 1.0, "the row's wash is eased (live value mid-tween)");
        check(rig.app.key(keyDown(39)), "Right is taken too");
        rig.settle(300.0);
        check(ms.openIndex() == 1 && rig.svc.model().selectedNode == rev, "it opens Edit - and the rack did not walk");
        int saves = 0;
        rig.app.onSaveWorkspaceRequested = [&saves] { ++saves; };
        check(rig.app.shortcut('s', true, true) && saves == 0, "Ctrl+Shift+S behind an open menu is swallowed");
        check(ms.openIndex() == 1, "and the menu is still open");
        check(rig.app.key(keyDown(0x1B)), "Escape is taken");
        rig.settle(300.0);
        check(ms.openIndex() < 0, "Escape closes the menu");

        // Enter runs the row: File > Settings…, reached with the keyboard.
        rig.click(p.x, p.y);
        rig.settle(300.0);
        rig.app.key(keyDown(35));   // End -> Close Project
        rig.app.key(keyDown(38));   // Up -> Home (R-FAMILY-8a: Home is in the last group, as in the siblings)
        check(ms.cursorItem() == menuRow(ms, 0, "Home"), "End then Up stands on Home, in the menu's last group");
        rig.app.key(keyDown(38));   // Up again, over the separator -> Settings…
        check(ms.cursorItem() == menuRow(ms, 0, "Settings"), "Up again skips the separator and stands on Settings…");
        rig.app.key(keyDown(0x0D));
        rig.settle(400.0);
        check(ms.openIndex() < 0 && rig.app.modalOpen(), "Enter runs it: the Settings modal opens and the menu closes");
        rig.app.key(keyDown(0x1B));
        rig.settle(400.0);
    }

    void theTopBarSaysWhatIsUnsaved()
    {
        std::printf("App: the project name sits left with the unsaved dot, the save icon lights, the title has a star (R-FAMILY-9, D-63)\n");
        using arstro::cosmo_v2::TopBar;
        {
            Rig home(1440.0, 900.0);
            home.settle(100.0);
            check(home.app.windowTitle() == "Cosmo", "on Home the window is titled Cosmo");
        }
        Rig rig(1440.0, 900.0);
        check(rig.loadFakePhoto(3), "a project is loaded");
        rig.app.showEditor();
        rig.settle(500.0);
        TopBar *bar = rig.app.topBar();
        const double menusRight = bar->menuStrip()->x.value() + bar->menuStrip()->width.value();
        artboard::Rect tr = bar->titleRect();
        check(bar->titleDrawn() == "cosmo_ui_fake", "the project's name is drawn");
        check(near(tr.x, menusRight + 9.75, 0.01), "LEFT, directly after the menus (not centred)");
        check(tr.x + tr.w < 1440.0 * 0.5, "well left of the bar's centre");

        // The wordmark: the accent dot at the MEASURED end of the name.
        double nameX = -1, dotX = -1;
        for (const auto &op : rig.target.ops())
            if (op.kind == artboard::DrawOp::Kind::DrawText && op.args[2] == 13.0 && op.args[1] < 29.25)
            {
                if (op.text == "cosmo") nameX = op.args[0];
                if (op.text == ".") dotX = op.args[0];
            }
        check(nameX >= 0 && near(dotX, nameX + rig.target.measureText("cosmo", 13.0, arstro::cosmo_v2::font::sansSemiBold(), -0.39), 0.01),
              "the wordmark's dot touches the name (Wordmark::draw)");

        check(!rig.svc.model().dirty && bar->dirtyAmount() < 0.001, "a project just opened is clean: no dot");
        check(rig.app.windowTitle() == "cosmo_ui_fake \xE2\x80\x94 Cosmo", "the title is <name> — Cosmo");
        // D-63: looking is not editing.
        std::string err;
        check(rig.svc.dispatchText("select next", err), "another photo is selected");
        rig.settle(300.0);
        check(!rig.svc.model().dirty && bar->dirtyAmount() < 0.001, "selecting a photo lights nothing");
        const artboard::Transform bt = bar->worldTransform();
        const artboard::Rect sr = bar->saveRect();
        check(sr.x + sr.w < bar->exportRect().x && sr.x > tr.x + tr.w, "the save icon sits at the right, left of Export");
        const auto mtimeOf = [] { std::error_code ec; return std::filesystem::last_write_time("/tmp/cosmo_ui_fake.cmp", ec); };
        const auto t0 = mtimeOf();
        rig.click(bt.e + sr.x + sr.w * 0.5, bt.f + sr.y + sr.h * 0.5);
        rig.settle(100.0);
        check(mtimeOf() == t0 && !rig.app.toast()->isShowing(), "clean, the save icon is dim and does nothing");

        check(rig.svc.dispatchText("set exposure=0.7", err), "an edit lands");
        rig.frames(3);
        const double mid = bar->dirtyAmount();
        check(mid > 0.0 && mid < 1.0, "the dot fades in (live value mid-tween), it does not pop");
        rig.settle(400.0);
        check(bar->dirtyAmount() > 0.999, "and settles lit");
        tr = bar->titleRect();
        const artboard::Point dc = bar->dirtyDotCentre();
        check(near(dc.x, tr.x + tr.w + 8.0, 0.01) && near(dc.y, 29.25 * 0.5, 0.01), "the dot is 8 px after the name, on the bar's centre line");
        check(rig.app.windowTitle() == "*cosmo_ui_fake \xE2\x80\x94 Cosmo", "the title gains its star");

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        rig.click(bt.e + sr.x + sr.w * 0.5, bt.f + sr.y + sr.h * 0.5);
        rig.frames(2);
        check(!rig.svc.model().dirty, "clicking the lit save icon saves");
        check(mtimeOf() != t0, "the project file was written");
        const double out = bar->dirtyAmount();
        check(out > 0.0 && out < 1.0, "the dot fades out (live value mid-tween)");
        rig.settle(400.0);
        check(bar->dirtyAmount() < 0.001 && rig.app.windowTitle() == "cosmo_ui_fake \xE2\x80\x94 Cosmo", "clean again: no dot, no star");
        check(rig.app.toast()->text() == "Saved" && rig.app.toast()->kind() == arstro::cosmo_v2::Toast::Kind::Info,
              "and the toast says Saved");

        // A long name is ellipsized and still leaves room for its dot before the filename.
        const std::string longName = "Iceland Ring Road, September 2026 - selects for the print portfolio, second pass (final), with the aurora set";
        bar->setProjectName(longName + " / " + longName + " / " + longName);
        rig.svc.dispatchText("set exposure=0.2", err);
        rig.settle(400.0);
        tr = bar->titleRect();
        double fileX = 1e9;
        for (const auto &op : rig.target.ops())
            if (op.kind == artboard::DrawOp::Kind::DrawText && op.text.find(".raf") != std::string::npos &&
                op.fontFamily == arstro::cosmo_v2::font::mono() && op.args[1] < 29.25)
                fileX = std::min(fileX, op.args[0]);
        const std::string drawn = bar->titleDrawn();
        check(drawn.size() >= 3 && drawn.compare(drawn.size() - 3, 3, "\xE2\x80\xA6") == 0, "a long name is ellipsized");
        check(fileX < 1e9 && bar->dirtyDotCentre().x + 3.0 <= fileX - 9.0, "and its dot still ends before the filename readout");
    }

    void theToastSaysWhatHappened()
    {
        std::printf("App: one toast, top-centre under the top bar - Saved, an export's result, a refused command (R-FAMILY-11)\n");
        using arstro::cosmo_v2::Toast;
        Rig rig(1440.0, 900.0);
        check(rig.loadFakePhoto(2), "a project is loaded");
        rig.app.showEditor();
        rig.settle(500.0);
        Toast *toast = rig.app.toast();
        check(!toast->isShowing(), "nothing is said at rest");
        if (toast->isShowing()) std::printf("      (it says \"%s\")\n", toast->text().c_str());
        std::string err;
        check(!rig.svc.dispatchText("history jump 99", err), "the service refuses a command");
        rig.frames(3);
        check(toast->kind() == Toast::Kind::Error && toast->text() == rig.svc.model().lastError && !toast->text().empty(),
              "the toast is an error carrying the service's own sentence");
        std::printf("      (it reads \"%s\")\n", toast->text().c_str());
        const double in = toast->amount();
        check(in > 0.0 && in < 1.0, "it fades in while sliding (live value mid-tween)");
        const artboard::Rect midRect = toast->rect();
        rig.settle(400.0);
        check(toast->amount() > 0.999, "and comes to rest");
        const artboard::Rect r = toast->rect();
        check(near(toast->y.value(), 29.25 + 4.0, 0.01) && near(r.y, 0.0, 0.01), "its top rests 4 px under the top bar");
        check(midRect.y < r.y, "mid-tween it was still above its resting place");
        check(near(r.x + r.w * 0.5, 1440.0 * 0.5, 1.0), "it is centred in the window");
        bool drawn = false;
        for (const auto &op : rig.target.ops())
            if (op.kind == artboard::DrawOp::Kind::DrawText && op.text == toast->text()) drawn = true;
        check(drawn, "it is painted over the editor");

        rig.app.exportFinished(3, 0, "");
        rig.frames(4);
        check(toast->kind() == Toast::Kind::Info && toast->text() == "Exported 3 photos", "an export says how many photos were written");
        check(toast->swapAmount() > 0.0 && toast->swapAmount() < 1.0, "a second toast cross-fades with the first (mid-tween)");
        rig.settle(400.0);
        rig.app.exportFinished(1, 0, "");
        rig.settle(400.0);
        check(toast->text() == "Exported 1 photo", "one photo is singular");
        rig.app.exportFinished(1, 2, "the disk is full");
        rig.settle(400.0);
        check(toast->kind() == Toast::Kind::Error && toast->text() == "2 of 3 photos could not be exported: the disk is full",
              "a failed export is an error with the count and the reason");
        check(rig.app.needsRedraw(rig.now), "the app keeps painting while the toast holds");
        rig.settle(3200.0 - 400.0 + 120.0);
        const double leaving = toast->amount();
        check(leaving > 0.0 && leaving < 1.0, "after 3200 ms it fades out (mid-tween)");
        rig.settle(600.0);
        check(!toast->isShowing(), "and is gone");

        // On Home too: the same toast, the same place.
        Rig home(1024.0, 640.0);
        home.settle(100.0);
        check(!home.svc.dispatchText("project save", err), "saving with no project is refused");
        home.settle(400.0);
        check(home.app.toast()->amount() > 0.999 && home.app.toast()->kind() == Toast::Kind::Error, "Home shows the refusal");
        check(near(home.app.toast()->rect().x + home.app.toast()->rect().w * 0.5, 512.0, 1.0), "centred in the smaller window");
    }

    void theUnsavedDialogIsTheFamilys()
    {
        std::printf("App: Save changes to \"<name>\"? - Cancel · Don't save · Save, the destructive button outlined (R-FAMILY-12)\n");
        Rig rig(1440.0, 900.0);
        check(rig.loadFakePhoto(2), "a project is loaded");
        rig.app.showEditor();
        rig.settle(500.0);
        std::string err;
        rig.svc.dispatchText("set exposure=0.5", err);
        rig.settle(200.0);
        int quits = 0, news = 0;
        rig.app.onQuitApproved = [&quits] { ++quits; };
        rig.app.onNewProjectRequested = [&news] { ++news; };
        rig.app.requestQuit();
        rig.settle(300.0);
        auto dlg = rig.app.confirmDialog();
        check(dlg->isOpen() && dlg->buttonCount() == 3, "a dirty project asks, with three buttons");
        check(dlg->buttonLabel(0) == "Cancel" && dlg->buttonLabel(1) == "Don't save" && dlg->buttonLabel(2) == "Save",
              "Cancel · Don't save · Save");
        check(dlg->drawsOutlined(1) && !dlg->drawsOutlined(2), "Don't save is outlined beside the filled Save");
        check(drew(rig, "Save changes to \"cosmo_ui_fake\"?"), "the title names the project");
        check(drew(rig, "Your changes will be lost if you don't save them."), "the body says what is lost");
        const artboard::Rect b0 = dlg->buttonRect(0), b1 = dlg->buttonRect(1), b2 = dlg->buttonRect(2);
        check(b0.x + b0.w <= b1.x && b1.x + b1.w <= b2.x, "left to right in that order");
        rig.app.key(keyDown(0x1B));
        rig.settle(300.0);
        check(quits == 0 && rig.svc.session().isDirty(), "Escape cancels");

        // Close Project and the wordmark ask the same question.
        rig.app.requestCloseProject();
        rig.settle(300.0);
        check(dlg->isOpen() && dlg->buttonLabel(1) == "Don't save" && drew(rig, "Save changes to \"cosmo_ui_fake\"?"),
              "Close Project asks the same question");
        rig.app.key(keyDown(0x1B));
        rig.settle(300.0);

        // New Project asks first; Don't save goes on to the host's dialog and forgets nothing.
        rig.app.requestNewProject();
        rig.settle(300.0);
        check(dlg->isOpen() && news == 0, "New Project asks before the host's dialog");
        check(dlg->confirmDestructive(), "Don't save is offered");
        rig.settle(300.0);
        check(news == 1 && rig.svc.session().isDirty(), "it goes on - and the edits are still unsaved if that dialog is cancelled");

        // A long name is shortened so the title fits the 400 px card.
        rig.app.topBar()->setProjectName("Iceland Ring Road, September 2026 - selects for the print portfolio");
        rig.app.requestQuit();
        rig.settle(300.0);
        std::string title;
        for (const auto &op : rig.target.ops())
            if (op.kind == artboard::DrawOp::Kind::DrawText && op.text.rfind("Save changes to \"Iceland", 0) == 0) title = op.text;
        check(!title.empty() && title.find("\xE2\x80\xA6\"?") != std::string::npos, "a long name ends in an ellipsis inside the quotes");
        check(rig.target.measureText(title, 14.0, arstro::cosmo_v2::font::sansSemiBold()) <= 360.0 + 0.5, "and the title fits the card");
        rig.app.key(keyDown(0x1B));
        rig.settle(300.0);

        // The remove-photo confirm keeps its single FILLED Remove.
        rig.app.requestDeleteSelected();
        rig.settle(300.0);
        check(dlg->isOpen() && dlg->buttonCount() == 2 && dlg->buttonLabel(1) == "Remove" && !dlg->drawsOutlined(1),
              "Remove, the only action, stays filled");
        rig.app.key(keyDown(0x1B));
        rig.settle(300.0);
    }

    void theKeysAreTheFamilys()
    {
        std::printf("App: Ctrl+O / Ctrl+N / Ctrl+S / Ctrl+, - and no plain letter opens anything (R-FAMILY-13)\n");
        using S = App::Shortcut;
        check(App::shortcutFor('o', false, false) == S::None && App::shortcutFor('s', false, false) == S::None,
              "plain o and plain s mean nothing");
        check(App::shortcutFor('o', true, false) == S::Open && App::shortcutFor('n', true, false) == S::NewProject &&
                  App::shortcutFor('s', true, false) == S::Save && App::shortcutFor('s', true, true) == S::SaveAs &&
                  App::shortcutFor(',', true, false) == S::Settings,
              "Ctrl+O, Ctrl+N, Ctrl+S, Ctrl+Shift+S, Ctrl+,");
        check(App::shortcutFor('z', true, false) == S::Undo && App::shortcutFor('z', true, true) == S::Redo &&
                  App::shortcutFor('y', true, false) == S::Redo,
              "Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y");
        check(App::shortcutFor(App::kKeyDelete, false, false) == S::Delete && App::shortcutFor(App::kKeyBackspace, false, false) == S::Delete,
              "Delete and Backspace are the same key");

        Rig rig(1440.0, 900.0);
        check(rig.loadFakePhoto(3), "photos are loaded");
        rig.app.showEditor();
        rig.settle(500.0);
        int opens = 0, saveAs = 0, news = 0, quick = 0;
        rig.app.onOpenRequested = [&opens] { ++opens; };
        rig.app.onSaveWorkspaceRequested = [&saveAs] { ++saveAs; };
        rig.app.onNewProjectRequested = [&news] { ++news; };
        rig.app.onExportRequested = [&quick] { ++quick; };
        check(!rig.app.shortcut('o', false, false) && !rig.app.shortcut('s', false, false) && opens == 0 && quick == 0,
              "typing o or s opens nothing");
        check(rig.app.shortcut('o', true, false) && opens == 1, "Ctrl+O opens");
        check(rig.app.shortcut('n', true, false) && news == 1, "Ctrl+N starts a new project (the project is clean: no question)");
        check(rig.app.shortcut('s', true, true) && saveAs == 1, "Ctrl+Shift+S is Save As");
        std::string err;
        rig.svc.dispatchText("set exposure=0.3", err);
        rig.settle(200.0);
        check(rig.svc.model().dirty && rig.app.shortcut('s', true, false), "Ctrl+S is taken");
        rig.frames(2);
        check(!rig.svc.model().dirty, "and saves");

        auto photos = [&rig] { int n = 0; for (const auto &nd : rig.svc.model().nodes) n += nd.group ? 0 : 1; return n; };
        check(rig.app.shortcut(App::kKeyBackspace, false, false), "Backspace is taken");
        rig.settle(300.0);
        check(rig.app.confirmDialog()->isOpen() && rig.app.confirmDialog()->buttonLabel(1) == "Remove" && photos() == 3,
              "it asks the Delete key's question, and removes nothing yet");
        // Behind the modal, every shortcut is swallowed.
        check(rig.app.shortcut('o', true, false) && opens == 1, "Ctrl+O behind a modal is swallowed");
        check(rig.app.shortcut(',', true, false), "Ctrl+, behind a modal is swallowed");
        rig.app.key(keyDown(0x1B));
        rig.settle(300.0);
        check(!rig.app.modalOpen(), "(the confirm is closed, and Settings did not open behind it)");
        check(rig.app.shortcut(App::kKeyDelete, false, false), "Delete is taken");
        rig.settle(300.0);
        check(rig.app.confirmDialog()->isOpen(), "and asks the same question");
        rig.app.key(keyDown(0x1B));
        rig.settle(300.0);

        check(rig.app.shortcut(',', true, false), "Ctrl+, is taken");
        rig.settle(300.0);
        check(rig.app.modalOpen(), "and opens Settings");
        rig.app.key(keyDown(0x1B));
        rig.settle(400.0);

        // Home: only the keys that make sense with no project.
        Rig home(1440.0, 900.0);
        home.settle(100.0);
        int projOpens = 0, projNews = 0;
        home.app.onOpenProjectRequested = [&projOpens] { ++projOpens; };
        home.app.onNewProjectRequested = [&projNews] { ++projNews; };
        check(home.app.shortcut('o', true, false) && projOpens == 1, "on Home, Ctrl+O is Open project…");
        check(home.app.shortcut('n', true, false) && projNews == 1, "on Home, Ctrl+N is New project");
        check(home.app.shortcut('s', true, false) && !home.app.toast()->isShowing(), "on Home, Ctrl+S is swallowed and does nothing");
        check(home.app.shortcut(',', true, false), "on Home, Ctrl+, is taken");
        home.settle(300.0);
        check(home.app.modalOpen(), "and opens Settings over the launcher");
    }

    void homeSpeaksInSentenceCaseAndSaysWhenItIsEmpty()
    {
        std::printf("Home: sentence-case actions, and an empty state of one sentence, one hint, one button (R-FAMILY-14)\n");
        using arstro::cosmo_v2::HomeScreen;
        Rig rig(1440.0, 900.0);
        HomeScreen *home = rig.app.home();
        home->setRecents({});
        rig.settle(200.0);
        check(drew(rig, "New project") && drew(rig, "Open project\xE2\x80\xA6") && drew(rig, "Import catalog\xE2\x80\xA6"),
              "the actions read New project / Open project… / Import catalog…");
        check(!drew(rig, "New Project") && !drew(rig, "Open Project\xE2\x80\xA6") && !drew(rig, "Recent Projects"),
              "no Title Case button or header is left");
        check(drew(rig, "Recent projects"), "the header reads Recent projects");
        check(!anyThreeDots(rig), "nothing on Home has three dots");
        check(home->showsEmptyState() && drew(rig, "No recent projects"), "with no recent project it says so, starting with a capital");
        check(drew(rig, "Create a project, or open one you already have."), "one hint");
        const artboard::Rect btn = home->emptyButtonRect();
        check(btn.w > 40.0 && btn.x > 300.0 && btn.x + btn.w < 1440.0 && btn.y > 60.0 && btn.y + btn.h < 900.0,
              "one button, inside the grid pane");
        int stroked = 0, labels = 0;
        for (const auto &op : rig.target.ops())
            if (op.kind == artboard::DrawOp::Kind::DrawText && op.text == "New project") ++labels;
        check(labels == 2, "New project is drawn twice: the sidebar's and the empty state's - and no dashed card");
        (void)stroked;
        int news = 0;
        rig.app.onNewProjectRequested = [&news] { ++news; };
        // Hover is eased: one frame after the pointer arrives the wash is between.
        rig.app.pointer(1, btn.x + btn.w * 0.5, btn.y + btn.h * 0.5, 0, rig.now);
        rig.frames(2);
        rig.click(btn.x + btn.w * 0.5, btn.y + btn.h * 0.5);
        check(news == 1, "clicking it starts a new project");

        // The same at the window minimum: the block stays inside the pane.
        Rig small(App::minLogicalWidth(), App::minLogicalHeight());
        small.app.home()->setRecents({});
        small.settle(200.0);
        const artboard::Rect sb = small.app.home()->emptyButtonRect();
        check(sb.x >= 300.0 && sb.x + sb.w <= App::minLogicalWidth() && sb.y + sb.h <= App::minLogicalHeight() && sb.y - 48.0 >= 60.0,
              "at the window minimum the sentence, hint and button fit under the header");

        HomeScreen::CardInfo card;
        card.name = "Tokyo"; card.photos = "12 photos"; card.date = "2h ago"; card.recentIndex = 0;
        home->setRecents({card});
        rig.settle(200.0);
        check(!home->showsEmptyState() && !drew(rig, "No recent projects"), "with a recent project the empty state is gone");
        check(home->emptyButtonRect().w == 0.0, "and so is its button");
    }

    // ── R-UX-4: the preset row says "preset"; the photo's export has a button ────────────
    void thePresetRowIsLabelledAndExportHasAButton()
    {
        std::printf("App: the action bar names presets, and the top bar has the photo's Export (R-UX-4)\n");
        using arstro::cosmo_v2::TopBar;
        Rig rig(1440.0, 900.0);
        rig.app.showEditor();
        rig.settle(400.0);
        check(drew(rig, "Save preset") && drew(rig, "Import preset") && drew(rig, "Export preset"),
              "the pinned row reads Save preset / Import preset / Export preset");
        check(!drew(rig, "Save") && !drew(rig, "Import"), "and no bare Save / Import is left to be read as the project's");
        check(drew(rig, "Export"), "the top bar draws an Export button");

        TopBar *bar = widget<TopBar>(rig, "TopBar");
        check(bar != nullptr, "the top bar is in the tree");
        if (!bar) return;
        const artboard::Transform bt = bar->worldTransform();
        const artboard::Rect er = bar->exportRect();
        check(er.x + er.w <= 1440.0 && er.w > 30.0, "the button is inside the bar");
        // No photo: dimmed, and a click opens nothing.
        check(bar->exportEnabledAmount() < 0.01, "with no photo it is dimmed");
        rig.click(bt.e + er.x + er.w * 0.5, bt.f + er.y + er.h * 0.5);
        rig.settle(200.0);
        check(!rig.app.modalOpen(), "and clicking it opens nothing");

        check(rig.loadFakePhoto(2), "photos arrive");
        rig.app.showEditor();
        rig.settle(500.0);
        check(bar->exportEnabledAmount() > 0.99, "with photos it is live");
        // The dim is a tween, not a flip: one frame after the change the live value is between.
        bar->setExportEnabled(false);
        rig.frames(2);
        const double mid = bar->exportEnabledAmount();
        check(mid > 0.0 && mid < 1.0, "the enable/disable fade is eased (live value mid-tween)");
        bar->setExportEnabled(true);
        rig.settle(300.0);
        rig.click(bt.e + er.x + er.w * 0.5, bt.f + er.y + er.h * 0.5);
        rig.settle(300.0);
        check(rig.app.modalOpen(), "clicking Export opens the Export modal");
        const artboard::Segment *root = rig.app.uiRoot("editor");
        const auto *dlg = static_cast<const arstro::cosmo_v2::ExportDialog *>(
            root ? arstro::cosmo_v2::findSegmentByType(*root, "ExportDialog") : nullptr);
        check(dlg && dlg->isOpen(), "and it is the Export dialog");
    }

    // ── R-UX-6: text fits ─────────────────────────────────────────────────────────────────
    void aLongProjectNameAndPresetNameAreFitted()
    {
        std::printf("App: a long project name stays between the menus and the filename; a long preset name ends in an ellipsis (R-UX-6)\n");
        using arstro::cosmo_v2::TopBar;
        using arstro::cosmo_v2::PresetTree;
        Rig rig(1024.0, 640.0);
        check(rig.loadFakePhoto(2), "photos are loaded");
        rig.app.showEditor();
        rig.settle(500.0);
        TopBar *bar = widget<TopBar>(rig, "TopBar");
        if (!bar) { check(false, "the top bar is in the tree"); return; }
        const double menusRight = bar->menuStrip()->x.value() + bar->menuStrip()->width.value();

        bar->setProjectName("Tokyo");
        rig.frames(2);
        artboard::Rect tr = bar->titleRect();
        check(bar->titleDrawn() == "Tokyo", "a short name is drawn whole");
        check(tr.x > menusRight, "clear of the menu strip");

        const std::string longName = "Iceland Ring Road, September 2026 - selects for the print portfolio, second pass (final)";
        bar->setProjectName(longName);
        rig.frames(2);
        tr = bar->titleRect();
        const std::string drawn = bar->titleDrawn();
        check(!drawn.empty() && drawn != longName && drawn.size() >= 3 && drawn.compare(drawn.size() - 3, 3, "\xE2\x80\xA6") == 0,
              "a long name is ellipsized");
        check(tr.x >= menusRight + 9.0, "it starts after the menu strip, not over it");
        // Its right edge stops before the filename readout: find that text op's x.
        double fileX = 1e9;
        for (const auto &op : rig.target.ops())
            if (op.kind == artboard::DrawOp::Kind::DrawText && op.text.find("ui0.raf") != std::string::npos &&
                op.fontFamily == arstro::cosmo_v2::font::mono() && op.args[2] == 11.0 && op.args[1] < 29.25)
                fileX = std::min(fileX, op.args[0]);   // the top bar's readout: 11 px mono, in the bar's band
        check(fileX < 1e9, "the filename readout is drawn");
        check(tr.x + tr.w <= fileX - 9.0, "and it stops before the filename readout");
        check(rig.target.measureText(drawn, 12.0, arstro::cosmo_v2::font::sansMedium()) <= tr.w + 0.5, "the drawn text is as wide as its box says");

        // The preset rail: a long name ends in an ellipsis inside the rail.
        PresetTree *tree = widget<PresetTree>(rig, "PresetTree");
        if (!tree) { check(false, "the preset tree is in the tree"); return; }
        arstro::cosmo::PresetNode leaf;
        leaf.name = "Kodak Portra 400 pushed two stops, warm highlights, lifted blacks v3";
        leaf.relPath = "long.apf";
        tree->setRoots({leaf});
        rig.frames(3);
        bool cut = false, whole = false;
        double right = 0.0;
        for (const auto &op : rig.target.ops())
            if (op.kind == artboard::DrawOp::Kind::DrawText && op.text.rfind("Kodak Portra", 0) == 0)
            {
                whole |= op.text == leaf.name;
                cut |= op.text.size() >= 3 && op.text.compare(op.text.size() - 3, 3, "\xE2\x80\xA6") == 0;
                right = op.args[0] + rig.target.measureText(op.text, 11.0, arstro::cosmo_v2::font::sans());
            }
        check(cut && !whole, "the long preset name is drawn with an ellipsis, not whole");
        check(right > 0.0 && right <= tree->width.value() - 6.0, "and ends inside the rail");
    }

    // ── R-UX-7: empty and missing are states ──────────────────────────────────────────────
    void anEmptyProjectAndAMissingPhotoSayWhatTheyAre()
    {
        std::printf("App: an empty project offers Add photos; a missing photo has a name and the stage says File not found (R-UX-7)\n");
        using arstro::cosmo_v2::PhotoCanvas;
        using arstro::cosmo_v2::PresetTree;
        {
            Rig rig(1024.0, 640.0);
            int asked = 0;
            rig.app.onOpenRequested = [&asked] { ++asked; };
            rig.app.showEditor();
            rig.frames(3);
            PhotoCanvas *canvas = widget<PhotoCanvas>(rig, "PhotoCanvas");
            if (!canvas) { check(false, "the canvas is in the tree"); return; }
            rig.settle(400.0);
            check(canvas->notice() == PhotoCanvas::Notice::Empty && canvas->noticeAmount() > 0.99, "an empty project shows its notice");
            check(drew(rig, "No photos yet") && drew(rig, "Add photos"), "it says No photos yet and offers Add photos");
            const artboard::Transform ct = canvas->worldTransform();
            const artboard::Rect b = canvas->noticeButtonRect();
            check(b.x >= 0.0 && b.x + b.w <= canvas->width.value() && b.y + b.h <= canvas->height.value(), "the button is inside the stage");
            rig.click(ct.e + b.x + b.w * 0.5, ct.f + b.y + b.h * 0.5);
            check(asked == 1, "Add photos asks the host for files (the context menu's Add Photo)");
            // The empty rail says so too.
            PresetTree *tree = widget<PresetTree>(rig, "PresetTree");
            if (tree && tree->rowCount() == 0)
                check(drew(rig, "No presets yet") && tree->emptyAmount() > 0.99, "an empty presets rail says No presets yet");
            else if (tree)
            {
                tree->setRoots({});
                rig.frames(2);
                const double mid = tree->emptyAmount();
                check(mid > 0.0 && mid < 1.0, "the rail's empty sentence fades in (live value mid-tween)");
                rig.settle(300.0);
                check(drew(rig, "No presets yet"), "an empty presets rail says No presets yet");
            }

            // Photos arrive: the notice leaves by FADING (a frame where it is partly there).
            {
                std::ofstream f("/tmp/cosmo_ui_fake1.cmp", std::ios::trunc);
                f << "cosmoworkspace=1\n#image\nparent=-1\npath=/fake/ui0.raf\n";
            }
            struct Grey : arstro::cosmo::IImageDecoder
            {
                arstro::cosmo::DecodedImage decodeFile(const std::string &path) override
                {
                    arstro::cosmo::DecodedImage d;
                    d.width = 96; d.height = 64;
                    d.rgba.assign((size_t)96 * 64 * 4, 150);
                    d.name = path;
                    return d;
                }
            };
            rig.svc.setDecoderFactory([] { return std::unique_ptr<arstro::cosmo::IImageDecoder>(new Grey()); });
            std::string err;
            check(rig.svc.dispatchText("project open /tmp/cosmo_ui_fake1.cmp", err), "a project opens");
            bool partly = false;
            for (int i = 0; i < 600 && canvas->noticeAmount() > 0.004; ++i)
            {
                rig.app.syncFromSession();
                rig.frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                partly |= canvas->noticeAmount() > 0.02 && canvas->noticeAmount() < 0.98;
            }
            check(partly, "the notice fades out rather than vanishing (a frame where it is partly there)");
            rig.settle(400.0);
            check(canvas->noticeAmount() < 0.01 && !drew(rig, "No photos yet"), "and with photos it is gone");
        }
        {
            // One good photo and one whose decode fails: the failed leaf is a MISSING photo.
            Rig rig(1024.0, 640.0);
            struct HalfDecoder : arstro::cosmo::IImageDecoder
            {
                arstro::cosmo::DecodedImage decodeFile(const std::string &path) override
                {
                    arstro::cosmo::DecodedImage d;
                    if (path.find("gone") != std::string::npos) return d;   // nothing decoded
                    d.width = 96; d.height = 64;
                    d.rgba.assign((size_t)96 * 64 * 4, 150);
                    d.name = path;
                    return d;
                }
            };
            rig.svc.setDecoderFactory([] { return std::unique_ptr<arstro::cosmo::IImageDecoder>(new HalfDecoder()); });
            const std::string path = "/tmp/cosmo_ui_missing.cmp";
            {
                std::ofstream f(path, std::ios::trunc);
                f << "cosmoworkspace=1\n#image\nparent=-1\npath=/fake/here.raf\n#image\nparent=-1\npath=/offline/shoot/gone_0042.raf\n";
            }
            std::string err;
            check(rig.svc.dispatchText("project open " + path, err), "a project with a missing file opens");
            for (int i = 0; i < 400 && rig.svc.model().load.active; ++i) { rig.frames(1); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            rig.app.showEditor();
            rig.settle(500.0);
            int missingNode = -1;
            for (const auto &nd : rig.svc.model().nodes) if (nd.failed) missingNode = nd.node;
            check(missingNode >= 0, "one photo failed to load");
            rig.frames(2);
            check(drew(rig, "gone_0042.raf"), "its filmstrip cell carries its name");
            check(rig.svc.dispatchText("select " + std::to_string(missingNode), err), "the missing photo can be selected");
            rig.app.syncFromSession();
            rig.settle(500.0);
            PhotoCanvas *canvas = widget<PhotoCanvas>(rig, "PhotoCanvas");
            check(canvas && canvas->notice() == PhotoCanvas::Notice::Missing, "the stage shows the missing notice");
            bool path42 = false;
            for (const auto &op : rig.target.ops())
                if (op.kind == artboard::DrawOp::Kind::DrawText) path42 |= op.text.find("gone_0042.raf") != std::string::npos && op.fontFamily == arstro::cosmo_v2::font::mono() && op.args[2] == 11.0;
            check(drew(rig, "File not found") && path42, "it says File not found and names the path");
        }
    }

    // ── R-UX-8: readouts in real units ────────────────────────────────────────────────────
    void sliderReadoutsAreInRealUnits()
    {
        std::printf("App: slider readouts are EV, kelvin and pixels, and a unipolar value has no plus (R-UX-8)\n");
        using namespace arstro::cosmo_v2;
        const EditControl *exp = nullptr, *temp = nullptr, *radius = nullptr, *dehaze = nullptr, *contrast = nullptr;
        for (const auto &sec : editControls())
            for (const auto &c : sec.controls)
            {
                const std::string f = c.field;
                if (f == "exposure") exp = &c;
                if (f == "temp") temp = &c;
                if (f == "sharpenRadius") radius = &c;
                if (f == "dehaze") dehaze = &c;
                if (f == "contrast") contrast = &c;
            }
        check(exp && temp && radius && dehaze && contrast, "the catalogue has the rows");
        if (!(exp && temp && radius && dehaze && contrast)) return;
        check(formatEditControl(*exp, 32) == "+0.40 EV", "exposure: track 32 reads +0.40 EV, not +32");
        check(formatEditControl(*exp, -80) == "-1.00 EV" && formatEditControl(*exp, 0) == "0.00 EV", "exposure is signed, two decimals, and 0 has no sign");
        check(formatEditControl(*temp, fromKelvin(6200)) == "6200 K", "temperature: 6200 K reads 6200 K, not -2");
        check(formatEditControl(*temp, 0) == "6500 K", "the neutral temperature reads 6500 K");
        check(formatEditControl(*radius, 10) == "1.0 px", "sharpening radius: track 10 reads 1.0 px, not +10");
        check(formatEditControl(*dehaze, 40) == "40", "a unipolar control has no plus sign");
        check(formatEditControl(*contrast, 25) == "+25" && formatEditControl(*contrast, -25) == "-25", "a bipolar control keeps its sign");

        // And the assembled column draws them.
        Rig rig(1440.0, 900.0);
        check(rig.loadFakePhoto(1), "a photo is loaded");
        rig.app.showEditor();
        std::string err;
        check(rig.svc.dispatchText("set exposure=0.4 temp=6200 sharpenRadius=1 dehaze=40", err), "values are set in engine units");
        rig.settle(500.0);
        check(drew(rig, "+0.40 EV"), "the Exposure row reads +0.40 EV");
        check(drew(rig, "6200 K"), "the Temperature row reads 6200 K");
        check(drew(rig, "40") && !drew(rig, "+40"), "Dehaze reads 40, with no plus");
        check(!drew(rig, "+32"), "and the track position +32 is not shown anywhere");
        const auto *col = widget<RightColumn>(rig, "RightColumn");
        if (col && col->basicPanel() && col->basicPanel()->row(0) && col->basicPanel()->row(1))
            check(near(col->basicPanel()->row(0)->children().front()->width.value(),
                       col->basicPanel()->row(1)->children().front()->width.value()), "every row's track is the same length");
    }

    // ── R-UX-5 / R-UX-9: dead buttons gone, one header style, a mask hint, a scroll indicator ──
    void theEditStackIsConsistent()
    {
        std::printf("App: Xform has no dead buttons, headers are capitals, the Mask tab explains itself, an overflowing page shows it (R-UX-5, R-UX-9)\n");
        using namespace arstro::cosmo_v2;
        Rig rig(1024.0, 640.0);
        check(rig.loadFakePhoto(1), "a photo is loaded");
        rig.app.showEditor();
        rig.settle(500.0);
        RightColumn *col = widget<RightColumn>(rig, "RightColumn");
        EditStackTabs *tabs = widget<EditStackTabs>(rig, "EditStackTabs");
        if (!col || !tabs) { check(false, "the right column is in the tree"); return; }

        // Basic/Detail overflows a 640-tall window: the indicator is there, inside the column,
        // and it moves with the scroll.
        const artboard::Rect t0 = col->scrollThumbRect();
        check(t0.w > 0.0 && t0.h >= 19.5 && col->scrollIndicatorAmount() > 0.99, "an overflowing page shows a scroll indicator");
        check(t0.x + t0.w <= col->width.value() && t0.x > col->width.value() - 10.0, "at the column's right edge");
        col->scrollActivePanel(-400.0);
        rig.settle(400.0);
        check(col->scrollThumbRect().y > t0.y + 1.0, "it moves down as the page scrolls");
        col->scrollActivePanel(-100000.0);
        rig.settle(400.0);
        const artboard::Rect tEnd = col->scrollThumbRect();
        const artboard::Rect band = col->localBounds();
        check(tEnd.y + tEnd.h <= band.h - ActionBar::kHeight, "and at the end of the page it stops above the action bar");

        // Mask: capitals, and the sentence.
        tabs->setSelectedIndex(RightColumn::kTabMask);
        rig.settle(400.0);
        check(drew(rig, "ADD MASK") && !drew(rig, "Add Mask"), "the Mask header is in capitals");
        check(col->maskPanel()->emptyAmount() > 0.99 && drew(rig, "A mask limits an adjustment to part of the photo."),
              "with no mask the tab says what a mask is for");
        check(col->scrollThumbRect().w == 0.0, "a page that fits has no indicator rect");
        check(col->scrollIndicatorAmount() < 0.01, "and the indicator has faded out");

        // Curve / Grade / Xform: capitals; Xform: no Flip / Auto.
        tabs->setSelectedIndex(RightColumn::kTabColor);
        rig.settle(400.0);
        col->scrollActivePanel(-100000.0);
        rig.settle(400.0);
        check(drew(rig, "TONE CURVE") && !drew(rig, "Tone Curve"), "the Curve header is in capitals");
        tabs->setSelectedIndex(RightColumn::kTabGrade);
        rig.settle(400.0);
        check(drew(rig, "SHADOWS") || drew(rig, "MIDTONES") || drew(rig, "HIGHLIGHTS"), "the Grade headers are in capitals");
        tabs->setSelectedIndex(RightColumn::kTabXform);
        rig.settle(400.0);
        check(drew(rig, "ROTATE") && drew(rig, "ASPECT RATIO"), "the Xform headers are in capitals");
        check(!drew(rig, "Auto Horizon") && !drew(rig, "Auto Geometry") && !drew(rig, "Horizontal") && !drew(rig, "Vertical"),
              "the four buttons with no engine behind them are not drawn");
        check(!drew(rig, "FLIP") && !drew(rig, "AUTO") && !drew(rig, "Flip") && !drew(rig, "Auto"), "nor are their two headers");
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
    // ── R-MASK-6: draw a mask on the photo, through the whole app ────────────────────────
    void drawingAMaskOnThePhotoReachesTheModel()
    {
        std::printf("App: the Draw chip plus three clicks on the photo make a path mask (R-MASK-6)\n");
        Rig rig(1440.0, 900.0);
        check(rig.loadFakePhoto(), "a photo is loaded");
        rig.app.showEditor();
        rig.settle(600.0);

        const artboard::Segment *root = rig.app.uiRoot("editor");
        const artboard::Segment *tabs =
            root ? arstro::cosmo_v2::findSegmentByType(*root, "EditStackTabs") : nullptr;
        check(tabs != nullptr, "the edit-stack tabs exist");
        if (!tabs) return;
        // Mask is the second of five tabs.
        const artboard::Transform tw = tabs->worldTransform();
        rig.click(tabCenter(tabs, 1), tw.f + 13.0);
        rig.settle(400.0);

        auto *panel = static_cast<const arstro::cosmo_v2::MaskPanel *>(
            arstro::cosmo_v2::findSegmentByType(*root, "MaskPanel"));
        check(panel != nullptr, "and the Mask panel is up");
        if (!panel) return;

        // The Draw chip is the fourth of the four "add mask" chips — asked for by position from
        // the panel itself, not indexed out of a child list whose order is a layout detail.
        // Clicked rather than dispatched, so this asserts the route a user takes.
        const artboard::Transform mw = panel->worldTransform();
        const artboard::Rect chip = panel->addChipRect(3);
        check(chip.w > 0.0, "the Draw chip is there");
        rig.click(mw.e + chip.x + chip.w * 0.5, mw.f + chip.y + chip.h * 0.5);
        rig.settle(400.0);

        const arstro::EditParams &p = rig.svc.model().params;
        check(p.masks.size() == 1u, "which adds a mask");
        if (p.masks.empty()) return;
        check(p.masks[0].type == arstro::MaskParams::Path, "of the drawn kind");
        check(p.masks[0].path.empty(), "with nothing drawn yet — the photo is where it is drawn");

        // Now draw on the photo. The overlay maps to the photo's fitted rect, so the clicks go
        // through the canvas exactly as a user's do.
        auto photo = rig.app.uiRoot("editor")
                         ? arstro::cosmo_v2::findSegmentByType(*root, "PhotoCanvas") : nullptr;
        check(photo != nullptr, "the photo canvas is in the tree");
        if (!photo) return;
        const artboard::Transform pw = photo->worldTransform();
        const double px = pw.e, py = pw.f;
        const double PW = photo->width.value(), PH = photo->height.value();
        rig.click(px + PW * 0.45, py + PH * 0.30);
        rig.click(px + PW * 0.62, py + PH * 0.62);
        rig.click(px + PW * 0.35, py + PH * 0.60);
        rig.settle(400.0);

        const arstro::EditParams &q = rig.svc.model().params;
        check(q.masks.size() == 1u && q.masks[0].path.size() == 3u,
              "three clicks on the photo place three points, in the MODEL");
        if (q.masks.empty() || q.masks[0].path.size() != 3u) return;
        // Inside the photo's own coordinate space, which is what makes the mask survive a zoom.
        for (const auto &cp : q.masks[0].path)
            check(cp.x > 0.0f && cp.x < 1.0f && cp.y > 0.0f && cp.y < 1.0f,
                  "each point is in normalised photo coordinates");
        // And the shape they make covers the middle of the photo, which is the whole point.
        check(arstro::maskCoverage(q.masks[0], 0.47f, 0.5f) > 0.99f,
              "and the engine covers the inside of what was drawn");
    }
}

namespace
{
    // ── D-62: an export from a turned JPEG tells viewers it is upright ───────────────
    //
    // The decoder now turns a JPEG by its EXIF Orientation, so the pixels the engine renders and
    // the exporter encodes are already upright. The exporter copies the source's APP1 verbatim;
    // left alone, the source's "rotate 90 CCW" would ride along and every viewer would turn the
    // upright file onto its side again.
    void anExportFromATurnedJpegSaysItIsUpright()
    {
        std::printf("Export: a JPEG shot as a portrait (Orientation 8) exports with Orientation 1 (D-62)\n");
        const auto dir = std::filesystem::temp_directory_path();
        const std::string src = (dir / "cosmo_ui_d62_src.jpg").string();
        const std::string out = (dir / "cosmo_ui_d62_out.jpg").string();
        std::vector<guchar> px((size_t)48 * 32 * 3, 128);
        GdkPixbuf *pb = gdk_pixbuf_new_from_data(px.data(), GDK_COLORSPACE_RGB, FALSE, 8, 48, 32, 48 * 3, nullptr, nullptr);
        gdk_pixbuf_save(pb, src.c_str(), "jpeg", nullptr, "quality", "90", nullptr);
        g_object_unref(pb);
        {
            std::ifstream in(src, std::ios::binary);
            std::vector<uint8_t> j((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            // FF E1 | len 34 | "Exif\0\0" | II*\0, IFD0 at 8 | one entry: 0x0112 SHORT x1 = 8 | next IFD 0
            const uint8_t app1[] = {0xFF, 0xE1, 0, 34, 'E', 'x', 'i', 'f', 0, 0, 'I', 'I', 42, 0, 8, 0, 0, 0,
                                    1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0};
            j.insert(j.begin() + 2, std::begin(app1), std::end(app1));
            std::ofstream(src, std::ios::binary | std::ios::trunc).write((const char *)j.data(), (std::streamsize)j.size());
        }
        auto orientation = [](const std::string &path) {
            for (const auto &kv : arstro::cosmo_v2::exif::read(path))
                if (kv.first == "Orientation") return kv.second;
            return std::string();
        };
        check(orientation(src) == "Rotated 90\xC2\xB0 CCW", "the source says it must be turned");
        arstro::cosmo_v2::ExportDialog::Request req;   // JPEG, EXIF embedded: the defaults
        std::vector<uint8_t> rgba((size_t)32 * 48 * 4, 128);   // what the engine renders: upright
        std::string err;
        const bool ok = arstro::cosmo_v2::exporter::write(req, rgba.data(), 32, 48, out, src, err);
        check(ok, "the export is written");
        check(orientation(out) == "Normal", "and its EXIF says Orientation 1, so no viewer turns it again");
        std::filesystem::remove(src);
        std::filesystem::remove(out);
    }

    // ── R-EXPORT-3a / D-72: the default export may not replace the photo it came from ────
    // The dialog's defaults are "same as source", JPEG, no prefix, no subfolder - so a JPEG's
    // output path was the JPEG. The bytes of the original are the assertion: a name check alone
    // would pass on a writer that picked a new name and still truncated the old file.
    void anExportBesideTheSourceLeavesTheOriginalAlone()
    {
        std::printf("Export: a JPEG exported beside itself keeps the original byte for byte (R-EXPORT-3a, D-72)\n");
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "cosmo_ui_d72";
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::string src = (dir / "IMG_1.jpg").string();
        std::vector<guchar> px((size_t)48 * 32 * 3, 90);
        GdkPixbuf *pb = gdk_pixbuf_new_from_data(px.data(), GDK_COLORSPACE_RGB, FALSE, 8, 48, 32, 48 * 3, nullptr, nullptr);
        gdk_pixbuf_save(pb, src.c_str(), "jpeg", nullptr, "quality", "90", nullptr);
        g_object_unref(pb);
        auto bytes = [](const std::string &p) {
            std::ifstream in(p, std::ios::binary);
            return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        };
        const std::vector<char> original = bytes(src);
        check(!original.empty(), "there is a source JPEG");

        arstro::cosmo_v2::ExportDialog::Request req;   // the dialog's defaults, untouched
        std::vector<uint8_t> rgba((size_t)48 * 32 * 4, 200);   // an EDITED frame: brighter than the source
        std::string err;
        const std::string out1 = arstro::cosmo_v2::exporter::resolvePath(req, src, "IMG_1.jpg");
        check(out1 != src, "the output path is not the source path");
        check(arstro::cosmo_v2::exporter::write(req, rgba.data(), 48, 32, out1, src, err), "the export is written");
        check(bytes(src) == original, "the original is byte-identical after the export");
        check(fs::path(out1).filename() == "IMG_1_edit.jpg" && fs::exists(out1), "the export is IMG_1_edit.jpg beside it");

        // A second export replaces neither the source nor the first export.
        const std::vector<char> first = bytes(out1);
        const std::string out2 = arstro::cosmo_v2::exporter::resolvePath(req, src, "IMG_1.jpg");
        std::vector<uint8_t> darker((size_t)48 * 32 * 4, 40);
        check(arstro::cosmo_v2::exporter::write(req, darker.data(), 48, 32, out2, src, err), "a second export is written");
        check(fs::path(out2).filename() == "IMG_1_edit-1.jpg", "it is IMG_1_edit-1.jpg");
        check(bytes(out1) == first && bytes(src) == original, "and the first export and the original are both untouched");

        // Another format beside the source has a free name and keeps it; once taken, it counts.
        req.format = "PNG";
        check(fs::path(arstro::cosmo_v2::exporter::resolvePath(req, src, "x")).filename() == "IMG_1.png", "a PNG beside a JPEG keeps the plain name");
        std::ofstream((dir / "IMG_1.png").string()) << "someone else's file";
        check(fs::path(arstro::cosmo_v2::exporter::resolvePath(req, src, "x")).filename() == "IMG_1-1.png", "an existing file of that name is not replaced either");

        // The scripted writer's rule: only the source is protected, its own outputs are replaced.
        using arstro::cosmo_v2::exporter::awayFromSource;
        check(awayFromSource(src, src) == (dir / "IMG_1_edit.jpg").string(), "export --outdir into the photo's folder: _edit, even though that name exists");
        check(awayFromSource((dir / "other.jpg").string(), src) == (dir / "other.jpg").string(), "any other path is written as given");
        fs::remove_all(dir);
    }
}

namespace
{
    // ── R-ASSIST-7: the Agent tab ──────────────────────────────────────────────────────
    //
    // The assembled app with a fake agent behind the service: the tab is reachable, the empty
    // state says what to do, Send is `assist send`, an answer FADES in (live alpha mid-tween), a
    // long conversation follows the newest message by EASING there (live scroll != target), a user
    // who scrolled up to read is not yanked back, and Stop is `assist stop`.
    /** R-ASSIST-12: every agent answers in Markdown; the parser and layout under the Agent tab. */
    void anAgentsMarkdownBecomesFormatting()
    {
        std::printf("MarkdownText: an agent's Markdown parses to styled text and fits its width (R-ASSIST-12)\n");
        using namespace arstro::cosmo_v2;
        auto find = [](const std::vector<md::Span> &sp, const std::string &t) -> const md::Span * {
            for (const auto &s : sp)
                if (s.text == t) return &s;
            return nullptr;
        };
        auto joined = [](const std::vector<md::Span> &sp) {
            std::string all;
            for (const auto &s : sp) all += s.text;
            return all;
        };
        const auto in = md::parseInline("**Exposure** lifted by `+0.3`, *gently*, ~~not~~ snake_case_name 2**3 local:<url>|<model>");
        const md::Span *b = find(in, "Exposure"), *c = find(in, "+0.3"), *i = find(in, "gently"), *x = find(in, "not");
        check(b && (b->style & md::Bold) && c && (c->style & md::Code) && i && (i->style & md::Italic) && x && (x->style & md::Strike),
              "bold, code, italic and strike become styles");
        const std::string all = joined(in);
        check(all.find("**E") == std::string::npos && all.find('`') == std::string::npos && all.find("snake_case_name") != std::string::npos &&
                  all.find("2**3") != std::string::npos && all.find("local:<url>|<model>") != std::string::npos,
              "their markers are gone; snake_case, 2**3 and <placeholders> stay text");

        const auto streaming = md::parse("- **Port");
        check(streaming.size() == 1 && streaming[0].kind == md::BlockKind::Bullet && streaming[0].spans.size() == 1 &&
                  streaming[0].spans[0].text == "Port" && (streaming[0].spans[0].style & md::Bold),
              "mid-stream, an unclosed ** is already bold - no stars flash while the reply arrives");
        const auto fence = md::parse("```sh\nedit.py run");
        check(fence.size() == 1 && fence[0].kind == md::BlockKind::CodeBlock && fence[0].code.size() == 1 && fence[0].code[0] == "edit.py run",
              "an unclosed fence is code to the end");

        const auto blocks = md::parse("## Title\n1. one\n2) two\n- [x] done\n> quote\n---\n| a | b |\n|:-|--:|\n| 1 | 2 |\nplain\nnext line");
        std::vector<md::BlockKind> kinds;
        for (const auto &bl : blocks) kinds.push_back(bl.kind);
        using K = md::BlockKind;
        check(kinds == std::vector<K>{K::Heading, K::Ordered, K::Ordered, K::Task, K::Quote, K::Rule, K::Table, K::Paragraph} &&
                  blocks[0].level == 2 && blocks[2].marker == "2." && blocks[3].done && blocks[6].rows.size() == 2 &&
                  blocks[6].align == std::vector<char>{'l', 'r'},
              "headings, ordered and task items, quotes, rules and tables are blocks");
        check(blocks[7].spans.size() == 3 && (blocks[7].spans[1].style & md::Break), "a single newline is a line break, as in a chat");

        check(joined(md::parse("<think>hmm</think>Answer")[0].spans) == "Answer" && md::parse("<think>still going").back().kind == K::Thinking &&
                  joined(md::parse("reasoning</think>Answer")[0].spans) == "Answer",
              "a reasoning model's <think> is not the answer: dropped when closed, one Thinking line while open");

        const auto odd = md::parseInline("\x1b[1mBold\x1b[0m \xF0\x9F\x8E\xAC done \xE2\x9C\x85 \xE2\x86\x92 next");
        bool tick = false, arrow = false;
        for (const auto &s : odd)
        {
            tick |= (s.style & md::Tick) != 0;
            arrow |= (s.style & md::Symbol) && s.text == "\xE2\x86\x92";
        }
        const std::string oddAll = joined(odd);
        check(tick && arrow && oddAll.find('\x1b') == std::string::npos && oddAll.find("\xF0\x9F") == std::string::npos,
              "ANSI colours go, a pictograph the faces lack goes, a check mark is drawn, an arrow is drawn in mono");
        check(md::plain("Read `edit.py` **now**") == "Read edit.py now", "a one-line tool title loses its markup");

        // Layout: nothing spills past the width, a long URL and a wide table included (R5).
        artboard::RecordingTarget rt;
        const double W = 180.0;
        const md::Layout l = md::layout(rt, md::parse("**Bold** and *italic* text that wraps across lines\n"
                                                      "https://example.com/a/very/long/path/that/cannot/fit/on/one/line\n\n"
                                                      "| Photo | Exposure | A long look name |\n|---|--:|---|\n| Lake | +0.30 | Natural Teal for the sky |\n\n"
                                                      "```\na code line that is much too long for a narrow column\n```"),
                                        W);
        bool fits = true, boldFace = false, italicFace = false;
        for (const auto &r : l.runs)
        {
            fits &= r.x >= -0.01 && r.x + r.w <= W + 0.5;
            boldFace |= r.text == "Bold" && std::string(r.font) == font::sansSemiBold();
            italicFace |= r.text == "italic" && std::string(r.font) == font::sansItalic();
        }
        check(fits && l.height > 0.0, "every run fits the width: words wrap, a URL and a code line are cut, a table shares it");
        check(boldFace && italicFace, "bold draws in Roboto SemiBold, italic in Roboto Italic");
    }

    void theAgentTabTalksThroughTheService()
    {
        std::printf("App: the Agent tab shows the conversation and talks through the service (R-ASSIST-7)\n");
        using namespace arstro::cosmo;
        using arstro::cosmo::AgentChoice;
        struct Shared
        {
            std::vector<std::string> sent;
            std::vector<AssistOutput> queue;
            bool stopped = false;
        };
        auto sh = std::make_shared<Shared>();
        struct Fake : IAssistant
        {
            std::shared_ptr<Shared> s;
            explicit Fake(std::shared_ptr<Shared> x) : s(std::move(x)) {}
            bool start(const AssistLaunch &, std::string &) override { return true; }
            bool send(const std::string &t, std::string &) override { s->sent.push_back(t); return true; }
            void stop() override { s->stopped = true; }
            void poll(std::vector<AssistOutput> &out) override { out.swap(s->queue); s->queue.clear(); }
            std::string name() const override { return "Claude Code"; }
        };
        Rig rig(1440.0, 900.0);
        rig.svc.setAssistantFactory([sh] { return std::unique_ptr<IAssistant>(new Fake(sh)); });
        rig.svc.setAgentDetector([] {
            return std::vector<AgentChoice>{{"claude", "Claude Code", "claude", true, "installed"},
                                            {"ollama:m", "Ollama - m", "local:http://127.0.0.1:11434/v1|m", true, "local model"}};
        });
        check(rig.loadFakePhoto(), "a photo is open");
        rig.app.showEditor();
        rig.settle(400.0);

        const artboard::Segment *root = rig.app.uiRoot("editor");
        const artboard::Segment *tabs = root ? arstro::cosmo_v2::findSegmentByType(*root, "EditStackTabs") : nullptr;
        auto *agent = const_cast<arstro::cosmo_v2::AgentPanel *>(static_cast<const arstro::cosmo_v2::AgentPanel *>(
            root ? arstro::cosmo_v2::findSegmentByType(*root, "AgentPanel") : nullptr));
        check(tabs && agent, "the Agent tab and its panel are in the editor");
        if (!tabs || !agent) return;
        // The Agent tab, clicked the way a user does, where the tabs say it is.
        const artboard::Transform tw = tabs->worldTransform();
        rig.click(tabCenter(tabs, arstro::cosmo_v2::RightColumn::kTabAgent), tw.f + 13.0);
        rig.settle(400.0);
        check(agent->visible && agent->width.value() > 300.0 && agent->messagesRect().h > 200.0,
              "selecting it lays the panel out across the column");
        check(agent->contentHeight() == 0.0 && rig.svc.model().assist.messages.empty(), "empty: nothing said yet");

        // R-ASSIST-11: with nothing said and no agent running, the tab is the CHOICES - looked for on
        // first sight - and Connect stores the chosen setting and starts it.
        check(agent->pickerShown() && agent->pickAmount() > 0.99, "it opens on the agents to choose from");
        check(agent->choiceCount() == 2 && agent->selectedChoice() == 0 && agent->fieldText() == "claude",
              "the machine's agents are listed, the field shows the selected one's setting");
        const artboard::Transform at = agent->worldTransform();
        const artboard::Rect c1 = agent->choiceRect(1);
        rig.click(at.e + c1.x + c1.w * 0.5, at.f + c1.y + c1.h * 0.5);
        check(agent->selectedChoice() == 1 && agent->fieldText() == "local:http://127.0.0.1:11434/v1|m",
              "clicking a choice selects it and shows its setting, editable");
        const artboard::Rect sr = agent->sendRect();
        rig.click(at.e + sr.x + sr.w * 0.5, at.f + sr.y + sr.h * 0.5);   // Connect
        check(rig.svc.model().settings.agent == "local:http://127.0.0.1:11434/v1|m" && rig.svc.model().assist.state != "off",
              "Connect stores the setting and starts that agent");
        bool fading = false;
        for (int i = 0; i < 8 && !fading; ++i)
        {
            rig.frame();
            fading = agent->pickAmount() > 0.01 && agent->pickAmount() < 0.99;
        }
        check(fading, "the choices cross-fade into the chat (a frame mid-fade)");
        rig.settle(300.0);
        check(!agent->pickerShown() && agent->pickAmount() < 0.01, "connected, the tab is the chat");

        // Send through the panel's own button.
        agent->typeForTest("make these cinematic");
        rig.frames(2);
        rig.click(at.e + sr.x + sr.w * 0.5, at.f + sr.y + sr.h * 0.5);
        check(sh->sent.size() == 1 && sh->sent[0] == "make these cinematic", "Send is `assist send`: the agent got the message");
        check(rig.svc.model().assist.state == "busy", "and the agent is working");

        // An answer arrives: it fades in rather than popping.
        sh->queue = {{AssistOutput::Kind::Ready, "Claude Code 2 - fake"},
                     {AssistOutput::Kind::Text, "Warm light, deep greens. Photo 1 is done."},
                     {AssistOutput::Kind::Done, "done in 3 s"}};
        rig.frame();
        rig.frame();
        const double mid = agent->newestAlpha();
        check(mid > 0.0 && mid < 1.0, "a new message is mid fade-in a frame after it arrives (live != target)");
        rig.settle(300.0);
        check(agent->newestAlpha() == 1.0 && rig.svc.model().assist.state == "ready", "and at rest it is fully shown");

        // A long conversation overflows: the list EASES to the newest message.
        for (int i = 0; i < 24; ++i)
            sh->queue.push_back({AssistOutput::Kind::Text, "Line " + std::to_string(i) + " of a long answer about photo 1."});
        rig.frames(3);
        check(agent->maxScroll() > 0.0, "the conversation overflows its box");
        bool easing = false;
        for (int i = 0; i < 12 && !easing; ++i)
        {
            rig.frame();
            easing = agent->scrollOffset() > 0.0 && agent->scrollOffset() < agent->scrollTarget() - 0.5;
        }
        check(easing, "it travels toward the newest message (a frame where the live offset is short of the target)");
        rig.settle(400.0);
        check(std::fabs(agent->scrollOffset() - agent->maxScroll()) < 1.0, "and comes to rest on it");

        // The user scrolls up to read: a new message must not yank them back.
        agent->scrollBy(200.0);
        rig.settle(400.0);
        const double reading = agent->scrollOffset();
        sh->queue = {{AssistOutput::Kind::Text, "One more thing."}};
        rig.settle(400.0);
        check(reading < agent->maxScroll() - 50.0 && std::fabs(agent->scrollOffset() - reading) < 1.0,
              "a reader scrolled up stays where they are when a message arrives");

        // R-ASSIST-12: a reply in Markdown is drawn as formatting - no `**` reaches the screen.
        sh->queue = {{AssistOutput::Kind::Text, "**Exposure** lifted by `+0.3` on *every* photo"}};
        agent->scrollBy(-100000.0);   // back to the newest
        rig.settle(400.0);
        bool stars = false, boldRun = false;
        for (const auto &op : rig.target.ops())
            if (op.kind == artboard::DrawOp::Kind::DrawText)
            {
                stars |= op.text.find("**") != std::string::npos || op.text.find('`') != std::string::npos;
                boldRun |= op.text == "Exposure" && op.fontFamily == arstro::cosmo_v2::font::sansSemiBold();
            }
        const arstro::cosmo_v2::md::Layout *ml = agent->messageLayout(rig.svc.model().assist.messages.size() - 1);
        check(!stars && boldRun && ml && !ml->runs.empty(), "an agent's **bold** and `code` are drawn as bold and code, not as markup");

        // Stop, from the header.
        const artboard::Rect ss = agent->startStopRect();
        rig.click(at.e + ss.x + ss.w * 0.5, at.f + ss.y + ss.h * 0.5);
        check(sh->stopped && rig.svc.model().assist.state == "off", "the header's Stop is `assist stop`");
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
    aModalOwnsTheKeyboard();
    deleteAsksBeforeRemovingAPhoto();
    closingAProjectAsksFirstAndClearsTheChrome();
    thePresetRowIsLabelledAndExportHasAButton();
    aLongProjectNameAndPresetNameAreFitted();
    anEmptyProjectAndAMissingPhotoSayWhatTheyAre();
    sliderReadoutsAreInRealUnits();
    theEditStackIsConsistent();
    rightClickShowsTheImageInformation();
    drawingAMaskOnThePhotoReachesTheModel();
    scaleChangeIsAnimatedNotSnapped();
    theStartupScaleDoesNotAnimate();
    everyScaleLaysOutAtItsOwnMinimum();
    theStageDissolvesWhenAnAdjustmentLands();
    theStageNeverBlinksDuringADrag();
    theBeforeAfterToggleDissolves();
    anExportFromATurnedJpegSaysItIsUpright();
    anExportBesideTheSourceLeavesTheOriginalAlone();
    anAgentsMarkdownBecomesFormatting();
    theAgentTabTalksThroughTheService();
    theMenusAreTheFamilys();
    theTopBarSaysWhatIsUnsaved();
    theToastSaysWhatHappened();
    theUnsavedDialogIsTheFamilys();
    theKeysAreTheFamilys();
    homeSpeaksInSentenceCaseAndSaysWhenItIsEmpty();
    std::printf("\n%s (%d failure%s)\n", gFailures ? "FAILED" : "all passed", gFailures,
                gFailures == 1 ? "" : "s");
    return gFailures ? 1 : 0;
}
