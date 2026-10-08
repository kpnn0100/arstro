/*
 *  solaris-cc — Solaris with no window (R-SVC-1, R-API-1, arstro.rule §5).
 *
 *  Seam rule: THIS FILE HOLDS NO BEHAVIOUR. It owns argv, stdout and the host's file functions —
 *  the things the core is forbidden to touch — and nothing else. Every verb is the service's
 *  grammar (`docs/API.md`), parsed by the same parser the GUI's text dispatch uses, so anything a
 *  script can do here a window can do, and the reverse.
 *
 *      solaris-cc project new song.slp --bpm 128 : strip add --kind instrument --instrument drums
 *      solaris-cc --script song.txt
 *      solaris-cc api --json            # the document an agent reads first
 *
 *  Prints what each command produced on stdout; a refusal goes to stderr with exit code 3;
 *  `--watch` streams every event to stderr.
 */
#include "AudioFiles.h"
#include "Machine.h"
#ifdef SOLARIS_HAVE_PULSE
#include "AudioOutPulse.h"
#endif
#include "SolarisService.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace arstro;
using namespace arstro::solaris;

namespace
{
    constexpr int kOk = 0, kUsage = 2, kRefused = 3;

    void usage()
    {
        std::printf(
            "solaris-cc — Solaris from a shell\n\n"
            "  solaris-cc [--watch] <command> [: <command> …]\n"
            "  solaris-cc [--watch] --script <file>      one command per line, `#` comments\n"
            "  solaris-cc api [--json | --md]            every command, event, model field and device parameter\n\n"
            "Examples:\n"
            "  project new song.slp --bpm 128\n"
            "  strip add --kind instrument --instrument drums --name Drums\n"
            "  pattern new --name beat --length 4 : note add pt_1 --pitch 36 --at 0\n"
            "  clip add --strip ch_2 --pattern pt_1 --at 0 --length 16\n"
            "  clip add --src samples/hat.wav --at 0\n"
            "  set dv_1.kick.tune=-2 ch_2.gain=-3\n"
            "  render --out mix.wav --stems ch_2\n"
            "  transport play : wait 8 : transport stop     # hear it on the clock device\n");
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
    bool watch = false;
    std::string script;
    while (!args.empty() && args[0].rfind("--", 0) == 0)
    {
        if (args[0] == "--watch") { watch = true; args.erase(args.begin()); }
        else if (args[0] == "--script" && args.size() > 1) { script = args[1]; args.erase(args.begin(), args.begin() + 2); }
        else break;
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

    std::vector<std::string> lines;
    if (!script.empty())
    {
        std::ifstream f(script);
        if (!f) { std::fprintf(stderr, "cannot read %s\n", script.c_str()); return kUsage; }
        std::string l;
        while (std::getline(f, l)) lines.push_back(l);
    }
    // ` : ` separates chained commands, so one invocation can be a whole scenario.
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
        std::string err;
        if (!svc.dispatchText(l, err))
        {
            std::fprintf(stderr, "refused: %s\n", err.c_str());
            return kRefused;
        }
        if (!svc.output().empty()) std::fputs(svc.output().c_str(), stdout);
    }
    return kOk;
}
