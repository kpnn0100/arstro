/*
 *  arstro-android-shell — NotifyService (M4.1): the org.freedesktop.Notifications daemon.
 *
 *  Exports the freedesktop Notifications spec (1.2) on the SESSION bus via GDBus and feeds a
 *  NotificationStore. On the Plasma session we OWN the name (plasmashell isn't running); on a
 *  stock desktop the name is already owned, so start() runs degraded (logs, store still usable for
 *  the M8 mirror path). UI-free — the panel/heads-up consume the store.
 */
#pragma once
#include "notifyd/NotificationStore.h"

#include <functional>

typedef struct _GDBusConnection GDBusConnection;

namespace arstro
{
namespace androidshell
{
    class NotifyService
    {
    public:
        explicit NotifyService(NotificationStore &store) : mStore(store) {}
        ~NotifyService();

        // Own org.freedesktop.Notifications on the session bus + register the object. Returns true
        // if the D-Bus setup started (name ownership is async; check owned() later).
        bool start();
        bool owned() const { return mOwned; }

        // Emit the spec signals (called when the user dismisses / invokes an action in the panel).
        void emitClosed(uint32_t id, uint32_t reason);   // 1 expired, 2 dismissed, 3 by-call, 4 undef
        void emitActionInvoked(uint32_t id, const std::string &actionKey);

        void markOwned() { mOwned = true; }  // called by the name-acquired callback
        NotificationStore &store() { return mStore; }

    private:
        NotificationStore &mStore;
        GDBusConnection *mBus = nullptr;
        unsigned int mNameId = 0;
        unsigned int mRegId = 0;
        bool mOwned = false;
    };

} // namespace androidshell
} // namespace arstro
