/*
 *  cosmo_demo — records a silent MP4 of the Agent tab at work, with no display.
 *
 *  The real App, the real CosmoService, the real Cairo adapter and the user's real agent (run by
 *  ProcessAssistant over a control socket, exactly as `cosmo-cc serve` and the window do) — only the
 *  GTK window is missing. Each frame is drawn at 2x into an image surface, cropped/zoomed into a
 *  1920x1080 frame (so a zoom into the prompt stays sharp) and piped to ffmpeg.
 *
 *  The script, in the video's order:
 *    1. the project in the editor; the pointer opens the Agent tab; Connect the agent
 *    2. zoom into the composer; the prompt is typed and sent
 *    3. zoom out; the agent works in REAL time, recorded at `--speed` (a "4x" badge says so)
 *    4. before / after: for `--compare` photos, Split mode and the seam swept across
 *
 *  Scripted parts run on a virtual 30 fps clock; the agent's part runs on the wall clock, one frame
 *  kept every speed/30 s. The project is edited IN PLACE — pass a copy.
 *
 *  usage: cosmo_demo --project P.cmp --out demo.mp4 [--prompt TEXT] [--agent claude] [--speed 4]
 *                    [--compare 6] [--workdir DIR] [--max-minutes 60] [--dry]
 *         --dry: no agent - the prompt is sent to nobody and the agent part is skipped (layout check)
 */
#include "App.h"
#include "AssistProcess.h"
#include "CosmoAssistProfile.h"
#include "ControlChannel.h"
#include "EmbeddedFonts.h"
#include "ExportWriter.h"
#include "OmpPin.h"
#include "PinnedDecoder.h"
#include "Theme.h"
#include "UiDump.h"
#include "adapter/native/CairoTarget.h"
#include "core/AppSettings.h"
#include "core/service/AppModelCodec.h"
#include "core/service/CosmoService.h"
#include "widgets/AgentPanel.h"
#include "widgets/EditStackTabs.h"
#include "widgets/PhotoCanvas.h"
#include "widgets/RightColumn.h"
#include <cairo/cairo.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using arstro::cosmo::AppSettings;
using arstro::cosmo::Command;
using arstro::cosmo::CosmoService;
using arstro::cosmo::Event;
using arstro::cosmo_v2::App;

namespace
{
    constexpr int W = 1920, H = 1080, S = 2;
    constexpr double kFps = 30.0, kFrameMs = 1000.0 / kFps;

    struct Opts
    {
        std::string project, out = "cosmo-demo.mp4", agent = "claude", workdir;
        std::string prompt = "Make these Tokyo photos cinematic";
        double speed = 4.0, maxMinutes = 60.0;
        int compare = 6;
        bool dry = false;
    } gOpt;

    double wallMs()
    {
        using namespace std::chrono;
        return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
    }

    /** EaseInOutCubic between two values, started at t0 over dur ms. */
    struct Tween
    {
        double from = 0, to = 0, t0 = 0, dur = 0;
        double at(double t) const
        {
            if (dur <= 0 || t >= t0 + dur) return to;
            if (t <= t0) return from;
            double p = (t - t0) / dur;
            p = p < 0.5 ? 4 * p * p * p : 1 - std::pow(-2 * p + 2, 3) / 2;
            return from + (to - from) * p;
        }
        void go(double target, double now, double ms) { from = at(now); to = target; t0 = now; dur = ms; }
        void set(double v) { from = to = v; dur = 0; }
    };

    struct Demo
    {
        arstro::cosmo::ThreadBudget budget;
        CosmoService svc{budget};
        App app{svc, (double)W, (double)H};
        artboard::CairoTarget target;
        arstro::cosmo_v2::ControlChannel control;
        double now = 0.0;
        bool coverSent = false, revealed = false;

        cairo_surface_t *big = nullptr, *out = nullptr, *prev = nullptr;
        cairo_t *bcr = nullptr, *ocr = nullptr;
        FILE *ff = nullptr;
        long frames = 0;

        Tween zx, zy, zw;            // the zoom rect, logical; height = zw * H / W
        Tween cx, cy, cAlpha;        // the pointer
        double clickAt = -1e9;
        Tween badge, captions;       // "4x" badge, Before/After captions
        int xfade = 0;               // frames left of a cross-fade from `prev`
        static constexpr int kXfade = 12;

