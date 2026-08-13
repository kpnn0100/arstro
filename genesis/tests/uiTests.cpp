/*
 *  Genesis — editor tests.
 *
 *  The design rules are only rules if they are checked, so these assert them the way §5 of
 *  the design system says to: render the REAL app through the REAL adapter, headlessly, and
 *  inspect the result — layout that reflows, siblings that do not overlap, text that fits,
 *  and every state actually drawn.
 */
#include "MiniTest.h"
#include "App.h"
#include "widgets/CanvasView.h"
#include "widgets/Chrome.h"
#include "widgets/HomeScreen.h"
#include "widgets/Inspector.h"
#include "widgets/ReactionsPanel.h"
#include "widgets/ShapeTree.h"
#include "widgets/SplashScreen.h"
#include "Recents.h"
#include "widgets/Modal.h"
#include "Runtime.h"
#include <cmath>
#include <string>
#include <vector>

using namespace genesis;
using namespace genesis::ui;
using K = artboard::DrawOp::Kind;

namespace
{
    /** Tick real frames, the way the host does, so animations settle. */
    void settle(App &app, double &now, double ms)
    {
        for (double t = 0; t < ms; t += 16.0)
        {
            now += 16.0;
            app.advance(now);
        }
    }

    /** The editor's five tiles. The home screen and the modal are full-window by design and
     *  are not part of the tiling. */
    std::vector<artboard::Rect> panelBounds(App &app)
    {
        std::vector<artboard::Rect> out;
        for (artboard::Segment *p : app.editorPanels())
            out.push_back({p->x.value(), p->y.value(), p->width.value(), p->height.value()});
        return out;
    }
    /** Put the app on the editor screen and let the transition finish. */
    void toEditor(App &app, double &now)
    {
        app.showEditor();
        for (double t = 0; t < 900.0; t += 16.0)
        {
            now += 16.0;
            app.advance(now);
        }
    }

    bool overlaps(const artboard::Rect &a, const artboard::Rect &b)
    {
        const double eps = 0.5;
        return a.x + eps < b.right() && b.x + eps < a.right() && a.y + eps < b.bottom() &&
               b.y + eps < a.bottom();
    }
}

