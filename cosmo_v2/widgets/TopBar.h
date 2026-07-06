/*
 *  cosmo_v2 by arstro — TopBar: wordmark, menu bar, filename readout, and the
 *  left-rail toggle. Pixel spec: App.tsx's top bar
 *  (`flex items-center h-9 border-b border-border px-3 gap-3`).
 *
 *  The menu bar is a MenuStrip child (real dropdowns with the original cosmo's
 *  options, centred titles, an animated highlight); TopBar only positions it.
 *
 *  Conversion note (see Theme.h doc comment / plan): the Figma source sets
 *  `html { font-size: 13px }`, so every rem-based Tailwind utility below is
 *  scaled to 13/16 of its nominal value: 1 spacing unit = 0.25rem = 3.25px.
 *    h-9 = 29.25px   px-3 = 9.75px   gap-3 = 9.75px   mr-1 = 3.25px
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "MenuStrip.h"
#include "IconButton.h"
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    class TopBar : public artboard::Segment
    {
    public:
        static constexpr double kHeight = 29.25;

        TopBar();

        std::shared_ptr<MenuStrip> menuStrip() { return mMenuStrip; }
        void setFilename(const std::string &name, int index, int total);  // total<=0 hides it
        void setRailOpen(bool open);

        std::function<void()> onRailToggle;

        void layout();  // call after width changes

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        static constexpr double kPad = 9.75;

        std::shared_ptr<MenuStrip> mMenuStrip;
        std::shared_ptr<IconButton> mRailToggle;
        std::string mFilename;
        int mFileIndex = 0, mFileTotal = 0;
        bool mRailOpen = true;
    };
}
}