        Demo()
        {
            svc.setDecoderFactory([] { return arstro::cosmo_v2::makePinnedDecoder(); });
            svc.setWorkerInit([] { arstro::cosmo_v2::pinNestedOpenMPForThisThread(); });
            svc.setJpegWriter([](const std::string &path, const uint8_t *rgba, int w, int h, int quality, std::string &err) {
                App::ExportRequest req;
                req.quality = quality;
                req.embedExif = false;
                return arstro::cosmo_v2::exporter::write(req, rgba, w, h, path, std::string(), err);
            });
            svc.subscribe([this](const Event &e) {
                if (control.isOpen()) control.broadcast(arstro::cosmo::formatEvent(e));
                onEvent(e);
            });
            svc.setAssistantFactory([this]() -> std::unique_ptr<arstro::cosmo::IAssistant> {
                return std::make_unique<arstro::cosmo_v2::ProcessAssistant>(
                    arstro::cosmo_v2::cosmoAssistProfile(),
                    arstro::cosmo_v2::ProcessAssistant::configFor(arstro::cosmo_v2::cosmoAssistProfile(), control.path()));
            });
            svc.setAgentDetector([this] { return arstro::cosmo_v2::ProcessAssistant::detect(arstro::cosmo_v2::cosmoAssistProfile(), svc.model().settings.agent); });
            app.onCommand = [this](Command c) { svc.dispatch(c); };

            AppSettings s = AppSettings::load();
            s.agent = gOpt.agent;
            app.applySettings(s);
            svc.applySettings(s);

            big = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W * S, H * S);
            cairo_surface_set_device_scale(big, S, S);
            bcr = cairo_create(big);
            out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
            ocr = cairo_create(out);
            prev = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
            zx.set(0);
            zy.set(0);
            zw.set(W);
            cx.set(W * 0.55);
            cy.set(H * 0.55);
            cAlpha.set(0);
            badge.set(0);
            captions.set(0);
        }
        ~Demo()
        {
            cairo_destroy(bcr);
            cairo_destroy(ocr);
            cairo_surface_destroy(big);
            cairo_surface_destroy(out);
            cairo_surface_destroy(prev);
        }

        // ── the host's job: what linux_main does with the service's events ──
        void onEvent(const Event &e)
        {
            using K = Event::Kind;
            switch (e.kind)
            {
            case K::ProjectOpening:
                app.beginOpenTransition(e.text);
                app.resetWorkspace();
                app.setLoadProgress(0, e.b);
                app.setStreamProgress(0, e.b);
                coverSent = revealed = false;
                break;
            case K::EntryDecoded:
                app.registerThumb(e.b);
                if (!coverSent && !app.hasLoadingCover())
                {
                    if (const auto *t = svc.session().thumbForSlot(e.b)) app.setLoadingCover(t->rgba.data(), t->w, t->h);
                    coverSent = true;
                }
                if (!revealed)
                {
                    revealed = true;
                    app.selectImage(e.b);
                    app.setLoadUsable();
                }
                else
                    app.refreshLibrary();
                break;
            case K::EntryFailed: app.refreshLibrary(); break;
            case K::LoadProgress:
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
            case K::HistoryChanged: app.syncFromSession(); break;
            case K::Error:
            case K::CommandRejected: std::fprintf(stderr, "  service: %s\n", e.text.c_str()); break;
            case K::AssistMessage: std::fprintf(stderr, "  agent: %.160s\n", e.text.c_str()); break;
            case K::AssistState: std::fprintf(stderr, "  agent state: %s\n", e.text.c_str()); break;
            default: break;
            }
        }

        /** The control socket, as `cosmo-cc serve` answers it: the agent's tools come back here. */
        void pollControl()
        {
            control.poll([&](const std::string &line) {
                std::string perr;
                const Command c = arstro::cosmo::parseCommand(line, perr);
                if (!c.valid())
                {
                    if (!perr.empty()) control.broadcast("[evt] command.rejected line=" + line + " why=" + perr);
                    return;
                }
                if (c.kind == Command::Kind::StatePrint)
                {
                    arstro::cosmo::ModelDumpOptions o;
                    o.json = c.flag;
                    o.stable = c.field("stable") == "1";
                    o.params = c.field("params") == "1";
                    control.broadcast("[evt] state.begin");
                    control.broadcast(arstro::cosmo::formatModel(svc.model(), o));
                    control.broadcast("[evt] state.end");
                    return;
                }
                svc.dispatch(c);
            });
        }

