/*
 *  arstro app SDK — what an application is, to the touch stack (R-APP-1..6, R-SDK-1..4).
 *
 *  An app is an Artboard Segment tree plus five moments in its life. It never opens a window,
 *  never reads an input device and never knows what is drawing it: `arstro::app::run` connects to
 *  the shell, hands the app its size and density, turns touches into Artboard gestures and puts
 *  the app's frames on screen. That is the whole reason an app written for this stack is the same
 *  binary on a Pi and on a desktop — and why a Windows port changes this library, not the apps.
 *
 *  All sizes an app sees are dp. The SDK multiplies by the display's scale when it draws.
 */
#pragma once
#include "artboard/artboard.h"
#include <string>

namespace arstro
{
namespace app
{
    /** What the shell told the app about where it lives. Read it in `layout()` every frame. */
    struct Context
    {
        double width = 0, height = 0;          // dp
        double scale = 1;                      // pixels per dp
        double insetTop = 0, insetBottom = 0;  // dp the shell draws over: keep controls out of them
        artboard::Color accent;
        double nowMs = 0;

        /** Ask for a frame. Input and the half second after it are drawn anyway; call this when
         *  something changed on its own (a clock ticked, a download finished). */
        void invalidate();
        /** Keep drawing every frame (a running animation) until turned off again. */
        void setContinuous(bool on);
        /** Close this app. */
        void finish();
        /** Post a notification to the shade. */
        void notify(const std::string &title, const std::string &body = "");
        /** Send one shell command line ("set brightness=0.4"). Only apps whose manifest has
         *  `permissions = system` are listened to; the answer arrives as shell events. */
        void request(const std::string &line);
    };

    class App
    {
    public:
        virtual ~App() = default;
        /** Build the tree once. The context's size is already valid. */
        virtual void create(Context &ctx) = 0;
        /** The root segment; the SDK sizes it to the screen before `layout()`. */
        virtual artboard::Segment &root() = 0;
        /** Every frame, after advance and before render: place children from LIVE values. */
        virtual void layout(Context &ctx) { (void)ctx; }
        /** The user went back. Return true if the app used it (closed a page); false lets the
         *  shell go home. */
        virtual bool back() { return false; }
        virtual void paused() {}
        virtual void resumed() {}
        /** One shell event line ("setting.changed key=volume value=0.5") — system apps only. */
        virtual void shellEvent(const std::string &line) { (void)line; }
    };

    /** Run the app until the shell stops it. Returns the process exit code. */
    int run(App &app, const std::string &appId);
}
}
