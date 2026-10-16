/*
 *  solaris_host — a MIDI keyboard through the ALSA sequencer (R-MIDI-2). One client, "Solaris", with one input
 *  port; `open` subscribes it to the chosen ports (by name — ALSA's client:port numbers move between boots; none
 *  named = every readable port but the system's and our own), and a thread waits on the sequencer (polled every
 *  100 ms, so `close` returns) and hands each note, controller and pitch bend to the sink as its MIDI bytes.
 *  `listMidiInputs` is the same walk without subscribing.
 */
#pragma once
#include "MidiIn.h"
#include <atomic>
#include <thread>

struct _snd_seq;

namespace arstro
{
namespace solaris_host
{
    bool listMidiInputs(std::vector<solaris::MidiPort> &out, std::string &err);

    class MidiInAlsa : public solaris::IMidiIn
    {
    public:
        ~MidiInAlsa() override;
        bool open(const std::vector<std::string> &ports, std::function<void(const solaris::MidiEvent &)> sink, std::string &err) override;
        void close() override;
        std::vector<std::string> listening() const override { return mListening; }

    private:
        void run();
        _snd_seq *mSeq = nullptr;
        int mPort = -1;
        std::thread mThread;
        std::atomic<bool> mRun{false};
        std::function<void(const solaris::MidiEvent &)> mSink;
        std::vector<std::string> mListening;
    };
}
}
