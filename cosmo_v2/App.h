/*
 *  cosmo_v2 by arstro — the Figma-exact editor UI, wired to cosmo_core's
 *  EditSession for real editing behavior. Mirrors CosmoApp's public shape
 *  (host-callback seam, session/workspace passthroughs) so linux_main.cpp's
 *  native dialog glue is nearly identical to cosmo's -- but the Segment tree
 *  built here is an entirely new visual language, not a reference to cosmo's.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include "../cosmo_core/EditSession.h"
#include "Theme.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class App
    {
    public:
        App(double width, double height);

        void render(artboard::IRenderTarget &target, double nowMs);
        void pointer(int kind, double x, double y, int button, double timeMs, bool alt = false, bool shift = false, bool ctrl = false);
        void wheel(double x, double y, double delta, bool ctrl);
        void setSize(double width, double height);

        int openImage(const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path = "");
        void selectImage(int slot);
        void deleteSelected();
        int imageCount() const { return mSession.imageCount(); }
        const uint8_t *exportFullRes(int &w, int &h) { return mSession.exportFullRes(w, h); }

        // ── File-menu actions wired by the host (it owns the file dialogs) ──
        std::function<void()> onOpenRequested;
        std::function<void()> onSaveRequested;
        std::function<void()> onSaveAsRequested;
        std::function<void()> onSavePresetRequested;
        std::function<void()> onExportPresetRequested;
        std::function<void()> onImportPresetRequested;
        std::function<void()> onRenameGroupRequested;
        std::function<void()> onSaveWorkspaceRequested;
        std::function<void()> onLoadWorkspaceRequested;
        void renameGroup(const std::string &name);

        void setPresetDir(const std::string &dir) { mSession.setPresetDir(dir); }
        bool savePreset(const std::string &name) { return mSession.savePreset(name); }
        bool exportPresetTo(const std::string &path) { return mSession.exportPresetTo(path); }
        /** NOTE: applies every present category immediately -- the category-picker
         *  modal (Figma brief frame 8) is one of the secondary states scoped for a
         *  follow-up pass, so import is "quick apply all" until it lands. */
        bool importPresetFrom(const std::string &path);
        bool applyPreset(const std::string &name) { return mSession.applyPreset(name); }

        void undo();
        void redo();
        bool canUndo() const { return mSession.canUndo(); }
        bool canRedo() const { return mSession.canRedo(); }

        std::string currentSourcePath() const { return mSession.currentSourcePath(); }
        void applyParams(const EditParams &p);
        void saveSession();
        bool saveSessionAs(const std::string &path) { return mSession.saveSessionAs(path); }
        static bool readSessionFile(const std::string &path, std::string &imagePath, EditParams &params)
        { return cosmo::EditSession::readSessionFile(path, imagePath, params); }

        using WorkspaceEntry = cosmo::EditSession::WorkspaceEntry;
        static bool readWorkspaceFile(const std::string &path, std::vector<WorkspaceEntry> &out)
        { return cosmo::EditSession::readWorkspaceFile(path, out); }
        bool saveWorkspaceAs(const std::string &path) { return mSession.saveWorkspaceAs(path); }
        void saveWorkspace();
        std::string currentWorkspacePath() const { return mSession.workspacePath(); }
        void resetWorkspace() { mSession.resetWorkspace(); }
        int addWorkspaceGroup(int parentNode, const std::string &name, const arstro::LocalAdjust &offset)
        { return mSession.addWorkspaceGroup(parentNode, name, offset); }
        int openImageInto(int parentNode, const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path)
        { return mSession.openImageInto(parentNode, rgba, w, h, name, path); }
        int addWorkspaceMissingImage(int parentNode, const std::string &name)
        { return mSession.addWorkspaceMissingImage(parentNode, name); }
        void applyParamsToSlot(int slot, const EditParams &p) { mSession.applyParamsToSlot(slot, p); }
        void finishWorkspaceLoad(const std::string &path);

    private:
        void layout();
        /** Push the current slot's params into every panel that isn't backed by
         *  live queries -- filled in as each panel lands (no-op until then). */
        void syncControlsToSlot();

        double mW, mH;
        cosmo::EditSession mSession;
        artboard::Theme mTheme;
        artboard::Color mAccent;
        artboard::GestureRecognizer mRecognizer;
        std::shared_ptr<artboard::Segment> mRoot;
    };
}
}