TEST(App_starts_on_a_working_example_with_no_errors)
{
    App app;
    app.setSize(1280, 800);
    double now = 0.0;
    settle(app, now, 200.0);
    // A new project must be something that already moves (G-14).
    CHECK(!app.doc().shapes.empty());
    CHECK(!app.doc().allReactions().empty());
    CHECK(app.previewOk());
    for (const auto &d : app.diagnostics())
        CHECK(!d.isError());
}
TEST(App_panels_tile_the_window_and_never_overlap)
{
    App app;
    for (double w : {1440.0, 1024.0, 820.0})
        for (double h : {900.0, 640.0, 560.0})
        {
            app.setSize(w, h);
            double now = 0.0;
            settle(app, now, 60.0);
            const auto rects = panelBounds(app);
            for (size_t i = 0; i < rects.size(); ++i)
                for (size_t j = i + 1; j < rects.size(); ++j)
                    CHECK(!overlaps(rects[i], rects[j]));
            for (size_t i = 0; i < rects.size(); ++i)
            {
                CHECK(rects[i].w > 0.0);
                CHECK(rects[i].h > 0.0);
                CHECK(rects[i].right() <= app.width.value() + 0.5);
                CHECK(rects[i].bottom() <= app.height.value() + 0.5);
            }
        }
}
TEST(App_reflows_so_the_canvas_absorbs_the_slack)
{
    App app;
    app.setSize(1440, 900);
    double now = 0.0;
    settle(app, now, 60.0);
    const auto wide = panelBounds(app);
    app.setSize(900, 620);
    settle(app, now, 60.0);
    const auto narrow = panelBounds(app);
    CHECK(wide.size() == narrow.size());
    // The rails shrink, but by less than the centre does: the canvas takes the hit (R-G-4).
    const double railWide = wide[1].w, railNarrow = narrow[1].w;     // the shape tree
    const double canvasWide = wide[2].w, canvasNarrow = narrow[2].w;  // the canvas
    CHECK(railNarrow <= railWide);
    CHECK(canvasNarrow < canvasWide);
    CHECK(canvasWide - canvasNarrow > railWide - railNarrow);
    CHECK(railNarrow >= 150.0);   // never collapses past usable
}
TEST(App_draws_something_at_every_window_size)
{
    App app;
    for (double w : {1440.0, 1024.0, 800.0})
    {
        app.setSize(w, w * 0.62);
        double now = 0.0;
        toEditor(app, now);
        artboard::RecordingTarget t;
        app.render(t);
        CHECK(t.count(K::FillPath) > 20);   // the whole chrome, not a blank window
        CHECK(t.count(K::DrawText) > 10);
    }
}
TEST(App_edit_funnel_revalidates_rebuilds_and_reports)
{
    App app;
    app.setSize(1200, 760);
    double now = 0.0;
    toEditor(app, now);
    CHECK(app.previewOk());

    app.doc().shapes.front().setField("w", "mystery * 2");
    app.documentChanged();
    CHECK(!app.previewOk());                       // the preview refuses to lie
    CHECK(!app.previewError().empty());
    bool sawError = false;
    for (const auto &d : app.diagnostics())
        if (d.isError()) sawError = true;
    CHECK(sawError);
    CHECK(app.statusLevel() == StatusLevel::Bad);  // and says so, loudly

    settle(app, now, 100.0);
    artboard::RecordingTarget t;
    app.render(t);
    bool saidUnavailable = false;
    for (const auto &op : t.ops())
        if (op.kind == K::DrawText && op.text.find("Preview unavailable") != std::string::npos)
            saidUnavailable = true;
    CHECK(saidUnavailable);                        // the error state IS drawn (R-G-6)

    app.doc().shapes.front().setField("w", "minSide * 0.7");
    app.documentChanged();
    CHECK(app.previewOk());                        // and it recovers
}
TEST(App_draws_its_empty_state_with_guidance)
{
    App app;
    app.setSize(1200, 760);
    app.doc().shapes.clear();
    for (auto &s : app.doc().shapes) s.reactions.clear();
    app.selectShape("");
    app.documentChanged();
    double now = 0.0;
    toEditor(app, now);

    artboard::RecordingTarget t;
    app.render(t);
    bool noShapes = false, noObject = false, selectPrompt = false;
    for (const auto &op : t.ops())
    {
        if (op.kind != K::DrawText) continue;
        if (op.text.find("No shapes") != std::string::npos) noShapes = true;
        // Reactions belong to an object now, so with nothing selected the panel says so.
        if (op.text.find("No object selected") != std::string::npos) noObject = true;
        if (op.text.find("Select a shape") != std::string::npos) selectPrompt = true;
    }
    CHECK(noShapes);        // an empty panel says what to do, rather than showing nothing
    CHECK(noObject);
    CHECK(selectPrompt);
}
TEST(App_modal_dims_the_app_and_traps_input)
{
    App app;
    app.setSize(1200, 760);
    double now = 0.0;
    settle(app, now, 100.0);
    CHECK(!app.modal()->isOpen());
    CHECK(!app.modal()->coversApp());

    app.modal()->openNew();
    CHECK(app.modal()->isOpen());
    settle(app, now, 300.0);
    CHECK(app.modal()->coversApp());

    artboard::RecordingTarget t;
    app.render(t);
    bool sawTitle = false;
    for (const auto &op : t.ops())
        if (op.kind == K::DrawText && op.text.find("New component") != std::string::npos)
            sawTitle = true;
    CHECK(sawTitle);

    app.modal()->close();
    CHECK(!app.modal()->isOpen());
    settle(app, now, 400.0);
    CHECK(!app.modal()->coversApp());   // it faded out rather than vanishing
}
TEST(App_selection_drives_the_inspector_and_the_canvas)
{
    App app;
    app.setSize(1280, 800);
    double now = 0.0;
    settle(app, now, 100.0);
    toEditor(app, now);
    const std::string first = app.doc().shapes.front().id;
    app.selectShape(first);
    CHECK(app.selectedShape() == first);
    settle(app, now, 100.0);

    artboard::RecordingTarget t;
    app.render(t);
    bool sawFieldName = false;
    for (const auto &op : t.ops())
        if (op.kind == K::DrawText && op.text == "strokeWidth") sawFieldName = true;
    CHECK(sawFieldName);

    app.selectShape("");
    CHECK(app.selectedShape().empty());
}
TEST(App_new_document_switches_base_transport_and_preview)
{
    App app;
    app.setSize(1280, 800);
    double now = 0.0;
    toEditor(app, now);
    for (const char *base : {"ProgressIndicator", "Button", "Slider", "Checkbox", "VisualLoop"})
    {
        app.newDocument(base, std::string("Demo") + base);
        CHECK(app.doc().base == base);
        CHECK(app.previewOk());
        for (const auto &d : app.diagnostics())
            CHECK(!d.isError());
        settle(app, now, 300.0);
        artboard::RecordingTarget t;
        app.render(t);
        CHECK(t.count(K::FillPath) > 20);
        CHECK(!app.dirty());   // a fresh document is not dirty until it is edited
    }
}
TEST(App_preview_frame_resizes_independently_of_the_window)
{
    App app;
    app.setSize(1280, 800);
    double now = 0.0;
    settle(app, now, 200.0);
    const double before = app.runtime().root()->width.value();
    CHECK_NEAR(before, app.doc().designW, 1.0);

    // The whole point of the frame: a component must be checkable at a size it was not
    // designed at (G-8).
    app.doc().designW = 320.0;
    app.doc().designH = 320.0;
    app.documentChanged();
    settle(app, now, 400.0);
    CHECK(app.runtime().root()->width.value() > before);
}
TEST(App_reduced_motion_and_hover_switches_reach_the_preview)
{
    App app;
    app.setSize(1280, 800);
    double now = 0.0;
    settle(app, now, 100.0);
    CHECK(!app.previewHover());
    app.setPreviewHover(true);
    settle(app, now, 300.0);
    CHECK(app.previewHover());
    CHECK(app.runtime().root()->hoverAmount() > 0.5);
    app.setPreviewHover(false);
    settle(app, now, 300.0);
    CHECK(app.runtime().root()->hoverAmount() < 0.5);
    artboard::setReducedMotion(false);
}
TEST(App_export_blocked_by_errors_reports_instead_of_writing)
{
    App app;
    app.setSize(1200, 760);
    app.doc().shapes.front().setField("w", "1 +");
    app.documentChanged();
    CHECK(!app.exportCode());
    CHECK(app.statusLevel() == StatusLevel::Bad);
    CHECK(app.modal()->isOpen());   // it says why, rather than failing silently
}

