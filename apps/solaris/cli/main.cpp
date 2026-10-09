/*
 *  solaris-cc — Solaris with no window (R-SVC-1, R-API-1, arstro.rule §5).
 *
 *  Seam rule: THIS FILE HOLDS NO BEHAVIOUR. It owns argv, stdin, stdout and the host's file
 *  functions — the things the core is forbidden to touch — and nothing else. Every verb is the
 *  service's grammar (`docs/API.md`), parsed by the same parser the GUI's text dispatch uses, so
 *  anything a script can do here a window can do, and the reverse.
 *
 *      solaris-cc project new song.slp --bpm 128 : strip add --kind instrument --instrument drums
 *      solaris-cc --script song.txt
 *      solaris-cc shell --song song.slp          # one service, many lines, undo kept (R-SVC-8)
 *      solaris-cc api --json                     # the document an agent reads first
 *
 *  Prints what each command produced on stdout; a refusal goes to stderr as `refused: line N: …`
 *  and stops the run with exit code 3 (`--keep-going` runs on and exits 3 at the end). A one-shot
 *  run that would end with UNSAVED edits says so on stderr and exits 4, unless `--discard` — an
 *  agent's work is never dropped in silence (R-SVC-8). `--watch` streams every event to stderr.
 */
#include "AudioFiles.h"
#include "Machine.h"
#ifdef SOLARIS_HAVE_PULSE
#include "AudioOutPulse.h"
#endif
#include "SolarisService.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace arstro;
using namespace arstro::solaris;

namespace
{
    constexpr int kOk = 0, kUsage = 2, kRefused = 3, kUnsaved = 4;

    void usage()
    {
        std::printf(
            "solaris-cc — Solaris from a shell\n\n"
            "  solaris-cc [options] <command> [: <command> …]\n"
            "  solaris-cc [options] --script <file>      one command per line; `#` starts a comment\n"
            "  solaris-cc [options] shell [--song <file.slp>]\n"
            "                                            lines from stdin into ONE song session (undo kept);\n"
            "                                            --song opens the file, or makes it when it is not there\n"
            "  solaris-cc api [--json | --md]            every command, event, model field and device parameter\n\n"
            "Options: --watch (events to stderr) · --keep-going (run past a refusal, exit 3 at the end) ·\n"
            "         --discard (end with unsaved edits without exit 4)\n"
            "Exit: 0 ok · 2 usage · 3 a line was refused (`refused: line N: …`) · 4 unsaved edits left\n\n"
            "Examples:\n"
            "  project new song.slp --bpm 128\n"
            "  clip add --instrument drums --at 0 --length 16      # prints ac_1, then made: strip=… pattern=…\n"
            "  pattern steps pt_1 --pitch kick \"x...x...x...x...\"\n"
            "  notes add pt_2 \"C2@0:0.5 Eb2@1:0.5 G2@2:1:90\"\n"
            "  note add pt_3 --chord Cm7 --at 0 --length 4\n"
            "  ls : show dv_2 : pattern print pt_2\n"
            "  set dv_1.kick.tune=-2 ch_2.gain=-3\n"
            "  project save : render --out mix.wav --stems ch_2\n"
            "  transport play : wait 8 : transport stop     # hear it on the clock device\n"
            "Read docs/AGENTS.md for a whole song.\n");
    }

    std::string quote(const std::string &s)
    {
        return s.find_first_of(" \t\"") == std::string::npos && !s.empty() ? s : "\"" + s + "\"";
    }
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h")
    {
        usage();
        return args.empty() ? kUsage : kOk;
    }
    bool watch = false, keepGoing = false, discard = false, shell = false;
    std::string script, song;
    auto option = [&](std::vector<std::string> &a) {
        if (a[0] == "--watch") { watch = true; a.erase(a.begin()); }
        else if (a[0] == "--keep-going") { keepGoing = true; a.erase(a.begin()); }
        else if (a[0] == "--discard") { discard = true; a.erase(a.begin()); }
        else if (a[0] == "--script" && a.size() > 1) { script = a[1]; a.erase(a.begin(), a.begin() + 2); }
        else return false;
        return true;
    };
    while (!args.empty() && args[0].rfind("--", 0) == 0 && option(args)) {}
    if (!args.empty() && args[0] == "shell")
    {
        shell = true;
        args.erase(args.begin());
        while (!args.empty())
        {
            if (args[0] == "--song" && args.size() > 1) { song = args[1]; args.erase(args.begin(), args.begin() + 2); }
            else if (!option(args)) { std::fprintf(stderr, "shell takes [--song <file.slp>] — not `%s`\n", args[0].c_str()); return kUsage; }
        }
        if (!script.empty()) { std::fprintf(stderr, "shell reads its lines from stdin — give it no --script\n"); return kUsage; }
    }

