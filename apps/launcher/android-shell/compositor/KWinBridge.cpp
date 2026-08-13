/*
 *  arstro-android-shell — KWinBridge implementation (M7). See the header. UNVERIFIED here.
 */
#include "compositor/KWinBridge.h"

#include <gio/gio.h>

namespace arstro
{
namespace androidshell
{
    namespace
    {
        constexpr int kTimeoutMs = 800;

        // Call a method on org.kde.KWin at `path`/`iface`; returns the reply or nullptr.
        GVariant *kwinCall(GDBusConnection *bus, const char *path, const char *iface,
                           const char *method, GVariant *args, const GVariantType *reply)
        {
            if (!bus) return nullptr;
            GError *e = nullptr;
            GVariant *r = g_dbus_connection_call_sync(bus, "org.kde.KWin", path, iface, method, args,
                                                      reply, G_DBUS_CALL_FLAGS_NONE, kTimeoutMs, nullptr, &e);
            if (e) { g_error_free(e); return nullptr; }
            return r;
        }
    }

    KWinBridge::KWinBridge()
    {
        GError *e = nullptr;
        mBus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &e);
        if (e) { g_error_free(e); mBus = nullptr; }
    }

    KWinBridge::~KWinBridge()
    {
        if (mBus) g_object_unref(mBus);
    }

    bool KWinBridge::loadScript(const char *scriptPath)
    {
        // org.kde.KWin /Scripting loadScript(path, pluginName) -> int id; then run() / start().
        GVariant *r = kwinCall(mBus, "/Scripting", "org.kde.kwin.Scripting", "loadScript",
                               g_variant_new("(ss)", scriptPath, "arstro-android-shell"),
                               G_VARIANT_TYPE("(i)"));
        if (!r) return false;
        g_variant_get(r, "(i)", &mScriptId);
        g_variant_unref(r);
        GVariant *run = kwinCall(mBus, "/Scripting", "org.kde.kwin.Scripting", "start", nullptr, nullptr);
        if (run) g_variant_unref(run);
        return true;
    }

    // The KWin script functions aren't directly callable over D-Bus in all KWin versions; the
    // production path invokes them through the script's own registered object. Here we implement
    // the operations best-effort and leave listWindows returning what KWin's Workspace exposes,
    // filled in when validated on a Plasma session. For now these are safe no-ops when KWin is
    // absent (the common case on this dev box and on the GNOME track).
    std::vector<WindowInfo> KWinBridge::listWindows()
    {
        // TODO(L2/Plasma): parse the KWin script's listWindows() JSON reply.
        return {};
    }
    void KWinBridge::activate(const std::string &) {}
    void KWinBridge::close(const std::string &) {}
    void KWinBridge::minimizeAll() {}
    void KWinBridge::setMaximizedDefault(bool) {}
    void KWinBridge::tilePair(const std::string &l, const std::string &r, double ratio)
    {
        mLeftId = l; mRightId = r; mSplitRatio = ratio;
    }
    void KWinBridge::setRatio(double ratio) { mSplitRatio = ratio; }
    void KWinBridge::untile() { mLeftId.clear(); mRightId.clear(); }
    void KWinBridge::back()
    {
        // Inject Esc via KWin as the universal fallback (plan §3.2.4). A browser/file-manager
        // allowlist maps to Alt+Left in the shell before reaching here.
    }
    void KWinBridge::requestThumbnail(const std::string &, PngSink) {}  // Wayland: no cross-client pixels

} // namespace androidshell
} // namespace arstro
