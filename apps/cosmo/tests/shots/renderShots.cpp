/*
 *  cosmo — cosmo_shots: the headless screenshot harness (P0.2; closes D-7 together with
 *  P0.1's COSMO_APP_NOMAIN).
 *
 *  Renders the REAL App through the REAL Cairo adapter into PNG files with no display, so
 *  that a UI change can be SEEN before it ships rather than assumed. This is the check
 *  that U1.2 did not have: an edit whose match string used `--` where the source had an em
 *  dash compiled, passed both suites and passed the widget test, and only a rendered frame
 *  of the assembled app showed that the click did nothing.
 *
 *  Mirrors apps/genesis/tests/renderShots.cpp — the ledger's rule is copy Genesis, do not
 *  invent — plus the two things cosmo has that Genesis does not: a service to drive
 *  (R-SVC-1) and a three-part open transition worth catching mid-flight (R-LOADING).
 *
 *  ── Three traps this file exists to avoid ─────────────────────────────────────────
 *
 *   * **The clock is a fixed 16 ms tick, never the wall clock.** Every animation in App is
 *     a function of the nowMs handed to render(), so a mid-transition frame is
 *     reproducible only when the frame INDEX decides where it is and the machine's speed
 *     does not. Where a shot cannot avoid waiting on real work (a RAW decode, a preview
 *     render) it waits for the animation to have SETTLED, so the extra frames cannot move
 *     the picture.
 *   * **`App::pointer`'s kind codes are 0 = Down, 2 = Up.** Anything else is a Move: a `1`
 *     produces a hover wash and no click, which is exactly how a screenshot can lie about
 *     a button working.
 *   * **XDG_CONFIG_HOME is redirected into --outdir before anything reads it.** The
 *     launcher renders the RECENTS FILE, so a shot taken against the developer's own
 *     config dir is a picture of their machine, not of the app — and the harness would
 *     also be writing to the user's real settings while it was at it.
 *
 *  No gtk_init(): GTK3 is linked for GdkPixbuf (the decoder + the export encoder live in
 *  the app layer, R-SVC-7) and never initialised, so nothing here needs a display. The
 *  vendored fonts are registered straight into fontconfig, the same call linux_main.cpp
 *  makes — with the host's default sans instead, every text measurement is off and the
 *  shots would report layout bugs that do not exist.
 *
 *  Usage:
 *     cosmo_shots [--outdir DIR] [--images a.RAF,b.RAF] [--only SUBSTR] [--check] [--list]
 *
 *  Without --images the project shots are skipped and everything else still renders — the
 *  fixture-free subset is what `ctest -R cosmo_shots_headless` runs.
 */
