/*
 *  interstellar-cc — Interstellar with no window.
 *
 *  Seam rule: THIS FILE HOLDS NO BEHAVIOUR. It owns argv, stdout, the clock and the codecs — the
 *  things the core is forbidden to touch — and nothing else.
 *
 *  At P1 it drives the RACK only, because the rack is all that exists: that is deliberate rather
 *  than partial. The gate this phase has to pass is a colour edit reaching a real `.cmp`, and this
 *  is the surface that proves it from a shell, with the output pasted into the commit.
 */
#include "Rack.h"
#include "core/ThreadBudget.h"
#ifdef INTERSTELLAR_HAVE_FFMPEG
#include "VideoFrameDecoder.h"
#endif
#include "core/decode/NativeImageDecoder.h"
#include "engine/EditParamsIO.h"
#include <cstdio>
#include <cstring>
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
            "interstellar-cc — the rack, from a shell (P1)\n\n"
            "  rack open <file.cmp>              open the colour project\n"
            "  rack new  <file.cmp>              create one\n"
            "  rack add  <media...>              add sources (video or still)\n"
            "  rack list                         the tree, with node ids\n"
            "  rack set  <node> <key>=<value>    grade a node — writes THROUGH to the .cmp\n"
            "  rack get  <node> <key>            its own value\n"
            "  rack eff  <node>                  its EFFECTIVE params (own, stacked up its parents)\n"
            "  rack save                         save the .cmp\n\n"
            "Several verbs may be chained in one invocation, which is how the P1 gate is run:\n"
            "  interstellar-cc rack open r.cmp : rack set 1 exposure=0.2 : rack save\n\n"
            "Keys are COSMO's own (exposure, contrast, temp, mixerSpread, …), so a preset, a .cmp,\n"
            "a `cosmo-cc set` line and this command all spell a parameter identically.\n");
    }

    /** The host's decoder: video-aware where FFmpeg is present, Cosmo's own otherwise. This is the
     *  seam that lets a video source be graded with no change to Cosmo (R-RACK-3). */
    Rack::DecoderFactory makeDecoderFactory()
    {
        return [] {
#ifdef INTERSTELLAR_HAVE_FFMPEG
            return std::unique_ptr<cosmo::IImageDecoder>(new interstellar_host::VideoFrameDecoder());
#else
            return std::unique_ptr<cosmo::IImageDecoder>(new cosmo::NativeImageDecoder());
#endif
        };
    }

    int runVerb(Rack &rack, const std::vector<std::string> &a)
    {
        std::string err;
        auto refuse = [&](const std::string &why) {
            std::fprintf(stderr, "refused: %s\n", why.c_str());
            return kRefused;
        };
        if (a.empty() || a[0] != "rack") { usage(); return kUsage; }
        if (a.size() < 2) { usage(); return kUsage; }
        const std::string &verb = a[1];

        if (verb == "open" || verb == "new")
        {
            if (a.size() < 3) return refuse(verb + " needs a .cmp path");
            const bool ok = verb == "open" ? rack.openProject(a[2], err) : rack.newProject(a[2], err);
            if (!ok) return refuse(err);
            std::printf("%s %s (%d image%s)\n", verb == "open" ? "opened" : "created", a[2].c_str(),
                        rack.imageCount(), rack.imageCount() == 1 ? "" : "s");
            return kOk;
        }
        if (verb == "add")
        {
            if (a.size() < 3) return refuse("add needs one or more media paths");
            std::vector<std::string> paths(a.begin() + 2, a.end());
            if (!rack.addSources(paths, err)) return refuse(err);
            std::printf("added %zu source%s (%d image%s in the rack)\n", paths.size(),
                        paths.size() == 1 ? "" : "s", rack.imageCount(),
                        rack.imageCount() == 1 ? "" : "s");
            return kOk;
        }
        if (verb == "list")
        {
            for (const auto &n : rack.nodes())
                std::printf("%*s%-4d %-7s %s%s%s\n", n.depth * 2, "", n.id,
                            n.group ? "group" : "source", n.cosmoName.c_str(),
                            n.bypass ? "  [bypassed]" : "", n.failed ? "  [offline]" : "");
            return kOk;
        }
        if (verb == "set")
        {
            if (a.size() < 4) return refuse("set needs <node> <key>=<value>");
            const std::string &kv = a[3];
            const auto eq = kv.find('=');
            if (eq == std::string::npos) return refuse("not an assignment: '" + kv + "'");
            if (!rack.setParam(std::atoi(a[2].c_str()), kv.substr(0, eq), kv.substr(eq + 1), err))
                return refuse(err);
            std::printf("node %s %s\n", a[2].c_str(), kv.c_str());
            return kOk;
        }
        if (verb == "get")
        {
            if (a.size() < 4) return refuse("get needs <node> <key>");
            double v = 0;
            if (!rack.getParam(std::atoi(a[2].c_str()), a[3], v))
                return refuse("no such node or key: " + a[2] + " " + a[3]);
            std::printf("%.6g\n", v);
            return kOk;
        }
        if (verb == "eff")
        {
            if (a.size() < 3) return refuse("eff needs <node>");
            EditParams p;
            if (!rack.effectiveParams(std::atoi(a[2].c_str()), p))
                return refuse("no such node: " + a[2]);
            std::fputs(serializeParams(p).c_str(), stdout);
            return kOk;
        }
        if (verb == "save")
        {
            if (!rack.save(err)) return refuse(err);
            std::printf("saved %s\n", rack.path().c_str());
            return kOk;
        }
        return refuse("unknown rack verb: " + verb);
    }
}

int main(int argc, char **argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "help" || args[0] == "--help") { usage(); return args.empty() ? kUsage : kOk; }

    cosmo::ThreadBudget budget(50);
    Rack rack(budget);
    rack.setDecoderFactory(makeDecoderFactory());
    // The event stream IS the log, so a shell run shows exactly what a window would have shown.
    rack.subscribe([](const cosmo::Event &e) {
        std::fprintf(stderr, "[rack] %s\n", cosmo::formatEvent(e).c_str());
    });

    // `:` separates chained verbs, so one invocation can be a whole scenario — which is what the
    // P1 gate is, and a gate that needs three shell lines is a gate nobody re-runs.
    std::vector<std::string> verb;
    int rc = kOk;
    for (size_t i = 0; i <= args.size(); ++i)
    {
        if (i == args.size() || args[i] == ":")
        {
            if (!verb.empty())
            {
                const int r = runVerb(rack, verb);
                if (r != kOk) return r;
                verb.clear();
            }
            continue;
        }
        verb.push_back(args[i]);
    }
    return rc;
}
