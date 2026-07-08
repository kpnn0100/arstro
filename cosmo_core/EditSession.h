/*
 *  Arstro cosmo_core — EditSession: the UI-free business logic of a Cosmo editing
 *  session (image slots, the nested group tree, develop-param edits, branching
 *  history, presets, session/workspace persistence). Extracted from cosmo's
 *  CosmoApp so a second, differently-styled UI (cosmo_v2) can drive the exact
 *  same editing behavior without depending on cosmo's Artboard scene at all.
 *
 *  This class owns no Artboard types and makes no Segment/IRenderTarget calls --
 *  a UI binds to it the same way cosmo's widgets bind to CosmoApp: read state via
 *  the query methods, push edits via the mutators, and poll renderService() for
 *  finished frames. Anything that requires showing a native dialog (open/save
 *  file pickers, a name prompt) is left to the host application; EditSession's
 *  own methods are synchronous and take the answer once the host has it.
 */
#pragma once
#include "../ImageProcessing/src/image_processing.h"
#include "../cosmo/History.h"
#include <cstdint>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class EditSession
    {
    public:
        EditSession();

        // ---- frame clock (history coalescing needs "now") ----
        void tick(double nowMs) { mNowMs = nowMs; }

        // ---- image I/O ----
        /** Decode + register an image into the currently-viewed group, select it,
         *  and return its slot id (-1 on failure). */
        int openImage(const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path = "");
        /** Add an image leaf under `parentNode` without selecting it (batch-friendly
         *  companion to openImage, used when loading a workspace). */
        int openImageInto(int parentNode, const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path);
        /** Add a placeholder leaf (no engine slot) for an image that failed to decode,
         *  so later workspace entries' `parent` indices stay aligned. */
        int addWorkspaceMissingImage(int parentNode, const std::string &name);
        int imageCount() const { return (int)mSlotParams.size(); }
        /** Remove every selected node (images and/or groups, with their contents). */
        void deleteSelected();

        // ---- group tree + selection ----
        struct GNode
        {
            bool group = false;
            std::string name;
            int parent = 0;              // parent node index; root (0) is its own parent
            int slot = -1;                // image leaf -> slot index
            arstro::LocalAdjust offset;   // group's additive scalar offset (groups only)
            std::vector<int> kids;        // child node indices, in display order
        };
        /** One row of "the current group's children" -- what a filmstrip widget draws. */
        struct Cell
        {
            bool group = false;
            int node = -1;
            int slot = -1;   // image leaf only
            std::string name;
            int count = 0;   // group leaf only (child count)
        };

        /** A small pre-downscaled copy of a slot's image, generated once on open --
         *  a filmstrip widget registers this (not the full-res image) as its thumb. */
        struct Thumb
        {
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
        };

        const std::vector<GNode> &nodes() const { return mNodes; }
        int currentGroup() const { return mCurGroup; }
        const std::vector<int> &selection() const { return mSel; }
        int editGroup() const { return mEditGroup; }  // >=0 while a group (not an image) is being edited
        int currentSlot() const { return mCurrentSlot; }
        int nodeForSlot(int slot) const;
        std::vector<Cell> currentGroupCells() const;      // for a filmstrip widget
        std::vector<std::string> breadcrumbPath() const;  // root -> ... -> current group, by name
        const Thumb *thumbForSlot(int slot) const;         // nullptr if slot is out of range

        void navigateToGroup(int node);                    // drill into / up to a group
        /** `cell` indexes currentGroupCells() / the current group's kids. */
        void selectNode(int cell, bool shift, bool ctrl);
        void selectImage(int slot);                         // jump straight to an image anywhere in the tree
        void createGroupFromSelection();
        void ungroupSelected();
        void renameGroup(int node, const std::string &name);
        void collectSubtree(int node, std::vector<int> &out) const;  // node + every descendant, pre-order
        std::vector<int> selectedImageSlots() const;  // selection expanded to leaf image slots (groups -> their images)

        // ---- develop params ----
        EditParams *curParams();                     // current image's own params (what controls edit)
        EditParams effectiveParams(int slot) const;  // image params + summed ancestor group offsets
        void applyParams(const EditParams &p);        // replace the current slot's params wholesale + submit
        void applyParamsToSlot(int slot, const EditParams &p);  // seed a slot's params + history root (no submit)
        /** As above, but restore a saved branching history instead of a fresh root
         *  (empty `history` falls back to a single-node root). Used on project load. */
        void applyParamsToSlot(int slot, const EditParams &p, const History &history);
        void submit();                                 // record history + re-render the current slot

        /** Unsaved-changes flag: true once an edit is submitted, cleared on
         *  save/load/reset. Drives the "save or discard?" prompt (R-HOME). */
        bool isDirty() const { return mDirty; }
        void markClean() { mDirty = false; }
        /** While true, submit() renders the full (uncropped) frame so a crop box can
         *  be dragged over the whole image; the slot's real crop is unaffected. */
        void setCropPreviewMode(bool on) { mCropPreviewMode = on; }

        // ---- clipboard ----
        void copyCurrent();                            // stash the current slot's params
        void pasteTo(const std::vector<int> &slots);   // apply the clipboard to each slot (discrete history step)
        bool hasClipboard() const { return mHasClip; }

        // ---- history (branching undo/redo) ----
        History *currentHistory();                      // nullptr if no current slot
        const EditParams *undo();
        const EditParams *redo();
        const EditParams *jumpToHistory(int node);
        void recordSlotEdit(int slot);                   // discrete step (paste/import/apply-preset)
        bool canUndo() const;
        bool canRedo() const;
        void setHistoryLimits(int maxSteps, double coalesceMs);

        // ---- presets (generic .apf) ----
        void setPresetDir(const std::string &dir) { mPresetDir = dir; }
        const std::string &presetDir() const { return mPresetDir; }
        /** Categories the next savePreset()/exportPresetTo() call restricts itself to
         *  (as chosen in a category-picker UI); empty = every category. */
        void setPendingCategories(std::vector<std::string> cats) { mPendingCategories = std::move(cats); }
        /** Save as `<presetDir>/name.apf` ("/" in `name` nests into a subfolder). */
        bool savePreset(const std::string &name);
        bool exportPresetTo(const std::string &path);
        /** Parse an .apf at `path`. On success fills `outPresentCategories` (for a
         *  category picker) and caches the document for a following applyImport(). */
        bool importPresetFrom(const std::string &path, std::vector<std::string> &outPresentCategories);
        /** Apply the categories (from the last importPresetFrom) to every selected
         *  image (or the current one if nothing is selected). */
        void applyImport(const std::vector<std::string> &categories);
        /** Quick-apply a named preset from presetDir to the current image (no picker). */
        bool applyPreset(const std::string &name);
        bool deletePresetFile(const std::string &name);

        // ---- session (single image + params, .cosmo) ----
        std::string currentSourcePath() const;
        bool saveSessionAs(const std::string &path);
        static bool readSessionFile(const std::string &path, std::string &imagePath, EditParams &params);
        std::string sessionPath() const { return (mCurrentSlot >= 0 && mCurrentSlot < (int)mSlotSessions.size()) ? mSlotSessions[mCurrentSlot] : std::string(); }

        // ---- workspace (every open image + group tree + settings, .cosmoproj) ----
        struct WorkspaceEntry
        {
            bool group = false;
            int parent = -1;
            std::string name;              // group name (groups only)
            arstro::LocalAdjust offset;    // group scalar offset (groups only)
            std::string imagePath;         // source file path (images only)
            EditParams params;              // develop settings (images only)
            History history;                // branching edit timeline (images only; empty = none saved)
        };
        static bool readWorkspaceFile(const std::string &path, std::vector<WorkspaceEntry> &out);
        bool saveWorkspaceAs(const std::string &path);
        std::string workspacePath() const { return mWorkspacePath; }
        /** Release every open image and reset to an empty, single-root session. */
        void resetWorkspace();
        int addWorkspaceGroup(int parentNode, const std::string &name, const arstro::LocalAdjust &offset);
        /** Rebuild derived state after a batch of addWorkspaceGroup/openImageInto
         *  calls and remember `path` for a plain "Save Workspace". */
        void finishWorkspaceLoad(const std::string &path);

        // ---- rendering engine (owned so the app just polls it) ----
        arstro::RenderService &renderService() { return mService; }
        /** Back to the base preview resolution (called when switching images). */
        void resetPreviewResolution();
        /** Base preview render long-edge in px (quality vs. speed). Settings panel
         *  writes this; it becomes the resolution every subsequent preview renders at. */
        int previewEdge() const { return mPreviewEdge; }
        void setPreviewEdge(int edge) { mPreviewEdge = edge < 64 ? 64 : edge; mService.setPreviewSize(mPreviewEdge); submit(); }
        /** Scale the preview resolution with the view's zoom factor (>=1) so a
         *  magnified image stays sharp; caller still calls submit() afterward. */
        void setPreviewZoom(double zoomFactor);
        const uint8_t *exportFullRes(int &w, int &h);
        /** No-edit (geometry-only) baseline for a before/after compare view; caches
         *  internally and only re-renders when the geometry actually changed.
         *  Returns nullptr if there is no current image. */
        const RenderService::Frame *renderBefore();

    private:
        const EditParams *applyHistoryParams(const EditParams *p);
        void recordHistory();  // snapshot the current slot's edit (called from submit())
        static Thumb makeThumb(const uint8_t *rgba, int w, int h, int maxEdge);

        arstro::RenderService mService;

        std::vector<EditParams> mSlotParams;   // UI-authoritative per-image params (ungrouped)
        std::vector<History> mSlotHistory;     // branching edit timeline per image
        std::vector<std::string> mSlotNames;
        std::vector<std::string> mSlotPaths;      // source image file path per slot
        std::vector<std::string> mSlotSessions;   // last .cosmo save path per slot
        std::vector<Thumb> mSlotThumbs;            // filmstrip thumbnail per slot
        std::string mWorkspacePath;                 // last workspace load/save path (empty = none yet)

        // recursive group tree (mNodes[0] = root group); images are leaves under it
        std::vector<GNode> mNodes;
        int mCurGroup = 0;         // group whose children currentGroupCells() shows
        std::vector<int> mSel;     // selected node indices within mCurGroup
        int mSelAnchor = -1;       // cell index in mCurGroup.kids for Shift range
        int mEditGroup = -1;       // group node being edited via the offset panel; -1 = editing an image
        int mCurrentSlot = -1;

        EditParams mClipboard;
        bool mHasClip = false;

        std::string mPresetDir;
        std::vector<std::string> mPendingCategories;
        arstro::apf::Document mPendingApf;

        double mNowMs = 0.0;
        int mHistorySteps = 100;
        double mHistoryCoalesceMs = 450.0;
        bool mSuppressHistory = false;
        bool mCropPreviewMode = false;
        int mPreviewEdge = 1600;
        bool mDirty = false;   // unsaved edits since the last save/load/reset

        RenderService::Frame mExportFrame;
        RenderService::Frame mBeforeFrame;
        int mBeforeSlot = -1;
        EditParams mBeforeGeom;
    };
}
}