#include "App.h"
#include "OmpPin.h"
#include "adapter/native/CairoTarget.h"
#include "core/AppSettings.h"
#include "core/ProjectStore.h"
#include "core/decode/NativeImageDecoder.h"
#include "core/service/CosmoService.h"
#include <algorithm>
#include <cairo/cairo.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fontconfig/fontconfig.h>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using arstro::cosmo::AppSettings;
    using arstro::cosmo::Command;
    using arstro::cosmo::CosmoService;
    using arstro::cosmo::DecodedImage;
    using arstro::cosmo::Event;
    using arstro::cosmo::NativeImageDecoder;
    using arstro::cosmo::ProjectStore;
    using arstro::cosmo::RecentEntry;
    using arstro::cosmo_v2::App;

    /** One 60 Hz frame. The shot clock's only unit — see the header: the frame index, not
     *  the machine, decides where an animation is. */
    constexpr double kFrameMs = 16.0;

    // The transition constants App::render works to (App.cpp). Duplicated deliberately:
    // they are private to App, and a shot that says "halfway through the intro" has to be
    // able to say WHICH half. If App's timings change and these do not, the mid-transition
    // shots move — which is a visible result, not a silent one.
    constexpr double kIntroMs = 460.0;   // wordmark fly + name grow + backdrop reveal
    constexpr double kRevealMs = 520.0;  // loading screen dissolves, editor materialises

    // ── the surface a shot is drawn on ────────────────────────────────────────────────

    /** A fixed-size ARGB32 image surface to draw a shot on. This is the whole of the "no
     *  display" part: GTK hands App a cairo_t for a window, we hand it a cairo_t for a
     *  buffer, and App cannot tell the difference.
     *
     *  Deliberately NOT the owner of the CairoTarget — that lives on the Rig, for a whole
     *  run, and is re-bound to each frame's context exactly as the host's onDraw re-binds
     *  its one target. IRenderTarget::registerImage hands back an id that the adapter
     *  resolves against ITS OWN cache, and every photo, filmstrip thumbnail and loading
     *  cover holds one; a fresh target per frame silently blanks all of them, which is
     *  first seen the moment a shot changes window size. */
    class Frame
    {
    public:
        Frame(int w, int h) : mW(w), mH(h)
        {
            mSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
            mCr = cairo_create(mSurface);
        }
        ~Frame()
        {
            cairo_destroy(mCr);
            cairo_surface_destroy(mSurface);
        }
        Frame(const Frame &) = delete;
        Frame &operator=(const Frame &) = delete;

        cairo_t *cr() { return mCr; }
        int width() const { return mW; }
        int height() const { return mH; }

        /** GTK hands the draw handler a FRESH context every frame; an image surface keeps
         *  what was there. Without this, one frame's overlay ghosts into the next and reads
         *  as a paint bug that the running app does not have. */
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

        /** How many distinct colours the frame actually contains. A shot renderer that
         *  quietly emits blank frames is worse than none, so every written PNG is measured:
         *  a real cosmo screen has thousands, a failed one has 1. */
        size_t distinctColours() const
        {
            cairo_surface_flush(mSurface);
            const unsigned char *data = cairo_image_surface_get_data(mSurface);
            const int stride = cairo_image_surface_get_stride(mSurface);
            if (!data) return 0;
            std::set<uint32_t> seen;
            for (int y = 0; y < mH; y += 2)          // every other pixel: enough to judge, cheap
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

    // ── the run ───────────────────────────────────────────────────────────────────────

    struct Options
    {
        fs::path outdir = "cosmo-shots";
        std::vector<std::string> images;   // real photos for the project shots; empty = skip them
        std::string only;                  // render only shots whose name contains this
        bool check = false;                // fail the run on a uniform-colour PNG
        bool list = false;
    };

    struct Written
    {
        std::string name;
        int w = 0, h = 0;
        size_t bytes = 0;
        size_t colours = 0;
    };

    std::vector<Written> gWritten;
    Options gOpt;

    bool wanted(const std::string &name)
    {
        return gOpt.only.empty() || name.find(gOpt.only) != std::string::npos;
    }

    /** Write the frame as `<outdir>/cosmo-<name>-<w>x<h>.png` and record what came out. */
    void save(Frame &f, const std::string &name)
    {
        char stem[256];
        std::snprintf(stem, sizeof stem, "cosmo-%s-%dx%d.png", name.c_str(), f.width(), f.height());
        const fs::path path = gOpt.outdir / stem;
        if (!f.write(path.string()))
        {
            std::printf("  FAILED to write %s\n", path.string().c_str());
            return;
        }
        Written w;
        w.name = stem;
        w.w = f.width();
        w.h = f.height();
        std::error_code ec;
        w.bytes = (size_t)fs::file_size(path, ec);
        w.colours = f.distinctColours();
        std::printf("  %-46s %5dx%-5d %8zu B  %5zu colours%s\n", w.name.c_str(), w.w, w.h,
                    w.bytes, w.colours, w.colours < 8 ? "   <-- BLANK" : "");
        gWritten.push_back(std::move(w));
    }

    // ── the app, wired the way linux_main.cpp wires it ────────────────────────────────

    /** App + CosmoService + the host-side jobs the core deliberately does not do (decode,
     *  the OpenMP pin). The event adapter below is linux_main.cpp's onServiceEvent with the
     *  window parts removed: the service says what changed, the view says what it does
     *  about it (R-SVC-4), and a shot must take the same path a click does or it is a
     *  picture of a code path nobody ships. */
    struct Rig
    {
        // S4c: declaration order is construction order — budget, service, then the view that
        // draws it. Identical to linux_main.cpp's Host, deliberately: a shot must take the
        // same path a click does.
        arstro::cosmo::ThreadBudget budget;
        CosmoService svc{budget};
        App app;
        // One adapter for the whole run, re-bound per frame — see Frame's comment: the
        // image ids the widgets hold belong to this object, not to a surface.
        artboard::CairoTarget target;
        NativeImageDecoder decoder;
        std::map<std::string, DecodedImage> covers;   // decoded first-images, keyed by path
        double now = 0.0;
        bool coverSent = false;
        bool revealed = false;

        Rig(int w, int h) : app(svc, (double)w, (double)h)
        {
            svc.setDecoderFactory(
                [] { return std::unique_ptr<arstro::cosmo::IImageDecoder>(new NativeImageDecoder()); });
            // Per-THREAD, on the thread: the OpenMP count is a per-thread ICV, so a front
            // end that drops this pin takes the whole machine however the pool was sized
            // (R-CPU-2c, D-12). A shot run is a front end like any other.
            svc.setWorkerInit([] { arstro::cosmo_v2::pinNestedOpenMPForThisThread(); });
            svc.subscribe([this](const Event &e) { onEvent(e); });

            // Whatever the (scratch) config dir says, applied through the one way in so the
            // shots start from the same state the window would (D-15).
            const AppSettings s = AppSettings::load();
            app.applySettings(s);
            svc.applySettings(s);
            app.onCommand = [this](Command c) { svc.dispatch(c); };

            // The launcher asks the host to decode each recent's cover; here that is a
            // synchronous decode, which is also what makes the home shots deterministic.
            app.onDecodeThumbnail = [this](int idx, const std::string &path) {
                const DecodedImage *cover = coverFor(path);
                if (cover) app.setHomeThumbnail(idx, cover->rgba.data(), cover->width, cover->height);
            };
        }

        /** Decode + downscale once, then reuse — the same cache linux_main keeps, for the
         *  same reason: the home grid and the loading screen want the same pixels. */
        const DecodedImage *coverFor(const std::string &path)
        {
            auto it = covers.find(path);
            if (it != covers.end()) return it->second.ok() ? &it->second : nullptr;
            DecodedImage img = decoder.decodeFile(path);
            if (!img.ok()) { covers[path] = DecodedImage{}; return nullptr; }
            covers[path] = downscale(img, 480);
            return &covers[path];
        }

        static DecodedImage downscale(const DecodedImage &src, int maxEdge)
        {
            if (!src.ok() || (src.width <= maxEdge && src.height <= maxEdge)) return src;
            const double sc = (double)maxEdge / std::max(src.width, src.height);
            const int nw = std::max(1, (int)(src.width * sc)), nh = std::max(1, (int)(src.height * sc));
            DecodedImage out;
            out.width = nw; out.height = nh;
            out.rgba.resize((size_t)nw * nh * 4);
            for (int y = 0; y < nh; ++y)
                for (int x = 0; x < nw; ++x)
                {
                    const int sx = std::min(src.width - 1, (int)(x / sc));
                    const int sy = std::min(src.height - 1, (int)(y / sc));
                    std::memcpy(&out.rgba[((size_t)y * nw + x) * 4],
                                &src.rgba[((size_t)sy * src.width + sx) * 4], 4);
                }
            return out;
        }

        void onEvent(const Event &e)
        {
            using K = Event::Kind;
            switch (e.kind)
            {
            case K::ProjectOpening:
                // Ordering is load-bearing (linux_main): resetWorkspace clears the visible
                // state, so beginning the transition after it would fade in from nothing.
                app.beginOpenTransition(e.text);
                app.resetWorkspace();
                app.setLoadProgress(0, e.b);
                app.setStreamProgress(0, e.b);
                coverSent = false;
                revealed = false;
                break;
            case K::EntryDecoded:
            {
                const int slot = e.b;
                // The filmstrip's thumb pool is indexed BY SLOT and must be fed in lockstep
                // with attachment, or a cell draws the previous project's image.
                app.registerThumb(slot);
                if (!coverSent && !app.hasLoadingCover())
                {
                    if (const auto *t = svc.session().thumbForSlot(slot))
                        app.setLoadingCover(t->rgba.data(), t->w, t->h);
                    coverSent = true;
                }
                if (!revealed)
                {
                    revealed = true;
                    app.selectImage(slot);
                    app.setLoadUsable();
                }
                else
                    app.refreshLibrary();
                break;
            }
            case K::EntryFailed:
                app.refreshLibrary();
                break;
            case K::LoadProgress:
                if (!e.text.empty()) app.setLoadStatus("Loading  " + e.text);
                app.setLoadProgress(e.a, e.b);
                app.setStreamProgress(e.a, e.b);
                break;
            case K::LoadFinished:
                app.setStreamProgress(e.b, e.b);
                app.refreshLibrary();
                app.finishOpenTransition();
                break;
            case K::SelectionChanged:
            case K::ParamsChanged:
            case K::HistoryChanged:
                app.syncFromSession();
                break;
            case K::Error:
            case K::CommandRejected:
                std::printf("  service: %s\n", e.text.c_str());
                break;
            default:
                break;
            }
        }

        /** `n` frames of the shot clock, pumped and drawn exactly as the GTK tick does. */
        void step(Frame &f, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                svc.pump(now);
                f.clear();
                target.setContext(f.cr());   // the host's onDraw, minus the window
                app.render(target, now);
                now += kFrameMs;
            }
        }
        void settle(Frame &f, double ms) { step(f, (int)(ms / kFrameMs)); }

        /** Tick until BOTH a simulated and a real budget are spent. Used only where the
         *  frame being waited for comes from another thread (the decode pool, the render
         *  worker): the animation has finished long before, so the extra frames cannot move
         *  the picture and the shot stays reproducible.
         *
         *  It exists because there is no signal to wait on: `AppModel::frameSeq` documents
         *  "a test can wait for one" and `Event::FrameReady` documents a preview landing,
         *  and NEITHER is ever produced — CosmoService's mFrameSeq is declared and never
         *  incremented, and no code path emits FrameReady. Reported, not worked around
         *  further; core/ is not this file's to change. */
        void settleQuiet(Frame &f, double simMs, int realMs)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(realMs);
            const double until = now + simMs;
            while (now < until || std::chrono::steady_clock::now() < deadline) step(f, 1);
        }

        /** Open a .cmp through the service and drive the transition to its end, the way the
         *  window does. Returns false if the load did not finish inside `timeoutSec`. */
        bool openProject(Frame &f, const std::string &cmp, int timeoutSec = 300)
        {
            Command c;
            c.kind = Command::Kind::ProjectOpen;
            c.path = cmp;
            if (!svc.dispatch(c)) return false;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);
            while (svc.model().load.active)
            {
                step(f, 1);
                if (std::chrono::steady_clock::now() > deadline) return false;
            }
            // The reveal is an ANIMATION, not a load state — the load can be finished while
            // the star sky is still on screen. Tick it out, then give the render worker its
            // real-time floor so the photo stage has an actual preview in it.
            settle(f, kRevealMs + 600.0);
            settleQuiet(f, 400.0, 6000);
            return true;
        }

        /** Jump the shot clock to a round ABSOLUTE value. Every animation phase is relative
         *  to the frame it starts on, so a scripted transition looks the same wherever the
         *  clock happens to be — but the loading screen's star twinkle is a function of the
         *  absolute nowMs, and after a real project load that number carries however many
         *  frames the decode took on this machine. Snapping first is the difference between
         *  shots that are equivalent and shots that are byte-identical run to run; without
         *  it the star sky is the one thing in the whole set that moves. */
        void snapClock() { now = std::ceil(now / 10000.0) * 10000.0; }

        /** A tap: Down, then Up at the same point 48 ms later. The codes are 0 and 2 — a
         *  `1` is a MOVE, which produces a hover wash and no click at all. */
        void click(Frame &f, double x, double y)
        {
            app.pointer(0, x, y, 1, now);
            step(f, 3);
            app.pointer(2, x, y, 1, now);
            step(f, 1);
        }
    };

    // ── fixtures the shots need on disk ───────────────────────────────────────────────

    /** A `.cmp` IS a workspace catalog referencing images on disk (R-HOME-2), so a project
     *  fixture is this file plus the photos it points at — no cosmo run required to make
     *  one. Format: EditSession::saveWorkspaceAs. */
    bool writeCmp(const fs::path &path, const std::vector<std::string> &images)
    {
        std::ofstream f(path);
        if (!f) return false;
        f << "cosmoworkspace=1\n";
        for (const auto &img : images)
            f << "#image\nparent=-1\npath=" << fs::absolute(img).string() << "\n";
        return true;
    }

    /** Seed the recents index the launcher reads. `lastOpened` is offset from NOW rather
     *  than from a fixed epoch because the card renders a RELATIVE date ("2h ago"), so a
     *  fixed timestamp would drift into "4 years ago" and change the shot over time. */
    void seedRecents(const std::vector<RecentEntry> &entries)
    {
        for (auto it = entries.rbegin(); it != entries.rend(); ++it)  // remember() prepends
            ProjectStore::remember(*it);
    }

    void registerBundledFonts()
    {
        // COSMO_SOURCE_DIR is baked in by CMakeLists.txt, exactly as for the app, so the
        // fonts are found whatever directory the harness was launched from.
        const std::string dir = std::string(COSMO_SOURCE_DIR) + "/assets/fonts";
        const char *files[] = {"/DMSans/DMSans-Regular.ttf", "/DMSans/DMSans-Medium.ttf",
                               "/DMSans/DMSans-SemiBold.ttf", "/JetBrainsMono/JetBrainsMono-Regular.ttf",
                               "/JetBrainsMono/JetBrainsMono-Medium.ttf"};
        for (const char *f : files)
        {
            const std::string path = dir + f;
            if (!FcConfigAppFontAddFile(FcConfigGetCurrent(), (const FcChar8 *)path.c_str()))
                std::printf("  warning: could not register font %s\n", path.c_str());
        }
    }

    void setEnv(const char *key, const std::string &value)
    {
#ifdef _WIN32
        _putenv_s(key, value.c_str());
#else
        setenv(key, value.c_str(), 1);
#endif
    }

    // ── the shots ─────────────────────────────────────────────────────────────────────

    /** Home with nothing in it: the empty launcher, which is the first thing a new install
     *  shows and the state most easily broken by a change to the recents grid. */
    void shotHomeEmpty(int w, int h)
    {
        if (!wanted("home-empty")) return;
        setEnv("XDG_CONFIG_HOME", (gOpt.outdir / "config-empty").string());
        Rig rig(w, h);
        Frame f(w, h);
        rig.app.showHome();
        rig.settle(f, 900.0);          // the entry cross-fade has played out
        save(f, "home-empty");
    }

    /** Home with recents, at two window sizes. The second size is not decoration: the grid
     *  auto-fills its columns, so a card that only fits at 1600 px is a layout bug nobody
     *  sees until someone runs cosmo on a laptop. */
    void shotHomeRecents(const std::vector<int> &sizes, const std::vector<std::string> &images)
    {
        if (!wanted("home-recents") && !wanted("home-settings")) return;
        setEnv("XDG_CONFIG_HOME", (gOpt.outdir / "config-recents").string());
        const fs::path projects = gOpt.outdir / "projects";
        fs::create_directories(projects);

        // Three projects, so the grid has to lay out more than one row's worth of chrome.
        // They reference the real photos when there are any: a launcher shot whose cards
        // are all placeholder rectangles cannot show that cover thumbnails still work.
        const long long nowSec = (long long)std::time(nullptr);
        struct Seed { const char *name; int photos; long long bytes; long long ago; };
        const Seed seeds[] = {{"Tokyo Streets", 2, 52945344LL, 2 * 3600},
                              {"Studio Portraits", 8, 1288490188LL, 3 * 86400},
                              {"Iceland Coast", 24, 4187593113LL, 21 * 86400}};
        std::vector<RecentEntry> recents;
        for (size_t i = 0; i < 3; ++i)
        {
            const fs::path cmp = projects / (std::string(seeds[i].name) + ".cmp");
            std::vector<std::string> imgs;
            if (!images.empty()) imgs.push_back(images[i % images.size()]);
            writeCmp(cmp, imgs);
            RecentEntry e;
            e.name = seeds[i].name;
            e.path = cmp.string();
            e.firstImagePath = imgs.empty() ? "" : fs::absolute(imgs.front()).string();
            e.photoCount = seeds[i].photos;
            e.sizeBytes = seeds[i].bytes;
            e.lastOpened = nowSec - seeds[i].ago;
            recents.push_back(std::move(e));
        }
        seedRecents(recents);

        for (size_t i = 0; i + 1 < sizes.size(); i += 2)
        {
            const int w = sizes[i], h = sizes[i + 1];
            if (wanted("home-recents"))
            {
                Rig rig(w, h);
                Frame f(w, h);
                rig.app.showHome();     // decodes the covers synchronously via onDecodeThumbnail
                rig.settle(f, 900.0);
                save(f, "home-recents");
            }
            if (i == 0 && wanted("home-settings"))
            {
                // A CLICK, not a call: the settings modal is reachable from Home through the
                // sidebar link (R-SETTINGS-5), and the point of a shot renderer is to prove
                // the path a user takes, not the one the code offers. The geometry mirrors
                // HomeScreen::bottomLinkRect(0) — link 0 of the block 137 px off the bottom,
                // inside the 300 px sidebar's 32 px padding. It is protected, so the widget
                // tests subclass to reach it and this duplicates it; if the sidebar moves,
                // the miss shows up as a shot with no dialog in it.
                Rig rig(w, h);
                Frame f(w, h);
                rig.app.showHome();
                rig.settle(f, 900.0);
                rig.click(f, 150.0, (double)h - 137.0 + 16.0 + 9.0);
                rig.settle(f, 500.0);   // the modal's open tween has finished
                save(f, "home-settings");
            }
        }
    }

    /** The editor with no project: the state the app is in after New before anything is
     *  imported, and the one every panel has to survive with nothing selected. */
    void shotEditorEmpty(int w, int h)
    {
        if (!wanted("editor-empty")) return;
        setEnv("XDG_CONFIG_HOME", (gOpt.outdir / "config-empty").string());
        Rig rig(w, h);
        Frame f(w, h);
        rig.app.showEditor();
        rig.settle(f, 1200.0);
        save(f, "editor-empty");
    }

    /** R-SCALE: the shell at every offered scale, in the SMALLEST window each scale allows.
     *
     *  That window size is the point of the shot, not decoration. A scaled shell looks fine in
     *  a big window at any scale; what can break is the smallest box the scale still permits,
     *  because that is where three fixed columns and a floor-bound canvas have the least room
     *  to disagree. So each shot is rendered at exactly `App::minPhysical*` for its scale —
     *  the size GTK will refuse to go below (R-SCALE-3) — which makes these the frames that
     *  prove the enforcement number is actually big enough.
     *
     *  Fixture-free (no project, no images), so this runs in ctest on any machine.
     *
     *  Both screens, because they have different minimums and only the larger one is enforced:
     *  the launcher is the one whose blocks are anchored top and bottom, the editor is the one
     *  whose middle column is squeezed between two fixed ones. */
    void shotScales()
    {
        if (!wanted("scale-")) return;
        for (int pct : AppSettings::uiScales())
        {
            setEnv("XDG_CONFIG_HOME", (gOpt.outdir / ("config-scale-" + std::to_string(pct))).string());
            // Seed the scale the way the app does — through AppSettings, so the shot exercises
            // applySettings rather than a back door only the harness has (D-15).
            AppSettings s;
            s.uiScale = pct;
            Rig rig(1440, 900);
            rig.app.applySettings(s);
            rig.svc.applySettings(s);

            const int w = (int)std::ceil(rig.app.minPhysicalWidth());
            const int h = (int)std::ceil(rig.app.minPhysicalHeight());
            rig.app.setSize((double)w, (double)h);

            const std::string tag = "scale-" + std::to_string(pct);
            {
                Frame f(w, h);
                rig.app.showHome();
                rig.settle(f, 400.0);
                save(f, tag + "-home-min");
            }
            {
                Frame f(w, h);
                rig.app.showEditor();
                rig.settle(f, 1200.0);
                save(f, tag + "-editor-min");
            }
            // The settings modal in the SMALLEST window this scale allows, because the dialog
            // that sets the scale is the one place a scale must never make itself unreachable:
            // five rows of chips at 125% in a 730x583 window is the tightest the card ever
            // gets, and a Done button off the bottom edge would be a trap rather than a bug.
            // Clicked through the sidebar link, not called, for the reason the home-settings
            // shot documents — the path a user takes is the path worth proving.
            {
                // A FRESH rig, the way shotHomeRecents does it for the same shot: the one above
                // has already been to the editor and back, and driving a click into a screen
                // that has just cross-faded produced a frame byte-identical to the home shot —
                // a modal that silently did not open, which is exactly the wrong-but-not-blank
                // failure --check cannot see.
                Rig r2(w, h);
                r2.app.applySettings(s);
                r2.svc.applySettings(s);
                r2.app.setSize((double)w, (double)h);
                Frame f(w, h);
                r2.app.showHome();
                r2.settle(f, 400.0);
                const double sc = pct / 100.0;   // the link's logical rect, in physical px
                r2.click(f, 150.0 * sc, ((double)h / sc - 137.0 + 16.0 + 9.0) * sc);
                r2.settle(f, 500.0);
                save(f, tag + "-settings-min");
            }
            // And the same scale in a normal window, so a reviewer can see what the setting
            // is FOR rather than only how it behaves under pressure.
            {
                Frame f(1280, 800);
                rig.app.setSize(1280.0, 800.0);
                rig.settle(f, 400.0);
                save(f, tag + "-editor-1280x800");
            }
        }
    }

    /** R-G-1 / R-SCALE-2a: the scale change caught IN FLIGHT.
     *
     *  The rule's own compliance clause says a snap cannot be ruled out by reading code, only
     *  by comparing frames half a tween apart — so here are those frames. 100% -> 200% is the
     *  largest jump the setting offers, which makes it the pair where a snap would be most
     *  obvious and an ease most legible. `cosmo_ui_tests` asserts the same thing numerically;
     *  this is the half a human can check. */
    void shotScaleZoom()
    {
        if (!wanted("scale-zoom")) return;
        setEnv("XDG_CONFIG_HOME", (gOpt.outdir / "config-scale-zoom").string());
        // 1280x960, not the usual 1280x800: 200% needs 1168x932 of window (R-SCALE-3), and in a
        // shorter one the last frame would show the logical-size CLAMP cropping the shell rather
        // than the ease this pair exists to show. The clamp is correct and documented; it is
        // just not what is being demonstrated, and a shot that shows the wrong true thing is
        // read as a bug.
        const int w = 1280, h = 960;
        Rig rig(w, h);
        AppSettings s;
        rig.app.applySettings(s);          // 100%, applied without a tween
        rig.svc.applySettings(s);
        rig.app.setSize((double)w, (double)h);
        Frame f(w, h);
        rig.app.showEditor();
        rig.settle(f, 800.0);
        save(f, "scale-zoom-000-before");

        rig.app.setUiScale(200);
        // kScaleAnimMs is 260; sample early and late in the EASE, not at its ends. EaseOutCubic
        // moves most in its first third, so 60 ms and 140 ms are visibly different sizes.
        rig.step(f, 4);    // ~64 ms
        save(f, "scale-zoom-060-mid");
        rig.step(f, 5);    // ~144 ms
        save(f, "scale-zoom-140-mid");
        rig.settle(f, 400.0);
        save(f, "scale-zoom-999-after");
    }

    /** The open transition, caught in the middle, with no project behind it. Deterministic
     *  by construction: the phases are driven from the shot clock, and `beginOpenTransition`
     *  starts them at the nowMs of the frame it was called on.
     *
     *  Two frames, because the transition has two halves that look nothing alike: the intro
     *  (the wordmark flying from its big home position to the top-bar slot while the project
     *  card fades in at the centre) and the loading part (the star sky, the card with its
     *  cover, the status line and the progress bar). */
    void shotTransition(int w, int h, const std::vector<std::string> &images)
    {
        if (!wanted("loading-")) return;
        setEnv("XDG_CONFIG_HOME", (gOpt.outdir / "config-recents").string());
        Rig rig(w, h);
        Frame f(w, h);
        rig.app.showHome();
        rig.settle(f, 900.0);

        if (wanted("loading-intro"))
        {
            rig.app.beginOpenTransition("Tokyo Streets");
            // 64 ms into a 460 ms intro. Sampled EARLY on purpose: the ease is EaseOutCubic,
            // so the linear midpoint is already ~88% of the way there and the "mid-flight"
            // shot would show a wordmark that has all but landed.
            rig.step(f, 4);
            save(f, "loading-intro");
        }

        if (wanted("loading-progress"))
        {
            rig.app.beginOpenTransition("Tokyo Streets");
            if (!images.empty())
                if (const DecodedImage *cover = rig.coverFor(images.front()))
                    rig.app.setLoadingCover(cover->rgba.data(), cover->width, cover->height);
            rig.app.setLoadStatus("Loading  " + fs::path(images.empty() ? "DSCF5186.RAF" : images.front())
                                                    .filename().string());
            rig.app.setLoadProgress(5, 12);
            // Past the intro (460 ms) so the phase is Loading, then far enough for the bar's
            // 200 ms ease to have arrived at 5/12 — a bar caught mid-ease says nothing about
            // where the load is.
            rig.settle(f, kIntroMs + 400.0);
            save(f, "loading-progress");
        }
    }

    /** The reveal — the loading screen dissolving and the editor materialising in its
     *  place — over a REAL loaded project, so what comes up is a real editor and not an
     *  empty one.
     *
     *  The transition is re-run scripted rather than photographed during the actual load,
     *  and that is the whole determinism argument: during a real load the reveal starts on
     *  whichever frame the last RAW file happened to finish on, so "15 frames in" would mean
     *  a different picture on every machine. Driven from a settled app it starts at a known
     *  tick: the intro ends at +460 ms, the minimum-visible gate is already satisfied by
     *  then, so the reveal begins on the frame at +464 ms and nowhere else. */
    void shotReveal(Rig &rig, int w, int h)
    {
        if (!wanted("loading-reveal") && !wanted("loading-dissolve")) return;
        Frame f(w, h);
        rig.app.beginOpenTransition("Tokyo Streets");
        rig.app.finishOpenTransition();       // the load is already done; only the animation runs
        if (const auto *t = rig.svc.session().thumbForSlot(0))
            rig.app.setLoadingCover(t->rgba.data(), t->w, t->h);
        rig.app.setLoadProgress(12, 12);
        rig.settle(f, kIntroMs + 16.0);       // the frame the reveal starts on
        // The reveal is a cross-fade in two halves (App.cpp): the loading elements dissolve
        // over its first 45%, and the editor only begins to materialise at 40%. No single
        // frame shows both, and the frame between them is the darkest moment in the app —
        // which is exactly the sort of thing a shot renderer that took one arbitrary sample
        // would present as "the reveal". Take one of each instead.
        rig.step(f, 2);   // eased ~0.17: the card, the status line and the stars going out
        save(f, "loading-dissolve");
        rig.step(f, 7);   // eased ~0.62: the loading screen is gone, the editor is coming up
        save(f, "loading-reveal");
        rig.settle(f, kRevealMs);             // hand back to the editor, so the next shot is clean
    }

    /** R-VIEW-1: the photo mid-dissolve, which is the only way to SEE that an adjustment
     *  cross-fades instead of cutting. Two frames of one 160 ms LINEAR dissolve — about 30% and
     *  60% across — so the mix of the two renders is visible rather than taken on trust from a
     *  number, and so a curve that front-loads the change would show up as an early frame that
     *  has already arrived (which is how EaseOutCubic was caught here).
     *
     *  The wait is the fiddly part and it is deliberate: the new render lands on a worker, so
     *  the frame is waited for in REAL time (pumping, not drawing) and only then are frames
     *  drawn. Advancing the shot clock while waiting would run it straight past the dissolve
     *  and photograph the settled editor twice. `frameSeq` is the signal — `pump` bumps it when
     *  it takes a frame out of the engine. */
    void shotDissolve(Rig &rig, int w, int h, const char *ev, const char *contrast)
    {
        if (!wanted("editor-dissolve")) return;
        Frame f(w, h);
        rig.settleQuiet(f, 200.0, 300);   // a settled editor, so the only motion is the photo

        Command set;
        set.kind = Command::Kind::Set;
        // Big enough to see in a PNG, INSIDE the range the slider can reach (±5 EV): a value
        // beyond it crashes the render worker on a NaN that walks through ToneCurve's clamp
        // (D-36), and a harness must not depend on a defect it just found. The values differ per
        // call for a duller reason that cost a render to find: `set` to the value already in
        // force still submits, and a dissolve between two IDENTICAL renders is a photograph of
        // nothing.
        set.fields = {{"exposure", ev}, {"contrast", contrast}};
        const unsigned seq0 = rig.svc.model().frameSeq;
        if (!rig.svc.dispatch(set)) { std::printf("  (no edit target: skipping editor-dissolve)\n"); return; }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (rig.svc.model().frameSeq == seq0 && std::chrono::steady_clock::now() < deadline)
        {
            rig.svc.pump(rig.now);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (rig.svc.model().frameSeq == seq0) { std::printf("  (no render arrived: skipping editor-dissolve)\n"); return; }

        rig.step(f, 1);   // this frame TAKES the render and starts the dissolve at alpha 0
        rig.step(f, 2);   // ~48 ms of 160 ms: three tenths across
        save(f, "editor-dissolve-early");
        rig.step(f, 3);   // ~96 ms: six tenths across
        save(f, "editor-dissolve-late");
        rig.settleQuiet(f, 200.0, 300);
    }

    /** The editor with a real project open, at two window sizes. This is the shot the whole
     *  harness exists for: the assembled app, real photos on the stage and in the filmstrip,
     *  every panel filled from a real session. */
    int shotEditorProject(const std::vector<int> &sizes, const std::vector<std::string> &images)
    {
        if (images.empty())
        {
            std::printf("  (skipping the project shots: no --images given)\n");
            return 0;
        }
        if (!wanted("editor-project") && !wanted("loading-reveal") && !wanted("loading-dissolve")
            && !wanted("editor-dissolve"))
            return 0;
        setEnv("XDG_CONFIG_HOME", (gOpt.outdir / "config-project").string());
        const fs::path cmp = gOpt.outdir / "projects" / "Tokyo Streets (shots).cmp";
        fs::create_directories(cmp.parent_path());
        if (!writeCmp(cmp, images)) { std::printf("  could not write %s\n", cmp.string().c_str()); return 1; }

        const int w0 = sizes[0], h0 = sizes[1];
        Rig rig(w0, h0);
        {
            Frame f(w0, h0);
            rig.app.showHome();
            rig.settle(f, 300.0);
            if (!rig.openProject(f, cmp.string()))
            {
                std::printf("  project load did not finish: %s\n", cmp.string().c_str());
                return 1;
            }
            if (wanted("editor-project")) save(f, "editor-project");
        }
        shotDissolve(rig, w0, h0, "1.5", "70");
        // The same app, resized — the path a real window resize takes (R4), so the second
        // size proves the editor REFLOWS rather than that it can be built small.
        for (size_t i = 2; i + 1 < sizes.size(); i += 2)
        {
            const int w = sizes[i], h = sizes[i + 1];
            Frame f(w, h);
            rig.app.setSize((double)w, (double)h);
            rig.settleQuiet(f, 800.0, 2500);
            if (wanted("editor-project")) save(f, "editor-project");
            shotDissolve(rig, w, h, "-1.2", "-50");   // dragging back the other way
        }
        // Back to the first size for the reveal shot, so it is comparable with the others.
        rig.app.setSize((double)w0, (double)h0);
        {
            Frame f(w0, h0);
            // Snap BEFORE these frames, never after: App keeps its own `mNowMs` from the
            // last frame it drew, and beginOpenTransition starts every phase from THAT. Jump
            // the clock without letting App see the jump and the transition is already over
            // on its first frame — the shot then quietly becomes a second copy of the editor,
            // which is precisely the kind of wrong-but-not-blank frame a colour count cannot
            // catch and a human comparing file sizes can.
            rig.snapClock();
            rig.settle(f, 400.0);
        }
        shotReveal(rig, w0, h0);
        return 0;
    }

    void usage()
    {
        std::printf(
            "cosmo_shots — headless PNG renders of the cosmo UI (P0.2, D-7)\n"
            "\n"
            "  --outdir DIR     where the PNGs go (default ./cosmo-shots)\n"
            "  --images A,B     real photos for the project shots; without them those are skipped\n"
            "  --only SUBSTR    render only shots whose name contains SUBSTR\n"
            "  --check          exit non-zero if any PNG came out uniform-colour\n"
            "  --list           list the shot names and exit\n"
            "\n"
            "XDG_CONFIG_HOME is redirected into --outdir: the launcher renders the recents\n"
            "file, so a shot must not be a picture of the developer's own machine.\n");
    }
}

