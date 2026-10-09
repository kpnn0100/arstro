// solaris_control tests — the window's control channel with no window (R-SVC-5, R-SVC-6).
//
// The REAL ControlServer (cosmo's ControlChannel compiled in place + the Solaris half) over the REAL
// SolarisService, on a real Unix socket in the scratch dir, polled by a fake clock — so every claim
// of DR-SVC-5 is checked here on any machine, display or not:
//
//   * each line is answered in order: its events, its output, then `[ok]` / `[refused]`;
//   * `wait` holds the QUEUE, never the thread (poll returns at once; the next line waits its turn);
//   * `solaris-cc attach` prints exactly what `solaris-cc --watch --script` prints for the same
//     script — stdout and the event stream byte for byte (the equivalence of R-SVC-6 with the
//     window's GTK half replaced by its own poll loop; tests/acceptance/run.sh adds the window).
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "AppModelCodec.h"
#include "ControlServer.h"
#include "Event.h"
#include "SolarisService.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <spawn.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#ifndef SOLARIS_CC
#error "SOLARIS_CC (the solaris-cc binary) must be defined by the build"
#endif

extern char **environ;
using namespace arstro;
namespace fs = std::filesystem;

namespace
{
    int passed = 0;
    void pass(const char *name)
    {
        std::printf("[PASS] %s\n", name);
        ++passed;
    }

    fs::path scratch(const char *sub)
    {
        const char *t = std::getenv("SOLARIS_TEST_DIR");
        fs::path d = (t ? fs::path(t) : fs::temp_directory_path() / "solaris_control_tests") / "control" / sub;
        fs::remove_all(d);
        fs::create_directories(d);
        return d;
    }

    std::string slurp(const fs::path &p)
    {
        std::ifstream f(p, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    /** A raw client: what `socat` or any agent would be. The socket is RELATIVE to the cwd — a scratch
     *  path is longer than sun_path's 108 bytes, a relative one never is. */
    struct Client
    {
        int fd = -1;
        std::string buf;
        explicit Client(const char *path)
        {
            fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
            sockaddr_un a{};
            a.sun_family = AF_UNIX;
            std::strncpy(a.sun_path, path, sizeof a.sun_path - 1);
            assert(::connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof a) == 0);
        }
        ~Client() { ::close(fd); }
        void send(const std::string &text) { assert(::write(fd, text.data(), text.size()) == (ssize_t)text.size()); }
        /** Everything that has arrived, as lines (non-blocking). */
        std::vector<std::string> lines()
        {
            char chunk[65536];
            for (;;)
            {
                const ssize_t n = ::recv(fd, chunk, sizeof chunk, MSG_DONTWAIT);
                if (n <= 0) break;
                buf.append(chunk, (size_t)n);
            }
            std::vector<std::string> out;
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos)
            {
                out.push_back(buf.substr(0, nl));
                buf.erase(0, nl + 1);
            }
            return out;
        }
    };

    bool isAnswer(const std::string &l) { return l.rfind("[ok] ", 0) == 0 || l.rfind("[refused] ", 0) == 0; }

