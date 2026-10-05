/*
 *  interstellar_core — the service's sound (R-AUD-5 amended, R-AUD-9): a timeline resolved into the
 *  mixer's plan, and the decoders each thread reads it through.
 *
 *  What sounds: every `#aclip` (picture is a `#clip`, sound is an `#aclip` — project-format §4) on
 *  an audio lane, either an audio-kind `#track` or a suite-schema `#atrack`: its file from `in`, its
 *  own gain and fades, its lane's gain (and an `#atrack`'s pan). A muted lane is silent; when any
 *  `#atrack` is soloed only the soloed ones sound. A camera file's dialogue is an `#aclip` naming
 *  that file (`audio clip add --src <bind>` places a rack source's sound). Buses, sends and
 *  instruments — the rest of the suite schema — parse and round-trip but sum straight to the master
 *  (said in DR-AUD-2).
 */
#include "ServiceInternal.h"
#include "Versions.h"
#include <algorithm>

namespace arstro
{
namespace interstellar
{
    bool InterstellarService::planAudio(const NodeId &tl, render::AudioPlan &out)
    {
        out = render::AudioPlan{};
        if (!mOpen) return false;
        const Project &P = *mProject;
        out.rate = P.sampleRate > 0 ? P.sampleRate : 48000;
        out.masterDb = P.masterGain;
        ResolvedTimeline R;
        std::string err;
        if (!resolved(tl, R, err)) return false;
        auto dangling = [&](const NodeId &id) {
            const auto p = R.provenance.find(id);
            return p != R.provenance.end() && p->second == Provenance::Dangling;
        };
        // the lanes: audio-kind #tracks and the suite schema's #atracks
        struct Lane { double gain = 0, pan = 0; bool mute = false, solo = false; };
        std::map<NodeId, Lane> lanes;
        bool anySolo = false;
        for (const auto &t : R.tracks)
            if (t.audio()) lanes[t.id] = Lane{t.gain, 0.0, t.mute, false};
        for (const auto &t : R.audioTracks)
        {
            lanes[t.id] = Lane{t.gain, t.pan, t.mute, t.solo};
            anySolo = anySolo || t.solo;
        }
        for (const auto &a : R.audioClips)
        {
            const auto ln = lanes.find(a.track);
            if (ln == lanes.end() || dangling(a.id) || a.src.empty()) continue;
            const Lane &l = ln->second;
            if (l.mute || (anySolo && !l.solo)) continue;
            render::AudioItem it;
            it.media = resolvePath(a.src);
            it.at = a.at;
            it.in = a.in;
            it.out = a.out;
            it.gainDb = a.gain + l.gain;
            it.fadeIn = a.fadeIn;
            it.fadeOut = a.fadeOut;
            it.pan = std::clamp(l.pan, -1.0, 1.0);
            out.items.push_back(it);
        }
        return true;
    }

    IAudioSource *InterstellarService::audioSourceFor(RenderCtx &ctx, const std::string &media, int rate)
    {
        const std::string key = media + "@" + std::to_string(rate);
        auto it = ctx.audio.find(key);
        if (it != ctx.audio.end()) return it->second.get();
        std::unique_ptr<IAudioSource> s;
        if (mHost.audioSource)
        {
            s = mHost.audioSource();
            IAudioSource::Info info;
            if (s && !s->open(media, rate, info)) s.reset();   // no audio stream: silent, remembered as such
        }
        IAudioSource *raw = s.get();
        ctx.audio[key] = std::move(s);
        return raw;
    }
}
}
