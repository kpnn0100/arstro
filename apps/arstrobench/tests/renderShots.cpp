/*
 *  Arstrobench by arstro — arstrobench_shots: the headless screenshot harness (R-TEST-3).
 *
 *  Renders the REAL BenchApp through the REAL Cairo adapter onto an image surface, with no
 *  display, so a UI change can be SEEN before it ships rather than only asserted about.
 *  Mirrors cosmo_shots (apps/cosmo/tests/shots/renderShots.cpp) — the repo's rule is copy
 *  the established harness, not invent one.
 *
 *  Two traps this file exists to avoid, both inherited from cosmo_shots:
 *   * The clock is a fixed 16 ms tick, never the wall clock. Every animation is a function
 *     of the nowMs handed to render(), so a mid-entrance frame is reproducible only when
 *     the frame INDEX decides where it is and the machine's speed does not.
 *   * The vendored fonts are registered before anything is measured. With the host's
 *     default sans instead, every text width is different and the shots would show layout
 *     bugs the app does not have.
 *
 *  No gtk_init(): GTK3 is linked for Cairo only, so nothing here needs a display.
 *
 *  Usage:  arstrobench_shots [--outdir DIR] [--check] [--run]
 *      --check  exit non-zero if any PNG came out uniform-colour (the one failure mode a
 *               shot harness has: quietly writing blank frames)
 *      --run    also drive a REAL benchmark run and shoot the finished state. Off by
 *               default so `ctest` stays fast and deterministic.
 */
#include "BenchApp.h"
#include "adapter/native/CairoTarget.h"
#include <cairo/cairo.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fontconfig/fontconfig.h>
#include <set>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using arstro::arstrobench::BenchApp;
    using arstro::arstrobench::DspWorkload;
    using arstro::arstrobench::ImageWorkload;

    constexpr double kFrameMs = 1000.0 / 60.0;

    class Frame
    {
    public:
        Frame(int w, int h) : mW(w), mH(h)
        {
            mSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
            mCr = cairo_create(mSurface);
        }
        ~Frame() { cairo_destroy(mCr); cairo_surface_destroy(mSurface); }
        Frame(const Frame &) = delete;
        Frame &operator=(const Frame &) = delete;

        cairo_t *cr() { return mCr; }

        /** An image surface keeps what was there; GTK hands a fresh context each frame.
         *  Without this, one frame ghosts into the next and reads as a paint bug. */
        void clear()
        {
            cairo_save(mCr);
            cairo_set_operator(mCr, CAIRO_OPERATOR_SOURCE);
            cairo_set_source_rgb(mCr, 0, 0, 0);
            cairo_paint(mCr);
            cairo_restore(mCr);
        }
        bool write(const std::string &path)
        {
            cairo_surface_flush(mSurface);
            return cairo_surface_write_to_png(mSurface, path.c_str()) == CAIRO_STATUS_SUCCESS;
        }
        /** Distinct colours in the frame. A real screen has thousands; a failed one has 1. */
        size_t distinctColours() const
        {
            cairo_surface_flush(mSurface);
            const unsigned char *data = cairo_image_surface_get_data(mSurface);
            const int stride = cairo_image_surface_get_stride(mSurface);
            if (!data) return 0;
            std::set<uint32_t> seen;
            for (int y = 0; y < mH; y += 2)
                for (int x = 0; x < mW; x += 2)
                {
                    uint32_t px;
                    std::memcpy(&px, data + (size_t)y * stride + (size_t)x * 4, 4);
                    seen.insert(px & 0x00FFFFFFu);
                    if (seen.size() > 4096) return seen.size();
                }
            return seen.size();
        }

    private:
        int mW, mH;
        cairo_surface_t *mSurface = nullptr;
        cairo_t *mCr = nullptr;
    };

    void registerFont(const std::string &path)
    {
        if (!FcConfigAppFontAddFile(FcConfigGetCurrent(), (const FcChar8 *)path.c_str()))
            std::fprintf(stderr, "arstrobench_shots: could not register font %s\n", path.c_str());
    }
    void registerBundledFonts()
    {
        const std::string dir = std::string(COSMO_ASSETS_DIR) + "/fonts";
        registerFont(dir + "/DMSans/DMSans-Regular.ttf");
        registerFont(dir + "/DMSans/DMSans-Medium.ttf");
        registerFont(dir + "/DMSans/DMSans-SemiBold.ttf");
        registerFont(dir + "/JetBrainsMono/JetBrainsMono-Regular.ttf");
        registerFont(dir + "/JetBrainsMono/JetBrainsMono-Medium.ttf");
    }
}

int main(int argc, char **argv)
{
    std::string outdir = "shots";
    bool check = false, doRun = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--outdir" && i + 1 < argc) outdir = argv[++i];
        else if (a == "--check") check = true;
        else if (a == "--run") doRun = true;
        else if (a == "--help") { std::printf("usage: arstrobench_shots [--outdir DIR] [--check] [--run]\n"); return 0; }
    }
    fs::create_directories(outdir);
    registerBundledFonts();

    const int w = (int)BenchApp::kWidth, h = (int)BenchApp::kHeight;
    BenchApp app(BenchApp::kWidth, BenchApp::kHeight);
    artboard::CairoTarget target;   // ONE target for the whole run, re-bound per frame,
    Frame frame(w, h);              // exactly as the host's onDraw re-binds its one target
    int failures = 0, frameIndex = 0;

    // Advance by a fixed tick and write the named frame — reproducible on any machine.
    const auto shoot = [&](const char *name, int frames) {
        for (int i = 0; i < frames; ++i, ++frameIndex)
        {
            frame.clear();
            target.setContext(frame.cr());
            app.render(target, frameIndex * kFrameMs);
        }
        const std::string path = outdir + "/" + name + ".png";
        const bool wrote = frame.write(path);
        const size_t colours = frame.distinctColours();
        std::printf("%-18s %s  (%zu colours)\n", name, wrote ? path.c_str() : "WRITE FAILED", colours);
        if (!wrote) ++failures;
        if (check && colours <= 1) { std::printf("  ^ uniform-colour frame\n"); ++failures; }
    };

    shoot("entrance", 8);   // mid-entrance: the cards are still fading and rising
    shoot("idle", 60);      // settled, nothing measured yet

    if (doRun)
    {
        // A real run, but on the tests' reduced workloads so the harness stays quick.
        app.setWorkloads(ImageWorkload(320, 200, 1), DspWorkload(4800, 1));
        app.startRun();
        shoot("running", 20);
        // Spin the render loop until the worker publishes its last result. Guarded, so a
        // wedged workload fails the harness instead of hanging it.
        for (int guard = 0; app.isRunning() && guard < 20000; ++guard)
        {
            frame.clear();
            target.setContext(frame.cr());
            app.render(target, (frameIndex++) * kFrameMs);
        }
        if (app.isRunning()) { std::printf("run did not finish\n"); ++failures; }
        shoot("done", 90);  // long enough for the count-up to settle
    }

    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "ok", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
