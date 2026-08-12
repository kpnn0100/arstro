/*
 *  Genesis — HomeScreen: the launcher.
 *
 *  The same shape as cosmo's home: a fixed left sidebar (wordmark, tagline, the primary
 *  actions, a version line) and a right pane that flexes — here a grid of recent components
 *  plus one card per authorable base, so "start something new" and "carry on with something"
 *  are the same gesture in the same place.
 *
 *  Self-drawn with hit-testing in handleGesture, like cosmo's home; each card hovers through
 *  the shared RowHover so the whole app has one hover language.
 */
#pragma once
#include "../Recents.h"
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
    class HomeScreen : public artboard::Segment
    {
    public:
        HomeScreen();

        std::function<void(const std::string &base)> onNewComponent;
        std::function<void()> onOpen;
        std::function<void(const std::string &path)> onOpenRecent;

        void setRecents(std::vector<RecentEntry> recents);
        void layout(double w, double h);
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;

    private:
        struct Card
        {
            artboard::Rect rect;
            bool isRecent = false;
            std::string title, subtitle, path, base;
        };
        void rebuildCards();
        int cardAt(const artboard::Point &p) const;
        double sidebarW() const;

        std::vector<RecentEntry> mRecents;
        std::vector<Card> mCards;
        std::shared_ptr<artboard::Button> mOpen;
        RowHover mHover;
        double mNowMs = 0.0;
        double mScroll = 0.0;
        long long mNowSeconds = 0;
        artboard::Property mAppear{0.0};
    };
}
}
