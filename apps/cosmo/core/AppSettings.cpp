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

    // Both directions, because "small screen" means two different things. 75 and 90 are for a
    // screen that is small in PIXELS — a 1366x768 or 1280x800 panel cannot show the editor's
    // three columns plus a usable canvas at the Figma sizes, so the shell is drawn smaller to
    // fit. 125 through 200 are for a screen that is small in INCHES but dense: a 7-inch
    // 1920x1200 panel has pixels to spare and controls too small to hit, so the shell is drawn
    // bigger. 200 is the top because it is what makes such a panel usable and its window
    // minimum (1168x932) still fits one.
    //
    // Not every scale fits every screen any more — 200% cannot be honoured on a 768 px-tall
    // display — so the settings row DISABLES the ones the display cannot give a window for,
    // rather than the list pretending they are universal (R-SCALE-3).
    const std::vector<int> &AppSettings::uiScales()
    {
        static const std::vector<int> v{75, 90, 100, 125, 150, 175, 200};
        return v;
    }

    int AppSettings::clampUiScale(int percent)
    {
        const auto &all = uiScales();
        int best = 100, bestD = 1 << 30;
        for (int s : all)
        {
            const int d = s > percent ? s - percent : percent - s;
            if (d < bestD) { bestD = d; best = s; }
        }
        return best;
    }

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
                else if (k == "uiScale") s.uiScale = std::stoi(v);
                else if (k == "touchUi") s.touchUi = (v != "0");
            }
            catch (...) {}
        }
        if (s.previewEdge < 64) s.previewEdge = 1600;
        if (s.threads < 0) s.threads = 0;
        // An out-of-range budget is a corrupt file, not a request to take the whole
        // machine — fall back to the default rather than clamping up to 100 (R-CPU-1).
        if (s.cpuPercent < 1 || s.cpuPercent > 100) s.cpuPercent = 50;
        // Snapped rather than range-checked: every scale the app has been laid out, rendered
        // and asserted at is one of the offered ones (R-SCALE-1).
        s.uiScale = clampUiScale(s.uiScale);
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
          << "cpuPercent=" << cpuPercent << "\n"
          << "uiScale=" << uiScale << "\n"
          << "touchUi=" << (touchUi ? 1 : 0) << "\n";
        return (bool)f;
    }
}
}
