/*
 *  arstro-pm — the package manager of the touch stack (R-PKG-1..6).
 *
 *    arstro-pm pack <bundle-dir> [-o <file.arstro>]   make a package from a bundle directory
 *    arstro-pm install <file.arstro | bundle-dir>     install or replace an app
 *    arstro-pm remove <app-id>                        remove an app
 *    arstro-pm list                                   what is installed
 *    arstro-pm info <file.arstro | bundle-dir>        read a manifest without installing
 *    (every command takes --root <dir>; default: the per-user app directory)
 *
 *  A package is a gzip-compressed tar of a bundle directory, manifest.arstro at its top. Nothing
 *  more: a board must be able to install an app with the tools it already has, and a developer
 *  must be able to look inside one with `tar tzf`.
 *
 *  An install is all-or-nothing: the package is unpacked beside its destination, checked, and
 *  only then moved into place with one rename — a shell that scans the directory mid-install
 *  sees the old app or the new one, never half of either.
 */
#include "../core/AppRegistry.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ftw.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace arstro::shell;

static int usage()
{
    std::fputs("usage: arstro-pm pack <bundle-dir> [-o <file.arstro>]\n"
               "       arstro-pm install <file.arstro | bundle-dir> [--root <dir>]\n"
               "       arstro-pm remove <app-id> [--root <dir>]\n"
               "       arstro-pm list [--root <dir>]\n"
               "       arstro-pm info <file.arstro | bundle-dir>\n", stderr);
    return 2;
}

static int fail(const std::string &why)
{
    std::fprintf(stderr, "arstro-pm: %s\n", why.c_str());
    return 1;
}

