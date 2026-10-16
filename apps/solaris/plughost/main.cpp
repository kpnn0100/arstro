/*
 *  solaris-plughost — a VST3 plugin, out of Solaris's process (R-VST-6; docs/discussion.md Round 9).
 *
 *  `solaris-plughost --scan <bundle>`: the bundle's audio classes, a line each on stdout —
 *      class <TAB> <uid> <TAB> instrument|effect <TAB> <name> <TAB> <vendor>
 *  and exit 0; a bundle that cannot be read: `error <TAB> <why>`, exit 1. Whatever the module does while it is loaded
 *  (crash, hang) happens HERE — the host reads the lines, the exit and the signal, and goes on to the next bundle.
 *
 *  `solaris-plughost --serve <bundle> <uid> <rate> <maxBlock>`: ONE instance of that class, played. Its fds: 3 the
 *  control socket (lines), 4 the shared block (PlugShared.h), 5 the "go" eventfd, 6 the "done" eventfd. It says
 *      ready <TAB> instrument|effect <TAB> <latency> <TAB> <parameters>
 *      param <TAB> <id> <TAB> <steps> <TAB> <default 0…1> <TAB> <title> <TAB> <units>     (one per parameter)
 *      end
 *  (or `error <TAB> <why>`), then serves: a "go" processes the block — the reset, the events and the parameter
 *  changes, the inputs — and answers "done"; control lines between blocks: `state` → `state <TAB> <base64>`, `setstate
 *  <TAB> <base64>` → `ok` | `error <TAB> <why>`, `latency` → `latency <TAB> <n>`, `editor-open <TAB> <title>` → `ok` |
 *  `error <TAB> <why>` (the plugin's own editor in a window of its own, N26), `editor-close` → `ok`, `quit`. Said on its
 *  own, any time: `edit <TAB> perform <TAB> <id> <TAB> <0…1>` (the editor turned it), `edit <TAB> values <TAB> <id>:<v>,…`
 *  (the plugin changed its own), `edit <TAB> latency <TAB> <n>`, `editor <TAB> closed` (its window's close). The control
 *  socket closing (Solaris gone) ends it. Two threads: the audio thread plays the blocks; the UI thread answers the
 *  lines, tells the controller what a block set and runs the editor's window and run loop — an editor never delays a
 *  block.
 */
#include "PlugShared.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <string>
#include <mutex>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
    std::string clean(std::string s)
    {
        for (auto &ch : s)
            if (ch == '\t' || ch == '\n' || ch == '\r') ch = ' ';
        return s;
    }

    int scan(const char *bundle)
    {
        std::string why;
        auto module = VST3::Hosting::Module::create(bundle, why);
        if (!module)
        {
            std::printf("error\t%s\n", clean(why.empty() ? "not a VST3 module" : why).c_str());
            return 1;
        }
        int n = 0;
        for (const auto &ci : module->getFactory().classInfos())
        {
            if (ci.category() != kVstAudioEffectClass) continue;
            const std::string sub = ci.subCategoriesString();
            const bool instrument = sub.find("Instrument") != std::string::npos;
            std::printf("class\t%s\t%s\t%s\t%s\n", ci.ID().toString().c_str(), instrument ? "instrument" : "effect", clean(ci.name()).c_str(),
                        clean(ci.vendor()).c_str());
            ++n;
        }
        if (n == 0) { std::printf("error\tno audio processor in it\n"); return 1; }
        return 0;
    }
}

namespace
{
    constexpr int kControl = 3, kMemory = 4, kGo = 5, kDone = 6;

    /** A line to Solaris — the UI thread's only (the audio thread never speaks). */
    void say(const std::string &line)
    {
        const std::string l = line + "\n";
        size_t at = 0;
        while (at < l.size())
        {
            const ssize_t w = write(kControl, l.data() + at, l.size() - at);
            if (w <= 0) _exit(0); // Solaris is gone
            at += (size_t)w;
        }
    }

    std::string utf8(const String128 s) { return VST3::StringConvert::convert(s); }
    std::string num(double v)
    {
        char b[40];
        std::snprintf(b, sizeof b, "%.17g", v);
        return b;
    }

