/*
 *  Genesis — Inspector: the right column.
 *
 *  Everything about the component that is not a reaction, in sections that appear only when
 *  they apply:
 *
 *    Component  name, namespace, design size — the document's own identity
 *    Shape      the selected shape's id (rename) and, for a label, its text
 *    Fields     every numeric and colour field as an editable EXPRESSION, with an animate
 *               toggle that promotes it to a Property and makes it a legal reaction target
 *    Path       for a path shape, each command's coordinates — also expressions
 *    Params     the component's knobs, with a live control each, add and remove
 *    Problems   the validator's errors and warnings, named and in one place
 *
 *  Rows are a single flat list built by refresh(), so scrolling, hover, and hit-testing have
 *  one implementation rather than one per section.
 */
#pragma once
#include "../Theme.h"
#include "Document.h"
#include "Panel.h"
#include <artboard/artboard.h>
#include <memory>
#include <string>
#include <vector>

namespace genesis
{
namespace ui
{
    class App;

    class Inspector : public artboard::Segment
    {
    public:
        explicit Inspector(App &app);
        void layout(double w, double h);
        void refresh();            // the selection or the document changed
        void advance(double nowMs) override;

        /** Scroll state of the field list. */
        bool listScrollable() const { return mScroll.scrollable(); }
        double listOffset() const { return mScroll.offset(); }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;

    private:
        enum class RowKind
        {
            SectionTitle,   // a heading; not interactive
            DocName, DocNamespace, DocWidth, DocHeight,
            ShapeId, ShapeText,
            Field,          // a shape field expression (+ animate toggle when animatable)
            PathCmd,        // one path command's coordinates
            PathAdd,        // the "add a command" row
            Param,          // a param's live control (+ remove)
            ParamAdd,
            Problem,        // a validator diagnostic
            Note            // an explanatory line in an empty state
        };
        struct Row
        {
            RowKind kind = RowKind::SectionTitle;
            std::string label;
            std::string key;                 // field name / param name
            int index = 0;                   // path command index, param index
            bool animatable = false;
            double y = 0.0;
            double height = 0.0;
            std::vector<std::shared_ptr<artboard::TextBox>> boxes;
            std::shared_ptr<artboard::Slider> slider;
        };

        /** A key for the row STRUCTURE (which rows exist), deliberately excluding their
         *  values. While it is unchanged, refresh() updates values in place instead of
         *  rebuilding — rebuilding destroys the very TextBox the author is typing into, which
         *  is what made every keystroke drop focus. */
        std::string structureKey() const;
        void rebuildRows();
        void syncValues();
        void rebuildProblems();
        void addSection(const std::string &title);
        std::shared_ptr<artboard::TextBox> makeBox(const std::string &value, const std::string &hint);
        void commitRow(const Row &row);
        void commitAll();
        int rowAt(double localY) const;
        bool toggleHit(const artboard::Point &p, const Row &row) const;
        bool removeHit(const artboard::Point &p, const Row &row) const;
        double contentHeight() const;
        double boxLeft() const;
        double boxWidth() const;

        App &mApp;
        std::string mShownShape;
        std::vector<Row> mRows;
        RowHover mHover;
        double mNowMs = 0.0;
        ListScroll mScroll;
        std::string mStructure;
    };
}
}
