#include "ApiDoc.h"
#include "AppModelCodec.h"
#include "Command.h"
#include "Event.h"
#include "Format.h"
#include "Json.h"
#include "Notation.h"
#include "device/Device.h"

namespace arstro
{
namespace solaris
{
    namespace
    {
        Json strings(const std::vector<std::string> &v)
        {
            Json a = Json::array();
            for (const auto &s : v) a.push(Json::string(s));
            return a;
        }
        std::string range(const ParamSpec &p)
        {
            if (p.isChoice())
            {
                std::string s;
                for (size_t i = 0; i < p.choices.size(); ++i) s += (i ? " · " : "") + p.choices[i];
                return s;
            }
            return canonicalNumber(p.min) + " … " + canonicalNumber(p.max) + (p.unit.empty() ? "" : " " + p.unit);
        }
        std::string defText(const ParamSpec &p)
        {
            return p.isChoice() ? p.choices[(size_t)p.clamp(p.def)] : canonicalNumber(p.def);
        }
        std::string md(const std::string &s)
        {
            std::string o;
            for (char c : s) o += (c == '|') ? std::string("\\|") : std::string(1, c);
            return o;
        }
    }

    std::string apiJson()
    {
        Json doc = Json::object();
        doc.set("app", "solaris").set("generatedBy", "solaris-cc api --json");
        Json cmds = Json::array();
        for (const auto &s : commandSpecs())
            cmds.push(Json::object().set("verb", s.verb).set("usage", usageOf(s)).set("args", s.args)
                          .set("flags", strings(s.flags)).set("summary", s.summary).set("requirement", s.requirement));
        doc.set("commands", cmds);
        Json evs = Json::array();
        for (const auto &e : eventSpecs())
            evs.push(Json::object().set("name", e.name).set("fields", strings(e.fields)).set("summary", e.summary));
        doc.set("events", evs);
        Json model = Json::array();
        for (const auto &f : appModelFields())
            model.push(Json::object().set("path", f.path).set("type", f.type).set("summary", f.summary).set("stable", f.stable));
        doc.set("model", model);
        Json devs = Json::array();
        for (const auto &t : DeviceRegistry::types())
        {
            Json params = Json::array();
            for (const auto &p : t.params)
            {
                Json j = Json::object();
                j.set("name", p.name).set("label", p.label).set("unit", p.unit);
                if (p.isChoice()) j.set("choices", strings(p.choices)).set("default", defText(p));
                else j.set("min", p.min).set("max", p.max).set("default", p.def).set("integer", p.integer).set("log", p.logScale);
                params.push(j);
            }
            Json dev = Json::object();
            dev.set("type", t.name).set("label", t.label).set("kind", t.kind == DeviceKind::Instrument ? "instrument" : "effect")
                .set("summary", t.summary).set("params", params);
            if (!t.noteNames.empty())
            {
                Json pads = Json::array(); // a pitch can be written by these names (R-SVC-8)
                for (const auto &nn : t.noteNames) pads.push(Json::object().set("name", padText(nn.second)).set("note", nn.first));
                dev.set("pads", pads);
            }
            devs.push(dev);
        }
        doc.set("devices", devs);
        Json chords = Json::array();
        for (const auto &q : chordQualities())
        {
            Json iv = Json::array();
            for (int k : q.intervals) iv.push(Json::integer(k));
            chords.push(Json::object().set("names", strings(q.names)).set("intervals", iv).set("label", q.label));
        }
        doc.set("notation", Json::object()
                                .set("pitch", "a number 0-127, a name (C4 = 60, F#3, Bb2, c-1 = 0) or a kit's pad (kick)")
                                .set("note", "<pitch>@<beat>[:<length>[:<vel>]]")
                                .set("steps", "x = a note, X = an accent (127), . or - = a rest; spaces and | ignored")
                                .set("chords", chords));
        return doc.dump();
    }

