/*
 *  arstro-android-shell — GnomeBridge implementation (M8). See the header. UNVERIFIED here.
 */
#include "compositor/GnomeBridge.h"

#include <gio/gio.h>

namespace arstro
{
namespace androidshell
{
    namespace
    {
        const char *NAME = "org.arstro.AndroidShell.Compositor";
        const char *PATH = "/org/arstro/AndroidShell/Compositor";
        constexpr int kTimeoutMs = 800;

        GVariant *call(GDBusConnection *bus, const char *method, GVariant *args, const GVariantType *reply)
        {
            if (!bus) return nullptr;
            GError *e = nullptr;
            GVariant *r = g_dbus_connection_call_sync(bus, NAME, PATH, NAME, method, args, reply,
                                                      G_DBUS_CALL_FLAGS_NONE, kTimeoutMs, nullptr, &e);
            if (e) { g_error_free(e); return nullptr; }
            return r;
        }
    }

    GnomeBridge::GnomeBridge()
    {
        GError *e = nullptr;
        mBus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &e);
        if (e) { g_error_free(e); mBus = nullptr; }
    }
    GnomeBridge::~GnomeBridge() { if (mBus) g_object_unref(mBus); }

    void GnomeBridge::call0(const char *method)
    {
        GVariant *r = call(mBus, method, nullptr, nullptr);
        if (r) g_variant_unref(r);
    }

    std::vector<WindowInfo> GnomeBridge::listWindows()
    {
        std::vector<WindowInfo> out;
        GVariant *r = call(mBus, "ListWindows", nullptr, G_VARIANT_TYPE("(a(sssbb))"));
        if (!r) return out;
        GVariantIter *it = nullptr;
        g_variant_get(r, "(a(sssbb))", &it);
        const gchar *id = nullptr, *app = nullptr, *title = nullptr;
        gboolean active = FALSE, full = FALSE;
        while (it && g_variant_iter_next(it, "(&s&s&sbb)", &id, &app, &title, &active, &full))
            out.push_back({id ? id : "", app ? app : "", title ? title : "", (bool)active, (bool)full});
        if (it) g_variant_iter_free(it);
        g_variant_unref(r);
        return out;
    }

    void GnomeBridge::activate(const std::string &id)
    {
        GVariant *r = call(mBus, "Activate", g_variant_new("(s)", id.c_str()), nullptr);
        if (r) g_variant_unref(r);
    }
    void GnomeBridge::close(const std::string &id)
    {
        GVariant *r = call(mBus, "Close", g_variant_new("(s)", id.c_str()), nullptr);
        if (r) g_variant_unref(r);
    }
    void GnomeBridge::minimizeAll() { call0("MinimizeAll"); }
    void GnomeBridge::back() { call0("Back"); }
    void GnomeBridge::tilePair(const std::string &l, const std::string &r, double ratio)
    {
        GVariant *rr = call(mBus, "TilePair", g_variant_new("(ssd)", l.c_str(), r.c_str(), ratio), nullptr);
        if (rr) g_variant_unref(rr);
    }

} // namespace androidshell
} // namespace arstro
