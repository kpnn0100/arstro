#include "EditSession.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

namespace arstro
{
namespace cosmo
{
    EditSession::EditSession()
    {
        mNodes.push_back(GNode{true, "All Photos", 0, -1, {}, {}, {}});  // root group (index 0)
    }

    EditSession::Thumb EditSession::makeThumb(const uint8_t *rgba, int w, int h, int maxEdge)
    {
        Thumb out;
        const int longEdge = w > h ? w : h;
        double scale = longEdge > maxEdge ? (double)maxEdge / longEdge : 1.0;
        out.w = (int)(w * scale + 0.5); if (out.w < 1) out.w = 1;
        out.h = (int)(h * scale + 0.5); if (out.h < 1) out.h = 1;
        out.rgba.resize((size_t)out.w * out.h * 4);
        for (int y = 0; y < out.h; ++y)
        {
            const int y0 = (int)((double)y * h / out.h);
            int y1 = (int)((double)(y + 1) * h / out.h); if (y1 <= y0) y1 = y0 + 1;
            for (int x = 0; x < out.w; ++x)
            {
                const int x0 = (int)((double)x * w / out.w);
                int x1 = (int)((double)(x + 1) * w / out.w); if (x1 <= x0) x1 = x0 + 1;
                long acc[4] = {0, 0, 0, 0}; int n = 0;
                for (int sy = y0; sy < y1; ++sy)
                    for (int sx = x0; sx < x1; ++sx)
                    {
                        const uint8_t *p = rgba + ((size_t)sy * w + sx) * 4;
                        acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; acc[3] += p[3]; ++n;
                    }
                uint8_t *d = out.rgba.data() + ((size_t)y * out.w + x) * 4;
                for (int c = 0; c < 4; ++c) d[c] = (uint8_t)(acc[c] / (n > 0 ? n : 1));
            }
        }
        return out;
    }

    const EditSession::Thumb *EditSession::thumbForSlot(int slot) const
    {
        return (slot >= 0 && slot < (int)mSlotThumbs.size()) ? &mSlotThumbs[slot] : nullptr;
    }

    // ---- group tree + selection ----

    int EditSession::nodeForSlot(int slot) const
    {
        for (int i = 0; i < (int)mNodes.size(); ++i)
            if (!mNodes[i].group && mNodes[i].slot == slot) return i;
        return -1;
    }

    std::vector<EditSession::Cell> EditSession::currentGroupCells() const
    {
        std::vector<Cell> cells;
        for (int n : mNodes[mCurGroup].kids)
        {
            const GNode &g = mNodes[n];
            Cell c;
            c.group = g.group; c.node = n; c.name = g.name;
            c.bypassed = g.bypass;  // R-BYPASS-5
            c.loading = g.pending;  // R-LOADUX-2
            if (g.group) c.count = (int)g.kids.size();
            else c.slot = g.slot;
            cells.push_back(c);
        }
        return cells;
    }

    bool EditSession::selectNodeById(int node, bool add, bool range)
    {
        if (node <= 0 || node >= (int)mNodes.size()) return false;   // 0 is root: never selectable
        const int parent = mNodes[node].parent;
        // A range extends from the anchor, and the anchor is a CELL in the current group —
        // so navigating first would move the anchor out from under it. Only navigate when
        // the node is somewhere else, which for a filmstrip click it never is.
        if (parent != mCurGroup) navigateToGroup(parent);
        const std::vector<int> &kids = mNodes[parent].kids;
        for (size_t i = 0; i < kids.size(); ++i)
            if (kids[i] == node) { selectNode((int)i, range, add); return true; }
        return false;   // a node whose parent does not list it would be a corrupt tree
    }

    std::vector<EditSession::TreeRow> EditSession::treeRows() const
    {
        // Depth-first in display order, so the flat list reads top to bottom exactly as the
        // filmstrip and a project dump present it, and a parent always precedes its kids.
        std::vector<TreeRow> rows;
        std::function<void(int, int, int)> walk = [&](int node, int parentId, int depth) {
            for (int k : mNodes[node].kids)
            {
                const GNode &g = mNodes[k];
                TreeRow r;
                r.node = k;
                r.parent = parentId;
                r.depth = depth;
                r.group = g.group;
                r.name = g.name;
                r.bypass = g.bypass;
                if (g.group) r.count = (int)g.kids.size();
                else
                {
                    r.slot = g.slot;
                    r.pending = g.pending;
                    // A leaf with no slot that is not pending never arrived (markImageFailed).
                    r.failed = g.slot < 0 && !g.pending;
                }
                rows.push_back(std::move(r));
                if (g.group) walk(k, k, depth + 1);
            }
        };
        walk(0, -1, 0);
        return rows;
    }

    std::vector<std::string> EditSession::breadcrumbPath() const
    {
        std::vector<int> chain;
        for (int n = mCurGroup; ; n = mNodes[n].parent) { chain.push_back(n); if (n == 0) break; }
        std::reverse(chain.begin(), chain.end());
        std::vector<std::string> names;
        for (int n : chain) names.push_back(mNodes[n].name);
        return names;
    }

    void EditSession::navigateToGroup(int node)
    {
        if (node < 0 || node >= (int)mNodes.size() || !mNodes[node].group) return;
        mCurGroup = node;
        mSel.clear(); mSelAnchor = -1;
    }

