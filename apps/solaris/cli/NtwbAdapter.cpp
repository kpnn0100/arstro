/*
 *  solaris-cc — NtwbAdapter implementation. See the header for the seam rule: every method below
 *  either hands a line to the service or reads its model and events.
 */
#include "NtwbAdapter.h"
#include "AppModelCodec.h"
#include "Command.h"
#include "Event.h"
#include "SolarisService.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace arstro
{
namespace solaris_cli
{
    using ntwb::Json;
    namespace fs = std::filesystem;

    namespace
    {
        std::string lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        /** The host's kind, with `other` named where Solaris opens the file: a picture (`video add`, R-VIDEO-1 — the
         *  containers FFmpeg reads a picture from), a device preset (R-PRESET-1), an instrument's definition (R-INST-9). */
        std::string kindOf(const solaris::BrowserEntry &e)
        {
            if (e.kind != "other") return e.kind;
            static const char *kVideo[] = {".mp4", ".m4v", ".mov", ".mkv", ".webm", ".avi", ".mpg", ".mpeg", ".ts", ".mts", ".m2ts", ".wmv", ".flv", ".ogv"};
            const std::string ext = lower(fs::path(e.path).extension().string());
            if (std::find(std::begin(kVideo), std::end(kVideo), ext) != std::end(kVideo)) return "video";
            if (ext == ".slpreset") return "preset";
            if (ext == ".sfz") return "sfz";
            return "other";
        }

        /** The service's JSON (solaris::Json) as the bridge's: one codec writes it, the other reads it. */
        Json reparse(const std::string &text)
        {
            std::string err;
            Json j = Json::parse(text, err);
            if (!err.empty())
            {
                j = Json::object();
                j.set("codecError", err);
            }
            return j;
        }

        /** An object from its members, in order (ntwb::Json::set returns the stored value, so it does not chain). */
        Json obj(std::initializer_list<std::pair<const char *, Json>> members)
        {
            Json o = Json::object();
            for (const auto &kv : members) o.set(kv.first, kv.second);
            return o;
        }

        template <typename T> Json pair(const T (&v)[2])
        {
            Json a = Json::array();
            a.push((double)v[0]);
            a.push((double)v[1]);
            return a;
        }
    }

    NtwbAdapter::NtwbAdapter(solaris::SolarisService &svc, ntwb::Client &client, Hooks hooks) : mSvc(svc), mClient(client), mHooks(std::move(hooks)) {}

    NtwbAdapter::NtwbAdapter(solaris::SolarisService &svc, ntwb::Client &client, Lister listDir) : mSvc(svc), mClient(client)
    {
        mHooks.listDir = std::move(listDir);
    }

    Json NtwbAdapter::modelJson(const solaris::AppModel &m) { return reparse(solaris::modelToJson(m, false).dump()); }

    Json NtwbAdapter::transportJson(const solaris::AppModel &m)
    {
        Json t = Json::object();
        t.set("playing", m.transport.playing);
        t.set("position", m.transport.position);
        t.set("loopFrom", m.transport.loopFrom);
        t.set("loopTo", m.transport.loopTo);
        t.set("latencyMs", m.transport.latencyMs);
        t.set("masterPeak", pair(m.transport.masterPeak));
        Json peaks = Json::object();
        for (const auto &s : m.strips) peaks.set(s.id, pair(s.peak));
        t.set("peaks", peaks);
        t.set("audition", m.audition.file);
        t.set("auditionPlaying", m.audition.playing);
        t.set("auditionProgress", m.audition.progress);
        return t;
    }

    Json NtwbAdapter::liveJson(const solaris::AppModel &m)
    {
        // every field AppModelCodec writes only when !stable — what pump() moves with no revision — at its own path
        const auto &tr = m.transport;
        Json t = obj({{"playing", tr.playing}, {"recording", tr.recording}, {"position", tr.position}, {"tempo", tr.tempo}, {"tempoId", tr.tempoId},
                      {"latencyMs", tr.latencyMs}, {"masterPeak", pair(tr.masterPeak)}, {"masterRms", pair(tr.masterRms)},
                      {"masterHold", pair(tr.masterHold)}, {"masterClip", tr.masterClip}, {"driver", tr.driver}, {"xruns", (long long)tr.xruns},
                      {"countingIn", tr.countingIn}, {"recorded", tr.recorded}, {"cpu", (double)tr.cpu}, {"cpuPeak", (double)tr.cpuPeak},
                      {"swaps", tr.swaps}});
        Json devs = Json::array(); // R-DEV-5
        for (const auto &d : tr.devices)
            devs.push(obj({{"id", d.id}, {"role", d.role}, {"latencyMs", d.latencyMs}, {"driftPpm", d.driftPpm}, {"xruns", (long long)d.xruns},
                           {"starved", (long long)d.starved}, {"dropped", (long long)d.dropped}, {"problem", d.problem}}));
        t.set("devices", devs);
        Json launched = Json::array(); // R-LAUNCH-1: progress moves while a slot plays
        for (const auto &x : tr.launched)
            launched.push(obj({{"strip", x.strip}, {"session", x.session}, {"slot", x.slot}, {"next", x.next}, {"at", x.at}, {"progress", x.progress}}));
        t.set("launched", launched);
        Json strips = Json::object(), readings = Json::object();
        auto readingsOf = [&](const std::vector<solaris::DeviceModel> &ds) {
            for (const auto &d : ds)
                if (!d.readings.empty())
                {
                    Json v = Json::array();
                    for (const auto &r : d.readings) v.push(r.value);
                    readings.set(d.id, v);
                }
        };
        for (const auto &s : m.strips)
        {
            strips.set(s.id, obj({{"peak", pair(s.peak)}, {"rms", pair(s.rms)}, {"hold", pair(s.hold)}, {"clip", s.clip}, {"cpu", (double)s.cpu}}));
            readingsOf(s.devices);
        }
        readingsOf(m.masterDevices);
        Json bindings = Json::object(); // R-MIX-16: a formula's value at the heard position
        for (const auto &b : m.bindings) bindings.set(b.address, b.live);
        Json bands = Json::array();
        for (double b : m.loudness.spectrum) bands.push(b);
        return obj({{"transport", t}, {"strips", strips}, {"readings", readings}, {"bindings", bindings},
                    {"loudness", obj({{"momentary", m.loudness.momentary}, {"shortTerm", m.loudness.shortTerm}, {"integrated", m.loudness.integrated},
                                      {"truePeak", m.loudness.truePeak}, {"correlation", m.loudness.correlation}, {"spectrum", bands}})},
                    {"video", obj({{"time", m.video.time}})},
                    {"audition", obj({{"file", m.audition.file}, {"playing", m.audition.playing}, {"progress", m.audition.progress}})},
                    {"midiMessages", (long long)m.midiMessages}});
    }

    void NtwbAdapter::start()
    {
        // R-SVC-2: an Event IS the log line — so it is also the NTWB event, named by the same dotted
        // name, carrying the same line and its declared fields. One text form on every channel.
        mSvc.subscribe([this](const solaris::Event &e) {
            Json fields = Json::object();
            for (const auto &kv : e.fields) fields.set(kv.first, kv.second);
            Json d = Json::object();
            d.set("line", solaris::formatEvent(e));
            d.set("fields", fields);
            mClient.event(solaris::eventName(e.kind), d);
        });
        mClient.onCall = [this](const ntwb::Call &c, bool &) { return onCall(c); };
        // a fader drag sends `set` as notifies (no reply awaited); a refusal still lands in
        // model.lastError and the command.rejected event, so nothing is lost
        mClient.onNotify = [this](const ntwb::Call &c) {
            try { onCall(c); } catch (const std::exception &) {}
        };
    }

    void NtwbAdapter::tick(double wallMs)
    {
        const solaris::AppModel &m = mSvc.model();
        if (m.revision != mSentRevision && wallMs - mLastModelMs >= kModelMinIntervalMs)
        {
            mSentRevision = m.revision;
            mLastModelMs = wallMs;
            mClient.state("model", modelJson(m));
        }
        // the transport and the meters move with no revision (pump() emits nothing): their own key,
        // sent when they changed, so a page's playhead and meters follow without a model per frame
        if (wallMs - mLastTransportMs >= kTransportMinIntervalMs)
        {
            const Json t = transportJson(m);
            std::string text = t.dump();
            if (text != mSentTransport)
            {
                mSentTransport = std::move(text);
                mLastTransportMs = wallMs;
                mClient.state("transport", t);
            }
        }
        // everything else the window reads every frame: the same rule — sent when it changed, at most 25 a second
        if (wallMs - mLastLiveMs >= kLiveMinIntervalMs)
        {
            const Json l = liveJson(m);
            std::string text = l.dump();
            if (text != mSentLive)
            {
                mSentLive = std::move(text);
                mLastLiveMs = wallMs;
                mClient.state("live", l);
            }
        }
    }

    Json NtwbAdapter::runCommand(const std::string &line)
    {
        std::string err;
        const solaris::Command c = solaris::parseCommand(line, err);
        if (err.empty() && c.kind == solaris::Command::Kind::Wait)
            throw ntwb::MethodError("wait is the caller's loop — watch the events (transport.changed) instead");
        if (err.empty() && c.kind == solaris::Command::Kind::None)
        {
            Json r = Json::object();
            r.set("output", "");
            r.set("revision", (long long)mSvc.model().revision);
            return r; // a blank line or a comment: nothing ran
        }
        if (!mSvc.dispatchText(line, err)) throw ntwb::MethodError(err);
        Json r = Json::object();
        r.set("output", mSvc.output());
        r.set("revision", (long long)mSvc.model().revision);
        return r;
    }

    Json NtwbAdapter::browse(const Json &params) const
    {
        if (!mHooks.listDir) throw ntwb::MethodError("this face has no folder lister - browse is the host's");
        std::string path = params["path"].isString() ? params["path"].asString() : std::string();
        if (path.empty())
        {
            const char *home = std::getenv("HOME");
            path = home && *home ? home : "/";
        }
        std::error_code ec;
        const fs::path p = fs::weakly_canonical(fs::path(path), ec);
        if (ec || !fs::is_directory(p, ec)) throw ntwb::MethodError(path + " is not a folder");
        std::vector<solaris::BrowserEntry> listed;
        std::string err;
        if (!mHooks.listDir(p.string(), listed, err)) throw ntwb::MethodError("cannot read " + p.string() + ": " + err);
        std::vector<std::string> kinds;
        if (params["kinds"].isArray())
            for (const auto &k : params["kinds"].items())
                if (k.isString()) kinds.push_back(k.asString());
        std::stable_sort(listed.begin(), listed.end(), [](const solaris::BrowserEntry &a, const solaris::BrowserEntry &b) {
            const bool da = a.kind == "dir", db = b.kind == "dir";
            if (da != db) return da;
            return lower(a.name) < lower(b.name);
        });
        Json entries = Json::array();
        for (const auto &e : listed)
        {
            if (e.name.empty() || e.name[0] == '.') continue;
            const std::string kind = kindOf(e);
            if (kind != "dir" && !kinds.empty() && std::find(kinds.begin(), kinds.end(), kind) == kinds.end()) continue;
            Json j = Json::object();
            j.set("name", e.name);
            j.set("path", e.path);
            j.set("kind", kind);
            if (kind != "dir")
            {
                std::error_code ec2;
                const auto size = fs::file_size(e.path, ec2);
                if (!ec2) j.set("size", (long long)size);
            }
            entries.push(j);
        }
        Json out = Json::object();
        out.set("path", p.string());
        out.set("parent", p.has_parent_path() && p != p.root_path() ? Json(p.parent_path().string()) : Json());
        out.set("entries", entries);
        return out;
    }

    Json NtwbAdapter::video(const ntwb::Call &call, bool thumb)
    {
        const Json &p = call.params;
        const char *what = thumb ? "video.thumb" : "video.frame";
        const VideoSource &source = thumb ? (mHooks.videoThumb ? mHooks.videoThumb : mHooks.videoFrame) : mHooks.videoFrame; // App.cpp:91-93
        if (!source) throw ntwb::MethodError(std::string(what) + ": this build has no video decoder (solaris-cc needs FFmpeg)");
        if (!mHooks.jpeg) throw ntwb::MethodError(std::string(what) + ": this build has no image encoder (solaris-cc needs gdk-pixbuf)");
        if (!p["file"].isString() || p["file"].asString().empty()) throw ntwb::MethodError(std::string(what) + " needs params.file (the model's video.path)");
        const double t = p["t"].isNumber() ? p["t"].asNumber() : 0.0;
        const int w = p["w"].isNumber() ? (int)p["w"].asInt() : 0, h = p["h"].isNumber() ? (int)p["h"].asInt() : 0;
        if (w < 1 || h < 1 || w > kVideoMaxEdge || h > kVideoMaxEdge)
            throw ntwb::MethodError(std::string(what) + " needs params.w and params.h, the box in device pixels (1 - " + std::to_string(kVideoMaxEdge) + ")");
        const std::string file = p["file"].asString();
        VideoPicture pic;
        Json out = Json::object();
        if (!source(file, t, w, h, pic) || pic.width <= 0 || pic.height <= 0 || pic.rgba.size() < (size_t)pic.width * pic.height * 4)
        {
            out.set("empty", true); // the host cannot read it here (the window shows no frame either — App.h onVideoFrame)
            return out;
        }
        std::string jpeg;
        if (!mHooks.jpeg(pic.rgba.data(), pic.width, pic.height, kVideoQuality, jpeg)) throw ntwb::MethodError(std::string(what) + ": the frame could not be encoded");
        const Json meta = obj({{"seq", ++mVideoSeq}, {"file", file}, {"t", t}, {"time", pic.time}, {"w", pic.width}, {"h", pic.height}});
        // the picture this page asked for goes to this page alone (another page may show another time)
        mClient.blob(what, "image/jpeg", (const uint8_t *)jpeg.data(), jpeg.size(), meta, call.client, false);
        return obj({{"seq", mVideoSeq}, {"w", pic.width}, {"h", pic.height}, {"time", pic.time}, {"bytes", (long long)jpeg.size()}});
    }

    Json NtwbAdapter::onCall(const ntwb::Call &call)
    {
        const std::string &m = call.method;
        if (m == "command")
        {
            if (!call.params["line"].isString()) throw ntwb::MethodError("command needs params.line (a command line)");
            return runCommand(call.params["line"].asString());
        }
        if (m == "model") return modelJson(mSvc.model());
        if (m == "browse") return browse(call.params);
        if (m == "video.frame") return video(call, false);
        if (m == "video.thumb") return video(call, true);
        if (m == "commands")
        {
            Json out = Json::array();
            for (const auto &s : solaris::commandSpecs())
            {
                Json c = Json::object();
                c.set("name", s.verb);
                c.set("usage", solaris::usageOf(s));
                c.set("summary", s.summary);
                out.push(c);
            }
            return out;
        }
        throw ntwb::MethodError("no method named " + m);
    }

    Json NtwbAdapter::apiDescription()
    {
        auto P = [](const char *type, bool required, const char *doc) {
            Json p = Json::object();
            p.set("type", type);
            p.set("required", required);
            p.set("doc", doc);
            return p;
        };
        auto method = [](const char *doc, Json params, const char *result) {
            Json m = Json::object();
            m.set("doc", doc);
            m.set("params", params);
            m.set("result", result);
            return m;
        };
        Json methods = Json::object();
        {
            Json p = Json::object();
            p.set("line", P("str", true,
                            "one line of the Solaris command grammar (R-SVC-1) - every command is in apps/solaris/docs/API.md, "
                            "and the `commands` method lists them"));
            methods.set("command", method("dispatch one command line to the service, as the window and solaris-cc do; "
                                          "`wait` is refused (watch the events instead)",
                                          p, "{output, revision} - output is what the command printed; a refusal fails with its reason"));
        }
        methods.set("model", method("the whole AppModel as JSON (what `state print --json` prints)", Json::object(), "the model"));
        methods.set("commands", method("the command grammar: every command with its usage and summary", Json::object(),
                                       "[{name, usage, summary}]"));
        {
            Json p = Json::object();
            p.set("path", P("str", false, "the folder to list (default: the home folder)"));
            p.set("kinds", P("list[str]", false, "keep only files of these kinds (folders always): song, audio, midi, video, preset, sfz, other"));
            methods.set("browse", method("list a folder of the machine for the page's pickers (open a song, save, render, a folder, a "
                                         "file to relink) - the host's lister, as the service's browser lists",
                                         p, "{path, parent, entries: [{name, path, kind: dir|song|audio|midi|video|preset|sfz|other, size?}]}"));
        }
        for (const bool thumb : {false, true})
        {
            Json p = Json::object();
            p.set("file", P("str", true, "the picture's file as the host opens it (the model's video.path)"));
            p.set("t", P("num", false, "seconds into the picture (default 0)"));
            p.set("w", P("int", true, "the box's width in device pixels: the frame is fitted to it, its aspect kept"));
            p.set("h", P("int", true, "the box's height in device pixels"));
            methods.set(thumb ? "video.thumb" : "video.frame",
                        method(thumb ? "a frame of the song's picture for the video lane's thumbnails (App's onVideoThumb - a decoder of its "
                                       "own, so its seeks never cost the window's frames), as a JPEG to this page alone (stream `video.thumb`)"
                                     : "the frame of the song's picture showing at `t` (App's onVideoFrame: the Video window), as a JPEG to "
                                       "this page alone (stream `video.frame`)",
                               p, "{seq, w, h, time, bytes}, or {empty: true} when the host cannot read the picture there"));
        }

        Json events = Json::object();
        for (const auto &s : solaris::eventSpecs())
        {
            Json e = Json::object();
            e.set("doc", s.summary);
            std::string data = "{line, fields: {";
            for (size_t i = 0; i < s.fields.size(); ++i) data += (i ? ", " : "") + s.fields[i];
            data += "}}";
            e.set("data", data);
            events.set(s.name, e);
        }
        Json state = Json::object();
        {
            Json s = Json::object();
            s.set("doc", "the AppModel (state print --json), pushed on every revision (at most 25/s)");
            s.set("data", "object");
            state.set("model", s);
            Json t = Json::object();
            t.set("doc", "what moves without an edit, pushed when it changed (at most 20/s)");
            t.set("data", "{playing, position, loopFrom, loopTo, latencyMs, masterPeak, peaks: {<strip>: [l, r]}, audition, "
                          "auditionPlaying, auditionProgress}");
            state.set("transport", t);
            Json l = Json::object();
            l.set("doc", "EVERY value that moves with no edit (the model's unstable fields - meters, loudness, a tuner's reading, a formula's "
                         "live value, the tempo map's tempo, the clock devices, the video's time ...), at the model's own paths; a page merges it "
                         "over `model`. Pushed when it changed (at most 25/s)");
            l.set("data", "{transport: {playing, recording, position, tempo, tempoId, latencyMs, masterPeak, masterRms, masterHold, masterClip, "
                          "driver, xruns, countingIn, recorded, cpu, cpuPeak, swaps, devices: [...], launched: [...]}, strips: {<strip>: {peak, rms, "
                          "hold, clip, cpu}}, readings: {<device>: [value, ...]}, bindings: {<address>: live}, loudness: {momentary, shortTerm, "
                          "integrated, truePeak, correlation, spectrum}, video: {time}, audition: {file, playing, progress}, midiMessages}");
            state.set("live", l);
        }
        Json api = Json::object();
        api.set("ntwb", ntwb::kVersion);
        api.set("app", "solaris");
        api.set("methods", methods);
        api.set("events", events);
        api.set("state", state);
        Json streams = Json::object();
        for (const char *name : {"video.frame", "video.thumb"})
        {
            Json st = Json::object();
            st.set("doc", std::string("a frame of the song's picture a page asked for (answer to `") + name + "`), to that page alone");
            st.set("mime", "image/jpeg");
            st.set("meta", "{seq, file, t, time, w, h}");
            streams.set(name, st);
        }
        api.set("streams", streams);
        return api;
    }
}
}
