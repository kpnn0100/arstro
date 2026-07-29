/*
 *  arstro-android-shell — DbusSystemServices implementation (M3.1). See the header.
 */
#include "system/DbusSystemServices.h"

#include <gio/gio.h>
#include <cstring>
#include <string>

namespace arstro
{
namespace androidshell
{
    namespace
    {
        constexpr int kTimeoutMs = 800;

        // org.freedesktop.DBus.Properties.Get(interface, prop) -> unwrapped GVariant* (caller
        // unrefs) or nullptr on any failure. Null-tolerant so a missing daemon is not fatal.
        GVariant *getProp(GDBusConnection *bus, const char *name, const char *path,
                          const char *iface, const char *prop)
        {
            if (!bus) return nullptr;
            GError *err = nullptr;
            GVariant *ret = g_dbus_connection_call_sync(
                bus, name, path, "org.freedesktop.DBus.Properties", "Get",
                g_variant_new("(ss)", iface, prop), G_VARIANT_TYPE("(v)"),
                G_DBUS_CALL_FLAGS_NONE, kTimeoutMs, nullptr, &err);
            if (err) { g_error_free(err); return nullptr; }
            GVariant *inner = nullptr;
            g_variant_get(ret, "(v)", &inner);
            g_variant_unref(ret);
            return inner;  // may be nullptr
        }

        void setProp(GDBusConnection *bus, const char *name, const char *path,
                     const char *iface, const char *prop, GVariant *value)
        {
            if (!bus) { if (value) g_variant_unref(g_variant_ref_sink(value)); return; }
            GError *err = nullptr;
            GVariant *ret = g_dbus_connection_call_sync(
                bus, name, path, "org.freedesktop.DBus.Properties", "Set",
                g_variant_new("(ssv)", iface, prop, value), nullptr,
                G_DBUS_CALL_FLAGS_NONE, kTimeoutMs, nullptr, &err);
            if (err) g_error_free(err);
            if (ret) g_variant_unref(ret);
        }

        // Small typed getters (each unrefs the intermediate variant).
        double getDouble(GDBusConnection *b, const char *n, const char *p, const char *i, const char *pr, double def)
        {
            GVariant *v = getProp(b, n, p, i, pr);
            if (!v) return def;
            double d = g_variant_is_of_type(v, G_VARIANT_TYPE_DOUBLE) ? g_variant_get_double(v) : def;
            g_variant_unref(v);
            return d;
        }
        unsigned getUint(GDBusConnection *b, const char *n, const char *p, const char *i, const char *pr, unsigned def)
        {
            GVariant *v = getProp(b, n, p, i, pr);
            if (!v) return def;
            unsigned u = def;
            if (g_variant_is_of_type(v, G_VARIANT_TYPE_UINT32)) u = g_variant_get_uint32(v);
            else if (g_variant_is_of_type(v, G_VARIANT_TYPE_BYTE)) u = g_variant_get_byte(v);
            g_variant_unref(v);
            return u;
        }
        bool getBool(GDBusConnection *b, const char *n, const char *p, const char *i, const char *pr, bool def)
        {
            GVariant *v = getProp(b, n, p, i, pr);
            if (!v) return def;
            bool r = g_variant_is_of_type(v, G_VARIANT_TYPE_BOOLEAN) ? g_variant_get_boolean(v) : def;
            g_variant_unref(v);
            return r;
        }
        std::string getObjPath(GDBusConnection *b, const char *n, const char *p, const char *i, const char *pr)
        {
            GVariant *v = getProp(b, n, p, i, pr);
            if (!v) return "";
            std::string s;
            if (g_variant_is_of_type(v, G_VARIANT_TYPE_OBJECT_PATH)) s = g_variant_get_string(v, nullptr);
            g_variant_unref(v);
            return s;
        }
        std::string getStr(GDBusConnection *b, const char *n, const char *p, const char *i, const char *pr)
        {
            GVariant *v = getProp(b, n, p, i, pr);
            if (!v) return "";
            std::string s;
            if (g_variant_is_of_type(v, G_VARIANT_TYPE_STRING)) s = g_variant_get_string(v, nullptr);
            g_variant_unref(v);
            return s;
        }

        // NM AccessPoint Strength (0..100) -> 0..4 bars (Android-style thresholds).
        int strengthToBars(unsigned pct)
        {
            if (pct == 0) return 0;
            if (pct < 25) return 1;
            if (pct < 50) return 2;
            if (pct < 75) return 3;
            return 4;
        }

        const char *NM = "org.freedesktop.NetworkManager";
        const char *NM_PATH = "/org/freedesktop/NetworkManager";
        const char *UPOWER = "org.freedesktop.UPower";
        const char *UPOWER_DEV = "/org/freedesktop/UPower/devices/DisplayDevice";
    }

    DbusSystemServices::DbusSystemServices()
    {
        GError *err = nullptr;
        mBus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &err);
        if (err) { g_error_free(err); mBus = nullptr; }
    }

    DbusSystemServices::~DbusSystemServices()
    {
        if (mBus) g_object_unref(mBus);
    }

    BatteryInfo DbusSystemServices::battery()
    {
        BatteryInfo b;
        // UPower State: 1=charging, 2=discharging, 3=empty, 4=fully-charged, 5=pending-charge...
        const double pct = getDouble(mBus, UPOWER, UPOWER_DEV, "org.freedesktop.UPower.Device", "Percentage", 100.0);
        const unsigned state = getUint(mBus, UPOWER, UPOWER_DEV, "org.freedesktop.UPower.Device", "State", 0);
        const bool present = getBool(mBus, UPOWER, UPOWER_DEV, "org.freedesktop.UPower.Device", "IsPresent", true);
        b.percent = (int)(pct + 0.5);
        b.charging = (state == 1 || state == 4);  // charging or full-on-AC reads as "charging" glyph
        b.present = present;
        return b;
    }

