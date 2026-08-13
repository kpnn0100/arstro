/*
 *  arstro-android-shell — GnomeBridge (M8): CompositorBridge over the GNOME extension.
 *
 *  Talks to org.arstro.AndroidShell.Compositor, which the companion GNOME Shell extension
 *  (gnome-extension/extension.js) exports using Meta.Window/global.display. Same shape as
 *  KWinBridge — the shell picks the bridge for the running desktop. Failure-tolerant: if the
 *  extension isn't loaded the calls no-op (the shell degrades). UNVERIFIED here (no gnome-shell).
 */
#pragma once
#include "compositor/CompositorBridge.h"

typedef struct _GDBusConnection GDBusConnection;

namespace arstro
{
namespace androidshell
{
    class GnomeBridge : public CompositorBridge
    {
    public:
        GnomeBridge();
        ~GnomeBridge() override;

        std::vector<WindowInfo> listWindows() override;
        void activate(const std::string &id) override;
        void close(const std::string &id) override;
        void minimizeAll() override;
        void setMaximizedDefault(bool) override {}
        void tilePair(const std::string &l, const std::string &r, double ratio) override;
        void setRatio(double) override {}
        void untile() override {}
        void back() override;
        void requestThumbnail(const std::string &, PngSink) override {}

    private:
        void call0(const char *method);
        GDBusConnection *mBus = nullptr;
    };

} // namespace androidshell
} // namespace arstro
