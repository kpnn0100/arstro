#include "AppModelCodec.h"

namespace arstro
{
namespace solaris
{
    const std::vector<ModelField> &appModelFields()
    {
        static const std::vector<ModelField> f = {
            {"screen", "string", "home | project"},
            {"projectPath", "string", "the open .slp"},
            {"projectName", "string", "the song's name"},
            {"dirty", "bool", "unsaved changes"},
            {"bpm", "number", "tempo"},
            {"sig", "string", "meter, n/d"},
            {"sampleRate", "int", "the project's rate"},
            {"masterGain", "number", "dB"},
            {"masterOut", "string[]", "the port ids the master feeds"},
            {"mixers", "object[]", "mixer pages in order"},
            {"mixers[].id", "string", ""},
            {"mixers[].name", "string", ""},
            {"mixers[].order", "int", "processing and tab order"},
            {"mixers[].strips", "string[]", "its strips in processing order"},
            {"strips", "object[]", "every strip, in processing order"},
            {"strips[].id", "string", ""},
            {"strips[].name", "string", ""},
            {"strips[].kind", "string", "audio | instrument | bus"},
            {"strips[].mixer", "string", "the mixer it lives on (resolved)"},
            {"strips[].order", "int", "position on its mixer"},
            {"strips[].out", "string", "master, a strip id, or a port id (resolved)"},
            {"strips[].gain", "number", "dB"},
            {"strips[].pan", "number", "−1 … +1"},
            {"strips[].mute", "bool", ""},
            {"strips[].solo", "bool", ""},
            {"strips[].audible", "bool", "false when muted or silenced by another strip's solo (R-MIX-7)"},
            {"strips[].colour", "int", "−1 = none"},
            {"strips[].sends", "object[]", "its sends"},
            {"strips[].sends[].id", "string", ""},
            {"strips[].sends[].to", "string", "a strip on a later mixer, master, or a port"},
            {"strips[].sends[].gain", "number", "dB"},
            {"strips[].sends[].pre", "bool", "pre-fader"},
            {"strips[].devices", "object[]", "the rack, in order (an instrument strip's first is its instrument)"},
            {"strips[].devices[].id", "string", ""},
            {"strips[].devices[].type", "string", "the DSP registry type"},
            {"strips[].devices[].label", "string", "the registry's label"},
            {"strips[].devices[].instrument", "bool", ""},
            {"strips[].devices[].bypass", "bool", ""},
            {"strips[].devices[].known", "bool", "false when this build's registry lacks the type (kept, not played)"},
            {"strips[].devices[].params", "object[]", "every registry parameter, stored or default (R-UI-5)"},
            {"strips[].devices[].params[].name", "string", "the registry name, e.g. filter.cutoff"},
            {"strips[].devices[].params[].label", "string", ""},
            {"strips[].devices[].params[].unit", "string", "Hz, dB, ms, st, ct, oct, or empty"},
            {"strips[].devices[].params[].text", "string", "as stored: a number, or a choice's name"},
            {"strips[].devices[].params[].value", "number", "engineering units; a choice = its index"},
            {"strips[].devices[].params[].min", "number", ""},
            {"strips[].devices[].params[].max", "number", ""},
            {"strips[].devices[].params[].def", "number", ""},
            {"strips[].devices[].params[].choices", "string[]", "empty unless a choice"},
            {"strips[].clipCount", "int", "fed by: clips playing through it (R-MIX-8)"},
            {"strips[].fromLanes", "string[]", "fed by: the lanes those clips are drawn on"},
            {"strips[].fromStrips", "string[]", "fed by: strips whose output or a send lands here"},
            {"masterDevices", "object[]", "the master's rack, shaped like strips[].devices"},
            {"lanes", "object[]", "timeline rows, in order"},
            {"lanes[].id", "string", ""},
            {"lanes[].name", "string", ""},
            {"lanes[].order", "int", ""},
            {"lanes[].colour", "int", "−1 = none"},
            {"clips", "object[]", "every clip"},
            {"clips[].id", "string", ""},
            {"clips[].name", "string", ""},
            {"clips[].track", "string", "the strip it sounds through"},
            {"clips[].lane", "string", "the lane it is drawn on (\"\" = its strip's own row)"},
            {"clips[].kind", "string", "audio | note"},
            {"clips[].src", "string", "audio: the file"},
            {"clips[].pattern", "string", "note: the pattern it plays"},
            {"clips[].at", "number", "beats"},
            {"clips[].length", "number", "beats, resolved"},
            {"clips[].in", "number", "seconds into the file"},
            {"clips[].out", "number", "seconds into the file"},
            {"clips[].gain", "number", "dB"},
            {"clips[].fadeIn", "number", "beats"},
            {"clips[].fadeOut", "number", "beats"},
            {"clips[].loop", "bool", ""},
            {"clips[].offline", "bool", "the file could not be read"},
            {"clips[].linked", "int", "clips playing the same pattern, this one included (R-CLIP-3)"},
            {"patterns", "object[]", "every pattern"},
            {"patterns[].id", "string", ""},
            {"patterns[].name", "string", ""},
            {"patterns[].length", "number", "beats"},
            {"patterns[].clips", "int", "clips playing it"},
            {"patterns[].notes", "object[]", "sorted by (at, pitch)"},
            {"patterns[].notes[].pitch", "int", "0…127"},
            {"patterns[].notes[].at", "number", "beats from the pattern's start"},
            {"patterns[].notes[].length", "number", "beats"},
            {"patterns[].notes[].vel", "int", "1…127"},
            {"ports", "object[]", "logical ports (R-DEV-3)"},
            {"ports[].id", "string", ""},
            {"ports[].name", "string", ""},
            {"ports[].dir", "string", "in | out"},
            {"ports[].channels", "int", ""},
            {"lengthBeats", "number", "where the last clip ends"},
            {"audit", "string[]", "the last `audit`'s findings"},
            {"lastError", "string", "the last refusal or failure"},
            {"recents", "object[]", "Home's cards, newest first (R-HOME-1)"},
            {"recents[].path", "string", ""},
            {"recents[].name", "string", ""},
            {"recents[].bpm", "number", ""},
            {"recents[].lengthBeats", "number", ""},
            {"recents[].strips", "int", ""},
            {"recents[].missing", "bool", "the file is gone or no longer a song"},
            {"settings", "object", "the MACHINE's settings (R-SET-2)"},
            {"settings.sampleRate", "int", "new songs, and the clock device's rate"},
            {"settings.bufferSize", "int", "frames per device write"},
            {"settings.latencyMs", "number", "what that buffer costs at that rate"},
            {"settings.output", "string", "the clock device ('' = the system default)"},
            {"settings.input", "string", ""},
            {"settings.folders", "string[]", "the browser's quick-access folders, in order"},
            {"settings.ports", "string[]", "<port>=<device>:<channel> on this machine"},
            {"devices", "object[]", "from the last `devices list`"},
            {"devices[].id", "string", "what `settings set output=` takes"},
            {"devices[].name", "string", ""},
            {"devices[].dir", "string", "out | in"},
            {"devices[].channels", "int", ""},
            {"devices[].rate", "int", ""},
            {"browser", "object", "the folder last browsed"},
            {"browser.path", "string", ""},
            {"browser.entries", "object[]", "folders first, then by name"},
            {"browser.entries[].name", "string", ""},
            {"browser.entries[].path", "string", ""},
            {"browser.entries[].kind", "string", "dir | audio | song | other"},
            {"revision", "int", "bumps on every change", false},
        };
        return f;
    }