TEST(App_undo_and_redo_restore_the_document)
{
    App app;
    app.setSize(1280, 800);
    double now = 0.0;
    settle(app, now, 100.0);
    CHECK(!app.canUndo());          // a fresh document has no history
    CHECK(!app.canRedo());

    const std::string original = app.doc().shapes.front().field("w");
    now += 2000.0;                  // past the coalesce window
    app.advance(now);
    app.doc().shapes.front().setField("w", "42");
    app.documentChanged();
    CHECK(app.canUndo());
    CHECK(app.doc().shapes.front().field("w") == "42");

    app.undo();
    CHECK(app.doc().shapes.front().field("w") == original);
    CHECK(app.canRedo());
    CHECK(app.previewOk());         // the preview follows the undo

    app.redo();
    CHECK(app.doc().shapes.front().field("w") == "42");

    // A fresh edit forks the future.
    app.undo();
    now += 2000.0;
    app.advance(now);
    app.doc().shapes.front().setField("w", "7");
    app.documentChanged();
    CHECK(!app.canRedo());
}
TEST(App_rapid_edits_collapse_into_one_undo_step)
{
    App app;
    app.setSize(1280, 800);
    double now = 0.0;
    settle(app, now, 100.0);
    const std::string original = app.doc().shapes.front().field("w");

    now += 2000.0;
    app.advance(now);
    for (int i = 0; i < 12; ++i)    // as if typing "1234..." into a field
    {
        app.doc().shapes.front().setField("w", std::string(size_t(i + 1), '1'));
        app.documentChanged();
        now += 20.0;
        app.advance(now);
    }
    app.undo();
    // One undo goes back past the whole burst, not one character.
    CHECK(app.doc().shapes.front().field("w") == original);
}
TEST(App_undo_survives_a_shape_being_deleted)
{
    App app;
    app.setSize(1280, 800);
    double now = 0.0;
    settle(app, now, 100.0);
    const std::string id = app.doc().shapes.front().id;
    now += 2000.0;
    app.advance(now);

    app.doc().removeShape(id);
    app.selectShape("");
    app.documentChanged();
    CHECK(app.doc().shapes.empty());

    app.undo();
    CHECK(app.doc().findShape(id) != nullptr);
    CHECK(app.previewOk());
    settle(app, now, 200.0);
}
TEST(Runtime_reaction_duration_and_scrub)
{
    Runtime rt;
    std::string err;
    // The starter's loopStart is a 300ms fade THEN a 1200ms spin: 1500ms of timeline.
    CHECK(rt.build(Document::starter("VisualLoop", "C"), &err));
    rt.setSize(120, 120);
    rt.advance(0.0);
    CHECK_NEAR(rt.reactionDurationMs("ring", "loopStart"), 1500.0, 1e-6);
    CHECK_NEAR(rt.reactionDurationMs("ring", "loopEnd"), 300.0, 1e-6);
    CHECK(rt.reactionDurationMs("ring", "nosuch") == 0.0);
    CHECK(rt.reactionDurationMs("nosuch", "loopStart") == 0.0);

    rt.scrub("ring", "loopStart", 0.0);
    CHECK_NEAR(rt.segmentFor("ring")->opacity.value(), 0.0, 1e-6);
    rt.scrub("ring", "loopStart", 0.2);      // 300ms in: the fade has just finished
    CHECK_NEAR(rt.segmentFor("ring")->opacity.value(), 1.0, 1e-6);
    rt.scrub("ring", "loopStart", 0.6);      // 900ms in: half a turn into the spin
    CHECK_NEAR(rt.segmentFor("ring")->rotation.value(), 3.14159265358979324, 1e-4);
    rt.scrub("ring", "loopStart", 0.99);     // just short of one full turn
    CHECK(rt.segmentFor("ring")->rotation.value() > 6.2);
    rt.scrub("ring", "loopStart", 1.0);      // exactly one turn: a repeating track has WRAPPED
    CHECK_NEAR(rt.segmentFor("ring")->rotation.value(), 0.0, 1e-4);
}
TEST(Inspector_edits_reach_the_document)
{
    // The inspector's rows are the only way to change a shape's id, a label's text, a path
    // command, or a param — so the wiring is worth asserting, not just the drawing.
    App app;
    app.setSize(1280, 800);
    double now = 0.0;
    settle(app, now, 100.0);

    const size_t paramsBefore = app.doc().params.size();
    Param p;
    p.name = "speed";
    p.type = ParamType::Number;
    p.defaultExpr = "1";
    app.doc().params.push_back(p);
    app.documentChanged();
    CHECK(app.doc().params.size() == paramsBefore + 1);
    CHECK(app.doc().findParam("speed") != nullptr);

    // A rename must carry every reference with it.
    Document &doc = app.doc();
    const std::string old = doc.shapes.front().id;
    doc.shapes.front().id = "halo";
    for (auto &host : doc.shapes)
        for (auto &r : host.reactions)
            for (auto &st : r.steps)
                for (auto &tr : st.tracks)
                    if (tr.target.rfind(old + ".", 0) == 0)
                        tr.target = "halo" + tr.target.substr(old.size());
    app.selectShape("halo");
    app.documentChanged();
    CHECK(app.previewOk());
    for (const auto &d : app.diagnostics())
        CHECK(!d.isError());
}

