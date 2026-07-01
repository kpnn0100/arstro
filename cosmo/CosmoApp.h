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
        int imageCount() const { return (int)mSlotParams.size(); }
        const uint8_t *exportFullRes(int &w, int &h);

        // ── File-menu actions wired by the host (it owns the file dialogs) ──
        std::function<void()> onOpenRequested;     // host shows an open dialog
        std::function<void()> onSaveRequested;     // Save (falls back to Save As if no path)
        std::function<void()> onSaveAsRequested;   // host shows a save dialog
        std::function<void()> onSavePresetRequested;  // host prompts for a preset name
        std::function<void()> onRenameGroupRequested; // host prompts for a group name
        /** Rename the group targeted by the last "Rename" context action. */
        void renameGroup(const std::string &name);
        /** Number of cells (images + sub-groups) shown for the current group (for tests). */
        int filmstripCells() const;
        /** Current photo zoom factor (for tests). */
        double imageZoom() const;
        /** Pixel width of the preview currently displayed (for tests). */
        int previewPixelWidth() const;

        /** Directory presets live in (host sets it; empty disables presets). */
        void setPresetDir(const std::string &dir);
        /** Save the current develop settings as a named preset file. */
        bool savePreset(const std::string &name);
        /** Apply a named preset to the current image. */
        bool applyPreset(const std::string &name);

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

    private:
        void layout();
        void submit();                 // push the current slot's params to the render service
        void syncControlsToSlot();
        void syncMaskUI();             // refresh mask panel + photo overlay from current slot
        void renderBefore();           // render the no-edit baseline for the compare view
        void pasteTo(const std::vector<int> &slots);  // copy clipboard params into slots
        void refreshPresetMenu();
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
        void createGroupFromSelection();
        void ungroupSelected();
        void setEditTarget(int node);   // image -> tabs; group -> offset panel
        EditParams effectiveParams(int slot) const;  // image params + sum of ancestor group offsets
        EditParams *curParams();       // the current image's params (what the tabs edit)

        double mW, mH;
        artboard::Rect mPhotoRect;
        artboard::Color mAccent;

        arstro::RenderService mService;  // engine on its own thread
        std::shared_ptr<artboard::Segment> mRoot;
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
        int mSelectedMask = -1;
        int mMaskTabIndex = 2;                       // Basic, Detail, Mask, ...
        int mXformTabIndex = 6;                      // ..., Mixer, Curve, Grade, Xform
        double mMenuBarX = 0, mMenuBarY = 0;        // position in root space (for outside-click)

        artboard::Theme mTheme;
        artboard::GestureRecognizer mRecognizer;

        std::vector<EditParams> mSlotParams;  // UI-authoritative per-image params (ungrouped)
        std::vector<std::string> mSlotNames;
        std::vector<std::string> mSlotPaths;      // source image file path per slot
        std::vector<std::string> mSlotSessions;   // last .cosmo save path per slot
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
        EditParams mClipboard;              // copy/paste develop settings
        bool mHasClip = false;
        std::string mPresetDir;             // where named presets are stored
        int mPresetMenuIndex = -1;          // menu bar index of the Preset dropdown
    };
}
}
