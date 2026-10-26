/*
 *  interstellar-cc — NtwbAdapter implementation. See the header for the seam rule: every method
 *  below either hands a line to the service, reads its model and events, or asks it for a picture.
 */
#include "NtwbAdapter.h"
#include "AppModelCodec.h"
#include "Command.h"
#include "Event.h"
#include "InterstellarService.h"
#include "Raster.h"
#include <algorithm>
#include <chrono>

namespace arstro
{
namespace interstellar_cli
{
    using ntwb::Json;

    namespace
    {
        /** The service's JSON (interstellar::Json, writer-only) as the bridge's: one codec writes it,
         *  the other reads it — the same text `state print --json` prints. */
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

        /** A deep (16-bit) picture as the 8-bit one a JPEG carries. */
        const std::vector<uint8_t> &bytes8(const interstellar::Raster &r, std::vector<uint8_t> &tmp)
        {
            if (!r.rgba.empty() || r.rgba16.empty()) return r.rgba;
            tmp.resize(r.rgba16.size());
            for (size_t i = 0; i < tmp.size(); ++i) tmp[i] = (uint8_t)(r.rgba16[i] >> 8);
            return tmp;
        }
    }

    NtwbAdapter::NtwbAdapter(interstellar::InterstellarService &svc, ntwb::Client &client, JpegEncoder jpeg)
        : mSvc(svc), mClient(client), mJpeg(std::move(jpeg))
    {
    }

    Json NtwbAdapter::modelJson(const interstellar::AppModel &m) { return reparse(interstellar::modelToJson(m).dump()); }

    Json NtwbAdapter::transportJson(const interstellar::AppModel &m)
    {
        Json t = Json::object();
        t.set("playing", m.playing);
        t.set("playhead", m.playhead);
        t.set("playheadFrame", m.playheadFrame);
        t.set("duration", m.duration);
        t.set("shuttle", m.shuttle);
        t.set("frameSeq", (long long)m.frameSeq);
        t.set("sourceView", m.sourceView);
        t.set("sourcePlayhead", m.sourcePlayhead);
        t.set("soundPlaying", m.soundPlaying);
        Json peak = Json::array();
        peak.push(m.meterPeakL);
        peak.push(m.meterPeakR);
        t.set("meterPeak", peak);
        Json rms = Json::array();
        rms.push(m.meterRmsL);
        rms.push(m.meterRmsR);
        t.set("meterRms", rms);
        t.set("meterClip", m.meterClip);
        return t;
    }

    void NtwbAdapter::start()
    {
        // R-SVC-2: an Event IS the log line — so it is also the NTWB event, named by the same dotted
        // name, carrying the same line and its declared fields. One text form on every channel.
        mSvc.subscribe([this](const interstellar::Event &e) {
            Json fields = Json::object();
            for (const auto &kv : e.fields) fields.set(kv.first, kv.second);
            Json d = Json::object();
            d.set("line", interstellar::formatEvent(e));
            d.set("fields", fields);
            mClient.event(interstellar::eventName(e.kind), d);
        });
        mClient.onCall = [this](const ntwb::Call &c, bool &) { return onCall(c); };
        // a scrub sends `playhead <t>` as notifies (no reply awaited); a refusal still lands in
        // model.lastError and the command.rejected event, so nothing is lost
        mClient.onNotify = [this](const ntwb::Call &c) {
            try { onCall(c); } catch (const std::exception &) {}
        };
    }

    void NtwbAdapter::tick(double wallMs)
    {
        const interstellar::AppModel &m = mSvc.model();
        const double every = m.playing ? kModelPlayingIntervalMs : kModelMinIntervalMs;
        if (m.revision != mSentRevision && wallMs - mLastModelMs >= every)
        {
            mSentRevision = m.revision;
            mLastModelMs = wallMs;
            mClient.state("model", modelJson(m));
        }
        // the playhead and the meters move every frame while playing: their own small key, sent when
        // they changed, so a page's playhead follows without the whole model per frame
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
    }

