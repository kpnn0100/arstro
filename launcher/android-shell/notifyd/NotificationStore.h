/*
 *  arstro-android-shell — NotificationStore (M4.1): the UI-free notification model.
 *
 *  Holds the active notifications (freedesktop spec 1.2 shape) and notifies observers on change,
 *  so the notification panel (M4.2) and heads-up (M4.3) render from ONE source of truth. Populated
 *  by NotifyService (our own daemon) on the Plasma track, or by the GNOME extension mirror (M8).
 *  No drawing, no D-Bus here.
 */
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace androidshell
{
    struct NotifAction { std::string key, label; };

    struct Notification
    {
        uint32_t id = 0;
        std::string appName;
        std::string summary;   // title
        std::string body;
        std::string iconName;  // themed icon name / app icon (best-effort)
        std::vector<NotifAction> actions;
        int urgency = 1;       // 0 low, 1 normal, 2 critical
        double timestampMs = 0.0;
        bool transient = false;
        bool resident = false;
        bool hasProgress = false;
        int progress = 0;      // 0..100 when hasProgress
    };

    class NotificationStore
    {
    public:
        using Observer = std::function<void()>;

        // Add or replace (by id). id 0 = assign the next id. Returns the effective id.
        uint32_t addOrReplace(Notification n)
        {
            if (n.id == 0) n.id = mNextId++;
            for (auto &e : mItems)
                if (e.id == n.id) { e = std::move(n); notify(); return e.id; }
            mItems.push_back(std::move(n));
            const uint32_t id = mItems.back().id;
            if (id >= mNextId) mNextId = id + 1;
            notify();
            return id;
        }

        // Remove by id; returns true if something was removed.
        bool close(uint32_t id)
        {
            for (size_t i = 0; i < mItems.size(); ++i)
                if (mItems[i].id == id) { mItems.erase(mItems.begin() + i); notify(); return true; }
            return false;
        }

        void clearAll()
        {
            bool any = false;
            for (size_t i = mItems.size(); i-- > 0;)
                if (!mItems[i].resident) { mItems.erase(mItems.begin() + i); any = true; }
            if (any) notify();
        }

        const std::vector<Notification> &items() const { return mItems; }
        int count() const { return (int)mItems.size(); }
        int dismissibleCount() const
        {
            int n = 0;
            for (const auto &e : mItems) if (!e.resident) ++n;
            return n;
        }

        void observe(Observer fn) { if (fn) mObservers.push_back(std::move(fn)); }

    private:
        void notify() { for (auto &o : mObservers) o(); }
        std::vector<Notification> mItems;
        std::vector<Observer> mObservers;
        uint32_t mNextId = 1;
    };

} // namespace androidshell
} // namespace arstro
