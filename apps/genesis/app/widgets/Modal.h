/*
 *  Genesis — Modal: the app's one overlay surface.
 *
 *  New / Save As / the verification report all share one modal so there is a single place
 *  that knows how a dialog fades in, dims what is behind it, traps the click that dismisses
 *  it, and draws in the OVERLAY pass (the only kind of overlap the layout rules allow).
 *  Open is NOT here: it's a native OS file-chooser (the host owns it, cosmo's pattern) so it
 *  can reach any .genesis file on disk, not just whatever sits in the process's cwd.
 */
#pragma once
#include "../Theme.h"
#include "Panel.h"
#include <artboard/artboard.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace genesis
{
namespace ui
{
    class App;

    class Modal : public artboard::Segment
    {
    public:
        explicit Modal(App &app);
        void layout(double w, double h);
        void advance(double nowMs) override;

        void openNew();
        void openSaveAs();
        void openReport(const std::string &title, const std::vector<std::string> &lines,
                        StatusLevel level);
        void close();
        bool isOpen() const { return mOpen; }
        /** True while the scrim is drawn at all (including the closing fade) — panels that
         *  paint in the overlay pass check this so nothing floats over the dialog. */
        bool coversApp() const { return mReveal.value() > 0.01; }
        /** The dialog card's rect in app space — what "inside the dialog" means. */
        artboard::Rect cardBounds() const { return cardRect(); }

    protected:
        // Drawn in the NORMAL pass, not the overlay pass: the modal is the app's last child,
        // so its scrim and card already cover every panel, and drawing here keeps its own
        // controls (which are ordinary child Segments) ON TOP of the card rather than under it.
        void onPaint(artboard::IRenderTarget &t) const override;
        /** Only while open — and still only inside our bounds, so a closed modal is
         *  completely out of the way. */
        bool hitTestSelf(const artboard::Point &p) const override
        {
            return mOpen && artboard::Segment::hitTestSelf(p);
        }
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;

    private:
        enum class Mode { None, New, SaveAs, Report };
        void rebuild();
        artboard::Rect cardRect() const;

        App &mApp;
        Mode mMode = Mode::None;
        bool mOpen = false;
        artboard::Property mReveal{0.0};
        double mNowMs = 0.0;

        std::string mTitle;
        std::vector<std::string> mLines;
        StatusLevel mLevel = StatusLevel::Info;

        std::vector<std::string> mChoices;      // base names, or file paths
        int mChoice = 0;
        RowHover mHover;

        std::shared_ptr<artboard::TextBox> mName;
        std::shared_ptr<artboard::Button> mOk;
        std::shared_ptr<artboard::Button> mCancel;
    };
}
}
