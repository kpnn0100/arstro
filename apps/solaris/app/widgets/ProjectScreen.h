/*
 *  solaris_ui — ProjectScreen: the song (R-UI-3).
 *
 *  The SongBar on top; below it, the song. U1 draws a calm summary of what the song holds — its
 *  mixers and their strips in processing order, its lanes and clips, what `audit` said — so the
 *  screen is never blank while the browser, the lanes and the mixer dock are built (U2–U3), and
 *  says so in words rather than looking broken.
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "SongBar.h"
#include <memory>

namespace arstro
{
namespace solaris_ui
{
    class ProjectScreen : public artboard::Segment
    {
    public:
        ProjectScreen();
        void bind(const solaris::AppModel &m);
        void layout();
        SongBar &bar() { return *mBar; }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        std::shared_ptr<SongBar> mBar;
        solaris::AppModel mModel;
    };
}
}
