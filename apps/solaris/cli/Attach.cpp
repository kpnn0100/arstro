/*
 *  solaris-cc attach — a terminal on a RUNNING window (R-SVC-5).
 *
 *      solaris --control /tmp/solaris.sock &
 *      solaris-cc attach /tmp/solaris.sock --script song.txt      # or: … attach <sock> clip add … : …
 *
 *  The only verb of solaris-cc that builds NO service: the song, the engine and the audio stay in
 *  the window's process and only text crosses (host/ControlServer.h has the wire). It sends one line,
 *  waits for that line's `[ok]` / `[refused]`, sends the next — so a script runs in order and ends
 *  when its last answer arrives, never on a guess from silence.
 *
 *  It prints what solaris-cc prints, where solaris-cc prints it: a command's output on stdout, the
 *  event lines on stderr, a refusal as `refused: <why>` with exit 3. So one script run headless with
 *  `solaris-cc --watch --script` and through a window with `attach` can be diffed line for line —
 *  which is the equivalence test (R-SVC-6, tests/acceptance/run.sh). Events are always printed:
 *  watching the song being made is what attaching is for.
 *
 *  `--follow` keeps printing the window's events after the last line (or with no lines at all)
 *  until the window closes or `--timeout` passes. One client should drive at a time; any number may
 *  follow.
 */
#include "Faces.h"
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
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
    namespace
    {
        constexpr int kOk = 0, kFail = 1, kUsage = 2, kRefused = 3;

        bool starts(const std::string &s, const char *p) { return s.rfind(p, 0) == 0; }

        std::string quote(const std::string &s)
        {
            return s.find_first_of(" \t\"") == std::string::npos && !s.empty() ? s : "\"" + s + "\"";
        }
    }

    int attachMain(const std::vector<std::string> &argsIn)
    {
#ifdef _WIN32
        (void)argsIn;
        std::fprintf(stderr, "attach: not implemented on Windows (cosmo's ControlChannel is POSIX only)\n");
        return kFail;
#else
        std::vector<std::string> args = argsIn;
        std::string sock, script;
        bool follow = false;
        double timeoutS = 300.0;
        std::vector<std::string> rest;
        for (size_t i = 0; i < args.size(); ++i)
        {
            const std::string &a = args[i];
            if (a == "--script" && i + 1 < args.size()) script = args[++i];
            else if (a == "--timeout" && i + 1 < args.size()) timeoutS = std::atof(args[++i].c_str());
            else if (a == "--follow") follow = true;
            else if (sock.empty() && !starts(a, "--")) sock = a;
            else rest.push_back(a);
        }
        if (sock.empty())
        {
            std::fprintf(stderr, "attach: need a socket path — solaris-cc attach <socket> [--script f] [--follow] [<command> : …]\n");
            return kUsage;
        }

        // the lines: a script, then any chained on the command line (` : ` between them, as solaris-cc)
        std::vector<std::string> lines;
        if (!script.empty())
        {
            std::ifstream f(script);
            if (!f) { std::fprintf(stderr, "attach: cannot read %s\n", script.c_str()); return kUsage; }
            for (std::string l; std::getline(f, l);) lines.push_back(l);
        }
        std::string cur;
        for (size_t i = 0; i <= rest.size(); ++i)
        {
            if (i == rest.size() || rest[i] == ":")
            {
                if (!cur.empty()) lines.push_back(cur);
                cur.clear();
                continue;
            }
            cur += (cur.empty() ? "" : " ") + quote(rest[i]);
        }
        const bool fromStdin = script.empty() && lines.empty() && !follow;

        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) { std::perror("attach: socket"); return kFail; }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (sock.size() >= sizeof(addr.sun_path))
        {
            std::fprintf(stderr, "attach: socket path is longer than %zu bytes (use a shorter or relative one)\n", sizeof(addr.sun_path) - 1);
            ::close(fd);
            return kUsage;
        }
        std::memcpy(addr.sun_path, sock.c_str(), sock.size());
        if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0)
        {
            std::fprintf(stderr, "attach: cannot connect to %s: %s — is solaris running with --control?\n", sock.c_str(), std::strerror(errno));
            ::close(fd);
            return kFail;
        }

        const auto t0 = std::chrono::steady_clock::now();
        auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
        std::string buf;
        size_t next = 0;
        bool awaiting = false, refused = false, closed = false;
        std::string sent;

        auto sendLine = [&](const std::string &line) {
            const std::string wire = line + "\n";
            size_t off = 0;
            while (off < wire.size())
            {
                const ssize_t n = ::send(fd, wire.data() + off, wire.size() - off, MSG_NOSIGNAL);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) return false;
                off += (size_t)n;
            }
            return true;
        };
        // one line that came back: print it where solaris-cc would, and see whether it answers ours
        auto take = [&](const std::string &line) {
            if (starts(line, "[evt] ")) { std::fprintf(stderr, "%s\n", line.c_str()); return; }
            if (starts(line, "[out] ")) { std::fputs((line.substr(6) + "\n").c_str(), stdout); return; }
            if (!awaiting) return; // someone else's answer
            if (starts(line, "[ok] ") && line.substr(5) == sent) awaiting = false;
            else if (starts(line, "[refused] "))
            {
                std::fprintf(stderr, "refused: %s\n", line.substr(10).c_str());
                awaiting = false;
                refused = true;
            }
        };

        while (elapsed() < timeoutS)
        {
            if (!awaiting && !refused)
            {
                std::string line;
                bool have = false;
                if (fromStdin)
                {
                    if (std::getline(std::cin, line)) have = true;
                }
                else if (next < lines.size())
                {
                    line = lines[next++];
                    have = true;
                }
                if (have && !line.empty() && line.back() == '\r') line.pop_back(); // the channel drops it too
                if (have && line.empty()) continue; // the channel delivers no empty line, so none is answered
                if (have)
                {
                    if (!sendLine(line)) { std::fprintf(stderr, "attach: the window closed the connection\n"); ::close(fd); return kFail; }
                    sent = line;
                    awaiting = true;
                }
                else if (!follow)
                    break; // every line answered
            }
            if (refused) break;

            fd_set rs;
            FD_ZERO(&rs);
            FD_SET(fd, &rs);
            timeval tv{0, 200000};
            const int r = ::select(fd + 1, &rs, nullptr, nullptr, &tv);
            if (r < 0 && errno != EINTR) { std::perror("attach: select"); break; }
            if (r <= 0) continue;
            char chunk[65536];
            const ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
            if (n == 0) { closed = true; break; }
            if (n < 0) { if (errno == EINTR) continue; std::perror("attach: recv"); break; }
            buf.append(chunk, (size_t)n);
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos)
            {
                std::string line = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                take(line);
            }
            std::fflush(stdout);
        }
        ::close(fd);
        std::fflush(stdout);
        if (refused) return kRefused;
        if (awaiting)
        {
            std::fprintf(stderr, closed ? "attach: the window closed the connection before answering: %s\n"
                                        : "attach: timed out waiting for the answer to: %s\n",
                         sent.c_str());
            return kFail;
        }
        if (closed && follow) std::fprintf(stderr, "attach: the window closed the connection\n");
        return kOk;
#endif
    }
}
}