        // ── drawing ──
        void drawPointer()
        {
            const double a = cAlpha.at(now);
            if (a <= 0.01) return;
            const double x = cx.at(now), y = cy.at(now);
            const double since = now - clickAt;
            if (since >= 0 && since < 420)
            {
                const double p = since / 420.0;
                cairo_new_path(bcr);
                cairo_arc(bcr, x, y, 6 + 18 * p, 0, 2 * M_PI);
                cairo_set_source_rgba(bcr, 79 / 255.0, 126 / 255.0, 247 / 255.0, 0.55 * (1 - p) * a);
                cairo_set_line_width(bcr, 2.0);
                cairo_stroke(bcr);
            }
            // A plain arrow, 19 px tall: white with a dark edge, like a system pointer.
            cairo_save(bcr);
            cairo_translate(bcr, x, y);
            cairo_new_path(bcr);
            cairo_move_to(bcr, 0, 0);
            cairo_line_to(bcr, 0, 17);
            cairo_line_to(bcr, 4.2, 13.2);
            cairo_line_to(bcr, 7.2, 19.6);
            cairo_line_to(bcr, 9.8, 18.4);
            cairo_line_to(bcr, 6.9, 12.2);
            cairo_line_to(bcr, 12.2, 12.2);
            cairo_close_path(bcr);
            cairo_set_source_rgba(bcr, 1, 1, 1, a);
            cairo_fill_preserve(bcr);
            cairo_set_source_rgba(bcr, 0.05, 0.05, 0.05, a);
            cairo_set_line_width(bcr, 1.1);
            cairo_stroke(bcr);
            cairo_restore(bcr);
        }

        void pill(cairo_t *cr, const std::string &text, double x, double y, double px, double alpha, bool right)
        {
            target.setContext(cr);
            const double tw = target.measureText(text, px, arstro::cosmo_v2::font::sansSemiBold());
            const double padX = px * 0.9, h = px * 2.1, w = tw + 2 * padX;
            const double left = right ? x - w : x;
            artboard::drawRoundedRect(target, artboard::Rect{left, y, w, h}, h * 0.5,
                                      artboard::Paint::filled(artboard::Color{0, 0, 0, 0.55 * alpha}));
            target.setFill(artboard::Color{1, 1, 1, 0.95 * alpha});
            target.drawText(text, left + padX, y + h * 0.5 + px * 0.35, px, arstro::cosmo_v2::font::sansSemiBold(), 0.06 * px);
        }

        const arstro::cosmo_v2::PhotoCanvas *canvas() const
        {
            const artboard::Segment *root = app.uiRoot("editor");
            return root ? static_cast<const arstro::cosmo_v2::PhotoCanvas *>(arstro::cosmo_v2::findSegmentByType(*root, "PhotoCanvas"))
                        : nullptr;
        }

        /** Where the photo is drawn: the preview's aspect, contained in the canvas (world coords). */
        artboard::Rect photoRect() const
        {
            const auto *c = canvas();
            if (!c) return artboard::Rect{0, 0, (double)W, (double)H};
            const artboard::Transform t = c->worldTransform();
            const double cw = c->width.value(), ch = c->height.value();
            const double fw = svc.model().frameWidth, fh = svc.model().frameHeight;
            if (fw <= 0 || fh <= 0) return artboard::Rect{t.e, t.f, cw, ch};
            const double k = std::min(cw / fw, ch / fh);
            return artboard::Rect{t.e + (cw - fw * k) * 0.5, t.f + (ch - fh * k) * 0.5, fw * k, fh * k};
        }

        void drawScene()
        {
            cairo_save(bcr);
            cairo_set_operator(bcr, CAIRO_OPERATOR_SOURCE);
            cairo_set_source_rgb(bcr, 0, 0, 0);
            cairo_paint(bcr);
            cairo_restore(bcr);
            target.setContext(bcr);
            app.render(target, now);
            const double ca = captions.at(now);
            if (ca > 0.01)
                if (const auto *c = canvas())
                {
                    // The labels ride either side of the seam, at the top of the photo.
                    const double seam = c->worldTransform().e + c->seamX();
                    const double top = photoRect().y + 14;
                    pill(bcr, "BEFORE", seam - 12, top, 11, ca, true);
                    pill(bcr, "AFTER", seam + 12, top, 11, ca, false);
                }
            drawPointer();
        }