    /** Poll the server until `answers` answers have arrived (or a few seconds pass). */
    std::vector<std::string> collect(solaris_host::ControlServer &server, Client &c, int answers, double now = 1.0)
    {
        std::vector<std::string> got;
        int seen = 0;
        for (int i = 0; i < 2000 && seen < answers; ++i)
        {
            server.poll(now);
            for (auto &l : c.lines())
            {
                if (isAnswer(l)) ++seen;
                got.push_back(l);
            }
            if (seen < answers) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return got;
    }

    /** What the wire must carry for `lines`, from a SECOND service given the same lines directly:
     *  its events as `[evt]`, its output as `[out]`, then the answer (DR-SVC-5's framing). */
    std::vector<std::string> expectedWire(const std::vector<std::string> &lines)
    {
        solaris::SolarisService ref;
        std::vector<std::string> wire;
        ref.subscribe([&](const solaris::Event &e) { wire.push_back(solaris::formatEvent(e)); });
        for (const auto &l : lines)
        {
            std::string err;
            const solaris::Command c = solaris::parseCommand(l, err);
            if (err.empty() && c.kind == solaris::Command::Kind::None) { wire.push_back("[ok] " + l); continue; }
            if (!ref.dispatchText(l, err)) { wire.push_back("[refused] " + err); continue; }
            std::istringstream out(ref.output());
            for (std::string o; std::getline(out, o);) wire.push_back("[out] " + o);
            wire.push_back("[ok] " + l);
        }
        return wire;
    }

    void test_each_line_is_answered_in_order()
    {
        const std::vector<std::string> lines = {"project new t.slp --bpm 100", "# a comment is answered, and runs nothing",
                                                "strip add --kind bus --name Verb",  "set ch_1.gain=-3 ch_2.pan=0.25",
                                                "get ch_1.gain",                     "flurb --now",
                                                "set ch_1.gain=99",                  "state print --json --stable"};
        const fs::path dir = scratch("order");
        fs::create_directories(dir / "ref");
        fs::current_path(dir / "ref");
        std::vector<std::string> want = expectedWire(lines);
        // recents are absolute (R-HOME, C6): the reference ran in ref/, the channel runs in dir — same song
        const std::string refDir = (dir / "ref").string(), liveDir = dir.string();
        for (auto &w : want)
            for (size_t at; (at = w.find(refDir)) != std::string::npos;) w.replace(at, refDir.size(), liveDir);

        fs::current_path(dir);
        solaris::SolarisService svc;
        solaris_host::ControlServer server(svc);
        std::string err;
        assert(server.open("control.sock", err));
        assert(fs::is_socket(dir / "control.sock"));
        Client c("control.sock");
        std::string all;
        for (const auto &l : lines) all += l + "\n";
        c.send(all); // one write, many lines: a heredoc arrives this way and must still run as N commands
        const std::vector<std::string> got = collect(server, c, (int)lines.size());
        if (got != want)
        {
            for (size_t i = 0; i < std::max(got.size(), want.size()); ++i)
                std::fprintf(stderr, "%3zu got  %s\n    want %s\n", i, i < got.size() ? got[i].c_str() : "-", i < want.size() ? want[i].c_str() : "-");
        }
        assert(got == want);
        // and the window's service is where the lines landed: the same model a direct dispatch made
        assert(svc.model().bpm == 100 && svc.model().strips.size() == 2 && !svc.model().lastError.empty());
        // a refusal is the service's own sentence, right after its command.rejected event
        int rejected = 0;
        for (size_t i = 0; i < got.size(); ++i)
            if (got[i].rfind("[refused] ", 0) == 0)
            {
                ++rejected;
                assert(i > 0 && got[i - 1].rfind("[evt] command.rejected", 0) == 0);
            }
        assert(rejected >= 1);
        server.close();
        assert(!fs::exists(dir / "control.sock")); // the node goes with the channel
        pass("each line is answered in order: its events, its output, then [ok] or [refused] — as a direct dispatch says");
    }

    void test_wait_holds_the_queue_not_the_thread()
    {
        const fs::path dir = scratch("wait");
        fs::current_path(dir);
        solaris::SolarisService svc;
        solaris_host::ControlServer server(svc);
        std::string err;
        assert(server.open("control.sock", err));
        Client c("control.sock");
        c.send("project new w.slp --bpm 90\nwait 0.5\nget project.bpm\n");
        std::vector<std::string> got = collect(server, c, 1, 10.0); // project new answered
        assert(!got.empty() && got.back() == "[ok] project new w.slp --bpm 90");
        // the wait arrived: poll returns at once (it would sleep half a second if the service ran it)
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i) server.poll(10.0 + 0.02 * i);
        const double spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        assert(spent < 0.2);
        assert(server.holding());
        got = c.lines();
        assert(got.empty()); // nothing answered while held — not the wait, not the line behind it
        server.poll(10.49);
        assert(c.lines().empty());
        got = collect(server, c, 2, 10.51);
        assert(got.size() == 3 && got[0] == "[ok] wait 0.5" && got[1].rfind("[out] 90", 0) == 0 && got[2] == "[ok] get project.bpm");
        // a wait the service would refuse is the service's to refuse — at once, never held
        c.send("wait 99999\n");
        got = collect(server, c, 1, 11.0);
        assert(!server.holding());
        assert(got.size() == 2 && got[0].rfind("[evt] command.rejected", 0) == 0 && got[1] == "[refused] wait takes seconds, 0 … 3600");
        pass("wait holds the queue, not the window's thread: poll returns at once, the next line waits its turn");
    }

