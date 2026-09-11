/*
 *  interstellar-cc — Interstellar with no window: front end #1 over InterstellarService (R-CLI-1).
 *
 *  Seam rule: THIS FILE HOLDS NO BEHAVIOUR. It owns argv, stdout, the clock, the writers and the
 *  filesystem — the things interstellar_core is forbidden to touch (R-SVC-7) — and nothing else.
 *  Every state change goes in as a `Command` (R-SVC-2) and everything printed comes out of
 *  `AppModel`/`Event` (R-SVC-3). If a subcommand here needs a behaviour no Command expresses,
 *  that is a defect in the command set, not a licence to reach past the service.
 *
 *  Consequently this is not "Interstellar without the GUI": it is the same service a window
 *  would drive, which is what makes `state print --stable` a comparison two front ends can be
 *  held to (R-SVC-9).
 *
 *  The one host job it does is the frame writer, and it deliberately writes **PPM/PNM** — a
 *  dozen lines, no image library, and byte-comparable, which is the only output a golden-frame
 *  test can assert on (R-RENDER-3). Real codecs are the GUI host's FFmpeg job.
 */
#include "service/AppModelCodec.h"
#include "service/InterstellarService.h"
#ifdef INTERSTELLAR_HAVE_FFMPEG
#include "FrameSourceFFmpeg.h"
#include "FrameWriterFFmpeg.h"
#endif
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace arstro::interstellar;

namespace
{
    constexpr int kOk = 0, kUsage = 2, kRefused = 3, kFail = 4;

    /** A PPM (P6) sequence writer. One file per frame when more than one is written, so a
     *  render produces `out.0001.ppm`, `out.0002.ppm`, … and a still produces `out.ppm`. */
    class PpmWriter : public IFrameWriter
    {
    public:
        explicit PpmWriter(std::string path) : mPath(std::move(path)) {}
        bool begin(const std::string &, int w, int h, double, long long frames) override
        {
            mW = w; mH = h; mIndex = 0;
            // Decided here, not on the second write: a sequence numbers EVERY frame from 0001,
            // and a single frame keeps the name the user asked for.
            mMulti = frames > 1;
            return w > 0 && h > 0;
        }
        bool write(const Raster &f) override
        {
            std::string path = mPath;
            if (mMulti)
            {
                const auto dot = mPath.rfind('.');
                char tail[32];
                std::snprintf(tail, sizeof tail, ".%04d", mIndex + 1);
                path = (dot == std::string::npos ? mPath : mPath.substr(0, dot)) + tail +
                       (dot == std::string::npos ? std::string(".ppm") : mPath.substr(dot));
            }
            std::ofstream o(path, std::ios::binary);
            if (!o) return false;
            o << "P6\n" << f.width << ' ' << f.height << "\n255\n";
            for (size_t i = 0; i + 3 < f.rgba.size(); i += 4)
                o.put((char)f.rgba[i]).put((char)f.rgba[i + 1]).put((char)f.rgba[i + 2]);
            ++mIndex;
            return (bool)o;
        }
        bool end() override { return true; }

    private:
        std::string mPath;
        int mW = 0, mH = 0, mIndex = 0;
        bool mMulti = false;
    };

    struct Options
    {
        bool watch = false, json = false, stable = false, resolved = false;
        std::string script;
    };

    void usage()
    {
        std::printf(
            "interstellar-cc — Interstellar with no window (R-CLI-1)\n\n"
            "  interstellar-cc <command>...            run one or more command lines\n"
            "  interstellar-cc run [--script <f>|-]    replay command lines from a file or stdin\n"
            "  interstellar-cc api [--json]            the generated API document (R-SVC-10)\n"
            "  interstellar-cc help                    this text\n\n"
            "Options\n"
            "  --watch      stream every Event as it happens (R-SVC-5: this IS the log)\n"
            "  --json       JSON where a command can produce it\n"
            "  --stable     omit what is a property of WHEN the dump was taken (R-SVC-9)\n"
            "  --resolved   include every resolved parameter address in a state dump\n\n"
            "The command grammar — one parser, shared by `run`, a script file and the journal:\n\n");
        for (const auto &s : commandSpecs())
            std::printf("  %-18s %s\n      %s\n", s.name, s.args, s.what);
        std::printf("\nA full worked example is in apps/interstellar/docs/project-format.md §9.\n");
    }

