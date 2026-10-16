#pragma once
/*
 *  The plugin seam (R-VST-6; docs/discussion.md Round 9) — beside the audio ones: another maker's VST3 plugin, one
 *  instance, played somewhere the core never sees (the host runs it out of process). The core asks it for blocks; it
 *  answers in time or goes OFFLINE, and says why. The core wraps an instance as a library Device (PluginDevice.h).
 */
#include <cstdint>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
    struct PluginParamInfo
    {
        uint32_t id = 0;
        std::string title, units;
        double def = 0.0;      // normalised 0 … 1
        int steps = 0;         // 0: continuous
    };

    struct PluginNote
    {
        int offset = 0, pitch = 60;
        bool on = true;
        float velocity = 1.0f; // 0 … 1
    };
    struct PluginChange
    {
        uint32_t id = 0;
        int offset = 0;
        double value = 0.0;    // normalised 0 … 1
    };

    /** What a plugin said on its own (R-VST-6, N26): its editor turned a parameter, changed many of its own (a preset of
     *  its own), was closed by its window, or its latency changed. */
    struct PluginEvent
    {
        enum Kind { Perform, Values, EditorClosed, Latency } kind = Perform;
        uint32_t id = 0;
        double value = 0.0;                              // Perform: 0 … 1; Latency: samples
        std::vector<std::pair<uint32_t, double>> values; // Values: every parameter, 0 … 1
    };

    class IPluginInstance
    {
    public:
        virtual ~IPluginInstance() = default;
        virtual bool isInstrument() const = 0;
        virtual int latency() const = 0;
        virtual const std::vector<PluginParamInfo> &params() const = 0;
        /** One block: `in` (stereo; an instrument's ignored) and `notes`/`changes` in, `out` (stereo) back — waiting at
         *  most `budgetSeconds`. False: it is offline (now or before) and `out` holds nothing to use. On the audio
         *  thread: no allocation, no lock. */
        virtual bool process(const float *const *in, float *const *out, int frames, bool reset, const PluginNote *notes, int nNotes,
                             const PluginChange *changes, int nChanges, double budgetSeconds) = 0;
        virtual bool offline() const = 0;
        /** Why it is offline ("" while it plays) — read off the audio thread. */
        virtual std::string why() const = 0;
        /** Its state as the plugin gives it, base64; and set — off the audio thread. */
        virtual bool getState(std::string &base64, std::string &err) = 0;
        virtual bool setState(const std::string &base64, std::string &err) = 0;
        /** Its own editor, in a window of its own titled `title` (N26) — off the audio thread. */
        virtual bool openEditor(const std::string &title, std::string &err) { (void)title; err = "it has no editor here"; return false; }
        virtual bool closeEditor(std::string &err) { (void)err; return true; }
        virtual bool editorOpen() const { return false; }
        /** What it said since the last call, without waiting — off the audio thread. */
        virtual void takeEvents(std::vector<PluginEvent> &out) { (void)out; }
    };
}
}
