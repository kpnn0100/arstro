/*
 *  cosmo_v2 by arstro — ExportDialog: the in-app Export modal (R-EXPORT).
 *
 *  Ported from the reference design's `ExportModal` (ref/cosmo/File Reader
 *  Design(1).zip -> src/app/App.tsx) onto this app's real group tree, with the
 *  three deltas the requirement calls out:
 *    * "Images to Export" is the GROUP TREE with a checkbox per row and
 *      parent<->child propagation (R-EXPORT-2), not chips + a thumbnail grid;
 *    * Destination carries a "Same as source" checkbox on its right; unticked,
 *      an explicit folder is chosen, defaulting to the first selected image's
 *      folder (R-EXPORT-3);
 *    * Quality is two rows — a label/value row, then the slider on its own row
 *      (R-EXPORT-4).
 *
 *  Same modal chrome as PresetDialog/SettingsDialog (R-PRESETPICK-3): drawn in
 *  the overlay pass, dim scrim, one centred card, eased fade+rise, click-outside
 *  or Esc cancels. Fully self-drawn (no child Segments) because a child renders
 *  in the NORMAL pass and would land UNDER this card — so the two text fields are
 *  hand-rolled the same way ContextMenu's inline rename field is (focusable
 *  Segment + handleKey).
 *
 *  Tri-state rule (R-EXPORT-2): the IMAGE LEAVES are the authority and a group's
 *  state is DERIVED from its descendants (all -> ticked, some -> indeterminate,
 *  none -> unticked). That single rule yields all four required behaviours —
 *  select parent selects every child, deselect parent deselects every child,
 *  deselecting any child unticks the parent, ticking the last child ticks the
 *  parent — and cannot drift out of sync the way a stored per-group flag can.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "HoverFade.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class ExportDialog : public artboard::Segment
    {
    public:
        /** One node of the group tree, in PRE-ORDER (a parent always precedes its
         *  children). `parent` indexes THIS vector; < 0 = a root-level entry. */
        struct Node
        {
            int parent = -1;
            bool group = false;
            int slot = -1;              // image leaf only: the engine slot to render
            std::string name;
            std::string sourcePath;     // image leaf only: the original file on disk
        };

        /** What the host needs to write the batch (R-EXPORT-3/4/5). */
        struct Request
        {
            std::vector<int> slots;        // engine slots to render, in tree order
            bool sameAsSource = true;      // write each image next to its own original
            std::string destination;       // used only when !sameAsSource
            bool usePrefix = false;
            std::string prefix;
            bool useSubfolder = false;
            std::string subfolder;
            std::string format = "JPEG";   // JPEG | PNG | TIFF
            int quality = 92;              // JPEG only, 60..100
            int longEdge = 0;              // 0 = original, else the long-edge cap in px
            bool embedExif = true;
            bool stripGps = false;
            bool embedProfile = true;
        };

        explicit ExportDialog(const artboard::Color &accent);

        /** Open on a freshly-snapshotted tree. `preselect` are the slots that start
         *  ticked (the current selection); empty = every image. */
        void show(std::vector<Node> nodes, const std::vector<int> &preselect);
        bool isOpen() const { return mOpen && !mClosing; }
        /** True while the batch is being written — the host is mid-export and the
         *  dialog is showing its progress state (R-EXPORT-6). */
        bool isExporting() const { return mExporting; }
        void scrollBy(double delta, double x, double y);  // wheel: tree box or body

        // ── host seam ──
        /** "Change…": the host shows a native folder chooser and answers with
         *  setDestination(). */
        std::function<void()> onChooseDestination;
        /** Export pressed: the host starts writing and then feeds setExportProgress()
         *  one image at a time so the UI never freezes (R-EXPORT-6 / R2). */
        std::function<void(Request)> onExport;
        void setDestination(const std::string &dir);
        /** Host progress pump. `done == total` finishes and closes the dialog. */
        void setExportProgress(int done, int total, const std::string &name);
        /** Abort the progress state (a write failed outright) and return to the form.
         *  The host must have STOPPED its export worker before calling this: while
         *  isExporting() is true the app keeps its own UI thread out of the engine's
         *  full-render channel, which the worker is using. */
        void cancelExport();

        // ── tree model, as a public surface (R-EXPORT-2) ──
        // handleGesture only maps a pixel to a row and then calls these, so the
        // propagation rules are exercisable (and testable) without synthesising
        // clicks at computed coordinates.
        int rowCount() const { return (int)mRows.size(); }
        bool rowIsGroup(int row) const;
        /** 0 = unticked, 1 = ticked, 2 = indeterminate (a group with only some of its
         *  descendants ticked). */
        int rowState(int row) const;
        /** Activate a row's checkbox: a group ticks/unticks its whole subtree, an
         *  image toggles itself. Group states re-derive, so unticking any child
         *  unticks its ancestors and ticking the last child ticks them. */
        void toggleRow(int row);
        void toggleExpand(int row);      // groups only; no-op on an image row
        void setAllChecked(bool on);     // the master Select all / Select none button
        std::vector<int> selectedSlots() const { return checkedSlots(); }

    protected:
        void advance(double nowMs) override;
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool handleKey(const artboard::KeyEvent &e) override;
        bool hitTestSelf(const artboard::Point &p) const override { return mOpen && !mClosing; }  // modal

    private:
        /** One visible tree row: which node it shows and how deep it sits. Declared
         *  before the helpers that take a `std::vector<Row>`. */
        struct Row { int node = 0; int depth = 0; };

        // ── tree model (R-EXPORT-2) ──
        void rebuildRows();                          // flatten to the visible rows
        void collectLeaves(int node, std::vector<int> &out) const;  // descendant image leaves of `node`
        /** 0 = none checked, 1 = all checked, 2 = partially checked. Leaves answer 0/1. */
        int checkState(int node) const;
        void setSubtreeChecked(int node, bool on);
        int leafCount() const;
        int checkedCount() const;
        std::vector<int> checkedSlots() const;       // in tree order
        std::string firstCheckedFolder() const;      // default destination (R-EXPORT-3)
        std::string previewFilename() const;         // the example name under the destination row

        // ── geometry ──
        double formContentH() const;                 // the body's full (unscrolled) height
        /** The card's left edge. Split out from cardRect() deliberately: the card's
         *  HEIGHT depends on the form's content height, which depends on the layout,
         *  which only needs the card's X -- routing the layout through cardRect()
         *  would make the two mutually recursive. */
        double cardX() const;
        artboard::Rect cardRect() const;
        artboard::Rect bodyRect() const;             // the scrolling viewport
        artboard::Rect treeRect() const;             // the bordered tree box (inside the body)
        double bodyTop() const;                      // content y of the first section, at scroll 0
        // Section geometry is derived from one running cursor so a row can never
        // overlap its neighbour (implement_artboard §2 "snap, don't stack").
        struct Layout
        {
            artboard::Rect masterBtn, tree, selInfo;
            artboard::Rect destBox, destChange, sameCheck, pathPreview;
            artboard::Rect prefixCheck, prefixField, subCheck, subField;
            artboard::Rect fmtChip[3], sizeChip[4];
            artboard::Rect qualityLabel, qualitySlider;
            artboard::Rect metaToggle[3];
            double contentH = 0.0;
        };
        Layout layoutForm(double top) const;         // `top` = the body's content origin (scrolled)
        artboard::Rect closeRect() const;
        artboard::Rect cancelRect() const;
        artboard::Rect exportRect() const;
        int rowAt(const artboard::Point &p, bool &onChevron) const;  // tree row under the pointer

        void beginClose();
        void applyQualityAt(double localX);
        void beginExport();              // form -> progress face (R-EXPORT-6 beat 1)
        void beginComplete();            // progress -> the green-tick face (beat 3)
        /** Rebuild the progress face's row list: ONLY the selected images and the groups
         *  that contain them, so the manifest shows exactly what is being written. */
        void buildExportRows();
        void advanceRowFades(double nowMs);
        void followActiveRow();      // auto-scroll the manifest to the file in flight
        double rowDoneAmount(int exportRow) const;   // eased 0..1 "this row is written"
        artboard::Rect headerRect() const;           // the drag handle (R-EXPORT-8)
        double progressTreeH() const;
        artboard::Rect progressTreeRect() const;
        artboard::Rect progressBarRect() const;
        // Paint helpers: the body has two mutually-exclusive faces (the form and the
        // progress panel) that cross-fade through mPhase, so each owns its own pass.
        void drawForm(artboard::IRenderTarget &t, double a) const;
        void drawProgress(artboard::IRenderTarget &t, double a) const;
        void drawComplete(artboard::IRenderTarget &t, double a, const artboard::Rect &body) const;
        void drawFooter(artboard::IRenderTarget &t, double a, const artboard::Rect &foot) const;
        /** One tree renderer for both faces. `rows` is the picker's list in the form face
         *  and the export manifest in the progress face; `progress` fades the checkboxes
         *  out, slides the rest left into their space, and washes written rows. */
        void drawTree(artboard::IRenderTarget &t, double a, const artboard::Rect &box,
                      const std::vector<Row> &rows, double progress) const;
        /** Shared checkbox: `state` 0 = empty, 1 = ticked, 2 = indeterminate (a dash --
         *  the group "some children selected" case, R-EXPORT-2). */
        void drawCheck(artboard::IRenderTarget &t, const artboard::Rect &box, int state, double a,
                       double hoverAmt = 0.0, bool dimmed = false) const;
        void drawChip(artboard::IRenderTarget &t, const artboard::Rect &r, const std::string &label,
                      bool selected, double a, double hoverAmt, double fontPx) const;
        void drawField(artboard::IRenderTarget &t, const artboard::Rect &r, const std::string &text,
                       const std::string &placeholder, bool active, bool focused, double a) const;
        void drawSectionHeader(artboard::IRenderTarget &t, double x, double y, double w,
                               const std::string &label, double a) const;
        int hitId(const artboard::Point &p) const;   // HoverFade / click id under the pointer
        Request buildRequest() const;

        artboard::Color mAccent;
        bool mOpen = false, mClosing = false;
        double mLastMs = 0.0;
        artboard::AnimatedProperty mAppear{0.0};     // fade + rise (R-G-1)

        std::vector<Node> mNodes;
        std::vector<std::vector<int>> mKids;         // adjacency, built in show()
        std::vector<int> mRoots;
        std::vector<char> mExpanded;                 // per node (groups only)
        std::vector<char> mLeafChecked;              // per node; only image leaves are authoritative
        std::vector<Row> mRows;                      // flattened visible rows

        artboard::AnimatedProperty mTreeScroll{0.0}; // eased (R-G-1)
        double mTreeScrollTarget = 0.0;
        artboard::AnimatedProperty mBodyScroll{0.0};
        double mBodyScrollTarget = 0.0;

        // form state
        bool mSameAsSource = true;
        std::string mDestination;                    // explicit folder (when !mSameAsSource)
        bool mUsePrefix = false, mUseSubfolder = false;
        std::string mPrefix = "export_", mSubfolder = "exported";
        int mFormat = 0;                             // index into kFormats
        int mSize = 0;                               // index into kSizes
        int mQuality = 92;
        bool mMeta[3] = {true, false, true};         // EXIF / strip GPS / sRGB profile
        artboard::AnimatedProperty mMetaKnob[3];     // eased toggle knobs (R-G-1)
        int mFocusField = -1;                        // -1 none, 0 prefix, 1 subfolder
        bool mDraggingQuality = false;

        // progress state (R-EXPORT-6)
        bool mExporting = false;
        int mDone = 0, mTotal = 0;
        std::string mProgressName;
        artboard::AnimatedProperty mPhase{0.0};      // 0 = form, 1 = progress (collapse + card resize)
        artboard::AnimatedProperty mProgress{0.0};   // eased 0..1 bar fill
        std::vector<Row> mExportRows;                // the manifest: participating rows only
        std::vector<int> mExportSeq;                 // per mExportRows entry: its position in the
                                                     // write order (-1 for a group row)
        std::vector<double> mRowFade;                // per mExportRows entry: eased "written" amount
        double mRowFadeLastMs = -1.0;
        // R-EXPORT-6 beat 1 is PURE ANIMATION: the request is snapshotted when Export is
        // pressed but only handed to the host once the collapse has finished playing, so
        // the batch's first full-res render can never stall the tween (the same split
        // R-LOADING-0/1 uses for the open-project intro).
        Request mPendingRequest;
        bool mFirePending = false;
        bool mComplete = false;                      // beat 3: the green-tick face
        artboard::AnimatedProperty mCompleteAmt{0.0};
        double mCompleteAtMs = 0.0;                  // when beat 3 began (drives the auto-close hold)

        // ── drag (R-EXPORT-8) ──
        // The card is moved by its header band; the offset is applied inside cardX()/
        // cardRect() so layout, hit-testing and paint all move together.
        artboard::Point mCardOffset{0.0, 0.0};
        bool mDraggingCard = false;
        artboard::Point mDragGrab{0.0, 0.0};         // pointer position at the grab, in card space
        bool mSuppressClick = false;                 // a finished drag must not fire the click under it

        HoverFade mHover;                            // every region cross-fades (R-G-3)

        static const char *const kFormats[3];
        static const char *const kSizes[4];
        static const int kSizeEdges[4];
        static const char *const kMetaLabels[3];
    };
}
}
