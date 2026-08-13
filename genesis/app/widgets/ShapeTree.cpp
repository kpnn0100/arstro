#include "ShapeTree.h"
#include "../App.h"
#include <cmath>
#include <functional>

namespace genesis
{
namespace ui
{
    namespace
    {
        constexpr double kAddH = 24.0;
        const char *kKindLabels[] = {"Rect", "Circle", "Path", "Label"};
        const ShapeKind kKinds[] = {ShapeKind::Rect, ShapeKind::Circle, ShapeKind::Path, ShapeKind::Label};

        /** A new shape of `kind`, pre-bound to something visible — a shape you cannot see is
         *  not a starting point (design rule: draw the empty state, not a blank). */
        Shape makeShape(ShapeKind kind, const Document &doc)
        {
            Shape s;
            s.kind = kind;
            s.id = doc.uniqueShapeId(shapeKindName(kind));
            s.setField("w", "minSide * 0.4");
            s.setField("h", "minSide * 0.4");
            s.setField("x", "(w - self.w) / 2");
            s.setField("y", "(h - self.h) / 2");
            if (kind == ShapeKind::Label)
            {
                s.text = "Label";
                s.setField("fill", "theme.foreground");
                s.setField("fontSize", "14");
            }
            else if (kind == ShapeKind::Path)
            {
                s.setField("stroke", "theme.accent");
                s.setField("strokeWidth", "2");
                s.path.push_back({'M', {"0", "self.h"}});
                s.path.push_back({'L', {"self.w / 2", "0"}});
                s.path.push_back({'L', {"self.w", "self.h"}});
            }
            else
            {
                s.setField("fill", "theme.accent");
                if (kind == ShapeKind::Rect)
                    s.setField("cornerRadius", "4");
            }
            return s;
        }
    }

    ShapeTree::ShapeTree(App &app) : mApp(app)
    {
        for (int i = 0; i < 4; ++i)
        {
            auto b = std::make_shared<artboard::Button>(kKindLabels[i], theme().button);
            b->height.set(kAddH);
            b->focusable = true;
            App *a = &mApp;
            const ShapeKind kind = kKinds[i];
            b->onClick = [a, kind] {
                Shape s = makeShape(kind, a->doc());
                const std::string id = s.id;
                // A new shape goes INSIDE the selection when one is a container, so building a
                // group is the default gesture rather than a separate reparent step.
                if (!a->selectedShape().empty())
                    s.parent = a->selectedShape();
                a->doc().addShape(s);
                a->documentChanged();
                a->selectShape(id);
                a->status("Added " + id, StatusLevel::Good);
            };
            addChild(b);
            mAdd.push_back(b);
        }
        App *app_ = &mApp;
        mDuplicate = std::make_shared<artboard::Button>("Duplicate", theme().button);
        mDuplicate->height.set(kAddH);
        mDuplicate->focusable = true;
        mDuplicate->onClick = [app_] { app_->duplicateSelected(); };
        addChild(mDuplicate);

        mDelete = std::make_shared<artboard::Button>("Delete", theme().button);
        mDelete->height.set(kAddH);
        mDelete->focusable = true;
        App *a = &mApp;
        mDelete->onClick = [a] {
            const std::string id = a->selectedShape();
            if (id.empty())
            {
                a->status("Select a shape to delete", StatusLevel::Warn);
                return;
            }
            a->doc().removeShape(id);
            a->selectShape("");
            a->documentChanged();
            a->status("Deleted " + id, StatusLevel::Good);
        };
        addChild(mDelete);
        refresh();
    }

    void ShapeTree::refresh()
    {
        mRows.clear();
        // Depth-first from the roots, so a child always appears under (and indented from)
        // its parent — the tree reads as a tree, not as a flat list with a parent column.
        std::function<void(const std::string &, int)> walk = [&](const std::string &parent, int depth) {
            for (const auto &id : mApp.doc().childrenOf(parent))
            {
                mRows.push_back({id, depth});
                walk(id, depth + 1);
            }
        };
        walk("", 0);
    }

    double ShapeTree::listTop() const { return metrics::pad() + 22.0; }

    /** Top of the fixed footer block (title + delete + the four add buttons). */
    double ShapeTree::footerTop() const
    {
        return height.value() - metrics::pad() - kAddH * 3.0 - 16.0 - 18.0;
    }

    void ShapeTree::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        const double pad = metrics::pad();
        const double innerW = w - pad * 2.0;
        const double cell = (innerW - 6.0) / 2.0;
        // The footer is one block measured from the bottom: two rows of add buttons, the
        // delete button above them, and the section title above that. Nothing is placed at a
        // coordinate another element can also land on.
        const double addTop = h - pad - kAddH * 2.0 - 6.0;
        for (int i = 0; i < 4; ++i)
        {
            mAdd[(size_t)i]->x.set(pad + (i % 2) * (cell + 6.0));
            mAdd[(size_t)i]->y.set(addTop + (i / 2) * (kAddH + 6.0));
            mAdd[(size_t)i]->width.set(cell);
        }
        mDuplicate->x.set(pad);
        mDuplicate->y.set(addTop - kAddH - 10.0);
        mDuplicate->width.set(cell);
        mDelete->x.set(pad + cell + 6.0);
        mDelete->y.set(addTop - kAddH - 10.0);
        mDelete->width.set(cell);
    }

    int ShapeTree::rowAt(double localY) const
    {
        const double top = listTop() - mScroll.value();
        const int i = (int)std::floor((localY - top) / metrics::rowH());
        return (i >= 0 && i < (int)mRows.size()) ? i : -1;
    }

