#include "ControlChannel.h"
#include "Log.h"

#include <string>

#ifdef _WIN32
// ── Windows: deliberately unimplemented, and deliberately not blocking ────────────────
//
// R-SVC-8 asks for a named pipe here. A named pipe that behaves the way poll() promises
// needs real overlapped I/O: CreateNamedPipe with FILE_FLAG_OVERLAPPED, one pipe INSTANCE
// per client, a pending ConnectNamedPipe carrying its own manual-reset event, a pending
// ReadFile per client, and GetOverlappedResult(..., FALSE) each frame to test them without
// waiting. That is a few hundred lines of state machine whose failure mode is a hung UI
// thread, and it cannot be exercised at all from the Linux host this was written on.
//
// The one thing that must not happen is a blocking stand-in: ConnectNamedPipe/ReadFile in
// their default mode would park the GTK main loop on a socket, which is the exact defect
// this class is shaped to avoid. PIPE_NOWAIT is not the escape either — Microsoft documents
// it as legacy, and its writes silently drop data when the buffer is full, so the event
// stream would lose lines with no error anywhere.
//
// So: open() fails with a clear reason, and `cosmo --control` on Windows says so at startup
// instead of freezing later. Two routes for whoever finishes it, on a machine that can test
// it: the overlapped named pipe above, or — much smaller — Winsock's AF_UNIX (Windows 10
// 1803+, <afunix.h>), which makes the POSIX branch below almost portable as-is with
// ioctlsocket(FIONBIO) for O_NONBLOCK, DeleteFileA for unlink, and no SIGPIPE to suppress.
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0   // BSD/macOS: SO_NOSIGPIPE below covers the same ground
#endif
#endif

namespace arstro
{
namespace cosmo_v2
{
#ifndef _WIN32
    namespace
    {
        // A line is a command or an event, so kilobytes at the very worst. These bounds exist
        // only to cap what a broken or hostile client can make the editor allocate.
        constexpr size_t kMaxPendingIn = 1u << 20;    // 1 MB of a "line" with no '\n' in it
        constexpr int kBacklog = 8;

        // The two ways a client stops being one, learned from the probe:
        //
        //   kStallFrames — it has pending events and has taken ZERO bytes for this many
        //     consecutive poll() frames. At the GTK tick that is about two seconds of a peer
        //     that is connected but not reading, which is the only honest definition of dead:
        //     a healthy client drains between frames even when it is behind.
        //   kHardOutBytes — the backlog itself is absurd. This is not a liveness test, it is
        //     a memory bound: an event storm inside ONE frame (a load finishing while a
        //     `state print` goes out) legitimately queues megabytes, and 16 MB of text is
        //     ~30k lines, so anything past it is a leak with a remote trigger.
        //
        // Judging by size ALONE is what the first draft did, and the probe caught it evicting
        // a reader that was keeping up perfectly well — 6000 events pushed in 3.9 ms will
        // outrun any peer for a few milliseconds, and that is the producer's speed, not the
        // consumer's failure.
        constexpr int kStallFrames = 120;
        constexpr size_t kHardOutBytes = 16u << 20;

        // "Non-blocking" is not the same as "bounded", and only the second one keeps the
        // window responsive. A peer writing as fast as we read never returns EAGAIN, so an
        // undrained `while (recv(...) > 0)` would spin for as long as it kept typing — a
        // frozen UI reached without a single blocking call. Same for a client reconnecting in
        // a loop. So each frame does a bounded amount of work and the rest simply waits: the
        // data is still in the kernel's buffer and the backlog still holds the connection.
        constexpr size_t kMaxReadPerFrame = 256u << 10;   // per client
        constexpr int kMaxAcceptsPerFrame = 8;

