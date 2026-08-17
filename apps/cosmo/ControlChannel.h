/*
 *  Cosmo by arstro — ControlChannel: the wire an agent drives the running window over.
 *
 *  R-SVC-8. `cosmo --control /tmp/cosmo.sock` opens this; whatever connects writes command
 *  lines in and reads event lines out, in exactly the R-SVC-5 grammar. The user keeps
 *  watching the same window while it happens, which is the entire point — a behaviour an
 *  agent cannot reach is a behaviour nobody can verify, and that is what R-SVC exists to fix.
 *
 *  ── Why the service stays in-process, so this channel stays small ─────────────────────
 *
 *  It would be tidier-looking to make cosmo_core a daemon and the GUI a client. It would
 *  also be a mistake: a preview frame is 5-20 MB and a full-res frame ~100 MB, so every
 *  frame would have to cross a socket or a shared-memory dance for no gain — the front ends
 *  are not remote, they are alternative faces of one program. So `CosmoService` lives in the
 *  hosting process and only the *text* crosses this channel: commands one way, `formatEvent`
 *  lines the other. Pixels never touch it. If a real multi-process daemon is ever wanted,
 *  the channel already exists and only its transport changes.
 *
 *  ── The seam rule: this class moves lines, and knows nothing else ─────────────────────
 *
 *  No parsing, no dispatch, no `Command`, no `Event`, no `CosmoService`. It hands each
 *  complete line to a callback and broadcasts the strings it is given. That is deliberate:
 *  the moment a socket learns to parse, R-SVC-5's "one codec" is false and the grammar has
 *  somewhere new to drift to. `Command::parse` and `formatEvent` remain the only two places
 *  that know what a line means.
 *
 *  ── Non-blocking is not a nicety here, it is the requirement ──────────────────────────
 *
 *  `poll()` is called from the GTK timeout at frame rate, on the UI thread. So a single
 *  blocking call anywhere in this file is a frozen window — and it would freeze because of
 *  something entirely outside the app's control, like an agent that connected, sent nothing,
 *  and went to lunch. Hence: non-blocking accept, non-blocking reads, a bounded per-client
 *  outbound buffer flushed a frame at a time, and a client dropped rather than waited on.
 *  `SIGPIPE` is suppressed too, because the default disposition kills the process — a
 *  detached `socat` would otherwise be able to take the editor down with it.
 *
 *  Host layer on purpose: sockets, `unlink` and logging are all forbidden in cosmo_core.
 */
#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class ControlChannel
    {
    public:
        ControlChannel() = default;
        ~ControlChannel();
        ControlChannel(const ControlChannel &) = delete;
        ControlChannel &operator=(const ControlChannel &) = delete;

        /** Start listening. POSIX: an `AF_UNIX`/`SOCK_STREAM` socket at `path`, replacing a
         *  stale node a crashed run left behind (and only ever a *socket* node — a regular
         *  file at that path is somebody's data and is refused instead). Windows: not yet
         *  implemented, see the note in the .cpp; returns false rather than blocking.
         *  False leaves the channel closed and `err` set for the caller to log. */
        bool open(const std::string &path, std::string &err);

        /** Close every client and the listener, and remove the socket node so the next run
         *  finds a clean path. Safe to call when never opened, and called by the destructor. */
        void close();
        bool isOpen() const { return mListen >= 0; }

        /** Accept new clients, read whatever has arrived, and hand each COMPLETE line to
         *  `onLine`. Never blocks: it does one non-blocking pass and returns, so it is safe
         *  from a 16 ms GTK timeout.
         *
         *  Partial reads are buffered per client until the newline, and one write may carry
         *  several commands — a heredoc arrives as one packet and must still run as N lines.
         *  Lines are collected before any callback runs, so `onLine` may freely `broadcast()`
         *  (dispatch usually does, via the event sink) or even `close()` this channel — a
         *  `quit` command does exactly that — without invalidating anything underneath.
         *
         *  The work per call is BOUNDED as well as non-blocking: a peer writing as fast as we
         *  read never returns EAGAIN, so an undrained read loop would hold the frame without
         *  ever blocking in it. Whatever does not fit in this pass waits in the kernel buffer
         *  for the next one. Measured worst case with a 200k-line firehose attached: 2.2 ms. */
        void poll(const std::function<void(const std::string &line)> &onLine);

        /** Send one line to every connected client — the event stream. A missing trailing
         *  newline is added, because a line-oriented reader on the far side will otherwise
         *  wait forever for the rest of a line that is already complete. */
        void broadcast(const std::string &line);

        int clientCount() const { return (int)mClients.size(); }
        /** The path passed to open(), or "" when closed. For the startup log line. */
        const std::string &path() const { return mPath; }

    private:
        /** One attached agent. `in` is the tail of a line that has not ended yet; `out` is
         *  what the socket would not take yet.
         *
         *  `idleFlushes` counts consecutive FRAMES in which `out` was non-empty and not one
         *  byte of it moved, and it is what decides a client is dead. The first version of
         *  this used a byte cap instead and the probe caught it dropping a perfectly healthy
         *  reader during a burst: a backlog measures how fast events were produced, not
         *  whether anyone is reading them. Time does. */
        struct Client
        {
            int fd = -1;
            std::string in;
            std::string out;
            int idleFlushes = 0;
        };

        void acceptNew();
        void readClients(std::vector<std::string> &linesOut);
        /** One frame's worth of outbound work: push what each socket will take, then drop
         *  the clients that have gone quiet or overrun the hard buffer cap. */
        void pumpOutbound();
        /** Push as much of `c.out` as the socket will take, without ever waiting. False =
         *  drop: the write failed for good, or the backlog passed the memory cap. */
        bool flushOne(Client &c);
        void dropClient(size_t index, const char *why);

        // POSIX descriptors. The Windows stub never fills these in; keeping them plain ints
        // is what lets this header stay free of <windows.h>, which every widget would
        // otherwise end up including through App.h.
        int mListen = -1;
        std::vector<Client> mClients;
        std::string mPath;
    };
}
}