    void EditSession::selectNode(int cell, bool shift, bool ctrl)
    {
        const auto &kids = mNodes[mCurGroup].kids;
        if (cell < 0 || cell >= (int)kids.size()) return;
        const int node = kids[cell];
        if (shift && mSelAnchor >= 0 && mSelAnchor < (int)kids.size())
        {
            mSel.clear();
            const int lo = mSelAnchor < cell ? mSelAnchor : cell, hi = mSelAnchor < cell ? cell : mSelAnchor;
            for (int k = lo; k <= hi; ++k) mSel.push_back(kids[k]);
        }
        else if (ctrl)
        {
            auto it = std::find(mSel.begin(), mSel.end(), node);
            if (it != mSel.end()) { if (mSel.size() > 1) mSel.erase(it); }
            else mSel.push_back(node);
            mSelAnchor = cell;
        }
        else
        {
            mSel = {node};
            mSelAnchor = cell;
        }

        // setEditTarget: image -> its slot; group -> edit the group's own params, and
        // preview them live on a representative member (the group's first descendant image).
        if (mNodes[node].group)
        {
            mEditGroup = node;
            const int rep = firstImageSlotUnder(node);
            if (rep >= 0) { mCurrentSlot = rep; resetPreviewResolution(); }
            submit();  // re-render the representative member with the group's settings stacked
        }
        else
        {
            mEditGroup = -1;
            // R-LOADUX-2: a still-loading leaf has no slot yet. Move the selection (the
            // ring travels, the name updates) but DON'T blank the stage -- keep showing
            // whatever was there until this photo's pixels actually arrive, at which
            // point attachImage() points the editor at it.
            if (mNodes[node].slot >= 0)
            {
                mCurrentSlot = mNodes[node].slot;
                resetPreviewResolution();
                submit();
            }
        }
    }

    void EditSession::selectImage(int slot)
    {
        if (slot < 0 || slot >= (int)mSlotParams.size()) return;
        const int n = nodeForSlot(slot);
        if (n >= 0)
        {
            mCurGroup = mNodes[n].parent;
            const auto &kids = mNodes[mCurGroup].kids;
            for (int c = 0; c < (int)kids.size(); ++c)
                if (kids[c] == n) { selectNode(c, false, false); return; }
        }
        mCurrentSlot = slot;  // fallback (no node yet)
        submit();
    }

    void EditSession::createGroupFromSelection()
    {
        if (mSel.empty()) return;
        int gi = 1; for (const auto &n : mNodes) if (n.group) ++gi;
        GNode grp; grp.group = true; grp.name = "Group " + std::to_string(gi); grp.parent = mCurGroup;
        grp.history.init(grp.params);  // seed the group's own edit timeline (neutral root)
        const int gnode = (int)mNodes.size();
        mNodes.push_back(grp);
        auto &siblings = mNodes[mCurGroup].kids;
        std::vector<int> moved = mSel;
        siblings.erase(std::remove_if(siblings.begin(), siblings.end(),
                       [&](int k) { return std::find(moved.begin(), moved.end(), k) != moved.end(); }),
                       siblings.end());
        for (int k : moved) { mNodes[k].parent = gnode; mNodes[gnode].kids.push_back(k); }
        siblings.push_back(gnode);
        mSel = {gnode};
        mSelAnchor = -1;
        const auto &kids = mNodes[mCurGroup].kids;
        for (int c = 0; c < (int)kids.size(); ++c) if (kids[c] == gnode) { selectNode(c, false, false); break; }
    }

    void EditSession::ungroupSelected()
    {
        std::vector<int> sel = mSel;
        for (int n : sel)
        {
            if (n < 0 || n >= (int)mNodes.size() || !mNodes[n].group || n == 0) continue;
            auto &kids = mNodes[mCurGroup].kids;
            for (int child : mNodes[n].kids) { mNodes[child].parent = mCurGroup; kids.push_back(child); }
            mNodes[n].kids.clear();
            kids.erase(std::remove(kids.begin(), kids.end(), n), kids.end());
        }
        mSel.clear(); mSelAnchor = -1;
        submit();
    }

    void EditSession::renameGroup(int node, const std::string &name)
    {
        if (node > 0 && node < (int)mNodes.size() && mNodes[node].group && !name.empty())
            mNodes[node].name = name;
    }

    void EditSession::collectSubtree(int node, std::vector<int> &out) const
    {
        if (node < 0 || node >= (int)mNodes.size()) return;
        out.push_back(node);
        if (mNodes[node].group)
            for (int k : mNodes[node].kids) collectSubtree(k, out);
    }

    namespace
    {
        /** The display name of an image entry: its filename. D-23 — the `.cmp` format stores a
         *  `path=` and no name for an image, so every consumer that wanted a name got an empty
         *  string and rendered nothing (or, in the export dialog, "(missing image)"). */
        std::string fileNameOf(const std::string &path)
        {
            const auto slash = path.find_last_of("/\\");
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        void deleteNodeImpl(std::vector<EditSession::GNode> &nodes, arstro::RenderService &service, int node)
        {
            if (node <= 0 || node >= (int)nodes.size()) return;  // node 0 (root) can't be deleted
            std::vector<int> subtree;
            std::function<void(int)> collect = [&](int n) {
                if (n < 0 || n >= (int)nodes.size()) return;
                subtree.push_back(n);
                if (nodes[n].group) for (int k : nodes[n].kids) collect(k);
            };
            collect(node);
            for (int n : subtree)
                if (!nodes[n].group && nodes[n].slot >= 0)
                    service.releaseImage(nodes[n].slot);

            const int parent = nodes[node].parent;
            auto &kids = nodes[parent].kids;
            kids.erase(std::remove(kids.begin(), kids.end(), node), kids.end());
        }
    }

