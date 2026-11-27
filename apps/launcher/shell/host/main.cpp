/*
 *  arstro-shell — the entry point (R-HOST-1, R-HOST-7).
 *
 *  Two ways to run the same host:
 *    - in a window (or borderless across a whole display) — what a device shows, and what a
 *      developer drives with a mouse standing in for a finger;
 *    - headless, from a script, on a virtual clock — every frame drawn by the same code into a
 *      plain surface, so a PNG from a test is what a screen would have shown.
 */
#include "ShellHost.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace arstro::shell;

static void usage()
{
    std::puts(
        "arstro-shell — the Arstro touch shell\n"
        "\n"
        "  arstro-shell [--display N] [--fullscreen | --size WxH] [--scale S]\n"
        "               [--apps DIR[:DIR…]] [--config FILE] [--control SOCKET] [--script FILE]\n"
        "  arstro-shell --headless --size WxH --script FILE [--scale S] [--apps DIR]\n"
        "\n"
        "  --display N     put the window on display N (0 is the first)\n"
        "  --fullscreen    cover that display, borderless\n"
        "  --size WxH      window size in pixels (default 1280x800)\n"
        "  --scale S       pixels per dp (default: from the screen size)\n"
        "  --apps DIRS     where app bundles are installed (default: ~/.local/share/arstro/apps, /opt/arstro/apps)\n"
        "  --config FILE   settings file (default: ~/.config/arstro/shell.conf; headless: none)\n"
        "  --control PATH  listen for control lines on this socket (see arstro-shell-cc api)\n"
        "  --script FILE   run control lines from a file; `wait <ms>` lets time pass\n"
        "  --headless      no window: draw into memory on a virtual clock (needs --script)\n"
        "\n"
        "Keys in a window: Esc back · Home/F1 home · F2 recents · F3 shade · Ctrl+Q quit");
}

static bool saveShot(SDL_Renderer *r, const std::string &path)
{
    SDL_Surface *raw = SDL_RenderReadPixels(r, nullptr);
    if (!raw) return false;
    SDL_Surface *argb = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(raw);
    if (!argb) return false;
    const bool ok = writePng(path, argb->pixels, argb->w, argb->h, argb->pitch);
    SDL_DestroySurface(argb);
    return ok;
}

