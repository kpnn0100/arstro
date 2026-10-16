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
 *  <TAB> <base64>` → `ok` | `error <TAB> <why>`, `latency` → `latency <TAB> <n>`, `quit`. The control socket closing
 *  (Solaris gone) ends it. One thread: a line and a block never overlap.
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
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

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

    void say(const std::string &line)
    {
        const std::string l = line + "\n";
        size_t at = 0;
        while (at < l.size())
        {
            const ssize_t w = write(kControl, l.data() + at, l.size() - at);
            if (w <= 0) std::exit(0); // Solaris is gone
            at += (size_t)w;
        }
    }

    std::string utf8(const String128 s) { return VST3::StringConvert::convert(s); }

    struct Served
    {
        VST3::Hosting::Module::Ptr module;
        IPtr<PlugProvider> provider;
        IPtr<IComponent> comp;
        IPtr<IEditController> ctrl;
        FUnknownPtr<IAudioProcessor> proc;
        HostProcessData data;
        EventList events;
        ParameterChanges changes{arstro::plughost::kMaxParams};
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

    int serve(const char *bundle, const char *uid, double rate, int maxBlock)
    {
        static HostApplication host;
        PluginContextFactory::instance().setPluginContext(&host);
        Served s;
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

        std::string pending;
        for (;;)
        {
            pollfd fds[2] = {{kControl, POLLIN, 0}, {kGo, POLLIN, 0}};
            if (poll(fds, 2, -1) < 0) continue;
            if (fds[1].revents & POLLIN)
            {
                uint64_t v = 0;
                if (read(kGo, &v, sizeof v) != sizeof v) continue;
                // a block: the reset first, then the events and the changes at their offsets, the inputs — processed
                if (sh->reset) s.start();
                const int n = std::max(0, std::min((int)sh->frames, s.maxBlock));
                sh->status = 0;
                {
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
                    for (int k = 0; k < std::min((int)sh->nParams, arstro::plughost::kMaxParams); ++k)
                    {
                        int32 qi = 0, pi = 0;
                        if (IParamValueQueue *q = s.changes.addParameterData(sh->params[k].id, qi))
                            q->addPoint(std::max(0, std::min(sh->params[k].offset, n - 1)), sh->params[k].value, pi); // (0 when no frames)
                        if (s.ctrl) s.ctrl->setParamNormalized(sh->params[k].id, sh->params[k].value); // its own idea of the value, kept
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
                }
                sh->reset = 0;
                const uint64_t one = 1;
                if (write(kDone, &one, sizeof one) != sizeof one) return 0;
            }
            if (fds[0].revents & (POLLIN | POLLHUP))
            {
                char buf[65536];
                const ssize_t r = read(kControl, buf, sizeof buf);
                if (r <= 0) return 0; // Solaris is gone
                pending.append(buf, (size_t)r);
                for (size_t eol; (eol = pending.find('\n')) != std::string::npos;)
                {
                    const std::string line = pending.substr(0, eol);
                    pending.erase(0, eol + 1);
                    const auto tab = line.find('\t');
                    const std::string cmd = line.substr(0, tab), arg = tab == std::string::npos ? std::string() : line.substr(tab + 1);
                    if (cmd == "quit") return 0;
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
                    say("error\tunknown `" + cmd + "`");
                }
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