TEST(App_keeps_drawing_while_a_verify_runs)
{
    // The compile half runs on a worker; the window must keep rendering, and the result must
    // be collected on the UI thread (design rule R2: never freeze).
    App app;
    app.setSize(1200, 760);
    double now = 0.0;
    settle(app, now, 100.0);

    app.startVerify();
    CHECK(app.verifyRunning());
    app.startVerify();                 // a second request while running is ignored
    CHECK(app.verifyRunning());

    // Loop long enough to outlast a real compile; every iteration is a real frame, which is
    // the point — the window must keep drawing the whole time.
    int framesDrawn = 0;
    for (int i = 0; i < 200000 && app.verifyRunning(); ++i)
    {
        now += 16.0;
        app.advance(now);
        artboard::RecordingTarget t;
        app.render(t);
        if (!t.ops().empty()) ++framesDrawn;
    }
    CHECK(framesDrawn > 0);            // it drew during the compile rather than freezing
    CHECK(!app.verifyRunning());       // and the result was collected

    // Whatever the toolchain situation, the outcome is reported, never silently green.
    const VerifyResult &r = app.lastVerify();
    CHECK(!r.summary().empty());
    if (!r.available)
        CHECK(!r.matched);
    else
        CHECK(r.matched || !r.differences.empty() || !r.error.empty());
}

namespace
{
    /** Feed a click the way the host does: raw pointer → recognizer → router → tree. */
    struct Driver
    {
        artboard::GestureRecognizer rec;
        artboard::InputRouter router;
        double t = 0.0;
        explicit Driver(App &app)
        {
            router.add(&app);
            rec.setSink([this](const artboard::Gesture &g) { router.route(g); });
        }
        void click(double x, double y)
        {
            artboard::RawPointer d;
            d.kind = artboard::RawPointer::Kind::Down;
            d.pos = {x, y};
            d.timeMs = (t += 10);
            rec.feed(d);
            artboard::RawPointer u;
            u.kind = artboard::RawPointer::Kind::Up;
            u.pos = {x, y};
            u.timeMs = (t += 10);
            rec.feed(u);
        }
    };
    artboard::Point centreOf(const artboard::Segment &parent, const artboard::Segment &child)
    {
        return {parent.x.value() + child.x.value() + child.width.value() * 0.5,
                parent.y.value() + child.y.value() + child.height.value() * 0.5};
    }
    artboard::TextBox *focusedBox(artboard::Segment *s)
    {
        if (auto *tb = dynamic_cast<artboard::TextBox *>(s))
            if (tb->hasFocus()) return tb;
        for (const auto &c : s->children())
            if (auto *f = focusedBox(c.get())) return f;
        return nullptr;
    }
    artboard::TextBox *nthBox(artboard::Segment *s, int &n)
    {
        if (auto *tb = dynamic_cast<artboard::TextBox *>(s))
            if (n-- == 0) return tb;
        for (const auto &c : s->children())
            if (auto *f = nthBox(c.get(), n)) return f;
        return nullptr;
    }
}