    void EditSession::deleteSelected()
    {
        if (mSel.empty()) return;

        const int editNode = mEditGroup >= 0 ? mEditGroup : (mCurrentSlot >= 0 ? nodeForSlot(mCurrentSlot) : -1);
        bool editTargetGone = false;
        if (editNode >= 0)
            for (int n : mSel)
            {
                std::vector<int> subtree;
                collectSubtree(n, subtree);
                if (std::find(subtree.begin(), subtree.end(), editNode) != subtree.end())
                { editTargetGone = true; break; }
            }

        int anchorCell = -1;
        if (editTargetGone)
        {
            const auto &kidsBefore = mNodes[mCurGroup].kids;
            for (int c = 0; c < (int)kidsBefore.size(); ++c)
                if (kidsBefore[c] == editNode) { anchorCell = c; break; }
        }

        const std::vector<int> victims = mSel;  // deleteNodeImpl mutates mNodes; copy first
        for (int n : victims) deleteNodeImpl(mNodes, mService, n);
        mSel.clear(); mSelAnchor = -1;

        if (editTargetGone)
        {
            mCurrentSlot = -1; mEditGroup = -1;
            const auto &kids = mNodes[mCurGroup].kids;
            if (anchorCell >= 0 && anchorCell < (int)kids.size())
                selectNode(anchorCell, false, false);  // the next image slid into the deleted slot
        }
        submit();
    }

    std::vector<int> EditSession::selectedImageSlots() const
    {
        std::vector<int> slots;
        std::function<void(int)> collect = [&](int node) {
            if (node < 0 || node >= (int)mNodes.size()) return;
            if (mNodes[node].group) { for (int k : mNodes[node].kids) collect(k); }
            else if (mNodes[node].slot >= 0) slots.push_back(mNodes[node].slot);
        };
        for (int n : mSel) collect(n);
        if (slots.empty() && mCurrentSlot >= 0) slots.push_back(mCurrentSlot);
        return slots;
    }

    // ---- filter bypass (R-BYPASS) ----

    bool EditSession::isBypassed(int node) const
    {
        return node >= 0 && node < (int)mNodes.size() && mNodes[node].bypass;
    }

    void EditSession::setBypassed(int node, bool on)
    {
        if (node < 0 || node >= (int)mNodes.size()) return;
        if (mNodes[node].bypass == on) return;
        mNodes[node].bypass = on;
        mDirty = true;               // a bypass toggle is an unsaved change (R-BYPASS-6)
        if (mCurrentSlot >= 0)       // re-render whatever is on screen through the new composition
            mService.render(mCurrentSlot, effectiveParams(mCurrentSlot));
    }

    void EditSession::toggleBypass(int node) { setBypassed(node, !isBypassed(node)); }

    int EditSession::editTargetNode() const
    {
        if (mEditGroup >= 0 && mEditGroup < (int)mNodes.size() && mNodes[mEditGroup].group) return mEditGroup;
        return mCurrentSlot >= 0 ? nodeForSlot(mCurrentSlot) : -1;
    }

    bool EditSession::editTargetBypassed() const { return isBypassed(editTargetNode()); }

    void EditSession::setSlotBypass(int slot, bool on)
    {
        const int n = nodeForSlot(slot);
        if (n >= 0) mNodes[n].bypass = on;
    }

    // ---- develop params ----

    EditParams *EditSession::curParams()
    {
        // While a group is the edit target, the develop panels edit the GROUP's own
        // params (which then stack onto its members); otherwise the current image's.
        if (mEditGroup >= 0 && mEditGroup < (int)mNodes.size() && mNodes[mEditGroup].group)
            return &mNodes[mEditGroup].params;
        return (mCurrentSlot >= 0 && mCurrentSlot < (int)mSlotParams.size()) ? &mSlotParams[mCurrentSlot] : nullptr;
    }

    EditParams EditSession::effectiveParams(int slot) const
    {
        if (slot < 0 || slot >= (int)mSlotParams.size()) return EditParams{};
        // The image's own params, with every ancestor group's params stacked on top,
        // folded child -> root (recursive: a nested group rides on its parent's).
        // R-BYPASS-2: a bypassed node contributes EditParams{} instead of its own
        // params -- the leaf's own edits and each ancestor group's offsets drop out
        // independently, so bypass composes down the whole chain.
        const int n = nodeForSlot(slot);
        EditParams e = (n >= 0 && mNodes[n].bypass) ? EditParams{} : mSlotParams[slot];
        if (n >= 0)
            for (int g = mNodes[n].parent; ; g = mNodes[g].parent)
            {
                if (!mNodes[g].bypass) e = composeParams(e, mNodes[g].params);
                if (g == 0) break;
            }
        return e;
    }

    EditParams EditSession::effectiveEditParams() const
    {
        if (mEditGroup > 0 && mEditGroup < (int)mNodes.size() && mNodes[mEditGroup].group)
        {
            // A group being edited: its own params stacked with its ancestor groups.
            // R-BYPASS-2: bypass is honoured on the ANCESTORS (a disabled group really
            // adds nothing to the green "stacked reach") but never on the edit target
            // itself -- the reach measures what sits ON TOP of the shown values, and
            // the target's own bypass is communicated by the dim scrim (R-BYPASS-4).
            EditParams e = mNodes[mEditGroup].params;
            for (int g = mNodes[mEditGroup].parent; ; g = mNodes[g].parent)
            {
                if (!mNodes[g].bypass) e = composeParams(e, mNodes[g].params);
                if (g == 0) break;
            }
            return e;
        }
        if (mEditGroup == 0) return mNodes[0].params;   // root group has no ancestors
        // An image: its own params (kept even when bypassed, see above) stacked with
        // its non-bypassed ancestor groups.
        if (mCurrentSlot < 0 || mCurrentSlot >= (int)mSlotParams.size()) return EditParams{};
        EditParams e = mSlotParams[mCurrentSlot];
        const int n = nodeForSlot(mCurrentSlot);
        if (n >= 0)
            for (int g = mNodes[n].parent; ; g = mNodes[g].parent)
            {
                if (!mNodes[g].bypass) e = composeParams(e, mNodes[g].params);
                if (g == 0) break;
            }
        return e;
    }

