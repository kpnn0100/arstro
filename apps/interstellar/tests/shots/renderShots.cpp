/*
 *  interstellar_shots — every named UI state rendered to a PNG with no display (R-UI-7).
 *
 *  A design change is not done until a frame has been SEEN. This builds the real `App` over a
 *  real service, drives it to a named state, and renders through `artboard::CairoTarget` into an
 *  image surface — no window, no X server.
 *
 *  Three things this harness has that cosmo's does NOT, because they are cheap while a harness
 *  is new and expensive to retrofit (cosmo's skills each cost an agent an investigation to
 *  discover they were missing):
 *
 *    --size WxH   an arbitrary window geometry
 *    --script f   replay command lines, so a shot can be of the state YOUR sequence produced
 *    --tree       a headless UI-tree dump: class, world rect, opacity, per node
 *
 *  And `--check` is what makes it a TEST rather than a smoke run: it fails a shot that came out
 *  uniform-colour, which is the one failure mode a screenshot harness has (a blank shot is worse
 *  than no shot).
 */
#include "../../App.h"
#include "../../../../core/Artboard/src/adapter/native/CairoTarget.h"
#include "../../core/service/InterstellarService.h"
#ifdef INTERSTELLAR_HAVE_FFMPEG
#include "FrameSourceFFmpeg.h"
#endif
#include <cairo/cairo.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace arstro;
using namespace arstro::interstellar;
using arstro::interstellar_v1::App;

namespace
{
    struct Opt
    {
        std::string outdir = "/tmp/interstellar-shots";
        std::string only, script;
        double w = 1440, h = 900;
        bool check = false, list = false, tree = false;
    };

    /** Settle a transition deterministically: one frame at a time at 16 ms, never a single jump
     *  to the end. "Settle then look" is how a snap passes a test — 400 ms is long enough for a
     *  260 ms fade to FINISH, so the assertion reads 1.0 and sees nothing wrong. */
    void settle(App &app, double &now, double ms)
    {
        for (double t = 0; t < ms; t += 16.0)
        {
            now += 16.0;
            app.advance(now);
        }
    }

    bool writePng(cairo_surface_t *s, const std::string &path, bool check)
    {
        cairo_surface_flush(s);
        if (cairo_surface_write_to_png(s, path.c_str()) != CAIRO_STATUS_SUCCESS) return false;
        if (!check) return true;
        // A uniform-colour shot is the one failure a screenshot harness can have and still look
        // like it worked.
        const unsigned char *px = cairo_image_surface_get_data(s);
        const int stride = cairo_image_surface_get_stride(s);
        const int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
        uint32_t first = 0;
        std::memcpy(&first, px, 4);
        for (int y = 0; y < h; y += 4)
            for (int x = 0; x < w; x += 4)
            {
                uint32_t v = 0;
                std::memcpy(&v, px + (size_t)y * stride + (size_t)x * 4, 4);
                if (v != first) return true;
            }
        std::fprintf(stderr, "FAIL %s is a uniform colour\n", path.c_str());
        return false;
    }

    void dumpTree(const artboard::Segment &s, const std::string &name, int depth,
                  std::vector<std::string> &out)
    {
        const artboard::Rect r{s.x.value(), s.y.value(), s.width.value(), s.height.value()};
        char line[256];
        std::snprintf(line, sizeof line, "%*s%-14s x=%.1f y=%.1f w=%.1f h=%.1f opacity=%.3f%s",
                      depth * 2, "", name.c_str(), r.x, r.y, r.w, r.h, s.opacity.value(),
                      s.visible ? "" : " hidden");
        out.push_back(line);
        int i = 0;
        for (const auto &c : s.children())
            dumpTree(*c, "child" + std::to_string(i++), depth + 1, out);
    }

    /** The project every shot is taken of: the user's own scenario. */
    void seed(InterstellarService &svc)
    {
        const char *lines[] = {
            "project new /tmp/shots.isp --fps 24 --res 1920x1080",
            "rack add /tmp/shots-a.mov /tmp/shots-b.mov",
            "track add --kind video --name v0",
            "track add --kind video --name v1",
            "clip add --track v0 --src rack:shots_a --in 12.4 --out 16.6 --at 0 --name clp_a",
            "clip add --track v0 --src rack:shots_b --in 88.0 --out 91.1 --at 4.2 --name clp_b",
            "clip add --track v1 --src rack:shots_a --in 30.0 --out 32.0 --at 1.0 --name clp_c",
            "transition add --between clp_a,clp_b --kind dissolve --dur 0.5",
            "auto new ac_push --dur 2.0 --points 0=0,1=1 --ease easeInOut",
            "auto link ac_push -> clp_a.geom.scale --at 1.0 --dur 2.0 --from 1.0 --to 1.08",
            "auto link ac_push -> clp_a.opacity --at 1.0 --dur 2.0 --from 1.0 --to 0.4",
            "auto new ac_step --dur 1.0 --points 0=1,1=1",
            "auto link ac_step -> clp_c.opacity --at 2.0 --from 0.5 --to 0.5",
            "bind clp_b.geom.scale = 1 + ac_push.value * 0.08",
            "marker add --at 6 --name chorus",
            "playhead 2.0"};
        for (const char *l : lines)
        {
            std::string err;
            if (!svc.dispatchText(l, err))
                std::fprintf(stderr, "seed failed: %s (%s)\n", l, svc.model().lastError.c_str());
        }
    }
}

