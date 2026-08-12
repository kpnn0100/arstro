#include "Inspector.h"
#include "../App.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace genesis
{
namespace ui
{
    namespace
    {
        constexpr double kRowH = 30.0;
        constexpr double kTitleH = 26.0;
        constexpr double kNoteH = 18.0;
        constexpr double kLabelW = 84.0;
        constexpr double kToggleW = 26.0;

        std::string numToText(double v)
        {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", v);
            return buf;
        }
    }

    Inspector::Inspector(App &app) : mApp(app) { refresh(); }

    double Inspector::boxLeft() const { return metrics::pad() + kLabelW; }
    double Inspector::boxWidth() const
    {
        return std::max(56.0, width.value() - boxLeft() - metrics::pad() - kToggleW - 4.0);
    }

    void Inspector::addSection(const std::string &title)
    {
        Row r;
        r.kind = RowKind::SectionTitle;
        r.label = title;
        r.height = kTitleH;
        mRows.push_back(std::move(r));
    }

    std::shared_ptr<artboard::TextBox> Inspector::makeBox(const std::string &value,
                                                          const std::string &hint)
    {
        auto b = std::make_shared<artboard::TextBox>(theme().textBox);
        b->text = value;
        b->placeholder = hint;
        b->caretToEnd();
        b->focusable = true;
        b->height.set(22.0);
        addChild(b);
        return b;
    }

    void Inspector::refresh()
    {
        clearChildren();
        mRows.clear();
        mShownShape = mApp.selectedShape();
        const Document &doc = mApp.doc();

        // ── Component ────────────────────────────────────────────────────────────
        addSection("Component");
        {
            Row r;
            r.kind = RowKind::DocName;
            r.label = "name";
            r.height = kRowH;
            r.boxes.push_back(makeBox(doc.name, "ComponentName"));
            mRows.push_back(r);
            r = Row();
            r.kind = RowKind::DocNamespace;
            r.label = "namespace";
            r.height = kRowH;
            r.boxes.push_back(makeBox(doc.nameSpace, "app"));
            mRows.push_back(r);
            r = Row();
            r.kind = RowKind::DocWidth;
            r.label = "design w";
            r.height = kRowH;
            r.boxes.push_back(makeBox(numToText(doc.designW), "160"));
            mRows.push_back(r);
            r = Row();
            r.kind = RowKind::DocHeight;
            r.label = "design h";
            r.height = kRowH;
            r.boxes.push_back(makeBox(numToText(doc.designH), "160"));
            mRows.push_back(r);
        }

        // ── Shape + fields + path ────────────────────────────────────────────────
        if (const Shape *s = doc.findShape(mShownShape))
        {
            addSection(mShownShape);
            {
                Row r;
                r.kind = RowKind::ShapeId;
                r.label = "id";
                r.height = kRowH;
                r.boxes.push_back(makeBox(s->id, "name"));
                mRows.push_back(r);
            }
            if (s->kind == ShapeKind::Label)
            {
                Row r;
                r.kind = RowKind::ShapeText;
                r.label = "text";
                r.height = kRowH;
                r.boxes.push_back(makeBox(s->text, "Label, or {param}"));
                mRows.push_back(r);
            }
            for (const auto *fd : fieldsFor(s->kind))
            {
                Row r;
                r.kind = RowKind::Field;
                r.label = fd->name;
                r.key = fd->name;
                r.animatable = fd->animatable;
                r.height = kRowH;
                // An empty colour field means "draw none" — say so rather than showing a
                // blank box that reads as an unfilled required value.
                r.boxes.push_back(makeBox(s->field(fd->name),
                                          fd->type == FieldType::Color && !*fd->defaultExpr
                                              ? "none"
                                              : fd->defaultExpr));
                mRows.push_back(r);
            }
            if (s->kind == ShapeKind::Path)
            {
                addSection("Path");
                for (int i = 0; i < (int)s->path.size(); ++i)
                {
                    Row r;
                    r.kind = RowKind::PathCmd;
                    r.index = i;
                    r.label = std::string(1, s->path[(size_t)i].op);
                    r.height = kRowH;
                    for (const auto &a : s->path[(size_t)i].args)
                        r.boxes.push_back(makeBox(a, "0"));
                    mRows.push_back(r);
                }
                Row add;
                add.kind = RowKind::PathAdd;
                add.height = kRowH;
                mRows.push_back(add);
                if (s->path.empty())
                {
                    Row note;
                    note.kind = RowKind::Note;
                    note.label = "M moves, L lines, Q/C curve, Z closes.";
                    note.height = kNoteH;
                    mRows.push_back(note);
                }
            }
        }
        else
        {
            addSection("Shape");
            Row note;
            note.kind = RowKind::Note;
            note.label = "Select a shape to edit its fields.";
            note.height = kNoteH;
            mRows.push_back(note);
            note.label = "Every field is an expression, not a number.";
            mRows.push_back(note);
        }

        // ── Params ───────────────────────────────────────────────────────────────
        addSection("Params");
        for (int i = 0; i < (int)doc.params.size(); ++i)
        {
            const Param &p = doc.params[(size_t)i];
            Row r;
            r.kind = RowKind::Param;
            r.label = p.name;
            r.key = p.name;
            r.index = i;
            r.height = kRowH;
            if (p.type == ParamType::Number && p.hasRange)
            {
                r.slider = std::make_shared<artboard::Slider>(theme().slider);
                r.slider->setRange(p.minimum, p.maximum);
                double v = 0.0;
                if (mApp.runtime().paramNumber(p.name, v)) r.slider->setValue(v);
                r.slider->focusable = true;
                r.slider->height.set(14.0);
                App *a = &mApp;
                const std::string name = p.name;
                r.slider->onChange = [a, name](double nv) { a->runtime().setParamNumber(name, nv); };
                addChild(r.slider);
            }
            else
                r.boxes.push_back(makeBox(p.defaultExpr, p.type == ParamType::Text ? "text" : "value"));
            mRows.push_back(r);
        }
        {
            Row add;
            add.kind = RowKind::ParamAdd;
            add.height = kRowH;
            mRows.push_back(add);
        }
        if (doc.params.empty())
        {
            Row note;
            note.kind = RowKind::Note;
            note.label = "Params become setters on the exported class.";
            note.height = kNoteH;
            mRows.push_back(note);
        }

        // ── Problems ─────────────────────────────────────────────────────────────
        if (!mApp.diagnostics().empty())
        {
            addSection("Problems");
            for (const auto &d : mApp.diagnostics())
            {
                Row r;
                r.kind = RowKind::Problem;
                r.label = d.where + ": " + d.message;
                r.index = d.isError() ? 1 : 0;
                r.height = kNoteH * 2.0;
                mRows.push_back(r);
            }
        }
    }

    double Inspector::contentHeight() const
    {
        double h = metrics::pad();
        for (const auto &r : mRows)
            h += r.height;
        return h + metrics::pad();
    }

    void Inspector::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        double y = metrics::pad() - mScroll;
        const double bw = boxWidth();
        for (auto &r : mRows)
        {
            r.y = y;
            // Rows with a remove affordance stop short of it; the rest may use the full
            // width because nothing else lives in that gutter.
            const bool hasRemove = r.kind == RowKind::Param || r.kind == RowKind::PathCmd;
            const double single = hasRemove ? bw : bw + kToggleW;
            if (r.slider)
            {
                r.slider->x.set(boxLeft());
                r.slider->y.set(y + 8.0);
                r.slider->width.set(single);
            }
            else if (r.kind == RowKind::PathCmd && !r.boxes.empty())
            {
                // Coordinates share the row evenly, so a cubic's six values all stay legible.
                const double each = (bw - 4.0 * (double)(r.boxes.size() - 1)) /
                                    (double)r.boxes.size();
                double x = boxLeft();
                for (auto &b : r.boxes)
                {
                    b->x.set(x);
                    b->y.set(y + 4.0);
                    b->width.set(std::max(28.0, each));
                    x += each + 4.0;
                }
            }
            else
                for (auto &b : r.boxes)
                {
                    b->x.set(boxLeft());
                    b->y.set(y + 4.0);
                    b->width.set(r.kind == RowKind::Field ? bw : single);
                }
            y += r.height;
        }
    }

    void Inspector::commitRow(const Row &row)
    {
        Document &doc = mApp.doc();
        auto text = [&](size_t i) { return i < row.boxes.size() ? row.boxes[i]->text : std::string(); };
        switch (row.kind)
        {
        case RowKind::DocName:
            if (!text(0).empty() && doc.name != text(0)) { doc.name = text(0); mApp.documentChanged(); }
            return;
        case RowKind::DocNamespace:
            if (!text(0).empty() && doc.nameSpace != text(0)) { doc.nameSpace = text(0); mApp.documentChanged(); }
            return;
        case RowKind::DocWidth:
        case RowKind::DocHeight:
        {
            const double v = std::atof(text(0).c_str());
            if (v <= 0.0) return;   // an empty or zero box mid-typing is not a command
            double &target = row.kind == RowKind::DocWidth ? doc.designW : doc.designH;
            if (target != v) { target = v; mApp.documentChanged(); }
            return;
        }
        case RowKind::ShapeId:
        {
            const std::string want = text(0);
            Shape *s = doc.findShape(mShownShape);
            if (!s || want.empty() || want == mShownShape) return;
            if (doc.findShape(want)) return;   // a duplicate id would break every reference
            // Renaming has to carry every reference with it, or the document would be left
            // pointing at a shape that no longer exists.
            const std::string old = mShownShape;
            s->id = want;
            for (auto &other : doc.shapes)
                if (other.parent == old) other.parent = want;
            for (auto &r : doc.reactions)
                for (auto &st : r.steps)
                    for (auto &tr : st.tracks)
                        if (tr.target.rfind(old + ".", 0) == 0)
                            tr.target = want + tr.target.substr(old.size());
            mShownShape = want;
            mApp.selectShape(want);
            mApp.documentChanged();
            return;
        }
        case RowKind::ShapeText:
            if (Shape *s = doc.findShape(mShownShape))
                if (s->text != text(0)) { s->text = text(0); mApp.documentChanged(); }
            return;
        case RowKind::Field:
            if (Shape *s = doc.findShape(mShownShape))
                if (s->field(row.key) != text(0)) { s->setField(row.key, text(0)); mApp.documentChanged(); }
            return;
        case RowKind::PathCmd:
            if (Shape *s = doc.findShape(mShownShape))
                if (row.index < (int)s->path.size())
                {
                    bool changed = false;
                    PathCmd &c = s->path[(size_t)row.index];
                    for (size_t i = 0; i < c.args.size() && i < row.boxes.size(); ++i)
                        if (c.args[i] != row.boxes[i]->text) { c.args[i] = row.boxes[i]->text; changed = true; }
                    if (changed) mApp.documentChanged();
                }
            return;
        case RowKind::Param:
            for (auto &p : doc.params)
                if (p.name == row.key && !row.boxes.empty() && p.defaultExpr != text(0))
                {
                    p.defaultExpr = text(0);
                    mApp.documentChanged();
                    return;
                }
            return;
        default:
            return;
        }
    }

    void Inspector::commitAll()
    {
        for (const auto &r : mRows)
        {
            bool focused = false;
            for (const auto &b : r.boxes)
                if (b->hasFocus()) focused = true;
            if (focused)
            {
                commitRow(r);
                return;   // one edit per frame; refresh() may have invalidated the rest
            }
        }
    }

    int Inspector::rowAt(double localY) const
    {
        for (int i = 0; i < (int)mRows.size(); ++i)
            if (localY >= mRows[(size_t)i].y && localY < mRows[(size_t)i].y + mRows[(size_t)i].height)
                return i;
        return -1;
    }

    bool Inspector::toggleHit(const artboard::Point &p, const Row &row) const
    {
        if (row.kind != RowKind::Field || !row.animatable) return false;
        const double x0 = width.value() - metrics::pad() - kToggleW;
        return p.x >= x0 && p.y >= row.y && p.y < row.y + row.height;
    }

    bool Inspector::removeHit(const artboard::Point &p, const Row &row) const
    {
        if (row.kind != RowKind::Param && row.kind != RowKind::PathCmd) return false;
        const double x0 = width.value() - metrics::pad() - 16.0;
        return p.x >= x0 && p.y >= row.y && p.y < row.y + row.height;
    }

    void Inspector::advance(double nowMs)
    {
        mNowMs = nowMs;
        mHover.advance(nowMs);
        // Committing while focused means the preview tracks typing — the point of a live
        // editor. An unparsable intermediate state just shows up in Problems.
        commitAll();
        Segment::advance(nowMs);
    }

    bool Inspector::handleGesture(const artboard::Gesture &g, const artboard::Point &p)
    {
        if (g.type == artboard::Gesture::Type::Move)
        {
            mHover.setHovered(rowAt(p.y));
            return true;
        }
        if (g.type == artboard::Gesture::Type::Drag)
        {
            const double maxScroll = std::max(0.0, contentHeight() - height.value());
            mScroll = std::min(maxScroll, std::max(0.0, mScroll - (p.y - g.start.y) * 0.3));
            layout(width.value(), height.value());
            return true;
        }
        if (g.type != artboard::Gesture::Type::Click)
            return artboard::Segment::handleGesture(g, p);

        const int i = rowAt(p.y);
        if (i < 0) return true;
        const Row &row = mRows[(size_t)i];
        Document &doc = mApp.doc();

        if (toggleHit(p, row))
        {
            if (Shape *s = doc.findShape(mShownShape))
            {
                const bool on = !s->isAnimated(row.key);
                s->setAnimated(row.key, on);
                mApp.documentChanged();
                mApp.status((on ? "Animatable: " : "No longer animatable: ") + mShownShape + "." + row.key,
                            StatusLevel::Good);
            }
            return true;
        }
        if (removeHit(p, row))
        {
            if (row.kind == RowKind::Param && row.index < (int)doc.params.size())
            {
                mApp.status("Removed param " + doc.params[(size_t)row.index].name, StatusLevel::Good);
                doc.params.erase(doc.params.begin() + row.index);
                mApp.documentChanged();
            }
            else if (row.kind == RowKind::PathCmd)
                if (Shape *s = doc.findShape(mShownShape))
                    if (row.index < (int)s->path.size())
                    {
                        s->path.erase(s->path.begin() + row.index);
                        mApp.documentChanged();
                    }
            return true;
        }
        if (row.kind == RowKind::PathAdd)
        {
            if (Shape *s = doc.findShape(mShownShape))
            {
                // Cycle M → L → Q → C → Z across the four quarters of the row, so every
                // command kind is reachable without a menu.
                const double frac = (p.x - metrics::pad()) /
                                    std::max(1.0, width.value() - metrics::pad() * 2.0);
                const char ops[] = {'M', 'L', 'Q', 'C', 'Z'};
                const char op = ops[std::min(4, std::max(0, (int)(frac * 5.0)))];
                PathCmd c;
                c.op = op;
                c.args.assign((size_t)std::max(0, PathCmd::argCount(op)), "0");
                s->path.push_back(c);
                mApp.documentChanged();
            }
            return true;
        }
        if (row.kind == RowKind::ParamAdd)
        {
            Param np;
            np.name = "param";
            for (int n = 2; mApp.doc().findParam(np.name); ++n)
                np.name = "param" + std::to_string(n);
            // Cycle the type by which third of the row was clicked.
            const double frac = (p.x - metrics::pad()) /
                                std::max(1.0, width.value() - metrics::pad() * 2.0);
            if (frac > 0.66) { np.type = ParamType::Text; np.defaultExpr = "text"; }
            else if (frac > 0.33) { np.type = ParamType::Color; np.defaultExpr = "#9a7bff"; }
            else { np.type = ParamType::Number; np.defaultExpr = "1"; np.hasRange = true; np.minimum = 0; np.maximum = 10; }
            doc.params.push_back(np);
            mApp.documentChanged();
            mApp.status("Added param " + np.name, StatusLevel::Good);
            return true;
        }
        return true;
    }

    void Inspector::onPaint(artboard::IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        const double pad = metrics::pad();
        artboard::drawRoundedRect(t, {0, 0, w, h}, 0.0, artboard::Paint::filled(palette::railBg()));
        artboard::drawRoundedRect(t, {0, 0, 1, h}, 0.0, artboard::Paint::filled(palette::border()));

        const Shape *shape = mApp.doc().findShape(mShownShape);
        for (int i = 0; i < (int)mRows.size(); ++i)
        {
            const Row &r = mRows[(size_t)i];
            if (r.y + r.height < 0.0 || r.y > h) continue;
            const double hover = mHover.amount(i);
            if (hover > 0.01 && r.kind != RowKind::SectionTitle && r.kind != RowKind::Note)
                artboard::drawRoundedRect(t, {4, r.y, w - 8, r.height - 2}, radius::hairline(),
                                          artboard::Paint::filled(palette::hoverWash(hover)));

            switch (r.kind)
            {
            case RowKind::SectionTitle:
            {
                drawSectionTitle(t, r.label, pad, r.y + 16.0);
                if (r.label == mShownShape && shape)
                    drawChip(t, shapeKindName(shape->kind),
                             pad + textWidth(r.label, type::micro(), font::sansSemiBold(), 1.1) + 10.0,
                             r.y + 6.0, 13.0, palette::secondary(), palette::secondaryForeground());
                artboard::drawRoundedRect(t, {pad, r.y + 22.0, w - pad * 2.0, 1.0}, 0.0,
                                          artboard::Paint::filled(palette::border()));
                break;
            }
            case RowKind::Note:
                drawFitted(t, r.label, pad, r.y + 12.0, w - pad * 2.0, type::micro(),
                           palette::mutedForeground(), font::sans());
                break;
            case RowKind::Problem:
            {
                const artboard::Color c = r.index ? palette::destructive() : palette::warning();
                artboard::drawRoundedRect(t, {pad, r.y + 4.0, 2.0, r.height - 12.0}, 1.0,
                                          artboard::Paint::filled(c));
                // Two lines, so a long diagnostic is readable rather than ellipsized to
                // uselessness in a narrow column.
                const double avail = w - pad * 2.0 - 10.0;
                const std::string first = ellipsize(r.label, avail, type::micro(), font::sans());
                drawFitted(t, first, pad + 8.0, r.y + 13.0, avail, type::micro(), c, font::sans());
                if (first != r.label)
                {
                    const std::string rest = r.label.substr(std::min(r.label.size(), first.size() - 1));
                    drawFitted(t, rest, pad + 8.0, r.y + 27.0, avail, type::micro(),
                               palette::mutedForeground(), font::sans());
                }
                break;
            }
            case RowKind::PathAdd:
                drawFitted(t, "+ command", pad, centreBaseline(r.y, r.height, type::small()),
                           kLabelW, type::small(), palette::mutedForeground(), font::sans());
                {
                    const char *ops[] = {"M", "L", "Q", "C", "Z"};
                    const double each = (w - boxLeft() - pad) / 5.0;
                    for (int k = 0; k < 5; ++k)
                        drawChip(t, ops[k], boxLeft() + k * each, r.y + 6.0, 17.0, palette::input(),
                                 palette::secondaryForeground());
                }
                break;
            case RowKind::ParamAdd:
                drawFitted(t, "+ param", pad, centreBaseline(r.y, r.height, type::small()), kLabelW,
                           type::small(), palette::mutedForeground(), font::sans());
                {
                    const char *kinds[] = {"number", "color", "text"};
                    const double each = (w - boxLeft() - pad) / 3.0;
                    for (int k = 0; k < 3; ++k)
                        drawChip(t, kinds[k], boxLeft() + k * each, r.y + 6.0, 17.0, palette::input(),
                                 palette::secondaryForeground());
                }
                break;
            case RowKind::PathCmd:
                drawFitted(t, r.label, pad, centreBaseline(r.y, r.height, type::small()), kLabelW - 6.0,
                           type::small(), palette::primary(), font::monoMedium());
                t.setStroke(palette::mutedForeground(), 1.2);
                t.beginPath();
                t.moveTo(w - pad - 12.0, r.y + r.height * 0.5 - 4.0);
                t.lineTo(w - pad - 4.0, r.y + r.height * 0.5 + 4.0);
                t.moveTo(w - pad - 4.0, r.y + r.height * 0.5 - 4.0);
                t.lineTo(w - pad - 12.0, r.y + r.height * 0.5 + 4.0);
                t.strokePath();
                break;
            case RowKind::Param:
            {
                drawFitted(t, r.label, pad, centreBaseline(r.y, r.height, type::small()), kLabelW - 6.0,
                           type::small(), palette::secondaryForeground(), font::sans());
                if (r.slider)
                {
                    char v[32];
                    std::snprintf(v, sizeof v, "%.2f", r.slider->value());
                    drawFittedRight(t, v, w - pad - 54.0, r.y + 10.0, 34.0, type::micro(),
                                    palette::mutedForeground(), font::mono());
                }
                t.setStroke(palette::mutedForeground(), 1.2);
                t.beginPath();
                t.moveTo(w - pad - 12.0, r.y + r.height * 0.5 - 4.0);
                t.lineTo(w - pad - 4.0, r.y + r.height * 0.5 + 4.0);
                t.moveTo(w - pad - 4.0, r.y + r.height * 0.5 - 4.0);
                t.lineTo(w - pad - 12.0, r.y + r.height * 0.5 + 4.0);
                t.strokePath();
                break;
            }
            case RowKind::Field:
            {
                drawFitted(t, r.label, pad, centreBaseline(r.y, r.height, type::small()), kLabelW - 6.0,
                           type::small(), palette::secondaryForeground(), font::sans());
                if (!r.animatable) break;
                const bool on = shape && shape->isAnimated(r.key);
                const double bx = w - pad - kToggleW;
                artboard::drawRoundedRect(t, {bx, r.y + 7.0, kToggleW - 4.0, 16.0}, radius::hairline(),
                                          artboard::Paint::filledStroked(
                                              on ? palette::primaryAlpha(0.9) : palette::input(),
                                              palette::border(), 1.0));
                // A circular-arrow mark: this field can be driven by a reaction.
                t.setStroke(on ? palette::primaryForeground() : palette::mutedForeground(), 1.3);
                t.beginPath();
                const double cx = bx + (kToggleW - 4.0) * 0.5, cy = r.y + 15.0, rr = 4.2;
                t.moveTo(cx + rr, cy);
                t.cubicTo(cx + rr, cy - rr * 1.1, cx - rr, cy - rr * 1.1, cx - rr, cy);
                t.cubicTo(cx - rr, cy + rr * 1.1, cx + rr, cy + rr * 1.1, cx + rr * 0.2, cy + rr * 0.8);
                t.strokePath();
                break;
            }
            default:
                drawFitted(t, r.label, pad, centreBaseline(r.y, r.height, type::small()), kLabelW - 6.0,
                           type::small(), palette::secondaryForeground(), font::sans());
                break;
            }
        }
    }
}
}