    /** `wait` and `state print` and `api` are FRONT-END verbs: the service holds the state and
     *  never blocks (R-SVC-6), so the driving loop implements them by pumping and reading the
     *  model. Doing it here rather than in `dispatch` is the seam, not a shortcut. */
    int runLine(InterstellarService &svc, const std::string &line, const Options &opt)
    {
        std::string err;
        const Command c = parseCommand(line, err);
        if (!c.valid())
        {
            if (err.empty()) return kOk;   // a blank or commented line
            std::fprintf(stderr, "refused: %s\n", err.c_str());
            return kRefused;
        }
        if (opt.watch) std::printf("[cmd] %s\n", formatCommand(c).c_str());

        if (c.kind == Command::Kind::StatePrint)
        {
            DumpOptions o;
            o.stable = opt.stable || c.field("stable") == "1";
            o.json = opt.json || c.field("json") == "1";
            o.resolved = opt.resolved || c.field("resolved") == "1";
            std::fputs(svc.statePrint(o).c_str(), stdout);
            return kOk;
        }
        if (c.kind == Command::Kind::Api)
        {
            std::fputs(svc.apiDocument(opt.json || c.field("json") == "1").c_str(), stdout);
            return kOk;
        }
        if (c.kind == Command::Kind::Wait)
        {
            // Headless, there is no asynchronous work to wait for yet, so this pumps a bounded
            // number of ticks and succeeds. It is here rather than absent so a script written
            // for a live window also runs headless — the same file must work in both.
            for (int i = 0; i < 64; ++i) svc.pump(i * 16.0);
            return kOk;
        }
        if (!svc.dispatch(c))
        {
            std::fprintf(stderr, "refused: %s\n", svc.model().lastError.c_str());
            return kRefused;
        }
        svc.pump(0.0);
        return kOk;
    }
}

int main(int argc, char **argv)
{
    Options opt;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--watch") opt.watch = true;
        else if (a == "--json") opt.json = true;
        else if (a == "--stable") opt.stable = true;
        else if (a == "--resolved") opt.resolved = true;
        else if (a == "--script" && i + 1 < argc) opt.script = argv[++i];
        else args.push_back(a);
    }
    if (args.empty() || args[0] == "help" || args[0] == "--help")
    {
        usage();
        return args.empty() ? kUsage : kOk;
    }

    InterstellarService::Hooks hooks;
#ifdef INTERSTELLAR_HAVE_FFMPEG
    // The real decoder, one per media path — the service asks for a new one per distinct file
    // because a decoder holds a position and is not thread-safe.
    hooks.makeFrameSource = []() -> std::unique_ptr<IFrameSource> {
        return std::unique_ptr<IFrameSource>(new arstro::interstellar_host::FrameSourceFFmpeg());
    };
#endif
    hooks.makeFrameWriter = [](const std::string &path) -> std::unique_ptr<IFrameWriter> {
#ifdef INTERSTELLAR_HAVE_FFMPEG
        // The extension picks the writer. A `.mp4`/`.mov`/`.mkv` gets a real codec; anything else
        // gets the PPM sequence, which is deliberate rather than a fallback — it is the only
        // output a golden test can compare byte for byte (R-RENDER-3).
        if (arstro::interstellar_host::FrameWriterFFmpeg::handles(path))
            return std::unique_ptr<IFrameWriter>(new arstro::interstellar_host::FrameWriterFFmpeg());
#endif
        return std::unique_ptr<IFrameWriter>(new PpmWriter(path));
    };
    InterstellarService svc(hooks);
    if (opt.watch)
        svc.subscribe([](const Event &e) { std::printf("%s\n", formatEvent(e).c_str()); });

    if (args[0] == "api")
    {
        std::fputs(svc.apiDocument(opt.json).c_str(), stdout);
        return kOk;
    }

    if (args[0] == "run")
    {
        std::istream *in = &std::cin;
        std::ifstream file;
        if (!opt.script.empty() && opt.script != "-")
        {
            file.open(opt.script);
            if (!file) { std::fprintf(stderr, "cannot read %s\n", opt.script.c_str()); return kFail; }
            in = &file;
        }
        std::string line;
        int rc = kOk;
        while (std::getline(*in, line))
        {
            const int r = runLine(svc, line, opt);
            if (r != kOk) rc = r;
            if (svc.quitRequested()) break;
        }
        return rc;
    }

    // Otherwise every argument is a command line, joined — so `interstellar-cc set a.b=1` and
    // `interstellar-cc "set a.b=1"` are the same thing.
    std::string joined;
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i) joined += ' ';
        joined += args[i];
    }
    return runLine(svc, joined, opt);
}
