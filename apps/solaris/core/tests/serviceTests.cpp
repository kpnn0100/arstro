// solaris_service tests — the tables (L1) and the REAL service driven through text lines with a
// fake decoder and WAV writer (L2) (R-SVC-1…3, R-MIX-2/3/4/7/8/9/10, R-CLIP-2/3, R-RENDER-1…3,
// R-API-1). Nothing reaches past `dispatchText`: what a test can do, the CLI and the UI can do.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "ApiDoc.h"
#include "AppModelCodec.h"
#include "Command.h"
#include "Event.h"
#include "device/Device.h"
#include "Compile.h"
#include "FaderLaw.h"
#include "Format.h"
#include "KeyCombo.h"
#include "Keymap.h"
#include "Notation.h"
#include "SolarisService.h"
#include "Surface.h"
#include "meter/Meters.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <string>
#include <tuple>

using namespace arstro::solaris;
namespace fs = std::filesystem;

// A counting allocator: off except inside the window a test opens, and counting only threads other than
// the test's own — the player's audio thread (R-PLAY-2: it never allocates).
static std::atomic<long> gThreadAllocs{0};
static std::atomic<bool> gCountThreads{false};
static std::thread::id gMainThread;
void *operator new(size_t n)
{
    if (gCountThreads.load(std::memory_order_relaxed) && std::this_thread::get_id() != gMainThread)
        gThreadAllocs.fetch_add(1, std::memory_order_relaxed);
    if (void *p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }

namespace
{
    int passed = 0;
    void pass(const char *name)
    {
        std::printf("[PASS] %s\n", name);
        ++passed;
    }
    bool contains(const std::string &s, const std::string &part) { return s.find(part) != std::string::npos; }
    // a creating command's FIRST line is its id (R-SVC-8: `made:` follows with whatever else it made)
    std::string firstLine(const std::string &s) { return s.substr(0, s.find('\n')); }

    std::string scratch()
    {
        const char *t = std::getenv("SOLARIS_TEST_DIR");
        fs::path d = t ? fs::path(t) : fs::temp_directory_path() / "solaris_service_tests";
        fs::create_directories(d);
        return d.string();
    }

    // The fake host: any path decodes to half a second of a 1 kHz sine at 0.5, except one that
    // says "missing"; written WAVs are kept in memory.
    // A clock device that keeps what it is given: frames land in `sink`, counted in `frames`
    // (read the sink only after `transport stop` has joined the player).
    struct Capture
    {
        std::vector<float> sink;
        std::atomic<long long> frames{0};
    };
    /** R-DEV-1/2/4: a device paced in REAL time on its own crystal — `ppm` off the nominal rate — that reports
     *  `latency` plus what it holds, and blocks a write while it holds more than two blocks. Every block it is given is
     *  kept with WHEN it will be heard, so a test can say when any frame it played was heard. */
    struct Paced
    {
        double ppm = 0, latency = 0;
        std::string fails;             // non-empty: it will not open, saying this
        int ch = 0, rate = 0, block = 0;
        bool opened = false, started = false;
        double next = 0;               // seconds (steady): when the next frame given will start to be heard, less `latency`
        long long frames = 0, xruns = 0;
        std::vector<float> sink;       // what it played, interleaved by `ch`
        std::vector<std::pair<long long, double>> blocks; // (its first frame, when that frame is heard)
        double heardAt(long long k) const
        {
            for (size_t b = blocks.size(); b-- > 0;)
                if (blocks[b].first <= k) return blocks[b].second + (double)(k - blocks[b].first) / (rate * (1 + ppm * 1e-6));
            return -1;
        }
    };
    double steadyNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    struct FakeOut : IAudioOut
    {
        std::shared_ptr<Capture> cap;
        const double *lat = nullptr;   // the latency it reports (R-REC-1's loopback sets one)
        std::shared_ptr<std::map<std::string, Paced>> devices; // R-DEV-1: devices paced in real time, by id
        Paced *paced = nullptr;
        std::atomic<long long> runDry{0}; // its underruns — read from any thread, as the seam says
        bool open(const std::string &dev, int rate, int ch, int block, std::string &err) override
        {
            if (devices && devices->count(dev))
            {
                paced = &(*devices)[dev];
                if (!paced->fails.empty()) { err = paced->fails; return false; }
                paced->opened = true;
                paced->ch = ch;
                paced->rate = rate;
                paced->block = block;
            }
            return true;
        }
        bool write(const float *x, int n) override
        {
            if (paced)
            {
                Paced &p = *paced;
                const double trueRate = p.rate * (1 + p.ppm * 1e-6), hold = 2.0 * p.block / trueRate;
                const double now = steadyNow();
                if (p.next < now) { if (p.started) { ++p.xruns; runDry.fetch_add(1); } p.next = now; } // it ran dry: an underrun
                while (p.next - steadyNow() > hold) std::this_thread::sleep_for(std::chrono::microseconds(100));
                p.blocks.emplace_back(p.frames, p.next + p.latency);
                p.sink.insert(p.sink.end(), x, x + (size_t)n * p.ch);
                p.frames += n;
                p.next += n / trueRate;
                p.started = true;
                return true;
            }
            cap->sink.insert(cap->sink.end(), x, x + 2 * n);
            cap->frames.fetch_add(n);
            std::this_thread::sleep_for(std::chrono::microseconds(300)); // a fast, but not runaway, device
            return true;
        }
        double latency() override
        {
            if (paced) return paced->latency + std::max(0.0, paced->next - steadyNow());
            return lat ? *lat : 0.0;
        }
        long long xruns() const override { return paced ? runDry.load() : -1; }
    };
    /** R-REC-1: a capture device. "loop": a cable from the clock device back into it — frame j read is the frame the
     *  output was given (out + in latency) × rate frames before, both counted from when it began (the one audio thread
     *  reads and writes in lock-step). "tone": a 1 kHz sine at 0.5. */
    /** R-DEV-1/2: a capture paced in REAL time on its own crystal (`ppm`): a 1 kHz tone at 0.5, landing a block at a
     *  time into a ring the reader takes from; it says what it holds and when its last block landed (`held`). */
    struct PacedIn
    {
        double ppm = 0, latency = 0;
        int block = 256;
    };
    struct FakeIn : IAudioIn
    {
        std::shared_ptr<std::map<std::string, PacedIn>> pacedIns; // R-DEV-1: by device
        const PacedIn *paced = nullptr;
        double t0 = 0;               // steady seconds when it opened
        long long consumed = 0;      // frames read
        long long landed(double now) const // frames landed by `now`: whole blocks
        {
            const double trueRate = rate * (1 + paced->ppm * 1e-6);
            return (long long)std::floor((now - t0) * trueRate / paced->block) * paced->block;
        }
        bool held(long long &frames, long long &lastNs, int &lastFrames) const override
        {
            if (!paced) return false;
            const double now = steadyNow(), trueRate = rate * (1 + paced->ppm * 1e-6);
            const long long l = landed(now);
            if (l <= 0) return false;
            frames = l - consumed;
            lastNs = (long long)std::llround((t0 + (double)l / trueRate) * 1e9);
            lastFrames = paced->block;
            return true;
        }
        std::shared_ptr<Capture> cap;
        std::string mode = "loop", *device = nullptr;
        bool fails = false; // R-DEV-9: a PipeWire capture that links nowhere
        double inLat = 0, outLat = 0;
        long long delay = 0, start = -1, got = 0, period = 0; // period: "steps" — a level per pass of this many frames
        int ch = 2, rate = 48000;
        bool open(const std::string &dev, int r, int channels, int, std::string &err) override
        {
            if (fails) { err = "PipeWire: the stream linked to no input within 2 s"; return false; }
            ch = channels;
            rate = r;
            if (pacedIns && pacedIns->count(dev)) { paced = &pacedIns->at(dev); t0 = steadyNow(); }
            delay = std::llround((outLat + inLat) * r);
            if (device) *device = dev;
            return true;
        }
        int read(float *x, int n) override
        {
            if (paced) // its own crystal: what has landed, then silence
            {
                const double trueRate = rate * (1 + paced->ppm * 1e-6);
                const long long have = landed(steadyNow()) - consumed;
                const int k = (int)std::max(0LL, std::min<long long>(n, have));
                for (int i = 0; i < n; ++i)
                {
                    const float v = i < k ? (float)(0.5 * std::sin(2 * M_PI * 1000.0 * (double)(consumed + i) / trueRate)) : 0.0f;
                    for (int c = 0; c < ch; ++c) x[(size_t)i * ch + c] = v;
                }
                consumed += k;
                return k;
            }
            if (start < 0) start = cap->frames.load(); // the output frame being written as it began
            for (int i = 0; i < n; ++i)
            {
                float l = 0, r = 0;
                const long long j = start + got + i - delay;
                if (mode == "tone") l = r = (float)(0.5 * std::sin(2 * M_PI * 1000.0 * (double)(got + i) / rate));
                else if (mode == "steps" && period > 0) // R-REC-2: 0.1 the first pass HEARD, 0.2 the next … (a pass is `period`)
                    l = r = 0.1f * (float)(1 + std::max(0LL, (got + i - delay) / period));
                else if (j >= 0 && (size_t)(2 * j + 1) < cap->sink.size()) { l = cap->sink[(size_t)(2 * j)]; r = cap->sink[(size_t)(2 * j + 1)]; }
                x[(size_t)i * ch] = l;
                if (ch > 1) x[(size_t)i * ch + 1] = r;
            }
            got += n;
            return n;
        }
        double latency() override
        {
            if (paced) return paced->latency + (double)std::max(0LL, landed(steadyNow()) - consumed) / rate; // its device's, and its ring
            return inLat;
        }
    };
    /** R-DEV-9: a PipeWire stream that opens and has had three underruns — or one that links nowhere. */
    struct PipewireOut : FakeOut
    {
        bool fails = false;
        bool open(const std::string &, int, int, int, std::string &err) override
        {
            if (fails) err = "PipeWire: the stream linked to no output within 2 s";
            return !fails;
        }
        long long xruns() const override { return 3; }
    };

    /** R-MIDI-2: a keyboard the test plays: `open` hands the sink over; the test calls it as the host's thread would. */
    struct FakeMidi : IMidiIn
    {
        std::shared_ptr<std::function<void(const MidiEvent &)>> sink, surface; // R-CTRL-2: the surface's port its own
        std::vector<std::string> on;
        bool isSurface = false;
        bool open(const std::vector<std::string> &ports, std::function<void(const MidiEvent &)> s, std::string &) override
        {
            isSurface = ports.size() == 1 && ports[0] == "X-Touch";
            *(isSurface ? surface : sink) = std::move(s);
            on = ports.empty() ? std::vector<std::string>{"Keys", "Pads"} : ports;
            return true;
        }
        void close() override { *(isSurface ? surface : sink) = nullptr; }
        std::vector<std::string> listening() const override { return on; }
    };
    /** R-CTRL-2: the surface's output — every byte it was sent. */
    struct FakeMidiOut : IMidiOut
    {
        std::shared_ptr<std::vector<uint8_t>> sent;
        bool open(const std::string &port, std::string &err) override
        {
            if (port != "X-Touch") { err = "no MIDI port `" + port + "` takes output"; return false; }
            return true;
        }
        void send(const std::vector<uint8_t> &b) override { sent->insert(sent->end(), b.begin(), b.end()); }
        void close() override {}
    };

    // R-VST-6: other makers' plugins, played "elsewhere" — here in process, through the same seam. Sweet Pad sounds a
    // sine per held note at 0.4 × Level (p1, 0.5); Air Verb passes its input × 2·Gain (p7, 0.5: unity), `latency`
    // samples late and saying so. One named by `crash` fails its next block of sound (offline, "it crashed (test)"); its state is
    // its values as text; every state set is kept.
    struct PluginWorld
    {
        int started = 0, latency = 0;
        std::string crash;                 // a class id
        bool liveOnMain = false;           // a block with a live budget processed on the service's thread (a warm-up would)
        // N26: its editor — the title it was opened with; no display; what it will say on its own; what it was sent
        std::string editorTitle;
        bool noDisplay = false;
        std::vector<PluginEvent> say;
        std::vector<std::pair<uint32_t, double>> received;
        std::vector<std::string> statesSet;
    };
    class FakePlugin : public IPluginInstance
    {
    public:
        FakePlugin(std::shared_ptr<PluginWorld> w, const std::string &uid, double rate) : w(std::move(w)), uid(uid), rate(rate)
        {
            inst = uid == "0123456789ABCDEF0123456789ABCDEF";
            ps = inst ? std::vector<PluginParamInfo>{{1, "Level", "", 0.5, 0}, {2, "Tone", "%", 0.25, 0}}
                      : std::vector<PluginParamInfo>{{7, "Gain", "dB", 0.5, 0}};
            for (const auto &p : ps) values[p.id] = p.def;
            lat = inst ? 0 : this->w->latency;
            for (auto &r : ring) r.assign((size_t)lat + 1, 0.0f);
        }
        bool isInstrument() const override { return inst; }
        int latency() const override { return lat; }
        const std::vector<PluginParamInfo> &params() const override { return ps; }
        bool process(const float *const *in, float *const *out, int frames, bool reset, const PluginNote *notes, int nNotes,
                     const PluginChange *changes, int nChanges, double budget) override
        {
            if (frames > 0 && budget < 1.0 && std::this_thread::get_id() == gMainThread) w->liveOnMain = true;
            if (dead) return false;
            if (w->crash == uid && frames > 0) { dead = true; return false; } // at a block: after its engine was built
            if (reset) held.clear();
            for (int k = 0; k < nChanges; ++k)
            {
                values[changes[k].id] = changes[k].value;
                if (frames > 0) w->received.emplace_back(changes[k].id, changes[k].value);
            }
            for (int k = 0; k < nNotes; ++k)
                if (notes[k].on) held[notes[k].pitch] = 0.0;
                else held.erase(notes[k].pitch);
            for (int i = 0; i < frames; ++i)
            {
                if (inst)
                {
                    double v = 0;
                    for (auto &h : held)
                    {
                        v += std::sin(h.second);
                        h.second += 2 * M_PI * 440.0 * std::pow(2.0, (h.first - 69) / 12.0) / rate;
                    }
                    out[0][i] = out[1][i] = (float)(0.4 * values[1] * v);
                    continue;
                }
                for (int c = 0; c < 2; ++c)
                {
                    ring[c][(size_t)at] = in[c][i];
                    out[c][i] = (float)(2.0 * values[7]) * ring[c][(size_t)((at + 1) % (lat + 1))]; // `lat` late
                }
                at = (at + 1) % (lat + 1);
            }
            return true;
        }
        bool offline() const override { return dead; }
        std::string why() const override { return dead ? "it crashed (test)" : std::string(); }
        bool openEditor(const std::string &title, std::string &err) override
        {
            if (w->noDisplay) { err = "there is no display to open it on"; return false; }
            w->editorTitle = title;
            editor = true;
            return true;
        }
        bool closeEditor(std::string &) override { editor = false; return true; }
        bool editorOpen() const override { return editor && !dead; }
        void takeEvents(std::vector<PluginEvent> &out) override
        {
            if (!editor) return; // only the one whose editor is open speaks
            for (const auto &e : w->say)
            {
                if (e.kind == PluginEvent::Perform) values[e.id] = e.value; // its editor set it
                if (e.kind == PluginEvent::EditorClosed) editor = false;
                out.push_back(e);
            }
            w->say.clear();
        }
        bool getState(std::string &s, std::string &) override
        {
            s.clear();
            for (const auto &v : values) s += std::to_string(v.first) + ":" + canonicalNumber(v.second) + ",";
            return true;
        }
        bool setState(const std::string &s, std::string &) override
        {
            w->statesSet.push_back(s);
            std::stringstream ss(s);
            std::string kv;
            while (std::getline(ss, kv, ','))
                if (kv.find(':') != std::string::npos) values[(uint32_t)std::stoul(kv)] = std::stod(kv.substr(kv.find(':') + 1));
            return true;
        }

    private:
        std::shared_ptr<PluginWorld> w;
        std::string uid;
        double rate;
        bool inst = false, dead = false, editor = false;
        int lat = 0, at = 0;
        std::vector<PluginParamInfo> ps;
        std::map<uint32_t, double> values;
        std::map<int, double> held;  // pitch → phase
        std::vector<float> ring[2];
    };

    struct Fake
    {
        std::string settings, recents; // "" = not persisted
        std::shared_ptr<PluginWorld> plugins = std::make_shared<PluginWorld>();
        std::shared_ptr<Capture> capture = std::make_shared<Capture>();
        std::shared_ptr<std::map<std::string, Paced>> devices = std::make_shared<std::map<std::string, Paced>>(); // R-DEV-1
        std::shared_ptr<std::map<std::string, PacedIn>> pacedIns = std::make_shared<std::map<std::string, PacedIn>>();
        bool canPlay = true;
        std::string pipewire;          // R-DEV-9: "" this host has no PipeWire driver, "ok" it opens, "fail" it links nowhere
        double outLatency = 0, inLatency = 0; // R-REC-1: what the clock device and the capture report
        long long inPeriod = 0;                // R-REC-2: "steps" — a pass's frames
        bool canRecord = true;
        std::string inMode = "loop", inDevice; // R-REC-1: the capture's sound; the device it was opened on
        std::shared_ptr<std::function<void(const MidiEvent &)>> midi = std::make_shared<std::function<void(const MidiEvent &)>>();
        void key(uint8_t status, uint8_t d1, uint8_t d2) const { if (*midi) (*midi)(MidiEvent{status, d1, d2}); }
        // R-CTRL-2: a surface on port "X-Touch" (listed only when a test asks), what it sends us and what we send it
        bool withSurface = false;
        std::shared_ptr<std::function<void(const MidiEvent &)>> surfaceIn = std::make_shared<std::function<void(const MidiEvent &)>>();
        std::shared_ptr<std::vector<uint8_t>> surfaceOut = std::make_shared<std::vector<uint8_t>>();
        void surfaceKey(uint8_t status, uint8_t d1, uint8_t d2) const { if (*surfaceIn) (*surfaceIn)(MidiEvent{status, d1, d2}); }
        std::map<std::string, std::vector<std::vector<float>>> written;
        std::map<std::string, int> bits, rates, kbps;
        std::map<std::string, std::string> formats;   // R-EXPORT-1: what each file was written as
        SolarisService::Host host()
        {
            SolarisService::Host h;
            h.decodeAudio = [this](const std::string &path, int rate, engine::Pcm &out, std::string &err) {
                if (contains(path, "missing")) { err = "no such file"; return false; }
                if (written.count(path)) // R-REC-2: a file this host wrote (a recording) is what it wrote
                {
                    const auto &ch = written[path];
                    out.channels = 2;
                    out.frames = (long long)ch[0].size();
                    out.samples.resize((size_t)out.frames * 2);
                    for (long long i = 0; i < out.frames; ++i)
                    {
                        out.samples[(size_t)i * 2] = ch[0][(size_t)i];
                        out.samples[(size_t)i * 2 + 1] = ch.size() > 1 ? ch[1][(size_t)i] : ch[0][(size_t)i];
                    }
                    return true;
                }
                out.channels = 2;
                out.frames = rate / 2;
                out.samples.resize((size_t)out.frames * 2);
                for (long long i = 0; i < out.frames; ++i)
                    out.samples[(size_t)i * 2] = out.samples[(size_t)i * 2 + 1] = (float)(0.5 * std::sin(2 * M_PI * 1000.0 * i / rate));
                return true;
            };
            h.writeWav = [this](const std::string &path, const std::vector<std::vector<float>> &ch, int rate, int b, std::string &) {
                written[path] = ch;
                bits[path] = b;
                rates[path] = rate;
                formats[path] = "wav";
                return true;
            };
            h.writeAudio = [this](const std::string &path, const std::vector<std::vector<float>> &ch, int rate, const std::string &f, int b,
                                  int k, std::string &) {
                written[path] = ch;
                bits[path] = b;
                rates[path] = rate;
                kbps[path] = k;
                formats[path] = f;
                return true;
            };
            h.listDir = [](const std::string &path, std::vector<BrowserEntry> &out, std::string &err) {
                if (contains(path, "nowhere")) { err = "no such folder"; return false; }
                out = {BrowserEntry{"kick.wav", path + "/kick.wav", "audio"}, BrowserEntry{"Loops", path + "/Loops", "dir"},
                       BrowserEntry{"notes.txt", path + "/notes.txt", "other"}, BrowserEntry{"a.slp", path + "/a.slp", "song"}};
                return true;
            };
            h.listDevices = [](std::vector<DeviceInfo> &out, std::string &) {
                out = {DeviceInfo{"card_a", "Speakers", "out", 2, 48000}, DeviceInfo{"card_b", "Headphones", "out", 2, 44100},
                       DeviceInfo{"mic", "Mic", "in", 1, 48000}};
                return true;
            };
            h.settingsPath = settings;
            h.recentsPath = recents;
            h.presetsPath = scratch() + "/presets/user";       // R-PRESET-1: never the machine's own folders
            h.factoryPresetsPath = scratch() + "/presets/factory";
            h.templatesPath = scratch() + "/templates/user";   // R-PRESET-2: likewise
            h.factoryTemplatesPath = scratch() + "/templates/factory";
            h.libraryPath = scratch() + "/library";                 // R-PRESET-3: likewise
            h.pluginsPath = scratch() + "/plugins";                 // R-VST-6: likewise — and a scan that finds two, one bundle broken
            h.scanPlugins = [](std::vector<PluginModel> &out, std::vector<std::string> &problems, std::string &) {
                out = {PluginModel{"0123456789ABCDEF0123456789ABCDEF", "Sweet Pad", "Acme", "instrument", "/vst3/Sweet.vst3"},
                       PluginModel{"FEDCBA9876543210FEDCBA9876543210", "Air Verb", "Acme", "effect", "/vst3/Air.vst3"}};
                problems = {"Broken.vst3: the plugin crashed while being read (Aborted)"};
                return true;
            };
            h.loadPlugin = [w = plugins](const std::string &bundle, const std::string &uid, double rate, int, std::string &err) -> std::unique_ptr<IPluginInstance> {
                if (contains(bundle, "Gone")) { err = bundle + " is not a module directory."; return nullptr; }
                ++w->started;
                return std::make_unique<FakePlugin>(w, uid, rate);
            };
            if (canPlay)
                h.audioOut = [this] {
                    auto o = std::make_unique<FakeOut>();
                    o->cap = capture;
                    o->lat = &outLatency;
                    o->devices = devices;
                    return std::unique_ptr<IAudioOut>(std::move(o));
                };
            if (canRecord)
                h.audioIn = [this] {
                    auto i = std::make_unique<FakeIn>();
                    i->cap = capture;
                    i->mode = inMode;
                    i->inLat = inLatency;
                    i->outLat = outLatency;
                    i->period = inPeriod;
                    i->device = &inDevice;
                    i->pacedIns = pacedIns;
                    return std::unique_ptr<IAudioIn>(std::move(i));
                };
            h.audioInFor = [this](const std::string &driver) -> std::unique_ptr<IAudioIn> { // R-DEV-9: a capture through PipeWire
                if (driver != "pipewire" || pipewire.empty() || !canRecord) return nullptr;
                auto i = std::make_unique<FakeIn>();
                i->cap = capture;
                i->mode = inMode;
                i->device = &inDevice;
                i->fails = pipewire == "fail";
                return i;
            };
            h.listMidiInputs = [this](std::vector<MidiPort> &out, std::string &) {
                out = {MidiPort{"Keys", "Keys"}, MidiPort{"Pads", "Pads"}};
                if (withSurface) out.push_back(MidiPort{"X-Touch", "X-Touch"});
                return true;
            };
            h.midiIn = [this] {
                auto m = std::make_unique<FakeMidi>();
                m->sink = midi;
                m->surface = surfaceIn;
                return std::unique_ptr<IMidiIn>(std::move(m));
            };
            h.midiOut = [this] {
                auto m = std::make_unique<FakeMidiOut>();
                m->sent = surfaceOut;
                return std::unique_ptr<IMidiOut>(std::move(m));
            };
            h.audioOutFor = [this](const std::string &driver) -> std::unique_ptr<IAudioOut> {
                if (driver != "pipewire" || pipewire.empty()) return nullptr;
                auto o = std::make_unique<PipewireOut>();
                o->cap = capture;
                o->fails = pipewire == "fail";
                return o;
            };
            return h;
        }
    };

    struct Run
    {
        Fake fake;
        SolarisService svc;
        std::vector<std::string> events;
        Run() : Run(std::string(), std::string()) {}
        Run(const std::string &settings, const std::string &recents) : fake{settings, recents}, svc(fake.host())
        {
            svc.subscribe([this](const Event &e) { events.push_back(formatEvent(e)); });
        }
        std::string ok(const std::string &line)
        {
            std::string err;
            if (!svc.dispatchText(line, err)) std::printf("    refused `%s`: %s\n", line.c_str(), err.c_str());
            assert(err.empty());
            return svc.output();
        }
        std::string no(const std::string &line)
        {
            std::string err;
            const bool okay = svc.dispatchText(line, err);
            if (okay) std::printf("    accepted `%s` (expected a refusal)\n", line.c_str());
            assert(!okay && !err.empty());
            return err;
        }
        std::string text() const { return serializeProject(*svc.project()); }
        const StripModel *strip(const std::string &id) const
        {
            for (const auto &s : svc.model().strips)
                if (s.id == id) return &s;
            return nullptr;
        }
        const ClipModel *clip(const std::string &id) const
        {
            for (const auto &c : svc.model().clips)
                if (c.id == id) return &c;
            return nullptr;
        }
    };

    std::string freshSong(const std::string &name)
    {
        const std::string p = scratch() + "/" + name + ".slp";
        fs::remove(p);
        return p;
    }
}

// ── L1: the tables ───────────────────────────────────────────────────────────────────────────

static void test_grammar_table_parses_and_rejects()
{
    std::set<std::string> verbs;
    for (const auto &s : commandSpecs())
    {
        assert(verbs.insert(s.verb).second);                         // one row per verb
        assert(!s.summary.empty() && s.requirement.rfind("R-", 0) == 0);
    }
    std::string err;
    Command c = parseCommand("clip add --src \"my kick.wav\" --at=4 --lane ln_1", err);
    assert(err.empty() && c.kind == Command::Kind::ClipAdd && c.flag("src") == "my kick.wav" && c.flag("at") == "4");
    assert(formatCommand(c) == "clip add --src \"my kick.wav\" --at 4 --lane ln_1");
    c = parseCommand("send add ch_2 --to ch_1 --pre", err);
    assert(err.empty() && c.has("pre") && c.arg(0) == "ch_2");
    c = parseCommand("set ch_2.gain=-3 dv_1.filter.cutoff=800", err);
    assert(err.empty() && c.fields.size() == 2 && c.fields[1].first == "dv_1.filter.cutoff");
    parseCommand("strp add", err);
    assert(contains(err, "unknown command") && contains(err, "strip add"));
    parseCommand("clip add --sorce a.wav", err);
    assert(contains(err, "does not take --sorce") && contains(err, "--src"));
    parseCommand("send add ch_2 --pre=1", err);
    assert(contains(err, "is a switch"));
    parseCommand("clip add --src", err);
    assert(contains(err, "needs a value"));
    parseCommand("route", err);
    assert(contains(err, "too few arguments"));
    parseCommand("set gain", err);
    assert(contains(err, "expected <address>=<value>"));
    c = parseCommand("   # a comment", err);
    assert(err.empty() && c.kind == Command::Kind::None);
    pass("the grammar is a table: each verb once; unknown verbs and flags refused with the nearest (R-SVC-3)");
}

static void test_event_and_model_tables()
{
    std::set<std::string> names;
    for (const auto &e : eventSpecs()) assert(names.insert(e.name).second);
    Event e(Event::Kind::ParamsChanged);
    e.with("address", "ch_2.gain").with("value", "-3.0");
    assert(formatEvent(e) == "[evt] params.changed address=ch_2.gain value=-3.0");
    std::set<std::string> paths;
    for (const auto &f : appModelFields()) assert(paths.insert(f.path).second);
    pass("event names unique; an event formats as its log line");
}

// ── L2: the real service ─────────────────────────────────────────────────────────────────────

static void test_a_new_song_has_its_mixers_and_round_trips()
{
    Run r;
    const std::string path = freshSong("new");
    r.ok("project new " + path + " --bpm 128 --name \"Night Drive\"");
    const AppModel &m = r.svc.model();
    assert(m.screen == "project" && m.projectName == "Night Drive" && m.bpm == 128.0 && !m.dirty);
    assert(m.mixers.size() == 2 && m.mixers[0].name == "Sources" && m.mixers[1].name == "Buses");
    assert(m.mixers[1].strips == std::vector<std::string>{"ch_1"} && r.strip("ch_1")->name == "Main" && r.strip("ch_1")->out == "master");
    assert(r.no("project new " + path) == path + " already exists — open it, or choose another name");
    const std::string before = r.text();
    r.ok("project close");
    assert(r.svc.model().screen == "home" && !r.svc.project());
    r.ok("project open " + path);
    assert(r.text() == before);
    assert(r.no("strip add --kind synth") == "--kind must be audio, instrument or bus");
    assert(contains(r.no("project new " + freshSong("x") + " --bpm 5"), "--bpm"));
    pass("project new: Sources, Buses with Main → master; saved, closed, reopened byte-identical");
}

static void test_every_sample_file_gets_its_own_strip()
{
    Run r;
    r.ok("project new " + freshSong("samples"));
    assert(r.ok("clip add --src kick.wav --at 0") == "ac_1\nmade: strip=ch_2 lane=ln_1\n");
    const StripModel *kick = r.strip("ch_2");
    assert(kick && kick->name == "kick" && kick->kind == "audio" && kick->mixer == "mx_1" && kick->out == "ch_1"); // Sources → Main
    assert(r.clip("ac_1")->lane == "ln_1" && r.svc.model().lanes[0].name == "kick");
    r.ok("clip add --src hat.wav --at 1");
    assert(r.strip("ch_3") && r.strip("ch_3")->name == "hat");          // a new file: a new strip
    r.ok("clip add --src kick.wav --at 4 --lane ln_2");
    assert(r.clip("ac_3")->track == "ch_2" && r.svc.model().strips.size() == 3); // the same file: the same strip
    assert(r.strip("ch_2")->clipCount == 2 && r.strip("ch_2")->fromLanes == std::vector<std::string>({"ln_1", "ln_2"}));
    assert(r.strip("ch_1")->fromStrips == std::vector<std::string>({"ch_2", "ch_3"}));        // fed by (R-MIX-8)
    assert(std::fabs(r.clip("ac_1")->length - 0.5 * 120 / 60) < 1e-9);                       // half a second at 120 bpm = 1 beat
    assert(contains(r.no("clip add --src missing.wav"), "cannot read missing.wav"));
    assert(contains(r.no("clip add --strip ch_2"), "notes need an instrument strip"));
    assert(r.no("clip add --at 3") == "clip add needs --src <file> (audio), --strip <instrument strip>, --lane <an instrument's track> or --instrument <type> (notes)");
    pass("every sample file gets its own strip on Sources → Main; the same file reuses it; fed-by is computed (R-MIX-2/3/8)");
}

static void test_an_instrument_drop_is_one_command_and_a_refusal_says_nothing_changed()
{
    Run r;
    r.ok("project new " + freshSong("instdrop"));
    r.events.clear();
    assert(r.ok("clip add --instrument drums --at 2 --length 4") ==                  // what dropping "Drum Machine" sends
           "ac_1\nmade: strip=ch_2 device=dv_1 pattern=pt_1 lane=ln_1\n");          // R-SVC-8: everything made is said
    const StripModel *d = r.strip("ch_2");
    assert(d && d->kind == "instrument" && d->name == "Drum Machine" && d->mixer == "mx_1" && d->out == "ch_1");
    assert(d->devices.size() == 1 && d->devices[0].type == "drums");
    const ClipModel *c = r.clip("ac_1");
    assert(c && c->track == "ch_2" && c->kind == "note" && c->at == 2.0 && c->length == 4.0 && c->lane == "ln_1");
    // …and each made node is announced (R-SVC-8)
    assert(r.events.size() == 5 && contains(r.events[0], "what=strip.added node=ch_2") && contains(r.events[1], "what=device.added node=dv_1") &&
           contains(r.events[2], "what=pattern.added node=pt_1") && contains(r.events[3], "what=lane.added node=ln_1") &&
           contains(r.events[4], "what=clip.added node=ac_1"));

    const std::string before = r.text();
    r.events.clear();
    assert(contains(r.no("clip add --instrument flute"), "`flute` is not an instrument (instruments: synth wavetable fm piano drums 808 granular multisample sampler)"));
    assert(contains(r.no("clip add --instrument synth --strip ch_2"), "without --src or --strip"));
    assert(contains(r.no("clip add --instrument synth --length -1"), "--length"));   // the strip was made, then the clip refused
    assert(r.text() == before && r.events.size() == 3);                                // three rejections, no change
    r.ok("clip add --src kick.wav");
    r.events.clear();
    assert(contains(r.no("clip add --src snare.wav --in -1"), "--in"));              // likewise: a new strip, then a refusal
    for (const auto &e : r.events) assert(!contains(e, "project.changed"));            // nothing said it changed…
    assert(r.svc.model().strips.size() == 3);                                           // …and nothing did
    // a strip's colour is its own or comes from its id, so one deleted recolours nobody else
    const int kick = r.strip("ch_3")->colour;
    assert(kick >= 0 && r.strip("ch_2")->colour != kick);
    r.ok("strip delete ch_2 --with-clips");
    assert(r.strip("ch_3")->colour == kick);
    pass("clip add --instrument: a new instrument strip and an empty note clip in ONE command; a refused edit emits no change (R-BROWSE-3)");
}

static void test_a_relative_src_is_found_in_the_songs_folder()
{
    Run r;
    const std::string dir = scratch() + "/relsong";
    fs::remove_all(dir);
    fs::create_directories(dir + "/samples");
    std::ofstream(dir + "/samples/snare.wav") << "x";                   // the fake decoder reads any path that exists or not
    r.ok("project new " + dir + "/song.slp");
    r.ok("clip add --src " + dir + "/samples/snare.wav");
    assert(r.clip("ac_1")->src == "samples/snare.wav");                 // stored relative to the song
    r.ok("clip add --src samples/snare.wav --at 4");                    // what the browser's Song tab hands back
    assert(r.clip("ac_2")->track == r.clip("ac_1")->track);            // the SAME file: the same strip
    assert(r.svc.model().strips.size() == 2 && r.svc.model().deviceTypes.size() == arstro::DeviceRegistry::types().size() && r.svc.model().deviceTypes[0].kind == "instrument");
    pass("a relative src names a file in the song's folder first — the Song tab re-places a sound with one `clip add`");
}

static void test_note_move_and_quantize_edit_the_pattern()
{
    Run r;
    r.ok("project new " + freshSong("roll"));
    r.ok("clip add --instrument drums --at 0 --length 4");                     // ch_2, pt_1
    r.ok("note add pt_1 --pitch 36 --at 0");
    r.ok("note add pt_1 --pitch 38 --at 1 --length 0.5 --vel 90");
    // one gesture, one line: move in time and pitch, resize, re-velocity
    r.ok("note move pt_1 --pitch 38 --at 1 --to-pitch 40 --to-at 1.5");
    r.ok("note move pt_1 --pitch 40 --at 1.5 --length 1 --vel 120");
    const auto &n = r.svc.model().patterns[0].notes;
    assert(n.size() == 2 && n[1].pitch == 40 && n[1].at == 1.5 && n[1].length == 1.0 && n[1].vel == 120);
    assert(contains(r.no("note move pt_1 --pitch 40 --at 1.5 --to-pitch 36 --to-at 0"), "a note is already at 36"));
    assert(contains(r.no("note move pt_1 --pitch 41 --at 1.5 --length 2"), "no note 41"));
    assert(contains(r.no("note move pt_1 --pitch 40 --at 1.5"), "needs --to-pitch, --to-at, --length, --vel and/or --chance"));
    // quantize: onto the grid; swing pushes every second step late; notes landing together merge
    r.ok("note add pt_1 --pitch 42 --at 0.27");
    r.ok("note add pt_1 --pitch 42 --at 0.49");
    r.ok("note add pt_1 --pitch 42 --at 0.51 --vel 127");
    r.ok("pattern quantize pt_1 --grid 0.25 --swing 0.5");
    std::vector<double> hats;
    int loud = 0;
    for (const auto &x : r.svc.model().patterns[0].notes)
        if (x.pitch == 42) { hats.push_back(x.at); loud = std::max(loud, x.vel); }
    // 0.27 → step 1 (odd, +0.125) = 0.375; 0.49 and 0.51 → step 2 (even) = 0.5, merged, the louder kept
    assert(hats == std::vector<double>({0.375, 0.5}) && loud == 127);
    assert(contains(r.no("pattern quantize pt_1 --swing 0.9"), "--swing must be 0 to 0.75"));
    // what a piano roll needs: the kit's key names, the pattern's strip and instrument
    const auto &m = r.svc.model();
    const auto &pm = m.patterns[0];
    assert(pm.strip == "ch_2" && pm.instrument == "drums");
    bool kick = false;
    for (const auto &t : m.deviceTypes)
        for (const auto &nn : t.noteNames) kick |= t.name == "drums" && nn.note == 36 && nn.name == "Kick";
    assert(kick);
    pass("note move: one line moves, resizes, re-velocities, refused onto a note; pattern quantize with swing merges collisions; the kit's keys are named (R-ROLL-2/4)");
}

static void test_strip_relink_moves_every_clip_in_one_edit()
{
    Run r;
    r.ok("project new " + freshSong("relink"));
    r.ok("clip add --instrument synth --at 0 --length 4");                 // ch_2
    r.ok("clip duplicate " + r.svc.model().clips[0].id);                    // a second clip, linked
    r.ok("strip add --kind instrument --instrument synth --name Lead");     // ch_3: the new line
    r.ok("strip add --kind bus --name Verb");                               // ch_4
    r.ok("strip add --kind audio --name Vox");                              // ch_5
    assert(r.ok("strip relink ch_2 --to ch_3") == "2\n");
    for (const auto &c : r.svc.model().clips) assert(c.track == "ch_3");
    assert(r.svc.model().patterns[0].strip == "ch_3");
    assert(contains(r.no("strip relink ch_3 --to ch_4"), "a bus plays no clips"));
    assert(contains(r.no("strip relink ch_3 --to ch_5"), "need an instrument strip"));
    assert(contains(r.no("strip relink ch_2 --to ch_3"), "has no clips to move"));
    assert(contains(r.no("strip relink ch_3 --to ch_3"), "already play through"));
    assert(contains(r.no("strip relink ch_3 --to ch_9"), "no strip `ch_9`"));
    assert(contains(r.no("strip relink ch_3"), "needs --to"));
    // ONE edit: one undo puts every clip back
    assert(r.ok("undo") == "strip relink ch_2\n");
    for (const auto &c : r.svc.model().clips) assert(c.track == "ch_2");
    pass("strip relink: every clip of a strip to another line in one edit (one undo); refused onto a bus, across kinds, onto itself, with nothing to move (R-MIX-14)");
}

static void test_a_sidechain_key_ducks_the_bass()
{
    Run r;
    const std::string dir = scratch();
    const std::string song = freshSong("sidechain");
    r.ok("project new " + song + " --bpm 120");
    r.ok("clip add --instrument drums --at 0 --length 8");                       // ch_2: the kick, first in order
    for (int b = 0; b < 8; ++b) r.ok("note add pt_1 --pitch 36 --at " + std::to_string(b));
    r.ok("clip add --src tone.wav --at 0 --length 8");                           // ch_3: the "bass" (a 1 kHz tone at 0.5)
    r.ok("set ch_2.gain=-120");                                                  // a ghost kick: heard by the key only
    r.ok("device add ch_3 --type compressor");
    r.ok("set dv_2.threshold=-30 dv_2.ratio=4 dv_2.attack=1 dv_2.release=80");
    // a key goes forward in processing order, to a strip — never back, never to the master (R-MIX-15)
    assert(contains(r.no("send add ch_3 --to ch_2 --sidechain"), "a sidechain key can only go to a strip LATER in processing order"));
    assert(contains(r.no("send add ch_2 --to master --sidechain"), "must go to a strip"));
    const std::string sd = r.ok("send add ch_2 --to ch_3 --sidechain --pre");
    assert(sd == "sd_1\n");
    // a key nothing hears is said so; Sidechain on, it is heard
    assert(contains(r.ok("audit"), "sidechain sd_1 keys ch_3, where no compressor has Sidechain on"));
    r.ok("set dv_2.sidechain=on");
    assert(!contains(r.ok("audit"), "sidechain sd_1"));
    const auto &m = r.svc.model();
    assert(m.strips[1].sends.size() == 1 || m.strips[0].sends.size() == 1);
    for (const auto &st : m.strips)
        if (st.id == "ch_2") assert(st.sends[0].sidechain && std::find(st.keyTargets.begin(), st.keyTargets.end(), "ch_3") != st.keyTargets.end());
    auto renderDb = [&](const std::string &name) {
        const std::string out = dir + "/" + name + ".wav";
        r.ok("render --out " + out);
        const auto &L = r.fake.written[out][0];
        // the bass 15–35 ms after the kick on beat 2, and 25–45 ms before the one on beat 3
        auto db = [&](long long a, long long b) {
            double sum = 0;
            for (long long i = a; i < b; ++i) sum += (double)L[(size_t)i] * L[(size_t)i];
            return 10.0 * std::log10(sum / (double)(b - a) + 1e-30);
        };
        return std::make_pair(db(48000 + 720, 48000 + 1680), db(72000 - 2160, 72000 - 1200));
    };
    const auto keyed = renderDb("keyed");
    // the baseline: the kick muted — a muted strip sends no key, so Sidechain on hears silence and cuts nothing
    // (Sidechain OFF is no baseline: the compressor would hear the −6 dBFS bass itself and cut it 18 dB)
    r.ok("set ch_2.mute=true");
    const auto plain = renderDb("plain");
    r.ok("set ch_2.mute=false");
    const double dip = plain.first - keyed.first, back = plain.second - keyed.second;
    if (dip < 6.0 || dip > 22.5 || std::fabs(back) > 1.0) std::printf("    dip %.2f dB, before the next kick %.2f dB\n", dip, back);
    assert(dip >= 6.0 && dip <= 22.5);                // ducked by the kick: at most (0 − (−30))·¾
    assert(std::fabs(back) < 1.0);                    // and back before the next one
    // the bass soloed keeps its pump — the kick, silenced by the solo, still keys; muted, it does not
    r.ok("set ch_3.solo=true");
    for (const auto &st : r.svc.model().strips)
        if (st.id == "ch_2") assert(!st.audible);       // the key brings the kick into the solo? no — it is silenced
    const auto soloed = renderDb("soloed");
    assert(std::fabs(soloed.first - keyed.first) < 1e-6); // and still keys
    r.ok("set ch_3.solo=false dv_2.sidechain=off");
    const auto self = renderDb("self");
    assert(self.second < plain.second - 10.0);        // un-keyed, it compresses on the bass itself, all the time
    r.ok("set dv_2.sidechain=on");
    // the file keeps it
    r.ok("project save");
    std::ifstream in(song);
    std::stringstream ss;
    ss << in.rdbuf();
    assert(contains(ss.str(), "#asend id=sd_1 from=ch_2 to=ch_3 gain=0.0 pre=true sidechain=true"));
    assert(r.ok("get sd_1.sidechain") == "true\n");
    // an audible send obeys the same rule (R-MIX-4 amended: forward = later in processing order): ch_3 comes after
    // ch_2 on Sources, so the key turned audible is taken; back, ch_3 → ch_2, it is refused naming both ends
    r.ok("set sd_1.sidechain=false");
    r.ok("set sd_1.sidechain=true");
    assert(contains(r.no("send add ch_3 --to ch_2"), "ch_3 (tone, on Sources) send sd_2 → ch_2 (Drum Machine, on Sources): a strip can only feed a strip LATER in processing order"));
    assert(r.ok("get sd_1.sidechain") == "true\n");
    pass("sidechain: a key goes forward to a strip's compressor; the bass dips 6–22.5 dB after each kick and is back before the next; soloed it still pumps; the kick muted, nothing; Sidechain off, it compresses on its input; audited, saved (R-MIX-15)");
}

static void test_strip_duck_is_one_line()
{
    // R-MIX-18: `strip duck` — the compressor, its settings and its key in ONE line, one undo; it ducks
    Run r;
    const std::string dir = scratch();
    r.ok("project new " + freshSong("duck") + " --bpm 120");
    r.ok("clip add --instrument drums --at 0 --length 8");                       // ch_2: the kick, first in order
    for (int b = 0; b < 8; ++b) r.ok("note add pt_1 --pitch 36 --at " + std::to_string(b));
    r.ok("clip add --src tone.wav --at 0 --length 8");                           // ch_3: the "bass"
    auto devicesOf = [&](const std::string &ch) {
        for (const auto &st : r.svc.model().strips)
            if (st.id == ch) return st.devices.size();
        return (size_t)0;
    };
    auto dipDb = [&](const std::string &name) {
        const std::string out = dir + "/" + name + ".wav";
        r.ok("render --out " + out + " --bits 32f --stems ch_3");          // the bass's own stem: the mix has the kick in it
        const auto &L = r.fake.written[dir + "/" + name + ".ch_3.wav"][0];
        auto db = [&](long long a, long long b) {
            double sum = 0;
            for (long long i = a; i < b; ++i) sum += (double)L[(size_t)i] * L[(size_t)i];
            return 10.0 * std::log10(sum / (double)(b - a) + 1e-30);
        };
        return std::make_pair(db(48000 + 720, 48000 + 1680), db(72000 - 2160, 72000 - 1200)); // after the kick on beat 2, before the one on beat 3
    };
    const auto plain = dipDb("plain");
    const int depth = r.svc.model().undoDepth;
    const std::string said = r.ok("strip duck ch_3 --under ch_2 --amount 12");
    assert(firstLine(said) == "dv_2" && contains(said, "made:") && contains(said, "sd_1"));
    assert(r.svc.model().undoDepth == depth + 1 && devicesOf("ch_3") == 1);
    // the compressor: Sidechain on, threshold −30, attack 1 ms, half a beat's release at 120 bpm, the ratio for 12 dB
    double v = 0;
    assert(r.ok("get dv_2.sidechain") == "on\n");
    assert(parseNumber(firstLine(r.ok("get dv_2.threshold")), v) && v == -30.0);
    assert(parseNumber(firstLine(r.ok("get dv_2.attack")), v) && v == 1.0);
    assert(parseNumber(firstLine(r.ok("get dv_2.release")), v) && v == 250.0);
    assert(parseNumber(firstLine(r.ok("get dv_2.ratio")), v) && std::fabs(v - 30.0 / 18.0) < 1e-3);
    // its key: a pre-fader sidechain send from the kick
    const auto *kick = r.strip("ch_2");
    assert(kick->sends.size() == 1 && kick->sends[0].to == "ch_3" && kick->sends[0].pre && kick->sends[0].sidechain);
    // and it ducks: the bass well down just after a kick, back before the next
    const auto ducked = dipDb("ducked");
    const double depth12 = plain.first - ducked.first, back = plain.second - ducked.second;
    if (!(depth12 > 6.0 && depth12 <= 12.0 && back < 3.0)) std::printf("    ducked %.2f dB just after a kick, %.2f dB just before the next\n", depth12, back);
    assert(depth12 > 6.0 && depth12 <= 12.0 && back < 3.0); // ≈ 7.3 and 1.8: a kick under full scale cuts less than the amount
    // one undo takes both back
    r.ok("undo");
    assert(devicesOf("ch_3") == 0 && r.strip("ch_2")->sends.empty());
    r.ok("redo");
    // refusals, and a refused line leaves nothing behind (all-or-nothing)
    assert(contains(r.no("strip duck ch_3 --under ch_2"), "already ducks under ch_2 (sd_1)"));
    const size_t before = devicesOf("ch_2");
    assert(contains(r.no("strip duck ch_2 --under ch_3"), "LATER in processing order"));
    assert(devicesOf("ch_2") == before && r.strip("ch_3")->sends.empty());
    assert(contains(r.no("strip duck ch_3 --under ch_3"), "itself"));
    assert(contains(r.no("strip duck ch_3 --under ch_9"), "no strip"));
    assert(contains(r.no("strip duck ch_9 --under ch_2"), "no strip"));
    assert(contains(r.no("strip duck ch_3 --under ch_2 --amount 30"), "1 … 24"));
    assert(contains(r.no("strip duck ch_3"), "--under"));
    pass("strip duck (R-MIX-18): ONE line makes the keyed compressor (Sidechain on, −30 dB, 1 ms, half a beat, the ratio for "
         "the amount) and its pre-fader key; the bass ducks after each kick; one undo takes both; a refused line leaves nothing");
}

static void test_a_sampler_plays_its_sound_by_the_key()
{
    Run r;
    const std::string dir = scratch();
    const std::string song = freshSong("sampler");
    r.ok("project new " + song + " --bpm 120");
    std::ofstream(dir + "/kick.wav") << "x";                          // in the song's folder: stored relative
    // a sampler line with its sound, in one command (R-EDM-8) — the fake decoder: a 1 kHz tone at 0.5, half a second
    r.ok("clip add --instrument sampler --sample kick.wav --at 0 --length 8");
    r.ok("set dv_1.velocity=0");                                      // every note full: the file's own level
    r.ok("note add pt_1 --pitch 60 --at 0 --length 1");                // the root: the file itself
    r.ok("note add pt_1 --pitch 72 --at 2 --length 1");                // an octave up
    const auto &m = r.svc.model();
    const auto *dm = &m.strips[0].devices[0];
    for (const auto &st : m.strips)
        if (!st.devices.empty() && st.devices[0].id == "dv_1") dm = &st.devices[0];
    assert(dm->takesSample && dm->sample == "kick.wav");
    assert(r.ok("get dv_1.sample") == "kick.wav\n");
    const std::string out = dir + "/sampler.wav";
    r.ok("render --out " + out + " --to 4 --bits 32f");               // the mix as mixed: no dither (R-EXPORT-1)
    const auto &L = r.fake.written[out][0];
    // at the root it IS the file, sample for sample (unity pan law at centre, 0 dB fader and master)
    bool exact = true;
    for (int i = 0; i < 20000; ++i) exact &= L[(size_t)i] == (float)(0.5 * std::sin(2 * M_PI * 1000.0 * i / 48000));
    assert(exact);
    // an octave up: 2 kHz, and over in half the time
    auto hz = [&](size_t a, size_t b) {
        int n = 0;
        for (size_t i = a + 1; i < b; ++i) n += (L[i - 1] < 0.0f) != (L[i] < 0.0f);
        return n / 2.0 / ((double)(b - a) / 48000.0);
    };
    assert(std::fabs(hz(0, 20000) - 1000.0) < 5.0);
    assert(std::fabs(hz(48000, 48000 + 11000) - 2000.0) < 10.0);
    float after = 0;
    for (size_t i = 48000 + 12100; i < 72000; ++i) after = std::max(after, std::fabs(L[i]));
    assert(after == 0.0f);
    // the file keeps it, relative to the song
    r.ok("project save");
    std::ifstream in(song);
    std::stringstream ss;
    ss << in.rdbuf();
    assert(contains(ss.str(), "type=sampler sample=kick.wav"));
    // refusals, each naming why; nothing changes
    assert(contains(r.no("set dv_1.sample=missing.wav"), "cannot read missing.wav"));
    assert(r.ok("get dv_1.sample") == "kick.wav\n");
    assert(contains(r.no("strip add --kind instrument --instrument synth --sample kick.wav"), "--sample is for an instrument that plays one"));
    r.ok("strip add --kind instrument --instrument synth");
    std::string synthDv;
    for (const auto &st : r.svc.model().strips)
        for (const auto &d : st.devices)
            if (d.type == "synth") synthDv = d.id;
    assert(contains(r.no("set " + synthDv + ".sample=kick.wav"), "plays no sample"));
    // a sampler with no sound is audited
    r.ok("strip add --kind instrument --instrument sampler");
    assert(contains(r.ok("audit"), "has no sample — give it one"));
    bool typed = false;
    for (const auto &t : r.svc.model().deviceTypes) typed |= t.name == "sampler" && t.takesSample && t.kind == "instrument";
    assert(typed);
    pass("sampler: made with its sound in one command; at the root it IS the file, an octave up 2 kHz and half as long; saved relative; refusals named; audited when silent (R-EDM-8)");
}

static void test_undo_and_redo_every_edit()
{
    Run r;
    const std::string song = freshSong("undo");
    r.ok("project new " + song);
    const std::string empty = r.text();
    r.ok("clip add --instrument drums --at 0 --length 4");                        // step 1
    const std::string withDrums = r.text();
    for (const char *g : {"-1", "-2", "-3"}) r.ok(std::string("set ch_2.gain=") + g); // a fader dragged: ONE step
    const std::string dragged = r.text();
    r.ok("set ch_2.pan=0.5");                                                       // step 3
    const auto &m = r.svc.model();
    assert(m.undoDepth == 3 && m.undoLabel == "set ch_2.pan" && m.redoDepth == 0);
    r.events.clear();
    assert(r.ok("undo") == "set ch_2.pan\n" && r.text() == dragged);
    assert(r.events.size() == 1 && contains(r.events[0], "what=undo node=\"set ch_2.pan\""));
    assert(r.ok("undo") == "set ch_2.gain\n" && r.text() == withDrums);           // the whole drag, in one
    assert(r.ok("redo") == "set ch_2.gain\n" && r.text() == dragged);
    r.ok("undo");
    assert(r.ok("undo") == "clip add\n" && r.text() == empty);
    assert(contains(r.no("undo"), "nothing to undo"));
    assert(r.ok("redo") == "clip add\n" && r.text() == withDrums && r.svc.model().redoDepth == 2);
    // a new edit is a new node: nothing below it to redo (the undone steps stay a branch — R-HIST-1)
    r.ok("lane add Extra");
    assert(r.svc.model().redoDepth == 0 && contains(r.no("redo"), "nothing to redo"));
    // not edits: a machine setting, the transport, a save
    const int depth = r.svc.model().undoDepth;
    r.ok("settings set bufferSize=256");
    r.ok("transport seek 4");
    r.ok("project save");
    assert(r.svc.model().undoDepth == depth);
    // automation is undone like anything else
    r.ok("auto create ch_2.gain");
    r.ok("undo");
    assert(r.svc.model().automations.empty() && r.svc.model().bindings.empty());
    // another song: its own history
    r.ok("project open " + song);
    assert(r.svc.model().undoDepth == 0 && contains(r.no("undo"), "nothing to undo"));
    pass("undo / redo: every edit, byte-exact; a dragged fader is one step; nothing to redo below a new edit; settings, transport and save are not edits (R-EDM-1)");
}

// R-HIST-1/2: the history is a tree — an edit after an undo starts a branch and the old future stays; `history` prints
// it, `history jump <id>` goes anywhere in it, redo takes the newest branch; 200 nodes, the oldest off-path first
static void test_history_is_a_tree()
{
    Run r;
    const std::string song = freshSong("tree");
    r.ok("project new " + song);
    const std::string empty = r.text();
    r.ok("clip add --instrument drums --at 0 --length 4");                        // node 1
    const std::string a = r.text();
    r.ok("set ch_2.gain=-3");                                                       // node 2
    const std::string b = r.text();
    r.ok("undo");                                                                   // back at 1 …
    r.ok("set ch_2.pan=0.5");                                                       // … node 3: a branch — 2 stays
    const std::string c = r.text();
    {
        const auto &m = r.svc.model();
        assert(m.history.size() == 4 && m.historyCurrent == 3 && m.history[0].label == "New song" && m.history[0].parent == -1);
        assert(m.history[2].parent == 1 && m.history[3].parent == 1 && m.history[2].label == "set ch_2.gain");
    }
    // the old future is still there: one line goes to it; a jump is no node, and the song is unsaved
    r.ok("project save");
    r.events.clear();
    assert(r.ok("history jump 2") == "set ch_2.gain\n" && r.text() == b && r.svc.model().historyCurrent == 2);
    assert(r.events.size() == 1 && contains(r.events[0], "what=history.jump node=\"set ch_2.gain\""));
    assert(r.svc.model().history.size() == 4 && r.svc.model().dirty);
    // undo goes up; redo goes down the NEWEST branch
    assert(r.ok("undo") == "set ch_2.gain\n" && r.text() == a);
    assert(r.svc.model().redoLabel == "set ch_2.pan" && r.ok("redo") == "set ch_2.pan\n" && r.text() == c);
    // the tree, printed and as JSON
    const std::string h = r.ok("history");
    assert(contains(h, "  0\t-\tNew song\n") && contains(h, "  2\t1\tset ch_2.gain\n") && contains(h, "* 3\t1\tset ch_2.pan\n"));
    assert(contains(r.ok("history --json"), "\"id\": 3,\n    \"parent\": 1,\n    \"label\": \"set ch_2.pan\",\n    \"current\": true"));
    // refused, naming why
    assert(contains(r.no("history jump 9"), "no history node `9`") && contains(r.no("history jump x"), "no history node"));
    // the root is the song as it was made
    r.ok("history jump 0");
    assert(r.text() == empty && contains(r.no("undo"), "nothing to undo") && r.svc.model().redoDepth == 2);
    // a dragged fader is still ONE node
    r.ok("history jump 1");
    for (const char *g : {"-1", "-2", "-3"}) r.ok(std::string("set ch_2.gain=") + g);
    assert(r.svc.model().history.size() == 5 && r.svc.model().historyCurrent == 4);
    // at most 200 nodes: the oldest leaves off the path go first (the branches 2 and 3), then the path's own root
    for (int i = 0; i < 230; ++i) r.ok(i % 2 ? std::string("set ch_2.pan=") + (i % 4 < 2 ? "0.1" : "0.2") : std::string("set ch_2.gain=") + (i % 4 < 2 ? "-1" : "-2"));
    {
        const auto &m = r.svc.model();
        assert(m.history.size() == 200);
        int roots = 0;
        for (const auto &n : m.history)
        {
            assert(n.id != 2 && n.id != 3);
            if (n.parent < 0) ++roots;
        }
        assert(roots == 1 && m.undoDepth == 199);
    }
    // another song: its own tree, rooted at it
    r.ok("project save");
    r.ok("project open " + song);
    assert(r.svc.model().history.size() == 1 && r.svc.model().history[0].label == "Opened" && r.svc.model().historyCurrent == 0);
    r.ok("project close");
    assert(r.svc.model().history.empty() && r.svc.model().historyCurrent == -1);
    pass("The history is a tree (R-HIST-1/2): an edit after an undo branches and the old future stays; `history jump` goes anywhere (no node itself); redo takes the newest branch; a drag is one node; 200 nodes, the oldest off-path first; a song opened is its own root");
}

static void test_formulas_bind_numbers_and_refuse_what_cannot_be_read()
{
    Run r;
    r.ok("project new " + freshSong("formulas"));
    r.ok("clip add --instrument synth --at 0 --length 8");             // ch_2, its synth dv_1, pt_1
    // automation FROM a property: one command, named after its owner and parameter, ranged as it, bound to it
    assert(firstLine(r.ok("auto create dv_1.filter.cutoff")) == "au_1");
    {
        const auto &m = r.svc.model();
        assert(m.automations.size() == 1);
        const auto &a = m.automations[0];
        assert(a.name == "Basic Synth \xC2\xB7 Cutoff" && a.unit == "Hz" && a.min == 20.0 && a.max == 20000.0 && a.from == "dv_1.filter.cutoff");
        assert(a.points.size() == 2 && a.points[0].at == 0.0 && a.points[1].at >= 8.0 && a.points[0].value == a.points[1].value);
        assert(a.usedBy == std::vector<std::string>{"dv_1.filter.cutoff"});
        assert(m.bindings.size() == 1 && m.bindings[0].formula == "=au_1" && m.bindings[0].ok);
        const auto &dv = r.strip("ch_2")->devices[0];
        bool bound = false;
        for (const auto &pm : dv.params) bound |= pm.name == "filter.cutoff" && pm.formula == "=au_1";
        assert(bound && dv.lastChanged == "filter.cutoff");
    }
    assert(r.ok("get dv_1.filter.cutoff") == "=au_1\n");                   // `get` prints what decides it
    // a formula with spaces, the clock, a link
    r.ok("set ch_2.pan=\"=0.25 * sin(beat * pi)\"");
    assert(r.ok("get ch_2.pan") == "=0.25 * sin(beat * pi)\n" && r.strip("ch_2")->panFormula == "=0.25 * sin(beat * pi)");
    assert(contains(r.no("set project.masterGain==ch_2.gain - 3"), "a formula with spaces is quoted"));
    r.ok("set project.masterGain=\"=ch_2.gain - 3\"");
    // what cannot be read is refused, naming it — and a refused line changes nothing and says nothing
    const std::string before = r.text();
    r.events.clear();
    assert(contains(r.no("set ch_2.gain==au_9"), "reads `au_9`, which is no automation"));
    assert(contains(r.no("set ch_2.gain==1+"), "the formula ends"));
    assert(contains(r.no("set ch_2.gain==foo(1)"), "unknown function `foo`"));
    assert(contains(r.no("set ch_2.gain==min(1)"), "`min` takes 2 values"));
    assert(contains(r.no("set dv_1.osc1.wave==1"), "is a choice"));
    assert(contains(r.no("set ch_2.mute==1"), "not a number a formula can drive"));
    assert(contains(r.no("set ch_2.gain==project.masterGain"), "a loop of links (")); // masterGain already reads ch_2.gain
    assert(r.text() == before);
    for (const auto &e : r.events) assert(!contains(e, "params.changed") && !contains(e, "project.changed"));
    // a plain number clears the formula and sets the value; `bind clear` keeps the value
    r.ok("set ch_2.pan=0.3");
    assert(r.strip("ch_2")->panFormula.empty() && r.strip("ch_2")->pan == 0.3 && r.ok("get ch_2.pan") == "0.3\n");
    r.ok("bind clear project.masterGain");
    assert(r.svc.model().masterGainFormula.empty() && contains(r.no("bind clear project.masterGain"), "has no formula"));
    // points: added sorted, moved, shaped; a value outside the range is REFUSED naming it (R-SVC-8: never clamped
    // in silence); refusals name the beat
    assert(contains(r.no("auto point add au_1 --at 4 --value 99999 --shape smooth"), "--value for au_1 must be between 20.0 and 20000.0 Hz, got 99999.0"));
    assert(r.svc.model().automations[0].points.size() == 2);
    r.ok("auto point add au_1 --at 4 --value 20000 --shape smooth");
    assert(r.svc.model().automations[0].points.size() == 3 && r.svc.model().automations[0].points[1].value == 20000.0);
    assert(contains(r.no("auto point move au_1 --at 4 --value 5"), "must be between 20.0 and 20000.0 Hz"));
    assert(contains(r.no("auto point move au_1 --at 3 --value 1"), "no point at beat 3.0"));
    r.ok("auto point move au_1 --at 4 --to 2 --value 400");
    r.ok("auto point shape au_1 --at 2 --shape hold");
    assert(contains(r.no("auto point shape au_1 --at 2 --shape bounce"), "linear, hold, smooth or bezier"));
    const auto &pts = r.svc.model().automations[0].points;
    assert(pts[1].at == 2.0 && pts[1].value == 400.0 && pts[1].shape == "hold");
    // eval: the value at a beat, and why
    const std::string ex = r.ok("eval dv_1.filter.cutoff --at 3 --explain");
    assert(contains(ex, "dv_1.filter.cutoff = 400.0 Hz at beat 3.0") && contains(ex, "au_1 = 400.0") && contains(ex, "automation \""));
    // an automation read by a formula is not deleted out from under it
    assert(contains(r.no("auto delete au_1"), "read by dv_1.filter.cutoff"));
    // deleting what a formula drives takes the formula with it
    r.ok("strip delete ch_2 --with-clips");
    assert(r.svc.model().bindings.empty() && r.svc.model().automations[0].usedBy.empty());
    r.ok("auto delete au_1");
    assert(r.svc.model().automations.empty());
    pass("formulas: auto create names, ranges and binds; links, the clock, quoting; unreadable formulas and loops refused; numbers clear; eval explains (R-AUTO-1…5, 8)");
}

static void test_an_automated_gain_renders_its_curve()
{
    Run r;
    const std::string dir = scratch();
    const std::string song = freshSong("autorender");
    r.ok("project new " + song + " --bpm 120");
    r.ok("clip add --src tone.wav --at 0 --length 8");                 // a 1 kHz sine at 0.5, looped for 8 beats
    r.ok("auto create ch_2.gain");
    r.ok("auto point move au_1 --at 0 --value 0");
    r.ok("auto point add au_1 --at 8 --value -20");
    const std::string out = dir + "/auto.wav";
    r.ok("render --out " + out);
    const auto &L = r.fake.written[out][0];
    auto dbAround = [&](long long s) {
        double sum = 0;
        for (long long i = s - 1200; i < s + 1200; ++i) sum += (double)L[(size_t)i] * L[(size_t)i];
        return 20.0 * std::log10(std::sqrt(sum / 2400.0) / (0.5 / std::sqrt(2.0)));
    };
    const double b2 = dbAround(48000), b6 = dbAround(144000);
    if (std::fabs(b2 + 5.0) > 0.1 || std::fabs(b6 + 15.0) > 0.1) std::printf("    beat 2: %.3f dB, beat 6: %.3f dB\n", b2, b6);
    assert(std::fabs(b2 + 5.0) < 0.1 && std::fabs(b6 + 15.0) < 0.1);   // the curve, measured (R-AUTO-7)
    // the file keeps it, and a hand-edited formula that cannot be read is INERT, said in the model and the audit
    r.ok("project save");
    std::ifstream in(song);
    std::stringstream ss;
    ss << in.rdbuf();
    assert(contains(ss.str(), "#aauto id=au_1") && contains(ss.str(), "#point at=8.0 value=-20.0") && contains(ss.str(), "#abind address=ch_2.gain formula=\"=au_1\""));
    // a hand-edited formula that cannot be read opens, plays its own value, and says why — in the model and the audit
    std::string text = ss.str();
    text.replace(text.find("formula=\"=au_1\""), std::string("formula=\"=au_1\"").size(), "formula=\"=au_1 * nope\"");
    { std::ofstream(song) << text; }
    r.ok("project open " + song);
    const auto &bm = r.svc.model().bindings;
    assert(bm.size() == 1 && !bm[0].ok && contains(bm[0].problem, "reads `nope`"));
    const std::string audit = r.ok("audit");
    assert(contains(audit, "binding ch_2.gain: formula") && contains(audit, "inert: its own value plays"));
    assert(!contains(audit, "automation au_1"));                       // a formula names it, even an inert one
    r.ok("auto add --name Spare");
    assert(contains(r.ok("audit"), "automation au_2 (Spare) moves nothing — no formula reads it"));
    pass("an automated gain renders its curve: −5 dB at beat 2, −15 dB at beat 6 of a 0 → −20 dB ramp, within 0.1 dB; saved as #aauto/#abind; an unreadable formula is inert and audited");
}

// R-AUTO-12/13: a pump — a one-beat cycle repeated through the song — MEASURED: eval repeats every beat, the render
// follows eval, the span limits where it repeats (outside: where a cycle ends), loop 0 is the curve as drawn, the .slp
// keeps it
static void test_an_automation_loops_and_a_pump_is_one_line()
{
    Run r;
    const std::string dir = scratch(), song = freshSong("pump");
    r.ok("project new " + song + " --bpm 120");
    r.ok("clip add --src tone.wav --at 0 --length 16");                 // ch_2: a 1 kHz sine at 0.5, looped
    assert(r.ok("auto create ch_2.gain --pump") == "au_1\n");
    {
        const auto &a = r.svc.model().automations[0];
        assert(a.loop == 1.0 && a.at == 0.0 && a.length == 0.0 && contains(a.name, "(pump)"));
        assert(a.points.size() == 2 && a.points[0].value == -18.0 && a.points[0].shape == "smooth" && a.points[1].at == 0.5 && a.points[1].value == 0.0);
        assert(r.svc.model().bindings[0].formula == "=au_1");
    }
    auto ev = [&](double beat) {
        const std::string e = r.ok("eval ch_2.gain --at " + canonicalNumber(beat));
        return std::atof(e.substr(e.find(" = ") + 3).c_str());
    };
    assert(ev(0) == -18.0 && ev(0.5) == 0.0 && ev(0.75) == 0.0);
    assert(ev(0.25) < -1.0 && ev(0.25) > -17.0);                       // on its way back up
    for (double b : {0.05, 0.25, 0.4})
        for (int k : {1, 3, 7, 12}) assert(std::fabs(ev(b + k) - ev(b)) < 1e-9); // every beat the same
    // rendered: the dip on every beat is what eval says
    const std::string out = dir + "/pump.wav";
    r.ok("render --out " + out + " --to 8");
    const auto &L = r.fake.written[out][0];
    auto dbAround = [&](double beat) {
        const long long s = std::llround(beat * 24000);
        double sum = 0;
        for (long long i = s - 240; i < s + 240; ++i) sum += (double)L[(size_t)i] * L[(size_t)i]; // ten periods of 1 kHz
        return 20.0 * std::log10(std::sqrt(sum / 480.0) / (0.5 / std::sqrt(2.0)));
    };
    for (double b : {1.05, 2.25, 3.4, 4.75, 6.05, 7.25})
    {
        if (std::fabs(dbAround(b) - ev(b)) >= 0.15) std::printf("    beat %.2f: rendered %.3f dB, eval %.3f dB\n", b, dbAround(b), ev(b));
        assert(std::fabs(dbAround(b) - ev(b)) < 0.15);
    }
    assert(dbAround(6.05) < dbAround(6.75) - 15.0);                     // the pump, heard: down on the beat
    // its span — from beat 4 for 8 beats; outside it rests where a cycle ends (0 dB here)
    const double dip = ev(0.05);
    r.ok("set au_1.at=4 au_1.length=8");
    assert(ev(0.05) == 0.0 && ev(2.05) == 0.0 && ev(4.05) == dip && ev(11.05) == dip && ev(12.05) == 0.0);
    assert(r.ok("get au_1.loop") == "1.0\n" && r.ok("get au_1.at") == "4.0\n" && r.ok("get au_1.length") == "8.0\n");
    // off: the curve as drawn — after its point at 0.5 it holds 0 dB
    r.ok("set au_1.loop=0");
    assert(ev(4.05) == 0.0 && ev(0.05) == dip);
    r.ok("undo");
    assert(r.svc.model().automations[0].loop == 1.0);
    // refused, naming why
    assert(contains(r.no("set au_1.loop=-1"), "beats from 0"));
    assert(contains(r.no("set au_1.lop=1"), "loop"));
    // the .slp keeps it, and reads it back
    r.ok("project save");
    std::ifstream f(song);
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    assert(contains(text, "loop=1") && contains(text, "at=4") && contains(text, "length=8"));
    r.ok("project open " + song);
    {
        const auto &a = r.svc.model().automations[0];
        assert(a.loop == 1.0 && a.at == 4.0 && a.length == 8.0);
    }
    pass("An automation loops (R-AUTO-12/13): `auto create --pump` is a one-beat dip repeated through the song — eval repeats every beat and the render is what eval says (the dip heard, > 15 dB); .at / .length bound where it repeats, resting where a cycle ends; loop 0 is the curve as drawn; the .slp keeps it");
}

static void test_a_bezier_gain_renders_what_eval_says_and_the_formula_says()
{
    // R-AUTO-10: a bezier automation, shaped by one line per handle gesture, MEASURED: the rendered
    // level at several beats, `eval` there, and After Effects' cubic solved independently agree.
    Run r;
    const std::string dir = scratch();
    r.ok("project new " + freshSong("bezier") + " --bpm 120");
    r.ok("clip add --src tone.wav --at 0 --length 8");                  // a 1 kHz sine at 0.5, looped
    r.ok("auto create ch_2.gain");                                     // au_1: 0 dB at 0 and at the song's end (16)
    r.ok("auto point move au_1 --at 0 --value -24");
    r.ok("auto point shape au_1 --at 0 --shape bezier --speed-out 0 --influence-out 80");
    r.ok("auto point add au_1 --at 8 --value 0 --shape bezier");      // flat handles: speed 0, influence 33.333
    r.ok("auto point shape au_1 --at 8 --speed-in 1.5 --influence-in 70"); // a bezier point keeps the flags not given
    {
        const auto &pts = r.svc.model().automations[0].points;
        assert(pts.size() == 3 && pts[0].shape == "bezier" && pts[0].speedOut == 0.0 && pts[0].inflOut == 80.0 && pts[0].inflIn == 33.333);
        assert(pts[1].shape == "bezier" && pts[1].speedIn == 1.5 && pts[1].inflIn == 70.0 && pts[1].speedOut == 0.0 && pts[1].inflOut == 33.333);
    }
    // the formula: P0 (0, −24), P1 (0.8·8, −24), P2 (8 − 0.7·8, 0 − 1.5·0.7·8), P3 (8, 0); time solved by Newton
    auto formula = [](double beat) {
        const double x0 = 0, x1 = 6.4, x2 = 8 - 5.6, x3 = 8, y0 = -24, y1 = -24, y2 = 0 - 1.5 * 5.6, y3 = 0;
        auto bez = [](double a, double b, double c, double d, double u) {
            const double m = 1 - u;
            return m * m * m * a + 3 * m * m * u * b + 3 * m * u * u * c + u * u * u * d;
        };
        double u = beat / 8.0;
        for (int i = 0; i < 80; ++i)
        {
            const double m = 1 - u, dx = 3 * m * m * (x1 - x0) + 6 * m * u * (x2 - x1) + 3 * u * u * (x3 - x2);
            u = std::clamp(u - (bez(x0, x1, x2, x3, u) - beat) / std::max(dx, 1e-6), 0.0, 1.0);
        }
        return bez(y0, y1, y2, y3, u);
    };
    const std::string out = dir + "/bezier.wav";
    r.ok("render --out " + out + " --to 8");
    const auto &L = r.fake.written[out][0];
    auto dbAround = [&](long long s) {
        double sum = 0;
        for (long long i = s - 240; i < s + 240; ++i) sum += (double)L[(size_t)i] * L[(size_t)i]; // ten periods of 1 kHz
        return 20.0 * std::log10(std::sqrt(sum / 480.0) / (0.5 / std::sqrt(2.0)));
    };
    double worst = 0;
    bool bends = false;
    for (double beat : {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0})
    {
        const std::string e = r.ok("eval ch_2.gain --at " + canonicalNumber(beat));
        const double said = std::atof(e.substr(e.find(" = ") + 3).c_str());
        const double heard = dbAround(std::llround(beat * 24000)), want = formula(beat);
        if (std::fabs(said - want) > 1e-6 || std::fabs(heard - want) > 0.05)
            std::printf("    beat %.0f: eval %.6f dB, rendered %.4f dB, the formula %.6f dB\n", beat, said, heard, want);
        assert(std::fabs(said - want) < 1e-6);                         // eval IS the formula
        assert(std::fabs(heard - want) < 0.05);                        // and what renders
        worst = std::max(worst, std::fabs(heard - want));
        bends |= std::fabs(want - (-24.0 + 3.0 * beat)) > 1.0;         // not the straight line
    }
    assert(bends);
    // the model's value at the playhead is the same evaluator's, there
    r.ok("transport seek 2");
    const std::string e2 = r.ok("eval ch_2.gain --at 2");
    assert(std::fabs(r.svc.model().automations[0].now - std::atof(e2.substr(e2.find(" = ") + 3).c_str())) < 1e-9); // eval's, printed
    assert(std::fabs(r.svc.model().automations[0].now - formula(2.0)) < 1e-9);
    assert(contains(r.ok("state print --json"), "\"now\":") && !contains(r.ok("state print --json --stable"), "\"now\":"));
    // … and follows the transport while playing
    r.ok("transport play --from 1");
    r.ok("wait 0.3");
    const double pos = r.svc.model().transport.position, now = r.svc.model().automations[0].now;
    assert(pos > 1.0 && std::fabs(now - formula(pos)) < 1e-3);
    r.ok("transport stop");
    // refusals name what is wrong — and change nothing
    const std::string before = r.text();
    assert(contains(r.no("auto point shape au_1 --at 16 --speed-in 2"), "shape a BEZIER point's handles; the point at beat 16.0 is linear"));
    assert(contains(r.no("auto point shape au_1 --at 0 --shape hold --speed-out 1"), "is hold"));
    assert(contains(r.no("auto point shape au_1 --at 0 --influence-out 0"), "0 < x ≤ 100 %, not `0`"));
    assert(contains(r.no("auto point shape au_1 --at 0 --influence-in 150"), "--influence-in is how far"));
    assert(contains(r.no("auto point shape au_1 --at 0 --speed-out fast"), "--speed-out must be a number"));
    assert(contains(r.no("auto point shape au_1 --at 0"), "needs --shape"));
    assert(contains(r.no("auto point shape au_1 --at 3 --shape bezier"), "no point at beat 3.0"));
    assert(contains(r.no("auto point shape au_1 --at 0 --speed 2"), "speed-in"));  // an unknown flag names the nearest
    assert(r.text() == before);
    // the file keeps it, Interstellar's spelling, and reads back the same curve
    assert(contains(r.text(), "#point at=0.0 value=-24.0 shape=bezier speedIn=0.0 inflIn=33.333 speedOut=0.0 inflOut=80.0"));
    // a point made linear again drops its handles — in the song as in the file
    r.ok("auto point shape au_1 --at 8 --shape linear");
    assert(r.svc.model().automations[0].points[1].speedIn == 0.0 && r.svc.model().automations[0].points[1].inflIn == 33.333);
    assert(!contains(r.text(), "speedIn=1.5"));
    pass("a bezier gain: rendered level, eval and After Effects' cubic agree at beats 1…7 (eval to 1e-6 dB, the render within 0.05 dB); "
         "now follows the playhead; handle refusals named (R-AUTO-10, R-AUTO-11)");
    std::printf("    worst rendered error %.4f dB\n", worst);
}

static void test_routing_only_goes_forward_and_refusals_change_nothing()
{
    Run r;
    r.ok("project new " + freshSong("routes"));
    r.ok("clip add --src kick.wav");
    const std::string before = r.text();
    const std::string err = r.no("route ch_1 --to ch_2");               // Main (Buses) → kick (Sources)
    assert(contains(err, "ch_1 (Main, on Buses) output → ch_2 (kick, on Sources)") && contains(err, "R-MIX-4"));
    assert(r.text() == before);                                       // all-or-nothing
    assert(contains(r.no("send add ch_1 --to ch_2"), "R-MIX-4"));
    assert(contains(r.no("mixer move mx_2 --to 0"), "R-MIX-4"));      // Buses before Sources would turn every route back
    assert(r.text() == before);
    assert(contains(r.no("mixer delete mx_1"), "still holds ch_2"));
    r.ok("mixer add Stems");
    r.ok("strip add --kind bus --name Drums --mixer mx_3");
    assert(r.strip("ch_3")->out == "master");                         // nothing later than Stems: master
    r.ok("mixer move mx_3 --to 1");                                   // Sources, Stems, Buses
    r.ok("route ch_3 --to ch_1");                                     // Stems → Buses: forward
    r.ok("route ch_2 --to ch_3");
    assert(r.strip("ch_2")->out == "ch_3" && r.svc.model().mixers[1].name == "Stems");
    r.ok("route ch_2 --to prt_1");                                    // straight to a port
    r.ok("route ch_2 --to master");
    assert(r.strip("ch_2")->out == "master");
    // what a matrix or a picker may offer — the same rule, published (Sources, Stems, Buses)
    assert(r.strip("ch_2")->targets == std::vector<std::string>({"ch_3", "ch_1", "master", "prt_1"}));
    assert(r.strip("ch_3")->targets == std::vector<std::string>({"ch_1", "master", "prt_1"}));
    assert(r.strip("ch_1")->targets == std::vector<std::string>({"master", "prt_1"}));
    const std::vector<std::string> offered = r.strip("ch_2")->targets;                 // a copy: each command rebuilds the model
    for (const auto &t : offered) r.ok("route ch_2 --to " + t);                       // every one offered is taken
    pass("routing only goes forward — route, send, mixer move — and a refused command changes nothing; `targets` offers exactly that (R-MIX-4)");
}

static void test_set_and_get_through_the_registry()
{
    Run r;
    r.ok("project new " + freshSong("params"));
    assert(r.ok("strip add --kind instrument --instrument synth --name Lead") == "ch_2\nmade: device=dv_1\n");
    const StripModel *lead = r.strip("ch_2");
    assert(lead->devices.size() == 1 && lead->devices[0].type == "synth" && lead->devices[0].instrument);
    r.events.clear();
    r.ok("set dv_1.filter.cutoff=800 dv_1.osc1.wave=sine ch_2.gain=-6");
    assert(r.events.size() == 3 && r.events[0] == "[evt] params.changed address=dv_1.filter.cutoff value=800.0");
    assert(r.events[1] == "[evt] params.changed address=dv_1.osc1.wave value=sine");
    assert(r.ok("get dv_1.filter.cutoff") == "800.0\n" && r.ok("get dv_1.osc1.wave") == "sine\n");
    assert(r.ok("get dv_1.filter.res") == "0.25\n");                   // not stored = the registry default
    // R-SVC-8: out of the registry's range is REFUSED naming the range — as a strip's gain is — and nothing changes
    assert(r.no("set dv_1.filter.cutoff=99999") == "dv_1.filter.cutoff must be between 20.0 and 20000.0 Hz, got 99999");
    assert(r.ok("get dv_1.filter.cutoff") == "800.0\n");
    assert(contains(r.no("set dv_1.filter.cutof=1"), "no field `filter.cutof` (did you mean: filter.cutoff?)"));
    assert(contains(r.no("set dv_1.osc1.wave=saww"), "one of: sine, saw, square, triangle"));
    assert(contains(r.no("set ch_2.gian=1"), "did you mean: gain?"));
    assert(contains(r.no("set ch_2.gain=40"), "between -120.0 and 12.0"));
    assert(contains(r.no("set ch_9.gain=1"), "no node `ch_9`"));
    // a line is atomic: the second address fails, so the first does not land and nothing is announced
    r.events.clear();
    r.no("set ch_2.gain=-1 ch_2.pan=7");
    assert(r.ok("get ch_2.gain") == "-6.0\n");
    for (const auto &e : r.events) assert(!contains(e, "params.changed"));
    assert(contains(r.svc.model().lastError, "pan must be between"));
    assert(contains(r.no("get ch_2.nothing"), "no field `nothing`"));
    r.ok("device add ch_2 --type eq");
    r.ok("set dv_2.peak2.gain=4.5 dv_2.bypass=true");
    const DeviceModel &eq = r.strip("ch_2")->devices[1];
    assert(eq.bypass && eq.label == "EQ");
    bool seen = false;
    for (const auto &p : eq.params)
        if (p.name == "peak2.gain") { seen = true; assert(p.value == 4.5 && p.text == "4.5" && p.unit == "dB" && p.max == 18.0); }
    assert(seen);
    assert(contains(r.no("device add ch_2 --type drums"), "ONE instrument"));
    assert(contains(r.no("device remove dv_1"), "keeps its instrument"));
    assert(contains(r.no("device add ch_2 --type flanger"), "no device type `flanger`"));
    r.ok("device add master --type compressor");
    assert(r.svc.model().masterDevices.size() == 1);
    pass("set/get read every device parameter from the DSP registry: out of range refused, choices by name, typos refused, lines atomic (R-DSP-2/3, R-SVC-3)");
}

static void test_patterns_are_shared_by_their_clips()
{
    Run r;
    r.ok("project new " + freshSong("patterns"));
    r.ok("strip add --kind instrument --instrument drums --name Drums");
    assert(r.ok("clip add --strip ch_2 --at 0 --length 16") == "ac_1\nmade: pattern=pt_1 lane=ln_1\n");
    assert(r.clip("ac_1")->pattern == "pt_1" && r.clip("ac_1")->length == 16.0);
    r.ok("clip duplicate ac_1");
    assert(r.clip("ac_2")->at == 16.0 && r.clip("ac_2")->pattern == "pt_1" && r.clip("ac_2")->linked == 2);
    r.ok("note add pt_1 --pitch 36 --at 0");
    r.ok("note add pt_1 --pitch 42 --at 0.5 --vel 70");
    r.ok("note add pt_1 --pitch 36 --at 0 --vel 90");                 // the same note again replaces, not stacks
    assert(r.svc.model().patterns[0].notes.size() == 2 && r.svc.model().patterns[0].notes[0].vel == 90);
    r.ok("clip unique ac_2");
    assert(r.clip("ac_2")->pattern == "pt_2" && r.clip("ac_1")->linked == 1);
    r.ok("note delete pt_2 --pitch 42 --at 0.5");
    assert(r.svc.model().patterns[0].notes.size() == 2 && r.svc.model().patterns[1].notes.size() == 1);
    assert(contains(r.no("note delete pt_2 --pitch 42 --at 0.5"), "no note 42"));
    assert(contains(r.no("clip unique ac_9"), "no clip"));
    pass("a duplicated note clip plays the same pattern (linked); `clip unique` gives it its own (R-CLIP-2/3)");
}

static void test_solo_mute_matrix_and_audit()
{
    Run r;
    r.ok("project new " + freshSong("mix"));
    r.ok("clip add --src kick.wav");                                   // ch_2 → Main
    r.ok("clip add --src hat.wav");                                    // ch_3 → Main
    r.ok("strip add --kind bus --name Verb");                          // ch_4 on Buses, placed BEFORE Main → Main
    r.ok("send add ch_3 --to ch_4 --gain -6 --pre");
    r.ok("set ch_2.solo=true");
    const AppModel &m = r.svc.model();
    auto audible = [&](const std::string &id) { return r.strip(id)->audible; };
    assert(audible("ch_2") && audible("ch_1") && !audible("ch_3") && !audible("ch_4")); // the solo's path stays alive
    r.ok("set ch_2.solo=false ch_3.solo=true");
    assert(audible("ch_3") && audible("ch_1") && audible("ch_4") && !audible("ch_2"));   // its send's return too
    r.ok("set ch_3.solo=false ch_2.mute=true");
    assert(!audible("ch_2") && audible("ch_3"));
    // R-MIX-9: every route has a cell — a key to a strip on its own mixer too; the columns are published once
    r.ok("send add ch_2 --to ch_3 --sidechain --gain -3");
    const std::vector<std::string> cols = {"ch_3", "ch_4", "ch_1", "master", "prt_1"};
    assert(m.matrix.columns == cols);
    const std::string mx = r.ok("matrix print");
    assert(contains(mx, "strip\tch_3\tch_4\tch_1\tmaster\tprt_1\n"));
    assert(contains(mx, "ch_2\t-3.0key\t\t●\t\t\n"));                 // its key to ch_3, on Sources like it
    assert(contains(mx, "ch_3\t\t-6.0pre\t●\t\t\n"));
    assert(contains(mx, "ch_4\t\t\t●\t\t\n"));                       // the new bus feeds Main
    const std::string js = r.ok("matrix print --json");
    assert(contains(js, "\"ch_4\": \"-6.0pre\"") && contains(js, "\"ch_3\": \"-3.0key\""));
    const std::string a = r.ok("audit");
    assert(contains(a, "silent: clip ac_1 plays through ch_2 (kick), which is muted"));
    assert(contains(a, "single input: bus ch_4 (Verb) is fed by one strip only"));
    assert(m.audit.size() >= 2);
    r.ok("strip add --kind audio --name Empty");
    assert(contains(r.ok("audit"), "unused: ch_5 (Empty) has no clips"));
    pass("solo keeps the soloed path audible; mute silences; the matrix and the audit read the mix (R-MIX-7/9/10)");
}

static void test_render_writes_the_mix_deterministically()
{
    Run r;
    const std::string dir = scratch();
    r.ok("project new " + freshSong("render") + " --bpm 120");
    r.ok("strip add --kind instrument --instrument drums --name Drums");
    r.ok("clip add --strip ch_2 --at 1 --length 4");                   // one bar of kicks from beat 1
    r.ok("note add pt_1 --pitch 36 --at 0");
    r.ok("note add pt_1 --pitch 36 --at 2");
    r.ok("set pt_1.length=4");
    r.events.clear();
    const std::string out = dir + "/mix.wav";
    r.ok("render --out " + out + " --stems ch_2,ch_1 --ports");
    assert(r.fake.written.count(out) && r.fake.written.count(dir + "/mix.ch_2.wav") && r.fake.written.count(dir + "/mix.Main.wav"));
    assert(r.fake.bits[out] == 24);
    const auto &L = r.fake.written[out][0];
    // beat 1 at 120 bpm = 24000 samples: silence before, the kick from there
    float before = 0, after = 0;
    for (int i = 0; i < 24000; ++i) before = std::max(before, std::fabs(L[i]));
    for (int i = 24000; i < 26400; ++i) after = std::max(after, std::fabs(L[i]));
    assert(before == 0.0f && after > 0.05f);
    // the tail ran past the end (beat 5 = 120000) and then stopped, under the 10 s cap
    assert(L.size() > 120000 && L.size() < 120000 + 480000);
    assert(contains(r.events.back(), "[evt] render.finished out=" + out));
    // the same song renders the same samples; --to is exact
    r.ok("render --out " + dir + "/again.wav");
    assert(r.fake.written[dir + "/again.wav"] == r.fake.written[out]);
    r.ok("render --out " + dir + "/bar.wav --from 1 --to 3 --bits 32f");
    assert(r.fake.written[dir + "/bar.wav"][0].size() == 48000 && r.fake.bits[dir + "/bar.wav"] == 32);
    assert(contains(r.no("render --out x.wav --stems ch_9"), "no strip"));
    assert(r.no("render") == "render needs --out <file>");
    pass("render: the mixdown, stems and ports; silent before the first note, the tail to −90 dBFS, byte-identical twice (R-RENDER-1…3)");
}

static void test_the_dump_and_the_document()
{
    Run r;
    r.ok("project new " + freshSong("dump") + " --name Song");
    r.ok("clip add --src kick.wav");
    r.ok("strip add --kind instrument --instrument synth");
    r.ok("clip add --strip ch_3");
    r.ok("note add pt_1 --pitch 60 --at 0");
    r.ok("send add ch_2 --to prt_1");
    r.ok("device add master --type eq");
    const std::string stable = r.ok("state print --json --stable"), full = r.ok("state print --json");
    assert(!contains(stable, "\"revision\"") && contains(full, "\"revision\""));
    // every key the codec writes is a field the API document lists (the drift test of R-API-1)
    std::set<std::string> listed;
    for (const auto &f : appModelFields())
    {
        const auto dot = f.path.rfind('.');
        std::string last = dot == std::string::npos ? f.path : f.path.substr(dot + 1);
        if (last.size() > 2 && last.compare(last.size() - 2, 2, "[]") == 0) last.resize(last.size() - 2);
        listed.insert(last);
    }
    std::istringstream in(full);
    std::string line;
    while (std::getline(in, line))
    {
        const auto q = line.find('"');
        const auto c = line.find("\": ");
        if (q == std::string::npos || c == std::string::npos || c < q) continue;
        const std::string key = line.substr(q + 1, c - q - 1);
        if (!listed.count(key)) std::printf("    `%s` is written but not documented\n", key.c_str());
        assert(listed.count(key));
    }
    // two runs of the same lines give the same stable dump
    Run r2;
    r2.ok("project new " + freshSong("dump2") + " --name Song");
    r2.ok("clip add --src kick.wav");
    r2.ok("strip add --kind instrument --instrument synth");
    r2.ok("clip add --strip ch_3");
    r2.ok("note add pt_1 --pitch 60 --at 0");
    r2.ok("send add ch_2 --to prt_1");
    r2.ok("device add master --type eq");
    std::string s2 = r2.ok("state print --json --stable");
    const auto strip = [](const std::string &s) { // the two songs' FILE PATHS differ (and so their recents); nothing else may
        std::istringstream in(s);
        std::string line, out;
        while (std::getline(in, line))
            if (!contains(line, "\"projectPath\"") && !contains(line, "\"path\"")) out += line + "\n";
        return out;
    };
    assert(strip(stable) == strip(s2));
    const std::string json = r.ok("api --json"), md = r.ok("api --md");
    for (const auto &s : commandSpecs()) assert(contains(json, "\"verb\": \"" + s.verb + "\"") && contains(md, s.verb));
    for (const char *t : {"synth", "drums", "compressor", "eq", "reverb", "delay", "chorus", "drive", "filter"})
        assert(contains(json, std::string("\"type\": \"") + t + "\"") && contains(md, std::string("### `") + t + "`"));
    assert(contains(md, "| `filter.cutoff` | 20.0 … 20000.0 Hz | 2400.0 Hz |"));
    assert(r.ok("state print") == "screen project · Song · 120.0 bpm · 2 mixers · 3 strips · 2 lanes · 2 clips · unsaved\n");
    pass("state print --stable is deterministic and every key is documented; api lists every verb and every DSP parameter (R-API-1)");
}

// R-SET-4: every shortcut is an action whose keys are this machine's settings — read in any spelling, kept in one,
// refused when another action of the same keyboard has the combo, published with the defaults, and saved
static void test_keys_are_settings()
{
    // the combo, spelt one way: the window spells a press with it, the service a setting
    std::string c, err;
    assert(parseCombo("shift+ctrl+z", c, err) && c == "Ctrl+Shift+Z");
    assert(parseCombo(" alt + f12 ", c, err) && c == "Alt+F12");
    assert(parseCombo("Ctrl+,", c, err) && c == "Ctrl+Comma" && comboText(188, true, false, false) == c);
    assert(comboText('A', false, false, false) == "A" && comboText(7, false, false, false).empty()); // no such key
    assert(!parseCombo("Ctrl", c, err) && contains(err, "names no key"));
    assert(!parseCombo("Ctrl+Foo", c, err) && contains(err, "no key `Foo`"));
    assert(!parseCombo("Meta+A", c, err) && contains(err, "not Ctrl, Alt or Shift"));
    assert(keyCodeOf("F13") == 0 && keyCodeOf("pgdn") == 34 && keyCodeOf("space") == 32);
    std::vector<std::string> cs;
    assert(parseCombos("Ctrl+Y,ctrl+y, Ctrl+Shift+Z", cs, err) && cs == (std::vector<std::string>{"Ctrl+Y", "Ctrl+Shift+Z"}));
    assert(parseCombos("none", cs, err) && cs.empty() && combosText(cs) == "none");
    assert(!parseCombos("Ctrl+Z,,F5", cs, err) && contains(err, "an empty combo"));

    const std::string settings = scratch() + "/keys-settings.txt", recents = scratch() + "/keys-recents";
    fs::remove(settings);
    {
        Run r(settings, recents);
        const auto keyOf = [&](const std::string &id) -> const KeyActionModel & {
            for (const auto &k : r.svc.model().settings.keys)
                if (k.id == id) return k;
            assert(!"no such action");
            return r.svc.model().settings.keys[0];
        };
        // every action is published with its keys and its defaults, in the sheet's order
        assert(r.svc.model().settings.keys.size() == keyActions().size() && r.svc.model().settings.keys[0].id == "transport.play");
        assert(keyOf("transport.play").keys == std::vector<std::string>{"Space"} && keyOf("transport.stopHere").keys == std::vector<std::string>{"Ctrl+Space"});
        assert((keyOf("song.redo").keys == std::vector<std::string>{"Ctrl+Shift+Z", "Ctrl+Y"}) && keyOf("roll.octaveUp").area == "Piano roll");
        // set: any spelling in, the canonical one kept; the event names the action
        r.events.clear();
        r.ok("settings set key.transport.play=\"shift+ctrl+p, F5\"");
        assert((keyOf("transport.play").keys == std::vector<std::string>{"Ctrl+Shift+P", "F5"}) && keyOf("transport.play").defaults == std::vector<std::string>{"Space"});
        assert(r.events.size() == 1 && contains(r.events[0], "settings.changed what=key.transport.play"));
        assert(contains(r.ok("settings print"), "key.transport.play = Ctrl+Shift+P, F5"));
        // refused, naming why — and nothing changes
        assert(contains(r.no("settings set key.transport.plya=F6"), "did you mean: key.transport.play?"));
        assert(contains(r.no("settings set key.song.save=Ctrl+Nope"), "no key `Nope`"));
        assert(contains(r.no("settings set key.song.save=Ctrl+D"), "Ctrl+D is already \"Duplicate the clip\" (key.lanes.duplicate)"));
        assert(contains(r.no("settings set key.lanes.delete=F5"), "(key.transport.play)"));     // the song's areas are one keyboard
        assert(contains(r.no("settings set key.roll.copy=Ctrl+V"), "(key.roll.paste)"));          // the roll's is one too
        assert(keyOf("song.save").keys == std::vector<std::string>{"Ctrl+S"} && keyOf("lanes.delete").keys.size() == 2);
        // the roll's keys may be the song's (they shadow them while a roll has the keys); one line may swap two
        r.ok("settings set key.roll.selectAll=Ctrl+S");
        r.ok("settings set key.lanes.duplicate=none key.song.save=Ctrl+D");
        assert(keyOf("song.save").keys == std::vector<std::string>{"Ctrl+D"} && keyOf("lanes.duplicate").keys.empty());
        // default: back to the defaults, and no line in the file for it
        r.ok("settings set key.roll.selectAll=default key.song.save=Ctrl+S");
        assert(keyOf("roll.selectAll").keys == std::vector<std::string>{"Ctrl+A"});
        assert(!contains(r.ok("settings print"), "key.roll.selectAll") && !contains(r.ok("settings print"), "key.song.save"));
        assert(contains(r.ok("settings print --json"), "\"lanes.duplicate=none\""));
    }
    // the machine's keys survive the process; a hand-edited line that does not read is the defaults
    Run again(settings, recents);
    for (const auto &k : again.svc.model().settings.keys)
    {
        if (k.id == "transport.play") assert((k.keys == std::vector<std::string>{"Ctrl+Shift+P", "F5"}));
        if (k.id == "lanes.duplicate") assert(k.keys.empty());
    }
    const Settings hand = Settings::parse("key.song.save = Ctrl+Nope\nkey.song.undo = F9\n");
    assert(keysOf(*keyAction("song.save"), hand.keys) == std::vector<std::string>{"Ctrl+S"});
    assert(keysOf(*keyAction("song.undo"), hand.keys) == std::vector<std::string>{"F9"});
    pass("Hotkeys are settings (R-SET-4): every action published with its keys and defaults; key.<action> takes any spelling, keeps one, refuses a key another action of its keyboard has, none / default; saved");
}

// R-SAFE-1/2: an unsaved song is written beside itself — on the clock, and at once after a removal; a save or a close
// drops it; a session that died leaves it, the next open offers it, `project recover` restores it
static void test_autosave_and_recovery()
{
    const std::string settings = scratch() + "/as-settings.txt", recents = scratch() + "/as-recents";
    fs::remove(settings);
    fs::remove(recents);
    const std::string song = freshSong("autosave"), as = song + ".autosave";
    fs::remove(as);
    auto slurp = [](const std::string &path) {
        std::ifstream f(path);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    {
        Run r(settings, recents);
        r.ok("settings set autosave=1");
        r.ok("project new " + song + " --bpm 120");
        r.ok("clip add --instrument drums --at 0 --length 4");          // an edit, not yet a second after opening
        assert(!fs::exists(as));
        r.events.clear();
        r.ok("wait 1.2");                                                // the clock: written beside it
        assert(fs::exists(as) && contains(slurp(as), "#aclip") && !contains(slurp(song), "#aclip"));
        bool said = false;
        for (const auto &e : r.events) said |= contains(e, "project.autosaved path=" + as);
        assert(said && !fs::exists(as + ".tmp"));                       // atomic: the temporary file is gone
        // a removal is written at once, not on the clock
        r.ok("clip add --instrument synth --at 4 --length 4");
        const std::string made = r.svc.model().clips.back().id;
        r.ok("wait 1.2");
        assert(contains(slurp(as), "id=" + made));
        r.ok("clip delete " + made);
        assert(!contains(slurp(as), "id=" + made));
        // a save drops it
        r.ok("project save");
        assert(!fs::exists(as));
        // edits after the save, then the session dies: no save, no close
        r.ok("set ch_2.gain=-6");
        r.ok("wait 1.2");
        assert(fs::exists(as) && contains(slurp(as), "gain=-6"));
    }
    {
        Run again(settings, recents);
        assert(!again.svc.model().recents.empty() && again.svc.model().recents[0].recovery); // Home's card says so
        const std::string out = again.ok("project open " + song);
        assert(contains(out, "recovery: " + as) && again.svc.model().recovery == song && again.svc.model().recoveryAge >= 0.0);
        assert(again.svc.model().strips[0].gain == 0.0 || again.svc.model().strips[1].gain == 0.0); // the SAVED song is what opened
        again.ok("project recover " + song);
        bool recovered = false;
        for (const auto &s : again.svc.model().strips) recovered |= s.id == "ch_2" && s.gain == -6.0;
        assert(recovered && again.svc.model().dirty && again.svc.model().recovery.empty() && again.svc.model().history[0].label == "Recovered");
        again.ok("project save");
        assert(!fs::exists(as) && contains(slurp(song), "gain=-6"));
        // refused, naming why
        assert(contains(again.no("project recover " + song), "no autosave for"));
        assert(contains(again.no("project discard-recovery " + song), "no autosave for"));
        // an unsaved edit, the session dies again; this time the saved one is kept
        again.ok("set ch_2.gain=-3");
        again.ok("wait 1.2");
        assert(fs::exists(as));
    }
    {
        Run third(settings, recents);
        assert(contains(third.ok("project open " + song), "recovery: "));
        third.ok("project discard-recovery " + song);
        assert(!fs::exists(as) && third.svc.model().recovery.empty());
        // closing is choosing: an unsaved edit's autosave goes with the close
        third.ok("set ch_2.gain=-1");
        third.ok("wait 1.2");
        assert(fs::exists(as));
        third.ok("project close");
        assert(!fs::exists(as));
        // off: nothing written
        third.ok("settings set autosave=off");
        third.ok("project open " + song);
        third.ok("set ch_2.gain=-2");
        third.ok("clip delete ac_1");
        third.ok("wait 1.2");
        assert(!fs::exists(as));
        assert(contains(third.no("settings set autosave=0"), "seconds from 1 to 3600, or off"));
    }
    pass("Autosave and recovery (R-SAFE-1/2): written beside an unsaved song on the clock and at once after a removal, atomically; a save or a close drops it; a dead session leaves it — Home marks the card, `project open` offers it, `project recover` restores it unsaved, `discard-recovery` drops it; off writes nothing");
}

static void test_the_machine_settings_folders_devices_and_recents()
{
    const std::string settings = scratch() + "/settings.txt", recents = scratch() + "/recents";
    fs::remove(settings);
    fs::remove(recents);
    {
        Run r(settings, recents);
        r.ok("settings set sampleRate=44100 bufferSize=512 output=card_a port.Phones=card_b:0");
        assert(r.svc.model().settings.sampleRate == 44100 && r.svc.model().settings.latencyMs == 11.6);
        assert(r.svc.model().settings.ports == std::vector<std::string>{"Phones=card_b:0"});
        assert(contains(r.no("settings set sampleRat=1"), "did you mean: sampleRate?"));
        assert(contains(r.no("settings set bufferSize=7"), "32 … 8192"));
        assert(contains(r.no("settings set port.Main=card_a"), "<device>:<first channel>"));
        r.ok("settings set showIds=on");                                  // R-UI-11: an agent can turn the ids on too
        assert(r.svc.model().settings.showIds && contains(r.ok("settings print"), "showIds = on"));
        assert(contains(r.no("settings set showIds=yes"), "showIds is on or off"));
        r.ok("folder add /music/Samples");
        r.ok("folder add /music/Loops");
        r.ok("folder move /music/Loops --to 0");
        assert((r.svc.model().settings.folders == std::vector<std::string>{"/music/Loops", "/music/Samples"}));
        assert(contains(r.no("folder add /nowhere"), "cannot list /nowhere"));
        assert(contains(r.no("folder add /music/Loops"), "already a sample folder"));
        r.ok("folder remove /music/Loops");
        assert(r.ok("browse /music/Samples") == "dir\tLoops\nsong\ta.slp\naudio\tkick.wav\nother\tnotes.txt\n");
        assert(r.svc.model().browser.entries[0].kind == "dir");          // folders first
        assert(contains(r.ok("devices list"), "out\tcard_b\t2 ch · 44100 Hz\tHeadphones"));
        assert(r.svc.model().devices.size() == 3);
        // a new song starts at the machine's rate; it lands on Home's recent list
        const std::string a = freshSong("recent_a"), b = freshSong("recent_b");
        r.ok("project new " + a + " --name First");
        assert(r.svc.model().sampleRate == 44100);
        r.ok("project new " + b + " --name Second");
        r.ok("project close");
        const auto &cards = r.svc.model().recents;
        assert(cards.size() == 2 && cards[0].name == "Second" && cards[1].name == "First" && cards[0].strips == 1);
    }
    // the machine's side survives the process: a second service reads the same files
    Run again(settings, recents);
    assert(again.svc.model().settings.output == "card_a" && again.svc.model().settings.folders == std::vector<std::string>{"/music/Samples"});
    assert(again.svc.model().settings.showIds);
    assert(again.svc.model().recents.size() == 2);
    again.ok("recents remove " + again.svc.model().recents[0].path);
    assert(again.svc.model().recents.size() == 1 && again.svc.model().recents[0].name == "First");
    fs::remove(again.svc.model().recents[0].path);
    Run third(settings, recents);
    assert(third.svc.model().recents[0].missing);                         // a deleted song is shown as missing
    {
        // a song opened by a RELATIVE path is remembered by its absolute one: a window started elsewhere finds it
        const fs::path was = fs::current_path();
        fs::current_path(scratch());
        fs::remove("relative_song.slp");
        third.ok("project new relative_song.slp --name Rel");
        third.ok("project close");
        fs::current_path(was);
        const std::string abs = (fs::path(scratch()) / "relative_song.slp").lexically_normal().string();
        assert(third.svc.model().recents[0].path == abs && !third.svc.model().recents[0].missing);
        third.ok("recents remove " + abs);
    }
    pass("the machine: settings, sample folders, browse, devices, recents (by absolute path) — saved, and read back by a second process (R-SET, R-HOME-1, R-BROWSE-1)");
}

namespace
{
    void waitFrames(Run &r, long long n)
    {
        for (int i = 0; i < 2000 && r.fake.capture->frames.load() < n; ++i) r.ok("wait 0.005");
        assert(r.fake.capture->frames.load() >= n);
    }
    float peakOf(const std::vector<float> &v, size_t from, size_t to)
    {
        float p = 0;
        for (size_t i = from; i < to && i < v.size(); ++i) p = std::max(p, std::fabs(v[i]));
        return p;
    }
}

static void test_live_playback_is_the_offline_render()
{
    Run r;
    r.ok("project new " + freshSong("live") + " --bpm 120");
    r.ok("strip add --kind instrument --instrument drums");
    r.ok("clip add --strip ch_2 --length 8");
    r.ok("note add pt_1 --pitch 36 --at 0");
    r.ok("note add pt_1 --pitch 42 --at 0.5");
    r.ok("note add pt_1 --pitch 38 --at 1");
    r.ok("strip add --kind instrument --instrument synth");
    r.ok("clip add --strip ch_3 --length 8");
    r.ok("note add pt_2 --pitch 48 --at 0 --length 2");
    r.ok("strip add --kind bus --name Verb");
    r.ok("device add ch_4 --type reverb");
    r.ok("send add ch_3 --to ch_4 --gain -6");
    r.events.clear();
    r.ok("transport play");
    assert(r.svc.model().transport.playing && contains(r.events.back(), "[evt] transport.changed playing=1 position=0.0 loop=off"));
    waitFrames(r, 96000 + 4800); // 4 beats, and a tenth of a second more: the position is what is HEARD, a latency behind
    r.ok("transport stop");
    assert(!r.svc.model().transport.playing && r.svc.model().transport.position >= 4.0); // 4 beats heard
    r.ok("render --out " + scratch() + "/live.wav --ports --to 4 --bits 32f"); // the mix as mixed, undithered (R-EXPORT-1)
    const auto &port = r.fake.written[scratch() + "/live.Main.wav"];
    const auto &live = r.fake.capture->sink;
    for (size_t i = 0; i < 96000; ++i)
    {
        if (live[2 * i] != port[0][i] || live[2 * i + 1] != port[1][i]) std::printf("    frame %zu: live %g offline %g\n", i, live[2 * i], port[0][i]);
        assert(live[2 * i] == port[0][i] && live[2 * i + 1] == port[1][i]);
    }
    assert(peakOf(live, 0, 2 * 96000) > 0.05f);
    pass("live playback on the clock device is the offline render, sample for sample, over 4 beats (R-PLAY-1)");
}

static void test_the_metronome_clicks_live_and_never_in_a_render()
{
    Run r;
    const std::string dir = scratch();
    r.ok("project new " + freshSong("click") + " --bpm 120");       // an empty song: every sound is the click
    r.ok("settings set metronome=on metronomeLevel=0");
    r.ok("transport play");
    waitFrames(r, 4 * 24000 + 4800);
    r.ok("transport stop");
    const auto &live = r.fake.capture->sink;
    // a click on every beat (24 000 frames at 120 bpm), starting on its exact sample
    for (int k = 0; k <= 4; ++k)
    {
        const size_t at = (size_t)k * 24000;
        const float on = peakOf(live, 2 * at, 2 * (at + 2400));
        const float before = k == 0 ? 0.0f : peakOf(live, 2 * (at - 48), 2 * at);
        if (!(on > 0.05f && before < 0.1f * on)) std::printf("    beat %d: click %.4f, just before %.4f\n", k, on, before);
        assert(on > 0.05f && before < 0.1f * on);
    }
    // the bar's first is another pad: beat 0 (cowbell) is not beat 1 (rim)
    float diff = 0;
    for (size_t i = 0; i < 2400; ++i) diff = std::max(diff, std::fabs(live[2 * i] - live[2 * (24000 + i)]));
    assert(diff > 0.05f);
    // off while playing: heard at once — the next beats are silent
    const size_t mark = live.size() / 2;
    r.ok("transport play");
    r.ok("settings set metronome=off");
    waitFrames(r, (long long)mark + 3 * 24000);
    r.ok("transport stop");
    assert(peakOf(r.fake.capture->sink, 2 * (mark + 24000), 2 * (mark + 3 * 24000)) == 0.0f);
    // never in a render: on or off, the same bytes (here: silence)
    r.ok("settings set metronome=on");
    r.ok("render --out " + dir + "/on.wav --to 4");
    r.ok("settings set metronome=off");
    r.ok("render --out " + dir + "/off.wav --to 4");
    assert(r.fake.written[dir + "/on.wav"] == r.fake.written[dir + "/off.wav"]);
    assert(peakOf(r.fake.written[dir + "/on.wav"][0], 0, 96000) == 0.0f);
    pass("metronome: a click on every beat's exact sample while playing, the bar's first another pad, off at once; never in a render (R-TIME-4, R-EDM-2)");
}

static void test_the_metronome_follows_the_tempo_map()
{
    // R-TEMPO-1: 120 bpm, then 60 from beat 2 — playing, a click on each beat where the map puts it (0, 0.5, 1, 2, 3 s),
    // none where 120's grid would have put beat 3; stopped, the time bar's beat and tempo through the map
    Run r;
    r.ok("project new " + freshSong("clickmap") + " --bpm 120");
    r.ok("tempo add --at 2 --bpm 60");
    r.ok("settings set metronome=on metronomeLevel=0");
    r.ok("transport play");
    waitFrames(r, 144000 + 4800);
    r.ok("transport stop");
    const auto &live = r.fake.capture->sink;
    for (size_t at : {(size_t)0, (size_t)24000, (size_t)48000, (size_t)96000, (size_t)144000})
    {
        const float on = peakOf(live, 2 * at, 2 * (at + 2400)), before = at == 0 ? 0.0f : peakOf(live, 2 * (at - 48), 2 * at);
        if (!(on > 0.05f && before < 0.1f * on)) std::printf("    at %zu: click %.4f, just before %.4f\n", at, on, before);
        assert(on > 0.05f && before < 0.1f * on);
    }
    assert(peakOf(live, 2 * 72000, 2 * (72000 + 2400)) == 0.0f); // 120's beat 3: nothing there now
    const double pos = r.svc.model().transport.position;
    assert(pos > 3.0 && std::fabs(r.svc.model().transport.tempo - 60.0) < 1e-9);
    // played from beat 3: it starts on that beat's sample — its click at once, the next a second on
    const size_t mark = live.size() / 2;
    r.ok("transport play --from 3");
    waitFrames(r, (long long)mark + 48000 + 4800);
    r.ok("transport stop");
    const auto &again = r.fake.capture->sink;
    const float first = peakOf(again, 2 * mark, 2 * (mark + 2400)), next = peakOf(again, 2 * (mark + 48000), 2 * (mark + 48000 + 2400));
    std::printf("    from beat 3: %.3f at once, %.3f a second on, %.4f between\n", first, next, peakOf(again, 2 * (mark + 12000), 2 * (mark + 36000)));
    assert(first > 0.05f && next > 0.05f && peakOf(again, 2 * (mark + 12000), 2 * (mark + 36000)) == 0.0f);
    pass("the metronome follows the tempo map (R-TEMPO-1): a click where the map puts each beat — none on the old grid; the time bar's beat and tempo through it; played from beat 3, its sample");
}

static void test_audition_plays_a_sample_outside_the_song()
{
    Run r;
    r.ok("project new " + freshSong("audition") + " --bpm 120");
    r.ok("settings set auditionLevel=-6");
    r.events.clear();
    // the browser's preview: heard now, outside the song (R-EDM-9) — not an edit
    r.ok("audition kick.wav");
    assert(r.svc.model().audition.playing && contains(r.events.back(), "[evt] audition.changed") && contains(r.events.back(), "playing=1"));
    assert(!r.svc.model().dirty);                                    // not an edit: the song stays saved
    waitFrames(r, 24000);                                             // the fake decoder: half a second at 48 kHz
    const auto &live = r.fake.capture->sink;
    const double g = std::pow(10.0, -6.0 / 20.0);
    bool exact = true;
    for (int i = 0; i < 24000; ++i)
    {
        const float x = (float)(0.5 * std::sin(2 * M_PI * 1000.0 * i / 48000));
        exact &= live[(size_t)(2 * i)] == (float)(g * x) && live[(size_t)(2 * i + 1)] == (float)(g * x);
    }
    assert(exact);
    // to its end it stops itself: said in the model and the stream
    r.ok("wait 0.05");
    assert(!r.svc.model().audition.playing && r.svc.model().audition.progress == 1.0);
    bool ended = false;
    for (const auto &e : r.events) ended |= contains(e, "[evt] audition.changed") && contains(e, "playing=0");
    assert(ended);
    // refusals name why; stop with nothing playing is fine
    assert(contains(r.no("audition missing.wav"), "cannot read missing.wav"));
    r.ok("audition stop");
    pass("audition: a sample heard now through the device at the audition level, sample for sample, outside the song (not an edit); it stops itself at its end, said (R-EDM-9)");
}

namespace
{
    // the preview's thread writes until a second of quiet, then closes: wait until the sink stops growing
    void waitQuiet(Run &r)
    {
        long long last = -1;
        for (int stable = 0, k = 0; stable < 4 && k < 400; ++k)
        {
            r.ok("wait 0.02");
            const long long now = r.fake.capture->frames.load();
            stable = now == last ? stable + 1 : 0;
            last = now;
        }
    }
}

static void test_a_note_preview_is_the_strip_heard_outside_the_song()
{
    Run r;
    r.ok("project new " + freshSong("preview") + " --bpm 120");
    r.ok("strip add --kind instrument --instrument synth");          // ch_2, its Basic Synth dv_1
    r.ok("set dv_1.osc1.wave=sine dv_1.osc2.level=0 dv_1.filter.cutoff=20000 dv_1.amp.attack=0 dv_1.amp.release=20");
    r.ok("set ch_2.gain=-3 ch_2.pan=0.25");                          // its fader and pan are part of its sound
    const int depth = r.svc.model().undoDepth;
    r.events.clear();
    r.ok("note preview ch_2 A4 --length 0.5 --vel 90");
    assert(contains(r.events.back(), "[evt] note.previewed strip=ch_2 pitch=69"));
    waitQuiet(r);
    assert(r.svc.model().undoDepth == depth);                          // not an edit
    std::vector<float> heard = r.fake.capture->sink;
    // the strip in the song, rendered: the same note on the same strip — the preview IS that sound
    r.ok("clip add --strip ch_2 --length 1");
    r.ok("note add pt_1 --pitch 69 --at 0 --length 0.5 --vel 90");
    r.ok("render --out " + scratch() + "/preview.wav --ports --to 1");
    const auto &port = r.fake.written[scratch() + "/preview.Main.wav"];
    float worst = 0.0f;
    for (size_t i = 0; i < 24000; ++i)
        worst = std::max({worst, std::fabs(heard[2 * i] - port[0][i]), std::fabs(heard[2 * i + 1] - port[1][i])});
    assert(worst < 1e-6f && peakOf(heard, 0, 2 * 12000) > 0.05f);
    // it ends: silence after its note and its release, then the stream closes
    assert(peakOf(heard, 2 * 14400, heard.size()) < 1e-4f);
    // a preview started while one rings takes over with a fade — never a jump the sine itself cannot make
    r.fake.capture->sink.clear();
    r.ok("set dv_1.amp.release=2000");                                // the first note would ring on
    r.ok("note preview ch_2 60 --length 2");
    r.ok("wait 0.01");
    r.ok("note preview ch_2 67 --length 0.25");
    waitQuiet(r);
    const auto &both = r.fake.capture->sink;
    float step = 0.0f, peak = 0.0f;
    for (size_t i = 2; i + 1 < both.size(); i += 2)
    {
        step = std::max(step, std::fabs(both[i] - both[i - 2]));
        peak = std::max(peak, std::fabs(both[i]));
    }
    assert(peak > 0.05f && step < 0.08f * peak);                       // a sine at 392 Hz moves ≤ 0.051·peak a sample
    // refusals name why; none is an edit
    assert(contains(r.no("note preview ch_1 60"), "plays no notes"));
    assert(contains(r.no("note preview ch_9 60"), "no strip 'ch_9'"));
    assert(contains(r.no("note preview ch_2 128"), "0–127"));
    assert(contains(r.no("note preview ch_2 60 --vel 0"), "--vel"));
    pass("`note preview`: a note through the strip's chain — its instrument, fader and pan — is the strip's sound in the song sample for sample, outside it (not an edit); it ends after its tail; a second takes over with a fade, no jump (R-ROLL-7)");
}

static void test_notes_move_and_delete_a_selection_as_one_edit()
{
    Run r;
    r.ok("project new " + freshSong("selection") + " --bpm 120");
    r.ok("pattern new --name Fig --length 4");
    r.ok("notes add pt_1 \"C4@0:2 E4@0.25:1.75 G4@0.5 C5@0.75 E5@1\"");
    const int depth = r.svc.model().undoDepth;
    // the upper three moved a beat later and up a fifth: ONE edit, the rest untouched
    r.ok("notes move pt_1 \"G4@0.5 C5@0.75 E5@1\" --by 1 --semi 7");
    // the notes as they are NOW: a reference held across a command dangles (each refresh rebuilds the model)
    auto notesNow = [&]() -> const std::vector<NoteModel> & { return r.svc.model().patterns[0].notes; };
    auto has = [&](int pitch, double at) {
        for (const auto &x : notesNow())
            if (x.pitch == pitch && std::fabs(x.at - at) < 1e-9) return true;
        return false;
    };
    assert(notesNow().size() == 5 && has(60, 0) && has(64, 0.25) && has(74, 1.5) && has(79, 1.75) && has(83, 2.0) && !has(67, 0.5));
    assert(r.svc.model().undoDepth == depth + 1);
    r.ok("undo");
    assert(has(67, 0.5) && has(72, 0.75) && has(76, 1.0));
    // refused whole, naming the first: no such note; onto a note not moved; out of range; before beat 0
    assert(contains(r.no("notes move pt_1 \"G4@0.5 A4@3\" --by 1"), "`A4@3`: pt_1 has no note there"));
    assert(contains(r.no("notes move pt_1 \"G4@0.5\" --by -0.5 --semi -7"), "would land on 60@0, which is not moved"));
    assert(contains(r.no("notes move pt_1 \"E5@1\" --semi 60"), "outside 0–127"));
    assert(contains(r.no("notes move pt_1 \"C4@0\" --by -1"), "before beat 0"));
    assert(has(67, 0.5) && notesNow().size() == 5);                             // nothing moved
    // a selection moved onto its own old places is fine (a chord moved up together)
    r.ok("notes move pt_1 \"C4@0 E4@0.25\" --semi 4");
    assert(has(64, 0) && has(68, 0.25));
    // deleted as one edit
    const int d2 = r.svc.model().undoDepth;
    r.ok("notes delete pt_1 \"G4@0.5 C5@0.75\"");
    assert(notesNow().size() == 3 && !has(67, 0.5) && !has(72, 0.75) && r.svc.model().undoDepth == d2 + 1);
    assert(contains(r.no("notes delete pt_1 \"G4@0.5\""), "has no note there"));
    pass("`notes move` / `notes delete`: a selection moved (beats and semitones) or removed as ONE edit; refused whole naming the first — no note, onto a note not moved, out of range, before 0 (R-ROLL-8)");
}

static void test_a_pattern_grows_to_hold_its_notes()
{
    Run r;
    r.ok("project new " + freshSong("grow") + " --bpm 120");
    r.ok("strip add --kind instrument --instrument synth");
    r.ok("clip add --strip ch_2 --length 4");                       // ac_1 plays pt_1 whole (4 beats)
    r.ok("clip add --strip ch_2 --pattern pt_1 --at 8 --length 2");  // ac_2: cut shorter
    r.ok("clip add --strip ch_2 --pattern pt_1 --at 16 --length 12"); // ac_3: looping on past it
    auto pt = [&]() { return r.svc.model().patterns[0]; };
    auto clipLen = [&](const std::string &id) {
        for (const auto &c : r.svc.model().clips)
            if (c.id == id) return c.length;
        return -1.0;
    };
    const int depth = r.svc.model().undoDepth;
    // a note written past the end: the pattern grows to the end of the bar that holds it, in the same edit
    assert(contains(r.ok("note add pt_1 --pitch 60 --at 5 --length 0.5"), "pt_1 grew to 8 beats to hold its notes; ac_1 with it"));
    assert(pt().length == 8.0 && clipLen("ac_1") == 8.0 && clipLen("ac_2") == 2.0 && clipLen("ac_3") == 12.0);
    assert(r.svc.model().undoDepth == depth + 1);
    r.ok("undo");                                                       // one step back takes both
    assert(pt().length == 4.0 && clipLen("ac_1") == 4.0 && pt().notes.empty());
    // a note within the end changes nothing; one resized past it grows it; one moved past it, and a selection
    r.ok("note add pt_1 --pitch 60 --at 3");
    assert(pt().length == 4.0);
    assert(contains(r.ok("note move pt_1 --pitch 60 --at 3 --length 2"), "grew to 8 beats"));
    assert(pt().length == 8.0 && clipLen("ac_1") == 8.0);
    r.ok("notes add pt_1 \"E4@0 G4@1\"");
    assert(contains(r.ok("notes move pt_1 \"E4@0 G4@1\" --by 9"), "grew to 12 beats"));
    assert(pt().length == 12.0 && clipLen("ac_1") == 12.0);
    // the end set back by hand stays (R-ROLL-5); the audit names what it leaves outside
    r.ok("set pt_1.length=4");
    assert(pt().length == 4.0 && contains(r.ok("audit"), "past its end: pt_1"));
    pass("A pattern grows to hold its notes (R-ROLL-9): added, resized or moved past the end, it reaches the end of that bar in the same edit (one undo); a clip that played it whole grows with it, one cut shorter or looping on stays; set back by hand it stays, and the audit names what is outside");
}

static void test_edits_while_playing_are_heard()
{
    Run r;
    r.ok("project new " + freshSong("liveedit") + " --bpm 120");
    r.ok("transport play");                                     // an empty song: silence…
    waitFrames(r, 12000);
    r.ok("clip add --src tone.wav --length 64");                // …a STRUCTURAL edit: a new engine swapped in
    const long long added = r.fake.capture->frames.load();
    waitFrames(r, added + 24000);
    r.ok("set ch_2.gain=-120");                                 // a LIVE edit: a message, the next block
    const long long muted = r.fake.capture->frames.load();
    waitFrames(r, muted + 24000);
    r.ok("transport stop");
    const auto &v = r.fake.capture->sink;
    assert(peakOf(v, 0, 2 * 11000) == 0.0f);                    // before the clip existed
    assert(peakOf(v, 2 * (added + 4096), 2 * (added + 12000)) > 0.3f);  // the tone, after the swap
    assert(peakOf(v, 2 * (muted + 4096), 2 * (muted + 24000)) < 1e-5f); // gone after −120 dB
    assert(contains(r.no("transport play --from -1"), "--from"));
    Run cannot;
    cannot.fake.canPlay = false;
    SolarisService mute(cannot.fake.host());
    std::string err;
    mute.dispatchText("project new " + freshSong("noout"), err);
    assert(!mute.dispatchText("transport play", err) && err == "this build cannot play audio (no output stream)");
    pass("while playing: a clip added is heard (an engine swap); a gain change is heard at the next block (R-PLAY-1)");
}

static void test_meters_measure_what_plays()
{
    // R-METER-1, R-METER-2: a 1 kHz sine at 0.5 in both channels reads −6.02 LUFS (BS.1770-4: a full-scale sine in
    // both is 0), −6.02 dBTP, correlation 1, its 1/6-octave band at −6 dB; RMS 0.354; held 0.5
    Run r;
    r.ok("project new " + freshSong("meters") + " --bpm 120");
    for (int k = 0; k < 16; ++k) r.ok("clip add --src tone.wav --at " + std::to_string(k) + " --length 1"); // 8 s: a beat is the tone's half second
    r.fake.capture->sink.reserve(2 * 2000000);
    r.ok("transport play");
    waitFrames(r, 96000);
    const LoudnessModel &l = r.svc.model().loudness;
    const TransportModel &t = r.svc.model().transport;
    if (std::fabs(l.integrated + 6.02) > 0.2 || std::fabs(l.truePeak + 6.02) > 0.2)
        std::printf("    momentary %.3f short %.3f integrated %.3f true peak %.3f corr %.3f band34 %.3f\n", l.momentary, l.shortTerm,
                    l.integrated, l.truePeak, l.correlation, l.spectrum.size() > 34 ? l.spectrum[34] : 0.0);
    assert(std::fabs(l.momentary + 6.02) < 0.2 && std::fabs(l.shortTerm + 6.02) < 0.3 && std::fabs(l.integrated + 6.02) < 0.2);
    assert(std::fabs(l.truePeak + 6.02) < 0.2 && l.correlation > 0.99);
    assert(l.spectrum.size() == 61 && std::fabs(l.spectrum[34] + 6.02) < 1.5 && l.spectrum[22] < l.spectrum[34] - 20.0);
    const StripModel *ch = r.strip("ch_2");
    assert(std::fabs(ch->rms[0] - 0.3536f) < 0.02f && std::fabs(ch->rms[1] - 0.3536f) < 0.02f && ch->rms[0] < ch->peak[0]);
    assert(std::fabs(ch->hold[0] - 0.5f) < 0.01f && !ch->clip);
    assert(std::fabs(t.masterRms[0] - 0.3536f) < 0.02f && std::fabs(t.masterHold[1] - 0.5f) < 0.01f && !t.masterClip);
    // over full scale: the clip lights latch, on the strip and the master
    r.ok("set ch_2.gain=12");
    waitFrames(r, r.fake.capture->frames.load() + 24000);
    assert(r.strip("ch_2")->clip && t.masterClip && r.strip("ch_2")->hold[0] > 1.0f);
    // a meters clear while playing: the audio thread takes it without allocating
    gThreadAllocs = 0;
    gCountThreads = true;
    r.ok("set ch_2.gain=0");
    r.ok("meters clear");
    waitFrames(r, r.fake.capture->frames.load() + 24000);
    gCountThreads = false;
    const long allocs = gThreadAllocs.load();
    if (allocs) std::printf("    %ld allocations on the audio thread\n", allocs);
    assert(allocs == 0);
    assert(!r.strip("ch_2")->clip && !t.masterClip && r.strip("ch_2")->hold[0] < 0.51f);
    assert(std::fabs(l.integrated + 6.02) < 0.3 && std::fabs(l.truePeak + 6.02) < 0.2); // started again: −6 since the clear, not the +6 before it
    assert(std::any_of(r.events.begin(), r.events.end(), [](const std::string &e) { return contains(e, "meters.cleared"); }));
    // a latch outlives a stop; the bars fall to nothing
    r.ok("set ch_2.gain=12");
    waitFrames(r, r.fake.capture->frames.load() + 24000);
    r.ok("transport stop");
    assert(r.strip("ch_2")->clip && t.masterClip && r.strip("ch_2")->peak[0] == 0.0f && r.strip("ch_2")->rms[0] == 0.0f);
    assert(std::fabs(l.integrated) < 20.0);                     // the loudness is kept after a stop, to be read
    r.ok("set ch_2.gain=0");                                     // an edit refreshes the model: still lit
    assert(r.strip("ch_2")->clip && r.svc.model().transport.masterClip);
    r.ok("meters clear");
    assert(!r.strip("ch_2")->clip && !r.svc.model().transport.masterClip && r.svc.model().loudness.integrated == -144.0);
    pass("the meters (R-METER-1, R-METER-2): a −6 dBFS sine reads −6.02 LUFS momentary, short-term and integrated, −6.02 dBTP, "
         "correlation 1, its 1/6-octave band; RMS 0.354 and a held 0.5 on the strip and the master; over full scale the clip "
         "lights latch through a stop and an edit; `meters clear` puts them out and starts the loudness again, with no allocation");
}

static void test_held_peak_holds_then_falls()
{
    // R-METER-2: one second of tone, then silence — held 1.5 s after the last peak, then falling 20 dB a second
    Run r;
    r.ok("project new " + freshSong("hold") + " --bpm 120");
    r.ok("clip add --src tone.wav --at 0 --length 1");
    r.ok("clip add --src tone.wav --at 1 --length 1");
    r.fake.capture->sink.reserve(2 * 2000000);
    r.ok("transport play");
    waitFrames(r, 48000 * 17 / 10);                              // 1.7 s: the tone ended 0.7 s ago
    const StripModel *ch = r.strip("ch_2");
    const float held = ch->hold[0], rms = ch->rms[0];
    assert(ch->peak[0] == 0.0f && held > 0.49f && rms < 0.25f); // the bar has fallen; the held line has not
    waitFrames(r, 48000 * 37 / 10);                              // 3.7 s: a second past the hold, falling
    ch = r.strip("ch_2");
    if (ch->hold[0] > 0.2f) std::printf("    held %.4f at 3.7 s\n", ch->hold[0]);
    assert(ch->hold[0] < 0.2f && ch->hold[0] > 0.0f);
    r.ok("transport stop");
    pass("a held peak stays 1.5 s after the last peak, then falls 20 dB a second; the RMS falls with the sound (R-METER-2)");
}

static void test_render_measure()
{
    // R-METER-1: a render's mixdown measured, one line
    Run r;
    r.ok("project new " + freshSong("measure") + " --bpm 120");
    for (int k = 0; k < 8; ++k) r.ok("clip add --src tone.wav --at " + std::to_string(k) + " --length 1");
    const std::string out = r.ok("render --out " + scratch() + "/measure.wav --to 8 --measure");
    const size_t at = out.find("measure ");
    assert(at != std::string::npos);
    double integrated = 0, mMax = 0, sMax = 0, tp = 0, corr = 0;
    const int got = std::sscanf(out.c_str() + at, "measure integrated=%lf momentaryMax=%lf shortTermMax=%lf truePeak=%lf correlationMin=%lf",
                                &integrated, &mMax, &sMax, &tp, &corr);
    if (got != 5) std::printf("    %s", out.c_str() + at);
    assert(got == 5);
    assert(std::fabs(integrated + 6.0) < 0.15 && std::fabs(mMax + 6.0) < 0.15 && std::fabs(sMax + 6.0) < 0.15);
    assert(std::fabs(tp + 6.0) < 0.15 && corr > 0.99);
    assert(!contains(r.ok("render --out " + scratch() + "/plain.wav --to 8"), "measure"));
    pass("render --measure prints the mixdown's integrated loudness, its most momentary and short-term, its true peak and its lowest correlation (R-METER-1)");
}

static void test_the_driver_and_its_fallback()
{
    // R-DEV-9: `settings set driver=pipewire|pulse`, saved; PipeWire when asked and it opens — its xruns published
    // (R-DEV-5); PulseAudio when it will not open or the build has none, said once; the transport says which
    const std::string dir = scratch();
    const std::string settings = dir + "/driver.settings";
    std::error_code ec;
    fs::remove(settings, ec);
    Run r(settings, "");
    assert(r.svc.model().settings.driver == "pulse");
    r.ok("settings set driver=pipewire");
    assert(r.svc.model().settings.driver == "pipewire");
    assert(contains(r.no("settings set driver=alsa"), "pulse or pipewire"));
    {
        Run again(settings, "");
        assert(again.svc.model().settings.driver == "pipewire"); // a second process reads it back
    }
    r.ok("project new " + freshSong("driver") + " --bpm 120");
    auto said = [&](const std::string &part) {
        return std::any_of(r.events.begin(), r.events.end(), [&](const std::string &e) { return contains(e, part); });
    };
    // this host has no PipeWire driver: PulseAudio, and why
    r.events.clear();
    r.ok("transport play");
    assert(r.svc.model().transport.driver == "pulse" && said("this build has no PipeWire driver — playing through PulseAudio"));
    assert(r.svc.model().transport.xruns == -1);                       // Pulse's simple API cannot count them
    r.ok("transport stop");
    assert(r.svc.model().transport.driver.empty());
    // a PipeWire that links nowhere: PulseAudio, its reason said (the driver set again: tried again)
    r.fake.pipewire = "fail";
    r.ok("settings set driver=pipewire");
    r.events.clear();
    r.ok("transport play");
    assert(r.svc.model().transport.driver == "pulse" && said("linked to no output within 2 s — playing through PulseAudio"));
    r.ok("transport stop");
    // said once: not tried again (a preview would wait each time) — until the driver is set again
    r.fake.pipewire = "ok";
    r.events.clear();
    r.ok("transport play");
    assert(r.svc.model().transport.driver == "pulse" && !said("playing through PulseAudio"));
    r.ok("transport stop");
    r.ok("settings set driver=pipewire");
    // one that opens: played through it, its underruns in the model
    r.events.clear();
    r.ok("transport play");
    r.ok("wait 0.02");
    assert(r.svc.model().transport.driver == "pipewire" && r.svc.model().transport.xruns == 3 && !said("playing through PulseAudio"));
    r.ok("transport stop");
    // asked for Pulse, PipeWire is not tried
    r.ok("settings set driver=pulse");
    r.ok("transport play");
    assert(r.svc.model().transport.driver == "pulse");
    r.ok("transport stop");
    pass("the driver (R-DEV-9): `settings set driver=pipewire|pulse`, saved; PipeWire when asked and it opens, its xruns in the "
         "model (R-DEV-5); PulseAudio when it links nowhere or the build has none, said once with why, tried again when set again; "
         "the transport says which");
}

static void test_a_keyboard_plays_the_armed_strip()
{
    // R-MIDI-2: `strip arm` — what the keyboard plays sounds through that strip at once: through an input monitor
    // (the song's mix without its clips) while stopped, through the song while it plays; the pedal holds; not an edit
    Run r;
    r.ok("project new " + freshSong("keys") + " --bpm 120");
    r.ok("strip add --kind instrument --instrument synth");      // ch_2: nothing to play but the keyboard
    r.ok("clip add --src tone.wav --at 0 --length 1");            // an audio line: armed for its INPUT (R-REC-1) — it has none
    const auto &cap = *r.fake.capture;
    r.fake.capture->sink.reserve(2 * 4000000);                    // the fake device keeps what it is given: no allocation
    const int depth = r.svc.model().undoDepth;
    assert(contains(r.no("strip arm ch_3"), "has no input"));
    assert(contains(r.no("strip arm ch_9"), "no strip"));
    r.events.clear();
    r.ok("strip arm ch_2");
    assert(r.svc.model().armed == "ch_2" && contains(r.events.back(), "strip.armed strip=ch_2"));
    assert(r.svc.model().undoDepth == depth && !contains(r.text(), "armed"));  // the session's, never the song's
    assert(contains(r.ok("midi devices"), "Keys  (listening)") && r.svc.model().midiDevices.size() == 2);
    // stopped: the monitor runs, silent until a key is struck
    waitFrames(r, cap.frames.load() + 9600);
    const size_t quiet = cap.frames.load();
    assert(peakOf(cap.sink, 0, 2 * quiet) == 0.0f);
    gThreadAllocs = 0;
    gCountThreads = true;                                         // from here the audio thread must not allocate
    r.fake.key(0x90, 69, 110);                                    // A4 struck
    waitFrames(r, quiet + 12000);
    const size_t struck = quiet;
    assert(peakOf(cap.sink, 2 * struck, 2 * (struck + 12000)) > 0.05f);
    r.fake.key(0x80, 69, 0);                                      // let go: it dies away
    const size_t off = cap.frames.load();
    waitFrames(r, off + 72000);
    assert(peakOf(cap.sink, 2 * (off + 48000), 2 * (off + 72000)) < 1e-3f);
    // the pedal: let go under it, the note rings on; up, it dies away
    r.fake.key(0xB0, 64, 127);
    r.fake.key(0x90, 72, 110);
    r.fake.key(0x80, 72, 0);
    const size_t held = cap.frames.load();
    waitFrames(r, held + 48000);
    assert(peakOf(cap.sink, 2 * (held + 36000), 2 * (held + 48000)) > 0.01f);
    r.fake.key(0xB0, 64, 0);
    const size_t up = cap.frames.load();
    waitFrames(r, up + 72000);
    assert(peakOf(cap.sink, 2 * (up + 48000), 2 * (up + 72000)) < 1e-3f);
    gCountThreads = false;
    const long allocs = gThreadAllocs.load();
    if (allocs) std::printf("    %ld allocations on the audio thread\n", allocs);
    assert(allocs == 0);
    // playing: the song's player takes the keyboard (the monitor stops), the note sounds through the song (from beat 4:
    // past the tone clip, so what sounds is the key)
    r.ok("transport play --from 4");
    waitFrames(r, cap.frames.load() + 4800);
    const size_t before = cap.frames.load();
    assert(peakOf(cap.sink, 2 * (before - 2400), 2 * before) == 0.0f);   // silent until struck
    r.fake.key(0x90, 60, 110);
    const size_t played = cap.frames.load();
    waitFrames(r, played + 12000);
    assert(peakOf(cap.sink, 2 * played, 2 * (played + 12000)) > 0.05f);
    r.fake.key(0x80, 60, 0);
    r.ok("transport stop");
    // disarmed: the monitor goes; struck now, nothing sounds
    r.ok("strip arm none");
    assert(r.svc.model().armed.empty() && contains(r.events.back(), "strip.armed strip="));
    r.ok("wait 0.01");
    assert(r.svc.model().midiMessages == 8);                      // every message the keyboard sent, counted
    pass("a MIDI keyboard (R-MIDI-2): `strip arm` an instrument strip — struck, it sounds at once through the input monitor "
         "while stopped and through the song while playing, no allocation on the audio thread; let go, it dies away; the "
         "pedal holds it; not an edit; audio lines and unknown strips refused; `midi devices` says what it listens to");
}

static void test_record_the_keyboard_into_a_clip()
{
    // R-MIDI-2: `transport record` — a count-in (the click, the song waiting; a key struck during it dropped), the take
    // landed at the stop as a clip on the armed strip's track from the bar it began in, each note where it was heard on
    // the input-quantize grid; one undo takes it; --overdub adds into the clip under the start
    Run r;
    r.ok("project new " + freshSong("record") + " --bpm 120");
    r.ok("strip add --kind instrument --instrument synth");          // ch_2
    r.fake.capture->sink.reserve(2 * 4000000);
    const auto &cap = *r.fake.capture;
    assert(contains(r.no("transport record"), "arm a strip first"));
    r.ok("strip arm ch_2");
    r.ok("settings set recordQuantize=1/4");
    const int depth = r.svc.model().undoDepth;
    r.ok("transport record --from 4 --count-in 1");                   // a bar of click, then the song from beat 4
    const long long f0 = cap.frames.load();
    assert(r.svc.model().transport.recording);
    assert(contains(r.no("transport record"), "already recording") && contains(r.no("transport seek 2"), "recording"));
    assert(contains(r.no("transport play"), "recording"));
    auto at = [&](double beat) { return f0 + (long long)(beat * 24000); };  // the count-in is beats 0 … 4: heard as the bar before
    waitFrames(r, at(1.0));
    assert(r.svc.model().transport.countingIn);
    r.fake.key(0x90, 72, 90);                                         // struck during the count-in: not in the take
    r.fake.key(0x80, 72, 0);
    waitFrames(r, at(4.2));
    assert(!r.svc.model().transport.countingIn);
    assert(peakOf(cap.sink, 2 * (size_t)f0, 2 * (size_t)at(0.5)) > 0.05f); // the click counted it in, the metronome off
    waitFrames(r, at(5.0));
    r.fake.key(0x90, 60, 100);
    waitFrames(r, at(5.5));
    r.fake.key(0x80, 60, 0);
    waitFrames(r, at(6.0));
    r.fake.key(0x90, 64, 80);
    waitFrames(r, at(6.5));
    r.fake.key(0x80, 64, 0);
    waitFrames(r, at(7.0));
    assert(r.svc.model().transport.recorded == 2);
    r.events.clear();
    const std::string landed = r.ok("transport stop");
    const auto &m = r.svc.model();
    assert(!m.transport.recording && m.undoDepth == depth + 1 && m.undoLabel == "Record");
    const ClipModel *take = nullptr;
    for (const auto &c : m.clips)
        if (c.track == "ch_2") take = &c;
    assert(take && firstLine(landed) == take->id && take->at == 4.0 && take->length == 4.0);
    assert(std::any_of(r.events.begin(), r.events.end(), [&](const std::string &e) { return contains(e, "record.finished clip=" + take->id + " notes=2"); }));
    const PatternModel *pt = nullptr;
    for (const auto &x : m.patterns)
        if (x.id == take->pattern) pt = &x;
    assert(pt && pt->notes.size() == 2);
    assert(pt->notes[0].pitch == 60 && pt->notes[0].at == 1.0 && pt->notes[0].vel == 100);   // beat 5 → 1 into the bar
    assert(pt->notes[1].pitch == 64 && pt->notes[1].at == 2.0 && pt->notes[1].vel == 80);
    // half a beat held — stamped where the audio thread drained each key, so a busy machine (ctest -j) moves it a little
    assert(pt->notes[0].length > 0.25 && pt->notes[0].length < 1.0);
    // one undo takes the take; redo brings it back
    r.ok("undo");
    assert(r.svc.model().clips.empty());
    r.ok("redo");
    assert(r.svc.model().clips.size() == 1);
    // --overdub: into the clip under the start, no new clip
    r.ok("transport record --from 4 --overdub");
    const long long f1 = cap.frames.load();
    waitFrames(r, f1 + 3 * 24000);
    r.fake.key(0x90, 67, 70);                                         // beat 7 → 3 into the bar
    waitFrames(r, f1 + (long long)(3.5 * 24000));
    r.fake.key(0x80, 67, 0);
    waitFrames(r, f1 + 4 * 24000);
    r.ok("transport stop");
    assert(r.svc.model().clips.size() == 1);
    for (const auto &x : r.svc.model().patterns)
        if (x.id == take->pattern)
            assert(x.notes.size() == 3 && x.notes[2].pitch == 67 && x.notes[2].at == 3.0);
    // nothing played: nothing lands
    r.events.clear();
    r.ok("transport record --from 8");
    waitFrames(r, cap.frames.load() + 24000);
    assert(contains(r.ok("transport stop"), "nothing was played") && r.svc.model().clips.size() == 1);
    assert(contains(r.no("settings set recordQuantize=1/5"), "1/16"));
    pass("record (R-MIDI-2): a count-in bar clicks with the song waiting (a key struck during it dropped); the take lands at "
         "the stop as a clip from the bar it began in, notes where they were heard on the 1/4 grid, velocities kept; one "
         "undo (\"Record\"); --overdub adds into the clip under the start; nothing played lands nothing; refusals");
}

namespace
{
    /** The frequency of x over [a, b): positive-going zero crossings, interpolated. */
    double zeroHz(const std::vector<float> &x, size_t a, size_t b)
    {
        double first = -1, last = -1;
        int n = 0;
        for (size_t i = a + 1; i < b && i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const double t = (double)(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
                if (first < 0) first = t;
                else ++n;
                last = t;
            }
        return n > 0 ? n * 48000.0 / (last - first) : 0.0;
    }
    void sineSynth(Run &r, const std::string &dv)
    {
        r.ok("set " + dv + ".osc1.wave=sine " + dv + ".osc2.level=0 " + dv + ".filter.cutoff=20000 " + dv + ".filter.env=0 " + dv +
             ".filter.keytrack=0 " + dv + ".amp.attack=0 " + dv + ".amp.sustain=1");
    }
}

static void test_expression_lanes_play_the_wheels()
{
    // R-MIDI-3: a pattern's lanes — the pitch wheel heard as pitch (a ramp, then held), at rest after its clip, a seek
    // landing bent; `mod` is `cc1`; kept in the .slp; refusals name the way
    Run r;
    const std::string song = freshSong("expr");
    r.ok("project new " + song + " --bpm 120");
    r.ok("clip add --instrument synth --at 0 --length 4");          // ch_2 dv_1, ac_1 pt_1
    sineSynth(r, "dv_1");
    r.ok("note add pt_1 --pitch 69 --at 0 --length 4");
    r.ok("expr add pt_1 --lane bend --at 0 --value 0");
    r.ok("expr add pt_1 --lane bend --at 2 --value 1");             // two semitones up over two beats (the default range)
    r.ok("clip add --strip ch_2 --at 4 --length 4");                 // ac_2, pt_2: the same note, no lane
    r.ok("note add pt_2 --pitch 69 --at 0 --length 4");
    const auto *pt = [&]() -> const PatternModel * { for (const auto &x : r.svc.model().patterns) if (x.id == "pt_1") return &x; return nullptr; }();
    assert(pt && pt->expr.size() == 2 && pt->expr[1].lane == "bend" && pt->expr[1].at == 2.0 && pt->expr[1].value == 1.0);
    const std::string out = scratch() + "/expr.wav";
    r.ok("render --out " + out + " --to 8 --bits 32f");
    const auto &L = r.fake.written[out][0];
    const double start = zeroHz(L, 480, 2400), mid = zeroHz(L, 24000 - 1200, 24000 + 1200), held = zeroHz(L, 3 * 24000, 4 * 24000 - 480);
    const double after = zeroHz(L, 5 * 24000, 7 * 24000);
    if (std::fabs(held - 493.88) > 1.0) std::printf("    start %.2f mid %.2f held %.2f after %.2f Hz\n", start, mid, held, after);
    assert(std::fabs(start - 440.0) < 2.0);                          // at rest
    assert(std::fabs(mid - 440.0 * std::pow(2.0, 1.0 / 12.0)) < 2.0); // half way up the ramp: a semitone
    assert(std::fabs(held - 440.0 * std::pow(2.0, 2.0 / 12.0)) < 1.0); // held after its last point
    assert(std::fabs(after - 440.0) < 1.0);                          // the next clip: at rest again
    // a seek into the bent part: it plays bent from the first sample (a seek silences a note begun before it, so an
    // E5 struck at beat 3 is what sounds — two semitones up, F#5)
    r.ok("note add pt_1 --pitch 76 --at 3 --length 1");
    const std::string from3 = scratch() + "/expr3.wav";
    r.ok("render --out " + from3 + " --from 3 --to 4 --bits 32f");
    const double seeked = zeroHz(r.fake.written[from3][0], 240, 12000);
    if (std::fabs(seeked - 739.99) > 1.5) std::printf("    after the seek %.2f Hz\n", seeked);
    assert(std::fabs(seeked - 739.99) < 1.5);
    // `mod` is cc1; values in range; a lane we know
    r.ok("expr add pt_1 --lane mod --at 1 --value 0.5");
    bool cc1 = false;
    for (const auto &x : r.svc.model().patterns[0].expr) cc1 |= x.lane == "cc1" && x.value == 0.5;
    assert(cc1);
    assert(contains(r.no("expr add pt_1 --lane bend --at 1 --value 1.5"), "--value for bend is -1"));
    assert(contains(r.no("expr add pt_1 --lane mod --at 1 --value -0.1"), "--value for cc1 is 0"));
    assert(contains(r.no("expr add pt_1 --lane pan --at 1 --value 0"), "bend, mod or cc0 … cc127"));
    assert(contains(r.no("expr delete pt_1 --lane bend --at 3"), "no bend point at 3"));
    assert(contains(r.no("expr add pt_9 --lane bend --at 1 --value 0"), "no pattern"));
    r.ok("expr add pt_1 --lane cc74 --at 0 --value 0.25");           // any controller: kept, exported, not played
    // the .slp keeps them, sorted by lane then beat
    r.ok("project save");
    std::ifstream f(song);
    std::stringstream ss;
    ss << f.rdbuf();
    assert(contains(ss.str(), "#expr lane=bend at=2.0 value=1") && contains(ss.str(), "#expr lane=cc1 at=1.0 value=0.5") &&
           contains(ss.str(), "#expr lane=cc74 at=0.0 value=0.25"));
    Run again;
    again.ok("project open " + song);
    size_t points = 0;
    for (const auto &x : again.svc.model().patterns)
        if (x.id == "pt_1") points = x.expr.size();
    assert(points == 4);
    r.ok("expr delete pt_1 --lane cc74 --at 0");
    r.ok("expr clear pt_1 --lane cc1");
    r.ok("undo");                                                     // an edit like any: undone
    r.ok("expr clear pt_1");
    assert(r.svc.model().patterns[0].expr.empty());
    pass("expression lanes (R-MIDI-3): a bend lane heard as pitch — at rest, half way up the ramp a semitone, held two after its "
         "last point, the next clip at rest, a seek landing bent; `mod` is cc1; any controller kept; in the .slp; refusals; undoable");
}

static void test_expression_lanes_go_through_midi_files()
{
    // R-MIDI-3: exported as pitch-bend and controller messages (a ramp stepped every 1/32 beat), read back as lanes
    Run r;
    r.ok("project new " + freshSong("exprmidi") + " --bpm 120");
    r.ok("clip add --instrument synth --at 0 --length 4");
    r.ok("note add pt_1 --pitch 60 --at 0 --length 4");
    r.ok("expr add pt_1 --lane bend --at 0 --value -0.5");
    r.ok("expr add pt_1 --lane bend --at 1 --value 0.5");
    r.ok("expr add pt_1 --lane mod --at 2 --value 1");
    const std::string mid = scratch() + "/expr.mid";
    r.ok("pattern export pt_1 " + mid);
    const std::string got = firstLine(r.ok("pattern import " + mid));
    const PatternModel *pt = nullptr;
    for (const auto &x : r.svc.model().patterns)
        if (x.id == got) pt = &x;
    assert(pt);
    std::vector<ExprPoint> lanes;
    for (const auto &x : pt->expr) lanes.push_back(ExprPoint{x.lane, x.at, x.value, {}});
    const double atHalf = exprAt(lanes, "bend", 0.5), atOne = exprAt(lanes, "bend", 1.5), mod = exprAt(lanes, "cc1", 3.0);
    if (std::fabs(atHalf) > 0.01 || std::fabs(atOne - 0.5) > 0.01) std::printf("    bend %.4f / %.4f, mod %.4f\n", atHalf, atOne, mod);
    assert(std::fabs(atHalf) < 0.01 && std::fabs(atOne - 0.5) < 0.01 && std::fabs(mod - 1.0) < 1e-9);
    assert(std::fabs(exprAt(lanes, "bend", 0.0) + 0.5) < 0.001);
    pass("expression lanes through a MIDI file (R-MIDI-3): bend and the mod wheel exported as their messages (a ramp stepped), imported as lanes — the same curve to 1 %");
}

static void test_the_keyboards_wheels_play_and_record()
{
    // R-MIDI-3: the keyboard's own wheels — into the armed strip at once, and into a take's lanes
    Run r;
    r.ok("project new " + freshSong("wheels") + " --bpm 120");
    r.ok("strip add --kind instrument --instrument synth");            // ch_2 dv_1
    sineSynth(r, "dv_1");
    r.fake.capture->sink.reserve(2 * 4000000);
    const auto &cap = *r.fake.capture;
    r.ok("strip arm ch_2");
    r.fake.key(0x90, 69, 100);
    r.fake.key(0xE0, 0x00, 0x7F);                                    // the wheel nearly full up: 16256 → +0.984
    const size_t bent = cap.frames.load();
    waitFrames(r, bent + 24000);
    std::vector<float> left;
    for (size_t i = 2 * (bent + 4800); i + 1 < 2 * (bent + 24000) && i < cap.sink.size(); i += 2) left.push_back(cap.sink[i]);
    const double heard = zeroHz(left, 0, left.size());
    if (std::fabs(heard - 440.0 * std::pow(2.0, 2.0 * 0.984375 / 12.0)) > 2.0) std::printf("    heard %.2f Hz\n", heard);
    assert(std::fabs(heard - 440.0 * std::pow(2.0, 2.0 * 0.984375 / 12.0)) < 2.0);
    r.fake.key(0x80, 69, 0);
    r.fake.key(0xE0, 0x00, 0x40);                                    // back to the centre
    // recorded: a take whose bend lane follows the wheel
    r.ok("transport record --from 4");
    const long long f0 = cap.frames.load();
    waitFrames(r, f0 + 24000);
    r.fake.key(0x90, 64, 100);
    r.fake.key(0xE0, 0x00, 0x60);                                    // +0.5
    waitFrames(r, f0 + 48000);
    r.fake.key(0xE0, 0x00, 0x40);                                    // the centre
    r.fake.key(0xB0, 1, 127);                                        // the mod wheel up
    waitFrames(r, f0 + 72000);
    r.fake.key(0x80, 64, 0);
    waitFrames(r, f0 + 84000);
    r.ok("transport stop");
    const PatternModel *take = nullptr;
    for (const auto &x : r.svc.model().patterns) take = &x;
    assert(take);
    bool up = false, centre = false, mod = false;
    for (const auto &x : take->expr)
    {
        up |= x.lane == "bend" && x.value == 0.5 && x.at > 0.9 && x.at < 1.6;
        centre |= x.lane == "bend" && x.value == 0.0 && x.at > 1.9;
        mod |= x.lane == "cc1" && x.value == 1.0;
    }
    if (!(up && centre && mod)) for (const auto &x : take->expr) std::printf("    %s @ %.3f = %.3f\n", x.lane.c_str(), x.at, x.value);
    assert(up && centre && mod && take->notes.size() == 1);
    pass("the keyboard's wheels (R-MIDI-3): the pitch wheel bends the armed strip at once; in a take, the bend and the mod wheel land as its lanes where they moved");
}

namespace
{
    const PatternModel *patternIn(const Run &r, const std::string &id)
    {
        for (const auto &x : r.svc.model().patterns)
            if (x.id == id) return &x;
        return nullptr;
    }
    // a pattern's notes as `pattern print` writes them, for comparing whole states
    std::string notesText(Run &r, const std::string &pt)
    {
        const std::string all = r.ok("pattern print " + pt);
        return all.substr(all.find('\n') + 1);
    }
}

static void test_markers_name_places_and_sections()
{
    // R-ARR-1: a marker a named place, with a length a section; a point's section runs to the next marker (else the
    // song's end); `transport seek|loop <mk>` reach them; edits sorted by time, undoable; refusals name the way
    Run r;
    r.ok("project new " + freshSong("markers") + " --bpm 120");
    r.ok("clip add --instrument synth --at 0 --length 40");         // the song ends at 40
    assert(r.ok("marker add Intro --at 0") == "mk_1\n");
    assert(r.ok("marker add Outro --at 32") == "mk_2\n");
    assert(r.ok("marker add Chorus --at 16 --length 8 --note \"big one\"") == "mk_3\n");
    {
        const auto &mk = r.svc.model().markers;
        assert(mk.size() == 3 && mk[0].id == "mk_1" && mk[1].id == "mk_3" && mk[2].id == "mk_2"); // by time
        assert(mk[0].end == 16.0 && mk[1].end == 24.0 && mk[2].end == 40.0 && mk[1].note == "big one" && mk[1].length == 8.0);
    }
    assert(contains(r.text(), "#amarker id=mk_3 name=Chorus at=16.0 length=8.0 note=\"big one\""));
    assert(contains(r.ok("ls"), "marker mk_3 \"Chorus\" @16 .. 24 (a section)") && contains(r.ok("ls"), "marker mk_2 \"Outro\" @32 .. 40\n"));
    // seek and loop by marker
    r.ok("transport seek mk_3");
    assert(r.svc.model().transport.position == 16.0);
    r.ok("transport loop mk_1");
    assert(r.svc.model().transport.loopFrom == 0.0 && r.svc.model().transport.loopTo == 16.0);
    r.ok("transport loop mk_3");
    assert(r.svc.model().transport.loopFrom == 16.0 && r.svc.model().transport.loopTo == 24.0);
    r.ok("transport loop off");
    // moved: still by time, and a point's section follows its neighbour
    r.ok("marker move mk_2 --at 20");
    r.ok("marker move mk_3 --length 0");                           // a point again: to the next marker
    {
        const auto &mk = r.svc.model().markers;
        assert(mk[1].id == "mk_3" && mk[1].end == 20.0 && mk[2].id == "mk_2" && mk[2].at == 20.0 && mk[2].end == 40.0);
    }
    r.ok("marker rename mk_2 Bridge --note \"the turn\"");
    assert(r.svc.model().markers[2].name == "Bridge" && r.svc.model().markers[2].note == "the turn");
    r.ok("marker delete mk_1");
    assert(r.svc.model().markers.size() == 2 && r.svc.model().markers[0].id == "mk_3");
    r.ok("undo");
    assert(r.svc.model().markers.size() == 3);
    // refusals
    assert(contains(r.no("marker add Verse"), "--at"));
    assert(contains(r.no("marker add Verse --at -2"), "--at is a number of beats"));
    const std::string none = r.no("marker move mk_9 --at 1");
    assert(contains(none, "no marker `mk_9`") && contains(none, "mk_3 (Chorus)"));
    assert(contains(r.no("marker move mk_3"), "needs --at and/or --length"));
    assert(contains(r.no("transport seek mk_9"), "or a marker"));
    assert(r.ok("marker add End --at 40") == "mk_4\n");             // at the song's end: no section to loop
    assert(contains(r.no("transport loop mk_4"), "section is empty"));
    pass("markers (R-ARR-1): added, by time, a section by length or to the next marker or the song's end; seek and loop by marker; moved, renamed, deleted, undone; .slp and ls; refusals name the way");
}

static void test_time_insert_and_delete_move_the_whole_song()
{
    // R-ARR-2: ONE edit across the song — clips, automation points, markers and the loop; a clip across an edge is said
    // (insert) or, unless what it plays repeats, refused (delete), before anything moves
    Run r;
    r.ok("project new " + freshSong("time") + " --bpm 120");
    r.ok("clip add --instrument synth --at 0 --length 4");          // ac_1 0 … 4 (ch_2, pt_1)
    r.ok("clip add --strip ch_2 --pattern pt_1 --at 8 --length 4");  // ac_2 8 … 12
    r.ok("clip add --strip ch_2 --pattern pt_1 --at 2 --length 8");  // ac_3 2 … 10: across 4 and 8
    r.ok("auto create ch_2.gain");                                   // au_1
    for (const char *pt : {"0 --value -6", "6 --value 0", "12 --value -3"}) r.ok(std::string("auto point add au_1 --at ") + pt);
    r.ok("marker add A --at 0");
    r.ok("marker add B --at 6 --length 4");
    r.ok("marker add C --at 12");
    r.ok("transport loop 5 9");
    auto clip = [&](const std::string &id) { return r.clip(id); };
    auto points = [&] {
        std::vector<double> v;
        for (const auto &x : r.svc.model().automations[0].points) v.push_back(x.at);
        return v;
    };
    auto marker = [&](const std::string &id) -> const MarkerModel & {
        for (const auto &m : r.svc.model().markers)
            if (m.id == id) return m;
        assert(false);
        return r.svc.model().markers[0];
    };
    const std::string before = r.text();
    const std::vector<double> was = points(); // `auto create` seeds its own points too
    auto shifted = [&](double from, double by, double cutTo) { // what a time edit should make of them
        std::vector<double> v;
        for (double x : was)
        {
            if (x >= from && x < cutTo) continue;
            v.push_back(x >= from ? x + by : x);
        }
        return v;
    };
    // insert a bar at 4: what is at or after it moves; a section, the loop that holds it grows; ac_3 crosses it — said
    const std::string opened = r.ok("time insert --at 4 --length 4");
    assert(contains(opened, "opened 4.0 beats at 4.0") && contains(opened, "ac_3 crosses beat 4.0 — it stays"));
    assert(clip("ac_1")->at == 0.0 && clip("ac_2")->at == 12.0 && clip("ac_3")->at == 2.0 && clip("ac_3")->length == 8.0);
    assert(points() == shifted(4.0, 4.0, 4.0) && points()[1] == 10.0 && points()[2] == 16.0);
    assert(marker("mk_1").at == 0.0 && marker("mk_2").at == 10.0 && marker("mk_2").length == 4.0 && marker("mk_3").at == 16.0);
    assert(r.svc.model().transport.loopFrom == 9.0 && r.svc.model().transport.loopTo == 13.0);
    r.ok("undo");                                                    // ONE step back — the loop is the session's and stays
    assert(r.text() == before && r.svc.model().transport.loopFrom == 9.0);
    r.ok("transport loop 5 9");
    // delete 4 … 8: a plain audio clip across an edge is refused, naming it, and nothing moves
    const std::string audio = firstLine(r.ok("clip add --src kick.wav --at 3.5")); // half a second: 3.5 … 4.5, not looping
    const std::string refused = r.no("time delete --from 4 --to 8");
    assert(contains(refused, audio + " crosses an edge of 4.0 … 8.0") && contains(refused, "nothing changed") && clip("ac_2")->at == 8.0);
    r.ok("clip delete " + audio);
    const std::string inside = firstLine(r.ok("clip add --strip ch_2 --pattern pt_1 --at 5 --length 2")); // wholly inside: it goes
    const std::string took = r.ok("time delete --from 4 --to 8");
    assert(contains(took, "took out 4.0 beats at 4.0") && contains(took, "1 clip gone"));
    assert(!clip(inside) && clip("ac_1")->at == 0.0 && clip("ac_2")->at == 4.0);
    assert(clip("ac_3")->at == 2.0 && clip("ac_3")->length == 4.0);  // a note clip across --from: shortened by the overlap
    const std::vector<double> after = points(), want = [&] { // the point inside went; the ones after moved
        std::vector<double> v;
        for (double x : was)
            if (x < 4.0) v.push_back(x);
            else if (x >= 8.0) v.push_back(x - 4.0);
        return v;
    }();
    assert(after == want && std::find(after.begin(), after.end(), 6.0) == after.end() && std::find(after.begin(), after.end(), 8.0) != after.end());
    assert(marker("mk_2").at == 4.0 && marker("mk_2").length == 2.0 && marker("mk_3").at == 8.0); // B's head was in the span
    assert(r.svc.model().transport.loopFrom == 4.0 && r.svc.model().transport.loopTo == 5.0);
    // refusals name the way
    assert(contains(r.no("time insert --at 4"), "--length"));
    assert(contains(r.no("time insert --at 4 --length 0"), "more than 0"));
    assert(contains(r.no("time delete --from 8 --to 4"), "--to must be after --from"));
    pass("time edits (R-ARR-2): insert moves what is after and grows what holds it, a crossing clip said; delete takes out what is inside and moves what follows, a looping clip shortened, a plain one across an edge refused before anything moves; ONE undo; the loop the session's");
}

static void test_lane_groups_gather_fold_and_move_together()
{
    // R-ARR-3: lanes gathered into a named group (side by side at the first one's place), folded as the song's, the
    // group moved whole, a lane joining or leaving by where it lands; clips moved together; ungrouped; refusals
    Run r;
    r.ok("project new " + freshSong("groups") + " --bpm 120");
    const std::string a = firstLine(r.ok("lane add A")), b = firstLine(r.ok("lane add B")), c = firstLine(r.ok("lane add C")), d = firstLine(r.ok("lane add D"));
    auto order = [&] {
        std::vector<std::string> v;
        std::vector<LaneModel> ls = r.svc.model().lanes;
        std::stable_sort(ls.begin(), ls.end(), [](const LaneModel &x, const LaneModel &y) { return x.order < y.order; });
        for (const auto &l : ls)
            if (l.id == a || l.id == b || l.id == c || l.id == d) v.push_back(l.id);
        return v;
    };
    auto groupOf = [&](const std::string &ln) {
        for (const auto &l : r.svc.model().lanes)
            if (l.id == ln) return l.group;
        return std::string("?");
    };
    assert(r.ok("lane group " + a + " " + c + " --name Drums") == "lg_1\n");
    assert((order() == std::vector<std::string>{a, c, b, d}) && groupOf(a) == "lg_1" && groupOf(c) == "lg_1" && groupOf(b).empty());
    assert(r.svc.model().groups.size() == 1 && r.svc.model().groups[0].name == "Drums" && (r.svc.model().groups[0].lanes == std::vector<std::string>{a, c}));
    assert(contains(r.text(), "#alanegroup id=lg_1 name=Drums\n") && contains(r.text(), "group=lg_1"));
    assert(contains(r.ok("ls"), "group lg_1 \"Drums\"\nlane " + a));
    // folded: the song's
    r.ok("set lg_1.folded=on");
    assert(r.svc.model().groups[0].folded && r.ok("get lg_1.folded") == "on\n" && contains(r.text(), "folded=on"));
    // the group moved whole; a lane landing inside its run joins it, one leaving its run leaves it
    r.ok("lane move lg_1 --to 2");
    assert((order() == std::vector<std::string>{b, d, a, c}));
    r.ok("lane move " + b + " --to 2");                             // between a and c
    assert((order() == std::vector<std::string>{d, a, b, c}) && groupOf(b) == "lg_1");
    r.ok("lane move " + a + " --to 0");                             // out of the run
    assert((order() == std::vector<std::string>{a, d, b, c}) && groupOf(a).empty() && (r.svc.model().groups[0].lanes == std::vector<std::string>{b, c}));
    // its clips moved together: ONE edit
    r.ok("clip add --instrument synth --at 0 --length 4 --lane " + b);
    const std::string c1 = r.svc.model().clips.back().id;
    r.ok("clip add --strip " + r.svc.model().clips.back().track + " --at 8 --length 4 --lane " + c);
    const std::string c2 = r.svc.model().clips.back().id;
    const int depth = r.svc.model().undoDepth;
    assert(r.ok("clips move \"" + c1 + " " + c2 + "\" --by 2") == "2 clips moved\n");
    assert(r.clip(c1)->at == 2.0 && r.clip(c2)->at == 10.0 && r.svc.model().undoDepth == depth + 1);
    assert(contains(r.no("clips move \"" + c1 + "\" --by -5"), "would start before beat 0"));
    assert(contains(r.no("clips move \"" + c1 + " ac_99\" --by 1"), "no clip `ac_99`") && r.clip(c1)->at == 2.0);
    // ungrouped: the lanes stay; undone: back
    r.ok("lane ungroup lg_1");
    assert(r.svc.model().groups.empty() && groupOf(b).empty() && (order() == std::vector<std::string>{a, d, b, c}));
    r.ok("undo");
    assert(r.svc.model().groups.size() == 1 && groupOf(b) == "lg_1");
    // a lane's .group: joins (gathered with it), leaves (placed after its run)
    r.ok("set " + a + ".group=lg_1");
    assert(groupOf(a) == "lg_1" && r.ok("get " + a + ".group") == "lg_1\n" && r.svc.model().groups[0].lanes.size() == 3);
    r.ok("set " + a + ".group=none");
    assert(groupOf(a).empty() && r.svc.model().groups[0].lanes.size() == 2 && (order() == std::vector<std::string>{b, c, a, d})); // joining gathered the group at a; leaving, a is right after its run
    assert(contains(r.no("set " + a + ".group=lg_9"), "no lane group `lg_9`"));
    // a group left with no lane goes
    r.ok("lane group " + d + " --name Solo");
    r.ok("lane delete " + d);
    assert(r.svc.model().groups.size() == 1 && r.svc.model().groups[0].id == "lg_1" && !contains(r.text(), "Solo")); // gone from the song too
    // refusals
    assert(contains(r.no("lane group ln_99"), "no lane `ln_99`"));
    assert(contains(r.no("set lg_1.folded=maybe"), "on or off"));
    assert(contains(r.no("set lg_1.colour=3"), "a lane group"));
    assert(contains(r.no("lane ungroup lg_9"), "no lane group `lg_9`"));
    // a hand-written song with a group scattered: gathered on open, and said
    const std::string hand = scratch() + "/scattered.slp";
    {
        std::ofstream f(hand);
        f << "arstro-project = 1\napp = solaris\nid = prj\nname = Scattered\nbpm = 120\n#amixer id=mx_1 name=Sources order=0\n"
             "#alanegroup id=lg_1 name=G\n#alane id=ln_1 name=A group=lg_1 order=0\n#alane id=ln_2 name=B order=1\n#alane id=ln_3 name=C group=lg_1 order=2\n";
    }
    r.events.clear();
    r.ok("project open " + hand);
    bool said = false;
    for (const auto &e : r.events) said |= contains(e, "lane groups gathered");
    std::vector<LaneModel> ls = r.svc.model().lanes;
    std::stable_sort(ls.begin(), ls.end(), [](const LaneModel &x, const LaneModel &y) { return x.order < y.order; });
    assert(said && ls.size() == 3 && ls[0].id == "ln_1" && ls[1].id == "ln_3" && ls[2].id == "ln_2");
    pass("lane groups (R-ARR-3): gathered at the first lane's place, folded as the song's, moved whole, a lane joining or leaving by where it lands; clips moved together as one edit; ungrouped and undone; an empty group goes; a scattered song gathered on open; refusals");
}

namespace
{
    // the first sample louder than `level` (−1: none) — where a burst begins
    long long onset(const std::vector<float> &x, float level, size_t from = 0)
    {
        for (size_t i = from; i < x.size(); ++i)
            if (std::fabs(x[i]) > level) return (long long)i;
        return -1;
    }
}

static void test_record_audio_lands_where_it_was_played()
{
    // R-REC-1: a cable from the output back into the input — the capture hears the clock device (out + in latency)
    // late, as the two report. A burst the song plays at beat 2, recorded from beat 0, is at beat 2 of the take to the
    // sample — the same sample the offline render puts it on. Punched 1 … 3, the clip starts at 1 and the burst is a beat
    // into it. One undo each; refusals name the way.
    Run r;
    r.fake.outLatency = 0.02; // 960 frames heard late …
    r.fake.inLatency = 0.01;  // … and 480 more before the input has it
    const std::string song = freshSong("recaudio");
    r.ok("project new " + song + " --bpm 120");
    r.ok("clip add --instrument synth --at 0 --length 8");         // ch_2 dv_1 pt_1
    sineSynth(r, "dv_1");
    r.ok("set dv_1.amp.release=5");
    r.ok("note add pt_1 --pitch 69 --at 2 --length 0.25");
    assert(r.ok("port add Mic --dir in") == "prt_2\n");
    const std::string vox = firstLine(r.ok("strip add --kind audio --name Vox"));
    r.ok("set " + vox + ".monitor=off");                            // a cable back would feed itself
    const int depth = r.svc.model().undoDepth;
    r.ok("strip arm " + vox + " --input Mic");                       // the input set: an edit; the arming: not
    assert(r.svc.model().armed == vox && r.svc.model().undoDepth == depth + 1);
    assert(r.ok("get " + vox + ".input") == "prt_2\n" && contains(r.text(), "input=prt_2 monitor=off"));
    // where the offline render puts the burst
    const std::string ref = scratch() + "/recaudio-ref.wav";
    r.ok("render --out " + ref + " --to 4 --bits 32f");
    const long long want = onset(r.fake.written[ref][0], 0.01f);
    assert(want >= 2 * 24000 && want < 2 * 24000 + 48);
    // recorded from 0: the take's burst on that very sample
    const long long f0 = r.fake.capture->frames.load();
    r.ok("transport record --from 0 --count-in 0");
    assert(r.svc.model().transport.recording);
    waitFrames(r, f0 + 5 * 24000);
    const std::string landed = r.ok("transport stop");
    const std::string folder = song.substr(0, song.find_last_of('/'));
    const std::string take = folder + "/Recordings/Vox 1.wav";
    assert(r.fake.written.count(take) && r.fake.bits[take] == 32);
    const auto &tk = r.fake.written[take];
    const long long got = onset(tk[0], 0.01f);
    if (got != want) std::printf("    burst: render %lld, take %lld\n", want, got);
    assert(got == want);
    const std::string clip = firstLine(landed);
    assert(contains(landed, "into Recordings/Vox 1.wav") && r.clip(clip) && r.clip(clip)->at == 0.0 && r.clip(clip)->track == vox);
    assert(r.clip(clip)->src == "Recordings/Vox 1.wav" && contains(r.text(), "src=\"Recordings/Vox 1.wav\"")); // beside the song, kept relative to it
    assert(r.svc.model().undoDepth == depth + 2 && r.svc.model().undoLabel == "Record");
    // punched 1 … 3: the clip starts at 1, two beats long, the burst a beat into it (the first take muted: what it plays
    // would come back down the cable too — the input is taken before the strip, so the take still hears the song)
    r.ok("set " + vox + ".mute=true");
    r.ok("transport punch 1 3");
    assert(r.svc.model().transport.punchFrom == 1.0 && r.svc.model().transport.punchTo == 3.0);
    const long long f1 = r.fake.capture->frames.load();
    r.ok("transport record --from 0 --count-in 0");
    waitFrames(r, f1 + 5 * 24000);
    const std::string punched = r.ok("transport stop");
    const std::string take2 = folder + "/Recordings/Vox 2.wav";
    assert(r.fake.written.count(take2) && r.fake.written[take2][0].size() == 2 * 24000);
    assert(onset(r.fake.written[take2][0], 0.01f) == want - 24000 && r.clip(firstLine(punched))->at == 1.0);
    r.ok("undo");                                                  // ONE step takes the take
    assert(!r.clip(firstLine(punched)) && r.clip(clip));
    r.ok("transport punch off");
    // refusals
    assert(contains(r.no("strip arm " + vox + " --input Main"), "is an output port"));
    const std::string bare = firstLine(r.ok("strip add --kind audio --name Bare"));
    assert(contains(r.no("strip arm " + bare), "has no input") && contains(r.no("strip arm " + bare), "prt_2"));
    assert(contains(r.no("strip arm ch_2 --input Mic"), "--input is an audio line's"));
    assert(contains(r.no("transport record --overdub"), "--overdub is the keyboard's"));
    assert(contains(r.no("transport punch 3 1"), "to > from"));
    assert(contains(r.no("set " + vox + ".monitor=loud"), "auto, on or off"));
    assert(contains(r.no("port add Mic --dir in"), "already called `Mic`"));
    pass("audio recording (R-REC-1): through a cable back, the take's burst on the render's very sample (out + in latency taken out); written beside the song, 32-bit, a clip where it began, one undo; punched 1…3 the clip starts at 1; refusals name the way");
}

static void test_an_armed_audio_line_is_monitored_as_set()
{
    // R-REC-1: monitoring — `auto` heard while stopped; `off` never; switching it heard at once; no capture, refused
    Run r;
    r.fake.inMode = "tone";
    r.ok("project new " + freshSong("monitor") + " --bpm 120");
    r.ok("port add Guitar --dir in --channels 1");
    r.ok("settings set port.Guitar=usb_in:0");
    const std::string g = firstLine(r.ok("strip add --kind audio --name Gtr"));
    const long long f0 = r.fake.capture->frames.load();
    r.ok("strip arm " + g + " --input Guitar");                      // auto, stopped: the monitor plays its input
    waitFrames(r, f0 + 24000);
    assert(r.fake.inDevice == "usb_in");                            // on the device the machine gives the port
    const size_t heardFrom = r.fake.capture->sink.size();
    waitFrames(r, (long long)heardFrom / 2 + 12000);
    float peak = 0;
    for (size_t i = heardFrom; i < r.fake.capture->sink.size(); ++i) peak = std::max(peak, std::fabs(r.fake.capture->sink[i]));
    assert(peak > 0.2f);                                             // the tone, through the strip
    r.ok("set " + g + ".monitor=off");                               // at once: the monitor stops
    const long long f1 = r.fake.capture->frames.load();
    r.ok("wait 0.05");
    assert(r.fake.capture->frames.load() == f1);                     // nothing plays
    // auto, playing the song back: the input is NOT heard (only while stopped or recording)
    r.ok("set " + g + ".monitor=auto");
    r.ok("transport play --from 0");
    const size_t playFrom = r.fake.capture->sink.size() + 2 * 4096;
    waitFrames(r, (long long)playFrom / 2 + 24000);
    float during = 0;
    for (size_t i = playFrom; i < r.fake.capture->sink.size(); ++i) during = std::max(during, std::fabs(r.fake.capture->sink[i]));
    r.ok("transport stop");
    assert(during < 1e-4f);
    r.ok("strip arm none");
    // R-DEV-9: the driver set to PipeWire and its capture linking nowhere — said once, recorded through PulseAudio
    r.fake.pipewire = "fail";
    r.ok("settings set driver=pipewire");
    r.events.clear();
    r.ok("strip arm " + g);
    bool said = false;
    for (const auto &e : r.events) said |= contains(e, "recording through PulseAudio: PipeWire: the stream linked to no input");
    assert(said && r.svc.model().armed == g);
    r.ok("strip arm none");
    r.ok("settings set driver=pulse");
    Run none;
    none.fake.canRecord = false;
    SolarisService deaf(none.fake.host()); // a host with no capture
    std::string err;
    assert(deaf.dispatchText("project new " + freshSong("nocapture") + " --bpm 120", err));
    assert(deaf.dispatchText("port add In --dir in", err));
    assert(deaf.dispatchText("strip add --kind audio --name A", err));
    const std::string a = deaf.output().substr(0, deaf.output().find('\n'));
    assert(!deaf.dispatchText("strip arm " + a + " --input In", err) && contains(err, "cannot record audio"));
    pass("monitoring (R-REC-1): auto heard while stopped, on the device the settings give the port; off stops it at once; a build with no capture refuses to arm an audio line");
}

static void test_a_looped_recording_makes_takes_and_a_comp_chooses()
{
    // R-REC-2: recording while the song loops 4 … 6 — the capture's level rises 0.1 each pass as it is HEARD (out + in
    // latency), so each take must be one level throughout: 0.1, 0.2, 0.3; the fourth pass, under half, is dropped. The
    // clip spans the loop; the last take plays where the comp does not say; `clip comp` puts take 1 over 4.5 … 5,
    // crossfaded 5 ms at each seam; one undo; refusals
    Run r;
    r.fake.outLatency = 0.02;
    r.fake.inLatency = 0.01;
    r.fake.inMode = "steps";
    r.fake.inPeriod = 2 * 24000;                                    // a pass: two beats at 120 bpm
    const std::string song = freshSong("takes");
    r.ok("project new " + song + " --bpm 120");
    r.ok("port add Mic --dir in");
    const std::string vox = firstLine(r.ok("strip add --kind audio --name Vox"));
    r.ok("set " + vox + ".monitor=off");
    r.ok("strip arm " + vox + " --input Mic");
    r.ok("transport loop 4 6");
    const long long f0 = r.fake.capture->frames.load();
    r.ok("transport record --from 4 --count-in 0");
    waitFrames(r, f0 + (long long)(3.4 * 2 * 24000));               // three passes and a bit
    const std::string said = r.ok("transport stop");
    assert(contains(said, "recorded 3 takes of 1.0 s"));
    const std::string clip = firstLine(said);
    const ClipModel *c = r.clip(clip);
    const std::string folder = song.substr(0, song.find_last_of('/'));
    assert(c && c->at == 4.0 && c->length == 2.0 && c->takes.size() == 3 && c->comp.empty() && c->takes[2] == "Recordings/Vox 3.wav");
    assert(c->src == "Recordings/Vox 3.wav");                       // the clip plays the last take, beside the song
    for (int k = 1; k <= 3; ++k)
    {
        const auto &w = r.fake.written[folder + "/Recordings/Vox " + std::to_string(k) + ".wav"][0];
        float lo = 1, hi = 0;
        for (float v : w) { lo = std::min(lo, v); hi = std::max(hi, v); }
        if (std::fabs(lo - 0.1f * k) > 1e-6f || std::fabs(hi - 0.1f * k) > 1e-6f) std::printf("    take %d: %.4f … %.4f\n", k, lo, hi);
        assert(w.size() == 2 * 24000 && std::fabs(lo - 0.1f * k) < 1e-6f && std::fabs(hi - 0.1f * k) < 1e-6f); // one pass, as heard
    }
    assert(contains(r.text(), "  #take src=\"Recordings/Vox 1.wav\"\n"));
    // the comp: take 1 over 4.5 … 5 — elsewhere the last; crossfaded 5 ms at each seam
    const int depth = r.svc.model().undoDepth;
    r.ok("clip comp " + clip + " --take 1 --from 4.5 --to 5");
    assert(r.clip(clip)->comp.size() == 1 && r.svc.model().undoDepth == depth + 1 && contains(r.text(), "#comp take=1 from=4.5 to=5.0"));
    r.ok("transport loop off");
    const std::string out = scratch() + "/takes.wav";
    r.ok("render --out " + out + " --from 4 --to 6 --bits 32f");
    const auto &L = r.fake.written[out][0];
    auto at = [&](double beat) { return L[(size_t)std::llround((beat - 4.0) * 24000)]; };
    assert(std::fabs(at(4.25) - 0.3f) < 1e-4f && std::fabs(at(4.75) - 0.1f) < 1e-4f && std::fabs(at(5.5) - 0.3f) < 1e-4f);
    const float seam = at(5.0);                                     // half way through the crossfade
    assert(seam > 0.15f && seam < 0.25f);
    assert(std::fabs(L[(size_t)(24000 + 260)] - 0.3f) < 1e-4f && std::fabs(L[(size_t)(24000 - 260)] - 0.1f) < 1e-4f); // 5 ms either side: done
    // a range over part of another: the rest of it stays
    r.ok("clip comp " + clip + " --take 2 --from 4.75 --to 5.5");
    const auto comp = r.clip(clip)->comp;
    assert(comp.size() == 2 && comp[0].take == 1 && comp[0].to == 4.75 && comp[1].take == 2 && comp[1].from == 4.75 && comp[1].to == 5.5);
    r.ok("undo");
    r.ok("undo");
    assert(r.clip(clip)->comp.empty());
    // refusals
    assert(contains(r.no("clip comp " + clip + " --take 4"), "--take is 1 … 3"));
    assert(contains(r.no("clip comp " + clip + " --take 1 --from 7 --to 8"), "the range is empty"));
    r.ok("clip add --src kick.wav --at 0");
    assert(contains(r.no("clip comp " + r.svc.model().clips.back().id + " --take 1"), "has no takes"));
    pass("takes (R-REC-2): a looped recording — each pass a take, one level throughout as HEARD (the latency taken out, the turn's tail the pass before's), a short last pass dropped; the clip spans the loop, the last take by default; a comp range plays its take, crossfaded 5 ms; a range over part of another keeps the rest; undo; refusals");
}

namespace
{
    // R-EDIT-1: a file the fake host hands back — two tones, so where a sample came from in it can be told
    std::string toneFile(Run &r, const std::string &name, long long frames)
    {
        const std::string path = scratch() + "/" + name + ".wav";
        std::vector<std::vector<float>> ch(2, std::vector<float>((size_t)frames));
        for (long long i = 0; i < frames; ++i)
            ch[0][(size_t)i] = ch[1][(size_t)i] = (float)(0.4 * std::sin(2 * M_PI * 440.0 * i / 48000) + 0.2 * std::sin(2 * M_PI * 1234.5 * i / 48000));
        r.fake.written[path] = ch;
        return path;
    }
    std::vector<float> heardL(Run &r, const std::string &name, double from, double to)
    {
        const std::string out = scratch() + "/" + name + ".wav";
        r.ok("render --out " + out + " --from " + std::to_string(from) + " --to " + std::to_string(to) + " --bits 32f");
        return r.fake.written[out][0];
    }
    double worstDiff(const std::vector<float> &a, const std::vector<float> &b)
    {
        double d = a.size() == b.size() ? 0.0 : 1e9;
        for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) d = std::max(d, (double)std::fabs(a[i] - b[i]));
        return d;
    }
    // a slice's pieces: the first line, then what `made:` names (R-SVC-8), in order
    std::vector<std::string> piecesOf(const std::string &said)
    {
        std::vector<std::string> out{firstLine(said)};
        const auto at = said.find("clip=");
        if (at == std::string::npos) return out;
        std::string cur;
        for (size_t i = at + 5; i < said.size() && said[i] != ' ' && said[i] != '\n'; ++i)
            if (said[i] == ',') { out.push_back(cur); cur.clear(); }
            else cur += said[i];
        if (!cur.empty()) out.push_back(cur);
        return out;
    }
}

static void test_a_clip_is_split_and_plays_the_same()
{
    // R-EDIT-1: `clip split` — two halves playing what the one did, to the sample (the gain both's, the fade in the
    // first's, the fade out the second's); a reversed clip played from its span's end, its first half that end; a
    // looping clip cut only where a repetition begins; one edit; refusals
    Run r;
    r.ok("project new " + freshSong("split") + " --bpm 120");
    const std::string src = toneFile(r, "split-src", 96000);       // 2 s: 4 beats
    const std::string ac = firstLine(r.ok("clip add --src " + src + " --at 1"));
    r.ok("set " + ac + ".gain=-3 " + ac + ".fadeIn=0.25 " + ac + ".fadeOut=0.5");
    const auto whole = heardL(r, "split-whole", 0, 6);
    const int depth = r.svc.model().undoDepth;
    const std::string second = firstLine(r.ok("clip split " + ac + " --at 2.5"));
    assert(r.svc.model().undoDepth == depth + 1 && r.svc.model().clips.size() == 2);
    {
        const ClipModel *a = r.clip(ac), *b = r.clip(second);
        assert(a && b && a->at == 1 && a->length == 1.5 && a->in == 0 && a->out == 0.75 && a->fadeIn == 0.25 && a->fadeOut == 0);
        assert(b->at == 2.5 && b->length == 2.5 && b->in == 0.75 && b->out == 2 && b->fadeIn == 0 && b->fadeOut == 0.5 && b->gain == -3);
        assert(b->lane == a->lane && b->track == a->track);
    }
    assert(heardL(r, "split-halves", 0, 6) == whole);              // to the sample
    r.ok("undo");
    assert(r.svc.model().clips.size() == 1 && r.clip(ac)->out == 2);
    // reversed: the span from its end; split, its first half is that end
    r.ok("set " + ac + ".fadeIn=0 " + ac + ".fadeOut=0 " + ac + ".gain=0 " + ac + ".reverse=true");
    assert(contains(r.text(), "reverse=true") && r.clip(ac)->reverse && r.ok("get " + ac + ".reverse") == "true\n");
    const auto back = heardL(r, "split-back", 1, 5);
    const auto file = r.fake.written[src][0];
    for (size_t i = 0; i < 96000; i += 997) assert(std::fabs(back[i] - file[95999 - i]) < 1e-6f);
    const std::string half = firstLine(r.ok("clip split " + ac + " --at 2"));
    assert(r.clip(ac)->in == 1.5 && r.clip(ac)->out == 2 && r.clip(half)->in == 0 && r.clip(half)->out == 1.5 && r.clip(half)->reverse);
    assert(heardL(r, "split-back2", 1, 5) == back);
    // looping: cut only where a repetition begins
    const std::string loop = firstLine(r.ok("clip add --src " + src + " --at 8 --length 12")); // repeats every 4 beats
    const auto looped = heardL(r, "split-loop", 8, 20);
    const std::string said = r.no("clip split " + loop + " --at 10");
    assert(contains(said, "loops every 4.0 beats") && contains(said, ": 12.0"));
    const std::string rest = firstLine(r.ok("clip split " + loop + " --at 12"));
    assert(r.clip(loop)->length == 4 && r.clip(rest)->at == 12 && r.clip(rest)->length == 8 && r.clip(rest)->loop);
    assert(heardL(r, "split-loop2", 8, 20) == looped);
    // refusals
    assert(contains(r.no("clip split " + ac + " --at 0.5"), "must fall inside"));
    assert(contains(r.no("clip split " + ac + " --at 2"), "must fall inside")); // its end
    assert(contains(r.no("clip split ac_99 --at 1"), "no clip"));
    const std::string notes = firstLine(r.ok("clip add --instrument synth --at 0 --length 4"));
    assert(contains(r.no("clip split " + notes + " --at 1"), "is a note clip"));
    assert(contains(r.no("set " + notes + ".reverse=true"), "is a note clip"));
    pass("split (R-EDIT-1): two halves play what the one did, to the sample — the gain both's, the fade in the first's, the fade out the second's; one edit; reversed, the span from its end and its first half that end; a loop cut only where a repetition begins, the refusal naming it; outside, a note clip refused");
}

static void test_overlapping_clips_on_a_lane_crossfade()
{
    // R-EDIT-1: two clips of one strip on one lane overlapping — the earlier fades out and the later in across the
    // overlap (linear: a level both carry stays that level through it); the heard fades published, the clips' own
    // untouched; on another lane, or one wholly inside another, they mix
    Run r;
    r.ok("project new " + freshSong("xfade") + " --bpm 120");
    const std::string path = scratch() + "/xfade-dc.wav";
    r.fake.written[path] = std::vector<std::vector<float>>(2, std::vector<float>(48000, 0.5f)); // 1 s, 2 beats, of 0.5
    const std::string a = firstLine(r.ok("clip add --src " + path + " --at 0"));
    const std::string b = firstLine(r.ok("clip add --src " + path + " --at 1.5"));
    assert(r.clip(b)->track == r.clip(a)->track && r.clip(b)->lane == r.clip(a)->lane);
    assert(r.clip(a)->fadeOutHeard == 0.5 && r.clip(b)->fadeInHeard == 0.5 && r.clip(a)->fadeInHeard == 0 && r.clip(b)->fadeOutHeard == 0);
    assert(r.clip(a)->fadeOut == 0 && r.clip(b)->fadeIn == 0 && !contains(r.text(), "fadeIn"));
    const auto L = heardL(r, "xfade", 0, 3.5);
    for (double beat : {0.5, 1.501, 1.6, 1.75, 1.9, 1.999, 2.5, 3.0})
    {
        const float v = L[(size_t)std::llround(beat * 24000)];
        if (std::fabs(v - 0.5f) > 1e-4f) std::printf("    at %.3f: %.5f\n", beat, v);
        assert(std::fabs(v - 0.5f) < 1e-4f);
    }
    // its own fade longer than the overlap: its own
    r.ok("set " + b + ".fadeIn=1");
    assert(r.clip(b)->fadeInHeard == 1);
    r.ok("undo");
    // on another lane: they mix
    r.ok("clip move " + b + " --lane none");
    assert(r.clip(a)->fadeOutHeard == 0 && r.clip(b)->fadeInHeard == 0);
    assert(std::fabs(heardL(r, "xfade2", 0, 3.5)[(size_t)(1.75 * 24000)] - 1.0f) < 1e-4f);
    r.ok("undo");
    // one inside another: they mix
    const std::string c = firstLine(r.ok("clip add --src " + path + " --at 0.5 --strip " + r.clip(a)->track));
    r.ok("set " + c + ".out=0.25");                                 // 0.5 … 1.0, inside a
    assert(r.clip(c)->lane == r.clip(a)->lane && r.clip(c)->fadeInHeard == 0 && r.clip(c)->fadeOutHeard == 0 && r.clip(a)->fadeOutHeard == 0.5);
    assert(std::fabs(heardL(r, "xfade3", 0, 3.5)[(size_t)(0.75 * 24000)] - 1.0f) < 1e-4f);
    pass("crossfades (R-EDIT-1): overlapping clips on a lane fade across the overlap — a level both carry holds through it; the heard fades published, the clips' own kept; a longer fade of its own stays; another lane, or one inside another, mixes");
}

static void test_consolidate_renders_clips_into_one()
{
    // R-EDIT-1: two clips of one strip — the second reversed, quieter, faded — joined into ONE clip of a file beside the
    // song: what they played, raw, the gap silent; the strip's fader, pan and device still after it, so the song sounds
    // as it did; one edit; refusals
    Run r;
    const std::string song = freshSong("bounce");
    r.ok("project new " + song + " --bpm 120");
    const std::string src = toneFile(r, "bounce-src", 48000);      // 1 s: 2 beats
    const std::string a = firstLine(r.ok("clip add --src " + src + " --at 1"));
    const std::string b = firstLine(r.ok("clip add --src " + src + " --at 4"));
    const std::string ch = r.clip(a)->track;
    r.ok("set " + b + ".reverse=true " + b + ".gain=-6 " + b + ".fadeOut=0.5 " + ch + ".gain=-4 " + ch + ".pan=0.3");
    r.ok("device add " + ch + " --type filter");
    const auto before = heardL(r, "bounce-before", 0, 7);
    const int depth = r.svc.model().undoDepth;
    const std::string one = firstLine(r.ok("clip consolidate " + a + " " + b));
    assert(r.svc.model().undoDepth == depth + 1);
    const std::string file = song.substr(0, song.find_last_of('/')) + "/Bounces/bounce-src 1.wav";
    assert(r.fake.written.count(file) && r.fake.bits[file] == 32);
    const auto raw = r.fake.written[file];
    const auto tone = r.fake.written[src][0];
    const ClipModel *c = r.clip(one);
    assert(c && r.svc.model().clips.size() == 1 && c->at == 1 && c->length == 5 && c->track == ch && c->src == "Bounces/bounce-src 1.wav");
    assert(raw[0].size() == 5 * 24000 && raw[0][1000] == tone[1000] && raw[1][1000] == tone[1000]); // raw: no fader, pan, filter
    assert(raw[0][60000] == 0.0f);                                  // the gap
    const float rev = raw[0][72000 + 1000], want = (float)(tone[47999 - 1000] * std::pow(10.0, -6.0 / 20.0));
    assert(std::fabs(rev - want) < 1e-6f);                          // the second: reversed, at its gain
    const double diff = worstDiff(heardL(r, "bounce-after", 0, 7), before);
    std::printf("    the song after: worst difference %.2e\n", diff);
    assert(diff < 1e-5);
    r.ok("undo");
    assert(r.svc.model().clips.size() == 2 && r.clip(a) && r.clip(b));
    // refusals
    const std::string notes = firstLine(r.ok("clip add --instrument synth --at 0 --length 4"));
    assert(contains(r.no("clip consolidate " + a + " " + notes), "is a note clip"));
    const std::string other = firstLine(r.ok("clip add --src " + toneFile(r, "bounce-other", 4800) + " --at 0"));
    assert(contains(r.no("clip consolidate " + a + " " + other), "consolidate joins clips of one strip"));
    assert(contains(r.no("clip consolidate ac_99"), "no clip"));
    pass("consolidate (R-EDIT-1): clips of one strip into ONE clip of a 32-bit file beside the song — what they played, raw (gain, fade, reverse in it; the strip's fader, pan, filter not), the gap silent; the song sounds as it did; one edit; a note clip, two strips refused");
}

static void test_slice_cuts_a_clip_or_makes_a_kit()
{
    // R-EDIT-1: a drum loop — hits at beats 0, 1, 2, 2.5, 3 of 4 — sliced every beat (clips playing what it did),
    // where its hits begin (the DSP library's onsets; reversed, where each swell does), or into a Sampler kit and a note clip that plays the same —
    // reversed, its slices reversed in its order; one edit each; refusals
    Run r;
    r.ok("project new " + freshSong("slice") + " --bpm 120");
    const std::string path = scratch() + "/slice-drums.wav";
    std::vector<std::vector<float>> drums(2, std::vector<float>(96000, 0.0f));
    for (double beat : {0.0, 1.0, 2.0, 2.5, 3.0})
        for (long long i = 0; i < 9000; ++i)
        {
            const size_t at = (size_t)std::llround(beat * 24000) + (size_t)i;
            drums[0][at] = drums[1][at] = (float)(0.8 * std::exp(-i / 2000.0) * std::sin(2 * M_PI * 180.0 * i / 48000));
        }
    r.fake.written[path] = drums;
    const std::string ac = firstLine(r.ok("clip add --src " + path + " --at 4"));
    const auto whole = heardL(r, "slice-whole", 4, 8);
    const int depth = r.svc.model().undoDepth;
    // every beat
    auto made = piecesOf(r.ok("clip slice " + ac + " --every 1"));
    assert(made.size() == 4 && made[0] == ac && r.svc.model().undoDepth == depth + 1);
    for (int k = 0; k < 4; ++k) assert(r.clip(made[(size_t)k])->at == 4 + k && r.clip(made[(size_t)k])->length == 1);
    assert(heardL(r, "slice-every", 4, 8) == whole);
    r.ok("undo");
    assert(r.svc.model().clips.size() == 1);
    // where a hit begins — not at its start
    made = piecesOf(r.ok("clip slice " + ac + " --transients"));
    std::string ats;
    for (const auto &id : made) ats += " " + canonicalBeats(r.clip(id)->at);
    std::printf("    cut at%s\n", ats.c_str());
    assert(made.size() == 5);
    const double want[] = {4, 5, 6, 6.5, 7};
    for (size_t k = 0; k < 5; ++k) assert(std::fabs(r.clip(made[k])->at - want[k]) < 0.01);
    assert(worstDiff(heardL(r, "slice-hits", 4, 8), whole) < 1e-6);
    r.ok("undo");
    // a kit: a Sampler in four slices, a note clip playing them in order — alone, it plays what the clip did
    const std::string kit = firstLine(r.ok("clip slice " + ac + " --every 1 --kit"));
    const std::string kitStrip = r.clip(kit)->track, dv = r.svc.project()->rack(kitStrip)->devices[0].id;
    assert(r.strip(kitStrip)->kind == "instrument" && r.clip(kit)->at == 4 && r.clip(kit)->length == 4 && r.clip(ac));
    assert(r.ok("get " + dv + ".mode") == "slices\n" && r.ok("get " + dv + ".slices") == "4.0\n");
    {
        const PatternModel *pt = patternIn(r, r.clip(kit)->pattern);
        assert(pt && pt->notes.size() == 4);
        for (int k = 0; k < 4; ++k) assert(pt->notes[(size_t)k].pitch == 60 + k && pt->notes[(size_t)k].at == k && pt->notes[(size_t)k].vel == 127);
    }
    r.ok("set " + r.clip(ac)->track + ".mute=true");
    double diff = worstDiff(heardL(r, "slice-kit", 4, 8), whole);
    std::printf("    the kit against the clip: worst difference %.2e\n", diff);
    assert(diff < 1e-4);
    r.ok("undo");
    r.ok("undo");
    // reversed: the kit plays its slices reversed, the span's last first
    r.ok("set " + ac + ".reverse=true");
    const auto backwards = heardL(r, "slice-back", 4, 8);
    // reversed, each hit is a swell: cut where it begins — the clip's order, not the file's
    made = piecesOf(r.ok("clip slice " + ac + " --transients"));
    const double swells[] = {4, 4.625, 5.125, 5.625, 6.625, 7.625};
    assert(made.size() == 6);
    for (size_t k = 0; k < 6; ++k) assert(std::fabs(r.clip(made[k])->at - swells[k]) < 0.01);
    r.ok("undo");
    const std::string kit2 = firstLine(r.ok("clip slice " + ac + " --every 1 --kit"));
    const std::string dv2 = r.svc.project()->rack(r.clip(kit2)->track)->devices[0].id;
    assert(r.ok("get " + dv2 + ".reverse") == "on\n" && patternIn(r, r.clip(kit2)->pattern)->notes[0].pitch == 63);
    r.ok("set " + r.clip(ac)->track + ".mute=true");
    diff = worstDiff(heardL(r, "slice-kit2", 4, 8), backwards);
    std::printf("    the reversed kit against the clip: worst difference %.2e\n", diff);
    assert(diff < 1e-4);
    // refusals
    assert(contains(r.no("clip slice " + ac + " --every 1 --transients"), "one of"));
    assert(contains(r.no("clip slice " + ac), "one of"));
    assert(contains(r.no("clip slice " + ac + " --transients --kit"), "--kit slices --every"));
    assert(contains(r.no("clip slice " + ac + " --every 1 --sensitivity 0.5"), "--sensitivity goes with --transients"));
    assert(contains(r.no("clip slice " + ac + " --every 4"), "is not shorter than"));
    assert(contains(r.no("clip slice " + ac + " --every 0.05 --kit"), "64 at most"));
    const std::string loop = firstLine(r.ok("clip add --src " + path + " --at 20 --length 8"));
    assert(contains(r.no("clip slice " + loop + " --every 1"), "loops"));
    assert(contains(r.no("clip slice " + kit2 + " --every 1"), "is a note clip"));
    pass("slice (R-EDIT-1): every beat — clips playing what it did; at its hits (the DSP onsets) — cuts within a hundredth of a beat, reversed where each swell begins; a kit — a Sampler in four slices and a note clip, alone the same sound, reversed its slices reversed in its order; one edit each; refusals");
}

namespace
{
    // R-WARP-1: a groove the fake host hands back — a kick on every beat, a snare on 2 and 4, a hat every eighth
    std::string grooveFile(Run &r, const std::string &name, double bpm, int beats)
    {
        const double fs = 48000.0, spb = 60.0 / bpm * fs;
        const long long n = (long long)std::llround(beats * spb);
        std::vector<float> x((size_t)n, 0.0f);
        unsigned seed = 5;
        auto noise = [&] { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / 16777216.0f - 0.5f; };
        auto hit = [&](double at, int kind) {
            const long long a = (long long)std::llround(at);
            for (long long i = 0; i < 12000 && a + i < n; ++i)
            {
                double v = 0;
                if (kind == 0) v = 0.8 * std::exp(-i / 3000.0) * std::sin(2 * M_PI * (60.0 + 80.0 * std::exp(-i / 400.0)) * i / fs);
                else if (kind == 1) v = 0.5 * std::exp(-i / 1500.0) * noise();
                else if (i < 2400) v = 0.15 * std::exp(-i / 300.0) * noise();
                x[(size_t)(a + i)] += (float)v;
            }
        };
        for (int b = 0; b < beats; ++b)
        {
            hit(b * spb, 0);
            if (b % 2 == 1) hit(b * spb, 1);
            hit(b * spb, 2);
            hit((b + 0.5) * spb, 2);
        }
        const std::string path = scratch() + "/" + name + ".wav";
        r.fake.written[path] = {x, x};
        return path;
    }
    std::string sineFile(Run &r, const std::string &name, double hz, long long frames)
    {
        std::vector<float> x((size_t)frames);
        for (long long i = 0; i < frames; ++i) x[(size_t)i] = (float)(0.5 * std::sin(2 * M_PI * hz * i / 48000.0));
        const std::string path = scratch() + "/" + name + ".wav";
        r.fake.written[path] = {x, x};
        return path;
    }
    double hzOf(const std::vector<float> &x, size_t from, size_t to)
    {
        double first = -1, last = -1;
        int count = 0;
        for (size_t i = std::max<size_t>(1, from); i < to && i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const double t = (double)(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
                if (first < 0) first = t;
                last = t;
                ++count;
            }
        return count > 1 ? (count - 1) * 48000.0 / (last - first) : 0.0;
    }
    double rmsIn(const std::vector<float> &x, size_t from, size_t to)
    {
        double e = 0;
        for (size_t i = from; i < to && i < x.size(); ++i) e += (double)x[i] * x[i];
        return std::sqrt(e / (double)std::max<size_t>(1, to - from));
    }
}

static void test_a_warped_clip_follows_the_tempo()
{
    // R-WARP-1: a groove dropped warped — its tempo heard on import (100, to the thousandth), its 8 beats its own; a
    // sine told its tempo and warped plays its 2 beats in the song's time, 440 Hz kept; pitched an octave, 880 Hz, as
    // long; the song slowed, it lasts its beats still; the .slp; pure; a warped split at its own beats; refusals
    Run r;
    r.ok("project new " + freshSong("warp") + " --bpm 120");
    const std::string g = firstLine(r.ok("clip add --src " + grooveFile(r, "warp-groove", 100.0, 8) + " --at 0 --warp"));
    std::printf("    the groove heard at %.3f bpm\n", r.clip(g)->bpm);
    assert(r.clip(g)->warp && std::fabs(r.clip(g)->bpm - 100.0) < 1e-3 && std::fabs(r.clip(g)->length - 8.0) < 1e-6);
    const std::string s = firstLine(r.ok("clip add --src " + sineFile(r, "warp-sine", 440.0, 96000) + " --at 16")); // 2 s: 4 beats at 120
    assert(r.clip(s)->length == 4 && !r.clip(s)->warp);
    r.ok("set " + s + ".bpm=60 " + s + ".warp=true");                // its own 60: 2 beats — played in 1 s
    assert(r.clip(s)->length == 2 && r.clip(s)->bpm == 60);
    auto heard = [&](const std::string &name, double from, double to) {
        const std::string out = scratch() + "/" + name + ".wav";
        r.ok("render --out " + out + " --from " + std::to_string(from) + " --to " + std::to_string(to) + " --bits 32f");
        return r.fake.written[out][0];
    };
    auto L = heard("warp-fast", 16, 19);
    double hz = hzOf(L, 4800, 43200);
    std::printf("    warped: %.3f Hz, %.3f in its second, %.5f after it\n", hz, rmsIn(L, 4800, 43200), rmsIn(L, 50400, 72000));
    assert(std::fabs(hz / 440.0 - 1.0) < 0.003 && std::fabs(rmsIn(L, 4800, 43200) - 0.3536) < 0.02 && rmsIn(L, 50400, 72000) < 1e-3);
    // an octave up: 880 Hz, as long
    r.ok("set " + s + ".pitch=12");
    L = heard("warp-octave", 16, 19);
    hz = hzOf(L, 4800, 43200);
    std::printf("    pitched: %.3f Hz\n", hz);
    assert(std::fabs(hz / 880.0 - 1.0) < 0.003 && rmsIn(L, 4800, 43200) > 0.3 && rmsIn(L, 50400, 72000) < 1e-3);
    assert(heard("warp-again", 16, 19) == L);                           // pure
    assert(contains(r.text(), "warp=true bpm=60.0 pitch=12.0") && r.ok("get " + s + ".bpm") == "60.0\n");
    // the song at 60: its two beats last two seconds
    r.ok("set project.bpm=60");
    assert(r.clip(s)->length == 2);
    L = heard("warp-slow", 16, 19);
    std::printf("    the song at 60: %.3f in its two seconds, %.5f after\n", rmsIn(L, 9600, 86400), rmsIn(L, 100800, 144000));
    assert(rmsIn(L, 9600, 86400) > 0.3 && rmsIn(L, 100800, 144000) < 1e-3 && std::fabs(hzOf(L, 9600, 86400) / 880.0 - 1.0) < 0.003);
    r.ok("set project.bpm=120");
    // cut at its own beat: a second of its file each
    const std::string half = firstLine(r.ok("clip split " + s + " --at 17"));
    assert(r.clip(s)->out == 1.0 && r.clip(half)->in == 1.0 && r.clip(half)->warp && r.clip(half)->bpm == 60 && r.clip(half)->length == 1);
    r.ok("undo");
    // refusals
    const std::string plain = firstLine(r.ok("clip add --src " + sineFile(r, "warp-plain", 330.0, 48000) + " --at 24"));
    assert(contains(r.no("set " + plain + ".warp=true"), "no tempo heard in " + plain));
    assert(contains(r.no("clip add --src " + scratch() + "/warp-plain.wav --at 30 --warp"), "no tempo heard"));
    assert(contains(r.no("set " + plain + ".pitch=30"), "pitch (semitones)"));
    assert(contains(r.no("set " + plain + ".bpm=5"), "bpm"));
    assert(contains(r.no("clip add --instrument synth --at 0 --warp"), "--warp is for an audio clip"));
    const std::string notes = firstLine(r.ok("clip add --instrument synth --at 0 --length 4"));
    assert(contains(r.no("set " + notes + ".warp=true"), "is a note clip"));
    assert(contains(r.no("clip slice " + s + " --every 1 --kit"), "consolidate it first"));
    pass("warp (R-WARP-1): a groove dropped warped is heard at 100 bpm, its 8 beats its own; a sine warped plays its 2 beats in the song's second at 440 Hz, an octave up at 880 as long, pure; the song slowed, its beats last; .slp; split at its own beat; refusals");
}

static void test_a_tempo_map_moves_time()
{
    // R-TEMPO-1: a click file at beats 2, 6 and 8; 120 bpm until beat 4, then 60 — each lands where the map puts its
    // beat; a glide back to 120 by beat 8 — where its closed form puts 6 and 8; the tempo where the time bar is; the
    // MIDI file's tempo changes; a warped clip across the change, 440 Hz on both sides; a time insert carries the change;
    // the .slp; refusals
    Run r;
    r.ok("project new " + freshSong("tempo") + " --bpm 120");
    std::vector<float> click(24000, 0.0f);
    for (int i = 0; i < 480; ++i) click[(size_t)i] = (float)(0.8 * std::sin(2 * M_PI * 1000.0 * i / 48000.0));
    const std::string path = scratch() + "/tempo-click.wav";
    r.fake.written[path] = {click, click};
    for (const char *at : {"2", "6", "8"}) r.ok("clip add --src " + path + " --at " + at);
    const std::string tp = firstLine(r.ok("tempo add --at 4 --bpm 60"));
    assert(contains(r.text(), "#atempo id=" + tp + " at=4.0 bpm=60.0"));
    auto onsets = [&](const std::string &name) {
        const std::string out = scratch() + "/" + name + ".wav";
        r.ok("render --out " + out + " --from 0 --to 12 --bits 32f");
        const auto &L = r.fake.written[out][0];
        std::vector<long long> at;
        for (size_t i = 0; i < L.size(); ++i)
            if (std::fabs(L[i]) > 0.05f && (at.empty() || (long long)i > at.back() + 4800)) at.push_back((long long)i);
        return at;
    };
    auto near = [](long long a, long long b) { return std::llabs(a - b - 1) <= 1; }; // the burst's first sample is a zero: found one after
    auto got = onsets("tempo-step");
    std::printf("    120 then 60 from beat 4: %lld %lld %lld\n", got.size() > 0 ? got[0] : -1, got.size() > 1 ? got[1] : -1, got.size() > 2 ? got[2] : -1);
    assert(got.size() == 3 && near(got[0], 48000) && near(got[1], 192000) && near(got[2], 288000));
    // a glide from 60 at 4 to 120 at 8: k = 15 bpm a beat, s(b) = 96000 + 48000·(60/15)·ln(bpm(b)/60)
    const std::string tp2 = firstLine(r.ok("tempo add --at 8 --bpm 120 --ramp"));
    got = onsets("tempo-glide");
    const long long six = std::llround(96000 + 48000.0 * 4.0 * std::log(90.0 / 60.0)), eight = std::llround(96000 + 48000.0 * 4.0 * std::log(2.0));
    std::printf("    gliding: beat 6 at %lld (%lld), beat 8 at %lld (%lld)\n", got.size() > 1 ? got[1] : -1, six, got.size() > 2 ? got[2] : -1, eight);
    assert(got.size() == 3 && near(got[0], 48000) && near(got[1], six) && near(got[2], eight));
    // the model: each change and where it is heard; the tempo where the time bar is
    r.ok("transport seek 6");
    const auto &m = r.svc.model();
    assert(m.tempos.size() == 2 && m.tempos[0].id == tp && m.tempos[1].ramp && std::fabs(m.tempos[1].seconds - (2.0 + 4.0 * std::log(2.0))) < 1e-9);
    assert(std::fabs(r.svc.model().transport.tempo - 90.0) < 1e-9);
    // the MIDI file: 120 at 0, then the glide's steps, then 120 at beat 8
    const std::string mid = scratch() + "/tempo.mid";
    const std::string nc = firstLine(r.ok("clip add --instrument synth --at 0 --length 4"));
    r.ok("notes add " + r.clip(nc)->pattern + " C4@0");
    r.ok("project export-midi " + mid);
    r.ok("undo");
    r.ok("undo");
    std::string bytes;
    {
        std::ifstream f(mid, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        bytes = ss.str();
    }
    std::vector<double> bpms;
    for (size_t i = 0; i + 5 < bytes.size(); ++i)
        if ((unsigned char)bytes[i] == 0xFF && bytes[i + 1] == 0x51 && bytes[i + 2] == 0x03)
        {
            const unsigned long us = ((unsigned long)(unsigned char)bytes[i + 3] << 16) | ((unsigned long)(unsigned char)bytes[i + 4] << 8) | (unsigned char)bytes[i + 5];
            bpms.push_back(60000000.0 / (double)us);
        }
    std::printf("    the MIDI file's tempos: %zu, first %.1f, at 4 %.1f, last %.1f\n", bpms.size(), bpms.front(), bpms.size() > 1 ? bpms[1] : 0.0, bpms.back());
    assert(bpms.size() == 1 + 1 + 16 + 1 && std::fabs(bpms[0] - 120) < 0.01 && std::fabs(bpms[1] - 60) < 0.01 && std::fabs(bpms.back() - 120) < 0.01);
    // a warped clip across the change (the glide gone): its own 2 beats from 3 — half a second at 120, a second at 60
    r.ok("tempo delete " + tp2);
    const std::string w = firstLine(r.ok("clip add --src " + sineFile(r, "tempo-sine", 440.0, 48000) + " --at 3"));
    r.ok("set " + w + ".bpm=120 " + w + ".warp=true");
    r.ok("set " + r.clip(r.svc.model().clips[0].id)->track + ".mute=true");
    const std::string out = scratch() + "/tempo-warp.wav";
    r.ok("render --out " + out + " --from 0 --to 8 --bits 32f");
    const auto &W = r.fake.written[out][0];
    const double fa = hzOf(W, 72000 + 2400, 96000 - 2400), fb = hzOf(W, 96000 + 4800, 144000 - 4800);
    std::printf("    warped across it: %.3f Hz then %.3f Hz; %.4f after\n", fa, fb, rmsIn(W, 146400, 190000));
    assert(std::fabs(fa / 440.0 - 1.0) < 0.003 && std::fabs(fb / 440.0 - 1.0) < 0.003 && rmsIn(W, 146400, 190000) < 1e-3 && rmsIn(W, 100000, 140000) > 0.3);
    // a time insert before it carries it
    r.ok("time insert --at 1 --length 2");
    assert(r.svc.project()->tempo(tp)->at == 6.0);
    r.ok("undo");
    // refusals
    assert(contains(r.no("tempo add --at 0 --bpm 90"), "the tempo at beat 0 is the song's"));
    assert(contains(r.no("tempo add --at 4 --bpm 90"), "already changes the tempo at 4.0"));
    assert(contains(r.no("tempo add --at 5 --bpm 5"), "--bpm is 20 … 999"));
    assert(contains(r.no("tempo move tp_99 --bpm 90"), "no tempo change"));
    assert(contains(r.no("auto create project.bpm"), "tempo add"));
    pass("a tempo map (R-TEMPO-1): clips land where the map puts their beats — a step and a glide to the sample of its closed form; the tempo at the time bar; the MIDI file's changes (a glide in sixteenths); a warped clip across a change keeps 440 Hz; a time insert carries the change; the .slp; refusals");
}

namespace
{
    void writeText(const std::string &path, const std::string &text)
    {
        fs::create_directories(fs::path(path).parent_path());
        std::ofstream f(path, std::ios::binary);
        f << text;
    }
    std::string readText(const std::string &path)
    {
        std::ifstream f(path, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
}

static void test_presets_are_saved_loaded_and_dropped()
{
    // R-PRESET-1: a synth's values saved as a preset of the user's (the machine's: no edit), loaded back over others —
    // every value to its default first, ONE edit; a factory preset loaded; a drop with a preset is one line; the user's
    // of a name before the factory's; listed and deleted (a factory's refused); refusals — a missing one naming those
    // there are, a preset of another type, an unknown value (nothing changed), a name with a slash, saving over one
    fs::remove_all(scratch() + "/presets");
    writeText(scratch() + "/presets/factory/synth/Warm.slpreset", "arstro-preset = 1\ntype = synth\nname = Warm\nosc1.wave = saw\nfilter.cutoff = 1800.0\n");
    writeText(scratch() + "/presets/factory/reverb/Hall.slpreset", "arstro-preset = 1\ntype = reverb\nmix = 0.35\n");
    Run r;
    r.ok("project new " + freshSong("presets") + " --bpm 120");
    r.ok("clip add --instrument synth --at 0 --length 4");           // ch_2, dv_1
    r.ok("set dv_1.filter.cutoff=500 dv_1.amp.attack=30");
    const int depth = r.svc.model().undoDepth;
    const bool dirty = r.svc.model().dirty;
    assert(r.ok("preset save dv_1 \"My Pad\"") == "saved synth preset \"My Pad\"\n");
    const std::string mine = readText(scratch() + "/presets/user/synth/My Pad.slpreset");
    assert(contains(mine, "type = synth\n") && contains(mine, "filter.cutoff = 500.0\n") && contains(mine, "amp.attack = 30.0\n"));
    assert(r.svc.model().undoDepth == depth && r.svc.model().dirty == dirty); // the machine's, not the song's
    auto has = [&](const std::string &type, const std::string &name, bool factory) {
        for (const auto &x : r.svc.model().presets)
            if (x.type == type && x.name == name && x.factory == factory) return true;
        return false;
    };
    assert(has("synth", "My Pad", false) && has("synth", "Warm", true) && has("reverb", "Hall", true));
    // loaded over other values: the preset's, the rest at their defaults — ONE edit
    r.ok("set dv_1.filter.cutoff=2000 dv_1.volume=-3");
    const int before = r.svc.model().undoDepth;
    r.ok("preset load dv_1 \"My Pad\"");
    assert(r.svc.model().undoDepth == before + 1);
    assert(r.ok("get dv_1.filter.cutoff") == "500.0\n" && r.ok("get dv_1.amp.attack") == "30.0\n" && r.ok("get dv_1.volume") != "-3.0\n");
    r.ok("undo");
    assert(r.ok("get dv_1.filter.cutoff") == "2000.0\n");
    // the factory's
    r.ok("preset load dv_1 Warm");
    assert(r.ok("get dv_1.osc1.wave") == "saw\n" && r.ok("get dv_1.filter.cutoff") == "1800.0\n" && r.ok("get dv_1.amp.attack") != "30.0\n");
    // a drop with a preset: one line
    const std::string ac = firstLine(r.ok("clip add --instrument synth --at 8 --length 4 --preset Warm"));
    const std::string dv2 = r.svc.project()->rack(r.clip(ac)->track)->devices[0].id;
    assert(r.ok("get " + dv2 + ".filter.cutoff") == "1800.0\n");
    const std::string fx = firstLine(r.ok("device add ch_2 --type reverb --preset Hall"));
    assert(r.ok("get " + fx + ".mix") == "0.35\n");
    // yours of a name before the factory's; listed; deleted — a factory's refused
    r.ok("set dv_1.filter.cutoff=777");
    r.ok("preset save dv_1 Warm");
    assert(has("synth", "Warm", false) && !has("synth", "Warm", true));
    r.ok("preset load " + dv2 + " Warm");
    assert(r.ok("get " + dv2 + ".filter.cutoff") == "777.0\n");
    const std::string list = r.ok("preset list synth");
    assert(contains(list, "synth: My Pad\n") && contains(list, "synth: Warm\n") && !contains(list, "reverb"));
    r.ok("preset delete synth Warm");
    assert(has("synth", "Warm", true) && !has("synth", "Warm", false));
    assert(contains(r.no("preset delete synth Warm"), "is a factory preset"));
    // refusals
    assert(contains(r.no("preset save dv_1 \"My Pad\""), "--replace"));
    r.ok("preset save dv_1 \"My Pad\" --replace");
    assert(contains(r.no("preset load dv_1 Nope"), "no synth preset `Nope` — the synth's: \"My Pad\" \"Warm\""));
    assert(contains(r.no("preset load dv_1 Hall"), "no synth preset `Hall`"));
    writeText(scratch() + "/presets/user/synth/Odd.slpreset", "arstro-preset = 1\ntype = reverb\nmix = 0.5\n");
    assert(contains(r.no("preset load dv_1 Odd"), "is a reverb preset"));
    writeText(scratch() + "/presets/user/synth/Bad.slpreset", "arstro-preset = 1\ntype = synth\nfilter.cutoff = 300.0\nfilter.cutof = 3\n");
    const std::string cut = r.ok("get dv_1.filter.cutoff");
    assert(contains(r.no("preset load dv_1 Bad"), "preset `Bad`") && r.ok("get dv_1.filter.cutoff") == cut); // all or nothing
    assert(contains(r.no("preset save dv_1 a/b"), "no slash"));
    assert(contains(r.no("clip add --src " + scratch() + "/x.wav --preset Warm"), "--preset goes with --instrument"));
    pass("presets (R-PRESET-1): saved as the user's (no edit, no unsaved mark); loaded over other values — the rest to their defaults, ONE edit, undone; a factory's; dropped with a clip or a device in one line; the user's of a name first; listed; deleted, a factory's refused; refusals — missing (naming those there are), another type, an unknown value (nothing changed), a slash, writing over one");
}

static void test_the_shipped_presets_load()
{
    // R-PRESET-1: every factory preset shipped with the app (apps/solaris/presets) loads onto a device of its type —
    // each of its values one `set` would take
    const fs::path shipped = fs::path(__FILE__).parent_path().parent_path().parent_path() / "presets";
    fs::remove_all(scratch() + "/presets");
    fs::create_directories(scratch() + "/presets/factory");
    fs::copy(shipped, scratch() + "/presets/factory", fs::copy_options::recursive);
    Run r;
    r.ok("project new " + freshSong("shipped") + " --bpm 120");
    const std::string audio = firstLine(r.ok("strip add --kind audio --name Bus"));
    int n = 0;
    const auto presets = r.svc.model().presets; // a copy: each command below rebuilds the model it lives in
    for (const auto &x : presets)
    {
        assert(x.factory);
        const arstro::DeviceType *t = arstro::DeviceRegistry::find(x.type);
        assert(t);
        std::string dv;
        if (t->kind == arstro::DeviceKind::Instrument)
        {
            const std::string ac = firstLine(r.ok("clip add --instrument " + x.type + " --at 0 --length 4"));
            dv = r.svc.project()->rack(r.clip(ac)->track)->devices[0].id;
        }
        else dv = firstLine(r.ok("device add " + audio + " --type " + x.type));
        r.ok("preset load " + dv + " \"" + x.name + "\"");
        ++n;
    }
    std::printf("    %d shipped presets loaded\n", n);
    assert(n >= 5);
    pass("the shipped presets (R-PRESET-1): every one loads onto a device of its type");
}

static void test_templates_start_songs()
{
    // R-PRESET-2: the open song kept as a template (the machine's: no edit) — its files made absolute, its markers and tempo
    // with it; a song started from it takes its own name and id and the template's tempo unless told; the factory's
    // shipped ones start songs; the user's of a name first; listed, deleted (a factory's refused); refusals
    const fs::path shipped = fs::path(__FILE__).parent_path().parent_path().parent_path() / "templates";
    fs::remove_all(scratch() + "/templates");
    fs::create_directories(scratch() + "/templates/factory");
    fs::copy(shipped, scratch() + "/templates/factory", fs::copy_options::recursive);
    fs::remove_all(scratch() + "/tplA");
    fs::create_directories(scratch() + "/tplA");
    const std::string kick = scratch() + "/tplA/kick.wav";
    Run r;
    r.fake.written[kick] = {std::vector<float>(4800, 0.25f), std::vector<float>(4800, 0.25f)};
    r.ok("project new " + scratch() + "/tplA/a.slp --bpm 99");
    r.ok("clip add --src " + kick + " --at 0");
    r.ok("marker add Verse --at 4");
    r.ok("tempo add --at 8 --bpm 140");
    assert(contains(r.text(), "src=kick.wav"));                      // the song keeps it relative to itself
    const int depth = r.svc.model().undoDepth;
    assert(r.ok("template save Mine") == "saved template \"Mine\"\n" && r.svc.model().undoDepth == depth);
    std::string kept;
    {
        std::ifstream f(scratch() + "/templates/user/Mine.slp");
        std::stringstream ss;
        ss << f.rdbuf();
        kept = ss.str();
    }
    assert(contains(kept, "src=\"" + kick + "\"") || contains(kept, "src=" + kick));
    assert(contains(kept, "#amarker") && contains(kept, "#atempo") && contains(kept, "bpm        = 99.0"));
    auto has = [&](const std::string &name, bool factory) {
        for (const auto &x : r.svc.model().templates)
            if (x.name == name && x.factory == factory) return true;
        return false;
    };
    assert(has("Mine", false) && has("Beat", true) && has("Piano Ballad", true));
    // a song from it: its own name and id, the template's tempo, the sample named wherever it is
    const std::string b = freshSong("from-mine");
    r.ok("project new " + b + " --template Mine");
    assert(r.svc.model().projectName == "from-mine" && r.svc.model().bpm == 99.0 && r.svc.model().markers.size() == 1 && r.svc.model().tempos.size() == 1);
    assert(r.svc.model().clips.size() == 1 && r.svc.model().clips[0].src == kick && contains(r.text(), "id         = prj_from_mine"));
    const std::string c = freshSong("from-mine-fast");
    r.ok("project new " + c + " --template Mine --bpm 120");
    assert(r.svc.model().bpm == 120.0);
    // the factory's: a beat's strips and its tempo
    const std::string d = freshSong("from-beat");
    r.ok("project new " + d + " --template Beat");
    std::set<std::string> names;
    for (const auto &st : r.svc.model().strips) names.insert(st.name);
    assert(names.count("Drums") && names.count("Bass") && names.count("Pad") && names.count("Reverb") && r.svc.model().bpm == 124.0);
    // the user's of a name first; listed; deleted, a factory's refused
    r.ok("set project.bpm=100");
    r.ok("template save Beat");
    assert(has("Beat", false) && !has("Beat", true));
    r.ok("project new " + freshSong("from-my-beat") + " --template Beat");
    assert(r.svc.model().bpm == 100.0);                              // the user's, not the factory's 124
    assert(contains(r.ok("template list"), "Beat\n") && contains(r.ok("template list"), "Piano Ballad  (factory)\n"));
    r.ok("template delete Beat");
    assert(has("Beat", true) && !has("Beat", false));
    assert(contains(r.no("template delete Beat"), "is a factory template"));
    // refusals
    assert(contains(r.no("project new " + freshSong("nope") + " --template Nope"), "no template `Nope` — there are:"));
    assert(contains(r.no("template save Mine"), "--replace"));
    assert(contains(r.no("template save a/b"), "no slash"));
    pass("templates (R-PRESET-2): the open song kept (no edit) — its files absolute, markers and tempo with it; a song from it named and id'd its own, the template's tempo unless told, its sample named absolutely; the factory's start songs; the user's of a name first; listed, deleted, a factory's refused; refusals");
}

static void test_the_sample_library_tags_and_finds()
{
    // R-PRESET-3: tags and favourites kept in the machine's library (on Home, no song: not an edit), in lower case; a
    // search by name through the sample folders and their sub-folders, by tag, and the library's own files wherever
    // they are; the favourites; untagged and unfavoured; refusals
    fs::remove(scratch() + "/library");
    Run r;
    const std::string kick = scratch() + "/kick.wav", far = "/elsewhere/Drums/snare 2.wav";
    r.ok("folder add " + scratch());
    assert(r.ok("sample tag " + kick + " Punchy") == kick + "\tpunchy\n");
    r.ok("sample tag " + kick + " drums");
    r.ok("sample tag \"" + far + "\" drums");
    r.ok("sample favourite \"" + far + "\"");
    std::string kept;
    {
        std::ifstream f(scratch() + "/library");
        std::stringstream ss;
        ss << f.rdbuf();
        kept = ss.str();
    }
    assert(contains(kept, kick + "\tdrums,punchy\n") && contains(kept, far + "\tdrums,favourite\n"));
    const auto &lib = r.svc.model().library;
    assert(lib.size() == 2 && lib[0].path == far && lib[0].favourite && lib[0].tags == std::vector<std::string>{"drums"} && lib[1].tags.size() == 2);
    // by name through the folders (the fake folder holds kick.wav and Loops/ at every level): the first found is the top's
    std::string out = r.ok("browse --search KIC");
    assert(contains(out, "audio\t" + kick + "\n") && !contains(out, "notes.txt") && r.svc.model().browser.search == "KIC");
    // by tag: the library's own, wherever it is
    out = r.ok("browse --search dru");
    assert(contains(out, far) && contains(out, kick));
    out = r.ok("browse --search pun");
    assert(contains(out, kick) && !contains(out, far));
    // the favourites
    out = r.ok("browse --favourites");
    assert(out == "audio\t" + far + "\n" && r.svc.model().browser.search == "favourites" && r.svc.model().browser.entries.size() == 1);
    // untagged, unfavoured: gone from the library when nothing is left
    r.ok("sample untag " + kick + " punchy");
    r.ok("sample untag " + kick + " drums");
    r.ok("sample favourite \"" + far + "\" off");
    assert(r.svc.model().library.size() == 1 && !r.svc.model().library[0].favourite);
    bool said = false;
    for (const auto &e : r.events) said |= contains(e, "library.changed") && contains(e, far);
    assert(said);
    // a plain folder listing clears the search
    r.ok("browse " + scratch());
    assert(r.svc.model().browser.search.empty());
    // refusals
    assert(contains(r.no("sample tag " + kick + " \"two words\""), "letters, digits or dashes"));
    assert(contains(r.no("sample favourite " + kick + " maybe"), "on|off"));
    assert(contains(r.no("browse"), "--search"));
    assert(contains(r.no("browse --search x --favourites"), "not both"));
    pass("the sample library (R-PRESET-3): tags and favourites kept in lower case on Home (not an edit), said; search by name through the folders, by tag, the library's own wherever they are; the favourites; untagged and unfavoured; a folder listing clears the search; refusals");
}

static void test_plugins_are_scanned_and_listed()
{
    // R-VST-6: `plugins scan` — what the host found, each a line, and each bundle it could not read; the list the machine's
    // (kept, read back by the next service, on Home: not an edit); `plugins list`; said
    fs::remove(scratch() + "/plugins");
    {
        Run r;
        const std::string out = r.ok("plugins scan");
        assert(contains(out, "0123456789ABCDEF0123456789ABCDEF\tinstrument\tSweet Pad (Acme)\n") && contains(out, "\teffect\tAir Verb (Acme)\n"));
        assert(contains(out, "problem: Broken.vst3: the plugin crashed while being read (Aborted)\n"));
        assert(r.svc.model().plugins.size() == 2 && r.svc.model().plugins[0].name == "Air Verb" && r.svc.model().plugins[1].kind == "instrument");
        bool said = false;
        for (const auto &e : r.events) said |= contains(e, "plugins.changed") && contains(e, "count=2") && contains(e, "problems=1");
        assert(said);
    }
    Run again; // the machine's list, read back
    assert(again.svc.model().plugins.size() == 2 && again.svc.model().plugins[1].path == "/vst3/Sweet.vst3");
    assert(contains(again.ok("plugins list"), "Sweet Pad (Acme)"));
    pass("plugins (R-VST-6): a scan's finds and its problems said, the list the machine's — kept and read back; `plugins list`");
}

static void test_a_plugin_plays_in_a_song()
{
    // R-VST-6: another maker's plugin in a song — an instrument strip made of one (and the refusals around it), its
    // parameters p<id> set, read, bound; a render on its own instance; an effect plugin replacing its input, its latency
    // compensated; one that crashes taken offline (said once, in the model, an instrument silent, an effect dry) and
    // reloaded; its state saved with the song and given back; a bundle that is not there offline, the song still open
    Run r;
    const std::string dir = scratch();
    PluginWorld &w = *r.fake.plugins;
    r.ok("plugins scan");
    const std::string song = freshSong("plugin");
    r.ok("project new " + song + " --bpm 120");
    assert(contains(r.no("clip add --plugin \"Sweet Pda\" --at 0"), "did you mean: Sweet Pad"));
    assert(contains(r.no("device add ch_1 --plugin \"Sweet Pad\""), "is an instrument — make it an instrument strip"));
    assert(contains(r.no("strip add --kind instrument --plugin \"Air Verb\""), "is an effect — put it in a rack"));
    assert(contains(r.no("device add ch_1 --plugin \"Air Verb\" --type reverb"), "not both"));
    assert(contains(r.no("strip add --kind bus --plugin \"Sweet Pad\""), "--plugin is for --kind instrument"));
    assert(w.started == 0);
    const std::string made = r.ok("clip add --plugin \"sweet pad\" --at 0 --length 4"); // by name, any case
    assert(contains(made, "made: strip=ch_2 device=dv_1 pattern=pt_1"));
    r.ok("notes add pt_1 \"A4@0:2\"");
    const StripModel *s = r.strip("ch_2");
    assert(s && s->name == "Sweet Pad" && s->devices.size() == 1);
    {
        const DeviceModel &d = s->devices[0];
        assert(d.type == "vst3" && d.plugin == "0123456789ABCDEF0123456789ABCDEF" && d.label == "Sweet Pad" && d.instrument && !d.offline && d.why.empty());
        assert(d.params.size() == 2 && d.params[0].name == "p1" && d.params[0].label == "Level" && d.params[0].value == 0.5 && d.params[0].max == 1.0);
    }
    assert(contains(r.text(), "#aeffect id=dv_1 type=vst3 plugin=0123456789ABCDEF0123456789ABCDEF name=\"Sweet Pad\" kind=instrument bundle=/vst3/Sweet.vst3"));
    assert(w.started == 1); // the live one, made when the model first asked
    auto render = [&](const std::string &name) {
        const std::string out = dir + "/" + name + ".wav";
        r.ok("render --out " + out + " --to 4 --bits 32f");
        return r.fake.written[out][0];
    };
    auto rms = [](const std::vector<float> &x, size_t a, size_t b) {
        double sum = 0;
        for (size_t i = a; i < b && i < x.size(); ++i) sum += (double)x[i] * x[i];
        return std::sqrt(sum / (double)(b - a));
    };
    const auto half = render("pl-half");
    assert(w.started == 2); // a render plays its own
    const double a = rms(half, 0, 48000);
    assert(std::fabs(a - 0.2 / std::sqrt(2.0)) < 0.002 && rms(half, 50000, 96000) < 1e-6); // 0.4 × 0.5, the note's two beats
    r.ok("set dv_1.p1=0.25");
    assert(r.ok("get dv_1.p1") == "0.25\n" && r.ok("get dv_1.p2") == "0.25\n"); // set, and the plugin's default
    assert(contains(r.no("set dv_1.p1=1.5"), "dv_1.p1 must be between 0.0 and 1.0"));
    assert(contains(r.no("set dv_1.p9=0.5"), "p1"));
    assert(std::fabs(rms(render("pl-quarter"), 0, 48000) / a - 0.5) < 0.01);
    r.ok("set dv_1.p1=\"=0\"");                                     // a formula drives it (R-AUTO-1): by name, at build
    assert(rms(render("pl-bound"), 0, 48000) == 0.0);
    r.ok("bind clear dv_1.p1");
    // an effect plugin replaces its input — at Gain 0.5 unity — and its latency is compensated: the same render
    w.latency = 64;
    r.ok("device add ch_2 --plugin \"Air Verb\"");                  // dv_2
    assert(r.strip("ch_2")->devices.size() == 2 && !r.strip("ch_2")->devices[1].instrument);
    const auto plain = render("pl-plain");
    bool same = plain.size() == half.size();
    for (size_t i = 0; same && i < plain.size(); ++i) same = std::fabs(plain[i] * 2 - half[i]) < 1e-6f; // Level 0.25 now: half
    assert(same);
    r.ok("set dv_2.p7=1");                                             // × 2
    assert(std::fabs(rms(render("pl-loud"), 0, 48000) / rms(plain, 0, 48000) - 2.0) < 0.01);
    // crashed in a render: its file says so — the effect passes dry, the instrument is silent
    w.crash = "FEDCBA9876543210FEDCBA9876543210";
    r.events.clear();
    const auto dry = render("pl-dry");
    float apart = dry.size() == plain.size() ? 0.0f : 1.0f;
    for (size_t i = 0; i < dry.size() && i < plain.size(); ++i) apart = std::max(apart, std::fabs(dry[i] - plain[i]));
    assert(apart < 1e-6f); // sample for sample: from its first block, and its latency kept (the engine compensated it)
    bool said = false;
    for (const auto &e : r.events) said |= contains(e, "render: dv_2 (Air Verb) was offline — it crashed (test); its input passes dry");
    assert(said);
    // … and live: playing, it goes offline — said once, the model shows why; `device reload` starts it again
    r.ok("transport play --from 0");
    waitFrames(r, 9600);
    r.ok("wait 0.05");
    r.ok("transport stop");
    int told = 0;
    for (const auto &e : r.events) told += contains(e, "plugin.offline") && contains(e, "device=dv_2") && contains(e, "it crashed (test)");
    assert(told == 1 && !w.liveOnMain); // only the audio thread plays a live instance: an engine is never warmed through one
    assert(r.strip("ch_2")->devices[1].offline && r.strip("ch_2")->devices[1].why == "it crashed (test)");
    assert(contains(r.ok("audit"), "offline: dv_2 (Air Verb) — it crashed (test) (device reload dv_2)"));
    assert(contains(r.no("device reload dv_9"), "no device"));
    w.crash.clear();
    const bool dirty = r.svc.model().dirty;
    assert(r.ok("device reload dv_2") == "playing\n" && !r.strip("ch_2")->devices[1].offline);
    assert(r.svc.model().dirty == dirty); // not an edit
    assert(contains(r.no("device reload ch_2"), "no device") && contains(r.no("preset save dv_2 Mine"), "its presets are its own"));
    // its state is the song's when it is saved — and given back when it is opened
    r.ok("project save");
    const std::string text = r.text();
    assert(contains(text, "p1=0.25 state=1:0.25,2:0.25,\n") && contains(text, "p7=1.0 state=7:1.0,\n")); // the reloaded one too
    w.statesSet.clear();
    r.ok("project close");
    r.ok("project open " + song);
    assert(r.strip("ch_2")->devices[0].params[0].value == 0.25);
    assert(std::find(w.statesSet.begin(), w.statesSet.end(), "1:0.25,2:0.25,") != w.statesSet.end());
    // a bundle that is not on this machine: the song opens, the device offline, said
    r.ok("project close");
    {
        std::string t = text;
        t.replace(t.find("/vst3/Air.vst3"), 14, "/vst3/Gone.vst3");
        std::ofstream(song, std::ios::binary) << t;
    }
    r.events.clear();
    r.ok("project open " + song);
    const DeviceModel &gone = r.strip("ch_2")->devices[1];
    assert(gone.offline && contains(gone.why, "Gone.vst3 is not a module directory") && gone.label == "Air Verb");
    told = 0;
    for (const auto &e : r.events) told += contains(e, "plugin.offline") && contains(e, "device=dv_2");
    assert(told == 1 && std::fabs(rms(render("pl-gone"), 0, 48000) / a - 0.5) < 0.01); // dry: what the instrument plays
    pass("a plugin in a song (R-VST-6): an instrument strip of one, refused where it does not belong; p<id> set, read, bound; a render's own instance; an effect replaces its input, its latency compensated; crashed — dry or silent, said once, in the model and the audit, reloaded; its state saved and given back; a missing bundle offline, the song open");
}

static void test_a_plugins_own_editor()
{
    // R-VST-6 (N26): `device editor` opens a plugin's own editor (titled by the plugin and its strip; not an edit) and
    // closes it; what it turns becomes `set <dv>.p<id>` — consecutive ones one undo step — landing once no command runs,
    // and never sent back to the plugin; a preset of its own moves the values the song sets; its window's own close is
    // said; refusals — no such device, a registry device, offline, no display
    Run r;
    PluginWorld &w = *r.fake.plugins;
    r.ok("plugins scan");
    r.ok("project new " + freshSong("editor") + " --bpm 120");
    r.ok("clip add --plugin \"Sweet Pad\" --at 0 --length 4");         // ch_2, dv_1
    r.ok("notes add pt_1 \"A4@0:4\"");
    r.ok("device add ch_2 --plugin \"Air Verb\"");                    // dv_2
    r.ok("project save");
    auto storedOf = [&](const std::string &dv) {
        for (const auto &rk : r.svc.project()->racks)
            for (const auto &d : rk.devices)
                if (d.id == dv) return d.params;
        return Fields();
    };
    const bool dirty = r.svc.model().dirty;
    r.events.clear();
    assert(r.ok("device editor dv_2") == "open\n");
    assert(w.editorTitle == "Air Verb — Sweet Pad" && r.strip("ch_2")->devices[1].editor && !r.strip("ch_2")->devices[0].editor);
    assert(r.svc.model().dirty == dirty); // not an edit
    bool said = false;
    for (const auto &e : r.events) said |= contains(e, "plugin.editor device=dv_2 open=1");
    assert(said);
    // turned in its editor, while playing: the song takes the last value once the running command (a wait) ends —
    // one step for the whole drag — and the plugin is not sent it back
    r.ok("transport play --from 0");
    waitFrames(r, 4800);
    w.received.clear();
    w.say = {PluginEvent{PluginEvent::Perform, 7, 0.6, {}}, PluginEvent{PluginEvent::Perform, 7, 0.7, {}}};
    r.ok("wait 0.03");                                                   // gathered inside the wait …
    assert(storedOf("dv_2").empty());                                     // … not yet landed
    assert(r.ok("get dv_2.p7") == "0.7\n");                             // landed first, before the next command
    w.say = {PluginEvent{PluginEvent::Perform, 7, 0.75, {}}};
    r.svc.pump();                                                         // the window's timer: no command running —
    assert(storedOf("dv_2").size() == 1 && storedOf("dv_2")[0].second == "0.75"); // landed at once
    assert(r.ok("get dv_2.p7") == "0.75\n");
    r.ok("wait 0.05");
    r.ok("transport stop");
    bool echoed = false;
    for (const auto &x : w.received) echoed |= x.first == 7;
    assert(!echoed);
    r.ok("undo");                                                         // the drag was ONE step
    assert(storedOf("dv_2").empty());
    r.ok("redo");
    // a preset of its own: the values the song sets follow, the others stay its state's
    w.say = {PluginEvent{PluginEvent::Values, 0, 0.0, {{7, 0.25}}}};
    r.ok("wait 0.02");
    assert(r.ok("get dv_2.p7") == "0.25\n");
    // its window's own close: said, the model shows it
    r.events.clear();
    w.say = {PluginEvent{PluginEvent::EditorClosed, 0, 0.0, {}}};
    r.ok("wait 0.02");
    assert(!r.strip("ch_2")->devices[1].editor);
    said = false;
    for (const auto &e : r.events) said |= contains(e, "plugin.editor device=dv_2 open=0");
    assert(said);
    assert(r.ok("device editor dv_1") == "open\n" && r.ok("device editor dv_1 --close") == "closed\n" && !r.strip("ch_2")->devices[0].editor);
    // refusals
    r.ok("device add ch_2 --type reverb");                                // dv_3
    assert(contains(r.no("device editor dv_3"), "only a plugin has an editor of its own"));
    assert(contains(r.no("device editor dv_9"), "no device"));
    w.noDisplay = true;
    assert(contains(r.no("device editor dv_1"), "there is no display to open it on"));
    w.noDisplay = false;
    w.crash = "0123456789ABCDEF0123456789ABCDEF";
    r.ok("transport play --from 0");
    waitFrames(r, 4800);
    r.ok("transport stop");
    assert(contains(r.no("device editor dv_1"), "dv_1 is offline: it crashed (test) — `device reload dv_1` first"));
    w.crash.clear();
    pass("a plugin's own editor (R-VST-6, N26): opened titled by plugin and strip, not an edit, said; what it turns lands as set p<id> once no command runs — a drag one step, never echoed; its own preset moves the set values; its window's close said; refused for no device, a registry device, offline, no display");
}

static void test_midi_learn_and_the_transport_keys()
{
    // R-CTRL-1: a MIDI control bound to a number (`map add`; the machine's, not an edit) moves it over its travel — a gain
    // by the fader law, a device's parameter by its taper — the last of a sweep landing, the sweep one undo step; on its
    // channel only when it names one; `map learn` binds the next control moved (its value not taken), --cancel; `map
    // remove`; MIDI Machine Control plays and stops; refusals; a mapping the song lacks inert, said by audit; kept
    const std::string settings = scratch() + "/map-settings.txt";
    fs::remove(settings);
    {
        Run r(settings, std::string());
        r.ok("project new " + freshSong("learn") + " --bpm 120");
        r.ok("clip add --instrument synth --at 0 --length 4");               // ch_2, dv_1
        r.ok("project save");
        assert(r.ok("map add 74 ch_2.gain") == "74\tch_2.gain\n");
        assert(!r.svc.model().dirty);                                         // the machine's, not an edit
        {
            std::ifstream f(settings);
            std::stringstream ss;
            ss << f.rdbuf();
            assert(contains(ss.str(), "map.74 = ch_2.gain\n"));
        }
        assert(r.svc.model().settings.mappings.size() == 1 && r.svc.model().settings.mappings[0].cc == "74" && !r.svc.model().settings.mappings[0].inert);
        // moved: the fader law, the top +6 dB; a sweep — its last value lands, ONE undo step
        r.fake.key(0xB0, 74, 127);
        r.ok("wait 0.02");
        assert(r.ok("get ch_2.gain") == "6.0\n");
        for (int v : {100, 64, 32})
        {
            r.fake.key(0xB0, 74, (uint8_t)v);
            r.ok("wait 0.02");
        }
        assert(std::fabs(std::stod(r.ok("get ch_2.gain")) - faderDb(32 / 127.0)) < 1e-9);
        r.ok("undo");
        assert(r.ok("get ch_2.gain") == "0.0\n");
        // on its channel only, when it names one
        r.ok("map add 2/10 ch_2.pan");
        r.fake.key(0xB0, 10, 127);                                             // channel 1: not this one
        r.ok("wait 0.02");
        assert(r.ok("get ch_2.pan") == "0.0\n");
        r.fake.key(0xB1, 10, 0);
        r.ok("wait 0.02");
        assert(r.ok("get ch_2.pan") == "-1.0\n");
        // a device's parameter by its taper — as its knob turns
        r.ok("map add 21 dv_1.filter.cutoff");
        const arstro::DeviceType *t = arstro::DeviceRegistry::find("synth");
        const arstro::ParamSpec &cut = t->params[(size_t)t->paramIndex("filter.cutoff")];
        r.fake.key(0xB0, 21, 64);
        r.ok("wait 0.02");
        assert(std::fabs(std::stod(r.ok("get dv_1.filter.cutoff")) - arstro::valueFromNormalized(cut, 64 / 127.0)) < 1e-6);
        r.fake.key(0xB0, 21, 127);
        r.ok("wait 0.02");
        assert(std::fabs(std::stod(r.ok("get dv_1.filter.cutoff")) - cut.max) < 1e-9);
        // learned: the next control moved, on its channel; its value not taken
        assert(contains(r.ok("map learn dv_1.filter.res"), "move a MIDI control for dv_1.filter.res"));
        assert(r.svc.model().mapLearning == "dv_1.filter.res");
        const std::string res0 = r.ok("get dv_1.filter.res");
        r.events.clear();
        r.fake.key(0xB2, 30, 99);
        r.ok("wait 0.02");
        assert(r.svc.model().mapLearning.empty() && contains(r.ok("map list"), "3/30\tdv_1.filter.res\n"));
        bool said = false;
        for (const auto &e : r.events) said |= contains(e, "map.learned cc=3/30 address=dv_1.filter.res");
        assert(said && r.ok("get dv_1.filter.res") == res0);
        r.fake.key(0xB2, 30, 127);
        r.ok("wait 0.02");
        assert(r.ok("get dv_1.filter.res") != res0);
        r.ok("map learn ch_2.pan");
        assert(r.ok("map learn --cancel") == "not waiting\n" && r.svc.model().mapLearning.empty());
        r.ok("map remove 2/10");
        r.fake.key(0xB1, 10, 127);
        r.ok("wait 0.02");
        assert(r.ok("get ch_2.pan") == "-1.0\n");
        // MIDI Machine Control: play, stop
        r.fake.key(0xF0, 0x02, 0x7F);
        r.ok("wait 0.02");
        r.ok("wait 0.05");                                                     // landed first thing in this one
        assert(r.svc.model().transport.playing);
        r.fake.key(0xF0, 0x01, 0x7F);
        r.ok("wait 0.02");
        r.ok("wait 0.02");
        assert(!r.svc.model().transport.playing);
        // refusals
        assert(contains(r.no("map add 128 ch_2.gain"), "is not a MIDI control"));
        assert(contains(r.no("map add 17/1 ch_2.gain"), "is not a MIDI control"));
        r.no("map add 5 ch_2.name");
        r.no("map add 5 ch_9.gain");
        assert(contains(r.no("map remove 99"), "no mapping for 99"));
        assert(contains(r.no("map learn"), "map learn <address>"));
        // another song: what it lacks is inert, and said
        r.ok("project new " + freshSong("learn2") + " --bpm 120");
        bool inert = false;
        for (const auto &m : r.svc.model().settings.mappings) inert |= m.cc == "74" && m.inert;
        assert(inert && contains(r.ok("audit"), "inert mapping: MIDI control 74 moves ch_2.gain, which this song does not have"));
    }
    Run again(settings, std::string());                                      // the machine's: read back
    assert(again.svc.model().settings.mappings.size() == 3);
    pass("MIDI learn (R-CTRL-1): a control moves its number — the fader law, a parameter's taper — the sweep's last landing, one undo step; on its channel when named; learned (its value not taken), cancelled, removed; MMC plays and stops; refusals; inert in a song without it, said; the machine's, kept");
}

static void test_the_surface_codecs()
{
    // R-CTRL-2: Mackie Control and HUI as tables — messages in, events out; a state in, the bytes that show it out (only
    // what changed; a touched fader left alone); HUI's ping
    auto ev = [](SurfaceCodec &c, uint8_t a, uint8_t b, uint8_t d) {
        SurfaceEvent e;
        return c.decode(MidiEvent{a, b, d}, e) ? e : SurfaceEvent{};
    };
    auto has = [](const std::vector<uint8_t> &v, std::vector<uint8_t> n) { return std::search(v.begin(), v.end(), n.begin(), n.end()) != v.end(); };
    auto mcu = makeSurfaceCodec("mcu");
    assert(mcu && !makeSurfaceCodec("xyz"));
    SurfaceEvent e = ev(*mcu, 0xE2, 0x7F, 0x7F);
    assert(e.kind == SurfaceEvent::Fader && e.strip == 2 && e.value == 1.0);
    e = ev(*mcu, 0xE8, 0x00, 0x40);
    assert(e.kind == SurfaceEvent::Master && std::fabs(e.value - 8192.0 / 16383.0) < 1e-12);
    e = ev(*mcu, 0xB0, 0x13, 0x05);
    assert(e.kind == SurfaceEvent::Pot && e.strip == 3 && e.value == 5.0);
    assert(ev(*mcu, 0xB0, 0x13, 0x43).value == -3.0);
    assert(ev(*mcu, 0x90, 0x10, 0x7F).kind == SurfaceEvent::Mute && ev(*mcu, 0x90, 0x09, 0x7F).kind == SurfaceEvent::Solo && ev(*mcu, 0x90, 0x09, 0x7F).strip == 1);
    assert(ev(*mcu, 0x90, 0x02, 0x7F).kind == SurfaceEvent::Arm && ev(*mcu, 0x90, 0x1A, 0x7F).kind == SurfaceEvent::Select);
    assert(ev(*mcu, 0x90, 0x10, 0x00).kind == SurfaceEvent::None); // let go: nothing
    assert(ev(*mcu, 0x90, 0x5E, 0x7F).kind == SurfaceEvent::Play && ev(*mcu, 0x90, 0x5D, 0x7F).kind == SurfaceEvent::Stop && ev(*mcu, 0x90, 0x5F, 0x7F).kind == SurfaceEvent::Record);
    assert(ev(*mcu, 0x90, 0x5B, 0x7F).kind == SurfaceEvent::Rewind && ev(*mcu, 0x90, 0x5C, 0x7F).kind == SurfaceEvent::Forward);
    assert(ev(*mcu, 0x90, 0x2F, 0x7F).kind == SurfaceEvent::BankRight && ev(*mcu, 0x90, 0x30, 0x7F).kind == SurfaceEvent::ChannelLeft);
    assert(ev(*mcu, 0x90, 0x68, 0x7F).kind == SurfaceEvent::Touch && ev(*mcu, 0x90, 0x68, 0x00).kind == SurfaceEvent::Release && ev(*mcu, 0x90, 0x70, 0x7F).strip == 8);
    SurfaceState was, now;
    now.strips[0] = SurfaceStrip{true, 0.5, 0.0, true, false, false, "Kick drum"};
    now.master = 1.0;
    std::array<bool, kSurfaceStrips + 1> none{};
    std::vector<uint8_t> out;
    mcu->encode(was, now, true, none, out);
    assert(has(out, {0xE0, 0x00, 0x40}) && has(out, {0x90, 0x10, 0x7F}) && has(out, {0xB0, 0x30, 0x16}) && has(out, {0xE8, 0x7F, 0x7F}));
    assert(has(out, {0xE1, 0x00, 0x00}) && has(out, {0xB0, 0x31, 0x00}));                           // an empty slot: down, dark
    assert(has(out, {0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, 0x00, 'K', 'i', 'c', 'k', ' ', 'd', ' '}));  // six characters and a gap
    out.clear();
    mcu->encode(now, now, false, none, out);
    assert(out.empty()); // nothing changed: nothing sent
    SurfaceState lit = now;
    lit.strips[0].mute = false;
    mcu->encode(now, lit, false, none, out);
    assert((out == std::vector<uint8_t>{0x90, 0x10, 0x00}));
    out.clear();
    lit.strips[0].fader = 0.75;
    auto touched = none;
    touched[0] = true;
    mcu->encode(now, lit, false, touched, out);
    assert(!has(out, {0xE0}));                                                                      // the hand's
    // HUI
    auto hui = makeSurfaceCodec("hui");
    SurfaceEvent h;
    assert(!hui->decode(MidiEvent{0xB0, 0x00, 0x40}, h));                                          // the high half waits for the low
    e = ev(*hui, 0xB0, 0x20, 0x00);
    assert(e.kind == SurfaceEvent::Fader && e.strip == 0 && std::fabs(e.value - 8192.0 / 16383.0) < 1e-12);
    ev(*hui, 0xB0, 0x0F, 0x02);
    assert(ev(*hui, 0xB0, 0x2F, 0x42).kind == SurfaceEvent::Mute);
    ev(*hui, 0xB0, 0x0F, 0x02);
    assert(ev(*hui, 0xB0, 0x2F, 0x40).kind == SurfaceEvent::Touch && ev(*hui, 0xB0, 0x2F, 0x00).kind == SurfaceEvent::Release);
    ev(*hui, 0xB0, 0x0F, 0x0E);
    assert(ev(*hui, 0xB0, 0x2F, 0x44).kind == SurfaceEvent::Play);
    ev(*hui, 0xB0, 0x0F, 0x0A);
    assert(ev(*hui, 0xB0, 0x2F, 0x43).kind == SurfaceEvent::BankRight);
    assert(ev(*hui, 0xB0, 0x41, 0x42).value == 2.0 && ev(*hui, 0xB0, 0x41, 0x02).value == -2.0 && ev(*hui, 0x90, 0x00, 0x7F).kind == SurfaceEvent::Ping);
    out.clear();
    hui->encode(was, now, true, none, out);
    assert(has(out, {0xB0, 0x00, 0x40, 0xB0, 0x20, 0x00}) && has(out, {0xB0, 0x0C, 0x00, 0xB0, 0x2C, 0x42}));
    assert(has(out, {0xF0, 0x00, 0x00, 0x66, 0x05, 0x00, 0x10, 0x00, 'K', 'i', 'c', 'k', 0xF7}));
    out.clear();
    hui->keepAlive(out);
    assert((out == std::vector<uint8_t>{0x90, 0x00, 0x00}));
    assert(ringPosition(-1.0) == 1 && ringPosition(0.0) == 6 && ringPosition(1.0) == 11);
    pass("surface codecs (R-CTRL-2): Mackie Control and HUI decoded — faders 14-bit, pots relative, buttons pressed, touch, transport, bank — and encoded as a diff: faders, lights, rings, names; nothing when nothing changed; a touched fader left alone; HUI's ping");
}

static void test_a_control_surface()
{
    // R-CTRL-2: a surface on its own port (the keyboard deaf to it): it connects and shows everything; its fader moves a
    // gain by the fader law, its pot a pan, its buttons mute and the transport; the song's changes move it back —
    // not a fader a hand is on, until let go; another bank; none disconnects; HUI pings; refusals
    Run r;
    r.fake.withSurface = true;
    r.ok("project new " + freshSong("surface") + " --bpm 120");
    r.ok("clip add --instrument synth --at 0 --length 4");
    r.ok("clip add --src kick.wav --at 0");
    const auto &strips = r.svc.model().strips;
    const std::string a = strips[0].id, b = strips[1].id;
    r.events.clear();
    r.ok("settings set surface=mcu surfacePort=X-Touch");
    r.ok("wait 0.02");
    assert(r.svc.model().surface.connected && r.svc.model().settings.surface == "mcu" && r.svc.model().surface.why.empty());
    bool said = false;
    for (const auto &e : r.events) said |= contains(e, "surface.changed protocol=mcu connected=1 bank=0");
    assert(said);
    auto &out = *r.fake.surfaceOut;
    auto has = [&](std::vector<uint8_t> n) { return std::search(out.begin(), out.end(), n.begin(), n.end()) != out.end(); };
    assert(has({0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, 0x00}));                                      // it shows everything
    out.clear();
    r.ok("wait 0.03");
    assert(out.empty());                                                                           // … and then only what changes
    // the keyboard is deaf to the surface's port
    r.ok("strip arm " + a);
    assert(contains(r.ok("midi devices"), "Keys  (listening)") && !contains(r.ok("midi devices"), "X-Touch  (listening)"));
    r.ok("strip arm none");
    // its fader: the fader law (the top is +6 dB); its pot: a pan; its mute — and they show at once (every command ends
    // with a pump)
    out.clear();
    r.fake.surfaceKey(0xE0, 0x7F, 0x7F);
    r.fake.surfaceKey(0xB1, 0x10, 0x00);                                                           // (a pot on channel 2: Mackie's are on 1)
    r.fake.surfaceKey(0xB0, 0x11, 0x05);                                                           // strip 2's pot, five steps clockwise
    r.fake.surfaceKey(0x90, 0x10, 0x7F);                                                           // strip 1's mute
    r.ok("wait 0.02");
    assert(r.ok("get " + a + ".gain") == "6.0\n" && r.ok("get " + b + ".pan") == "0.1\n" && r.ok("get " + a + ".mute") == "true\n");
    assert(has({0x90, 0x10, 0x7F}) && has({0xB0, 0x31, 0x17}));                                   // its light, its ring
    // the song moves it back
    out.clear();
    r.ok("set " + b + ".gain=-6");
    const int v = (int)std::lround(faderPos(-6.0) * 16383.0);
    assert(has({0xE1, (uint8_t)(v & 0x7F), (uint8_t)(v >> 7)}));
    // not a fader a hand is on — until it is let go
    r.fake.surfaceKey(0x90, 0x69, 0x7F);
    r.ok("wait 0.02");
    out.clear();
    r.ok("set " + b + ".gain=-12");
    r.ok("wait 0.02");
    assert(!has({0xE1}));
    r.fake.surfaceKey(0x90, 0x69, 0x00);
    r.ok("wait 0.02");
    const int w = (int)std::lround(faderPos(-12.0) * 16383.0);
    assert(has({0xE1, (uint8_t)(w & 0x7F), (uint8_t)(w >> 7)}));
    // the transport keys, and the play light
    out.clear();
    r.fake.surfaceKey(0x90, 0x5E, 0x7F);
    r.ok("wait 0.02");
    r.ok("wait 0.05");
    assert(r.svc.model().transport.playing && has({0x90, 0x5E, 0x7F}));
    r.fake.surfaceKey(0x90, 0x5D, 0x7F);
    r.ok("wait 0.02");
    r.ok("wait 0.02");
    assert(!r.svc.model().transport.playing);
    // another bank: one channel on — its first strip is the song's second
    r.events.clear();
    r.fake.surfaceKey(0x90, 0x31, 0x7F);
    r.ok("wait 0.02");
    assert(r.svc.model().surface.bank == 1);
    r.fake.surfaceKey(0xE0, 0x00, 0x00);                                                           // strip 1 is now `b`: to the bottom
    r.ok("wait 0.02");
    assert(r.ok("get " + b + ".gain") == "-120.0\n");
    said = false;
    for (const auto &e : r.events) said |= contains(e, "surface.changed protocol=mcu connected=1 bank=1");
    assert(said);
    // none: disconnected, said; the keyboard hears every port again
    r.events.clear();
    r.ok("settings set surface=none");
    r.ok("wait 0.02");
    assert(!r.svc.model().surface.connected);
    said = false;
    for (const auto &e : r.events) said |= contains(e, "surface.changed protocol=none connected=0");
    assert(said);
    // HUI: its fader pair; its ping
    r.ok("set " + a + ".gain=0");
    out.clear();
    r.ok("settings set surface=hui");
    r.ok("wait 0.02");
    r.fake.surfaceKey(0xB0, 0x00, 0x7F);
    r.fake.surfaceKey(0xB0, 0x20, 0x7F);
    r.ok("wait 0.02");
    r.ok("wait 0.02");
    assert(r.svc.model().surface.connected && r.ok("get " + a + ".gain") == "6.0\n" && has({0x90, 0x00, 0x00}));
    // refusals, and a port that is not there
    assert(contains(r.no("settings set surface=xyz"), "surface is none, mcu (Mackie Control) or hui"));
    r.ok("settings set surface=mcu surfacePort=Nowhere");
    r.ok("wait 0.02");
    assert(!r.svc.model().surface.connected && contains(r.svc.model().surface.why, "Nowhere"));
    r.ok("settings set surface=none surfacePort=");
    pass("a control surface (R-CTRL-2): on its own port, the keyboard deaf to it; connected it shows everything; its fader (the fader law), pot and mute move the song; the song moves it back — not a touched fader until let go; the transport keys and light; another bank; none disconnects; HUI's fader pair and ping; refusals; a missing port said");
}

static void test_the_cpu_meter()
{
    // R-PERF-1: while playing, the audio thread's load is published — finite, a fraction of a block for this small song,
    // its worst block held — and each strip's share: a synth playing a chord costs more than an empty bus; stopped, 0;
    // a render is never timed (its bytes the same either way: R-RENDER-1)
    Run r;
    r.ok("project new " + freshSong("cpu") + " --bpm 120");
    r.ok("clip add --instrument synth --at 0 --length 16");
    r.ok("notes add pt_1 \"C4@0:16 E4@0:16 G4@0:16 C5@0:16 E5@0:16\"");
    const std::string synth = r.svc.model().clips.back().track;
    const std::string bus = firstLine(r.ok("strip add --kind bus --name Empty"));
    assert(r.svc.model().transport.cpu == 0.0f);
    r.ok("render --out " + scratch() + "/cpu-a.wav --to 2 --bits 32f");
    r.ok("transport play --from 0");
    waitFrames(r, 48000);
    r.ok("wait 0.05");
    const TransportModel &t = r.svc.model().transport;
    std::printf("    load %.4f, worst %.4f\n", t.cpu, t.cpuPeak);
    assert(t.cpu > 0.0f && t.cpu < 10.0f && t.cpuPeak > 0.0f && std::isfinite(t.cpuPeak));
    float sc = -1, bc = -1;
    for (const auto &s : r.svc.model().strips)
    {
        if (s.id == synth) sc = s.cpu;
        if (s.id == bus) bc = s.cpu;
    }
    std::printf("    the synth %.5f, the empty bus %.5f\n", sc, bc);
    assert(sc > 0.0f && sc <= t.cpu * 1.2f + 1e-3f && bc >= 0.0f && sc > bc); // a strip's time is part of the block's
    r.ok("transport stop");
    assert(r.svc.model().transport.cpu == 0.0f && r.svc.model().transport.cpuPeak == 0.0f);
    for (const auto &s : r.svc.model().strips) assert(s.cpu == 0.0f);
    r.ok("render --out " + scratch() + "/cpu-b.wav --to 2 --bits 32f");
    assert(r.fake.written[scratch() + "/cpu-a.wav"] == r.fake.written[scratch() + "/cpu-b.wav"]);
    pass("the CPU meter (R-PERF-1): playing, the load published — finite, its worst block held — and each strip's share (a chord's synth more than an empty bus); stopped, 0; a render untimed, its bytes the same");
}

static void test_the_engine_on_several_threads()
{
    // R-PERF-2: `settings set threads` — a render on one thread and on four, byte-identical; live playback on four is
    // that render (as one thread's is, DR-PLAY-1); auto and refusals
    Run r;
    r.ok("project new " + freshSong("threads") + " --bpm 120");
    for (const char *inst : {"synth", "drums", "synth", "drums", "piano"})
    {
        const std::string ac = firstLine(r.ok(std::string("clip add --instrument ") + inst + " --at 0 --length 8"));
        const std::string pt = r.clip(ac)->pattern;
        r.ok("notes add " + pt + " \"C3@0:2 G3@1:1:90 E4@2:3 C5@4:2:70 A3@6:1\"");
    }
    r.ok("strip add --kind bus --name Verb");
    const std::string verb = r.svc.model().strips.back().id;
    r.ok("device add " + verb + " --type reverb");
    const auto strips = r.svc.model().strips; // a copy: each command below rebuilds the model it lives in (D-15)
    for (const auto &s : strips)
        if (s.kind == "instrument") r.ok("send add " + s.id + " --to " + verb + " --gain -6");
    const std::string dir = scratch();
    auto render = [&](const std::string &threads) {
        r.ok("settings set threads=" + threads);
        const std::string out = dir + "/threads-" + threads + ".wav";
        r.ok("render --out " + out + " --to 8 --bits 32f");
        return r.fake.written[out];
    };
    const auto one = render("1"), four = render("4");
    float peak = 0;
    for (float v : one[0]) peak = std::max(peak, std::fabs(v));
    assert(peak > 0.05f && one == four);
    // live, four threads: what the device hears is the render, frame for frame
    r.ok("settings set threads=4");
    r.fake.capture->sink.clear();
    r.fake.capture->frames = 0;
    r.ok("transport play --from 0");
    waitFrames(r, 96000);
    r.ok("transport stop");
    r.ok("render --out " + dir + "/threads-ports.wav --to 8 --bits 32f --ports");
    const auto &port = r.fake.written[dir + "/threads-ports.Main.wav"];
    const auto &live = r.fake.capture->sink;
    bool same = port.size() == 2 && live.size() >= 2 * 90000; // (no device here is latent: no lag to allow for)
    for (size_t i = 0; same && i < 90000; ++i) same = live[2 * i] == port[0][i] && live[2 * i + 1] == port[1][i];
    assert(same);
    r.ok("settings set threads=auto");
    assert(r.svc.model().settings.threads == "auto");
    assert(contains(r.no("settings set threads=0"), "threads is auto or 1 … 16"));
    assert(contains(r.no("settings set threads=17"), "threads is auto or 1 … 16"));
    assert(contains(r.no("settings set threads=two"), "threads is auto or 1 … 16"));
    pass("the engine on several threads (R-PERF-2): a render on one thread and on four, byte-identical; live on four is that render, frame for frame; auto; refusals");
}

namespace
{
    long long gSimNs = 0;
    long long simNow() { return gSimNs; }
    struct SimOut : IAudioOut
    {
        std::vector<float> *got = nullptr;
        bool open(const std::string &, int, int, int, std::string &) override { return true; }
        bool write(const float *x, int n) override { got->insert(got->end(), x, x + n); return true; }
        double latency() override { return 0.0; }
    };
}

static void test_a_follower_alone()
{
    // R-DEV-2/4, Follower in SIMULATED time (its clock injected, its device a plain list): a clock 200 ppm slow and a
    // device block unlike the clock's, the follower started at that drift (as a second play is) — an impulse comes out
    // exactly `target` frames after the clock made it (within a frame); the clock stalling starves it — silence, counted,
    // primed again, and back on time; its device stalling fills the ring — pushes dropped, counted, and it is emptied
    // and primed again: on time at once (not drained over a minute)
    std::vector<float> got;
    auto o = std::make_unique<SimOut>();
    o->got = &got;
    Follower::Config c;
    c.device = "sim";
    c.rate = 48000;
    c.channels = 1;
    c.block = 128;
    c.target = 1040;
    c.startPpm = 200.0; // the truth below: the device 200 ppm fast against the clock
    Follower f(std::move(o), c);
    f.now = simNow;
    assert(f.state().driftPpm == 200.0);
    const double fc = 48000.0 * (1 - 200e-6), ff = 48000.0;
    const int bc = 256;
    long long made = 0;
    double tPush = 0, tPull = 0;
    std::vector<std::pair<long long, double>> impulses; // (clock frame, when made — the continuous clock's)
    std::vector<double> pulledAt;                        // when each device block was pulled
    std::vector<float> block((size_t)bc);
    auto run = [&](double until, bool clockStalls, bool deviceStalls) {
        while (std::min(tPush, tPull) < until)
        {
            if (tPush <= tPull)
            {
                if (!clockStalls)
                {
                    for (int i = 0; i < bc; ++i, ++made)
                    {
                        block[(size_t)i] = made % 9600 == 4800 ? 1.0f : 0.0f; // an impulse every 0.2 s
                        if (block[(size_t)i] != 0.0f) impulses.emplace_back(made, tPush + i / fc);
                    }
                    gSimNs = (long long)std::llround(tPush * 1e9);
                    f.push(block.data(), bc);
                }
                tPush += bc / fc;
            }
            else
            {
                if (!deviceStalls)
                {
                    gSimNs = (long long)std::llround(tPull * 1e9);
                    pulledAt.push_back(tPull);
                    f.pullOnce();
                }
                tPull += c.block / ff;
            }
        }
    };
    auto lateness = [&](double from, double to) { // each impulse in [from, to): how late it came out, frames past target
        double worst = 0;
        int seen = 0;
        for (const auto &im : impulses)
        {
            if (im.second < from || im.second >= to) continue;
            // where it came out: the device's loudest frame near when it should have (target frames later)
            const double want = im.second + c.target / ff;
            long long best = -1;
            for (size_t b = 0; b < pulledAt.size(); ++b)
                if (std::fabs(pulledAt[b] - want) < 0.01)
                    for (int j = 0; j < c.block; ++j)
                    {
                        const long long k = (long long)b * c.block + j;
                        if (k < (long long)got.size() && (best < 0 || got[(size_t)k] > got[(size_t)best])) best = k;
                    }
            if (best < 0 || best + 1 >= (long long)got.size() || got[(size_t)best] < 0.5f) return 1e9; // not there
            // the peak between frames (a parabola through the loudest three): the resampler places an impulse anywhere
            const double a = got[(size_t)best - 1], m = got[(size_t)best], b = got[(size_t)best + 1];
            const double at = pulledAt[(size_t)(best / c.block)] + (best % c.block + 0.5 * (a - b) / (a - 2 * m + b)) / ff;
            worst = std::max(worst, std::fabs(at - want) * ff);
            ++seen;
        }
        return seen ? worst : 1e9;
    };
    run(3.0, false, false);
    const double onTime = lateness(0.5, 2.8);
    // the clock stalls 60 ms: it starves, is silent, primes again and comes back on time
    run(3.06, true, false);
    run(6.0, false, false);
    const double afterStall = lateness(3.5, 5.8);
    const long long starved = f.state().starved;
    // its device stalls 300 ms (the ring holds about 170 ms): pushes dropped; then it plays on
    run(6.3, false, true);
    const long long dropped = f.state().dropped;
    run(9.0, false, false);
    const double afterDrop = lateness(7.0, 8.8);
    std::printf("    a follower alone: impulses out %.2f frames from the target's delay; after the clock stalled (%lld blocks starved) %.2f; after its device stalled (%lld pushes dropped) %.2f\n",
                onTime, starved, afterStall, dropped, afterDrop);
    assert(onTime <= 1.0 && afterStall <= 1.0 && afterDrop <= 1.0); // a frame: 21 µs
    assert(starved > 0 && dropped > 0);
    pass("a follower alone (R-DEV-2/4): in simulated time its sound comes out exactly its target after the clock made it (within a frame), whatever their blocks and a 200 ppm drift; a stalled clock starves it — silent, primed again, on time; a stalled device drops pushes, counted, and it is on time again at once");
}

static void test_a_port_on_another_device_follows_the_clock()
{
#ifdef __SANITIZE_THREAD__
    constexpr bool kRealTime = false; // ThreadSanitizer runs every thread several times slower: no device keeps pace
#else
    constexpr bool kRealTime = true;
#endif
    // R-DEV-1/2/4/5: Main on the clock (10 ms of latency), Phones on card-b (its crystal 300 ppm fast, 4 ms), Cue on
    // card-c (it will not open) — every device paced in REAL time. One sound — an impulse at 0.25 s, then 1 kHz — to
    // all three: card-c is said and silent, the song plays on; card-b plays it continuously (no gap, no click, its
    // level), and the impulse is HEARD on both at the same moment (within 1 ms); the devices published while playing
    Run r;
    (*r.fake.devices)[""] = Paced{0.0, 0.010};
    (*r.fake.devices)["card-b"] = Paced{300.0, 0.004};
    (*r.fake.devices)["card-c"].fails = "no such device";
    const std::string song = freshSong("devices"), dir = scratch(), file = dir + "/impulse.wav";
    std::vector<float> x(96000, 0.0f);
    x[12000] = 1.0f;
    for (size_t i = 24000; i < x.size(); ++i) x[i] = (float)(0.5 * std::sin(2 * M_PI * 1000.0 * (double)i / 48000.0));
    r.fake.written[file] = {x, x};
    r.ok("project new " + song + " --bpm 120");
    const std::string ch = r.clip(firstLine(r.ok("clip add --src " + file + " --at 0")))->track;
    r.ok("port add Phones --dir out");
    r.ok("port add Cue --dir out");
    r.ok("send add " + ch + " --to prt_2");
    r.ok("send add " + ch + " --to prt_3");
    r.ok("settings set port.Phones=card-b:0 port.Cue=card-c:0");
    r.events.clear();
    r.ok("transport play");
    r.ok("wait 1.0");
    {
        const auto devs = r.svc.model().transport.devices; // a copy: the next command rebuilds the model
        assert(devs.size() == 3 && devs[0].role == "clock" && devs[0].id.empty());
        const AudioDeviceModel *b = nullptr, *c = nullptr;
        for (const auto &d : devs)
        {
            if (d.id == "card-b") b = &d;
            if (d.id == "card-c") c = &d;
        }
        assert(b && b->role == "follower" && b->problem.empty());
        assert(!kRealTime || (b->starved == 0 && b->dropped == 0 && b->xruns == 0));
        assert(c && c->role == "follower" && c->problem == "no such device");
        std::printf("    clock %.1f ms, card-b %.1f ms (%+.2f ppm so far)\n", devs[0].latencyMs, b->latencyMs, b->driftPpm);
        assert(!kRealTime || std::fabs(b->latencyMs - devs[0].latencyMs) < 12.0); // the same moment, but for what each device holds now
    }
    bool said = false;
    for (const auto &e : r.events) said = said || contains(e, "the device card-c did not open (no such device) — its ports are silent");
    assert(said);
    r.ok("wait 1.3");
    r.ok("transport stop");
    assert(r.svc.model().transport.devices.empty());
    const Paced &clock = (*r.fake.devices)[""], &b = (*r.fake.devices)["card-b"];
    assert(!(*r.fake.devices)["card-c"].opened && b.ch == 2 && clock.frames > 48000 && b.frames > 48000);
    // the impulse — the loudest frame — on each: when it was heard (card-b's through the resampler, its peak placed)
    auto peakOf = [](const Paced &p) {
        long long at = 0;
        for (long long k = 0; k < p.frames; ++k)
            if (std::fabs(p.sink[(size_t)(k * p.ch)]) > std::fabs(p.sink[(size_t)(at * p.ch)])) at = k;
        const double a = p.sink[(size_t)((at - 1) * p.ch)], m = p.sink[(size_t)(at * p.ch)], c = p.sink[(size_t)((at + 1) * p.ch)];
        assert(m > 0.5f && at > 0); // the impulse itself, not a quiet frame
        const double frac = 0.5 * (a - c) / (a - 2 * m + c);
        return std::make_pair(at, p.heardAt(at) + frac / 48000.0);
    };
    const auto pc = peakOf(clock), pb = peakOf(b);
    std::printf("    the impulse heard at %.3f ms on the clock, %.3f ms on card-b: %.3f ms apart\n", 0.0, (pb.second - pc.second) * 1e3,
                std::fabs(pb.second - pc.second) * 1e3);
    assert(!kRealTime || std::fabs(pb.second - pc.second) < 1e-3);
    // card-b's 1 kHz, a second of it: no gap or click (no step past the sine's own slope), at its level
    double steepest = 0.0, e = 0.0;
    const long long from = pb.first + 24000, to = from + 48000;
    assert(to < b.frames);
    for (long long k = from; k < to; ++k)
    {
        const double v = b.sink[(size_t)(k * 2)];
        steepest = std::max(steepest, std::fabs(v - b.sink[(size_t)((k - 1) * 2)]));
        e += v * v;
    }
    const double level = 20 * std::log10(std::sqrt(e / 48000.0) / (0.5 / std::sqrt(2.0)));
    std::printf("    card-b's 1 kHz: its steepest step %.4f (the sine's own %.4f), its level %+.3f dB\n", steepest, 0.5 * 2 * M_PI * 1000.0 / 48000.0, level);
    assert(!kRealTime || (steepest < 0.5 * 2 * M_PI * 1000.0 / 48000.0 * 1.02 && std::fabs(level) < 0.05));
    // played again, the clock now the SLOWER path (40 ms): nothing on the clock is delayed — the follower's target
    // carries the difference — and the impulse still heard together; card-b starts from the drift it was left at
    double was = 0.0;
    for (auto &kv : *r.fake.devices)
    {
        kv.second.sink.clear();
        kv.second.blocks.clear();
        kv.second.frames = 0;
        kv.second.started = false;
        kv.second.next = 0;
    }
    (*r.fake.devices)[""].latency = 0.040;
    {
        r.ok("transport play --from 0");
        const auto devs = r.svc.model().transport.devices;
        for (const auto &d : devs) if (d.id == "card-b") was = d.driftPpm;
    }
    r.ok("wait 0.8");
    r.ok("transport stop");
    const auto qc = peakOf((*r.fake.devices)[""]), qb = peakOf((*r.fake.devices)["card-b"]);
    std::printf("    the clock slower: %.3f ms apart; card-b started at %+.2f ppm\n", std::fabs(qb.second - qc.second) * 1e3, was);
    assert(!kRealTime || std::fabs(qb.second - qc.second) < 1e-3);
    assert(was != 0.0); // the drift it was left at, not 0
    pass("a port on another device (R-DEV-1/2/4/5): a follower held to the clock in real time — its sound continuous and at its level, the impulse heard on both at the same moment (within 1 ms) whichever path is slower; a device that will not open said and silent, the song playing on; the devices published while playing; a follower's drift kept for the next play");
}

namespace
{
    /** R-DEV-2: a capture in SIMULATED time — blocks land when the test says; it says what it holds (`held`). */
    struct SimIn : IAudioIn
    {
        std::vector<float> stream;            // every frame it captured
        std::vector<long long> landedAt;      // each block's landing (ns)
        int block = 256;
        long long consumed = 0;
        bool open(const std::string &, int, int, int, std::string &) override { return true; }
        bool held(long long &frames, long long &lastNs, int &lastFrames) const override
        {
            if (landedAt.empty()) return false;
            frames = (long long)stream.size() - consumed;
            lastNs = landedAt.back();
            lastFrames = block;
            return true;
        }
        int read(float *x, int n) override
        {
            const int k = (int)std::max(0LL, std::min<long long>(n, (long long)stream.size() - consumed));
            for (int i = 0; i < n; ++i) x[i] = i < k ? stream[(size_t)(consumed + i)] : 0.0f;
            consumed += k;
            return k;
        }
        double latency() override { return (double)((long long)stream.size() - consumed) / 48000.0; }
    };
}

static void test_an_input_held_alone()
{
    // R-DEV-2/4, DriftIn in SIMULATED time: a capture 200 ppm fast landing 256-frame blocks, the clock reading 128 at a
    // time, started at that drift — an impulse is read exactly `target` frames after it landed (within a frame); its
    // device stalling starves it (silence, counted, primed again, on time); the clock stalling leaves it far ahead
    // (emptied, primed again, on time)
    auto owned = std::make_unique<SimIn>();
    SimIn *in = owned.get();
    const double target = 1040.0, fin = 48000.0 * (1 + 200e-6), fc = 48000.0;
    DriftIn d(std::move(owned), "sim-in", 48000, 1, 128, target, 200.0);
    d.now = simNow;
    assert(d.state().driftPpm == 200.0);
    double tLand = 0, tRead = 0;
    std::vector<std::pair<double, int>> impulses; // when each impulse's frame is due by the continuous clock (s), and nothing else
    std::vector<float> out;
    std::vector<double> readAt;
    std::vector<float> buf(128);
    auto run = [&](double until, bool deviceStalls, bool clockStalls) {
        while (std::min(tLand, tRead) < until)
        {
            if (tLand <= tRead)
            {
                if (!deviceStalls)
                {
                    for (int i = 0; i < in->block; ++i)
                    {
                        const long long k = (long long)in->stream.size();
                        const bool hit = k % 9600 == 4800;
                        in->stream.push_back(hit ? 1.0f : 0.0f);
                        if (hit) impulses.emplace_back(tLand + i / fin, 0); // landed with its block, at its place in it
                    }
                    in->landedAt.push_back((long long)std::llround(tLand * 1e9));
                }
                tLand += in->block / fin;
            }
            else
            {
                if (!clockStalls)
                {
                    gSimNs = (long long)std::llround(tRead * 1e9);
                    d.read(buf.data(), 128);
                    out.insert(out.end(), buf.begin(), buf.end());
                    readAt.push_back(tRead);
                }
                tRead += 128 / fc;
            }
        }
    };
    auto lateness = [&](double from, double to) {
        double worst = 0;
        int seen = 0;
        for (const auto &im : impulses)
        {
            if (im.first < from || im.first >= to) continue;
            const double want = im.first + target / fc;
            long long best = -1;
            for (size_t b = 0; b < readAt.size(); ++b)
                if (std::fabs(readAt[b] - want) < 0.01)
                    for (int j = 0; j < 128; ++j)
                    {
                        const long long k = (long long)b * 128 + j;
                        if (best < 0 || out[(size_t)k] > out[(size_t)best]) best = k;
                    }
            if (best < 1 || best + 1 >= (long long)out.size() || out[(size_t)best] < 0.5f) return 1e9;
            const double a = out[(size_t)best - 1], m = out[(size_t)best], b = out[(size_t)best + 1];
            const double at = readAt[(size_t)(best / 128)] + (best % 128 + 0.5 * (a - b) / (a - 2 * m + b)) / fc;
            worst = std::max(worst, std::fabs(at - want) * fc);
            ++seen;
        }
        return seen ? worst : 1e9;
    };
    run(3.0, false, false);
    const double onTime = lateness(0.5, 2.8);
    run(3.06, true, false); // its device stalls 60 ms
    run(6.0, false, false);
    const double afterStall = lateness(3.5, 5.8);
    const long long starved = d.state().starved;
    run(6.3, false, true);  // the clock stalls 300 ms
    run(9.0, false, false);
    const double afterClock = lateness(7.0, 8.8);
    std::printf("    an input alone: read %.2f frames from its target's delay; after its device stalled (%lld blocks starved) %.2f; after the clock stalled %.2f\n",
                onTime, starved, afterStall, afterClock);
    assert(onTime <= 1.0 && afterStall <= 1.0 && afterClock <= 1.0 && starved > 0);
    pass("an input held alone (R-DEV-2/4): in simulated time what it captures is read exactly its target after it landed (within a frame), its drift as started; a stalled device starves it — silent, primed again, on time; a stalled clock — emptied, primed again, on time");
}

static void test_an_input_on_another_device_follows_the_clock()
{
    // R-DEV-1/2/4: the guitar's input on card-in (its crystal 500 ppm fast, 3 ms), the clock paced in real time too —
    // the armed line monitors it: what reaches the clock is its tone, continuous (no gap, no click) and at its level;
    // the input published while playing, its latency its device's and its target's; the default input untouched
    Run r;
    (*r.fake.devices)[""] = Paced{0.0, 0.010};
    (*r.fake.pacedIns)["card-in"] = PacedIn{500.0, 0.003, 256};
    r.ok("project new " + freshSong("input-device") + " --bpm 120");
    const std::string ch = firstLine(r.ok("strip add --kind audio --name Guitar"));
    r.ok("port add Guitar --dir in --channels 1");
    r.ok("set " + ch + ".input=prt_2 " + ch + ".monitor=on");
    r.ok("settings set port.Guitar=card-in:0");
    r.ok("strip arm " + ch);
    r.ok("transport play");
    r.ok("wait 1.0");
    {
        const auto devs = r.svc.model().transport.devices;
        const AudioDeviceModel *in = nullptr;
        for (const auto &d : devs)
            if (d.role == "input") in = &d;
        assert(in && in->id == "card-in" && in->starved == 0 && in->driftPpm > 0.0); // its crystal fast, and read so
        std::printf("    the input on card-in: %.1f ms, %+.2f ppm so far\n", in->latencyMs, in->driftPpm);
        assert(in->latencyMs > 3.0 + 21.0 && in->latencyMs < 3.0 + 21.7 + 12.0); // its device's, the target's (and its ring's turn)
    }
    r.ok("wait 0.6");
    r.ok("transport stop");
    const Paced &clock = (*r.fake.devices)[""];
    const long long from = 24000, to = from + 48000;
    assert(clock.frames > to);
    double steepest = 0.0, e = 0.0;
    for (long long k = from; k < to; ++k)
    {
        const double v = clock.sink[(size_t)(k * 2)];
        steepest = std::max(steepest, std::fabs(v - clock.sink[(size_t)((k - 1) * 2)]));
        e += v * v;
    }
    const double level = 20 * std::log10(std::sqrt(e / 48000.0) / (0.5 / std::sqrt(2.0)));
    std::printf("    the guitar on the clock: its steepest step %.4f (the sine's own %.4f), its level %+.3f dB\n", steepest, 0.5 * 2 * M_PI * 1000.0 / 48000.0, level);
#ifndef __SANITIZE_THREAD__
    assert(steepest < 0.5 * 2 * M_PI * 1000.0 / 48000.0 * 1.02 && std::fabs(level) < 0.05);
#endif
    // the input chosen beside the clock in Settings shares its crystal: read as it is, not held
    r.ok("settings set input=card-in");
    r.ok("transport play --from 0");
    bool held = false;
    for (const auto &dv : r.svc.model().transport.devices) held = held || dv.role == "input";
    r.ok("transport stop");
    assert(!held);
    pass("an input on another device (R-DEV-1/2/4): held to the clock in real time — the monitored tone continuous and at its level, the input published with its latency and drift; the input chosen beside the clock read as it is");
}

static void test_the_wavetable_plays_with_no_code_of_its_own()
{
    // R-INST-7: the DSP library's "wavetable" is an instrument here as the registry describes it — dropped as one line,
    // its parameters addresses, a sound given as its sample a table (`set <dv>.sample=`), the render its sound
    Run r;
    const std::string dir = scratch(), cycle = dir + "/square-cycle.wav";
    std::vector<float> sq(2048);
    for (int i = 0; i < 2048; ++i) sq[(size_t)i] = i < 1024 ? 0.8f : -0.8f;
    r.fake.written[cycle] = {sq, sq};
    r.ok("project new " + freshSong("wavetable") + " --bpm 120");
    r.ok("clip add --instrument wavetable --at 0 --length 4");
    r.ok("notes add pt_1 \"A4@0:2\"");
    const DeviceModel *d = nullptr;
    for (const auto &st : r.svc.model().strips)
        for (const auto &dv : st.devices)
            if (dv.type == "wavetable") d = &dv;
    assert(d && d->instrument && d->takesSample && d->label == "Wavetable");
    assert(r.ok("get dv_1.osc1.table") == "basic\n");
    auto render = [&](const std::string &name) {
        const std::string out = dir + "/" + name + ".wav";
        r.ok("render --out " + out + " --to 4 --bits 32f");
        return r.fake.written[out][0];
    };
    auto peakOf = [](const std::vector<float> &v) { float p = 0; for (float x : v) p = std::max(p, std::fabs(x)); return p; };
    const auto basic = render("wt-basic");
    assert(peakOf(basic) > 0.05f);
    r.ok("set dv_1.sample=" + cycle + " dv_1.osc1.table=sample");
    const auto fromSound = render("wt-sound");
    double apart = 0;
    for (size_t i = 0; i < basic.size() && i < fromSound.size(); ++i) apart = std::max(apart, (double)std::fabs(basic[i] - fromSound[i]));
    assert(peakOf(fromSound) > 0.05f && apart > 0.05);
    assert(contains(r.text(), "osc1.table=sample"));
    pass("the wavetable (R-INST-7): the DSP library's instrument, here as its registry says — one line to drop, its parameters addresses, a sound given as its table, its render");
}

static void test_every_instrument_drops_as_one_line_and_sounds()
{
    // R-INST-7…11, law 1: every instrument the DSP library's registry describes is one here with no code of its own —
    // dropped as ONE line, a note in its pattern (a kit's first named key), a render that sounds; one that takes a
    // sound given one first
    const std::string dir = scratch(), cycle = dir + "/any-sound.wav";
    std::vector<float> tone(24000);
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = (float)(0.6 * std::sin(2 * M_PI * 220.0 * (double)i / 48000.0));
    int played = 0;
    for (const auto &t : arstro::DeviceRegistry::types())
    {
        if (t.kind != arstro::DeviceKind::Instrument) continue;
        Run r;
        r.fake.written[cycle] = {tone, tone};
        r.ok("project new " + freshSong("every-" + t.name) + " --bpm 120");
        r.ok("clip add --instrument " + t.name + " --at 0 --length 4");
        if (t.takesDefinition) // an instrument's definition: an .sfz naming the sound, beside it
        {
            const std::string sfz = dir + "/any-instrument.sfz";
            { std::ofstream(sfz) << "<region> sample=any-sound.wav lokey=0 hikey=127 pitch_keycenter=60\n"; }
            r.ok("set dv_1.sample=" + sfz);
        }
        else if (t.takesSample) r.ok("set dv_1.sample=" + cycle);
        const int key = t.noteNames.empty() ? 60 : t.noteNames.front().first;
        r.ok("notes add pt_1 \"" + std::to_string(key) + "@0:1\"");
        const std::string out = dir + "/every-" + t.name + ".wav";
        r.ok("render --out " + out + " --to 4 --bits 32f");
        float peak = 0;
        for (float x : r.fake.written[out][0]) peak = std::max(peak, std::fabs(x));
        std::printf("    %-10s %s: peak %.3f\n", t.name.c_str(), t.label.c_str(), peak);
        assert(peak > 0.01f);
        ++played;
    }
    assert(played >= 6); // the five of before N33, the wavetable, and each one since
    pass("every instrument (R-INST-7…11, law 1): each the DSP library's registry describes drops as one line and sounds, with no code here");
}

static void test_a_multisample_reads_its_sfz()
{
    // R-INST-9: the DSP library's "multisample" given an .sfz — its text read here, the sounds it names decoded beside
    // it: a soft layer (220 Hz) and a loud one taking turns (330 Hz, then 440 Hz); a sound it names that is missing
    // said and its region silent; an opcode not read said; a file that is no instrument refused; with none, the audit
    Run r;
    const std::string dir = scratch();
    auto tone = [](double hz) {
        std::vector<float> x(48000);
        for (size_t i = 0; i < x.size(); ++i) x[i] = (float)(0.5 * std::sin(2 * M_PI * hz * (double)i / 48000.0));
        return x;
    };
    for (const auto &kv : std::vector<std::pair<std::string, double>>{{"soft.wav", 220}, {"loud1.wav", 330}, {"loud2.wav", 440}})
        r.fake.written[dir + "/kit/" + kv.first] = {tone(kv.second), tone(kv.second)};
    std::filesystem::create_directories(dir + "/kit");
    const std::string sfz = dir + "/kit/piano.sfz";
    {
        std::ofstream(sfz) << "// two layers, the loud one in turn\n<global> pitch_keycenter=57 lokey=57 hikey=57 ampeg_release=0.01\n"
                              "<region> sample=soft.wav hivel=63\n<group> lovel=64 seq_length=2\n"
                              "<region> sample=loud1.wav seq_position=1\n<region> sample=loud2.wav seq_position=2 amp_velcurve_1=0.3\n"
                              "<region> sample=missing.wav key=70\n";
    }
    r.ok("project new " + freshSong("multisample") + " --bpm 120");
    r.ok("clip add --instrument multisample --at 0 --length 4");
    assert(contains(r.ok("audit"), "has no instrument — give it an .sfz"));
    assert(contains(r.no("set dv_1.sample=" + dir + "/nothing-here.sfz"), "cannot read"));
    r.ok("set dv_1.sample=" + sfz);
    r.ok("notes add pt_1 \"A3@0:0.5:40 A3@1:0.5:110 A3@2:0.5:110\""); // soft, loud (1st turn), loud (2nd turn)
    r.events.clear();
    const std::string out = dir + "/multisample.wav";
    r.ok("render --out " + out + " --to 3 --bits 32f");
    bool missingSaid = false, opcodeSaid = false;
    for (const auto &e : r.events)
    {
        missingSaid = missingSaid || contains(e, "cannot read missing.wav (named by");
        opcodeSaid = opcodeSaid || contains(e, "amp_velcurve_1");
    }
    assert(missingSaid && opcodeSaid);
    const auto &L = r.fake.written[out][0];
    auto at = [&](int beat, double hz) { // the first 0.2 s of each beat (0.5 s a beat at 120)
        double re = 0, im = 0;
        const int from = beat * 24000 + 960, n = 9600; // 0.2 s: whole cycles of 220, 330 and 440 Hz
        for (int i = 0; i < n; ++i)
        {
            const double a = 2 * M_PI * hz * (from + i) / 48000.0;
            re += L[(size_t)(from + i)] * std::cos(a);
            im += L[(size_t)(from + i)] * std::sin(a);
        }
        return 2.0 * std::hypot(re, im) / n;
    };
    std::printf("    beat 0 (velocity 40): 220 Hz %.3f; beat 1: 330 Hz %.3f; beat 2: 440 Hz %.3f\n", at(0, 220), at(1, 330), at(2, 440));
    assert(at(0, 220) > 0.01 && at(0, 330) < 1e-4 && at(0, 440) < 1e-4);
    assert(at(1, 330) > 0.05 && at(1, 220) < 1e-4 && at(1, 440) < 1e-4);
    assert(at(2, 440) > 0.05 && at(2, 330) < 1e-4);
    pass("a multi-sample (R-INST-9): its .sfz read here, its sounds decoded beside it — velocity layers, round robin; a missing sound and an opcode not read said; a file that is no instrument refused; none, said by the audit");
}

static void test_every_effect_drops_as_one_line_and_passes_sound()
{
    // R-FX-6…17, law 1: every effect the DSP library's registry describes is one here with no code of its own — added
    // to a synth's strip as ONE line, a render finite and sounding (one that needs a key may be silent without it)
    const std::string dir = scratch();
    int added = 0;
    for (const auto &t : arstro::DeviceRegistry::types())
    {
        if (t.kind != arstro::DeviceKind::Effect) continue;
        Run r;
        r.ok("project new " + freshSong("fx-" + t.name) + " --bpm 120");
        r.ok("clip add --instrument synth --at 0 --length 4");
        r.ok("notes add pt_1 \"C4@0:2 G4@2:2\"");
        r.ok("device add ch_2 --type " + t.name);
        const std::string out = dir + "/fx-" + t.name + ".wav";
        r.ok("render --out " + out + " --to 4 --bits 32f");
        float peak = 0;
        bool finite = true;
        for (const auto &ch : r.fake.written[out])
            for (float x : ch) { finite = finite && std::isfinite(x); peak = std::max(peak, std::fabs(x)); }
        std::printf("    %-11s %s: peak %.3f\n", t.name.c_str(), t.label.c_str(), peak);
        assert(finite && (peak > 0.001f || t.takesKey));
        ++added;
    }
    assert(added >= 12); // the eight before N34, the multiband, the gate, the transient shaper, the de-esser
    pass("every effect (R-FX-6…17, law 1): each the DSP library's registry describes drops onto a strip as one line and passes sound, with no code here");
}

static void test_freeze_and_bounce()
{
    // R-PERF-3: a strip's own sound frozen beside the song and played instead — the song sounds as it did (to float
    // precision), its fader, pan and send live, its devices idle (an edit there unheard until unfrozen, said by audit);
    // unfreeze, undone; refusals (fed, not frozen, frozen twice); bounce — the same sound as a clip on a new strip
    Run r;
    const std::string song = freshSong("freeze"), dir = scratch();
    r.ok("project new " + song + " --bpm 120");
    r.ok("clip add --instrument synth --at 0 --length 4");
    r.ok("notes add pt_1 \"C4@0:2 E4@1:2 G4@2:2\"");
    const std::string ch = r.clip("ac_1")->track;
    r.ok("clip add --instrument synth --at 0 --length 4"); // another strip's clip: no warning of it
    r.ok("device add " + ch + " --type chorus");
    r.ok("device add " + ch + " --type limiter"); // its lookahead: the file is trimmed to start on time
    r.ok("set " + ch + ".gain=-6 " + ch + ".pan=0.3");
    const std::string verb = firstLine(r.ok("strip add --kind bus --name Verb"));
    r.ok("device add " + verb + " --type reverb");
    r.ok("send add " + ch + " --to " + verb + " --gain -9");
    assert(contains(r.no("strip freeze " + verb), "is fed by"));
    assert(contains(r.no("strip freeze ch_99"), "no strip"));
    assert(contains(r.no("strip unfreeze " + ch), "is not frozen"));
    auto render = [&](const std::string &name) {
        const std::string out = dir + "/" + name + ".wav";
        r.events.clear();
        r.ok("render --out " + out + " --to 6 --bits 32f");
        for (const auto &e : r.events) assert(!contains(e, "cannot be read"));
        return r.fake.written[out];
    };
    auto apart = [](const std::vector<std::vector<float>> &a, const std::vector<std::vector<float>> &b) {
        float worst = a.size() == b.size() && a[0].size() == b[0].size() ? 0.0f : 1e9f;
        for (size_t c = 0; worst < 1e9f && c < a.size(); ++c)
            for (size_t i = 0; i < a[c].size(); ++i) worst = std::max(worst, std::fabs(a[c][i] - b[c][i]));
        return worst;
    };
    const auto before = render("fz-before");
    float peak = 0;
    for (float v : before[0]) peak = std::max(peak, std::fabs(v));
    assert(peak > 0.05f);
    const std::string said = r.ok("strip freeze " + ch);
    const std::string file = "Frozen/Basic Synth " + ch + ".wav";
    assert(said == ch + " frozen into " + file + "\n" && r.strip(ch)->frozen == file && contains(r.text(), "frozen=\"" + file + "\""));
    assert(r.fake.written.count(dir + "/" + file) && r.fake.written[dir + "/" + file][0].size() > 96000); // the song and its tail
    const auto frozen = render("fz-frozen");
    std::printf("    frozen against its own work: %.3g apart\n", apart(before, frozen));
    assert(apart(before, frozen) < 1e-6f);                       // the song sounds as it did; fader, pan, send live
    assert(contains(r.no("strip freeze " + ch), "frozen already"));
    // its devices idle: an edit there is kept, not heard — and said
    r.ok("set dv_1.volume=-30");
    assert(apart(render("fz-edited"), frozen) == 0.0f && contains(r.ok("audit"), "frozen: " + ch));
    r.ok("set " + ch + ".gain=-12");                              // its fader is live
    assert(apart(render("fz-fader"), frozen) > 0.01f);
    r.ok("set " + ch + ".gain=-6");
    // unfrozen: its own work again — the edit heard; undone: frozen again
    r.ok("strip unfreeze " + ch);
    assert(r.strip(ch)->frozen.empty() && apart(render("fz-unfrozen"), frozen) > 0.01f);
    const auto unfrozen = render("fz-unfrozen2");
    r.ok("undo");
    assert(r.strip(ch)->frozen == file && apart(render("fz-undone"), frozen) == 0.0f);
    // frozen again: the file rewritten and heard anew (never the decode of the one it replaced)
    r.ok("strip unfreeze " + ch);
    r.ok("strip freeze " + ch);
    assert(apart(render("fz-refrozen"), unfrozen) < 1e-6f);
    r.ok("strip unfreeze " + ch);
    // bounce: its own sound as a clip at beat 0 on a NEW strip; the strip itself untouched
    const size_t strips = r.svc.model().strips.size();
    const std::string out = r.ok("strip bounce " + ch);
    assert(contains(out, "made: strip=") && r.svc.model().strips.size() == strips + 1 && r.strip(ch)->frozen.empty());
    const ClipModel *bc = r.clip(firstLine(out));
    assert(bc && bc->src == "Bounces/Basic Synth 1.wav" && bc->at == 0.0 && bc->track != ch);
    assert(r.fake.written.count(dir + "/Bounces/Basic Synth 1.wav"));
    // a frozen file that cannot be read: the song opens, the strip is silent, and the render says why
    r.ok("strip freeze " + ch);
    r.ok("project save");
    std::string text;
    {
        std::ifstream in(song);
        std::stringstream ss;
        ss << in.rdbuf();
        text = ss.str();
    }
    const std::string key = "frozen=\"" + file + "\"";
    assert(contains(text, key));
    text.replace(text.find(key), key.size(), "frozen=\"Frozen/missing.wav\"");
    { std::ofstream(song) << text; }
    r.ok("project open " + song);
    r.events.clear();
    r.ok("render --out " + dir + "/fz-missing.wav --to 6 --bits 32f");
    bool warned = false;
    for (const auto &e : r.events) warned = warned || contains(e, "strip " + ch + " is frozen, but its file Frozen/missing.wav cannot be read");
    assert(warned);
    pass("freeze and bounce (R-PERF-3): a strip's own sound frozen beside the song and played instead — the song as it was (float precision), its fader, pan and send live, its devices idle (an edit unheard, said); unfreeze, undo and a refreeze heard anew; refusals; bounce onto a new strip, the strip untouched");
}

static void test_scale_lock_places_and_moves_notes_on_the_scale()
{
    // R-ROLL-11: a pattern locked to a scale — placed notes on its nearest pitch (a tie down: a major chord on any degree
    // is that degree's own triad), moved notes on to its next pitch the way they went; a move in time keeps a pitch
    Run r;
    r.ok("project new " + freshSong("scale") + " --bpm 120");
    r.ok("clip add --instrument synth --at 0 --length 4");          // ch_2 dv_1 ac_1 pt_1
    r.ok("note add pt_1 --pitch C#4 --at 3");                       // written before the lock: it stays
    assert(r.ok("pattern scale pt_1 C major") == "C major: C D E F G A B\n");
    assert(r.ok("pattern scale pt_1 Bb dorian") == "A# dorian: A# C C# D# F G G#\n");
    r.ok("pattern scale pt_1 C major");
    const PatternModel *pt = patternIn(r, "pt_1");
    assert(pt && pt->scale == "C major" && pt->scaleMask == 0xAB5);
    assert(contains(r.text(), "scale=\"C major\""));
    r.ok("note add pt_1 --pitch 61 --at 0");                        // C#4 → C4 (a tie goes down)
    r.ok("note add pt_1 --pitch 66 --at 0.5");                      // F#4 → F4
    assert(contains(r.ok("note add pt_1 --chord A --octave 4 --at 1"), "A: A4 C5 E5")); // A C# E → A minor, in the key
    r.ok("notes add pt_1 \"D#4@2 G#4@2.5\"");                     // → D4, G4
    std::string t = notesText(r, "pt_1");
    for (const char *want : {"C4@0:", "F4@0.5:", "A4@1:", "C5@1:", "E5@1:", "D4@2:", "G4@2.5:", "C#4@3:"}) assert(contains(t, want));
    assert(!contains(t, "C#5@1") && !contains(t, "D#4") && !contains(t, "G#4"));
    // moved: up from E goes to F, up from F to G (not F#); down from C to B
    r.ok("note move pt_1 --pitch E5 --at 1 --to-pitch F5");
    r.ok("notes move pt_1 \"F4@0.5\" --semi 1");                  // F4 → G4
    r.ok("notes move pt_1 \"C4@0\" --semi -1");                   // C4 → B3
    r.ok("notes move pt_1 \"C#4@3\" --by 0.5");                   // in time only: an off-scale note keeps its pitch
    t = notesText(r, "pt_1");
    for (const char *want : {"F5@1:", "G4@0.5:", "B3@0:", "C#4@3.5:"}) assert(contains(t, want));
    // two moved onto one pitch: refused whole (F#4 and F4 both up a step land on G4)
    r.ok("notes add pt_1 F4@3.75");
    r.ok("pattern scale pt_1 off");
    r.ok("notes add pt_1 F#4@3.75");
    r.ok("pattern scale pt_1 C major");
    assert(contains(r.no("notes move pt_1 \"F4@3.75 F#4@3.75\" --semi 1"), "with another moved there"));
    // refusals name the way; off unlocks; undo brings the lock back
    assert(contains(r.no("pattern scale pt_1 H major"), "`H` is not a key"));
    assert(contains(r.no("pattern scale pt_1 C bebop"), "`bebop` is not a scale") );
    assert(contains(r.no("pattern scale pt_1 C"), "a key and a name"));
    assert(r.ok("pattern scale pt_1 off") == "pt_1: no scale\n" && patternIn(r, "pt_1")->scaleMask == 0);
    r.ok("note add pt_1 --pitch 61 --at 3.25");                     // unlocked: C#4 stays C#4
    assert(contains(notesText(r, "pt_1"), "C#4@3.25:"));
    r.ok("undo");
    r.ok("undo");
    assert(patternIn(r, "pt_1")->scale == "C major");
    pass("scale lock (R-ROLL-11): placed notes on the nearest pitch, a tie down (a stamped A major is A minor in C); moved notes on to the next the way they went; a move in time keeps a pitch; two landing on one refused; key and scale refused by name; off; undone");
}

static void test_note_chance_is_rolled_per_pass_and_the_render_stays_pure()
{
    // R-ROLL-11: a note's chance rolled per clip and per pass by a hash — the same bytes twice, about its odds heard,
    // two clips of one pattern apart, the song's MIDI file what the render plays; set by a token, a selection, one note
    Run r;
    r.ok("project new " + freshSong("chance") + " --bpm 120");
    r.ok("clip add --instrument synth --at 0 --length 32");         // ch_2 dv_1 ac_1 pt_1: a 4-beat pattern, 8 passes
    sineSynth(r, "dv_1");
    r.ok("set dv_1.amp.release=5");
    r.ok("notes add pt_1 \"A4@0:0.25:100:0.5 A4@1:0.25:100:0.5 A4@2:0.25:100:0.5 A4@3:0.25:100:0.5\"");
    const PatternModel *pt = patternIn(r, "pt_1");
    assert(pt && pt->notes.size() == 4 && pt->notes[0].chance == 0.5);
    assert(contains(r.text(), "vel=100 chance=0.5") && contains(r.ok("pattern print pt_1"), "A4@0:0.25:100:0.5"));
    auto heard = [&](const std::string &name, double from, int beats) {
        const std::string out = scratch() + "/" + name + ".wav";
        r.ok("render --out " + out + " --from " + std::to_string(from) + " --to " + std::to_string(from + beats) + " --bits 32f");
        const auto &L = r.fake.written[out][0];
        std::string got;
        for (int b = 0; b < beats; ++b)
        {
            double e = 0;
            for (size_t i = (size_t)b * 24000 + 480; i < (size_t)b * 24000 + 4800 && i < L.size(); ++i) e += (double)L[i] * L[i];
            got += e > 1.0 ? 'x' : '.';
        }
        return got;
    };
    const std::string once = heard("chance1", 0, 32), twice = heard("chance2", 0, 32);
    const auto hits = std::count(once.begin(), once.end(), 'x');
    std::printf("    heard %s (%ld of 32)\n", once.c_str(), (long)hits);
    assert(once == twice && hits >= 8 && hits <= 24);               // pure; about half
    // a second clip of the same pattern: its own draws
    r.ok("clip add --strip ch_2 --pattern pt_1 --at 32 --length 32"); // ac_2
    const std::string second = heard("chance3", 32, 32);
    assert(second != once);
    // the song's MIDI file: exactly the notes the render played
    const std::string mid = scratch() + "/chance.mid";
    r.ok("project export-midi " + mid);
    const std::string got = firstLine(r.ok("pattern import " + mid));
    const auto played = std::count(once.begin(), once.end(), 'x') + std::count(second.begin(), second.end(), 'x');
    assert(patternIn(r, got) && (long)patternIn(r, got)->notes.size() == (long)played);
    // set: a selection, one note; 1 always, 0 never; refusals
    r.ok("notes set pt_1 \"A4@0 A4@1 A4@2 A4@3\" --chance 1");
    assert(heard("chance4", 0, 8) == "xxxxxxxx");
    r.ok("note move pt_1 --pitch A4 --at 0 --chance 0");
    assert(heard("chance5", 0, 8) == ".xxx.xxx" && patternIn(r, "pt_1")->notes[0].chance == 0.0);
    assert(contains(r.no("notes set pt_1 \"A4@0\" --chance 2"), "--chance is the odds"));
    assert(contains(r.no("notes set pt_1 \"A4@0.5\" --vel 90"), "has no note there"));
    assert(contains(r.no("notes set pt_1 \"A4@0\""), "needs --vel, --chance and/or --length"));
    assert(contains(r.no("notes add pt_1 \"A4@0:0.25:100:1.5\""), "the chance `1.5` must be 0 to 1"));
    pass("note chance (R-ROLL-11): rolled per clip and pass by a hash — the same render twice, near its odds, a second clip apart, the MIDI file what the render plays; set by a token, notes set, note move; 1 always, 0 never; refusals");
}

static void test_the_note_tools_humanise_groove_legato_arpeggiate()
{
    // R-ROLL-11: each tool ONE edit, on every note or the ones named; seeded where it draws, so the same line on the same
    // notes writes the same notes
    Run r;
    r.ok("project new " + freshSong("tools") + " --bpm 120");
    r.ok("clip add --instrument synth --at 0 --length 4");          // pt_1
    r.ok("notes add pt_1 \"C4@0 D4@0.5 E4@1 F4@1.5 G4@2 A4@2.5 B4@3 C5@3.5\" --vel 80");
    const std::string straight = notesText(r, "pt_1");
    // humanise: seeded, bounded, reproducible; --notes keeps the rest
    const std::string said = r.ok("pattern humanise pt_1 --timing 0.05 --vel 10 --seed 7");
    assert(said == "8 notes humanised (seed 7)\n");
    const std::string loose = notesText(r, "pt_1");
    assert(loose != straight);
    for (const auto &n : patternIn(r, "pt_1")->notes)
    {
        const double grid = std::round(n.at * 2) / 2;
        assert(std::fabs(n.at - grid) <= 0.05 + 1e-9 && n.vel >= 70 && n.vel <= 90);
    }
    r.ok("undo");
    assert(notesText(r, "pt_1") == straight);
    r.ok("pattern humanise pt_1 --timing 0.05 --vel 10 --seed 7");
    assert(notesText(r, "pt_1") == loose);                         // the same line, the same notes
    r.ok("undo");
    const std::string byDefault = r.ok("pattern humanise pt_1");     // a seed of the notes, printed — and it reproduces
    const std::string seed = byDefault.substr(byDefault.find("seed ") + 5, byDefault.find(')') - byDefault.find("seed ") - 5);
    const std::string loose2 = notesText(r, "pt_1");
    r.ok("undo");
    r.ok("pattern humanise pt_1 --seed " + seed);
    assert(notesText(r, "pt_1") == loose2);
    r.ok("undo");
    r.ok("pattern humanise pt_1 --timing 0.05 --seed 3 --notes \"C4@0 D4@0.5\"");
    const std::string some = notesText(r, "pt_1");
    assert(contains(some, "E4@1:") && contains(some, "C5@3.5:") && contains(some, ":80  #")); // the rest untouched
    r.ok("undo");
    // a groove: the hip-hop template on sixteenths — each step's offset and accent; half strength half way
    r.ok("pattern clear pt_1");
    r.ok("notes add pt_1 \"C4@0 C4@0.25 C4@0.5 C4@0.75\" --vel 100");
    r.ok("pattern quantize pt_1 --groove hip-hop");
    {
        const auto &n = patternIn(r, "pt_1")->notes;
        assert(n.size() == 4);
        const double at[4] = {0, 0.25 + 0.2 * 0.25, 0.5 + 0.05 * 0.25, 0.75 + 0.25 * 0.25};
        const int vel[4] = {100, 70, 85, 65};
        for (int i = 0; i < 4; ++i) assert(std::fabs(n[i].at - toTick(at[i])) < 1e-9 && n[i].vel == vel[i]);
    }
    r.ok("undo");
    r.ok("pattern quantize pt_1 --groove hip-hop --strength 0.5");
    assert(std::fabs(patternIn(r, "pt_1")->notes[1].at - toTick(0.25 + 0.1 * 0.25)) < 1e-9 && patternIn(r, "pt_1")->notes[1].vel == 85);
    r.ok("undo");
    assert(contains(r.no("pattern quantize pt_1 --groove bebop"), "no groove `bebop` (mpc-54"));
    assert(contains(r.no("pattern quantize pt_1 --groove shuffle --swing 0.3"), "carries its own grid and swing"));
    assert(contains(r.no("pattern quantize pt_1 --strength 2"), "--strength"));
    // legato: each to the next start, a chord together, the last to the pattern's end; an overlap shortened
    r.ok("pattern clear pt_1");
    r.ok("notes add pt_1 \"C4@0:0.25 E4@1:3 G4@1:0.5 B4@3:0.25\"");
    assert(r.ok("pattern legato pt_1") == "4 notes legato\n");
    assert(notesText(r, "pt_1") == "C4@0:1:100  # 60\nE4@1:2:100  # 64\nG4@1:2:100  # 67\nB4@3:1:100  # 71\n");
    r.ok("undo");
    r.ok("pattern legato pt_1 --notes \"C4@0 B4@3\"");               // the next start among them: C4 runs to beat 3
    assert(contains(notesText(r, "pt_1"), "C4@0:3:100") && contains(notesText(r, "pt_1"), "E4@1:3:100"));
    r.ok("undo");
    // arpeggiate: a held C major triad, eighths
    r.ok("pattern clear pt_1");
    r.ok("notes add pt_1 \"C4@0:2:90 E4@0:2:80 G4@0:2:70\"");
    assert(r.ok("pattern arpeggiate pt_1 --rate 0.5") == "3 notes → 4 steps (up)\n");
    assert(notesText(r, "pt_1") == "C4@0:0.5:90  # 60\nE4@0.5:0.5:80  # 64\nG4@1:0.5:70  # 67\nC4@1.5:0.5:90  # 60\n");
    r.ok("undo");
    r.ok("pattern arpeggiate pt_1 --rate 0.25 --mode updown --octaves 2 --gate 0.5");
    std::string seq;
    for (const auto &n : patternIn(r, "pt_1")->notes) seq += std::to_string(n.pitch) + " ";
    assert(seq == "60 64 67 72 76 79 76 72 ");                        // up two octaves and back, the turns once
    assert(patternIn(r, "pt_1")->notes[0].length == 0.125);
    r.ok("undo");
    r.ok("pattern arpeggiate pt_1 --mode down --rate 1");
    assert(notesText(r, "pt_1") == "G4@0:1:70  # 67\nE4@1:1:80  # 64\n");
    r.ok("undo");
    r.ok("pattern arpeggiate pt_1 --mode random --seed 5 --rate 0.25");
    const std::string rnd = notesText(r, "pt_1");
    r.ok("undo");
    r.ok("pattern arpeggiate pt_1 --mode random --seed 5 --rate 0.25");
    assert(notesText(r, "pt_1") == rnd);
    r.ok("undo");
    // after a rest the arpeggio starts again from the first
    r.ok("notes add pt_1 \"C4@3:1 E4@3:1\"");
    r.ok("pattern arpeggiate pt_1 --rate 0.5");
    assert(contains(notesText(r, "pt_1"), "C4@3:0.5") && contains(notesText(r, "pt_1"), "E4@3.5:0.5"));
    r.ok("undo");
    assert(contains(r.no("pattern arpeggiate pt_1 --mode sideways"), "--mode is up, down, updown or random"));
    assert(contains(r.no("pattern arpeggiate pt_1 --octaves 5"), "--octaves is 1 to 4"));
    assert(contains(r.no("pattern arpeggiate pt_1 --rate 8"), "--rate is a step"));
    assert(contains(r.no("pattern arpeggiate pt_1 --notes \"D4@0\""), "has no note there"));
    r.ok("pattern clear pt_1");
    assert(contains(r.no("pattern arpeggiate pt_1"), "no notes to arpeggiate"));
    pass("the note tools (R-ROLL-11): humanise seeded, bounded, reproduced by its printed seed, --notes alone; a groove's offsets and accents, at half strength half way; legato to the next start, a chord together, the last to the end; arpeggiate up, updown over octaves (turns once), down, seeded random, again after a rest; refusals");
}

static void test_loop_and_seek()
{
    Run r;
    r.ok("project new " + freshSong("loop") + " --bpm 120");
    r.ok("strip add --kind instrument --instrument drums");
    r.ok("clip add --strip ch_2 --length 8");
    r.ok("note add pt_1 --pitch 36 --at 0");                    // one kick at beat 0 of a 4-beat pattern
    r.ok("transport loop 0 1");                                 // loop the first beat: a kick every beat
    r.ok("transport play");
    waitFrames(r, 4 * 24000 + 1000);
    r.ok("transport stop");
    const auto &v = r.fake.capture->sink;
    for (int k = 1; k <= 3; ++k)
    {
        const size_t at = (size_t)k * 24000;
        assert(peakOf(v, 2 * at, 2 * (at + 480)) > 10.0f * peakOf(v, 2 * (at - 2400), 2 * at)); // the kick again, each beat
    }
    r.ok("transport loop off");
    assert(r.svc.model().transport.loopTo == 0.0);
    r.ok("transport seek 6");
    assert(r.svc.model().transport.position == 6.0);
    assert(contains(r.no("transport loop 3 2"), "to > from"));
    pass("loop repeats its span while playing; seek moves the transport (R-TIME-4)");
}

static void test_stop_returns_to_the_cue_or_stays()
{
    Run r;
    r.ok("project new " + freshSong("cue") + " --bpm 120");
    r.ok("strip add --kind instrument --instrument drums");
    r.ok("clip add --strip ch_2 --length 16");
    const auto &t = r.svc.model().transport;
    // the time bar put at beat 6: the cue
    r.ok("transport seek 6");
    assert(t.cue == 6.0 && t.position == 6.0);
    r.ok("transport play");
    waitFrames(r, r.fake.capture->frames.load() + 48000); // the helper waits for a total: wait from now                                       // a second: two beats on
    r.ok("transport stop --return");                            // back where it was put (R-TIME-7)
    assert(!t.playing && t.position == 6.0 && t.cue == 6.0);
    // stop where it is (R-TIME-8): the next play starts there — but that place is temporary: the cue stays where the
    // time bar was put, so the next stop with the return goes back THERE, not to where the plain stop left it
    r.ok("transport play");
    waitFrames(r, r.fake.capture->frames.load() + 48000); // the helper waits for a total: wait from now
    r.ok("transport stop");
    const double here = t.position;
    assert(here > 7.0 && t.cue == 6.0);
    r.ok("transport play");
    // (the position is what is HEARD — the device's latency behind the start for a moment: a tenth of a beat's room)
    assert(r.svc.model().transport.cue == 6.0 && r.svc.model().transport.position > here - 0.1);
    waitFrames(r, r.fake.capture->frames.load() + 24000);
    r.ok("transport stop --return");
    assert(r.svc.model().transport.position == 6.0 && r.svc.model().transport.cue == 6.0);
    // a play that says where it begins puts the time bar there
    r.ok("transport play --from 3");
    assert(r.svc.model().transport.cue == 3.0);
    r.ok("transport stop --return");
    assert(r.svc.model().transport.position == 3.0);
    // a seek while playing is the time bar put there too
    r.ok("transport play");
    r.ok("transport seek 2");
    waitFrames(r, r.fake.capture->frames.load() + 24000);
    r.ok("transport stop --return");
    assert(r.svc.model().transport.position == 2.0);
    // the roll's play begins at its clip: stopped with the return, the clip's start
    r.ok("clip add --strip ch_2 --pattern pt_1 --at 20 --length 4");
    r.ok("transport play --clip ac_2");
    waitFrames(r, r.fake.capture->frames.load() + 24000);
    r.ok("transport stop --return");
    assert(r.svc.model().transport.position == 20.0 && r.svc.model().transport.cue == 20.0);
    // a new song starts its cue at 0
    r.ok("project new " + freshSong("cue2") + " --bpm 120");
    assert(r.svc.model().transport.cue == 0.0);
    pass("The cue (R-TIME-7/8, amended): `transport stop --return` puts the time bar back where it was put — a seek, --from, a roll's clip; a plain stop stays where heard, the next play starts there, and the cue stays put");
}

static void test_a_clip_plays_looped_and_gives_the_songs_loop_back()
{
    Run r;
    r.ok("project new " + freshSong("cliploop") + " --bpm 120");
    r.ok("strip add --kind instrument --instrument drums");
    r.ok("clip add --strip ch_2 --at 8 --length 2");             // ac_1: beats 8 … 10
    r.ok("note add pt_1 --pitch 36 --at 0");                    // a kick on the clip's first beat
    r.ok("transport loop 0 4");                                 // the song's own loop
    r.ok("transport play --clip ac_1");
    const auto &t = r.svc.model().transport;
    assert(t.loopClip == "ac_1" && t.loopFrom == 8.0 && t.loopTo == 10.0);
    waitFrames(r, 3 * 48000 + 1000);                            // three loops of one second
    const auto &v = r.fake.capture->sink;
    for (int k = 0; k < 3; ++k)
    {
        const size_t at = (size_t)k * 48000;
        assert(peakOf(v, 2 * at, 2 * (at + 480)) > 10.0f * peakOf(v, 2 * (at + 12000), 2 * (at + 24000))); // the kick, each loop
    }
    r.ok("clip move ac_1 --at 12");                             // edited while it loops: the loop follows
    assert(r.svc.model().transport.loopFrom == 12.0 && r.svc.model().transport.loopTo == 14.0);
    r.ok("transport stop");
    assert(r.svc.model().transport.loopClip.empty());
    assert(r.svc.model().transport.loopFrom == 0.0 && r.svc.model().transport.loopTo == 4.0); // the song's, back
    r.ok("transport play --clip ac_1");
    r.ok("transport play");                                     // the song's play: the song's loop
    assert(r.svc.model().transport.loopClip.empty() && r.svc.model().transport.loopTo == 4.0);
    r.ok("transport play --clip ac_1");
    r.ok("transport loop 2 3");                                 // a loop set by hand wins, and stays
    r.ok("transport stop");
    assert(r.svc.model().transport.loopFrom == 2.0 && r.svc.model().transport.loopTo == 3.0);
    r.ok("transport play --clip ac_1");
    r.ok("clip delete ac_1");                                   // gone: the song's loop again
    assert(r.svc.model().transport.loopClip.empty() && r.svc.model().transport.loopTo == 3.0);
    r.ok("transport stop");
    assert(contains(r.no("transport play --clip ac_9"), "no clip 'ac_9'"));
    pass("`transport play --clip` loops the clip's span, follows its edits, and gives the song's loop back (R-ROLL-6)");
}

namespace
{
    const BindingModel *binding(const Run &r, const std::string &address)
    {
        for (const auto &b : r.svc.model().bindings)
            if (b.address == address) return &b;
        return nullptr;
    }
}

static void test_a_bound_value_is_published_live()
{
    // R-MIX-16: bindings[].live is the value at the HEARD position — the engine's own while playing,
    // `eval`'s at the transport when stopped — and the audio thread copies it without allocating
    Run r;
    r.ok("project new " + freshSong("livebind") + " --bpm 120");
    r.ok("clip add --src tone.wav --at 0 --length 16");                // ch_2
    r.ok("auto create ch_2.gain");                                     // au_1: 0 dB at beats 0 and 16
    r.ok("auto point move au_1 --at 16 --value -32");                  // a ramp: −2 dB a beat
    r.ok("set project.masterGain=\"=ch_2.gain / 2\"");                 // a link reads the evaluated value: −1 dB a beat
    r.ok("strip add --kind instrument --instrument synth");            // ch_3, dv_1: a device parameter formula too
    r.ok("clip add --strip ch_3 --length 16");
    r.ok("note add pt_1 --pitch 48 --at 0 --length 16");
    r.ok("set dv_1.filter.cutoff=\"=1000 + 500 * sin(beat)\"");
    // stopped: eval's answer at the transport
    r.ok("transport seek 6");
    assert(binding(r, "ch_2.gain")->live == -12.0 && binding(r, "project.masterGain")->live == -6.0);
    assert(contains(r.ok("eval ch_2.gain --at 6"), "ch_2.gain = " + canonicalNumber(binding(r, "ch_2.gain")->live) + " dB"));
    r.ok("transport seek 0");
    assert(binding(r, "ch_2.gain")->live == 0.0);
    // playing: it follows the curve the engine evaluates, block by block (the fake device has no latency)
    r.fake.capture->sink.reserve(2 * 2000000); // the fake device keeps what it is given: within capacity it allocates nothing
    r.ok("transport play");
    waitFrames(r, 24000);
    gThreadAllocs = 0;
    gCountThreads = true;                                              // from here the audio thread must not allocate
    double last = 1.0;
    int seen = 0;
    for (long long beat = 2; beat <= 12; beat += 2)
    {
        waitFrames(r, beat * 24000);
        const double pos = r.svc.model().transport.position, g = binding(r, "ch_2.gain")->live, m = binding(r, "project.masterGain")->live;
        const double cut = binding(r, "dv_1.filter.cutoff")->live;
        if (std::fabs(g + 2.0 * pos) > 0.1 || std::fabs(m - g / 2.0) > 1e-9)
            std::printf("    beat %.4f: gain live %.4f (curve %.4f), master %.4f\n", pos, g, -2.0 * pos, m);
        assert(std::fabs(g + 2.0 * pos) < 0.1 && std::fabs(m - g / 2.0) < 1e-9);    // within a block of the curve; the link follows it
        assert(std::fabs(cut - (1000.0 + 500.0 * std::sin(pos))) < 25.0);
        assert(g < last);                                                          // it MOVES
        last = g;
        ++seen;
    }
    gCountThreads = false;
    const long allocs = gThreadAllocs.load();
    r.ok("transport stop");
    if (allocs) std::printf("    %ld allocations on the audio thread\n", allocs);
    assert(seen == 6 && allocs == 0);
    // stopped again: eval's answer where it stopped
    const double pos = r.svc.model().transport.position;
    if (!(pos >= 11.9 && std::fabs(binding(r, "ch_2.gain")->live + 2.0 * pos) < 1e-3)) std::printf("    stopped at %.6f: live %.6f\n", pos, binding(r, "ch_2.gain")->live);
    assert(pos >= 11.9 && std::fabs(binding(r, "ch_2.gain")->live + 2.0 * pos) < 1e-3);
    // a structural edit while playing swaps the engine: the values come from the new one, never the old one's order
    r.ok("transport play --from 4");
    r.ok("set ch_3.pan=\"=-0.5\"");                                       // a new binding: a new engine, a new bind order
    waitFrames(r, r.fake.capture->frames.load() + 24000);
    assert(binding(r, "ch_3.pan")->live == -0.5 && std::fabs(binding(r, "ch_2.gain")->live + 2.0 * r.svc.model().transport.position) < 0.1);
    r.ok("transport stop");
    // not in the stable dump: it moves with the transport
    assert(!contains(r.ok("state print --json --stable"), "\"live\"") && contains(r.ok("state print --json"), "\"live\""));
    pass("bindings[].live: eval's value at the transport when stopped; while playing the engine's, block by block, following a ramp, a link and a device formula; copied without allocating on the audio thread (R-MIX-16, R-PLAY-2)");
}

static void test_a_refusal_is_an_event_and_lands_in_lastError()
{
    Run r;
    r.events.clear();
    assert(r.no("strip add") == "no song is open (project new <path.slp> or project open <path.slp>)");
    assert(r.events.size() == 1 && contains(r.events[0], "[evt] command.rejected line=\"strip add\""));
    assert(r.svc.model().lastError == "no song is open (project new <path.slp> or project open <path.slp>)");
    r.no("flurb");
    assert(contains(r.svc.model().lastError, "unknown command: flurb"));
    pass("a refusal is a command.rejected event AND lastError — inspectable by a front end that was not listening");
}


// ── R-SVC-8: composition at an agent's size ─────────────────────────────────────────────────

static void test_the_notation_tables()
{
    int m = -1;
    std::string err;
    assert(parsePitchName("C4", m, err) && m == 60);
    assert(parsePitchName("F#3", m, err) && m == 54);
    assert(parsePitchName("Bb2", m, err) && m == 46);
    assert(parsePitchName("c-1", m, err) && m == 0);
    assert(parsePitchName("G9", m, err) && m == 127);
    assert(parsePitchName("eb4", m, err) && m == 63);
    assert(!parsePitchName("G#9", m, err) && contains(err, "128, outside MIDI's 0–127"));
    assert(!parsePitchName("H4", m, err) && !parsePitchName("C", m, err) && !parsePitchName("kick", m, err));
    assert(pitchName(61) == "C#4" && pitchName(0) == "C-1" && pitchName(127) == "G9");
    std::vector<int> ch;
    assert(chordPitches("Cm7", 4, 0, ch, err) && ch == std::vector<int>({60, 63, 67, 70}));
    assert(chordPitches("Cm7", 4, 1, ch, err) && ch == std::vector<int>({63, 67, 70, 72}));     // first inversion
    assert(chordPitches("F#", 3, 0, ch, err) && ch == std::vector<int>({54, 58, 61}));
    assert(chordPitches("Bbmaj7", 3, 0, ch, err) && ch == std::vector<int>({58, 62, 65, 69}));
    assert(chordPitches("Gsus4", 4, 0, ch, err) && ch == std::vector<int>({67, 72, 74}));
    assert(!chordPitches("Cxyz", 4, 0, ch, err) && contains(err, "unknown chord `Cxyz`") && contains(err, "m7") && contains(err, "sus4"));
    assert(!chordPitches("C", 4, 3, ch, err) && contains(err, "--inversion for C is 0 to 2"));
    assert(!chordPitches("C9", 9, 0, ch, err) && contains(err, "outside 0–127"));
    NoteToken t;
    assert(parseNoteToken("C4@0:0.5:90", t, err) && t.pitch == "C4" && t.at == 0 && t.length == 0.5 && t.vel == 90 && t.hasLength && t.hasVel);
    assert(parseNoteToken("E4@2::70", t, err) && t.at == 2 && !t.hasLength && t.vel == 70);
    assert(parseNoteToken("kick@1.5", t, err) && t.pitch == "kick" && !t.hasLength && !t.hasVel);
    assert(!parseNoteToken("C4", t, err) && contains(err, "<pitch>@<beat>"));
    assert(!parseNoteToken("C4@x", t, err) && contains(err, "the beat `x`"));
    assert(!parseNoteToken("C4@0:0:90", t, err) && contains(err, "the length `0`"));
    assert(!parseNoteToken("C4@0:1:128", t, err) && contains(err, "the velocity `128`"));
    std::vector<int> st;
    assert(parseSteps("x...X..-| x", st, err) && st == std::vector<int>({1, 0, 0, 0, 2, 0, 0, 0, 1}));
    assert(!parseSteps("x..o", st, err) && contains(err, "`o`"));
    // a `#` that starts a word ends the line; one inside a word is a sharp
    Command c = parseCommand("note add pt_1 --pitch F#3 --at 0   # the root", err);
    assert(err.empty() && c.flag("pitch") == "F#3" && c.args.size() == 1);
    c = parseCommand("strip add --kind bus --name \"Bus #1\" # a bus", err);
    assert(err.empty() && c.flag("name") == "Bus #1");
    pass("the notation: C4 = 60, F#3, Bb2, c-1; chords with inversions; <pitch>@<beat>:<len>:<vel>; step rows; a trailing # comment (R-SVC-8)");
}

static void test_comment_lines_print_nothing()
{
    Run r;
    r.ok("project new " + freshSong("comments"));
    assert(r.ok("strip add --kind bus --name Verb   # a return") == "ch_2\n");
    // the audit's bug: a comment line kept the LAST output, and the CLI printed the id again
    assert(r.ok("# a comment").empty());
    assert(r.ok("").empty());
    assert(r.ok("   ").empty());
    r.ok("set ch_2.gain=-3 # quieter");
    assert(r.ok("get ch_2.gain") == "-3.0\n");
    pass("a comment or a blank line prints nothing (it reprinted the line before it); `#` may end a line (R-SVC-8)");
}

static void test_everything_made_is_said()
{
    Run r;
    r.ok("project new " + freshSong("made"));
    // every node a command made is in its output AND announced by its own event
    auto check = [&](const std::string &line, const std::string &expected) {
        r.events.clear();
        const std::string out = r.ok(line);
        if (out != expected) std::printf("    `%s` printed `%s`\n", line.c_str(), out.c_str());
        assert(out == expected);
        std::vector<std::string> ids = {firstLine(out)};
        const auto made = out.find("made:");
        if (made != std::string::npos)
        {
            std::istringstream in(out.substr(made + 5));
            std::string kv;
            while (in >> kv)
            {
                std::string list = kv.substr(kv.find('=') + 1), id;
                std::istringstream items(list);
                while (std::getline(items, id, ',')) ids.push_back(id);
            }
        }
        for (const auto &id : ids)
        {
            bool said = false;
            for (const auto &e : r.events) said |= contains(e, "project.changed") && contains(e, ".added node=" + id);
            if (!said) std::printf("    %s was made by `%s` but not announced\n", id.c_str(), line.c_str());
            assert(said);
        }
    };
    check("strip add --kind instrument --instrument synth --name Bass", "ch_2\nmade: device=dv_1\n");
    check("clip add --strip ch_2 --at 0 --length 8", "ac_1\nmade: pattern=pt_1 lane=ln_1\n");
    check("clip add --instrument drums --at 0", "ac_2\nmade: strip=ch_3 device=dv_2 pattern=pt_2 lane=ln_2\n");
    check("clip add --src kick.wav --at 0", "ac_3\nmade: strip=ch_4 lane=ln_3\n");
    check("clip add --src kick.wav --at 4", "ac_4\n");                       // the same file: its strip, its lane — nothing else made
    check("clip add --strip ch_2 --pattern pt_1 --at 8", "ac_5\n");
    check("clip duplicate ac_5 --count 3", "ac_6\nmade: clip=ac_7,ac_8\n");
    check("pattern duplicate pt_1", "pt_3\n");
    check("lane add Spare", "ln_4\n");
    check("auto create dv_1.filter.cutoff", "au_1\n");
    check("send add ch_2 --to ch_1", "sd_1\n");
    check("device add ch_2 --type reverb", "dv_3\n");
    pass("everything made is said: the id first (as before), then `made: strip=… device=… pattern=… lane=…`; each one announced by its own event (R-SVC-8)");
}

static void test_notes_in_bulk_and_by_name()
{
    Run r;
    r.ok("project new " + freshSong("bulk"));
    r.ok("clip add --instrument synth --at 0 --length 8");                     // ch_2, pt_1
    r.ok("clip add --instrument drums --at 0 --length 8");                     // ch_3, pt_2
    const std::string before = r.text();
    const int depth = r.svc.model().undoDepth;
    // ONE line, one edit: names, numbers, defaults, per-note length and velocity
    assert(r.ok("notes add pt_1 \"C2@0:0.5 Eb2@1:0.5:90 48@2 G2@3::70\" Bb1@3.5 --length 0.25 --vel 110") == "5 notes\n");
    {
        const auto &n = r.svc.model().patterns[0].notes;
        assert(n.size() == 5);
        assert(n[0].pitch == 36 && n[0].at == 0 && n[0].length == 0.5 && n[0].vel == 110);
        assert(n[1].pitch == 39 && n[1].vel == 90);
        assert(n[2].pitch == 48 && n[2].length == 0.25 && n[2].vel == 110);
        assert(n[3].pitch == 43 && n[3].length == 0.25 && n[3].vel == 70);
        assert(n[4].pitch == 34 && n[4].at == 3.5);
    }
    assert(r.svc.model().undoDepth == depth + 1);                               // ONE undo step…
    assert(r.ok("undo") == "notes add pt_1 C2@0:0.5 Eb2@1:0.5:90 48@2 G2@3::70 Bb1@3.5\n");   // …that takes the whole line back
    assert(r.text() == before);
    r.ok("redo");
    // a kit's pads by name — case, spaces and hyphens ignored
    assert(r.ok("notes add pt_2 \"Kick@0 snare@1 closed-hat@0.5 OPENHAT@1.5 low_tom@3\"") == "5 notes\n");
    std::set<int> pads;
    for (const auto &x : r.svc.model().patterns[1].notes) pads.insert(x.pitch);
    assert(pads == std::set<int>({36, 38, 42, 46, 41}));
    // a bad token is refused BY NAME, and nothing of the line lands (no event either)
    const std::string mid = r.text();
    r.events.clear();
    assert(r.no("notes add pt_1 \"C4@0 D4@1 X9@2\"") ==
           "note 3 `X9@2`: `X9` is not a pitch (a number 0–127, or a name: C4 = 60, F#3, Bb2); pt_1 plays through no kit, so a pad name does not apply — nothing was added");
    assert(contains(r.no("notes add pt_1 C4@0 D4@x"), "note 2 `D4@x`: `D4@x`: the beat `x` is not a number"));
    assert(contains(r.no("notes add pt_2 \"kick@0 kik@1\""), "note 2 `kik@1`: `kik` is not a pitch (60, C4, F#3) nor a pad of drums (pads: kick rim snare clap low-tom closed-hat mid-tom open-hat high-tom crash ride cowbell)"));
    assert(contains(r.no("notes add pt_1 kick@0"), "`kick` is not a pitch"));     // a synth has no pads
    assert(contains(r.no("notes add pt_9 C4@0"), "no pattern `pt_9`"));
    assert(r.text() == mid);
    for (const auto &e : r.events) assert(!contains(e, "project.changed"));
    // a pitch by name everywhere: note add / delete / move
    r.ok("note add pt_1 --pitch F#4 --at 2 --length 1");
    r.ok("note move pt_1 --pitch F#4 --at 2 --to-pitch G4");
    assert(contains(r.no("note delete pt_1 --pitch F#4 --at 2"), "no note 66"));
    r.ok("note delete pt_1 --pitch G4 --at 2");
    r.ok("note add pt_2 --pitch cowbell --at 2");
    assert(contains(r.no("note add pt_2 --pitch bell --at 2"), "pads: kick"));
    assert(contains(r.no("note add pt_1 --pitch 128 --at 0"), "0–127"));
    // chords: one line, one edit
    const size_t had = r.svc.model().patterns[0].notes.size();
    // written past the 4-beat end: the pattern grows to the bar that holds it, in the same edit (R-ROLL-9 — this
    // used to be a warning, R-SVC-8 amended)
    const double was = r.svc.model().patterns[0].length;
    assert(was == 4.0);
    assert(r.ok("note add pt_1 --chord Cm7 --at 4 --length 4 --octave 3 --vel 80").rfind("Cm7: C3 D#3 G3 A#3\npt_1 grew to 8 beats to hold its notes", 0) == 0);
    assert(r.svc.model().patterns[0].notes.size() == had + 4 && r.svc.model().patterns[0].length == 8.0);
    assert(r.ok("undo") == "note add pt_1\n" && r.svc.model().patterns[0].notes.size() == had && r.svc.model().patterns[0].length == 4.0);
    r.ok("note add pt_1 --chord F --at 0 --length 4 --inversion 2");            // F4 A4 C5, its two lowest raised: C5 F5 A5
    std::set<int> f;
    for (const auto &x : r.svc.model().patterns[0].notes)
        if (x.length == 4.0) f.insert(x.pitch);
    assert(f == std::set<int>({72, 77, 81}));
    assert(contains(r.no("note add pt_1 --chord Cm13 --at 0"), "unknown chord `Cm13`: after the root comes one of (major), m, 5, dim, aug"));
    assert(contains(r.no("note add pt_1 --chord C --pitch 60 --at 0"), "without --pitch"));
    assert(contains(r.no("note add pt_1 --pitch 60 --at 0 --inversion 1"), "shape a --chord"));
    pass("notes add: many notes by name in ONE edit (one undo), a bad token refused by name with nothing landed; pads by name; pitch names in note add/delete/move; chords with --octave/--inversion (R-SVC-8)");
}

static void test_steps_and_pattern_edits()
{
    Run r;
    r.ok("project new " + freshSong("steps"));
    r.ok("clip add --instrument drums --at 0 --length 16");                     // ch_2, pt_1
    assert(r.ok("pattern steps pt_1 --pitch kick \"x...x...x...x...\"") == "4 notes\n");
    assert(r.ok("pattern steps pt_1 --pitch closed-hat \"..x. ..x. ..x. ..X.\" --vel 70") == "4 notes\n");
    auto at = [&](int pitch) {
        std::vector<std::pair<double, int>> v;
        for (const auto &n : r.svc.model().patterns[0].notes)
            if (n.pitch == pitch) v.emplace_back(n.at, n.vel);
        return v;
    };
    assert(at(36) == (std::vector<std::pair<double, int>>{{0, 100}, {1, 100}, {2, 100}, {3, 100}}));
    assert(at(42) == (std::vector<std::pair<double, int>>{{0.5, 70}, {1.5, 70}, {2.5, 70}, {3.5, 127}})); // X = an accent
    // the row REPLACES that pitch in its span — and only that pitch, only that span
    r.ok("pattern steps pt_1 --pitch kick \"x.x.\" --step 0.5");                 // beats 0 … 2
    assert(at(36) == (std::vector<std::pair<double, int>>{{0, 100}, {1, 100}, {2, 100}, {3, 100}}));
    r.ok("pattern steps pt_1 --pitch 36 \"X..x\" --step 0.5 --at 2");            // beats 2 … 4
    assert(at(36) == (std::vector<std::pair<double, int>>{{0, 100}, {1, 100}, {2, 127}, {3.5, 100}}) && at(42).size() == 4);
    for (const auto &n : r.svc.model().patterns[0].notes)
        if (n.pitch == 36 && n.at == 3.5) assert(n.length == 0.5);               // a note lasts a step
    assert(contains(r.no("pattern steps pt_1 --pitch kick \"x..y\""), "`y`"));
    assert(contains(r.no("pattern steps pt_1 \"x...\""), "needs --pitch"));
    assert(contains(r.no("pattern steps pt_1 --pitch kick \"x...\" --step 0"), "--step must be more than 0"));
    // duplicate: a NEW pattern, the same notes
    assert(r.ok("pattern duplicate pt_1") == "pt_2\n");
    assert(r.svc.model().patterns[1].name == "Drum Machine copy" && r.svc.model().patterns[1].notes.size() == r.svc.model().patterns[0].notes.size());
    // transpose, refused whole when a note would leave 0–127
    r.ok("pattern transpose pt_2 --semi 12");
    assert(r.svc.model().patterns[1].notes[0].pitch == 48);
    r.ok("note add pt_2 --pitch G9 --at 1");
    const std::string before = r.text();
    assert(contains(r.no("pattern transpose pt_2 --semi 1"), "G9 (127) at beat 1 would be 128, outside 0–127 — nothing moved"));
    assert(r.text() == before);
    // clear one pitch, then all
    assert(r.ok("pattern clear pt_2 --pitch G9") == "1 note removed\n");
    assert(r.ok("pattern clear pt_2") == "8 notes removed\n" && r.svc.model().patterns[1].notes.empty());
    // the audit: an empty pattern, an unused one, notes past the end — a note written past the end grows the
    // pattern (R-ROLL-9), so the end is set back over it by hand, as a song saved before could have it
    assert(contains(r.ok("note add pt_1 --pitch kick --at 6"), "pt_1 grew to 8 beats"));
    r.ok("set pt_1.length=4");
    const std::string a = r.ok("audit");
    assert(contains(a, "empty pattern: pt_2 (Drum Machine copy) has no notes"));
    assert(contains(a, "unused pattern: pt_2 (Drum Machine copy) — no clip plays it"));
    assert(contains(a, "past its end: pt_1 (Drum Machine) has 1 note(s) starting at or after beat 4.0"));
    // delete: refused while a clip plays it, naming the clips
    r.ok("clip duplicate ac_1");
    assert(r.no("pattern delete pt_1") == "pt_1 is played by ac_1, ac_2 — delete those clips (or `clip unique` them) first");
    r.ok("pattern delete pt_2");
    assert(r.svc.model().patterns.size() == 1);
    assert(contains(r.no("pattern delete pt_2"), "no pattern `pt_2`"));
    pass("pattern steps: x/X/. rows that replace their pitch's span; pattern duplicate/transpose/clear/delete (refused while played); the audit names empty, unused and past-the-end patterns (R-SVC-8)");
}

static void test_a_clip_joins_its_strips_lane()
{
    Run r;
    r.ok("project new " + freshSong("lanes"));
    r.ok("clip add --instrument synth --at 0 --length 4");                    // ch_2 on a new lane ln_1
    r.ok("clip add --strip ch_2 --at 4 --length 4");                          // no --lane: ch_2's lane, not a new one
    r.ok("clip add --src kick.wav --at 0");                                   // a new strip: a new lane ln_2
    r.ok("clip add --src kick.wav --at 2");                                   // its strip has clips: ln_2
    assert(r.clip("ac_2")->lane == "ln_1" && r.clip("ac_3")->lane == "ln_2" && r.clip("ac_4")->lane == "ln_2");
    assert(r.svc.model().lanes.size() == 2);
    assert(r.ok("clip add --strip ch_2 --at 8 --lane new") == "ac_5\nmade: pattern=pt_3 lane=ln_3\n");   // asked for
    r.ok("clip add --strip ch_2 --at 12");                                    // the newest clip's lane now
    assert(r.clip("ac_6")->lane == "ln_3");
    assert(contains(r.no("clip add --strip ch_2 --lane ln_9"), "no lane `ln_9` (or `new`)"));
    // N copies, end to end
    assert(firstLine(r.ok("clip duplicate ac_1 --count 3")) == "ac_7");
    assert(r.clip("ac_7")->at == 4.0 && r.clip("ac_8")->at == 8.0 && r.clip("ac_9")->at == 12.0 && r.clip("ac_9")->pattern == "pt_1");
    r.ok("clip duplicate ac_1 --count 2 --at 32");
    assert(r.clip("ac_10")->at == 32.0 && r.clip("ac_11")->at == 36.0);
    assert(r.ok("undo") == "clip duplicate ac_1\n" && !r.clip("ac_10") && !r.clip("ac_11"));                 // one edit
    assert(contains(r.no("clip duplicate ac_1 --count 0"), "--count must be a whole number from 1 to 256"));
    pass("a clip with no --lane joins its strip's lane (a new lane only for a strip with none; --lane new asks); clip duplicate --count N, end to end, one edit (R-SVC-8)");
}

static void test_an_instruments_groups_are_titled()
{
    // R-UI-12: the model carries the registry's group titles (REQ-device-10), so a face can draw sections
    Run r;
    r.ok("project new " + freshSong("groups"));
    r.ok("clip add --instrument synth --at 0");                              // dv_1
    r.ok("device add ch_2 --type reverb");                                   // dv_2, an effect
    const auto &d = r.svc.model().strips[0].devices;
    auto title = [&](const DeviceModel &dm, const std::string &prefix) {
        for (const auto &g : dm.groups)
            if (g.prefix == prefix) return g.label;
        return std::string("-");
    };
    assert(d[0].type == "synth" && title(d[0], "osc1") == "Oscillator 1" && title(d[0], "fenv") == "Filter Envelope" && title(d[0], "") == "Mix & Output");
    assert(d[1].type == "reverb" && d[1].groups.empty());
    assert(contains(r.ok("state print --json"), "\"label\": \"Filter Envelope\""));
    pass("an instrument's parameter groups arrive titled by the registry (Oscillator 1 … Mix & Output); an effect's have none (R-UI-12)");
}

// R-CLIP-9: a copy can go to another lane in the same line — the move's rule: onto an instrument's track a MIDI copy
// plays through that instrument (the pattern still shared), an audio copy is refused there; a plain lane keeps it
static void test_a_clip_copied_onto_another_lane()
{
    Run r;
    r.ok("project new " + freshSong("copylane"));
    r.ok("clip add --instrument drums --at 0 --length 4");                   // ac_1 on ch_2's track ln_1
    r.ok("clip add --instrument synth --at 0 --length 4");                   // ac_2 on ch_3's track ln_2
    r.ok("lane add Extra");                                                   // ln_3: a plain lane
    r.ok("clip add --src kick.wav --at 0 --lane ln_3");                     // ac_3: audio
    // a MIDI clip copied onto the synth's track: it plays through the synth, the drum pattern still linked
    const std::string out = r.ok("clip duplicate ac_1 --at 8 --lane ln_2");
    assert(firstLine(out) == "ac_4" && contains(out, "it plays through ch_3"));
    assert(r.clip("ac_4")->lane == "ln_2" && r.clip("ac_4")->track == "ch_3" && r.clip("ac_4")->pattern == r.clip("ac_1")->pattern);
    assert(r.clip("ac_4")->at == 8.0 && r.clip("ac_1")->lane == "ln_1" && r.clip("ac_1")->linked == 2);
    // onto a plain lane: organisation only — it keeps its instrument
    r.ok("clip duplicate ac_1 --at 4 --lane ln_3");
    assert(r.clip("ac_5")->lane == "ln_3" && r.clip("ac_5")->track == "ch_2");
    // audio onto an instrument's track: refused, as a move is; a lane that is not there, named
    assert(contains(r.no("clip duplicate ac_3 --at 8 --lane ln_1"), "ac_3 is audio — ln_1 is the track of ch_2"));
    assert(contains(r.no("clip duplicate ac_3 --lane ln_9"), "no lane `ln_9`"));
    r.ok("clip duplicate ac_3 --at 8 --lane ln_3");
    assert(r.clip("ac_6")->lane == "ln_3" && r.clip("ac_6")->src == r.clip("ac_3")->src);
    // ONE edit: one undo takes the copy back
    assert(r.ok("undo") == "clip duplicate ac_3\n" && !r.clip("ac_6"));
    pass("clip duplicate --lane (R-CLIP-9): a MIDI copy onto another track plays through it, linked; a plain lane keeps its instrument; audio onto a track refused; one edit");
}

// R-MIDI-1: Standard MIDI Files — a pattern exported and imported back is the same notes to the tick; a file made
// elsewhere (running status, note-on velocity 0, 96 ticks a beat, named tracks; SMPTE time through its tempo) reads
// as its beats; a dropped .mid is one line — a clip on a track or a new instrument; the song exports as it plays
static void test_midi_files()
{
    Run r;
    const std::string dir = scratch();
    r.ok("project new " + freshSong("midi") + " --bpm 120");
    r.ok("clip add --instrument synth --at 0 --length 4");
    const std::string pt = r.svc.model().clips[0].pattern;
    r.ok("notes add " + pt + " \"C4@0:1:90 E4@0.5:0.25:70 G4@1.3333:0.6667:100 C5@3.75:0.25:127\"");
    auto notesOf = [&](const std::string &id) {
        for (const auto &x : r.svc.model().patterns)
            if (x.id == id) return x.notes;
        return std::vector<NoteModel>{};
    };
    // exported, imported: the same notes to the tick
    const std::string mid = dir + "/pattern.mid";
    r.ok("pattern export " + pt + " " + mid);
    {
        std::ifstream f(mid, std::ios::binary);
        char head[4] = {};
        f.read(head, 4);
        assert(std::string(head, 4) == "MThd");
    }
    const std::string back = firstLine(r.ok("pattern import " + mid));
    const auto a = notesOf(pt), b = notesOf(back);
    assert(a.size() == 4 && a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        assert(a[i].pitch == b[i].pitch && a[i].at == b[i].at && a[i].length == b[i].length && a[i].vel == b[i].vel);
    }
    // a file made elsewhere: format 1, 96 ticks a beat, two named tracks, running status, a note-on of velocity 0 as its off
    auto vlq = [](std::string &o, unsigned v) {
        std::string t(1, (char)(v & 0x7F));
        while (v >>= 7) t.insert(t.begin(), (char)((v & 0x7F) | 0x80));
        o += t;
    };
    auto be = [](std::string &o, unsigned long v, int n) { for (int i = n - 1; i >= 0; --i) o += (char)((v >> (8 * i)) & 0xFF); };
    auto track = [&](const std::string &name, const std::string &events) {
        std::string body;
        vlq(body, 0); body += "\xFF\x03"; vlq(body, (unsigned)name.size()); body += name;
        body += events;
        vlq(body, 0); body += std::string("\xFF\x2F\x00", 3);
        std::string chunk = "MTrk";
        be(chunk, body.size(), 4);
        return chunk + body;
    };
    auto header = [&](int format, int tracks, unsigned division) {
        std::string h = "MThd";
        be(h, 6, 4); be(h, (unsigned long)format, 2); be(h, (unsigned long)tracks, 2); be(h, division, 2);
        return h;
    };
    std::string bass, lead;
    // bass: C2 at beat 0 for a beat (an 0x80 off), then F2 at beat 1 — running status, its off a note-on of velocity 0
    vlq(bass, 0); bass += "\x90\x24\x64"; vlq(bass, 96); bass += std::string("\x80\x24\x00", 3);
    vlq(bass, 0); bass += "\x90\x29\x50"; vlq(bass, 48); bass += std::string("\x29\x00", 2);
    vlq(bass, 96); bass += "\x90\x2B\x40"; vlq(bass, 48); bass += std::string("\x80\x2B\x00", 3); // G2 later: F2 must END at its velocity-0 on
    vlq(lead, 192); lead += "\x91\x48\x7F"; vlq(lead, 24); lead += std::string("\x48\x00", 2);
    const std::string made = dir + "/made.mid";
    std::ofstream(made, std::ios::binary) << header(1, 2, 96) + track("Bass", bass) + track("Lead", lead);
    const std::string out = r.ok("pattern import " + made);
    const std::string pBass = firstLine(out);
    const std::string pLead = out.substr(out.find("pattern=") + 8, out.find('\n', out.find("pattern=")) - out.find("pattern=") - 8);
    {
        const auto nb = notesOf(pBass), nl = notesOf(pLead);
        assert(nb.size() == 3 && nb[0].pitch == 36 && nb[0].at == 0.0 && nb[0].length == 1.0 && nb[0].vel == 100);
        assert(nb[2].pitch == 43 && nb[2].at == 2.5 && nb[2].length == 0.5);
        assert(nb[1].pitch == 41 && nb[1].at == 1.0 && nb[1].length == 0.5 && nb[1].vel == 80);
        assert(nl.size() == 1 && nl[0].pitch == 72 && nl[0].at == 2.0 && nl[0].length == 0.25);
        for (const auto &x : r.svc.model().patterns)
            if (x.id == pBass) assert(x.name == "Bass" && x.length == 4.0);
    }
    // SMPTE time: 25 frames of 40 ticks a second, 120 bpm — a note at tick 1000 (one second) is beat 2
    std::string smpte;
    vlq(smpte, 0); smpte += std::string("\xFF\x51\x03\x07\xA1\x20", 6); // 500 000 µs a quarter
    vlq(smpte, 1000); smpte += "\x90\x3C\x64"; vlq(smpte, 250); smpte += std::string("\x80\x3C\x00", 3);
    const std::string sm = dir + "/smpte.mid";
    std::ofstream(sm, std::ios::binary) << header(0, 1, 0xE728) + track("Time", smpte); // −25 fps, 40 ticks a frame
    const auto ns = notesOf(firstLine(r.ok("pattern import " + sm)));
    assert(ns.size() == 1 && ns[0].at == 2.0 && ns[0].length == 0.5);
    // dropped on a track: ONE line — the tracks merged into one pattern, a clip of it there
    const std::string ln = r.svc.model().clips[0].lane;
    r.events.clear();
    const std::string dropped = r.ok("pattern import " + made + " --lane " + ln + " --at 8");
    const std::string merged = firstLine(dropped);
    assert(notesOf(merged).size() == 4 && contains(dropped, "made: clip="));
    bool onTrack = false;
    for (const auto &cl : r.svc.model().clips) onTrack |= cl.pattern == merged && cl.lane == ln && cl.at == 8.0 && cl.track == r.svc.model().clips[0].track;
    assert(onTrack);
    assert(contains(r.ok("undo"), "pattern import"));                    // one edit: the pattern and the clip together
    // on empty space: a new instrument of it
    const std::string drums = r.ok("pattern import " + made + " --track 1 --instrument drums --at 4");
    assert(contains(drums, " clip=") && contains(drums, "made: strip="));
    // the song, as it plays: a 4-beat pattern on an 8-beat clip at beat 8 is its notes twice, from beat 8
    r.ok("clip add --strip " + r.svc.model().clips[0].track + " --pattern " + pt + " --at 8 --length 8");
    const std::string song = dir + "/song.mid";
    r.ok("project export-midi " + song);
    const auto sn = notesOf(firstLine(r.ok("pattern import " + song + " --track 1")));
    int fromEight = 0;
    for (const auto &x : sn) fromEight += x.at >= 8.0 && x.at < 16.0;
    assert(fromEight == 8 && sn.size() == 12);                          // 4 at beat 0 + 8 from the long clip
    // refused, naming why
    std::ofstream(dir + "/not.mid") << "hello";
    assert(contains(r.no("pattern import " + dir + "/not.mid"), "not a MIDI file"));
    assert(contains(r.no("pattern import " + made + " --track 3"), "--track is one of the file's 2 tracks"));
    assert(contains(r.no("pattern import " + dir + "/nowhere.mid"), "cannot read"));
    assert(contains(r.no("pattern export pt_99 " + song), "no pattern `pt_99`"));
    pass("MIDI files (R-MIDI-1): a pattern exported and imported back is the same notes to the tick; a file made elsewhere — running status, velocity-0 offs, 96 ticks a beat, named tracks, SMPTE time through its tempo — reads as its beats; a .mid dropped on a track is one line (merged, a clip), on space a new instrument; the song exports as it plays");
}

static void test_instrument_tracks()
{
    // R-LANE-3: a lane may be an instrument's TRACK — what is put on it plays through it
    Run r;
    r.ok("project new " + freshSong("tracks"));
    auto lane = [&](const std::string &id) -> const LaneModel * {
        for (const auto &l : r.svc.model().lanes)
            if (l.id == id) return &l;
        return nullptr;
    };
    // an instrument's first clip makes its track, which shows the strip's name — and follows a rename (D-2)
    assert(r.ok("clip add --instrument synth --at 0 --length 4") == "ac_1\nmade: strip=ch_2 device=dv_1 pattern=pt_1 lane=ln_1\n");
    assert(lane("ln_1")->strip == "ch_2" && !lane("ln_1")->ownName && lane("ln_1")->name == "Basic Synth");
    r.ok("set ch_2.name=Bass");
    assert(lane("ln_1")->name == "Bass");
    r.ok("clip add --instrument synth --at 0 --length 4");                 // ch_3, ln_2
    r.ok("set ch_3.name=Pad");
    assert(lane("ln_2")->strip == "ch_3");
    // put on a track: --strip implied; a pattern placed there plays through it
    assert(r.ok("clip add --lane ln_2 --at 8") == "ac_3\nmade: pattern=pt_3\n");
    assert(r.clip("ac_3")->track == "ch_3" && r.clip("ac_3")->lane == "ln_2");
    r.ok("clip add --pattern pt_1 --lane ln_2 --at 12");
    assert(r.clip("ac_4")->track == "ch_3" && r.clip("ac_4")->pattern == "pt_1");
    // moved onto another track: re-routed in the same command, and said; ONE undo takes both back
    const int depth = r.svc.model().undoDepth;
    assert(r.ok("clip move ac_1 --lane ln_2 --at 4") == "ac_1 plays through ch_3 (Pad)\n");
    assert(r.clip("ac_1")->track == "ch_3" && r.clip("ac_1")->lane == "ln_2" && r.clip("ac_1")->at == 4.0);
    assert(r.svc.model().undoDepth == depth + 1);
    r.ok("undo");
    assert(r.clip("ac_1")->track == "ch_2" && r.clip("ac_1")->lane == "ln_1" && r.clip("ac_1")->at == 0.0);
    r.ok("redo");
    // told to play through another instrument, it goes to that one's track
    assert(r.ok("clip move ac_1 --strip ch_2") == "ac_1 plays through ch_2 (Bass)\n");
    assert(r.clip("ac_1")->lane == "ln_1");
    // audio is refused on a track; a new instrument asks for its own
    assert(contains(r.no("clip add --src kick.wav --lane ln_1"), "ln_1 is the track of ch_2 (Bass), an instrument — an audio clip needs a lane of its own"));
    r.ok("clip add --src kick.wav --at 0");                                 // ch_4, its own plain lane ln_3
    assert(lane("ln_3")->strip.empty() && lane("ln_3")->name == "kick");
    assert(contains(r.no("clip move ac_5 --lane ln_1"), "ac_5 is audio — ln_1 is the track of ch_2 (Bass), an instrument"));
    assert(contains(r.no("clip add --instrument drums --lane ln_1"), "a new instrument gets its own track: give --lane new"));
    assert(contains(r.no("clip add --strip ch_2 --lane ln_2"), "a clip on it plays through ch_3; leave out --strip"));
    assert(contains(r.no("clip move ac_1 --lane ln_2 --strip ch_2"), "give --lane or --strip, not both"));
    // a plain lane is organisation only, as before
    r.ok("lane add Ideas");                                                 // ln_4
    assert(r.ok("clip move ac_1 --lane ln_4").empty() && r.clip("ac_1")->track == "ch_2");
    // a lane made a track, and plain again (keeping the name it showed)
    assert(r.ok("lane add --strip ch_2") == "ln_5\n" && lane("ln_5")->name == "Bass" && lane("ln_5")->strip == "ch_2");
    assert(contains(r.no("set ln_3.strip=ch_2"), "ln_3 holds ac_5, which is audio"));
    assert(contains(r.no("set ln_4.strip=ch_4"), "a track belongs to an instrument"));
    assert(contains(r.no("set ln_4.strip=ch_9"), "no strip `ch_9`"));
    r.ok("set ln_4.strip=ch_2");
    assert(lane("ln_4")->strip == "ch_2" && lane("ln_4")->name == "Ideas" && lane("ln_4")->ownName);
    r.ok("set ln_5.strip=none");
    assert(lane("ln_5")->strip.empty() && lane("ln_5")->name == "Bass" && r.ok("get ln_5.strip") == "none\n");
    // relinked, its tracks go with its clips; deleted, its tracks become plain lanes
    r.ok("clip add --instrument synth --at 0");                              // ch_5, ln_6
    r.ok("strip relink ch_3 --to ch_5");
    assert(lane("ln_2")->strip == "ch_5" && lane("ln_2")->name == "Pad");
    r.ok("strip delete ch_5 --with-clips");
    assert(lane("ln_2")->strip.empty() && lane("ln_2")->name == "Pad" && lane("ln_6")->strip.empty());
    // ls says which lanes are tracks; the file keeps them
    assert(contains(r.ok("ls"), "lane ln_1 \"Bass\" · track of ch_2\n"));
    r.ok("project save");
    {
        std::ifstream f(scratch() + "/tracks.slp");
        std::stringstream ss;
        ss << f.rdbuf();
        assert(contains(ss.str(), "tracks     = on\n") && contains(ss.str(), "#alane id=ln_1 name=\"\" track=ch_2 order=0"));
    }
    pass("instrument tracks: an instrument's lane is its track, named by it; a clip put or moved there plays through it, in one edit and said; audio refused there; lane add --strip, <lane>.strip; relink and delete carry them (R-LANE-3)");
}

static void test_a_song_from_before_tracks_adopts_them()
{
    // R-LANE-3: no `tracks` line — a lane whose clips all play through one instrument becomes its track; one
    // still carrying the name it was made with shows its strip's name; a lane of audio or of two instruments stays plain
    const std::string path = freshSong("before-tracks");
    {
        std::ofstream f(path);
        f << "arstro-project = 1\napp = solaris\nid = prj_old\nname = Old\nbpm = 120.0\n"
             "#amixer id=mx_1 name=Sources order=0\n#amixer id=mx_2 name=Buses order=1\n"
             "#atrack id=ch_1 name=Main kind=bus mixer=mx_2 order=0 gain=0.0 pan=0.0\n"
             "#atrack id=ch_2 name=Bass kind=instrument mixer=mx_1 order=0 gain=0.0 pan=0.0 out=ch_1\n"
             "#atrack id=ch_3 name=Lead kind=instrument mixer=mx_1 order=1 gain=0.0 pan=0.0 out=ch_1\n"
             "#atrack id=ch_4 name=kick kind=audio mixer=mx_1 order=2 gain=0.0 pan=0.0 out=ch_1\n"
             "#arack track=ch_2\n  #aeffect id=dv_1 type=synth\n#arack track=ch_3\n  #aeffect id=dv_2 type=synth\n"
             "#alane id=ln_1 name=\"Basic Synth\" order=0\n#alane id=ln_2 name=Hooks order=1\n#alane id=ln_3 name=Mixed order=2\n"
             "#alane id=ln_4 name=Drums order=3\n#alane id=ln_5 name=Empty order=4\n"
             "#apattern id=pt_1 name=A length=4.0\n"
             "#aclip id=ac_1 track=ch_2 lane=ln_1 pattern=pt_1 at=0.0\n#aclip id=ac_2 track=ch_2 lane=ln_1 pattern=pt_1 at=4.0\n"
             "#aclip id=ac_3 track=ch_3 lane=ln_2 pattern=pt_1 at=0.0\n"
             "#aclip id=ac_4 track=ch_2 lane=ln_3 pattern=pt_1 at=0.0\n#aclip id=ac_5 track=ch_3 lane=ln_3 pattern=pt_1 at=4.0\n"
             "#aclip id=ac_6 track=ch_4 lane=ln_4 src=kick.wav in=0.0 out=0.5 at=0.0\n";
    }
    Run r;
    r.events.clear();
    r.ok("project open " + path);
    auto lane = [&](const std::string &id) -> const LaneModel * {
        for (const auto &l : r.svc.model().lanes)
            if (l.id == id) return &l;
        return nullptr;
    };
    assert(lane("ln_1")->strip == "ch_2" && lane("ln_1")->name == "Bass" && !lane("ln_1")->ownName); // made as "Basic Synth": now its strip's
    assert(lane("ln_2")->strip == "ch_3" && lane("ln_2")->name == "Hooks" && lane("ln_2")->ownName); // a name of its own: kept
    assert(lane("ln_3")->strip.empty() && lane("ln_4")->strip.empty() && lane("ln_5")->strip.empty());
    int said = 0;
    for (const auto &e : r.events) said += contains(e, "became the track of") ? 1 : 0;
    assert(said == 2);
    assert(!r.svc.model().dirty);                                            // opening is not an edit
    pass("a song from before tracks: its one-instrument lanes become tracks on opening, said; a lane still named as it was made shows its strip's name; audio, mixed and empty lanes stay plain (R-LANE-3)");
}

static void test_reading_a_song_back()
{
    Run r;
    const std::string song = freshSong("readback");
    r.ok("project new " + song + " --name \"Read Back\" --bpm 100");
    r.ok("clip add --instrument synth --at 0 --length 8");                    // ch_2, dv_1, pt_1, ln_1
    r.ok("notes add pt_1 \"C4@0:1 E4@1\"");
    r.ok("set dv_1.filter.cutoff=800 dv_1.osc1.wave=sine ch_2.gain=-6");
    r.ok("strip add --kind bus --name Verb");                                 // ch_3
    r.ok("device add ch_3 --type reverb");                                    // dv_2
    r.ok("send add ch_2 --to ch_3 --gain -12");
    r.ok("project save");
    const int undo = r.svc.model().undoDepth;
    const std::string ls = r.ok("ls");
    assert(contains(ls, "song \"Read Back\" · 100.0 bpm · 4/4 · 8 beats\n"));
    assert(contains(ls, "mixer mx_1 \"Sources\"\n  ch_2 \"Basic Synth\" instrument [synth dv_1] → ch_1 · gain -6.0 · sd_1 → ch_3 -12.0 dB\n"));
    // R-MIX-4 amended (C6): a bus made on Buses is placed before Main and feeds it
    assert(contains(ls, "mixer mx_2 \"Buses\"\n  ch_3 \"Verb\" bus [reverb dv_2] → ch_1\n  ch_1 \"Main\" bus → master\n"));
    assert(contains(ls, "lane ln_1 \"Basic Synth\" · track of ch_2\n  ac_1 @0 len 8 pt_1 via ch_2\n")); // R-LANE-3
    assert(contains(ls, "pattern pt_1 \"Basic Synth\" 4 beats · 2 notes · 1 clip\n"));
    const std::string dv = r.ok("show dv_1");
    assert(contains(dv, "dv_1 synth (Basic Synth) on ch_2\n"));
    assert(contains(dv, "  filter.cutoff 800.0 Hz  (default 2400.0)\n") && contains(dv, "  osc1.wave sine  (default saw)\n"));
    assert(!contains(dv, "filter.res"));                                      // a default is not shown
    assert(contains(r.ok("show ch_2"), "  gain -6.0 dB\n") && contains(r.ok("show ac_1"), "  pattern pt_1\n"));
    assert(contains(r.ok("show project"), "  bpm 100.0\n"));
    assert(r.no("show dv_9") == "no node `dv_9` (did you mean: dv_1?)");
    assert(r.ok("pattern print pt_1") == "# pt_1 \"Basic Synth\" · 4 beats · 2 notes · played by ac_1\nC4@0:1:100  # 60\nE4@1:0.25:100  # 64\n");
    const std::string compact = r.ok("state print --json --compact");
    assert(std::count(compact.begin(), compact.end(), '\n') == 1);            // one line
    assert(!contains(compact, "deviceTypes") && !contains(compact, "\"label\"") && !contains(compact, "\"min\"") && !contains(compact, "recents"));
    assert(contains(compact, "\"params\":[\"osc1.wave=sine\",\"filter.cutoff=800.0\"]") && contains(compact, "\"notes\":[\"C4@0:1:100\",\"E4@1:0.25:100\"]"));
    assert(compact.size() * 4 < r.ok("state print --json").size());            // a quarter of the full dump, or less
    // reading is not editing: still saved, no undo step
    assert(!r.svc.model().dirty && r.svc.model().undoDepth == undo);
    assert(r.no("set dv_1.filter.cutoff=10") == "dv_1.filter.cutoff must be between 20.0 and 20000.0 Hz, got 10");
    assert(r.no("set dv_1.osc1.octave=1.5") == "dv_1.osc1.octave takes whole numbers, got 1.5");
    assert(contains(r.no("set dv_1.osc1.wave=7"), "one of: sine, saw, square, triangle"));
    r.ok("set dv_1.osc1.wave=2");                                             // a choice by its index still reads
    assert(r.ok("get dv_1.osc1.wave") == "square\n");
    Run home;
    assert(contains(home.no("ls"), "no song is open"));
    pass("ls / show / pattern print / state print --json --compact read a song back (ids, names, non-default values with units) without editing it; out-of-range values refused (R-SVC-8)");
}

static void test_latency_is_compensated_and_a_render_starts_on_the_beat()
{
    // R-MIX-17. Two sources of the same tone (two files, two strips — R-MIX-2), a limiter on ONE: the other is
    // delayed to meet it, so the mix is their sum to the bit; and a limiter on the master: the render still
    // starts on beat 0 — the same bytes as with no limiter (under its ceiling a limiter is exactly its delay).
    Run r;
    const std::string dir = scratch();
    r.ok("project new " + freshSong("latency") + " --bpm 120");
    r.ok("clip add --src a.wav --at 0");                               // ch_2 → Main
    r.ok("clip add --src b.wav --at 0");                               // ch_3 → Main, the same samples
    r.ok("set ch_2.gain=-12 ch_3.gain=-12");                           // under every ceiling: no gain is reduced
    auto render = [&](const std::string &name, const std::string &more = std::string()) {
        const std::string out = dir + "/" + name + ".wav";
        r.ok("render --out " + out + " --stems ch_2,ch_3 --ports --bits 32f" + more); // undithered: files compared to the bit
        return std::make_tuple(r.fake.written[out], r.fake.written[dir + "/" + name + ".ch_2.wav"],
                               r.fake.written[dir + "/" + name + ".ch_3.wav"], r.fake.written[dir + "/" + name + ".Main.wav"]);
    };
    auto onset = [](const std::vector<float> &x) {
        for (size_t i = 0; i < x.size(); ++i)
            if (std::fabs(x[i]) > 1e-4f) return (long long)i;
        return -1LL;
    };
    const auto plain = render("plain");
    r.ok("device add ch_2 --type limiter");                            // 96 samples of lookahead on ONE twin
    const auto one = render("one");
    const auto &mix = std::get<0>(one)[0], &a = std::get<1>(one)[0], &b = std::get<2>(one)[0];
    assert(a == b && a == std::get<1>(plain)[0]);                       // each stem from beat 0, sample for sample
    bool summed = mix.size() == a.size();
    for (size_t i = 0; summed && i < mix.size(); ++i) summed = mix[i] == a[i] + b[i];
    assert(summed && std::get<0>(one) == std::get<0>(plain));           // they meet at Main: the mix is their sum
    r.ok("device remove dv_1");
    r.ok("device add master --type limiter");                          // the master's lookahead: 96 samples
    const auto master = render("master");
    assert(onset(std::get<0>(master)[0]) == onset(std::get<0>(plain)[0]) && onset(std::get<0>(plain)[0]) < 48);
    assert(std::get<0>(master) == std::get<0>(plain) && std::get<3>(master) == std::get<3>(plain)); // mix and port: the same bytes
    assert(std::get<1>(master) == std::get<1>(plain));
    r.ok("render --out " + dir + "/exact.wav --from 1 --to 3");        // --to is exact, latency or not
    assert(r.fake.written[dir + "/exact.wav"][0].size() == 48000);
    r.ok("render --out " + dir + "/all.wav --stems all");               // every strip a stem
    assert(r.fake.written.count(dir + "/all.ch_1.wav") && r.fake.written.count(dir + "/all.ch_2.wav") && r.fake.written.count(dir + "/all.ch_3.wav"));
    // live: what the device hears is the render, `outputLatency` later — the first 96 frames are the lookahead's
    r.ok("transport play");
    waitFrames(r, 48000);
    r.ok("transport stop");
    const auto &live = r.fake.capture->sink;
    const auto &port = std::get<3>(master);
    bool late = true;
    for (size_t i = 0; i < 96; ++i) late &= live[2 * i] == 0.0f;
    assert(port[0].size() == 28096);                                    // the tone's 24000 frames, a tail block
    for (size_t i = 0; i < port[0].size(); ++i)
    {
        const bool same = live[2 * (i + 96)] == port[0][i] && live[2 * (i + 96) + 1] == port[1][i];
        if (!same && late) std::printf("    frame %zu: live %g offline %g\n", i, live[2 * (i + 96)], port[0][i]);
        late &= same;
    }
    assert(late);
    // a lookahead moved WHILE playing re-plans the compensation (a new engine), never left stale: back on one
    // twin, set to 5 ms (240 samples) as it plays, the twins still meet — what is heard is the 5 ms render, 240 late
    r.ok("device remove dv_1");
    r.ok("device add ch_2 --type limiter");                            // dv_1 again: ids are the document's
    r.fake.capture->sink.clear();
    r.fake.capture->frames = 0;
    r.ok("transport play --from 0");
    r.ok("set dv_1.lookahead=5");
    waitFrames(r, 30000);
    r.ok("transport stop");
    r.ok("render --out " + dir + "/five.wav --ports --bits 32f");
    const auto &five = r.fake.written[dir + "/five.Main.wav"];
    bool met = true;
    // (the tone is periodic in 48 samples, so its END tells the twins apart: un-re-planned, one would stop 144 early)
    for (size_t f = 20000; f < 24400; ++f) met &= live[2 * f] == five[0][f - 240] && live[2 * f + 1] == five[1][f - 240];
    assert(met);
    pass("latency compensated (R-MIX-17): a limiter's twin meets it at Main (the mix is their sum to the bit); a master limiter's render starts on beat 0, byte-identical to none; stems each from beat 0; live is the render, 96 frames later");
}

static void test_delivery_formats()
{
    // R-EXPORT-1: the format by the extension (or --format), the song rendered AT --rate, integer depths dithered onto
    // their grid, lossy ones at --kbps, --normalize to a loudness held under a true-peak ceiling; refusals name the way
    Run r;
    r.ok("project new " + freshSong("export") + " --bpm 120");
    for (int k = 0; k < 4; ++k) r.ok("clip add --src tone.wav --at " + std::to_string(k) + " --length 1"); // 2 s of a 1 kHz sine at 0.5
    const std::string d = scratch() + "/export";
    auto &f = r.fake;
    r.ok("render --out " + d + ".flac --to 4");
    assert(f.formats[d + ".flac"] == "flac" && f.bits[d + ".flac"] == 24 && f.rates[d + ".flac"] == 48000);
    r.ok("render --out " + d + ".mp3 --to 4");
    assert(f.formats[d + ".mp3"] == "mp3" && f.kbps[d + ".mp3"] == 320);
    r.ok("render --out " + d + ".ogg --to 4 --kbps 160");
    assert(f.formats[d + ".ogg"] == "ogg" && f.kbps[d + ".ogg"] == 160);
    r.ok("render --out " + d + ".opus --to 4");
    assert(f.formats[d + ".opus"] == "opus" && f.rates[d + ".opus"] == 48000 && f.kbps[d + ".opus"] == 192);
    r.ok("render --out " + d + ".bin --format ogg --to 4");
    assert(f.formats[d + ".bin"] == "ogg");
    // the song AT 44.1 kHz: 2 s are 88200 frames, and the tone is still 1 kHz (its file decoded at that rate)
    r.ok("render --out " + d + "44.wav --to 4 --rate 44100");
    const auto &w44 = f.written[d + "44.wav"][0];
    assert(f.rates[d + "44.wav"] == 44100 && w44.size() == 88200);
    int ups = 0;
    for (size_t i = 1; i < 44100; ++i) ups += w44[i - 1] < 0.0f && w44[i] >= 0.0f;
    assert(ups >= 998 && ups <= 1001);
    // 16 bits: on the grid, dithered (not every sample the plain rounding), and the same bytes twice
    r.ok("render --out " + d + "16.wav --to 4 --bits 16");
    const auto a16 = f.written[d + "16.wav"];
    r.ok("render --out " + d + "f32.wav --to 4 --bits 32f");
    const auto &raw = f.written[d + "f32.wav"][0];
    bool grid = true;
    int moved = 0;
    for (size_t i = 0; i < a16[0].size(); ++i)
    {
        const double k = (double)a16[0][i] * 32768.0;
        grid &= k == std::floor(k);
        moved += a16[0][i] != (float)(std::round((double)raw[i] * 32768.0) / 32768.0);
    }
    assert(grid && f.bits[d + "16.wav"] == 16 && moved > 1000);
    r.ok("render --out " + d + "16.wav --to 4 --bits 16");
    assert(f.written[d + "16.wav"] == a16);
    assert(raw.size() == a16[0].size());
    // --normalize: every file alike to the loudness — the line says what it did, the meter agrees
    const std::string said = r.ok("render --out " + d + "n.wav --to 4 --bits 32f --normalize -20 --stems ch_1");
    const std::string stem = d + "n.ch_1.wav";
    assert(contains(said, "normalize integrated=-20.0 gain=-14.0") && !contains(said, "held"));
    arstro::LoudnessMeter lm(2);
    const auto &n = f.written[d + "n.wav"];
    lm.process(n[0].data(), n[1].data(), (int)n[0].size());
    assert(std::fabs(lm.integrated() + 20.0) < 0.05);
    assert(f.written.count(stem) && std::fabs(f.written[stem][0][1000] - n[0][1000]) < 1e-6f); // Main's stem: the mix, scaled alike
    // held by the ceiling: −1 LUFS would put a sine's peak over −1 dBTP; it stops where the peak meets it
    const std::string held = r.ok("render --out " + d + "h.wav --to 4 --bits 32f --normalize -1 --ceiling -2");
    assert(contains(held, "truePeak=-2.0 held=ceiling"));
    arstro::TruePeakMeter tp(2);
    const auto &h = f.written[d + "h.wav"];
    tp.process(h[0].data(), h[1].data(), (int)h[0].size());
    assert(tp.peakDb() <= -2.0 + 1e-3 && tp.peakDb() > -2.1);
    // refusals, each naming the way
    assert(contains(r.no("render --out " + d + ".aiff"), ".wav, .flac, .mp3, .ogg or .opus"));
    assert(contains(r.no("render --out " + d + ".mp3 --bits 16"), "no bit depth"));
    assert(contains(r.no("render --out " + d + ".flac --bits 32f"), "16 or 24 for flac"));
    assert(contains(r.no("render --out " + d + ".wav --kbps 128"), "--kbps is for mp3, ogg and opus"));
    assert(contains(r.no("render --out " + d + ".mp3 --kbps 400"), "32 … 320"));
    assert(contains(r.no("render --out " + d + ".opus --rate 44100"), "48 kHz"));
    assert(contains(r.no("render --out " + d + ".mp3 --rate 96000"), "up to 48 kHz"));
    assert(contains(r.no("render --out " + d + ".wav --ceiling -1"), "goes with --normalize"));
    assert(contains(r.no("render --out " + d + ".wav --normalize 3"), "−60 … 0"));
    assert(contains(r.no("render --out " + d + ".wav --format aiff"), "wav, flac, mp3, ogg or opus"));
    pass("delivery formats (R-EXPORT-1): FLAC / MP3 / Ogg / Opus by the extension or --format; the song rendered AT 44.1 kHz "
         "(88200 frames for 2 s, the tone still 1 kHz); 16 bits on the grid, dithered, the same bytes twice; --normalize gains "
         "every file alike to −20 LUFS (the meter agrees), held at a −2 dBTP ceiling; refusals name the way");
}

static void test_a_true_peak_limiter_on_the_master()
{
    // R-EDM-4a: the registry's `truePeak` on the master's limiter — a bright square, +6 dB on its strip and 6 more into the
    // limiter's −1 dB: the render's
    // true peak (the library's BS.1770 meter) at the ceiling; off, over it between samples; from beat 0 either way
    Run r;
    r.ok("project new " + freshSong("truepeak") + " --bpm 120");
    r.ok("strip add --kind instrument --instrument synth");     // ch_2, dv_1
    r.ok("clip add --strip ch_2 --length 4");
    r.ok("note add pt_1 --pitch 100 --at 0 --length 4");         // 2.6 kHz
    r.ok("set dv_1.osc1.wave=square dv_1.filter.cutoff=20000 ch_2.gain=6");
    const std::string dir = scratch();
    auto take = [&](const std::string &name, double &tpDb, long long &onset) {
        r.ok("render --out " + dir + "/" + name + ".wav --to 4");
        const auto &w = r.fake.written[dir + "/" + name + ".wav"];
        arstro::TruePeakMeter m;
        m.process(w[0].data(), w[1].data(), (int)w[0].size());
        tpDb = m.peakDb();
        onset = -1;
        for (size_t i = 0; i < w[0].size() && onset < 0; ++i)
            if (std::fabs(w[0][i]) > 1e-4f) onset = (long long)i;
    };
    double none = 0, off = 0, on = 0;
    long long at0 = 0, atOff = 0, atOn = 0;
    take("none", none, at0);
    r.ok("device add master --type limiter");                    // dv_2
    r.ok("set dv_2.ceiling=-1 dv_2.gain=6");
    take("off", off, atOff);
    r.ok("set dv_2.truePeak=on");
    assert(r.ok("get dv_2.truePeak") == "on\n");
    take("on", on, atOn);
    if (!(off > -0.9 && on <= -1.0 + 1e-4)) std::printf("    none %.4f off %.4f on %.4f dBTP\n", none, off, on);
    assert(none > -6.0 && off > -0.9 && on <= -1.0 + 1e-4 && on > -1.5); // ≈ −4, −0.52 (0.48 over between samples), −1.0000
    assert(atOff == at0 && atOn == at0);                         // its 16 more samples are compensated too (R-MIX-17)
    pass("a true-peak limiter (R-EDM-4a): `truePeak` on the master's limiter holds a bright square, driven into it, at −1 dBTP "
         "by the library's meter where sample-peak limiting reads over it between samples; the render still starts on beat 0");
}

static void test_forward_is_processing_order_and_moves_renumber()
{
    // R-MIX-4 amended: forward = LATER IN PROCESSING ORDER (mixer order, then strip order) for routes and sends
    Run r;
    r.ok("project new " + freshSong("forward"));
    auto onBuses = [&] { return r.svc.model().mixers[1].strips; };
    r.ok("strip add --kind bus --name Verb");                           // ch_2, made on Buses, beside Main
    assert(r.strip("ch_2")->out == "ch_1");                             // its default output is Main…
    assert(onBuses() == std::vector<std::string>({"ch_2", "ch_1"}));    // …so it is placed BEFORE Main
    r.ok("strip add --kind bus --name Delay");
    assert(r.strip("ch_3")->out == "ch_1" && onBuses() == std::vector<std::string>({"ch_2", "ch_3", "ch_1"}));
    assert(r.strip("ch_2")->targets == std::vector<std::string>({"ch_3", "ch_1", "master", "prt_1"})); // later on its own mixer
    r.ok("route ch_2 --to ch_3");                                       // same mixer, later: forward
    r.ok("send add ch_2 --to ch_1 --gain -6");
    const std::string before = r.text();
    assert(contains(r.no("route ch_3 --to ch_2"), "ch_3 (Delay, on Buses) output → ch_2 (Verb, on Buses): a strip can only feed a strip LATER in processing order"));
    assert(contains(r.no("strip move ch_1 --order 0"), "R-MIX-4"));    // Main first would turn both routes back
    assert(contains(r.no("strip move ch_3 --order 0"), "R-MIX-4"));    // ch_2 → ch_3 would point back
    assert(r.text() == before);
    // a new source still feeds Main, the bus LEAVING Buses — not the first one on it
    r.ok("clip add --src kick.wav");                                    // ch_4 on Sources
    assert(r.strip("ch_4")->out == "ch_1");
    // strip move renumbers, as mixer move does: --order 0 is first, never a tie
    r.ok("clip add --src hat.wav");                                     // ch_5
    r.ok("clip add --src snare.wav");                                   // ch_6
    r.ok("strip move ch_6 --order 0");
    assert(r.svc.model().mixers[0].strips == std::vector<std::string>({"ch_6", "ch_4", "ch_5"}));
    assert(r.strip("ch_6")->order == 0 && r.strip("ch_4")->order == 1 && r.strip("ch_5")->order == 2);
    assert(contains(r.no("strip move ch_6 --order 3"), "from 0 to 2"));
    r.ok("strip move ch_4 --mixer mx_2 --order 0");                     // onto Buses, first: its route to Main stays forward
    assert(onBuses() == std::vector<std::string>({"ch_4", "ch_2", "ch_3", "ch_1"}));
    assert(r.svc.model().mixers[0].strips == std::vector<std::string>({"ch_6", "ch_5"}) && r.strip("ch_5")->order == 1);
    assert(contains(r.no("strip move ch_5 --mixer mx_2"), "R-MIX-4")); // at the END of Buses it would feed Main backward
    pass("forward is processing order (R-MIX-4 amended): a bus made on Buses goes before Main and feeds it; same-mixer routes forward, backward ones refused naming both ends; a reorder that would point one back is refused; strip move renumbers");
}

static void test_lanes_are_reordered()
{
    Run r;
    r.ok("project new " + freshSong("lanes"));
    r.ok("lane add A");
    r.ok("lane add B");
    r.ok("lane add C");                                                 // ln_1 … ln_3
    r.ok("lane move ln_3 --to 0");
    auto ids = [&] {
        std::vector<std::string> v;
        for (const auto &l : r.svc.model().lanes) v.push_back(l.id + ":" + std::to_string(l.order));
        return v;
    };
    assert(ids() == std::vector<std::string>({"ln_3:0", "ln_1:1", "ln_2:2"}));
    r.ok("lane move ln_3 --to 2");
    assert(ids() == std::vector<std::string>({"ln_1:0", "ln_2:1", "ln_3:2"}));
    assert(contains(r.no("lane move ln_1 --to 3"), "from 0 to 2"));
    assert(contains(r.no("lane move ln_9 --to 0"), "no lane"));
    assert(contains(r.no("lane move ln_1"), "--to"));
    r.ok("undo");
    assert(ids() == std::vector<std::string>({"ln_3:0", "ln_1:1", "ln_2:2"}));
    pass("lane move reorders the lanes — renumbered, refused out of range, undone (R-LANE-1)");
}

int main()
{
    gMainThread = std::this_thread::get_id();
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_grammar_table_parses_and_rejects();
    test_event_and_model_tables();
    test_a_new_song_has_its_mixers_and_round_trips();
    test_every_sample_file_gets_its_own_strip();
    test_an_instrument_drop_is_one_command_and_a_refusal_says_nothing_changed();
    test_a_relative_src_is_found_in_the_songs_folder();
    test_note_move_and_quantize_edit_the_pattern();
    test_strip_relink_moves_every_clip_in_one_edit();
    test_undo_and_redo_every_edit();
    test_history_is_a_tree();
    test_formulas_bind_numbers_and_refuse_what_cannot_be_read();
    test_an_automated_gain_renders_its_curve();
    test_a_bezier_gain_renders_what_eval_says_and_the_formula_says();
    test_an_automation_loops_and_a_pump_is_one_line();
    test_a_sidechain_key_ducks_the_bass();
    test_strip_duck_is_one_line();
    test_a_sampler_plays_its_sound_by_the_key();
    test_routing_only_goes_forward_and_refusals_change_nothing();
    test_set_and_get_through_the_registry();
    test_patterns_are_shared_by_their_clips();
    test_solo_mute_matrix_and_audit();
    test_render_writes_the_mix_deterministically();
    test_the_dump_and_the_document();
    test_the_machine_settings_folders_devices_and_recents();
    test_autosave_and_recovery();
    test_keys_are_settings();
    test_live_playback_is_the_offline_render();
    test_the_metronome_clicks_live_and_never_in_a_render();
    test_audition_plays_a_sample_outside_the_song();
    test_a_note_preview_is_the_strip_heard_outside_the_song();
    test_notes_move_and_delete_a_selection_as_one_edit();
    test_a_pattern_grows_to_hold_its_notes();
    test_edits_while_playing_are_heard();
    test_meters_measure_what_plays();
    test_the_driver_and_its_fallback();
    test_a_keyboard_plays_the_armed_strip();
    test_record_the_keyboard_into_a_clip();
    test_expression_lanes_play_the_wheels();
    test_expression_lanes_go_through_midi_files();
    test_the_keyboards_wheels_play_and_record();
    test_markers_name_places_and_sections();
    test_time_insert_and_delete_move_the_whole_song();
    test_lane_groups_gather_fold_and_move_together();
    test_record_audio_lands_where_it_was_played();
    test_an_armed_audio_line_is_monitored_as_set();
    test_a_looped_recording_makes_takes_and_a_comp_chooses();
    test_scale_lock_places_and_moves_notes_on_the_scale();
    test_a_clip_is_split_and_plays_the_same();
    test_overlapping_clips_on_a_lane_crossfade();
    test_consolidate_renders_clips_into_one();
    test_slice_cuts_a_clip_or_makes_a_kit();
    test_a_warped_clip_follows_the_tempo();
    test_a_tempo_map_moves_time();
    test_the_metronome_follows_the_tempo_map();
    test_presets_are_saved_loaded_and_dropped();
    test_the_shipped_presets_load();
    test_templates_start_songs();
    test_the_sample_library_tags_and_finds();
    test_plugins_are_scanned_and_listed();
    test_a_plugin_plays_in_a_song();
    test_a_plugins_own_editor();
    test_midi_learn_and_the_transport_keys();
    test_the_surface_codecs();
    test_a_control_surface();
    test_the_cpu_meter();
    test_the_engine_on_several_threads();
    test_freeze_and_bounce();
    test_the_wavetable_plays_with_no_code_of_its_own();
    test_every_instrument_drops_as_one_line_and_sounds();
    test_a_multisample_reads_its_sfz();
    test_every_effect_drops_as_one_line_and_passes_sound();
    test_a_follower_alone();
    test_a_port_on_another_device_follows_the_clock();
    test_an_input_held_alone();
    test_an_input_on_another_device_follows_the_clock();
    test_note_chance_is_rolled_per_pass_and_the_render_stays_pure();
    test_the_note_tools_humanise_groove_legato_arpeggiate();
    test_held_peak_holds_then_falls();
    test_render_measure();
    test_loop_and_seek();
    test_stop_returns_to_the_cue_or_stays();
    test_a_clip_plays_looped_and_gives_the_songs_loop_back();
    test_a_bound_value_is_published_live();
    test_a_refusal_is_an_event_and_lands_in_lastError();
    test_the_notation_tables();
    test_comment_lines_print_nothing();
    test_everything_made_is_said();
    test_notes_in_bulk_and_by_name();
    test_steps_and_pattern_edits();
    test_a_clip_joins_its_strips_lane();
    test_reading_a_song_back();
    test_an_instruments_groups_are_titled();
    test_a_clip_copied_onto_another_lane();
    test_midi_files();
    test_instrument_tracks();
    test_a_song_from_before_tracks_adopts_them();
    test_latency_is_compensated_and_a_render_starts_on_the_beat();
    test_delivery_formats();
    test_a_true_peak_limiter_on_the_master();
    test_forward_is_processing_order_and_moves_renumber();
    test_lanes_are_reordered();
    std::printf("\n%d passed, 0 failed\n", passed);
    return 0;
}
