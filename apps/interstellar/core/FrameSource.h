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
            bool valid() const { return width > 0 && height > 0; }
        };

        virtual ~IFrameSource() = default;
        virtual bool open(const std::string &path, Info &out) = 0;
        /** Straight RGBA8 for `frame`. Decoders are SEQUENTIAL: `frame` advancing by one is the
         *  common case and a seek is the exception, so an implementation keeps its position and
         *  seeks only backwards or a long way forward. A still image is a one-frame stream, which
         *  is why the timeline needs no second code path for stills. */
        virtual bool frameAt(long long frame, Raster &out) = 0;
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
        virtual bool end() = 0;
    };
}
}
