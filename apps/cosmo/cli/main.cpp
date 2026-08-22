/*
 *  cosmo-cc — cosmo with no window: front end #2 over CosmoService (R-SVC-1, S3).
 *
 *  Seam rule: THIS FILE HOLDS NO BEHAVIOUR. It owns argv, stdout, the clock, the codecs
 *  and the filesystem — the four things cosmo_core is forbidden to touch (R-SVC-7) — and
 *  nothing else. Every state change goes in as a `Command` (R-SVC-2) and everything
 *  printed comes out of `AppModel`/`Event` (R-SVC-3). If a subcommand here needs a
 *  behaviour no Command expresses, that is a defect in the command set, not a licence to
 *  call `EditSession`.
 *
 *  Consequently this is not "a second cosmo without the GUI": it is the same service the
 *  window drives, and `cosmo-cc project X.cmp --print --stable` must dump byte-identical
 *  text to a GUI open of X.cmp (R-SVC-9). That equality is the whole point — the last time
 *  a load could not be run from a shell, both CPU-budget defects (D-11, D-12) had to be
 *  found by reading, and the log line that was supposed to prove the feature had never
 *  once been observed (D-6).
 *
 *  Three host-side jobs, all of them injected rather than imported by the core:
 *
 *    * the decoder      — GdkPixbuf + LibRaw live here, so cosmo_core stays codec-free;
 *    * the worker init  — `pinNestedOpenMPForThisThread()` on every decode thread. This is
 *                         the D-12 fix (R-CPU-2c) and it is per-THREAD, so a front end
 *                         that forgets it silently takes the whole machine again;
 *    * the image writer — `export` renders and hands bytes over; encoding is ours.
 *
 *  There is no logging layer. Under R-SVC-5 an Event IS the log line, so `--watch` prints
 *  `formatEvent()` and that is the log — one text form, shared with the journal, the
 *  control socket and cosmo_v2.log.
 */
#include "ExportWriter.h"
#include "OmpPin.h"
#include "core/AppSettings.h"
#include "core/ProjectStore.h"
#include "PinnedDecoder.h"
#include "core/service/AppModelCodec.h"
#include "core/service/CosmoService.h"
#include "base/Parallel.h"
#include "engine/Apf.h"
#include "engine/EditEngine.h"
#include "engine/EditParamsApf.h"
#include "engine/EditParamsIO.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#ifndef _WIN32
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <process.h>
#define COSMO_CC_PID _getpid()
#else
#include <unistd.h>
#define COSMO_CC_PID ::getpid()
#endif

using arstro::cosmo::AppModel;
using arstro::cosmo::AppSettings;
using arstro::cosmo::Command;
using arstro::cosmo::CosmoService;
using arstro::cosmo::DecodedImage;
using arstro::cosmo::EditSession;
using arstro::cosmo::Event;
using arstro::cosmo::ModelDumpOptions;
using arstro::cosmo_v2::PinnedDecoder;   // R-CPU-2c / D-41: the host has ONE decoder, and it pins its thread
using arstro::cosmo::ThreadBudget;

namespace
{
    // A1: 0 ok / 1 the thing failed / 2 the command line was wrong. Kept apart because a
    // script that cannot tell "no such flag" from "that project is broken" cannot branch.
    constexpr int kOk = 0, kFail = 1, kUsage = 2;

    int fail(const std::string &why)
    {
        std::cerr << "cosmo-cc: " << why << "\n";
        return kFail;
    }

    // ── argv ──────────────────────────────────────────────────────────────────────────
    //
    // Deliberately NOT the Command grammar's parser: argv is the shell's shape (`--flag
    // value`, `-o out.png`, repeated `--set`), the grammar's is a line of text. The two
    // meet in `run`, which hands whole lines to `parseCommand` — the one parser (R-SVC-5).

    struct Args
    {
        std::string verb;
        std::vector<std::string> positional;                        // verb excluded
        std::vector<std::pair<std::string, std::string>> flags;     // repeats preserved

        bool has(const std::string &k) const
        {
            for (const auto &f : flags) if (f.first == k) return true;
            return false;
        }
        std::string value(const std::string &k, const std::string &fallback = std::string()) const
        {
            for (const auto &f : flags) if (f.first == k) return f.second;
            return fallback;
        }
        std::vector<std::string> values(const std::string &k) const
        {
            std::vector<std::string> out;
            for (const auto &f : flags) if (f.first == k) out.push_back(f.second);
            return out;
        }
        int intValue(const std::string &k, int fallback) const
        {
            const std::string v = value(k);
            return v.empty() ? fallback : std::atoi(v.c_str());
        }
        const std::string &at(size_t i) const
        {
            static const std::string empty;
            return i < positional.size() ? positional[i] : empty;
        }
    };

    /** `--k v`, `--k=v`, a bare `--k` (-> "1"), `-o v`, and `-` as a positional (stdin).
     *  A bare `k=v` token also becomes a `set` field, so `--set exposure=1.2 temp=7000`
     *  reads the way a shell user expects without a second `--set`. */
    Args parseArgs(int argc, char **argv)
    {
        Args a;
        for (int i = 1; i < argc; ++i)
        {
            std::string t = argv[i];
            auto takesValue = [&](const std::string &key) {
                const bool next = i + 1 < argc && argv[i + 1][0] != '-';
                a.flags.emplace_back(key, next ? argv[++i] : "1");
            };
            if (t == "-o") { takesValue("o"); continue; }
            if (t.rfind("--", 0) == 0)
            {
                t = t.substr(2);
                const auto eq = t.find('=');
                if (eq != std::string::npos) a.flags.emplace_back(t.substr(0, eq), t.substr(eq + 1));
                else takesValue(t);
                continue;
            }
            if (t != "-" && t.find('=') != std::string::npos) { a.flags.emplace_back("set", t); continue; }
            if (a.verb.empty()) a.verb = t;
            else a.positional.push_back(t);
        }
        return a;
    }

    // ── the one clock, and the one host wiring ────────────────────────────────────────

    double wallMs()
    {
        using namespace std::chrono;
        static const steady_clock::time_point t0 = steady_clock::now();
        return duration<double, std::milli>(steady_clock::now() - t0).count();
    }

    /** What `export`'s fields resolved to for the batch now running. The service hands the
     *  writer bytes and a suggested path only (R-SVC-7), so format/quality/size live on
     *  this side, where the codec is. Filled from the Command's own fields, never from a
     *  second parse of the line. */
    struct WriteOptions
    {
        std::string format = "PNG";   // ExportDialog::Request's spelling: JPEG | PNG | TIFF
        int quality = 92;
        int longEdge = 0;             // 0 = full resolution
        std::string forcePath;        // `render -o`: one exact file, ignore the suggested name
        bool metadata = true;         // EXIF + sRGB where the container carries them (R-EXPORT-5)
    };

    struct Options
    {
        bool json = false;
        bool watch = false;
        bool serial = false;
        bool stable = false;
        bool params = false;
    };

    struct Host
    {
        // S4c: the service owns the session; a front end constructs the budget, then the
        // service, and holds nothing else.
        ThreadBudget budget;
        CosmoService svc{budget};

