/*
 *  solaris_ui — FloatWindow and WindowLayer: windows INSIDE the song view, as FL Studio's channel
 *  windows are (R-WIN-1, R-WIN-4).
 *
 *  A FloatWindow is a frame — a title bar (the title, ×) over a content Segment. Dragged by its
 *  title bar it follows the pointer exactly (direct manipulation), kept inside the layer; touched
 *  anywhere it comes to the front (`raise`); opened and closed it FADES (150 / 120 ms) — a setter
 *  has no clock, so `open`/`close` record intent and `advance` starts the tween. While closing it
 *  takes no input (hit-testing follows the intent, as Interstellar's FadePage does). A closed window
 *  is kept, hidden, and reopened where it was: placement and stacking are the view's, not saved,
 *  not commands — like zoom.
 *
 *  The WindowLayer covers the song view under the song bar and holds the windows, keyed: one per
 *  device (`dev:<dv>`, a DevicePanel inside — R-WIN-1's "a window of that synth"), one per pattern
 *  (`roll:<pt>`, the piano roll, R-ROLL-1). It is not itself a target: a click between windows falls
 *  through to the lanes and the dock. The App offers it right-clicks first (`contextClick`), because
 *  a parameter row's slider would swallow them.
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "../../../cosmo/widgets/ContextMenu.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_ui
{
    class DevicePanel;
    class PianoRoll;

    class FloatWindow : public artboard::Segment
    {
    public:
        static constexpr double kTitleH = 26.0; // space::u(8)

        FloatWindow(std::string key, std::shared_ptr<artboard::Segment> content);
        const std::string &key() const { return mKey; }
        void setTitle(const std::string &t) { mTitle = t; }
        const std::string &title() const { return mTitle; }
        artboard::Segment &content() { return *mContent; }

        void open();
        void close();
        bool isOpen() const { return mWanted; }
        double appearAmount() const { return mAppear.value(); } // LIVE
        artboard::Rect titleRect() const { return artboard::Rect{0, 0, width.value(), kTitleH}; }
        artboard::Rect closeRect() const { return artboard::Rect{width.value() - 6.5 - 19.5, 3.25, 19.5, 19.5}; }
        void layout();
        void advance(double nowMs) override;
        bool hitTest(const artboard::Point &p) const override { return mWanted && Segment::hitTest(p); }

        std::function<void()> onClosed;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return mWanted && localBounds().contains(p); }

    private:
        std::string mKey, mTitle;
        std::shared_ptr<artboard::Segment> mContent;
        bool mWanted = false, mApplied = false, mDragging = false;
        artboard::Point mGrab{0, 0};
        artboard::AnimatedProperty mAppear{0.0};
        cosmo_v2::HoverFade mHover;
    };

    class WindowLayer : public artboard::Segment
    {
    public:
        WindowLayer() { clipToBounds = true; }
        /** Open (or bring forward) the window of device `dv`. */
        FloatWindow &openDevice(const std::string &dv);
        FloatWindow *window(const std::string &key) const;
        DevicePanel *devicePanel(const std::string &dv) const;
        /** Open (or bring forward) the piano roll of pattern `pt` (R-ROLL-1). */
        FloatWindow &openRoll(const std::string &pt);
        PianoRoll *roll(const std::string &pt) const;
        bool isOpen(const std::string &key) const;
        void close(const std::string &key);

        void bind(const solaris::AppModel &m, bool interacting);
        void layout();
        /** A right-click at a window point, offered first: true when a window's content took it. */
        bool contextClick(artboard::Point world);

        std::function<bool(const std::string &line)> onCommand;
        std::function<void(std::vector<cosmo_v2::ContextMenu::Item> items, artboard::Point world)> onMenu;
        std::function<void(const std::string &current, artboard::Point world, std::function<void(const std::string &)> done)> onRename;

    protected:
        bool hitTestSelf(const artboard::Point &) const override { return false; } // only the windows take input

    private:
        FloatWindow &place(std::shared_ptr<FloatWindow> w, double width, double height);
        std::map<std::string, std::shared_ptr<FloatWindow>> mWindows;
        solaris::AppModel mModel;
        bool mInteracting = false;
        int mCascade = 0;
    };
}
}
