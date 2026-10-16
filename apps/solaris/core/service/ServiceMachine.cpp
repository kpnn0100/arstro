// SolarisService — the MACHINE's side (R-SET, R-DEV-1, R-BROWSE-1, R-HOME-1): settings, sample
// folders, devices, the browser, the recent songs. None of it is a song's: it persists in the
// host-given settings and recents files, and works with no song open.
#include "Compile.h"
#include "Format.h"
#include "Json.h"
#include "KeyCombo.h"
#include "Keymap.h"
#include "SolarisService.h"
#include "MixLaws.h"
#include "Player.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    void SolarisService::loadMachine()
    {
        if (!mHost.settingsPath.empty())
        {
            std::ifstream f(mHost.settingsPath);
            if (f)
            {
                std::stringstream ss;
                ss << f.rdbuf();
                mSettings = Settings::parse(ss.str());
            }
        }
        if (!mHost.recentsPath.empty())
        {
            std::ifstream f(mHost.recentsPath);
            std::string line;
            while (std::getline(f, line))
                if (!line.empty()) mRecents.push_back(line);
        }
    }

    bool SolarisService::saveSettings(std::string &err)
    {
        if (mHost.settingsPath.empty()) return true; // not persisted: a test, or a host that chose not to
        std::ofstream f(mHost.settingsPath, std::ios::binary);
        if (!f) { err = "cannot write " + mHost.settingsPath; return false; }
        f << mSettings.text();
        return (bool)f;
    }

    namespace fs = std::filesystem;

    namespace
    {
        // R-HOME-1: a recent song is named by its ABSOLUTE path — `project open song.slp` from one folder must
        // still be found by a window started in another
        std::string absolutePath(const std::string &p)
        {
            if (p.empty()) return p;
            std::error_code ec;
            const fs::path a = fs::absolute(fs::path(p), ec);
            return ec ? p : a.lexically_normal().string();
        }
    }

    void SolarisService::touchRecent(const std::string &given)
    {
        const std::string path = absolutePath(given);
        mRecentsStale = true;
        mRecents.erase(std::remove(mRecents.begin(), mRecents.end(), path), mRecents.end());
        mRecents.insert(mRecents.begin(), path);
        if (mRecents.size() > 20) mRecents.resize(20);
        if (!mHost.recentsPath.empty())
        {
            std::ofstream f(mHost.recentsPath, std::ios::binary);
            for (const auto &r : mRecents) f << r << "\n";
        }
        emit(Event(Event::Kind::RecentsChanged).with("count", (long long)mRecents.size()));
    }

    bool SolarisService::machineCommand(const Command &c, std::string &err)
    {
        switch (c.kind)
        {
        case K::SettingsSet:
        {
            Settings next = mSettings;
            std::vector<std::string> what, keyed;
            for (const auto &f : c.fields)
            {
                const std::string &k = f.first, &v = f.second;
                double x = 0;
                if (k == "sampleRate")
                {
                    if (!parseNumber(v, x) || x < 8000 || x > 192000 || x != std::floor(x)) { err = "sampleRate must be a whole rate from 8000 to 192000"; return false; }
                    next.sampleRate = (int)x;
                }
                else if (k == "bufferSize")
                {
                    if (!parseNumber(v, x) || x < 32 || x > 8192 || x != std::floor(x)) { err = "bufferSize must be 32 … 8192 frames"; return false; }
                    next.bufferSize = (int)x;
                }
                else if (k == "output") next.output = v;
                else if (k == "input") next.input = v;
                else if (k == "metronome" || k == "reducedMotion" || k == "showIds")
                {
                    if (v != "on" && v != "off") { err = k + " is on or off"; return false; }
                    (k == "metronome" ? next.metronome : k == "reducedMotion" ? next.reducedMotion : next.showIds) = v == "on";
                }
                else if (k == "metronomeLevel" || k == "auditionLevel")
                {
                    if (!parseNumber(v, x) || x < -40 || x > 6) { err = k + " must be dB from -40 to 6"; return false; }
                    (k == "metronomeLevel" ? next.metronomeLevel : next.auditionLevel) = x;
                }
                else if (k == "newBpm")
                {
                    if (!parseNumber(v, x) || x < 20 || x > 999) { err = "newBpm must be a tempo from 20 to 999"; return false; }
                    next.newBpm = x;
                }
                else if (k == "newSig")
                {
                    int n = 0, d = 0;
                    if (std::sscanf(v.c_str(), "%d/%d", &n, &d) != 2 || n < 1 || n > 32 || (d != 2 && d != 4 && d != 8 && d != 16))
                    { err = "newSig must be a meter like 4/4, 3/4 or 7/8"; return false; }
                    next.newSig = v;
                }
                else if (k.rfind("port.", 0) == 0 && k.size() > 5)
                {
                    const auto colon = v.rfind(':');
                    if (!v.empty() && (colon == std::string::npos || !parseNumber(v.substr(colon + 1), x) || x < 0))
                    { err = k + " must be <device>:<first channel> (or empty)"; return false; }
                    const std::string name = k.substr(5);
                    next.ports.erase(std::remove_if(next.ports.begin(), next.ports.end(), [&](const auto &p) { return p.first == name; }), next.ports.end());
                    if (!v.empty()) next.ports.emplace_back(name, v);
                }
                else if (k == "driver")
                {
                    // R-DEV-9: how the clock device is reached — the next play opens it
                    if (v != "pulse" && v != "pipewire") { err = "driver is pulse or pipewire"; return false; }
                    next.driver = v;
                    mPipewireDown = false; // set again: PipeWire is tried again
                }
                else if (k == "recordQuantize")
                {
                    // R-MIDI-2: where a recorded note's start lands
                    if (v != "off" && v != "1/4" && v != "1/8" && v != "1/16" && v != "1/32") { err = "recordQuantize is off, 1/4, 1/8, 1/16 or 1/32"; return false; }
                    next.recordQuantize = v;
                }
                else if (k == "countIn")
                {
                    if (!parseNumber(v, x) || x < 0 || x > 4 || x != std::floor(x)) { err = "countIn is bars, 0 … 4"; return false; }
                    next.countIn = (int)x;
                }
                else if (k == "midiInputs")
                {
                    // R-MIDI-2: which keyboards Solaris listens to — reopened at once when it listens
                    if (v.empty()) { err = "midiInputs is all, none, or port names separated by commas"; return false; }
                    next.midiInputs = v;
                }
                else if (k == "autosave")
                {
                    // R-SAFE-1: seconds between autosaves, or off
                    if (v == "off") next.autosave = 0;
                    else if (!parseNumber(v, x) || x < 1 || x > 3600 || x != std::floor(x)) { err = "autosave is seconds from 1 to 3600, or off"; return false; }
                    else next.autosave = (int)x;
                }
                else if (k.rfind("key.", 0) == 0)
                {
                    // R-SET-4: an action's keys — a comma list of combos, `none`, or `default`
                    const std::string id = k.substr(4);
                    const KeyAction *a = keyAction(id);
                    if (!a)
                    {
                        std::vector<std::string> ids;
                        for (const auto &x : keyActions()) ids.push_back(x.id);
                        const auto near = nearest(id, ids);
                        err = "no keyboard action `" + id + "`" + (near.empty() ? std::string(" (`solaris-cc api` lists them)") : " (did you mean: key." + near[0] + "?)");
                        return false;
                    }
                    next.keys.erase(std::remove_if(next.keys.begin(), next.keys.end(), [&](const auto &o) { return o.first == id; }), next.keys.end());
                    if (keyLower(keyTrim(v)) != "default")
                    {
                        std::vector<std::string> combos;
                        if (!parseCombos(v, combos, err)) { err = k + ": " + err; return false; }
                        if (combos != keysOf(*a, {})) next.keys.emplace_back(id, combosText(combos)); // its defaults again: no line
                    }
                    keyed.push_back(id);
                }
                else
                {
                    err = "no setting `" + k + "`";
                    const auto near = nearest(k, {"sampleRate", "bufferSize", "output", "input", "metronome", "metronomeLevel", "auditionLevel", "newBpm", "newSig", "reducedMotion", "showIds", "autosave", "driver", "midiInputs", "recordQuantize", "countIn"});
                    err += near.empty() ? std::string(" (settings: sampleRate, bufferSize, output, input, port.<name>, metronome, "
                                                      "metronomeLevel, auditionLevel, newBpm, newSig, reducedMotion, showIds, autosave, driver, midiInputs, recordQuantize, countIn, key.<action>)")
                                        : " (did you mean: " + near[0] + "?)";
                    return false;
                }
                what.push_back(k);
            }
            // a combo means one action on one keyboard: the song's (Transport, Song, Lanes) or the roll's — checked
            // once every key of the line is in, so one line can swap two actions' keys
            for (const auto &id : keyed)
            {
                const KeyAction &a = *keyAction(id);
                const auto mine = keysOf(a, next.keys);
                for (const auto &b : keyActions())
                {
                    if (b.id == a.id || keyGroup(b.area) != keyGroup(a.area)) continue;
                    for (const auto &c : keysOf(b, next.keys))
                        if (std::find(mine.begin(), mine.end(), c) != mine.end())
                        {
                            err = c + " is already \"" + b.label + "\" (key." + b.id + ") — give that one another key first, or `key." + b.id + "=none`";
                            return false;
                        }
                }
            }
            const Settings keep = mSettings;
            mSettings = next;
            if (!saveSettings(err)) { mSettings = keep; return false; }
            if (mPlayer) mPlayer->setClick(mSettings.metronome, engine::dbToLinear(mSettings.metronomeLevel)); // heard at once
            if (mMidiIn && mMidiOpenFor != mSettings.midiInputs)
            {
                std::string e; // R-MIDI-2: listening now to the keyboards it names
                if (!ensureMidi(e)) emit(Event(Event::Kind::Info).with("text", "MIDI: " + e));
            }
            for (const auto &w : what) emit(Event(Event::Kind::SettingsChanged).with("what", w));
            return true;
        }
        case K::SettingsPrint:
            if (!c.has("json")) { mOutput = mSettings.text(); return true; }
            {
                Json folders = Json::array(), ports = Json::array();
                for (const auto &f : mSettings.folders) folders.push(Json::string(f));
                for (const auto &p : mSettings.ports) ports.push(Json::string(p.first + "=" + p.second));
                mOutput = Json::object().set("sampleRate", mSettings.sampleRate).set("bufferSize", mSettings.bufferSize)
                              .set("output", mSettings.output).set("input", mSettings.input)
                              .set("folders", folders).set("ports", ports).set("metronome", mSettings.metronome)
                              .set("metronomeLevel", mSettings.metronomeLevel).set("auditionLevel", mSettings.auditionLevel).set("newBpm", mSettings.newBpm)
                              .set("newSig", mSettings.newSig).set("reducedMotion", mSettings.reducedMotion)
                              .set("showIds", mSettings.showIds).set("autosave", mSettings.autosave).set("driver", mSettings.driver).set("midiInputs", mSettings.midiInputs).set("recordQuantize", mSettings.recordQuantize).set("countIn", mSettings.countIn).set("keys", [&] {
                                  Json keys = Json::array(); // only the re-keyed actions, as the file has them
                                  for (const auto &k : mSettings.keys) keys.push(Json::string(k.first + "=" + k.second));
                                  return keys;
                              }()).dump();
            }
            return true;
        case K::FolderAdd:
        case K::FolderRemove:
        case K::FolderMove:
        {
            const std::string path = c.arg(0);
            auto &fs = mSettings.folders;
            const auto it = std::find(fs.begin(), fs.end(), path);
            const Settings keep = mSettings;
            if (c.kind == K::FolderAdd)
            {
                if (it != fs.end()) { err = path + " is already a sample folder"; return false; }
                std::vector<BrowserEntry> probe;
                if (mHost.listDir && !mHost.listDir(path, probe, err)) { err = "cannot list " + path + (err.empty() ? "" : ": " + err); return false; }
                fs.push_back(path);
            }
            else
            {
                if (it == fs.end()) { err = path + " is not a sample folder"; return false; }
                const long at = it - fs.begin();
                fs.erase(it);
                if (c.kind == K::FolderMove)
                {
                    if (!c.has("to")) { err = "folder move needs --to <index>"; mSettings = keep; return false; }
                    double to = 0;
                    if (!parseNumber(c.flag("to"), to) || to < 0 || to > (double)fs.size() || to != std::floor(to))
                    { err = "--to must be 0 … " + std::to_string(fs.size()); mSettings = keep; return false; }
                    fs.insert(fs.begin() + (long)to, path);
                    (void)at;
                }
            }
            if (!saveSettings(err)) { mSettings = keep; return false; }
            emit(Event(Event::Kind::SettingsChanged).with("what", "folders"));
            return true;
        }
        case K::DevicesList:
        {
            if (!mHost.listDevices) { err = "this build cannot list audio devices"; return false; }
            std::vector<DeviceInfo> devs;
            if (!mHost.listDevices(devs, err)) return false;
            mDevices = devs;
            for (const auto &d : mDevices)
                mOutput += d.dir + "\t" + d.id + "\t" + std::to_string(d.channels) + " ch · " + std::to_string(d.rate) + " Hz\t" + d.name + "\n";
            if (mDevices.empty()) mOutput = "no audio devices\n";
            if (mHost.listMidiInputs)
            {
                // R-MIDI-2: the MIDI inputs too, for Settings — quietly: a machine with none still lists its audio
                std::vector<MidiPort> ports;
                std::string e;
                if (mHost.listMidiInputs(ports, e))
                {
                    const std::vector<std::string> on = mMidiIn ? mMidiIn->listening() : std::vector<std::string>{};
                    mMidiPorts.clear();
                    for (const auto &pt : ports) mMidiPorts.push_back(MidiPortModel{pt.id, pt.name, std::find(on.begin(), on.end(), pt.id) != on.end()});
                    mModel.midiDevices = mMidiPorts;
                }
            }
            emit(Event(Event::Kind::DevicesChanged).with("count", (long long)mDevices.size()));
            return true;
        }
        case K::Browse:
        {
            if (c.has("search") || c.has("favourites")) // R-PRESET-3
            {
                if (c.has("search") && c.has("favourites")) { err = "browse --search <text> or --favourites, not both"; return false; }
                if (c.has("search") && c.flag("search").empty()) { err = "--search needs some text"; return false; }
                return searchSamples(c.arg(0), c.flag("search"), c.has("favourites"), err);
            }
            if (c.arg(0).empty()) { err = "browse <folder> — or --search <text>, --favourites"; return false; }
            if (!mHost.listDir) { err = "this build cannot list folders"; return false; }
            std::vector<BrowserEntry> entries;
            std::string why;
            if (!mHost.listDir(c.arg(0), entries, why)) { err = "cannot list " + c.arg(0) + (why.empty() ? "" : ": " + why); return false; }
            std::stable_sort(entries.begin(), entries.end(), [](const BrowserEntry &a, const BrowserEntry &b) {
                if ((a.kind == "dir") != (b.kind == "dir")) return a.kind == "dir";
                return a.name < b.name;
            });
            mBrowser.path = c.arg(0);
            mBrowser.entries = entries;
            mBrowser.search.clear();
            for (const auto &e : entries) mOutput += e.kind + "\t" + e.name + "\n";
            emit(Event(Event::Kind::BrowseChanged).with("path", mBrowser.path).with("entries", (long long)entries.size()));
            return true;
        }
        case K::RecentsRemove:
        {
            auto it = std::find(mRecents.begin(), mRecents.end(), c.arg(0));
            if (it == mRecents.end()) it = std::find(mRecents.begin(), mRecents.end(), absolutePath(c.arg(0)));
            if (it == mRecents.end()) { err = c.arg(0) + " is not on the recent list"; return false; }
            mRecents.erase(it);
            mRecentsStale = true;
            if (!mHost.recentsPath.empty())
            {
                std::ofstream f(mHost.recentsPath, std::ios::binary);
                for (const auto &r : mRecents) f << r << "\n";
            }
            emit(Event(Event::Kind::RecentsChanged).with("count", (long long)mRecents.size()));
            return true;
        }
        default:
            return false;
        }
    }
}
}
