/*
 *  solaris_core — Notation: how a person (or an agent) writes music in a command line (R-SVC-8).
 *
 *  Pure text ↔ numbers, no project, no service — so the grammar, the API document and the tests
 *  all read the same tables:
 *
 *      pitch      60 · C4 · F#3 · Bb2 · c-1          C4 = 60 (MIDI), c-1 = 0, G9 = 127
 *      note       <pitch>@<beat>[:<length>[:<vel>]]  C4@0:0.5:90 · kick@1 · E4@2::70 (length left default)
 *      steps      x...x...X..x....                   x = a note, X = an accent (127), . or - = a rest;
 *                                                    spaces and | are ignored ("x... x... | x...")
 *      chord      <root><quality>                    C · Cm7 · F#dim · Bbmaj7 · Gsus4 — `chordQualities()`
 *
 *  A kit's pad names (`kick`, `closed-hat`) are a pitch too, but they belong to an instrument, so
 *  the SERVICE resolves them (`SolarisService::pitchOf`) from the DSP registry's note names; here a
 *  name is only compared, case and spaces/hyphens ignored (`padKey`).
 */
#pragma once
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
    /** "C4" → 60; letter A–G (any case), any number of `#`/`b`, an octave −1…9. False if not a name
     *  or outside 0…127 (`err` says which). */
    bool parsePitchName(const std::string &text, int &midi, std::string &err);
    /** 0…127 → "C4", sharps ("C#4"). */
    std::string pitchName(int midi);
    /** A pad name compared: lower case, spaces / `-` / `_` dropped ("Closed Hat" → "closedhat"). */
    std::string padKey(const std::string &name);
    /** A pad name printed: lower case, spaces → `-` ("Closed Hat" → "closed-hat"). */
    std::string padText(const std::string &name);

    struct ChordQuality
    {
        std::vector<std::string> names; // the spellings it answers to, the first is the canonical one
        std::vector<int> intervals;     // semitones above the root, ascending
        std::string label;              // "minor seventh"
    };
    /** Every chord `--chord` knows, in the order the API document lists them. */
    const std::vector<ChordQuality> &chordQualities();
    /** "Cm7" at `octave` (the root's: 4 → C4 = 60), `inversion` lowest notes raised an octave →
     *  MIDI pitches, ascending. False with `err` naming the vocabulary or the range. */
    bool chordPitches(const std::string &chord, int octave, int inversion, std::vector<int> &out, std::string &err);

    struct NoteToken
    {
        std::string pitch;              // as written — resolved by the caller (a number, a name or a pad)
        double at = 0, length = 0;      // beats
        int vel = 0;
        bool hasLength = false, hasVel = false;
    };
    /** `<pitch>@<beat>[:<length>[:<vel>]]`; an empty length or velocity keeps the default. */
    bool parseNoteToken(const std::string &token, NoteToken &out, std::string &err);
    /** A step string → one entry per step: 0 = rest, 1 = a note, 2 = an accent. */
    bool parseSteps(const std::string &text, std::vector<int> &steps, std::string &err);
}
}