    void EditSession::applyParams(const EditParams &p)
    {
        if (auto *cur = curParams()) { *cur = p; submit(); }  // group's params or the slot's
    }

    void EditSession::applyParamsToSlot(int slot, const EditParams &p)
    {
        if (slot < 0 || slot >= (int)mSlotParams.size()) return;
        mSlotParams[slot] = p;
        mSlotHistory[slot].init(p);  // seed history with the LOADED state, not a blank one
    }

    void EditSession::applyParamsToSlot(int slot, const EditParams &p, const History &history)
    {
        if (slot < 0 || slot >= (int)mSlotParams.size()) return;
        mSlotParams[slot] = p;
        if (history.nodes.empty())
            mSlotHistory[slot].init(p);  // no saved tree (old project) -> single root
        else
            mSlotHistory[slot].restore(history.nodes, history.current, history.maxSteps, history.coalesceMs);
    }

    History *EditSession::editHistory()
    {
        // The timeline of whatever is being edited: the selected group, else the slot.
        if (mEditGroup >= 0 && mEditGroup < (int)mNodes.size() && mNodes[mEditGroup].group)
            return &mNodes[mEditGroup].history;
        return (mCurrentSlot >= 0 && mCurrentSlot < (int)mSlotHistory.size()) ? &mSlotHistory[mCurrentSlot] : nullptr;
    }

    int EditSession::firstImageSlotUnder(int node) const
    {
        if (node < 0 || node >= (int)mNodes.size()) return -1;
        if (!mNodes[node].group) return mNodes[node].slot;
        for (int k : mNodes[node].kids)
        {
            const int s = firstImageSlotUnder(k);
            if (s >= 0) return s;
        }
        return -1;
    }

    void EditSession::recordHistory()
    {
        if (mSuppressHistory) return;
        History *h = editHistory();
        EditParams *p = curParams();  // group params while editing a group, else the slot's
        if (!h || !p) return;
        if (h->empty()) h->init(*p);
        h->record(*p, mNowMs);
    }

    void EditSession::submit() { submitWith(RenderService::RenderIntent::Final); }

    void EditSession::submitInteractive() { submitWith(RenderService::RenderIntent::Interactive); }

    void EditSession::submitRefine(int level)
    {
        // A refinement is not an edit: no history, no dirty flag, same params — only the
        // level changes. Going through submit() would record a second history entry for
        // a value the user never touched again (R-PREVIEW-3).
        if (mCurrentSlot < 0) return;
        EditParams p = effectiveParams(mCurrentSlot);
        if (mCropPreviewMode)
        { p.cropX = 0; p.cropY = 0; p.cropW = 1; p.cropH = 1; }
        mService.render(mCurrentSlot, p, RenderService::RenderIntent::Final, level);
    }

    void EditSession::submitWith(RenderService::RenderIntent intent)
    {
        if (mCurrentSlot < 0) return;
        mDirty = true;   // an edit is being committed -> unsaved changes
        recordHistory();
        EditParams p = effectiveParams(mCurrentSlot);
        if (mCropPreviewMode)
        { p.cropX = 0; p.cropY = 0; p.cropW = 1; p.cropH = 1; }
        mService.render(mCurrentSlot, p, intent);
    }

    // ---- clipboard ----

    void EditSession::copyCurrent()
    {
        if (auto *p = curParams()) { mClipboard = *p; mHasClip = true; }
    }

    void EditSession::pasteTo(const std::vector<int> &slots)
    {
        if (!mHasClip) return;
        bool affectedCurrent = false;
        for (int i : slots)
            if (i >= 0 && i < (int)mSlotParams.size())
            {
                mSlotParams[i] = mClipboard;
                recordSlotEdit(i);
                if (i == mCurrentSlot) affectedCurrent = true;
            }
        if (affectedCurrent) submit();
    }

    // ---- history ----

    History *EditSession::currentHistory() { return editHistory(); }

    void EditSession::recordSlotEdit(int slot)
    {
        if (slot < 0 || slot >= (int)mSlotHistory.size()) return;
        History &h = mSlotHistory[slot];
        if (h.empty()) h.init(mSlotParams[slot]);
        h.breakCoalesce();
        h.record(mSlotParams[slot], mNowMs);
    }

    const EditParams *EditSession::applyHistoryParams(const EditParams *p)
    {
        if (!p) return nullptr;
        // Restore into whatever is being edited (the group's own params, or the slot).
        if (mEditGroup >= 0 && mEditGroup < (int)mNodes.size() && mNodes[mEditGroup].group)
            mNodes[mEditGroup].params = *p;
        else if (mCurrentSlot >= 0 && mCurrentSlot < (int)mSlotParams.size())
            mSlotParams[mCurrentSlot] = *p;
        else
            return nullptr;
        mSuppressHistory = true;
        submit();
        mSuppressHistory = false;
        return p;
    }

    const EditParams *EditSession::undo() { History *h = editHistory(); return h ? applyHistoryParams(h->undo()) : nullptr; }
    const EditParams *EditSession::redo() { History *h = editHistory(); return h ? applyHistoryParams(h->redo()) : nullptr; }
    const EditParams *EditSession::jumpToHistory(int node) { History *h = editHistory(); return h ? applyHistoryParams(h->jumpTo(node)) : nullptr; }