        void compose()
        {
            const double w = zw.at(now), h = w * H / W, x = zx.at(now), y = zy.at(now);
            cairo_save(ocr);
            cairo_identity_matrix(ocr);
            cairo_scale(ocr, W / w, H / h);
            cairo_translate(ocr, -x, -y);
            cairo_set_source_surface(ocr, big, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(ocr), CAIRO_FILTER_GOOD);
            cairo_set_operator(ocr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(ocr);
            cairo_restore(ocr);
            if (xfade > 0)
            {
                cairo_save(ocr);
                cairo_set_source_surface(ocr, prev, 0, 0);
                const double p = (double)xfade / kXfade;
                cairo_paint_with_alpha(ocr, p * p * (3 - 2 * p));
                cairo_restore(ocr);
                --xfade;
            }
            const double ba = badge.at(now);
            if (ba > 0.01) speedBadge(ba);
        }

        /** "▶▶ 4× SPEED", top right. The fast-forward mark is DRAWN: the app's compiled-in faces
         *  have no ▶ (R-FONT-1), and a glyph they lack draws as an empty box. */
        void speedBadge(double alpha)
        {
            char t[32];
            std::snprintf(t, sizeof t, "%g\xC3\x97 SPEED", gOpt.speed);
            const double px = 20.0, h = 48.0, iconW = 19.0, gap = 9.0, padX = 22.0;
            target.setContext(ocr);
            const double tw = target.measureText(t, px, arstro::cosmo_v2::font::sansSemiBold());
            const double w = padX * 2 + iconW + gap + tw, x = W - 26.0 - w, y = 49.0;
            artboard::drawRoundedRect(target, artboard::Rect{x, y, w, h}, h * 0.5,
                                      artboard::Paint::filledStroked(artboard::Color{0.133, 0.133, 0.133, alpha},
                                                                     artboard::Color{1, 1, 1, 0.08 * alpha}, 1.0));
            const double cy = y + h * 0.5;
            cairo_set_source_rgba(ocr, 1, 1, 1, 0.95 * alpha);
            for (int k = 0; k < 2; ++k)
            {
                const double x0 = x + padX + k * 9.5;
                cairo_new_path(ocr);
                cairo_move_to(ocr, x0, cy - 6.5);
                cairo_line_to(ocr, x0 + 9.5, cy);
                cairo_line_to(ocr, x0, cy + 6.5);
                cairo_close_path(ocr);
                cairo_fill(ocr);
            }
            target.setFill(artboard::Color{1, 1, 1, 0.95 * alpha});
            target.drawText(t, x + padX + iconW + gap, cy + px * 0.35, px, arstro::cosmo_v2::font::sansSemiBold(), 0.06 * px);
        }

        void emit()
        {
            cairo_surface_flush(out);
            const unsigned char *d = cairo_image_surface_get_data(out);
            const int stride = cairo_image_surface_get_stride(out);
            for (int r = 0; r < H; ++r) std::fwrite(d + (size_t)r * stride, 1, (size_t)W * 4, ff);
            // Kept for a cross-fade from this frame.
            cairo_t *pc = cairo_create(prev);
            cairo_set_source_surface(pc, out, 0, 0);
            cairo_set_operator(pc, CAIRO_OPERATOR_SOURCE);
            cairo_paint(pc);
            cairo_destroy(pc);
            ++frames;
        }

        void tick()
        {
            svc.pump(now);
            pollControl();
        }

        /** One recorded frame of the virtual clock. */
        void frame()
        {
            tick();
            drawScene();
            compose();
            emit();
            now += kFrameMs;
        }
        void hold(double ms)
        {
            for (int i = 0, n = (int)std::lround(ms / kFrameMs); i < n; ++i) frame();
        }

        /** Real time passes with nothing recorded (a preview rendering off camera); the next
         *  recorded frames cross-fade from the last one written. */
        void offCamera(double realMs)
        {
            const double end = wallMs() + realMs;
            double last = wallMs();
            while (wallMs() < end)
            {
                const double w = wallMs();
                now += w - last;
                last = w;
                tick();
                drawScene();
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            xfade = kXfade;
        }

        // ── the pointer, as a user moves it ──
        void moveTo(double x, double y, double ms)
        {
            cAlpha.go(1.0, now, 200);
            cx.go(x, now, ms);
            cy.go(y, now, ms);
            for (int i = 0, n = std::max(1, (int)std::lround(ms / kFrameMs)); i < n; ++i)
            {
                app.pointer(1, cx.at(now), cy.at(now), 0, now);
                frame();
            }
            app.pointer(1, x, y, 0, now);
        }
        void click()
        {
            const double x = cx.at(now), y = cy.at(now);
            clickAt = now;
            app.pointer(0, x, y, 1, now);
            frame();
            frame();
            app.pointer(2, x, y, 1, now);
            frame();
        }
        void drag(double x0, double y0, double x1, double y1, double ms)
        {
            app.pointer(0, x0, y0, 1, now);
            frame();
            cx.go(x1, now, ms);
            cy.go(y1, now, ms);
            for (int i = 0, n = std::max(1, (int)std::lround(ms / kFrameMs)); i < n; ++i)
            {
                app.pointer(1, cx.at(now), cy.at(now), 1, now);
                frame();
            }
            app.pointer(1, x1, y1, 1, now);
            frame();
            app.pointer(2, x1, y1, 1, now);
        }

        /** Zoom so the logical rect (x, y, w) fills the frame - kept 16:9 and inside the window. */
        void zoomTo(double x, double y, double w, double ms)
        {
            w = std::clamp(w, (double)W / S, (double)W);
            const double h = w * H / W;
            zx.go(std::clamp(x, 0.0, W - w), now, ms);
            zy.go(std::clamp(y, 0.0, H - h), now, ms);
            zw.go(w, now, ms);
        }
        void zoomOut(double ms) { zoomTo(0, 0, W, ms); }

        bool openProject(const std::string &path)
        {
            Command c;
            c.kind = Command::Kind::ProjectOpen;
            c.path = path;
            if (!svc.dispatch(c)) return false;
            const double deadline = wallMs() + 15 * 60 * 1000.0;
            double last = wallMs();
            int n = 0;
            while ((svc.model().load.active || n < 120) && wallMs() < deadline)
            {
                const double w = wallMs();
                now += std::max(kFrameMs, w - last);
                last = w;
                tick();
                drawScene();
                if (!svc.model().load.active) ++n;   // and four more seconds for the reveal and the preview
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
            return !svc.model().load.active;
        }
    };
}

int main(int argc, char **argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::string &a = args[i];
        auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string(); };
        if (a == "--project") gOpt.project = next();
        else if (a == "--out") gOpt.out = next();
        else if (a == "--prompt") gOpt.prompt = next();
        else if (a == "--agent") gOpt.agent = next();
        else if (a == "--speed") gOpt.speed = std::atof(next().c_str());
        else if (a == "--compare") gOpt.compare = std::atoi(next().c_str());
        else if (a == "--workdir") gOpt.workdir = next();
        else if (a == "--max-minutes") gOpt.maxMinutes = std::atof(next().c_str());
        else if (a == "--dry") gOpt.dry = true;
        else
        {
            std::fprintf(stderr, "usage: cosmo_demo --project P.cmp --out demo.mp4 [--prompt TEXT] [--agent SETTING] [--speed 4]\n"
                                 "                  [--compare 6] [--workdir DIR] [--max-minutes 60] [--dry]\n");
            return 2;
        }
    }
    if (gOpt.project.empty() || !fs::exists(gOpt.project)) { std::fprintf(stderr, "cosmo_demo: --project <existing .cmp>\n"); return 2; }
    if (gOpt.speed < 1.0) gOpt.speed = 1.0;
    if (gOpt.workdir.empty()) gOpt.workdir = (fs::path(gOpt.out).parent_path() / "cosmo-demo-work").string();
    fs::create_directories(fs::path(gOpt.workdir) / "config");
    // The recording neither reads nor writes the user's settings, recents or agent workspace.
    setenv("XDG_CONFIG_HOME", (fs::absolute(gOpt.workdir) / "config").c_str(), 1);
    arstro::cosmo_v2::registerEmbeddedFonts();

