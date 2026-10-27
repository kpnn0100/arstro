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
 *  it borrows, copied beside it rather than forked:
 *    - every file `web/BORROW` lists — one path per line, relative to apps/cosmo/web (`#` starts a
 *      comment) — copied to the SAME relative path in the installed page: cosmo's reactive core
 *      (js/core/*), its UI kit (js/ui/*) and the stylesheets that kit needs. A listed file that does
 *      not exist fails the install, naming it (a page that imports it would break in the browser,
 *      where nobody reads the error);
 *    - cosmo's typeface (Roboto, JetBrains Mono, OFL) — the faces the window compiles in;
 *    - web/files.json: every file installed, so the page's module loader can tell a widget that is
 *      not there yet from one that failed (js/kit/files.js).
 *
 *  `serve` is the app: THE service with the same host as interstellar-cc (HostSetup.h: the codecs,
 *  the window's settings, recents and presets), the adapter, and the loop that owns the service's
 *  clock: read the bridge, pump, publish. It also binds what the GTK host binds in AppHooks beyond
 *  the service (linux_main.cpp:498-512): the asynchronous thumbnailer, resolved against the project
 *  as linux_main's resolveMedia does, the box scaler and a PNG encoder. Holds no behaviour
 *  (NtwbAdapter.h).
 */
#include "HostSetup.h"
#include "InterstellarService.h"
#include "Ntwb.h"
#include "NtwbAdapter.h"
#include "core/ThreadBudget.h"
#ifdef INTERSTELLAR_HAVE_FFMPEG
#include "Thumbnailer.h"              // the window's thumbnail hook (linux_main.cpp:501-505)
#include <gdk-pixbuf/gdk-pixbuf.h>   // the host links GdkPixbuf (PngWriter): its JPEG / PNG savers are the encoders
#endif
#include <algorithm>
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
        /** RGBA8 -> PNG through GdkPixbuf, alpha kept (Copy Frame's picture, R-UI-11). */
        bool encodePng(const uint8_t *rgba, int w, int h, std::string &out)
        {
            if (!rgba || w <= 0 || h <= 0) return false;
            GdkPixbuf *pb = gdk_pixbuf_new_from_data(rgba, GDK_COLORSPACE_RGB, TRUE, 8, w, h, w * 4, nullptr, nullptr);
            if (!pb) return false;
            gchar *buf = nullptr;
            gsize size = 0;
            GError *err = nullptr;
            const gboolean ok = gdk_pixbuf_save_to_buffer(pb, &buf, &size, "png", &err, "compression", "3", (char *)nullptr);
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

        /** A rack source's media is stored relative to the project; the thumbnail hook gets it as the
         *  model spells it, so it is resolved here — paths are the host's business (linux_main.cpp
         *  resolveMedia, R-SCOPE-3). */
        std::string resolveMedia(const interstellar::InterstellarService &svc, const std::string &p)
        {
            if (p.empty() || fs::path(p).is_absolute()) return p;
            const std::string isp = svc.model().projectPath;
            return isp.empty() ? p : (fs::path(isp).parent_path() / p).lexically_normal().string();
        }

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

        /** The cosmo files `web/BORROW` lists (one path per line, relative to apps/cosmo/web; `#` comments,
         *  blank lines ignored). A missing BORROW borrows nothing. */
        std::vector<std::string> borrowList(const fs::path &webDir)
        {
            std::vector<std::string> out;
            std::ifstream f(webDir / "BORROW");
            std::string line;
            while (std::getline(f, line))
            {
                const auto hash = line.find('#');
                if (hash != std::string::npos) line.erase(hash);
                const auto a = line.find_first_not_of(" \t\r"), b = line.find_last_not_of(" \t\r");
                if (a == std::string::npos) continue;
                out.push_back(line.substr(a, b - a + 1));
            }
            return out;
        }

        int install(const std::vector<std::string> &args)
        {
            const fs::path appDir = dataDir(args) / kAppId;
            const fs::path src = option(args, "web-dir", std::string(INTERSTELLAR_SOURCE_DIR) + "/web");
            const fs::path cosmo = COSMO_SOURCE_DIR;
            if (!fs::exists(src / "index.html")) return fail("no web UI at " + src.string() + " (--web-dir)");
            std::error_code ec;
            const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
            if (ec) return fail("cannot find this executable: " + ec.message());

            // what the page borrows, checked BEFORE anything is replaced: a listed file that is not there
            // fails the install by name and leaves the installed page as it was
            const std::vector<std::string> borrowed = borrowList(src);
            for (const auto &b : borrowed)
            {
                const fs::path rel(b);
                if (rel.is_absolute() || b.find("..") != std::string::npos) return fail("web/BORROW: " + b + " is not a path inside apps/cosmo/web");
                if (!fs::is_regular_file(cosmo / "web" / rel)) return fail("web/BORROW lists " + b + ", which apps/cosmo/web does not have");
            }
            fs::remove_all(appDir / "web", ec);
            fs::create_directories(appDir / "web" / "fonts", ec);
            fs::copy(src, appDir / "web", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec) return fail("cannot copy the web UI: " + ec.message());
            fs::remove(appDir / "web" / "BORROW", ec);   // the list is the installer's, not a page file
            // borrowed, not forked: cosmo's files at the same relative paths, as Solaris's page borrows them
            for (const auto &b : borrowed)
            {
                const fs::path to = appDir / "web" / b;
                fs::create_directories(to.parent_path(), ec);
                fs::copy_file(cosmo / "web" / b, to, fs::copy_options::overwrite_existing, ec);
                if (ec) return fail("cannot copy cosmo's " + b + ": " + ec.message());
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
                // what the page has, for its module loader (js/kit/files.js): a widget module not installed
                // yet is shown as a quiet placeholder instead of being requested (and logged as a 404)
                std::vector<std::string> files;
                for (auto it = fs::recursive_directory_iterator(appDir / "web", ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                    if (it->is_regular_file()) files.push_back(fs::relative(it->path(), appDir / "web").generic_string());
                std::sort(files.begin(), files.end());
                ntwb::Json list = ntwb::Json::array();
                for (const auto &f : files) list.push(f);
                ntwb::Json j = ntwb::Json::object();
                j.set("files", list);
                std::ofstream out(appDir / "web" / "files.json");
                out << j.dump() << "\n";
                if (!out) return fail("cannot write " + (appDir / "web" / "files.json").string());
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
            // what the GTK host binds beyond the service (linux_main.cpp:501-512): stills decoded on the
            // Thumbnailer's worker, re-asked when its epoch rises (D-5, D-6)
            interstellar_host::Thumbnailer thumbs;
            NtwbPictures pictures;
            pictures.thumbnail = [&svc, &thumbs](const std::string &media, double t, int edge, interstellar::Raster &out) {
                return thumbs.get(resolveMedia(svc, media), t, edge, out);
            };
            pictures.thumbnailEpoch = [&thumbs] { return thumbs.epoch(); };
            pictures.fit = [](const interstellar::Raster &in, int edge, interstellar::Raster &out) { interstellar_host::boxDownscale(in, edge, out); };
            pictures.png = encodePng;
            NtwbAdapter adapter(svc, client, encodeJpeg, pictures);
#else
            NtwbAdapter adapter(svc, client);   // no codec in this build: the pictures say so, the rest works
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
