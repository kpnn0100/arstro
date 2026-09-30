/*
 *  Arstro Ntwb — Client: the app side of NTWB, for C++ apps (NTWB-09).
 *
 *      ntwb::Client c({"cosmo", "2.4"});                  // app id, app version
 *      c.onCall = [&](const ntwb::Call &call) -> ntwb::Json {
 *          if (call.method == "add") return call.params["a"].asInt() + call.params["b"].asInt();
 *          throw ntwb::MethodError("no such method");
 *      };
 *      std::string err;
 *      if (!c.connect(err)) ...                            // NTWB_SOCKET / NTWB_TOKEN from the env
 *      while (c.poll(16)) { ... pump your service, c.state("model", ...) ... }
 *
 *  Poll-driven on purpose: the host of an Arstro app owns the clock (R-SVC-6 in cosmo), so
 *  the client never starts a thread. `poll()` reads what arrived (waiting at most the given
 *  time), dispatches handlers on the caller's thread, and answers `ping` itself.
 *
 *  Everything sent is validated against the protocol table first (NTWB-06): a message the
 *  host would refuse is reported as an error here instead of being sent.
 */
#pragma once
#include "ntwb/Json.h"
#include "ntwb/Protocol.h"
#include "ntwb/Transport.h"
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace arstro
{
namespace ntwb
{
    /** Throw from a call handler: answered as `result` ok=false with this sentence. */
    class MethodError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    struct Call
    {
        std::string id;         // empty for a notify
        std::string client;
        std::string method;
        Json params;            // always an object
    };

    class Client
    {
    public:
        struct Options
        {
            std::string app;                    // manifest id
            std::string version;                // the app's own version
            std::vector<std::string> capabilities;
            std::string socketPath;             // default: $NTWB_SOCKET
            std::string token;                  // default: $NTWB_TOKEN
        };

        explicit Client(Options o);
        ~Client();

        /** Use this transport instead of connecting to a socket (tests). Then connect() only
         *  sends `hello`. */
        void setTransport(std::shared_ptr<ITransport> t) { mTransport = std::move(t); }

        bool connect(std::string &err);
        /** One pass: wait up to `timeoutMs` for input, dispatch everything complete. False once
         *  the connection is over (host `bye`, closed socket, broken stream) — see lastError(). */
        bool poll(int timeoutMs = 0);
        bool connected() const { return mWelcomed && mTransport && mTransport->isOpen() && !mEnded; }
        bool welcomed() const { return mWelcomed; }
        const std::string &session() const { return mSession; }
        const std::string &lastError() const { return mLastError; }
        const std::set<std::string> &clients() const { return mClients; }

        // ── handlers (called from poll) ──
        /** Return the result's data; throw MethodError for a user-facing failure. Leave unset
         *  and every call is answered "no method". To answer later, set `deferred` and call
         *  reply()/replyError() yourself. */
        std::function<Json(const Call &, bool &deferred)> onCall;
        std::function<void(const Call &)> onNotify;
        std::function<void(const std::string &client)> onClientOpen;
        std::function<void(const std::string &client)> onClientClose;
        std::function<void()> onWelcome;
        std::function<void(const std::string &reason)> onBye;
        /** The host reported a protocol error about something we sent. */
        std::function<void(const std::string &error, const std::string &about)> onError;

        // ── sending ── (each returns false if the message was invalid or the link is down)
        bool reply(const std::string &callId, const Json &data);
        bool replyError(const std::string &callId, const std::string &error);
        bool event(const std::string &name, const Json &data = Json(), const std::string &client = "");
        bool state(const std::string &key, const Json &data);
        /** `coalesce` = a live stream (previews): a newer blob may replace this one while it
         *  waits for a slow client. Leave it false for anything that must arrive (thumbnails). */
        bool blob(const std::string &stream, const std::string &mime, const uint8_t *data, size_t n,
                  const Json &meta = Json(), const std::string &client = "", bool coalesce = false);
        bool log(const std::string &level, const std::string &msg);
        bool bye(const std::string &reason);

    private:
        bool sendMsg(const Json &msg);
        void dispatch(const Json &msg);

        Options mOpt;
        std::shared_ptr<ITransport> mTransport;
        Decoder mDecoder;
        bool mWelcomed = false;
        bool mEnded = false;
        std::string mSession;
        std::string mLastError;
        std::set<std::string> mClients;
    };
}
}
