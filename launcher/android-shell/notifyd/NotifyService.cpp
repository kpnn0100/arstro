/*
 *  arstro-android-shell — NotifyService implementation (M4.1). See the header.
 */
#include "notifyd/NotifyService.h"

#include <gio/gio.h>
#include <cstdio>
#include <cstring>

namespace arstro
{
namespace androidshell
{
    namespace
    {
        const char *kIntrospectionXml =
            "<node>"
            "  <interface name='org.freedesktop.Notifications'>"
            "    <method name='GetCapabilities'>"
            "      <arg type='as' name='capabilities' direction='out'/>"
            "    </method>"
            "    <method name='Notify'>"
            "      <arg type='s' name='app_name' direction='in'/>"
            "      <arg type='u' name='replaces_id' direction='in'/>"
            "      <arg type='s' name='app_icon' direction='in'/>"
            "      <arg type='s' name='summary' direction='in'/>"
            "      <arg type='s' name='body' direction='in'/>"
            "      <arg type='as' name='actions' direction='in'/>"
            "      <arg type='a{sv}' name='hints' direction='in'/>"
            "      <arg type='i' name='expire_timeout' direction='in'/>"
            "      <arg type='u' name='id' direction='out'/>"
            "    </method>"
            "    <method name='CloseNotification'>"
            "      <arg type='u' name='id' direction='in'/>"
            "    </method>"
            "    <method name='GetServerInformation'>"
            "      <arg type='s' name='name' direction='out'/>"
            "      <arg type='s' name='vendor' direction='out'/>"
            "      <arg type='s' name='version' direction='out'/>"
            "      <arg type='s' name='spec_version' direction='out'/>"
            "    </method>"
            "    <signal name='NotificationClosed'>"
            "      <arg type='u' name='id'/>"
            "      <arg type='u' name='reason'/>"
            "    </signal>"
            "    <signal name='ActionInvoked'>"
            "      <arg type='u' name='id'/>"
            "      <arg type='s' name='action_key'/>"
            "    </signal>"
            "  </interface>"
            "</node>";

        GDBusNodeInfo *gNode = nullptr;

        void handleMethod(GDBusConnection *, const gchar *, const gchar *, const gchar *,
                          const gchar *method, GVariant *params, GDBusMethodInvocation *inv,
                          gpointer user)
        {
            auto *self = static_cast<NotifyService *>(user);
            if (std::strcmp(method, "GetServerInformation") == 0)
            {
                g_dbus_method_invocation_return_value(
                    inv, g_variant_new("(ssss)", "arstro-android-shell", "arstro", "0.1", "1.2"));
            }
            else if (std::strcmp(method, "GetCapabilities") == 0)
            {
                const char *caps[] = {"body", "actions", "body-markup", "icon-static", "persistence", nullptr};
                g_dbus_method_invocation_return_value(inv, g_variant_new("(^as)", caps));
            }
            else if (std::strcmp(method, "Notify") == 0)
            {
                const gchar *app = nullptr, *icon = nullptr, *summary = nullptr, *body = nullptr;
                guint replaces = 0;
                gint timeout = -1;
                GVariantIter *actionsIter = nullptr;
                GVariant *hints = nullptr;
                g_variant_get(params, "(&su&s&s&sas@a{sv}i)", &app, &replaces, &icon, &summary,
                              &body, &actionsIter, &hints, &timeout);

                Notification n;
                n.id = replaces;
                n.appName = app ? app : "";
                n.iconName = icon ? icon : "";
                n.summary = summary ? summary : "";
                n.body = body ? body : "";
                // actions come as [key, label, key, label, ...]
                const gchar *a = nullptr;
                std::string pendingKey;
                bool haveKey = false;
                while (actionsIter && g_variant_iter_next(actionsIter, "&s", &a))
                {
                    if (!haveKey) { pendingKey = a ? a : ""; haveKey = true; }
                    else { n.actions.push_back({pendingKey, a ? a : ""}); haveKey = false; }
                }
                if (actionsIter) g_variant_iter_free(actionsIter);
                // hints: urgency (y), transient (b), value (i)
                if (hints)
                {
                    GVariant *u = g_variant_lookup_value(hints, "urgency", G_VARIANT_TYPE_BYTE);
                    if (u) { n.urgency = g_variant_get_byte(u); g_variant_unref(u); }
                    GVariant *tr = g_variant_lookup_value(hints, "transient", G_VARIANT_TYPE_BOOLEAN);
                    if (tr) { n.transient = g_variant_get_boolean(tr); g_variant_unref(tr); }
                    GVariant *val = g_variant_lookup_value(hints, "value", G_VARIANT_TYPE_INT32);
                    if (val) { n.hasProgress = true; n.progress = g_variant_get_int32(val); g_variant_unref(val); }
                    g_variant_unref(hints);
                }
                const uint32_t id = self->store().addOrReplace(std::move(n));
                g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", id));
            }
            else if (std::strcmp(method, "CloseNotification") == 0)
            {
                guint id = 0;
                g_variant_get(params, "(u)", &id);
                self->store().close(id);
                self->emitClosed(id, 3);  // closed by CloseNotification call
                g_dbus_method_invocation_return_value(inv, nullptr);
            }
            else
            {
                g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                                      "unknown method %s", method);
            }
        }