        /** O_NONBLOCK because poll() must never wait; FD_CLOEXEC because an export helper or
         *  a file dialog that forks would otherwise inherit the listener and keep the socket
         *  alive after cosmo exits — the next run would then find a path that looks live. */
        bool prepFd(int fd)
        {
            const int fl = ::fcntl(fd, F_GETFL, 0);
            if (fl < 0 || ::fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) return false;
            const int fdfl = ::fcntl(fd, F_GETFD, 0);
            if (fdfl >= 0) ::fcntl(fd, F_SETFD, fdfl | FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
            const int on = 1;
            ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
            return true;
        }

        /** MSG_NOSIGNAL already protects our own sends; this protects the process from any
         *  other write to a dead peer, since the DEFAULT disposition of SIGPIPE is to kill
         *  it and losing the user's editing session because an agent pressed ctrl-C would be
         *  indefensible. Only SIG_DFL is overridden, so a handler someone installed on
         *  purpose is left exactly where it is. */
        void ignoreSigPipeOnce()
        {
            static bool done = false;
            if (done) return;
            done = true;
            struct sigaction cur;
            std::memset(&cur, 0, sizeof cur);
            if (::sigaction(SIGPIPE, nullptr, &cur) != 0) return;
            if (cur.sa_handler != SIG_DFL) return;
            struct sigaction ign;
            std::memset(&ign, 0, sizeof ign);
            ign.sa_handler = SIG_IGN;
            ::sigaction(SIGPIPE, &ign, nullptr);
        }

        std::string sysErr(const char *what)
        {
            return std::string(what) + ": " + std::strerror(errno);
        }
    }
#endif

    ControlChannel::~ControlChannel() { close(); }

#ifdef _WIN32
    bool ControlChannel::open(const std::string &path, std::string &err)
    {
        err = "control channel is not implemented on Windows yet (R-SVC-8 wants an "
              "overlapped named pipe; a blocking one would freeze the window) — asked for: " + path;
        return false;
    }
    void ControlChannel::close() {}
    void ControlChannel::poll(const std::function<void(const std::string &)> &) {}
    void ControlChannel::broadcast(const std::string &) {}
    void ControlChannel::acceptNew() {}
    void ControlChannel::readClients(std::vector<std::string> &) {}
    void ControlChannel::pumpOutbound() {}
    bool ControlChannel::flushOne(Client &) { return false; }
    void ControlChannel::dropClient(size_t, const char *) {}
#else

    bool ControlChannel::open(const std::string &path, std::string &err)
    {
        if (isOpen())
        {
            err = "control channel is already listening on " + mPath;
            return false;
        }
        if (path.empty())
        {
            err = "control channel: --control needs a socket path";
            return false;
        }

        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof addr);
        addr.sun_family = AF_UNIX;
        // sun_path is a fixed 108-byte field, so this is a real limit and not a defensive
        // check: silently truncating would bind a DIFFERENT path than the one logged, and
        // the client would then fail to connect for no visible reason.
        if (path.size() >= sizeof(addr.sun_path))
        {
            err = "control socket path is " + std::to_string(path.size()) + " bytes, limit is " +
                  std::to_string(sizeof(addr.sun_path) - 1) + ": " + path;
            return false;
        }
        std::memcpy(addr.sun_path, path.c_str(), path.size());

        // A crash or a kill -9 leaves the socket node on disk and bind() then fails with
        // EADDRINUSE forever after, which would make one bad exit poison that path for good.
        // Only a socket is removed: a regular file there belongs to someone, and deleting it
        // would be the worst possible interpretation of "unlink a stale path".
        struct stat st;
        if (::stat(path.c_str(), &st) == 0)
        {
            if (!S_ISSOCK(st.st_mode))
            {
                err = "refusing to use " + path + ": something is already there and it is not a socket";
                return false;
            }
            if (::unlink(path.c_str()) != 0)
            {
                err = sysErr(("could not remove the stale control socket " + path).c_str());
                return false;
            }
            LOGI("control: removed a stale socket node at %s (previous run did not exit cleanly)", path.c_str());
        }

        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
        {
            err = sysErr("control channel: socket(AF_UNIX)");
            return false;
        }
        // Non-blocking BEFORE listen(), so there is no window in which an accept() could
        // wait. prepFd is also what makes the listener not survive into a forked child.
        if (!prepFd(fd))
        {
            err = sysErr("control channel: could not set O_NONBLOCK on the listener");
            ::close(fd);
            return false;
        }
        if (::bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof addr) != 0)
        {
            err = sysErr(("control channel: bind " + path).c_str());
            ::close(fd);
            return false;
        }
        if (::listen(fd, kBacklog) != 0)
        {
            err = sysErr(("control channel: listen " + path).c_str());
            ::close(fd);
            ::unlink(path.c_str());
            return false;
        }

