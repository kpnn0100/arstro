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

    class IFrameWriter
    {
    public:
        virtual ~IFrameWriter() = default;
        /** `frames` is how many will be written, so a sequence writer decides its naming BEFORE
         *  the first one rather than discovering it after — the first build numbered every frame
         *  but the first, which a golden comparison cannot use. */
        virtual bool begin(const std::string &path, int w, int h, double fps, long long frames) = 0;
        virtual bool write(const Raster &frame) = 0;
        virtual bool end() = 0;
    };
}
}
