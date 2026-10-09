/*
 *  solaris_host — ControlServer: the running window as a peer of solaris-cc (R-SVC-5, R-SVC-6).
 *
 *  `solaris --control <socket>` opens one of these on the window's own SolarisService. Whatever
 *  connects writes command lines in — the grammar of docs/API.md, the same text a click sends —
 *  and reads back, in order:
 *
 *      [evt] <name> k=v …     every event of the service, exactly the line `solaris-cc --watch` prints
 *      [out] <text>           each line of what the command printed (state print, get, audit, …)
 *      [ok] <line>            the command landed — after its events and its output
 *      [refused] <why>        the command was refused — after its `[evt] command.rejected`
 *
 *  so a client knows where one command's answer ends without guessing from silence. Events the
 *  window makes by itself (a click, the user's own edits) arrive as `[evt]` lines with no answer.
 *
 *  ── What is reused, and why this file exists anyway ──────────────────────────────────────
 *
 *  The wire is cosmo's `ControlChannel`, COMPILED IN PLACE (apps/cosmo/ControlChannel.cpp, linked
 *  not copied, as R-UI-4 reuses cosmo's widgets): a non-blocking Unix line socket that moves
 *  lines and never parses them. Its only dependency on cosmo's host is the LOGI/LOGW macros,
 *  which `ControlLog.cpp` satisfies with a stderr line. What the channel deliberately does NOT
 *  know — which line is a command, what to answer — is this class: the Solaris half of cosmo's
 *  `pollControl` (linux_main.cpp), kept out of linux_main so it runs, and is tested, with no
 *  window (`solaris_control_tests`).
 *
 *  ── `wait` is the loop's, not the window's ───────────────────────────────────────────────
 *
 *  The service's `wait <s>` sleeps and pumps — right for solaris-cc, whose loop it is, and a
 *  frozen window if it ran on the GTK thread. Here a valid `wait` HOLDS the queue instead: later
 *  lines wait their turn while the host keeps ticking (the transport moves, meters fall), and
 *  `[ok] wait …` comes when the time is up. An invalid one (out of range, not a number) is
 *  handed to the service, which refuses it with its own sentence and never sleeps.
 *
 *  Host layer: sockets live here, never in solaris_core (R-SVC-4). POSIX only, like the channel.
 */
#pragma once
#include <deque>
#include <memory>
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    class ControlChannel;
}
namespace solaris
{
    class SolarisService;
}
namespace solaris_host
{
    class ControlServer
    {
    public:
        explicit ControlServer(solaris::SolarisService &svc);
        ~ControlServer();
        ControlServer(const ControlServer &) = delete;
        ControlServer &operator=(const ControlServer &) = delete;

        /** Listen on `path` (a stale socket node is replaced; anything else there is refused). */
        bool open(const std::string &path, std::string &err);
        void close();
        bool isOpen() const;
        const std::string &path() const;
        int clientCount() const;

        /** One pass from the host's clock, on the UI thread: accept, read, run what is queued in
         *  order, answer each line. Never blocks. `nowS` is any monotonic clock in seconds. Returns
         *  how many lines it ran (the host repaints when it is non-zero). */
        int poll(double nowS);

        /** True while a `wait` holds the queue. */
        bool holding() const { return mHoldUntil >= 0.0; }

        // the line prefixes of the wire (docs/requirements.md DR-SVC-5)
        static constexpr const char *kOut = "[out] ";
        static constexpr const char *kOk = "[ok] ";
        static constexpr const char *kRefused = "[refused] ";

    private:
        solaris::SolarisService &mSvc;
        std::shared_ptr<cosmo_v2::ControlChannel> mChannel; // the sink holds it weakly: it may outlive us
        std::deque<std::string> mQueue;
        double mHoldUntil = -1.0;
        std::string mHeldLine;
    };
}
}
