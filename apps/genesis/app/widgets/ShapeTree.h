/*
 *  Genesis — ShapeTree: the left column.
 *
 *  The component's shape tree (parents indent their children), plus the add/remove/reparent
 *  actions. Selecting here drives the inspector and the canvas selection, so there is one
 *  notion of "the selected shape".
 */
#pragma once
#include "../Theme.h"
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

    class ShapeTree : public artboard::Segment
    {
    public:
        explicit ShapeTree(App &app);
        void layout(double w, double h);
        void refresh();                 // rebuild the flattened row list from the document
        void advance(double nowMs) override;

        /** Scroll state of the object list, so a caller (or a test) can ask whether there is
         *  anything out of view rather than inferring it from what got drawn. */
        bool listScrollable() const { return mScroll.scrollable(); }
        double listOffset() const { return mScroll.offset(); }
        /** True when the last row sits fully inside the drawn list box at the current offset.
         *  Scrolled to the end this must hold, or the bottom row can never be read (G-20). */
        bool lastRowFullyVisible() const;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;

    private:
        struct Row
        {
            std::string id;
            int depth = 0;
        };
        int rowAt(double localY) const;
        double listTop() const;
        /** Bottom of the drawn list box — the same number `measure()` and the paint clip use. */
        double listBottom() const;
        double footerTop() const;

        App &mApp;
        std::vector<Row> mRows;
        std::vector<std::shared_ptr<artboard::Button>> mAdd;
        std::shared_ptr<artboard::Button> mDuplicate;
        std::shared_ptr<artboard::Button> mDelete;
        RowHover mHover;
        double mNowMs = 0.0;
        ListScroll mScroll;
    };
}
}
