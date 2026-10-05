/*
 *  interstellar_render — AudioMix implementation. See AudioMix.h.
 */
#include "AudioMix.h"
#include "AudioSource.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace interstellar
{
namespace render
{
    namespace
    {
        inline double dbToLin(double db) { return std::pow(10.0, db / 20.0); }
        constexpr double kHalfPi = 1.57079632679489661923;
    }

    std::string AudioPlan::key() const
    {
        std::string k = std::to_string(rate) + "|" + std::to_string(masterDb);
        char b[256];
        for (const auto &i : items)
        {
            std::snprintf(b, sizeof b, "|%.9g,%.9g,%.9g,%.9g,%.6g,%.6g,%.6g,%.6g", i.at, i.in, i.out, i.speed, i.gainDb, i.fadeIn, i.fadeOut, i.pan);
            k += i.media + b;
        }
        return k;
    }

    void mixAudio(const AudioPlan &plan, const AudioSourceFor &sourceFor, long long start, int frames, float *out)
    {
        std::fill(out, out + 2 * (size_t)std::max(0, frames), 0.0f);
        if (frames <= 0 || plan.items.empty()) return;
        const double rate = plan.rate > 0 ? plan.rate : 48000;
        const double master = dbToLin(plan.masterDb);
        std::vector<float> buf;
        for (const AudioItem &it : plan.items)
        {
            const double speed = it.speed > 0 ? it.speed : 1.0;
            // the clip's own frames on the mix clock, half-open; a frame belongs to it when its
            // start is inside — the same rule a picture frame follows
            const long long c0 = (long long)std::ceil(it.at * rate - 1e-6), c1 = (long long)std::ceil(it.end() * rate - 1e-6);
            const long long a = std::max(start, c0), b = std::min(start + frames, c1);
            if (b <= a || !sourceFor) continue;
            IAudioSource *src = sourceFor(it.media);
            if (!src) continue;
            const double g = dbToLin(it.gainDb) * master;
            // balance with unity at centre: the side panned away from fades on a quarter cosine
            const double gl = g * (it.pan > 0 ? std::cos(std::min(1.0, it.pan) * kHalfPi) : 1.0);
            const double gr = g * (it.pan < 0 ? std::cos(std::min(1.0, -it.pan) * kHalfPi) : 1.0);
            const double dur = it.end() - it.at;
            auto envelope = [&](long long f) {
                const double t = (double)f / rate - it.at;
                double e = 1.0;
                if (it.fadeIn > 0 && t < it.fadeIn) e = std::max(0.0, t / it.fadeIn);
                if (it.fadeOut > 0 && t > dur - it.fadeOut) e = std::min(e, std::max(0.0, (dur - t) / it.fadeOut));
                return e;
            };
            const int n = (int)(b - a);
            if (std::fabs(speed - 1.0) < 1e-9)
            {
                // the common case: frame f of the timeline is frame f + offset of the file, exactly
                const long long offset = std::llround(it.in * rate) - c0;
                buf.assign((size_t)n * 2, 0.0f);
                src->read(a + offset, n, buf.data());
                for (int k = 0; k < n; ++k)
                {
                    const double e = envelope(a + k);
                    out[(size_t)(a - start + k) * 2] += (float)(buf[(size_t)k * 2] * gl * e);
                    out[(size_t)(a - start + k) * 2 + 1] += (float)(buf[(size_t)k * 2 + 1] * gr * e);
                }
                continue;
            }
            // varispeed: the file position advances `speed` frames per output frame, read once and
            // interpolated linearly
            auto pos = [&](long long f) { return it.in * rate + (double)(f - c0) * speed; };
            const long long s0 = (long long)std::floor(pos(a)), s1 = (long long)std::ceil(pos(b - 1)) + 2;
            const int m = (int)std::max<long long>(1, s1 - s0);
            buf.assign((size_t)m * 2, 0.0f);
            src->read(s0, m, buf.data());
            for (int k = 0; k < n; ++k)
            {
                const double p = pos(a + k) - (double)s0;
                const int i0 = std::clamp((int)std::floor(p), 0, m - 1), i1 = std::min(m - 1, i0 + 1);
                const double fr = p - std::floor(p), e = envelope(a + k);
                const double l = buf[(size_t)i0 * 2] + (buf[(size_t)i1 * 2] - buf[(size_t)i0 * 2]) * fr;
                const double r = buf[(size_t)i0 * 2 + 1] + (buf[(size_t)i1 * 2 + 1] - buf[(size_t)i0 * 2 + 1]) * fr;
                out[(size_t)(a - start + k) * 2] += (float)(l * gl * e);
                out[(size_t)(a - start + k) * 2 + 1] += (float)(r * gr * e);
            }
        }
    }
}
}
}
