/*
 *  arstro-shell-cc — the shell without a screen (R-CORE-4, R-CORE-5).
 *
 *    arstro-shell-cc api [--json]          the API document, generated from the code
 *    arstro-shell-cc run <script|->        run command lines against a fresh service; prints
 *                                          every event and the final state
 *    arstro-shell-cc send <socket> <line>  send one control line to a running shell
 *
 *  `run` uses the real ShellService with the installed apps (or $ARSTRO_APPS): it is the whole
 *  application minus pixels and processes.
 */
#include "../core/AppRegistry.h"
#include "../core/ShellService.h"
#include "../protocol/Channel.h"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <string>
#include <unistd.h>

using namespace arstro::shell;

static int usage()
{
    std::fputs("usage: arstro-shell-cc api [--json]\n"
               "       arstro-shell-cc run <script|->\n"
               "       arstro-shell-cc send <socket> <control line…>\n", stderr);
    return 2;
}

int main(int argc, char **argv)
{
    if (argc < 2) return usage();
    const std::string cmd = argv[1];
    if (cmd == "api")
    {
        if (argc == 3 && std::string(argv[2]) == "--json") std::fputs(apiJson().c_str(), stdout);
        else if (argc == 2) std::fputs(apiMarkdown().c_str(), stdout);
        else return usage();
        return 0;
    }
    if (cmd == "run" && argc == 3)
    {
        ShellService svc;
        svc.subscribe([](const Event &e) { std::printf("[evt] %s\n", formatEvent(e).c_str()); });
        svc.setScanner([] { return scanApps(defaultAppRoots()); });
        svc.run("apps rescan");
        std::ifstream file;
        const bool in = std::string(argv[2]) == "-";
        if (!in) { file.open(argv[2]); if (!file) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 2; } }
        std::istream &src = in ? std::cin : file;
        int failed = 0;
        for (std::string line; std::getline(src, line);)
        {
            if (line.empty() || line[0] == '#') continue;
            if (!svc.run(line)) ++failed;
        }
        std::fputs(svc.dump().c_str(), stdout);
        return failed ? 1 : 0;
    }
    if (cmd == "send" && argc >= 4)
    {
        const int fd = arstro::wire::connectTo(argv[2]);
        if (fd < 0) { std::fprintf(stderr, "cannot reach a shell at %s\n", argv[2]); return 2; }
        std::string line;
        for (int i = 3; i < argc; ++i) line += (i > 3 ? " " : "") + quoteValue(argv[i]);
        line += "\n";
        (void)!::write(fd, line.data(), line.size());
        // The reply ends with a line that is "ok" or starts with "error:".
        std::string reply;
        for (;;)
        {
            pollfd p{fd, POLLIN, 0};
            if (::poll(&p, 1, 3000) <= 0) break;
            char buf[4096];
            const ssize_t n = ::read(fd, buf, sizeof buf);
            if (n <= 0) break;
            reply.append(buf, (size_t)n);
            const size_t last = reply.rfind('\n', reply.size() - 2);
            const std::string tail = reply.substr(last == std::string::npos ? 0 : last + 1);
            if (reply.back() == '\n' && (tail == "ok\n" || tail.compare(0, 6, "error:") == 0)) break;
        }
        ::close(fd);
        std::fputs(reply.c_str(), stdout);
        return reply.find("error:") == std::string::npos && !reply.empty() ? 0 : 1;
    }
    return usage();
}
