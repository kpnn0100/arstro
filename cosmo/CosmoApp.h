/*
 *  Cosmo by arstro — a Lightroom-style photo editor built on Artboard (UI) and the
 *  ImageProcessing library (headless edit engine). Mirrors pulsar/PulsarApp.
 *
 *  Strict layering: the UI owns only an EditParams per image and submits it to a
 *  RenderService, which runs the EditEngine on its OWN worker thread (UI core +
 *  engine core). The UI never renders pixels or blocks; it polls finished frames.
 *  The engine + EditParams + RenderService are UI-free and reusable (video editor).
 *
 *  Right column: histogram (log/linear) above a TabView of edit sections — Basic,
 *  Mixer, Curve, Grade, Transform, Settings. Layout is responsive (setSize).
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include "../ImageProcessing/src/image_processing.h"
#include "CosmoTheme.h"
#include "History.h"
#include "panels/ParamPanel.h"
#include "panels/HistogramPanel.h"
#include "panels/MixerPanel.h"
#include "panels/ToneCurvePanel.h"
#include "panels/ColorGradingPanel.h"
#include "panels/TransformPanel.h"
#include "panels/SettingsPanel.h"
#include "panels/MaskPanel.h"
#include "widgets/Filmstrip.h"
#include "widgets/MenuBar.h"
#include "widgets/PresetBar.h"
#include "widgets/PresetDialog.h"
#include "widgets/PresetPanel.h"
#include "widgets/HistoryView.h"
#include "widgets/MaskOverlay.h"
#include "widgets/CropOverlay.h"
#include "widgets/CompareView.h"
#include "widgets/TextToggle.h"
#include "widgets/ContextMenu.h"
#include "widgets/Breadcrumb.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class CosmoApp
    {
    public:
        CosmoApp(double width, double height);

        void render(artboard::IRenderTarget &target, double nowMs);
        void pointer(int kind, double x, double y, int button, double timeMs, bool alt = false, bool shift = false, bool ctrl = false);
        /** Mouse wheel; with ctrl held over the photo it zooms about the cursor (#13). */
        void wheel(double x, double y, double delta, bool ctrl);
        void setSize(double width, double height);

        // `path` is the source file path (used by Save to record the working image).
        int openImage(const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path = "");
        void selectImage(int slot);
        /** Remove the current selection (images and/or groups, with their contents)
         *  from the session -- via the filmstrip's right-click menu, or the Delete key. */
        void deleteSelected();
        int imageCount() const { return (int)mSlotParams.size(); }
        const uint8_t *exportFullRes(int &w, int &h);

        // ── File-menu actions wired by the host (it owns the file dialogs) ──
        std::function<void()> onOpenRequested;     // host shows an open dialog
        std::function<void()> onSaveRequested;     // Save (falls back to Save As if no path)
        std::function<void()> onSaveAsRequested;   // host shows a save dialog
        std::function<void()> onSavePresetRequested;   // host prompts for a preset name (Save -> .apf in preset dir)
        std::function<void()> onExportPresetRequested; // host prompts for a save path (Export -> .apf anywhere)
        std::function<void()> onImportPresetRequested; // host shows an open dialog for a .apf file
        std::function<void()> onRenameGroupRequested;   // host prompts for a group name
        /** Rename the group targeted by the last "Rename" context action. */
        void renameGroup(const std::string &name);
        /** Whether the preset category picker is currently open (for tests). */
        bool presetPickerOpen() const;
        /** PresetBar world rect {x,y,w,h} (for tests). */
        std::vector<double> testBarRect() const;
        /** World {x,y} of history node i in the open popup (for tests). */
        std::vector<double> testHistoryNodeXY(int i) const;
        bool historyPopupOpen() const;
        /** Number of cells (images + sub-groups) shown for the current group (for tests). */
        int filmstripCells() const;
        /** Current photo zoom factor (for tests). */
        double imageZoom() const;
        /** Pixel width of the preview currently displayed (for tests). */
        int previewPixelWidth() const;

        /** Directory presets live in (host sets it; empty disables presets). */
        void setPresetDir(const std::string &dir);
        /** Save the current develop settings as `name`.apf in the preset dir, containing
         *  only the categories last chosen in the Save picker (all if none chosen). */
        bool savePreset(const std::string &name);
        /** Write the current develop settings as an .apf to an arbitrary path (Export),
         *  using the categories last chosen in the Export picker. */
        bool exportPresetTo(const std::string &path);
        /** Load an .apf from `path` and raise the category picker; on confirm the chosen
         *  categories are applied to the current image or the whole selected group.
         *  Returns false if the file cannot be parsed or targets a different engine. */
        bool importPresetFrom(const std::string &path);
        /** Quick-apply a named preset from the preset dir to the current image (all
         *  present categories, no picker) — used by the Preset menu list. */
        bool applyPreset(const std::string &name);

        // ── edit history (branching "time machine"; Ctrl+Z / Ctrl+Y) ──
        void undo();               // step back to the parent state
        void redo();               // step forward to the newest child state
        void openHistoryView();    // show the git-tree history popup
        bool canUndo() const;      // (for tests / host)
        bool canRedo() const;
        int historyNodeCount() const;   // nodes in the current image's history (for tests)
        int historyCurrent() const;     // current node index (for tests)
        /** Configure history for all images: max stored steps and the coalesce window. */
        void setHistoryLimits(int maxSteps, double coalesceMs);

        /** Source image path of the current slot (for the host's Save dialog default). */
        std::string currentSourcePath() const;
        /** Apply a full param set to the current slot (e.g. after loading a session). */
        void applyParams(const EditParams &p);
        /** Save the current slot's session (image path + params) to its remembered path,
         *  or trigger Save As if it has none yet. */
        void saveSession();
        /** Write the current slot's session to `path` and remember it. */
        bool saveSessionAs(const std::string &path);
        /** Parse a .cosmo session file into the referenced image path + params. */
        static bool readSessionFile(const std::string &path, std::string &imagePath, EditParams &params);

        // ── whole workspace (every open image + its settings + the group tree) ──
        /** One entry read back from a workspace file, in file order. `parent` indexes
         *  an earlier entry in the same vector (-1 = attach at the workspace root). */
        struct WorkspaceEntry
        {
            bool group = false;
            int parent = -1;
            std::string name;              // group name (groups only)
            arstro::LocalAdjust offset;    // group scalar offset (groups only)
            std::string imagePath;         // source file path (images only)
            EditParams params;              // develop settings (images only)
        };
        /** Parse a workspace file into its entries, in the order they should be
         *  recreated (a group always precedes anything it's the parent of). */
        static bool readWorkspaceFile(const std::string &path, std::vector<WorkspaceEntry> &out);
        /** Write every open image + the group tree + each image's settings to `path`. */
        bool saveWorkspaceAs(const std::string &path);
        /** Save to the remembered workspace path, or trigger onSaveWorkspaceRequested
         *  if this session didn't come from (or hasn't yet been saved to) one. */
        void saveWorkspace();
        /** Remembered path from the last load/save, for the host's dialog default. */
        std::string currentWorkspacePath() const { return mWorkspacePath; }
        /** Release every open image and reset to an empty, single-root session --
         *  call before recreating one from a loaded WorkspaceEntry list. */
        void resetWorkspace();
        /** Add a group node under `parentNode` (a real node index; use 0 for the
         *  workspace root). Returns the new node's index. */
        int addWorkspaceGroup(int parentNode, const std::string &name, const arstro::LocalAdjust &offset);
        /** Add an image leaf under `parentNode` without selecting it or touching the
         *  filmstrip (batch-friendly companion to openImage). Returns its slot id. */
        int openImageInto(int parentNode, const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path);
        /** Add a placeholder leaf (no engine slot) for an image that failed to decode,
         *  so later entries' `parent` indices stay aligned with the ones just created. */
        int addWorkspaceMissingImage(int parentNode, const std::string &name);
        /** Seed a freshly-created slot's params + history root (skips recording). */
        void applyParamsToSlot(int slot, const EditParams &p);
        /** Rebuild the filmstrip/selection after a batch of addWorkspaceGroup /
         *  openImageInto calls, and remember `path` for a plain "Save Workspace". */
        void finishWorkspaceLoad(const std::string &path);
        std::function<void()> onSaveWorkspaceRequested;  // host shows a save dialog -> saveWorkspaceAs(path)
        std::function<void()> onLoadWorkspaceRequested;  // host shows an open dialog -> loads via readWorkspaceFile

    private:
        /** `animateShift`: ease the properties that move because the preset panel
         *  opened/closed, instead of snapping (used only by that toggle). */
        void layout(bool animateShift = false);
        void submit();                 // push the current slot's params to the render service
        void syncControlsToSlot();
        void syncMaskUI();             // refresh mask panel + photo overlay from current slot
        void renderBefore();           // render the no-edit baseline for the compare view
        void pasteTo(const std::vector<int> &slots);  // copy clipboard params into slots
        void recordHistory();                          // snapshot the current slot's edit (from submit)
        void recordSlotEdit(int slot);                 // snapshot a specific slot as a discrete step
        void applyHistoryParams(const EditParams *p);  // apply an undo/redo/jump result to the slot
        void jumpToHistory(int node);                  // jump the current image to a history node
        void refreshPresetMenu();
        // ── preset save/import/export (generic .apf) ──
        void presetSaveClicked();    // bar SAVE   -> picker -> host name dialog
        void presetImportClicked();  // bar IMPORT -> host open dialog
        void presetExportClicked();  // bar EXPORT -> picker -> host path dialog
        void applyImport(const std::vector<std::string> &categories);  // apply mPendingApf to targets
        std::vector<int> selectedImageSlots() const;  // slots implied by the current selection (groups expand)
        static std::vector<PresetDialog::Row> buildPresetRows(const std::vector<std::string> &keys);
        // ── group tree (recursive; additive scalar offsets per group level) ──
        struct GNode
        {
            bool group = false;
            std::string name;
            int parent = 0;             // parent node index; root (0) is its own parent
            int slot = -1;              // image leaf -> mSlotParams index
            arstro::LocalAdjust offset; // group's additive scalar offset (groups only)
            std::vector<int> kids;      // child node indices, in display order
        };
        int nodeForSlot(int slot) const;
        void rebuildFilmstrip();        // show mCurGroup's children + breadcrumb
        void navigateToGroup(int node); // drill into / up to a group
        void selectNode(int cell, bool shift, bool ctrl);
        void showCellContext(int cell, double x, double y);
        void showPhotoContext(double x, double y);  // right-click anywhere on the photo -> Add Photo
        void showPresetContext(const std::string &name, double x, double y);  // right-click a preset -> Delete
        void deletePresetFile(const std::string &name);
        void createGroupFromSelection();
        void ungroupSelected();
        void collectSubtree(int node, std::vector<int> &out) const;  // node + every descendant, pre-order
        void deleteNode(int node);      // remove a node (and, for a group, its whole subtree)
        void setEditTarget(int node);   // image -> tabs; group -> offset panel
        EditParams effectiveParams(int slot) const;  // image params + sum of ancestor group offsets
        EditParams *curParams();       // the current image's params (what the tabs edit)

        double mW, mH;
        artboard::Rect mPhotoRect;
        artboard::Color mAccent;

        arstro::RenderService mService;  // engine on its own thread
        std::shared_ptr<artboard::Segment> mRoot;
        std::shared_ptr<artboard::Segment> mPhotoContext;  // right-click "Add Photo" over the photo area
        std::shared_ptr<artboard::ImageView> mImageView;
        std::shared_ptr<HistogramPanel> mHistogram;
        std::shared_ptr<artboard::TabView> mTabs;
        std::shared_ptr<ParamPanel> mBasic;
        std::shared_ptr<ParamPanel> mDetail;   // sharpening + noise reduction + lens
        std::shared_ptr<MixerPanel> mMixer;
        std::shared_ptr<ToneCurvePanel> mCurve;
        std::shared_ptr<ColorGradingPanel> mGrade;
        std::shared_ptr<TransformPanel> mXform;
        std::shared_ptr<MaskPanel> mMaskPanel;
        std::shared_ptr<MaskOverlay> mMaskOverlay;   // sits over the photo
        std::shared_ptr<CropOverlay> mCropOverlay;   // sits over the photo (Transform tab)
        std::shared_ptr<CompareView> mCompareView;   // before/after split over the photo
        std::shared_ptr<TextToggle> mCompareToggle;  // top-bar before/after switch
        std::shared_ptr<ParamPanel> mGroupPanel;     // group offset sliders (when a group is selected)
        std::shared_ptr<ContextMenu> mContextMenu;   // right-click popup
        std::shared_ptr<Breadcrumb> mBreadcrumb;     // group navigation path
        std::shared_ptr<SettingsPanel> mSettings;   // floating overlay (not a tab)
        std::shared_ptr<Filmstrip> mFilmstrip;
        std::shared_ptr<MenuBar> mMenuBar;
        std::shared_ptr<PresetBar> mPresetBar;       // bottom of the edit column (Save/Import/Export)
        std::shared_ptr<PresetDialog> mPresetDialog; // modal category picker (overlay)
        std::shared_ptr<PresetPanel> mPresetPanel;   // preset browser docked to the left (toggled)
        std::shared_ptr<IconButton> mPresetToggle;   // shows/hides mPresetPanel
        std::shared_ptr<HistoryView> mHistoryView;   // git-tree history popup (overlay)
        int mSelectedMask = -1;
        int mMaskTabIndex = 2;                       // Basic, Detail, Mask, ...
        int mXformTabIndex = 6;                      // ..., Mixer, Curve, Grade, Xform
        double mMenuBarX = 0, mMenuBarY = 0;        // position in root space (for outside-click)

        artboard::Theme mTheme;
        artboard::GestureRecognizer mRecognizer;

        std::vector<EditParams> mSlotParams;  // UI-authoritative per-image params (ungrouped)
        std::vector<History> mSlotHistory;    // branching edit timeline per image
        std::vector<std::string> mSlotNames;
        std::vector<std::string> mSlotPaths;      // source image file path per slot
        std::vector<std::string> mSlotSessions;   // last .cosmo save path per slot
        std::string mWorkspacePath;                // last workspace load/save path (empty = none yet)
        // recursive group tree (mNodes[0] = root group); images are leaves under it
        std::vector<GNode> mNodes;
        int mCurGroup = 0;        // group whose children the filmstrip shows
        std::vector<int> mSel;    // selected node indices within mCurGroup
        int mSelAnchor = -1;      // cell index in mCurGroup.kids for Shift range
        int mEditGroup = -1;      // group node edited via the offset panel; -1 = editing an image
        int mRenameTarget = -1;   // group awaiting a name from onRenameGroupRequested
        int mCurrentSlot = -1;
        int mPreviewEdge = 1600;
        RenderService::Frame mExportFrame;
        RenderService::Frame mBeforeFrame;  // no-edit baseline for compare
        int mBeforeSlot = -1;                // slot mBeforeFrame was last rendered for
        EditParams mBeforeGeom;              // geometry fields last rendered into mBeforeFrame
        EditParams mClipboard;              // copy/paste develop settings
        bool mHasClip = false;
        std::string mPresetDir;             // where named presets are stored
        int mPresetMenuIndex = -1;          // menu bar index of the Preset dropdown
        bool mPresetPanelOpen = true;       // target state of the animated left-docked preset browser
        std::vector<std::string> mPendingCategories;  // categories chosen in the Save/Export picker
        arstro::apf::Document mPendingApf;             // parsed doc awaiting Import confirmation
        double mNowMs = 0.0;                           // last frame time (for history coalescing)
        int mHistorySteps = 100;                       // default max steps for new images
        double mHistoryCoalesceMs = 450.0;             // default coalesce window for new images
        bool mSuppressHistory = false;                 // guards param changes that must not record
    };
}
}
