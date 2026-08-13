#include "AppSettings.h"
#include "ProjectStore.h"
#include <fstream>
#include <sstream>
#include <string>

namespace arstro
{
namespace cosmo
{
    std::string AppSettings::path() { return ProjectStore::configDir() + "/settings.txt"; }

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
            }
            catch (...) {}
        }
        if (s.previewEdge < 64) s.previewEdge = 1600;
        if (s.threads < 0) s.threads = 0;
        return s;
    }

    bool AppSettings::save() const
    {
        std::ofstream f(path(), std::ios::trunc);
        if (!f) return false;
        f << "cosmosettings=1\n"
          << "previewEdge=" << previewEdge << "\n"
          << "threads=" << threads << "\n"
          << "useGpu=" << (useGpu ? 1 : 0) << "\n";
        return (bool)f;
    }
}
}
