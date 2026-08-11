#include "ExportDialog.h"
#include "Icons.h"
#include "TextMetrics.h"
#include "../Theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    const char *const ExportDialog::kFormats[3] = {"JPEG", "PNG", "TIFF"};
    const char *const ExportDialog::kSizes[4] = {"Original", "2048 px", "1080 px", "720 px"};
    const int ExportDialog::kSizeEdges[4] = {0, 2048, 1080, 720};
    const char *const ExportDialog::kMetaLabels[3] = {
        "Embed EXIF data", "Strip GPS coordinates", "Embed colour profile (sRGB)"};

    namespace
    {
        // ── card chrome ──
        constexpr double kCardW = 560.0;
        constexpr double kPad = 20.0;
        constexpr double kHeaderH = 40.0;
        constexpr double kFooterH = 46.0;
        constexpr double kMargin = 24.0;      // min gap to the window edge
        constexpr double kCompleteBodyH = 92.0;  // body height once the batch is done
        // Beat 3 lingers briefly on the tick so the confirmation is readable, then the
        // dialog dismisses itself.
        constexpr double kCompleteHoldMs = 950.0;
        constexpr double kBarH = 4.0;

        // ── body rhythm (one type ramp, one spacing scale) ──
        constexpr double kSecH = 24.0;        // section-header band
        constexpr double kBtnH = 26.0;
        constexpr double kFieldH = 26.0;
        constexpr double kCheck = 15.0;
        constexpr double kFontPx = 11.0;
        constexpr double kMonoPx = 10.0;
        constexpr double kSmallPx = 10.0;
        // The shared label column is sized to its LONGEST label ("Export to subfolder")
        // rather than a round number, so neither modifier row ellipsizes (R5).
        constexpr double kLabelColW = 132.0;
        constexpr double kGapS = 6.0, kGapM = 8.0, kGapL = 14.0;

        // ── tree box ──
        constexpr double kTreeRowH = 22.0;
        constexpr int kTreeRows = 8;                              // visible rows
        constexpr double kTreeH = kTreeRows * kTreeRowH + 2.0;    // + the 1px border top/bottom,
                                                                  // so a row is never half-clipped
        constexpr double kIndent = 14.0;
        constexpr double kChevW = 12.0;

        constexpr double kDestBoxH = 30.0;
        constexpr double kMetaRowH = 26.0;
        constexpr double kToggleW = 30.0, kToggleH = 16.0;

        // HoverFade / hit ids. Tree rows live at kIdTree + rowIndex so they never
        // collide with the fixed controls below.
        enum : int {
            kIdClose = 0, kIdMaster, kIdChange, kIdSame,
            kIdPrefixCheck, kIdPrefixField, kIdSubCheck, kIdSubField,
            kIdFmt0, kIdFmt1, kIdFmt2,
            kIdSize0, kIdSize1, kIdSize2, kIdSize3,
            kIdMeta0, kIdMeta1, kIdMeta2,
            kIdQuality, kIdCancel, kIdExport,
            kIdTree = 64
        };

        inline Color fade(Color c, double a) { return Color{c.r, c.g, c.b, c.a * a}; }
        inline double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

        std::string folderOf(const std::string &path)
        {
            const auto slash = path.find_last_of('/');
            return slash == std::string::npos ? std::string() : path.substr(0, slash);
        }
        std::string fileOf(const std::string &path)
        {
            const auto slash = path.find_last_of('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        /** Truncate with a trailing ellipsis so the string fits `maxW` (R5). */
        std::string fitEnd(const std::string &s, double maxW, double px)
        {
            if (estimateTextWidth(s, px) <= maxW) return s;
            std::string out = s;
            while (!out.empty() && estimateTextWidth(out + "…", px) > maxW) out.pop_back();
            return out + "…";
        }
        /** Truncate from the FRONT — for paths, where the tail (the filename) is the
         *  part worth keeping: "…/Rivers 2023/export_river_02.jpg" (R5). */
        std::string fitFront(const std::string &s, double maxW, double px)
        {
            if (estimateTextWidth(s, px) <= maxW) return s;
            std::string out = s;
            while (!out.empty() && estimateTextWidth("…" + out, px) > maxW) out.erase(out.begin());
            return "…" + out;
        }
    }

    ExportDialog::ExportDialog(const Color &accent) : mAccent(accent) { focusable = true; }

    // ── open / close ───────────────────────────────────────────────────────────

    void ExportDialog::show(std::vector<Node> nodes, const std::vector<int> &preselect)
    {
        mNodes = std::move(nodes);
        const int n = (int)mNodes.size();
        mKids.assign(n, {});
        mRoots.clear();
        mExpanded.assign(n, 1);      // groups start expanded: the tree IS the picker
        mLeafChecked.assign(n, 0);
        for (int i = 0; i < n; ++i)
        {
            const int p = mNodes[i].parent;
            if (p >= 0 && p < n) mKids[p].push_back(i);
            else mRoots.push_back(i);
        }
        // Seed the ticks: the caller's selection, or every image when it is empty.
        for (int i = 0; i < n; ++i)
        {
            if (mNodes[i].group || mNodes[i].slot < 0) continue;
            mLeafChecked[i] = preselect.empty() ||
                              std::find(preselect.begin(), preselect.end(), mNodes[i].slot) != preselect.end();
        }
        rebuildRows();

        mTreeScroll.set(0.0); mTreeScrollTarget = 0.0;
        mBodyScroll.set(0.0); mBodyScrollTarget = 0.0;
        mFocusField = -1;
        mDraggingQuality = false;
        mExporting = false; mDone = mTotal = 0; mProgressName.clear();
        mPhase.set(0.0); mProgress.set(0.0);
        mComplete = false; mCompleteAmt.set(0.0); mCompleteAtMs = 0.0;
        mFirePending = false;
        mExportRows.clear(); mExportSeq.clear(); mRowFade.clear(); mRowFadeLastMs = -1.0;
        mCardOffset = Point{0.0, 0.0};   // a fresh open is centred again (R-EXPORT-8)
        mDraggingCard = false; mSuppressClick = false;
        for (int i = 0; i < 3; ++i) mMetaKnob[i].set(mMeta[i] ? 1.0 : 0.0);
        // R-EXPORT-3: default the explicit destination to the first selected image's
        // folder, so unticking "Same as source" lands somewhere sensible immediately.
        if (mDestination.empty()) mDestination = firstCheckedFolder();

        mOpen = true; mClosing = false;
        mAppear.animateTo(1.0, 160.0, Easing::EaseOutCubic, mLastMs);
        // Take keyboard focus on open: dispatchKey() only reaches the FOCUSED segment,
        // so without this Escape (and, once a field is clicked, typing) never arrives.
        requestFocus();
        raise();
    }

    void ExportDialog::beginClose()
    {
        mClosing = true;
        mFocusField = -1;
        mAppear.animateTo(0.0, 120.0, Easing::EaseOutCubic, mLastMs);
    }

    void ExportDialog::setDestination(const std::string &dir)
    {
        if (dir.empty()) return;
        mDestination = dir;
        mSameAsSource = false;   // the photographer just picked a folder; honour it
    }

    void ExportDialog::setExportProgress(int done, int total, const std::string &name)
    {
        mTotal = std::max(0, total);
        mDone = std::max(0, std::min(done, mTotal));
        mProgressName = name;
        mProgress.animateTo(mTotal > 0 ? (double)mDone / (double)mTotal : 0.0, 180.0,
                            Easing::EaseOutCubic, mLastMs);
        if (mTotal > 0 && mDone >= mTotal) beginComplete();   // beat 3, then auto-close
    }

    void ExportDialog::cancelExport()
    {
        if (!mExporting) return;
        mExporting = false;
        mComplete = false;
        mFirePending = false;
        mCompleteAmt.set(0.0);
        mPhase.animateTo(0.0, 200.0, Easing::EaseInOutCubic, mLastMs);
    }

    // ── R-EXPORT-6 beat 1: collapse the form into the progress card ──
    // Nothing is exported yet. The request is snapshotted here and handed to the host
    // from advance() once the collapse has finished, so the animation plays against an
    // idle main loop instead of competing with a full-resolution render.
    void ExportDialog::beginExport()
    {
        mPendingRequest = buildRequest();
        mFirePending = true;
        mExporting = true;
        mComplete = false;
        mDone = 0;
        mTotal = (int)checkedSlots().size();
        mProgressName.clear();
        mProgress.set(0.0);
        mCompleteAmt.set(0.0);
        mFocusField = -1;
        mDraggingQuality = false;
        mHover.clear();
        buildExportRows();
        // The manifest is a different list from the picker's, so start it at the top
        // rather than inheriting the picker's scroll.
        mTreeScroll.set(0.0); mTreeScrollTarget = 0.0;
        mPhase.animateTo(1.0, 260.0, Easing::EaseInOutCubic, mLastMs);
    }

    // ── R-EXPORT-6 beat 3: the green tick, then dismiss ──
    void ExportDialog::beginComplete()
    {
        if (mComplete) return;
        mComplete = true;
        mCompleteAtMs = mLastMs;
        mCompleteAmt.animateTo(1.0, 260.0, Easing::EaseInOutCubic, mLastMs);
    }

    void ExportDialog::buildExportRows()
    {
        // Walk the FULL tree in pre-order and keep a node only if it is a selected image
        // or an ancestor of one -- the manifest then reads as exactly what is being
        // written, with the group structure that gives each file its context.
        mExportRows.clear();
        mExportSeq.clear();
        int seq = 0;
        std::function<void(int, int)> walk = [&](int node, int depth) {
            if (mNodes[node].group)
            {
                std::vector<int> leaves;
                collectLeaves(node, leaves);
                bool any = false;
                for (int l : leaves) if (mLeafChecked[l]) { any = true; break; }
                if (!any) return;                       // nothing under here is being written
                mExportRows.push_back({node, depth});
                mExportSeq.push_back(-1);               // a group is not itself a file
                for (int k : mKids[node]) walk(k, depth + 1);
                return;
            }
            if (mNodes[node].slot < 0 || !mLeafChecked[node]) return;
            mExportRows.push_back({node, depth});
            mExportSeq.push_back(seq++);                // matches checkedSlots() order
        };
        for (int r : mRoots) walk(r, 0);
        mRowFade.assign(mExportRows.size(), 0.0);
        mRowFadeLastMs = -1.0;
    }

    void ExportDialog::advanceRowFades(double nowMs)
    {
        if (mRowFade.size() != mExportRows.size()) mRowFade.assign(mExportRows.size(), 0.0);
        constexpr double kDurMs = 220.0;
        const bool rm = artboard::reducedMotion();
        const double dt = (mRowFadeLastMs < 0.0) ? 0.0 : (nowMs - mRowFadeLastMs);
        const double step = rm ? 1.0 : std::min(1.0, std::max(0.0, dt / kDurMs));
        mRowFadeLastMs = nowMs;
        for (size_t i = 0; i < mRowFade.size(); ++i)
        {
            // A file row is "written" once the batch has passed it; a group row lights up
            // only when every one of its own members has been written.
            bool done = false;
            if (mExportSeq[i] >= 0) done = mExportSeq[i] < mDone;
            else
            {
                done = true;
                for (size_t j = i + 1; j < mExportRows.size(); ++j)
                {
                    if (mExportRows[j].depth <= mExportRows[i].depth) break;   // left the subtree
                    if (mExportSeq[j] >= 0 && mExportSeq[j] >= mDone) { done = false; break; }
                }
            }
            const double tgt = done ? 1.0 : 0.0;
            if (mRowFade[i] < tgt) mRowFade[i] = std::min(tgt, mRowFade[i] + step);
            else if (mRowFade[i] > tgt) mRowFade[i] = std::max(tgt, mRowFade[i] - step);
        }
    }

    void ExportDialog::followActiveRow()
    {
        // Keep the file currently being written inside the manifest's viewport, so a
        // long batch doesn't leave the user staring at rows that finished minutes ago.
        const Rect box = progressTreeRect();
        const double view = box.h - 2.0;
        const double contentH = mExportRows.size() * kTreeRowH;
        if (view <= 0.0 || contentH <= view) return;
        int active = -1;
        for (size_t i = 0; i < mExportSeq.size(); ++i)
            if (mExportSeq[i] == mDone) { active = (int)i; break; }
        if (active < 0) active = (int)mExportRows.size() - 1;
        const double top = active * kTreeRowH, bottom = top + kTreeRowH;
        double target = mTreeScrollTarget;
        if (top < target) target = top;
        else if (bottom > target + view) target = bottom - view;
        target = std::min(contentH - view, std::max(0.0, target));
        if (std::fabs(target - mTreeScrollTarget) > 0.5)
        {
            mTreeScrollTarget = target;
            mTreeScroll.animateTo(target, 220.0, Easing::EaseOutCubic, mLastMs);
        }
    }

    double ExportDialog::rowDoneAmount(int i) const
    {
        if (i < 0 || i >= (int)mRowFade.size()) return 0.0;
        const double t = mRowFade[i];
        return t * t * (3.0 - 2.0 * t);   // smoothstep, matching HoverFade's ease
    }

    void ExportDialog::advance(double nowMs)
    {
        mLastMs = nowMs;
        mAppear.update(nowMs);
        mPhase.update(nowMs);
        mProgress.update(nowMs);
        mCompleteAmt.update(nowMs);
        mTreeScroll.update(nowMs);
        mBodyScroll.update(nowMs);
        for (auto &k : mMetaKnob) k.update(nowMs);

        if (mExporting)
        {
            // Beat 1 finished -> ONLY NOW does the host start writing (R-EXPORT-6).
            if (mFirePending && !mPhase.isAnimating())
            {
                mFirePending = false;
                if (onExport) onExport(mPendingRequest);
            }
            advanceRowFades(nowMs);
            if (!mComplete) followActiveRow();
            // Beat 3 holds on the tick just long enough to read, then dismisses itself.
            else if (!mClosing && nowMs - mCompleteAtMs >= kCompleteHoldMs) beginClose();
        }

        if (mClosing && !mAppear.isAnimating())
        {
            mOpen = false; mClosing = false; mExporting = false; mComplete = false;
        }
        if (!isOpen()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    // ── tree model (R-EXPORT-2) ────────────────────────────────────────────────

    void ExportDialog::rebuildRows()
    {
        mRows.clear();
        // Iterative pre-order walk (a project's tree is arbitrary depth; recursion
        // here would be fine but the explicit stack keeps the visible order obvious).
        struct Frame { int node; int depth; };
        std::vector<Frame> stack;
        for (auto it = mRoots.rbegin(); it != mRoots.rend(); ++it) stack.push_back({*it, 0});
        while (!stack.empty())
        {
            const Frame f = stack.back();
            stack.pop_back();
            mRows.push_back({f.node, f.depth});
            if (mNodes[f.node].group && mExpanded[f.node])
                for (auto it = mKids[f.node].rbegin(); it != mKids[f.node].rend(); ++it)
                    stack.push_back({*it, f.depth + 1});
        }
    }

    void ExportDialog::collectLeaves(int node, std::vector<int> &out) const
    {
        if (node < 0 || node >= (int)mNodes.size()) return;
        if (!mNodes[node].group) { if (mNodes[node].slot >= 0) out.push_back(node); return; }
        for (int k : mKids[node]) collectLeaves(k, out);
    }

    int ExportDialog::checkState(int node) const
    {
        // A group's state is DERIVED from its descendant leaves — never stored — so
        // the four propagation rules hold by construction (see the header comment).
        std::vector<int> leaves;
        collectLeaves(node, leaves);
        if (leaves.empty()) return 0;              // an empty group has nothing to export
        int on = 0;
        for (int l : leaves) on += mLeafChecked[l] ? 1 : 0;
        if (on == 0) return 0;
        return on == (int)leaves.size() ? 1 : 2;
    }

    void ExportDialog::setSubtreeChecked(int node, bool on)
    {
        std::vector<int> leaves;
        collectLeaves(node, leaves);
        for (int l : leaves) mLeafChecked[l] = on ? 1 : 0;
    }

    bool ExportDialog::rowIsGroup(int row) const
    {
        return row >= 0 && row < (int)mRows.size() && mNodes[mRows[row].node].group;
    }

    int ExportDialog::rowState(int row) const
    {
        if (row < 0 || row >= (int)mRows.size()) return 0;
        const int node = mRows[row].node;
        return mNodes[node].group ? checkState(node) : (mLeafChecked[node] ? 1 : 0);
    }

    void ExportDialog::toggleRow(int row)
    {
        if (row < 0 || row >= (int)mRows.size()) return;
        const int node = mRows[row].node;
        // A group: "all ticked" -> untick the whole subtree, anything else (none or
        // partial) -> tick the whole subtree. An image: plain toggle. Group states are
        // DERIVED, so the parent follows automatically in both directions.
        if (mNodes[node].group) setSubtreeChecked(node, checkState(node) != 1);
        else if (mNodes[node].slot >= 0) mLeafChecked[node] = mLeafChecked[node] ? 0 : 1;
    }

    void ExportDialog::toggleExpand(int row)
    {
        if (row < 0 || row >= (int)mRows.size()) return;
        const int node = mRows[row].node;
        if (!mNodes[node].group) return;
        mExpanded[node] = !mExpanded[node];
        rebuildRows();
    }

    void ExportDialog::setAllChecked(bool on)
    {
        for (int i = 0; i < (int)mNodes.size(); ++i)
            if (!mNodes[i].group && mNodes[i].slot >= 0) mLeafChecked[i] = on ? 1 : 0;
    }

    int ExportDialog::leafCount() const
    {
        int n = 0;
        for (int i = 0; i < (int)mNodes.size(); ++i)
            if (!mNodes[i].group && mNodes[i].slot >= 0) ++n;
        return n;
    }

    int ExportDialog::checkedCount() const
    {
        int n = 0;
        for (int i = 0; i < (int)mNodes.size(); ++i)
            if (!mNodes[i].group && mNodes[i].slot >= 0 && mLeafChecked[i]) ++n;
        return n;
    }

    std::vector<int> ExportDialog::checkedSlots() const
    {
        std::vector<int> slots;
        for (int i = 0; i < (int)mNodes.size(); ++i)  // mNodes is pre-order == tree order
            if (!mNodes[i].group && mNodes[i].slot >= 0 && mLeafChecked[i]) slots.push_back(mNodes[i].slot);
        return slots;
    }

    std::string ExportDialog::firstCheckedFolder() const
    {
        for (int i = 0; i < (int)mNodes.size(); ++i)
            if (!mNodes[i].group && mNodes[i].slot >= 0 && mLeafChecked[i] && !mNodes[i].sourcePath.empty())
                return folderOf(mNodes[i].sourcePath);
        return std::string();
    }

    std::string ExportDialog::previewFilename() const
    {
        std::string base = "image.jpg";
        for (int i = 0; i < (int)mNodes.size(); ++i)
            if (!mNodes[i].group && mNodes[i].slot >= 0 && mLeafChecked[i])
            { base = mNodes[i].name.empty() ? fileOf(mNodes[i].sourcePath) : mNodes[i].name; break; }
        // Strip the source extension and append the chosen format's.
        const auto dot = base.find_last_of('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        const std::string ext = mFormat == 0 ? ".jpg" : mFormat == 1 ? ".png" : ".tif";
        return (mUsePrefix ? mPrefix : std::string()) + base + ext;
    }

    // ── geometry ───────────────────────────────────────────────────────────────

    ExportDialog::Layout ExportDialog::layoutForm(double top) const
    {
        Layout L;
        const double ix = cardX() + kPad;
        const double iw = kCardW - 2.0 * kPad;
        double y = top;

        // ── Images to Export ──
        y += kSecH;
        L.masterBtn = Rect{ix, y, 118.0, kBtnH};
        y += kBtnH + kGapM;
        L.tree = Rect{ix, y, iw, kTreeH};
        y += kTreeH + 7.0;
        L.selInfo = Rect{ix, y, iw, 14.0};
        y += 14.0 + kGapL;

        // ── Destination ──
        y += kSecH;
        const double sameW = kCheck + kGapM + estimateTextWidth("Same as source", kFontPx);
        L.destBox = Rect{ix, y, iw - sameW - 12.0, kDestBoxH};
        L.destChange = Rect{L.destBox.x + L.destBox.w - 8.0 - 56.0, y + (kDestBoxH - 18.0) * 0.5, 56.0, 18.0};
        L.sameCheck = Rect{ix + iw - sameW, y + (kDestBoxH - kCheck) * 0.5 - 2.0, sameW, kCheck + 4.0};
        y += kDestBoxH + kGapS;
        L.pathPreview = Rect{ix, y, iw, 14.0};
        y += 14.0 + 10.0;

        const double fieldX = ix + kCheck + 10.0 + kLabelColW + 10.0;
        const double fieldW = ix + iw - fieldX;
        L.prefixCheck = Rect{ix, y + (kFieldH - kCheck) * 0.5, kCheck, kCheck};
        L.prefixField = Rect{fieldX, y, fieldW, kFieldH};
        y += kFieldH + kGapS;
        L.subCheck = Rect{ix, y + (kFieldH - kCheck) * 0.5, kCheck, kCheck};
        L.subField = Rect{fieldX, y, fieldW, kFieldH};
        y += kFieldH + kGapL;

        // ── Format ──
        y += kSecH;
        const double fw = (iw - 2.0 * kGapS) / 3.0;
        for (int i = 0; i < 3; ++i) L.fmtChip[i] = Rect{ix + i * (fw + kGapS), y, fw, kBtnH};
        y += kBtnH + kGapS;
        const double sw = (iw - 3.0 * kGapS) / 4.0;
        for (int i = 0; i < 4; ++i) L.sizeChip[i] = Rect{ix + i * (sw + kGapS), y, sw, kBtnH};
        y += kBtnH + 10.0;

        // ── Quality: label row, then the slider on its OWN row (R-EXPORT-4) ──
        if (mFormat == 0)  // JPEG only; PNG/TIFF have no quality knob
        {
            L.qualityLabel = Rect{ix, y, iw, 15.0};
            y += 15.0 + kGapS;
            L.qualitySlider = Rect{ix, y, iw, 18.0};
            y += 18.0 + kGapL;
        }

        // ── Metadata ──
        y += kSecH;
        for (int i = 0; i < 3; ++i)
            L.metaToggle[i] = Rect{ix + iw - kToggleW, y + i * kMetaRowH + (kMetaRowH - kToggleH) * 0.5,
                                   kToggleW, kToggleH};
        y += 3.0 * kMetaRowH + 4.0;

        L.contentH = y - top;
        return L;
    }

    double ExportDialog::formContentH() const { return layoutForm(0.0).contentH; }

    double ExportDialog::cardX() const
    {
        // R-EXPORT-8: centred, plus the drag offset, clamped so a good chunk of the
        // header stays grabbable no matter how far it was flung.
        constexpr double kKeepVisible = 120.0;
        const double x = (width.value() - kCardW) * 0.5 + mCardOffset.x;
        const double lo = std::min(4.0, width.value() - kKeepVisible);
        const double hi = std::max(lo, width.value() - kKeepVisible);
        return std::min(hi, std::max(lo - (kCardW - kKeepVisible), x));
    }

    double ExportDialog::progressTreeH() const
    {
        // The manifest sizes to its own rows (capped), which is what makes beat 1 a
        // real shrink rather than a swap of equally tall panels.
        const double rows = std::max(1.0, (double)mExportRows.size());
        return std::min((double)kTreeRows, rows) * kTreeRowH + 2.0;
    }

    Rect ExportDialog::headerRect() const
    {
        const Rect c = cardRect();
        return Rect{c.x, c.y, c.w, kHeaderH};
    }

    Rect ExportDialog::progressTreeRect() const
    {
        // The SAME box the form drew, tweened to the manifest's place and size: in the
        // form the tree sits below the master button, in the progress face it sits
        // directly under the section header, and it shrinks to the manifest's rows.
        // Interpolating one box (rather than cross-fading two) is what keeps beat 1
        // reading as the dialog shedding its controls instead of two lists overlapping.
        const double p = mPhase.value();
        const Rect b = bodyRect();
        const double y = b.y + kSecH + (kBtnH + kGapM) * (1.0 - p);
        const double h = kTreeH + (progressTreeH() - kTreeH) * p;
        return Rect{cardX() + kPad, y, kCardW - 2.0 * kPad, h};
    }

    Rect ExportDialog::progressBarRect() const
    {
        const Rect tr = progressTreeRect();
        return Rect{tr.x, tr.y + tr.h + 12.0, tr.w, kBarH};
    }

    Rect ExportDialog::cardRect() const
    {
        const double formBody = formContentH();
        const double avail = std::max(120.0, height.value() - 2.0 * kMargin - kHeaderH - kFooterH);
        // Three body heights, tweened through in order (R-EXPORT-6): the form, the
        // progress card (section header + manifest + bar + status line), then the
        // compact confirmation. Each is clamped to what the window can hold (R4:
        // derived from mH, never a baked-in card height).
        const double progressBody = kSecH + progressTreeH() + 12.0 + kBarH + 8.0 + 14.0 + 8.0;
        const double formH = (1.0 - mPhase.value()) * std::min(formBody, avail) +
                             mPhase.value() * std::min(progressBody, avail);
        const double bodyH = (1.0 - mCompleteAmt.value()) * formH +
                             mCompleteAmt.value() * std::min(kCompleteBodyH, avail);
        const double h = kHeaderH + bodyH + kFooterH;
        // The card rises 8px into place as it fades in (R-G-1). Folding the offset in
        // HERE (rather than shifting only the paint) keeps layout, hit-testing and
        // drawing on one rect, so a click can never land where the card isn't drawn.
        // R-EXPORT-8: the drag offset moves the card; y is clamped so the header band
        // (the drag handle) can never be pushed off the top or bottom of the window.
        const double y = (height.value() - h) * 0.5 + mCardOffset.y + (1.0 - mAppear.value()) * 8.0;
        const double loY = 4.0;
        const double hiY = std::max(loY, height.value() - kHeaderH - 4.0);
        return Rect{cardX(), std::min(hiY, std::max(loY, y)), kCardW, h};
    }

    Rect ExportDialog::bodyRect() const
    {
        const Rect c = cardRect();
        return Rect{c.x, c.y + kHeaderH, c.w, std::max(0.0, c.h - kHeaderH - kFooterH)};
    }

    double ExportDialog::bodyTop() const { return bodyRect().y - mBodyScroll.value(); }

    Rect ExportDialog::treeRect() const { return layoutForm(bodyTop()).tree; }

    Rect ExportDialog::closeRect() const
    {
        const Rect c = cardRect();
        return Rect{c.x + c.w - kPad - 18.0, c.y + (kHeaderH - 18.0) * 0.5, 18.0, 18.0};
    }
    Rect ExportDialog::exportRect() const
    {
        const Rect c = cardRect();
        const double w = std::max(120.0, estimateTextWidth("Export 00 photos", kFontPx) + 44.0);
        return Rect{c.x + c.w - kPad - w, c.y + c.h - kFooterH + (kFooterH - kBtnH) * 0.5, w, kBtnH};
    }
    Rect ExportDialog::cancelRect() const
    {
        const Rect e = exportRect();
        const double w = 72.0;
        return Rect{e.x - kGapM - w, e.y, w, kBtnH};
    }

    int ExportDialog::rowAt(const Point &p, bool &onChevron) const
    {
        onChevron = false;
        const Rect box = treeRect();
        if (!box.contains(p)) return -1;
        const double local = p.y - (box.y + 1.0) + mTreeScroll.value();
        const int i = (int)std::floor(local / kTreeRowH);
        if (i < 0 || i >= (int)mRows.size()) return -1;
        const double cx = box.x + 6.0 + mRows[i].depth * kIndent;
        if (mNodes[mRows[i].node].group && p.x >= cx && p.x <= cx + kChevW) onChevron = true;
        return i;
    }

    int ExportDialog::hitId(const Point &p) const
    {
        if (mExporting) return -1;                    // the progress state has no controls
        if (closeRect().contains(p)) return kIdClose;
        if (exportRect().contains(p)) return kIdExport;
        if (cancelRect().contains(p)) return kIdCancel;
        if (!bodyRect().contains(p)) return -1;       // below/above the viewport: nothing

        bool chev = false;
        const int row = rowAt(p, chev);
        if (row >= 0) return kIdTree + row;

        const Layout L = layoutForm(bodyTop());
        if (L.masterBtn.contains(p)) return kIdMaster;
        if (!mSameAsSource && L.destChange.contains(p)) return kIdChange;
        if (L.sameCheck.contains(p)) return kIdSame;
        if (L.prefixCheck.contains(p)) return kIdPrefixCheck;
        if (mUsePrefix && L.prefixField.contains(p)) return kIdPrefixField;
        if (L.subCheck.contains(p)) return kIdSubCheck;
        if (mUseSubfolder && L.subField.contains(p)) return kIdSubField;
        for (int i = 0; i < 3; ++i) if (L.fmtChip[i].contains(p)) return kIdFmt0 + i;
        for (int i = 0; i < 4; ++i) if (L.sizeChip[i].contains(p)) return kIdSize0 + i;
        if (mFormat == 0 && L.qualitySlider.contains(p)) return kIdQuality;
        for (int i = 0; i < 3; ++i) if (L.metaToggle[i].contains(p)) return kIdMeta0 + i;
        return -1;
    }

    // ── input ──────────────────────────────────────────────────────────────────

    void ExportDialog::scrollBy(double delta, double x, double y)
    {
        if (!isOpen()) return;
        const Point p{x, y};
        if (mExporting)
        {
            // Only the manifest scrolls in the progress face; there is no form behind it.
            if (mComplete) return;
            const Rect box = progressTreeRect();
            if (!box.contains(p)) return;
            const double maxScroll = std::max(0.0, mExportRows.size() * kTreeRowH - (box.h - 2.0));
            mTreeScrollTarget = std::min(maxScroll, std::max(0.0, mTreeScrollTarget - delta * 3.0 * kTreeRowH));
            mTreeScroll.animateTo(mTreeScrollTarget, 160.0, Easing::EaseOutCubic, mLastMs);
            return;
        }
        const Rect tree = treeRect();
        if (tree.contains(p))
        {
            const double maxScroll = std::max(0.0, mRows.size() * kTreeRowH - (tree.h - 2.0));
            mTreeScrollTarget = std::min(maxScroll, std::max(0.0, mTreeScrollTarget - delta * 3.0 * kTreeRowH));
            mTreeScroll.animateTo(mTreeScrollTarget, 160.0, Easing::EaseOutCubic, mLastMs);
            return;
        }
        const Rect body = bodyRect();
        const double maxBody = std::max(0.0, formContentH() - body.h);
        mBodyScrollTarget = std::min(maxBody, std::max(0.0, mBodyScrollTarget - delta * 48.0));
        mBodyScroll.animateTo(mBodyScrollTarget, 160.0, Easing::EaseOutCubic, mLastMs);
    }

    void ExportDialog::applyQualityAt(double localX)
    {
        const Rect s = layoutForm(bodyTop()).qualitySlider;
        if (s.w <= 0.0) return;
        const double t = clamp01((localX - s.x) / s.w);
        mQuality = 60 + (int)std::lround(t * 40.0);  // 60..100, matching the reference range
    }

    ExportDialog::Request ExportDialog::buildRequest() const
    {
        Request r;
        r.slots = checkedSlots();
        r.sameAsSource = mSameAsSource;
        r.destination = mDestination;
        r.usePrefix = mUsePrefix; r.prefix = mPrefix;
        r.useSubfolder = mUseSubfolder; r.subfolder = mSubfolder;
        r.format = kFormats[mFormat];
        r.quality = mQuality;
        r.longEdge = kSizeEdges[mSize];
        r.embedExif = mMeta[0]; r.stripGps = mMeta[1]; r.embedProfile = mMeta[2];
        return r;
    }

    bool ExportDialog::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mOpen || mClosing) return false;
        using T = Gesture::Type;

        // ── R-EXPORT-8: drag the card by its header, in every state ──
        // Handled before anything else so it works mid-export too, and so a drag that
        // began on the header keeps receiving Move even once the pointer leaves it.
        if (mDraggingCard)
        {
            if (g.type == T::Move || g.type == T::Drag || g.type == T::DragStart)
            {
                mCardOffset.x += local.x - mDragGrab.x;
                mCardOffset.y += local.y - mDragGrab.y;
                mDragGrab = local;
                mSuppressClick = true;    // this gesture ends in a Click; it must not act
                return true;
            }
            if (g.type == T::Up || g.type == T::Drop) { mDraggingCard = false; return true; }
            if (g.type == T::Click || g.type == T::DoubleClick)
            {
                mDraggingCard = false; mSuppressClick = false;
                return true;             // swallow the click the drag ended with
            }
        }
        if (g.type == T::Down && headerRect().contains(local) && !closeRect().contains(local))
        {
            mDraggingCard = true;
            mDragGrab = local;
            mSuppressClick = false;
            return true;
        }
        if (mSuppressClick && (g.type == T::Click || g.type == T::DoubleClick))
        {
            mSuppressClick = false;
            return true;                 // the click that terminated a drag
        }

        if (mExporting)
        {
            // Modal and non-cancellable while the batch is being written (R-EXPORT-6):
            // swallow everything else so nothing behind the card is touched.
            if (g.type == T::Move) mHover.clear();
            return true;
        }

        if (g.type == T::Move)
        {
            if (mDraggingQuality) { applyQualityAt(local.x); return true; }
            mHover.setHovered(hitId(local));
            return true;
        }
        if (g.type == T::Down)
        {
            if (mFormat == 0 && layoutForm(bodyTop()).qualitySlider.contains(local))
            { mDraggingQuality = true; applyQualityAt(local.x); }
            return true;
        }
        if (g.type == T::DragStart || g.type == T::Drag)
        {
            if (mDraggingQuality) { applyQualityAt(local.x); return true; }
            return true;
        }
        if (g.type == T::Up || g.type == T::Drop) { mDraggingQuality = false; return true; }
        if (g.type != T::Click && g.type != T::DoubleClick) return true;  // modal: swallow the rest

        mDraggingQuality = false;
        if (!cardRect().contains(local)) { beginClose(); return true; }   // click outside cancels
        if (closeRect().contains(local)) { beginClose(); return true; }
        if (cancelRect().contains(local)) { beginClose(); return true; }
        if (exportRect().contains(local))
        {
            if (checkedCount() == 0) return true;    // disabled: nothing selected
            beginExport();   // snapshots the request; fires onExport when beat 1 ends
            return true;
        }
        if (!bodyRect().contains(local)) return true;   // header/footer dead space

        // ── tree ──
        bool chev = false;
        const int row = rowAt(local, chev);
        if (row >= 0)
        {
            const int node = mRows[row].node;
            (void)node;
            if (chev) toggleExpand(row);
            else      toggleRow(row);   // whole-row click ticks/unticks (R-EXPORT-2)
            return true;
        }

        const Layout L = layoutForm(bodyTop());
        if (L.masterBtn.contains(local))
        {
            setAllChecked(!(checkedCount() == leafCount() && leafCount() > 0));
            return true;
        }
        if (L.sameCheck.contains(local))
        {
            mSameAsSource = !mSameAsSource;
            // R-EXPORT-3: switching to an explicit folder seeds it from the first
            // selected image, so the field is never blank.
            if (!mSameAsSource && mDestination.empty()) mDestination = firstCheckedFolder();
            return true;
        }
        if (!mSameAsSource && L.destChange.contains(local)) { if (onChooseDestination) onChooseDestination(); return true; }
        if (L.prefixCheck.contains(local)) { mUsePrefix = !mUsePrefix; if (!mUsePrefix && mFocusField == 0) mFocusField = -1; return true; }
        if (L.subCheck.contains(local)) { mUseSubfolder = !mUseSubfolder; if (!mUseSubfolder && mFocusField == 1) mFocusField = -1; return true; }
        if (mUsePrefix && L.prefixField.contains(local)) { mFocusField = 0; requestFocus(); return true; }
        if (mUseSubfolder && L.subField.contains(local)) { mFocusField = 1; requestFocus(); return true; }
        for (int i = 0; i < 3; ++i) if (L.fmtChip[i].contains(local)) { mFormat = i; return true; }
        for (int i = 0; i < 4; ++i) if (L.sizeChip[i].contains(local)) { mSize = i; return true; }
        for (int i = 0; i < 3; ++i)
            if (L.metaToggle[i].contains(local))
            {
                mMeta[i] = !mMeta[i];
                mMetaKnob[i].animateTo(mMeta[i] ? 1.0 : 0.0, 150.0, Easing::EaseOutCubic, mLastMs);
                return true;
            }
        mFocusField = -1;   // clicked inert body space: drop the text focus
        return true;
    }

    bool ExportDialog::handleKey(const KeyEvent &e)
    {
        if (!isOpen()) return false;
        if (e.type == KeyEvent::Type::Down && e.keyCode == 27)  // Escape
        {
            if (mExporting) return true;                        // never abandon a running batch
            if (mFocusField >= 0) { mFocusField = -1; return true; }
            beginClose();
            return true;
        }
        if (mExporting) return true;
        if (mFocusField < 0) return true;                       // modal: swallow keys with no field focused

        std::string &buf = mFocusField == 0 ? mPrefix : mSubfolder;
        if (e.type == KeyEvent::Type::Text && !e.text.empty()) { buf += e.text; return true; }
        if (e.type == KeyEvent::Type::Down)
        {
            if (e.keyCode == 8) { if (!buf.empty()) buf.pop_back(); return true; }   // Backspace
            if (e.keyCode == 13) { mFocusField = -1; return true; }                  // Enter commits the field
        }
        return true;
    }

    // ── paint ──────────────────────────────────────────────────────────────────

    void ExportDialog::onOverlay(IRenderTarget &t) const
    {
        const double a = mAppear.value();
        if (!mOpen || a <= 0.001) return;

        // Three faces, in order: the form, the progress card, the confirmation. Each
        // fades as the next comes up, and the card height tweens between them, so the
        // whole sequence is one continuous shrink (R-EXPORT-6).
        const double phase = mPhase.value();          // 0 = form, 1 = progress
        const double doneAmt = mCompleteAmt.value();  // 0 = writing, 1 = confirmation
        const double formA = (1.0 - phase) * (1.0 - doneAmt) * a;
        const double progA = phase * (1.0 - doneAmt) * a;
        const double doneA = doneAmt * a;

        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, 0.0,
                        Paint::filled(fade(Color{0, 0, 0, 0.6}, a)));

        const Rect c = cardRect();                     // already carries the fade-in rise
        drawRoundedRect(t, c, radius::control(),
                        Paint::filledStroked(fade(palette::card(), a), fade(palette::border(), a), 1.0));

        // ── header band ──
        const Rect head{c.x, c.y, c.w, kHeaderH};
        drawRoundedRect(t, head, 0.0, Paint::filled(fade(palette::muted(), a)));
        t.beginPath();
        t.moveTo(head.x, head.y + head.h); t.lineTo(head.x + head.w, head.y + head.h);
        t.setStroke(fade(palette::border(), a), 1.0); t.strokePath();
        icon::download(t, Rect{head.x + kPad, head.y + (kHeaderH - 13.0) * 0.5, 13.0, 13.0}, fade(mAccent, a), 1.3);
        t.setFill(fade(palette::foreground(), a));
        t.drawText("Export", head.x + kPad + 21.0, head.y + kHeaderH * 0.5 + 4.5, 13.0, font::sansSemiBold());

        const Rect close = closeRect();
        const double closeHover = mHover.amount(kIdClose) * a;
        if (closeHover > 0.001)
            drawRoundedRect(t, close, radius::control(), Paint::filled(palette::hoverWash(closeHover)));
        icon::close(t, Rect{close.x + 4.0, close.y + 4.0, 10.0, 10.0},
                    fade(lerpColor(palette::mutedForeground(), palette::foreground(), mHover.amount(kIdClose)), a), 1.3);

        // ── body ── (clipped viewport; the form and the progress panel cross-fade)
        const Rect body = bodyRect();
        t.save();
        t.clipRect(body.x, body.y, body.w, body.h);
        if (formA > 0.004) drawForm(t, formA);
        if (progA > 0.004) drawProgress(t, progA);
        if (doneA > 0.004) drawComplete(t, doneA, body);

        // "More below" affordance: at a short window the body scrolls, and without a
        // hint the cut-off content reads as the end of the dialog. A short fade to the
        // card colour at each overflowing edge says otherwise (and the wheel scrolls it).
        if (formA > 0.004 && !mExporting)
        {
            const double maxScroll = std::max(0.0, formContentH() - body.h);
            const double sc = mBodyScroll.value();
            constexpr double kFadeH = 22.0;
            const Color solid = fade(palette::card(), formA);
            const Color clear = Color{solid.r, solid.g, solid.b, 0.0};
            if (sc < maxScroll - 0.5)
            {
                t.setLinearFill(0, body.y + body.h - kFadeH, 0, body.y + body.h, clear, solid);
                t.beginPath();
                t.moveTo(body.x, body.y + body.h - kFadeH); t.lineTo(body.x + body.w, body.y + body.h - kFadeH);
                t.lineTo(body.x + body.w, body.y + body.h); t.lineTo(body.x, body.y + body.h);
                t.closePath(); t.fillPath();
            }
            if (sc > 0.5)
            {
                t.setLinearFill(0, body.y, 0, body.y + kFadeH, solid, clear);
                t.beginPath();
                t.moveTo(body.x, body.y); t.lineTo(body.x + body.w, body.y);
                t.lineTo(body.x + body.w, body.y + kFadeH); t.lineTo(body.x, body.y + kFadeH);
                t.closePath(); t.fillPath();
            }
        }
        t.restore();

        // ── footer ──
        const Rect foot{c.x, c.y + c.h - kFooterH, c.w, kFooterH};
        drawRoundedRect(t, foot, 0.0, Paint::filled(fade(palette::muted(), a)));
        t.beginPath();
        t.moveTo(foot.x, foot.y); t.lineTo(foot.x + foot.w, foot.y);
        t.setStroke(fade(palette::border(), a), 1.0); t.strokePath();
        drawFooter(t, a, foot);
    }

    // ── shared paint primitives ────────────────────────────────────────────────

    void ExportDialog::drawSectionHeader(IRenderTarget &t, double x, double y, double w,
                                         const std::string &label, double a) const
    {
        // The one section-header treatment used across every panel in the app: a
        // tracked-out small caps label with a hairline running to the right edge.
        t.setFill(fade(palette::mutedForeground(), a));
        t.drawText(label, x, y + 13.0, kSmallPx, font::sansMedium());
        const double lx = x + estimateTextWidth(label, kSmallPx) + 8.0;
        t.beginPath();
        t.moveTo(lx, y + 9.0); t.lineTo(x + w, y + 9.0);
        t.setStroke(fade(palette::border(), a), 1.0); t.strokePath();
    }

    void ExportDialog::drawCheck(IRenderTarget &t, const Rect &box, int state, double a,
                                 double hoverAmt, bool dimmed) const
    {
        const Color accent = dimmed ? palette::secondary() : mAccent;
        const bool filled = state != 0;
        Color border = palette::border();
        if (hoverAmt > 0.001 && !filled)
            border = lerpColor(border, palette::primaryAlpha(0.6), hoverAmt);
        drawRoundedRect(t, box, 3.0,
                        filled ? Paint::filled(fade(accent, a))
                               : Paint::filledStroked(fade(palette::secondary(), a), fade(border, a), 1.0));
        if (state == 1)  // tick
        {
            t.beginPath();
            t.moveTo(box.x + 3.5, box.y + box.h * 0.55);
            t.lineTo(box.x + box.w * 0.42, box.y + box.h - 4.0);
            t.lineTo(box.x + box.w - 3.0, box.y + 4.0);
            t.setStroke(fade(palette::primaryForeground(), a), 1.8);
            t.strokePath();
        }
        else if (state == 2)  // indeterminate: a dash, "some children selected"
        {
            t.beginPath();
            t.moveTo(box.x + 3.5, box.y + box.h * 0.5);
            t.lineTo(box.x + box.w - 3.5, box.y + box.h * 0.5);
            t.setStroke(fade(palette::primaryForeground(), a), 1.8);
            t.strokePath();
        }
    }

    void ExportDialog::drawChip(IRenderTarget &t, const Rect &r, const std::string &label,
                                bool selected, double a, double hoverAmt, double fontPx) const
    {
        Color border = selected ? mAccent : palette::border();
        if (!selected && hoverAmt > 0.001) border = lerpColor(border, palette::primaryAlpha(0.4), hoverAmt);
        drawRoundedRect(t, r, radius::control(),
                        selected ? Paint::filledStroked(fade(palette::primaryAlpha(0.1), a), fade(border, a), 1.0)
                                 : Paint::filledStroked(fade(Color{0, 0, 0, 0.0}, a), fade(border, a), 1.0));
        if (!selected && hoverAmt > 0.001)
            drawRoundedRect(t, r, radius::control(), Paint::filled(palette::hoverWash(hoverAmt * a)));
        const Color label_c = selected ? mAccent
                                       : lerpColor(palette::mutedForeground(), palette::foreground(), hoverAmt);
        t.setFill(fade(label_c, a));
        const std::string fitted = fitEnd(label, r.w - 10.0, fontPx);
        t.drawText(fitted, r.x + (r.w - estimateTextWidth(fitted, fontPx)) * 0.5,
                   r.y + r.h * 0.5 + fontPx * 0.36, fontPx, font::sansMedium());
    }

    void ExportDialog::drawField(IRenderTarget &t, const Rect &r, const std::string &text,
                                 const std::string &placeholder, bool active, bool focused, double a) const
    {
        // Disabled (its checkbox is off) reads as a muted outline with muted text --
        // the control is visibly present but plainly not in play (draw every state).
        const Color border = focused ? palette::primaryAlpha(0.6)
                                     : (active ? palette::border() : fade(palette::border(), 0.5));
        drawRoundedRect(t, r, radius::control(),
                        Paint::filledStroked(fade(palette::background(), a), fade(border, a), 1.0));
        const bool empty = text.empty();
        const std::string shown = empty ? placeholder : text;
        const Color c = !active ? palette::whiteAlpha(0.18)
                                : (empty ? palette::whiteAlpha(0.25) : palette::foreground());
        t.setFill(fade(c, a));
        const double inner = r.w - 16.0;
        const std::string fitted = fitEnd(shown, focused ? inner - 6.0 : inner, kFontPx);
        t.drawText(fitted, r.x + 8.0, r.y + r.h * 0.5 + 4.0, kFontPx, font::mono());
        if (focused)  // caret: a plain bar after the text (no blink -- nothing to animate against)
        {
            const double cx = r.x + 8.0 + estimateTextWidth(fitted, kFontPx) + 1.5;
            t.beginPath();
            t.moveTo(cx, r.y + 6.0); t.lineTo(cx, r.y + r.h - 6.0);
            t.setStroke(fade(mAccent, a), 1.2); t.strokePath();
        }
    }

    // ── the tree (R-EXPORT-2) ──────────────────────────────────────────────────

    void ExportDialog::drawTree(IRenderTarget &t, double a, const Rect &box,
                                const std::vector<Row> &rows, double progress) const
    {
        drawRoundedRect(t, box, radius::control(),
                        Paint::filledStroked(fade(palette::background(), a), fade(palette::border(), a), 1.0));
        if (rows.empty())
        {
            // Empty state: an open project with no images still has to say so.
            const std::string msg = "No images in this project";
            t.setFill(fade(palette::whiteAlpha(0.22), a));
            t.drawText(msg, box.x + (box.w - estimateTextWidth(msg, kFontPx)) * 0.5,
                       box.y + box.h * 0.5 + 4.0, kFontPx, font::sans());
            return;
        }

        t.save();
        t.clipRect(box.x + 1.0, box.y + 1.0, box.w - 2.0, box.h - 2.0);
        const double scroll = mTreeScroll.value();
        // In the progress face the checkbox fades out and everything to its RIGHT slides
        // left into the freed space, so the picker becomes a manifest without a jump.
        // The indent and chevron keep their place: shifting those too would push a
        // depth-0 chevron out through the box's left edge.
        const double checkSlot = 13.0 + 7.0;
        for (int i = 0; i < (int)rows.size(); ++i)
        {
            const double ry = box.y + 1.0 + i * kTreeRowH - scroll;
            if (ry + kTreeRowH < box.y || ry > box.y + box.h) continue;   // offscreen
            const Row &row = rows[i];
            const Node &n = mNodes[row.node];
            const int state = n.group ? checkState(row.node) : (mLeafChecked[row.node] ? 1 : 0);

            const double hv = progress < 0.5 ? mHover.amount(kIdTree + i) * a * (1.0 - progress) : 0.0;
            if (hv > 0.001)
                drawRoundedRect(t, Rect{box.x + 1.0, ry, box.w - 2.0, kTreeRowH}, 0.0,
                                Paint::filled(palette::hoverWash(hv)));

            // R-EXPORT-6 beat 2: a written row's background lights up (and the row being
            // written right now carries the accent), eased in per row so nothing pops.
            double doneAmt = 0.0;
            if (progress > 0.004)
            {
                doneAmt = rowDoneAmount(i);
                const bool active = mExportSeq[i] >= 0 && mExportSeq[i] == mDone && !mComplete;
                const Rect rowRect{box.x + 1.0, ry, box.w - 2.0, kTreeRowH};
                if (doneAmt > 0.004)
                    drawRoundedRect(t, rowRect, 0.0, Paint::filled(palette::successAlpha(0.13 * doneAmt * a)));
                else if (active)
                    drawRoundedRect(t, rowRect, 0.0, Paint::filled(palette::primaryAlpha(0.14 * a * progress)));
            }

            double x = box.x + 6.0 + row.depth * kIndent;
            if (n.group)   // chevron: its own hit slot, toggles expand/collapse
            {
                const Rect chev{x, ry + (kTreeRowH - 10.0) * 0.5, 10.0, 10.0};
                const Color cc = fade(palette::mutedForeground(), a);
                const bool open = progress > 0.004 ? true : (bool)mExpanded[row.node];
                if (open) icon::chevronDown(t, chev, cc);
                else      icon::chevronRight(t, chev, cc);
            }
            x += kChevW + 2.0;

            if (progress < 0.996)
                drawCheck(t, Rect{x, ry + (kTreeRowH - 13.0) * 0.5, 13.0, 13.0}, state, a * (1.0 - progress));
            x += checkSlot * (1.0 - progress);   // the slot closes up as the box fades out

            const Color iconC = fade(palette::whiteAlpha(state ? 0.42 : 0.22), a);
            if (n.group) icon::folder(t, Rect{x, ry + (kTreeRowH - 11.0) * 0.5, 11.0, 11.0}, iconC, 1.1);
            else         icon::image(t, Rect{x, ry + (kTreeRowH - 11.0) * 0.5, 11.0, 11.0}, iconC, 1.1);
            x += 11.0 + 7.0;

            // A group also reports how many images sit under it, right-aligned so the
            // counts line up down the tree instead of ragging after each name.
            std::string suffix;
            if (n.group)
            {
                std::vector<int> leaves;
                collectLeaves(row.node, leaves);
                suffix = std::to_string(leaves.size());
            }
            constexpr double kCountRight = 14.0;   // clears the scroll thumb at box.w - 4
            const double suffixW = suffix.empty() ? 0.0 : estimateTextWidth(suffix, kSmallPx) + 10.0;
            const double nameW = box.x + box.w - kCountRight - suffixW - x;
            const Color nameC = lerpColor(state ? palette::foreground() : palette::mutedForeground(),
                                          palette::success(), doneAmt * 0.55);
            t.setFill(fade(nameC, a));
            t.drawText(fitEnd(n.name, nameW, kFontPx), x, ry + kTreeRowH * 0.5 + 4.0, kFontPx,
                       n.group ? font::sansMedium() : font::sans());
            if (!suffix.empty())
            {
                t.setFill(fade(palette::whiteAlpha(0.25), a));
                t.drawText(suffix, box.x + box.w - kCountRight - estimateTextWidth(suffix, kSmallPx),
                           ry + kTreeRowH * 0.5 + 3.5, kSmallPx, font::mono());
            }
            if (doneAmt > 0.004)   // the written tick, right-aligned in the same column
                icon::check(t, Rect{box.x + box.w - kCountRight - 9.0, ry + (kTreeRowH - 9.0) * 0.5, 9.0, 9.0},
                            palette::successAlpha(0.95 * doneAmt * a), 1.5);
        }
        t.restore();

        // Scroll thumb, only while the content actually overflows.
        const double contentH = rows.size() * kTreeRowH;
        if (contentH > box.h - 2.0)
        {
            const double frac = (box.h - 2.0) / contentH;
            const double th = std::max(24.0, (box.h - 2.0) * frac);
            const double maxScroll = contentH - (box.h - 2.0);
            const double ty = box.y + (box.h - th) * clamp01(scroll / maxScroll);
            drawRoundedRect(t, Rect{box.x + box.w - 4.0, ty, 3.0, th}, radius::pill(),
                            Paint::filled(fade(palette::whiteAlpha(0.18), a)));
        }
    }

    // ── the form ───────────────────────────────────────────────────────────────

    void ExportDialog::drawForm(IRenderTarget &t, double a) const
    {
        const Layout L = layoutForm(bodyTop());
        const double ix = cardX() + kPad, iw = kCardW - 2.0 * kPad;

        // Only the section header and the tree survive the collapse (R-EXPORT-6 beat 1);
        // everything below fades with `rest` while the card height tweens down.
        const int total = leafCount(), sel = checkedCount();
        const bool allOn = total > 0 && sel == total;
        // The master button is gone well before the tree slides up into its row, so the
        // two never occupy the same pixels.
        const double btnA = a * (1.0 - clamp01(mPhase.value() * 2.5));
        if (btnA > 0.004)
            drawChip(t, L.masterBtn, allOn ? "Select none" : "Select all (" + std::to_string(total) + ")",
                     allOn, btnA, mHover.amount(kIdMaster), kFontPx);
        if (!mExporting)   // once exporting, drawProgress owns the header + tree pair
        {
            drawSectionHeader(t, ix, L.masterBtn.y - kSecH, iw, "IMAGES TO EXPORT", a);
            drawTree(t, a, L.tree, mRows, 0.0);
        }

        {
            const std::string countTxt = std::to_string(sel);
            t.setFill(fade(palette::whiteAlpha(0.7), a));
            t.drawText(countTxt, L.selInfo.x, L.selInfo.y + 11.0, kSmallPx, font::monoMedium());
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText(" of " + std::to_string(total) + " photos selected",
                       L.selInfo.x + estimateTextWidth(countTxt, kSmallPx), L.selInfo.y + 11.0,
                       kSmallPx, font::sans());
        }

        // ── Destination ──
        drawSectionHeader(t, ix, L.destBox.y - kSecH, iw, "DESTINATION", a);
        const bool same = mSameAsSource;
        drawRoundedRect(t, L.destBox, radius::control(),
                        Paint::filledStroked(fade(palette::background(), a),
                                             fade(same ? fade(palette::border(), 0.5) : palette::border(), a), 1.0));
        icon::folder(t, Rect{L.destBox.x + 9.0, L.destBox.y + (L.destBox.h - 11.0) * 0.5, 11.0, 11.0},
                     fade(palette::whiteAlpha(same ? 0.2 : 0.4), a), 1.1);
        {
            // Ticked -> the field is an inert hint ("each image's own folder"); unticked
            // -> the chosen folder, with Change… live at its right (R-EXPORT-3).
            const std::string shown = same ? "Each image's own folder"
                                           : (mDestination.empty() ? "Choose a folder…" : mDestination);
            const double avail = L.destChange.x - (L.destBox.x + 26.0) - (same ? 8.0 : 10.0);
            t.setFill(fade(same ? palette::whiteAlpha(0.28) : palette::whiteAlpha(0.62), a));
            t.drawText(fitFront(shown, avail, kMonoPx), L.destBox.x + 26.0,
                       L.destBox.y + L.destBox.h * 0.5 + 3.5, kMonoPx, font::mono());
            if (!same)
            {
                const double hv = mHover.amount(kIdChange);
                t.setFill(fade(lerpColor(mAccent, palette::primaryAlpha(0.7), hv), a));
                t.drawText("Change…", L.destChange.x + L.destChange.w - estimateTextWidth("Change…", kSmallPx),
                           L.destChange.y + L.destChange.h * 0.5 + 3.5, kSmallPx, font::sansMedium());
            }
        }
        {
            const double hv = mHover.amount(kIdSame) * a;
            if (hv > 0.001)
                drawRoundedRect(t, Rect{L.sameCheck.x - 4.0, L.sameCheck.y - 2.0, L.sameCheck.w + 8.0, L.sameCheck.h + 4.0},
                                radius::control(), Paint::filled(palette::hoverWash(hv)));
            drawCheck(t, Rect{L.sameCheck.x, L.sameCheck.y + 2.0, kCheck, kCheck}, same ? 1 : 0, a,
                      mHover.amount(kIdSame));
            t.setFill(fade(same ? palette::foreground() : palette::mutedForeground(), a));
            t.drawText("Same as source", L.sameCheck.x + kCheck + kGapM,
                       L.sameCheck.y + L.sameCheck.h * 0.5 + 4.0, kFontPx, font::sans());
        }

        // Live preview of exactly where the first image will land.
        {
            const std::string dir = same ? (firstCheckedFolder().empty() ? std::string("<image folder>")
                                                                        : firstCheckedFolder())
                                         : (mDestination.empty() ? std::string("<no folder chosen>") : mDestination);
            std::string full = dir + "/";
            if (mUseSubfolder && !mSubfolder.empty()) full += mSubfolder + "/";
            full += previewFilename();
            t.setFill(fade(palette::whiteAlpha(0.32), a));
            t.drawText(fitFront(full, iw, kMonoPx), L.pathPreview.x, L.pathPreview.y + 11.0, kMonoPx, font::mono());
        }

        // ── prefix / subfolder ──
        auto modifierRow = [&](const Rect &check, const Rect &field, bool on, const std::string &label,
                               const std::string &value, const std::string &placeholder,
                               int checkId, int fieldIdx) {
            const double hv = mHover.amount(checkId) * a;
            if (hv > 0.001)
                drawRoundedRect(t, Rect{check.x - 4.0, check.y - 4.0, kLabelColW + kCheck + 18.0, kCheck + 8.0},
                                radius::control(), Paint::filled(palette::hoverWash(hv)));
            drawCheck(t, check, on ? 1 : 0, a, mHover.amount(checkId));
            t.setFill(fade(on ? palette::foreground() : palette::mutedForeground(), a));
            t.drawText(fitEnd(label, kLabelColW, kFontPx), check.x + kCheck + 10.0,
                       check.y + kCheck * 0.5 + 4.0, kFontPx, font::sans());
            drawField(t, field, value, placeholder, on, mFocusField == fieldIdx, a);
        };
        modifierRow(L.prefixCheck, L.prefixField, mUsePrefix, "Filename prefix", mPrefix, "e.g. export_",
                    kIdPrefixCheck, 0);
        modifierRow(L.subCheck, L.subField, mUseSubfolder, "Export to subfolder", mSubfolder, "subfolder name",
                    kIdSubCheck, 1);

        // ── Format / size ──
        drawSectionHeader(t, ix, L.fmtChip[0].y - kSecH, iw, "FORMAT", a);
        for (int i = 0; i < 3; ++i)
            drawChip(t, L.fmtChip[i], kFormats[i], mFormat == i, a, mHover.amount(kIdFmt0 + i), kFontPx);
        for (int i = 0; i < 4; ++i)
            drawChip(t, L.sizeChip[i], kSizes[i], mSize == i, a, mHover.amount(kIdSize0 + i), kSmallPx);

        // ── Quality: label row, then the slider on its own row (R-EXPORT-4) ──
        if (mFormat == 0)
        {
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText("Quality", L.qualityLabel.x, L.qualityLabel.y + 11.0, kFontPx, font::sans());
            const std::string val = std::to_string(mQuality) + "%";
            t.setFill(fade(palette::foreground(), a));
            t.drawText(val, L.qualityLabel.x + L.qualityLabel.w - estimateTextWidth(val, kFontPx),
                       L.qualityLabel.y + 11.0, kFontPx, font::monoMedium());

            const Rect s = L.qualitySlider;
            const double tv = (mQuality - 60) / 40.0;
            const double ty = s.y + s.h * 0.5;
            drawRoundedRect(t, Rect{s.x, ty - 2.0, s.w, 4.0}, radius::pill(), Paint::filled(fade(palette::secondary(), a)));
            drawRoundedRect(t, Rect{s.x, ty - 2.0, s.w * tv, 4.0}, radius::pill(), Paint::filled(fade(mAccent, a)));
            const double hv = mHover.amount(kIdQuality);
            const double tr = 4.5 + (mDraggingQuality ? 1.5 : hv * 1.0);   // grows under the pointer / while dragged
            drawRoundedRect(t, Rect{s.x + s.w * tv - tr, ty - tr, tr * 2.0, tr * 2.0}, radius::pill(),
                            Paint::filled(fade(palette::white(), a)));
        }

        // ── Metadata ──
        drawSectionHeader(t, ix, L.metaToggle[0].y - (kMetaRowH - kToggleH) * 0.5 - kSecH, iw, "METADATA", a);
        for (int i = 0; i < 3; ++i)
        {
            const Rect tg = L.metaToggle[i];
            const double rowY = tg.y + kToggleH * 0.5;
            const double hv = mHover.amount(kIdMeta0 + i) * a;
            if (hv > 0.001)
                drawRoundedRect(t, Rect{ix - 4.0, rowY - kMetaRowH * 0.5, iw + 8.0, kMetaRowH}, radius::control(),
                                Paint::filled(palette::hoverWash(hv)));
            t.setFill(fade(mMeta[i] ? palette::foreground() : palette::mutedForeground(), a));
            t.drawText(fitEnd(kMetaLabels[i], iw - kToggleW - 16.0, kFontPx), ix, rowY + 4.0, kFontPx, font::sans());
            const double k = mMetaKnob[i].value();   // eased 0..1, so the knob glides (R-G-1)
            drawRoundedRect(t, tg, radius::pill(),
                            Paint::filled(fade(lerpColor(palette::switchBackground(), mAccent, k), a)));
            const double kr = (kToggleH - 4.0) * 0.5;
            const double kx = tg.x + 2.0 + kr + (tg.w - 4.0 - kr * 2.0) * k;
            drawRoundedRect(t, Rect{kx - kr, tg.y + 2.0, kr * 2.0, kr * 2.0}, radius::pill(),
                            Paint::filled(fade(palette::white(), a)));
        }
    }

    // ── the progress face (R-EXPORT-6 beats 2 & 3) ─────────────────────────────

    void ExportDialog::drawProgress(IRenderTarget &t, double a) const
    {
        const double ix = cardX() + kPad, iw = kCardW - 2.0 * kPad;
        const Rect tree = progressTreeRect();
        const double phase = mPhase.value();

        // Beat 2 keeps the SAME two elements the form kept -- the section header and the
        // tree. They are NOT faded by the phase (the form already stopped drawing them),
        // so they simply survive the collapse; only the outer modal alpha applies, plus
        // the completion fade the caller has already folded into `a`.
        const double keepA = mAppear.value() * (1.0 - mCompleteAmt.value());
        drawSectionHeader(t, ix, tree.y - kSecH, iw, "IMAGES TO EXPORT", keepA);
        drawTree(t, keepA, tree, mExportRows, phase);

        // The determinate bar and its status line arrive only in the BACK HALF of the
        // collapse, once the form's own content has faded: shed first, then reveal, so
        // two different strings never share the same pixels mid-cross-fade.
        const double revealA = a * clamp01((phase - 0.55) / 0.45);
        if (revealA <= 0.004) return;
        const Rect bar = progressBarRect();
        drawRoundedRect(t, bar, radius::pill(), Paint::filled(fade(palette::secondary(), revealA)));
        drawRoundedRect(t, Rect{bar.x, bar.y, bar.w * clamp01(mProgress.value()), bar.h}, radius::pill(),
                        Paint::filled(fade(mAccent, revealA)));

        // ...above a status line naming the file in flight, with the count on the right.
        const double ty = bar.y + bar.h + 8.0 + 10.0;
        const std::string name = mProgressName.empty() ? std::string("Preparing…") : mProgressName;
        const std::string count = std::to_string(mDone) + " / " + std::to_string(mTotal);
        const double countW = estimateTextWidth(count, kSmallPx);
        t.setFill(fade(palette::whiteAlpha(0.42), revealA));
        t.drawText(fitFront(name, iw - countW - 12.0, kMonoPx), ix, ty, kMonoPx, font::mono());
        t.setFill(fade(palette::mutedForeground(), revealA));
        t.drawText(count, ix + iw - countW, ty, kSmallPx, font::mono());
    }

    void ExportDialog::drawComplete(IRenderTarget &t, double a, const Rect &body) const
    {
        // Beat 3: everything else has faded; all that is left is the confirmation.
        const double cx = body.x + body.w * 0.5;
        const double cy = body.y + body.h * 0.5;
        const double r = 15.0;
        icon::checkCircle(t, Rect{cx - r, cy - r - 8.0, r * 2.0, r * 2.0}, fade(palette::success(), a), 1.5);
        const std::string msg = "Exported " + std::to_string(mTotal) +
                                (mTotal == 1 ? " photo" : " photos");
        t.setFill(fade(palette::foreground(), a));
        t.drawText(msg, cx - estimateTextWidth(msg, 12.0) * 0.5, cy + r + 8.0, 12.0, font::sansMedium());
    }

    // ── the footer ─────────────────────────────────────────────────────────────

    void ExportDialog::drawFooter(IRenderTarget &t, double a, const Rect &foot) const
    {
        // Beat 3 fades the footer's content out with everything else; the band itself is
        // chrome and stays, so the card keeps its shape while it shrinks.
        const double a0 = a * (1.0 - mCompleteAmt.value());
        if (a0 <= 0.004) return;
        a = a0;
        const double phase = mPhase.value();
        const int sel = checkedCount();

        // Summary + buttons leave first, the "writing" line arrives after — they share
        // the footer's right-hand slot, so overlapping their fades would smear two
        // strings over each other.
        const double leaveA = a * (1.0 - clamp01(phase / 0.45));
        const double arriveA = a * clamp01((phase - 0.55) / 0.45);
        if (leaveA > 0.004)
        {
            std::string summary = std::to_string(sel) + (sel == 1 ? " photo · " : " photos · ") +
                                  kFormats[mFormat] + " · " + kSizes[mSize];
            if (mUsePrefix && !mPrefix.empty()) summary += " · prefix: " + mPrefix;
            t.setFill(fade(palette::mutedForeground(), leaveA));
            t.drawText(fitEnd(summary, cancelRect().x - foot.x - kPad - 12.0, kSmallPx),
                       foot.x + kPad, foot.y + foot.h * 0.5 + 3.5, kSmallPx, font::sans());
        }

        auto btn = [&](const Rect &r, const std::string &label, bool primary, double hoverAmt, bool disabled) {
            const double ba = leaveA;
            if (ba <= 0.004) return;
            Color fill = primary ? mAccent : palette::secondary();
            Color border = primary ? mAccent : palette::border();
            if (disabled) { fill = palette::secondary(); border = fade(palette::border(), 0.6); }
            drawRoundedRect(t, r, radius::control(), Paint::filledStroked(fade(fill, ba), fade(border, ba), 1.0));
            if (!disabled && hoverAmt > 0.001)
                drawRoundedRect(t, r, radius::control(), Paint::filled(palette::hoverWash(hoverAmt * ba)));
            const Color lc = disabled ? palette::whiteAlpha(0.22)
                                      : (primary ? palette::primaryForeground() : palette::foreground());
            double tx = r.x + (r.w - estimateTextWidth(label, kFontPx)) * 0.5;
            if (primary)   // the download glyph rides just left of the label
            {
                const double total = estimateTextWidth(label, kFontPx) + 15.0;
                tx = r.x + (r.w - total) * 0.5 + 15.0;
                icon::download(t, Rect{tx - 14.0, r.y + (r.h - 10.0) * 0.5, 10.0, 10.0}, fade(lc, ba), 1.2);
            }
            t.setFill(fade(lc, ba));
            t.drawText(label, tx, r.y + r.h * 0.5 + 4.0, kFontPx, font::sansMedium());
        };

        if (leaveA > 0.004)
        {
            btn(cancelRect(), "Cancel", false, mHover.amount(kIdCancel), false);
            const std::string exp = "Export " + std::to_string(sel) + (sel == 1 ? " photo" : " photos");
            btn(exportRect(), exp, true, mHover.amount(kIdExport), sel == 0);
        }
        if (arriveA > 0.004)
        {
            // Mid-batch the footer says so instead of offering buttons that would
            // abandon a half-written export.
            t.setFill(fade(palette::mutedForeground(), arriveA));
            const std::string msg = "Writing files — please wait";
            t.drawText(msg, foot.x + foot.w - kPad - estimateTextWidth(msg, kSmallPx),
                       foot.y + foot.h * 0.5 + 3.5, kSmallPx, font::sans());
        }
    }
}
}
