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
 *  embedded faces are handed to the render adapter, the same call linux_main.cpp makes
 *  (R-FONT-1) — with the host's default sans instead, every text measurement is off and the
 *  shots would report layout bugs that do not exist.
 *
 *  Usage:
 *     cosmo_shots [--outdir DIR] [--images a.RAF,b.RAF] [--only SUBSTR] [--check] [--list]
 *
 *  Without --images the project shots are skipped and everything else still renders — the
 *  fixture-free subset is what `ctest -R cosmo_shots_headless` runs.
 */
#include "App.h"
#include "UiDump.h"              // findSegmentByType — R-VIEW-3's shot asks the tree for the canvas
#include "widgets/HomeScreen.h"
#include "widgets/PhotoCanvas.h"  // ...and the canvas where its seam is
#include "widgets/XformPanel.h"    // R-CROP: the shot asks the panel where its chips are
#include "widgets/MaskPanel.h"     // R-MASK-6: and where the Draw chip is
#include "widgets/AgentPanel.h"    // R-ASSIST-7: the Agent tab
#include "widgets/EditStackTabs.h"
#include "widgets/MenuStrip.h"     // R-FAMILY: the shared building blocks, drawn on their own
#include "widgets/ContextMenu.h"
#include "widgets/Toast.h"
#include "widgets/ConfirmDialog.h"
#include "widgets/HistoryView.h"
#include "widgets/Wordmark.h"
#include "widgets/DirtyDot.h"
#include "Theme.h"
#include "EmbeddedFonts.h"
#include "OmpPin.h"
#include "adapter/native/CairoTarget.h"
#include "core/AppSettings.h"
#include "core/ProjectStore.h"
#include "PinnedDecoder.h"
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
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{

    /** Tab `i`'s centre, in world x, from the tabs themselves (EditStackTabs::tabCenterX). */
    double tabCenter(const artboard::Segment *tabs, int i)
    {
        return tabs->worldTransform().e + static_cast<const arstro::cosmo_v2::EditStackTabs *>(tabs)->tabCenterX(i);
    }
    namespace fs = std::filesystem;
    using arstro::cosmo::AppSettings;
    using arstro::cosmo::Command;
    using arstro::cosmo::CosmoService;
    using arstro::cosmo::DecodedImage;
    using arstro::cosmo::Event;
    using arstro::cosmo_v2::PinnedDecoder;   // R-CPU-2c / D-41
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
        PinnedDecoder decoder;
        std::map<std::string, DecodedImage> covers;   // decoded first-images, keyed by path
        double now = 0.0;
        bool coverSent = false;
        bool revealed = false;

        Rig(int w, int h) : app(svc, (double)w, (double)h)
        {
            svc.setDecoderFactory(
                [] { return arstro::cosmo_v2::makePinnedDecoder(); });
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
        // The same call the app makes (R-FONT-1): the faces are in this binary, so a shot is in
        // the app's own type on any machine — which is the whole point of a shot that measures
        // text. Nothing to find on disk, nothing for Fontconfig to resolve differently here.
        arstro::cosmo_v2::registerEmbeddedFonts();
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
                {
                    const artboard::Rect lr = arstro::cosmo_v2::HomeScreen::settingsLinkRectFor((double)h);
                    rig.click(f, lr.x + lr.w * 0.5, lr.y + lr.h * 0.5);
                }
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

    /** R-UX: the states the 2026-11-17 audit found undrawn or lying - fixture-free, because
     *  every one of them is about NOT having a photo: a project whose files are gone (cells
     *  with a name and a mark, "File not found" on the stage, the presets rail's empty
     *  sentence, Export dimmed), the confirm that now stands in front of Delete - caught
     *  mid-fade and at rest - and the same at a small window. */
    void shotUxStates(int w, int h)
    {
        if (!wanted("editor-missing") && !wanted("editor-delete-confirm")) return;
        setEnv("XDG_CONFIG_HOME", (gOpt.outdir / "config-ux").string());
        const fs::path cmp = gOpt.outdir / "projects" / "A long project name, to prove the top bar fits it between the menus and Export.cmp";
        fs::create_directories(cmp.parent_path());
        if (!writeCmp(cmp, {"/offline/2026-09 Iceland/DSCF_0001.RAF", "/offline/2026-09 Iceland/DSCF_0002.RAF"})) return;
        Rig rig(w, h);
        Frame f(w, h);
        rig.app.showHome();
        rig.settle(f, 300.0);
        if (!rig.openProject(f, cmp.string(), 30)) { std::printf("  editor-missing: the project did not open\n"); return; }
        std::string err;
        for (const auto &nd : rig.svc.model().nodes)
            if (nd.failed) { rig.svc.dispatchText("select " + std::to_string(nd.node), err); break; }
        rig.app.syncFromSession();
        rig.step(f, 3);
        save(f, "editor-missing-mid");          // the notice mid fade-in
        rig.settle(f, 600.0);
        save(f, "editor-missing");
        rig.app.requestDeleteSelected();
        rig.step(f, 3);
        save(f, "editor-delete-confirm-mid");   // the confirm mid fade-in
        rig.settle(f, 500.0);
        save(f, "editor-delete-confirm");
    }

    /** R-FAMILY-8…14: Cosmo with the family standard adopted. Fixture-free: the photos are two
     *  gradients this harness paints and writes as PNG into --outdir, so the project is real
     *  (it decodes, it can be edited, it can be saved) and nothing outside --outdir is touched.
     *  Every eased change is caught mid-tween as well as at rest. */
    void shotFamilyAdoption(int w, int h)
    {
        if (!wanted("fam-")) return;
        setEnv("XDG_CONFIG_HOME", (gOpt.outdir / "config-fam").string());
        const fs::path dir = gOpt.outdir / "fam-project";
        fs::create_directories(dir);
        std::vector<std::string> photos;
        for (int i = 0; i < 3; ++i)
        {
            Frame g(480, 320);
            cairo_t *cr = g.cr();
            cairo_pattern_t *pat = cairo_pattern_create_linear(0, 0, 480, 320);
            cairo_pattern_add_color_stop_rgb(pat, 0.0, 0.10 + 0.25 * i, 0.22, 0.42 - 0.1 * i);
            cairo_pattern_add_color_stop_rgb(pat, 1.0, 0.92, 0.62 + 0.1 * i, 0.30);
            cairo_set_source(cr, pat);
            cairo_paint(cr);
            cairo_pattern_destroy(pat);
            cairo_set_source_rgb(cr, 0.96, 0.93, 0.85);
            cairo_arc(cr, 150 + 90 * i, 120, 46, 0, 6.2832);
            cairo_fill(cr);
            const fs::path png = dir / ("dune_0" + std::to_string(i + 1) + ".png");
            if (!g.write(png.string())) return;
            photos.push_back(png.string());
        }
        const fs::path cmp = dir / "Sahara dunes.cmp";
        if (!writeCmp(cmp, photos)) return;

        // ── Home, empty: one sentence, one hint, one button (and its hover) ──
        // Its own config dir: the project below is remembered as a recent the moment it opens.
        {
            setEnv("XDG_CONFIG_HOME", (gOpt.outdir / "config-fam-empty").string());
            Rig rig(w, h);
            Frame f(w, h);
            rig.app.showHome();
            rig.settle(f, 900.0);
            save(f, "fam-home-empty");
            const artboard::Rect b = rig.app.home()->emptyButtonRect();
            rig.app.pointer(1, b.x + b.w * 0.5, b.y + b.h * 0.5, 0, rig.now);
            rig.step(f, 3);
            save(f, "fam-home-empty-hover-mid");
            rig.settle(f, 300.0);
            save(f, "fam-home-empty-hover");
        }
        setEnv("XDG_CONFIG_HOME", (gOpt.outdir / "config-fam").string());

        Rig rig(w, h);
        Frame f(w, h);
        rig.app.showHome();
        rig.settle(f, 300.0);
        if (!rig.openProject(f, cmp.string(), 60)) { std::printf("  fam-: the project did not open\n"); return; }
        std::string err;
        rig.svc.dispatchText("select " + std::to_string(rig.svc.model().nodes.front().node), err);
        rig.app.syncFromSession();
        rig.settleQuiet(f, 400.0, 1500);
        save(f, "fam-topbar-clean");                 // name left after the menus, no dot, save icon dim
        std::printf("  window title: \"%s\"\n", rig.app.windowTitle().c_str());

        rig.svc.dispatchText("set exposure=0.6", err);
        rig.step(f, 4);
        save(f, "fam-topbar-dirty-mid");             // the dot and the icon half lit
        rig.settleQuiet(f, 400.0, 1500);
        save(f, "fam-topbar-dirty");
        std::printf("  window title: \"%s\"\n", rig.app.windowTitle().c_str());

        auto *bar = rig.app.topBar();
        auto &ms = *bar->menuStrip();
        const artboard::Transform mt = ms.worldTransform();
        auto openMenu = [&](int m) {
            const artboard::Rect r = ms.titleRect(m);
            rig.click(f, mt.e + r.x + r.w * 0.5, mt.f + r.y + r.h * 0.5);
        };
        const char *names[] = {"file", "edit", "develop", "preset", "view"};
        for (int m = 0; m < ms.menuCount() && m < 5; ++m)
        {
            openMenu(m);
            if (m == 0) { rig.step(f, 2); save(f, "fam-menu-file-mid"); }   // the dropdown growing
            rig.settle(f, 400.0);
            save(f, std::string("fam-menu-") + names[m]);
            if (m == 0)
            {
                artboard::KeyEvent k; k.type = artboard::KeyEvent::Type::Down;
                k.keyCode = 40; rig.app.key(k); rig.app.key(k); rig.app.key(k);   // Down x3: over the separator
                rig.step(f, 3);
                save(f, "fam-menu-file-keyboard-mid");
                rig.settle(f, 300.0);
                save(f, "fam-menu-file-keyboard");
            }
            artboard::KeyEvent esc; esc.type = artboard::KeyEvent::Type::Down; esc.keyCode = 27;
            rig.app.key(esc);
            rig.settle(f, 300.0);
        }

        // ── the unsaved dialog, mid fade and at rest ──
        rig.app.requestCloseProject();
        rig.step(f, 3);
        save(f, "fam-unsaved-dialog-mid");
        rig.settle(f, 500.0);
        save(f, "fam-unsaved-dialog");
        {
            const artboard::Rect b = rig.app.confirmDialog()->buttonRect(1);
            rig.app.pointer(1, b.x + b.w * 0.5, b.y + b.h * 0.5, 0, rig.now);
            rig.settle(f, 300.0);
            save(f, "fam-unsaved-dialog-hover-dont-save");
        }
        artboard::KeyEvent esc; esc.type = artboard::KeyEvent::Type::Down; esc.keyCode = 27;
        rig.app.key(esc);
        rig.settle(f, 400.0);
        rig.app.requestDeleteSelected();
        rig.settle(f, 500.0);
        save(f, "fam-remove-confirm");               // the single FILLED Remove
        rig.app.key(esc);
        rig.settle(f, 400.0);

        // ── the toast: a refusal, an export's result, Saved ──
        rig.svc.dispatchText("history jump 99", err);
        rig.step(f, 3);
        save(f, "fam-toast-error-in-mid");
        rig.settle(f, 500.0);
        save(f, "fam-toast-error");
        rig.app.exportFinished(3, 0, "");
        rig.step(f, 6);
        save(f, "fam-toast-swap-mid");
        rig.settle(f, 500.0);
        save(f, "fam-toast-exported");
        {
            const artboard::Rect sr = bar->saveRect();
            const artboard::Transform bt = bar->worldTransform();
            rig.click(f, bt.e + sr.x + sr.w * 0.5, bt.f + sr.y + sr.h * 0.5);   // the lit save icon
        }
        rig.step(f, 4);
        save(f, "fam-toast-saved-mid");              // Saved arriving, the dot leaving
        rig.settle(f, 600.0);
        save(f, "fam-toast-saved");                  // clean again: no dot, the icon dim
        std::printf("  window title: \"%s\"\n", rig.app.windowTitle().c_str());
        rig.settle(f, 3200.0 - 600.0 + 150.0);
        save(f, "fam-toast-out-mid");

        // ── View > Show Agent ──
        rig.app.setAgentShown(true);
        rig.settle(f, 500.0);
        openMenu(4);
        rig.settle(f, 400.0);
        save(f, "fam-view-hide-agent");
    }

    /** R-ASSIST-7: the Agent tab in each of its states, fixture-free (a fake agent behind the
     *  service, no project, no network): empty, a message arriving (mid fade-in), working, and a
     *  finished turn with every kind of line — the user's, the agent's (wrapped), tool and info
     *  lines, an error — long enough to overflow, so the scroll indicator shows. */
    void shotAgent(int w, int h)
    {
        if (!wanted("agent-")) return;
        using namespace arstro::cosmo;
        using arstro::cosmo::AgentChoice;
        setEnv("XDG_CONFIG_HOME", (gOpt.outdir / "config-agent").string());
        struct Shared { std::vector<AssistOutput> queue; };
        auto sh = std::make_shared<Shared>();
        struct Fake : IAssistant
        {
            std::shared_ptr<Shared> s;
            explicit Fake(std::shared_ptr<Shared> x) : s(std::move(x)) {}
            bool start(const AssistLaunch &, std::string &) override { return true; }
            bool send(const std::string &, std::string &) override { return true; }
            void stop() override {}
            void poll(std::vector<AssistOutput> &out) override { out.swap(s->queue); s->queue.clear(); }
            std::string name() const override { return "Claude Code"; }
        };
        Rig rig(w, h);
        rig.svc.setAssistantFactory([sh] { return std::unique_ptr<IAssistant>(new Fake(sh)); });
        rig.svc.setAgentDetector([] {
            return std::vector<AgentChoice>{
                {"claude", "Claude Code", "claude", true, "installed"},
                {"gemini", "Gemini CLI", "acp:gemini --experimental-acp", false, "not found - install: npm i -g @google/gemini-cli"},
                {"opencode", "OpenCode", "acp:opencode acp", true, "installed"},
                {"goose", "Goose", "acp:goose acp", false, "not found - install: block.github.io/goose"},
                {"ollama:qwen2.5:7b", "Ollama - qwen2.5:7b", "local:http://127.0.0.1:11434/v1|qwen2.5:7b", true,
                 "local model, cosmo's own agent loop"},
                {"custom", "Custom agent", "acp:", true, "acp:<command> any ACP agent - local:<url>|<model> - cmd:<command>"}};
        });
        Frame f(w, h);
        rig.app.showEditor();
        rig.settle(f, 600.0);
        auto *tabs = const_cast<arstro::cosmo_v2::EditStackTabs *>(static_cast<const arstro::cosmo_v2::EditStackTabs *>(
            arstro::cosmo_v2::findSegmentByType(*rig.app.uiRoot("editor"), "EditStackTabs")));
        if (!tabs) return;
        tabs->setSelectedIndex(tabs->pageCount() - 1);
        rig.settle(f, 600.0);
        save(f, "agent-choices");   // R-ASSIST-11: no agent yet - the machine's agents to choose from

        std::string err;
        rig.svc.dispatchText("assist send make these cinematic, keep her skin true", err);
        sh->queue = {{AssistOutput::Kind::Ready, "Claude Code 2.1.316 - claude-opus-5-5"}};
        rig.step(f, 6);
        save(f, "agent-connecting");   // mid cross-fade, choices into the chat
        rig.settle(f, 600.0);
        save(f, "agent-working");

        sh->queue = {
            {AssistOutput::Kind::Tool, "Skill: arstro.color.grading"},
            {AssistOutput::Kind::Tool, "Bash: python3 .claude/skills/arstro.color.grading/scripts/edit.py review dichoi.cmp --out dichoi-graded"},
            {AssistOutput::Kind::Tool, "cosmo_preview: 2"},
            {AssistOutput::Kind::Text, "I looked at all four photos. Two are portraits of the same woman in a cream hat, one is a lake under pine hills, one a green valley.\nPlan: Natural Teal on the landscapes, the teal kept off her skin, and the orange cushions behind her calmed."},
            {AssistOutput::Kind::Tool, "Bash: python3 .claude/skills/arstro.color.grading/scripts/edit.py run dichoi.cmp --plan plan.json"},
            {AssistOutput::Kind::Failed, "edit.py: the valley's white balance walked the wrong way - kept the camera's"},
            {AssistOutput::Kind::Text, "Done. Every edit is in your history, one step per photo - undo any you do not like."},
            {AssistOutput::Kind::Done, "done in 94 s - $1.120"}};
        rig.step(f, 2);
        save(f, "agent-message-arriving");   // mid fade-in, the list mid-travel
        rig.settle(f, 800.0);
        save(f, "agent-conversation");

        // R6: more than the box holds - the list rests on the newest line and shows the indicator.
        for (int i = 0; i < 16; ++i)
            sh->queue.push_back({AssistOutput::Kind::Tool, "cosmo_run: select " + std::to_string(i % 4 + 1) + ";set exposure=0.1"});
        sh->queue.push_back({AssistOutput::Kind::Text, "All four photos are graded; the last line is always in view."});
        rig.settle(f, 800.0);
        save(f, "agent-overflow");

        // R-ASSIST-12: a reply in the Markdown every agent writes, drawn as what it means - first
        // mid-stream (an unclosed ** already bold, an unclosed fence already code), then whole.
        const std::string reply =
            "## Graded 4 photos \xE2\x80\x94 *Natural Teal*\n"
            "**What I did**\n"
            "- **Lake** (photo 1): white balance from the clouds, `temp=5600`, teal in the shadows\n"
            "- **Portraits** (2, 3): skin kept on the skin line \xE2\x86\x92 warm *Natural Cinema*\n"
            "  - the orange cushions calmed (`hue_orange=-8`)\n"
            "- [x] project saved\n\n"
            "| Photo | Exposure | Look |\n|---|--:|---|\n| Lake | +0.30 | Natural Teal |\n| Valley | \xE2\x88\x92" "0.15 | Natural Teal |\n\n"
            "> Undo any photo you do not like \xE2\x80\x94 each is one step.\n\n"
            "```sh\nedit.py run dichoi.cmp --plan plan.json\n```\n"
            "\xE2\x9C\x85 Exported to `~/photo/dichoi-graded/export` \xF0\x9F\x8E\xAC";
        rig.svc.dispatchText("assist send make the valley warmer and tell me what you changed", err);
        sh->queue = {{AssistOutput::Kind::TextDelta, reply.substr(0, reply.find("Portraits") + 4)}};
        rig.settle(f, 400.0);
        save(f, "agent-markdown-streaming");
        sh->queue = {{AssistOutput::Kind::TextDelta, reply.substr(reply.find("Portraits") + 4)},
                     {AssistOutput::Kind::Done, "done in 41 s - $0.380"}};
        rig.settle(f, 800.0);
        save(f, "agent-markdown");
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
                {
                    // Asked of the widget (R-UX-5 moved the link; a typed offset clicked nothing).
                    const artboard::Rect lr = arstro::cosmo_v2::HomeScreen::settingsLinkRectFor((double)h / sc);
                    r2.click(f, (lr.x + lr.w * 0.5) * sc, (lr.y + lr.h * 0.5) * sc);
                }
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

    /** R-VIEW-3: the split seam, dragged off centre. The whole point of the feature is that
     *  the seam is somewhere the photographer put it, so a shot at 50% would prove nothing —
     *  this drags it to about a third across and photographs that, plus the hover state, which
     *  is the affordance that tells anyone the seam can be grabbed at all. */
    void shotSplitSeam(Rig &rig, int w, int h)
    {
        if (!wanted("editor-split-seam")) return;
        Frame f(w, h);
        rig.settleQuiet(f, 200.0, 300);

        auto *canvas = const_cast<arstro::cosmo_v2::PhotoCanvas *>(
            static_cast<const arstro::cosmo_v2::PhotoCanvas *>(
                arstro::cosmo_v2::findSegmentByType(*rig.app.uiRoot("editor"), "PhotoCanvas")));
        if (!canvas) { std::printf("  (no photo canvas: skipping editor-split-seam)\n"); return; }
        const artboard::Segment *pill =
            arstro::cosmo_v2::findSegmentByType(*canvas, "SegmentedControl");
        if (!pill) { std::printf("  (no compare pill: skipping editor-split-seam)\n"); return; }

        // Split mode on, through the pill, so the shot shows the state a user can reach.
        const artboard::Transform pw = pill->worldTransform();
        rig.app.pointer(0, pw.e + pill->width.value() * 0.5, pw.f + pill->height.value() * 0.5, 1, rig.now);
        rig.app.pointer(2, pw.e + pill->width.value() * 0.5, pw.f + pill->height.value() * 0.5, 1, rig.now);
        rig.settleQuiet(f, 300.0, 300);

        const artboard::Transform cw = canvas->worldTransform();
        const double CW = canvas->width.value(), CH = canvas->height.value();
        const double y = cw.f + CH * 0.5;
        const double from = cw.e + canvas->seamX(), to = cw.e + CW * 0.34;
        rig.app.pointer(0, from, y, 1, rig.now);
        for (int i = 1; i <= 6; ++i)
            rig.app.pointer(1, from + (to - from) * i / 6.0, y, 1, rig.now);
        // Photographed WITH THE BUTTON STILL DOWN and the pointer on the seam, so the hover
        // brighten and the thickened line are both in the frame — at rest they would have
        // faded out and the shot would not show the affordance at all.
        rig.step(f, 6);
        save(f, "editor-split-seam");
        rig.app.pointer(2, to, y, 1, rig.now);
        rig.settleQuiet(f, 300.0, 300);
    }

    /** R-CROP: the Xform tab, with a ratio locked and the Custom row open, plus the crop box
     *  on the photo. Two shots because the panel and the overlay are two halves of one
     *  feature and a reader has to be able to see both. */
    void shotCrop(Rig &rig, int w, int h)
    {
        if (!wanted("editor-crop")) return;
        Frame f(w, h);
        rig.settleQuiet(f, 200.0, 300);

        const artboard::Segment *tabs =
            arstro::cosmo_v2::findSegmentByType(*rig.app.uiRoot("editor"), "EditStackTabs");
        if (!tabs) { std::printf("  (no edit-stack tabs: skipping editor-crop)\n"); return; }
        // Xform is the last of five tabs.
        const artboard::Transform tw = tabs->worldTransform();
        const double tx = tabCenter(tabs, 4), ty = tw.f + 13.0;
        rig.app.pointer(0, tx, ty, 1, rig.now);
        rig.app.pointer(2, tx, ty, 1, rig.now);
        rig.settleQuiet(f, 400.0, 300);
        save(f, "editor-crop-free");

        // Pick 16:9 (chip index 3), so the panel shows a locked ratio and the photo shows the
        // box that ratio produced — against the PHOTO's shape, which is the R-CROP-1 fix.
        auto *xf = const_cast<arstro::cosmo_v2::XformPanel *>(
            static_cast<const arstro::cosmo_v2::XformPanel *>(
                arstro::cosmo_v2::findSegmentByType(*rig.app.uiRoot("editor"), "XformPanel")));
        if (xf)
        {
            const artboard::Transform xw = xf->worldTransform();
            // The chips are laid out left to right on the aspect row; ask the panel where the
            // one we want is rather than restating its arithmetic here.
            const artboard::Rect chip = xf->aspectChipRect(3);
            rig.app.pointer(0, xw.e + chip.x + chip.w * 0.5, xw.f + chip.y + chip.h * 0.5, 1, rig.now);
            rig.app.pointer(2, xw.e + chip.x + chip.w * 0.5, xw.f + chip.y + chip.h * 0.5, 1, rig.now);
            rig.settleQuiet(f, 400.0, 400);
            save(f, "editor-crop-16x9");

            // ...and Custom, so the two typed fields are in a frame.
            const artboard::Rect cc = xf->aspectChipRect(arstro::cosmo_v2::XformPanel::kAspectCustom);
            rig.app.pointer(0, xw.e + cc.x + cc.w * 0.5, xw.f + cc.y + cc.h * 0.5, 1, rig.now);
            rig.app.pointer(2, xw.e + cc.x + cc.w * 0.5, xw.f + cc.y + cc.h * 0.5, 1, rig.now);
            rig.settleQuiet(f, 400.0, 400);
            save(f, "editor-crop-custom");
        }
    }

    /** R-CROP-7: the zoom out of the crop, photographed MID-FLIGHT. A shot at either end shows
     *  nothing about the transition — the whole point is that the intermediate framings exist,
     *  so this catches two of them. */
    void shotCropZoom(Rig &rig, int w, int h)
    {
        if (!wanted("editor-crop-zoom")) return;
        Frame f(w, h);
        rig.settleQuiet(f, 200.0, 300);

        const artboard::Segment *tabs =
            arstro::cosmo_v2::findSegmentByType(*rig.app.uiRoot("editor"), "EditStackTabs");
        if (!tabs) { std::printf("  (no tabs: skipping editor-crop-zoom)\n"); return; }
        const artboard::Transform tw = tabs->worldTransform();
        auto clickTab = [&](int i) {
            rig.app.pointer(0, tabCenter(tabs, i), tw.f + 13.0, 1, rig.now);
            rig.app.pointer(2, tabCenter(tabs, i), tw.f + 13.0, 1, rig.now);
        };

        // Xform, and a crop worth zooming between.
        clickTab(4);
        rig.settleQuiet(f, 400.0, 300);
        Command set;
        set.kind = Command::Kind::Set;
        set.fields = {{"crop", "0.22,0.20,0.56,0.58"}};
        if (!rig.svc.dispatch(set)) { std::printf("  (no edit target: skipping)\n"); return; }
        rig.settleQuiet(f, 500.0, 600);
        save(f, "editor-crop-zoom-000-open");

        // Leave the tab and catch the zoom IN toward the crop, two frames apart. The renders
        // land on a worker, so each step waits for a frame in real time the way shotDissolve
        // does — advancing the shot clock without waiting would run straight past the motion.
        clickTab(0);
        for (int step = 0; step < 2; ++step)
        {
            const unsigned seq0 = rig.svc.model().frameSeq;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (rig.svc.model().frameSeq == seq0 && std::chrono::steady_clock::now() < deadline)
            {
                rig.svc.pump(rig.now);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            rig.step(f, 2);
            save(f, step == 0 ? "editor-crop-zoom-060-mid" : "editor-crop-zoom-140-mid");
        }
        rig.settleQuiet(f, 700.0, 900);
        save(f, "editor-crop-zoom-999-closed");
    }


    /** R-INFO: the image-information panel, opened the way a user opens it — a right-click on
     *  the photo and a click on the item. The rows come from the REAL file the shot was given,
     *  so this is also the only place the Exif reader's output is ever looked at by a human.
     */
    void shotImageInfo(Rig &rig, int w, int h)
    {
        if (!wanted("editor-image-info")) return;
        Frame f(w, h);
        rig.settleQuiet(f, 200.0, 300);

        auto *canvas = arstro::cosmo_v2::findSegmentByType(*rig.app.uiRoot("editor"), "PhotoCanvas");
        if (!canvas) { std::printf("  (no photo canvas: skipping editor-image-info)\n"); return; }
        const artboard::Transform cw = canvas->worldTransform();
        const double cx = cw.e + canvas->width.value() * 0.5;
        const double cy = cw.f + canvas->height.value() * 0.4;
        rig.app.pointer(0, cx, cy, 2, rig.now);
        rig.app.pointer(2, cx, cy, 2, rig.now);
        rig.settleQuiet(f, 300.0, 300);
        save(f, "editor-context-menu");

        // Item 3 of the photo menu: Add Photo / Group / Ungroup / Image Information. Clicked
        // rather than called, so the shot proves the ROUTE and not just the dialog.
        rig.click(f, cx + 20.0, cy + 4.0 + 3 * 24.0 + 12.0);
        rig.settleQuiet(f, 400.0, 400);
        if (!rig.app.infoDialog()->isOpen())
        {
            std::printf("  (the info panel did not open: skipping editor-image-info)\n");
            return;
        }
        save(f, "editor-image-info");

        // Mid-scroll, so the shot shows the thumb and proves the list moves over frames
        // rather than jumping (R-G-1).
        rig.app.wheel(cx, cy, -90.0, false);
        rig.step(f, 4);
        save(f, "editor-image-info-scrolled");

        artboard::KeyEvent esc;
        esc.type = artboard::KeyEvent::Type::Down;
        esc.keyCode = 0x1B;
        rig.app.key(esc);
        rig.settleQuiet(f, 300.0, 300);
    }


    /** R-MASK-6: a hand-drawn mask, drawn the way a user draws one — the Draw chip, then clicks
     *  on the photo, then an Alt-drag to bow one segment. Two frames: the outline while it is
     *  still being placed, and the finished shape with its handles out. */
    void shotMaskDraw(Rig &rig, int w, int h)
    {
        if (!wanted("editor-mask-draw")) return;
        Frame f(w, h);
        rig.settleQuiet(f, 200.0, 300);

        const artboard::Segment *tabs =
            arstro::cosmo_v2::findSegmentByType(*rig.app.uiRoot("editor"), "EditStackTabs");
        if (!tabs) { std::printf("  (no edit-stack tabs: skipping editor-mask-draw)\n"); return; }
        const artboard::Transform tw = tabs->worldTransform();
        rig.app.pointer(0, tabCenter(tabs, 1), tw.f + 13.0, 1, rig.now);   // Mask is the second tab
        rig.app.pointer(2, tabCenter(tabs, 1), tw.f + 13.0, 1, rig.now);
        rig.settleQuiet(f, 400.0, 300);

        auto *panel = static_cast<const arstro::cosmo_v2::MaskPanel *>(
            arstro::cosmo_v2::findSegmentByType(*rig.app.uiRoot("editor"), "MaskPanel"));
        if (!panel) { std::printf("  (no mask panel: skipping editor-mask-draw)\n"); return; }
        const artboard::Transform mw = panel->worldTransform();
        const artboard::Rect chip = panel->addChipRect(3);   // Draw
        rig.click(f, mw.e + chip.x + chip.w * 0.5, mw.f + chip.y + chip.h * 0.5);
        rig.settleQuiet(f, 400.0, 300);

        auto *photo = arstro::cosmo_v2::findSegmentByType(*rig.app.uiRoot("editor"), "PhotoCanvas");
        if (!photo) { std::printf("  (no photo canvas: skipping editor-mask-draw)\n"); return; }
        const artboard::Transform pw = photo->worldTransform();
        const double PW = photo->width.value(), PH = photo->height.value();
        auto at = [&](double fx, double fy) {
            return artboard::Point{pw.e + PW * fx, pw.f + PH * fy};
        };

        // Two points down: the half-drawn state, which is a normal thing to be looking at and
        // is drawn dimmer because it renders nothing yet.
        rig.click(f, at(0.30, 0.28).x, at(0.30, 0.28).y);
        rig.click(f, at(0.62, 0.22).x, at(0.62, 0.22).y);
        rig.settleQuiet(f, 250.0, 250);
        save(f, "editor-mask-draw-placing");

        // Kept inside the photo rather than out in the letterbox: a point MAY sit off-frame
        // (R-MASK-5) but a shot is not the place to demonstrate that.
        rig.click(f, at(0.70, 0.52).x, at(0.70, 0.52).y);
        rig.click(f, at(0.34, 0.58).x, at(0.34, 0.58).y);
        rig.settleQuiet(f, 300.0, 300);

        // Alt-drag the second point's handles, so the finished shot shows a bowed segment and
        // the tangent handles that produced it.
        const artboard::Point p = at(0.62, 0.22);
        rig.app.pointer(0, p.x, p.y, 1, rig.now, /*alt=*/true);
        for (int i = 1; i <= 6; ++i)
            rig.app.pointer(1, p.x + 70.0 * i / 6.0, p.y - 26.0 * i / 6.0, 1, rig.now, true);
        rig.app.pointer(2, p.x + 70.0, p.y - 26.0, 1, rig.now, true);
        rig.settleQuiet(f, 400.0, 400);
        save(f, "editor-mask-draw");
    }

    int shotEditorProject(const std::vector<int> &sizes, const std::vector<std::string> &images)
    {
        if (images.empty())
        {
            std::printf("  (skipping the project shots: no --images given)\n");
            return 0;
        }
        if (!wanted("editor-project") && !wanted("loading-reveal") && !wanted("loading-dissolve")
            && !wanted("editor-dissolve") && !wanted("editor-split-seam")
            && !wanted("editor-crop") && !wanted("editor-crop-zoom")
            && !wanted("editor-image-info") && !wanted("editor-context-menu")
            && !wanted("editor-mask-draw") && !wanted("editor-mask-detect"))
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
        shotSplitSeam(rig, w0, h0);          // R-VIEW-3
        shotCrop(rig, w0, h0);              // R-CROP
        shotCropZoom(rig, w0, h0);          // R-CROP-7
        shotImageInfo(rig, w0, h0);         // R-INFO
        shotMaskDraw(rig, w0, h0);          // R-MASK-6
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


    // ── R-FAMILY: the shared building blocks, each drawn on its own ───────────────────────
    //
    // These are widgets no app has adopted yet (step 1 of the family plan adds them opt-in), so
    // there is no App state that shows them. They are drawn here directly, through the same Cairo
    // adapter and the same embedded faces, so the shortcut column, the three toast kinds and the
    // dialog's Auto style can be LOOKED at before any app wires them in.
    namespace family
    {
        using namespace arstro::cosmo_v2;
        using artboard::Point;
        using artboard::Rect;

        struct OpenMenuStrip : MenuStrip { using MenuStrip::handleGesture; };
        struct OpenContextMenu : ContextMenu { using ContextMenu::handleGesture; };
        struct OpenConfirm : ConfirmDialog
        {
            OpenConfirm() : ConfirmDialog(palette::primary()) {}
            using ConfirmDialog::advance;
        };

        artboard::Gesture gesture(artboard::Gesture::Type t) { artboard::Gesture g; g.type = t; return g; }
        Point mid(const Rect &r) { return Point{r.x + r.w * 0.5, r.y + r.h * 0.5}; }

        /** The window's ground and a top bar band, so a block is seen where it will live. */
        void backdrop(artboard::CairoTarget &t, Frame &f, bool bar)
        {
            f.clear();
            t.setContext(f.cr());
            t.setTransform(artboard::Transform::identity());
            artboard::drawRoundedRect(t, Rect{0, 0, (double)f.width(), (double)f.height()}, 0.0, artboard::Paint::filled(palette::background()));
            if (!bar) return;
            artboard::drawRoundedRect(t, Rect{0, 0, (double)f.width(), space::barH()}, 0.0, artboard::Paint::filled(palette::leftRailBg()));
            artboard::drawRoundedRect(t, Rect{0, space::barH(), (double)f.width(), 1.0}, 0.0, artboard::Paint::filled(palette::border()));
        }

        void menus(artboard::CairoTarget &t)
        {
            if (!wanted("family-menu")) return;
            OpenMenuStrip ms;
            ms.x.set(space::padX()); ms.y.set((space::barH() - 21.0) * 0.5); ms.width.set(400.0);
            auto nop = [] {};
            ms.addMenu({"File", {{"New Project\xE2\x80\xA6", nop, "Ctrl+N"},
                                 {"Open Project\xE2\x80\xA6", nop, "Ctrl+O"},
                                 MenuStrip::Item::sep(),
                                 {"Save", nop, "Ctrl+S", false},
                                 {"Save As\xE2\x80\xA6", nop, "Ctrl+Shift+S"},
                                 {"Export\xE2\x80\xA6", nop, "Ctrl+E"},
                                 MenuStrip::Item::sep(),
                                 {"Settings\xE2\x80\xA6", nop, "Ctrl+,"},
                                 {"Close Project", nop, "Ctrl+W"}}});
            ms.addMenu({"Edit", {{"Undo", nop, "Ctrl+Z"}, {"Redo", nop, "Ctrl+Y", false}}});
            ms.addMenu({"View", {{"Show Agent", nop}, {"Show History Tree\xE2\x80\xA6", nop}}});   // a plain menu: as before

            auto draw = [&](Frame &f) {
                backdrop(t, f, true);
                ms.render(t);
                ms.renderOverlay(t);
            };
            double now = 0.0;
            ms.advance(now);
            ms.handleGesture(gesture(artboard::Gesture::Type::Click), mid(ms.titleRect(0)));
            // Mid-transition: the dropdown growing out of its title, 64 ms into 160.
            for (int i = 0; i < 4; ++i) ms.advance(now += kFrameMs);
            { Frame f(460, 300); draw(f); save(f, "family-menu-shortcuts-mid"); }
            for (int i = 0; i < 30; ++i) ms.advance(now += kFrameMs);
            { Frame f(460, 300); draw(f); }                                    // a paint measures the column
            artboard::Segment::setHovered(&ms);
            ms.handleGesture(gesture(artboard::Gesture::Type::Move), mid(ms.itemRect(0, 4)));
            for (int i = 0; i < 30; ++i) ms.advance(now += kFrameMs);
            { Frame f(460, 300); draw(f); save(f, "family-menu-shortcuts"); }
            // The pointer on the disabled row: no wash.
            ms.handleGesture(gesture(artboard::Gesture::Type::Move), mid(ms.itemRect(0, 3)));
            for (int i = 0; i < 30; ++i) ms.advance(now += kFrameMs);
            { Frame f(460, 300); draw(f); save(f, "family-menu-disabled-hover"); }
            // The keyboard: Down twice from the top lands past the separator and the disabled row.
            artboard::Segment::setHovered(nullptr);
            artboard::KeyEvent down; down.type = artboard::KeyEvent::Type::Down; down.keyCode = 40;
            ms.menuKey(down); ms.menuKey(down); ms.menuKey(down);
            for (int i = 0; i < 30; ++i) ms.advance(now += kFrameMs);
            { Frame f(460, 300); draw(f); save(f, "family-menu-keyboard"); }
            // A plain menu beside it: drawn exactly as it always was.
            ms.handleGesture(gesture(artboard::Gesture::Type::Click), mid(ms.titleRect(2)));
            for (int i = 0; i < 30; ++i) ms.advance(now += kFrameMs);
            { Frame f(460, 300); draw(f); save(f, "family-menu-plain"); }

            OpenContextMenu cm;
            cm.width.set(360.0); cm.height.set(220.0);
            cm.open({{"Copy Edits", nop, "Ctrl+C"}, {"Paste Edits", nop, "Ctrl+V", false}, ContextMenu::Item::sep(),
                     {"Rename Group\xE2\x80\xA6", nop, "F2"}, {"Delete", nop, "Del"}}, 40.0, 30.0);
            now = 0.0;
            cm.advance(now);
            for (int i = 0; i < 30; ++i) cm.advance(now += kFrameMs);
            { Frame f(360, 220); backdrop(t, f, false); cm.renderOverlay(t); }
            cm.handleGesture(gesture(artboard::Gesture::Type::Move), mid(cm.itemRect(3)));
            for (int i = 0; i < 30; ++i) cm.advance(now += kFrameMs);
            { Frame f(360, 220); backdrop(t, f, false); cm.renderOverlay(t); save(f, "family-context-menu"); }
        }

        void toasts(artboard::CairoTarget &t)
        {
            if (!wanted("family-toast")) return;
            const double top = space::barH() + 4.0;   // the caller's choice: under the top bar
            auto shot = [&](Toast &toast, int w, const char *name) {
                Frame f(w, 96);
                backdrop(t, f, true);
                toast.x.set(0.0); toast.y.set(top); toast.width.set((double)w);
                toast.renderOverlay(t);
                save(f, name);
            };
            struct K { Toast::Kind kind; const char *text; const char *name; };
            const K kinds[] = {{Toast::Kind::Info, "Exported 12 photos to ~/Pictures/Tokyo", "family-toast-info"},
                               {Toast::Kind::Warning, "2 files are missing \xE2\x80\x94 File \xE2\x80\xBA Relink\xE2\x80\xA6", "family-toast-warning"},
                               {Toast::Kind::Error, "Could not save: the disk is full", "family-toast-error"}};
            for (const K &k : kinds)
            {
                Toast toast;
                toast.show(k.kind, k.text, 0.0);
                for (double now = 0.0; now <= 400.0; now += kFrameMs) toast.advance(now);
                shot(toast, 640, k.name);
            }
            {   // mid-transition, in: half faded, half way down
                Toast toast;
                toast.show(Toast::Kind::Warning, kinds[1].text, 0.0);
                toast.advance(0.0); toast.advance(32.0);
                shot(toast, 640, "family-toast-in-mid");
            }
            {   // mid-transition, out
                Toast toast;
                toast.show(Toast::Kind::Error, kinds[2].text, 0.0);
                double now = 0.0;
                for (; now <= 3232.0; now += kFrameMs) toast.advance(now);
                toast.advance(now += 144.0);
                shot(toast, 640, "family-toast-out-mid");
            }
            {   // a second toast while one shows: the words cross-fade
                Toast toast;
                toast.show(Toast::Kind::Info, kinds[0].text, 0.0);
                double now = 0.0;
                for (; now <= 400.0; now += kFrameMs) toast.advance(now);
                toast.show(Toast::Kind::Error, kinds[2].text, now);
                toast.advance(now); toast.advance(now += 96.0);
                shot(toast, 640, "family-toast-swap-mid");
            }
            {   // a sentence longer than a small window: ellipsized, inside the band
                Toast toast;
                toast.show(Toast::Kind::Warning, "Rendered the whole song to a file with a very long name indeed, and then some more words", 0.0);
                for (double now = 0.0; now <= 400.0; now += kFrameMs) toast.advance(now);
                shot(toast, 360, "family-toast-long");
            }
        }

        void confirms(artboard::CairoTarget &t)
        {
            if (!wanted("family-confirm")) return;
            using Style = ConfirmDialog::DestructiveStyle;
            auto shot = [&](Style style, bool three, const char *name) {
                OpenConfirm d;
                d.width.set(640.0); d.height.set(300.0);
                d.setDestructiveStyle(style);
                d.advance(0.0);
                if (three)
                    d.show("Save changes to \xE2\x80\x9CTokyo Streets\xE2\x80\x9D?", "Your changes will be lost if you don\xE2\x80\x99t save them.",
                           {{"Cancel"}, {"Don't save", true}, {"Save", false, true}});
                else
                    d.show("Delete 3 photos?", "They are removed from the project. The files stay on disk.", {{"Cancel"}, {"Delete", true}});
                for (double now = 0.0; now <= 400.0; now += kFrameMs) d.advance(now);
                Frame f(640, 300);
                backdrop(t, f, true);
                d.renderOverlay(t);
                save(f, name);
            };
            shot(Style::Auto, true, "family-confirm-auto");               // outlined beside a primary
            shot(Style::Auto, false, "family-confirm-auto-single");       // filled when it is the only action
            shot(Style::Filled, true, "family-confirm-default");          // what every existing caller still gets
        }

        void history(artboard::CairoTarget &t)
        {
            if (!wanted("family-history")) return;
            HistoryView hv;
            hv.width.set(900.0); hv.height.set(560.0);
            hv.advance(0.0);
            hv.show({HistoryView::Node{-1, "Open"}, HistoryView::Node{0, "Exposure +0.30"}, HistoryView::Node{1, "Contrast +12"},
                     HistoryView::Node{1, "Temp 5600 K"}, HistoryView::Node{3, "Crop 16:9"}}, 4);
            for (double now = 0.0; now <= 600.0; now += kFrameMs) hv.advance(now);
            Frame f(900, 560);
            backdrop(t, f, true);
            hv.renderOverlay(t);
            save(f, "family-history");
        }

        void marks(artboard::CairoTarget &t)
        {
            if (!wanted("family-wordmark")) return;
            Frame f(520, 150);
            backdrop(t, f, false);
            const char *names[] = {"cosmo", "interstellar", "solaris"};
            const artboard::Color accents[] = {artboard::Color::hex(0x4F7EF7), artboard::Color::hex(0x159387), artboard::Color::hex(0xDD5A0C)};
            const artboard::Color keep = palette::primary();
            DirtyDot dirty;
            dirty.setImmediate(true);
            double y = 26.0;
            for (int i = 0; i < 3; ++i, y += 26.0)
            {
                palette::setAccent(accents[i]);
                const double w = Wordmark::draw(t, names[i], 16.0, y + 13.0 * 0.35, 13.0);
                // ...then the project name, and the unsaved dot after it, both placed by measurement.
                const double nameX = 16.0 + w + 16.0;
                t.setFill(palette::foreground());
                t.drawText("Tokyo Streets", nameX, y + 12.0 * 0.35, 12.0, font::sansMedium());
                dirty.draw(t, Point{nameX + t.measureText("Tokyo Streets", 12.0, font::sansMedium()) + DirtyDot::kGap, y});
            }
            palette::setAccent(artboard::Color::hex(0x4F7EF7));
            Wordmark::draw(t, "cosmo", 300.0, 96.0, 46.0);
            palette::setAccent(keep);
            save(f, "family-wordmark-dirtydot");
        }

        void all()
        {
            artboard::CairoTarget target;
            menus(target);
            toasts(target);
            confirms(target);
            history(target);
            marks(target);
        }
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
                    "scale-zoom-{000-before,060-mid,140-mid,999-after}\n"
                    "editor-split-seam  editor-crop-{free,16x9,custom}  editor-crop-zoom-*\n"
                    "editor-context-menu  editor-image-info  editor-image-info-scrolled\n"
                    "editor-mask-draw-placing  editor-mask-draw\n"
                    "editor-missing{,-mid}  editor-delete-confirm{,-mid}   (each at 1600x1000 and 1024x640)\n"
                    "family-menu-{shortcuts,shortcuts-mid,disabled-hover,keyboard,plain}  family-context-menu\n"
                    "family-toast-{info,warning,error,in-mid,out-mid,swap-mid,long}\n"
                    "family-confirm-{auto,auto-single,default}  family-history  family-wordmark-dirtydot\n");
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

    family::all();   // R-FAMILY: the shared building blocks, on their own
    shotHomeEmpty(sizes[0], sizes[1]);
    shotHomeRecents(sizes, images);
    shotEditorEmpty(sizes[0], sizes[1]);
    shotEditorEmpty(1024, 640);      // R-UX-7: the empty stage's notice, at a small window too
    shotUxStates(sizes[0], sizes[1]); // R-UX-3 / R-UX-7
    shotUxStates(1024, 640);
    shotFamilyAdoption(sizes[0], sizes[1]);   // R-FAMILY-8…14: Cosmo adopts the family standard
    shotFamilyAdoption(1024, 640);
    shotFamilyAdoption((int)App::minLogicalWidth(), (int)App::minLogicalHeight());   // the window minimum
    shotAgent(sizes[0], sizes[1]);   // R-ASSIST-7
    shotAgent(sizes[2], sizes[3]);
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
