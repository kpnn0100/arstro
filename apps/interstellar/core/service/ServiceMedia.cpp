/*
 *  interstellar_core — relinking media (R-MEDIA-3).
 *
 *  A source's file is the `.isp`'s (`#rackobj media=`); Cosmo's slot is identified by the path it was
 *  added with and cannot be renamed from outside. So a relink moves the `.isp`'s path and teaches the
 *  decoder seam where the stored path's file now is (`FrameSelector::fileFor`, as `frameFor` already
 *  answers which frame) — Cosmo keeps its slot, its grade and its history; the timeline decodes the
 *  new file directly.
 *
 *  Cosmo only decodes on a load, so the rack is reloaded from its `.cmp` — which, while a source is
 *  offline, is as old as the last save (Cosmo cannot save then: D-2). What is only in memory is put
 *  back after the reload: every node's own params and bypass, written THROUGH Cosmo exactly as an
 *  undo restore writes them. A structural change since the save (a group made, a node renamed or
 *  removed) cannot be put back that way, so a relink then is refused, saying why — never a silent
 *  loss.
 */
#include "ServiceInternal.h"
#include "FrameSelector.h"
#include "Project.h"
#include <algorithm>
#include <filesystem>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace arstro
{
namespace interstellar
{
    using CK = Command::Kind;
    using EK = Event::Kind;

    namespace
    {
        // every file and folder under `root`, by lower-case name — searched once per relink
        struct Index
        {
            std::map<std::string, std::vector<std::string>> files, dirs;
        };

        Index indexFolder(const std::string &root)
        {
            Index ix;
            std::error_code ec;
            int budget = 500000;   // a whole drive is not a search folder; stop rather than stall
            for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end && budget-- > 0;
                 it.increment(ec))
            {
                if (ec) { ec.clear(); continue; }
                const std::string n = seq::lower(it->path().filename().string());
                (it->is_directory(ec) ? ix.dirs : ix.files)[n].push_back(it->path().string());
            }
            for (auto *m : {&ix.files, &ix.dirs})
                for (auto &kv : *m) std::sort(kv.second.begin(), kv.second.end());
            return ix;
        }
    }

    bool InterstellarService::mediaCommand(const Command &c)
    {
        Project &P = *mProject;
        const std::string verb = std::string(specFor(c.kind)->verb);
        if (c.kind == CK::MediaOffline)
        {
            std::ostringstream o;
            int n = 0;
            for (const auto &r : mModel.rack)
                if (r.failed && !r.group)
                {
                    ++n;
                    o << (r.bindName.empty() ? r.cosmoName : r.bindName) << "  " << r.media << "  ("
                      << (r.offlineWhy.empty() ? std::string("missing") : r.offlineWhy) << ")\n";
                }
            mOutput = n ? o.str() : "no source is offline\n";
            return true;
        }
        if (mPending) return fail(verb + ": the rack is still loading — `wait rack.loaded` first");

        // ── what moves where ──
        std::vector<std::pair<RackObj *, std::string>> moves;
        std::vector<std::string> notFound;
        auto vendorRefused = [&](const std::string &file) {
            const VendorRaw *v = vendorRawFor(file);
            if (!v || (mHost.hasVendorDecoder && mHost.hasVendorDecoder(v->ext))) return false;
            fail(verb + ": " + file + " is " + v->format + " — decoding it needs " + v->sdk + ", which is not in this build");
            return true;
        };
        if (c.has("search"))
        {
            if (!c.args.empty()) return fail(verb + ": --search relinks every offline source — name none");
            const std::string root = fs::absolute(c.flag("search")).lexically_normal().string();
            if (!fs::is_directory(root)) return fail(verb + ": no folder " + c.flag("search"));
            const Index ix = indexFolder(root);
            for (const auto &r : mModel.rack)
            {
                if (!r.failed || r.group || !r.offlineWhy.empty()) continue;   // a missing SDK is not a missing file
                RackObj *ro = P.rackObj(r.rackObj);
                if (!ro || ro->media.empty()) continue;
                std::string found;
                seq::Pattern pat;
                if (seq::parse(ro->media, pat))
                {
                    // a sequence: a folder of the same name holding the same run
                    const std::string dirName = seq::lower(fs::path(pat.dir).filename().string());
                    const auto d = ix.dirs.find(dirName);
                    if (d != ix.dirs.end())
                        for (const auto &dir : d->second)
                        {
                            const std::string p = (fs::path(dir) / (pat.prefix + "%0" + std::to_string(pat.width) + "d" + pat.suffix)).string();
                            if (seq::firstFrameExists(p)) { found = p; break; }
                        }
                }
                else
                {
                    std::string file;
                    double t = 0;
                    splitFrameSelector(ro->media, file, t);
                    const auto f = ix.files.find(seq::lower(fs::path(file).filename().string()));
                    if (f != ix.files.end()) found = f->second.front();
                }
                if (found.empty()) notFound.push_back(ro->name);
                else moves.push_back({ro, found});
            }
            if (moves.empty())
                return fail(verb + ": nothing under " + root + " has an offline source's name" +
                            (notFound.empty() ? std::string(" (no source is offline)") : " (looked for " + std::to_string(notFound.size()) + ")"));
        }
        else
        {
            if (c.args.size() != 2) return fail(verb + ": <source> <file>, or --search <folder>");
            RackObj *ro = P.rackObj(P.idForRef(c.arg(0)));
            if (!ro || ro->kind == "group" || ro->media.empty()) return fail(verb + ": no source named " + c.arg(0));
            std::string abs = fs::absolute(c.arg(1)).lexically_normal().string();
            if (fs::is_directory(abs))
            {
                std::string pattern, why;
                if (!seq::fromFolder(abs, pattern, why)) return fail(verb + ": " + why);
                abs = pattern;
            }
            else if (seq::isSequence(abs) ? !seq::firstFrameExists(abs) : !fs::exists(abs)) return fail(verb + ": no such file: " + c.arg(1));
            if (looksLikeVideo(abs) != looksLikeVideo(ro->media))
                return fail(verb + ": " + ro->name + " is " + (looksLikeVideo(ro->media) ? "a video" : "a still") + " and " + c.arg(1) + " is not — "
                            "a relink points at the same picture where it is now");
            if (vendorRefused(abs)) return false;
            moves.push_back({ro, abs});
        }

        // ── the reload must not lose what is only in memory ──
        std::string cmp;
        if (mRack.isOpen() && P.hasRack)
        {
            cmp = resolvePath(P.rack.path);
            ColourTree disk;
            std::string err;
            if (!colourTreeFromCmp(cmp, disk, err)) return fail(verb + ": " + err);
            const auto &live = mRack.model().nodes;
            bool same = disk.size() == live.size();
            for (size_t i = 0; same && i < live.size(); ++i)
            {
                const int parent = live[i].parent < 0 ? -1 : mRack.indexOf(live[i].parent);
                same = disk[i].group == live[i].group && disk[i].name == live[i].name && disk[i].parent == parent;
            }
            if (!same)
                return fail(verb + ": the rack's groups or names changed since it was last saved, and Cosmo cannot save while a "
                                   "source is offline (D-2) — a relink reloads the rack, which would undo that change. Undo the "
                                   "regrouping or renaming, relink, then redo it");
        }

        std::ostringstream o;
        for (const auto &m : moves)
        {
            // Cosmo's slot keeps the path it was added with; the .isp remembers it, so the slot is found
            if (m.first->cosmoPath.empty()) m.first->cosmoPath = m.first->media;
            m.first->media = relativePath(m.second);
            if (resolvePath(m.first->cosmoPath) == resolvePath(m.first->media)) m.first->cosmoPath.clear();   // back where it was added
            o << m.first->name << " → " << m.second << "\n";
        }
        if (!notFound.empty())
        {
            o << "not found: ";
            for (size_t i = 0; i < notFound.size(); ++i) o << (i ? ", " : "") << notFound[i];
            o << "\n";
        }
        markDirty();
        if (!cmp.empty())
        {
            auto keep = std::make_shared<UndoState>();
            captureState(*keep);   // every node's own params and bypass, as they are now
            ColourTree entries;
            std::string err;
            if (!colourTreeFromCmp(cmp, entries, err)) return fail(verb + ": " + err);
            mPending.reset(new PendingRack());
            mPending->kind = PendingRack::Kind::Reload;
            mPending->restore = keep;
            bindFromCmp(entries);   // the decoder's map: each stored path → the file the .isp names now
            if (!mRack.beginOpen(cmp, err)) { mPending.reset(); return fail(verb + ": " + err); }
        }
        mSync->sources.clear();   // the timeline opens the new files
        bumpFrame();
        mOutput = o.str();
        for (const auto &m : moves) emit(Event(EK::RackChanged).with("what", "relinked").with("node", m.first->id));
        return true;
    }
}
}
