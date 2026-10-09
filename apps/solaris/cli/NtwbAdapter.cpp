/*
 *  solaris-cc — NtwbAdapter implementation. See the header for the seam rule: every method below
 *  either hands a line to the service or reads its model and events.
 */
#include "NtwbAdapter.h"
#include "AppModelCodec.h"
#include "Command.h"
#include "Event.h"
#include "SolarisService.h"

namespace arstro
{
namespace solaris_cli
{
    using ntwb::Json;

    namespace
    {
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

        template <typename T> Json pair(const T (&v)[2])
        {
            Json a = Json::array();
            a.push((double)v[0]);
            a.push((double)v[1]);
            return a;
        }
    }

    NtwbAdapter::NtwbAdapter(solaris::SolarisService &svc, ntwb::Client &client) : mSvc(svc), mClient(client) {}

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

    Json NtwbAdapter::onCall(const ntwb::Call &call)
    {
        const std::string &m = call.method;
        if (m == "command")
        {
            if (!call.params["line"].isString()) throw ntwb::MethodError("command needs params.line (a command line)");
            return runCommand(call.params["line"].asString());
        }
        if (m == "model") return modelJson(mSvc.model());
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
        }
        Json api = Json::object();
        api.set("ntwb", ntwb::kVersion);
        api.set("app", "solaris");
        api.set("methods", methods);
        api.set("events", events);
        api.set("state", state);
        api.set("streams", Json::object());
        return api;
    }
}
}
