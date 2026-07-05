#include "App.h"
#include "../cosmo_core/PresetLibrary.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace { constexpr double kRailAnimMs = 200.0; }

    App::App(double width, double height)
        : mW(width), mH(height), mTheme(makeCosmoV2Theme()), mAccent(palette::primary())
    {
        mRoot = std::make_shared<Segment>();
        mRoot->width.set(width);
        mRoot->height.set(height);
        mRecognizer.setSink([this](const Gesture &g) { mRoot->onGesture(g); });

        mTopBar = std::make_shared<TopBar>();
        mTopBar->width.set(width);
        mTopBar->onRailToggle = [this] { toggleRail(); };
        mRoot->addChild(mTopBar);

        mLeftRail = std::make_shared<LeftRail>();
        mLeftRail->width.set(LeftRail::kOpenWidth);
        mLeftRail->tree()->onApply = [this](std::string relPath) {
            if (mSession.applyPreset(relPath)) { mLeftRail->tree()->setSelected(relPath); syncControlsToSlot(); }
        };
        mRoot->addChild(mLeftRail);

        mCenterStage = std::make_shared<CenterStage>();
        mCenterStage->photo()->onBeforeAfterChange = [this](bool after) {
            if (after)
            {
                if (mLastAfterFrame.width > 0)
                    mCenterStage->photo()->imageView()->setImage(mLastAfterFrame.rgba.data(), mLastAfterFrame.width, mLastAfterFrame.height);
            }
            else if (const auto *before = mSession.renderBefore())
            {
                if (before->width > 0)
                    mCenterStage->photo()->imageView()->setImage(before->rgba.data(), before->width, before->height);
            }
        };
        mCenterStage->filmstrip()->onSelect = [this](int cell, bool shift, bool ctrl) {
            mSession.selectNode(cell, shift, ctrl);
            syncControlsToSlot();
        };
        mCenterStage->filmstrip()->onActivate = [this](int cell) {
            const auto cells = mSession.currentGroupCells();
            if (cell >= 0 && cell < (int)cells.size() && cells[cell].group)
            {
                mSession.navigateToGroup(cells[cell].node);
                syncControlsToSlot();
            }
        };
        mCenterStage->breadcrumb()->onCrumbClick = [this](int idx) {
            std::vector<int> chain;
            for (int n = mSession.currentGroup(); ; n = mSession.nodes()[n].parent) { chain.push_back(n); if (n == 0) break; }
            std::reverse(chain.begin(), chain.end());
            if (idx >= 0 && idx < (int)chain.size()) { mSession.navigateToGroup(chain[idx]); syncControlsToSlot(); }
        };
        mRoot->addChild(mCenterStage);

        // The right column (histogram/tabs/panels/action bar) lands in the next
        // milestone; center stage currently spans to the window's right edge.

        layout();
    }

    void App::layout()
    {
        mTopBar->width.set(mW);
        mTopBar->layout();

        mLeftRail->x.set(0.0);
        mLeftRail->y.set(TopBar::kHeight);
        mLeftRail->height.set(mH - TopBar::kHeight);
        mLeftRail->layout();

        mCenterStage->x.set(mLeftRail->width.value());
        mCenterStage->y.set(TopBar::kHeight);
        mCenterStage->width.set(std::max(0.0, mW - mLeftRail->width.value()));
        mCenterStage->height.set(mH - TopBar::kHeight);
        mCenterStage->layout();
    }

    void App::toggleRail()
    {
        mRailOpen = !mRailOpen;
        mTopBar->setRailOpen(mRailOpen);
        mLeftRail->width.animateTo(mRailOpen ? LeftRail::kOpenWidth : 0.0, kRailAnimMs, Easing::EaseOutCubic, mNowMs);
    }

    void App::refreshPresetTree()
    {
        mLeftRail->tree()->setRoots(cosmo::PresetLibrary::scan(mSession.presetDir()));
    }

    void App::registerThumb(int slot)
    {
        const auto *thumb = mSession.thumbForSlot(slot);
        if (thumb) mCenterStage->filmstrip()->addThumb(thumb->rgba.data(), thumb->w, thumb->h);
    }

    namespace
    {
        std::string filenameOf(const std::string &path)
        {
            const auto slash = path.find_last_of('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }
    }

    void App::syncControlsToSlot()
    {
        const int slot = mSession.currentSlot();
        if (slot >= 0)
            mTopBar->setFilename(filenameOf(mSession.currentSourcePath()), slot + 1, mSession.imageCount());
        else
            mTopBar->setFilename("", 0, 0);

        // Breadcrumb: the group path, plus the selected image's own filename as
        // the trailing crumb (matching the Figma mock's "Library > Album >
        // river_02.jpg") when an image, not a group, is being edited.
        std::vector<std::string> crumbs = mSession.breadcrumbPath();
        if (mSession.editGroup() < 0 && slot >= 0)
        {
            const std::string name = filenameOf(mSession.currentSourcePath());
            if (!name.empty()) crumbs.push_back(name);
        }
        mCenterStage->breadcrumb()->setPath(crumbs);

        // Filmstrip: the current group's children + which cell(s) are selected.
        std::vector<Filmstrip::Cell> cells;
        for (const auto &c : mSession.currentGroupCells())
            cells.push_back({c.group, c.node, c.slot, c.name, c.count});
        mCenterStage->filmstrip()->setCells(cells);

        const auto &kids = mSession.nodes()[mSession.currentGroup()].kids;
        const auto &sel = mSession.selection();
        std::vector<int> selCells;
        int primary = -1;
        for (int c = 0; c < (int)kids.size(); ++c)
            if (std::find(sel.begin(), sel.end(), kids[c]) != sel.end()) selCells.push_back(c);
        for (int c = 0; c < (int)kids.size(); ++c)
            if ((mSession.editGroup() >= 0 && kids[c] == mSession.editGroup()) ||
                (mSession.editGroup() < 0 && !mSession.nodes()[kids[c]].group && mSession.nodes()[kids[c]].slot == slot))
                primary = c;
        mCenterStage->filmstrip()->setSelection(selCells, primary);
    }

    void App::setSize(double width, double height)
    {
        if (width < 320) width = 320;
        if (height < 240) height = 240;
        mW = width; mH = height;
        mRoot->width.set(width); mRoot->height.set(height);
        layout();
    }

    void App::pointer(int kind, double x, double y, int button, double timeMs, bool alt, bool shift, bool ctrl)
    {
        RawPointer::Kind k = kind == 0 ? RawPointer::Kind::Down
                             : kind == 2 ? RawPointer::Kind::Up
                                         : RawPointer::Kind::Move;
        PointerButton b = button == 2 ? PointerButton::Right : PointerButton::Left;
        RawPointer rp{k, Point{x, y}, b, timeMs};
        rp.alt = alt; rp.shift = shift; rp.ctrl = ctrl;
        mRecognizer.feed(rp);
    }

    void App::wheel(double x, double y, double delta, bool ctrl)
    {
        (void)ctrl;
        const double railX = mLeftRail->x.value(), railY = mLeftRail->y.value();
        if (x >= railX && x <= railX + mLeftRail->width.value() &&
            y >= railY && y <= railY + mLeftRail->height.value())
        {
            mLeftRail->scrollBy(delta);
        }
        // Center-stage zoom and right-column panel scrolling wire in once those
        // regions exist.
    }

    void App::render(IRenderTarget &target, double nowMs)
    {
        mNowMs = nowMs;
        mSession.tick(nowMs);
        mRoot->advance(nowMs);
        // Re-run manual composite layout every frame (cheap arithmetic) so
        // children stay in sync while the rail's width Property is mid-animation
        // -- matching the design brief's "the edit area reflows in step, it
        // doesn't just get covered or revealed" for the rail toggle.
        layout();

        RenderService::Frame f;
        if (mSession.renderService().tryAcquire(f) && f.width > 0)
        {
            mLastAfterFrame = f;
            if (mCenterStage->photo()->showAfter())
                mCenterStage->photo()->imageView()->setImage(f.rgba.data(), f.width, f.height);
            // Histogram/curve/mixer taps wire in with the right column.
        }

        target.save();
        target.setTransform(Transform::identity());
        drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(palette::background()));
        target.restore();

        mRoot->render(target);
        mRoot->renderOverlay(target);
    }

    int App::openImage(const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path)
    {
        const int slot = mSession.openImage(rgba, w, h, name, path);
        if (slot >= 0) registerThumb(slot);
        syncControlsToSlot();
        return slot;
    }

    int App::openImageInto(int parentNode, const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path)
    {
        const int slot = mSession.openImageInto(parentNode, rgba, w, h, name, path);
        if (slot >= 0) registerThumb(slot);
        return slot;
    }

    void App::selectImage(int slot)
    {
        mSession.selectImage(slot);
        syncControlsToSlot();
    }

    void App::deleteSelected()
    {
        mSession.deleteSelected();
        syncControlsToSlot();
    }

    void App::renameGroup(const std::string &name)
    {
        // The rename target is tracked by the (not-yet-built) filmstrip context
        // menu; until then this is a no-op seam for linux_main.cpp to call into.
        (void)name;
    }

    bool App::importPresetFrom(const std::string &path)
    {
        std::vector<std::string> present;
        if (!mSession.importPresetFrom(path, present)) return false;
        mSession.applyImport(present);
        syncControlsToSlot();
        return true;
    }

    void App::undo()
    {
        if (mSession.undo()) syncControlsToSlot();
    }

    void App::redo()
    {
        if (mSession.redo()) syncControlsToSlot();
    }

    void App::applyParams(const EditParams &p)
    {
        mSession.applyParams(p);
        syncControlsToSlot();
    }

    void App::saveSession()
    {
        if (mSession.currentSlot() < 0) return;
        if (mSession.sessionPath().empty())
        {
            if (onSaveAsRequested) onSaveAsRequested();
        }
        else
            mSession.saveSessionAs(mSession.sessionPath());
    }

    void App::saveWorkspace()
    {
        if (mSession.imageCount() == 0) return;
        if (mSession.workspacePath().empty())
        {
            if (onSaveWorkspaceRequested) onSaveWorkspaceRequested();
        }
        else
            mSession.saveWorkspaceAs(mSession.workspacePath());
    }

    void App::finishWorkspaceLoad(const std::string &path)
    {
        mSession.finishWorkspaceLoad(path);
        syncControlsToSlot();
    }
}
}
