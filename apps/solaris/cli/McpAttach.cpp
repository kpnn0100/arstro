/*
 *  solaris-cc mcp --attach <socket> — the MCP tools on the song that is OPEN IN THE WINDOW (R-MCP-2).
 *
 *      solaris --control /run/user/1000/solaris.sock &
 *      solaris-cc mcp --attach /run/user/1000/solaris.sock      # what an agent's client launches
 *
 *  `solaris-cc mcp` owns a service of its own: a private song the user cannot see. This is the same
 *  three tools (solaris_api / solaris_run / solaris_state) with no service at all: each line goes
 *  down the window's control socket and comes back framed by the ControlServer's own `[out]`,
 *  `[ok] <line>` and `[refused] <why>` (host/ControlServer.h), so the answer to one line ends where
 *  the window says it ends, never on a guess from silence. The user watches the song change, and
 *  every line is one step of the window's own undo.
 *
 *  It is what an in-app agent is given as its hands (as cosmo's `cosmo-cc mcp --attach`).
 */
#include "Faces.h"
#include "mcp/Server.h"
#include "mcp/ServiceTools.h"
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#ifndef _WIN32
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace arstro
{
namespace solaris_cli
{
#ifndef _WIN32
    namespace
    {
        bool starts(const std::string &s, const char *p) { return s.rfind(p, 0) == 0; }

        /** One connection to a window. run() sends a line and reads until that line is answered. */
        class Relay
        {
        public:
            ~Relay() { if (mFd >= 0) ::close(mFd); }

            bool connect(const std::string &sock, std::string &err)
            {
                mFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
                if (mFd < 0) { err = std::string("socket: ") + std::strerror(errno); return false; }
                sockaddr_un addr{};
                addr.sun_family = AF_UNIX;
                if (sock.size() >= sizeof(addr.sun_path)) { err = "the socket path is too long"; return false; }
                std::memcpy(addr.sun_path, sock.c_str(), sock.size());
                if (::connect(mFd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0)
                {
                    err = "cannot connect to " + sock + ": " + std::strerror(errno) + " — is solaris running with --control?";
                    return false;
                }
                return true;
            }

            /** False + `err` = refused, or the window went away. A render can take minutes: the
             *  wait is long, and ends on the window's answer or on the connection closing. */
            bool run(const std::string &lineIn, std::string &out, std::string &err, double timeoutS = 1800.0)
            {
                std::string line = lineIn;
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
                const size_t first = line.find_first_not_of(" \t");
                if (first == std::string::npos || line[first] == '#') return true; // the channel answers no empty line
                const std::string wire = line + "\n";
                for (size_t off = 0; off < wire.size();)
                {
                    const ssize_t n = ::send(mFd, wire.data() + off, wire.size() - off, MSG_NOSIGNAL);
                    if (n < 0 && errno == EINTR) continue;
                    if (n <= 0) { err = "the window closed the connection"; return false; }
                    off += (size_t)n;
                }
                const auto t0 = std::chrono::steady_clock::now();
                while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < timeoutS)
                {
                    size_t nl;
                    while ((nl = mBuf.find('\n')) != std::string::npos)
                    {
                        std::string l = mBuf.substr(0, nl);
                        mBuf.erase(0, nl + 1);
                        if (!l.empty() && l.back() == '\r') l.pop_back();
                        if (starts(l, "[evt] ")) mEvents += l.substr(6) + "\n";
                        else if (starts(l, "[out] ")) out += l.substr(6) + "\n";
                        else if (starts(l, "[ok] ") && l.substr(5) == line) return true;
                        else if (starts(l, "[refused] ")) { err = l.substr(10); return false; }
                    }
                    fd_set rs;
                    FD_ZERO(&rs);
                    FD_SET(mFd, &rs);
                    timeval tv{0, 200000};
                    const int r = ::select(mFd + 1, &rs, nullptr, nullptr, &tv);
                    if (r < 0 && errno != EINTR) { err = std::string("select: ") + std::strerror(errno); return false; }
                    if (r <= 0) continue;
                    char chunk[65536];
                    const ssize_t n = ::recv(mFd, chunk, sizeof chunk, 0);
                    if (n == 0) { err = "the window closed the connection"; return false; }
                    if (n < 0) { if (errno == EINTR) continue; err = std::string("recv: ") + std::strerror(errno); return false; }
                    mBuf.append(chunk, (size_t)n);
                }
                err = "timed out waiting for the window to answer: " + line;
                return false;
            }

            std::string takeEvents() { std::string e; e.swap(mEvents); return e; }

        private:
            int mFd = -1;
            std::string mBuf, mEvents;
        };
    }
#endif

    int mcpAttachMain(const std::string &sock)
    {
#ifdef _WIN32
        (void)sock;
        std::fprintf(stderr, "mcp --attach: not implemented on Windows (cosmo's ControlChannel is POSIX only)\n");
        return 1;
#else
        Relay relay;
        std::string err;
        if (!relay.connect(sock, err)) { std::fprintf(stderr, "mcp --attach: %s\n", err.c_str()); return 1; }
        arstro::mcp::Server server({"solaris", "1",
            "Solaris, the Arstro DAW, ATTACHED to the window the user has open: every line you run changes the song "
            "they are looking at, and is one step of their undo. Read solaris_api first (commands, events, model, "
            "every device parameter). solaris_state shows the song as it is now. Do not `project new` or `project "
            "open` unless asked: that replaces what is on their screen. Check your work with `ls`, `show <id>`, "
            "`audit` and `render --out <file.wav> --measure`."});
        arstro::mcp::TextService t;
        t.app = "solaris";
        t.runHint = "Grammar: solaris_api (e.g. `clip add --instrument drums --at 0 --length 16`).";
        t.runLine = [&relay](const std::string &line, std::string &out, std::string &e) { return relay.run(line, out, e); };
        t.takeEvents = [&relay]() { return relay.takeEvents(); };
        arstro::mcp::addTextServiceTools(server, t);
        return arstro::mcp::serveStdio(server);
#endif
    }
}
}