        const GDBusInterfaceVTable kVtable = {handleMethod, nullptr, nullptr, {nullptr}};

        void nameAcquiredTramp(GDBusConnection *, const gchar *, gpointer user)
        {
            std::fprintf(stderr, "arstro-android-shell: owns org.freedesktop.Notifications\n");
            static_cast<NotifyService *>(user)->markOwned();
        }
        void nameLostTramp(GDBusConnection *, const gchar *, gpointer)
        {
            std::fprintf(stderr, "arstro-android-shell: org.freedesktop.Notifications already owned "
                                 "(running degraded; store still usable for the mirror path)\n");
        }
    }

    bool NotifyService::start()
    {
        GError *err = nullptr;
        mBus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
        if (err) { g_error_free(err); mBus = nullptr; return false; }
        if (!gNode) gNode = g_dbus_node_info_new_for_xml(kIntrospectionXml, nullptr);
        GError *e2 = nullptr;
        mRegId = g_dbus_connection_register_object(mBus, "/org/freedesktop/Notifications",
                                                   gNode->interfaces[0], &kVtable, this, nullptr, &e2);
        if (e2) { g_error_free(e2); }
        mNameId = g_bus_own_name_on_connection(
            mBus, "org.freedesktop.Notifications", G_BUS_NAME_OWNER_FLAGS_NONE,
            nameAcquiredTramp, nameLostTramp, this, nullptr);
        return true;
    }

    void NotifyService::emitClosed(uint32_t id, uint32_t reason)
    {
        if (mBus)
            g_dbus_connection_emit_signal(mBus, nullptr, "/org/freedesktop/Notifications",
                                          "org.freedesktop.Notifications", "NotificationClosed",
                                          g_variant_new("(uu)", id, reason), nullptr);
    }

    void NotifyService::emitActionInvoked(uint32_t id, const std::string &actionKey)
    {
        if (mBus)
            g_dbus_connection_emit_signal(mBus, nullptr, "/org/freedesktop/Notifications",
                                          "org.freedesktop.Notifications", "ActionInvoked",
                                          g_variant_new("(us)", id, actionKey.c_str()), nullptr);
    }

    NotifyService::~NotifyService()
    {
        if (mNameId) g_bus_unown_name(mNameId);
        if (mRegId && mBus) g_dbus_connection_unregister_object(mBus, mRegId);
        if (mBus) g_object_unref(mBus);
    }

} // namespace androidshell
} // namespace arstro