    namespace
    {
        Json strings(const std::vector<std::string> &v)
        {
            Json a = Json::array();
            for (const auto &s : v) a.push(Json::string(s));
            return a;
        }
        Json devices(const std::vector<DeviceModel> &ds)
        {
            Json a = Json::array();
            for (const auto &d : ds)
            {
                Json params = Json::array();
                for (const auto &p : d.params)
                    params.push(Json::object()
                                    .set("name", p.name).set("label", p.label).set("unit", p.unit).set("text", p.text)
                                    .set("value", p.value).set("min", p.min).set("max", p.max).set("def", p.def)
                                    .set("choices", strings(p.choices)));
                a.push(Json::object().set("id", d.id).set("type", d.type).set("label", d.label)
                           .set("instrument", d.instrument).set("bypass", d.bypass).set("known", d.known).set("params", params));
            }
            return a;
        }
    }

    Json modelToJson(const AppModel &m, bool stable)
    {
        Json j = Json::object();
        j.set("screen", m.screen).set("projectPath", m.projectPath).set("projectName", m.projectName).set("dirty", m.dirty)
            .set("bpm", m.bpm).set("sig", m.sig).set("sampleRate", m.sampleRate).set("masterGain", m.masterGain)
            .set("masterOut", strings(m.masterOut));
        Json mixers = Json::array();
        for (const auto &x : m.mixers)
            mixers.push(Json::object().set("id", x.id).set("name", x.name).set("order", x.order).set("strips", strings(x.strips)));
        j.set("mixers", mixers);
        Json strips = Json::array();
        for (const auto &s : m.strips)
        {
            Json sends = Json::array();
            for (const auto &sd : s.sends) sends.push(Json::object().set("id", sd.id).set("to", sd.to).set("gain", sd.gain).set("pre", sd.pre));
            strips.push(Json::object()
                            .set("id", s.id).set("name", s.name).set("kind", s.kind).set("mixer", s.mixer).set("order", s.order)
                            .set("out", s.out).set("gain", s.gain).set("pan", s.pan).set("mute", s.mute).set("solo", s.solo)
                            .set("audible", s.audible).set("colour", s.colour).set("sends", sends).set("devices", devices(s.devices))
                            .set("clipCount", s.clipCount).set("fromLanes", strings(s.fromLanes)).set("fromStrips", strings(s.fromStrips)));
        }
        j.set("strips", strips);
        j.set("masterDevices", devices(m.masterDevices));
        Json lanes = Json::array();
        for (const auto &l : m.lanes) lanes.push(Json::object().set("id", l.id).set("name", l.name).set("order", l.order).set("colour", l.colour));
        j.set("lanes", lanes);
        Json clips = Json::array();
        for (const auto &c : m.clips)
            clips.push(Json::object()
                           .set("id", c.id).set("name", c.name).set("track", c.track).set("lane", c.lane).set("kind", c.kind)
                           .set("src", c.src).set("pattern", c.pattern).set("at", c.at).set("length", c.length).set("in", c.in)
                           .set("out", c.out).set("gain", c.gain).set("fadeIn", c.fadeIn).set("fadeOut", c.fadeOut)
                           .set("loop", c.loop).set("offline", c.offline).set("linked", c.linked));
        j.set("clips", clips);
        Json patterns = Json::array();
        for (const auto &p : m.patterns)
        {
            Json notes = Json::array();
            for (const auto &n : p.notes)
                notes.push(Json::object().set("pitch", n.pitch).set("at", n.at).set("length", n.length).set("vel", n.vel));
            patterns.push(Json::object().set("id", p.id).set("name", p.name).set("length", p.length).set("clips", p.clips).set("notes", notes));
        }
        j.set("patterns", patterns);
        Json ports = Json::array();
        for (const auto &p : m.ports) ports.push(Json::object().set("id", p.id).set("name", p.name).set("dir", p.dir).set("channels", p.channels));
        j.set("ports", ports);
        j.set("lengthBeats", m.lengthBeats).set("audit", strings(m.audit)).set("lastError", m.lastError);
        Json recents = Json::array();
        for (const auto &r : m.recents)
            recents.push(Json::object().set("path", r.path).set("name", r.name).set("bpm", r.bpm).set("lengthBeats", r.lengthBeats)
                             .set("strips", r.strips).set("missing", r.missing));
        j.set("recents", recents);
        j.set("settings", Json::object().set("sampleRate", m.settings.sampleRate).set("bufferSize", m.settings.bufferSize)
                              .set("latencyMs", m.settings.latencyMs).set("output", m.settings.output).set("input", m.settings.input)
                              .set("folders", strings(m.settings.folders)).set("ports", strings(m.settings.ports)));
        Json devs = Json::array();
        for (const auto &d : m.devices)
            devs.push(Json::object().set("id", d.id).set("name", d.name).set("dir", d.dir).set("channels", d.channels).set("rate", d.rate));
        j.set("devices", devs);
        Json entries = Json::array();
        for (const auto &e : m.browser.entries) entries.push(Json::object().set("name", e.name).set("path", e.path).set("kind", e.kind));
        j.set("browser", Json::object().set("path", m.browser.path).set("entries", entries));
        if (!stable) j.set("revision", m.revision);
        return j;
    }
}
}
