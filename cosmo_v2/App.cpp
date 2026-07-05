#include "App.h"
#include "../cosmo_core/PresetLibrary.h"

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

        // Center stage / right column land in the next milestones; the root is
        // otherwise an empty themed canvas below the chrome built so far.

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

    void App::syncControlsToSlot()
    {
        const int slot = mSession.currentSlot();
        if (slot >= 0)
        {
            // mSlotNames/paths aren't exposed by name lookup on EditSession yet
            // (only the currently-open source path is) -- the filmstrip milestone
            // adds a proper per-slot name accessor; until then this shows the
            // source path's filename via currentSourcePath().
            std::string name = mSession.currentSourcePath();
            auto slash = name.find_last_of('/');
            if (slash != std::string::npos) name = name.substr(slash + 1);
            mTopBar->setFilename(name, slot + 1, mSession.imageCount());
        }
        else
        {
            mTopBar->setFilename("", 0, 0);
        }
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

        RenderService::Frame f;
        if (mSession.renderService().tryAcquire(f) && f.width > 0)
        {
            // Pushed into the canvas/histogram once those widgets exist.
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
        syncControlsToSlot();
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
