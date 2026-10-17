// SolarisService — the files a song uses (R-COLLECT-1; docs/discussion.md Round 16): listed, collected into the
// song's folder, relinked. A song names a file in an audio clip (and its takes), a device's sample (a sampler's sound,
// a convolution's response, an instrument's `.sfz` — whose own sounds lie beside it) and a frozen strip's render — in
// main and in every version. Collect and the song-wide relinks rewrite every reference in the whole document's text
// (main, each version's changes and own nodes, each snapshot), so a version rendered later finds its files too.
#include "Format.h"
#include "SolarisService.h"
#include "Versions.h"
#include "instrument/Sfz.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    namespace
    {
        bool isFileKey(const std::string &k) { return k == "src" || k == "sample" || k == "frozen"; }

        // A line's raw tokens with where each sits — quotes kept — up to an inline comment
        struct Tok
        {
            size_t at, len;
            std::string text;
        };
        std::vector<Tok> rawTokens(const std::string &line)
        {
            std::vector<Tok> out;
            size_t i = 0;
            while (i < line.size())
            {
                while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
                if (i >= line.size() || line[i] == ';') break;
                const size_t start = i;
                bool q = false;
                while (i < line.size() && (q || (line[i] != ' ' && line[i] != '\t')))
                {
                    if (line[i] == '"') q = !q;
                    ++i;
                }
                out.push_back(Tok{start, i - start, line.substr(start, i - start)});
            }
            return out;
        }

        std::string unquoted(const std::string &v)
        {
            return (v.size() >= 2 && v.front() == '"' && v.back() == '"') ? v.substr(1, v.size() - 2) : v;
        }

        bool sameBytes(const fs::path &a, const fs::path &b)
        {
            std::error_code ec;
            if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec) return false;
            std::ifstream x(a, std::ios::binary), y(b, std::ios::binary);
            return std::equal(std::istreambuf_iterator<char>(x), std::istreambuf_iterator<char>(), std::istreambuf_iterator<char>(y));
        }

        bool isSfz(const std::string &path)
        {
            std::string ext = fs::path(path).extension().string();
            for (auto &c : ext) c = (char)std::tolower((unsigned char)c);
            return ext == ".sfz";
        }

        // "kick.wav" in `dir`, or "kick 2.wav", "kick 3.wav"… — never over a different file, nor onto a name this collect
        // already gives another; the same bytes already there are reused
        fs::path freeName(const fs::path &dir, const fs::path &from, const std::set<std::string> &planned, bool &reused)
        {
            reused = false;
            const std::string stem = from.stem().string(), ext = from.extension().string();
            for (int n = 1;; ++n)
            {
                const fs::path cand = dir / (n == 1 ? stem + ext : stem + " " + std::to_string(n) + ext);
                if (planned.count(cand.string())) continue;
                std::error_code ec;
                if (!fs::exists(cand, ec)) return cand;
                if (sameBytes(cand, from)) { reused = true; return cand; }
            }
        }
    }

    std::vector<SolarisService::FileRef> SolarisService::fileRefs() const
    {
        // every file the song names — main and every version — by where it resolves, in the order first named
        std::vector<FileRef> out;
        std::map<std::string, size_t> at;
        const std::string doc = serializeProject(mProject);
        std::istringstream in(doc);
        std::string line, version, node;
        while (std::getline(in, line))
        {
            const auto first = line.find_first_not_of(' ');
            if (first == std::string::npos || line[first] != '#') continue;
            const LineTokens tok = tokenizeLine(line.substr(first));
            if (first == 0) version = tok.type == "aversion" ? tok.fields.empty() ? std::string() : tok.fields[0].second : std::string();
            for (const auto &f : tok.fields)
                if (f.first == "id" || f.first == "node") node = f.second;
            for (const auto &f : tok.fields)
            {
                if (!isFileKey(f.first) || f.second.empty() || tok.type == "aversion") continue;
                const std::string resolved = resolvePath(f.second);
                auto it = at.find(resolved);
                if (it == at.end())
                {
                    it = at.emplace(resolved, out.size()).first;
                    out.push_back(FileRef{f.second, resolved, {}});
                }
                const std::string user = (version.empty() ? "" : version + ":") + node + (tok.type == "take" ? " (a take)" : "");
                auto &users = out[it->second].users;
                if (std::find(users.begin(), users.end(), user) == users.end()) users.push_back(user);
            }
        }
        return out;
    }

    bool SolarisService::rewriteFiles(const std::map<std::string, std::string> &to, std::string &err)
    {
        // each reference whose file is in `to` (resolved → as the song will name it), in the whole document's text;
        // then the song read back, the version that was open opened again
        const std::string doc = serializeProject(mProject);
        std::istringstream in(doc);
        std::string line, out;
        while (std::getline(in, line))
        {
            const auto first = line.find_first_not_of(' ');
            if (first != std::string::npos && line[first] == '#')
            {
                const auto toks = rawTokens(line);
                for (auto t = toks.rbegin(); t != toks.rend(); ++t)
                {
                    const auto eq = t->text.find('=');
                    if (eq == std::string::npos || !isFileKey(t->text.substr(0, eq))) continue;
                    const std::string v = unquoted(t->text.substr(eq + 1));
                    if (v.empty()) continue;
                    const auto hit = to.find(resolvePath(v));
                    if (hit == to.end()) continue;
                    line.replace(t->at, t->len, t->text.substr(0, eq + 1) + quoteIfNeeded(hit->second));
                }
            }
            out += line + "\n";
        }
        Project q;
        if (!parseProject(out, q, err)) { err = "the song would not read back: " + err; return false; }
        const std::string open = mProject.version;
        if (!open.empty() && !openVersion(q, open, err)) return false;
        mProject = std::move(q);
        return true;
    }

    bool SolarisService::filesCommand(const Command &c, std::string &err)
    {
        if (!requireOpen(err)) return false;
        std::error_code ec;
        const fs::path dir = fs::absolute(fs::path(mPath)).parent_path().lexically_normal();
        auto inside = [&](const std::string &resolved) {
            const fs::path rel = fs::path(resolved).lexically_relative(dir);
            return !rel.empty() && rel.is_relative() && *rel.begin() != "..";
        };
        auto exists = [&](const std::string &p) { return fs::exists(p, ec) && !fs::is_directory(p, ec); };
        auto usersOf = [](const FileRef &f) {
            std::string u;
            for (const auto &x : f.users) u += (u.empty() ? "" : ", ") + x;
            return u;
        };
        const bool dry = c.has("dry-run");
        switch (c.kind)
        {
        case K::ProjectFiles:
        {
            mOutput.clear();
            for (const auto &f : fileRefs())
            {
                const bool there = exists(f.resolved);
                if (c.has("missing") && there) continue;
                mOutput += f.named + "  \xC2\xB7 " + (!there ? "missing" : inside(f.resolved) ? "in the song's folder" : "outside") +
                           "  \xC2\xB7 " + usersOf(f) + "\n";
            }
            if (mOutput.empty()) mOutput = c.has("missing") ? "no file is missing\n" : "the song names no file\n";
            return true;
        }
        case K::ProjectCollect:
        {
            // R-COLLECT-1: what is outside the folder, copied into Samples/ — an .sfz with its sounds, their layout kept
            const fs::path samples = dir / "Samples";
            std::map<std::string, std::string> to;
            std::string said, missing;
            int copied = 0;
            std::vector<std::pair<fs::path, fs::path>> copies; // from → to, made only once everything is decided
            std::set<std::string> planned;                     // targets this collect makes (so two sources do not meet)
            auto take = [&](const fs::path &from, fs::path target, bool reused) {
                if (!reused) { copies.emplace_back(from, target); planned.insert(target.string()); }
            };
            for (const auto &f : fileRefs())
            {
                if (inside(f.resolved)) continue;
                if (!exists(f.resolved)) { missing += "  " + f.named + " \xE2\x80\x94 " + usersOf(f) + "\n"; continue; }
                const fs::path from(f.resolved);
                if (isSfz(f.resolved))
                {
                    std::ifstream in(from, std::ios::binary);
                    const std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    const auto sounds = sfzSounds(parseSfz(body));
                    std::string why;
                    for (const auto &s : sounds)
                    {
                        const fs::path sp(s);
                        if (sp.is_absolute() || std::any_of(sp.begin(), sp.end(), [](const fs::path &x) { return x == ".."; }))
                            why = "it names " + s + ", outside its folder";
                    }
                    if (!why.empty()) { said += "  not collected: " + f.named + " \xE2\x80\x94 " + why + "\n"; continue; }
                    // its own folder in Samples/: "<stem>", or "<stem> 2" when that holds another instrument
                    fs::path into;
                    for (int n = 1;; ++n)
                    {
                        into = samples / (n == 1 ? from.stem().string() : from.stem().string() + " " + std::to_string(n));
                        const fs::path there = into / from.filename();
                        if ((!fs::exists(there, ec) && !planned.count(there.string())) || (fs::exists(there, ec) && sameBytes(there, from))) break;
                    }
                    const fs::path there = into / from.filename();
                    const bool reused = fs::exists(there, ec);
                    take(from, there, reused);
                    int found = 0;
                    for (const auto &s : sounds)
                    {
                        const fs::path sf = from.parent_path() / s, st = into / s;
                        if (!exists(sf.string())) { said += "  " + f.named + ": its sound " + s + " is missing \xE2\x80\x94 not copied\n"; continue; }
                        if (!fs::exists(st, ec)) take(sf, st, false);
                        ++found;
                    }
                    to[f.resolved] = relativePath(there.string());
                    said += "  " + f.named + " \xE2\x86\x92 " + to[f.resolved] + " (and " + std::to_string(found) + (found == 1 ? " sound" : " sounds") + ")" +
                            (reused ? " \xE2\x80\x94 already there" : "") + "\n";
                    ++copied;
                    continue;
                }
                bool reused = false;
                const fs::path target = freeName(samples, from, planned, reused);
                take(from, target, reused);
                to[f.resolved] = relativePath(target.string());
                said += "  " + f.named + " \xE2\x86\x92 " + to[f.resolved] + (reused ? " \xE2\x80\x94 already there" : "") + "\n";
                ++copied;
            }
            mOutput = copied ? std::string(dry ? "would collect " : "collected ") + std::to_string(copied) + (copied == 1 ? " file" : " files") + " into Samples/:\n" + said
                             : "nothing to collect \xE2\x80\x94 every file the song names is in its folder\n" + said;
            if (!missing.empty()) mOutput += "missing \xE2\x80\x94 relink them (`project relink`):\n" + missing;
            if (dry || to.empty()) return true;
            for (const auto &cp : copies)
            {
                fs::create_directories(cp.second.parent_path(), ec);
                if (!fs::copy_file(cp.first, cp.second, fs::copy_options::skip_existing, ec) || ec)
                {
                    err = "cannot copy " + cp.first.string() + " to " + cp.second.string() + (ec ? ": " + ec.message() : "");
                    return false;
                }
            }
            if (!rewriteFiles(to, err)) return false;
            changed("project.collected", std::to_string(copied));
            return true;
        }
        case K::ProjectRelink:
        {
            std::map<std::string, std::string> to;
            const auto refs = fileRefs();
            if (c.has("folder"))
            {
                // every missing file looked for by its name under the folder — the first by path order
                const fs::path where = fs::absolute(fs::path(c.flag("folder"))).lexically_normal();
                if (!fs::is_directory(where, ec)) { err = "no folder " + c.flag("folder"); return false; }
                std::vector<fs::path> files;
                for (auto it = fs::recursive_directory_iterator(where, fs::directory_options::skip_permission_denied, ec);
                     it != fs::recursive_directory_iterator(); it.increment(ec))
                    if (it->is_regular_file(ec)) files.push_back(it->path());
                std::sort(files.begin(), files.end());
                int missingCount = 0, found = 0;
                mOutput.clear();
                for (const auto &f : refs)
                {
                    if (exists(f.resolved)) continue;
                    ++missingCount;
                    const std::string name = fs::path(f.resolved).filename().string();
                    std::vector<fs::path> hits;
                    for (const auto &x : files)
                        if (x.filename().string() == name) hits.push_back(x);
                    if (hits.empty()) { mOutput += "  not found: " + f.named + "\n"; continue; }
                    to[f.resolved] = relativePath(hits.front().string());
                    ++found;
                    mOutput += "  " + f.named + " \xE2\x86\x92 " + to[f.resolved] +
                               (hits.size() > 1 ? " (" + std::to_string(hits.size()) + " found there \xE2\x80\x94 the first taken)" : "") + "\n";
                }
                mOutput = (missingCount == 0 ? std::string("no file is missing\n")
                                             : std::string(dry ? "would relink " : "relinked ") + std::to_string(found) + " of " + std::to_string(missingCount) +
                                                   " missing " + (missingCount == 1 ? "file" : "files") + ":\n") + mOutput;
            }
            else
            {
                if (c.args.empty() || !c.has("to")) { err = "project relink <file> --to <new file>, or project relink --folder <dir>"; return false; }
                const std::string want = resolvePath(c.arg(0));
                const fs::path now = fs::absolute(fs::path(c.flag("to"))).lexically_normal();
                if (!exists(now.string())) { err = "cannot read " + c.flag("to"); return false; }
                for (const auto &f : refs)
                    if (f.named == c.arg(0) || f.resolved == want) to[f.resolved] = relativePath(now.string());
                if (to.empty()) { err = "the song names no file " + c.arg(0) + " (`project files` lists them)"; return false; }
                mOutput = std::string(dry ? "would relink " : "relinked ") + c.arg(0) + " \xE2\x86\x92 " + to.begin()->second + "\n";
            }
            if (dry || to.empty()) return true;
            for (const auto &x : to) mOffline.erase(x.first);
            if (!rewriteFiles(to, err)) return false;
            changed("project.relinked", std::to_string(to.size()));
            return true;
        }
        case K::ClipRelink:
        {
            Clip *cl = mProject.clip(c.arg(0));
            if (!cl) { err = "no clip " + c.arg(0); return false; }
            if (!cl->isAudio()) { err = cl->id + " plays notes, not a file"; return false; }
            const fs::path now = fs::absolute(fs::path(c.arg(1))).lexically_normal();
            if (!exists(now.string())) { err = "cannot read " + c.arg(1); return false; }
            if (c.has("take"))
            {
                double n = 0;
                if (!parseNumber(c.flag("take"), n) || n != (int)n || n < 1 || n > (double)cl->takes.size())
                {
                    err = cl->id + " has " + std::to_string(cl->takes.size()) + (cl->takes.size() == 1 ? " take" : " takes") + " — --take is 1 … that";
                    return false;
                }
                cl->takes[(size_t)n - 1].src = relativePath(now.string());
            }
            else cl->src = relativePath(now.string());
            changed("clip.relinked", cl->id);
            mOutput = cl->id + "\n";
            return true;
        }
        default:
            return false;
        }
    }
}
}
