#include "GradeDeck.h"
#include "CommandLine.h"
#include "Glyphs.h"
#include "TextFit.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kPadX = 9.75;
        constexpr int kThumbEdge = 172;      // two cosmo cells' worth of pixels: sharp at 2x
        constexpr int kFrameEdge = 160;
        Color fade(Color c, double a) { c.a *= a; return c; }
    }

    GradeDeck::GradeDeck()
    {
        clipToBounds = true;
        mStrip = std::make_shared<cosmo_v2::Filmstrip>();
        // Cosmo's filmstrip selection: Shift-click a range, Ctrl-click to toggle.
        mStrip->onSelect = [this](int cell, bool shift, bool ctrl) {
            const int i = rackIndexOfCell(cell);
            if (i < 0 || i >= (int)mRack.size()) return;
            const std::string b = cmd::quote(mRack[(size_t)i].bindName);
            if (shift) emit("rack select " + b + " --range");
            else if (ctrl) emit("rack select " + b + " --add");
            else
            {
                int selectedCount = 0;
                for (const auto &r : mRack) selectedCount += r.selected;
                if (i != mSelected || selectedCount > 1) emit("rack select " + b);
            }
        };
        mStrip->onContext = [this](int cell, double x, double y) {
            const int i = rackIndexOfCell(cell);
            if (i >= 0 && onContext) onContext(i, Point{x, y});   // cosmo's filmstrip reports the gesture's world point
        };
        // cosmo's drill-in: double-click a folder chip
        mStrip->onActivate = [this](int cell) {
            const int i = rackIndexOfCell(cell);
            if (i < 0 || !mRack[(size_t)i].group) return;
            openGroup(mRack[(size_t)i].rackObj);
            if (onNavigate) onNavigate(mRack[(size_t)i].rackObj);
        };
        addChild(mStrip);
        // R-CLR-4: the stills gallery — Cosmo's strip again, its cells the stills
        mStills = std::make_shared<cosmo_v2::Filmstrip>();
        mStills->onContext = [this](int cell, double x, double y) {
            const std::string id = stillOfCell(cell);
            if (!id.empty() && onStillContext) onStillContext(id, Point{x, y});
        };
        mStills->onActivate = [this](int cell) {
            const std::string id = stillOfCell(cell);
            if (!id.empty() && onStillActivate) onStillActivate(id);
        };
        mStills->opacity.set(0.0);
        mStills->visible = false;
        addChild(mStills);
        // R-CLR-3: the node graph of the Grade target
        mGraph = std::make_shared<NodeGraph>();
        mGraph->opacity.set(0.0);
        mGraph->visible = false;
        addChild(mGraph);
        for (int k = 0; k < 2; ++k)
        {
            mCrumb[k] = std::make_shared<cosmo_v2::Breadcrumb>();
            mCrumb[k]->setMeasuredText(true);   // real metrics: the estimate leaves wide gaps (design rule R5)
            mCrumb[k]->onCrumbClick = [this, k](int idx) {
                if (idx >= 0 && idx < (int)mCrumbChain[k].size()) openGroup(mCrumbChain[k][(size_t)idx]);
            };
            mCrumb[k]->opacity.set(k == 0 ? 1.0 : 0.0);
            addChild(mCrumb[k]);
        }
    }

    void GradeDeck::openGroup(const std::string &rackObj)
    {
        if (!rackObj.empty())
        {
            bool isGroup = false;
            for (const auto &n : mRack) isGroup = isGroup || (n.group && n.rackObj == rackObj);
            if (!isGroup) return;
        }
        mLevelWanted = rackObj;   // the fade-out → swap → fade-in runs in advance
    }

    int GradeDeck::cellOfRack(int rackIndex) const
    {
        for (int c = 0; c < (int)mCellRack.size(); ++c)
            if (mCellRack[(size_t)c] == rackIndex) return c;
        return -1;
    }

    void GradeDeck::bind(const interstellar::AppModel &m)
    {
        mRack = m.rack;
        mFps = m.fps > 0 ? m.fps : 24.0;
        mSelected = m.selectedRack;
        // The Grade target moved to a node another level holds: the strip follows it there (once
        // per move, so the user can still browse away from the selection).
        const std::string selKey = mSelected >= 0 && mSelected < (int)m.rack.size() ? m.rack[(size_t)mSelected].rackObj : std::string();
        if (selKey != mSelKeyLast)
        {
            mSelKeyLast = selKey;
            if (mSelected >= 0 && mSelected < (int)m.rack.size())
            {
                const int p = m.rack[(size_t)mSelected].parent;
                mLevelWanted = p >= 0 && p < (int)m.rack.size() ? m.rack[(size_t)p].rackObj : std::string();
            }
        }
        bool wantedThere = mLevelWanted.empty();
        for (const auto &n : m.rack) wantedThere = wantedThere || (n.group && n.rackObj == mLevelWanted);
        if (!wantedThere) mLevelWanted.clear();   // the group went away (ungrouped): back to the top
        if (!mLevelInit) { mLevel = mLevelWanted; mLevelInit = true; }
        rebuildCells();
        // R-CLR-4: the stills' cells, rebuilt (thumbnails and all) only when the gallery changed
        mStillList = m.stills;
        std::string skey;
        for (const auto &st : m.stills) skey += st.id + "|" + st.name + "|" + st.file + ";";
        if (skey != mStillsKey)
        {
            mStillsKey = skey;
            mStills->clearThumbs();
            std::vector<cosmo_v2::Filmstrip::Cell> cells;
            int slot = 0;
            for (const auto &st : m.stills)
            {
                cosmo_v2::Filmstrip::Cell c;
                c.name = st.name;
                c.thumbSlot = slot++;
                interstellar::Raster r;
                if (!(stillPicture && stillPicture(st.id, r) && !r.empty()))
                {
                    r.allocate(16, 9, 0);
                    for (size_t p = 0; p < r.rgba.size(); p += 4) { r.rgba[p] = r.rgba[p + 1] = r.rgba[p + 2] = 0x1A; r.rgba[p + 3] = 255; }
                }
                mStills->addThumb(r.rgba.data(), r.width, r.height);
                cells.push_back(c);
            }
            mStills->setCells(cells);
        }

        mGraph->bind(m);

        // the selected source's reference frame
        bindFrame(m);
    }

    void GradeDeck::rebuildCells()
    {
        mLevelIdx = -1;
        for (int i = 0; i < (int)mRack.size(); ++i)
            if (mRack[(size_t)i].group && mRack[(size_t)i].rackObj == mLevel) mLevelIdx = i;
        if (mLevelIdx < 0) mLevel.clear();
        mCellRack.clear();
        for (int i = 0; i < (int)mRack.size(); ++i)
            if (mRack[(size_t)i].parent == mLevelIdx) mCellRack.push_back(i);
        // Rebuild the cells (and their thumbnails) only when the level's SHAPE or media changed —
        // the strip's thumb pool is indexed by slot and must be refilled in lockstep.
        std::string key = mLevel + "#";
        for (int i : mCellRack)
        {
            const auto &n = mRack[(size_t)i];
            key += n.rackObj + "|" + n.media + "|" + cmd::num(n.frame) + "|" + (n.pending ? "p" : "") + (n.failed ? "f" : "") + ";";
        }
        const bool rebuild = key != mStructureKey;
        if (rebuild) mStrip->clearThumbs();
        std::vector<cosmo_v2::Filmstrip::Cell> cells;
        int slot = 0;
        for (int i : mCellRack)
        {
            const auto &n = mRack[(size_t)i];
            cosmo_v2::Filmstrip::Cell c;
            c.group = n.group;
            c.node = n.node;
            c.name = n.cosmoName.empty() ? n.bindName : n.cosmoName;
            c.bypassed = n.bypass;
            c.loading = n.pending;
            if (n.group)
            {
                for (const auto &k : mRack) if (k.parent == i) ++c.count;
            }
            else if (!n.pending)   // a decoding cell has no thumb, so cosmo's spinner shows through
            {
                c.thumbSlot = slot++;
                if (rebuild)
                {
                    interstellar::Raster r;
                    const bool got = !n.failed && thumbnail && thumbnail(n.media, n.frame, kThumbEdge, r) && !r.empty();
                    if (!got)
                    {
                        // an honest plate: no pixels were offered, so none are invented
                        r.allocate(16, 9, 0);
                        for (size_t p = 0; p < r.rgba.size(); p += 4) { r.rgba[p] = r.rgba[p + 1] = r.rgba[p + 2] = 0x1A; r.rgba[p + 3] = 255; }
                    }
                    mStrip->addThumb(r.rgba.data(), r.width, r.height);
                }
            }
            cells.push_back(c);
        }
        mStructureKey = key;
        mStrip->setCells(cells);
        // the selection, in this level's cells (cosmo rings every selected cell)
        std::vector<int> sel;
        for (int c = 0; c < (int)mCellRack.size(); ++c)
            if (mRack[(size_t)mCellRack[(size_t)c]].selected || mCellRack[(size_t)c] == mSelected) sel.push_back(c);
        const int primary = cellOfRack(mSelected);
        mStrip->setSelection(sel, primary);
        if (primary >= 0) mStrip->scrollCellIntoView(primary);

        // the path: the top, each group down to the shown one, and — cosmo's mock — the selected
        // source when it is in this level
        std::vector<std::string> path{"All sources"}, chain{std::string()};
        std::vector<int> up;
        for (int p = mLevelIdx; p >= 0 && p < (int)mRack.size(); p = mRack[(size_t)p].parent) up.push_back(p);
        for (auto it = up.rbegin(); it != up.rend(); ++it)
        {
            const auto &g = mRack[(size_t)*it];
            path.push_back(g.cosmoName.empty() ? g.bindName : g.cosmoName);
            chain.push_back(g.rackObj);
        }
        if (primary >= 0 && !mRack[(size_t)mSelected].group)
            path.push_back(mRack[(size_t)mSelected].cosmoName.empty() ? mRack[(size_t)mSelected].bindName : mRack[(size_t)mSelected].cosmoName);
        mCrumbPath = path;
        mCrumbChainNext = chain;
    }

    void GradeDeck::bindFrame(const interstellar::AppModel &m)
    {
        mSelVideo = false;
        mReason.clear();
        mSelMedia.clear();
        if (mSelected >= 0 && mSelected < (int)m.rack.size())
        {
            const auto &n = m.rack[mSelected];
            mSelName = n.cosmoName.empty() ? n.bindName : n.cosmoName;
            if (n.group) mReason = "A group grades every source under it \xE2\x80\x94 no reference frame to pick.";
            else if (!n.video) mReason = "A still is graded as it is \xE2\x80\x94 no reference frame to pick.";
            else if (n.failed) mReason = "Offline \xE2\x80\x94 the reference frame cannot be read.";
            else
            {
                mSelVideo = true;
                mSelMedia = n.media;
                mSrcFps = n.mediaFps > 0 ? n.mediaFps : mFps;
                // the WHOLE source once the service has opened it; until then, what the cut uses
                double dur = n.mediaDuration;
                if (dur <= 0.0) for (const auto &c : m.clips) if (c.src == n.rackObj) dur = std::max(dur, c.out);
                if (dur <= 0.0) dur = std::max(m.duration, n.frame + 1.0);
                mSourceDur = std::max(1.0 / mSrcFps, dur);
                if (!mDragging) mFrameTarget = n.frame;   // a gesture in flight outranks the model
            }
        }
        else
            mReason = m.rack.empty() ? std::string() : "Select a source to choose the frame it is graded on.";
        mSelectorWanted = mSelVideo;
    }

    void GradeDeck::layout()
    {
        // a level change: the strip slides a little from the side it came from while it fades
        const double lf = mLevelFade.value();
        mStrip->x.set(18.0 * (1.0 - lf) * (mLevelSwapping ? -mLevelDir : mLevelDir));
        mStrip->y.set(kHeaderH);
        mStrip->width.set(width.value());
        mStrip->height.set(cosmo_v2::Filmstrip::kHeight);
        // R-CLR-3/4: the sources, the stills and the node graph cross-fade in one place
        const double v0 = mViewAmt[0].value(), v1 = mViewAmt[1].value(), v2 = mViewAmt[2].value();
        mStrip->opacity.set(lf * v0);
        mStrip->visible = v0 > 0.001;
        mStills->x.set(0.0);
        mStills->y.set(kHeaderH);
        mStills->width.set(width.value());
        mStills->height.set(cosmo_v2::Filmstrip::kHeight);
        mStills->opacity.set(v1);
        mStills->visible = v1 > 0.001;
        mGraph->x.set(0.0);
        mGraph->y.set(kHeaderH);
        mGraph->width.set(width.value());
        mGraph->height.set(cosmo_v2::Filmstrip::kHeight);
        mGraph->opacity.set(v2);
        mGraph->visible = v2 > 0.001;
        const double sa = 1.0 - v0;   // how far the sources' breadcrumb has gone
        // cosmo's breadcrumb sits in the header, after "SOURCES n", where the rule was — up to the tabs
        const double bx = std::min(mHeaderRight, width.value());
        const double chipW = mViewTab[0].w > 0 ? width.value() - mViewTab[0].x + 8.0 : 200.0;
        for (auto &c : mCrumb)
        {
            c->x.set(bx);
            c->y.set((kHeaderH - cosmo_v2::Breadcrumb::kHeight) * 0.5);
            c->width.set(std::max(0.0, width.value() - kPadX - bx - chipW));
            c->height.set(cosmo_v2::Breadcrumb::kHeight);
            c->visible = c->opacity.value() > 0.001 && sa < 0.999;   // the faded-out one takes no clicks
        }
    }

    bool GradeDeck::hasFrameStrip() const { return height.value() - bandTop() - kBandLineH - 8.0 >= kMinStripH; }

    Rect GradeDeck::frameTrackRect() const
    {
        const double top = bandTop();
        if (hasFrameStrip())
        {
            const double h = std::min(kMaxStripH, height.value() - top - kBandLineH - 8.0);
            return Rect{kPadX, top + kBandLineH, std::max(0.0, width.value() - 2 * kPadX), h};
        }
        const double x0 = kPadX + 205.0;   // after the label, ‹ timecode ›, measured generously
        return Rect{x0, top, std::max(0.0, width.value() - kPadX - x0), std::max(0.0, std::min(kBandLineH, height.value() - top))};
    }

    double GradeDeck::frameToX(double t) const
    {
        const Rect r = frameTrackRect();
        return r.x + std::clamp(t / mSourceDur, 0.0, 1.0) * r.w;
    }

    void GradeDeck::refreshFrames()
    {
        // One thumbnail per strip cell, across the source's length. Rebuilt only when the media, the
        // cell count (a resize) or the length changed — the hook is promised "once per (path, t)".
        if (!mSelVideo || !hasFrameStrip()) return;
        const Rect r = frameTrackRect();
        const double cellW = r.h * 16.0 / 9.0;
        const int n = std::max(1, (int)std::floor(r.w / std::max(8.0, cellW)));
        const std::string key = mSelMedia + "|" + std::to_string(n) + "|" + cmd::num(mSourceDur);
        const bool retry = mFramesRetry && key == mFramesKey && (int)mFrames.size() == n;
        mFramesRetry = false;
        if (key == mFramesKey && !retry) return;
        if (!retry)
        {
            mFramesKey = key;
            if (!mFramesMedia.empty() && mFramesMedia != mSelMedia) mStripFadePending = true;   // a content change, not a resize
            mFramesMedia = mSelMedia;
            mFrames.assign(n, ImageSlot{});
            mFrameAlpha.clear();
            for (int i = 0; i < n; ++i) mFrameAlpha.emplace_back(new AnimatedProperty(1.0));
            mFrameFade.assign(n, false);
        }
        if (!thumbnail) return;
        for (int i = 0; i < n; ++i)
        {
            if (mFrames[i].has()) continue;
            interstellar::Raster ras;
            if (thumbnail(mSelMedia, (i + 0.5) / n * mSourceDur, kFrameEdge, ras) && !ras.empty())
            {
                mFrames[i].set(ras);
                // A frame the host had to decode lands later than its neighbours: it fades in.
                if (retry) { mFrameAlpha[i]->set(0.0); mFrameFade[i] = true; }
            }
        }
    }

    /** A source time on the source's own frame grid, inside the source (the last frame starts one
     *  frame before the end). */
    double GradeDeck::snapToFrame(double t) const
    {
        const double last = std::max(0.0, std::floor(mSourceDur * mSrcFps - 1e-6) / mSrcFps);
        return std::clamp(std::round(t * mSrcFps) / mSrcFps, 0.0, last);
    }

    void GradeDeck::preview(double t)
    {
        if (mSelected < 0 || mSelected >= (int)mRack.size()) return;
        if (std::fabs(t - mFrameLast) > 1e-9 || t < 0) { if (onPreview) onPreview(mRack[mSelected].bindName, t); }
    }

    /** ‹ › — one frame at the source's own rate, committed at once: the exact frame. */
    void GradeDeck::step(int dir)
    {
        if (!mSelVideo || mSelected < 0 || mSelected >= (int)mRack.size()) return;
        const double from = mRack[mSelected].frame;
        const double to = snapToFrame(from + dir / mSrcFps);
        if (std::fabs(to - snapToFrame(from)) < 1e-9) return;   // already at that end
        emit("rack frame " + cmd::quote(mRack[mSelected].bindName) + " --at " + cmd::seconds(to, mSrcFps));
    }

    bool GradeDeck::handleGesture(const Gesture &g, const Point &local)
    {
        const Rect tr = frameTrackRect();
        auto timeAt = [&](double x) { return tr.w > 0 ? std::clamp((x - tr.x) / tr.w, 0.0, 1.0) * mSourceDur : 0.0; };
        auto stepAt = [&](const Point &p) {
            if (!mSelVideo || mSelectorAmt.value() < 0.5) return 0;
            return mStepRect[0].contains(p) ? -1 : (mStepRect[1].contains(p) ? 1 : 0);
        };
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            {
                int hv = -1;
                for (int v = 0; v < 3; ++v) if (mViewTab[v].contains(local)) hv = v;
                mTabHover.setHovered(hv);
            }
            mTrackHovered = mSelVideo && tr.contains(local);
            const int sd = stepAt(local);
            mStepHover.setHovered(sd < 0 ? 0 : (sd > 0 ? 1 : -1));
            return true;
        }
        case Gesture::Type::Click:
            for (int v = 0; v < 3; ++v)
                if (mViewTab[v].contains(local)) { setView(v); return true; }   // R-CLR-3/4: the deck's views
            if (const int sd = stepAt(local)) { step(sd); return true; }
            break;
        case Gesture::Type::Down:
            if (mSelVideo && tr.contains(local))
            {
                mDragging = true;
                const double t = snapToFrame(timeAt(local.x));
                preview(t);
                mFrameTarget = t;
                mShownFrame.set(mFrameTarget);   // direct manipulation: under the pointer
                mFrameLast = mFrameTarget;
            }
            return true;
        case Gesture::Type::DragStart:
        case Gesture::Type::Drag:
            if (mDragging)
            {
                const double t = snapToFrame(timeAt(local.x));   // land on a frame of the source
                preview(t);                                       // the monitor shows it, graded
                mFrameTarget = t;
                mShownFrame.set(mFrameTarget);
                mFrameLast = mFrameTarget;
            }
            return true;
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
            if (mDragging)
            {
                mDragging = false;
                if (mSelected >= 0 && mSelected < (int)mRack.size())
                {
                    emit("rack frame " + cmd::quote(mRack[mSelected].bindName) + " --at " + cmd::seconds(mFrameTarget, mSrcFps));
                    if (onPreview) onPreview(mRack[mSelected].bindName, -1.0);   // the commit is what shows now
                }
            }
            return true;
        case Gesture::Type::Scroll:
            // cosmo's strip scrolls through scrollBy(notches × one cell) and does not take the
            // gesture itself, so it bubbles here. Elsewhere in the deck there is nothing to scroll.
            if (local.y >= kHeaderH && local.y <= kHeaderH + cosmo_v2::Filmstrip::kHeight)
            {
                mStrip->scrollBy(-g.delta.y / shell::wheelNotchPx() * cosmo_v2::Filmstrip::kWheelStep);
                return true;
            }
            return false;
        default:
            break;
        }
        return Segment::handleGesture(g, local);
    }

    void GradeDeck::advance(double nowMs)
    {
        // R-CLR-3/4: the views cross-fade; the tabs' underline travels to the one shown
        if (mViewWanted != mViewApplied)
        {
            for (int v = 0; v < 3; ++v) mViewAmt[v].animateTo(v == mViewWanted ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mViewApplied = mViewWanted;
        }
        for (auto &a : mViewAmt) a.update(nowMs);
        {
            const Rect tab = mViewTab[mViewWanted];
            if (tab.w > 0)
            {
                if (!mTabPlaced) { mTabX.set(tab.x); mTabW.set(tab.w); mTabTX = tab.x; mTabTW = tab.w; mTabPlaced = true; }
                else if (std::fabs(mTabTX - tab.x) > 0.5 || std::fabs(mTabTW - tab.w) > 0.5)
                {
                    mTabX.animateTo(tab.x, 200.0, Easing::EaseOutCubic, nowMs);
                    mTabW.animateTo(tab.w, 200.0, Easing::EaseOutCubic, nowMs);
                    mTabTX = tab.x;
                    mTabTW = tab.w;
                }
            }
            mTabX.update(nowMs);
            mTabW.update(nowMs);
        }
        if (!isHovered()) mTabHover.clear();
        mTabHover.advance(nowMs);
        // the level: fade the old cells out, swap, fade the new ones in (never a one-frame swap)
        if (!mLevelSwapping && mLevelWanted != mLevel && !mLevelFade.isAnimating())
        {
            int dw = -1, dl = -1;   // depth of each level, to know which way the strip slides
            for (const auto &n : mRack)
            {
                if (n.group && n.rackObj == mLevelWanted) dw = n.depth;
                if (n.group && n.rackObj == mLevel) dl = n.depth;
            }
            mLevelDir = dw > dl ? 1.0 : -1.0;
            mLevelSwapping = true;
            mLevelFade.animateTo(0.0, motion::kHoverMs, Easing::EaseOutCubic, nowMs);
        }
        if (mLevelSwapping && !mLevelFade.isAnimating() && mLevelFade.value() <= 0.001)
        {
            mLevel = mLevelWanted;
            rebuildCells();
            mLevelSwapping = false;
            mLevelFade.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
        }
        mLevelFade.update(nowMs);
        // the breadcrumb: the new path cross-fades over the old one (two cosmo instances)
        if (!mCrumbInit)
        {
            mCrumb[mCrumbFront]->setPath(mCrumbPath);
            mCrumbChain[mCrumbFront] = mCrumbChainNext;
            mCrumbPathShown = mCrumbPath;
            mCrumbInit = true;
        }
        else if (mCrumbPath != mCrumbPathShown)
        {
            const int back = 1 - mCrumbFront;
            mCrumb[back]->setPath(mCrumbPath);
            mCrumbChain[back] = mCrumbChainNext;
            mCrumb[back]->opacity.set(0.0);
            mCrumb[back]->opacity.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mCrumb[mCrumbFront]->opacity.animateTo(0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mCrumb[back]->raise();
            mCrumbFront = back;
            mCrumbPathShown = mCrumbPath;
        }
        for (size_t i = 0; i < mFrameAlpha.size(); ++i)
        {
            if (i < mFrameFade.size() && mFrameFade[i])
            {
                mFrameAlpha[i]->animateTo(1.0, motion::kScrollMs, Easing::EaseOutCubic, nowMs);
                mFrameFade[i] = false;
            }
            mFrameAlpha[i]->update(nowMs);
        }
        if (!mSelectorInit)
        {
            mSelectorAmt.set(mSelectorWanted ? 1.0 : 0.0);
            mSelectorApplied = mSelectorWanted;
            mShownFrame.set(mFrameTarget);
            mFrameLast = mFrameTarget;
            mSelectorInit = true;
        }
        if (mSelectorWanted != mSelectorApplied)
        {
            mSelectorAmt.animateTo(mSelectorWanted ? 1.0 : 0.0, motion::kScrollMs, Easing::EaseOutCubic, nowMs);
            mSelectorApplied = mSelectorWanted;
        }
        mSelectorAmt.update(nowMs);
        if (!mDragging && mFrameTarget != mFrameLast)
        {
            mShownFrame.animateTo(mFrameTarget, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
            mFrameLast = mFrameTarget;
        }
        mShownFrame.update(nowMs);
        if (!isHovered()) mTrackHovered = false;
        if (mTrackHovered != mTrackHoverApplied)
        {
            mTrackHover.animateTo(mTrackHovered ? 1.0 : 0.0, motion::kHoverMs, Easing::EaseOutCubic, nowMs);
            mTrackHoverApplied = mTrackHovered;
        }
        mTrackHover.update(nowMs);
        if (!isHovered()) mStepHover.clear();
        mStepHover.advance(nowMs);
        refreshFrames();
        if (mStripFadePending)
        {
            mStripFade.set(0.0);
            mStripFade.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mStripFadePending = false;
        }
        mStripFade.update(nowMs);
        Segment::advance(nowMs);
    }

    void GradeDeck::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(surface::deckBg()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, 0.5); t.lineTo(w, 0.5); t.strokePath();

        // header row: SOURCES · n ─── (or STILLS · n — R-CLR-4: the two cross-fade) ··· [STILLS n]
        const double hy = kHeaderH * 0.5;
        const double stA = mViewAmt[1].value(), srcA = mViewAmt[0].value(), grA = mViewAmt[2].value();
        int sources = 0;
        for (const auto &n : mRack) if (!n.group) ++sources;
        const std::string srcCount = std::to_string(sources), stillCount = std::to_string(mStillList.size());
        auto title = [&](const char *word, const std::string &count, double a) {
            if (a <= 0.001) return 0.0;
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText(word, kPadX, textfit::baseline(hy, 9.0), 9.0, font::sansSemiBold(), 0.13 * 9.0);
            const double lw = t.measureText(word, 9.0, font::sansSemiBold(), 0.13 * 9.0);
            t.setFill(fade(palette::mutedForeground(), 0.7 * a));
            t.drawText(count, kPadX + lw + 6.0, textfit::baseline(hy, 9.0), 9.0, font::mono());
            return lw + 6.0 + t.measureText(count, 9.0, font::mono());
        };
        const double srcW = title("SOURCES", srcCount, srcA);
        title("STILLS", stillCount, stA);
        title("NODES", std::string(), grA);
        // the breadcrumb (a child) takes the header's remaining width, where a rule would run
        mHeaderRight = kPadX + (srcW > 0 ? srcW : t.measureText("SOURCES", 9.0, font::sansSemiBold(), 0.13 * 9.0) + 20.0) + 6.5;
        {
            // the views: SOURCES · STILLS · NODES, right-aligned, the shown one underlined (the line travels)
            const std::string labels[3] = {"SOURCES", "STILLS", "NODES"};
            double right = w - kPadX;
            for (int v = 2; v >= 0; --v)
            {
                const double tw = t.measureText(labels[v], 9.0, font::sansSemiBold(), 0.06 * 9.0) + 12.0;
                mViewTab[v] = Rect{right - tw, (kHeaderH - 17.0) * 0.5, tw, 17.0};
                right -= tw + 2.0;
            }
            for (int v = 0; v < 3; ++v)
            {
                const Rect r = mViewTab[v];
                const double hv = mTabHover.amount(v);
                if (hv > 0.001) drawRoundedRect(t, r, radius::control(), Paint::filled(palette::hoverWash(hv)));
                const double on = mViewAmt[v].value();
                t.setFill(lerpColor(lerpColor(palette::mutedForeground(), palette::foreground(), hv * 0.6), palette::foreground(), on));
                t.drawText(labels[v], r.x + 6.0, textfit::baseline(hy, 9.0), 9.0, font::sansSemiBold(), 0.06 * 9.0);
            }
            if (mTabW.value() > 0)
                drawRoundedRect(t, Rect{mTabX.value() + 4.0, mViewTab[0].bottom() - 1.0, std::max(0.0, mTabW.value() - 8.0), 1.5}, radius::pill(), Paint::filled(palette::primary()));
        }
        if (mRack.empty() && stA < 0.999)
        {
            const std::string s = "Sources appear here as footage joins the rack.";
            t.setFill(fade(palette::mutedForeground(), 1.0 - stA));
            t.drawText(s, (w - t.measureText(s, 11.0, font::sans())) * 0.5, kHeaderH + cosmo_v2::Filmstrip::kHeight * 0.5 + 4.0, 11.0, font::sans());
        }
        if (mStillList.empty() && stA > 0.001)
        {
            const std::string s = textfit::ellipsize(t, "Grab a still (Ctrl+Alt+G) to keep a frame and the grade it was made with.", w - 2 * kPadX, 11.0, font::sans());
            t.setFill(fade(palette::mutedForeground(), stA));
            t.drawText(s, (w - t.measureText(s, 11.0, font::sans())) * 0.5, kHeaderH + cosmo_v2::Filmstrip::kHeight * 0.5 + 4.0, 11.0, font::sans());
        }

        // the reference-frame band
        const double top = bandTop();
        if (h - top < 12.0) return;
        const double lineCy = top + std::min(kBandLineH, h - top) * 0.5;
        const double sa = mSelectorAmt.value();
        const Rect tr = frameTrackRect();
        const bool strip = hasFrameStrip();
        if (sa > 0.001)
        {
            // label line: REF FRAME  ‹ 00:00:02:12 › · <source> ……… length
            t.setFill(fade(palette::mutedForeground(), sa));
            t.drawText("REF FRAME", kPadX, textfit::baseline(lineCy, 9.0), 9.0, font::sansSemiBold(), 0.13 * 9.0);
            const double lx = kPadX + t.measureText("REF FRAME", 9.0, font::sansSemiBold(), 0.13 * 9.0) + 6.5;
            // ‹ › step one source frame (R-RACK-3): 16 px targets around the mono timecode
            constexpr double kStep = 16.0;
            const std::string tc = cmd::timecode(mShownFrame.value(), mSrcFps);
            const double tcW = t.measureText(tc, 10.0, font::mono());
            mStepRect[0] = Rect{lx, lineCy - kStep * 0.5, kStep, kStep};
            mStepRect[1] = Rect{lx + kStep + 3.0 + tcW + 3.0, lineCy - kStep * 0.5, kStep, kStep};
            for (int k = 0; k < 2; ++k)
            {
                const double hvk = mStepHover.amount(k);
                if (hvk > 0.001) drawRoundedRect(t, mStepRect[k], radius::control(), Paint::filled(fade(palette::hoverWash(hvk), sa)));
                const Color gc = fade(lerpColor(palette::mutedForeground(), palette::foreground(), hvk), sa);
                const Rect gr{mStepRect[k].x + 3.0, mStepRect[k].y + 3.0, kStep - 6.0, kStep - 6.0};
                if (k == 0) glyph::chevronLeft(t, gr, gc); else glyph::chevronRightSmall(t, gr, gc);
            }
            t.setFill(fade(palette::foreground(), sa));
            t.drawText(tc, lx + kStep + 3.0, textfit::baseline(lineCy, 10.0), 10.0, font::mono());
            const double nx = mStepRect[1].right() + 8.0;
            const std::string len = "of " + cmd::timecode(mSourceDur, mSrcFps);
            const double lenW = t.measureText(len, 9.0, font::mono());
            if (strip)
            {
                t.setFill(fade(palette::mutedForeground(), 0.8 * sa));
                t.drawText(len, w - kPadX - lenW, textfit::baseline(lineCy, 9.0), 9.0, font::mono());
                t.setFill(fade(palette::mutedForeground(), sa));
                t.drawText(textfit::ellipsize(t, mSelName + (mDragging ? " \xC2\xB7 previewing in the monitor \xE2\x80\x94 release to grade on this frame"
                                                                    : " \xC2\xB7 drag to seek, \xE2\x80\xB9 \xE2\x80\xBA for the exact frame"),
                                              w - kPadX - lenW - 12.0 - nx, 10.0, font::sans()),
                           nx, textfit::baseline(lineCy, 10.0), 10.0, font::sans());
            }
            const double hv = mTrackHover.value();
            const double x = frameToX(mShownFrame.value());
            if (strip)
            {
                // the strip of frames: thumbnails across the source, dimmed except under the marker
                const int n = (int)mFrames.size();
                drawRoundedRect(t, tr, radius::control(), Paint::filled(fade(surface::thumbPlaceholder(), sa)));
                t.save();
                t.clipRect(tr.x, tr.y, tr.w, tr.h);
                for (int i = 0; i < n; ++i)
                {
                    const Rect cell{tr.x + tr.w * i / n, tr.y, tr.w / n - 1.0, tr.h};
                    if (mFrames[i].has())
                    {
                        const double fa = i < (int)mFrameAlpha.size() ? mFrameAlpha[(size_t)i]->value() : 1.0;
                        t.pushLayer(sa * mStripFade.value() * (0.55 + 0.25 * hv) * fa);
                        mFrames[i].drawCover(t, cell);
                        t.popLayer();
                    }
                    else
                        glyph::film(t, Rect{cell.x + cell.w * 0.5 - 8.0, cell.y + cell.h * 0.5 - 6.0, 16.0, 12.0}, fade(palette::mutedForeground(), 0.4 * sa), 1.0);
                }
                t.restore();
                drawRoundedRect(t, tr, radius::control(), Paint::stroked(fade(lerpColor(palette::border(), palette::whiteAlpha(0.2), hv), sa), 1.0));
                // the slider's thumb: the reference frame, in the accent — a line through the strip
                // and cosmo's white slider knob on it, growing a little under the pointer
                drawRoundedRect(t, Rect{x - 1.0, tr.y - 3.0, 2.0, tr.h + 6.0}, radius::hairline(), Paint::filled(fade(palette::primary(), sa)));
                drawRoundedRect(t, Rect{x - 5.0, tr.y - 5.0, 10.0, 6.0}, radius::hairline(), Paint::filled(fade(palette::primary(), sa)));
                const double knob = 5.0 + 1.0 * std::max(hv, mDragging ? 1.0 : 0.0);
                drawCircle(t, x, tr.y + tr.h * 0.5, knob + 1.5, Paint::filled(fade(palette::primary(), sa)));
                drawCircle(t, x, tr.y + tr.h * 0.5, knob, Paint::filled(fade(palette::white(), sa)));
            }
            else
            {
                const double cy = tr.y + tr.h * 0.5;
                const double th = 3.0 + hv;
                drawRoundedRect(t, Rect{tr.x, cy - th * 0.5, tr.w, th}, radius::pill(), Paint::filled(fade(palette::secondary(), sa)));
                if (x - tr.x >= th)
                    drawRoundedRect(t, Rect{tr.x, cy - th * 0.5, x - tr.x, th}, radius::pill(), Paint::filled(fade(palette::primary(), sa)));
                drawCircle(t, x, cy, 4.5 + hv, Paint::filled(fade(palette::white(), sa)));
            }
        }
        if (sa < 0.999 && !mReason.empty())
        {
            const double ra = 1.0 - sa;
            const double cy = strip ? tr.y + tr.h * 0.5 : lineCy;
            const std::string s = textfit::ellipsize(t, mReason, w - 2 * kPadX, 10.0, font::sans());
            t.setFill(fade(palette::mutedForeground(), ra));
            t.drawText(s, (w - t.measureText(s, 10.0, font::sans())) * 0.5, textfit::baseline(cy, 10.0), 10.0, font::sans());
        }
    }

    void GradeDeck::onOverlay(IRenderTarget &t) const
    {
        // OFFLINE chips over the cells of missing sources (cosmo's strip has no word for it)
        t.save();
        t.clipRect(0, kHeaderH, width.value(), cosmo_v2::Filmstrip::kHeight);
        for (int c = 0; c < (int)mCellRack.size(); ++c)
        {
            if (!mRack[(size_t)mCellRack[(size_t)c]].failed) continue;
            const double x = mStrip->x.value() + mStrip->cellXForTest(c);
            const double cellY = kHeaderH + (cosmo_v2::Filmstrip::kHeight - 62.0) * 0.5;
            const std::string s = "OFFLINE";
            const double tw = t.measureText(s, 7.5, font::sansSemiBold());
            const Rect chip{x + (86.0 - tw - 8.0) * 0.5, cellY + 20.0, tw + 8.0, 13.0};
            const double lf = mLevelFade.value();   // the chip travels and fades with its cell
            drawRoundedRect(t, chip, radius::hairline(), Paint::filledStroked(fade(surface::scrim(0.8), lf), fade(palette::destructive(), lf), 1.0));
            t.setFill(fade(palette::destructive(), lf));
            t.drawText(s, chip.x + 4.0, textfit::baseline(chip.y + chip.h * 0.5, 7.5), 7.5, font::sansSemiBold());
        }
        t.restore();
    }
}
}
