#include "AppSettings.h"
#include "ProjectStore.h"
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace arstro
{
namespace cosmo
{
    std::string AppSettings::path() { return ProjectStore::configDir() + "/settings.txt"; }

    int AppSettings::workersFor(int percent, int cap)
    {
        if (percent < 1) percent = 1;
        if (percent > 100) percent = 100;
        const unsigned hc = std::thread::hardware_concurrency();
        const int cores = hc ? (int)hc : 4;   // an unknowable core count is not a reason to run serial
        int n = (cores * percent + 50) / 100; // round to nearest, so 50% of 3 cores is 2, not 1
        if (n < 1) n = 1;                     // R-CPU-1: never zero
        if (cap > 0 && n > cap) n = cap;
        return n;
    }

    AppSettings AppSettings::load()
    {
        AppSettings s;   // defaults stand for anything the file does not say
        std::ifstream f(path());
        if (!f) return s;
        std::string line;
        while (std::getline(f, line))
        {
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            // A malformed value keeps the default rather than throwing: a settings file
            // that got truncated must not stop cosmo from starting.
            try
            {
                if (k == "previewEdge") s.previewEdge = std::stoi(v);
                else if (k == "threads") s.threads = std::stoi(v);
                else if (k == "useGpu") s.useGpu = (v != "0");
                else if (k == "cpuPercent") s.cpuPercent = std::stoi(v);
            }
            catch (...) {}
        }
        if (s.previewEdge < 64) s.previewEdge = 1600;
        if (s.threads < 0) s.threads = 0;
        // An out-of-range budget is a corrupt file, not a request to take the whole
        // machine — fall back to the default rather than clamping up to 100 (R-CPU-1).
        if (s.cpuPercent < 1 || s.cpuPercent > 100) s.cpuPercent = 50;
        return s;
    }

    bool AppSettings::save() const
    {
        std::ofstream f(path(), std::ios::trunc);
        if (!f) return false;
        f << "cosmosettings=1\n"
          << "previewEdge=" << previewEdge << "\n"
          << "threads=" << threads << "\n"
          << "useGpu=" << (useGpu ? 1 : 0) << "\n"
          << "cpuPercent=" << cpuPercent << "\n";
        return (bool)f;
    }
}
}
