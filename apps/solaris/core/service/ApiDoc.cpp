#include "ApiDoc.h"
#include "AppModelCodec.h"
#include "Command.h"
#include "Event.h"
#include "Format.h"
#include "Json.h"
#include "KeyCombo.h"
#include "Keymap.h"
#include "Notation.h"
#include "Project.h"
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
        Json scales = Json::array(), grooveList = Json::array(); // R-ROLL-11
        for (const auto &k : scaleKinds())
        {
            Json st = Json::array();
            for (int x : k.steps) st.push(Json::integer(x));
            scales.push(Json::object().set("names", strings(k.names)).set("steps", st).set("label", k.label));
        }
        for (const auto &g : grooves())
        {
            Json off = Json::array(), acc = Json::array();
            for (double x : g.offset) off.push(Json::number(x));
            for (double x : g.accent) acc.push(Json::number(x));
            grooveList.push(Json::object().set("name", g.name).set("step", g.step).set("offset", off).set("accent", acc).set("label", g.label));
        }
        doc.set("notation", Json::object()
                                .set("pitch", "a number 0-127, a name (C4 = 60, F#3, Bb2, c-1 = 0) or a kit's pad (kick)")
                                .set("note", "<pitch>@<beat>[:<length>[:<vel>[:<chance>]]]")
                                .set("steps", "x = a note, X = an accent (127), . or - = a rest; spaces and | ignored")
                                .set("chords", chords)
                                .set("scale", "<key> <scale> — a key C … B (# or b), a scale below: `D dorian`, `Bb minor`")
                                .set("scales", scales)
                                .set("grooves", grooveList));
        Json actions = Json::array(), names = Json::array();
        for (const auto &a : keyActions())
            actions.push(Json::object().set("id", a.id).set("area", a.area).set("label", a.label).set("defaults", a.defaults));
        for (const auto &k : namedKeys()) names.push(Json::string(k.name));
        doc.set("keyboard", Json::object()
                                .set("setting", "key.<id>=<combos> | none | default (R-SET-4)")
                                .set("combo", "[Ctrl+][Alt+][Shift+]<key>; a list is comma-separated")
                                .set("keys", "A-Z, 0-9, F1-F12 and these").set("named", names)
                                .set("actions", actions));
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
        o += "| `<clip>.name` · `.at` · `.length` · `.fadeIn` · `.fadeOut` · `.gain` · `.loop` · `.in` · `.out` · `.reverse` | text · beats · beats · beats · beats · dB · bool · s · s · bool (audio, R-EDIT-1) |\n";
        o += "| `<lane>.name` · `.colour` · `.strip` (`<ch>` an instrument: the lane is its track; `none`: plain — R-LANE-3) · `<mixer>.name` · `<send>.gain` · `.pre` · `.sidechain` (a key, R-MIX-15) · `<sampler>.sample` (its sound, a file; R-EDM-8) · `<pattern>.name` · `.length` · `<port>.name` · `.channels` | |\n";
        o += "| `<lane group>.name` · `.folded` · `<lane>.group` (R-ARR-3) | text · `on` (one row) / `off` · `<lg>` joins it (gathered with it) / `none` leaves it (placed after it) |\n";
        o += "| `<device>.bypass` · `<device>.<param>` | bool · any parameter of its type below, in its unit; a choice by name |\n";
        o += "| `<automation>.name` · `.unit` · `.min` · `.max` · `.loop` · `.at` · `.length` (read: also `.from` · `.points`) | text · text · number · number · beats (0: no loop) · beats · beats (0: to the end) — R-AUTO-12 |\n";
        o += "| any NUMBER above (a strip's gain/pan, a send's gain, `project.masterGain`, a numeric device parameter) `=<formula>` | "
             "binds it (R-AUTO-1): numbers, `+ - * / ^ ( )`, `sin cos tan abs sign min max clamp lerp pow exp log sqrt floor ceil "
             "round frac`, `pi`, `beat bar bpm t`, an automation id (`au_1`), another numeric address (a link). `get` prints "
             "the formula; a plain number clears it |\n";
        o += "\n## Notation (R-SVC-8)\n\n";
        o += "| what | written | e.g. |\n|---|---|---|\n";
        o += "| a pitch (`--pitch`, `--to-pitch`, a note) | a number 0–127, a name — a letter, `#`/`b`, an octave; C4 = 60, c-1 = 0 — or a "
             "kit's pad (below, case and `-` ignored) | `60` · `C4` · `F#3` · `Bb2` · `kick` · `closed-hat` |\n";
        o += "| a note (`notes add`) | `<pitch>@<beat>[:<length>[:<vel>[:<chance>]]]`; an empty field keeps the default; the chance 0 … 1 "
             "(R-ROLL-11) | `C4@0:0.5:90` · `kick@1` · `E4@2::70` · `hat@1.5:::0.5` |\n";
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
        o += "\n| a scale (`pattern scale`, R-ROLL-11) | its pitch classes above the key | |\n|---|---|---|\n";
        for (const auto &k : scaleKinds())
        {
            std::string names, st;
            for (const auto &n : k.names) names += (names.empty() ? "" : " · ") + std::string("`") + n + "`";
            for (int x : k.steps) st += (st.empty() ? "" : " ") + std::to_string(x);
            o += "| " + md(names) + " | " + st + " | " + k.label + " |\n";
        }
        o += "\nA groove (`pattern quantize --groove`, R-ROLL-11) is a cycle of grid steps from beat 0, each moved by its offset (a "
             "fraction of a step, + late) and its velocity scaled by its accent — empirical, shaped by ear.\n\n";
        o += "| groove | step | offsets | accents | |\n|---|---|---|---|---|\n";
        for (const auto &g : grooves())
        {
            std::string off, acc;
            for (double x : g.offset) off += (off.empty() ? "" : " ") + canonicalNumber(x);
            for (double x : g.accent) acc += (acc.empty() ? "" : " ") + canonicalNumber(x);
            o += "| `" + g.name + "` | " + canonicalNumber(g.step) + " | " + off + " | " + acc + " | " + g.label + " |\n";
        }
        o += "\n## Keyboard (R-SET-4)\n\nEvery shortcut is an action; `settings set key.<id>=<combos>` gives it this machine's keys "
             "(`none` takes them all away, `default` gives these back). A combo is `[Ctrl+][Alt+][Shift+]<key>` — the key A–Z, 0–9, "
             "F1–F12";
        for (const auto &k : namedKeys()) o += std::string(", ") + k.name;
        o += " — and a list is comma-separated (`\"Ctrl+Shift+Z, Ctrl+Y\"`, quoted for its space). Transport, Song and Lanes are one "
             "keyboard and the piano roll another: a combo means one action on each, and the roll's shadow the song's while a "
             "roll has the keys. `settings.keys` in the model is every action with its keys now.\n\n";
        o += "| action | area | what | default |\n|---|---|---|---|\n";
        for (const auto &a : keyActions()) o += "| `" + a.id + "` | " + a.area + " | " + md(a.label) + " | " + md(a.defaults) + " |\n";
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