        Options opt;
        WriteOptions write;
        double tickMs = 0.0;          // the tick handed to pump()
        double encodeMs = 0.0;        // time the writer spent encoding (bench splits on it)
        int written = 0, failures = 0;
        std::vector<std::string> outputs;
    };

    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    }
    std::string extensionOf(const std::string &p)
    {
        return lower(std::filesystem::path(p).extension().string());
    }
    /** `<dir>/<stem><ext>` — the service suggests `<outdir>/<source name>`, and only the
     *  host knows which container it is about to encode into. */
    std::string withExtension(const std::string &path, const std::string &ext)
    {
        std::filesystem::path p(path);
        p.replace_extension(ext);
        return p.string();
    }

    /** The service suggests `<outdir>/<display name>`, and a project reloaded from a `.cmp`
     *  has EMPTY image display names — `saveWorkspaceAs` writes `path=` for an `#image` but
     *  no `name=`, so the pending leaf, and then `nameForSlot`, come back blank. Left alone
     *  that makes every image in a batch resolve to the same dotfile. The Export modal never
     *  hit it because `exporter::resolvePath` falls back to the SOURCE file's stem, so this
     *  does the same thing — path resolution has always been the host's job. See the report:
     *  the empty name itself is a load-path defect, and fixing it is a core change. */
    std::string nameableOutPath(const std::string &suggested, const std::string &sourcePath, int nth)
    {
        std::filesystem::path p(suggested);
        if (!p.filename().empty() && !p.stem().empty()) return suggested;
        std::string stem = std::filesystem::path(sourcePath).stem().string();
        if (stem.empty()) stem = "image-" + std::to_string(nth + 1);
        return (p.parent_path() / stem).string();
    }

    /** JPEG|PNG|TIFF from anything a user might type (`jpg`, `.JPEG`, `tif`). */
    std::string canonicalFormat(const std::string &raw, const std::string &fallback)
    {
        const std::string f = lower(raw);
        if (f.empty()) return fallback;
        if (f == "jpg" || f == "jpeg" || f == ".jpg" || f == ".jpeg") return "JPEG";
        if (f == "png" || f == ".png") return "PNG";
        if (f == "tif" || f == "tiff" || f == ".tif" || f == ".tiff") return "TIFF";
        return std::string();
    }
    std::string extensionFor(const std::string &format)
    {
        return format == "PNG" ? ".png" : format == "TIFF" ? ".tif" : ".jpg";
    }

    void wire(Host &h)
    {
        // R-SVC-7: the codecs live on this side of the seam, so cosmo_core carries none.
        h.svc.setDecoderFactory([] {
            return arstro::cosmo_v2::makePinnedDecoder();
        });
        // R-CPU-2c / D-12: per-THREAD, on the thread, because the OpenMP thread count is a
        // per-thread ICV. A front end that drops this pin takes the whole machine no matter
        // how the pool was sized — which is exactly how the last version of the CPU budget
        // came to be "verified" by a log line nobody had seen.
        h.svc.setWorkerInit([] { arstro::cosmo_v2::pinNestedOpenMPForThisThread(); });

        h.svc.setImageWriter([&h](const std::string &outPath, const std::string &sourcePath,
                                  const uint8_t *rgba, int w, int h2, std::string &err) {
            // Reuses the GUI's encoder verbatim (R-EXPORT-4/5) so a headless export and the
            // Export modal cannot diverge on quality, the long-edge cap, EXIF or the
            // colour profile. Only the destination differs, and that is the host's call.
            arstro::cosmo_v2::ExportDialog::Request req;
            req.sameAsSource = false;
            req.format = h.write.format;
            req.quality = h.write.quality;
            req.longEdge = h.write.longEdge;
            req.embedExif = h.write.metadata;
            req.embedProfile = h.write.metadata;

            const std::string path = h.write.forcePath.empty()
                                         ? withExtension(nameableOutPath(outPath, sourcePath, h.written),
                                                         extensionFor(h.write.format))
                                         : h.write.forcePath;
            std::error_code ec;
            const std::filesystem::path dir = std::filesystem::path(path).parent_path();
            if (!dir.empty()) std::filesystem::create_directories(dir, ec);

            const double t0 = wallMs();
            const bool ok = arstro::cosmo_v2::exporter::write(req, rgba, w, h2, path, sourcePath, err);
            h.encodeMs += wallMs() - t0;
            if (ok) h.outputs.push_back(path);
            return ok;
        });

        if (h.opt.watch)
        {
            // R-SVC-5: the event stream IS the log. One line, one shape, whoever is reading.
            h.svc.subscribe([](const Event &e) { std::cout << arstro::cosmo::formatEvent(e) << "\n"; });
        }
        h.svc.subscribe([&h](const Event &e) {
            if (e.kind == Event::Kind::ExportFinished) { h.written += e.a; h.failures += e.b; }
        });
    }

    /** The settings the GUI would be running under, put in force through the one way in
     *  (R-SVC-2) rather than poked into the session — otherwise the two front ends start
     *  from different state and R-SVC-9's equality check is meaningless. Nothing is written
     *  back: a CLI run must never edit the user's settings file. */
    bool applyStartupSettings(Host &h, const Args &a)
    {
        const AppSettings s = AppSettings::load();
        Command c;
        c.kind = Command::Kind::SettingsSet;
        c.fields = {{"cpuPercent", std::to_string(s.cpuPercent)},
                    {"threads", std::to_string(s.threads)},
                    {"previewEdge", std::to_string(s.previewEdge)},
                    {"useGpu", s.useGpu ? "1" : "0"}};
        // `--serial` is as deterministic as this stage of the migration can be: one decode
        // worker and one engine thread, and the GPU off. The real fix is ITaskPool
        // (R-SVC-7, piece 7), which does not exist yet — a synchronous pool is what makes a
        // run reproducible rather than merely narrow.
        if (h.opt.serial)
        {
            c.fields[0].second = "1";
            c.fields[3].second = "0";
        }
        if (a.has("preview-edge")) c.fields[2].second = a.value("preview-edge");
        if (a.has("gpu")) c.fields[3].second = "1";
        if (a.has("cpu")) c.fields[3].second = "0";
        if (!h.svc.dispatch(c)) return false;

        // Presets are looked up in a directory the session is told about, and there is no
        // Command for that yet — so `preset apply` from a script needs this one reach past
        // the service. TECHNICAL DEBT: it wants a `preset dir <path>` command (or a
        // settings key), and every use of svc.session() is a line S4 has to delete.
        std::string presets = a.value("presets");
        if (presets.empty())
        {
            // Matching the GUI's convention (`exeDir()/presets`), then its binary's dir one
            // level up — cosmo-cc lives in cli/ beside it, so without the second guess the
            // two front ends would read different preset libraries and `preset apply` would
            // find nothing the GUI had saved.
            std::error_code ec;
            const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
            if (!ec)
            {
                const std::filesystem::path own = exe.parent_path() / "presets";
                const std::filesystem::path up = exe.parent_path().parent_path() / "presets";
                presets = std::filesystem::exists(own) || !std::filesystem::exists(up) ? own.string()
                                                                                       : up.string();
            }
        }
        if (!presets.empty()) h.svc.session().setPresetDir(presets);
        return true;
    }

    // ── driving the service (R-SVC-6: the host owns the loop) ─────────────────────────

    /** One pump. The tick is a fixed 16 ms step rather than the wall clock so a scripted
     *  run is reproducible; deadlines below use the real clock, because a timeout is about
     *  the machine, not about the model. */
    void pumpOnce(Host &h)
    {
        h.svc.pump(h.tickMs);
        h.tickMs += 16.0;
    }

    /** Spin until the running load is finished or `timeoutMs` elapses. Never blocks inside
     *  the service — this is exactly the loop a GTK timeout replaces. */
    bool pumpUntilIdle(Host &h, int timeoutMs = 300000)
    {
        const double deadline = wallMs() + timeoutMs;
        for (;;)
        {
            pumpOnce(h);
            if (!h.svc.model().load.active) return true;
            if (wallMs() > deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    /** `cmd` is the `state print` that asked, when there was one. Its per-command options win
     *  over the global flags — D-18: the options were read ONLY from the global argv, so
     *  `state print --params` inside a script parsed fine and was then ignored, which is the
     *  same failure as D-14 one layer up. An option a caller wrote and the tool dropped is
     *  worse than one that does not exist. Global flags remain the default for every dump. */
    void printModel(const Host &h, bool json, const Command *cmd = nullptr)
    {
        ModelDumpOptions o;
        o.json = json;
        o.stable = h.opt.stable || (cmd && cmd->field("stable") == "1");
        o.params = h.opt.params || (cmd && cmd->field("params") == "1");
        std::cout << arstro::cosmo::formatModel(h.svc.model(), o);
    }

    /** Dispatch and report. The model already carries why a rejection happened, so there is
     *  no second error channel to keep in sync. */
    bool run(Host &h, const Command &c)
    {
        if (h.svc.dispatch(c)) return true;
        std::cerr << "cosmo-cc: rejected: " << arstro::cosmo::formatCommand(c) << ": "
                  << h.svc.model().lastError << "\n";
        return false;
    }

    Command setCommand(const std::vector<std::string> &kvs)
    {
        Command c;
        c.kind = Command::Kind::Set;
        for (const std::string &kv : kvs)
        {
            const auto eq = kv.find('=');
            if (eq == std::string::npos || eq == 0) continue;
            c.fields.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        }
        return c;
    }

    // ── A1 · --help ───────────────────────────────────────────────────────────────────

    /** Argument shapes for the grammar `commandNames()` returns. The names themselves come
     *  from the codec so help can never list a command that does not exist, or miss one
     *  that does (R-SVC-5/9); only the human hint lives here, which is a view concern. */
    const std::map<std::string, std::string> &grammarHints()
    {
        static const std::map<std::string, std::string> hints = {
            {"project open", "<path.cmp>"},
            {"project new", "<path.cmp>"},
            {"project save", "[path.cmp]"},
            {"project close", ""},
            {"import", "<img> [img ...]"},
            {"select", "<node>"},
            {"select next", ""},
            {"select prev", ""},
            {"set", "<key>=<value> [key=value ...]"},
            {"bypass", "<node> on|off"},
            {"group new", "[\"name\"]"},
            {"group ungroup", "<node>"},
            {"undo", ""},
            {"redo", ""},
            {"preset apply", "\"<name>\""},
            {"preset save", "\"<name>\""},
            {"export", "--outdir <D> [--format jpg|png|tiff] [--quality N] [--long-edge N]"},
            {"settings set", "cpuPercent=N previewEdge=N threads=N useGpu=0|1"},
            {"screen", "home|editor"},
            {"state print", "[--json]"},
            {"wait", "load.finished|export.finished|quit|<ms> [--timeout 120s]  (hyphens accepted)"},
            {"quit", ""}};
        return hints;
    }

    int usage(int code)
    {
        std::ostream &o = code == kOk ? std::cout : std::cerr;
        o << "cosmo-cc — cosmo with no window: the CLI front end over CosmoService (R-SVC-1)\n"
             "\n"
             "  cosmo-cc <subcommand> [options]\n"
             "\n"
             "  info <file>                     decode one image; print size, RAW-or-not, decoder, ms\n"
             "  backends                        threads, CPU budget, GPU backend, LibRaw, build defines\n"
             "  project <f.cmp> --print         open a project through the service and dump the model\n"
             "        [--node-params]           plus every node's own params, walked by `select`\n"
             "  render <img> -o <out>           render one image and write it\n"
             "        [--set k=v ...] [--preview-edge N] [--gpu|--cpu] [--long-edge N]\n"
             "  export <f.cmp> --outdir <D>     batch export, down the same path the modal drives\n"
             "        [--format jpg|png|tiff] [--quality N] [--long-edge N]\n"
             "  params --print <f>              serialize the params in a .cosmo / .cmp / .apf\n"
             "  params --diff <a> <b>           field-level diff of two of them\n"
             "  check <f.cmp|f.cosmo|f.apf|settings.txt>\n"
             "                                  validate only; exit 1 with the first error located\n"
             "  bench <img> [--iters N]         per-stage ms, so a perf claim is measured\n"
             "  run [--script <f>] [-]          replay command lines from a file or stdin\n"
             "  attach <socket> [--script <f>]   drive a RUNNING cosmo --control window; its\n"
             "                                  events stream back (R-SVC-8). No session here:\n"
             "                                  the window that owns the service does the work\n"
             "\n"
             "  global   --json      machine-readable output where it makes sense\n"
             "           --watch     stream every Event as it happens (R-SVC-5: this is the log)\n"
             "           --serial    one decode worker, one engine thread, GPU off\n"
             "           --stable    omit revision/frameSeq/peak from a model dump, so a CLI dump\n"
             "                       and a GUI dump of the same project compare equal (R-SVC-9)\n"
             "           --params    include the full EditParams block in a model dump\n"
             "           --presets <dir>  where .apf presets are looked up\n"
             "\n"
             "  exit 0 ok · 1 failure · 2 usage\n"
             "\n"
             "The command grammar — one parser, shared by `run`, `--script` files, the control\n"
             "socket and the journal (R-SVC-5):\n"
             "\n";
        for (const std::string &name : arstro::cosmo::commandNames())
        {
            const auto it = grammarHints().find(name);
            o << "    " << name;
            if (it != grammarHints().end() && !it->second.empty()) o << ' ' << it->second;
            o << '\n';
        }
        o << "\n"
             "`state print` and `wait` are handled by the front end, not the service: only the\n"
             "caller knows where to print and only the caller owns the loop a wait would spin.\n";
        return code;
    }

    // ── A2 · info ─────────────────────────────────────────────────────────────────────

    int cmdInfo(const Args &a, const Options &opt)
    {
        const std::string path = a.at(0);
        if (path.empty()) return usage(kUsage);

        // One decode, on this thread, with no pool around it — so it gets the whole budget
        // and stays inside it (R-CPU-2c / D-41). Before the pin reached this path it took
        // 4.6 cores of 16 at cpuPercent=25 and 4.5 at 100%: the setting did nothing to it.
        PinnedDecoder dec;
        ThreadBudget budget(AppSettings::load().cpuPercent, 0);
        dec.setBudget(&budget);
        const double t0 = wallMs();
        const DecodedImage img = dec.decodeFile(path);
        const double ms = wallMs() - t0;

        const bool raw = PinnedDecoder::isRawExtension(path);
        // COSMO_HAVE_LIBRAW is PRIVATE to cosmo_core, so it is not visible in this TU on
        // purpose: asking the decoder is the only answer that cannot go stale.
        const char *used = raw ? (PinnedDecoder::rawSupported() ? "LibRaw" : "none")
                               : "GdkPixbuf";
        std::error_code ec;
        const auto bytes = std::filesystem::file_size(path, ec);

        if (opt.json)
        {
            std::cout << "{\n"
                      << "  \"file\": \"" << path << "\",\n"
                      << "  \"ok\": " << (img.ok() ? "true" : "false") << ",\n"
                      << "  \"width\": " << img.width << ",\n"
                      << "  \"height\": " << img.height << ",\n"
                      << "  \"megapixels\": " << (img.width * (double)img.height / 1e6) << ",\n"
                      << "  \"raw\": " << (raw ? "true" : "false") << ",\n"
                      << "  \"rawSupported\": " << (PinnedDecoder::rawSupported() ? "true" : "false") << ",\n"
                      << "  \"decoder\": \"" << used << "\",\n"
                      << "  \"fileBytes\": " << (ec ? 0 : (long long)bytes) << ",\n"
                      << "  \"ompPinnedThreads\": " << arstro::cosmo_v2::ompPinnedThreadCount() << ",\n"
                      << "  \"ms\": " << ms << "\n}\n";
        }
        else
        {
            std::cout << "file=" << path << '\n'
                      << "ok=" << (img.ok() ? 1 : 0) << '\n'
                      << "width=" << img.width << " height=" << img.height << '\n'
                      << "megapixels=" << (img.width * (double)img.height / 1e6) << '\n'
                      << "raw=" << (raw ? 1 : 0) << " rawSupported=" << (PinnedDecoder::rawSupported() ? 1 : 0) << '\n'
                      << "decoder=" << used << '\n'
                      << "fileBytes=" << (ec ? 0 : (long long)bytes) << '\n'
                      // R-CPU-4 as amended, and the one line that makes D-41 checkable from the
                      // command that found it: `info` decodes on its own thread, outside any
                      // pool, so a 1 here says that thread WAS pinned. It read 0 before D-41.
                      << "ompPinnedThreads=" << arstro::cosmo_v2::ompPinnedThreadCount() << '\n'
                      << "ms=" << ms << '\n';
        }
        if (!img.ok()) return fail("could not decode " + path);
        return kOk;
    }

    // ── A7 · backends ─────────────────────────────────────────────────────────────────

    int cmdBackends(const Args &a, const Options &opt)
    {
        // No EditSession here: an engine of its own is enough to answer, and constructing a
        // session would build a SECOND GPU context just to read its name back.
        arstro::EditEngine engine;
        const char *cpuName = engine.activeBackendName();
        engine.setPreferGpu(true);
        const char *gpuName = engine.activeBackendName();
        const bool gpu = engine.gpuAvailable();

        ThreadBudget budget(AppSettings::load().cpuPercent, 0);
        const unsigned hc = std::thread::hardware_concurrency();

        std::vector<std::string> defines;
#ifdef ARSTRO_ENABLE_THREADS
        defines.push_back("ARSTRO_ENABLE_THREADS");
#endif
#ifdef ARSTRO_GL_COMPUTE
        defines.push_back("ARSTRO_GL_COMPUTE");
#endif
#ifdef ARSTRO_GLES_COMPUTE
        defines.push_back("ARSTRO_GLES_COMPUTE");
#endif
        std::string defs;
        for (const std::string &d : defines) { if (!defs.empty()) defs += ","; defs += d; }
        if (defs.empty()) defs = "-";

        if (opt.json)
        {
            std::cout << "{\n"
                      << "  \"cores\": " << budget.cores() << ",\n"
                      << "  \"hardwareConcurrency\": " << hc << ",\n"
                      << "  \"engineThreadsNow\": " << arstro::par::threads() << ",\n"
                      << "  \"budgetPercent\": " << budget.percent() << ",\n"
                      << "  \"budgetTotal\": " << budget.total() << ",\n"
                      << "  \"budgetEngineThreads\": " << budget.engineThreads() << ",\n"
                      << "  \"budgetDecodeWorkers\": " << budget.decodeWorkers() << ",\n"
                      << "  \"maxDecodeWorkers\": " << ThreadBudget::kMaxDecodeWorkers << ",\n"
                      << "  \"engineFloor\": " << ThreadBudget::kEngineFloor << ",\n"
                      << "  \"gpuAvailable\": " << (gpu ? "true" : "false") << ",\n"
                      << "  \"backendCpu\": \"" << cpuName << "\",\n"
                      << "  \"backendGpu\": \"" << gpuName << "\",\n"
                      << "  \"libraw\": " << (PinnedDecoder::rawSupported() ? "true" : "false") << ",\n"
                      << "  \"ompPin\": \"" << arstro::cosmo_v2::ompPinStatus() << "\",\n"
                      << "  \"ompPinUserOverride\": " << arstro::cosmo_v2::ompPinUserOverride() << ",\n"
                      << "  \"ompPinnedThreads\": " << arstro::cosmo_v2::ompPinnedThreadCount() << ",\n"
                      << "  \"defines\": \"" << defs << "\"\n}\n";
        }
        else
        {
            std::cout << "cores=" << budget.cores() << " hardwareConcurrency=" << hc << '\n'
                      << "engineThreadsNow=" << arstro::par::threads() << "  (par::threads, after ThreadBudget::apply)\n"
                      // R-SVC-10 / R-CPU-4: printed rather than inferred, because the last
                      // version of this feature was only ever claimed.
                      << "budget percent=" << budget.percent() << " total=" << budget.total()
                      << " of " << budget.cores() << " cores"
                      << " engine=" << budget.engineThreads()
                      << " decode=" << budget.decodeWorkers()
                      << " (cap " << ThreadBudget::kMaxDecodeWorkers
                      << ", engine floor " << ThreadBudget::kEngineFloor << ")\n"
                      << "gpuAvailable=" << (gpu ? 1 : 0) << '\n'
                      << "backend cpu=" << cpuName << " gpu=" << gpuName << '\n'
                      << "libraw=" << (PinnedDecoder::rawSupported() ? 1 : 0) << '\n'
                      << "ompPin=" << arstro::cosmo_v2::ompPinStatus() << '\n'
                      // R-CPU-4 as amended: how many decoding threads the pin ACTUALLY bound,
                      // not how many it was supposed to. `backends` decodes nothing, so 0 here
                      // is correct and the number is only interesting after a run that did.
                      << "ompPinnedThreads=" << arstro::cosmo_v2::ompPinnedThreadCount() << '\n'
                      << "defines=" << defs << '\n'
                      << "configDir=" << arstro::cosmo::ProjectStore::configDir() << '\n';
        }
        (void)a;
        return kOk;
    }

    // ── A4 · project --print ──────────────────────────────────────────────────────────

    /** Per-node params, the only way a front end is allowed to ask for them: select the
     *  node, read the model back. The selection is put back afterwards, so a dump is never
     *  itself a state change. (An `AppModel::nodes[].params` field would turn this whole
     *  walk into one read — see the report; that is a core change, not a CLI one.)
     *
     *  Pending and failed leaves are skipped: they own no engine slot, so selecting one
     *  leaves the edit target where it was and the params printed under its name would be
     *  the PREVIOUS node's — a wrong answer is worse than an absent one. */
    void printPerNodeParams(Host &h)
    {
        struct Row { int node; bool group; int slot; std::string name; };
        std::vector<Row> rows;
        for (const auto &n : h.svc.model().nodes) rows.push_back({n.node, n.group, n.slot, n.name});
        const int was = h.svc.model().selectedNode;

        for (const Row &r : rows)
        {
            if (!r.group && r.slot < 0) continue;
            Command c;
            c.kind = Command::Kind::Select;
            c.index = r.node;
            if (!h.svc.dispatch(c)) continue;
            std::cout << "params node=" << r.node << " kind=" << (r.group ? "group" : "image")
                      << " name=" << r.name << '\n'
                      << arstro::serializeParams(h.svc.model().params);
        }
        if (was >= 0)
        {
            Command c;
            c.kind = Command::Kind::Select;
            c.index = was;
            h.svc.dispatch(c);
        }
    }

    int cmdProject(Host &h, const Args &a)
    {
        const std::string path = a.at(0);
        if (path.empty()) return usage(kUsage);

        Command open;
        open.kind = Command::Kind::ProjectOpen;
        open.path = path;
        if (!run(h, open)) return kFail;
        if (!pumpUntilIdle(h, a.intValue("timeout", 300) * 1000)) return fail("load did not finish in time");

        // Nothing but the dump goes to stdout: `cosmo-cc project X --print --stable` has to
        // compare byte-for-byte against a GUI `state print --stable` of the same project
        // (R-SVC-9), so a summary line here would break the only check that keeps the two
        // front ends honest.
        printModel(h, h.opt.json);
        // `--node-params` is a SEPARATE flag from `--params` on purpose: `--params` is the
        // codec's own block and stays part of the comparable dump, while this walk is
        // CLI-only extra output. Text only — putting a walk of `select` dumps inside a JSON
        // object would mean this file deciding the shape, and the shape belongs to the codec.
        if (a.has("node-params") && !h.opt.json) printPerNodeParams(h);

        // A missing image is a normal, tolerated state (R-LOADUX-1: it reads as missing,
        // not as a stall) and it is visible in the dump as `kind=failed`. Judging a project
        // is `check`'s job, so this exits 0 whenever the open itself succeeded.
        return kOk;
    }

    // ── A3 · render ───────────────────────────────────────────────────────────────────

    /** A scratch project, because `import` needs one to import INTO and the grammar has no
     *  "just render this file" command — correctly so: cosmo edits projects, and a
     *  one-image render is a one-image project. It is deleted afterwards, and the recents
     *  entry it leaves behind self-cleans (ProjectStore drops entries whose .cmp is gone). */
    std::string scratchProjectPath()
    {
        std::error_code ec;
        const std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
        return (dir / ("cosmo-cc-" + std::to_string((long long)COSMO_CC_PID) + ".cmp")).string();
    }

    int cmdRender(Host &h, const Args &a)
    {
        const std::string img = a.at(0);
        const std::string out = a.value("o");
        if (img.empty() || out.empty()) return usage(kUsage);

        const std::string scratch = scratchProjectPath();
        Command make;
        make.kind = Command::Kind::ProjectNew;
        make.path = scratch;
        if (!run(h, make)) return kFail;

        Command imp;
        imp.kind = Command::Kind::Import;
        imp.paths.push_back(img);
        if (!run(h, imp)) return kFail;
        if (!pumpUntilIdle(h)) return fail("decode did not finish in time");
        if (h.svc.model().imageCount == 0)
        {
            std::filesystem::remove(scratch);
            return fail("could not decode " + img);
        }

        const std::vector<std::string> sets = a.values("set");
        if (!sets.empty() && !run(h, setCommand(sets))) return kFail;

        // The written file's container comes from the -o name, so `render -o x.jpg` means
        // JPEG without a second flag saying so.
        const std::string byName = canonicalFormat(extensionOf(out), "PNG");
        h.write.format = byName.empty() ? "PNG" : byName;
        h.write.quality = a.intValue("quality", 92);
        h.write.forcePath = out;
        // R-EXPORT-4 semantics: --preview-edge caps the WRITTEN image's long edge as well as
        // the service's preview size, so `render --preview-edge 1200` produces the 1200 px
        // frame the GUI would be showing rather than a full-res file of the same pixels.
        h.write.longEdge = a.intValue("long-edge", a.intValue("preview-edge", 0));

        Command exp;
        exp.kind = Command::Kind::Export;
        exp.path = std::filesystem::path(out).parent_path().string();
        if (exp.path.empty()) exp.path = ".";
        const bool ok = run(h, exp);

        std::error_code ec;
        std::filesystem::remove(scratch, ec);
        if (!ok || h.written == 0) return fail("nothing was written");

        const AppModel &m = h.svc.model();
        if (h.opt.json)
            std::cout << "{\n  \"out\": \"" << out << "\",\n  \"backend\": \""
                      << (m.gpuActive ? "gpu" : "cpu") << "\",\n  \"encodeMs\": " << h.encodeMs
                      << "\n}\n";
        else
            std::cout << "out=" << out << '\n'
                      << "backend=" << (m.gpuActive ? "gpu" : "cpu") << '\n'
                      << "encodeMs=" << h.encodeMs << '\n';
        return kOk;
    }

    // ── A5 · export ───────────────────────────────────────────────────────────────────

    /** The `export` Command's own fields decide the container, so a scripted `export
     *  --format png` and an argv `--format png` land in the same place. Read off the parsed
     *  Command — never off a second parse of the line. */
    bool applyExportOptions(Host &h, const Command &c)
    {
        const std::string fmt = canonicalFormat(c.field("format"), h.write.format);
        if (fmt.empty()) return false;
        h.write.format = fmt;
        if (!c.field("quality").empty()) h.write.quality = std::atoi(c.field("quality").c_str());
        if (!c.field("long-edge").empty()) h.write.longEdge = std::atoi(c.field("long-edge").c_str());
        h.write.forcePath.clear();
        return true;
    }

    int cmdExport(Host &h, const Args &a)
    {
        const std::string path = a.at(0);
        const std::string outdir = a.value("outdir");
        if (path.empty() || outdir.empty()) return usage(kUsage);

        Command open;
        open.kind = Command::Kind::ProjectOpen;
        open.path = path;
        if (!run(h, open)) return kFail;
        if (!pumpUntilIdle(h)) return fail("load did not finish in time");

        Command exp;
        exp.kind = Command::Kind::Export;
        exp.path = outdir;
        exp.fields.emplace_back("outdir", outdir);
        if (a.has("format")) exp.fields.emplace_back("format", a.value("format"));
        if (a.has("quality")) exp.fields.emplace_back("quality", a.value("quality"));
        if (a.has("long-edge")) exp.fields.emplace_back("long-edge", a.value("long-edge"));
        if (!applyExportOptions(h, exp)) return fail("unknown format: " + a.value("format"));

        const double t0 = wallMs();
        const bool ok = run(h, exp);
        const double ms = wallMs() - t0;

        if (h.opt.json)
        {
            std::cout << "{\n  \"written\": " << h.written << ",\n  \"failures\": " << h.failures
                      << ",\n  \"ms\": " << ms << ",\n  \"files\": [";
            for (size_t i = 0; i < h.outputs.size(); ++i)
                std::cout << (i ? ", " : "") << '"' << h.outputs[i] << '"';
            std::cout << "]\n}\n";
        }
        else
        {
            for (const std::string &f : h.outputs) std::cout << "wrote=" << f << '\n';
            std::cout << "written=" << h.written << " failures=" << h.failures << " ms=" << ms << '\n';
        }
        return ok && h.failures == 0 ? kOk : kFail;
    }

    // ── A6 · params ───────────────────────────────────────────────────────────────────

    std::string readFile(const std::string &path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f) return std::string();
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    /** One labelled param set out of any file cosmo persists params in. Every branch goes
     *  through the format's OWN reader, so this can never accept a different set of keys
     *  than the app does. */
    struct NamedParams
    {
        std::string label;
        arstro::EditParams params;
    };

    bool loadParams(const std::string &path, std::vector<NamedParams> &out, std::string &err)
    {
        const std::string ext = extensionOf(path);
        if (!std::filesystem::exists(path)) { err = "no such file: " + path; return false; }

        if (ext == ".apf")
        {
            arstro::apf::Document doc;
            if (!arstro::apf::parse(readFile(path), doc)) { err = path + ": not an .apf file"; return false; }
            if (doc.engine != arstro::apfImageEngine())
            {
                err = path + ": engine=" + doc.engine + ", expected " + arstro::apfImageEngine();
                return false;
            }
            NamedParams np;
            np.label = doc.name.empty() ? std::filesystem::path(path).stem().string() : doc.name;
            // Only the categories the file actually carries, on top of the defaults — the
            // same forward-compatible rule an apply uses.
            arstro::applyApfToEditParams(doc, arstro::apfPresentImageCategories(doc), np.params);
            out.push_back(std::move(np));
            return true;
        }
        if (ext == ".cmp" || ext == ".cosmoproj")
        {
            std::vector<EditSession::WorkspaceEntry> entries;
            if (!EditSession::readWorkspaceFile(path, entries)) { err = path + ": not a cosmo project"; return false; }
            for (size_t i = 0; i < entries.size(); ++i)
            {
                NamedParams np;
                np.label = (entries[i].group ? "group " : "image ") + std::to_string(i) + " " +
                           (entries[i].group ? entries[i].name : entries[i].imagePath);
                np.params = entries[i].params;
                out.push_back(std::move(np));
            }
            return true;
        }
        // .cosmo, and anything else that is a bare key=value params blob.
        std::string imagePath;
        arstro::EditParams p;
        if (EditSession::readSessionFile(path, imagePath, p))
        {
            out.push_back({imagePath.empty() ? path : imagePath, p});
            return true;
        }
        if (arstro::deserializeParams(readFile(path), p))
        {
            out.push_back({path, p});
            return true;
        }
        err = path + ": not a params file (.cosmo / .cmp / .apf / key=value)";
        return false;
    }

    /** Split a serialized blob into its key=value pairs, in file order. */
    std::vector<std::pair<std::string, std::string>> paramFields(const arstro::EditParams &p)
    {
        std::vector<std::pair<std::string, std::string>> out;
        std::istringstream in(arstro::serializeParams(p));
        std::string line;
        while (std::getline(in, line))
        {
            const auto eq = line.find('=');
            if (eq == std::string::npos || eq == 0) continue;
            out.emplace_back(line.substr(0, eq), line.substr(eq + 1));
        }
        return out;
    }

    int cmdParams(const Args &a, const Options &opt)
    {
        if (a.has("print"))
        {
            const std::string path = a.value("print") == "1" ? a.at(0) : a.value("print");
            if (path.empty()) return usage(kUsage);
            std::vector<NamedParams> sets;
            std::string err;
            if (!loadParams(path, sets, err)) return fail(err);
            for (const NamedParams &np : sets)
            {
                if (opt.json)
                {
                    std::cout << "{\n  \"label\": \"" << np.label << "\",\n  \"fields\": {\n";
                    const auto f = paramFields(np.params);
                    for (size_t i = 0; i < f.size(); ++i)
                        std::cout << "    \"" << f[i].first << "\": \"" << f[i].second << '"'
                                  << (i + 1 < f.size() ? "," : "") << '\n';
                    std::cout << "  }\n}\n";
                }
                else
                    std::cout << "# " << np.label << '\n' << arstro::serializeParams(np.params);
            }
            return kOk;
        }

        if (a.has("diff"))
        {
            // `--diff a b`: the flag swallows the first path, the second is positional.
            const std::string left = a.value("diff") == "1" ? a.at(0) : a.value("diff");
            const std::string right = a.value("diff") == "1" ? a.at(1) : a.at(0);
            if (left.empty() || right.empty()) return usage(kUsage);

            std::vector<NamedParams> l, r;
            std::string err;
            if (!loadParams(left, l, err) || !loadParams(right, r, err)) return fail(err);
            if (l.empty() || r.empty()) return fail("nothing to compare");

            // Field-level, not blob-level: "which param changed" has to be one command, and
            // a diff of two serialized blobs answers "something did".
            const auto lf = paramFields(l.front().params), rf = paramFields(r.front().params);
            std::vector<std::string> lines;
            for (const auto &kv : lf)
            {
                std::string other;
                bool found = false;
                for (const auto &o : rf) if (o.first == kv.first) { other = o.second; found = true; break; }
                if (!found) lines.push_back(kv.first + ": " + kv.second + " -> (absent)");
                else if (other != kv.second) lines.push_back(kv.first + ": " + kv.second + " -> " + other);
            }
            for (const auto &kv : rf)
            {
                bool found = false;
                for (const auto &o : lf) if (o.first == kv.first) { found = true; break; }
                if (!found) lines.push_back(kv.first + ": (absent) -> " + kv.second);
            }

            if (opt.json)
            {
                std::cout << "{\n  \"a\": \"" << left << "\",\n  \"b\": \"" << right
                          << "\",\n  \"changed\": " << lines.size() << ",\n  \"fields\": [";
                for (size_t i = 0; i < lines.size(); ++i)
                    std::cout << (i ? ", " : "") << '"' << lines[i] << '"';
                std::cout << "]\n}\n";
            }
            else
            {
                std::cout << "--- " << left << "\n+++ " << right << '\n';
                for (const std::string &s : lines) std::cout << "  " << s << '\n';
                std::cout << "changed=" << lines.size() << '\n';
            }
            return kOk;
        }
        return usage(kUsage);
    }

    // ── A8 · check ────────────────────────────────────────────────────────────────────

    /** The 1-based line of the first line containing `needle`, or 0. A validator that says
     *  "an image is missing" and not WHICH LINE is a validator you still have to grep after. */
    int lineOf(const std::string &path, const std::string &needle)
    {
        std::ifstream f(path);
        std::string line;
        for (int n = 1; std::getline(f, line); ++n)
            if (line.find(needle) != std::string::npos) return n;
        return 0;
    }

    struct CheckError
    {
        int line = 0;
        std::string what;
    };

    bool checkProject(const std::string &path, CheckError &e)
    {
        std::vector<EditSession::WorkspaceEntry> entries;
        if (!EditSession::readWorkspaceFile(path, entries)) { e.what = "not a cosmo project"; return false; }
        if (entries.empty()) { e.what = "project has no entries"; return false; }
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto &en = entries[i];
            // A forward or self reference silently reparents to root at load time, which
            // reads as "the tree came back wrong" rather than as a broken file.
            if (en.parent >= (int)i)
            {
                e.line = lineOf(path, "parent=" + std::to_string(en.parent));
                e.what = "entry " + std::to_string(i) + ": parent=" + std::to_string(en.parent) +
                         " is not an earlier entry";
                return false;
            }
            if (!en.group && !std::filesystem::exists(en.imagePath))
            {
                e.line = lineOf(path, "path=" + en.imagePath);
                e.what = "entry " + std::to_string(i) + ": missing image " + en.imagePath;
                return false;
            }
        }
        return true;
    }

    bool checkPreset(const std::string &path, CheckError &e)
    {
        arstro::apf::Document doc;
        if (!arstro::apf::parse(readFile(path), doc)) { e.line = 1; e.what = "not an .apf file"; return false; }
        if (doc.engine != arstro::apfImageEngine())
        {
            e.line = lineOf(path, "engine=");
            e.what = "engine=" + doc.engine + ", expected " + arstro::apfImageEngine();
            return false;
        }
        if (arstro::apfPresentImageCategories(doc).empty())
        {
            e.what = "no category this engine understands";
            return false;
        }
        return true;
    }

    bool checkSession(const std::string &path, CheckError &e)
    {
        std::string imagePath;
        arstro::EditParams p;
        if (!EditSession::readSessionFile(path, imagePath, p)) { e.what = "not a .cosmo session"; return false; }
        if (!imagePath.empty() && !std::filesystem::exists(imagePath))
        {
            e.line = lineOf(path, imagePath);
            e.what = "missing image " + imagePath;
            return false;
        }
        return true;
    }

    /** settings.txt is validated against the SAME keys and ranges AppSettings::load()
     *  enforces — but loudly. `load()` must never refuse to start the app, so it silently
     *  substitutes a default; a linter exists to say what the file actually got wrong. */
    bool checkSettings(const std::string &path, CheckError &e)
    {
        std::ifstream f(path);
        if (!f) { e.what = "cannot read"; return false; }
        std::string line;
        for (int n = 1; std::getline(f, line); ++n)
        {
            if (line.empty() || line[0] == '#' || line == "cosmosettings=1") continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos) { e.line = n; e.what = "not a key=value: " + line; return false; }
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            const int i = std::atoi(v.c_str());
            if (k == "previewEdge" && i < 64) { e.line = n; e.what = "previewEdge must be >= 64"; return false; }
            else if (k == "cpuPercent" && (i < 1 || i > 100)) { e.line = n; e.what = "cpuPercent must be 1..100 (R-CPU-1)"; return false; }
            else if (k == "threads" && i < 0) { e.line = n; e.what = "threads must be >= 0 (0 = auto)"; return false; }
            else if (k == "useGpu" && v != "0" && v != "1") { e.line = n; e.what = "useGpu must be 0 or 1"; return false; }
            else if (k != "previewEdge" && k != "cpuPercent" && k != "threads" && k != "useGpu" &&
                     k != "cosmosettings")
            { e.line = n; e.what = "unknown key: " + k; return false; }
        }
        return true;
    }

    int cmdCheck(const Args &a, const Options &opt)
    {
        const std::string path = a.at(0);
        if (path.empty()) return usage(kUsage);
        if (!std::filesystem::exists(path)) return fail("no such file: " + path);

        const std::string ext = extensionOf(path);
        const std::string base = std::filesystem::path(path).filename().string();
        CheckError e;
        bool ok = false;
        std::string kind;
        if (ext == ".cmp" || ext == ".cosmoproj") { kind = "project"; ok = checkProject(path, e); }
        else if (ext == ".apf") { kind = "preset"; ok = checkPreset(path, e); }
        else if (ext == ".cosmo") { kind = "session"; ok = checkSession(path, e); }
        else if (base == "settings.txt" || ext == ".txt") { kind = "settings"; ok = checkSettings(path, e); }
        else return fail("do not know how to check " + base + " (.cmp / .cosmo / .apf / settings.txt)");

        if (opt.json)
            std::cout << "{\n  \"file\": \"" << path << "\",\n  \"kind\": \"" << kind
                      << "\",\n  \"ok\": " << (ok ? "true" : "false")
                      << ",\n  \"line\": " << e.line << ",\n  \"error\": \"" << e.what << "\"\n}\n";
        else if (ok)
            std::cout << path << ": ok (" << kind << ")\n";
        else
            std::cerr << path << ':' << (e.line ? std::to_string(e.line) : "-") << ": " << e.what << '\n';
        return ok ? kOk : kFail;
    }

    // ── A9 · bench ────────────────────────────────────────────────────────────────────

    void printStage(bool json, bool last, const char *name, double mean, int iters)
    {
        if (json)
            std::cout << "    {\"stage\": \"" << name << "\", \"meanMs\": " << mean
                      << ", \"iters\": " << iters << "}" << (last ? "\n" : ",\n");
        else
            std::cout << "stage=" << name << " meanMs=" << mean << " iters=" << iters << '\n';
    }

    int cmdBench(Host &h, const Args &a)
    {
        const std::string img = a.at(0);
        if (img.empty()) return usage(kUsage);
        const int iters = std::max(1, a.intValue("iters", 3));

        // ── decode: the host's own seam, timed directly ──
        // Budgeted like `info`: a bench that decoded outside the budget would report a
        // number the app can never reproduce (R-CPU-2c / D-41).
        PinnedDecoder dec;
        ThreadBudget benchBudget(AppSettings::load().cpuPercent, 0);
        dec.setBudget(&benchBudget);
        double decodeMs = 0;
        int w = 0, hh = 0;
        for (int i = 0; i < iters; ++i)
        {
            const double t0 = wallMs();
            const DecodedImage d = dec.decodeFile(img);
            decodeMs += wallMs() - t0;
            if (!d.ok()) return fail("could not decode " + img);
            w = d.width; hh = d.height;
        }

        // ── load: the whole project pipeline, pool and all, for one image ──
        const std::string scratch = scratchProjectPath();
        Command make;
        make.kind = Command::Kind::ProjectNew;
        make.path = scratch;
        if (!run(h, make)) return kFail;
        Command imp;
        imp.kind = Command::Kind::Import;
        imp.paths.push_back(img);
        const double tLoad = wallMs();
        if (!run(h, imp)) return kFail;
        if (!pumpUntilIdle(h)) return fail("decode did not finish in time");
        const double loadMs = wallMs() - tLoad;

        // ── render + encode: one export per iteration, split on the writer's own clock ──
        std::error_code ec;
        const std::filesystem::path outDir = std::filesystem::temp_directory_path(ec) / "cosmo-cc-bench";
        std::filesystem::create_directories(outDir, ec);
        h.write.format = "PNG";
        h.write.longEdge = a.intValue("long-edge", 0);
        h.encodeMs = 0;
        double exportMs = 0;
        for (int i = 0; i < iters; ++i)
        {
            Command exp;
            exp.kind = Command::Kind::Export;
            exp.path = outDir.string();
            const double t0 = wallMs();
            if (!run(h, exp)) return kFail;
            exportMs += wallMs() - t0;
        }
        // The writer times its own encode, so what is left of the export is the engine's
        // full-resolution render — the split a perf claim about "the engine" needs.
        const double encodeMs = h.encodeMs;
        std::filesystem::remove_all(outDir, ec);
        std::filesystem::remove(scratch, ec);

        const AppModel &m = h.svc.model();
        if (h.opt.json)
        {
            std::cout << "{\n  \"image\": \"" << img << "\",\n  \"width\": " << w
                      << ",\n  \"height\": " << hh << ",\n  \"iters\": " << iters
                      << ",\n  \"backend\": \"" << (m.gpuActive ? "gpu" : "cpu")
                      << "\",\n  \"engineThreads\": " << m.budget.engineThreads
                      << ",\n  \"stages\": [\n";
            printStage(true, false, "decode", decodeMs / iters, iters);
            printStage(true, false, "load", loadMs, 1);
            printStage(true, false, "renderFull", (exportMs - encodeMs) / iters, iters);
            printStage(true, true, "encode", encodeMs / iters, iters);
            std::cout << "  ]\n}\n";
        }
        else
        {
            std::cout << "image=" << img << " " << w << 'x' << hh << " iters=" << iters << '\n'
                      << "backend=" << (m.gpuActive ? "gpu" : "cpu")
                      << " engineThreads=" << m.budget.engineThreads
                      << " decodeWorkers=" << m.budget.decodeWorkers << '\n';
            printStage(false, false, "decode", decodeMs / iters, iters);
            printStage(false, false, "load", loadMs, 1);
            printStage(false, false, "renderFull", (exportMs - encodeMs) / iters, iters);
            printStage(false, true, "encode", encodeMs / iters, iters);
            std::cout << "totalMs=" << (decodeMs / iters + loadMs + exportMs / iters) << '\n';
        }
        return kOk;
    }

    // ── A1 · run: the grammar, from a file or stdin ───────────────────────────────────

    /** `wait <condition> --timeout Ns`. The service refuses to guess (only the caller owns
     *  a loop), so the conditions live here — and a bare number of ms is a plain timed pump,
     *  which is what a script that needs a preview to settle actually wants. */
    // D-16: `wait` used two vocabularies — this took model predicates (`load-finished`) while
    // the socket client matched event names (`load.finished`), so one documented command meant
    // different things to two front ends, which is the exact divergence R-SVC-5 exists to
    // prevent. The canonical form is the EVENT name, because events are the observable
    // contract; the hyphenated spellings stay accepted as aliases.
    std::string canonicalWait(std::string cond)
    {
        for (char &c : cond) if (c == '-') c = '.';
        return cond;
    }

    bool waitFor(Host &h, const std::string &condRaw, int timeoutMs)
    {
        const std::string cond = canonicalWait(condRaw);
        const double deadline = wallMs() + (timeoutMs > 0 ? timeoutMs : 120000);
        if (!cond.empty() && std::isdigit((unsigned char)cond[0]))
        {
            const double until = wallMs() + std::atof(cond.c_str());
            while (wallMs() < until) { pumpOnce(h); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            return true;
        }
        for (;;)
        {
            pumpOnce(h);
            const AppModel &m = h.svc.model();
            if (cond == "load.finished" && !m.load.active) return true;
            if (cond == "export.finished" && !m.exports.active) return true;
            if (cond == "quit" && h.svc.quitRequested()) return true;
            if (cond != "load.finished" && cond != "export.finished" && cond != "quit")
            {
                std::cerr << "cosmo-cc: wait: unknown condition " << cond << "\n";
                return false;
            }
            if (wallMs() > deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    /** One script line. Parsed by `parseCommand` — the ONE parser (R-SVC-5) — and then
     *  either handled here (the three the service leaves to the front end) or dispatched.
     *  This is why the loop does not use `dispatchText`: `state print` needs the parsed
     *  `--json` flag, `wait` needs the parsed timeout, and `export` needs its format
     *  fields, and none of those survive being handed to the service as text. */
    bool runLine(Host &h, const std::string &line)
    {
        std::string err;
        const Command c = arstro::cosmo::parseCommand(line, err);
        if (!c.valid())
        {
            if (err.empty()) return true;   // blank or a `#` comment
            std::cerr << "cosmo-cc: " << err << ": " << line << "\n";
            return false;
        }
        if (h.opt.watch) std::cout << "[cmd] " << arstro::cosmo::formatCommand(c) << '\n';

        switch (c.kind)
        {
            case Command::Kind::StatePrint:
                pumpOnce(h);
                printModel(h, c.flag || h.opt.json, &c);
                return true;
            case Command::Kind::UiDump:
                // A headless run has no Segment tree — there is no window and no view object
                // to walk. Say so and succeed, so ONE acceptance script can run through both
                // front ends (tests/acceptance/run.sh) without branching on which one it is.
                std::cout << "[evt] ui.begin\n"
                          << "ui-root " << (c.name.empty() ? "all" : c.name)
                          << " (no view attached: cosmo-cc is headless; run `cosmo-cc attach` "
                             "against a live window to dump it)\n"
                          << "[evt] ui.end\n";
                return true;
            case Command::Kind::Wait:
                if (waitFor(h, c.name, c.index)) return true;
                std::cerr << "cosmo-cc: wait " << c.name << ": timed out after " << c.index << "ms\n";
                return false;
            case Command::Kind::Export:
                if (!applyExportOptions(h, c)) { std::cerr << "cosmo-cc: unknown format\n"; return false; }
                break;
            default: break;
        }
        if (!run(h, c)) return false;
        pumpOnce(h);   // let anything the command started make progress before the next line
        return true;
    }

    int cmdRun(Host &h, const Args &a)
    {
        std::vector<std::string> lines;
        const std::string script = a.value("script");
        if (!script.empty() && script != "1")
        {
            std::ifstream f(script);
            if (!f) return fail("cannot read script " + script);
            std::string line;
            while (std::getline(f, line)) lines.push_back(line);
        }
        else
        {
            std::string line;
            while (std::getline(std::cin, line)) lines.push_back(line);
        }

        for (const std::string &line : lines)
        {
            if (!runLine(h, line)) return kFail;
            if (h.svc.quitRequested()) break;
        }
        // A script that started a load and never waited still gets its results applied,
        // rather than the process exiting with half a project decoded.
        pumpUntilIdle(h, 1000);
        return kOk;
    }
}

// ── attach: the only subcommand that builds NO service ───────────────────────────────
//
// R-SVC-8. Everything else here constructs a CosmoService and is the application; this one
// is a terminal on somebody else's. That asymmetry is the point: the pixels stay in the
// process that owns them, and only lines cross. `wait <event>` is handled here rather than
// sent, because a wait is a property of OUR loop — the service does not own it, which is
// exactly what CosmoService's Wait case says.
static int cmdAttach(const Args &a, const Options &opt)
{
#ifdef _WIN32
    (void)a; (void)opt;
    fprintf(stderr, "attach: not implemented on Windows (see ControlChannel.cpp)\n");
    return kFail;
#else
    const std::string sockPath = a.positional.empty() ? std::string() : a.positional.front();
    if (sockPath.empty()) { fprintf(stderr, "attach: need a socket path\n"); return kUsage; }

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("attach: socket"); return kFail; }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (sockPath.size() >= sizeof(addr.sun_path))
    { fprintf(stderr, "attach: socket path too long\n"); ::close(fd); return kFail; }
    memcpy(addr.sun_path, sockPath.c_str(), sockPath.size());
    if (::connect(fd, (sockaddr *)&addr, sizeof(addr)) != 0)
    {
        fprintf(stderr, "attach: cannot connect to %s: %s — is cosmo running with --control?\n",
                sockPath.c_str(), strerror(errno));
        ::close(fd);
        return kFail;
    }

    // Commands come from --script or stdin. Read them all up front: a script is short, and
    // it keeps the loop below about the socket rather than about input buffering.
    std::vector<std::string> pending;
    {
        const std::string script = a.value("script");
        std::ifstream f(script);
        std::istream &in = script.empty() ? std::cin : f;
        if (!script.empty() && !f) { fprintf(stderr, "attach: cannot read %s\n", script.c_str()); ::close(fd); return kFail; }
        for (std::string line; std::getline(in, line);) if (!line.empty()) pending.push_back(line);
    }

    std::string waitFor, buf;
    // A numeric `wait <ms>` is a SLEEP, exactly as it is in `cosmo-cc run` (waitFor(), which
    // spins pumpOnce for the duration). It needs its own state rather than riding in
    // `waitFor`, because that string is matched against arriving event text — and "wait 60"
    // treated as a pattern matches the "260" in a 420x260 splash dump, so a timed sample
    // loop fired all its samples in a single frame and reported the same instant forty times.
    // D-16 is exactly this: one word, one meaning, in both front ends.
    std::chrono::steady_clock::time_point sleepUntil;
    bool sleeping = false;
    const double timeoutS = a.value("timeout").empty() ? 300.0 : atof(a.value("timeout").c_str());
    const auto t0 = std::chrono::steady_clock::now();
    int rejections = 0;
    // A command's events arrive AFTER we stop sending, so the loop cannot end when the
    // script does — the first version did, and every response to the last four commands was
    // dropped, `state print` included. Instead: once nothing is left to send, keep reading
    // until the stream has been quiet for this long. A frame is 16 ms, so 600 ms is ~37
    // frames of silence; a dump is emitted in one pass, so it cannot straddle that.
    const double kQuietS = 0.6;
    auto lastData = std::chrono::steady_clock::now();

    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < timeoutS)
    {
        if (sleeping && std::chrono::steady_clock::now() >= sleepUntil) sleeping = false;
        if (!pending.empty() && waitFor.empty() && !sleeping)
        {
            const std::string line = pending.front();
            pending.erase(pending.begin());
            if (line.rfind("wait ", 0) == 0)
            {
                std::string w = line.substr(5);
                while (!w.empty() && w.back() == ' ') w.pop_back();
                w = canonicalWait(w);   // D-16: one vocabulary, both front ends
                if (!w.empty() && std::isdigit((unsigned char)w[0]))
                {
                    // A duration, not a condition. Events keep being drained below while it
                    // runs, so a sleep between two `ui dump`s samples two different frames —
                    // which is the whole point of being able to write one.
                    sleeping = true;
                    sleepUntil = std::chrono::steady_clock::now() +
                                 std::chrono::milliseconds((long)atof(w.c_str()));
                    if (opt.watch) printf("... sleeping %sms\n", w.c_str());
                }
                else
                {
                    waitFor = w;
                    if (opt.watch) printf("... waiting for %s\n", waitFor.c_str());
                }
            }
            else
            {
                if (opt.watch) printf("--> %s\n", line.c_str());
                const std::string out = line + "\n";
                if (::send(fd, out.data(), out.size(), MSG_NOSIGNAL) < 0)
                { perror("attach: send"); ::close(fd); return kFail; }
                lastData = std::chrono::steady_clock::now();
                continue;
            }
        }

        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(fd, &rs);
        // Cap the poll at what is left of a running sleep, or a `wait 60` between samples
        // would really be 200 ms and the sample cadence would be a lie.
        long usec = 200000;
        if (sleeping)
        {
            const auto left = std::chrono::duration_cast<std::chrono::microseconds>(
                                  sleepUntil - std::chrono::steady_clock::now()).count();
            usec = left > 0 ? std::min<long>(usec, (long)left) : 0;
        }
        timeval tv{0, usec};
        if (::select(fd + 1, &rs, nullptr, nullptr, &tv) > 0)
        {
            char chunk[65536];
            const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
            if (n == 0) { printf("attach: the window closed the connection\n"); break; }
            if (n < 0) { if (errno == EINTR) continue; perror("attach: recv"); ::close(fd); return kFail; }
            buf.append(chunk, (size_t)n);
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos)
            {
                const std::string line = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                printf("%s\n", line.c_str());
                if (line.find("command.rejected") != std::string::npos) ++rejections;
                if (!waitFor.empty() && line.find(waitFor) != std::string::npos)
                {
                    if (opt.watch) printf("... got %s\n", waitFor.c_str());
                    waitFor.clear();
                }
            }
            fflush(stdout);
            lastData = std::chrono::steady_clock::now();
        }
        if (pending.empty() && waitFor.empty() && !sleeping &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - lastData).count() > kQuietS)
            break;
    }
    ::close(fd);
    if (!waitFor.empty()) { fprintf(stderr, "attach: timed out waiting for %s\n", waitFor.c_str()); return kFail; }
    // A rejected command is a failed run: a script that silently half-worked is the thing
    // this whole architecture exists to make impossible.
    return rejections ? kFail : kOk;
#endif
}

