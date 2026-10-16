// SolarisService — offline render (R-RENDER-1…3). Compile → build the engine → render the range
// in one pass, capturing the master bus (the mixdown), any stems (strips' post-fader outputs) and,
// with --ports, every output port; then hand each buffer to the host's writer (the core holds
// no codec, R-SVC-4). R-EXPORT-1: the format is the file's extension (or --format) — WAV through
// `writeWav`, FLAC / MP3 / Ogg Vorbis / Opus through `writeAudio`; at --rate the SONG is rendered at
// that rate (its devices built at it, its samples decoded at it — no resampler); --normalize gains
// every file alike to a loudness, held under --ceiling dBTP; integer depths are TPDF-dithered (the
// DSP library's Dither, seeded per file). The tail runs past the song's end until the master falls below −90 dBFS
// for a whole block, or 10 s. The render starts at its first beat EXACTLY (R-MIX-17): the engine's
// output lags the song by its latency (a master limiter's lookahead), so that many samples more are
// rendered at the end and dropped at the start — a stem by its own strip's latency.
#include "Compile.h"
#include "Engine.h"
#include "Format.h"
#include "SolarisService.h"
#include "meter/Meters.h"
#include "output/Dither.h"
#include "base/AudioConfig.h"
#include <cctype>
#include <map>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace arstro
{
namespace solaris
{
    namespace
    {
        std::string withSuffix(const std::string &out, const std::string &suffix)
        {
            const fs::path p(out);
            return (p.parent_path() / (p.stem().string() + "." + suffix + p.extension().string())).string();
        }
        double dbfs(float lin) { return lin > 0 ? 20.0 * std::log10(lin) : -1000.0; }
        std::string dbText(float lin) { return lin > 0 ? canonicalNumber(std::round(dbfs(lin) * 10.0) / 10.0) : "-inf"; }
        std::string fixed(double v, int places)
        {
            if (v <= LoudnessMeter::kSilence + 0.5) return "-inf";
            char b[32];
            std::snprintf(b, sizeof b, "%.*f", places, v);
            return b;
        }
        /** R-METER-1: a mixdown through the DSP library's meters, 100 ms at a time, as one line. */
        std::string measured(const std::vector<std::vector<float>> &master, size_t len, int rate)
        {
            LoudnessMeter lm(2);
            TruePeakMeter tp(2);
            CorrelationMeter cm;
            const size_t step = (size_t)std::max(1, rate / 10);
            double maxM = LoudnessMeter::kSilence, maxS = LoudnessMeter::kSilence, minCorr = 1.0;
            bool heard = false;
            for (size_t i = 0; i < len; i += step)
            {
                const int k = (int)std::min(step, len - i);
                lm.process(master[0].data() + i, master[1].data() + i, k);
                tp.process(master[0].data() + i, master[1].data() + i, k);
                cm.process(master[0].data() + i, master[1].data() + i, k);
                maxM = std::max(maxM, lm.momentary());
                maxS = std::max(maxS, lm.shortTerm());
                if (lm.momentary() > -70.0) { minCorr = std::min(minCorr, cm.correlation()); heard = true; } // silence has no phase
            }
            return "measure integrated=" + fixed(lm.integrated(), 1) + " momentaryMax=" + fixed(maxM, 1) + " shortTermMax=" + fixed(maxS, 1) +
                   " truePeak=" + fixed(tp.peakDb(), 1) + " correlationMin=" + (heard ? fixed(minCorr, 2) : std::string("-")) + "\n";
        }
    }

    bool SolarisService::render(const Command &c, std::string &err)
    {
        if (!requireOpen(err)) return false;
        const std::string out = c.flag("out");
        if (out.empty()) { err = "render needs --out <file>"; return false; }
        // R-EXPORT-1: the format is --format, else the file's extension
        std::string format = c.flag("format");
        if (format.empty())
        {
            std::string ext = fs::path(out).extension().string();
            for (auto &ch : ext) ch = (char)std::tolower((unsigned char)ch);
            format = ext == ".wav" ? "wav" : ext == ".flac" ? "flac" : ext == ".mp3" ? "mp3" : ext == ".ogg" || ext == ".oga" ? "ogg" : ext == ".opus" ? "opus" : "";
            if (format.empty()) { err = "`" + out + "`: the format is its extension — .wav, .flac, .mp3, .ogg or .opus (or --format)"; return false; }
        }
        if (format != "wav" && format != "flac" && format != "mp3" && format != "ogg" && format != "opus") { err = "--format is wav, flac, mp3, ogg or opus"; return false; }
        const bool lossy = format == "mp3" || format == "ogg" || format == "opus";
        if (lossy && c.has("bits")) { err = "a " + format + " file has no bit depth (--kbps sets how much it keeps)"; return false; }
        const std::string bitsText = c.flag("bits", "24");
        int bits = 24;
        if (!lossy)
        {
            if (bitsText == "16") bits = 16;
            else if (bitsText == "24") bits = 24;
            else if (bitsText == "32f" && format == "wav") bits = 32;
            else { err = format == "wav" ? "--bits is 16, 24 or 32f" : "--bits is 16 or 24 for flac"; return false; }
        }
        int kbps = format == "mp3" ? 320 : format == "ogg" ? 256 : 192;
        double v = 0;
        if (c.has("kbps"))
        {
            if (!lossy) { err = "--kbps is for mp3, ogg and opus (" + format + " keeps everything)"; return false; }
            const int lo = format == "opus" ? 6 : format == "ogg" ? 45 : 32, hi = format == "opus" ? 510 : format == "ogg" ? 500 : 320;
            if (!parseNumber(c.flag("kbps"), v) || v != std::floor(v) || v < lo || v > hi)
            {
                err = "--kbps for " + format + " is " + std::to_string(lo) + " … " + std::to_string(hi);
                return false;
            }
            kbps = (int)v;
        }
        // the rate: the SONG rendered at it — every device reads the rate it is built at, a sample is decoded at it
        int rate = format == "opus" ? 48000 : mProject.header.sampleRate;
        if (c.has("rate"))
        {
            if (!parseNumber(c.flag("rate"), v) || v != std::floor(v) || v < 8000 || v > 192000) { err = "--rate is a sample rate in Hz, 8000 … 192000"; return false; }
            rate = (int)v;
        }
        if (format == "opus" && rate != 48000) { err = "an Opus file is 48 kHz (--rate 48000)"; return false; }
        if (format == "mp3" && rate > 48000) { err = "an MP3 goes up to 48 kHz"; return false; }
        if (rate != mProject.header.sampleRate && mPlayer && mPlayer->running())
        {
            err = "stop first: a render at another rate re-tunes the devices the playing song shares";
            return false;
        }
        const bool normalize = c.has("normalize");
        double target = 0, ceiling = -1.0;
        if (normalize && (!parseNumber(c.flag("normalize"), target) || target < -60 || target > 0)) { err = "--normalize is a loudness in LUFS, −60 … 0"; return false; }
        if (c.has("ceiling"))
        {
            if (!normalize) { err = "--ceiling goes with --normalize"; return false; }
            if (!parseNumber(c.flag("ceiling"), ceiling) || ceiling < -24 || ceiling > 0) { err = "--ceiling is dBTP, −24 … 0"; return false; }
        }
        if (format == "wav" ? !mHost.writeWav : !mHost.writeAudio) { err = "this build has no " + format + " writer"; return false; }

        Project rated = mProject;
        rated.header.sampleRate = rate;
        std::map<std::string, std::shared_ptr<const engine::Pcm>> decoded; // at the render's rate, when not the song's
        std::function<std::shared_ptr<const engine::Pcm>(const std::string &)> pcmAt = [&](const std::string &src) -> std::shared_ptr<const engine::Pcm> {
            if (rate == mProject.header.sampleRate) return pcmFor(src);
            if (src.compare(0, 8, "stretch:") == 0) // R-WARP-1: stretched at the render's rate, from the file decoded at it
            {
                auto st = decoded.find(src);
                if (st != decoded.end()) return st->second;
                return decoded[src] = stretched(src, pcmAt, rate);
            }
            const std::string path = resolvePath(src);
            auto it = decoded.find(path);
            if (it != decoded.end()) return it->second;
            auto pcm = std::make_shared<engine::Pcm>();
            std::string e;
            if (!mHost.decodeAudio || !mHost.decodeAudio(path, rate, *pcm, e) || pcm->frames <= 0) pcm.reset();
            decoded[path] = pcm;
            return pcm;
        };
        // the build sets the library's rate; put back what it was, so nothing built later reads the render's
        struct RateBack
        {
            int rate = AudioConfig::instance().sampleRate();
            ~RateBack() { AudioConfig::instance().setSampleRate(rate); }
        } rateBack;
        CompileResult cr = compile(rated, pcmAt);
        for (const auto &w : cr.warnings) emit(Event(Event::Kind::Info).with("text", w));

        std::vector<int> stems;
        std::vector<std::string> stemIds;
        if (c.has("stems"))
        {
            std::string cur;
            std::string list = c.flag("stems") + ",";
            if (c.flag("stems") == "all") // every strip, in processing order
            {
                list.clear();
                for (const auto &id : cr.stripIds) list += id + ",";
            }
            for (char ch : list)
            {
                if (ch != ',') { cur += ch; continue; }
                if (cur.empty()) continue;
                auto it = std::find(cr.stripIds.begin(), cr.stripIds.end(), cur);
                if (it == cr.stripIds.end()) { err = "--stems names `" + cur + "`, which is no strip"; return false; }
                stems.push_back((int)(it - cr.stripIds.begin()));
                stemIds.push_back(cur);
                cur.clear();
            }
        }

        engine::Engine eng;
        if (!eng.build(cr.graph, err)) return false;
        double from = 0, to = -1;
        if (c.has("from")) { if (!parseNumber(c.flag("from"), v) || v < 0) { err = "--from must be beats ≥ 0"; return false; } from = v; }
        if (c.has("to")) { if (!parseNumber(c.flag("to"), v) || v <= from) { err = "--to must be beats after --from"; return false; } to = v; }
        const long long start = beatsToSamples(rated, from);
        const long long end = to >= 0 ? beatsToSamples(rated, to) : std::max(start, cr.graph.end);
        const long long tailCap = to >= 0 ? 0 : 10LL * rate; // an explicit --to is exact: no tail
        const float floor = (float)std::pow(10.0, -90.0 / 20.0);

        eng.seek(start);
        const long long lag = eng.outputLatency(); // R-MIX-17: rendered past the end, trimmed from the start
        const long long endLate = end + lag;
        eng.captureMaster(true);
        eng.captureStrips(stems);
        std::vector<std::vector<float>> master(2);
        std::vector<std::vector<std::vector<float>>> stemData(stems.size(), std::vector<std::vector<float>>(2));
        std::vector<std::vector<std::vector<float>>> portData(cr.graph.ports.size());
        for (size_t i = 0; i < portData.size(); ++i) portData[i].resize(std::max(1, cr.graph.ports[i].channels));

        engine::PortBuffers pb;
        const int chunk = 4096;
        long long pos = start;
        while (true)
        {
            const bool inSong = pos < endLate;
            const int n = inSong ? (int)std::min<long long>(chunk, endLate - pos) : chunk;
            if (!inSong && pos - endLate >= tailCap) break;
            eng.render(n, pb);
            const auto &m = eng.capturedMaster();
            for (int ch = 0; ch < 2; ++ch) master[ch].insert(master[ch].end(), m[ch].begin(), m[ch].end());
            for (size_t k = 0; k < stems.size(); ++k)
                for (int ch = 0; ch < 2; ++ch) stemData[k][ch].insert(stemData[k][ch].end(), eng.captured(k)[ch].begin(), eng.captured(k)[ch].end());
            if (c.has("ports"))
                for (size_t i = 0; i < portData.size(); ++i)
                    for (size_t ch = 0; ch < portData[i].size(); ++ch)
                        portData[i][ch].insert(portData[i][ch].end(), pb.ports[i][ch].begin(), pb.ports[i][ch].end());
            pos += n;
            // (R-RENDER-3) past the end: stop at the first block the master spends below −90 dBFS
            if (!inSong && eng.masterMeter().peak[0] < floor && eng.masterMeter().peak[1] < floor) break;
        }
        // R-MIX-17: each file from the song's `start` — the master and the ports `lag` late, a stem its strip's
        const size_t len = master[0].size() - (size_t)lag;
        auto trim = [len](std::vector<float> &ch, long long late) {
            std::vector<float> t(len, 0.0f);
            for (size_t i = 0; i < len && (size_t)late + i < ch.size(); ++i) t[i] = ch[(size_t)late + i];
            ch.swap(t);
        };
        for (auto &ch : master) trim(ch, lag);
        for (size_t k = 0; k < stems.size(); ++k)
            for (auto &ch : stemData[k]) trim(ch, eng.stripLatency(stems[k]));
        if (c.has("ports"))
            for (auto &port : portData)
                for (auto &ch : port) trim(ch, lag);

        // R-EXPORT-1: to a loudness — the gain the master's integrated loudness asks, no more than its true peak
        // allows under the ceiling; every file alike, so the stems still sum to the mix
        std::string normalized;
        if (normalize)
        {
            LoudnessMeter lm(2);
            TruePeakMeter tp(2);
            lm.process(master[0].data(), master[1].data(), (int)len);
            tp.process(master[0].data(), master[1].data(), (int)len);
            const double I = lm.integrated(), P = tp.peakDb();
            if (I <= LoudnessMeter::kSilence + 0.5) normalized = "normalize: nothing to measure — written as mixed\n";
            else
            {
                double g = target - I;
                const bool held = P + g > ceiling;
                if (held) g = ceiling - P;
                const float k = (float)std::pow(10.0, g / 20.0);
                auto scale = [k](std::vector<std::vector<float>> &chs) {
                    for (auto &ch : chs)
                        for (auto &x : ch) x *= k;
                };
                scale(master);
                for (auto &s : stemData) scale(s);
                for (auto &s : portData) scale(s);
                normalized = "normalize integrated=" + fixed(I + g, 1) + " gain=" + (g >= 0 ? "+" : "") + fixed(g, 1) +
                             " truePeak=" + fixed(P + g, 1) + (held ? " held=ceiling" : "") + "\n";
            }
        }
        // integer depths: TPDF dither onto the grid (the DSP library's), seeded per file — the same song, the same bytes
        uint32_t fileNo = 0;
        auto write = [&](const std::string &path, std::vector<std::vector<float>> &chs) {
            if (!lossy && bits != 32)
            {
                Dither d(bits, Dither::kSeed + fileNo);
                for (auto &ch : chs) d.process(ch.data(), (int)ch.size());
            }
            ++fileNo;
            return format == "wav" ? mHost.writeWav(path, chs, rate, bits, err) : mHost.writeAudio(path, chs, rate, format, bits, kbps, err);
        };
        if (!write(out, master)) return false;
        mOutput = out + "\n";
        for (size_t k = 0; k < stems.size(); ++k)
        {
            const std::string f = withSuffix(out, stemIds[k]);
            if (!write(f, stemData[k])) return false;
            mOutput += f + "\n";
        }
        if (c.has("ports"))
            for (size_t i = 0; i < portData.size(); ++i)
            {
                const std::string f = withSuffix(out, cr.graph.ports[i].name);
                if (!write(f, portData[i])) return false;
                mOutput += f + "\n";
            }
        mOutput += normalized;
        if (c.has("measure")) mOutput += measured(master, len, rate); // LUFS, LUFS, LUFS, dBTP, −1…+1 — of what was written

        // Peaks for `audit` (R-MIX-10): any strip, or the master, that went over full scale.
        mLastRenderPeaks.clear();
        for (size_t i = 0; i < eng.stripMeters().size(); ++i)
        {
            const float pk = std::max(eng.stripMeters()[i].maxPeak[0], eng.stripMeters()[i].maxPeak[1]);
            if (pk > 1.0f) mLastRenderPeaks.push_back("clipping: " + cr.stripIds[i] + " peaked at +" + dbText(pk) + " dBFS in the last render");
        }
        const float mpk = std::max(eng.masterMeter().maxPeak[0], eng.masterMeter().maxPeak[1]);
        if (mpk > 1.0f) mLastRenderPeaks.push_back("clipping: the master peaked at +" + dbText(mpk) + " dBFS in the last render");
        emit(Event(Event::Kind::RenderFinished).with("out", out).with("frames", (long long)master[0].size()).with("peak", dbText(mpk)));
        return true;
    }
}
}
