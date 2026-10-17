#include "SolarisService.h"
#include "Versions.h"
#include "Bindings.h"
#include "ApiDoc.h"
#include "AppModelCodec.h"
#include "Compile.h"
#include "Format.h"
#include "device/Device.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    SolarisService::SolarisService(Host host) : mHost(std::move(host))
    {
        loadMachine();
        refreshModel();
    }

    void SolarisService::emit(const Event &e)
    {
        if (e.kind == Event::Kind::Error || e.kind == Event::Kind::CommandRejected)
            mModel.lastError = e.field("why");
        for (auto &s : mSinks) s(e);
    }

    void SolarisService::changed(const std::string &what, const std::string &node)
    {
        // held until the command lands: a refused command restores the project, so it must not have said it changed
        mPending.push_back(Event(Event::Kind::ProjectChanged).with("what", what).with("node", node));
    }

    bool SolarisService::requireOpen(std::string &err) const
    {
        if (mOpen) return true;
        err = "no song is open (project new <path.slp> or project open <path.slp>)";
        return false;
    }

    bool SolarisService::dispatchText(const std::string &line, std::string &err)
    {
        if (mDepth == 0) applyPendingEdits(); // R-VST-6: what a plugin's editor turned meanwhile lands first, in order
        const Command c = parseCommand(line, err);
        if (!err.empty())
        {
            mOutput.clear();
            emit(Event(Event::Kind::CommandRejected).with("line", line).with("why", err));
            return false;
        }
        if (c.kind == K::None)
        {
            mOutput.clear(); // a comment or a blank line prints nothing — not the line before it again (R-SVC-8)
            return true;
        }
        if (!dispatch(c, err))
        {
            emit(Event(Event::Kind::CommandRejected).with("line", line).with("why", err));
            return false;
        }
        return true;
    }

    namespace
    {
        // R-LANE-3: a song saved before lanes could be tracks. The lanes whose clips all play through one
        // instrument — what R-SVC-8's lane reuse built — become its tracks; one still carrying the name it was
        // made with (its strip's, or its instrument's label) shows the strip's name from now on (D-2's lane half)
        void adoptTracks(Project &p, std::vector<std::string> &notes)
        {
            if (p.header.tracks) return;
            p.header.tracks = true;
            for (auto &l : p.lanes)
            {
                if (!l.track.empty()) continue;
                std::string owner;
                bool one = true, any = false;
                for (const auto &c : p.clips)
                    if (c.lane == l.id)
                    {
                        if (c.isAudio() || (any && c.track != owner)) one = false;
                        owner = c.track;
                        any = true;
                    }
                const Strip *s = any && one ? p.strip(owner) : nullptr;
                if (!s || s->kind != "instrument") continue;
                l.track = owner;
                std::string label;
                if (const Rack *rk = p.rack(owner); rk && !rk->devices.empty())
                    if (const DeviceType *t = DeviceRegistry::find(rk->devices.front().type)) label = t->label;
                if (l.name == s->name || (!label.empty() && l.name == label)) l.name.clear();
                notes.push_back("lane " + l.id + " became the track of " + owner + " (" + s->name + ") — a song from before tracks (R-LANE-3)");
            }
        }

        // R-VER: what works on a version itself — allowed while a frozen one is open, and never recorded as its change
        bool versionOnly(const Command &c, const Project &p)
        {
            switch (c.kind)
            {
            case K::VersionNew: case K::VersionOpen: case K::VersionFreeze: case K::VersionThaw: case K::VersionRebase: case K::VersionDelete:
                return true;
            case K::Set:
                for (const auto &f : c.fields)
                {
                    const std::string id = f.first.substr(0, f.first.find('.'));
                    const Version *v = p.versionOf(id);
                    if (!v || v->id != id) return false;
                }
                return !c.fields.empty();
            default:
                return false;
            }
        }

        bool mutates(K k)
        {
            switch (k)
            {
            case K::ProjectNew: case K::ProjectOpen: case K::ProjectSave: case K::ProjectClose: case K::ProjectRecover: case K::ProjectDiscardRecovery:
            case K::Get: case K::Eval: case K::Undo: case K::Redo: case K::HistoryPrint: case K::HistoryJump: case K::Render: case K::MatrixPrint: case K::Audit: case K::StatePrint: case K::Api:
            case K::SettingsSet: case K::SettingsPrint: case K::FolderAdd: case K::FolderRemove: case K::FolderMove:
            case K::DevicesList: case K::Browse: case K::RecentsRemove:
            case K::TransportPlay: case K::TransportStop: case K::TransportSeek: case K::TransportLoop: case K::TransportRecord: case K::TransportMono: case K::Wait:
            case K::Audition: case K::NotePreview: case K::MetersClear: case K::MidiDevices: case K::StripArm: case K::TransportPunch: case K::None:
            case K::Ls: case K::Show: case K::PatternPrint: // R-SVC-8: reading back
            case K::PatternExport: case K::ProjectExportMidi: // R-MIDI-1: writing a file, not the song
            case K::PresetSave: case K::PresetList: case K::PresetDelete: // R-PRESET-1: the machine's presets, not the song
            case K::TemplateSave: case K::TemplateList: case K::TemplateDelete: // R-PRESET-2: … and its templates
            case K::SampleTag: case K::SampleUntag: case K::SampleFavourite:    // R-PRESET-3: … and its library
            case K::PluginsScan: case K::PluginsList: case K::DeviceReload: case K::DeviceEditor: // R-VST-6: … and its plugins
            case K::MapAdd: case K::MapRemove: case K::MapList: case K::MapLearn:                 // R-CTRL-1: … and its MIDI mappings
            case K::VersionList: case K::VersionDiff:                                             // R-VER: reading back
                return false;
            default:
                return true;
            }
        }
    }

    bool SolarisService::dispatch(const Command &c, std::string &err)
    {
        struct Depth
        {
            int &d;
            explicit Depth(int &x) : d(x) { ++d; }
            ~Depth() { --d; }
        } depth(mDepth); // a command is running: what `pump` gathers waits for it to end
        err.clear();
        mOutput.clear();
        const bool editing = mutates(c.kind) || (c.kind == K::TransportStop && mRecording) // R-MIDI-2: the take is an edit
                             || (c.kind == K::StripArm && c.has("input"));                  // R-REC-1: an input set is one
        if (editing && !requireOpen(err)) return false;
        if ((editing || c.kind == K::TransportRecord) && mOpen && !mProject.version.empty() && !versionOnly(c, mProject))
            if (const Version *v = mProject.versionOf(mProject.version); v && v->frozen)
            {
                // R-VER-3: a frozen version is read-only — a freeze that yields is not a freeze
                err = "version " + v->name + " is frozen — `version thaw " + v->id + "` to change it, or `version new <name> --base " +
                      v->id + " --open` to work on a version of it";
                return false;
            }
        const Project before = editing ? mProject : Project();
        mPending.clear();
        std::set<std::string> idsBefore; // R-SVC-8: what an edit made is said, by the ids it added
        if (editing)
            for (const auto &id : mProject.allIds()) idsBefore.insert(id);

        bool ok = false;
        switch (c.kind)
        {
        case K::ProjectNew: case K::ProjectOpen: case K::ProjectSave: case K::ProjectClose: case K::ProjectRecover: case K::ProjectDiscardRecovery:
            ok = projectCommand(c, err);
            break;
        case K::Set:
        {
            ok = true;
            for (const auto &f : c.fields)
            {
                std::string stored;
                if (!setAddress(f.first, f.second, stored, err)) { ok = false; break; }
                mPending.push_back(Event(Event::Kind::ParamsChanged).with("address", f.first).with("value", stored));
                const auto dot = f.first.find('.');
                if (mProject.device(f.first.substr(0, dot))) mLastChanged[f.first.substr(0, dot)] = f.first.substr(dot + 1);
            }
            break;
        }
        case K::Undo: case K::Redo: case K::HistoryPrint: case K::HistoryJump:
            ok = historyCommand(c, err);
            break;
        case K::Get:
        {
            std::string v;
            ok = requireOpen(err) && getAddress(c.arg(0), v, err);
            if (ok) mOutput = v + "\n";
            break;
        }
        case K::MixerAdd: case K::MixerDelete: case K::MixerMove: case K::StripAdd: case K::StripDelete:
        case K::StripMove: case K::StripRelink: case K::StripDuck: case K::Route: case K::SendAdd: case K::SendDelete: case K::DeviceAdd:
        case K::DeviceRemove: case K::DeviceMove: case K::LaneAdd: case K::LaneDelete: case K::LaneMove:
            ok = mixCommand(c, err);
            break;
        case K::AutoAdd: case K::AutoCreate: case K::AutoDelete: case K::AutoPointAdd: case K::AutoPointMove:
        case K::AutoPointDelete: case K::AutoPointShape: case K::BindClear:
            ok = autoCommand(c, err);
            break;
        case K::Eval:
            ok = evalCommand(c, err);
            break;
        case K::ClipAdd: case K::ClipMove: case K::ClipDuplicate: case K::ClipUnique: case K::ClipDelete:
        case K::PatternNew: case K::NoteAdd: case K::NoteDelete: case K::NoteMove:
            ok = clipCommand(c, err);
            break;
        case K::PatternImport: case K::PatternExport: case K::ProjectExportMidi: case K::ExprAdd: case K::ExprDelete: case K::ExprClear:
            ok = midiCommand(c, err);
            break;
        case K::MarkerAdd: case K::MarkerRename: case K::MarkerMove: case K::MarkerDelete: case K::TimeInsert: case K::TimeDelete:
        case K::LaneGroup: case K::LaneUngroup: case K::ClipsMove: case K::ClipComp:
            ok = arrangeCommand(c, err);
            break;
        case K::StripFreeze: case K::StripUnfreeze: case K::StripBounce:
            ok = freezeCommand(c, err);
            break;
        case K::ClipSplit: case K::ClipConsolidate: case K::ClipSlice:
            ok = audioEditCommand(c, err);
            break;
        case K::TempoAdd: case K::TempoMove: case K::TempoDelete:
            ok = tempoCommand(c, err);
            break;
        case K::PresetSave: case K::PresetLoad: case K::PresetList: case K::PresetDelete:
            ok = presetCommand(c, err);
            break;
        case K::TemplateSave: case K::TemplateList: case K::TemplateDelete:
            ok = templateCommand(c, err);
            break;
        case K::SampleTag: case K::SampleUntag: case K::SampleFavourite:
            ok = libraryCommand(c, err);
            break;
        case K::PluginsScan: case K::PluginsList:
            ok = pluginsCommand(c, err);
            break;
        case K::DeviceReload:
            ok = pluginReload(c, err);
            break;
        case K::DeviceEditor:
            ok = pluginEditor(c, err);
            break;
        case K::MapAdd: case K::MapRemove: case K::MapList: case K::MapLearn:
            ok = mapCommand(c, err);
            break;
        case K::VersionNew: case K::VersionList: case K::VersionOpen: case K::VersionDiff: case K::VersionFreeze:
        case K::VersionThaw: case K::VersionRebase: case K::VersionDelete: case K::Revert:
            ok = versionCommand(c, err);
            break;
        case K::NotesAdd: case K::NotesMove: case K::NotesDelete: case K::PatternSteps: case K::PatternDuplicate: case K::PatternClear: case K::PatternDelete:
        case K::PatternTranspose: case K::PatternQuantize: case K::PatternScale: case K::NotesSet: case K::PatternHumanise: case K::PatternLegato:
        case K::PatternArpeggiate:
            ok = composeCommand(c, err);
            break;
        case K::Ls: case K::Show: case K::PatternPrint:
            ok = readCommand(c, err);
            break;
        case K::Render:
            ok = render(c, err);
            break;
        case K::MatrixPrint:
            ok = requireOpen(err);
            if (ok) mOutput = matrixText(c.has("json"));
            break;
        case K::Audit:
        {
            ok = requireOpen(err);
            if (!ok) break;
            mAudit = audit();
            for (const auto &a : mAudit) mOutput += a + "\n";
            if (mAudit.empty()) mOutput = "no findings\n";
            emit(Event(Event::Kind::AuditReport).with("findings", (long long)mAudit.size()));
            ok = true;
            break;
        }
        case K::StatePrint:
        {
            ok = true;
            refreshModel();
            if (c.has("compact")) mOutput = modelToJson(mModel, true, true).dumpCompact(); // R-SVC-8: the song, for an agent
            else if (c.has("json")) mOutput = modelToJson(mModel, c.has("stable")).dump();
            else
            {
                std::ostringstream o;
                o << "screen " << mModel.screen;
                if (mOpen) o << " · " << mModel.projectName << " · " << canonicalNumber(mModel.bpm) << " bpm · "
                             << mModel.mixers.size() << " mixers · " << mModel.strips.size() << " strips · "
                             << mModel.lanes.size() << " lanes · " << mModel.clips.size() << " clips"
                             << (mModel.dirty ? " · unsaved" : "");
                mOutput = o.str() + "\n";
            }
            break;
        }
        case K::Api:
            ok = true;
            mOutput = c.has("md") ? apiMarkdown() : apiJson();
            break;
        case K::SettingsSet: case K::SettingsPrint: case K::FolderAdd: case K::FolderRemove: case K::FolderMove:
        case K::DevicesList: case K::Browse: case K::RecentsRemove:
            ok = machineCommand(c, err);
            break;
        case K::MidiDevices: case K::StripArm: case K::PortAdd:
            ok = inputCommand(c, err);
            break;
        case K::TransportPlay: case K::TransportStop: case K::TransportSeek: case K::TransportLoop: case K::TransportPunch: case K::TransportRecord: case K::TransportMono: case K::Wait: case K::Audition: case K::MetersClear:
        case K::NotePreview:
            ok = transportCommand(c, err);
            break;
        case K::None:
            ok = true;
            break;
        }

        if (!ok && err.empty())
        {
            // R-SVC-3: a refusal always says why — a path that forgot to is named, never silent
            const CommandSpec *sp = specFor(c.kind);
            err = "`" + std::string(sp ? sp->verb : "command") + "` was refused without a reason";
        }
        if (ok && editing)
        {
            const auto errors = validateProject(mProject);
            if (!errors.empty())
            {
                ok = false;
                err = errors.front();
            }
        }
        if (!ok)
        {
            if (editing) mProject = before; // a refused command changes nothing
            mOutput.clear();
            mPending.clear();
            refreshModel();
            return false;
        }
        // R-VER-2: an edit that landed on an open version is recorded as what it changes from its base — for every
        // command at once, so none of them knows versions exist
        if (editing && !mProject.version.empty() && !versionOnly(c, mProject)) recordVersion(mProject);
        const bool history = c.kind == K::Undo || c.kind == K::Redo || c.kind == K::HistoryJump;
        if (editing)
        {
            // R-SVC-8: everything made is said — the first line stays the command's own id (scripts read
            // it); `made:` names the rest by kind, so an agent never has to predict an id
            static const std::vector<std::pair<std::string, std::string>> kinds = {
                {"ch_", "strip"}, {"dv_", "device"}, {"pt_", "pattern"}, {"ln_", "lane"}, {"ac_", "clip"},
                {"au_", "automation"}, {"sd_", "send"}, {"mx_", "mixer"}, {"prt_", "port"}, {"lg_", "group"}, {"mk_", "marker"},
                {"vr_", "version"}};
            const std::string primary = mOutput.substr(0, mOutput.find('\n'));
            std::vector<std::string> fresh;
            for (const auto &id : mProject.allIds())
                if (!idsBefore.count(id) && id != primary) fresh.push_back(id);
            std::string made;
            for (const auto &k : kinds)
            {
                std::string list;
                for (const auto &id : fresh)
                    if (id.compare(0, k.first.size(), k.first) == 0) list += (list.empty() ? "" : ",") + id;
                if (!list.empty()) made += " " + k.second + "=" + list;
            }
            if (!made.empty()) mOutput += "made:" + made + "\n";

            // R-HIST-1: the song after this edit is a node — a child of the current one, a BRANCH when that one has
            // children — unless it continues the last edit (a fader dragged: R-EDM-1), which it overwrites
            std::string key;
            if (c.kind == K::Set)
            {
                std::vector<std::string> addresses;
                for (const auto &f : c.fields) addresses.push_back(f.first);
                std::sort(addresses.begin(), addresses.end());
                key = "set";
                for (const auto &a : addresses) key += " " + a;
            }
            const auto cur = mHist.find(mHistCur);
            if (!key.empty() && key == mCoalesce && cur != mHist.end() && cur->second.parent >= 0 && cur->second.kids.empty())
                cur->second.project = mProject;
            else
                historyAdd(labelOf(c));
            mCoalesce = key;
        }
        if (c.kind == K::ProjectNew || c.kind == K::ProjectOpen || c.kind == K::ProjectClose || c.kind == K::ProjectRecover)
        {
            // another song: its own tree, rooted at the song as it opened
            historyReset(c.kind == K::ProjectClose ? std::string() : c.kind == K::ProjectNew ? "New song" : c.kind == K::ProjectRecover ? "Recovered" : "Opened");
            mCoalesce.clear();
            mArmed.clear();     // R-MIDI-2: another song: nothing armed
            mModel.armed.clear();
            stopMonitor();
            mClipLatch.clear(); // R-METER-2: another song's meters start out
            mMasterClip = false;
            mModel.loudness = LoudnessModel();
            mModel.transport.masterClip = false;
            for (auto &st : mModel.strips) st.clip = false;
        }
        // R-SAFE-1: an edit that landed — written beside the song at once when it removed something, else on the clock
        if ((editing || history) && mOpen && mSettings.autosave > 0)
        {
            const CommandSpec *sp = specFor(c.kind);
            const std::string verb = sp ? sp->verb : std::string();
            const bool removal = verb.find("delete") != std::string::npos || verb.find("remove") != std::string::npos;
            if (removal || steadySeconds() - mLastAutosave >= mSettings.autosave) autosave();
        }
        if ((editing && c.kind != K::VersionOpen) || history) mModel.dirty = true; // R-VER: which version is open is not the file's
        if (editing || history) followClipLoop(); // R-ROLL-6: the looped clip, as edited
        refreshModel();
        std::vector<Event> landed;
        landed.swap(mPending);
        for (const auto &e : landed) emit(e);
        if (editing || history) liveUpdate(c); // heard while playing (DR-PLAY-1)
        if ((editing || history) && !mArmed.empty())
        {
            // R-MIDI-2: the armed strip gone — disarmed; else the monitor hears the song as it is now
            if (!mProject.strip(mArmed))
            {
                mArmed.clear();
                mModel.armed.clear();
                if (mPlayer) mPlayer->setArmed(-1);
                stopMonitor();
                emit(Event(Event::Kind::StripArmed).with("strip", ""));
            }
            else if (mMonitor) startMonitor();
            else if (mPlayer && mPlayer->running()) mPlayer->setArmed(armedIn(mLiveStrips)); // an engine swap renumbers
        }
        pump();
        return true;
    }

    // ── history (R-EDM-1) ────────────────────────────────────────────────────────────────────

    std::string SolarisService::labelOf(const Command &c) const
    {
        if (c.kind == K::TransportStop) return "Record"; // R-MIDI-2: a stop that is an edit lands a take
        const CommandSpec *sp = specFor(c.kind);
        std::string l = sp ? sp->verb : "edit";
        if (c.kind == K::Set)
            for (const auto &f : c.fields) l += " " + f.first;
        else
            for (const auto &a : c.args) l += " " + a;
        return l;
    }

    void SolarisService::historyReset(const std::string &rootLabel)
    {
        mHist.clear();
        mHistCur = -1;
        mHistNext = 0;
        if (!rootLabel.empty()) historyAdd(rootLabel);
    }

    void SolarisService::historyAdd(const std::string &label)
    {
        const int id = mHistNext++;
        HistNode n;
        n.project = mProject;
        n.label = label;
        n.parent = mHistCur;
        mHist[id] = std::move(n);
        if (mHistCur >= 0) mHist[mHistCur].kids.push_back(id);
        mHistCur = id;
        historyPrune();
    }

    void SolarisService::historyPrune()
    {
        // past kHistory nodes the OLDEST off the path to the current node goes first — a leaf of another branch; when
        // the tree is one path, its root (the oldest step, as the stack dropped it) and the next node is the root
        while (mHist.size() > kHistory)
        {
            std::set<int> path;
            for (int i = mHistCur; i >= 0; i = mHist[i].parent) path.insert(i);
            int victim = -1;
            for (const auto &n : mHist)
                if (n.second.kids.empty() && !path.count(n.first)) { victim = n.first; break; } // ascending: the oldest
            if (victim >= 0)
            {
                auto &kids = mHist[mHist[victim].parent].kids;
                kids.erase(std::remove(kids.begin(), kids.end(), victim), kids.end());
                mHist.erase(victim);
                continue;
            }
            const int root = mHist.begin()->first;
            if (root == mHistCur || mHist[root].kids.size() != 1) break; // nothing that may go
            mHist[mHist[root].kids.front()].parent = -1;
            mHist.erase(root);
        }
    }

    bool SolarisService::historyCommand(const Command &c, std::string &err)
    {
        if (!requireOpen(err)) return false;
        if (c.kind == K::HistoryPrint)
        {
            if (c.has("json"))
            {
                Json a = Json::array();
                for (const auto &n : mHist)
                    a.push(Json::object().set("id", n.first).set("parent", n.second.parent).set("label", n.second.label).set("current", n.first == mHistCur));
                mOutput = a.dump();
                return true;
            }
            std::ostringstream o;
            for (const auto &n : mHist)
                o << (n.first == mHistCur ? "* " : "  ") << n.first << "\t" << (n.second.parent < 0 ? std::string("-") : std::to_string(n.second.parent))
                  << "\t" << n.second.label << "\n";
            mOutput = o.str();
            return true;
        }
        const auto cur = mHist.find(mHistCur);
        if (cur == mHist.end()) { err = "this song has no history"; return false; }
        int to = -1;
        std::string label;
        if (c.kind == K::Undo)
        {
            if (cur->second.parent < 0) { err = "nothing to undo"; return false; }
            to = cur->second.parent;
            label = cur->second.label; // what is taken back
        }
        else if (c.kind == K::Redo)
        {
            if (cur->second.kids.empty()) { err = "nothing to redo"; return false; }
            to = *std::max_element(cur->second.kids.begin(), cur->second.kids.end()); // the newest branch
            label = mHist[to].label;
        }
        else
        {
            double x = 0;
            if (!parseNumber(c.arg(0), x) || x != std::floor(x) || !mHist.count((int)x))
            {
                err = "no history node `" + c.arg(0) + "` (`history` lists them: " + std::to_string(mHist.begin()->first) + " … " +
                      std::to_string(mHist.rbegin()->first) + ")";
                return false;
            }
            to = (int)x;
            label = mHist[to].label;
        }
        mProject = mHist[to].project;
        mHistCur = to;
        mCoalesce.clear();       // the next edit starts a node of its own
        mBindingsTouched = true; // the song changed shape under the player
        changed(c.kind == K::Undo ? "undo" : c.kind == K::Redo ? "redo" : "history.jump", label);
        mOutput = label + "\n";
        return true;
    }

    // ── files ────────────────────────────────────────────────────────────────────────────────

    std::string SolarisService::resolvePath(const std::string &src) const
    {
        const fs::path p(src);
        if (p.is_absolute() || mPath.empty()) return p.lexically_normal().string();
        return (fs::path(mPath).parent_path() / p).lexically_normal().string();
    }

    std::string SolarisService::relativePath(const std::string &file) const
    {
        const fs::path abs = fs::absolute(fs::path(file)).lexically_normal();
        if (mPath.empty()) return abs.string();
        const fs::path dir = fs::absolute(fs::path(mPath)).parent_path().lexically_normal();
        const fs::path rel = abs.lexically_relative(dir);
        // Inside the song's folder: stored relative, so the folder moves as one. Outside: absolute.
        if (!rel.empty() && *rel.begin() != "..") return rel.generic_string();
        return abs.generic_string();
    }

    double SolarisService::sampleOfBeat(double beat) const
    {
        if (mProject.tempos.empty()) return beat * (60.0 / mProject.header.bpm * mProject.header.sampleRate);
        return tempoMapOf(mProject).sampleAt(beat);
    }

    double SolarisService::beatOfSample(double sample) const
    {
        if (mProject.tempos.empty()) return sample / (60.0 / mProject.header.bpm * mProject.header.sampleRate);
        return tempoMapOf(mProject).beatAt(sample);
    }

    void SolarisService::tempoAt(double beat, double &bpm, std::string &id) const
    {
        id.clear();
        if (mProject.tempos.empty()) { bpm = mProject.header.bpm; return; }
        bpm = tempoMapOf(mProject).bpmAt(beat);
        for (const auto &t : mProject.tempos)
            if (t.at <= beat + 1e-9) id = t.id;
    }

    bool SolarisService::textFor(const std::string &src, std::string &text, std::string &resolved)
    {
        resolved = resolvePath(src);
        std::ifstream f(resolved, std::ios::binary);
        if (!f) return false;
        text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }

    std::shared_ptr<const engine::Pcm> SolarisService::pcmFor(const std::string &src)
    {
        if (mPcmRate != mProject.header.sampleRate)
        {
            mPcm.clear(); // decoded at another rate: stale
            mOffline.clear();
            mPcmRate = mProject.header.sampleRate;
        }
        if (src.compare(0, 8, "stretch:") == 0) // R-WARP-1: a clip's span stretched — made once, kept while it is used
        {
            auto st = mPcm.find(src);
            if (st != mPcm.end()) return st->second;
            auto made = stretched(src, [this](const std::string &s) { return pcmFor(s); }, mPcmRate);
            if (!made) return nullptr;
            int kept = 0;
            for (const auto &e : mPcm) kept += e.first.compare(0, 8, "stretch:") == 0 ? 1 : 0;
            if (kept >= 16) // a tempo dragged makes one a step: what the song no longer plays goes
            {
                std::set<std::string> used;
                for (const auto &c : mProject.clips) used.insert(stretchKey(mProject, c));
                for (auto e = mPcm.begin(); e != mPcm.end();)
                    e = e->first.compare(0, 8, "stretch:") == 0 && !used.count(e->first) ? mPcm.erase(e) : std::next(e);
            }
            mPcm[src] = made;
            return made;
        }
        const std::string path = resolvePath(src);
        auto it = mPcm.find(path);
        if (it != mPcm.end()) return it->second;
        if (mOffline.count(path) || !mHost.decodeAudio) return nullptr;
        auto pcm = std::make_shared<engine::Pcm>();
        std::string err;
        if (!mHost.decodeAudio(path, mPcmRate, *pcm, err) || pcm->frames <= 0)
        {
            mOffline.insert(path);
            return nullptr;
        }
        mPcm[path] = pcm;
        return pcm;
    }

    // ── R-SAFE: autosave and recovery ────────────────────────────────────────────────────────────

    double SolarisService::steadySeconds()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::string SolarisService::autosavePath(const std::string &song) { return song + ".autosave"; }

    bool SolarisService::recoveryOf(const std::string &song, double &ageSeconds)
    {
        // an autosave NEWER than the song: edits made after its last save, never saved
        std::error_code ec;
        const fs::path a = autosavePath(song);
        if (!fs::exists(a, ec) || !fs::exists(song, ec)) return false;
        const auto ta = fs::last_write_time(a, ec), ts = fs::last_write_time(song, ec);
        if (ec || ta <= ts) return false;
        ageSeconds = std::max(0.0, std::chrono::duration<double>(fs::file_time_type::clock::now() - ta).count());
        return true;
    }

    bool SolarisService::autosave()
    {
        if (!mOpen || mPath.empty() || !mModel.dirty || mSettings.autosave <= 0) return false;
        const std::string to = autosavePath(mPath), tmp = to + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary);
            if (!f) return false;
            capturePluginStates(); // R-VST-6: each plugin as it stands
            f << serializeProject(mProject);
            if (!f) return false;
        }
        std::error_code ec;
        fs::rename(tmp, to, ec); // atomic: a crash mid-write leaves the last good one
        if (ec) { fs::remove(tmp, ec); return false; }
        mLastAutosave = steadySeconds();
        mRecentsStale = true;
        emit(Event(Event::Kind::ProjectAutosaved).with("path", to));
        return true;
    }

    void SolarisService::dropAutosave(const std::string &song)
    {
        std::error_code ec;
        if (!song.empty() && fs::remove(autosavePath(song), ec)) mRecentsStale = true;
        if (song == mRecovery) { mRecovery.clear(); mRecoveryAge = -1.0; }
    }

    bool SolarisService::projectCommand(const Command &c, std::string &err)
    {
        auto open = [&](Project p, const std::string &path) {
            stopPlayer();
            mPluginLive.clear(); // R-VST-6: the last song's plugins end; this one's start as the model asks for them
            mPosition = mLoopFrom = mLoopTo = mCue = 0;
            mClipLoop.clear();
            mModel.transport = TransportModel();
            mProject = std::move(p);
            mPath = path;
            mOpen = true;
            mPcm.clear();
            mOffline.clear();
            mAudit.clear();
            mLastRenderPeaks.clear();
            mLastAutosave = steadySeconds(); // R-SAFE-1: the clock starts with the song
            mRecovery.clear();
            mRecoveryAge = -1.0;
            refreshModel();
            mModel.dirty = false;
            touchRecent(path);
            emit(Event(Event::Kind::ScreenChanged).with("screen", "project"));
            emit(Event(Event::Kind::ProjectOpened).with("path", path)
                     .with("strips", (long long)mProject.strips.size()).with("clips", (long long)mProject.clips.size()));
        };
        auto write = [&](const std::string &path) {
            std::ofstream f(path, std::ios::binary);
            if (!f) { err = "cannot write " + path; return false; }
            capturePluginStates(); // R-VST-6: each plugin's state, as the plugin gives it
            f << serializeProject(mProject);
            return (bool)f;
        };

        switch (c.kind)
        {
        case K::ProjectNew:
        {
            const std::string path = c.arg(0);
            if (fs::exists(path)) { err = path + " already exists — open it, or choose another name"; return false; }
            double bpm = mSettings.newBpm; // R-SET-3: the machine's choice for a new song
            if (c.has("bpm") && (!parseNumber(c.flag("bpm"), bpm) || !(bpm >= 20 && bpm <= 999)))
            { err = "--bpm must be a tempo between 20 and 999"; return false; }
            const std::string sig = c.flag("sig", mSettings.newSig);
            int num = 0, den = 0;
            if (std::sscanf(sig.c_str(), "%d/%d", &num, &den) != 2 || num < 1 || num > 32 || (den != 2 && den != 4 && den != 8 && den != 16))
            { err = "--sig must be a meter like 4/4, 3/4 or 7/8"; return false; }
            double rate = mSettings.sampleRate; // R-SET-1: the machine's rate is a new song's default
            if (c.has("rate") && (!parseNumber(c.flag("rate"), rate) || !(rate >= 8000 && rate <= 192000)))
            { err = "--rate must be a sample rate between 8000 and 192000"; return false; }
            const std::string stem = fs::path(path).stem().string();
            std::string id = "prj_";
            for (char ch : stem) id += (std::isalnum((unsigned char)ch) ? ch : '_');
            Project p = newProject(id, c.flag("name", stem), bpm, sig, (int)rate);
            if (c.has("template"))
            {
                // R-PRESET-2: the template's song, its own name and id; its tempo, meter and rate unless said
                if (!templateSong(c.flag("template"), p, err)) return false;
                p.header.id = id;
                p.header.name = c.flag("name", stem);
                if (c.has("bpm")) p.header.bpm = bpm;
                if (c.has("sig")) p.header.sig = sig;
                if (c.has("rate")) p.header.sampleRate = (int)rate;
            }
            const Project keep = mProject;
            mProject = p;
            if (!write(path)) { mProject = keep; return false; }
            open(p, path);
            mOutput = path + "\n";
            return true;
        }
        case K::ProjectOpen:
        {
            const std::string path = c.arg(0);
            std::ifstream f(path, std::ios::binary);
            if (!f) { err = "cannot read " + path; return false; }
            std::stringstream ss;
            ss << f.rdbuf();
            Project p;
            ParseReport rep;
            if (!parseProject(ss.str(), p, err, &rep)) { err = path + ": " + err; return false; }
            adoptTracks(p, rep.notes);
            if (gatherLaneGroups(p)) rep.notes.push_back("lane groups gathered: each group's lanes side by side (R-ARR-3)");
            open(std::move(p), path);
            for (const auto &n : rep.notes) emit(Event(Event::Kind::Info).with("text", n));
            double age = 0;
            if (recoveryOf(path, age))
            {
                // R-SAFE-2: said, and offered — the saved song is what is open until `project recover`
                mRecovery = path;
                mRecoveryAge = age;
                char b[32];
                std::snprintf(b, sizeof b, "%.0f", age);
                mOutput += "recovery: " + autosavePath(path) + " (" + b + " s old) — `project recover " + path + "` restores it, `project discard-recovery " +
                           path + "` drops it\n";
                emit(Event(Event::Kind::ProjectRecovery).with("path", autosavePath(path)).with("age", age));
            }
            return true;
        }
        case K::ProjectRecover:
        {
            const std::string path = c.arg(0), from = autosavePath(path);
            std::ifstream f(from, std::ios::binary);
            if (!f) { err = "no autosave for " + path + " (" + from + ")"; return false; }
            std::stringstream ss;
            ss << f.rdbuf();
            Project p;
            ParseReport rep;
            if (!parseProject(ss.str(), p, err, &rep)) { err = from + ": " + err; return false; }
            adoptTracks(p, rep.notes);
            gatherLaneGroups(p);
            open(std::move(p), path); // the song is <path>: `project save` writes it there and drops the autosave
            mModel.dirty = true;      // what was recovered is not saved yet
            mOutput = "recovered " + path + " from " + from + "\n";
            return true;
        }
        case K::ProjectDiscardRecovery:
        {
            const std::string path = c.arg(0);
            std::error_code ec;
            if (!fs::exists(autosavePath(path), ec)) { err = "no autosave for " + path; return false; }
            dropAutosave(path);
            return true;
        }
        case K::ProjectSave:
        {
            if (!requireOpen(err)) return false;
            const std::string path = c.arg(0, mPath);
            if (!write(path)) return false;
            dropAutosave(mPath); // R-SAFE-1: saved — nothing left to recover (its old place too, when saved elsewhere)
            dropAutosave(path);
            mRecentsStale = true; // its card shows the saved name and length
            if (path != mPath) { mPath = path; mPcm.clear(); mOffline.clear(); touchRecent(path); } // relative media now resolve from here
            mModel.dirty = false;
            emit(Event(Event::Kind::ProjectSaved).with("path", path));
            return true;
        }
        case K::ProjectClose:
            dropAutosave(mPath); // R-SAFE-1: closing is choosing — saved or discarded, nothing to recover
            mRecentsStale = true; // Home is where the cards matter: re-read them (a song may have moved meanwhile)
            stopPlayer();
            mPluginLive.clear(); // R-VST-6: its plugins' processes end
            mPosition = mLoopFrom = mLoopTo = mCue = 0;
            mClipLoop.clear();
            mModel.transport = TransportModel();
            mOpen = false;
            mProject = Project();
            mPath.clear();
            mPcm.clear();
            mOffline.clear();
            emit(Event(Event::Kind::ProjectClosed));
            emit(Event(Event::Kind::ScreenChanged).with("screen", "home"));
            return true;
        default:
            return false;
        }
    }

    // ── addresses ────────────────────────────────────────────────────────────────────────────

    namespace
    {
        bool number(const std::string &v, double &out, std::string &err)
        {
            if (parseNumber(v, out) && std::isfinite(out)) return true;
            err = "`" + v + "` is not a number";
            return false;
        }
        bool boolean(const std::string &v, bool &out, std::string &err)
        {
            if (parseBool(v, out)) return true;
            err = "`" + v + "` is not true/false";
            return false;
        }
        bool inRange(double v, double lo, double hi, const std::string &what, std::string &err)
        {
            if (v >= lo && v <= hi) return true;
            err = what + " must be between " + canonicalNumber(lo) + " and " + canonicalNumber(hi) + ", got " + canonicalNumber(v);
            return false;
        }
        std::string unknownField(const std::string &id, const std::string &what, const std::string &field,
                                 const std::vector<std::string> &fields)
        {
            std::string e = "`" + id + "` (" + what + ") has no field `" + field + "`";
            const auto near = nearest(field, fields);
            if (!near.empty()) e += " (did you mean: " + near[0] + "?)";
            else
            {
                e += " (it has: ";
                for (size_t i = 0; i < fields.size(); ++i) e += (i ? ", " : "") + fields[i];
                e += ")";
            }
            return e;
        }
    }

    bool SolarisService::setAddress(const std::string &address, const std::string &value, std::string &stored, std::string &err)
    {
        const auto dot = address.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == address.size())
        {
            err = "an address is <id>.<field>, got `" + address + "` (`api` lists them)";
            return false;
        }
        const std::string id = address.substr(0, dot), f = address.substr(dot + 1);
        double x = 0;
        bool b = false;
        Project &p = mProject;

        // a FORMULA drives the number (R-AUTO-1); a plain number clears it and sets the value
        if (!value.empty() && value[0] == '=')
        {
            AddressSpec spec;
            if (!describeAddress(p, address, spec, err)) return false;
            if ((spec.kind == AddressSpec::StripGain || spec.kind == AddressSpec::StripPan) && p.strip(spec.node) && p.strip(spec.node)->kind == "vca")
            { err = address + ": a VCA's gain is a value for now — not a formula (R-MIX-19)"; return false; }
            if (!checkBinding(p, address, value, err)) { err = address + ": " + err; return false; }
            if (Binding *bd = p.binding(address)) bd->formula = value;
            else p.bindings.push_back(Binding{address, value, {}, {}});
            mBindingsTouched = true;
            stored = value;
            return true;
        }
        if (p.binding(address))
        {
            p.bindings.erase(std::remove_if(p.bindings.begin(), p.bindings.end(), [&](const Binding &bd) { return bd.address == address; }),
                             p.bindings.end());
            mBindingsTouched = true;
        }
        if (Version *v = p.versionOf(id); v && v->id == id) // R-VER: a version has a name, its only field here
        {
            if (f != "name") { err = id + " is a version: its field is `name` (`version` verbs do the rest)"; return false; }
            if (value.empty()) { err = "a version needs a name"; return false; }
            v->name = value;
            stored = value;
            return true;
        }
        if (Automation *a = p.automation(id))
        {
            if (f == "name") { a->name = value; stored = value; mBindingsTouched = true; return true; }
            if (f == "unit") { a->unit = value; stored = value; return true; }
            if (f == "min" || f == "max")
            {
                if (!number(value, x, err)) return false;
                const double lo = f == "min" ? x : a->min, hi = f == "max" ? x : a->max;
                if (!(lo < hi)) { err = a->id + "'s range would be empty (" + canonicalNumber(lo) + "…" + canonicalNumber(hi) + ")"; return false; }
                (f == "min" ? a->min : a->max) = x;
                for (auto &pt : a->points) pt.value = std::min(std::max(pt.value, a->min), a->max);
                stored = canonicalNumber(x);
                mBindingsTouched = true;
                return true;
            }
            if (f == "loop" || f == "at" || f == "length")
            {
                // R-AUTO-12: beats ≥ 0 (loop 0 = it does not loop; length 0 = to the song's end)
                if (!number(value, x, err)) return false;
                if (!(x >= 0) || x > 100000) { err = id + "." + f + " is beats from 0" + (f == "loop" ? " (0: no loop)" : f == "length" ? " (0: to the song's end)" : ""); return false; }
                (f == "loop" ? a->loop : f == "at" ? a->at : a->length) = toTick(x);
                stored = canonicalBeats(toTick(x));
                mBindingsTouched = true;
                return true;
            }
            err = unknownField(id, "an automation", f, {"name", "unit", "min", "max", "loop", "at", "length"}) + " — points are `auto point …`";
            return false;
        }

        if (id == "project")
        {
            static const std::vector<std::string> fields = {"name", "bpm", "sig", "masterGain", "sampleRate"};
            if (f == "name") { p.header.name = value; stored = value; return true; }
            if (f == "bpm") { if (!number(value, x, err) || !inRange(x, 20, 999, "bpm", err)) return false; p.header.bpm = x; stored = canonicalNumber(x); return true; }
            if (f == "masterGain") { if (!number(value, x, err) || !inRange(x, -120, 12, "masterGain", err)) return false; p.header.masterGain = x; stored = canonicalNumber(x); return true; }
            if (f == "sampleRate") { if (!number(value, x, err) || !inRange(x, 8000, 192000, "sampleRate", err)) return false; p.header.sampleRate = (int)x; stored = std::to_string((int)x); return true; }
            if (f == "sig")
            {
                int n = 0, d = 0;
                if (std::sscanf(value.c_str(), "%d/%d", &n, &d) != 2 || n < 1 || n > 32 || (d != 2 && d != 4 && d != 8 && d != 16))
                { err = "sig must be a meter like 4/4"; return false; }
                p.header.sig = value;
                stored = value;
                return true;
            }
            err = unknownField(id, "the song", f, fields);
            return false;
        }
        if (Strip *s = p.strip(id))
        {
            static const std::vector<std::string> fields = {"name", "gain", "pan", "mute", "solo", "colour", "input", "monitor", "vca", "phase", "width", "soloSafe"};
            if (f == "name") { s->name = value; stored = value; return true; }
            if (f == "vca") // R-MIX-19: the VCA it follows — a strip of kind vca, by id or name — or none
            {
                if (value == "none" || value.empty()) { s->vca.clear(); stored = "none"; return true; }
                if (s->kind == "vca") { err = id + " is a VCA — a VCA follows no VCA (not yet)"; return false; }
                const Strip *v = nullptr;
                std::string vcas;
                for (const auto &x : p.strips)
                {
                    if (x.kind != "vca") continue;
                    if (x.id == value || x.name == value) v = &x;
                    vcas += (vcas.empty() ? "" : ", ") + x.id + " (" + x.name + ")";
                }
                if (!v)
                {
                    const Strip *other = p.strip(value);
                    err = other ? value + " is a " + other->kind + " strip, not a VCA" : "no VCA `" + value + "`";
                    err += " — the song's VCAs: " + (vcas.empty() ? std::string("none (`strip add --kind vca`)") : vcas);
                    return false;
                }
                s->vca = v->id;
                stored = v->id;
                return true;
            }
            if (s->kind == "vca" && (f == "pan" || f == "input" || f == "monitor" || f == "phase" || f == "width"))
            { err = id + " is a VCA — it carries no sound: it has a gain, a mute and a solo (R-MIX-19)"; return false; }
            // R-MIX-20: its polarity, its width, and — a bus's — solo-safe
            if (f == "phase")
            {
                if (value != "on" && value != "off" && value != "invert" && value != "normal") { err = id + ".phase is on or off, got `" + value + "`"; return false; }
                s->phase = value == "on" || value == "invert";
                stored = s->phase ? "on" : "off";
                return true;
            }
            if (f == "width") { if (!number(value, x, err) || !inRange(x, 0, 200, "width (%)", err)) return false; s->width = x; stored = canonicalNumber(x); return true; }
            if (f == "soloSafe")
            {
                if (!boolean(value, b, err)) return false;
                if (b && s->kind != "bus") { err = id + " is a" + std::string(s->kind == "audio" ? "n audio" : s->kind == "instrument" ? "n instrument" : " " + s->kind) + " strip — solo-safe is a bus's (R-MIX-20)"; return false; }
                s->soloSafe = b;
                stored = boolText(b);
                return true;
            }
            if (f == "input") // R-REC-1: an input port of the song — by id or name — or none
            {
                if (value == "none") { s->input.clear(); stored = "none"; return true; }
                if (s->kind != "audio") { err = id + " is " + (s->kind == "bus" ? "a bus" : "an instrument strip") + ": only an audio line records an input"; return false; }
                const Port *pt = nullptr;
                std::string ins;
                for (const auto &x : p.ports)
                {
                    if (x.id == value || x.name == value) pt = &x;
                    if (x.dir == "in") ins += (ins.empty() ? "" : ", ") + x.id + " (" + x.name + ")";
                }
                if (!pt) { err = "no port `" + value + "` — the song's inputs: " + (ins.empty() ? std::string("none (`port add <name> --dir in`)") : ins); return false; }
                if (pt->dir != "in") { err = pt->id + " is an output port — an input is a port with --dir in"; return false; }
                s->input = pt->id;
                stored = pt->id;
                return true;
            }
            if (f == "monitor")
            {
                if (value != "auto" && value != "on" && value != "off") { err = id + ".monitor is auto, on or off, got `" + value + "`"; return false; }
                s->monitor = value;
                stored = value;
                if (mArmed == id) // armed: heard (or not) at once — the song's player switches, the monitor starts or stops
                {
                    if (mPlayer && mPlayer->running()) mPlayer->setMonitor(armedIn(mLiveStrips), monitorHears(*s, true, mRecordingAudio));
                    else { stopMonitor(); startMonitor(); }
                }
                return true;
            }
            if (f == "gain") { if (!number(value, x, err) || !inRange(x, -120, 12, "gain (dB)", err)) return false; s->gain = x; stored = canonicalNumber(x); return true; }
            if (f == "pan") { if (!number(value, x, err) || !inRange(x, -1, 1, "pan", err)) return false; s->pan = x; stored = canonicalNumber(x); return true; }
            if (f == "mute") { if (!boolean(value, b, err)) return false; s->mute = b; stored = boolText(b); return true; }
            if (f == "solo") { if (!boolean(value, b, err)) return false; s->solo = b; stored = boolText(b); return true; }
            if (f == "colour") { if (!number(value, x, err) || !inRange(x, -1, 15, "colour", err)) return false; s->colour = (int)x; stored = std::to_string((int)x); return true; }
            err = unknownField(id, "a strip", f, fields) + " — routing is `route " + id + " --to …`";
            return false;
        }
        if (Clip *c = p.clip(id))
        {
            static const std::vector<std::string> fields = {"name", "at", "in", "out", "length", "gain", "fadeIn", "fadeOut", "loop", "reverse", "warp", "bpm", "pitch"};
            if (f == "name") { c->name = value; stored = value; return true; }
            if (f == "loop") { if (!boolean(value, b, err)) return false; c->loop = b; stored = boolText(b); return true; }
            if (f == "warp" || f == "bpm" || f == "pitch") // R-WARP-1
            {
                if (!c->isAudio()) { err = id + " is a note clip — `" + f + "` is an audio clip's"; return false; }
                if (!c->takes.empty()) { err = id + " is comped from takes — consolidate it first, then " + (f == "pitch" ? "move its pitch" : "warp it"); return false; }
                if (f == "warp")
                {
                    if (!boolean(value, b, err)) return false;
                    if (b && c->bpm <= 0)
                    {
                        // its tempo heard, in the same edit (REQ-tempo-1)
                        const double heard = tempoOf(*c);
                        if (heard <= 0) { err = "no tempo heard in " + id + " — set " + id + ".bpm=<its tempo> first"; return false; }
                        c = p.clip(id);
                        c->bpm = heard;
                    }
                    c->warp = b;
                    stored = boolText(b);
                    return true;
                }
                if (!number(value, x, err)) return false;
                if (f == "bpm") { if (!inRange(x, 20, 999, "bpm", err)) return false; c->bpm = x; }
                else { if (!inRange(x, -24, 24, "pitch (semitones)", err)) return false; c->pitch = x; }
                stored = canonicalNumber(x);
                return true;
            }
            if (f == "reverse") // R-EDIT-1
            {
                if (!c->isAudio()) { err = id + " is a note clip — `reverse` plays an audio clip's span backwards"; return false; }
                if (!boolean(value, b, err)) return false;
                c->reverse = b;
                stored = boolText(b);
                return true;
            }
            if (std::find(fields.begin(), fields.end(), f) == fields.end()) { err = unknownField(id, "a clip", f, fields); return false; }
            if (!number(value, x, err)) return false;
            if (f == "gain") { if (!inRange(x, -120, 12, "gain (dB)", err)) return false; c->gain = x; stored = canonicalNumber(x); return true; }
            if (f == "in" || f == "out")
            {
                if (!c->isAudio()) { err = id + " is a note clip — `" + f + "` is an audio clip's source range"; return false; }
                if (x < 0) { err = f + " is seconds into the file and cannot be negative"; return false; }
                (f == "in" ? c->in : c->out) = x;
                stored = canonicalSeconds(x);
                return true;
            }
            if (x < 0) { err = f + " cannot be negative"; return false; }
            const double t = toTick(x);
            if (f == "at") c->at = t;
            else if (f == "length") c->length = t;
            else if (f == "fadeIn") c->fadeIn = t;
            else c->fadeOut = t;
            stored = canonicalBeats(t);
            return true;
        }
        if (Lane *l = p.lane(id))
        {
            if (f == "name") { l->name = value; stored = value; return true; }
            if (f == "colour") { if (!number(value, x, err) || !inRange(x, -1, 15, "colour", err)) return false; l->colour = (int)x; stored = std::to_string((int)x); return true; }
            if (f == "strip")
            {
                // R-LANE-3: a lane becomes an instrument's track — or plain again, keeping the name it showed
                if (value == "none")
                {
                    l->name = laneTitle(p, *l);
                    l->track.clear();
                    stored = "none";
                    return true;
                }
                const Strip *s = p.strip(value);
                if (!s) { err = "no strip `" + value + "` (or `none`)"; return false; }
                if (s->kind != "instrument") { err = value + " is a " + s->kind + " strip — a track belongs to an instrument (R-LANE-3)"; return false; }
                for (const auto &cl : p.clips)
                    if (cl.lane == l->id && cl.track != value)
                    {
                        err = id + " holds " + cl.id + ", which " + (cl.isAudio() ? "is audio" : "plays through " + cl.track) + " — move it first (R-LANE-3)";
                        return false;
                    }
                l->track = value;
                stored = value;
                return true;
            }
            if (f == "group") // R-ARR-3: a lane joins a group (gathered with it) or leaves its own (placed after it)
            {
                if (value == "none")
                {
                    if (l->group.empty()) { stored = "none"; return true; }
                    const std::string g = l->group, me = l->id;
                    l->group.clear();
                    std::vector<Lane *> order;
                    for (auto &x : p.lanes) order.push_back(&x);
                    std::stable_sort(order.begin(), order.end(), [](const Lane *a, const Lane *b) { return a->order < b->order; });
                    order.erase(std::remove_if(order.begin(), order.end(), [&](const Lane *x) { return x->id == me; }), order.end());
                    size_t at = order.size();
                    for (size_t k = 0; k < order.size(); ++k)
                        if (order[k]->group == g) at = k + 1;
                    order.insert(order.begin() + (long)std::min(at, order.size()), p.lane(me));
                    for (size_t k = 0; k < order.size(); ++k) order[k]->order = (int)k;
                }
                else
                {
                    if (!p.group(value)) { err = "no lane group `" + value + "` (or `none`)"; return false; }
                    l->group = value;
                }
                gatherLaneGroups(p);
                stored = value;
                return true;
            }
            err = unknownField(id, "a lane", f, {"name", "colour", "strip", "group"});
            return false;
        }
        if (LaneGroup *g = p.group(id)) // R-ARR-3
        {
            if (f == "name") { g->name = value; stored = value; return true; }
            if (f == "folded")
            {
                if (value != "on" && value != "off") { err = id + ".folded is on or off, got `" + value + "`"; return false; }
                g->folded = value == "on";
                stored = value;
                return true;
            }
            err = unknownField(id, "a lane group", f, {"name", "folded"}) + " — its lanes are `lane group` / `lane ungroup`";
            return false;
        }
        if (Mixer *m = p.mixer(id))
        {
            if (f == "name") { m->name = value; stored = value; return true; }
            err = unknownField(id, "a mixer", f, {"name"}) + " — order is `mixer move " + id + " --to …`";
            return false;
        }
        if (Send *sd = p.send(id))
        {
            if (f == "gain") { if (!number(value, x, err) || !inRange(x, -120, 12, "gain (dB)", err)) return false; sd->gain = x; stored = canonicalNumber(x); return true; }
            if (f == "pre") { if (!boolean(value, b, err)) return false; sd->pre = b; stored = boolText(b); return true; }
            if (f == "sidechain") { if (!boolean(value, b, err)) return false; sd->sidechain = b; stored = boolText(b); return true; } // R-MIX-15
            err = unknownField(id, "a send", f, {"gain", "pre", "sidechain"});
            return false;
        }
        if (Pattern *pt = p.pattern(id))
        {
            if (f == "name") { pt->name = value; stored = value; return true; }
            if (f == "length") { if (!number(value, x, err)) return false; if (!(x > 0)) { err = "a pattern's length must be more than 0 beats"; return false; } pt->length = toTick(x); stored = canonicalBeats(pt->length); return true; }
            err = unknownField(id, "a pattern", f, {"name", "length"}) + " — notes are `note add`/`note delete`";
            return false;
        }
        if (Port *po = p.port(id))
        {
            if (f == "name") { po->name = value; stored = value; return true; }
            if (f == "channels") { if (!number(value, x, err) || !inRange(x, 1, 64, "channels", err)) return false; po->channels = (int)x; stored = std::to_string((int)x); return true; }
            err = unknownField(id, "a port", f, {"name", "channels"});
            return false;
        }
        if (DeviceNode *d = p.device(id))
        {
            if (f == "bypass") { if (!boolean(value, b, err)) return false; d->bypass = b; stored = boolText(b); return true; }
            const DeviceType *t = typeOf(*d); // R-VST-6: a plugin's parameters are its own, p<id> 0 … 1
            if (!t) { err = "this build has no `" + d->type + "` device, so " + id + "'s parameters cannot be checked"; return false; }
            if (f == "sample" && t->takesSample)
            {
                // R-EDM-8: the sound it plays — "" takes it away
                if (value.empty()) { d->sample.clear(); stored.clear(); return true; }
                if (!resolveSample(value, d->sample, err, t->takesDefinition)) return false;
                stored = d->sample;
                return true;
            }
            if (f == "sample") { err = id + " is a " + t->label + ", which plays no sample (a sampler does)"; return false; }
            if (t->readingIndex(f) >= 0) // R-FX-17: what it measures is read, never written
            { err = id + "." + f + " is what the " + t->label + " measures — `get` it; it cannot be set"; return false; }
            const int i = t->paramIndex(f);
            if (i < 0)
            {
                std::vector<std::string> names = {"bypass"};
                if (t->takesSample) names.push_back("sample");
                for (const auto &ps : t->params) names.push_back(ps.name);
                err = unknownField(id, "a " + t->label, f, names);
                return false;
            }
            const ParamSpec &spec = t->params[i];
            // R-SVC-8 / R-DSP-3: a value outside the registry's range is REFUSED naming the range, as a strip's
            // gain is — never clamped in silence; a choice is one of its names (or its index)
            bool valid = parseParam(spec, value, x);
            double typed = 0;
            const bool numeric = parseNumber(value, typed) && std::isfinite(typed);
            if (valid && spec.isChoice() && std::find(spec.choices.begin(), spec.choices.end(), value) == spec.choices.end())
                valid = numeric && typed == std::floor(typed) && typed >= 0 && typed < (double)spec.choices.size();
            if (valid && !spec.isChoice() && (typed < spec.min || typed > spec.max))
            {
                err = id + "." + f + " must be between " + canonicalNumber(spec.min) + " and " + canonicalNumber(spec.max) +
                      (spec.unit.empty() ? std::string() : " " + spec.unit) + ", got " + value;
                return false;
            }
            if (valid && spec.integer && typed != std::floor(typed)) { err = id + "." + f + " takes whole numbers, got " + value; return false; }
            if (!valid)
            {
                err = "`" + value + "` is not a value of " + id + "." + f;
                if (spec.isChoice())
                {
                    err += " (one of: ";
                    for (size_t k = 0; k < spec.choices.size(); ++k) err += (k ? ", " : "") + spec.choices[k];
                    err += ")";
                }
                return false;
            }
            stored = paramText(spec, x);
            for (auto &kv : d->params)
                if (kv.first == f) { kv.second = stored; return true; }
            d->params.emplace_back(f, stored);
            return true;
        }
        std::vector<std::string> ids = p.allIds();
        ids.push_back("project");
        err = "no node `" + id + "`";
        const auto near = nearest(id, ids);
        if (!near.empty()) err += " (did you mean: " + near[0] + "?)";
        return false;
    }

    bool SolarisService::getAddress(const std::string &address, std::string &value, std::string &err) const
    {
        const auto dot = address.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == address.size())
        {
            err = "an address is <id>.<field>, got `" + address + "` (`api` lists them)";
            return false;
        }
        const std::string id = address.substr(0, dot), f = address.substr(dot + 1);
        const Project &p = mProject;
        bool have = false;
        auto put = [&](const char *field, const std::string &v) {
            if (f == field) { value = v; have = true; }
        };
        if (const Binding *bd = p.binding(address))
        {
            value = bd->formula; // what decides it
            return true;
        }
        if (const Automation *a = p.automation(id))
        {
            put("name", a->name); put("unit", a->unit); put("min", canonicalNumber(a->min)); put("max", canonicalNumber(a->max));
            put("from", a->from); put("points", std::to_string(a->points.size()));
            put("loop", canonicalBeats(a->loop)); put("at", canonicalBeats(a->at)); put("length", canonicalBeats(a->length)); // R-AUTO-12
        }
        else if (id == "project")
        {
            put("name", p.header.name); put("bpm", canonicalNumber(p.header.bpm)); put("sig", p.header.sig);
            put("masterGain", canonicalNumber(p.header.masterGain)); put("sampleRate", std::to_string(p.header.sampleRate));
        }
        else if (const Strip *s = p.strip(id))
        {
            put("name", s->name); put("kind", s->kind); put("gain", canonicalNumber(s->gain)); put("pan", canonicalNumber(s->pan));
            put("mute", boolText(s->mute)); put("solo", boolText(s->solo)); put("colour", std::to_string(s->colour));
            put("out", s->out.empty() ? "master" : s->out);
            put("mixer", p.mixerOf(*s) ? p.mixerOf(*s)->id : std::string());
            put("input", s->input.empty() ? "none" : s->input); // R-REC-1
            put("monitor", s->monitor);
            put("vca", s->vca.empty() ? "none" : s->vca); // R-MIX-19
            put("phase", s->phase ? "on" : "off");          // R-MIX-20
            put("width", canonicalNumber(s->width));
            put("soloSafe", boolText(s->soloSafe));
        }
        else if (const Clip *c = p.clip(id))
        {
            put("name", c->name); put("at", canonicalBeats(c->at)); put("length", canonicalBeats(clipLengthBeats(p, *c)));
            put("gain", canonicalNumber(c->gain)); put("fadeIn", canonicalBeats(c->fadeIn)); put("fadeOut", canonicalBeats(c->fadeOut));
            put("loop", boolText(c->loop)); put("track", c->track); put("lane", c->lane);
            if (c->isAudio())
            {
                put("in", canonicalSeconds(c->in)); put("out", canonicalSeconds(c->out)); put("src", c->src); put("reverse", boolText(c->reverse));
                put("warp", boolText(c->warp)); put("bpm", canonicalNumber(c->bpm)); put("pitch", canonicalNumber(c->pitch)); // R-WARP-1
            }
            else put("pattern", c->pattern);
        }
        else if (const Lane *l = p.lane(id)) { put("name", l->name); put("colour", std::to_string(l->colour)); put("strip", l->track.empty() ? "none" : l->track); put("group", l->group.empty() ? "none" : l->group); }
        else if (const LaneGroup *g = p.group(id)) { put("name", g->name); put("folded", g->folded ? "on" : "off"); } // R-ARR-3
        else if (const Mixer *m = p.mixer(id)) { put("name", m->name); put("order", std::to_string(m->order)); }
        else if (const Pattern *pt = p.pattern(id)) { put("name", pt->name); put("length", canonicalBeats(pt->length)); }
        else if (const Port *po = p.port(id)) { put("name", po->name); put("channels", std::to_string(po->channels)); }
        else
        {
            for (const auto &sd : p.sends)
                if (sd.id == id) { put("to", sd.to); put("gain", canonicalNumber(sd.gain)); put("pre", boolText(sd.pre)); put("sidechain", boolText(sd.sidechain)); }
            for (const auto &r : p.racks)
                for (const auto &d : r.devices)
                    if (d.id == id)
                    {
                        put("bypass", boolText(d.bypass));
                        put("type", d.type);
                        put("sample", d.sample);
                        put("plugin", d.plugin); // R-VST-6
                        for (const auto &kv : d.params) put(kv.first.c_str(), kv.second);
                        if (!have)
                            if (const DeviceType *t = d.type != "vst3" ? DeviceRegistry::find(d.type)
                                                      : mPluginLive.count(d.id) ? &mPluginLive.at(d.id)->type : nullptr) // R-VST-6
                            {
                                const int i = t->paramIndex(f);
                                if (i >= 0) put(f.c_str(), paramText(t->params[i], t->params[i].def)); // not stored = the default
                                else if (t->readingIndex(f) >= 0)
                                {
                                    // R-FX-17: a reading — as heard now while playing (the model's), 0 stopped
                                    double v = 0.0;
                                    auto find = [&](const std::vector<DeviceModel> &ds) {
                                        for (const auto &dm : ds)
                                            if (dm.id == id)
                                                for (const auto &rd : dm.readings)
                                                    if (rd.name == f) v = rd.value;
                                    };
                                    for (const auto &s : mModel.strips) find(s.devices);
                                    find(mModel.masterDevices);
                                    put(f.c_str(), canonicalNumber(v));
                                }
                            }
                    }
        }
        if (have) return true;
        // Unreadable: let the writer's router name the problem (no such node, no such field).
        SolarisService probe(*this);
        probe.mSinks.clear();
        std::string stored;
        if (!probe.setAddress(address, "0", stored, err) && !err.empty()) return false;
        err = "`" + address + "` cannot be read";
        return false;
    }
}
}