    SolarisService::Host host;
    host.decodeAudio = solaris_host::decodeAudio;
    host.writeWav = solaris_host::writeWav;
    host.listDir = solaris_host::listDir;
    host.listDevices = solaris_host::listDevices;
#ifdef SOLARIS_HAVE_PULSE
    host.audioOut = [] { return std::unique_ptr<IAudioOut>(new solaris_host::AudioOutPulse()); };
#endif
    // The same settings and recents files the window uses: `settings set` here and the Settings
    // dialog are one setting.
    solaris_host::machinePaths(host.settingsPath, host.recentsPath);
    SolarisService svc(host);
    // The event stream IS the log: a shell run with --watch shows exactly what a window would.
    svc.subscribe([watch](const Event &e) {
        if (watch || e.kind == Event::Kind::Error) std::fprintf(stderr, "%s\n", formatEvent(e).c_str());
    });

    std::string err;
    // One line, numbered as the user wrote it: a refusal says WHERE (R-SVC-8).
    auto run = [&](const std::string &line, int number) {
        if (svc.dispatchText(line, err))
        {
            if (!svc.output().empty()) std::fputs(svc.output().c_str(), stdout);
            return true;
        }
        std::fflush(stdout);
        if (number > 0) std::fprintf(stderr, "refused: line %d: %s\n", number, err.c_str());
        else std::fprintf(stderr, "refused: --song: %s\n", err.c_str());
        return false;
    };
    // An agent's edits are never dropped in silence: a run that ends with an unsaved song says so (R-SVC-8).
    auto finish = [&](int code) {
        if (svc.project() && svc.model().dirty && !discard)
        {
            std::fprintf(stderr, "unsaved: %s has edits that were not saved — end with `project save`, or pass --discard to drop them\n",
                         svc.model().projectPath.c_str());
            if (code == kOk) code = kUnsaved;
        }
        return code;
    };

    if (shell)
    {
        // ONE service for every line, so undo, the open song and its ids last the whole session
        if (!song.empty())
        {
            const bool exists = std::filesystem::exists(song);
            if (!run((exists ? "project open " : "project new ") + quote(song), 0)) return kRefused;
        }
        const bool tty = isatty(0) && isatty(1);
        bool refused = false;
        std::string line;
        for (int n = 1;; ++n)
        {
            if (tty) { std::fputs("solaris> ", stdout); std::fflush(stdout); }
            if (!std::getline(std::cin, line)) break;
            if (line == "exit" || line == "quit") break;
            if (!run(line, n)) refused = true;
        }
        return finish(refused ? kRefused : kOk);
    }

    std::vector<std::pair<std::string, int>> lines; // the text and the line number it is reported as
    if (!script.empty())
    {
        std::ifstream f(script);
        if (!f) { std::fprintf(stderr, "cannot read %s\n", script.c_str()); return kUsage; }
        std::string l;
        for (int n = 1; std::getline(f, l); ++n) lines.emplace_back(l, n);
    }
    // ` : ` separates chained commands, so one invocation can be a whole scenario.
    int number = (int)lines.size();
    std::string cur;
    for (size_t i = 0; i <= args.size(); ++i)
    {
        if (i == args.size() || args[i] == ":")
        {
            if (!cur.empty()) lines.emplace_back(cur, ++number);
            cur.clear();
            continue;
        }
        cur += (cur.empty() ? "" : " ") + quote(args[i]);
    }
    bool refused = false;
    for (const auto &l : lines)
        if (!run(l.first, l.second))
        {
            refused = true;
            if (!keepGoing) break;
        }
    return finish(refused ? kRefused : kOk);
}
