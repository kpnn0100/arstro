/*
 *  solaris-cc ntwb — Solaris as an Arstro Remote app (R-SVC-7; cosmo's `cosmo-cc ntwb`).
 *
 *      solaris-cc ntwb install [--data-dir D] [--web-dir W]   write what Arstro Remote reads to list the app
 *      solaris-cc ntwb uninstall [--data-dir D]
 *      solaris-cc ntwb serve                                   started BY Arstro Remote (NTWB_SOCKET / NTWB_TOKEN)
 *      solaris-cc ntwb api                                     the API description (docs/ntwb-api.json)
 *
 *  `install` writes <D>/solaris/{ntwb.json, api.json, web/…}; D defaults to $XDG_DATA_HOME/ntwb/apps
 *  (~/.local/share/ntwb/apps). The web UI is apps/solaris/web plus what it borrows, copied beside it
 *  rather than forked: cosmo's `signal.js` and `dom.js` (the reactive core and the keyed DOM helpers,
 *  apps/cosmo/web/js/core) and cosmo's typeface (Roboto, JetBrains Mono, OFL).
 *
 *  `serve` is the app: THE service with the same host as the window (files, devices, the clock
 *  device — a browser's `transport play` plays on this machine), the adapter, and the loop that owns
 *  the service's clock: read the bridge, pump, publish. Holds no behaviour (NtwbAdapter.h).
 */
#include "AudioFiles.h"
#include "Faces.h"
#include "Machine.h"
#include "NtwbAdapter.h"
#include "SolarisService.h"
#ifdef SOLARIS_HAVE_PULSE
#include "AudioOutPulse.h"
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef SOLARIS_SOURCE_DIR
#define SOLARIS_SOURCE_DIR "."
#endif
#ifndef COSMO_SOURCE_DIR
#define COSMO_SOURCE_DIR "../cosmo"
#endif

namespace arstro
{
namespace solaris_cli
{
    namespace
    {
        namespace fs = std::filesystem;
        constexpr int kOk = 0, kFail = 1, kUsage = 2;
        constexpr const char *kAppId = "solaris";
        constexpr const char *kAppVersion = "1";

        int fail(const std::string &why)
        {
            std::fprintf(stderr, "solaris-cc ntwb: %s\n", why.c_str());
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

        int install(const std::vector<std::string> &args)
        {
            const fs::path appDir = dataDir(args) / kAppId;
            const fs::path src = option(args, "web-dir", std::string(SOLARIS_SOURCE_DIR) + "/web");
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
            // borrowed, not forked (R-UI-4's rule for the web): cosmo's reactive core and DOM helpers
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
            m.set("name", "Solaris");
            m.set("version", kAppVersion);
            m.set("description", "DAW: the song, its lanes and its mixer - the engine and the audio run on this machine");
            m.set("icon", "web/icon.svg");
            ntwb::Json exec = ntwb::Json::array();
            exec.push(exe.string());
            exec.push("ntwb");
            exec.push("serve");
            m.set("exec", exec);
            m.set("web", "web");
            m.set("api", "api.json");
            m.set("capabilities", ntwb::Json::array());
            // ONE session: a second would be a second engine on the same clock device. The pages of
            // the session share the song (MVVM), which is how two people work on one.
            m.set("single", true);
            {
                std::ofstream mf(appDir / "ntwb.json");
                mf << m.dumpPretty();
                if (!mf) return fail("cannot write " + (appDir / "ntwb.json").string());
            }
            std::cout << "installed solaris for Arstro Remote in " << appDir.string() << "\n"
                      << "  exec " << exe.string() << " ntwb serve\n"
                      << "  (Arstro Remote lists it under Apps; `arstro-remote apps list`)\n";
            return kOk;
        }

        int serve()
        {
            solaris::SolarisService::Host host;
            host.decodeAudio = solaris_host::decodeAudio;
            host.writeWav = solaris_host::writeWav;
            host.listDir = solaris_host::listDir;
            host.listDevices = solaris_host::listDevices;
#ifdef SOLARIS_HAVE_PULSE
            host.audioOut = [] { return std::unique_ptr<solaris::IAudioOut>(new solaris_host::AudioOutPulse()); };
#endif
            solaris_host::machinePaths(host.settingsPath, host.recentsPath); // the window's settings and recents
            solaris::SolarisService svc(host);

            ntwb::Client::Options o;
            o.app = kAppId;
            o.version = kAppVersion;
            ntwb::Client client(o);
            NtwbAdapter adapter(svc, client);
            adapter.start();
            std::string err;
            if (!client.connect(err)) return fail(err);
            const auto t0 = std::chrono::steady_clock::now();
            // the service's clock is ours: read the bridge, pump, publish — at most ~8 ms asleep in
            // the poll, so a fader drag is answered within a frame
            while (client.poll(8))
            {
                svc.pump();
                adapter.tick(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            }
            std::fprintf(stderr, "solaris-cc ntwb: %s\n", client.lastError().c_str());
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
        std::fprintf(stderr, "solaris-cc ntwb: serve | install [--data-dir D] [--web-dir W] | uninstall [--data-dir D] | api\n");
        return kUsage;
    }

    const char *facesUsage()
    {
        return "\nThe other faces (R-SVC-5…7):\n"
               "  solaris-cc attach <socket> [--script <f>] [--follow] [--timeout <s>] [<command> : …]\n"
               "                                            drive a running `solaris --control <socket>` window:\n"
               "                                            output on stdout, events on stderr, as here\n"
               "  solaris-cc ntwb install [--data-dir <D>]  Solaris as an Arstro Remote app (its web UI in a browser)\n"
               "  solaris-cc ntwb uninstall [--data-dir <D>] | serve | api\n";
    }
}
}
