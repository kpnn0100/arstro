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
#include "mcp/ServiceTools.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>

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
            "  interstellar-cc ntwb uninstall [--data-dir D] | serve | api\n"
            "  interstellar-cc mcp                            Interstellar as an MCP server on stdio (R-MCP-1)\n\n"
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

    /** R-MCP-1: Interstellar as a Model Context Protocol server. The same service, and the same
     *  `run` an argv command goes through — so an agent's line and a shell's line cannot differ.
     *  One extra tool, `interstellar_frame`, because an agent grading video must SEE a frame:
     *  it is `export-still` (the grammar's own, R-RENDER-5) into a temp file, read back. */
    int serveMcp(InterstellarService &svc, std::string &events)
    {
        arstro::mcp::Server server({"interstellar", "1",
            "Interstellar, the Arstro video colour tool that can cut, driven through its own command grammar. "
            "Read interstellar_api first (commands, addresses, events, model). Typical loop: interstellar_run "
            "\"project open <f.isp>\" -> interstellar_state -> interstellar_frame at=<t> (look) -> interstellar_run "
            "\"set <address>=<value>\" -> interstellar_frame again -> interstellar_run \"project save\"."});
        arstro::mcp::TextService t;
        t.app = "interstellar";
        t.runHint = "Grammar: docs/API.md (e.g. `set s_a.basic.exposure=0.35`, `timeline new social30 --base main`).";
        t.runLine = [&svc](const std::string &line, std::string &out, std::string &err) {
            if (!svc.dispatchText(line, err)) return false;
            if (!svc.pumpUntilIdle()) { err = "timed out waiting for: " + line; return false; }
            out = svc.output();
            return true;
        };
        t.takeEvents = [&events]() { std::string e; e.swap(events); return e; };
        arstro::mcp::addTextServiceTools(server, t);

        arstro::mcp::Tool f;
        f.name = "interstellar_frame";
        f.description = "Look at one composited, graded frame of a timeline (the current one unless named) at a "
                        "time in seconds, as a PNG — what export-still writes.";
        std::string perr;
        f.inputSchema = arstro::mcp::Json::parse(
            R"({"type":"object","properties":{"at":{"type":"number"},"timeline":{"type":"string"}}})", perr);
        f.call = [&svc, &events](const arstro::mcp::Json &a) {
            const std::string png = (std::filesystem::temp_directory_path() /
                                     ("interstellar-mcp-" + std::to_string((long)getpid()) + ".png")).string();
            // The current timeline unless one is named — what the monitor shows. export-still
            // requires the name (R-RENDER-1), so the default is resolved from the model, by name.
            std::string tl = a.has("timeline") ? a["timeline"].asString() : std::string();
            if (tl.empty())
                for (const auto &x : svc.model().timelines)
                    if (x.id == svc.model().currentTimeline) tl = x.name;
            if (tl.empty()) return arstro::mcp::ToolResult::error("no timeline is open (project open / project new first)");
            std::string line = "export-still --out " + quote(png) + " --timeline " + quote(tl);
            if (a.has("at")) line += " --at " + std::to_string(a["at"].asNumber());
            std::string err;
            if (!svc.dispatchText(line, err) || !svc.pumpUntilIdle())
                return arstro::mcp::ToolResult::error(line + ": " + (err.empty() ? "timed out" : err));
            std::ifstream in(png, std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::filesystem::remove(png);
            events.clear();
            if (bytes.empty()) return arstro::mcp::ToolResult::error(line + ": nothing was written");
            return arstro::mcp::ToolResult::text(line).addImage(bytes, "image/png");
        };
        server.add(f);
        return arstro::mcp::serveStdio(server);
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
    if (!args.empty() && args[0] == "mcp")
    {
        std::string events;
        svc.subscribe([&events](const Event &e) { events += formatEvent(e) + "\n"; });
        svc.subscribe([](const Event &e) {
            if (e.kind == Event::Kind::Error) std::fprintf(stderr, "%s\n", formatEvent(e).c_str());
        });
        return serveMcp(svc, events);
    }
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
