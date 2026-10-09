#include "Settings.h"
#include <cstdlib>
#include <sstream>

namespace arstro
{
namespace solaris
{
    std::string Settings::text() const
    {
        std::ostringstream o;
        o << "sampleRate = " << sampleRate << "\n";
        o << "bufferSize = " << bufferSize << "\n";
        o << "output = " << output << "\n";
        o << "input = " << input << "\n";
        for (const auto &f : folders) o << "folder = " << f << "\n";
        for (const auto &p : ports) o << "port." << p.first << " = " << p.second << "\n";
        o << "metronome = " << (metronome ? "on" : "off") << "\n";
        o << "metronomeLevel = " << metronomeLevel << "\n";
        o << "auditionLevel = " << auditionLevel << "\n";
        o << "newBpm = " << newBpm << "\n";
        o << "newSig = " << newSig << "\n";
        o << "reducedMotion = " << (reducedMotion ? "on" : "off") << "\n";
        return o.str();
    }

    Settings Settings::parse(const std::string &text)
    {
        Settings s;
        std::istringstream in(text);
        std::string line;
        auto trim = [](std::string v) {
            const auto a = v.find_first_not_of(" \t\r");
            if (a == std::string::npos) return std::string();
            return v.substr(a, v.find_last_not_of(" \t\r") - a + 1);
        };
        while (std::getline(in, line))
        {
            const auto eq = line.find('=');
            if (eq == std::string::npos || line[0] == ';' || line[0] == '#') continue;
            const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
            if (k == "sampleRate") { const int r = std::atoi(v.c_str()); if (r >= 8000 && r <= 192000) s.sampleRate = r; }
            else if (k == "bufferSize") { const int b = std::atoi(v.c_str()); if (b >= 32 && b <= 8192) s.bufferSize = b; }
            else if (k == "output") s.output = v;
            else if (k == "input") s.input = v;
            else if (k == "folder" && !v.empty()) s.folders.push_back(v);
            else if (k.rfind("port.", 0) == 0 && k.size() > 5) s.ports.emplace_back(k.substr(5), v);
            else if (k == "metronome") s.metronome = v == "on";
            else if (k == "metronomeLevel") { const double d = std::atof(v.c_str()); if (d >= -40 && d <= 6) s.metronomeLevel = d; }
            else if (k == "auditionLevel") { const double d = std::atof(v.c_str()); if (d >= -40 && d <= 6) s.auditionLevel = d; }
            else if (k == "newBpm") { const double b = std::atof(v.c_str()); if (b >= 20 && b <= 999) s.newBpm = b; }
            else if (k == "newSig" && !v.empty()) s.newSig = v;
            else if (k == "reducedMotion") s.reducedMotion = v == "on";
        }
        return s;
    }

    std::string Settings::portDevice(const std::string &portName) const
    {
        for (const auto &p : ports)
            if (p.first == portName) return p.second.substr(0, p.second.rfind(':'));
        return std::string();
    }
}
}
