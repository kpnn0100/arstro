/*
 *  R-VST-6 — other makers' plugins found, for real: the host's scan runs `solaris-plughost --scan` on each bundle in a
 *  folder holding this build's own plugins (Basic Synth, Drum Machine) and a module that aborts when loaded. The two are
 *  found, as instruments, by their class ids; the crash costs only its own bundle, said by name; and `solaris-cc plugins
 *  scan` prints the same. Never the user's folders.
 */
#ifdef NDEBUG
#undef NDEBUG // a test asserts in every build
#endif
#include "AudioFiles.h"
#include "PluginInstance.h"
#include "Plugins.h"
#include <cmath>
#include <csignal>
#include <dirent.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace arstro;

namespace
{
    /** The helper this process started (its only child of that name) — found the way `ps` would. */
    pid_t helperPid()
    {
        DIR *d = opendir("/proc");
        pid_t found = 0;
        while (dirent *e = readdir(d))
        {
            const pid_t pid = (pid_t)std::atoi(e->d_name);
            if (pid <= 0) continue;
            std::ifstream st(std::string("/proc/") + e->d_name + "/stat");
            std::string line;
            std::getline(st, line);
            const auto close = line.rfind(')');
            if (close == std::string::npos || line.find("(solaris-plughos") == std::string::npos) continue;
            std::istringstream rest(line.substr(close + 2));
            char state;
            long ppid = 0;
            rest >> state >> ppid;
            if (ppid == (long)getpid()) found = pid;
        }
        closedir(d);
        return found;
    }

    /** `blocks` blocks of 256 frames, a note held from the first — its RMS. */
    double play(solaris::IPluginInstance &p, int blocks, bool &ok, int pitch = 69)
    {
        float l[256], r[256];
        float *out[2] = {l, r};
        double sum = 0;
        ok = true;
        for (int b = 0; b < blocks && ok; ++b)
        {
            solaris::PluginNote on{0, pitch, true, 0.8f};
            ok = p.process(nullptr, out, 256, b == 0, &on, b == 0 ? 1 : 0, nullptr, 0, 2.0);
            for (int i = 0; ok && i < 256; ++i) sum += (double)l[i] * l[i];
        }
        return std::sqrt(sum / (blocks * 256.0));
    }
}

