/*
 *  interstellar/host — VideoFrameDecoder: how a VIDEO source gets graded in Cosmo, with no change
 *  to Cosmo at all (R-RACK-3).
 *
 *  Cosmo grades photographs. A video is not a photograph — but `CosmoService::setDecoderFactory`
 *  is an injectable seam, and a decoder that answers a `.mov` with **one extracted frame** is all
 *  it takes: Cosmo then holds an ordinary image slot, and every one of its facilities — grouping,
 *  stacking, bypass, history, presets, before/after, export — works on it unchanged.
 *
 *  **That is the whole of the requirement that looked like work in another app.** The first
 *  specification listed "a video source in Cosmo" as a cross-app prerequisite blocking this one;
 *  it turns out to be a seam that already exists, used from this side of it.
 *
 *  Which frame is the reference is carried in the path as `…/DSC01.MOV#t=4.250`, because Cosmo's
 *  slot identity IS its path and a selector that lived anywhere else would not survive a save.
 *  Stills delegate to Cosmo's own `NativeImageDecoder`, so nothing about photographs changes.
 */
#pragma once
#include "core/decode/ImageDecoder.h"
#include "FrameSelector.h"
#include <memory>
#include <string>

namespace arstro
{
namespace interstellar_host
{
    /** Split `path#t=4.25` into its file and its reference time. Public because the rack stores
     *  the two separately and must spell the join exactly as this splits it. */
    void splitFrameSelector(const std::string &spec, std::string &path, double &seconds);
    std::string joinFrameSelector(const std::string &path, double seconds);
    /** True when this extension is one libavformat should open rather than GdkPixbuf. */
    bool looksLikeVideo(const std::string &path);

    class VideoFrameDecoder : public cosmo::IImageDecoder
    {
    public:
        /** `selector`, when given, overrides the frame baked into a stored path (R-RACK-3). */
        explicit VideoFrameDecoder(std::shared_ptr<const interstellar::FrameSelector> selector = nullptr);
        ~VideoFrameDecoder() override;

        cosmo::DecodedImage decodeFile(const std::string &path) override;
        cosmo::DecodedImage decodeFile(const std::string &path, cosmo::Fidelity f) override;

    private:
        std::unique_ptr<cosmo::IImageDecoder> mStills;   // Cosmo's own, for everything else
        std::shared_ptr<const interstellar::FrameSelector> mSelector;
    };
}
}
