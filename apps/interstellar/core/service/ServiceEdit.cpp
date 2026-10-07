/*
 *  InterstellarService — Cosmo's Edit, Settings and Preset menus, as commands (R-EDIT, R-SET).
 *
 *  UNDO is one history across both authorities. An edit is recorded as a pair of states — the
 *  `.isp` text and each bound rack node's own params and bypass — captured around the command.
 *  Undo restores the earlier state: the project by re-parsing its text, the rack by writing the
 *  earlier params back THROUGH Cosmo (a `set` of every key), so the `.cmp` and Cosmo's own history
 *  stay the authority and nothing here is a second copy of colour. A change to the rack's node set
 *  (add, group, ungroup, duplicate, import, open) is not undoable — exactly as in Cosmo, whose
 *  history is per node — and starts a fresh history rather than leave entries that point at nodes
 *  that moved.
 *
 *  Consecutive writes to the same addresses within half a second are ONE step: a slider drag sends
 *  dozens of `set` lines and is one thing to undo.
 */
#include "ServiceInternal.h"
#include "Project.h"
#include "Versions.h"
#include "core/AppSettings.h"
#include "core/PresetLibrary.h"
#include "engine/EditParamsIO.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
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
        double wallMs()
        {
            using namespace std::chrono;
            return (double)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        }

        std::string joinNames(const std::vector<std::string> &v)
        {
            std::string s;
            for (size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + v[i];
            return s;
        }
    }

    bool InterstellarService::UndoState::operator==(const UndoState &o) const
    {
        if (project != o.project || rack.size() != o.rack.size() || nodeIds != o.nodeIds) return false;
        for (size_t i = 0; i < rack.size(); ++i)
            if (rack[i].rackObj != o.rack[i].rackObj || rack[i].params != o.rack[i].params || rack[i].bypass != o.rack[i].bypass)
                return false;
        return true;
    }

    /** Every key of a params set except masks: `set mask=` APPENDS in Cosmo, so writing masks
     *  back would duplicate them. Interstellar never edits masks, so no edit it records can
     *  differ in them. */
    std::vector<std::pair<std::string, std::string>> paramFields(const EditParams &p)
    {
        std::vector<std::pair<std::string, std::string>> out;
        std::istringstream in(serializeParams(p));
        std::string line;
        while (std::getline(in, line))
        {
            const auto eq = line.find('=');
            if (eq == std::string::npos || line.compare(0, eq, "mask") == 0) continue;
            out.emplace_back(line.substr(0, eq), line.substr(eq + 1));
        }
        return out;
    }

    bool InterstellarService::undoable(Command::Kind k)
    {
        switch (k)
        {
            case CK::Set: case CK::Revert: case CK::RackRename: case CK::RackFrame: case CK::ColourWorking:
            case CK::TimelineNew: case CK::TimelinePin: case CK::TimelineUnpin: case CK::TimelineFreeze:
            case CK::TimelineThaw: case CK::TimelineRebase: case CK::TimelineDelete:
            case CK::TrackAdd: case CK::ClipAdd: case CK::ClipTrim: case CK::ClipSplit: case CK::ClipMove:
            case CK::ClipDelete: case CK::ClipRoll: case CK::ClipSlip: case CK::ClipSpeed:
            case CK::TransitionAdd: case CK::MarkerAdd: case CK::FxAdd: case CK::FxDelete:
            case CK::AudioTrackAdd: case CK::AudioClipAdd: case CK::GradePaste: case CK::PresetApply:
            case CK::ClipPaste: case CK::EffectAdd: case CK::EffectRemove: case CK::EffectMove:
            case CK::KeyAdd: case CK::KeyRemove: case CK::KeySet: case CK::KeyClear: case CK::KeyShift: case CK::KeyPaste:
            case CK::KeyMark: case CK::KeyMode:
            case CK::EditInsert: case CK::EditOverwrite: case CK::MulticamNew: case CK::MulticamAngle: case CK::StillApply:
            case CK::CaptionImport: case CK::CaptionAdd: case CK::CaptionRemove:
                return true;
            default: return false;
        }
    }

    bool InterstellarService::structural(Command::Kind k)
    {
        switch (k)
        {
            case CK::ProjectNew: case CK::ProjectOpen: case CK::ProjectClose: case CK::RackImport:
            case CK::RackAdd: case CK::RackGroupNew: case CK::RackUngroup: case CK::RackDuplicate: case CK::RackRemove:
            case CK::NodeSerial: case CK::NodeParallel: case CK::NodeRemove:
                return true;
            default: return false;
        }
    }

    void InterstellarService::captureState(UndoState &out) const
    {
        out.project = mProject->serialize();
        out.rack.clear();
        out.nodeIds.clear();
        if (mRack.isOpen())
            for (const auto &n : mRack.model().nodes) out.nodeIds.push_back(n.node);
        for (const auto &kv : mNodeOf)
        {
            EditParams own;
            if (!mRack.ownParams(kv.second, own)) continue;
            UndoState::Node n;
            n.rackObj = kv.first;
            n.params = serializeParams(own);
            for (const auto &m : mRack.model().nodes)
                if (m.node == kv.second) n.bypass = m.bypass;
            out.rack.push_back(std::move(n));
        }
    }

    bool InterstellarService::applyState(const UndoState &s, std::string &err, bool keepMedia)
    {
        if (s.project != mProject->serialize())
        {
            Project p;
            if (!p.parse(s.project, err)) return false;
            // Which timeline is OPEN is presentation: undoing a cut must not switch the version
            // being looked at.
            if (p.timeline(mProject->current)) p.current = mProject->current;
            // R-MEDIA-2/3: which proxies exist, whether the monitor uses them, and where a source's file
            // is are media management, outside undo like a render — a proxy finished or a file relinked
            // after an edit must survive undoing it
            if (keepMedia) p.proxies = mProject->proxies;
            if (keepMedia) p.stills = mProject->stills;   // R-CLR-4: the gallery is a reference shelf, outside undo
            for (auto &ro : p.rackObjs)
                if (const RackObj *now = keepMedia ? mProject->rackObj(ro.id) : nullptr)
                {
                    ro.proxy = now->proxy;
                    ro.proxyScale = now->proxyScale;
                    ro.media = now->media;   // R-MEDIA-3: a relink, too — Cosmo decoded the file where it is now
                    ro.cosmoPath = now->cosmoPath;
                }
            *mProject = std::move(p);
        }
        for (const auto &n : s.rack)
        {
            const int node = cosmoNodeOf(n.rackObj);
            if (node < 0) continue;
            EditParams cur, want;
            if (!mRack.ownParams(node, cur)) continue;
            deserializeParams(n.params, want);
            if (serializeParams(cur) != n.params && !mRack.setParams(node, paramFields(want), err)) return false;
            bool bypass = false;
            for (const auto &m : mRack.model().nodes)
                if (m.node == node) bypass = m.bypass;
            if (bypass != n.bypass && !mRack.setBypass(node, n.bypass, err)) return false;
        }
        markDirty();
        bumpFrame();
        return true;
    }

    void InterstellarService::recordEdit(const Command &c, const UndoState &before)
    {
        UndoState after;
        captureState(after);
        if (after == before) return;
        if (after.nodeIds != before.nodeIds) { clearHistory(); return; }   // the node set moved

        std::string key;
        if (c.kind == CK::Set)
        {
            std::vector<std::string> addrs;
            for (const auto &f : c.fields) addrs.push_back(f.first);
            std::sort(addrs.begin(), addrs.end());
            key = "set:" + currentTimeline() + ":" + joinNames(addrs);
        }
        std::string label = formatCommand(c);
        if (c.kind == CK::Set)
        {
            label = "set";
            for (size_t i = 0; i < c.fields.size() && i < 2; ++i) label += " " + c.fields[i].first;
            if (c.fields.size() > 2) label += " …";
        }
        if (label.size() > 48) label = label.substr(0, 47) + "…";

        const double now = wallMs();
        if (!key.empty() && !mUndo.empty() && mRedo.empty() && mUndo.back().key == key && now - mUndo.back().atMs < 500.0)
        {
            mUndo.back().after = std::move(after);   // the same drag: one step
            mUndo.back().atMs = now;
            return;
        }
        UndoEntry e;
        e.label = label;
        e.key = key;
        e.atMs = now;
        e.before = before;
        e.after = std::move(after);
        mUndo.push_back(std::move(e));
        if (mUndo.size() > 200) mUndo.erase(mUndo.begin());
        mRedo.clear();
        emit(Event(EK::HistoryChanged).with("did", "edit").with("label", label).with("canUndo", true).with("canRedo", false));
    }

    void InterstellarService::clearHistory()
    {
        if (mUndo.empty() && mRedo.empty()) return;
        mUndo.clear();
        mRedo.clear();
        emit(Event(EK::HistoryChanged).with("did", "cleared").with("label", "").with("canUndo", false).with("canRedo", false));
    }

    bool InterstellarService::editCommand(const Command &c)
    {
        Project &P = *mProject;
        std::string err;
        auto rackObjRef = [&](const std::string &ref, RackObj *&out) {
            out = P.rackObj(P.idForRef(ref));
            if (out) return true;
            const auto near = nearest(ref, P.bindNames());
            return fail("no rack node named " + ref + (near.empty() ? "" : " (did you mean: " + joinNames(near) + "?)"));
        };
        // Colour written THROUGH to Cosmo happens on a root timeline only; a version holds deltas.
        auto rootOnly = [&](const std::string &what) {
            const Timeline *t = P.timeline(currentTimeline());
            if (t && t->pinned()) return fail(what + ": timeline " + t->name + " is pinned at " + t->pinCommit() + " (R-VER-3)");
            if (t && !t->base.empty())
                return fail(what + ": " + t->name + " is a version — it stores overrides, not grades. Do this on " +
                            (P.timeline(t->base) ? P.timeline(t->base)->name : t->base) + ", or set addresses here");
            return true;
        };

        switch (c.kind)
        {
            case CK::Undo:
            case CK::Redo:
            {
                auto &from = c.kind == CK::Undo ? mUndo : mRedo;
                auto &to = c.kind == CK::Undo ? mRedo : mUndo;
                if (from.empty()) return fail(c.kind == CK::Undo ? "nothing to undo" : "nothing to redo");
                UndoEntry e = std::move(from.back());
                from.pop_back();
                if (!applyState(c.kind == CK::Undo ? e.before : e.after, err))
                {
                    clearHistory();
                    return fail(std::string(c.kind == CK::Undo ? "undo" : "redo") + " failed: " + err);
                }
                const std::string label = e.label;
                to.push_back(std::move(e));
                emit(Event(EK::HistoryChanged)
                         .with("did", c.kind == CK::Undo ? "undo" : "redo")
                         .with("label", label)
                         .with("canUndo", !mUndo.empty())
                         .with("canRedo", !mRedo.empty()));
                return true;
            }
            case CK::GradeCopy:
            {
                RackObj *ro = nullptr;
                if (!rackObjRef(c.arg(0), ro)) return false;
                ColourTree tree;
                std::map<NodeId, int> idx;
                std::string source;
                if (!colourTreeFor(currentTimeline(), tree, idx, source, err)) return fail("grade copy: " + err);
                if (!idx.count(ro->id)) return fail("grade copy: " + ro->name + " is offline");
                // What this node's own grade IS in the open version — overrides included.
                EditParams own = tree[(size_t)idx[ro->id]].own;
                applyDeltas(own, gradeDeltas(P, currentTimeline(), ro->id));
                mClipboard = own;
                mHasClipboard = true;
                mClipboardFrom = ro->name;
                emit(Event(EK::Info).with("text", "grade copied from " + ro->name));
                return true;
            }
            case CK::GradePaste:
            {
                if (!mHasClipboard) return fail("grade paste: nothing copied — `grade copy <node>` first");
                if (!rootOnly("grade paste")) return false;
                std::vector<RackObj *> targets;
                if (c.has("all"))
                {
                    for (auto &ro : P.rackObjs)
                        if (ro.kind != "group" && cosmoNodeOf(ro.id) >= 0) targets.push_back(&ro);
                }
                else if (!c.args.empty())
                {
                    for (const auto &a : c.args)
                    {
                        RackObj *ro = nullptr;
                        if (!rackObjRef(a, ro)) return false;
                        targets.push_back(ro);
                    }
                }
                else if (mRack.selectedNode() >= 0)
                {
                    RackObj *ro = P.rackObj(rackObjOfCosmo(mRack.selectedNode()));
                    if (ro) targets.push_back(ro);
                }
                if (targets.empty()) return fail("grade paste: name the nodes, or --all");
                const auto fields = paramFields(mClipboard);
                for (RackObj *ro : targets)
                {
                    const int node = cosmoNodeOf(ro->id);
                    if (node < 0) return fail("grade paste: " + ro->name + " is offline");
                    if (!mRack.setParams(node, fields, err)) return fail("grade paste: " + ro->name + ": " + err);
                }
                markDirty();
                bumpFrame();
                emit(Event(EK::ParamsChanged).with("address", "grade paste").with("value", mClipboardFrom).with("target", "rack"));
                return true;
            }
            case CK::RackUngroup:
            {
                if (mPending) return fail("the rack is still loading — `wait rack.loaded` first");
                RackObj *ro = nullptr;
                if (!rackObjRef(c.arg(0), ro)) return false;
                if (ro->kind != "group") return fail("rack ungroup: " + ro->name + " is not a group");
                const int node = cosmoNodeOf(ro->id);
                if (node < 0) return fail("rack ungroup: " + ro->name + " is offline");
                if (!mRack.ungroup(node, err)) return fail("rack ungroup: " + err);
                const NodeId id = ro->id;
                mNodeOf.erase(id);
                P.imageEffects.erase(std::remove_if(P.imageEffects.begin(), P.imageEffects.end(), [&](const Effect &f) { return f.node == id; }),
                                     P.imageEffects.end());   // a group's plugins go with the group (R-FX-5)
                P.rackObjs.erase(std::remove_if(P.rackObjs.begin(), P.rackObjs.end(), [&](const RackObj &r) { return r.id == id; }),
                                 P.rackObjs.end());
                markDirty();
                bumpFrame();
                emit(Event(EK::RackChanged).with("what", "ungrouped").with("node", id));
                return true;
            }
            case CK::PresetApply:
            case CK::PresetSave:
            {
                if (mHost.presetDir.empty()) return fail("presets are unavailable: the host gave no preset library");
                RackObj *ro = nullptr;
                if (c.has("node")) { if (!rackObjRef(c.flag("node"), ro)) return false; }
                else if (mRack.selectedNode() >= 0) ro = P.rackObj(rackObjOfCosmo(mRack.selectedNode()));
                if (!ro) return fail(std::string(specFor(c.kind)->verb) + ": select a rack node, or give --node <bind>");
                const int node = cosmoNodeOf(ro->id);
                if (node < 0) return fail(ro->name + " is offline");
                if (c.kind == CK::PresetApply)
                {
                    if (ro->kind == "group") return fail("preset apply: presets apply to a source, as in Cosmo — " + ro->name + " is a group");
                    if (!rootOnly("preset apply")) return false;
                    if (!mRack.presetApply(node, c.arg(0), err)) return fail("preset apply: " + err);
                    markDirty();
                    bumpFrame();
                    emit(Event(EK::ParamsChanged).with("address", ro->name).with("value", "preset " + c.arg(0)).with("target", "rack"));
                    return true;
                }
                if (!mRack.presetSave(node, c.arg(0), err)) return fail("preset save: " + err);
                rescanPresets();
                return true;
            }
            case CK::PresetImport:
            {
                if (mHost.presetDir.empty()) return fail("presets are unavailable: the host gave no preset library");
                const fs::path src(c.arg(0));
                if (src.extension() != ".apf") return fail("preset import: a preset is an .apf file, got " + src.string());
                if (!fs::exists(src)) return fail("preset import: no such file: " + src.string());
                std::error_code ec;
                fs::create_directories(mHost.presetDir, ec);
                fs::copy_file(src, fs::path(mHost.presetDir) / src.filename(), fs::copy_options::overwrite_existing, ec);
                if (ec) return fail("preset import: " + ec.message());
                rescanPresets();
                return true;
            }
            case CK::SettingsSet: return settingsCommand(c);
            default: return fail("not an edit command");
        }
    }

    // ── settings ───────────────────────────────────────────────────────────────────────────────

    bool InterstellarService::settingsCommand(const Command &c)
    {
        SettingsModel next = mSettings;
        for (const auto &f : c.fields)
        {
            char *end = nullptr;
            const long v = std::strtol(f.second.c_str(), &end, 10);
            const bool num = !f.second.empty() && end && *end == '\0';
            if (f.first == "cpuPercent")
            {
                if (!num || v < 1 || v > 100) return fail("settings: cpuPercent is 1..100 (Cosmo offers 25, 50, 75, 100)");
                next.cpuPercent = (int)v;
            }
            else if (f.first == "threads")
            {
                if (!num || v < 0 || v > 256) return fail("settings: threads is 0 (auto, from cpuPercent) or a count");
                next.threads = (int)v;
            }
            else if (f.first == "previewEdge")
            {
                if (!num || (v != 0 && (v < 256 || v > 16384))) return fail("settings: previewEdge is 0 (full) or 256..16384 px");
                next.previewEdge = (int)v;
            }
            else if (f.first == "useGpu")
            {
                if (f.second != "0" && f.second != "1") return fail("settings: useGpu is 0 or 1");
                next.useGpu = f.second == "1";
            }
            else if (f.first == "hardwareVideo" || f.first == "previewCache")
            {
                if (f.second != "0" && f.second != "1") return fail("settings: " + f.first + " is 0 or 1");
                (f.first == "hardwareVideo" ? next.hardwareVideo : next.previewCache) = f.second == "1";
            }
            else if (f.first == "autosave")
            {
                if (!num || (v != 0 && (v < 10 || v > 3600))) return fail("settings: autosave is 0 (off) or 10..3600 seconds");
                next.autosaveSeconds = (int)v;
            }
            else if (f.first == "keyLaneHeight")
            {
                if (!num || v < 80 || v > 600) return fail("settings: keyLaneHeight is 80..600 px");
                next.keyLaneHeight = (int)v;
            }
            else if (f.first == "uiScale")
            {
                const auto &ok = cosmo::AppSettings::uiScales();
                if (!num || std::find(ok.begin(), ok.end(), (int)v) == ok.end())
                {
                    std::string list;
                    for (int s : ok) list += (list.empty() ? "" : ", ") + std::to_string(s);
                    return fail("settings: uiScale is one of " + list);
                }
                next.uiScale = (int)v;
            }
            else
            {
                const auto near = nearest(f.first, {"cpuPercent", "threads", "previewEdge", "useGpu", "uiScale", "hardwareVideo", "previewCache", "keyLaneHeight", "autosave"});
                return fail("settings: no setting `" + f.first + "`" + (near.empty() ? "" : " (did you mean: " + joinNames(near) + "?)"));
            }
        }
        mSettings = next;
        applySettingsNow();
        if (!saveSettings()) emit(Event(EK::Error).with("why", "settings: could not write " + mHost.settingsPath));
        return true;
    }

    void InterstellarService::loadSettings()
    {
        if (mHost.settingsPath.empty()) return;
        std::ifstream f(mHost.settingsPath);
        std::string line;
        while (std::getline(f, line))
        {
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq);
            const int v = std::atoi(line.c_str() + eq + 1);
            // A file is repaired, never refused: a stray value falls back to the default.
            if (k == "cpuPercent" && v >= 1 && v <= 100) mSettings.cpuPercent = v;
            else if (k == "threads" && v >= 0 && v <= 256) mSettings.threads = v;
            else if (k == "previewEdge" && (v == 0 || (v >= 256 && v <= 16384))) mSettings.previewEdge = v;
            else if (k == "useGpu") mSettings.useGpu = v != 0;
            else if (k == "uiScale") mSettings.uiScale = cosmo::AppSettings::clampUiScale(v);
            else if (k == "hardwareVideo") mSettings.hardwareVideo = v != 0;
            else if (k == "previewCache") mSettings.previewCache = v != 0;
            else if (k == "keyLaneHeight" && v >= 80 && v <= 600) mSettings.keyLaneHeight = v;
            else if (k == "autosave" && (v == 0 || (v >= 10 && v <= 3600))) mSettings.autosaveSeconds = v;
        }
    }

    bool InterstellarService::saveSettings() const
    {
        if (mHost.settingsPath.empty()) return true;
        std::error_code ec;
        fs::create_directories(fs::path(mHost.settingsPath).parent_path(), ec);
        std::ofstream f(mHost.settingsPath, std::ios::trunc);
        f << "cpuPercent=" << mSettings.cpuPercent << "\nthreads=" << mSettings.threads << "\npreviewEdge="
          << mSettings.previewEdge << "\nuseGpu=" << (mSettings.useGpu ? 1 : 0) << "\nuiScale=" << mSettings.uiScale
          << "\nhardwareVideo=" << (mSettings.hardwareVideo ? 1 : 0) << "\npreviewCache=" << (mSettings.previewCache ? 1 : 0) << "\nkeyLaneHeight=" << mSettings.keyLaneHeight << "\nautosave=" << mSettings.autosaveSeconds << "\n";
        return (bool)f;
    }

    void InterstellarService::applySettingsNow()
    {
        // ONE budget: the hosted Cosmo's decode pool and the engine threads Interstellar's own
        // frame path runs on (par::setThreads) both come from it (R-SET-2).
        mBudget.setPercent(mSettings.cpuPercent);
        mBudget.setExplicitEngineThreads(mSettings.threads);
        mBudget.apply();
        cosmo::AppSettings cs;
        cs.cpuPercent = mSettings.cpuPercent;
        cs.threads = mSettings.threads;
        cs.previewEdge = mSettings.previewEdge > 0 ? mSettings.previewEdge : 4096;
        cs.useGpu = mSettings.useGpu;
        cs.uiScale = mSettings.uiScale;
        mRack.applySettings(cs);
        mGrade->setPreferGpu(mSettings.useGpu);
        mGpuWanted.store(mSettings.useGpu);   // R-GPU-1: every render thread's engine takes it at its next frame
        if (mAhead) mAhead->cv.notify_all();
        mSettings.cores = mBudget.cores();
        mSettings.engineThreads = mBudget.engineThreads();
        mSettings.decodeWorkers = mBudget.decodeWorkers();
        emit(Event(EK::SettingsChanged)
                 .with("cpuPercent", mSettings.cpuPercent)
                 .with("threads", mSettings.threads)
                 .with("previewEdge", mSettings.previewEdge)
                 .with("useGpu", mSettings.useGpu)
                 .with("uiScale", mSettings.uiScale)
                 .with("hardwareVideo", mSettings.hardwareVideo)
                 .with("previewCache", mSettings.previewCache)
                 .with("keyLaneHeight", mSettings.keyLaneHeight)
                 .with("autosave", mSettings.autosaveSeconds));
    }

    void InterstellarService::rescanPresets()
    {
        mPresets.clear();
        if (!mHost.presetDir.empty())
        {
            // Cosmo's own scanner, in Cosmo's order: depth first, folders before their presets.
            std::function<void(const std::vector<cosmo::PresetNode> &, const std::string &)> walk =
                [&](const std::vector<cosmo::PresetNode> &level, const std::string &folder) {
                    for (const auto &n : level)
                    {
                        if (n.folder) walk(n.kids, n.relPath);
                        else mPresets.push_back({n.relPath, folder});
                    }
                };
            walk(cosmo::PresetLibrary::scan(mHost.presetDir), std::string());
        }
        emit(Event(EK::PresetsChanged).with("count", (int)mPresets.size()));
    }
}
}
