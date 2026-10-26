/*
 *  interstellar-cc — Interstellar with no window (R-SVC-1, R-API-2).
 *
 *  Seam rule: THIS FILE HOLDS NO BEHAVIOUR. It owns argv, stdout, the clock, the codecs and the
 *  OS paths — the things the core is forbidden to touch — and nothing else. Every verb is the
 *  service's grammar (`docs/API.md`), parsed by the same parser the GUI's text dispatch uses, so
 *  anything a script can do here a window can do, and the reverse.
 *
 *      interstellar-cc project open mv.isp : set s_day01.basic.exposure=0.35 : project save
 *      interstellar-cc --script cut.txt
 *      interstellar-cc api --json            # the document an agent reads first
 *
 *  After each command it pumps until the service is idle (a rack decode, a render), so a script
 *  line never races the one before it — and prints what the command produced on stdout. Refusals
 *  go to stderr with exit code 3; `--watch` streams every event to stderr.
 */
#include "HostSetup.h"
#include "InterstellarService.h"
#include "Ntwb.h"
#include "core/ThreadBudget.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace arstro;
using namespace arstro::interstellar;

namespace
{
    constexpr int kOk = 0, kUsage = 2, kRefused = 3;

    void usage()
    {
        std::printf(
            "interstellar-cc — Interstellar from a shell\n\n"
            "  interstellar-cc [--watch] <command> [: <command> …]\n"
            "  interstellar-cc [--watch] --script <file>      one command per line, `#` comments\n"
            "  interstellar-cc api [--json | --md]            every command, event, model field, address\n"
            "  interstellar-cc ntwb install [--data-dir D]     Interstellar as an Arstro Remote app (its web UI in a browser)\n"
            "  interstellar-cc ntwb uninstall [--data-dir D] | serve | api\n\n"
            "Commands are the service's grammar — `interstellar-cc api --md` prints all of them.\n"
            "Examples:\n"
            "  project new mv.isp --fps 24 --res 1920x1080\n"
            "  rack add footage/a.mov \"footage/b.mov#t=2.0\"\n"
            "  set s_a.basic.exposure=0.35 s_a.basic.temp=5600\n"
            "  timeline new social30 --base main : timeline open social30\n"
            "  render --timeline social30 --out social.mp4\n");
    }

    std::string quote(const std::string &s)
    {
        return s.find_first_of(" \t\"") == std::string::npos || s.empty() ? s : "\"" + s + "\"";
    }

    int run(InterstellarService &svc, const std::string &line)
    {
        std::string err;
        if (!svc.dispatchText(line, err))
        {
            std::fprintf(stderr, "refused: %s\n", err.c_str());
            return kRefused;
        }
        if (!svc.pumpUntilIdle())
        {
            std::fprintf(stderr, "timed out waiting for: %s\n", line.c_str());
            return kRefused;
        }
        if (!svc.output().empty()) std::fputs(svc.output().c_str(), stdout);
        return kOk;
    }
}

int main(int argc, char **argv)
{
    // Line-buffered, so stdout (what a command produced) and stderr (events, refusals) interleave
    // in the order they happened.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h")
    {
        usage();
        return args.empty() ? kUsage : kOk;
    }
    // the web face (R-SVC-4) builds its own service, started by Arstro Remote - before this one exists
    if (args[0] == "ntwb") return interstellar_cli::ntwbMain(std::vector<std::string>(args.begin() + 1, args.end()));
    bool watch = false;
    std::string script;
    while (!args.empty() && args[0].rfind("--", 0) == 0)
    {
        if (args[0] == "--watch") { watch = true; args.erase(args.begin()); }
        else if (args[0] == "--script" && args.size() > 1) { script = args[1]; args.erase(args.begin(), args.begin() + 2); }
        else break;
    }

    cosmo::ThreadBudget budget(50);
    InterstellarService svc(budget, interstellar_cli::makeHost());
    // The event stream IS the log: a shell run with --watch shows exactly what a window would.
    svc.subscribe([watch](const Event &e) {
        if (watch || e.kind == Event::Kind::Error) std::fprintf(stderr, "%s\n", formatEvent(e).c_str());
    });

    std::vector<std::string> lines;
    if (!script.empty())
    {
        std::ifstream f(script);
        if (!f) { std::fprintf(stderr, "cannot read %s\n", script.c_str()); return kUsage; }
        std::string l;
        while (std::getline(f, l)) lines.push_back(l);
    }
    // `:` separates chained commands, so one invocation can be a whole scenario.
    std::string cur;
    for (size_t i = 0; i <= args.size(); ++i)
    {
        if (i == args.size() || args[i] == ":")
        {
            if (!cur.empty()) lines.push_back(cur);
            cur.clear();
            continue;
        }
        cur += (cur.empty() ? "" : " ") + quote(args[i]);
    }
    for (const auto &l : lines)
    {
        const int rc = run(svc, l);
        if (rc != kOk) return rc;
        if (svc.quitRequested()) break;
    }
    return kOk;
}