int main(int argc, char **argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::string &a = args[i];
        auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string(); };
        if (a == "--outdir") gOpt.outdir = next();
        else if (a == "--only") gOpt.only = next();
        else if (a == "--check") gOpt.check = true;
        else if (a == "--list") gOpt.list = true;
        else if (a == "--images")
        {
            std::string v = next(), item;
            for (char c : v + ",")
            {
                if (c == ',') { if (!item.empty()) gOpt.images.push_back(item); item.clear(); }
                else item += c;
            }
        }
        else { usage(); return a == "--help" || a == "-h" ? 0 : 2; }
    }

    if (gOpt.list)
    {
        std::printf("home-empty  home-recents  home-settings  editor-empty\n"
                    "loading-intro  loading-progress  loading-dissolve  loading-reveal  editor-project\n"
                    "editor-dissolve-early  editor-dissolve-late\n"
                    "scale-75-*  scale-90-*  scale-100-*  scale-125-*   (-home-min, -editor-min,\n"
                    "                            -settings-min, -editor-1280x800)\n"
                    "scale-zoom-{000-before,060-mid,140-mid,999-after}\n");
        return 0;
    }

    std::error_code ec;
    fs::create_directories(gOpt.outdir, ec);
    gOpt.outdir = fs::absolute(gOpt.outdir);
    // Before ANY cosmo code runs: ProjectStore, AppSettings and the log all resolve their
    // paths through this, and a shot must neither read nor write the user's real config.
    setEnv("XDG_CONFIG_HOME", (gOpt.outdir / "config").string());
    registerBundledFonts();

    std::vector<std::string> images;
    for (const auto &p : gOpt.images)
    {
        if (fs::exists(p)) images.push_back(p);
        else std::printf("  warning: no such image, ignoring: %s\n", p.c_str());
    }
    std::printf("cosmo_shots -> %s%s\n", gOpt.outdir.string().c_str(),
                images.empty() ? "  (no --images: fixture-free shots only)" : "");

    // Two window sizes throughout: the 1600x1000 the app opens at, and a 1280x800 laptop —
    // the size where a panel that only fits on the big one shows itself.
    const std::vector<int> sizes = {1600, 1000, 1280, 800};

    shotHomeEmpty(sizes[0], sizes[1]);
    shotHomeRecents(sizes, images);
    shotEditorEmpty(sizes[0], sizes[1]);
    shotScales();   // R-SCALE: every offered scale, at the smallest window it permits
    shotScaleZoom();  // R-G-1: the scale change caught mid-tween
    shotTransition(sizes[0], sizes[1], images);
    shotTransition(sizes[2], sizes[3], images);
    const int rc = shotEditorProject(sizes, images);

    std::printf("\n%zu shot%s in %s\n", gWritten.size(), gWritten.size() == 1 ? "" : "s",
                gOpt.outdir.string().c_str());
    size_t blank = 0;
    for (const auto &w : gWritten)
        if (w.colours < 8) ++blank;
    if (blank)
        std::printf("%zu shot%s came out uniform-colour — a blank shot is worse than no shot\n",
                    blank, blank == 1 ? "" : "s");
    if (rc != 0) return rc;
    return (gOpt.check && (blank || gWritten.empty())) ? 1 : 0;
}