    Json NtwbAdapter::runCommand(const std::string &line)
    {
        using K = interstellar::Command::Kind;
        std::string err;
        const interstellar::Command c = interstellar::parseCommand(line, err);
        if (err.empty() && c.kind == K::Wait)
            throw ntwb::MethodError("wait is the caller's loop - watch the events (frame.ready, render.finished) instead");
        if (err.empty() && c.kind == K::Quit)
            throw ntwb::MethodError("quit is Arstro Remote's - stop the session from Apps (arstro-remote apps stop interstellar)");
        if (err.empty() && c.kind == K::None)
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

    Json NtwbAdapter::frame(const ntwb::Call &call)
    {
        const Json &p = call.params;
        const interstellar::AppModel &m = mSvc.model();
        const std::string source = p["source"].isString() ? p["source"].asString() : std::string();
        // the timeline at its playhead by default; a source at its reference frame (t < 0) by default
        const double t = p["t"].isNumber() ? p["t"].asNumber() : (source.empty() ? m.playhead : -1.0);
        const int edge = (int)std::clamp<long long>(p["edge"].isNumber() ? p["edge"].asInt() : kDefaultEdge, kMinEdge, kMaxEdge);
        if (!mJpeg) throw ntwb::MethodError("this build has no image encoder (interstellar-cc needs gdk-pixbuf for pictures)");

        const auto t0 = std::chrono::steady_clock::now();
        interstellar::Raster r;
        const bool ok = source.empty() ? mSvc.renderFrame(t, edge, r) : mSvc.renderSourceFrame(source, t, edge, r);
        Json out = Json::object();
        if (!ok || r.width <= 0 || r.height <= 0)
        {
            out.set("empty", true); // nothing to show here (no timeline, no clip at t, an unknown source)
            out.set("lastError", m.lastError);
            return out;
        }
        std::vector<uint8_t> tmp;
        const std::vector<uint8_t> &px = bytes8(r, tmp);
        std::string jpeg;
        if (!mJpeg(px.data(), r.width, r.height, kFrameQuality, jpeg)) throw ntwb::MethodError("the frame could not be encoded");
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

        Json meta = Json::object();
        meta.set("seq", ++mFrameSeq);
        meta.set("t", t);
        meta.set("source", source);
        meta.set("w", r.width);
        meta.set("h", r.height);
        meta.set("edge", edge);
        meta.set("frameSeq", (long long)m.frameSeq);
        meta.set("ms", ms);
        // the picture this page asked for goes to this page alone (another page may be on another tab)
        mClient.blob("frame", "image/jpeg", (const uint8_t *)jpeg.data(), jpeg.size(), meta, call.client, false);
        out.set("seq", mFrameSeq);
        out.set("w", r.width);
        out.set("h", r.height);
        out.set("bytes", (long long)jpeg.size());
        out.set("ms", ms);
        return out;
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
        if (m == "frame") return frame(call);
        if (m == "commands")
        {
            Json out = Json::array();
            for (const auto &s : interstellar::commandSpecs())
            {
                Json c = Json::object();
                c.set("name", s.verb);
                c.set("usage", interstellar::usageOf(s));
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
                            "one line of the Interstellar command grammar (R-SVC-1) - every command is in "
                            "apps/interstellar/docs/API.md, and the `commands` method lists them"));
            methods.set("command", method("dispatch one command line to the service, as the window and interstellar-cc do; "
                                          "`wait` and `quit` are refused (watch the events; stop the session from Apps)",
                                          p, "{output, revision} - output is what the command printed; a refusal fails with its reason"));
        }
        methods.set("model", method("the whole AppModel as JSON (what `state print --json` prints)", Json::object(), "the model"));
        methods.set("commands", method("the command grammar: every command with its usage and summary", Json::object(),
                                       "[{name, usage, summary}]"));
        {
            Json p = Json::object();
            p.set("t", P("float", false, "seconds: the timeline's time (default: the playhead), or the source's (default: its reference frame)"));
            p.set("source", P("str", false, "a rack source's bind name: its own picture, as the Grade tab and the source viewer show it"));
            p.set("edge", P("int", false, "the long edge in pixels, 64-3840 (default 960)"));
            methods.set("frame", method("render the picture this page shows and send it to this page alone (stream `frame`)",
                                        p, "{seq, w, h, bytes, ms}, or {empty: true, lastError} when there is nothing to show"));
        }

        Json events = Json::object();
        for (const auto &s : interstellar::eventSpecs())
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
            s.set("doc", "the AppModel (state print --json), pushed on every revision (at most 25/s; 2/s while playing)");
            s.set("data", "object");
            state.set("model", s);
            Json t = Json::object();
            t.set("doc", "what moves without an edit, pushed when it changed (at most 25/s)");
            t.set("data", "{playing, playhead, playheadFrame, duration, shuttle, frameSeq, sourceView, sourcePlayhead, soundPlaying, "
                          "meterPeak: [l, r], meterRms: [l, r], meterClip}");
            state.set("transport", t);
        }
        Json streams = Json::object();
        {
            Json f = Json::object();
            f.set("doc", "the picture a page asked for (answer to `frame`), to that page alone");
            f.set("mime", "image/jpeg");
            f.set("meta", "{seq, t, source, w, h, edge, frameSeq, ms}");
            streams.set("frame", f);
        }
        Json api = Json::object();
        api.set("ntwb", ntwb::kVersion);
        api.set("app", "interstellar");
        api.set("methods", methods);
        api.set("events", events);
        api.set("state", state);
        api.set("streams", streams);
        return api;
    }
}
}