    bool EditSession::canUndo() const
    {
        if (mEditGroup >= 0 && mEditGroup < (int)mNodes.size() && mNodes[mEditGroup].group)
            return mNodes[mEditGroup].history.canUndo();
        return mCurrentSlot >= 0 && mCurrentSlot < (int)mSlotHistory.size() && mSlotHistory[mCurrentSlot].canUndo();
    }
    bool EditSession::canRedo() const
    {
        if (mEditGroup >= 0 && mEditGroup < (int)mNodes.size() && mNodes[mEditGroup].group)
            return mNodes[mEditGroup].history.canRedo();
        return mCurrentSlot >= 0 && mCurrentSlot < (int)mSlotHistory.size() && mSlotHistory[mCurrentSlot].canRedo();
    }

    void EditSession::setHistoryLimits(int maxSteps, double coalesceMs)
    {
        mHistorySteps = maxSteps > 2 ? maxSteps : 2;
        mHistoryCoalesceMs = coalesceMs < 0 ? 0 : coalesceMs;
        for (auto &h : mSlotHistory) h.setLimits(mHistorySteps, mHistoryCoalesceMs);
    }

    // ---- presets ----

    bool EditSession::savePreset(const std::string &name)
    {
        if (mPresetDir.empty() || name.empty() || mCurrentSlot < 0) return false;
        const std::filesystem::path full = std::filesystem::path(mPresetDir) / (name + ".apf");
        std::error_code ec;
        std::filesystem::create_directories(full.parent_path(), ec);
        const std::vector<std::string> cats = mPendingCategories.empty() ? apfImageCategories() : mPendingCategories;
        const apf::Document doc = editParamsToApf(mSlotParams[mCurrentSlot], cats, name);
        std::ofstream f(full);
        if (!f) return false;
        f << apf::serialize(doc);
        return true;
    }

    bool EditSession::exportPresetTo(const std::string &path)
    {
        if (mCurrentSlot < 0 || path.empty()) return false;
        const std::vector<std::string> cats = mPendingCategories.empty() ? apfImageCategories() : mPendingCategories;
        const std::string name = std::filesystem::path(path).stem().string();
        const apf::Document doc = editParamsToApf(mSlotParams[mCurrentSlot], cats, name);
        std::ofstream f(path);
        if (!f) return false;
        f << apf::serialize(doc);
        return true;
    }

    bool EditSession::importPresetFrom(const std::string &path, std::vector<std::string> &outPresentCategories)
    {
        std::ifstream f(path);
        if (!f) return false;
        std::stringstream ss; ss << f.rdbuf();
        apf::Document doc;
        if (!apf::parse(ss.str(), doc)) return false;
        if (!doc.engine.empty() && doc.engine != apfImageEngine()) return false;
        const std::vector<std::string> present = apfPresentImageCategories(doc);
        if (present.empty()) return false;
        mPendingApf = doc;
        outPresentCategories = present;
        return true;
    }

    void EditSession::applyImport(const std::vector<std::string> &categories)
    {
        bool affectedCurrent = false;
        for (int s : selectedImageSlots())
            if (s >= 0 && s < (int)mSlotParams.size())
            {
                if (applyApfToEditParams(mPendingApf, categories, mSlotParams[s]))
                {
                    recordSlotEdit(s);
                    if (s == mCurrentSlot) affectedCurrent = true;
                }
            }
        if (affectedCurrent) submit();
    }

    bool EditSession::applyPreset(const std::string &name)
    {
        if (mPresetDir.empty() || mCurrentSlot < 0) return false;
        std::ifstream f(mPresetDir + "/" + name + ".apf");
        if (!f) return false;
        std::stringstream ss; ss << f.rdbuf();
        apf::Document doc;
        if (!apf::parse(ss.str(), doc)) return false;
        if (!doc.engine.empty() && doc.engine != apfImageEngine()) return false;
        if (!applyApfToEditParams(doc, apfPresentImageCategories(doc), mSlotParams[mCurrentSlot])) return false;
        submit();
        return true;
    }

    bool EditSession::deletePresetFile(const std::string &name)
    {
        if (mPresetDir.empty()) return false;
        std::error_code ec;
        return std::filesystem::remove(std::filesystem::path(mPresetDir) / (name + ".apf"), ec);
    }

    // ---- session ----

    std::string EditSession::currentSourcePath() const
    {
        return (mCurrentSlot >= 0 && mCurrentSlot < (int)mSlotPaths.size()) ? mSlotPaths[mCurrentSlot] : std::string();
    }

    bool EditSession::saveSessionAs(const std::string &path)
    {
        if (mCurrentSlot < 0) return false;
        std::ofstream f(path);
        if (!f) return false;
        f << "image=" << mSlotPaths[mCurrentSlot] << "\n" << serializeParams(effectiveParams(mCurrentSlot));
        mSlotSessions[mCurrentSlot] = path;
        return true;
    }

    bool EditSession::readSessionFile(const std::string &path, std::string &imagePath, EditParams &params)
    {
        std::ifstream f(path);
        if (!f) return false;
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        imagePath.clear();
        std::stringstream ss(text);
        std::string line;
        while (std::getline(ss, line))
            if (line.rfind("image=", 0) == 0) { imagePath = line.substr(6); break; }
        deserializeParams(text, params);
        return true;
    }

    // ---- workspace ----

