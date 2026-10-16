/*
 *  R-VST-6 — other makers' plugins found, for real: the host's scan runs `solaris-plughost --scan` on each bundle in a
 *  folder holding this build's own plugins (Basic Synth, Drum Machine) and a module that aborts when loaded. The two are
 *  found, as instruments, by their class ids; the crash costs only its own bundle, said by name; and `solaris-cc plugins
 *  scan` prints the same. Never the user's folders.
 */
#ifdef NDEBUG
#undef NDEBUG // a test asserts in every build
#endif
#include "Plugins.h"
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
    const std::string cmd = std::string(SOLARIS_CC) + " plugins scan > " + (dir / "out.txt").string() + " 2>&1";
    assert(std::system(cmd.c_str()) == 0);
    std::ifstream f(dir / "out.txt");
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string out = ss.str();
    assert(out.find("instrument\tArstro Basic Synth (Arstro)") != std::string::npos && out.find("problem: Crash.vst3") != std::string::npos);
    assert(fs::exists(list));
    fs::remove_all(dir);
    std::printf("[PASS] plugins found for real (R-VST-6): this build's two by their class ids, as instruments; a module that aborts while read costs only itself, said; solaris-cc says the same and keeps the list\n");
    return 0;
}