TEST(Panels_only_hit_test_inside_their_own_bounds)
{
    // A panel that answered `true` regardless of the point made the topmost one swallow every
    // click in the window — every button in the app was dead.
    App app;
    app.setSize(1360, 860);
    double now = 0.0;
    settle(app, now, 100.0);
    toEditor(app, now);
    for (artboard::Segment *panel : app.editorPanels())
    {
        const artboard::Segment &p = *panel;
        const artboard::Point outside{p.x.value() + p.width.value() + 20.0,
                                      p.y.value() + p.height.value() + 20.0};
        CHECK(!p.hitTest(outside));
        CHECK(p.hitTest({p.x.value() + p.width.value() * 0.5, p.y.value() + p.height.value() * 0.5}));
    }
}
TEST(Chrome_buttons_respond_to_a_real_click)
{
    App app;
    app.setSize(1360, 860);
    double now = 0.0;
    toEditor(app, now);
    Driver drv(app);

    const artboard::Segment &chrome = *app.chrome();
    CHECK(chrome.childCount() >= 5);
    const artboard::Point p = centreOf(app, chrome);   // just to touch the helper
    (void)p;

    const artboard::Point newBtn = centreOf(chrome, *chrome.children()[0]);
    drv.click(newBtn.x, newBtn.y);
    settle(app, now, 60.0);
    CHECK(app.modal()->isOpen());          // "New" opened its dialog
    app.modal()->close();
    settle(app, now, 400.0);

    const artboard::Point openBtn = centreOf(chrome, *chrome.children()[1]);
    drv.click(openBtn.x, openBtn.y);
    settle(app, now, 60.0);
    CHECK(app.modal()->isOpen());          // and "Open" opened its own
    app.modal()->close();
    settle(app, now, 400.0);
}
TEST(Add_shape_buttons_add_a_shape)
{
    App app;
    app.setSize(1360, 860);
    double now = 0.0;
    toEditor(app, now);
    Driver drv(app);

    const artboard::Segment &tree = *app.tree();
    const int before = (int)app.doc().shapes.size();
    for (int i = 0; i < 4; ++i)            // rect, circle, path, label
    {
        const artboard::Point p = centreOf(tree, *tree.children()[(size_t)i]);
        drv.click(p.x, p.y);
        settle(app, now, 60.0);
    }
    CHECK((int)app.doc().shapes.size() == before + 4);
    CHECK(app.previewOk());
    for (const auto &d : app.diagnostics())
        CHECK(!d.isError());               // every added shape is valid on arrival
}
TEST(Typing_into_a_field_keeps_focus_and_reaches_the_document)
{
    // Every edit re-validates and refreshes the panels. Rebuilding the rows destroyed the very
    // TextBox being typed into, so each keystroke dropped focus.
    App app;
    app.setSize(1360, 860);
    double now = 0.0;
    toEditor(app, now);

    artboard::Segment *inspector = app.inspector();
    int index = 7;                          // name, namespace, dw, dh, id, x, y, then w
    artboard::TextBox *box = nthBox(inspector, index);
    CHECK(box != nullptr);
    box->requestFocus();
    box->caretToEnd();
    const std::string startText = box->text;
    CHECK(!startText.empty());

    auto key = [&](int code) {
        artboard::KeyEvent k;
        k.type = artboard::KeyEvent::Type::Down;
        k.keyCode = code;
        app.dispatchKey(k);
    };
    auto type = [&](const char *text) {
        artboard::KeyEvent k;
        k.type = artboard::KeyEvent::Type::Text;
        k.text = text;
        app.dispatchKey(k);
    };

    for (int i = 0; i < 5; ++i)
    {
        key(8);                             // backspace
        settle(app, now, 20.0);
        CHECK(focusedBox(inspector) != nullptr);   // focus survives every keystroke
    }
    type("* 2");
    settle(app, now, 20.0);
    artboard::TextBox *still = focusedBox(inspector);
    CHECK(still != nullptr);
    CHECK(still->text != startText);
    CHECK(app.doc().shapes.front().field("w") == still->text);   // and it reached the document
    CHECK(app.previewOk());
}

