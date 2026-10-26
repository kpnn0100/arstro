/*
 *  interstellar-cc ntwb — Interstellar as an Arstro Remote app (R-SVC-4; solaris-cc's and cosmo-cc's
 *  `ntwb` verbs are the worked examples).
 *
 *      interstellar-cc ntwb install [--data-dir D] [--web-dir W]   write what Arstro Remote reads to list the app
 *      interstellar-cc ntwb uninstall [--data-dir D]
 *      interstellar-cc ntwb serve                                   started BY Arstro Remote (NTWB_SOCKET / NTWB_TOKEN)
 *      interstellar-cc ntwb api                                     the API description (docs/ntwb-api.json)
 *
 *  `install` writes <D>/interstellar/{ntwb.json, api.json, web/…}; D defaults to
 *  $XDG_DATA_HOME/ntwb/apps (~/.local/share/ntwb/apps). The web UI is apps/interstellar/web plus what
 *  it borrows, copied beside it rather than forked: cosmo's `signal.js` and `dom.js` (the reactive
 *  core and the keyed DOM helpers, apps/cosmo/web/js/core) and cosmo's typeface (Roboto, JetBrains
 *  Mono, OFL) — the faces the window compiles in.
 *
 *  `serve` is the app: THE service with the same host as interstellar-cc (HostSetup.h: the codecs,
 *  the window's settings, recents and presets), the adapter, and the loop that owns the service's
 *  clock: read the bridge, pump, publish. Holds no behaviour (NtwbAdapter.h).
 */
#include "HostSetup.h"
#include "InterstellarService.h"
#include "Ntwb.h"
#include "NtwbAdapter.h"
#include "core/ThreadBudget.h"
#ifdef INTERSTELLAR_HAVE_FFMPEG
#include <gdk-pixbuf/gdk-pixbuf.h>   // the host links GdkPixbuf (PngWriter): its JPEG saver is the encoder
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef INTERSTELLAR_SOURCE_DIR
#define INTERSTELLAR_SOURCE_DIR "."
#endif
#ifndef COSMO_SOURCE_DIR
#define COSMO_SOURCE_DIR "../cosmo"
#endif

namespace arstro
{
namespace interstellar_cli
{
    namespace
    {
        namespace fs = std::filesystem;
        constexpr int kOk = 0, kFail = 1, kUsage = 2;
        constexpr const char *kAppId = "interstellar";
        constexpr const char *kAppVersion = "1";

        int fail(const std::string &why)
        {
            std::fprintf(stderr, "interstellar-cc ntwb: %s\n", why.c_str());
            return kFail;
        }

        std::string option(const std::vector<std::string> &args, const std::string &name, const std::string &fallback = "")
        {
            for (size_t i = 0; i + 1 < args.size(); ++i)
                if (args[i] == "--" + name) return args[i + 1];
            for (const auto &a : args)
                if (a.rfind("--" + name + "=", 0) == 0) return a.substr(name.size() + 3);
            return fallback;
        }

        fs::path dataDir(const std::vector<std::string> &args)
        {
            const std::string d = option(args, "data-dir");
            if (!d.empty()) return d;
            const char *x = std::getenv("XDG_DATA_HOME");
            const char *home = std::getenv("HOME");
            return fs::path(x && *x ? std::string(x) : std::string(home ? home : ".") + "/.local/share") / "ntwb" / "apps";
        }

#ifdef INTERSTELLAR_HAVE_FFMPEG
        /** RGBA8 -> JPEG through GdkPixbuf (cosmo-cc's encodeJpeg). JPEG has no alpha: packed to RGB first. */
        bool encodeJpeg(const uint8_t *rgba, int w, int h, int quality, std::string &out)
        {
            if (!rgba || w <= 0 || h <= 0) return false;
            std::vector<guchar> rgb((size_t)w * h * 3);
            for (size_t i = 0, n = (size_t)w * h; i < n; ++i)
            {
                rgb[i * 3] = rgba[i * 4];
                rgb[i * 3 + 1] = rgba[i * 4 + 1];
                rgb[i * 3 + 2] = rgba[i * 4 + 2];
            }
            GdkPixbuf *pb = gdk_pixbuf_new_from_data(rgb.data(), GDK_COLORSPACE_RGB, FALSE, 8, w, h, w * 3, nullptr, nullptr);
            if (!pb) return false;
            gchar *buf = nullptr;
            gsize size = 0;
            GError *err = nullptr;
            const std::string q = std::to_string(quality);
            const gboolean ok = gdk_pixbuf_save_to_buffer(pb, &buf, &size, "jpeg", &err, "quality", q.c_str(), (char *)nullptr);
            g_object_unref(pb);
            if (!ok)
            {
                if (err) g_error_free(err);
                return false;
            }
            out.assign(buf, size);
            g_free(buf);
            return true;
        }
#endif

