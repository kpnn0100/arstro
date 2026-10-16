// solaris_host — the ALSA sequencer input, live (R-MIDI-2): a second client plays into the kernel's "Midi Through"
// port and MidiInAlsa, listening to it, hands each message to its sink as MIDI bytes. No sound device; SKIPPED (77)
// where there is no sequencer or no Midi Through.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "MidiInAlsa.h"
#include <alsa/asoundlib.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

using namespace arstro;

int main()
{
    std::vector<solaris::MidiPort> ports;
    std::string err;
    if (!solaris_host::listMidiInputs(ports, err)) { std::printf("SKIPPED: %s\n", err.c_str()); return 77; }
    std::string through;
    for (const auto &p : ports)
        if (p.name.rfind("Midi Through", 0) == 0) through = p.name;
    if (through.empty()) { std::printf("SKIPPED: no Midi Through port\n"); return 77; }

    std::mutex m;
    std::vector<solaris::MidiEvent> got;
    solaris_host::MidiInAlsa in;
    assert(in.open({through}, [&](const solaris::MidiEvent &e) { std::lock_guard<std::mutex> l(m); got.push_back(e); }, err));
    assert(in.listening().size() == 1 && in.listening()[0] == through);

    // the player: a client of its own, its port connected TO Midi Through
    snd_seq_t *seq = nullptr;
    assert(snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, 0) >= 0);
    const int port = snd_seq_create_simple_port(seq, "test out", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, SND_SEQ_PORT_TYPE_MIDI_GENERIC);
    int throughClient = -1, throughPort = 0;
    {
        snd_seq_client_info_t *ci;
        snd_seq_port_info_t *pi;
        snd_seq_client_info_alloca(&ci);
        snd_seq_port_info_alloca(&pi);
        snd_seq_client_info_set_client(ci, -1);
        while (snd_seq_query_next_client(seq, ci) >= 0 && throughClient < 0)
        {
            snd_seq_port_info_set_client(pi, snd_seq_client_info_get_client(ci));
            snd_seq_port_info_set_port(pi, -1);
            while (snd_seq_query_next_port(seq, pi) >= 0)
                if (through == snd_seq_port_info_get_name(pi)) { throughClient = snd_seq_port_info_get_client(pi); throughPort = snd_seq_port_info_get_port(pi); }
        }
    }
    assert(throughClient >= 0 && snd_seq_connect_to(seq, port, throughClient, throughPort) >= 0);
    auto send = [&](auto fill) {
        snd_seq_event_t ev;
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_source(&ev, port);
        snd_seq_ev_set_subs(&ev);
        snd_seq_ev_set_direct(&ev);
        fill(ev);
        snd_seq_event_output_direct(seq, &ev);
    };
    send([](snd_seq_event_t &ev) { snd_seq_ev_set_noteon(&ev, 0, 60, 100); });
    send([](snd_seq_event_t &ev) { snd_seq_ev_set_controller(&ev, 0, 64, 127); });
    send([](snd_seq_event_t &ev) { snd_seq_ev_set_noteoff(&ev, 0, 60, 0); });
    send([](snd_seq_event_t &ev) { snd_seq_ev_set_pitchbend(&ev, 0, 0); });
    for (int k = 0; k < 100; ++k)
    {
        {
            std::lock_guard<std::mutex> l(m);
            if (got.size() >= 4) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    in.close();
    snd_seq_close(seq);
    std::lock_guard<std::mutex> l(m);
    if (got.size() != 4) std::printf("    %zu messages\n", got.size());
    assert(got.size() == 4);
    assert(got[0].noteOn() && got[0].d1 == 60 && got[0].d2 == 100);
    assert(got[1].kind() == 0xB0 && got[1].d1 == 64 && got[1].d2 == 127);
    assert(got[2].noteOff() && got[2].d1 == 60);
    assert(got[3].kind() == 0xE0 && got[3].d1 == 0 && got[3].d2 == 64); // the centre, 8192
    std::printf("[PASS] ALSA MIDI in (R-MIDI-2): through \"%s\" a note on, the pedal, its off and a centred bend arrive as their bytes\n", through.c_str());
    std::printf("\n1 passed, 0 failed\n");
    return 0;
}
