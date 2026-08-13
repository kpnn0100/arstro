/*
 *  Genesis — Chrome: the top bar.
 *
 *  Identity on the left (app mark, document name, base chip, a dirty dot), actions on the
 *  right (New / Open / Save / Export / Verify), and the status line between them. The
 *  status area is the app's one place for "what just happened", so no panel invents its own.
 */
#pragma once
#include "../Theme.h"
#include "Panel.h"
#include <artboard/artboard.h>
#include <memory>
#include <vector>

namespace genesis
{
namespace ui
{
    class App;

    class Chrome : public artboard::Segment
    {
    public:
        explicit Chrome(App &app);
        void layout(double w);
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        struct Action
        {
            std::string label;
            std::shared_ptr<artboard::Button> button;
        };
        App &mApp;
        double mNowMs = 0.0;
        std::vector<Action> mActions;
    };
}
}
