/*
 *  arstro-android-shell — CompositorBridge: the per-desktop window-management seam (M1.5).
 *
 *  Platform-free interface (plan §3.2). Everything the shell needs from the compositor —
 *  list/activate/close/tile windows, "home" (minimise all), open-maximised, split-ratio,
 *  "back", per-window thumbnails — goes through this one abstraction so the shared surfaces
 *  (M1–M6) never depend on KWin or GNOME directly. Concrete backends arrive later: KWinBridge
 *  (a KWin script + D-Bus, M7) and GnomeBridge (a GNOME Shell extension, M8). NullBridge here
 *  is the no-op used until then (and in tests / on a bare compositor).
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace androidshell
{
    struct WindowInfo
    {
        std::string id, appId, title;
        bool active = false;
        bool fullscreen = false;
        bool operator==(const WindowInfo &o) const
        {
            return id == o.id && appId == o.appId && title == o.title &&
                   active == o.active && fullscreen == o.fullscreen;
        }
        bool operator!=(const WindowInfo &o) const { return !(*this == o); }
    };

    // Hint the compositor pushes about the top-most window, for status-bar tinting + immersive
    // (fullscreen) bar hiding (plan §2.3, §2.7.4).
    struct TopWindowHint
    {
        bool fullscreen = false;    // top window is fullscreen -> hide bars until an edge reveal
        bool lightSurface = false;  // top window is light -> bar uses dark icons (else white)
    };

    // Receives a PNG blob for requestThumbnail (may never be called if unsupported).
    using PngSink = std::function<void(const uint8_t *png, size_t len)>;

    class CompositorBridge
    {
    public:
        virtual ~CompositorBridge() = default;

        virtual std::vector<WindowInfo> listWindows() = 0;               // MRU order
        virtual void activate(const std::string &id) = 0;
        virtual void close(const std::string &id) = 0;
        virtual void minimizeAll() = 0;                                  // "home"
        virtual void setMaximizedDefault(bool on) = 0;
        virtual void tilePair(const std::string &l, const std::string &r, double ratio) = 0;
        virtual void setRatio(double ratio) = 0;
        virtual void untile() = 0;
        virtual void back() = 0;                                         // plan §3.2.4
        virtual void requestThumbnail(const std::string &id, PngSink sink) = 0;  // may no-op

        // The bridge invokes these when the compositor state changes; surfaces subscribe.
        std::function<void()> onWindowsChanged;
        std::function<void(TopWindowHint)> onTopWindowHint;
    };

    // No-op bridge: no windows, every action a no-op. Used until KWinBridge (M7) / GnomeBridge
    // (M8) exist, in headless tests, and when the shell runs on a bare compositor.
    class NullBridge : public CompositorBridge
    {
    public:
        std::vector<WindowInfo> listWindows() override { return {}; }
        void activate(const std::string &) override {}
        void close(const std::string &) override {}
        void minimizeAll() override {}
        void setMaximizedDefault(bool) override {}
        void tilePair(const std::string &, const std::string &, double) override {}
        void setRatio(double) override {}
        void untile() override {}
        void back() override {}
        void requestThumbnail(const std::string &, PngSink) override {}  // no thumbnails
    };

} // namespace androidshell
} // namespace arstro