static bool isDir(const std::string &p)
{
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
static bool exists(const std::string &p)
{
    struct stat st{};
    return ::lstat(p.c_str(), &st) == 0;
}

/** Run a program with an argument VECTOR — never through a shell, so a path is never parsed. */
static bool run(const std::vector<std::string> &args)
{
    const pid_t pid = ::fork();
    if (pid < 0) return false;
    if (pid == 0)
    {
        std::vector<char *> argv;
        for (const std::string &a : args) argv.push_back(const_cast<char *>(a.c_str()));
        argv.push_back(nullptr);
        ::execvp(argv[0], argv.data());
        ::_exit(127);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int removeOne(const char *path, const struct stat *, int, struct FTW *) { return ::remove(path); }
static bool removeTree(const std::string &dir) { return !exists(dir) || ::nftw(dir.c_str(), removeOne, 32, FTW_DEPTH | FTW_PHYS) == 0; }

static void makeDirs(const std::string &path)
{
    for (size_t i = 1; i <= path.size(); ++i)
        if (i == path.size() || path[i] == '/') ::mkdir(path.substr(0, i).c_str(), 0755);
}

static bool readManifest(const std::string &dir, AppInfo &app, std::string &error)
{
    std::ifstream f(dir + "/manifest.arstro");
    if (!f) { error = "no manifest.arstro in " + dir; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    if (!parseManifest(ss.str(), dir, app, error)) { error = "manifest.arstro: " + error; return false; }
    // The program and the icon must be inside the bundle: a manifest may not point an app's
    // launch at an arbitrary file on the device.
    for (const std::string *p : {&app.exec, &app.icon})
    {
        if (p->empty()) continue;
        if (p->compare(0, dir.size() + 1, dir + "/") != 0 || p->find("/../") != std::string::npos)
        { error = "manifest.arstro: '" + *p + "' is outside the bundle"; return false; }
    }
    if (::access(app.exec.c_str(), X_OK) != 0) { error = "the program " + app.exec + " is missing or not executable"; return false; }
    if (!app.icon.empty() && !exists(app.icon)) { error = "the icon " + app.icon + " is missing"; return false; }
    return true;
}

static void printApp(const AppInfo &a)
{
    std::printf("%-28s %-10s %s%s\n", a.id.c_str(), a.version.empty() ? "-" : a.version.c_str(), a.name.c_str(),
                a.system ? "  [system]" : "");
}

int main(int argc, char **argv)
{
    std::vector<std::string> args;
    std::string root, out;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--root" && i + 1 < argc) root = argv[++i];
        else if (a == "-o" && i + 1 < argc) out = argv[++i];
        else args.push_back(a);
    }
    if (args.empty()) return usage();
    if (root.empty())
    {
        const std::vector<std::string> roots = defaultAppRoots();
        if (roots.empty()) return fail("no app directory: set --root");
        root = roots.front();
    }
    const std::string cmd = args[0];
    std::string error;
    AppInfo app;

    if (cmd == "list" && args.size() == 1)
    {
        std::vector<std::string> problems;
        const std::vector<AppInfo> apps = scanApps({root}, &problems);
        for (const AppInfo &a : apps) printApp(a);
        for (const std::string &p : problems) std::fprintf(stderr, "arstro-pm: skipped %s\n", p.c_str());
        if (apps.empty()) std::printf("No apps installed in %s\n", root.c_str());
        return 0;
    }
    if (cmd == "remove" && args.size() == 2)
    {
        if (!validAppId(args[1])) return fail("'" + args[1] + "' is not an app id");
        const std::string dir = root + "/" + args[1];
        if (!isDir(dir)) return fail(args[1] + " is not installed in " + root);
        if (!removeTree(dir)) return fail("could not remove " + dir);
        std::printf("Removed %s\n", args[1].c_str());
        return 0;
    }
    if (cmd == "pack" && args.size() == 2)
    {
        const std::string dir = args[1];
        if (!isDir(dir)) return fail(dir + " is not a directory");
        if (!readManifest(dir, app, error)) return fail(error);
        if (out.empty()) out = app.id + "-" + (app.version.empty() ? "0" : app.version) + ".arstro";
        if (!run({"tar", "-czf", out, "-C", dir, "."})) return fail("tar could not write " + out);
        std::printf("Packed %s %s -> %s\n", app.id.c_str(), app.version.c_str(), out.c_str());
        return 0;
    }
    if ((cmd == "install" || cmd == "info") && args.size() == 2)
    {
        const std::string src = args[1];
        if (!exists(src)) return fail("no such file or directory: " + src);
        if (cmd == "info" && isDir(src))
        {
            if (!readManifest(src, app, error)) return fail(error);
            printApp(app);
            return 0;
        }
        makeDirs(root);
        // Unpack beside the destination, so the final rename stays on one filesystem.
        const std::string stage = root + "/.staging-" + std::to_string(getpid());
        removeTree(stage);
        ::mkdir(stage.c_str(), 0755);
        const bool unpacked = isDir(src) ? run({"cp", "-a", src + "/.", stage})
                                         : run({"tar", "-xzf", src, "-C", stage, "--no-same-owner"});
        if (!unpacked) { removeTree(stage); return fail("could not read " + src + " — is it an .arstro package?"); }
        if (!readManifest(stage, app, error)) { removeTree(stage); return fail(error); }
        if (cmd == "info") { printApp(app); removeTree(stage); return 0; }
        const std::string dest = root + "/" + app.id, old = dest + ".old-" + std::to_string(getpid());
        const bool replacing = isDir(dest);
        if (replacing && ::rename(dest.c_str(), old.c_str()) != 0) { removeTree(stage); return fail("could not replace " + dest); }
        if (::rename(stage.c_str(), dest.c_str()) != 0)
        {
            if (replacing) ::rename(old.c_str(), dest.c_str());  // put the old app back
            removeTree(stage);
            return fail("could not install into " + dest);
        }
        removeTree(old);
        std::printf("%s %s %s\n", replacing ? "Updated" : "Installed", app.id.c_str(), app.version.c_str());
        return 0;
    }
    return usage();
}
