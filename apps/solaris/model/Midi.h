/*
 *  solaris_model — Standard MIDI Files, read and written (R-MIDI-1).
 *
 *  Pure: bytes in, notes out (and back) — no device, no clock, no file; the service reads and writes the
 *  file and makes patterns of what this returns, as it does with the `.slp` parser. A file's own format
 *  (0, 1 or 2), running status, a note-on of velocity 0 as a note-off, and both kinds of division are read:
 *  ticks per quarter note (beats are ticks / division), or SMPTE frames (seconds through the file's tempo
 *  map, then beats). What is written is a format-1 file at 960 ticks per quarter — Solaris's own tick
 *  (`kPpq`), so a pattern exported and imported again lands on the same ticks.
 */
#pragma once
#include "Project.h"
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace solaris
{
    struct MidiNote
    {
        int pitch = 60, vel = 100, channel = 0;
        long long on = 0, off = 0;   // ticks
    };

    struct MidiTrack
    {
        std::string name;            // its FF 03 name, "" when it has none
        std::vector<MidiNote> notes; // in the order they started
    };

    struct MidiFile
    {
        int format = 1;
        int division = 960;          // ticks per quarter note (PPQ); 0 when `smpte`
        bool smpte = false;
        int fps = 0, ticksPerFrame = 0;
        std::vector<std::pair<long long, double>> tempos; // tick → microseconds per quarter, ascending
        std::vector<MidiTrack> tracks;                    // every track, notes or not
    };

    /** Read a Standard MIDI File. Refuses anything that is not one, naming why. */
    bool parseMidi(const std::string &bytes, MidiFile &out, std::string &err);

    /** A tick of the file as beats (quarter notes). */
    double midiBeats(const MidiFile &f, long long tick);

    struct MidiOutTrack
    {
        std::string name;
        int channel = 0;             // 0 … 15 (9: General MIDI drums)
        std::vector<Note> notes;     // beats, as a pattern's
    };

    /** A format-1 file at 960 PPQ: a tempo track (tempo, meter, name), then a track per entry. */
    std::string writeMidi(const std::string &songName, double bpm, const std::string &sig, const std::vector<MidiOutTrack> &tracks);
}
}
