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
                case SND_SEQ_EVENT_SYSEX:
                {
                    // R-CTRL-1: MIDI Machine Control — F0 7F <device> 06 <command> F7, any device id
                    const auto *b = static_cast<const uint8_t *>(ev->data.ext.ptr);
                    if (!b || ev->data.ext.len < 6 || b[0] != 0xF0 || b[1] != 0x7F || b[3] != 0x06) continue;
                    m = {0xF0, (uint8_t)(b[4] & 0x7F), 0x7F};
                    break;
                }
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

    // ── R-CTRL-2: out ────────────────────────────────────────────────────────────────────────────────

    MidiOutAlsa::~MidiOutAlsa() { close(); }

    bool MidiOutAlsa::open(const std::string &port, std::string &err)
    {
        close();
        if (snd_seq_open(&mSeq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) { mSeq = nullptr; err = "the ALSA sequencer cannot be opened"; return false; }
        snd_seq_set_client_name(mSeq, "Solaris");
        mPort = snd_seq_create_simple_port(mSeq, "Solaris surface out", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                                           SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        if (mPort < 0) { err = "the ALSA sequencer gave no port"; close(); return false; }
        // the writable port of that name
        snd_seq_client_info_t *ci;
        snd_seq_port_info_t *pi;
        snd_seq_client_info_alloca(&ci);
        snd_seq_port_info_alloca(&pi);
        snd_seq_client_info_set_client(ci, -1);
        bool connected = false;
        const int self = snd_seq_client_id(mSeq);
        while (!connected && snd_seq_query_next_client(mSeq, ci) >= 0)
        {
            const int client = snd_seq_client_info_get_client(ci);
            if (client == 0 || client == self) continue;
            snd_seq_port_info_set_client(pi, client);
            snd_seq_port_info_set_port(pi, -1);
            while (snd_seq_query_next_port(mSeq, pi) >= 0)
            {
                const unsigned caps = snd_seq_port_info_get_capability(pi);
                if ((caps & SND_SEQ_PORT_CAP_WRITE) == 0 || (caps & SND_SEQ_PORT_CAP_SUBS_WRITE) == 0) continue;
                if (port != snd_seq_port_info_get_name(pi)) continue;
                connected = snd_seq_connect_to(mSeq, mPort, client, snd_seq_port_info_get_port(pi)) >= 0;
                break;
            }
        }
        if (!connected) { err = "no MIDI port `" + port + "` takes output"; close(); return false; }
        snd_midi_event_t *enc = nullptr;
        if (snd_midi_event_new(4096, &enc) < 0) { err = "no MIDI encoder"; close(); return false; }
        mEncoder = enc;
        return true;
    }

    void MidiOutAlsa::send(const std::vector<uint8_t> &bytes)
    {
        if (!mSeq || !mEncoder) return;
        auto *enc = static_cast<snd_midi_event_t *>(mEncoder);
        size_t at = 0;
        while (at < bytes.size())
        {
            snd_seq_event_t ev;
            snd_seq_ev_clear(&ev);
            const long used = snd_midi_event_encode(enc, bytes.data() + at, (long)(bytes.size() - at), &ev);
            if (used <= 0) break;
            at += (size_t)used;
            if (ev.type == SND_SEQ_EVENT_NONE) continue; // a message not finished yet
            snd_seq_ev_set_source(&ev, mPort);
            snd_seq_ev_set_subs(&ev);
            snd_seq_ev_set_direct(&ev);
            snd_seq_event_output_direct(mSeq, &ev);
        }
    }

    void MidiOutAlsa::close()
    {
        if (mEncoder) snd_midi_event_free(static_cast<snd_midi_event_t *>(mEncoder));
        mEncoder = nullptr;
        if (mSeq) snd_seq_close(mSeq);
        mSeq = nullptr;
        mPort = -1;
    }
}
}
