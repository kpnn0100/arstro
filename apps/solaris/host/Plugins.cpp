#include "Plugins.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
namespace fs = std::filesystem;

namespace arstro
{
namespace solaris_host
{
    std::vector<std::string> pluginSearchPath()
    {
        std::vector<std::string> dirs;
        if (const char *p = std::getenv("SOLARIS_VST3_PATH"))
        {
            std::string cur;
            for (const char *c = p;; ++c)
            {
                if (*c == ':' || *c == '\0') { if (!cur.empty()) dirs.push_back(cur); cur.clear(); }
                else cur += *c;
                if (*c == '\0') break;
            }
            return dirs;
        }
        if (const char *home = std::getenv("HOME")) dirs.push_back(std::string(home) + "/.vst3");
        dirs.push_back("/usr/lib/vst3");
        dirs.push_back("/usr/local/lib/vst3");
        return dirs;
    }

    std::string pluginsListPath()
    {
        if (const char *p = std::getenv("SOLARIS_PLUGINS")) return p;
        const char *xd = std::getenv("XDG_DATA_HOME"), *home = std::getenv("HOME");
        if (xd) return std::string(xd) + "/solaris/plugins";
        if (home) return std::string(home) + "/.local/share/solaris/plugins";
        return std::string();
    }

    std::string plughostPath()
    {
        if (const char *p = std::getenv("SOLARIS_PLUGHOST")) return p;
#ifdef SOLARIS_PLUGHOST
        return SOLARIS_PLUGHOST;
#else
        return std::string();
#endif
    }

    namespace
    {
        /** One bundle read by the helper: its lines, or why not. */
        bool readBundle(const std::string &helper, const std::string &bundle, std::vector<solaris::PluginModel> &out, std::string &why)
        {
            int fd[2];
            if (pipe(fd) != 0) { why = "cannot make a pipe"; return false; }
            posix_spawn_file_actions_t fa;
            posix_spawn_file_actions_init(&fa);
            posix_spawn_file_actions_adddup2(&fa, fd[1], 1);
            posix_spawn_file_actions_addclose(&fa, fd[0]);
            posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0); // a module's chatter is not ours
            std::vector<std::string> args{helper, "--scan", bundle};
            std::vector<char *> argv;
            for (auto &a : args) argv.push_back(a.data());
            argv.push_back(nullptr);
            pid_t pid = 0;
            const int rc = posix_spawn(&pid, helper.c_str(), &fa, nullptr, argv.data(), environ);
            posix_spawn_file_actions_destroy(&fa);
            close(fd[1]);
            if (rc != 0) { close(fd[0]); why = "cannot start " + helper + ": " + std::strerror(rc); return false; }
            std::string text;
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            bool timedOut = false;
            for (;;)
            {
                const int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now()).count();
                if (left <= 0) { timedOut = true; break; }
                pollfd p{fd[0], POLLIN, 0};
                if (poll(&p, 1, left) <= 0) continue;
                char buf[4096];
                const ssize_t n = read(fd[0], buf, sizeof buf);
                if (n <= 0) break;
                text.append(buf, (size_t)n);
            }
            close(fd[0]);
            if (timedOut) kill(pid, SIGKILL);
            int status = 0;
            waitpid(pid, &status, 0);
            if (timedOut) { why = "it did not answer in 10 s"; return false; }
            if (WIFSIGNALED(status)) { why = "the plugin crashed while being read (" + std::string(strsignal(WTERMSIG(status))) + ")"; return false; }
            size_t at = 0;
            bool any = false;
            while (at < text.size())
            {
                size_t eol = text.find('\n', at);
                if (eol == std::string::npos) eol = text.size();
                const std::string line = text.substr(at, eol - at);
                at = eol + 1;
                std::vector<std::string> f;
                size_t a = 0;
                for (size_t t; (t = line.find('\t', a)) != std::string::npos; a = t + 1) f.push_back(line.substr(a, t - a));
                f.push_back(line.substr(a));
                if (f[0] == "error" && f.size() >= 2) why = f[1];
                if (f[0] == "class" && f.size() >= 5) { out.push_back(solaris::PluginModel{f[1], f[3], f[4], f[2], bundle}); any = true; }
            }
            if (!any && why.empty()) why = "it said nothing";
            return any;
        }
    }

    bool scanPlugins(const std::vector<std::string> &dirs, std::vector<solaris::PluginModel> &out, std::vector<std::string> &problems, std::string &err)
    {
        const std::string helper = plughostPath();
        if (helper.empty() || access(helper.c_str(), X_OK) != 0) { err = "this build has no plugin host (solaris-plughost: the VST3 SDK was not found)"; return false; }
        std::vector<std::string> bundles;
        for (const auto &d : dirs)
        {
            std::error_code ec;
            for (auto it = fs::recursive_directory_iterator(d, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator();
                 it.increment(ec))
            {
                if (it.depth() > 3) { it.disable_recursion_pending(); continue; }
                if (it->path().extension() == ".vst3")
                {
                    bundles.push_back(it->path().string());
                    it.disable_recursion_pending(); // a bundle is read whole, never searched inside
                }
            }
        }
        std::sort(bundles.begin(), bundles.end());
        for (const auto &b : bundles)
        {
            std::string why;
            if (!readBundle(helper, b, out, why)) problems.push_back(fs::path(b).filename().string() + ": " + why);
        }
        return true;
    }
}
}
