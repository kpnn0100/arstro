/*
 *  solaris — the plugins' own editor as a VST3 view on Linux (R-VST-7): `arstro::vst3::createEditor`,
 *  the seam the DSP repo's controller calls when the umbrella links this in (its Editor.h).
 *
 *  An X11 child window inside the host's (`kPlatformTypeX11EmbedWindowID`), on a display connection of
 *  its own; the host's `IRunLoop` (queried from the plug frame, as the SDK asks) drives it — our X
 *  connection's descriptor for input, a 16 ms timer for frames — so nothing here owns a thread or a
 *  loop. Each frame `InstrumentEditor` draws into a back buffer that is then copied to the window
 *  (no flicker), through Artboard's Cairo target with the suite's embedded fonts. Resizable: the host's
 *  size is the editor's (R4), never below its minimum.
 */
#include "PluginAccess.h"
#include "Editor.h"
#include "adapter/native/CairoTarget.h"
#include "base/source/fobject.h"
#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/common/pluginview.h"
#include <chrono>
#include <cstdint>
// X11 last: its macros (Bool, None, Status) must not reach the SDK's headers
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo/cairo-xlib.h>
#include <cairo/cairo.h>

using namespace Steinberg;

namespace arstro
{
namespace vst3
{
    namespace
    {
        class X11View;

        /** The host's run loop calls these: input waiting on our X connection, and a frame. */
        class Pump : public FObject, public Linux::IEventHandler, public Linux::ITimerHandler
        {
        public:
            explicit Pump(X11View *v) : mView(v) {}
            void PLUGIN_API onFDIsSet(Linux::FileDescriptor) SMTG_OVERRIDE;
            void PLUGIN_API onTimer() SMTG_OVERRIDE;
            void detach() { mView = nullptr; }

            OBJ_METHODS(Pump, FObject)
            DEFINE_INTERFACES
                DEF_INTERFACE(Linux::IEventHandler)
                DEF_INTERFACE(Linux::ITimerHandler)
            END_DEFINE_INTERFACES(FObject)
            REFCOUNT_METHODS(FObject)

        private:
            X11View *mView;
        };

        class X11View : public CPluginView
        {
        public:
            explicit X11View(Controller &c)
                : CPluginView(nullptr), mAccess(c), mEditor(c.type(), mAccess), mT0(std::chrono::steady_clock::now())
            {
                ViewRect r(0, 0, (int32)solaris_ui::InstrumentEditor::kWidth, (int32)solaris_ui::InstrumentEditor::kHeight);
                setRect(r);
            }
            ~X11View() SMTG_OVERRIDE { close(); }

            tresult PLUGIN_API isPlatformTypeSupported(FIDString type) SMTG_OVERRIDE
            {
                return type && FIDStringsEqual(type, kPlatformTypeX11EmbedWindowID) ? kResultTrue : kResultFalse;
            }

            tresult PLUGIN_API attached(void *parent, FIDString type) SMTG_OVERRIDE
            {
                if (!parent || isPlatformTypeSupported(type) != kResultTrue) return kResultFalse;
                mDpy = XOpenDisplay(nullptr);
                if (!mDpy) return kResultFalse;
                const int w = rect.getWidth(), h = rect.getHeight();
                mWin = XCreateSimpleWindow(mDpy, (Window)(uintptr_t)parent, 0, 0, (unsigned)w, (unsigned)h, 0, 0, 0);
                XSelectInput(mDpy, mWin, ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask);
                // XEmbed (the protocol hosts embed a plugin's window by): version 0, mapped — set before the window
                // is mapped, in the same batch as its creation, so a host reading it on CreateNotify finds it
                const Atom info = XInternAtom(mDpy, "_XEMBED_INFO", False);
                const long xembed[2] = {0, 1 /* XEMBED_MAPPED */};
                XChangeProperty(mDpy, mWin, info, info, 32, PropModeReplace, reinterpret_cast<const unsigned char *>(xembed), 2);
                XMapWindow(mDpy, mWin);
                XWindowAttributes attr;
                XGetWindowAttributes(mDpy, mWin, &attr);
                mWinSurf = cairo_xlib_surface_create(mDpy, mWin, attr.visual, w, h);
                resizeBack(w, h);
                mEditor.setSize(w, h);
                XFlush(mDpy);
                // the host's loop drives us (the SDK: the run loop is the plug frame's)
                FUnknownPtr<Linux::IRunLoop> loop(plugFrame);
                if (loop)
                {
                    mLoop = loop;
                    mPump = owned(new Pump(this));
                    mLoop->registerEventHandler(mPump, ConnectionNumber(mDpy));
                    mLoop->registerTimer(mPump, 16);
                }
                return CPluginView::attached(parent, type);
            }

            tresult PLUGIN_API removed() SMTG_OVERRIDE
            {
                close();
                return CPluginView::removed();
            }

            tresult PLUGIN_API onSize(ViewRect *r) SMTG_OVERRIDE
            {
                if (!r) return kInvalidArgument;
                CPluginView::onSize(r);
                const int w = r->getWidth(), h = r->getHeight();
                mEditor.setSize(w, h);
                if (mDpy && mWin)
                {
                    XResizeWindow(mDpy, mWin, (unsigned)w, (unsigned)h);
                    cairo_xlib_surface_set_size(mWinSurf, w, h);
                    resizeBack(w, h);
                    frame();
                }
                return kResultTrue;
            }