    /** Run solaris-cc with `args`, stdout and stderr to files, in `cwd`; true once it has exited. */
    pid_t spawnCc(const std::vector<std::string> &args, const fs::path &out, const fs::path &err)
    {
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, 1, out.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_addopen(&fa, 2, err.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        std::vector<std::string> all = {SOLARIS_CC};
        all.insert(all.end(), args.begin(), args.end());
        std::vector<char *> argv;
        for (auto &a : all) argv.push_back(a.data());
        argv.push_back(nullptr);
        pid_t pid = 0;
        const int rc = posix_spawn(&pid, SOLARIS_CC, &fa, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&fa);
        assert(rc == 0);
        return pid;
    }

    void test_attach_prints_what_solaris_cc_prints()
    {
        const fs::path dir = scratch("attach");
        const std::string script =
            "project new song.slp --bpm 124\n"
            "clip add --instrument drums --at 0 --length 8\n"
            "note add pt_1 --pitch 36 --at 0\n"
            "note add pt_1 --pitch 38 --at 1\n"
            "clip add --instrument synth --at 0 --length 8\n"
            "note add pt_2 --pitch 48 --at 0 --length 1.5\n"
            "set dv_2.filter.cutoff=900 ch_2.gain=-4.5 ch_3.pan=-0.3\n"
            "strip add --kind bus --name Verb\n"
            "device add ch_4 --type reverb\n"
            "send add ch_3 --to ch_4 --gain -10\n"
            "get ch_2.gain\n"
            "undo\n"
            "redo\n"
            "audit\n"
            "matrix print\n"
            "project save\n"
            "state print --json --stable\n";
        { std::ofstream(dir / "script.txt") << script; }
        // (a) headless: solaris-cc, its own service, in its own folder with its own machine files
        fs::create_directories(dir / "cli");
        fs::current_path(dir / "cli");
        setenv("SOLARIS_SETTINGS", (dir / "cli" / "settings.txt").c_str(), 1);
        setenv("SOLARIS_RECENTS", (dir / "cli" / "recents").c_str(), 1);
        int status = 0;
        const pid_t a = spawnCc({"--watch", "--script", "../script.txt"}, dir / "cli" / "out.txt", dir / "cli" / "err.txt");
        assert(waitpid(a, &status, 0) == a && WIFEXITED(status) && WEXITSTATUS(status) == 0);

        // (b) through the channel: the service lives HERE (as in the window), attach builds none
        fs::create_directories(dir / "live");
        fs::current_path(dir / "live");
        solaris::SolarisService::Host host;
        host.settingsPath = (dir / "live" / "settings.txt").string(); // as the window's: its own files
        host.recentsPath = (dir / "live" / "recents").string();
        solaris::SolarisService svc(host);
        solaris_host::ControlServer server(svc);
        std::string err;
        assert(server.open("control.sock", err));
        const pid_t b = spawnCc({"attach", "control.sock", "--script", "../script.txt"}, dir / "live" / "out.txt", dir / "live" / "err.txt");
        bool done = false;
        const auto t0 = std::chrono::steady_clock::now();
        while (!done && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 30.0)
        {
            server.poll(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            if (waitpid(b, &status, WNOHANG) == b) done = true;
            else std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        assert(done && WIFEXITED(status) && WEXITSTATUS(status) == 0);

        std::string cliOut = slurp(dir / "cli" / "out.txt");
        const std::string liveOut = slurp(dir / "live" / "out.txt");
        // recents are absolute (R-HOME, C6): the dump's recents name each run's own folder — same song
        const std::string cliDir = (dir / "cli").string(), liveDir = (dir / "live").string();
        for (size_t at; (at = cliOut.find(cliDir)) != std::string::npos;) cliOut.replace(at, cliDir.size(), liveDir);
        const std::string cliErr = slurp(dir / "cli" / "err.txt"), liveErr = slurp(dir / "live" / "err.txt");
        if (cliOut != liveOut || cliErr != liveErr)
            std::fprintf(stderr, "diff -u %s/{cli,live}/out.txt ; diff -u %s/{cli,live}/err.txt\n", dir.c_str(), dir.c_str());
        assert(!cliOut.empty() && cliOut.find("\"screen\": \"project\"") != std::string::npos);
        assert(cliErr.find("[evt] project.opened path=song.slp") != std::string::npos);
        assert(cliOut == liveOut); // every output, the stable state dump included
        assert(cliErr == liveErr); // every event, in order
        // and both songs on disk are one song
        assert(slurp(dir / "cli" / "song.slp") == slurp(dir / "live" / "song.slp"));
        pass("solaris-cc attach prints what solaris-cc --watch --script prints: outputs, events and the saved song, byte for byte");
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_each_line_is_answered_in_order();
    test_wait_holds_the_queue_not_the_thread();
    test_attach_prints_what_solaris_cc_prints();
    std::printf("solaris_control: %d passed\n", passed);
    return 0;
}
