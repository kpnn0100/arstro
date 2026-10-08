#include "SolarisService.h"
#include "ApiDoc.h"
#include "AppModelCodec.h"
#include "Compile.h"
#include "Format.h"
#include "device/Device.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    SolarisService::SolarisService(Host host) : mHost(std::move(host)) { refreshModel(); }

    void SolarisService::emit(const Event &e)
    {
        if (e.kind == Event::Kind::Error || e.kind == Event::Kind::CommandRejected)
            mModel.lastError = e.field("why");
        for (auto &s : mSinks) s(e);
    }

    void SolarisService::changed(const std::string &what, const std::string &node)
    {
        emit(Event(Event::Kind::ProjectChanged).with("what", what).with("node", node));
    }

    bool SolarisService::requireOpen(std::string &err) const
    {
        if (mOpen) return true;
        err = "no song is open (project new <path.slp> or project open <path.slp>)";
        return false;
    }

    bool SolarisService::dispatchText(const std::string &line, std::string &err)
    {
        const Command c = parseCommand(line, err);
        if (!err.empty())
        {
            mOutput.clear();
            emit(Event(Event::Kind::CommandRejected).with("line", line).with("why", err));
            return false;
        }
        if (c.kind == K::None) return true;
        if (!dispatch(c, err))
        {
            emit(Event(Event::Kind::CommandRejected).with("line", line).with("why", err));
            return false;
        }
        return true;
    }

    namespace
    {
        bool mutates(K k)
        {
            switch (k)
            {
            case K::ProjectNew: case K::ProjectOpen: case K::ProjectSave: case K::ProjectClose:
            case K::Get: case K::Render: case K::MatrixPrint: case K::Audit: case K::StatePrint: case K::Api:
            case K::None:
                return false;
            default:
                return true;
            }
        }
    }

    bool SolarisService::dispatch(const Command &c, std::string &err)
    {
        err.clear();
        mOutput.clear();
        const bool editing = mutates(c.kind);
        if (editing && !requireOpen(err)) return false;
        const Project before = editing ? mProject : Project();
        std::vector<Event> pending; // a `set`'s events, emitted only once the whole line has landed

        bool ok = false;
        switch (c.kind)
        {
        case K::ProjectNew: case K::ProjectOpen: case K::ProjectSave: case K::ProjectClose:
            ok = projectCommand(c, err);
            break;
        case K::Set:
        {
            ok = true;
            for (const auto &f : c.fields)
            {
                std::string stored;
                if (!setAddress(f.first, f.second, stored, err)) { ok = false; break; }
                pending.push_back(Event(Event::Kind::ParamsChanged).with("address", f.first).with("value", stored));
            }
            break;
        }
        case K::Get:
        {
            std::string v;
            ok = requireOpen(err) && getAddress(c.arg(0), v, err);
            if (ok) mOutput = v + "\n";
            break;
        }
        case K::MixerAdd: case K::MixerDelete: case K::MixerMove: case K::StripAdd: case K::StripDelete:
        case K::StripMove: case K::Route: case K::SendAdd: case K::SendDelete: case K::DeviceAdd:
        case K::DeviceRemove: case K::DeviceMove: case K::LaneAdd: case K::LaneDelete:
            ok = mixCommand(c, err);
            break;
        case K::ClipAdd: case K::ClipMove: case K::ClipDuplicate: case K::ClipUnique: case K::ClipDelete:
        case K::PatternNew: case K::NoteAdd: case K::NoteDelete:
            ok = clipCommand(c, err);
            break;
        case K::Render:
            ok = render(c, err);
            break;
        case K::MatrixPrint:
            ok = requireOpen(err);
            if (ok) mOutput = matrixText(c.has("json"));
            break;
        case K::Audit:
        {
            ok = requireOpen(err);
            if (!ok) break;
            mAudit = audit();
            for (const auto &a : mAudit) mOutput += a + "\n";
            if (mAudit.empty()) mOutput = "no findings\n";
            emit(Event(Event::Kind::AuditReport).with("findings", (long long)mAudit.size()));
            ok = true;
            break;
        }
        case K::StatePrint:
        {
            ok = true;
            refreshModel();
            if (c.has("json")) mOutput = modelToJson(mModel, c.has("stable")).dump();
            else
            {
                std::ostringstream o;
                o << "screen " << mModel.screen;
                if (mOpen) o << " · " << mModel.projectName << " · " << canonicalNumber(mModel.bpm) << " bpm · "
                             << mModel.mixers.size() << " mixers · " << mModel.strips.size() << " strips · "
                             << mModel.lanes.size() << " lanes · " << mModel.clips.size() << " clips"
                             << (mModel.dirty ? " · unsaved" : "");
                mOutput = o.str() + "\n";
            }
            break;
        }
        case K::Api:
            ok = true;
            mOutput = c.has("md") ? apiMarkdown() : apiJson();
            break;
        case K::None:
            ok = true;
            break;
        }

        if (ok && editing)
        {
            const auto errors = validateProject(mProject);
            if (!errors.empty())
            {
                ok = false;
                err = errors.front();
            }
        }
        if (!ok)
        {
            if (editing) mProject = before; // a refused command changes nothing
            mOutput.clear();
            refreshModel();
            return false;
        }
        if (editing) mModel.dirty = true;
        refreshModel();
        for (const auto &e : pending) emit(e);
        return true;
    }

    // ── files ────────────────────────────────────────────────────────────────────────────────

    std::string SolarisService::resolvePath(const std::string &src) const
    {
        const fs::path p(src);
        if (p.is_absolute() || mPath.empty()) return p.lexically_normal().string();
        return (fs::path(mPath).parent_path() / p).lexically_normal().string();
    }

    std::string SolarisService::relativePath(const std::string &file) const
    {
        const fs::path abs = fs::absolute(fs::path(file)).lexically_normal();
        if (mPath.empty()) return abs.string();
        const fs::path dir = fs::absolute(fs::path(mPath)).parent_path().lexically_normal();
        const fs::path rel = abs.lexically_relative(dir);
        // Inside the song's folder: stored relative, so the folder moves as one. Outside: absolute.
        if (!rel.empty() && *rel.begin() != "..") return rel.generic_string();
        return abs.generic_string();
    }

    std::shared_ptr<const engine::Pcm> SolarisService::pcmFor(const std::string &src)
    {
        if (mPcmRate != mProject.header.sampleRate)
        {
            mPcm.clear(); // decoded at another rate: stale
            mOffline.clear();
            mPcmRate = mProject.header.sampleRate;
        }
        const std::string path = resolvePath(src);
        auto it = mPcm.find(path);
        if (it != mPcm.end()) return it->second;
        if (mOffline.count(path) || !mHost.decodeAudio) return nullptr;
        auto pcm = std::make_shared<engine::Pcm>();
        std::string err;
        if (!mHost.decodeAudio(path, mPcmRate, *pcm, err) || pcm->frames <= 0)
        {
            mOffline.insert(path);
            return nullptr;
        }
        mPcm[path] = pcm;
        return pcm;
    }

    bool SolarisService::projectCommand(const Command &c, std::string &err)
    {
        auto open = [&](Project p, const std::string &path) {
            mProject = std::move(p);
            mPath = path;
            mOpen = true;
            mPcm.clear();
            mOffline.clear();
            mAudit.clear();
            mLastRenderPeaks.clear();
            refreshModel();
            mModel.dirty = false;
            emit(Event(Event::Kind::ScreenChanged).with("screen", "project"));
            emit(Event(Event::Kind::ProjectOpened).with("path", path)
                     .with("strips", (long long)mProject.strips.size()).with("clips", (long long)mProject.clips.size()));
        };
        auto write = [&](const std::string &path) {
            std::ofstream f(path, std::ios::binary);
            if (!f) { err = "cannot write " + path; return false; }
            f << serializeProject(mProject);
            return (bool)f;
        };

        switch (c.kind)
        {
        case K::ProjectNew:
        {
            const std::string path = c.arg(0);
            if (fs::exists(path)) { err = path + " already exists — open it, or choose another name"; return false; }
            double bpm = 120.0;
            if (c.has("bpm") && (!parseNumber(c.flag("bpm"), bpm) || !(bpm >= 20 && bpm <= 999)))
            { err = "--bpm must be a tempo between 20 and 999"; return false; }
            const std::string sig = c.flag("sig", "4/4");
            int num = 0, den = 0;
            if (std::sscanf(sig.c_str(), "%d/%d", &num, &den) != 2 || num < 1 || num > 32 || (den != 2 && den != 4 && den != 8 && den != 16))
            { err = "--sig must be a meter like 4/4, 3/4 or 7/8"; return false; }
            double rate = 48000;
            if (c.has("rate") && (!parseNumber(c.flag("rate"), rate) || !(rate >= 8000 && rate <= 192000)))
            { err = "--rate must be a sample rate between 8000 and 192000"; return false; }
            const std::string stem = fs::path(path).stem().string();
            std::string id = "prj_";
            for (char ch : stem) id += (std::isalnum((unsigned char)ch) ? ch : '_');
            Project p = newProject(id, c.flag("name", stem), bpm, sig, (int)rate);
            const Project keep = mProject;
            mProject = p;
            if (!write(path)) { mProject = keep; return false; }
            open(p, path);
            mOutput = path + "\n";
            return true;
        }
        case K::ProjectOpen:
        {
            const std::string path = c.arg(0);
            std::ifstream f(path, std::ios::binary);
            if (!f) { err = "cannot read " + path; return false; }
            std::stringstream ss;
            ss << f.rdbuf();
            Project p;
            ParseReport rep;
            if (!parseProject(ss.str(), p, err, &rep)) { err = path + ": " + err; return false; }
            open(std::move(p), path);
            for (const auto &n : rep.notes) emit(Event(Event::Kind::Info).with("text", n));
            return true;
        }
        case K::ProjectSave:
        {
            if (!requireOpen(err)) return false;
            const std::string path = c.arg(0, mPath);
            if (!write(path)) return false;
            if (path != mPath) { mPath = path; mPcm.clear(); mOffline.clear(); } // relative media now resolve from here
            mModel.dirty = false;
            emit(Event(Event::Kind::ProjectSaved).with("path", path));
            return true;
        }
        case K::ProjectClose:
            mOpen = false;
            mProject = Project();
            mPath.clear();
            mPcm.clear();
            mOffline.clear();
            emit(Event(Event::Kind::ProjectClosed));
            emit(Event(Event::Kind::ScreenChanged).with("screen", "home"));
            return true;
        default:
            return false;
        }
    }

    // ── addresses ────────────────────────────────────────────────────────────────────────────

    namespace
    {
        bool number(const std::string &v, double &out, std::string &err)
        {
            if (parseNumber(v, out) && std::isfinite(out)) return true;
            err = "`" + v + "` is not a number";
            return false;
        }
        bool boolean(const std::string &v, bool &out, std::string &err)
        {
            if (parseBool(v, out)) return true;
            err = "`" + v + "` is not true/false";
            return false;
        }
        bool inRange(double v, double lo, double hi, const std::string &what, std::string &err)
        {
            if (v >= lo && v <= hi) return true;
            err = what + " must be between " + canonicalNumber(lo) + " and " + canonicalNumber(hi) + ", got " + canonicalNumber(v);
            return false;
        }
        std::string unknownField(const std::string &id, const std::string &what, const std::string &field,
                                 const std::vector<std::string> &fields)
        {
            std::string e = "`" + id + "` (" + what + ") has no field `" + field + "`";
            const auto near = nearest(field, fields);
            if (!near.empty()) e += " (did you mean: " + near[0] + "?)";
            else
            {
                e += " (it has: ";
                for (size_t i = 0; i < fields.size(); ++i) e += (i ? ", " : "") + fields[i];
                e += ")";
            }
            return e;
        }
    }

    bool SolarisService::setAddress(const std::string &address, const std::string &value, std::string &stored, std::string &err)
    {
        const auto dot = address.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == address.size())
        {
            err = "an address is <id>.<field>, got `" + address + "` (`api` lists them)";
            return false;
        }
        const std::string id = address.substr(0, dot), f = address.substr(dot + 1);
        double x = 0;
        bool b = false;
        Project &p = mProject;

        if (id == "project")
        {
            static const std::vector<std::string> fields = {"name", "bpm", "sig", "masterGain", "sampleRate"};
            if (f == "name") { p.header.name = value; stored = value; return true; }
            if (f == "bpm") { if (!number(value, x, err) || !inRange(x, 20, 999, "bpm", err)) return false; p.header.bpm = x; stored = canonicalNumber(x); return true; }
            if (f == "masterGain") { if (!number(value, x, err) || !inRange(x, -120, 12, "masterGain", err)) return false; p.header.masterGain = x; stored = canonicalNumber(x); return true; }
            if (f == "sampleRate") { if (!number(value, x, err) || !inRange(x, 8000, 192000, "sampleRate", err)) return false; p.header.sampleRate = (int)x; stored = std::to_string((int)x); return true; }
            if (f == "sig")
            {
                int n = 0, d = 0;
                if (std::sscanf(value.c_str(), "%d/%d", &n, &d) != 2 || n < 1 || n > 32 || (d != 2 && d != 4 && d != 8 && d != 16))
                { err = "sig must be a meter like 4/4"; return false; }
                p.header.sig = value;
                stored = value;
                return true;
            }
            err = unknownField(id, "the song", f, fields);
            return false;
        }
        if (Strip *s = p.strip(id))
        {
            static const std::vector<std::string> fields = {"name", "gain", "pan", "mute", "solo", "colour"};
            if (f == "name") { s->name = value; stored = value; return true; }
            if (f == "gain") { if (!number(value, x, err) || !inRange(x, -120, 12, "gain (dB)", err)) return false; s->gain = x; stored = canonicalNumber(x); return true; }
            if (f == "pan") { if (!number(value, x, err) || !inRange(x, -1, 1, "pan", err)) return false; s->pan = x; stored = canonicalNumber(x); return true; }
            if (f == "mute") { if (!boolean(value, b, err)) return false; s->mute = b; stored = boolText(b); return true; }
            if (f == "solo") { if (!boolean(value, b, err)) return false; s->solo = b; stored = boolText(b); return true; }
            if (f == "colour") { if (!number(value, x, err) || !inRange(x, -1, 15, "colour", err)) return false; s->colour = (int)x; stored = std::to_string((int)x); return true; }
            err = unknownField(id, "a strip", f, fields) + " — routing is `route " + id + " --to …`";
            return false;
        }
        if (Clip *c = p.clip(id))
        {
            static const std::vector<std::string> fields = {"name", "at", "in", "out", "length", "gain", "fadeIn", "fadeOut", "loop"};
            if (f == "name") { c->name = value; stored = value; return true; }
            if (f == "loop") { if (!boolean(value, b, err)) return false; c->loop = b; stored = boolText(b); return true; }
            if (std::find(fields.begin(), fields.end(), f) == fields.end()) { err = unknownField(id, "a clip", f, fields); return false; }
            if (!number(value, x, err)) return false;
            if (f == "gain") { if (!inRange(x, -120, 12, "gain (dB)", err)) return false; c->gain = x; stored = canonicalNumber(x); return true; }
            if (f == "in" || f == "out")
            {
                if (!c->isAudio()) { err = id + " is a note clip — `" + f + "` is an audio clip's source range"; return false; }
                if (x < 0) { err = f + " is seconds into the file and cannot be negative"; return false; }
                (f == "in" ? c->in : c->out) = x;
                stored = canonicalSeconds(x);
                return true;
            }
            if (x < 0) { err = f + " cannot be negative"; return false; }
            const double t = toTick(x);
            if (f == "at") c->at = t;
            else if (f == "length") c->length = t;
            else if (f == "fadeIn") c->fadeIn = t;
            else c->fadeOut = t;
            stored = canonicalBeats(t);
            return true;
        }
        if (Lane *l = p.lane(id))
        {
            if (f == "name") { l->name = value; stored = value; return true; }
            if (f == "colour") { if (!number(value, x, err) || !inRange(x, -1, 15, "colour", err)) return false; l->colour = (int)x; stored = std::to_string((int)x); return true; }
            err = unknownField(id, "a lane", f, {"name", "colour"});
            return false;
        }
        if (Mixer *m = p.mixer(id))
        {
            if (f == "name") { m->name = value; stored = value; return true; }
            err = unknownField(id, "a mixer", f, {"name"}) + " — order is `mixer move " + id + " --to …`";
            return false;
        }
        if (Send *sd = p.send(id))
        {
            if (f == "gain") { if (!number(value, x, err) || !inRange(x, -120, 12, "gain (dB)", err)) return false; sd->gain = x; stored = canonicalNumber(x); return true; }
            if (f == "pre") { if (!boolean(value, b, err)) return false; sd->pre = b; stored = boolText(b); return true; }
            err = unknownField(id, "a send", f, {"gain", "pre"});
            return false;
        }
        if (Pattern *pt = p.pattern(id))
        {
            if (f == "name") { pt->name = value; stored = value; return true; }
            if (f == "length") { if (!number(value, x, err)) return false; if (!(x > 0)) { err = "a pattern's length must be more than 0 beats"; return false; } pt->length = toTick(x); stored = canonicalBeats(pt->length); return true; }
            err = unknownField(id, "a pattern", f, {"name", "length"}) + " — notes are `note add`/`note delete`";
            return false;
        }
        if (Port *po = p.port(id))
        {
            if (f == "name") { po->name = value; stored = value; return true; }
            if (f == "channels") { if (!number(value, x, err) || !inRange(x, 1, 64, "channels", err)) return false; po->channels = (int)x; stored = std::to_string((int)x); return true; }
            err = unknownField(id, "a port", f, {"name", "channels"});
            return false;
        }
        if (DeviceNode *d = p.device(id))
        {
            if (f == "bypass") { if (!boolean(value, b, err)) return false; d->bypass = b; stored = boolText(b); return true; }
            const DeviceType *t = DeviceRegistry::find(d->type);
            if (!t) { err = "this build has no `" + d->type + "` device, so " + id + "'s parameters cannot be checked"; return false; }
            const int i = t->paramIndex(f);
            if (i < 0)
            {
                std::vector<std::string> names = {"bypass"};
                for (const auto &ps : t->params) names.push_back(ps.name);
                err = unknownField(id, "a " + t->label, f, names);
                return false;
            }
            const ParamSpec &spec = t->params[i];
            if (!parseParam(spec, value, x))
            {
                err = "`" + value + "` is not a value of " + id + "." + f;
                if (spec.isChoice())
                {
                    err += " (one of: ";
                    for (size_t k = 0; k < spec.choices.size(); ++k) err += (k ? ", " : "") + spec.choices[k];
                    err += ")";
                }
                return false;
            }
            stored = paramText(spec, x);
            for (auto &kv : d->params)
                if (kv.first == f) { kv.second = stored; return true; }
            d->params.emplace_back(f, stored);
            return true;
        }
        std::vector<std::string> ids = p.allIds();
        ids.push_back("project");
        err = "no node `" + id + "`";
        const auto near = nearest(id, ids);
        if (!near.empty()) err += " (did you mean: " + near[0] + "?)";
        return false;
    }

    bool SolarisService::getAddress(const std::string &address, std::string &value, std::string &err) const
    {
        const auto dot = address.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == address.size())
        {
            err = "an address is <id>.<field>, got `" + address + "` (`api` lists them)";
            return false;
        }
        const std::string id = address.substr(0, dot), f = address.substr(dot + 1);
        const Project &p = mProject;
        bool have = false;
        auto put = [&](const char *field, const std::string &v) {
            if (f == field) { value = v; have = true; }
        };
        if (id == "project")
        {
            put("name", p.header.name); put("bpm", canonicalNumber(p.header.bpm)); put("sig", p.header.sig);
            put("masterGain", canonicalNumber(p.header.masterGain)); put("sampleRate", std::to_string(p.header.sampleRate));
        }
        else if (const Strip *s = p.strip(id))
        {
            put("name", s->name); put("kind", s->kind); put("gain", canonicalNumber(s->gain)); put("pan", canonicalNumber(s->pan));
            put("mute", boolText(s->mute)); put("solo", boolText(s->solo)); put("colour", std::to_string(s->colour));
            put("out", s->out.empty() ? "master" : s->out);
            put("mixer", p.mixerOf(*s) ? p.mixerOf(*s)->id : std::string());
        }
        else if (const Clip *c = p.clip(id))
        {
            put("name", c->name); put("at", canonicalBeats(c->at)); put("length", canonicalBeats(clipLengthBeats(p, *c)));
            put("gain", canonicalNumber(c->gain)); put("fadeIn", canonicalBeats(c->fadeIn)); put("fadeOut", canonicalBeats(c->fadeOut));
            put("loop", boolText(c->loop)); put("track", c->track); put("lane", c->lane);
            if (c->isAudio()) { put("in", canonicalSeconds(c->in)); put("out", canonicalSeconds(c->out)); put("src", c->src); }
            else put("pattern", c->pattern);
        }
        else if (const Lane *l = p.lane(id)) { put("name", l->name); put("colour", std::to_string(l->colour)); }
        else if (const Mixer *m = p.mixer(id)) { put("name", m->name); put("order", std::to_string(m->order)); }
        else if (const Pattern *pt = p.pattern(id)) { put("name", pt->name); put("length", canonicalBeats(pt->length)); }
        else if (const Port *po = p.port(id)) { put("name", po->name); put("channels", std::to_string(po->channels)); }
        else
        {
            for (const auto &sd : p.sends)
                if (sd.id == id) { put("to", sd.to); put("gain", canonicalNumber(sd.gain)); put("pre", boolText(sd.pre)); }
            for (const auto &r : p.racks)
                for (const auto &d : r.devices)
                    if (d.id == id)
                    {
                        put("bypass", boolText(d.bypass));
                        put("type", d.type);
                        for (const auto &kv : d.params) put(kv.first.c_str(), kv.second);
                        if (!have)
                            if (const DeviceType *t = DeviceRegistry::find(d.type))
                            {
                                const int i = t->paramIndex(f);
                                if (i >= 0) put(f.c_str(), paramText(t->params[i], t->params[i].def)); // not stored = the default
                            }
                    }
        }
        if (have) return true;
        // Unreadable: let the writer's router name the problem (no such node, no such field).
        SolarisService probe(*this);
        probe.mSinks.clear();
        std::string stored;
        if (!probe.setAddress(address, "0", stored, err) && !err.empty()) return false;
        err = "`" + address + "` cannot be read";
        return false;
    }
}
}
