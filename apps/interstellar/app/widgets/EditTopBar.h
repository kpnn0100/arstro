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
 *  Unsaved work is also the family's DirtyDot after the project name — FOREGROUND, radius 3, eased
 *  (R-UI-21c): it is a fact about the document, and the accent means selection.
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
#include "../../../cosmo/widgets/DirtyDot.h"
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
        /** Cosmo's menu bar (File · Edit · Preset · Colour · View), after the wordmark as in cosmo's
         *  TopBar. The App fills it. */
        std::shared_ptr<cosmo_v2::MenuStrip> menus() { return mMenus; }
        artboard::Rect wordmarkRect() const { return artboard::Rect{0, 0, mWordmarkW + 2 * 9.75, height.value()}; }
        /** The box the project name was drawn into (its right edge stops before the tabs). */
        artboard::Rect nameRect() const;

        /** R-UI-21c: the unsaved dot — its LIVE eased amount (0 saved … 1 unsaved) and where its
         *  centre was last drawn (local; measured in the paint, so it follows the name's real end). */
        double dirtyDotAmount() const { return mDot.amount(); }
        artboard::Point dirtyDotCentre() const { return mDotAt; }

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
        cosmo_v2::DirtyDot mDot;                 // R-UI-21c: after the project name, foreground
        mutable artboard::Point mDotAt{0.0, 0.0};
        artboard::AnimatedProperty mDirtyAmt{0.0};   // the save icon's light
        bool mDirtyApplied = false, mDirtyInit = false, mBound = false;   // first placement sets
    };
}
}
