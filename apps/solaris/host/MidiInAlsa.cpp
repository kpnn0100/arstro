#include "MidiInAlsa.h"
#include <alsa/asoundlib.h>
#include <algorithm>
#include <poll.h>

namespace arstro
{
namespace solaris_host
{
    namespace
    {
        struct Found
        {
            std::string name;
            int client = 0, port = 0;
        };
        /** Every port a keyboard could be: readable, subscribable, not the system's (client 0) nor `self`. */
        std::vector<Found> readablePorts(snd_seq_t *seq, int self)
        {
            std::vector<Found> out;
            snd_seq_client_info_t *ci;
            snd_seq_port_info_t *pi;
            snd_seq_client_info_alloca(&ci);
            snd_seq_port_info_alloca(&pi);
            snd_seq_client_info_set_client(ci, -1);
            while (snd_seq_query_next_client(seq, ci) >= 0)
            {
                const int client = snd_seq_client_info_get_client(ci);
                if (client == 0 || client == self) continue;
                snd_seq_port_info_set_client(pi, client);
                snd_seq_port_info_set_port(pi, -1);
                while (snd_seq_query_next_port(seq, pi) >= 0)
                {
                    const unsigned caps = snd_seq_port_info_get_capability(pi);
                    if ((caps & SND_SEQ_PORT_CAP_READ) == 0 || (caps & SND_SEQ_PORT_CAP_SUBS_READ) == 0) continue;
                    if (caps & SND_SEQ_PORT_CAP_NO_EXPORT) continue;
                    out.push_back(Found{snd_seq_port_info_get_name(pi), client, snd_seq_port_info_get_port(pi)});
                }
            }
            return out;
        }
    }

    bool listMidiInputs(std::vector<solaris::MidiPort> &out, std::string &err)
    {
        out.clear();
        snd_seq_t *seq = nullptr;
        if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, 0) < 0) { err = "the ALSA sequencer cannot be opened"; return false; }
        for (const auto &f : readablePorts(seq, snd_seq_client_id(seq))) out.push_back(solaris::MidiPort{f.name, f.name});
        snd_seq_close(seq);
        return true;
    }

    MidiInAlsa::~MidiInAlsa() { close(); }

    bool MidiInAlsa::open(const std::vector<std::string> &ports, std::function<void(const solaris::MidiEvent &)> sink, std::string &err)
    {
        close();
        if (snd_seq_open(&mSeq, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) < 0) { mSeq = nullptr; err = "the ALSA sequencer cannot be opened"; return false; }
        snd_seq_set_client_name(mSeq, "Solaris");
        mPort = snd_seq_create_simple_port(mSeq, "Solaris MIDI in", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                           SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        if (mPort < 0) { err = "the ALSA sequencer gave no port"; close(); return false; }
        mListening.clear();
        for (const auto &f : readablePorts(mSeq, snd_seq_client_id(mSeq)))
        {
            if (!ports.empty() && std::find(ports.begin(), ports.end(), f.name) == ports.end()) continue;
            if (snd_seq_connect_from(mSeq, mPort, f.client, f.port) >= 0) mListening.push_back(f.name);
        }
        mSink = std::move(sink);
        mRun.store(true);
        mThread = std::thread([this] { run(); });
        return true;
    }

    void MidiInAlsa::run()
    {
        const int nfds = snd_seq_poll_descriptors_count(mSeq, POLLIN);
        std::vector<pollfd> fds((size_t)std::max(1, nfds));
        snd_seq_poll_descriptors(mSeq, fds.data(), (unsigned)fds.size(), POLLIN);
        while (mRun.load())
        {
            if (poll(fds.data(), (nfds_t)fds.size(), 100) <= 0) continue; // a tenth of a second: `close` is heard
            snd_seq_event_t *ev = nullptr;
            while (snd_seq_event_input(mSeq, &ev) >= 0 && ev)
            {
                solaris::MidiEvent m;
                switch (ev->type)
                {
                case SND_SEQ_EVENT_NOTEON:
                    m = {(uint8_t)(0x90 | (ev->data.note.channel & 0x0F)), (uint8_t)(ev->data.note.note & 0x7F), (uint8_t)(ev->data.note.velocity & 0x7F)};
                    break;
                case SND_SEQ_EVENT_NOTEOFF:
                    m = {(uint8_t)(0x80 | (ev->data.note.channel & 0x0F)), (uint8_t)(ev->data.note.note & 0x7F), 0};
                    break;
                case SND_SEQ_EVENT_CONTROLLER:
                    m = {(uint8_t)(0xB0 | (ev->data.control.channel & 0x0F)), (uint8_t)(ev->data.control.param & 0x7F), (uint8_t)(ev->data.control.value & 0x7F)};
                    break;
                case SND_SEQ_EVENT_PITCHBEND:
                {
                    const int v = std::clamp(ev->data.control.value + 8192, 0, 16383);
                    m = {(uint8_t)(0xE0 | (ev->data.control.channel & 0x0F)), (uint8_t)(v & 0x7F), (uint8_t)(v >> 7)};
                    break;
                }
                default:
                    continue;
                }
                if (mSink) mSink(m);
            }
        }
    }

    void MidiInAlsa::close()
    {
        mRun.store(false);
        if (mThread.joinable()) mThread.join();
        if (mSeq) snd_seq_close(mSeq);
        mSeq = nullptr;
        mPort = -1;
        mListening.clear();
    }
}
}
