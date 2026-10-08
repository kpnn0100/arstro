/*
 *  solaris_core — Compile: a `.slp` Project → the engine's MixGraph (R-MIX, R-CLIP, R-DSP-2/3).
 *
 *  The seam between the model (text, beats, ids) and the engine (samples, indices, devices).
 *  Everything a second front end would otherwise have to re-derive to hear the same song lives
 *  here, once:
 *
 *    • strips in processing order (mixer order, then strip order — topological, R-MIX-4);
 *    • solo resolution (R-MIX-7): with any strip soloed, a strip is audible only if it is soloed,
 *      feeds a soloed strip, or is fed by one — the signal path to and from a solo stays alive;
 *    • patterns expanded: a clip longer than its pattern loops it, notes cut at the clip's end;
 *    • beats → samples at the project's tempo; dB → linear;
 *    • every stored device parameter read against the DSP registry — a choice by NAME, a number
 *      clamped to the spec; a type or a name the registry does not know is LEFT OUT of the graph
 *      and reported (the file keeps it: a newer build may know it).
 */
#pragma once
#include "MixGraph.h"
#include "Project.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace arstro
{
struct ParamSpec;
namespace solaris
{
    /** The decoded file for a clip's `src` (resolved by the caller), or null when it is offline. */
    using PcmProvider = std::function<std::shared_ptr<const engine::Pcm>(const std::string &src)>;

    struct CompileResult
    {
        engine::MixGraph graph;
        std::vector<std::string> stripIds;   // graph strip index → strip id
        std::map<std::string, std::pair<int, int>> devices; // device id → (graph strip index or −1 = master, index in its compiled rack)
        std::vector<std::string> warnings;   // devices or parameters left out, offline media
    };

    CompileResult compile(const Project &p, const PcmProvider &pcm);

    long long beatsToSamples(const Project &p, double beats);
    double secondsToBeats(const Project &p, double seconds);
    /** Strip ids that are silent: muted, or silenced by another strip's solo (R-MIX-7). */
    std::set<std::string> silentStrips(const Project &p);
    /** A clip's length in beats, resolved (the file span at the tempo, or its pattern's). */
    double clipLengthBeats(const Project &p, const Clip &c);

    /** A stored parameter text → its value per the spec (a choice by name or index). False if neither. */
    bool parseParam(const ParamSpec &spec, const std::string &text, double &value);
    /** The canonical text a value is stored as (a choice's name; a number via canonicalNumber). */
    std::string paramText(const ParamSpec &spec, double value);
}
}