    std::string apiMarkdown()
    {
        std::string o;
        o += "# Solaris — API\n\n";
        o += "> **Generated** by `solaris-cc api --md` from the tables the code runs on — the grammar table, the "
             "event table, the model's field table and the DSP library's device registry. Do not edit: a test "
             "regenerates this file and fails on any difference (R-API-1).\n\n";
        o += "A line is `<verb…> <positional…> [--flag value]…`; chain lines with ` : ` on the command line, "
             "one per line with `--script`, or a session on stdin with `shell` (one song, undo kept). `#` starts a "
             "comment, also at the end of a line. An unknown verb, flag, address or parameter is refused, naming the "
             "nearest candidates. A command that makes nodes prints the id it made first, then `made: kind=id …` for "
             "anything else it made. How an agent writes a song: `docs/AGENTS.md`.\n\n";
        o += "## Commands\n\n| usage | what | asked by |\n|---|---|---|\n";
        for (const auto &s : commandSpecs())
            o += "| `" + md(usageOf(s)) + "` | " + md(s.summary) + " | " + s.requirement + " |\n";
        o += "\n## Addresses (`set` / `get`)\n\n";
        o += "| address | value |\n|---|---|\n";
        o += "| `project.name` · `project.bpm` · `project.sig` · `project.masterGain` · `project.sampleRate` | text · 20…999 · n/d · dB · Hz |\n";
        o += "| `<strip>.name` · `.gain` · `.pan` · `.mute` · `.solo` · `.colour` | text · dB (−120…12) · −1…1 · bool · bool · −1…15 |\n";
        o += "| `<clip>.name` · `.at` · `.length` · `.fadeIn` · `.fadeOut` · `.gain` · `.loop` · `.in` · `.out` | text · beats · beats · beats · beats · dB · bool · s · s |\n";
        o += "| `<lane>.name` · `.colour` · `.strip` (`<ch>` an instrument: the lane is its track; `none`: plain — R-LANE-3) · `<mixer>.name` · `<send>.gain` · `.pre` · `.sidechain` (a key, R-MIX-15) · `<sampler>.sample` (its sound, a file; R-EDM-8) · `<pattern>.name` · `.length` · `<port>.name` · `.channels` | |\n";
        o += "| `<device>.bypass` · `<device>.<param>` | bool · any parameter of its type below, in its unit; a choice by name |\n";
        o += "| `<automation>.name` · `.unit` · `.min` · `.max` (read: also `.from` · `.points`) | text · text · number · number |\n";
        o += "| any NUMBER above (a strip's gain/pan, a send's gain, `project.masterGain`, a numeric device parameter) `=<formula>` | "
             "binds it (R-AUTO-1): numbers, `+ - * / ^ ( )`, `sin cos tan abs sign min max clamp lerp pow exp log sqrt floor ceil "
             "round frac`, `pi`, `beat bar bpm t`, an automation id (`au_1`), another numeric address (a link). `get` prints "
             "the formula; a plain number clears it |\n";
        o += "\n## Notation (R-SVC-8)\n\n";
        o += "| what | written | e.g. |\n|---|---|---|\n";
        o += "| a pitch (`--pitch`, `--to-pitch`, a note) | a number 0–127, a name — a letter, `#`/`b`, an octave; C4 = 60, c-1 = 0 — or a "
             "kit's pad (below, case and `-` ignored) | `60` · `C4` · `F#3` · `Bb2` · `kick` · `closed-hat` |\n";
        o += "| a note (`notes add`) | `<pitch>@<beat>[:<length>[:<vel>]]`; an empty field keeps the default | `C4@0:0.5:90` · `kick@1` · `E4@2::70` |\n";
        o += "| a step row (`pattern steps`) | `x` a note, `X` an accent (127), `.` or `-` a rest; spaces and `\\|` ignored | `x...x...x...X...` |\n";
        o += "| a chord (`note add --chord`) | a root (`C`, `F#`, `Bb`) then a quality below; the root sits in `--octave` (4) | `Cm7` · `F#dim` · `Bbmaj7` |\n";
        o += "\n| chord quality | semitones | |\n|---|---|---|\n";
        for (const auto &q : chordQualities())
        {
            std::string names, iv;
            for (const auto &n : q.names) names += (names.empty() ? "" : " · ") + std::string("`") + (n.empty() ? "C" : "C" + n) + "`";
            for (int k : q.intervals) iv += (iv.empty() ? "" : " ") + std::to_string(k);
            o += "| " + md(names) + " | " + iv + " | " + q.label + " |\n";
        }
        o += "\n## Events\n\nEach is one line: `[evt] <name> key=value …` — the log line, the `--watch` stream.\n\n";
        o += "| event | fields | when |\n|---|---|---|\n";
        for (const auto &e : eventSpecs())
        {
            std::string f;
            for (size_t i = 0; i < e.fields.size(); ++i) f += (i ? ", " : "") + e.fields[i];
            o += "| `" + e.name + "` | " + f + " | " + md(e.summary) + " |\n";
        }
        o += "\n## Model (`state print --json`)\n\n| field | type | meaning |\n|---|---|---|\n";
        for (const auto &f : appModelFields())
            o += "| `" + f.path + "` | " + f.type + " | " + md(f.summary) + (f.stable ? "" : " *(not in `--stable`)*") + " |\n";
        o += "\n## Devices — the DSP library's registry\n\n";
        o += "Every instrument and effect is a module of `core/DigitalSignalProcessing`; this section is read from "
             "its `DeviceRegistry` (R-DSP-2).\n";
        for (const auto &t : DeviceRegistry::types())
        {
            o += "\n### `" + t.name + "` — " + t.label + " (" + (t.kind == DeviceKind::Instrument ? "instrument" : "effect") + ")\n\n";
            o += md(t.summary) + "\n\n";
            if (!t.noteNames.empty())
            {
                o += "Pads — a pitch by name:";
                for (const auto &nn : t.noteNames) o += " `" + padText(nn.second) + "` " + std::to_string(nn.first) + (nn.first == t.noteNames.back().first ? "" : " ·");
                o += "\n\n";
            }
            o += "| parameter | range | default |\n|---|---|---|\n";
            for (const auto &p : t.params)
                o += "| `" + p.name + "` | " + md(range(p)) + " | " + defText(p) + (p.unit.empty() || p.isChoice() ? "" : " " + p.unit) + " |\n";
        }
        return o;
    }
}
}