    bool EditSession::saveWorkspaceAs(const std::string &path)
    {
        std::ofstream f(path);
        if (!f) return false;
        f << "cosmoworkspace=1\n";
        int nextId = 0;
        // A group and an image serialize the same way: a header + full params + an
        // optional branching-history block (tree config + one #hnode per node, in
        // vector-index order so parent indices stay valid on load).
        auto writeParamsAndHistory = [&](const EditParams &p, const History &h) {
            f << serializeParams(p);
            if (!h.nodes.empty())
            {
                f << "hcurrent=" << h.current << "\nhmax=" << h.maxSteps
                  << "\nhcoalesce=" << h.coalesceMs << '\n';
                for (const auto &n : h.nodes)
                    f << "#hnode\nhparent=" << n.parent << "\nhseq=" << n.seq
                      << "\nhlabel=" << n.label << '\n' << serializeParams(n.params);
            }
        };
        std::function<void(int, int)> walk = [&](int node, int parentId) {
            for (int k : mNodes[node].kids)
            {
                const GNode &g = mNodes[k];
                const int myId = nextId++;
                if (g.group)
                {
                    f << "#group\nparent=" << parentId << "\nname=" << g.name << '\n';
                    if (g.bypass) f << "bypass=1\n";   // R-BYPASS-6 (absent = enabled)
                    writeParamsAndHistory(g.params, g.history);  // groups carry full settings now
                    walk(k, myId);
                }
                else if (g.slot >= 0 && g.slot < (int)mSlotPaths.size())
                {
                    f << "#image\nparent=" << parentId << "\npath=" << mSlotPaths[g.slot] << '\n';
                    if (g.bypass) f << "bypass=1\n";   // R-BYPASS-6
                    writeParamsAndHistory(mSlotParams[g.slot], mSlotHistory[g.slot]);
                }
            }
        };
        walk(0, -1);
        mWorkspacePath = path;
        mDirty = false;   // just persisted
        return true;
    }

    bool EditSession::readWorkspaceFile(const std::string &path, std::vector<WorkspaceEntry> &out)
    {
        std::ifstream f(path);
        if (!f) return false;
        out.clear();

        // One WorkspaceEntry per #group/#image. Inside an #image the lines up to the
        // first #hnode are the image's CURRENT params (+ history header: hcurrent/hmax/
        // hcoalesce); each following #hnode is a saved history node (its own params +
        // hparent/hseq/hlabel). A section's `paramsBuf` collects only the params lines;
        // the h*/parent/name/offset keys are consumed here and never fed to the parser.
        WorkspaceEntry cur;
        bool haveCur = false, inImage = false, inNode = false;
        std::string paramsBuf;
        HistoryNode node;

        auto closeParamBlock = [&] {
            if (inNode) { deserializeParams(paramsBuf, node.params); cur.history.nodes.push_back(node); node = HistoryNode{}; }
            else if (haveCur) deserializeParams(paramsBuf, cur.params);  // group OR image own params
            paramsBuf.clear();
        };
        auto flushEntry = [&] {
            if (!haveCur) return;
            closeParamBlock();
            // D-23: the format carries `path=` for an image and a name only for a group, so an
            // image entry arrived nameless and stayed that way all the way to the filmstrip and
            // the export tree. Naming it HERE fixes every consumer at once — the service's node
            // tree, the filmstrip cells, the export dialog and cosmo-cc — rather than each of
            // them learning to derive it, which is how they came to disagree in the first place.
            if (!cur.group && cur.name.empty() && !cur.imagePath.empty())
                cur.name = fileNameOf(cur.imagePath);
            out.push_back(std::move(cur));
            cur = WorkspaceEntry{};
            haveCur = false; inImage = false; inNode = false;
        };

        std::string line;
        while (std::getline(f, line))
        {
            if (line.rfind("cosmoworkspace=", 0) == 0) continue;
            if (line == "#group" || line == "#image")
            {
                flushEntry();
                haveCur = true;
                cur.group = (line == "#group");
                inImage = (line == "#image");
                inNode = false;
                continue;
            }
            if (line == "#hnode") { closeParamBlock(); inNode = true; continue; }

            const auto eq = line.find('=');
            if (eq == std::string::npos) { paramsBuf += line; paramsBuf += '\n'; continue; }
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (inNode)
            {
                if (k == "hparent") { try { node.parent = std::stoi(v); } catch (...) {} }
                else if (k == "hseq") { try { node.seq = std::stoi(v); } catch (...) {} }
                else if (k == "hlabel") node.label = v;
                else { paramsBuf += line; paramsBuf += '\n'; }
            }
            else if (k == "parent") { try { cur.parent = std::stoi(v); } catch (...) {} }
            else if (k == "name") cur.name = v;
            else if (k == "path") cur.imagePath = v;
            else if (k == "bypass") cur.bypass = (v != "0");   // R-BYPASS-6
            else if (k == "hcurrent") { try { cur.history.current = std::stoi(v); } catch (...) {} }
            else if (k == "hmax") { try { cur.history.maxSteps = std::stoi(v); } catch (...) {} }
            else if (k == "hcoalesce") { try { cur.history.coalesceMs = std::stod(v); } catch (...) {} }
            else if (k == "offset")
            {
                // Legacy (pre-full-params) group: a 12-scalar LocalAdjust. Map it into the
                // group's EditParams so old projects still load (temp was a relative
                // -100..100 shift -> Kelvin offset from 6500).
                std::stringstream ts(v); std::string t; float vals[12] = {0};
                int i = 0;
                while (i < 12 && std::getline(ts, t, ',')) { try { vals[i] = std::stof(t); } catch (...) {} ++i; }
                EditParams &o = cur.params;
                o.exposure = vals[0]; o.contrast = vals[1]; o.highlights = vals[2]; o.shadows = vals[3];
                o.whites = vals[4]; o.blacks = vals[5]; o.temp = 6500.f + vals[6] / 100.f * 3500.f; o.tint = vals[7];
                o.saturation = vals[8]; o.texture = vals[9]; o.clarity = vals[10]; o.dehaze = vals[11];
            }
            else { paramsBuf += line; paramsBuf += '\n'; }  // a params line
        }
        flushEntry();
        return true;
    }

