/*
 *  arstro-shell — the shell's own surfaces: home, the status bar, the shade (R-HOME, R-BAR, R-SHADE).
 *
 *  Each is an Artboard tree drawn into its own layer; the compositor moves the layers. A view
 *  reads the model and dispatches text commands — it never changes the model and never decides
 *  anything a second front end would have to decide again.
 *
 *  A layer is redrawn only while its view says it is busy (`busy(now)`): a slide of the whole
 *  shade moves a finished texture and costs no drawing at all. That is what keeps a full-screen
 *  animation smooth on a small board whose CPU could not redraw the sheet 60 times a second.
 */
#pragma once
#include "../core/ShellService.h"
#include "../kit/Widgets.h"
#include "GeneratedIcons.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace shell
{
    using Dispatch = std::function<void(const std::string &)>;
    /** A registered picture for an app's icon, or 0 when the app has none (draw the monogram). */
    using IconImage = std::function<int(const AppInfo &)>;

    /** Draw an app's icon into `r`: its own picture if it has one, else the monogram tile. */
    void drawAppIcon(artboard::IRenderTarget &t, const AppInfo &app, int image, const artboard::Rect &r, double alpha = 1.0);

    /** Shared by every view: "has anything changed that a frame must show?" */
    class BusyClock
    {
    public:
        void touch(double nowMs) { mUntil = std::max(mUntil, nowMs + 700.0); }
        bool busy(double nowMs) const { return nowMs < mUntil; }

    private:
        double mUntil = 0.0;
    };

    class AppIcon : public kit::Pressable
    {
    public:
        AppInfo app;
        int image = 0;
        artboard::Rect iconBox() const;  // the square, in this segment's local space

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
    };

    /** The home screen: a quiet clock and the installed apps. */
    class HomeView : public artboard::Segment
    {
    public:
        HomeView(Dispatch dispatch, IconImage image);

        void sync(const Model &m, const std::string &time, const std::string &date, double nowMs);
        void layout(double w, double h);
        /** Where an app's icon is, in dp on the screen. False if the app is not on the grid. */
        bool iconRect(const std::string &appId, artboard::Rect &out) const;
        bool busy(double nowMs) const { return mBusy.busy(nowMs); }
        void touched(double nowMs) { mBusy.touch(nowMs); }

        static constexpr double kCellW = 104.0, kCellH = 112.0;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        Dispatch mDispatch;
        IconImage mImage;
        std::vector<std::shared_ptr<AppIcon>> mIcons;
        std::string mAppsKey, mTime, mDate;
        BusyClock mBusy;
        bool mPlaced = false;
        double mNow = 0.0, mLaidW = 0.0, mLaidH = 0.0;
        artboard::Property mEmptyAlpha{0.0};
    };

    /** The strip at the top: the time, and what is switched on. */
    class StatusBarView : public artboard::Segment
    {
    public:
        void sync(const Model &m, const std::string &time, double nowMs);
        void advance(double nowMs) override;
        bool busy(double nowMs) const { return mBusy.busy(nowMs); }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        std::string mTime;
        bool mWant[3] = {false, false, false};     // wifi, bluetooth, do-not-disturb
        bool mApplied[3] = {false, false, false};
        artboard::Property mAmount[3];
        bool mPlaced = false;
        BusyClock mBusy;
    };

    /** Notifications, self-drawn so each one can fade in, slide up into a gap, and fade out. */
    class NotificationList : public kit::Pressable
    {
    public:
        std::function<void(int)> onDismiss;
        void sync(const std::vector<Notification> &list, double nowMs);
        void advance(double nowMs) override;
        /** The live height: the bottom of the lowest notification still visible. */
        double contentHeight() const;
        bool moving() const;
        double emptyAmount() const { return mEmpty.value(); }

        static constexpr double kItemH = 68.0, kGap = 8.0;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;

    private:
        struct Item
        {
            Notification n;
            artboard::Property y, alpha;
            double yTarget = -1.0;
            bool dying = false, fresh = true;
        };
        std::vector<Item> mItems;
        artboard::Property mEmpty{0.0};
        bool mEmptyApplied = false, mFirst = true;
    };

    /** The shade: quick settings and notifications on one sheet that comes down from the top. */
    class ShadeView : public artboard::Segment
    {
    public:
        explicit ShadeView(Dispatch dispatch);

        void sync(const Model &m, const std::string &time, const std::string &date, double nowMs);
        void layout(double w, double h);
        bool busy(double nowMs) const { return mBusy.busy(nowMs) || mScroll->moving() || mList->moving(); }
        void touched(double nowMs) { mBusy.touch(nowMs); }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        Dispatch mDispatch;
        std::shared_ptr<kit::TouchScroll> mScroll;
        std::shared_ptr<artboard::Segment> mContent;
        std::shared_ptr<kit::Tile> mWifi, mBluetooth, mDnd, mMotion;
        std::shared_ptr<kit::TouchSlider> mBrightness, mVolume;
        std::shared_ptr<kit::TextButton> mClear;
        std::shared_ptr<NotificationList> mList;
        std::string mTime, mDate;
        unsigned mRevision = ~0u;
        bool mPlaced = false;
        double mColX = 0, mColW = 0, mNotesY = 0;
        artboard::Property mClearAlpha{0.0};
        bool mClearWanted = false, mClearApplied = false;
        BusyClock mBusy;
    };
}
}