    Demo d;
    // A unix socket path is limited to 107 bytes: the runtime dir, as the window's (R-ASSIST-3).
    const char *rt = std::getenv("XDG_RUNTIME_DIR");
    const std::string sock = std::string(rt && *rt ? rt : "/tmp") + "/cosmo-demo-" + std::to_string(getpid()) + ".sock";
    std::string err;
    if (!d.control.open(sock, err)) { std::fprintf(stderr, "cosmo_demo: control socket: %s\n", err.c_str()); return 1; }

    std::fprintf(stderr, "cosmo_demo: opening %s\n", gOpt.project.c_str());
    d.app.showEditor();
    if (!d.openProject(fs::absolute(gOpt.project).string())) { std::fprintf(stderr, "cosmo_demo: the project did not load\n"); return 1; }

    const std::string cmd = "ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt bgra -s 1920x1080 -r 30 -i - -an "
                            "-c:v libx264 -preset slow -crf 17 -pix_fmt yuv420p -movflags +faststart '" + gOpt.out + "'";
    d.ff = popen(cmd.c_str(), "w");
    if (!d.ff) { std::fprintf(stderr, "cosmo_demo: cannot start ffmpeg\n"); return 1; }

    const artboard::Segment *root = d.app.uiRoot("editor");
    const auto *tabs = static_cast<const arstro::cosmo_v2::EditStackTabs *>(arstro::cosmo_v2::findSegmentByType(*root, "EditStackTabs"));
    auto *agent = const_cast<arstro::cosmo_v2::AgentPanel *>(
        static_cast<const arstro::cosmo_v2::AgentPanel *>(arstro::cosmo_v2::findSegmentByType(*root, "AgentPanel")));
    if (!tabs || !agent) { std::fprintf(stderr, "cosmo_demo: no Agent tab\n"); return 1; }

