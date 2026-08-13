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
        double footerTop() const;

        App &mApp;
        std::vector<Row> mRows;
        std::vector<std::shared_ptr<artboard::Button>> mAdd;
        std::shared_ptr<artboard::Button> mDuplicate;
        std::shared_ptr<artboard::Button> mDelete;
        RowHover mHover;
        double mNowMs = 0.0;
        artboard::Spring mScroll{0.0};
        double mScrollTarget = 0.0;
    };
}
}