int main(int argc, char **argv)
{
    const Args a = parseArgs(argc, argv);
    if (a.verb.empty() || a.has("help") || a.has("h")) return usage(a.verb.empty() && !a.has("help") ? kUsage : kOk);

    Options opt;
    opt.json = a.has("json");
    opt.watch = a.has("watch");
    opt.serial = a.has("serial");
    opt.stable = a.has("stable");
    opt.params = a.has("params");

    // The two subcommands that need no session build no session: `info` is a decode and
    // `backends` is introspection, and constructing an EditSession would start a render
    // thread and a GPU context for nothing.
    // attach drives somebody else's service, so it must come before the Host below: building
    // one here would start a render thread and a GPU context for a terminal.
    if (a.verb == "attach") return cmdAttach(a, opt);

    if (a.verb == "info") return cmdInfo(a, opt);
    if (a.verb == "backends") return cmdBackends(a, opt);
    if (a.verb == "params") return cmdParams(a, opt);
    if (a.verb == "check") return cmdCheck(a, opt);

    Host h;
    h.opt = opt;
    wire(h);
    if (!applyStartupSettings(h, a)) return fail(h.svc.model().lastError);

    if (a.verb == "project") return cmdProject(h, a);
    if (a.verb == "render") return cmdRender(h, a);
    if (a.verb == "export") return cmdExport(h, a);
    if (a.verb == "bench") return cmdBench(h, a);
    if (a.verb == "run") return cmdRun(h, a);

    std::cerr << "cosmo-cc: unknown subcommand: " << a.verb << "\n";
    return usage(kUsage);
}