        int install(const std::vector<std::string> &args)
        {
            const fs::path appDir = dataDir(args) / kAppId;
            const fs::path src = option(args, "web-dir", std::string(INTERSTELLAR_SOURCE_DIR) + "/web");
            const fs::path cosmo = COSMO_SOURCE_DIR;
            if (!fs::exists(src / "index.html")) return fail("no web UI at " + src.string() + " (--web-dir)");
            std::error_code ec;
            const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
            if (ec) return fail("cannot find this executable: " + ec.message());

            fs::remove_all(appDir / "web", ec);
            fs::create_directories(appDir / "web" / "fonts", ec);
            fs::create_directories(appDir / "web" / "js" / "core", ec);
            fs::copy(src, appDir / "web", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec) return fail("cannot copy the web UI: " + ec.message());
            // borrowed, not forked: cosmo's reactive core and DOM helpers, as Solaris's page borrows them
            for (const char *f : {"signal.js", "dom.js"})
            {
                fs::copy_file(cosmo / "web" / "js" / "core" / f, appDir / "web" / "js" / "core" / f, fs::copy_options::overwrite_existing, ec);
                if (ec) return fail(std::string("cannot copy cosmo's ") + f + ": " + ec.message());
            }
            // the faces the window compiles in (cosmo's), shipped beside the page, never fetched
            for (const char *f : {"Roboto/Roboto-Regular.ttf", "Roboto/Roboto-Medium.ttf", "Roboto/Roboto-SemiBold.ttf", "Roboto/OFL.txt",
                                  "JetBrainsMono/JetBrainsMono-Regular.ttf", "JetBrainsMono/JetBrainsMono-Medium.ttf"})
            {
                const fs::path from = cosmo / "assets" / "fonts" / f;
                const fs::path to = appDir / "web" / "fonts" /
                                    (std::string(f).find("OFL") != std::string::npos ? "OFL-Roboto.txt" : from.filename().string());
                fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
                if (ec) return fail("cannot copy font " + from.string() + ": " + ec.message());
            }
            {
                std::ofstream api(appDir / "api.json");
                api << NtwbAdapter::apiDescription().dumpPretty();
                if (!api) return fail("cannot write " + (appDir / "api.json").string());
            }
            ntwb::Json m = ntwb::Json::object();
            m.set("ntwb", ntwb::kVersion);
            m.set("id", kAppId);
            m.set("name", "Interstellar");
            m.set("version", kAppVersion);
            m.set("description", "Video editor with a colour rack: cut, grade and deliver - the core, the media and the renders stay on this machine");
            m.set("icon", "web/icon.svg");
            ntwb::Json exec = ntwb::Json::array();
            exec.push(exe.string());
            exec.push("ntwb");
            exec.push("serve");
            m.set("exec", exec);
            m.set("web", "web");
            m.set("api", "api.json");
            ntwb::Json caps = ntwb::Json::array();
            caps.push("blobs");   // the monitor's pictures
            m.set("capabilities", caps);
            // MANY sessions: each its own process, its own project (cosmo's choice). The pages of one
            // session share the project (MVVM), which is how two people cut one film.
            m.set("single", false);
            {
                std::ofstream mf(appDir / "ntwb.json");
                mf << m.dumpPretty();
                if (!mf) return fail("cannot write " + (appDir / "ntwb.json").string());
            }
            std::cout << "installed interstellar for Arstro Remote in " << appDir.string() << "\n"
                      << "  exec " << exe.string() << " ntwb serve\n"
                      << "  (Arstro Remote lists it under Apps; `arstro-remote apps list`)\n";
            return kOk;
        }

        double monotonicMs()
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        int serve()
        {
            cosmo::ThreadBudget budget(50);   // interstellar-cc's: `settings set cpuPercent=` moves it
            interstellar::InterstellarService svc(budget, makeHost());

            ntwb::Client::Options o;
            o.app = kAppId;
            o.version = kAppVersion;
            o.capabilities = {"blobs"};
            ntwb::Client client(o);
#ifdef INTERSTELLAR_HAVE_FFMPEG
            NtwbAdapter adapter(svc, client, encodeJpeg);
#else
            NtwbAdapter adapter(svc, client);   // no codec in this build: `frame` says so, the rest works
#endif
            adapter.start();
            std::string err;
            if (!client.connect(err)) return fail(err);
            // the service's clock is ours: read the bridge, pump, publish — at most ~8 ms asleep in
            // the poll, so a scrub is answered within a frame
            while (client.poll(8))
            {
                const double now = monotonicMs();
                svc.pump(now);
                adapter.tick(now);
            }
            std::fprintf(stderr, "interstellar-cc ntwb: %s\n", client.lastError().c_str());
            // an orderly end (the host said bye, or closed) is not a failure of this program
            return client.lastError().find("bye") != std::string::npos || client.lastError().find("closed") != std::string::npos ? kOk
                                                                                                                                 : kFail;
        }
    }

    int ntwbMain(const std::vector<std::string> &args)
    {
        const std::string sub = args.empty() ? "" : args[0];
        if (sub == "api")
        {
            std::cout << NtwbAdapter::apiDescription().dumpPretty();
            return kOk;
        }
        if (sub == "install") return install(args);
        if (sub == "uninstall")
        {
            const fs::path appDir = dataDir(args) / kAppId;
            std::error_code ec;
            fs::remove_all(appDir, ec);
            if (ec) return fail("cannot remove " + appDir.string() + ": " + ec.message());
            std::cout << "removed " << appDir.string() << "\n";
            return kOk;
        }
        if (sub == "serve") return serve();
        std::fprintf(stderr, "interstellar-cc ntwb: serve | install [--data-dir D] [--web-dir W] | uninstall [--data-dir D] | api\n");
        return kUsage;
    }
}
}
