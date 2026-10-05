/*
 *  interstellar_core — the codec seams. The core NAMES them and carries no codec; the host FILLS
 *  them with FFmpeg, exactly as `cosmo_core` names `IImageDecoder` and its host fills it with
 *  GdkPixbuf and LibRaw.
 *
 *  `IFrameSource` is also the floor the lazy Volume is built on (R-VOL): a volume's `window()`
 *  materialises frames through one of these and caches them. Keeping the seam at "give me frame N"
 *  rather than "give me a volume" is deliberate — the residency policy belongs to the core, which
 *  knows the declared radius, and not to the decoder, which knows only the file.
 */
#pragma once
#include "Raster.h"
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
    class IFrameSource
    {
    public:
        struct Info
        {
            int width = 0, height = 0;
            double fps = 24.0;
            long long frames = 0;
            int bitDepth = 8;           // bits per component the source carries (frameAt is 8-bit; frameAtDeep keeps them)
            std::string timecode;       // R-XCH-5: the first frame's source timecode ("10:00:00:00"), "" = none
            std::string reel;           // R-XCH-5: the reel/tape name the camera wrote, "" = none
            bool hasAudio = false;      // the file carries sound too
            bool valid() const { return width > 0 && height > 0; }
        };

        virtual ~IFrameSource() = default;
        virtual bool open(const std::string &path, Info &out) = 0;
        /** Straight RGBA8 for `frame`. Decoders are SEQUENTIAL: `frame` advancing by one is the
         *  common case and a seek is the exception, so an implementation keeps its position and
         *  seeks only backwards or a long way forward. A still image is a one-frame stream, which
         *  is why the timeline needs no second code path for stills. */
        virtual bool frameAt(long long frame, Raster &out) = 0;
        /** The same frame at 16 bits per component (a deep Raster, R-COLOR-1) — what a render to a
         *  deep codec decodes, so a 10-bit source keeps its 10 bits. The default widens frameAt
         *  (v * 257): right for an 8-bit source, and for a decoder that has nothing better. */
        virtual bool frameAtDeep(long long frame, Raster &out)
        {
            if (!frameAt(frame, out)) return false;
            toDeep(out, out);
            return true;
        }
    };

    /** How to encode a render (R-RENDER-6). The service validates it against the codec before a job
     *  is queued; the host's writer honours it or refuses, naming why — never a silent substitute. */
    struct EncodeSpec
    {
        std::string codec = "h264";     // h264 | h265 | prores | dnxhr
        std::string profile;            // prores: proxy|lt|standard|hq|4444 · dnxhr: lb|sq|hq|hqx|444
        int quality = 18;               // constant quality (CRF) for h264/h265: 0 best … 51
        std::string speed = "medium";   // the x264/x265 preset
        int bitDepth = 8;               // h265: 8 | 10 · prores 10 · dnxhr per profile · h264 8
        bool hardware = false;          // R-PLAY-3: encode on the GPU's video unit when there is one
        std::string output = "rec709";  // R-COLOR-4: what the pixels are — tags the stream (primaries,
                                        // transfer, matrix) and, for pq/hlg, its HDR signalling
        double peak = 1000.0;           // pq: the mastering display's peak, cd/m²
        int audioRate = 0;              // R-AUD-9: 0 = no sound; else the master, stereo, at this rate —
                                        // AAC beside H.264/H.265, 24-bit PCM beside ProRes/DNxHR
        // R-DLV-1: a subtitle track — mov_text in an MP4/MOV, SubRip in an MKV — its cues in the
        // render's own seconds (0 = its first frame), in order, none overlapping
        struct Cue { double start = 0, end = 0; std::string text; };
        std::vector<Cue> subtitles;
    };

    class IFrameWriter
    {
    public:
        virtual ~IFrameWriter() = default;
        /** `frames` is how many will be written, so a sequence writer decides its naming BEFORE
         *  the first one rather than discovering it after — the first build numbered every frame
         *  but the first, which a golden comparison cannot use. */
        virtual bool begin(const std::string &path, int w, int h, double fps, long long frames, const EncodeSpec &spec) = 0;
        virtual bool write(const Raster &frame) = 0;
        /** R-AUD-9: the master's samples for the frames written so far — interleaved stereo float,
         *  in order, as many as the render's clock says (a writer begun without audio ignores them). */
        virtual bool writeAudio(const float *stereo, int frames) { (void)stereo; (void)frames; return true; }
        virtual bool end() = 0;
        /** What the writer did differently from what was asked, in words ("" = nothing) — e.g. a
         *  hardware encode that fell back to software because no device was there (R-PLAY-3). */
        virtual std::string note() const { return std::string(); }
    };
}
}