    // ── 1. the project; open the Agent tab ──
    d.hold(1600);
    const double tabX = tabs->worldTransform().e + tabs->tabCenterX(arstro::cosmo_v2::RightColumn::kTabAgent);
    const double tabY = tabs->worldTransform().f + 13.0;
    d.moveTo(tabX, tabY, 900);
    d.click();
    d.hold(900);   // the choices fade in; the machine's agents are looked for

    // ── Connect, zoomed onto the whole panel ──
    const artboard::Transform at = agent->worldTransform();
    const double pw = agent->width.value(), ph = agent->height.value();
    const double panelZoomW = std::max((ph + 120.0) * W / H, pw + 640.0);
    d.zoomTo(at.e + pw + 24 - panelZoomW, at.f - 60, panelZoomW, 900);
    artboard::Rect sr = agent->sendRect();
    d.moveTo(at.e + sr.x + sr.w * 0.5, at.f + sr.y + sr.h * 0.5, 900);
    d.hold(300);
    d.click();   // Connect
    d.hold(900);

    // ── 2. zoom into the composer; type the prompt; send ──
    const double fieldY = at.f + ph - arstro::cosmo_v2::AgentPanel::kComposerH * 0.5;
    const double fieldX = at.e + arstro::cosmo_v2::AgentPanel::kPadX +
                          (pw - 2 * arstro::cosmo_v2::AgentPanel::kPadX - arstro::cosmo_v2::AgentPanel::kGap - arstro::cosmo_v2::AgentPanel::kSendW) * 0.5;
    const double promptZoomW = W / S;   // 2x: exactly the render's resolution
    d.zoomTo(at.e + pw + 12 - promptZoomW, fieldY + 120 - promptZoomW * H / W, promptZoomW, 1000);
    d.moveTo(fieldX - 40, fieldY, 1000);
    d.click();
    d.moveTo(fieldX + 40, fieldY - 90, 350);   // off the text (into the empty chat), so the words being typed are seen
    // A person's typing: ~14 characters a second, a little uneven.
    for (size_t n = 1; n <= gOpt.prompt.size(); ++n)
    {
        while (n < gOpt.prompt.size() && ((unsigned char)gOpt.prompt[n] & 0xC0) == 0x80) ++n;
        agent->typeForTest(gOpt.prompt.substr(0, n));
        d.hold(n % 5 == 0 ? 110 : 66);
    }
    d.hold(500);
    sr = agent->sendRect();
    d.moveTo(at.e + sr.x + sr.w * 0.5, at.f + sr.y + sr.h * 0.5, 600);
    d.hold(150);
    if (gOpt.dry)
    {
        agent->typeForTest("");
        d.click();
    }
    else
        d.click();   // Send: `assist send` to the real agent
    d.hold(700);