int main(int argc, char **argv)
{
    int display = 0, w = 1280, h = 800;
    bool fullscreen = false, headless = false;
    std::string script;
    HostOptions opt;
    bool configGiven = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--display") display = std::atoi(next().c_str());
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--headless") headless = true;
        else if (a == "--size") { if (std::sscanf(next().c_str(), "%dx%d", &w, &h) != 2) { usage(); return 2; } }
        else if (a == "--scale") opt.scale = std::atof(next().c_str());
        else if (a == "--config") { opt.configPath = next(); configGiven = true; }
        else if (a == "--control") opt.controlPath = next();
        else if (a == "--socket") opt.socketPath = next();
        else if (a == "--script") script = next();
        else if (a == "--apps")
        {
            std::string dirs = next(), part;
            for (size_t p = 0; p <= dirs.size(); ++p)
                if (p == dirs.size() || dirs[p] == ':') { if (!part.empty()) opt.appRoots.push_back(part); part.clear(); }
                else part += dirs[p];
        }
        else if (a == "--help" || a == "-h") { usage(); return 0; }
        else { std::fprintf(stderr, "arstro-shell: unknown option '%s'\n\n", a.c_str()); usage(); return 2; }
    }
    if (headless && script.empty()) { std::fprintf(stderr, "arstro-shell: --headless needs --script\n"); return 2; }
    if (!configGiven && !headless)
        if (const char *home = std::getenv("HOME")) opt.configPath = std::string(home) + "/.config/arstro/shell.conf";

    std::vector<std::string> lines;
    if (!script.empty())
    {
        std::ifstream in(script);
        if (!in) { std::fprintf(stderr, "arstro-shell: cannot read script %s\n", script.c_str()); return 2; }
        for (std::string l; std::getline(in, l);)
            if (!l.empty() && l[0] != '#') lines.push_back(l);
    }

    // Headless needs no SDL at all: frames are composited with Cairo.
    if (!headless && !SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "arstro-shell: SDL: %s\n", SDL_GetError()); return 1; }

    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    if (!headless)
    {
        int count = 0;
        SDL_DisplayID *ids = SDL_GetDisplays(&count);
        SDL_Rect bounds{0, 0, w, h};
        const bool haveDisplay = ids && count > 0 && SDL_GetDisplayBounds(ids[std::min(std::max(display, 0), count - 1)], &bounds);
        SDL_free(ids);
        SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
        int x = SDL_WINDOWPOS_CENTERED, y = SDL_WINDOWPOS_CENTERED;
        if (fullscreen && haveDisplay)
        {
            // Borderless over the display rather than a mode switch: it never changes what the
            // rest of the desktop is doing, and on a device there is no desktop to disturb.
            flags |= SDL_WINDOW_BORDERLESS;
            w = bounds.w;
            h = bounds.h;
            x = bounds.x;
            y = bounds.y;
        }
        else
        {
            flags |= SDL_WINDOW_RESIZABLE;
            if (haveDisplay) { x = bounds.x + (bounds.w - w) / 2; y = bounds.y + (bounds.h - h) / 2; }
        }
        window = SDL_CreateWindow("Arstro", w, h, flags);
        if (window)
        {
            SDL_SetWindowPosition(window, x, y);
            renderer = SDL_CreateRenderer(window, nullptr);
            if (renderer) SDL_SetRenderVSync(renderer, 1);
        }
    }
    if (!renderer && !headless) { std::fprintf(stderr, "arstro-shell: cannot create a renderer: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    int exitCode = 0;
    {
        ShellHost host(renderer, opt);
        int pw = w, ph = h;
        if (renderer) SDL_GetRenderOutputSize(renderer, &pw, &ph);
        host.resize(pw, ph);

        size_t pc = 0;          // next script line
        double waitUntil = 0.0; // script: time must pass before the next line
        double virtualNow = 1000.0;
        bool running = true, pressed = false;
        const double pxPerPoint = window ? SDL_GetWindowPixelDensity(window) : 1.0;

        while (running && !host.quitRequested())
        {
            const double now = headless ? virtualNow : (double)SDL_GetTicksNS() / 1e6;
            if (!headless)
            {
                SDL_Event e;
                while (SDL_PollEvent(&e))
                {
                    switch (e.type)
                    {
                    case SDL_EVENT_QUIT: running = false; break;
                    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                        SDL_GetRenderOutputSize(renderer, &pw, &ph);
                        host.resize(pw, ph);
                        break;
                    // A mouse stands in for one finger. SDL also turns a real touch into these
                    // events, so one path serves both.
                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                        if (e.button.button == SDL_BUTTON_LEFT)
                        {
                            pressed = true;
                            host.touch(TouchSample::Kind::Down, e.button.x * pxPerPoint, e.button.y * pxPerPoint, now);
                        }
                        break;
                    case SDL_EVENT_MOUSE_MOTION:
                        if (pressed) host.touch(TouchSample::Kind::Move, e.motion.x * pxPerPoint, e.motion.y * pxPerPoint, now);
                        break;
                    case SDL_EVENT_MOUSE_BUTTON_UP:
                        if (e.button.button == SDL_BUTTON_LEFT && pressed)
                        {
                            pressed = false;
                            host.touch(TouchSample::Kind::Up, e.button.x * pxPerPoint, e.button.y * pxPerPoint, now);
                        }
                        break;
                    case SDL_EVENT_KEY_DOWN:
                        if (e.key.key == SDLK_ESCAPE) host.runLine("nav back");
                        else if (e.key.key == SDLK_HOME || e.key.key == SDLK_F1) host.runLine("nav home");
                        else if (e.key.key == SDLK_F2) host.runLine("nav recents");
                        else if (e.key.key == SDLK_F3) host.runLine("shade toggle");
                        else if (e.key.key == SDLK_Q && (e.key.mod & SDL_KMOD_CTRL)) running = false;
                        break;
                    default: break;
                    }
                }
            }

            // The script: one line at a time, never while a scripted finger is still moving.
            while (pc < lines.size() && now >= waitUntil && !host.synthBusy())
            {
                const std::string &line = lines[pc++];
                if (line.compare(0, 5, "wait ") == 0) { waitUntil = now + std::atof(line.c_str() + 5); break; }
                const std::string reply = host.runLine(line);
                if (reply.compare(0, 6, "error:") == 0)
                {
                    std::fprintf(stderr, "%s: line %zu: %s\n  %s\n", script.c_str(), pc, line.c_str(), reply.c_str());
                    exitCode = 1;
                }
                else if (reply != "ok") std::fputs(reply.c_str(), stdout), std::fputc('\n', stdout);
                if (line.compare(0, 4, "tap ") == 0 || line.compare(0, 6, "swipe ") == 0 || line.compare(0, 5, "shot ") == 0) break;
            }

            host.frame(now);
            const std::string shot = host.takeShotRequest();
            if (!shot.empty())
            {
                cairo_surface_t *cv = host.canvas();
                const bool ok = headless ? (cv && cairo_surface_write_to_png(cv, shot.c_str()) == CAIRO_STATUS_SUCCESS)
                                         : saveShot(renderer, shot);
                if (!ok) { std::fprintf(stderr, "arstro-shell: cannot write %s\n", shot.c_str()); exitCode = 1; }
            }
            if (renderer) SDL_RenderPresent(renderer);
            host.presented(now);

            if (headless)
            {
                virtualNow += 1000.0 / 60.0;
                usleep(2500);  // real time for the app processes to answer between virtual frames
                if (pc >= lines.size() && now >= waitUntil && !host.synthBusy()) running = false;
            }
        }
    }
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    if (!headless) SDL_Quit();
    return exitCode;
}
