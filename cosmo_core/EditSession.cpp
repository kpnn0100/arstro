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
    namespace
    {
        // Add a group's scalar offset onto an EditParams (one level of the chain).
        void addOffset(EditParams &e, const arstro::LocalAdjust &d)
        {
            e.exposure += d.exposure; e.contrast += d.contrast;
            e.highlights += d.highlights; e.shadows += d.shadows; e.whites += d.whites; e.blacks += d.blacks;
            e.temp += d.temp / 100.f * 3500.f;  // relative warm/cool shift in Kelvin
            e.tint += d.tint; e.saturation += d.saturation;
            e.texture += d.texture; e.clarity += d.clarity; e.dehaze += d.dehaze;
        }
    }

    EditSession::EditSession()
    {
        mNodes.push_back(GNode{true, "All Photos", 0, -1, {}, {}});  // root group (index 0)
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
            if (g.group) c.count = (int)g.kids.size();
            else c.slot = g.slot;
            cells.push_back(c);
        }
        return cells;
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

        // setEditTarget: image -> current slot (re-render); group -> offset editing.
        if (mNodes[node].group)
        {
            mEditGroup = node;
        }
        else
        {
            mEditGroup = -1;
            mCurrentSlot = mNodes[node].slot;
            resetPreviewResolution();
            submit();
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

    // ---- develop params ----

    EditParams *EditSession::curParams() { return (mCurrentSlot >= 0 && mCurrentSlot < (int)mSlotParams.size()) ? &mSlotParams[mCurrentSlot] : nullptr; }

    EditParams EditSession::effectiveParams(int slot) const
    {
        if (slot < 0 || slot >= (int)mSlotParams.size()) return EditParams{};
        EditParams e = mSlotParams[slot];
        int n = nodeForSlot(slot);
        if (n >= 0)
            for (int g = mNodes[n].parent; ; g = mNodes[g].parent)
            {
                addOffset(e, mNodes[g].offset);
                if (g == 0) break;
            }
        return e;
    }

    void EditSession::applyParams(const EditParams &p)
    {
        if (mCurrentSlot < 0 || mCurrentSlot >= (int)mSlotParams.size()) return;
        mSlotParams[mCurrentSlot] = p;
        submit();
    }

    void EditSession::applyParamsToSlot(int slot, const EditParams &p)
    {
        if (slot < 0 || slot >= (int)mSlotParams.size()) return;
        mSlotParams[slot] = p;
        mSlotHistory[slot].init(p);  // seed history with the LOADED state, not a blank one
    }

    void EditSession::recordHistory()
    {
        if (mSuppressHistory) return;
        if (mCurrentSlot < 0 || mCurrentSlot >= (int)mSlotHistory.size()) return;
        History &h = mSlotHistory[mCurrentSlot];
        if (h.empty()) h.init(mSlotParams[mCurrentSlot]);
        h.record(mSlotParams[mCurrentSlot], mNowMs);
    }

    void EditSession::submit()
    {
        if (mCurrentSlot < 0) return;
        mDirty = true;   // an edit is being committed -> unsaved changes
        recordHistory();
        EditParams p = effectiveParams(mCurrentSlot);
        if (mCropPreviewMode)
        { p.cropX = 0; p.cropY = 0; p.cropW = 1; p.cropH = 1; }
        mService.render(mCurrentSlot, p);
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

    History *EditSession::currentHistory()
    {
        return (mCurrentSlot >= 0 && mCurrentSlot < (int)mSlotHistory.size()) ? &mSlotHistory[mCurrentSlot] : nullptr;
    }

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
        if (!p || mCurrentSlot < 0) return nullptr;
        mSlotParams[mCurrentSlot] = *p;
        mSuppressHistory = true;
        submit();
        mSuppressHistory = false;
        return p;
    }

    const EditParams *EditSession::undo()
    {
        if (mCurrentSlot < 0 || mCurrentSlot >= (int)mSlotHistory.size()) return nullptr;
        return applyHistoryParams(mSlotHistory[mCurrentSlot].undo());
    }

    const EditParams *EditSession::redo()
    {
        if (mCurrentSlot < 0 || mCurrentSlot >= (int)mSlotHistory.size()) return nullptr;
        return applyHistoryParams(mSlotHistory[mCurrentSlot].redo());
    }

    const EditParams *EditSession::jumpToHistory(int node)
    {
        if (mCurrentSlot < 0 || mCurrentSlot >= (int)mSlotHistory.size()) return nullptr;
        return applyHistoryParams(mSlotHistory[mCurrentSlot].jumpTo(node));
    }

    bool EditSession::canUndo() const
    { return mCurrentSlot >= 0 && mCurrentSlot < (int)mSlotHistory.size() && mSlotHistory[mCurrentSlot].canUndo(); }
    bool EditSession::canRedo() const
    { return mCurrentSlot >= 0 && mCurrentSlot < (int)mSlotHistory.size() && mSlotHistory[mCurrentSlot].canRedo(); }

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
        std::function<void(int, int)> walk = [&](int node, int parentId) {
            for (int k : mNodes[node].kids)
            {
                const GNode &g = mNodes[k];
                const int myId = nextId++;
                if (g.group)
                {
                    const arstro::LocalAdjust &o = g.offset;
                    f << "#group\nparent=" << parentId << "\nname=" << g.name << "\noffset="
                      << o.exposure << ',' << o.contrast << ',' << o.highlights << ',' << o.shadows << ','
                      << o.whites << ',' << o.blacks << ',' << o.temp << ',' << o.tint << ','
                      << o.saturation << ',' << o.texture << ',' << o.clarity << ',' << o.dehaze << '\n';
                    walk(k, myId);
                }
                else if (g.slot >= 0 && g.slot < (int)mSlotPaths.size())
                {
                    f << "#image\nparent=" << parentId << "\npath=" << mSlotPaths[g.slot] << '\n'
                      << serializeParams(mSlotParams[g.slot]);
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

        bool inGroup = false, inImage = false;
        std::vector<std::string> block;
        auto flush = [&] {
            if (!inGroup && !inImage) return;
            WorkspaceEntry e;
            e.group = inGroup;
            std::string blockText;
            for (const auto &l : block)
            {
                blockText += l; blockText += '\n';
                const auto eq = l.find('=');
                if (eq == std::string::npos) continue;
                const std::string k = l.substr(0, eq), v = l.substr(eq + 1);
                if (k == "parent") { try { e.parent = std::stoi(v); } catch (...) {} }
                else if (k == "name") e.name = v;
                else if (k == "path") e.imagePath = v;
                else if (k == "offset")
                {
                    std::stringstream ts(v); std::string t; float vals[12] = {0};
                    int i = 0;
                    while (i < 12 && std::getline(ts, t, ',')) { try { vals[i] = std::stof(t); } catch (...) {} ++i; }
                    arstro::LocalAdjust &o = e.offset;
                    o.exposure = vals[0]; o.contrast = vals[1]; o.highlights = vals[2]; o.shadows = vals[3];
                    o.whites = vals[4]; o.blacks = vals[5]; o.temp = vals[6]; o.tint = vals[7];
                    o.saturation = vals[8]; o.texture = vals[9]; o.clarity = vals[10]; o.dehaze = vals[11];
                }
            }
            if (inImage) deserializeParams(blockText, e.params);
            out.push_back(std::move(e));
            block.clear();
        };

        std::string line;
        while (std::getline(f, line))
        {
            if (line == "#group" || line == "#image")
            {
                flush();
                inGroup = (line == "#group");
                inImage = (line == "#image");
                continue;
            }
            if (line.rfind("cosmoworkspace=", 0) == 0) continue;
            block.push_back(line);
        }
        flush();
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
        mNodes.push_back(GNode{true, "All Photos", 0, -1, {}, {}});
        mCurGroup = 0; mSel.clear(); mSelAnchor = -1; mEditGroup = -1;
        mCurrentSlot = -1;
        mHasClip = false;
        mDirty = false;
    }

    int EditSession::addWorkspaceGroup(int parentNode, const std::string &name, const arstro::LocalAdjust &offset)
    {
        if (parentNode < 0 || parentNode >= (int)mNodes.size() || !mNodes[parentNode].group) parentNode = 0;
        GNode g; g.group = true; g.name = name.empty() ? "Group" : name; g.parent = parentNode; g.offset = offset;
        const int node = (int)mNodes.size();
        mNodes.push_back(g);
        mNodes[parentNode].kids.push_back(node);
        return node;
    }

    int EditSession::openImageInto(int parentNode, const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path)
    {
        if (parentNode < 0 || parentNode >= (int)mNodes.size() || !mNodes[parentNode].group) parentNode = mCurGroup;
        const int slot = mService.addImage(rgba, w, h, 4);
        if (slot < 0) return -1;
        mSlotParams.push_back(EditParams{});
        History hist; hist.maxSteps = mHistorySteps; hist.coalesceMs = mHistoryCoalesceMs;
        hist.init(EditParams{});
        mSlotHistory.push_back(std::move(hist));
        mSlotNames.push_back(name);
        mSlotPaths.push_back(path);
        mSlotSessions.push_back("");
        mSlotThumbs.push_back(makeThumb(rgba, w, h, 110));

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
        if (!mSlotParams.empty())
            selectImage(0);
        else
            mCurGroup = 0;
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

    const uint8_t *EditSession::exportFullRes(int &w, int &h)
    {
        if (mCurrentSlot < 0) { w = h = 0; return nullptr; }
        if (!mService.renderFull(mCurrentSlot, effectiveParams(mCurrentSlot), mExportFrame)) { w = h = 0; return nullptr; }
        w = mExportFrame.width; h = mExportFrame.height;
        return mExportFrame.rgba.data();
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