int main(int argc, char **argv)
{
    Opt o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--outdir" && i + 1 < argc) o.outdir = argv[++i];
        else if (a == "--only" && i + 1 < argc) o.only = argv[++i];
        else if (a == "--script" && i + 1 < argc) o.script = argv[++i];
        else if (a == "--check") o.check = true;
        else if (a == "--list") o.list = true;
        else if (a == "--tree") o.tree = true;
        else if (a == "--size" && i + 1 < argc)
        {
            const std::string s = argv[++i];
            const auto x = s.find('x');
            if (x != std::string::npos)
            {
                o.w = std::atof(s.substr(0, x).c_str());
                o.h = std::atof(s.substr(x + 1).c_str());
            }
        }
    }

    // Every named state, including the ones that get skipped: empty, mid-transition and a
    // deliberately small window.
    struct Shot { const char *name; double w, h; };
    const std::vector<Shot> shots = {
        {"cut-1440x900", 1440, 900},
        {"cut-1024x640", 1024, 640},
        {"mix-1440x900", 1440, 900},
        {"mix-midfade-1440x900", 1440, 900},
        {"grade-1440x900", 1440, 900},
        {"deliver-1440x900", 1440, 900},
        {"empty-1440x900", 1440, 900},
        {"empty-1024x640", 1024, 640}};
    if (o.list)
    {
        for (const auto &s : shots) std::printf("%s\n", s.name);
        return 0;
    }

    int failures = 0;
    for (const auto &shot : shots)
    {
        const std::string name = shot.name;
        if (!o.only.empty() && name.find(o.only) == std::string::npos) continue;

        const double w = o.w != 1440 || o.h != 900 ? o.w : shot.w;
        const double h = o.w != 1440 || o.h != 900 ? o.h : shot.h;

        InterstellarService::Hooks hooks;
#ifdef INTERSTELLAR_HAVE_FFMPEG
        // The real decoder, so a `--script` shot can be of REAL footage rather than of a
        // placeholder — which is the only way to see what a user will actually see.
        hooks.makeFrameSource = []() -> std::unique_ptr<IFrameSource> {
            return std::unique_ptr<IFrameSource>(new arstro::interstellar_host::FrameSourceFFmpeg());
        };
#endif
        InterstellarService svc(hooks);
        const bool empty = name.rfind("empty", 0) == 0;
        if (!empty && o.script.empty()) seed(svc);
        if (!o.script.empty())
        {
            std::ifstream f(o.script);
            std::string line;
            while (std::getline(f, line))
            {
                std::string err;
                svc.dispatchText(line, err);
            }
        }

        App app(svc, w, h);
        double now = 0;
        app.advance(now);

        if (name.rfind("mix-midfade", 0) == 0)
        {
            // MID-TRANSITION, deliberately: a still frame at rest cannot tell an eased
            // implementation from a snapping one, so half of a 260 ms fade is the state to shoot.
            app.showWorkspace(Workspace::Mix);
            settle(app, now, 130.0);
        }
        else if (name.rfind("mix", 0) == 0)
        {
            app.showWorkspace(Workspace::Mix);
            settle(app, now, 400.0);
        }
        else if (name.rfind("grade", 0) == 0)
        {
            app.showWorkspace(Workspace::Grade);
            settle(app, now, 400.0);
        }
        else if (name.rfind("deliver", 0) == 0)
        {
            app.showWorkspace(Workspace::Deliver);
            settle(app, now, 400.0);
        }
        else settle(app, now, 400.0);

        cairo_surface_t *surface =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)w, (int)h);
        cairo_t *cr = cairo_create(surface);
        {
            artboard::CairoTarget target(cr);
            app.render(target, now);
        }
        cairo_destroy(cr);

        const std::string path = o.outdir + "/" + name + ".png";
        if (!writePng(surface, path, o.check)) ++failures;
        else std::printf("wrote %s (%.0fx%.0f)\n", path.c_str(), w, h);
        cairo_surface_destroy(surface);

        if (o.tree)
        {
            // Named by ROLE, not by index. `child2 opacity=0.000` cannot answer "which node is at
            // fault", which is the only question a tree dump exists for — and an empty panel is
            // three different bugs (rows missing, rows off-screen, rows transparent) that only a
            // named node distinguishes.
            std::printf("--- %s tree ---\n", name.c_str());
            std::vector<std::string> lines;
            dumpTree(app.root(), "root", 0, lines);
            std::printf("%s\n", lines.empty() ? "" : lines[0].c_str());
            const std::pair<const char *, artboard::Segment *> named[] = {
                {"WorkspaceBar", &app.bar()},
                {"Monitor", &app.monitor()},
                {"Transport", &app.transport()},
                {"TimelineView", &app.timeline()},
                {"LaneStack", &app.lanes()}};
            for (const auto &n : named)
            {
                std::vector<std::string> sub;
                dumpTree(*n.second, n.first, 1, sub);
                for (const auto &l : sub) std::printf("%s\n", l.c_str());
            }
        }
    }
    if (failures) std::fprintf(stderr, "%d shot(s) failed\n", failures);
    return failures ? 1 : 0;
}