            tresult PLUGIN_API canResize() SMTG_OVERRIDE { return kResultTrue; }

            tresult PLUGIN_API checkSizeConstraint(ViewRect *r) SMTG_OVERRIDE
            {
                if (!r) return kInvalidArgument;
                if (r->getWidth() < (int32)solaris_ui::InstrumentEditor::kMinWidth) r->right = r->left + (int32)solaris_ui::InstrumentEditor::kMinWidth;
                if (r->getHeight() < (int32)solaris_ui::InstrumentEditor::kMinHeight) r->bottom = r->top + (int32)solaris_ui::InstrumentEditor::kMinHeight;
                return kResultTrue;
            }

            // ── the run loop's calls ──
            void drain()
            {
                if (!mDpy) return;
                while (XPending(mDpy))
                {
                    XEvent e;
                    XNextEvent(mDpy, &e);
                    const double now = nowMs();
                    switch (e.type)
                    {
                    case ButtonPress:
                        if (e.xbutton.button == 4 || e.xbutton.button == 5)
                            mEditor.wheel(e.xbutton.x, e.xbutton.y, e.xbutton.button == 4 ? 1.0 : -1.0, now);
                        else if (e.xbutton.button == 1 || e.xbutton.button == 3)
                            mEditor.pointer(0, e.xbutton.x, e.xbutton.y, e.xbutton.button == 3 ? 2 : 0, now, (e.xbutton.state & ShiftMask) != 0,
                                            (e.xbutton.state & ControlMask) != 0);
                        break;
                    case ButtonRelease:
                        if (e.xbutton.button == 1 || e.xbutton.button == 3)
                            mEditor.pointer(2, e.xbutton.x, e.xbutton.y, e.xbutton.button == 3 ? 2 : 0, now, (e.xbutton.state & ShiftMask) != 0,
                                            (e.xbutton.state & ControlMask) != 0);
                        break;
                    case MotionNotify:
                        mEditor.pointer(1, e.xmotion.x, e.xmotion.y, 0, now, (e.xmotion.state & ShiftMask) != 0, (e.xmotion.state & ControlMask) != 0);
                        break;
                    case Expose:
                        if (e.xexpose.count == 0) frame();
                        break;
                    default:
                        break;
                    }
                }
            }

            void frame()
            {
                if (!mDpy || !mBack) return;
                cairo_save(mBackCr);
                cairo_set_operator(mBackCr, CAIRO_OPERATOR_SOURCE);
                cairo_set_source_rgb(mBackCr, 0, 0, 0);
                cairo_paint(mBackCr);
                cairo_restore(mBackCr);
                mTarget.setContext(mBackCr);
                mEditor.render(mTarget, nowMs());
                cairo_surface_flush(mBack);
                cairo_t *cr = cairo_create(mWinSurf);
                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                cairo_set_source_surface(cr, mBack, 0, 0);
                cairo_paint(cr);
                cairo_destroy(cr);
                cairo_surface_flush(mWinSurf);
                XFlush(mDpy);
            }

        private:
            double nowMs() const { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mT0).count(); }

            void resizeBack(int w, int h)
            {
                if (mBackCr) cairo_destroy(mBackCr);
                if (mBack) cairo_surface_destroy(mBack);
                mBack = cairo_image_surface_create(CAIRO_FORMAT_RGB24, std::max(1, w), std::max(1, h));
                mBackCr = cairo_create(mBack);
            }

            void close()
            {
                if (mLoop && mPump)
                {
                    mLoop->unregisterTimer(mPump);
                    mLoop->unregisterEventHandler(mPump);
                }
                if (mPump) mPump->detach();
                mPump = nullptr;
                mLoop = nullptr;
                mTarget.setContext(nullptr);
                if (mBackCr) cairo_destroy(mBackCr);
                if (mBack) cairo_surface_destroy(mBack);
                if (mWinSurf) cairo_surface_destroy(mWinSurf);
                mBackCr = nullptr;
                mBack = mWinSurf = nullptr;
                if (mDpy)
                {
                    if (mWin) XDestroyWindow(mDpy, mWin);
                    XCloseDisplay(mDpy);
                }
                mDpy = nullptr;
                mWin = 0;
            }

            solaris_ui::ControllerAccess mAccess;
            solaris_ui::InstrumentEditor mEditor;
            std::chrono::steady_clock::time_point mT0;
            Display *mDpy = nullptr;
            Window mWin = 0;
            cairo_surface_t *mWinSurf = nullptr, *mBack = nullptr;
            cairo_t *mBackCr = nullptr;
            artboard::CairoTarget mTarget;
            IPtr<Pump> mPump;
            IPtr<Linux::IRunLoop> mLoop;
        };

        void PLUGIN_API Pump::onFDIsSet(Linux::FileDescriptor) { if (mView) mView->drain(); }
        void PLUGIN_API Pump::onTimer() { if (mView) mView->frame(); }
    }

    IPlugView *createEditor(Controller &controller) { return new X11View(controller); }
}
}