        ignoreSigPipeOnce();
        mListen = fd;
        mPath = path;
        LOGI("control: listening on %s — commands in, events out (R-SVC-8)", mPath.c_str());
        return true;
    }

    void ControlChannel::close()
    {
        const bool was = isOpen();
        for (Client &c : mClients)
            if (c.fd >= 0) ::close(c.fd);
        mClients.clear();
        if (mListen >= 0)
        {
            ::close(mListen);
            mListen = -1;
        }
        if (!mPath.empty())
        {
            // Leaving the node behind would mean the next run has to decide whether a live
            // cosmo owns it. Removing it on the way out keeps that question from existing.
            ::unlink(mPath.c_str());
            if (was) LOGI("control: closed %s", mPath.c_str());
            mPath.clear();
        }
    }

    void ControlChannel::acceptNew()
    {
        // Loop, because several clients can be queued in the backlog between two frames and
        // one accept() per frame would make a burst of attaches look like a stall — but a
        // bounded loop, so a client reconnecting in a tight loop cannot own the frame.
        for (int taken = 0; taken < kMaxAcceptsPerFrame; ++taken)
        {
            const int fd = ::accept(mListen, nullptr, nullptr);
            if (fd < 0)
            {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) return;   // nobody waiting: normal
                LOGW("control: accept failed: %s", std::strerror(errno));
                return;
            }
            if (!prepFd(fd))
            {
                LOGW("control: dropping a client we could not make non-blocking: %s", std::strerror(errno));
                ::close(fd);
                continue;
            }
            Client c;
            c.fd = fd;
            mClients.push_back(std::move(c));
            LOGI("control: client attached (%d now connected)", (int)mClients.size());
        }
    }

    void ControlChannel::dropClient(size_t index, const char *why)
    {
        if (index >= mClients.size()) return;
        if (mClients[index].fd >= 0) ::close(mClients[index].fd);
        mClients.erase(mClients.begin() + (long)index);
        LOGI("control: client detached (%s; %d still connected)", why, (int)mClients.size());
    }

    bool ControlChannel::flushOne(Client &c)
    {
        while (!c.out.empty())
        {
            // MSG_NOSIGNAL: without it a peer that died between two frames would raise
            // SIGPIPE here and the DEFAULT action for that is to end the process — the
            // user's window, closed by somebody else's ctrl-C.
            const ssize_t n = ::send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
            if (n > 0)
            {
                c.out.erase(0, (size_t)n);
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;   // full: keep the rest
            return false;                                                   // EPIPE/ECONNRESET: gone
        }
        // A short write is ordinary: a client mid-`read` while a dump goes out has a full
        // socket buffer for a few milliseconds and must not lose events over it, so the
        // remainder waits for the next frame. Whether it is ALIVE is pumpOutbound's call.
        if (c.out.size() > kHardOutBytes)
        {
            LOGW("control: client is %zu bytes behind — over the %zu byte cap, dropping it",
                 c.out.size(), kHardOutBytes);
            return false;
        }
        return true;
    }

    void ControlChannel::pumpOutbound()
    {
        for (size_t i = 0; i < mClients.size();)
        {
            Client &c = mClients[i];
            const size_t before = c.out.size();
            if (!flushOne(c)) { dropClient(i, "write failed"); continue; }

            // Progress, not position: shrinking (or emptied) means the peer is reading, however
            // far behind it is. Only a frame in which literally nothing moved counts against it.
            if (c.out.empty() || c.out.size() < before) c.idleFlushes = 0;
            else ++c.idleFlushes;

            if (c.idleFlushes >= kStallFrames)
            {
                LOGW("control: client has taken no bytes for %d frames with %zu queued — dropping it "
                     "rather than letting a dead peer own the event stream", c.idleFlushes, c.out.size());
                dropClient(i, "stopped reading the event stream");
                continue;
            }
            ++i;
        }
    }

    void ControlChannel::readClients(std::vector<std::string> &linesOut)
    {
        char buf[4096];
        for (size_t i = 0; i < mClients.size();)
        {
            Client &c = mClients[i];
            const char *dead = nullptr;
            size_t readThisFrame = 0;
            while (readThisFrame < kMaxReadPerFrame)
            {
                const ssize_t n = ::recv(c.fd, buf, sizeof buf, 0);
                if (n > 0)
                {
                    c.in.append(buf, (size_t)n);
                    readThisFrame += (size_t)n;
                    if (c.in.size() > kMaxPendingIn) { dead = "sent a line with no end to it"; break; }
                    continue;   // drain: one write can be bigger than the buffer
                }
                if (n == 0) { dead = "closed the connection"; break; }
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;   // nothing more this frame
                dead = std::strerror(errno);
                break;
            }

            // Complete lines are harvested even when the client has just vanished: an agent
            // that writes `quit` and closes in the same breath still meant the command, and
            // a heredoc'd script arrives as ONE write holding many lines (R-SVC-5's grammar
            // is line-oriented precisely so this is well defined).
            size_t start = 0;
            for (;;)
            {
                const size_t nl = c.in.find('\n', start);
                if (nl == std::string::npos) break;
                size_t end = nl;
                if (end > start && c.in[end - 1] == '\r') --end;   // a CRLF client is still a client
                if (end > start) linesOut.emplace_back(c.in, start, end - start);
                start = nl + 1;
            }
            if (start > 0) c.in.erase(0, start);

            if (dead) dropClient(i, dead);
            else ++i;
        }
    }

    void ControlChannel::poll(const std::function<void(const std::string &line)> &onLine)
    {
        if (!isOpen()) return;

        // Events first: a client that was behind last frame gets room before more is queued,
        // and this is the one place per frame where "is it still reading?" is judged.
        pumpOutbound();

        acceptNew();

        // Read everything, THEN dispatch. onLine almost always ends in broadcast() through
        // the service's event sink, and `quit` closes this channel outright — iterating
        // mClients across a callback that can erase from it or destroy the listener is how
        // this file would earn a use-after-free. The batch is a local copy, so it cannot.
        std::vector<std::string> lines;
        readClients(lines);
        if (!onLine) return;
        for (const std::string &line : lines) onLine(line);
    }

    void ControlChannel::broadcast(const std::string &line)
    {
        if (!isOpen() || mClients.empty()) return;

        std::string wire = line;
        if (wire.empty() || wire.back() != '\n') wire.push_back('\n');

        for (size_t i = 0; i < mClients.size();)
        {
            // Appended, not written directly: if this client already has a backlog, writing
            // now would put the new event AHEAD of older ones and the event stream would
            // reorder itself under load, which no `expect` assertion could survive.
            mClients[i].out += wire;
            // Flushed but NOT judged — broadcast can be called thousands of times inside one
            // frame, and counting each full socket as a stall is exactly how a healthy reader
            // gets evicted. Liveness is a per-frame question, so pumpOutbound owns it.
            if (!flushOne(mClients[i])) dropClient(i, "write failed");
            else ++i;
        }
    }
#endif
}
}