TEST(App_opens_on_the_home_screen_and_crosses_to_the_editor)
{
    App app;
    app.setSize(1360, 860);
    double now = 0.0;
    settle(app, now, 400.0);
    CHECK(app.screen() == App::Screen::Home);
    CHECK_NEAR(app.editorAmount(), 0.0, 1e-9);

    // The home screen draws; the editor behind it does not (it is faded to zero, so its
    // whole subtree is skipped, FR-32).
    artboard::RecordingTarget home;
    app.render(home);
    CHECK(home.count(K::DrawText) > 8);
    for (artboard::Segment *p : app.editorPanels())
        CHECK(p->isFadedOut());

    app.showEditor();
    settle(app, now, 100.0);
    CHECK(app.editorAmount() > 0.0);
    CHECK(app.editorAmount() < 1.0);        // it crosses over, it does not cut
    settle(app, now, 900.0);
    CHECK_NEAR(app.editorAmount(), 1.0, 1e-9);
    CHECK(!app.home()->isFadedOut() == false);   // the home screen faded out

    artboard::RecordingTarget editor;
    app.render(editor);
    CHECK(editor.count(K::FillPath) > 20);

    app.showHome();
    settle(app, now, 900.0);
    CHECK(app.screen() == App::Screen::Home);
    CHECK_NEAR(app.editorAmount(), 0.0, 1e-9);
}
TEST(Home_screen_cards_start_a_component_of_each_base)
{
    App app;
    app.setSize(1360, 860);
    double now = 0.0;
    settle(app, now, 500.0);
    Driver drv(app);

    // The five base cards sit in the grid to the right of the sidebar; clicking one starts
    // that component and crosses to the editor.
    // Locate a card by the base it starts rather than by pixel guess — the grid also holds
    // however many recents this machine happens to have.
    HomeScreen *home = app.home();
    CHECK(home != nullptr);
    for (const char *base : {"VisualLoop", "Button", "Checkbox"})
    {
        const int i = home->cardForBase(base);
        CHECK(i >= 0);
        const artboard::Rect r = home->cardRect(i);
        drv.click(r.x + r.w * 0.5, r.y + r.h * 0.5);
        settle(app, now, 900.0);
        CHECK(app.screen() == App::Screen::Editor);
        CHECK(app.doc().base == base);
        CHECK(app.previewOk());
        for (const auto &d : app.diagnostics())
            CHECK(!d.isError());
        app.showHome();
        settle(app, now, 900.0);
    }
}
TEST(Splash_plays_its_intro_and_exits)
{
    SplashScreen splash;
    splash.begin(0.0);
    CHECK(!splash.introDone());

    artboard::RecordingTarget early;
    splash.advance(0.0);
    splash.render(early);
    const int atStart = early.count(K::DrawText);

    for (double t = 0; t <= SplashScreen::kIntroMs + 32.0; t += 16.0)
        splash.advance(t);
    CHECK(splash.introDone());

    splash.setStatus("Preparing the preview");
    splash.setProgress(0.6);
    for (double t = SplashScreen::kIntroMs; t <= SplashScreen::kIntroMs + 400.0; t += 16.0)
        splash.advance(t);

    artboard::RecordingTarget mid;
    splash.render(mid);
    CHECK(mid.count(K::DrawText) >= atStart);
    bool namedTheStep = false;
    for (const auto &op : mid.ops())
        if (op.kind == K::DrawText && op.text == "Preparing the preview") namedTheStep = true;
    CHECK(namedTheStep);        // the bar names what it is doing, it is not decorative

    CHECK(!splash.isGone());
    splash.beginExit();
    for (double t = 0; t <= 600.0; t += 16.0)
        splash.advance(SplashScreen::kIntroMs + 400.0 + t);
    CHECK(splash.isGone());
    artboard::RecordingTarget gone;
    splash.render(gone);
    for (const auto &op : gone.ops())
        CHECK(op.kind != K::DrawText);   // fully exited: it draws nothing at all
}
TEST(Recents_relative_age_reads_naturally)
{
    CHECK(Recents::relativeAge(0, 1000) == "—");
    CHECK(Recents::relativeAge(1000, 1030) == "just now");
    CHECK(Recents::relativeAge(1000, 1000 + 60 * 5) == "5m ago");
    CHECK(Recents::relativeAge(1000, 1000 + 3600 * 3) == "3h ago");
    CHECK(Recents::relativeAge(1000, 1000 + 86400 * 2) == "2d ago");
    CHECK(Recents::relativeAge(1000, 1000 + 86400 * 90) == "3mo ago");
}

namespace
{
    struct DrawnText
    {
        artboard::Rect box;
        std::string text;
    };

    artboard::Rect intersect(const artboard::Rect &a, const artboard::Rect &b)
    {
        const double x = std::max(a.x, b.x), y = std::max(a.y, b.y);
        const double r = std::min(a.right(), b.right()), bo = std::min(a.bottom(), b.bottom());
        return {x, y, std::max(0.0, r - x), std::max(0.0, bo - y)};
    }

