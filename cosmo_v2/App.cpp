#include "App.h"

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    App::App(double width, double height)
        : mW(width), mH(height), mTheme(makeCosmoV2Theme()), mAccent(palette::primary())
    {
        mRoot = std::make_shared<Segment>();
        mRoot->width.set(width);
        mRoot->height.set(height);

        // The full chrome (top bar, left rail, center stage, right column) is
        // built incrementally in following milestones; for now the root is an
        // empty themed canvas so the app boots and renders the background.
    }

    void App::layout()
    {
        // Populated as chrome/panels land.
    }

    void App::syncControlsToSlot()
    {
        // No-op until panels exist to sync into.
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

    void App::wheel(double, double, double, bool)
    {
        // Wired once a scrollable panel exists.
    }

    void App::render(IRenderTarget &target, double nowMs)
    {
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
        return mSession.openImage(rgba, w, h, name, path);
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
