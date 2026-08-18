#include "ProjectStore.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace arstro
{
namespace cosmo
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr int kMaxRecents = 24;

        // Fields are tab-separated on one line; strip any tab/newline defensively.
        std::string sanitize(std::string s)
        {
            for (char &c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
            return s;
        }

        // The ONE getenv in cosmo_core, and it is the documented exception rather than a
        // precedent (see the module rule in arstro.cosmo.core.implement §0); IFileStore
        // (R-SVC-7) is meant to remove even this. An empty value counts as unset — an
        // exported-but-blank HOME must not resolve to "/.config/cosmo_v2".
        const char *env(const char *name)
        {
            const char *v = std::getenv(name);
            return (v && *v) ? v : nullptr;
        }

        // The old rule's last resort, kept verbatim so nothing about a bare POSIX launch
        // changes: "." + "/.config" + "/cosmo_v2".
        const char *kCwdConfig = "./.config/cosmo_v2";

#if defined(_WIN32)
        // A pre-D-8 directory counts as one to adopt only if something is IN it.
        // configDir() calls create_directories on EVERY launch, so mere existence proves
        // nothing: D-8's own evidence is an empty `C:\Users\...\.config\cosmo_v2\` on the
        // dev host, created by a launch whose settings, recents and log went elsewhere.
        bool hasState(const std::string &dir)
        {
            std::error_code ec;
            if (!fs::is_directory(dir, ec)) return false;
            fs::directory_iterator it(dir, ec);
            return !ec && it != fs::directory_iterator();
        }
#endif

        // ── Where cosmo keeps settings.txt, recent.tsv and cosmo_v2.log (D-8) ──────────
        //
        // The order is, and must stay:
        //   1. $XDG_CONFIG_HOME — an explicit override, on every platform.
        //   2. (Windows) a pre-D-8 directory that already holds state — adopted, see below.
        //   3. (Windows) %APPDATA%\cosmo_v2, else %USERPROFILE%\.config\cosmo_v2.
        //   4. $HOME/.config/cosmo_v2 — POSIX, and MSYS2, which sets HOME.
        //   5. ./.config/cosmo_v2 — last resort, and what the pre-D-8 code did whenever HOME
        //      was unset.
        //
        // (1) is load-bearing for this project, not a Linux nicety: every scripted run
        // sandboxes itself with it (the acceptance script, `cosmo-cc` runs, and
        // settings_roundtrip_and_survive_a_bad_file, which writes the REAL settings path),
        // so it has to win before any platform reasoning happens.
        //
        // %APPDATA% sits ABOVE $HOME on Windows deliberately. HOME exists only inside an
        // MSYS2/Git-Bash shell, so ranking it first is what produced D-8: one installation
        // kept two config dirs depending on how it was launched, and an Explorer or cmd.exe
        // launch (no HOME) fell all the way to "." and scattered config next to whatever
        // directory it started in. %APPDATA% is the one location all three launches agree
        // on, which is exactly what D-8 asks for. The log rides along in the same directory
        // rather than in %LOCALAPPDATA%: it is a debug channel an agent must be able to FIND
        // from a one-line instruction, and one predictable directory is worth more here than
        // the roaming/local split.
        //
        // Migration is by ADOPTION, not by copying, because reordering the rule would
        // otherwise orphan an MSYS2 user's recents:
        //   * cosmo_core has no logging and no error channel by design, so a copy that half
        //     succeeded would leave two divergent config dirs and nothing able to say so.
        //     Adoption cannot half-succeed.
        //   * Moving someone's recents is destructive and this layer cannot ask permission.
        //   * Adoption is idempotent and re-decided identically on every launch; a migration
        //     is a one-shot event whose outcome depends on which build ran first.
        // The cost is that a legacy install never converges on %APPDATA% — accepted: finding
        // a user's state beats tidying it.
        std::string resolveConfigDir()
        {
            if (const char *xdg = env("XDG_CONFIG_HOME")) return std::string(xdg) + "/cosmo_v2";

            const char *home = env("HOME");
            const std::string homeConfig = home ? std::string(home) + "/.config/cosmo_v2" : std::string();

#if defined(_WIN32)
            if (!homeConfig.empty() && hasState(homeConfig)) return homeConfig;  // an MSYS2 shell
            if (hasState(kCwdConfig)) return kCwdConfig;                         // cmd.exe / Explorer

            if (const char *appdata = env("APPDATA")) return std::string(appdata) + "/cosmo_v2";
            if (const char *profile = env("USERPROFILE")) return std::string(profile) + "/.config/cosmo_v2";
#endif
            if (!homeConfig.empty()) return homeConfig;
            return kCwdConfig;
        }
    }

    std::string ProjectStore::configDir()
    {
        const std::string dir = resolveConfigDir();
        std::error_code ec;
        fs::create_directories(dir, ec);
        return dir;
    }

    std::string ProjectStore::indexPath() { return configDir() + "/recent.tsv"; }

    std::vector<RecentEntry> ProjectStore::recents()
    {
        std::vector<RecentEntry> out;
        std::ifstream f(indexPath());
        if (!f) return out;
        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty()) continue;
            std::stringstream ss(line);
            RecentEntry e;
            std::string count, size, opened;
            std::getline(ss, e.name, '\t');
            std::getline(ss, e.path, '\t');
            std::getline(ss, e.firstImagePath, '\t');
            std::getline(ss, count, '\t');
            std::getline(ss, size, '\t');
            std::getline(ss, opened, '\t');
            if (e.path.empty()) continue;
            std::error_code ec;
            if (!fs::exists(e.path, ec)) continue;  // project deleted/moved -> drop it
            try { e.photoCount = count.empty() ? 0 : std::stoi(count); } catch (...) { e.photoCount = 0; }
            try { e.sizeBytes = size.empty() ? 0 : std::stoll(size); } catch (...) { e.sizeBytes = 0; }
            try { e.lastOpened = opened.empty() ? 0 : std::stoll(opened); } catch (...) { e.lastOpened = 0; }
            out.push_back(std::move(e));
        }
        return out;
    }

    void ProjectStore::remember(RecentEntry e)
    {
        if (e.path.empty()) return;
        std::vector<RecentEntry> list = recents();
        // Drop any existing entry for the same project, then push to the front.
        for (auto it = list.begin(); it != list.end();)
            it = (it->path == e.path) ? list.erase(it) : it + 1;
        list.insert(list.begin(), std::move(e));
        if ((int)list.size() > kMaxRecents) list.resize(kMaxRecents);

        std::ofstream f(indexPath(), std::ios::trunc);
        if (!f) return;
        for (const auto &r : list)
            f << sanitize(r.name) << '\t' << sanitize(r.path) << '\t' << sanitize(r.firstImagePath) << '\t'
              << r.photoCount << '\t' << r.sizeBytes << '\t' << r.lastOpened << '\n';
    }
}
}
