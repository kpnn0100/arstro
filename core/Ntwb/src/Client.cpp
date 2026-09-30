/*
 *  Arstro Ntwb — Client implementation. The only getenv of the library is here, and only
 *  when the caller did not pass the socket/token explicitly: the host hands them over in
 *  the environment (NTWB_SOCKET, NTWB_TOKEN), which is the protocol, not a convenience.
 */
#include "ntwb/Client.h"
#include <cstdlib>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace arstro
{
namespace ntwb
{
    Client::Client(Options o) : mOpt(std::move(o)) {}

    Client::~Client()
    {
        if (mTransport) mTransport->close();
    }

    bool Client::connect(std::string &err)
    {
        if (!mTransport)
        {
            if (mOpt.socketPath.empty())
                if (const char *p = std::getenv("NTWB_SOCKET")) mOpt.socketPath = p;
            if (mOpt.socketPath.empty())
            {
                err = "NTWB_SOCKET is not set - start the app from Arstro Remote (Apps)";
                return false;
            }
            auto t = std::make_shared<UnixSocketTransport>();
            if (!t->connect(mOpt.socketPath, err)) return false;
            mTransport = t;
        }
        if (mOpt.token.empty())
            if (const char *t = std::getenv("NTWB_TOKEN")) mOpt.token = t;
        Json hello = Json::object();
        hello.set("t", "hello");
        hello.set("ntwb", kVersion);
        hello.set("app", mOpt.app);
        hello.set("version", mOpt.version);
        if (!mOpt.token.empty()) hello.set("token", mOpt.token);
#ifndef _WIN32
        hello.set("pid", (long long)::getpid());
#endif
        if (!mOpt.capabilities.empty())
        {
            Json caps = Json::array();
            for (const auto &c : mOpt.capabilities) caps.push(c);
            hello.set("capabilities", caps);
        }
        if (!sendMsg(hello)) { err = mLastError; return false; }
        return true;
    }

    bool Client::sendMsg(const Json &msg)
    {
        const std::string why = validate(msg, A2H);
        if (!why.empty()) { mLastError = "not sent: " + why; return false; }
        if (!mTransport || !mTransport->send(encodeJson(msg))) { mLastError = "connection lost"; return false; }
        return true;
    }

    bool Client::poll(int timeoutMs)
    {
        if (!mTransport || mEnded) return false;
        char buf[65536];
        long n = mTransport->recv(buf, sizeof buf, timeoutMs);
        while (n > 0)
        {
            mDecoder.feed(buf, (size_t)n);
            n = mTransport->recv(buf, sizeof buf, 0);     // drain what is already there
        }
        Frame f;
        while (!mEnded && mDecoder.next(f))
        {
            if (f.blob) continue;                          // the host never sends blobs to an app
            const std::string why = validate(f.json, H2A);
            if (!why.empty()) { mLastError = "host sent an invalid message: " + why; continue; }
            dispatch(f.json);
        }
        if (!mDecoder.error().empty())
        {
            mLastError = "broken stream: " + mDecoder.error();
            mEnded = true;
        }
        if (n < 0 && !mEnded)
        {
            mLastError = mLastError.empty() ? "the host closed the connection" : mLastError;
            mEnded = true;
        }
        if (mEnded) mTransport->close();
        return !mEnded;
    }

    void Client::dispatch(const Json &msg)
    {
        const std::string t = msg["t"].asString();
        if (t == "welcome")
        {
            mWelcomed = true;
            mSession = msg["session"].asString();
            for (const auto &c : msg["clients"].items()) mClients.insert(c.asString());
            if (onWelcome) onWelcome();
        }
        else if (t == "client.open")
        {
            mClients.insert(msg["client"].asString());
            if (onClientOpen) onClientOpen(msg["client"].asString());
        }
        else if (t == "client.close")
        {
            mClients.erase(msg["client"].asString());
            if (onClientClose) onClientClose(msg["client"].asString());
        }
        else if (t == "call" || t == "notify")
        {
            Call c;
            c.id = msg["id"].asString();
            c.client = msg["client"].asString();
            c.method = msg["method"].asString();
            c.params = msg.has("params") ? msg["params"] : Json::object();
            if (t == "notify")
            {
                if (onNotify) onNotify(c);
                return;
            }
            if (!onCall) { replyError(c.id, "no method named " + c.method); return; }
            try
            {
                bool deferred = false;
                Json data = onCall(c, deferred);
                if (!deferred) reply(c.id, data);
            }
            catch (const MethodError &e) { replyError(c.id, e.what()); }
            catch (const std::exception &e) { replyError(c.id, std::string("internal error: ") + e.what()); }
        }
        else if (t == "ping")
        {
            Json pong = Json::object();
            pong.set("t", "pong");
            pong.set("n", msg["n"]);
            sendMsg(pong);
        }
        else if (t == "bye")
        {
            mEnded = true;
            mLastError = "the host said bye: " + msg["reason"].asString();
            if (onBye) onBye(msg["reason"].asString());
        }
        else if (t == "error")
        {
            if (!mWelcomed) { mEnded = true; mLastError = "refused: " + msg["error"].asString(); }
            if (onError) onError(msg["error"].asString(), msg["about"].asString());
        }
    }

    bool Client::reply(const std::string &callId, const Json &data)
    {
        Json r = Json::object();
        r.set("t", "result");
        r.set("id", callId);
        r.set("ok", true);
        if (!data.isNull()) r.set("data", data);
        return sendMsg(r);
    }

    bool Client::replyError(const std::string &callId, const std::string &error)
    {
        Json r = Json::object();
        r.set("t", "result");
        r.set("id", callId);
        r.set("ok", false);
        r.set("error", error.empty() ? std::string("failed") : error);
        return sendMsg(r);
    }

    bool Client::event(const std::string &name, const Json &data, const std::string &client)
    {
        Json e = Json::object();
        e.set("t", "event");
        e.set("name", name);
        if (!data.isNull()) e.set("data", data);
        if (!client.empty()) e.set("client", client);
        return sendMsg(e);
    }

    bool Client::state(const std::string &key, const Json &data)
    {
        Json s = Json::object();
        s.set("t", "state");
        s.set("key", key);
        s.set("data", data);
        return sendMsg(s);
    }

    bool Client::blob(const std::string &stream, const std::string &mime, const uint8_t *data, size_t n,
                      const Json &meta, const std::string &client, bool coalesce)
    {
        Json h = Json::object();
        h.set("stream", stream);
        h.set("mime", mime);
        if (!meta.isNull()) h.set("meta", meta);
        if (!client.empty()) h.set("client", client);
        if (coalesce) h.set("coalesce", true);
        const std::string why = validateBlobHeader(h, A2H);
        if (!why.empty()) { mLastError = "not sent: " + why; return false; }
        if (!mTransport || !mTransport->send(encodeBlob(h, data, n))) { mLastError = "connection lost"; return false; }
        return true;
    }

    bool Client::log(const std::string &level, const std::string &msg)
    {
        Json l = Json::object();
        l.set("t", "log");
        l.set("level", level);
        l.set("msg", msg);
        return sendMsg(l);
    }

    bool Client::bye(const std::string &reason)
    {
        Json b = Json::object();
        b.set("t", "bye");
        if (!reason.empty()) b.set("reason", reason);
        const bool ok = sendMsg(b);
        mEnded = true;
        return ok;
    }
}
}
