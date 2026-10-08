// SolarisService — offline render (R-RENDER-1…3). Compile → build the engine → render the range
// in one pass, capturing the master bus (the mixdown), any stems (strips' post-fader outputs) and,
// with --ports, every output port; then hand each buffer to the host's WAV writer (the core holds
// no codec, R-SVC-4). The tail runs past the song's end until the master falls below −90 dBFS
// for a whole block, or 10 s.
#include "Compile.h"
#include "Engine.h"
#include "Format.h"
#include "SolarisService.h"
#include <algorithm>
#include <cmath>
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
    }

    bool SolarisService::render(const Command &c, std::string &err)
    {
        if (!requireOpen(err)) return false;
        const std::string out = c.flag("out");
        if (out.empty()) { err = "render needs --out <file.wav>"; return false; }
        const std::string bitsText = c.flag("bits", "24");
        if (bitsText != "24" && bitsText != "32f") { err = "--bits must be 24 or 32f"; return false; }
        if (!mHost.writeWav) { err = "this build has no WAV writer"; return false; }

        CompileResult cr = compile(mProject, [this](const std::string &src) { return pcmFor(src); });
        for (const auto &w : cr.warnings) emit(Event(Event::Kind::Info).with("text", w));

        std::vector<int> stems;
        std::vector<std::string> stemIds;
        if (c.has("stems"))
        {
            std::string cur;
            const std::string list = c.flag("stems") + ",";
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
        double from = 0, to = -1, v = 0;
        if (c.has("from")) { if (!parseNumber(c.flag("from"), v) || v < 0) { err = "--from must be beats ≥ 0"; return false; } from = v; }
        if (c.has("to")) { if (!parseNumber(c.flag("to"), v) || v <= from) { err = "--to must be beats after --from"; return false; } to = v; }
        const long long start = beatsToSamples(mProject, from);
        const long long end = to >= 0 ? beatsToSamples(mProject, to) : std::max(start, cr.graph.end);
        const int rate = mProject.header.sampleRate;
        const long long tailCap = to >= 0 ? 0 : 10LL * rate; // an explicit --to is exact: no tail
        const float floor = (float)std::pow(10.0, -90.0 / 20.0);

        eng.seek(start);
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
            const bool inSong = pos < end;
            const int n = inSong ? (int)std::min<long long>(chunk, end - pos) : chunk;
            if (!inSong && pos - end >= tailCap) break;
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

        const int bits = bitsText == "24" ? 24 : 32;
        if (!mHost.writeWav(out, master, rate, bits, err)) return false;
        mOutput = out + "\n";
        for (size_t k = 0; k < stems.size(); ++k)
        {
            const std::string f = withSuffix(out, stemIds[k]);
            if (!mHost.writeWav(f, stemData[k], rate, bits, err)) return false;
            mOutput += f + "\n";
        }
        if (c.has("ports"))
            for (size_t i = 0; i < portData.size(); ++i)
            {
                const std::string f = withSuffix(out, cr.graph.ports[i].name);
                if (!mHost.writeWav(f, portData[i], rate, bits, err)) return false;
                mOutput += f + "\n";
            }

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
