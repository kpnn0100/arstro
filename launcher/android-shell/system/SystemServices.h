/*
 *  arstro-android-shell — SystemServices: the system/D-Bus seam (M1.5).
 *
 *  Platform-free interface over everything the status bar + quick settings read/toggle
 *  (plan §3.3): wifi, battery, bluetooth, brightness, volume, airplane, DND, dark theme,
 *  night light, and the power actions. The shared surfaces depend only on this interface
 *  and on FakeSystemServices (canned values) for tests; the real clients — NetworkManager,
 *  UPower, BlueZ, logind, PipeWire/libpulse, portals — land in M3 as DbusSystemServices.
 *  (Notifications are a separate seam, the notifyd service in M4.)
 */
#pragma once
#include <string>

namespace arstro
{
namespace androidshell
{
    struct WifiInfo
    {
        bool enabled = true;
        std::string ssid;   // empty = not connected
        int strength = 0;   // 0..4 bars
    };
    struct BatteryInfo
    {
        int percent = 100;
        bool charging = false;
        bool present = true;
    };
    struct BluetoothInfo
    {
        bool powered = false;
        bool connected = false;
    };

    class SystemServices
    {
    public:
        virtual ~SystemServices() = default;

        // ---- status (read) ----
        virtual WifiInfo wifi() = 0;
        virtual BatteryInfo battery() = 0;
        virtual BluetoothInfo bluetooth() = 0;
        virtual double brightness() = 0;   // 0..1
        virtual double volume() = 0;       // 0..1
        virtual bool muted() = 0;
        virtual bool airplaneMode() = 0;
        virtual bool doNotDisturb() = 0;
        virtual bool darkTheme() = 0;
        virtual bool nightLight() = 0;

        // ---- toggles / setters (write) ----
        virtual void setWifiEnabled(bool on) = 0;
        virtual void setBluetoothPowered(bool on) = 0;
        virtual void setBrightness(double v) = 0;
        virtual void setVolume(double v) = 0;
        virtual void setMuted(bool on) = 0;
        virtual void setAirplaneMode(bool on) = 0;
        virtual void setDoNotDisturb(bool on) = 0;
        virtual void setDarkTheme(bool on) = 0;
        virtual void setNightLight(bool on) = 0;

        // ---- power actions ----
        virtual void suspend() = 0;
        virtual void powerOff() = 0;
        virtual void reboot() = 0;
        virtual void lockSession() = 0;
        virtual void takeScreenshot() = 0;
    };

    // Canned, in-memory implementation for tests and for running the shell before the real
    // D-Bus clients exist (M3). Setters mutate the held state so a toggle round-trips; power
    // actions just count calls (so a test can assert they were invoked without side effects).
    class FakeSystemServices : public SystemServices
    {
    public:
        WifiInfo wifiState{true, "arstro-wifi", 3};
        BatteryInfo batteryState{72, false, true};
        BluetoothInfo bluetoothState{true, false};
        double brightnessLevel = 0.6;
        double volumeLevel = 0.4;
        bool isMuted = false;
        bool airplane = false;
        bool dnd = false;
        bool dark = true;
        bool night = false;
        int suspendCalls = 0, powerOffCalls = 0, rebootCalls = 0, lockCalls = 0, screenshotCalls = 0;

        WifiInfo wifi() override { return wifiState; }
        BatteryInfo battery() override { return batteryState; }
        BluetoothInfo bluetooth() override { return bluetoothState; }
        double brightness() override { return brightnessLevel; }
        double volume() override { return volumeLevel; }
        bool muted() override { return isMuted; }
        bool airplaneMode() override { return airplane; }
        bool doNotDisturb() override { return dnd; }
        bool darkTheme() override { return dark; }
        bool nightLight() override { return night; }

        void setWifiEnabled(bool on) override { wifiState.enabled = on; if (!on) { wifiState.ssid.clear(); wifiState.strength = 0; } }
        void setBluetoothPowered(bool on) override { bluetoothState.powered = on; if (!on) bluetoothState.connected = false; }
        void setBrightness(double v) override { brightnessLevel = v; }
        void setVolume(double v) override { volumeLevel = v; }
        void setMuted(bool on) override { isMuted = on; }
        void setAirplaneMode(bool on) override { airplane = on; if (on) { setWifiEnabled(false); setBluetoothPowered(false); } }
        void setDoNotDisturb(bool on) override { dnd = on; }
        void setDarkTheme(bool on) override { dark = on; }
        void setNightLight(bool on) override { night = on; }

        void suspend() override { ++suspendCalls; }
        void powerOff() override { ++powerOffCalls; }
        void reboot() override { ++rebootCalls; }
        void lockSession() override { ++lockCalls; }
        void takeScreenshot() override { ++screenshotCalls; }
    };

} // namespace androidshell
} // namespace arstro
