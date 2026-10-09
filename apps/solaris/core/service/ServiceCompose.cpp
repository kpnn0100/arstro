// SolarisService — composition at an agent's size (R-SVC-8): notes in bulk and by name, step rows,
// pattern edits, and the views that read a song back (`ls`, `show`, `pattern print`). An audit had an
// agent spend 110 commands on a 16-bar song, 64 of them single notes with every pitch computed by
// hand; these are the lines it was missing. Every edit here runs inside `dispatch`'s all-or-nothing,
// so a bad token anywhere in a `notes add` leaves the pattern as it was, and the whole line is ONE
// undo step.
#include "Compile.h"
#include "Format.h"
#include "Notation.h"
#include "SolarisService.h"
#include "device/Device.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    namespace
    {
        bool sameTick(double a, double b) { return std::fabs(a - b) < 0.5 / kPpq; }
        void sortNotes(Pattern &pt)
        {
            std::stable_sort(pt.notes.begin(), pt.notes.end(), [](const Note &a, const Note &b) {
                return a.at != b.at ? a.at < b.at : a.pitch < b.pitch;
            });
        }
        // a note's place, as the notation writes it: "0", "1.5" — never "4.0"
        std::string beatText(double beats)
        {
            std::string s = canonicalBeats(beats);
            if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
            return s;
        }
        std::string q(const std::string &name) { return "\"" + name + "\""; }
        std::string countOf(size_t n, const char *one, const char *many) { return std::to_string(n) + " " + (n == 1 ? one : many); }
        double barBeats(const Project &p)
        {
            int num = 4, den = 4;
            std::sscanf(p.header.sig.c_str(), "%d/%d", &num, &den);
            return num > 0 && den > 0 ? num * 4.0 / den : 4.0;
        }
        bool beatsOf(const Command &c, const std::string &name, double fallback, double &out, std::string &err)
        {
            out = fallback;
            if (!c.has(name)) return true;
            if (!parseNumber(c.flag(name), out) || !std::isfinite(out) || out < 0)
            {
                err = "--" + name + " must be a number of beats ≥ 0, got `" + c.flag(name) + "`";
                return false;
            }
            out = toTick(out);
            return true;
        }
        bool velOf(const Command &c, int fallback, int &out, std::string &err)
        {
            out = fallback;
            if (!c.has("vel")) return true;
            double v = 0;
            if (!parseNumber(c.flag("vel"), v) || v != std::floor(v) || v < 1 || v > 127) { err = "--vel must be a whole number from 1 to 127"; return false; }
            out = (int)v;
            return true;
        }
        // every word of the positionals after the pattern: `notes add pt_1 "C4@0 E4@1" G4@2` is three notes
        std::vector<std::string> wordsAfterFirst(const Command &c)
        {
            std::vector<std::string> out;
            for (size_t i = 1; i < c.args.size(); ++i)
                for (const auto &w : tokenize(c.args[i])) out.push_back(w);
            return out;
        }
    }

    // ── pitches ─────────────────────────────────────────────────────────────────────────────────

    bool SolarisService::pitchOf(const std::string &text, const std::string &pattern, int &midi, std::string &err) const
    {
        double v = 0;
        if (parseNumber(text, v) && std::isfinite(v))
        {
            if (v != std::floor(v) || v < 0 || v > 127) { err = "a pitch number is a whole number 0–127, got " + text; return false; }
            midi = (int)v;
            return true;
        }
        std::string nameErr;
        if (parsePitchName(text, midi, nameErr)) return true;
        // a kit's pad: the instruments whose strips play this pattern — or, when none does yet, every kit
        // the registry has (a pattern written before its clip)
        std::vector<const DeviceType *> kits;
        for (const auto &c : mProject.clips)
        {
            if (c.pattern != pattern) continue;
            const Rack *rk = mProject.rack(c.track);
            const DeviceType *t = rk && !rk->devices.empty() ? DeviceRegistry::find(rk->devices.front().type) : nullptr;
            if (t && !t->noteNames.empty() && std::find(kits.begin(), kits.end(), t) == kits.end()) kits.push_back(t);
        }
        bool played = false;
        for (const auto &c : mProject.clips) played |= c.pattern == pattern;
        if (!played)
            for (const auto &t : DeviceRegistry::types())
                if (!t.noteNames.empty()) kits.push_back(&t);
        const std::string key = padKey(text);
        for (const DeviceType *t : kits)
            for (const auto &nn : t->noteNames)
                if (padKey(nn.second) == key) { midi = nn.first; return true; }
        if (nameErr.find("outside") != std::string::npos || kits.empty())
        {
            err = nameErr;
            if (kits.empty()) err += "; " + pattern + " plays through no kit, so a pad name does not apply";
            return false;
        }
        err = "`" + text + "` is not a pitch (60, C4, F#3) nor a pad of " + kits.front()->name + " (pads:";
        for (const auto &nn : kits.front()->noteNames) err += " " + padText(nn.second);
        err += ")";
        return false;
    }

    std::string SolarisService::pastEndWarning(const Pattern &pt) const
    {
        // notes that start at or after the end spill into the pattern's next repeat (Compile loops it at its
        // length) — said at once, and by `audit`
        size_t n = 0;
        double last = 0;
        for (const auto &x : pt.notes)
            if (x.at >= pt.length - 0.5 / kPpq) { ++n; last = std::max(last, x.at + x.length); }
        if (!n) return std::string();
        const double bar = barBeats(mProject);
        return "warning: " + countOf(n, "note of ", "notes of ") + pt.id + (n == 1 ? " starts" : " start") + " at or after its end (beat " + beatText(pt.length) +
               ") — they play over its next repeat; set " + pt.id + ".length=" + beatText(std::ceil(last / bar - 1e-9) * bar) + "\n";
    }

    // ── the edits ───────────────────────────────────────────────────────────────────────────────

    bool SolarisService::composeCommand(const Command &c, std::string &err)
    {
        Project &p = mProject;
        Pattern *pt = p.pattern(c.arg(0));
        if (!pt)
        {
            std::vector<std::string> ids;
            for (const auto &x : p.patterns) ids.push_back(x.id);
            err = "no pattern `" + c.arg(0) + "`";
            const auto near = nearest(c.arg(0), ids);
            if (!near.empty()) err += " (did you mean: " + near[0] + "?)";
            return false;
        }
        switch (c.kind)
        {
        case K::NotesAdd:
        {
            double length = 0.25;
            int vel = 100;
            if (!beatsOf(c, "length", 0.25, length, err) || !velOf(c, 100, vel, err)) return false;
            if (!(length > 0)) { err = "--length must be more than 0 beats"; return false; }
            const auto words = wordsAfterFirst(c);
            if (words.empty()) { err = "notes add needs notes: <pitch>@<beat>[:<length>[:<vel>]] …"; return false; }
            std::vector<Note> add;
            for (size_t i = 0; i < words.size(); ++i)
            {
                NoteToken t;
                Note n;
                std::string why;
                if (!parseNoteToken(words[i], t, why) || !pitchOf(t.pitch, pt->id, n.pitch, why))
                {
                    err = "note " + std::to_string(i + 1) + " `" + words[i] + "`: " + why + " — nothing was added";
                    return false;
                }
                n.at = toTick(t.at);
                n.length = t.hasLength ? toTick(t.length) : length;
                if (!(n.length > 0)) { err = "note " + std::to_string(i + 1) + " `" + words[i] + "`: shorter than a tick"; return false; }
                n.vel = t.hasVel ? t.vel : vel;
                add.push_back(n);
            }
            for (const auto &n : add)
            {
                pt->notes.erase(std::remove_if(pt->notes.begin(), pt->notes.end(),
                                               [&](const Note &o) { return o.pitch == n.pitch && sameTick(o.at, n.at); }),
                                pt->notes.end()); // a note again replaces, as `note add` does
                pt->notes.push_back(n);
            }
            sortNotes(*pt);
            changed("notes.added", pt->id);
            mOutput = countOf(add.size(), "note", "notes") + "\n" + pastEndWarning(*pt);
            return true;
        }
        case K::PatternSteps:
        {
            if (!c.has("pitch")) { err = "pattern steps needs --pitch (a number, a name or a pad: kick)"; return false; }
            int pitch = 0, vel = 100;
            if (!pitchOf(c.flag("pitch"), pt->id, pitch, err) || !velOf(c, 100, vel, err)) return false;
            double step = 0.25, at = 0, length = 0;
            if (!beatsOf(c, "step", 0.25, step, err) || !beatsOf(c, "at", 0, at, err)) return false;
            if (!(step > 0)) { err = "--step must be more than 0 beats"; return false; }
            if (!beatsOf(c, "length", step, length, err)) return false;
            if (!(length > 0)) { err = "--length must be more than 0 beats"; return false; }
            std::string text;
            for (size_t i = 1; i < c.args.size(); ++i) text += c.args[i];
            std::vector<int> steps;
            if (!parseSteps(text, steps, err)) return false;
            const double end = at + steps.size() * step;
            // the row REPLACES that pitch's notes in its span: what is not an x there is a rest now
            pt->notes.erase(std::remove_if(pt->notes.begin(), pt->notes.end(),
                                           [&](const Note &n) { return n.pitch == pitch && n.at >= at - 0.5 / kPpq && n.at < end - 0.5 / kPpq; }),
                            pt->notes.end());
            size_t hits = 0;
            for (size_t i = 0; i < steps.size(); ++i)
            {
                if (!steps[i]) continue;
                Note n;
                n.pitch = pitch;
                n.at = toTick(at + i * step);
                n.length = length;
                n.vel = steps[i] == 2 ? 127 : vel;
                pt->notes.push_back(n);
                ++hits;
            }
            sortNotes(*pt);
            changed("pattern.steps", pt->id);
            mOutput = countOf(hits, "note", "notes") + "\n" + pastEndWarning(*pt);
            return true;
        }
        case K::PatternDuplicate:
        {
            Pattern copy = *pt;
            copy.id = p.nextId("pt");
            copy.name = c.flag("name", pt->name + " copy");
            copy.remarks = Remarks();
            p.patterns.push_back(copy);
            changed("pattern.added", copy.id);
            mOutput = copy.id + "\n";
            return true;
        }
        case K::PatternClear:
        {
            int pitch = -1;
            if (c.has("pitch") && !pitchOf(c.flag("pitch"), pt->id, pitch, err)) return false;
            const size_t before = pt->notes.size();
            pt->notes.erase(std::remove_if(pt->notes.begin(), pt->notes.end(), [&](const Note &n) { return pitch < 0 || n.pitch == pitch; }),
                            pt->notes.end());
            changed("pattern.cleared", pt->id);
            mOutput = countOf(before - pt->notes.size(), "note", "notes") + " removed\n";
            return true;
        }
        case K::PatternDelete:
        {
            std::string users;
            for (const auto &cl : p.clips)
                if (cl.pattern == pt->id) users += (users.empty() ? "" : ", ") + cl.id;
            if (!users.empty()) { err = pt->id + " is played by " + users + " — delete those clips (or `clip unique` them) first"; return false; }
            const std::string id = pt->id;
            p.patterns.erase(std::remove_if(p.patterns.begin(), p.patterns.end(), [&](const Pattern &x) { return x.id == id; }), p.patterns.end());
            changed("pattern.deleted", id);
            return true;
        }
        case K::PatternTranspose:
        {
            double semi = 0;
            if (!c.has("semi")) { err = "pattern transpose needs --semi <n> (negative = down)"; return false; }
            if (!parseNumber(c.flag("semi"), semi) || semi != std::floor(semi) || std::fabs(semi) > 127)
            { err = "--semi must be a whole number of semitones, got `" + c.flag("semi") + "`"; return false; }
            for (const auto &n : pt->notes)
                if (n.pitch + semi < 0 || n.pitch + semi > 127)
                {
                    err = pitchName(n.pitch) + " (" + std::to_string(n.pitch) + ") at beat " + beatText(n.at) + " would be " +
                          std::to_string(n.pitch + (int)semi) + ", outside 0–127 — nothing moved";
                    return false;
                }
            for (auto &n : pt->notes) n.pitch += (int)semi;
            sortNotes(*pt);
            changed("pattern.transposed", pt->id);
            return true;
        }
        default:
            return false;
        }
    }

    // ── reading back ────────────────────────────────────────────────────────────────────────────

    bool SolarisService::readCommand(const Command &c, std::string &err)
    {
        if (!requireOpen(err)) return false;
        switch (c.kind)
        {
        case K::Ls:
            mOutput = lsText();
            return true;
        case K::Show:
            return showText(c.arg(0), mOutput, err);
        case K::PatternPrint:
        {
            const Pattern *pt = mProject.pattern(c.arg(0));
            if (!pt) { err = "no pattern `" + c.arg(0) + "`"; return false; }
            // a kit names its keys: print a pad by name where the kit that plays it has one
            const DeviceType *kit = nullptr;
            std::string users;
            for (const auto &cl : mProject.clips)
                if (cl.pattern == pt->id)
                {
                    users += (users.empty() ? "" : ", ") + cl.id;
                    const Rack *rk = mProject.rack(cl.track);
                    const DeviceType *t = rk && !rk->devices.empty() ? DeviceRegistry::find(rk->devices.front().type) : nullptr;
                    if (!kit && t && !t->noteNames.empty()) kit = t;
                }
            std::ostringstream o;
            o << "# " << pt->id << " " << q(pt->name) << " · " << beatText(pt->length) << " beats · " << countOf(pt->notes.size(), "note", "notes")
              << " · " << (users.empty() ? std::string("no clip plays it") : "played by " + users) << "\n";
            for (const auto &n : pt->notes)
            {
                std::string name = pitchName(n.pitch);
                if (kit)
                    for (const auto &nn : kit->noteNames)
                        if (nn.first == n.pitch) name = padText(nn.second);
                o << name << "@" << beatText(n.at) << ":" << beatText(n.length) << ":" << n.vel << "  # " << n.pitch
                  << (name != pitchName(n.pitch) ? " " + pitchName(n.pitch) : std::string()) << "\n";
            }
            mOutput = o.str();
            return true;
        }
        default:
            return false;
        }
    }

    std::string SolarisService::lsText() const
    {
        const Project &p = mProject;
        std::ostringstream o;
        double end = 0;
        for (const auto &c : p.clips) end = std::max(end, c.at + clipLengthBeats(p, c));
        o << "song " << q(p.header.name) << " · " << canonicalNumber(p.header.bpm) << " bpm · " << p.header.sig << " · "
          << beatText(end) << " beats" << (mModel.dirty ? " · unsaved" : "") << "\n";
        auto rackText = [&](const std::string &track) {
            std::string r;
            if (const Rack *rk = p.rack(track))
                for (const auto &d : rk->devices) r += (r.empty() ? "" : ", ") + d.type + " " + d.id + (d.bypass ? " (bypassed)" : "");
            return r.empty() ? r : " [" + r + "]";
        };
        for (const Mixer *m : p.mixersInOrder())
        {
            o << "mixer " << m->id << " " << q(m->name) << "\n";
            for (const Strip *s : p.stripsInOrder())
            {
                if (p.mixerOf(*s) != m) continue;
                o << "  " << s->id << " " << q(s->name) << " " << s->kind << rackText(s->id) << " → " << (s->out.empty() ? "master" : s->out);
                if (s->gain != 0) o << " · gain " << canonicalNumber(s->gain);
                if (s->pan != 0) o << " · pan " << canonicalNumber(s->pan);
                if (s->mute) o << " · muted";
                if (s->solo) o << " · solo";
                for (const auto &sd : p.sends)
                    if (sd.from == s->id)
                        o << " · " << sd.id << " → " << sd.to << " " << canonicalNumber(sd.gain) << " dB" << (sd.pre ? " pre" : "")
                          << (sd.sidechain ? " key" : "");
                o << "\n";
            }
        }
        o << "master" << rackText("master") << " → ";
        for (size_t i = 0; i < p.header.masterOut.size(); ++i) o << (i ? ", " : "") << p.header.masterOut[i];
        o << (p.header.masterOut.empty() ? "nothing" : "") << "\n";
        std::vector<const Lane *> lanes;
        for (const auto &l : p.lanes) lanes.push_back(&l);
        std::stable_sort(lanes.begin(), lanes.end(), [](const Lane *a, const Lane *b) { return a->order < b->order; });
        auto clipLine = [&](const Clip &c) {
            o << "  " << c.id << " @" << beatText(c.at) << " len " << beatText(clipLengthBeats(p, c)) << " ";
            if (c.isAudio()) o << q(c.src);
            else o << c.pattern;
            o << " via " << c.track << "\n";
        };
        for (const Lane *l : lanes)
        {
            o << "lane " << l->id << " " << q(l->name) << "\n";
            for (const auto &c : p.clips)
                if (c.lane == l->id) clipLine(c);
        }
        bool loose = false;
        for (const auto &c : p.clips)
            if (!p.lane(c.lane))
            {
                if (!loose) o << "no lane\n";
                loose = true;
                clipLine(c);
            }
        for (const auto &pt : p.patterns)
        {
            size_t clips = 0;
            for (const auto &c : p.clips) clips += c.pattern == pt.id ? 1 : 0;
            o << "pattern " << pt.id << " " << q(pt.name) << " " << beatText(pt.length) << " beats · " << countOf(pt.notes.size(), "note", "notes")
              << " · " << countOf(clips, "clip", "clips") << "\n";
        }
        for (const auto &a : p.automations)
        {
            std::string drives;
            for (const auto &address : readersOf(a.id)) drives += (drives.empty() ? "" : ", ") + address;
            o << "automation " << a.id << " " << q(a.name) << " " << countOf(a.points.size(), "point", "points")
              << (drives.empty() ? " · drives nothing" : " → " + drives) << "\n";
        }
        return o.str();
    }

    bool SolarisService::showText(const std::string &id, std::string &out, std::string &err) const
    {
        const Project &p = mProject;
        std::ostringstream o;
        auto field = [&](const std::string &name, const std::string &value) { o << "  " << name << " " << value << "\n"; };
        if (id == "project")
        {
            o << "project " << q(p.header.name) << "\n";
            field("bpm", canonicalNumber(p.header.bpm));
            field("sig", p.header.sig);
            field("sampleRate", std::to_string(p.header.sampleRate));
            if (p.header.masterGain != 0) field("masterGain", canonicalNumber(p.header.masterGain) + " dB");
            if (!mPath.empty()) field("path", mPath);
            if (mModel.dirty) field("unsaved", "true");
        }
        else if (const Strip *s = p.strip(id))
        {
            o << s->id << " strip " << q(s->name) << "\n";
            field("kind", s->kind);
            if (const Mixer *m = p.mixerOf(*s)) field("mixer", m->id + " " + q(m->name));
            field("out", s->out.empty() ? "master" : s->out);
            if (s->gain != 0) field("gain", canonicalNumber(s->gain) + " dB");
            if (s->pan != 0) field("pan", canonicalNumber(s->pan));
            if (s->mute) field("mute", "true");
            if (s->solo) field("solo", "true");
            if (s->colour >= 0) field("colour", std::to_string(s->colour));
            if (const Rack *rk = p.rack(s->id))
            {
                std::string d;
                for (const auto &x : rk->devices) d += (d.empty() ? "" : ", ") + x.id + " " + x.type;
                if (!d.empty()) field("devices", d);
            }
            for (const auto &sd : p.sends)
                if (sd.from == s->id) field("send", sd.id + " → " + sd.to + " " + canonicalNumber(sd.gain) + " dB" + (sd.pre ? " pre" : "") + (sd.sidechain ? " key" : ""));
            std::string clips;
            for (const auto &c : p.clips)
                if (c.track == s->id) clips += (clips.empty() ? "" : ", ") + c.id;
            if (!clips.empty()) field("clips", clips);
        }
        else if (const Clip *c = p.clip(id))
        {
            o << c->id << " " << (c->isAudio() ? "audio" : "note") << " clip " << q(c->name) << "\n";
            field("at", beatText(c->at));
            field("length", beatText(clipLengthBeats(p, *c)) + (c->length > 0 ? "" : " (its " + std::string(c->isAudio() ? "file's" : "pattern's") + ")"));
            if (c->isAudio()) { field("src", c->src); field("in", canonicalSeconds(c->in) + " s"); field("out", canonicalSeconds(c->out) + " s"); }
            else field("pattern", c->pattern);
            field("strip", c->track);
            field("lane", c->lane.empty() ? "(none)" : c->lane);
            if (c->gain != 0) field("gain", canonicalNumber(c->gain) + " dB");
            if (c->fadeIn != 0) field("fadeIn", beatText(c->fadeIn));
            if (c->fadeOut != 0) field("fadeOut", beatText(c->fadeOut));
            if (c->loop) field("loop", "true");
        }
        else if (const Pattern *pt = p.pattern(id))
        {
            o << pt->id << " pattern " << q(pt->name) << "\n";
            field("length", beatText(pt->length) + " beats");
            field("notes", std::to_string(pt->notes.size()) + " (`pattern print " + pt->id + "`)");
            std::string clips;
            for (const auto &c : p.clips)
                if (c.pattern == pt->id) clips += (clips.empty() ? "" : ", ") + c.id;
            field("clips", clips.empty() ? "none" : clips);
            const std::string warn = pastEndWarning(*pt);
            if (!warn.empty()) o << "  " << warn;
        }
        else if (const Lane *l = p.lane(id))
        {
            o << l->id << " lane " << q(l->name) << "\n";
            if (l->colour >= 0) field("colour", std::to_string(l->colour));
            std::string clips;
            for (const auto &c : p.clips)
                if (c.lane == l->id) clips += (clips.empty() ? "" : ", ") + c.id;
            field("clips", clips.empty() ? "none" : clips);
        }
        else if (const Mixer *m = p.mixer(id))
        {
            o << m->id << " mixer " << q(m->name) << "\n";
            field("order", std::to_string(m->order));
            std::string strips;
            for (const Strip *s : p.stripsInOrder())
                if (p.mixerOf(*s) == m) strips += (strips.empty() ? "" : ", ") + s->id;
            field("strips", strips.empty() ? "none" : strips);
        }
        else if (const Automation *a = p.automation(id))
        {
            o << a->id << " automation " << q(a->name) << "\n";
            field("range", canonicalNumber(a->min) + " … " + canonicalNumber(a->max) + (a->unit.empty() ? "" : " " + a->unit));
            if (!a->from.empty()) field("from", a->from);
            for (const auto &pt : a->points) field("point", "@" + beatText(pt.at) + " " + canonicalNumber(pt.value) + " " + pt.shape);
        }
        else if (const Port *po = p.port(id))
        {
            o << po->id << " port " << q(po->name) << "\n";
            field("dir", po->dir);
            field("channels", std::to_string(po->channels));
        }
        else
        {
            for (const auto &sd : p.sends)
                if (sd.id == id)
                {
                    o << sd.id << " send\n";
                    field("from", sd.from);
                    field("to", sd.to);
                    field("gain", canonicalNumber(sd.gain) + " dB");
                    if (sd.pre) field("pre", "true");
                    if (sd.sidechain) field("sidechain", "true");
                }
            for (const auto &rk : p.racks)
                for (const auto &d : rk.devices)
                {
                    if (d.id != id) continue;
                    const DeviceType *t = DeviceRegistry::find(d.type);
                    o << d.id << " " << d.type << (t ? " (" + t->label + ")" : std::string(" (unknown to this build)")) << " on " << rk.track << "\n";
                    if (d.bypass) field("bypass", "true");
                    if (!d.sample.empty()) field("sample", d.sample);
                    size_t shown = 0;
                    if (t)
                        for (const auto &spec : t->params)
                        {
                            // its non-default parameters, with the unit and what the default was
                            const std::string address = d.id + "." + spec.name;
                            const Binding *b = p.binding(address);
                            double v = spec.def;
                            bool stored = false;
                            for (const auto &kv : d.params)
                                if (kv.first == spec.name) stored = parseParam(spec, kv.second, v);
                            if (!b && (!stored || paramText(spec, v) == paramText(spec, spec.def))) continue;
                            const std::string unit = spec.unit.empty() || spec.isChoice() ? std::string() : " " + spec.unit;
                            std::string line = paramText(spec, v) + unit + "  (default " + paramText(spec, spec.def) + ")";
                            if (b) line = b->formula + "  (a formula; its own value " + paramText(spec, v) + unit + ")";
                            field(spec.name, line);
                            ++shown;
                        }
                    if (!shown && t) o << "  (every parameter at its default — `api` lists them)\n";
                }
            if (o.str().empty())
            {
                std::vector<std::string> ids = p.allIds();
                ids.push_back("project");
                err = "no node `" + id + "`";
                const auto near = nearest(id, ids);
                if (!near.empty()) err += " (did you mean: " + near[0] + "?)";
                return false;
            }
        }
        out = o.str();
        return true;
    }
}
}