int main()
{
    const fs::path dir = fs::temp_directory_path() / ("solaris-plugins-test-" + std::to_string((long)getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir / "Crash.vst3" / "Contents" / "x86_64-linux");
    for (const char *b : {"ArstroBasicSynth.vst3", "ArstroDrumMachine.vst3"})
        fs::copy(fs::path(ARSTRO_VST3_DIR) / b, dir / b, fs::copy_options::recursive);
    fs::copy_file(SOLARIS_CRASH_MODULE, dir / "Crash.vst3" / "Contents" / "x86_64-linux" / "Crash.so");
    setenv("SOLARIS_PLUGHOST", SOLARIS_PLUGHOST_BIN, 1);

    std::vector<solaris::PluginModel> found;
    std::vector<std::string> problems;
    std::string err;
    const bool scanned = solaris_host::scanPlugins({dir.string()}, found, problems, err); // never inside an assert
    assert(scanned);
    bool synth = false, drums = false;
    for (const auto &p : found)
    {
        std::printf("    found %s %s \"%s\" (%s)\n", p.id.c_str(), p.kind.c_str(), p.name.c_str(), p.vendor.c_str());
        synth |= p.name == "Arstro Basic Synth" && p.kind == "instrument" && p.id.size() == 32 && p.path.find("ArstroBasicSynth.vst3") != std::string::npos;
        drums |= p.name.find("Drum Machine") != std::string::npos && p.kind == "instrument";
    }
    for (const auto &x : problems) std::printf("    problem %s\n", x.c_str());
    assert(found.size() == 2 && synth && drums);
    assert(problems.size() == 1 && problems[0].find("Crash.vst3: the plugin crashed while being read") == 0);

    // the same through solaris-cc — its list the machine's, here a scratch file
    const std::string list = (dir / "plugins").string();
    setenv("SOLARIS_VST3_PATH", dir.c_str(), 1);
    setenv("SOLARIS_PLUGINS", list.c_str(), 1);
    setenv("SOLARIS_SETTINGS", (dir / "settings.txt").c_str(), 1);
    setenv("SOLARIS_RECENTS", (dir / "recents").c_str(), 1);
    setenv("SOLARIS_PRESETS", (dir / "presets").c_str(), 1);     // never the machine's own folders
    setenv("SOLARIS_TEMPLATES", (dir / "templates").c_str(), 1);
    setenv("SOLARIS_LIBRARY", (dir / "library").c_str(), 1);
    const std::string cmd = std::string(SOLARIS_CC) + " plugins scan > " + (dir / "out.txt").string() + " 2>&1";
    assert(std::system(cmd.c_str()) == 0);
    std::ifstream f(dir / "out.txt");
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string out = ss.str();
    assert(out.find("instrument\tArstro Basic Synth (Arstro)") != std::string::npos && out.find("problem: Crash.vst3") != std::string::npos);
    assert(fs::exists(list));

    // one instance PLAYED out of process: Basic Synth, a note in, sound out; its state out and back; then the helper
    // killed (offline, why), frozen (an overrun, offline) — and a bundle that is not there refused at load
    const std::string bundle = (dir / "ArstroBasicSynth.vst3").string(), uid = "9FEB6596B6B044A4A46AC1C4145CD7D7";
    {
        auto p = solaris_host::loadPlugin(bundle, uid, 48000, 512, err);
        assert(p && err.empty());
        assert(p->isInstrument() && !p->params().empty() && !p->offline());
        bool ok = false;
        const double rms = play(*p, 40, ok);
        std::printf("    Basic Synth out of process: %zu parameters, RMS %.4f\n", p->params().size(), rms);
        assert(ok && rms > 0.01);
        std::string st, st2;
        const bool got = p->getState(st, err);
        assert(got && !st.empty());
        const bool put = p->setState(st, err);
        assert(put);
        const bool again = p->getState(st2, err);
        assert(again && st2 == st);
        const pid_t pid = helperPid();
        assert(pid > 0);
        kill(pid, SIGKILL);
        const double after = play(*p, 1, ok);
        assert(!ok && after == 0.0 && p->offline());
        std::printf("    killed: offline, \"%s\"\n", p->why().c_str());
        assert(p->why().find("its process ended") == 0);
        const bool gone = p->getState(st, err);
        assert(!gone && err.find("offline") != std::string::npos);
    }
    {
        auto p = solaris_host::loadPlugin(bundle, uid, 48000, 512, err);
        assert(p);
        bool ok = false;
        play(*p, 2, ok);
        assert(ok);
        const pid_t pid = helperPid();
        kill(pid, SIGSTOP);
        float l[256], r[256];
        float *out[2] = {l, r};
        const bool late = p->process(nullptr, out, 256, false, nullptr, 0, nullptr, 0, 0.05);
        assert(!late && p->offline() && p->why() == "it overran its budget");
        std::printf("    frozen: offline, \"%s\"\n", p->why().c_str());
        kill(pid, SIGCONT);
    } // and its end kills what does not leave
    assert(helperPid() == 0);
    err.clear();
    assert(!solaris_host::loadPlugin((dir / "Nothing.vst3").string(), uid, 48000, 512, err) && !err.empty());
    std::printf("    not there: \"%s\"\n", err.c_str());

    // in a song, through solaris-cc (the list the scan above kept): Basic Synth as a plugin plays what the native synth
    // plays — to the bit — and a value set on it is in its state, saved with the song and given back when it is opened
    auto run = [&](const std::string &script) {
        const fs::path f = dir / "script.txt";
        std::ofstream(f) << script;
        const std::string c = "cd " + dir.string() + " && " + SOLARIS_CC + " --script " + f.string() + " > " + (dir / "cc.txt").string() + " 2>&1";
        const int rc = std::system(c.c_str());
        std::ifstream o(dir / "cc.txt");
        std::stringstream os;
        os << o.rdbuf();
        if (rc != 0) std::printf("    solaris-cc: %s\n", os.str().c_str());
        return rc == 0;
    };
    auto samples = [&](const std::string &name) {
        solaris::engine::Pcm pcm;
        std::string e;
        const bool read = solaris_host::decodeAudio((dir / name).string(), 48000, pcm, e);
        assert(read);
        return pcm.samples;
    };
    const std::string notes = "notes add pt_1 \"A4@0:1 C5@1:1:90 E5@2:1.5 A3@0:3:70\"\n";
    assert(run("project new native.slp --bpm 120\nclip add --instrument synth --at 0 --length 4\n" + notes + "render --out native.wav --to 4 --bits 32f\nproject save\n"));
    assert(run("project new plugged.slp --bpm 120\nclip add --plugin \"Arstro Basic Synth\" --at 0 --length 4\n" + notes + "render --out plugged.wav --to 4 --bits 32f\nproject save\n"));
    const auto native = samples("native.wav"), plugged = samples("plugged.wav");
    double worst = 0, peak = 0;
    for (size_t i = 0; i < native.size() && i < plugged.size(); ++i)
    {
        worst = std::max(worst, (double)std::fabs(native[i] - plugged[i]));
        peak = std::max(peak, (double)std::fabs(native[i]));
    }
    std::printf("    Basic Synth as a plugin vs the native synth: peak %.3f, the largest difference %.3g\n", peak, worst);
    assert(native.size() == plugged.size() && peak > 0.1 && worst < 1e-6);
    assert(run("project open plugged.slp\nset dv_1.p0=0.6\nrender --out set.wav --to 4 --bits 32f\nproject save\n"));
    {
        std::ifstream f(dir / "plugged.slp");
        std::stringstream ss;
        ss << f.rdbuf();
        assert(ss.str().find("type=vst3 plugin=" + uid) != std::string::npos && ss.str().find(" state=") != std::string::npos);
    }
    assert(run("project open plugged.slp\nrender --out again.wav --to 4 --bits 32f\n"));
    const auto set = samples("set.wav"), again = samples("again.wav");
    assert(set == again && set != plugged);

    fs::remove_all(dir);
    std::printf("[PASS] plugins found for real (R-VST-6): this build's two by their class ids, as instruments; a module that aborts while read costs only itself, said; solaris-cc says the same and keeps the list; one PLAYED out of process — sound, state, killed and frozen offline; in a song it renders what the native synth does, its state saved and given back\n");
    return 0;
}
