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
 *  rather than forked:
 *    - what `web/BORROW` lists — one path per line, relative to apps/cosmo/web (`#` starts a comment; a
 *      `*` in the last part is a glob over that folder): cosmo's reactive core, its UI kit and the CSS
 *      the kit needs, landing at the same path in the installed tree. A listed file that is not there
 *      (or a glob matching nothing) is an ERROR naming it, and so is a borrowed file the app also has —
 *      borrowed, never forked;
 *    - the window's words: apps/solaris/lang/*.strings → web/lang/ (R-ACCESS-2: the page looks its
 *      words up in the same tables the window compiles in);
 *    - cosmo's typeface (Roboto, JetBrains Mono, OFL).
 *
 *  `install` also writes web/files.json — every file of the installed page, sorted — so the page mounts only the
 *  widget modules that exist (kit/slot.js) and never asks the host for one that does not.
 *
 *  `serve` is the app: THE service with the same host as the window (files, devices, the clock
 *  device — a browser's `transport play` plays on this machine), the adapter with the host's pieces
 *  the window also has (the folder lister, two video decoders — App's onVideoFrame / onVideoThumb — and
 *  a JPEG encoder), and the loop that owns the service's clock: read the bridge, pump, publish. Holds no
 *  behaviour (NtwbAdapter.h).
 */
#include "AudioFiles.h"
#include "VideoFiles.h"
#include "Faces.h"
#include "PluginInstance.h"
#include "Plugins.h"
#include "Machine.h"
#include "NtwbAdapter.h"
#include "SolarisService.h"
#ifdef SOLARIS_HAVE_PULSE
#include "AudioInPulse.h"
#include "AudioOutPulse.h"
#ifdef SOLARIS_HAVE_PIPEWIRE
#include "AudioInPipewire.h"
#include "AudioOutPipewire.h"
#endif
#ifdef SOLARIS_HAVE_ALSA
#include "MidiInAlsa.h"
#endif
#endif
#ifdef SOLARIS_HAVE_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
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

#ifdef SOLARIS_HAVE_PIXBUF
        /** RGBA8 -> JPEG through GdkPixbuf (interstellar-cc's and cosmo-cc's encoder). JPEG has no alpha: RGB first. */
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

        /** One line of BORROW expanded: the files of apps/cosmo/web it names (relative paths). Empty: none. */
        std::vector<fs::path> borrowed(const fs::path &cosmoWeb, const std::string &line)
        {
            std::vector<fs::path> out;
            const fs::path rel(line);
            const std::string leaf = rel.filename().string();
            const size_t star = leaf.find('*');
            if (star == std::string::npos)
            {
                std::error_code ec;
                if (fs::is_regular_file(cosmoWeb / rel, ec)) out.push_back(rel);
                return out;
            }
            const std::string pre = leaf.substr(0, star), post = leaf.substr(star + 1);
            std::error_code ec;
            for (auto it = fs::directory_iterator(cosmoWeb / rel.parent_path(), ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            {
                const std::string n = it->path().filename().string();
                if (!it->is_regular_file(ec) || n.size() < pre.size() + post.size()) continue;
                if (n.compare(0, pre.size(), pre) == 0 && n.compare(n.size() - post.size(), post.size(), post) == 0)
                    out.push_back(rel.parent_path() / n);
            }
            std::sort(out.begin(), out.end());
            return out;
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

            // what the page borrows: read and checked BEFORE anything is removed, so a bad list leaves the old install
            std::vector<fs::path> borrow;
            {
                std::ifstream list(src / "BORROW");
                if (!list) return fail("no " + (src / "BORROW").string() + " (the cosmo files the page borrows)");
                std::string line;
                while (std::getline(list, line))
                {
                    if (const size_t hash = line.find('#'); hash != std::string::npos) line.erase(hash);
                    while (!line.empty() && std::isspace((unsigned char)line.back())) line.pop_back();
                    size_t at = 0;
                    while (at < line.size() && std::isspace((unsigned char)line[at])) ++at;
                    line.erase(0, at);
                    if (line.empty()) continue;
                    if (line.find("..") != std::string::npos || line[0] == '/') return fail("BORROW: " + line + " is not a path inside apps/cosmo/web");
                    const auto files = borrowed(cosmo / "web", line);
                    if (files.empty()) return fail("BORROW names " + line + ", but " + (cosmo / "web" / line).string() + " is not there");
                    for (const auto &f : files)
                    {
                        if (fs::exists(src / f, ec)) return fail("web/" + f.string() + " is the app's AND borrowed from cosmo (BORROW) - borrowed, never forked");
                        borrow.push_back(f);
                    }
                }
            }

            fs::remove_all(appDir / "web", ec);
            fs::create_directories(appDir / "web" / "fonts", ec);
            fs::copy(src, appDir / "web", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec) return fail("cannot copy the web UI: " + ec.message());
            // borrowed, not forked (R-UI-4's rule for the web): cosmo's core, its UI kit and its CSS, at the same paths
            for (const auto &f : borrow)
            {
                fs::create_directories((appDir / "web" / f).parent_path(), ec);
                fs::copy_file(cosmo / "web" / f, appDir / "web" / f, fs::copy_options::overwrite_existing, ec);
                if (ec) return fail("cannot copy cosmo's " + f.string() + ": " + ec.message());
            }
            // the window's words (R-ACCESS-2): the tables the window compiles in, for the page to look up
            fs::create_directories(appDir / "web" / "lang", ec);
            for (auto it = fs::directory_iterator(fs::path(SOLARIS_SOURCE_DIR) / "lang", ec); !ec && it != fs::directory_iterator(); it.increment(ec))
                if (it->path().extension() == ".strings")
                {
                    std::error_code ec2;
                    fs::copy_file(it->path(), appDir / "web" / "lang" / it->path().filename(), fs::copy_options::overwrite_existing, ec2);
                    if (ec2) return fail("cannot copy " + it->path().string() + ": " + ec2.message());
                }
            if (ec) return fail("cannot read " + (fs::path(SOLARIS_SOURCE_DIR) / "lang").string() + ": " + ec.message());
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
            // the installed page's files: what kit/slot.js mounts (a widget module not there yet keeps its placeholder)
            {
                std::vector<std::string> files;
                for (auto it = fs::recursive_directory_iterator(appDir / "web", ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                    if (std::error_code e2; it->is_regular_file(e2)) files.push_back(fs::relative(it->path(), appDir / "web").generic_string());
                std::sort(files.begin(), files.end());
                ntwb::Json list = ntwb::Json::array();
                for (const auto &f : files) list.push(f);
                std::ofstream fl(appDir / "web" / "files.json");
                fl << list.dump();
                if (!fl) return fail("cannot write " + (appDir / "web" / "files.json").string());
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
            host.writeAudio = solaris_host::writeAudio;
            host.probeVideo = solaris_host::probeVideo; // R-VIDEO-1
            host.muxVideo = solaris_host::muxVideo;
            host.listDir = solaris_host::listDir;
            host.listDevices = solaris_host::listDevices;
#ifdef SOLARIS_HAVE_PULSE
            host.audioOut = [] { return std::unique_ptr<solaris::IAudioOut>(new solaris_host::AudioOutPulse()); };
            host.audioIn = [] { return std::unique_ptr<solaris::IAudioIn>(new solaris_host::AudioInPulse()); }; // R-REC-1
#endif
            // R-SVC-7: the "web" driver - the engine plays into the pages that listen (WebAudio.h); PipeWire's as the CLI has it
            auto webAudio = std::make_shared<WebAudioHub>();
            host.audioOutFor = [webAudio](const std::string &d) -> std::unique_ptr<solaris::IAudioOut> {
                if (d == "web") return std::unique_ptr<solaris::IAudioOut>(new WebAudioOut(webAudio));
#ifdef SOLARIS_HAVE_PIPEWIRE
                if (d == "pipewire") return std::unique_ptr<solaris::IAudioOut>(new solaris_host::AudioOutPipewire()); // R-DEV-9
#endif
                return nullptr;
            };
#ifdef SOLARIS_HAVE_PIPEWIRE
            host.audioInFor = [](const std::string &d) { return d == "pipewire" ? std::unique_ptr<solaris::IAudioIn>(new solaris_host::AudioInPipewire()) : nullptr; }; // R-REC-1
#endif
#ifdef SOLARIS_HAVE_ALSA
            host.listMidiInputs = solaris_host::listMidiInputs; // R-MIDI-2
            host.midiIn = [] { return std::unique_ptr<solaris::IMidiIn>(new solaris_host::MidiInAlsa()); };
            host.midiOut = [] { return std::unique_ptr<solaris::IMidiOut>(new solaris_host::MidiOutAlsa()); }; // R-CTRL-2: a surface's port
#endif
            solaris_host::machinePaths(host.settingsPath, host.recentsPath); // the window's settings and recents
            solaris_host::presetPaths(host.presetsPath, host.factoryPresetsPath); // R-PRESET-1
            solaris_host::templatePaths(host.templatesPath, host.factoryTemplatesPath); // R-PRESET-2
            host.libraryPath = solaris_host::libraryPath(); // R-PRESET-3
            host.pluginsPath = solaris_host::pluginsListPath(); // R-VST-6
            host.scanPlugins = [](std::vector<arstro::solaris::PluginModel> &out, std::vector<std::string> &problems, std::string &err) {
                return solaris_host::scanPlugins(solaris_host::pluginSearchPath(), out, problems, err);
            };
            host.loadPlugin = solaris_host::loadPlugin; // R-VST-6: each in a process of its own
            solaris::SolarisService svc(host);

            ntwb::Client::Options o;
            o.app = kAppId;
            o.version = kAppVersion;
            ntwb::Client client(o);
            // what the window's host gives the App beyond model + dispatch, and a browser cannot do for itself
            NtwbAdapter::Hooks hooks;
            hooks.listDir = solaris_host::listDir; // `browse`: the same lister the service's browser has
            hooks.audio = webAudio;                // R-SVC-7: `audio.listen` and the stream `audio`
            // R-VIDEO-1: two decoders, as the window has (linux_main.cpp: one for the Video window, one for the lane's thumbnails)
            struct Decoder
            {
                solaris_host::VideoReader reader;
                std::string opened;
            };
            auto frameDecoder = std::make_shared<Decoder>(), thumbDecoder = std::make_shared<Decoder>();
            auto frameFrom = [](const std::shared_ptr<Decoder> &d) {
                return [d](const std::string &file, double t, int bw, int bh, VideoPicture &out) {
                    std::string err;
                    if (file != d->opened)
                    {
                        d->opened.clear();
                        if (!d->reader.open(file, err)) return false;
                        d->opened = file;
                    }
                    solaris_host::VideoFrame f;
                    if (!d->reader.frame(t, bw, bh, f, err)) return false;
                    out.width = f.width;
                    out.height = f.height;
                    out.time = f.time;
                    out.rgba = std::move(f.rgba);
                    return true;
                };
            };
            hooks.videoFrame = frameFrom(frameDecoder);
            hooks.videoThumb = frameFrom(thumbDecoder);
#ifdef SOLARIS_HAVE_PIXBUF
            hooks.jpeg = encodeJpeg;
#endif
            NtwbAdapter adapter(svc, client, hooks);
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
