#pragma once
/*
 *  TempoMap — where each beat of the song falls in samples, and back (R-TEMPO-1). Plain data, as the rest of the
 *  graph: segments of a constant tempo, or of one gliding linearly (in beats) from one tempo to the next.
 *
 *  A song of one tempo is ONE constant segment, and every conversion is then the very expression the engine used
 *  before there was a map — beat · (60 / bpm · rate) — so such a song renders to the same bytes.
 *
 *  (T1) constant:  s(b) = s0 + (b − b0)·S,  S = 60/bpm·rate;           b(s) = b0 + (s − s0)/S
 *  (T2) a glide from bpm0 at b0 to bpm1 at b1, k = (bpm1 − bpm0)/(b1 − b0), bpm(b) = bpm0 + k(b − b0):
 *       s(b) = s0 + rate·(60/k)·ln(bpm(b)/bpm0);                         b(s) = b0 + (bpm0/k)·(e^(k·(s − s0)/(60·rate)) − 1)
 *  Before the first segment the first tempo holds; after the last, the last.
 */
#include <cmath>
#include <vector>

namespace arstro
{
namespace solaris
{
namespace engine
{
    struct TempoMap
    {
        struct Seg
        {
            double beat0 = 0.0, sample0 = 0.0;
            double bpm0 = 120.0, bpm1 = 120.0; // equal: constant (T1); else a glide to bpm1 at `beat1` (T2)
            double beat1 = 0.0;                // where a glide ends (the next segment's start)
        };
        double rate = 48000.0;
        std::vector<Seg> segs{Seg{}};

        static TempoMap constant(double bpm, double rate)
        {
            TempoMap m;
            m.rate = rate;
            m.segs[0].bpm0 = m.segs[0].bpm1 = bpm;
            return m;
        }
        bool isConstant() const { return segs.size() == 1 && segs[0].bpm0 == segs[0].bpm1; }

        /** A tempo from `beat` on: `bpm` held from there (glide = false), or reached there gliding from the tempo
         *  before (glide = true). Points are added in rising beats; the first is the song's at beat 0. */
        void add(double beat, double bpm, bool glide)
        {
            Seg &last = segs.back();
            if (beat <= last.beat0) { last.bpm0 = last.bpm1 = bpm; return; } // at the same beat: it is the tempo there
            if (glide)
            {
                last.bpm1 = bpm; // the segment before glides to it
                last.beat1 = beat;
            }
            Seg s;
            s.beat0 = beat;
            s.sample0 = sampleAt(beat);
            s.bpm0 = s.bpm1 = bpm;
            segs.push_back(s);
        }

        const Seg &segAtBeat(double beat) const
        {
            size_t i = 0;
            while (i + 1 < segs.size() && segs[i + 1].beat0 <= beat) ++i;
            return segs[i];
        }
        const Seg &segAtSample(double sample) const
        {
            size_t i = 0;
            while (i + 1 < segs.size() && segs[i + 1].sample0 <= sample) ++i;
            return segs[i];
        }

        double sampleAt(double beat) const
        {
            const Seg &s = segAtBeat(beat);
            if (s.bpm0 == s.bpm1 || beat <= s.beat0) return s.sample0 + (beat - s.beat0) * (60.0 / s.bpm0 * rate); // (T1)
            const double k = (s.bpm1 - s.bpm0) / (s.beat1 - s.beat0);
            return s.sample0 + rate * (60.0 / k) * std::log((s.bpm0 + k * (beat - s.beat0)) / s.bpm0); // (T2)
        }
        double beatAt(double sample) const
        {
            const Seg &s = segAtSample(sample);
            if (s.bpm0 == s.bpm1 || sample <= s.sample0) return s.beat0 + (sample - s.sample0) / (60.0 / s.bpm0 * rate); // (T1)
            const double k = (s.bpm1 - s.bpm0) / (s.beat1 - s.beat0);
            return s.beat0 + (s.bpm0 / k) * (std::exp(k * (sample - s.sample0) / (60.0 * rate)) - 1.0); // (T2)
        }
        double bpmAt(double beat) const
        {
            const Seg &s = segAtBeat(beat);
            if (s.bpm0 == s.bpm1 || beat <= s.beat0) return s.bpm0;
            return s.bpm0 + (s.bpm1 - s.bpm0) * (beat - s.beat0) / (s.beat1 - s.beat0);
        }
        double samplesPerBeatAt(double beat) const { return 60.0 / bpmAt(beat) * rate; }
    };
}
}
}