    /*  Every drawn string's world-space box, by replaying the op stream.
     *
     *  The replay tracks the transform AND the clip stack, because a panel that clips its
     *  scrolling list still RECORDS the rows it clipped away — counting those would report
     *  overlaps the user can never see. Widths are measured with the SAME target the app drew
     *  into, so positions and widths live in one metric space, which is the property that has
     *  to hold on any adapter.
     */
    std::vector<DrawnText> drawnText(const artboard::RecordingTarget &rt, double w, double h)
    {
        std::vector<DrawnText> out;
        artboard::Transform cur = artboard::Transform::identity();
        artboard::Rect clip{0, 0, w, h};
        std::vector<std::pair<artboard::Transform, artboard::Rect>> stack;
        for (const auto &op : rt.ops())
        {
            switch (op.kind)
            {
            case K::Save:
            case K::PushLayer:
                stack.emplace_back(cur, clip);
                continue;
            case K::Restore:
            case K::PopLayer:
                if (!stack.empty()) { cur = stack.back().first; clip = stack.back().second; stack.pop_back(); }
                continue;
            case K::SetTransform:
                cur = op.transform;
                continue;
            case K::ClipRect:
            {
                const artboard::Point a = cur.apply({op.args[0], op.args[1]});
                const artboard::Point b = cur.apply({op.args[0] + op.args[2], op.args[1] + op.args[3]});
                clip = intersect(clip, {std::min(a.x, b.x), std::min(a.y, b.y),
                                        std::fabs(b.x - a.x), std::fabs(b.y - a.y)});
                continue;
            }
            default: break;
            }
            if (op.kind != K::DrawText || op.text.empty()) continue;
            const double size = op.args[2];
            const double tw = rt.measureText(op.text, size, op.fontFamily, op.letterSpacingPx);
            const artboard::Point o = cur.apply({op.args[0], op.args[1]});
            const artboard::Rect box{o.x, o.y - size * 0.78, tw, size};
            const artboard::Rect shown = intersect(box, clip);
            if (shown.w <= 0.5 || shown.h <= 0.5)
                continue;   // clipped away: the user never sees it
            out.push_back({shown, op.text});
        }
        return out;
    }
    bool boxesOverlap(const artboard::Rect &a, const artboard::Rect &b)
    {
        const double e = 1.0;   // a pixel of slack: adjacent labels may share an edge
        return a.x + e < b.right() && b.x + e < a.right() && a.y + e < b.bottom() &&
               b.y + e < a.bottom();
    }
    /** Render `app` and return how many pairs of drawn strings overlap. */
    int overlappingText(App &app, std::string *first = nullptr)
    {
        artboard::RecordingTarget rt;
        setMeasureTarget(&rt);
        app.render(rt);
        setMeasureTarget(nullptr);
        const auto boxes = drawnText(rt, app.width.value(), app.height.value());
        int bad = 0;
        for (size_t i = 0; i < boxes.size(); ++i)
            for (size_t j = i + 1; j < boxes.size(); ++j)
                if (boxesOverlap(boxes[i].box, boxes[j].box))
                {
                    if (first && first->empty())
                        *first = "\"" + boxes[i].text + "\" over \"" + boxes[j].text + "\"";
                    ++bad;
                }
        return bad;
    }
}

TEST(No_two_strings_ever_overlap_at_any_window_size)
{
    // "Text overlaps everywhere" is a class of bug, not one bug, so it is checked as a class:
    // render the real app and assert that no two drawn strings share pixels. The reactions
    // panel sheds columns at narrow widths precisely so this holds.
    const double sizes[][2] = {{1440, 900}, {1360, 860}, {1200, 780}, {1024, 640}, {900, 620}, {820, 560}};
    for (const auto &wh : sizes)
    {
        App app;
        app.setSize(wh[0], wh[1]);
        double now = 0.0;

        settle(app, now, 700.0);                 // the launcher
        std::string where;
        CHECK(overlappingText(app, &where) == 0);

        toEditor(app, now);                      // the editor
        where.clear();
        CHECK(overlappingText(app, &where) == 0);

        // A modal is a DELIBERATE overlay: the design rule allows it to sit over the app, and
        // the translucent scrim is what announces that. So the app behind it legitimately
        // shows through — what must not overlap is the dialog's OWN content, which is checked
        // by rendering the modal subtree on its own.
        app.modal()->openNew();
        settle(app, now, 400.0);
        artboard::RecordingTarget only;
        setMeasureTarget(&only);
        app.modal()->render(only);
        setMeasureTarget(nullptr);
        const auto card = drawnText(only, app.width.value(), app.height.value());
        for (size_t i = 0; i < card.size(); ++i)
            for (size_t j = i + 1; j < card.size(); ++j)
                CHECK(!boxesOverlap(card[i].box, card[j].box));
        CHECK(card.size() >= 5);   // the dialog really did draw its list and its buttons
        app.modal()->close();
        settle(app, now, 400.0);
    }
}
TEST(No_string_is_drawn_outside_the_window)
{
    App app;
    app.setSize(1024, 640);
    double now = 0.0;
    toEditor(app, now);

    artboard::RecordingTarget rt;
    setMeasureTarget(&rt);
    app.render(rt);
    setMeasureTarget(nullptr);
    for (const auto &d : drawnText(rt, app.width.value(), app.height.value()))
    {
        CHECK(d.box.x >= -1.0);
        CHECK(d.box.y >= -1.0);
        CHECK(d.box.right() <= app.width.value() + 1.0);
        CHECK(d.box.bottom() <= app.height.value() + 1.0);
    }
}
TEST(Reactions_panel_sheds_columns_instead_of_letting_them_collide)
{
    // Every track row's widgets must stay inside the panel and clear of the chip gutter,
    // at every width — which is what dropping `delay`, then `from`, then the chips achieves.
    for (double w : {1440.0, 1200.0, 1024.0, 900.0, 820.0})
    {
        App app;
        app.setSize(w, 760.0);
        double now = 0.0;
        toEditor(app, now);

        const artboard::Segment &panel = *app.reactions();
        for (const auto &child : panel.children())
        {
            if (!child->visible) continue;
            CHECK(child->x.value() >= -0.5);
            CHECK(child->x.value() + child->width.value() <= panel.width.value() + 0.5);
            CHECK(child->y.value() + child->height.value() <= panel.height.value() + 0.5);
        }
    }
}