    void EditSession::resetWorkspace()
    {
        // Drop every engine slot AND restart slot-id assignment from 0 (not just
        // release-in-place): the per-slot vectors below are cleared, so if the
        // service kept its monotonic counter the next opened image would get an
        // id past the end of these vectors -> out-of-bounds deref (the segfault
        // on New/Open/Import Catalog after a project is already open).
        mService.reset();
        mSlotParams.clear(); mSlotHistory.clear(); mSlotNames.clear();
        mSlotPaths.clear(); mSlotSessions.clear(); mSlotThumbs.clear();
        mNodes.clear();
        mNodes.push_back(GNode{true, "All Photos", 0, -1, {}, {}, {}});
        mCurGroup = 0; mSel.clear(); mSelAnchor = -1; mEditGroup = -1;
        mCurrentSlot = -1;
        mHasClip = false;
        mDirty = false;
    }

    int EditSession::addWorkspaceGroup(int parentNode, const std::string &name, const EditParams &params,
                                       const History &history, bool bypass)
    {
        if (parentNode < 0 || parentNode >= (int)mNodes.size() || !mNodes[parentNode].group) parentNode = 0;
        GNode g; g.group = true; g.name = name.empty() ? "Group" : name; g.parent = parentNode; g.params = params;
        g.bypass = bypass;   // R-BYPASS-6
        if (history.nodes.empty()) g.history.init(params);  // no saved tree -> single root
        else g.history.restore(history.nodes, history.current, history.maxSteps, history.coalesceMs);
        const int node = (int)mNodes.size();
        mNodes.push_back(g);
        mNodes[parentNode].kids.push_back(node);
        return node;
    }

    int EditSession::openImageInto(int parentNode, const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path)
    {
        if (parentNode < 0 || parentNode >= (int)mNodes.size() || !mNodes[parentNode].group) parentNode = mCurGroup;
        const int slot = mService.addImage(rgba, w, h, 4);   // copying path: no eviction story (R-MEM-2)
        if (slot < 0) return -1;
        mSlotParams.push_back(EditParams{});
        History hist; hist.maxSteps = mHistorySteps; hist.coalesceMs = mHistoryCoalesceMs;
        hist.init(EditParams{});
        mSlotHistory.push_back(std::move(hist));
        mSlotNames.push_back(name);
        mSlotPaths.push_back(path);
        mSlotSessions.push_back("");
        mSlotThumbs.push_back(makeThumb(rgba, w, h, kThumbEdge));

        GNode leaf; leaf.group = false; leaf.name = name; leaf.parent = parentNode; leaf.slot = slot;
        const int node = (int)mNodes.size();
        mNodes.push_back(leaf);
        mNodes[parentNode].kids.push_back(node);
        return slot;
    }

    // ── R-LOADUX-1: the rack exists before the pixels do ──────────────────────

    int EditSession::addPendingImage(int parentNode, const std::string &name)
    {
        if (parentNode < 0 || parentNode >= (int)mNodes.size() || !mNodes[parentNode].group) parentNode = mCurGroup;
        GNode leaf; leaf.group = false; leaf.name = name; leaf.parent = parentNode;
        leaf.slot = -1; leaf.pending = true;
        const int node = (int)mNodes.size();
        mNodes.push_back(leaf);
        mNodes[parentNode].kids.push_back(node);
        return node;
    }

    int EditSession::attachImage(int node, std::vector<uint8_t> &&rgba, int w, int h,
                                 const std::string &path, Thumb &&thumb)
    {
        if (node < 0 || node >= (int)mNodes.size()) return -1;
        GNode &leaf = mNodes[node];
        if (leaf.group || !leaf.pending || leaf.slot >= 0) return -1;
        // The source path travels with the pixels so the engine can re-decode this slot
        // after eviction instead of holding ~387 MB of it for the life of the project
        // (R-MEM-2). A slot added with no path is simply never re-decodable.
        const int slot = mService.addImage(std::move(rgba), w, h, 4, path);
        if (slot < 0) return -1;
        mSlotParams.push_back(EditParams{});
        History hist; hist.maxSteps = mHistorySteps; hist.coalesceMs = mHistoryCoalesceMs;
        hist.init(EditParams{});
        mSlotHistory.push_back(std::move(hist));
        // D-23 belt and braces: the reader now names entries, but this is the sink every
        // producer funnels through and it has the path right here. An empty name failed
        // silently — it rendered as nothing, or as "(missing image)" — and a silent failure
        // deserves a second line of defence at the point where the truth is available.
        if (leaf.name.empty() && !path.empty()) leaf.name = fileNameOf(path);
        mSlotNames.push_back(leaf.name);
        mSlotPaths.push_back(path);
        mSlotSessions.push_back("");
        mSlotThumbs.push_back(std::move(thumb));
        leaf.slot = slot;
        leaf.pending = false;
        // If this is the photo the user is sitting on (they clicked its spinner, or an
        // arrow walked onto it), show it now that it exists.
        if (mEditGroup < 0 && std::find(mSel.begin(), mSel.end(), node) != mSel.end())
        {
            mCurrentSlot = slot;
            resetPreviewResolution();
            submit();
        }
        return slot;
    }

    void EditSession::markImageFailed(int node)
    {
        if (node < 0 || node >= (int)mNodes.size()) return;
        mNodes[node].pending = false;   // slot stays -1: it reads as missing, not coming
    }

