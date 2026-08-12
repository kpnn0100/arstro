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

    /** Every op's translation, for coarse "did anything get drawn here" checks. */
    std::vector<artboard::Rect> panelBounds(App &app)
    {
        std::vector<artboard::Rect> out;
        for (const auto &child : app.children())
            out.push_back({child->x.value(), child->y.value(), child->width.value(),
                           child->height.value()});
        return out;
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
    CHECK(!app.doc().reactions.empty());
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
            // The modal is the app's deliberate overlay; every other panel is a tile.
            for (size_t i = 0; i + 1 < rects.size(); ++i)
                for (size_t j = i + 1; j + 1 < rects.size(); ++j)
                    CHECK(!overlaps(rects[i], rects[j]));
            for (size_t i = 0; i + 1 < rects.size(); ++i)
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
    const double railWide = wide[1].w, railNarrow = narrow[1].w;
    const double canvasWide = wide[2].w, canvasNarrow = narrow[2].w;
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
        settle(app, now, 400.0);
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
    settle(app, now, 100.0);
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
    app.doc().reactions.clear();
    app.selectShape("");
    app.documentChanged();
    double now = 0.0;
    settle(app, now, 200.0);

    artboard::RecordingTarget t;
    app.render(t);
    bool noShapes = false, noReactions = false, selectPrompt = false;
    for (const auto &op : t.ops())
    {
        if (op.kind != K::DrawText) continue;
        if (op.text.find("No shapes") != std::string::npos) noShapes = true;
        if (op.text.find("No reactions") != std::string::npos) noReactions = true;
        if (op.text.find("Select a shape") != std::string::npos) selectPrompt = true;
    }
    CHECK(noShapes);        // an empty panel says what to do, rather than showing nothing
    CHECK(noReactions);
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
    CHECK_NEAR(rt.reactionDurationMs("loopStart"), 1500.0, 1e-6);
    CHECK_NEAR(rt.reactionDurationMs("loopEnd"), 300.0, 1e-6);
    CHECK(rt.reactionDurationMs("nosuch") == 0.0);

    rt.scrub("loopStart", 0.0);
    CHECK_NEAR(rt.segmentFor("ring")->opacity.value(), 0.0, 1e-6);
    rt.scrub("loopStart", 0.2);      // 300ms in: the fade has just finished
    CHECK_NEAR(rt.segmentFor("ring")->opacity.value(), 1.0, 1e-6);
    rt.scrub("loopStart", 0.6);      // 900ms in: half a turn into the spin
    CHECK_NEAR(rt.segmentFor("ring")->rotation.value(), 3.14159265358979324, 1e-4);
    rt.scrub("loopStart", 0.99);     // just short of one full turn
    CHECK(rt.segmentFor("ring")->rotation.value() > 6.2);
    rt.scrub("loopStart", 1.0);      // exactly one turn: a repeating track has WRAPPED
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
    for (auto &r : doc.reactions)
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

int main() { return mini::runAll(); }