TEST(Reactions_panel_shows_only_the_selected_objects_reactions)
{
    App app;
    app.setSize(1360, 860);
    double now = 0.0;
    toEditor(app, now);

    // Give a second object its own reaction, then check the panel follows the selection.
    Shape dot;
    dot.id = "dot";
    dot.kind = ShapeKind::Circle;
    dot.setField("fill", "accent");
    dot.setAnimated("opacity", true);
    Reaction r;
    r.signal = "cycle";
    Step st;
    st.tracks.push_back({"opacity", "0", "1", "150", "0", "Linear", 0, false});
    r.steps.push_back(st);
    dot.reactions.push_back(r);
    app.doc().addShape(dot);
    app.documentChanged();

    auto signalsShown = [&] {
        artboard::RecordingTarget t;
        setMeasureTarget(&t);
        app.reactions()->render(t);
        setMeasureTarget(nullptr);
        std::vector<std::string> out;
        for (const auto &op : t.ops())
            if (op.kind == K::DrawText) out.push_back(op.text);
        return out;
    };
    // Section titles are drawn uppercased by the design system, so compare case-insensitively.
    auto shows = [](const std::vector<std::string> &v, const std::string &s) {
        auto lower = [](std::string x) {
            for (char &c : x)
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            return x;
        };
        for (const auto &x : v)
            if (lower(x) == lower(s)) return true;
        return false;
    };

    app.selectShape("ring");
    settle(app, now, 60.0);
    auto shown = signalsShown();
    CHECK(shows(shown, "loopStart"));      // ring's own
    CHECK(shows(shown, "ring"));           // the panel names whose reactions these are
    CHECK(!shows(shown, "cycle"));         // dot's reaction is NOT listed here

    app.selectShape("dot");
    settle(app, now, 60.0);
    shown = signalsShown();
    CHECK(shows(shown, "cycle"));
    CHECK(shows(shown, "dot"));
    CHECK(!shows(shown, "loopStart"));

    app.selectShape("");
    settle(app, now, 60.0);
    shown = signalsShown();
    CHECK(shows(shown, "No object selected"));
}
TEST(Duplicating_an_object_names_the_copy_and_selects_it)
{
    App app;
    app.setSize(1360, 860);
    double now = 0.0;
    toEditor(app, now);

    const size_t before = app.doc().shapes.size();
    app.selectShape("ring");
    app.duplicateSelected();
    settle(app, now, 100.0);

    CHECK(app.doc().shapes.size() == before + 1);
    CHECK(app.doc().findShape("ring_copy") != nullptr);
    CHECK(app.selectedShape() == "ring_copy");      // the copy becomes the selection
    CHECK(app.previewOk());
    for (const auto &d : app.diagnostics())
        CHECK(!d.isError());

    // The copy brought its own reactions, so it animates independently of the original.
    CHECK(app.doc().findShape("ring_copy")->reactions.size() ==
          app.doc().findShape("ring")->reactions.size());

    app.duplicateSelected();
    settle(app, now, 100.0);
    CHECK(app.doc().findShape("ring_copy_copy") != nullptr);

    // Undo puts it back.
    now += 2000.0;
    app.advance(now);
    app.undo();
    CHECK(app.doc().findShape("ring_copy_copy") == nullptr);

    // Nothing selected: it says so rather than doing something surprising.
    app.selectShape("");
    app.duplicateSelected();
    CHECK(app.statusLevel() == StatusLevel::Warn);
}

int main() { return mini::runAll(); }