    // ── 3. zoom out; the agent works in real time, recorded at --speed ──
    d.cAlpha.go(0.0, d.now, 400);
    d.zoomOut(1100);
    d.hold(1200);
    if (gOpt.dry)
    {
        d.badge.go(1.0, d.now, 300);   // the agent's part, as a layout check: just the badge
        d.hold(1500);
        d.badge.go(0.0, d.now, 300);
        d.hold(400);
    }
    else
    {
        d.badge.go(1.0, d.now, 300);
        const double step = kFrameMs * gOpt.speed;
        double last = wallMs(), next = d.now;
        const double deadline = wallMs() + gOpt.maxMinutes * 60000.0;
        bool sawBusy = false;
        long agentFrames = 0;
        while (wallMs() < deadline)
        {
            const double w = wallMs();
            d.now += w - last;
            last = w;
            d.tick();
            const std::string &st = d.svc.model().assist.state;
            if (st == "busy") sawBusy = true;
            if ((sawBusy && st != "busy") || st == "failed" || (st == "off" && agentFrames > 30)) break;
            if (d.now >= next)
            {
                d.drawScene();
                d.compose();
                d.emit();
                ++agentFrames;
                next += step;
                if (agentFrames % 150 == 0)
                    std::fprintf(stderr, "cosmo_demo: agent working, %.1f min real, %ld frames\n", agentFrames * step / 60000.0, agentFrames);
            }
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        if (wallMs() >= deadline) std::fprintf(stderr, "cosmo_demo: the agent ran past --max-minutes; cutting to the result\n");
        d.badge.go(0.0, d.now, 300);
        d.hold(2200);   // the agent's last words, at normal speed
    }

    // ── 4. before / after ──
    std::vector<int> nodes;
    for (const auto &n : d.svc.model().nodes)
        if (!n.group && !n.failed && n.slot >= 0) nodes.push_back(n.node);
    std::vector<int> pick;
    const int k = std::min<int>(gOpt.compare, (int)nodes.size());
    for (int i = 0; i < k; ++i) pick.push_back(nodes[(size_t)((i * (int)nodes.size()) / std::max(1, k))]);
    const auto *cv = d.canvas();
    if (cv && !pick.empty())
    {
        const artboard::Transform ct = cv->worldTransform();
        const double chh = cv->height.value();
        const double zoomW = std::min((double)W, chh * W / H);
        const artboard::Rect pr0 = d.photoRect();
        d.zoomTo(pr0.x + pr0.w * 0.5 - zoomW * 0.5, ct.f, zoomW, 1100);
        const artboard::Segment *pillSeg = arstro::cosmo_v2::findSegmentByType(*cv, "SegmentedControl");
        bool split = false;
        for (size_t i = 0; i < pick.size(); ++i)
        {
            std::string e2;
            d.svc.dispatchText("select " + std::to_string(pick[i]), e2);
            if (i == 0)
            {
                d.hold(600);
                d.offCamera(2500);
            }
            else
                d.offCamera(2500);
            if (!split && pillSeg)
            {
                const artboard::Transform pt = pillSeg->worldTransform();
                d.moveTo(pt.e + pillSeg->width.value() * 0.5, pt.f + pillSeg->height.value() * 0.5, 800);
                d.click();   // Split
                d.hold(500);
                split = true;
                d.captions.go(1.0, d.now, 400);
            }
            // Swept across the PHOTO (a portrait leaves wide margins in the canvas): from mostly as
            // shot to mostly the agent's grade, and back to the middle.
            const artboard::Rect pr = d.photoRect();
            const double midY = pr.y + pr.h * 0.5;
            const double seam = ct.e + cv->seamX();
            const double right = pr.x + pr.w * 0.92, left = pr.x + pr.w * 0.08, mid = pr.x + pr.w * 0.5;
            d.moveTo(seam, midY, 500);
            d.drag(seam, midY, right, midY, 900);
            d.hold(400);
            d.drag(right, midY, left, midY, 1800);
            d.hold(600);
            d.drag(left, midY, mid, midY, 700);
            d.hold(600);
        }
        d.captions.go(0.0, d.now, 300);
        d.cAlpha.go(0.0, d.now, 300);
        d.zoomOut(1100);
        d.hold(1800);
    }

    pclose(d.ff);
    std::string e3;
    d.svc.dispatchText("assist stop", e3);
    d.tick();
    d.control.close();
    std::fprintf(stderr, "cosmo_demo: %ld frames (%.1f s) -> %s\n", d.frames, d.frames / kFps, gOpt.out.c_str());
    return 0;
}