    void ShapeTree::advance(double nowMs)
    {
        mNowMs = nowMs;
        mHover.advance(nowMs);
        const double dt = 1.0 / 60.0;
        mScroll.setTarget(mScrollTarget);
        mScroll.advance(dt, artboard::motion::kSpatialFast);
        Segment::advance(nowMs);
    }

    bool ShapeTree::handleGesture(const artboard::Gesture &g, const artboard::Point &p)
    {
        if (g.type == artboard::Gesture::Type::Move)
        {
            mHover.setHovered(rowAt(p.y));
            return true;
        }
        if (g.type == artboard::Gesture::Type::Down || g.type == artboard::Gesture::Type::Click)
        {
            const int row = rowAt(p.y);
            if (row >= 0)
            {
                mApp.selectShape(mRows[(size_t)row].id);
                return true;
            }
            if (g.type == artboard::Gesture::Type::Click)
                mApp.selectShape("");   // clicking the empty area deselects
            return true;
        }
        if (g.type == artboard::Gesture::Type::Drag)
        {
            // Scroll the list by dragging it, with the same kinetic feel as a ScrollView.
            const double maxScroll = std::max(
                0.0, (double)mRows.size() * metrics::rowH() - (footerTop() - listTop()));
            mScrollTarget = std::min(maxScroll, std::max(0.0, mScrollTarget - (p.y - g.start.y) * 0.35));
            return true;
        }
        return artboard::Segment::handleGesture(g, p);
    }

    void ShapeTree::onPaint(artboard::IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        artboard::drawRoundedRect(t, {0, 0, w, h}, 0.0, artboard::Paint::filled(palette::railBg()));
        artboard::drawRoundedRect(t, {w - 1, 0, 1, h}, 0.0, artboard::Paint::filled(palette::border()));

        const double pad = metrics::pad();
        drawSectionTitle(t, "Shapes", pad, pad + 10.0);

        const double listBottom = footerTop() - 8.0;
        t.save();
        t.clipRect(0, listTop() - 4.0, w, listBottom - listTop() + 4.0);
        double y = listTop() - mScroll.value();
        const double rowH = metrics::rowH();

        if (mRows.empty())
        {
            // Empty state: say what to do, don't just show nothing.
            drawFitted(t, "No shapes yet", pad, y + 14.0, w - pad * 2.0, type::small(),
                       palette::mutedForeground(), font::sans());
            drawFitted(t, "Add one below to start drawing.", pad, y + 30.0, w - pad * 2.0,
                       type::micro(), palette::mutedForeground(), font::sans());
        }

        for (int i = 0; i < (int)mRows.size(); ++i, y += rowH)
        {
            if (y + rowH < listTop() - rowH || y > listBottom) continue;
            const Row &r = mRows[(size_t)i];
            const bool selected = r.id == mApp.selectedShape();
            const double hover = mHover.amount(i);
            if (selected)
                artboard::drawRoundedRect(t, {4, y, w - 8, rowH - 2}, radius::hairline(),
                                          artboard::Paint::filled(palette::selectedWash(1.0)));
            else if (hover > 0.01)
                artboard::drawRoundedRect(t, {4, y, w - 8, rowH - 2}, radius::hairline(),
                                          artboard::Paint::filled(palette::hoverWash(hover)));

            const double indent = pad + r.depth * 12.0;
            const Shape *s = mApp.doc().findShape(r.id);
            // Kind glyph: a filled dot for a circle, a square for a rect, a tick for a path,
            // a bar for a label — enough to scan the tree without reading every name.
            const artboard::Color glyph = selected ? palette::primary() : palette::mutedForeground();
            const double gy = y + rowH * 0.5;
            if (s && s->kind == ShapeKind::Circle)
                artboard::drawCircle(t, indent + 4.0, gy, 4.0, artboard::Paint::filled(glyph));
            else if (s && s->kind == ShapeKind::Rect)
                artboard::drawRoundedRect(t, {indent, gy - 4, 8, 8}, 1.5, artboard::Paint::filled(glyph));
            else if (s && s->kind == ShapeKind::Label)
                artboard::drawRoundedRect(t, {indent, gy - 3, 8, 2}, 1.0, artboard::Paint::filled(glyph));
            else
            {
                t.setStroke(glyph, 1.5);
                t.beginPath();
                t.moveTo(indent, gy + 3);
                t.lineTo(indent + 4, gy - 3);
                t.lineTo(indent + 8, gy + 3);
                t.strokePath();
            }

            const double textX = indent + 16.0;
            const double animCount = s ? (double)s->animated.size() : 0.0;
            const double badgeW = animCount > 0 ? 22.0 : 0.0;
            drawFitted(t, r.id, textX, centreBaseline(y, rowH, type::small()),
                       w - textX - pad - badgeW, type::small(),
                       selected ? palette::foreground() : palette::secondaryForeground(),
                       selected ? font::sansMedium() : font::sans());
            if (animCount > 0)
            {
                // The count of animatable fields: what makes this shape a motion target.
                const std::string n = std::to_string((int)animCount);
                drawFittedRight(t, n, w - pad - 14.0, centreBaseline(y, rowH, type::micro()), 14.0,
                                type::micro(), palette::primary(), font::monoMedium());
                artboard::drawCircle(t, w - pad - 4.0, gy, 2.0, artboard::Paint::filled(palette::primary()));
            }
        }
        t.restore();

        drawSectionTitle(t, "Add", pad, footerTop() + 10.0);
    }
}
}
