/*
 *  arstro-android-shell — DbusSystemServices (M3.1): the real system backend.
 *
 *  Implements SystemServices (M1.5 interface) against the live system bus via GDBus (no new
 *  dependency — GLib/GIO comes with GTK). Wired for the status bar's needs (plan §3.3):
 *    battery()   — UPower  org.freedesktop.UPower /…/DisplayDevice  Percentage/State/IsPresent
 *    wifi()      — NetworkManager  WirelessEnabled + PrimaryConnection→AP Strength/Id
 *    bluetooth() — BlueZ  org.bluez  (ObjectManager: any Adapter1.Powered / Device1.Connected)
 *  and their toggles (setWifiEnabled / setBluetoothPowered).
 *
 *  Brightness / volume / airplane / DND / dark / night-light / power are STUBBED here (return
 *  sensible defaults / no-op) — they are wired in M3.2 (brightness) and M5 (QS tiles), where they
 *  are actually consumed. Tests use FakeSystemServices; this class is the runtime backend.
 *
 *  Reads are synchronous property Gets (fresh each poll); the shell polls on a timer, not per
 *  frame. Every getter is null/failure-tolerant: if a daemon is absent it falls back to a default
 *  instead of throwing, so the shell runs on a bare session.
 */
#pragma once
#include "system/SystemServices.h"

typedef struct _GDBusConnection GDBusConnection;

namespace arstro
{
namespace androidshell
{
    class DbusSystemServices : public SystemServices
    {
    public:
        DbusSystemServices();
        ~DbusSystemServices() override;

        // ---- wired to D-Bus (M3.1) ----
        WifiInfo wifi() override;
        BatteryInfo battery() override;
        BluetoothInfo bluetooth() override;
        void setWifiEnabled(bool on) override;
        void setBluetoothPowered(bool on) override;

        // ---- stubbed until their consumer lands (M3.2 brightness, M5 QS) ----
        double brightness() override { return 0.5; }
        double volume() override { return 0.5; }
        bool muted() override { return false; }
        bool airplaneMode() override;   // derived from NM (WirelessEnabled==false heuristic)
        bool doNotDisturb() override { return false; }
        bool darkTheme() override { return true; }
        bool nightLight() override { return false; }
        void setBrightness(double) override {}
        void setVolume(double) override {}
        void setMuted(bool) override {}
        void setAirplaneMode(bool on) override;  // best-effort: toggles Wi-Fi
        void setDoNotDisturb(bool) override {}
        void setDarkTheme(bool) override {}
        void setNightLight(bool) override {}
        void suspend() override {}
        void powerOff() override {}
        void reboot() override {}
        void lockSession() override {}
        void takeScreenshot() override {}

        // True if the system bus connected (else every getter returns defaults).
        bool connected() const { return mBus != nullptr; }

    private:
        GDBusConnection *mBus = nullptr;  // system bus (owned)
    };

} // namespace androidshell
} // namespace arstro
