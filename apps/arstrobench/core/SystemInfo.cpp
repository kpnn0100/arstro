#include "SystemInfo.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
// RegGetValueA is a Vista-and-later API. MinGW-w64 targets an older Windows by default,
// which would leave it undeclared -- so state the floor before windows.h is parsed rather
// than discover it as a build break on the Windows host.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#else
#include <fstream>
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace arstro
{
namespace arstrobench
{
    namespace
    {
        std::string trim(std::string s)
        {
            const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
            s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                s = s.substr(1, s.size() - 2);
            return s;
        }

#ifndef _WIN32
        /** First value of `key` in a `key<sep>value` text file, or "" when absent. */
        std::string fileValue(const char *path, const std::string &key, char sep)
        {
            std::ifstream in(path);
            std::string line;
            while (std::getline(in, line))
            {
                const auto pos = line.find(sep);
                if (pos == std::string::npos) continue;
                if (trim(line.substr(0, pos)) != key) continue;
                return trim(line.substr(pos + 1));
            }
            return {};
        }
#else
        /** REG_SZ value, or "" when the key/value is absent. */
        std::string regString(HKEY root, const char *subKey, const char *value)
        {
            char buf[512] = {0};
            DWORD size = sizeof(buf);
            if (RegGetValueA(root, subKey, value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
                return {};
            return trim(std::string(buf));
        }
#endif
    }

    SystemInfo SystemInfo::query()
    {
        SystemInfo info;
        info.threads = (int)std::thread::hardware_concurrency();

#ifdef _WIN32
        // ── chip: the CPU's own name string, where Windows records it ──
        info.chip = regString(HKEY_LOCAL_MACHINE,
                              "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                              "ProcessorNameString");
        if (info.chip.empty()) info.chip = "Unknown";

        // ── memory ──
        MEMORYSTATUSEX mem;
        mem.dwLength = sizeof(mem);
        if (GlobalMemoryStatusEx(&mem))
            info.ramBytes = (uint64_t)mem.ullTotalPhys;

        // ── operating system: the registry carries the marketing name AND the build,
        // which GetVersionEx no longer reports honestly for unmanifested apps.
        const char *cv = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
        const std::string product = regString(HKEY_LOCAL_MACHINE, cv, "ProductName");
        const std::string display = regString(HKEY_LOCAL_MACHINE, cv, "DisplayVersion");
        const std::string build = regString(HKEY_LOCAL_MACHINE, cv, "CurrentBuildNumber");
        std::string os = product.empty() ? std::string("Windows") : product;
        if (!display.empty()) os += " " + display;
        if (!build.empty()) os += " (build " + build + ")";
        info.os = os;
#else
        // ── chip: /proc/cpuinfo names it "model name" on x86 and "Model" or
        // "Hardware" on ARM, so try each before giving up.
        info.chip = fileValue("/proc/cpuinfo", "model name", ':');
        if (info.chip.empty()) info.chip = fileValue("/proc/cpuinfo", "Model", ':');
        if (info.chip.empty()) info.chip = fileValue("/proc/cpuinfo", "Hardware", ':');
        if (info.chip.empty()) info.chip = "Unknown";

        // ── memory: sysconf is the portable route; /proc/meminfo is the fallback.
        const long pages = sysconf(_SC_PHYS_PAGES), pageSize = sysconf(_SC_PAGE_SIZE);
        if (pages > 0 && pageSize > 0)
            info.ramBytes = (uint64_t)pages * (uint64_t)pageSize;
        else
        {
            const std::string kb = fileValue("/proc/meminfo", "MemTotal", ':');
            info.ramBytes = (uint64_t)strtoull(kb.c_str(), nullptr, 10) * 1024ULL;
        }

        // ── operating system: the distro's own pretty name + the running kernel.
        std::string os = fileValue("/etc/os-release", "PRETTY_NAME", '=');
        utsname uts;
        if (uname(&uts) == 0)
        {
            if (os.empty()) os = uts.sysname;
            os += std::string(" (") + uts.sysname + " " + uts.release + ")";
        }
        info.os = os.empty() ? "Unknown" : os;
#endif
        return info;
    }

    std::string SystemInfo::chipText() const
    {
        if (threads <= 0) return chip;
        return chip + "  ·  " + std::to_string(threads) + " threads";
    }

    std::string SystemInfo::ramText() const
    {
        if (ramBytes == 0) return "Unknown";
        // Decimal GB, the unit a machine is sold in — a "16 GB" machine should not
        // read "14.9 GB" in a report the user compares against a spec sheet.
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f GB", (double)ramBytes / 1e9);
        return buf;
    }
}
}