    int EditSession::openImageInto(int parentNode, std::vector<uint8_t> &&rgba, int w, int h,
                                   const std::string &name, const std::string &path, Thumb &&thumb)
    {
        // R-LOADPERF-2: the caller (a loader thread) already downsampled the thumbnail and
        // owns a buffer it will never touch again, so neither the ~100 MB copy nor the
        // full-image downsample is charged to whichever thread calls this.
        if (parentNode < 0 || parentNode >= (int)mNodes.size() || !mNodes[parentNode].group) parentNode = mCurGroup;
        // The source path travels with the pixels so the engine can re-decode this slot
        // after eviction instead of holding ~387 MB of it for the life of the project
        // (R-MEM-2). A slot added with no path is simply never re-decodable.
        const int slot = mService.addImage(std::move(rgba), w, h, 4, path);
        if (slot < 0) return -1;
        mSlotParams.push_back(EditParams{});
        History hist; hist.maxSteps = mHistorySteps; hist.coalesceMs = mHistoryCoalesceMs;
        hist.init(EditParams{});
        mSlotHistory.push_back(std::move(hist));
        mSlotNames.push_back(name);
        mSlotPaths.push_back(path);
        mSlotSessions.push_back("");
        mSlotThumbs.push_back(std::move(thumb));

        GNode leaf; leaf.group = false; leaf.name = name; leaf.parent = parentNode; leaf.slot = slot;
        const int node = (int)mNodes.size();
        mNodes.push_back(leaf);
        mNodes[parentNode].kids.push_back(node);
        return slot;
    }

    int EditSession::addWorkspaceMissingImage(int parentNode, const std::string &name)
    {
        if (parentNode < 0 || parentNode >= (int)mNodes.size() || !mNodes[parentNode].group) parentNode = 0;
        GNode leaf; leaf.group = false; leaf.name = name; leaf.parent = parentNode; leaf.slot = -1;
        const int node = (int)mNodes.size();
        mNodes.push_back(leaf);
        mNodes[parentNode].kids.push_back(node);
        return node;
    }

    void EditSession::finishWorkspaceLoad(const std::string &path)
    {
        mWorkspacePath = path;
        // R-LOADPERF-3: with a progressive reveal the editor has been usable since the
        // FIRST image landed, so by the time the last one arrives the photographer may
        // already be working on a different photo. Only pick image 0 when nothing has
        // been selected yet -- otherwise finishing the load would yank them back.
        if (mCurrentSlot < 0 && mEditGroup < 0)
        {
            if (!mSlotParams.empty()) selectImage(0);
            else mCurGroup = 0;
        }
        mDirty = false;   // freshly loaded == clean
    }

    int EditSession::openImage(const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path)
    {
        const int slot = openImageInto(mCurGroup, rgba, w, h, name, path);
        if (slot < 0) return -1;
        const auto &kids = mNodes[mCurGroup].kids;
        for (int c = 0; c < (int)kids.size(); ++c)
            if (mNodes[kids[c]].slot == slot) { selectNode(c, false, false); break; }
        return slot;
    }

    // ---- rendering engine ----

    void EditSession::resetPreviewResolution() { mService.setPreviewSize(mPreviewEdge); }

    void EditSession::setPreviewZoom(double zoomFactor)
    {
        const int eff = std::max(mPreviewEdge, (int)(mPreviewEdge * zoomFactor + 0.5));
        mService.setPreviewSize(eff);
    }

    const uint8_t *EditSession::exportFullRes(int &w, int &h) { return exportFullResSlot(mCurrentSlot, w, h); }

    const uint8_t *EditSession::exportFullResSlot(int slot, int &w, int &h)
    {
        // R-EXPORT-7: the batch exporter renders each selected slot through exactly the
        // composition the preview uses, so bypass (R-BYPASS-2) is honoured identically
        // and what the photographer saw is what lands on disk.
        if (slot < 0 || slot >= (int)mSlotParams.size()) { w = h = 0; return nullptr; }
        if (!mService.renderFull(slot, effectiveParams(slot), mExportFrame)) { w = h = 0; return nullptr; }
        w = mExportFrame.width; h = mExportFrame.height;
        return mExportFrame.rgba.data();
    }

    std::string EditSession::sourcePathForSlot(int slot) const
    {
        return (slot >= 0 && slot < (int)mSlotPaths.size()) ? mSlotPaths[slot] : std::string();
    }

    std::string EditSession::nameForSlot(int slot) const
    {
        return (slot >= 0 && slot < (int)mSlotNames.size()) ? mSlotNames[slot] : std::string();
    }

    const RenderService::Frame *EditSession::renderBefore()
    {
        if (mCurrentSlot < 0) return nullptr;
        const EditParams cur = effectiveParams(mCurrentSlot);
        EditParams b;
        b.cropX = cur.cropX; b.cropY = cur.cropY; b.cropW = cur.cropW; b.cropH = cur.cropH;
        b.rotation = cur.rotation; b.quarterTurns = cur.quarterTurns;
        b.lensDistortion = cur.lensDistortion; b.lensCA = cur.lensCA; b.lensVignette = cur.lensVignette;

        if (mBeforeSlot == mCurrentSlot &&
            b.cropX == mBeforeGeom.cropX && b.cropY == mBeforeGeom.cropY &&
            b.cropW == mBeforeGeom.cropW && b.cropH == mBeforeGeom.cropH &&
            b.rotation == mBeforeGeom.rotation && b.quarterTurns == mBeforeGeom.quarterTurns &&
            b.lensDistortion == mBeforeGeom.lensDistortion && b.lensCA == mBeforeGeom.lensCA &&
            b.lensVignette == mBeforeGeom.lensVignette)
            return &mBeforeFrame;

        mService.renderPreviewSync(mCurrentSlot, b, mBeforeFrame);
        mBeforeSlot = mCurrentSlot;
        mBeforeGeom = b;
        return &mBeforeFrame;
    }
}
}
