// SolarisService — the song's file, watched (R-COLLAB-1; docs/discussion.md Round 19). The BASE is the text this window
// last read or wrote; the file is looked at once a second (`watchFile`, from `pump`) or now (`project sync`). Changed by
// someone else: with nothing unsaved here the song becomes the file's; with edits here the two are merged node by node
// (`mergeSongs`, the model's) as ONE edit, the base then theirs. A conflict is kept as here and asked until `--take`
// answers it; meanwhile a save is refused, and a further change to the file waits — one merge at a time.
#include "Merge.h"
#include "SolarisService.h"
#include "Versions.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace arstro
{
namespace solaris
{
    namespace fs = std::filesystem;
    using K = Command::Kind;

    bool SolarisService::readDisk(std::string &raw) const
    {
        if (mPath.empty()) return false;
        std::ifstream f(mPath, std::ios::binary);
        if (!f) return false;
        std::stringstream ss;
        ss << f.rdbuf();
        raw = ss.str();
        return true;
    }

    void SolarisService::noteDisk(const std::string &raw, const std::string &song)
    {
        mDiskRaw = raw;
        mDiskBase = song;
        mTheirs = song;
        mDiskSize = -1; // its size and time are taken at the next look (the path may not be the song's yet)
    }

    bool SolarisService::syncEdits(const Command &c)
    {
        if (!mOpen) return false;
        if (c.has("take")) return c.flag("take") == "theirs" && !mConflicts.empty();
        if (!mConflicts.empty()) return false; // refused: answered first
        std::string raw;
        return readDisk(raw) && raw != mDiskRaw;
    }

    void SolarisService::watchFile()
    {
        if (!mOpen || mPath.empty() || mDepth > 0) return;
        const double now = steadySeconds();
        if (now - mSyncChecked < kSyncEveryS) return;
        mSyncChecked = now;
        std::error_code ec;
        const auto size = fs::file_size(mPath, ec);
        if (ec) return; // gone, or not there yet: nothing to follow
        const auto when = fs::last_write_time(mPath, ec);
        if (ec || ((long long)size == mDiskSize && when == mDiskTime)) return;
        std::string raw;
        if (!readDisk(raw)) return;
        if (raw == mDiskRaw) // this window's own save, or a touch: nothing changed
        {
            mDiskSize = (long long)size;
            mDiskTime = when;
            if (mUnreadable > 0) { mUnreadable = 0; refreshModel(); } // whole again
            return;
        }
        if (!mConflicts.empty()) // one merge at a time: the asked ones answered first
        {
            if (!mSyncWaiting)
            {
                mSyncWaiting = true;
                refreshModel();
            }
            return;
        }
        Project probe;
        std::string why;
        if (!songOf(raw, probe, why)) // a copy still arriving, Git's markers: waited for, said once
        {
            if (++mUnreadable == 3)
            {
                emit(Event(Event::Kind::Info).with("text", mPath + " does not read as a song yet (" + why + ") — waiting for it"));
                refreshModel();
            }
            return;
        }
        std::string err;
        dispatchText("project sync", err); // ONE edit, through the one way in — the line a script would send
    }

    bool SolarisService::syncCommand(const Command &c, std::string &err)
    {
        if (!requireOpen(err)) return false;
        auto asked = [this]() {
            std::string s;
            for (const auto &k : mConflicts) s += (s.empty() ? "" : ", ") + k.node;
            return s;
        };
        if (c.has("take"))
        {
            const std::string side = c.flag("take");
            if (side != "theirs" && side != "mine") { err = "--take is theirs or mine"; return false; }
            if (mConflicts.empty()) { err = "nothing to answer — no conflict is asked"; return false; }
            std::vector<std::string> nodes;
            if (!c.arg(0).empty())
            {
                const bool known = std::any_of(mConflicts.begin(), mConflicts.end(), [&c](const MergeConflict &k) { return k.node == c.arg(0); });
                if (!known) { err = "`" + c.arg(0) + "` is not asked (asked: " + asked() + ")"; return false; }
                nodes.push_back(c.arg(0));
            }
            else
                for (const auto &k : mConflicts) nodes.push_back(k.node);
            if (side == "theirs")
            {
                std::string text = serializeProject(mProject);
                for (const auto &n : nodes)
                    if (!takeTheirs(text, mTheirs, n, err)) return false;
                Project q;
                std::string why;
                if (!songOf(text, q, why)) { err = "theirs does not stand here: " + why; return false; }
                tidyOrders(q);
                if (!mProject.version.empty() && q.versionOf(mProject.version)) openVersion(q, mProject.version, why); // it stays open
                mProject = std::move(q);
                for (const auto &n : nodes) changed("sync.taken", n);
            }
            mConflicts.erase(std::remove_if(mConflicts.begin(), mConflicts.end(),
                                            [&nodes](const MergeConflict &k) { return std::find(nodes.begin(), nodes.end(), k.node) != nodes.end(); }),
                             mConflicts.end());
            mSyncedClean = false;
            mOutput = "answered " + std::to_string(nodes.size()) + " (" + side + ")" +
                      (mConflicts.empty() ? std::string() : "; still asked: " + asked()) + "\n";
            return true;
        }
        if (!c.arg(0).empty()) { err = "name a node with --take theirs|mine"; return false; }
        std::string raw;
        if (!readDisk(raw)) { err = "cannot read " + mPath; return false; }
        if (raw == mDiskRaw)
        {
            mUnreadable = 0; // whole again
            mOutput = "the file is as this window last read or wrote it\n";
            return true;
        }
        if (!mConflicts.empty())
        {
            mSyncWaiting = true;
            err = "answer the " + std::to_string(mConflicts.size()) + " asked first (" + asked() + ") — the file's newer changes are merged then";
            return false;
        }
        Project theirsSong;
        std::string why;
        if (!songOf(raw, theirsSong, why)) { ++mUnreadable; err = mPath + " does not read as a song yet: " + why; return false; }
        mUnreadable = 0;
        const std::string theirs = serializeProject(theirsSong);
        const std::string open = mProject.version;
        MergeResult r;
        Project next;
        const bool reload = !mModel.dirty; // nothing of mine to keep: the song becomes the file's
        if (reload) next = std::move(theirsSong);
        else
        {
            r = mergeSongs(mDiskBase, serializeProject(mProject), theirs);
            if (!songOf(r.text, next, why)) { err = "the merge does not stand as a song: " + why; return false; }
            tidyOrders(next);
            if (const auto errors = validateProject(next); !errors.empty()) { err = "the merge does not stand as a song: " + errors.front(); return false; }
        }
        if (!open.empty() && next.versionOf(open)) openVersion(next, open, why); // the version open here stays open
        mProject = std::move(next);
        mConflicts = r.conflicts;
        mDiskRaw = raw;
        mDiskBase = theirs;
        mTheirs = theirs;
        mDiskSize = -1;
        mSyncWaiting = false;
        mSyncedClean = reload;
        ++mSyncCount;
        const std::string file = fs::path(mPath).filename().string();
        if (reload) mSyncSaid = "Reloaded — " + file + " changed on disk";
        else
        {
            mSyncSaid = "Merged the changes to " + file + " — " + std::to_string(r.fromTheirs) + " from the file, " + std::to_string(r.fromMine) + " kept here";
            if (!r.conflicts.empty()) mSyncSaid += "; " + std::to_string(r.conflicts.size()) + " to answer";
        }
        mPending.push_back(Event(Event::Kind::ProjectSynced).with("how", reload ? "reloaded" : "merged").with("theirs", (long long)r.fromTheirs)
                               .with("mine", (long long)r.fromMine).with("conflicts", (long long)r.conflicts.size()).with("renamed", (long long)r.renamed.size()));
        mOutput = mSyncSaid + "\n";
        for (const auto &rn : r.renamed) mOutput += "renumbered " + rn.first + " → " + rn.second + " (both sides added " + rn.first + ")\n";
        for (const auto &k : r.conflicts) mOutput += "asked " + k.node + ": " + k.what + " — " + k.how + "\n";
        return true;
    }
}
}
