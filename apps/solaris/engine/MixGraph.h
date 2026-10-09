/*
 *  solaris_engine — MixGraph: what the engine renders, as plain data (R-MIX, R-PLAY-1, R-RENDER-1).
 *
 *  The engine KNOWS NO PROJECT and no file: the core compiles a `.slp` into this — strips in
 *  processing order, each with its devices (DSP registry types + values), its audio regions in
 *  samples (decoded PCM handed in), its note events in samples, its output and its sends as
 *  indices — and the engine renders it. A second front end, a test or a benchmark can build one
 *  by hand.
 *
 *  The one structural promise: **every strip target points FORWARD** (a later strip index, the
 *  master, or a port). The model guarantees it (R-MIX-4); the engine checks it when it is built
 *  and refuses a graph that breaks it, because processing in index order is only correct then.
 */
#pragma once
#include "Expr.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace solaris
{
namespace engine
{
    /** Decoded audio at the graph's sample rate. Interleaved; 1 or 2 channels. */
    struct Pcm
    {
        int channels = 2;
        long long frames = 0;
        std::vector<float> samples;
        float at(long long frame, int ch) const
        {
            return samples[(size_t)(frame * channels + (channels == 1 ? 0 : ch))];
        }
    };

    struct Target
    {
        enum Kind { Master, Strip, Port, None } kind = Master;
        int index = -1; // a strip index (later than the source) or a port index
    };

    struct DeviceDesc
    {
        std::string id, type;                               // a DSP registry type
        std::vector<std::pair<std::string, double>> params; // registry names, engineering units
        bool bypass = false;
    };

    /** An audio clip, in samples. */
    struct Region
    {
        std::string clip;
        std::shared_ptr<const Pcm> pcm;   // null = offline media: the region is silent
        long long start = 0;              // timeline sample where it begins
        long long frames = 0;             // timeline samples it lasts
        long long srcOffset = 0;          // source frame at `start`
        long long srcFrames = 0;          // source span; with `loop` the region repeats it
        bool loop = false;
        double gain = 1.0;                // linear
        long long fadeIn = 0, fadeOut = 0; // samples
    };

    struct NoteEvent
    {
        long long at = 0;
        int note = 60, velocity = 100;
        bool on = true;
    };

    struct Send
    {
        Target to;
        double gain = 1.0; // linear
        bool pre = false;
        bool key = false;  // R-MIX-15: into the target strip's KEY — what its compressors detect on — not its input
    };

    struct Strip
    {
        std::string id;
        enum Kind { Audio, Instrument, Bus } kind = Audio;
        double gain = 1.0;                // linear
        double pan = 0.0;                 // −1 … +1, balance law
        bool silent = false;              // muted, or silenced by another strip's solo (the core decides)
        bool keyLive = true;              // its sidechain keys still run while silent — false only when muted
        std::vector<DeviceDesc> rack;     // an instrument strip's rack[0] is its instrument
        std::vector<Region> regions;      // audio strips
        std::vector<NoteEvent> notes;     // instrument strips; sorted by `at`, offs before ons
        Target out;
        std::vector<Send> sends;
    };

    struct Port
    {
        std::string id, name;
        int channels = 2;
    };

    struct MixGraph
    {
        int sampleRate = 48000;
        std::vector<Strip> strips;        // in processing order
        std::vector<DeviceDesc> masterRack;
        double masterGain = 1.0;          // linear
        std::vector<int> masterPorts;     // port indices the master feeds
        std::vector<Port> ports;
        long long end = 0;                // the last sample anything is scheduled at (the song's length)
        // automation and formulas (R-AUTO-7): evaluated every Engine::kControl samples at absolute positions
        Clock clock;
        std::vector<Curve> curves;        // automation curves, slot kClockSlots + i
        std::vector<Bind> binds;          // in evaluation order: a link reads only an earlier one
    };
}
}
}