    WifiInfo DbusSystemServices::wifi()
    {
        WifiInfo w;
        w.enabled = getBool(mBus, NM, NM_PATH, NM, "WirelessEnabled", false);
        w.strength = 0;
        w.ssid.clear();
        // Walk PrimaryConnection -> (Active) SpecificObject -> AccessPoint Strength; Id -> ssid.
        const std::string primary = getObjPath(mBus, NM, NM_PATH, NM, "PrimaryConnection");
        if (!primary.empty() && primary != "/")
        {
            const char *ACT = "org.freedesktop.NetworkManager.Connection.Active";
            w.ssid = getStr(mBus, NM, primary.c_str(), ACT, "Id");
            const std::string ap = getObjPath(mBus, NM, primary.c_str(), ACT, "SpecificObject");
            if (!ap.empty() && ap != "/")
                w.strength = strengthToBars(
                    getUint(mBus, NM, ap.c_str(), "org.freedesktop.NetworkManager.AccessPoint", "Strength", 0));
        }
        return w;
    }

    BluetoothInfo DbusSystemServices::bluetooth()
    {
        BluetoothInfo bt;
        if (!mBus) return bt;
        // ObjectManager on org.bluez "/" -> a{oa{sa{sv}}}: any Adapter1.Powered / Device1.Connected.
        GError *err = nullptr;
        GVariant *ret = g_dbus_connection_call_sync(
            mBus, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
            nullptr, G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NONE, kTimeoutMs, nullptr, &err);
        if (err) { g_error_free(err); return bt; }

        GVariantIter *objs = nullptr;
        g_variant_get(ret, "(a{oa{sa{sv}}})", &objs);
        const gchar *opath = nullptr;
        GVariantIter *ifaces = nullptr;
        while (g_variant_iter_next(objs, "{&oa{sa{sv}}}", &opath, &ifaces))
        {
            const gchar *iname = nullptr;
            GVariantIter *props = nullptr;
            while (g_variant_iter_next(ifaces, "{&sa{sv}}", &iname, &props))
            {
                const bool isAdapter = std::strcmp(iname, "org.bluez.Adapter1") == 0;
                const bool isDevice = std::strcmp(iname, "org.bluez.Device1") == 0;
                if (isAdapter || isDevice)
                {
                    const gchar *pname = nullptr;
                    GVariant *pval = nullptr;
                    while (g_variant_iter_next(props, "{&sv}", &pname, &pval))
                    {
                        if (isAdapter && std::strcmp(pname, "Powered") == 0 &&
                            g_variant_is_of_type(pval, G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean(pval))
                            bt.powered = true;
                        if (isDevice && std::strcmp(pname, "Connected") == 0 &&
                            g_variant_is_of_type(pval, G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean(pval))
                            bt.connected = true;
                        g_variant_unref(pval);
                    }
                }
                g_variant_iter_free(props);
            }
            g_variant_iter_free(ifaces);
        }
        g_variant_iter_free(objs);
        g_variant_unref(ret);
        return bt;
    }

    void DbusSystemServices::setWifiEnabled(bool on)
    {
        setProp(mBus, NM, NM_PATH, NM, "WirelessEnabled", g_variant_new_boolean(on));
    }

    void DbusSystemServices::setBluetoothPowered(bool on)
    {
        // Find the first adapter and set Powered. (Enumerate via ObjectManager.)
        if (!mBus) return;
        GError *err = nullptr;
        GVariant *ret = g_dbus_connection_call_sync(
            mBus, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
            nullptr, G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NONE, kTimeoutMs, nullptr, &err);
        if (err) { g_error_free(err); return; }
        GVariantIter *objs = nullptr;
        g_variant_get(ret, "(a{oa{sa{sv}}})", &objs);
        const gchar *opath = nullptr;
        GVariantIter *ifaces = nullptr;
        std::string adapter;
        while (adapter.empty() && g_variant_iter_next(objs, "{&oa{sa{sv}}}", &opath, &ifaces))
        {
            const gchar *iname = nullptr;
            GVariantIter *props = nullptr;
            while (g_variant_iter_next(ifaces, "{&sa{sv}}", &iname, &props))
            {
                if (std::strcmp(iname, "org.bluez.Adapter1") == 0) adapter = opath;
                g_variant_iter_free(props);
            }
            g_variant_iter_free(ifaces);
        }
        g_variant_iter_free(objs);
        g_variant_unref(ret);
        if (!adapter.empty())
            setProp(mBus, "org.bluez", adapter.c_str(), "org.bluez.Adapter1", "Powered",
                    g_variant_new_boolean(on));
    }

    // Airplane mode is not a first-class NM property; approximate it as "wireless off". A real
    // implementation also touches WWAN + rfkill (plan §3.3) — deferred to M5.
    bool DbusSystemServices::airplaneMode()
    {
        return !getBool(mBus, NM, NM_PATH, NM, "WirelessEnabled", true);
    }
    void DbusSystemServices::setAirplaneMode(bool on)
    {
        setWifiEnabled(!on);
    }

} // namespace androidshell
} // namespace arstro
