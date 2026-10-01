/*
 *  interstellar_v1 — EditTopBar: the Edit page's 29.25 px chrome (ui-brief §3).
 *
 *      interstellar.   « project »   [ Grade | Cut | Deliver ]   ‹ main ▾ ›            [save]
 *
 *  Cosmo's TopBar rhythm (height, 9.75 px pads, the bottom hairline, the wordmark at 13 px
 *  SemiBold tracked −0.03×size with the accent dot at the MEASURED end of the word) carrying
 *  Interstellar's contents: the project name, ELLIPSIZED against the room actually left before
 *  the tab switcher (R5); the tab switcher centred on the window; the version switcher as chrome
 *  right after it (R-UI-4); and a save button whose accent lights while the project is dirty.
 *
 *  Not cosmo's TopBar class: that one owns cosmo's menu strip and rail toggle, which Interstellar
 *  does not have. Clicking the wordmark asks to go Home (the App turns it into `project close`,
 *  behind cosmo's own ConfirmDialog when there are unsaved changes).
 */
#pragma once
#include "../Theme.h"
#include "TabSwitcher.h"
#include "VersionSwitcher.h"
#include "../../../cosmo/widgets/IconButton.h"
#include "../../../cosmo/widgets/MenuStrip.h"
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class EditTopBar : public artboard::Segment
    {
    public:
        EditTopBar();

        void bind(const interstellar::AppModel &m);
        void layout();
        void setRootSize(double w, double h) { mVersions->setRootSize(w, h); }

        std::shared_ptr<TabSwitcher> tabs() { return mTabs; }
        std::shared_ptr<VersionSwitcher> versions() { return mVersions; }
        /** Cosmo's menu bar (File · Edit · Settings · Workspace · Preset), after the wordmark as in
         *  cosmo's TopBar. The App fills it. */
        std::shared_ptr<cosmo_v2::MenuStrip> menus() { return mMenus; }
        artboard::Rect wordmarkRect() const { return artboard::Rect{0, 0, mWordmarkW + 2 * 9.75, height.value()}; }
        /** The box the project name was drawn into (its right edge stops before the tabs). */
        artboard::Rect nameRect() const;

        std::function<void()> onHome;
        std::function<void()> onSave;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }
        void advance(double nowMs) override;

    private:
        std::shared_ptr<TabSwitcher> mTabs;
        std::shared_ptr<VersionSwitcher> mVersions;
        std::shared_ptr<cosmo_v2::IconButton> mSave;
        std::shared_ptr<cosmo_v2::MenuStrip> mMenus;
        std::string mProjectName;
        bool mDirty = false;
        /** The wordmark's measured width from the LAST paint (measurement is only valid during a
         *  render). Seeded with an estimate for the very first layout. */
        mutable double mWordmarkW = 82.0;
        artboard::AnimatedProperty mHomeHover{0.0};
        bool mHomeHovered = false;
        double mNowMs = 0.0;
        artboard::AnimatedProperty mDirtyAmt{0.0};
        bool mDirtyApplied = false, mDirtyInit = false, mBound = false;   // first placement sets
    };
}
}