    /** Between the two threads: what the editor turned, for the next block; what a block set, for the controller (whose
     *  calls are the UI thread's, as VST3 has it). `wake` tells the UI thread there is something for the controller. */
    struct Queues
    {
        std::mutex m;
        std::vector<std::pair<ParamID, ParamValue>> toProcessor, toController;
        int wake = -1;
    } gQ;

    struct Served
    {
        VST3::Hosting::Module::Ptr module;
        IPtr<PlugProvider> provider;
        IPtr<IComponent> comp;
        IPtr<IEditController> ctrl;
        FUnknownPtr<IAudioProcessor> proc;
        HostProcessData data;
        EventList events;
        ParameterChanges changes{arstro::plughost::kMaxParams * 2};
        bool instrument = false;
        int maxBlock = 128;
        double rate = 48000;

        bool start(SymbolicSampleSizes sz = kSample32)
        {
            proc->setProcessing(false);
            comp->setActive(false);
            ProcessSetup setup{kRealtime, sz, maxBlock, rate};
            if (proc->setupProcessing(setup) != kResultOk) return false;
            if (comp->setActive(true) != kResultOk) return false;
            proc->setProcessing(true);
            return true;
        }
    };

    /** The editor's gestures (N26): each value to the processor at its next block and to Solaris as a line; a plugin that
     *  changes its own values says them all; a new latency is said. */
    class Handler : public U::ImplementsNonDestroyable<U::Directly<IComponentHandler>>
    {
    public:
        Served *s = nullptr;
        tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
        tresult PLUGIN_API performEdit(ParamID id, ParamValue v) override
        {
            {
                std::lock_guard<std::mutex> l(gQ.m);
                gQ.toProcessor.emplace_back(id, v);
            }
            say("edit\tperform\t" + std::to_string(id) + "\t" + num(v));
            return kResultOk;
        }
        tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
        tresult PLUGIN_API restartComponent(int32 flags) override
        {
            if ((flags & kParamValuesChanged) && s->ctrl)
            {
                std::string l = "edit\tvalues\t";
                const int32 n = s->ctrl->getParameterCount();
                for (int32 i = 0; i < n; ++i)
                {
                    ParameterInfo pi{};
                    if (s->ctrl->getParameterInfo(i, pi) == kResultOk) l += std::to_string(pi.id) + ":" + num(s->ctrl->getParamNormalized(pi.id)) + ",";
                }
                say(l);
            }
            if (flags & kLatencyChanged) say("edit\tlatency\t" + std::to_string(s->proc->getLatencySamples()));
            return kResultOk;
        }
    };

    /** The editor's window: the plug frame (resizes) and the run loop the plugin's view asks it for (its fds, its
     *  timers) — served by the UI thread's poll. */
    class Frame : public U::ImplementsNonDestroyable<U::Directly<IPlugFrame, Linux::IRunLoop>>
    {
    public:
        using Clock = std::chrono::steady_clock;
        struct Timer
        {
            IPtr<Linux::ITimerHandler> h;
            int ms = 16;
            Clock::time_point next;
        };
        Display *dpy = nullptr;
        Window win = 0;
        std::vector<std::pair<int, IPtr<Linux::IEventHandler>>> fds;
        std::vector<Timer> timers;

