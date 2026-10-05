/*
 *  interstellar_core — autosave and crash recovery (R-DLV-5, R-DLV-6).
 *
 *  While the project has unsaved changes, every `autosave` seconds (60 unless set; 0 = off) the
 *  service writes the state beside the project: `<stem>.autosave.isp` (the `.isp` as it is in
 *  memory — a valid project file) and `<stem>.autosave.grades` (every rack node's own params and
 *  bypass, as Cosmo holds them — Cosmo's `.cmp` cannot be saved anywhere but over itself, and not at
 *  all while a source is offline, D-2). Each file is written beside its name and renamed over it, so
 *  a crash mid-write leaves the previous autosave whole.
 *
 *  A save removes them, and so does a deliberate close (`project close`): the changes were kept or
 *  given up. Only a session that ended without either — a crash — leaves them. Opening a project
 *  whose autosave is newer than its file offers it (`recoveryAvailable`); `project recover` puts it
 *  back — the .isp, and every grade written THROUGH Cosmo as an undo restore writes it — and leaves
 *  the project dirty, so the user decides whether to keep it; `--discard` removes it.
 */
#include "ServiceInternal.h"
#include "Project.h"
#include "engine/EditParamsIO.h"
#include <chrono>
#include <filesystem>
#include <fstream>
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
        bool writeAtomically(const std::string &path, const std::string &text, std::string &err)
        {
            const std::string tmp = path + ".part";
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                if (!f || !(f << text) || !f.flush()) { err = "cannot write " + tmp; return false; }
            }
            std::error_code ec;
            fs::rename(tmp, path, ec);
            if (ec) { err = "cannot move " + tmp + " over " + path; return false; }
            return true;
        }

        // file_clock's epoch is the library's, so only differences mean anything here
        bool newer(const std::string &a, const std::string &b)
        {
            std::error_code ea, eb;
            const auto ta = fs::last_write_time(a, ea), tb = fs::last_write_time(b, eb);
            return !ea && (eb || ta > tb);
        }
    }

    std::string InterstellarService::autosavePath(const char *ext) const
    {
        if (mIspPath.empty()) return std::string();
        const fs::path p(mIspPath);
        return (p.parent_path() / (p.stem().string() + ".autosave." + ext)).string();
    }

    bool InterstellarService::writeAutosave(std::string &err)
    {
        if (!mOpen || mIspPath.empty()) { err = "no project to autosave"; return false; }
        UndoState s;
        captureState(s);
        std::ostringstream grades;
        for (const auto &n : s.rack)
            grades << "@" << n.rackObj << " bypass=" << (n.bypass ? 1 : 0) << "\n" << n.params << (n.params.empty() || n.params.back() == '\n' ? "" : "\n") << "@end\n";
        // the grades first: an .isp without them is the one that would mislead
        if (!writeAtomically(autosavePath("grades"), grades.str(), err) || !writeAtomically(autosavePath("isp"), s.project, err)) return false;
        mModel.autosavedAt = (long long)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        emit(Event(EK::Info).with("text", "autosaved beside " + mIspPath));
        return true;
    }

    void InterstellarService::removeAutosave()
    {
        std::error_code ec;
        if (mIspPath.empty()) return;
        fs::remove(autosavePath("isp"), ec);
        fs::remove(autosavePath("grades"), ec);
        mModel.recoveryAvailable = false;
        mModel.recoveryTime = 0;
    }

    void InterstellarService::checkRecovery()
    {
        // R-DLV-6: an autosave newer than the file is a session that ended without saving
        const std::string a = autosavePath("isp");
        mModel.recoveryAvailable = !a.empty() && fs::exists(a) && newer(a, mIspPath) && fs::exists(autosavePath("grades"));
        mModel.recoveryTime = 0;
        if (mModel.recoveryAvailable)
        {
            // when it was written, on the wall clock: the file clock's age of it, taken from now
            std::error_code ec;
            const auto age = fs::file_time_type::clock::now() - fs::last_write_time(a, ec);
            mModel.recoveryTime = (long long)std::chrono::duration_cast<std::chrono::seconds>(
                                      (std::chrono::system_clock::now() - std::chrono::duration_cast<std::chrono::system_clock::duration>(age)).time_since_epoch())
                                      .count();
        }
        if (mModel.recoveryAvailable) emit(Event(EK::Info).with("text", "an autosave newer than " + mIspPath + " can be recovered — `project recover`"));
    }

    void InterstellarService::pumpAutosave()
    {
        if (!mOpen || mIspPath.empty() || mSettings.autosaveSeconds <= 0 || !mModel.dirty) { mAutosaveDueMs = 0; return; }
        // the first unsaved change starts the clock; then every interval while changes stay unsaved
        if (mAutosaveDueMs <= 0) { mAutosaveDueMs = mNowMs + mSettings.autosaveSeconds * 1000.0; return; }
        if (mNowMs < mAutosaveDueMs) return;
        std::string err;
        if (!writeAutosave(err)) emit(Event(EK::Error).with("why", "autosave: " + err));
        mAutosaveDueMs = mNowMs + mSettings.autosaveSeconds * 1000.0;
        refreshModel();
    }

    bool InterstellarService::safetyCommand(const Command &c)
    {
        std::string err;
        if (c.kind == CK::ProjectAutosave)
        {
            if (!writeAutosave(err)) return fail("project autosave: " + err);
            mOutput = autosavePath("isp") + "\n";
            return true;
        }
        // project recover [--discard]
        if (!fs::exists(autosavePath("isp")) || !fs::exists(autosavePath("grades"))) return fail("project recover: there is no autosave beside " + mIspPath);
        if (c.has("discard"))
        {
            removeAutosave();
            emit(Event(EK::Info).with("text", "the autosave was discarded"));
            return true;
        }
        if (mPending) return fail("project recover: the rack is still loading — `wait rack.loaded` first");
        std::ifstream pi(autosavePath("isp"), std::ios::binary), gi(autosavePath("grades"), std::ios::binary);
        UndoState s;
        s.project.assign((std::istreambuf_iterator<char>(pi)), std::istreambuf_iterator<char>());
        std::string line;
        UndoState::Node node;
        bool in = false;
        while (std::getline(gi, line))
        {
            if (!in && line.size() > 1 && line[0] == '@')
            {
                const auto sp = line.find(' ');
                node = UndoState::Node{};
                node.rackObj = line.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
                node.bypass = line.find("bypass=1") != std::string::npos;
                in = true;
            }
            else if (in && line == "@end") { s.rack.push_back(node); in = false; }
            else if (in) node.params += line + "\n";
        }
        Project check;
        if (s.project.empty() || !check.parse(s.project, err)) return fail("project recover: the autosave cannot be read: " + err);
        if (!applyState(s, err, false)) return fail("project recover: " + err);
        clearHistory();
        markDirty();   // recovered, not saved: the user keeps it by saving
        mModel.recoveryAvailable = false;
        mModel.recoveryTime = 0;
        bumpFrame();
        emit(Event(EK::Info).with("text", "recovered the autosave — save to keep it"));
        return true;
    }
}
}
