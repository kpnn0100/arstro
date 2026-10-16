// SolarisService — MIDI learn and the transport keys (R-CTRL-1). A MIDI control (`<n>` on any channel, `<ch>/<n>` on
// one) moves a number a formula could drive; the mappings are the machine's settings, never the song's. What a control
// does arrives on the host's thread into a lock-free queue `pump` drains: a moved control becomes a `set` line, a
// transport key a `transport` line, both landing as a plugin editor's edits do — when no command is running.
#include "Bindings.h"
#include "FaderLaw.h"
#include "Format.h"
#include "SolarisService.h"
#include "device/Device.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    namespace
    {
        /** `<n>` (0–127) or `<ch>/<n>` (1–16, 0–127) → its canonical spelling; false and why when it is neither. */
        bool controlKey(const std::string &text, std::string &key, std::string &err)
        {
            const auto slash = text.find('/');
            const std::string chText = slash == std::string::npos ? std::string() : text.substr(0, slash);
            const std::string nText = slash == std::string::npos ? text : text.substr(slash + 1);
            auto whole = [](const std::string &t, int lo, int hi, int &v) {
                if (t.empty() || t.size() > 3 || t.find_first_not_of("0123456789") != std::string::npos) return false;
                v = std::atoi(t.c_str());
                return v >= lo && v <= hi;
            };
            int ch = 0, n = 0;
            if (!whole(nText, 0, 127, n) || (slash != std::string::npos && !whole(chText, 1, 16, ch)))
            {
                err = "`" + text + "` is not a MIDI control — `<n>` (0–127, any channel) or `<ch>/<n>` (channel 1–16)";
                return false;
            }
            key = slash == std::string::npos ? std::to_string(n) : std::to_string(ch) + "/" + std::to_string(n);
            return true;
        }
    }

    bool SolarisService::mapInert(const std::string &address) const
    {
        AddressSpec sp;
        std::string err;
        return !mOpen || !describeAddress(mProject, address, sp, err);
    }

    bool SolarisService::controlValue(const std::string &address, double travel, std::string &text)
    {
        // where a control's travel (0 … 1) puts the number: a gain by the fader law; a device's parameter by the
        // registry's normalisation (its taper, as a knob); a plugin's p<id> is its own 0 … 1; anything else linearly
        AddressSpec sp;
        std::string err;
        if (!mOpen || !describeAddress(mProject, address, sp, err)) return false;
        travel = std::clamp(travel, 0.0, 1.0);
        double x = sp.lo + (sp.hi - sp.lo) * travel;
        if (sp.kind == AddressSpec::StripGain || sp.kind == AddressSpec::SendGain || sp.kind == AddressSpec::MasterGain) x = faderDb(travel);
        else if (sp.kind == AddressSpec::DeviceParam && sp.paramIndex >= 0)
            if (const DeviceNode *d = mProject.device(sp.node))
                if (const DeviceType *t = DeviceRegistry::find(d->type)) x = valueFromNormalized(t->params[(size_t)sp.paramIndex], travel);
        x = std::clamp(x, sp.lo, sp.hi);
        if (sp.integer) x = std::round(x);
        text = canonicalNumber(x);
        return true;
    }

    void SolarisService::controlMidi()
    {
        // the input listens while there are mappings or a learn waits (as it does for an armed strip) — asked once
        if ((!mSettings.maps.empty() || !mMapLearning.empty()) && !mMidiIn && !mControlTried)
        {
            mControlTried = true;
            std::string err;
            if (!ensureMidi(err)) emit(Event(Event::Kind::Info).with("text", "the MIDI mappings cannot listen: " + err));
        }
        if (!mCtlQ) return;
        std::map<std::string, std::string> moved; // address → its value text, the last of each
        bool learned = false;
        MidiEvent e;
        while (mCtlQ->pop(e))
        {
            if (e.mmc())
            {
                // MIDI Machine Control: a keyboard's transport keys
                const double bar = std::max(1, std::atoi(mProject.header.sig.c_str()));
                const double at = mModel.transport.position;
                switch (e.d1)
                {
                case 0x01: case 0x09: mControlLines.push_back("transport stop"); break;  // Stop, Pause
                case 0x02: case 0x03: mControlLines.push_back("transport play"); break;  // Play, Deferred Play
                case 0x06: mControlLines.push_back("transport record"); break;             // Record Strobe
                case 0x04: mControlLines.push_back("transport seek " + canonicalNumber(std::floor(at / bar + 1e-9) * bar + bar)); break; // Fast Forward
                case 0x05: mControlLines.push_back("transport seek " + canonicalNumber(std::max(0.0, std::ceil(at / bar - 1e-9) * bar - bar))); break; // Rewind
                default: break;
                }
                continue;
            }
            if (e.kind() != 0xB0) continue;
            const std::string own = std::to_string((e.status & 0x0F) + 1) + "/" + std::to_string(e.d1), any = std::to_string(e.d1);
            if (!mMapLearning.empty())
            {
                // learned: THIS control, on its channel — its value is not taken
                auto &maps = mSettings.maps;
                maps.erase(std::remove_if(maps.begin(), maps.end(), [&](const auto &m) { return m.first == own; }), maps.end());
                maps.emplace_back(own, mMapLearning);
                std::string err;
                saveSettings(err);
                emit(Event(Event::Kind::MapLearned).with("cc", own).with("address", mMapLearning));
                emit(Event(Event::Kind::SettingsChanged).with("what", "map." + own));
                mMapLearning.clear();
                learned = true;
                continue;
            }
            std::string address;
            for (const auto &m : mSettings.maps)
                if (m.first == own) address = m.second;
            if (address.empty())
                for (const auto &m : mSettings.maps)
                    if (m.first == any) address = m.second;
            std::string text;
            if (!address.empty() && controlValue(address, e.d2 / 127.0, text)) moved[address] = text;
        }
        if (!moved.empty())
        {
            std::string line = "set";
            for (const auto &m : moved) line += " " + m.first + "=" + m.second;
            mControlLines.push_back(line);
        }
        if (learned) refreshModel();
    }

    bool SolarisService::mapCommand(const Command &c, std::string &err)
    {
        auto &maps = mSettings.maps;
        if (c.kind == K::MapList)
        {
            for (const auto &m : maps) mOutput += m.first + "\t" + m.second + (mapInert(m.second) ? "\t(inert)" : "") + "\n";
            return true;
        }
        if (c.kind == K::MapLearn)
        {
            if (c.has("cancel"))
            {
                mMapLearning.clear();
                refreshModel();
                mOutput = "not waiting\n";
                return true;
            }
            if (c.arg(0).empty()) { err = "map learn <address> — or --cancel"; return false; }
            AddressSpec sp;
            if (!requireOpen(err) || !describeAddress(mProject, c.arg(0), sp, err)) return false;
            if (!ensureMidi(err)) return false; // nothing could be learned
            mMapLearning = c.arg(0);
            refreshModel();
            mOutput = "move a MIDI control for " + mMapLearning + "\n";
            return true;
        }
        std::string key;
        if (!controlKey(c.arg(0), key, err)) return false;
        const Settings keep = mSettings;
        if (c.kind == K::MapRemove)
        {
            const auto n = maps.size();
            maps.erase(std::remove_if(maps.begin(), maps.end(), [&](const auto &m) { return m.first == key; }), maps.end());
            if (maps.size() == n) { err = "no mapping for " + key + " (map list names them)"; return false; }
        }
        else
        {
            // K::MapAdd: a number the open song has, as a formula could drive it
            AddressSpec sp;
            if (!requireOpen(err) || !describeAddress(mProject, c.arg(1), sp, err)) return false;
            maps.erase(std::remove_if(maps.begin(), maps.end(), [&](const auto &m) { return m.first == key; }), maps.end());
            maps.emplace_back(key, c.arg(1));
            mOutput = key + "\t" + c.arg(1) + "\n";
        }
        if (!saveSettings(err)) { mSettings = keep; return false; }
        emit(Event(Event::Kind::SettingsChanged).with("what", "map." + key));
        if (c.kind == K::MapAdd)
        {
            std::string why;
            if (!ensureMidi(why)) emit(Event(Event::Kind::Info).with("text", "the mapping is kept, but cannot listen: " + why));
        }
        refreshModel();
        return true;
    }
}
}