        tresult PLUGIN_API resizeView(IPlugView *view, ViewRect *r) override
        {
            if (!view || !r || !dpy) return kInvalidArgument;
            XResizeWindow(dpy, win, (unsigned)std::max(1, r->getWidth()), (unsigned)std::max(1, r->getHeight()));
            XFlush(dpy);
            view->onSize(r);
            return kResultTrue;
        }
        tresult PLUGIN_API registerEventHandler(Linux::IEventHandler *h, Linux::FileDescriptor fd) override
        {
            if (!h) return kInvalidArgument;
            fds.emplace_back(fd, IPtr<Linux::IEventHandler>(h));
            return kResultTrue;
        }
        tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler *h) override
        {
            fds.erase(std::remove_if(fds.begin(), fds.end(), [h](const auto &f) { return f.second.get() == h; }), fds.end());
            return kResultTrue;
        }
        tresult PLUGIN_API registerTimer(Linux::ITimerHandler *h, Linux::TimerInterval ms) override
        {
            if (!h) return kInvalidArgument;
            const int every = std::max(1, (int)ms);
            timers.push_back(Timer{IPtr<Linux::ITimerHandler>(h), every, Clock::now() + std::chrono::milliseconds(every)});
            return kResultTrue;
        }
        tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler *h) override
        {
            timers.erase(std::remove_if(timers.begin(), timers.end(), [h](const Timer &t) { return t.h.get() == h; }), timers.end());
            return kResultTrue;
        }
    };

    struct Editor
    {
        IPtr<IPlugView> view;
        Frame frame;
        Display *dpy = nullptr;
        Window win = 0;
        Atom del = 0;
        int w = 0, h = 0;
    };

    bool openEditor(Served &s, Editor &e, const std::string &title, std::string &why)
    {
        if (e.view) { XRaiseWindow(e.dpy, e.win); XFlush(e.dpy); return true; } // already open: brought forward
        if (!s.ctrl) { why = "it has no editor"; return false; }
        IPtr<IPlugView> v = owned(s.ctrl->createView(ViewType::kEditor));
        if (!v) { why = "it has no editor of its own"; return false; }
        if (v->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID) != kResultTrue) { why = "its editor is not made for X11"; return false; }
        Display *dpy = XOpenDisplay(nullptr);
        if (!dpy) { why = "there is no display to open it on"; return false; }
        ViewRect r(0, 0, 480, 320);
        v->getSize(&r);
        const int w = std::max(1, r.getWidth()), h = std::max(1, r.getHeight());
        const Window win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, (unsigned)w, (unsigned)h, 0, 0, BlackPixel(dpy, DefaultScreen(dpy)));
        // its name: UTF-8 for the window manager, the old property too
        const Atom utf8String = XInternAtom(dpy, "UTF8_STRING", False), netName = XInternAtom(dpy, "_NET_WM_NAME", False);
        XChangeProperty(dpy, win, netName, utf8String, 8, PropModeReplace, reinterpret_cast<const unsigned char *>(title.data()), (int)title.size());
        XStoreName(dpy, win, title.c_str());
        Atom del = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(dpy, win, &del, 1);
        if (v->canResize() != kResultTrue)
            if (XSizeHints *hints = XAllocSizeHints())
            {
                hints->flags = PMinSize | PMaxSize;
                hints->min_width = hints->max_width = w;
                hints->min_height = hints->max_height = h;
                XSetWMNormalHints(dpy, win, hints);
                XFree(hints);
            }
        XSelectInput(dpy, win, StructureNotifyMask);
        XMapWindow(dpy, win);
        XSync(dpy, False);
        e.frame.dpy = dpy;
        e.frame.win = win;
        v->setFrame(&e.frame);
        if (v->attached(reinterpret_cast<void *>(static_cast<uintptr_t>(win)), kPlatformTypeX11EmbedWindowID) != kResultTrue)
        {
            v->setFrame(nullptr);
            XDestroyWindow(dpy, win);
            XCloseDisplay(dpy);
            e.frame = Frame();
            why = "its editor would not open";
            return false;
        }
        e.view = v;
        e.dpy = dpy;
        e.win = win;
        e.del = del;
        e.w = w;
        e.h = h;
        return true;
    }

    void closeEditor(Editor &e)
    {
        if (!e.view) return;
        e.view->removed();
        e.view->setFrame(nullptr);
        e.view = nullptr;
        e.frame.fds.clear();
        e.frame.timers.clear();
        XDestroyWindow(e.dpy, e.win);
        XCloseDisplay(e.dpy);
        e.dpy = nullptr;
        e.win = 0;
        e.frame.dpy = nullptr;
    }

    /** One block (the audio thread's): the reset, Solaris's notes and changes and the editor's, the inputs, processed. */
    void block(Served &s, arstro::plughost::Shared *sh)
    {
        if (sh->reset) s.start();
        const int n = std::max(0, std::min((int)sh->frames, s.maxBlock));
        sh->status = 0;
        // (no frames: only the parameter changes — VST3's flush)
        s.events.clear();
        for (int k = 0; n > 0 && k < std::min((int)sh->nEvents, arstro::plughost::kMaxEvents); ++k)
        {
            const auto &e = sh->events[k];
            Event ev{};
            ev.busIndex = 0;
            ev.sampleOffset = std::max(0, std::min(e.offset, n - 1));
            ev.type = e.on ? Event::kNoteOnEvent : Event::kNoteOffEvent;
            if (e.on) { ev.noteOn.pitch = (int16)e.pitch; ev.noteOn.velocity = e.velocity; ev.noteOn.noteId = -1; }
            else { ev.noteOff.pitch = (int16)e.pitch; ev.noteOff.velocity = 0.0f; ev.noteOff.noteId = -1; }
            s.events.addEvent(ev);
        }
        s.changes.clearQueue();
        auto change = [&](ParamID id, int offset, ParamValue v) {
            int32 qi = 0, pi = 0;
            if (IParamValueQueue *q = s.changes.addParameterData(id, qi)) q->addPoint(std::max(0, std::min(offset, n - 1)), v, pi); // (0 when no frames)
        };
        {
            // the editor's, first: Solaris's for the same parameter (later in the queue) win
            std::unique_lock<std::mutex> l(gQ.m, std::try_to_lock);
            if (l.owns_lock())
            {
                for (const auto &e : gQ.toProcessor) change(e.first, 0, e.second);
                gQ.toProcessor.clear();
            }
        }
        const int np = std::min((int)sh->nParams, arstro::plughost::kMaxParams);
        for (int k = 0; k < np; ++k) change(sh->params[k].id, sh->params[k].offset, sh->params[k].value);
        if (np > 0)
        {
            {
                std::lock_guard<std::mutex> l(gQ.m);
                for (int k = 0; k < np; ++k) gQ.toController.emplace_back(sh->params[k].id, sh->params[k].value); // its own idea of the value, kept
            }
            const uint64_t one = 1;
            (void)!write(gQ.wake, &one, sizeof one);
        }
        if (s.data.numInputs > 0)
            for (int c = 0; c < std::min(2, (int)s.data.inputs[0].numChannels); ++c) std::memcpy(s.data.inputs[0].channelBuffers32[c], sh->in[c], sizeof(float) * (size_t)n);
        s.data.numSamples = n;
        s.data.inputEvents = &s.events;
        s.data.inputParameterChanges = &s.changes;
        if (s.proc->process(s.data) != kResultOk) sh->status = 1;
        for (int c = 0; c < 2; ++c)
            if (s.data.numOutputs > 0 && c < s.data.outputs[0].numChannels) std::memcpy(sh->out[c], s.data.outputs[0].channelBuffers32[c], sizeof(float) * (size_t)n);
            else std::memset(sh->out[c], 0, sizeof(float) * (size_t)n);
        sh->reset = 0;
    }

    int serve(const char *bundle, const char *uid, double rate, int maxBlock)
    {
        static HostApplication host;
        PluginContextFactory::instance().setPluginContext(&host);
        static Served s;
        static Handler handler;
        static Editor ed;
        handler.s = &s;
        s.rate = rate;
        s.maxBlock = std::max(1, std::min(maxBlock, arstro::plughost::kMaxBlock));
        std::string why;
        s.module = VST3::Hosting::Module::create(bundle, why);
        if (!s.module) { say("error\t" + clean(why.empty() ? "not a VST3 module" : why)); return 1; }
        VST3::Hosting::ClassInfo info;
        bool found = false;
        for (const auto &ci : s.module->getFactory().classInfos())
            if (ci.category() == kVstAudioEffectClass && ci.ID().toString() == uid) { info = ci; found = true; }
        if (!found) { say(std::string("error\tno class ") + uid + " in it"); return 1; }
        s.instrument = info.subCategoriesString().find("Instrument") != std::string::npos;
        s.provider = owned(new PlugProvider(s.module->getFactory(), info, true));
        if (!s.provider->initialize() || !(s.comp = s.provider->getComponentPtr())) { say("error\tit did not start"); return 1; }
        s.ctrl = s.provider->getControllerPtr();
        if (s.ctrl) s.ctrl->setComponentHandler(&handler);
        s.proc = FUnknownPtr<IAudioProcessor>(s.comp);
        if (!s.proc) { say("error\tno audio processor"); return 1; }
        SpeakerArrangement stereo = SpeakerArr::kStereo, in = SpeakerArr::kStereo;
        const int32 nIn = s.comp->getBusCount(kAudio, kInput);
        if (s.proc->setBusArrangements(nIn > 0 ? &in : nullptr, nIn > 0 ? 1 : 0, &stereo, 1) != kResultOk) { say("error\tit will not play stereo"); return 1; }
        if (nIn > 0) s.comp->activateBus(kAudio, kInput, 0, true);
        s.comp->activateBus(kAudio, kOutput, 0, true);
        if (s.comp->getBusCount(kEvent, kInput) > 0) s.comp->activateBus(kEvent, kInput, 0, true);
        if (!s.start()) { say("error\tit refused the rate or the block"); return 1; }
        s.data.prepare(*s.comp, s.maxBlock, kSample32);
        auto *sh = (arstro::plughost::Shared *)mmap(nullptr, sizeof(arstro::plughost::Shared), PROT_READ | PROT_WRITE, MAP_SHARED, kMemory, 0);
        if (sh == MAP_FAILED) { say("error\tno shared block"); return 1; }
        gQ.wake = eventfd(0, EFD_CLOEXEC);

        const int32 count = s.ctrl ? s.ctrl->getParameterCount() : 0;
        say("ready\t" + std::string(s.instrument ? "instrument" : "effect") + "\t" + std::to_string(s.proc->getLatencySamples()) + "\t" + std::to_string(count));
        for (int32 i = 0; i < count; ++i)
        {
            ParameterInfo pi{};
            if (s.ctrl->getParameterInfo(i, pi) != kResultOk) continue;
            say("param\t" + std::to_string(pi.id) + "\t" + std::to_string(pi.stepCount) + "\t" + std::to_string(pi.defaultNormalizedValue) + "\t" +
                clean(utf8(pi.title)) + "\t" + clean(utf8(pi.units)));
        }
        say("end");

        // the audio thread: a "go", a block, a "done" — never waiting on the editor
        std::thread([sh] {
            for (;;)
            {
                uint64_t v = 0;
                if (read(kGo, &v, sizeof v) != sizeof v)
                {
                    if (errno == EINTR) continue;
                    return;
                }
                block(s, sh);
                const uint64_t one = 1;
                if (write(kDone, &one, sizeof one) != sizeof one) return;
            }
        }).detach();

        // the UI thread: Solaris's lines, the controller's values, the editor's window and the plugin's run loop
        std::string pending;
        for (;;)
        {
            std::vector<pollfd> fds{{kControl, POLLIN, 0}, {gQ.wake, POLLIN, 0}};
            const auto handlers = ed.frame.fds; // as polled: a callback may change the list
            if (ed.view)
            {
                fds.push_back({ConnectionNumber(ed.dpy), POLLIN, 0});
                for (const auto &h : handlers) fds.push_back({h.first, POLLIN, 0});
            }
            int timeout = -1;
            if (ed.view)
            {
                timeout = 100;
                const auto now = Frame::Clock::now();
                for (const auto &t : ed.frame.timers)
                    timeout = std::min<int>(timeout, (int)std::max<long long>(0, std::chrono::duration_cast<std::chrono::milliseconds>(t.next - now).count()));
            }
            if (poll(fds.data(), (nfds_t)fds.size(), timeout) < 0 && errno != EINTR) continue;
            if (fds[1].revents & POLLIN)
            {
                uint64_t v = 0;
                (void)!read(gQ.wake, &v, sizeof v);
                std::vector<std::pair<ParamID, ParamValue>> values;
                {
                    std::lock_guard<std::mutex> l(gQ.m);
                    values.swap(gQ.toController);
                }
                if (s.ctrl)
                    for (const auto &x : values) s.ctrl->setParamNormalized(x.first, x.second);
            }
            if (fds[0].revents & (POLLIN | POLLHUP))
            {
                char buf[65536];
                const ssize_t r = read(kControl, buf, sizeof buf);
                if (r <= 0) { closeEditor(ed); _exit(0); } // Solaris is gone
                pending.append(buf, (size_t)r);
                for (size_t eol; (eol = pending.find('\n')) != std::string::npos;)
                {
                    const std::string line = pending.substr(0, eol);
                    pending.erase(0, eol + 1);
                    const auto tab = line.find('\t');
                    const std::string cmd = line.substr(0, tab), arg = tab == std::string::npos ? std::string() : line.substr(tab + 1);
                    if (cmd == "quit") { closeEditor(ed); _exit(0); }
                    if (cmd == "latency") { say("latency\t" + std::to_string(s.proc->getLatencySamples())); continue; }
                    if (cmd == "state")
                    {
                        IPtr<MemoryStream> st = owned(new MemoryStream());
                        if (s.comp->getState(st) != kResultOk) { say("error\tit gave no state"); continue; }
                        say("state\t" + arstro::plughost::toBase64(std::string(st->getData(), (size_t)st->getSize())));
                        continue;
                    }
                    if (cmd == "setstate")
                    {
                        std::string bytes;
                        if (!arstro::plughost::fromBase64(arg, bytes)) { say("error\tnot base64"); continue; }
                        IPtr<MemoryStream> st = owned(new MemoryStream());
                        int32 put = 0;
                        st->write(bytes.data(), (int32)bytes.size(), &put);
                        st->seek(0, IBStream::kIBSeekSet, nullptr);
                        if (s.comp->setState(st) != kResultOk) { say("error\tit refused the state"); continue; }
                        if (s.ctrl)
                        {
                            st->seek(0, IBStream::kIBSeekSet, nullptr);
                            s.ctrl->setComponentState(st);
                        }
                        say("ok");
                        continue;
                    }
                    if (cmd == "editor-open")
                    {
                        std::string e;
                        say(openEditor(s, ed, arg, e) ? std::string("ok") : "error\t" + clean(e));
                        continue;
                    }
                    if (cmd == "editor-close") { closeEditor(ed); say("ok"); continue; }
                    say("error\tunknown `" + cmd + "`");
                }
            }
            if (!ed.view) continue;
            while (ed.view && XPending(ed.dpy))
            {
                XEvent ev;
                XNextEvent(ed.dpy, &ev);
                if (ev.type == ClientMessage && (Atom)ev.xclient.data.l[0] == ed.del)
                {
                    closeEditor(ed); // its window's own close
                    say("editor\tclosed");
                }
                else if (ev.type == ConfigureNotify && (ev.xconfigure.width != ed.w || ev.xconfigure.height != ed.h))
                {
                    ed.w = ev.xconfigure.width;
                    ed.h = ev.xconfigure.height;
                    ViewRect r(0, 0, ed.w, ed.h);
                    if (ed.view->canResize() == kResultTrue && ed.view->checkSizeConstraint(&r) == kResultTrue) ed.view->onSize(&r);
                }
            }
            if (!ed.view) continue;
            for (size_t i = 0; i < handlers.size() && ed.view; ++i)
                if (fds[3 + i].revents & POLLIN) handlers[i].second->onFDIsSet(handlers[i].first);
            const auto now = Frame::Clock::now();
            const auto timers = ed.frame.timers;
            for (const auto &t : timers)
            {
                if (!ed.view || t.next > now) continue;
                t.h->onTimer();
                for (auto &live : ed.frame.timers)
                    if (live.h.get() == t.h.get()) live.next = std::max(live.next + std::chrono::milliseconds(live.ms), now);
            }
        }
    }
}

int main(int argc, char **argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--scan") == 0) return scan(argv[2]);
    if (argc == 6 && std::strcmp(argv[1], "--serve") == 0) return serve(argv[2], argv[3], std::atof(argv[4]), std::atoi(argv[5]));
    std::fprintf(stderr, "usage: solaris-plughost --scan <bundle.vst3> | --serve <bundle> <uid> <rate> <maxBlock>\n");
    return 2;
}
