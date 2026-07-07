/*
 *  cosmo_v2 by arstro — HomeScreen: the project launcher (R-HOME), a faithful port
 *  of the Figma `HomeScreenDesktop` frame (ref/2 Figma export). A 300px left
 *  sidebar (wordmark, tagline, New / Open / Import actions, reserved bottom links,
 *  version) and a right pane with a "Recent Projects" header + count + search box
 *  and an auto-fill grid of project cards (16:9 thumbnail, Edited badge, name,
 *  "N photos · size · date") plus a dashed New-Project card and an empty-search
 *  state.
 *
 *  The card chrome, sidebar and header are drawn in onPaint and hit-tested in
 *  handleGesture (like the modal dialogs); each card's thumbnail is a real
 *  ImageView child so project covers render as pixels. Search is a focusable
 *  TextBox; its text is polled each frame to re-filter the grid.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class HomeScreen : public artboard::Segment
    {
    public:
        struct CardInfo
        {
            std::string name;
            std::string photos;   // "10 photos"
            std::string size;     // "2.4 GB" ("" hides the size dot)
            std::string date;     // "2h ago"
            bool edited = false;
            int recentIndex = -1; // index into the app's recents list
        };

        HomeScreen();

        // ── sidebar / grid actions ──
        std::function<void()> onNewProject;
        std::function<void()> onOpenProject;
        std::function<void()> onImportCatalog;
        std::function<void(int recentIndex)> onOpenRecent;

        /** Rebuild the recent-project grid (adds the trailing New-Project card). */
        void setRecents(const std::vector<CardInfo> &cards);
        /** Supply a decoded cover for a recent (host decodes the first image). */
        void setThumbnail(int recentIndex, const uint8_t *rgba, int w, int h);
        /** Scroll the grid by a wheel delta (positive = up). */
        void scrollBy(double delta);
        void layout();
        void advance(double nowMs) override;  // App drives this (Home is not in the editor tree)

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Card
        {
            CardInfo info;
            std::shared_ptr<artboard::ImageView> thumb;
            bool shown = true;             // passes the current search filter
            artboard::Rect rect{0, 0, 0, 0};  // full card rect (grid space), set in layout
        };

        // One identifier scheme for every hover-able region (sidebar actions, bottom
        // links, recent cards, the trailing New-Project card) -- see regionAt().
        enum class Region { None, Action, Link, Card, NewCard };

        artboard::Rect actionRect(int i) const;    // 0=New 1=Open 2=Import (sidebar)
        artboard::Rect bottomLinkRect(int i) const; // 0=Settings 1=What's New 2=Help
        artboard::Rect searchRect() const;
        double gridTop() const;
        double contentX() const;
        double scrollY() const { return mScrollYAnim.value(); }  // eased (drawn) scroll, not the target
        void regionAt(const artboard::Point &local, Region &kind, int &index) const;
        void relayoutGrid();
        void applyFilter();
        int visibleCount() const;

        std::shared_ptr<artboard::TextBox> mSearch;
        std::shared_ptr<artboard::Segment> mGridClip;                 // clips thumbnails to the grid viewport
        std::vector<std::shared_ptr<artboard::ImageView>> mThumbPool; // reused across setRecents (no removeChild)
        std::vector<Card> mCards;              // recents (the New card is drawn, not stored here)
        std::string mLastSearch;
        double mScrollY = 0.0;                 // TARGET scroll; eased into place by mScrollYAnim
        double mContentH = 0.0;                // total grid height (for scroll clamp)
        artboard::Rect mNewCardRect{0, 0, 0, 0};

        // hover + eased scroll (R-G-1: everything animates, nothing snaps)
        Region mHoverKind = Region::None;           // which region the pointer is over
        int mHoverIndex = -1;                       // index within that region kind
        bool mHoverPrev = false;
        artboard::AnimatedProperty mHoverAmt{0.0};  // hover-feedback fade
        double mScrollIssued = 0.0;                 // last scroll target handed to the tween
        artboard::AnimatedProperty mScrollYAnim{0.0};  // eased grid scroll
    };
}
}
