#include "PluginDevice.h"
#include "Engine.h"
#include <algorithm>
#include <array>
#include <cstdlib>

namespace arstro
{
namespace solaris
{
    bool pluginParamId(const std::string &name, uint32_t &id)
    {
        if (name.size() < 2 || name[0] != 'p' || name.find_first_not_of("0123456789", 1) != std::string::npos) return false;
        id = (uint32_t)std::strtoul(name.c_str() + 1, nullptr, 10);
        return true;
    }

    void setPluginType(PluginConnection &c, const std::vector<std::string> &storedNames)
    {
        c.type = DeviceType{};
        c.type.name = "vst3";
        c.type.label = c.name.empty() ? "VST3 plugin" : c.name;
        c.type.kind = c.instrument ? DeviceKind::Instrument : DeviceKind::Effect;
        c.type.summary = "another maker's plugin (VST3), played in a process of its own";
        if (c.inst)
            for (const auto &p : c.inst->params())
            {
                ParamSpec s;
                s.name = "p" + std::to_string(p.id);
                s.label = p.title.empty() ? s.name : p.title;
                s.unit = p.units;
                s.min = 0.0;
                s.max = 1.0;
                s.def = std::min(1.0, std::max(0.0, p.def));
                c.type.params.push_back(s);
            }
        else
            for (const auto &n : storedNames) // offline from the start: what the song stores, so its bindings still name something
            {
                ParamSpec s;
                s.name = s.label = n;
                c.type.params.push_back(s);
            }
    }

    namespace
    {
        constexpr int kChunk = 4096;
        constexpr int kMaxNotes = 512, kMaxChanges = 512;

        class PluginDevice : public Device
        {
        public:
            PluginDevice(std::shared_ptr<PluginConnection> c, bool live) : Device(c->type), mC(std::move(c)), mLive(live)
            {
                // sized here, never on the audio thread; nothing is sent until a value is SET (the plugin's own state
                // holds the rest — a default pushed over it would undo it)
                mDirty.assign(type().params.size(), 0);
                mIds.assign(type().params.size(), 0);
                for (size_t i = 0; i < mIds.size(); ++i) pluginParamId(type().params[i].name, mIds[i]);
                for (auto *b : {&mInL, &mInR, &mOutL, &mOutR}) b->assign(kChunk, 0.0f);
                // its latency for this engine's life — what the engine compensates; offline, the dry signal takes it too
                mLat = mC->inst && !mC->inst->offline() ? std::max(0, mC->inst->latency()) : 0;
                for (auto &d : mDry) d.assign((size_t)mLat, 0.0);
            }

            void process(Sample *const *io, int channels, int frames) override
            {
                IPluginInstance *p = mC->inst.get();
                if (!p || p->offline()) { offline(io, channels, 0, frames); return; } // silent / dry (R-VST-6)
                const int step = std::max(1, std::min(kChunk, mC->maxBlock));
                for (int at = 0; at < frames; at += step)
                {
                    const int n = std::min(step, frames - at);
                    int nc = 0;
                    for (size_t i = 0; i < mDirty.size() && nc < kMaxChanges; ++i)
                        if (mDirty[i])
                        {
                            mChanges[(size_t)nc++] = PluginChange{mIds[i], 0, param((int)i)};
                            mDirty[i] = 0;
                        }
                    float *in[2] = {mInL.data(), mInR.data()}, *out[2] = {mOutL.data(), mOutR.data()};
                    for (int i = 0; i < n; ++i)
                    {
                        mInL[(size_t)i] = channels > 0 ? (float)io[0][at + i] : 0.0f;
                        mInR[(size_t)i] = channels > 1 ? (float)io[1][at + i] : mInL[(size_t)i];
                    }
                    // live: the block's own duration (an engine block's at least) — later is an overrun; a render waits
                    const double budget = mLive ? (double)std::max(n, engine::Engine::kBlock) / std::max(1, mC->rate) : 60.0;
                    const bool ok = p->process(in, out, n, mReset, mNoteBuf.data(), mNotes, mChanges.data(), nc, budget);
                    mReset = false;
                    mNotes = 0;
                    if (!ok) { offline(io, channels, at, frames); return; } // offline now: the rest silent / dry
                    for (int c = 0; c < channels && c < 2; ++c)
                        for (int i = 0; i < n; ++i)
                        {
                            const double v = (double)(c == 0 ? mOutL : mOutR)[(size_t)i];
                            io[c][at + i] = isInstrument() ? io[c][at + i] + v : v; // an instrument adds; an effect replaces
                        }
                }
            }
            void noteOn(int note, int velocity) override
            {
                queue(note, true, std::min(1.0f, std::max(0.0f, (float)velocity / 127.0f)));
                if (note >= 0 && note < 128) mHeld[(size_t)note] = 1;
            }
            void noteOff(int note) override
            {
                queue(note, false, 0.0f);
                if (note >= 0 && note < 128) mHeld[(size_t)note] = 0;
            }
            void allNotesOff() override
            {
                for (int k = 0; k < 128; ++k)
                    if (mHeld[(size_t)k]) noteOff(k);
            }
            void reset() override
            {
                mReset = true;
                mNotes = 0;
                mHeld.fill(0);
            }
            int latency() const override { return mLat; }

        protected:
            void apply(int index, double) override
            {
                if (index >= 0 && index < (int)mDirty.size()) mDirty[(size_t)index] = 1;
            }

        private:
            /** Offline: an instrument adds nothing; an effect leaves its input — `mLat` late, as the engine expects it. */
            void offline(Sample *const *io, int channels, int from, int to)
            {
                mNotes = 0;
                if (isInstrument() || mLat == 0) return;
                for (int i = from; i < to; ++i)
                {
                    for (int c = 0; c < channels && c < 2; ++c)
                    {
                        const double y = mDry[c][(size_t)mDryAt];
                        mDry[c][(size_t)mDryAt] = io[c][i];
                        io[c][i] = y;
                    }
                    mDryAt = (mDryAt + 1) % mLat;
                }
            }
            void queue(int note, bool on, float velocity)
            {
                if (mNotes < kMaxNotes) mNoteBuf[(size_t)mNotes++] = PluginNote{0, note, on, velocity}; // at the next block's start
            }

            std::shared_ptr<PluginConnection> mC;
            bool mLive;
            std::vector<char> mDirty;
            std::vector<uint32_t> mIds;
            std::vector<float> mInL, mInR, mOutL, mOutR;
            std::array<PluginNote, kMaxNotes> mNoteBuf{};
            std::array<PluginChange, kMaxChanges> mChanges{};
            std::array<char, 128> mHeld{};
            int mNotes = 0;
            bool mReset = false;
            int mLat = 0, mDryAt = 0;
            std::vector<double> mDry[2];
        };
    }

    std::unique_ptr<Device> makePluginDevice(std::shared_ptr<PluginConnection> c, bool live)
    {
        return std::make_unique<PluginDevice>(std::move(c), live);
    }
}
}
