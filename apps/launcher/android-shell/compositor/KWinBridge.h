/*
 *  arstro-android-shell — KWinBridge (M7): CompositorBridge over KWin (Plasma track).
 *
 *  Talks to the KWin script (kwin-script/main.js) via the session bus. KWin scripts expose their
 *  functions through the `org.kde.KWin.Script` object once loaded; this bridge marshals
 *  list/activate/close/tile/back into those calls. Because KWin's scripting D-Bus surface varies
 *  by version, each call is best-effort and failure-tolerant — a missing KWin just yields no-ops
 *  (so the shell still runs, degraded, and the GNOME track uses GnomeBridge instead).
 *
 *  UNVERIFIED on this dev machine (no kwin_wayland). The JSON round-trip + the exact KWin script
 *  invocation path are validated on a Plasma session (L2/L4).
 */
#pragma once
#include "compositor/CompositorBridge.h"

typedef struct _GDBusConnection GDBusConnection;

namespace arstro
{
namespace androidshell
{
    class KWinBridge : public CompositorBridge
    {
    public:
        KWinBridge();
        ~KWinBridge() override;

        // Load kwin-script/main.js into KWin (idempotent) and start it.
        bool loadScript(const char *scriptPath);

        std::vector<WindowInfo> listWindows() override;
        void activate(const std::string &id) override;
        void close(const std::string &id) override;
        void minimizeAll() override;
        void setMaximizedDefault(bool on) override;
        void tilePair(const std::string &l, const std::string &r, double ratio) override;
        void setRatio(double ratio) override;
        void untile() override;
        void back() override;
        void requestThumbnail(const std::string &id, PngSink sink) override;

    private:
        GDBusConnection *mBus = nullptr;  // session bus
        int mScriptId = -1;
        double mSplitRatio = 0.5;
        std::string mLeftId, mRightId;
    };

} // namespace androidshell
} // namespace arstro
