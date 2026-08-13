/*
 *  Arstro ImageProcessing Library
 *
 *  VideoProcessor: a thin wrapper that applies an ImageProcessor (typically an
 *  ImageBlock edit stack) to a sequence of frames. It owns no decoding/muxing —
 *  it is strictly the per-frame application abstraction, the video-domain
 *  analogue of running a DSP chain sample-by-sample. A frame index is advanced
 *  per frame so time-varying parameters (or Grain's seed) can change over time.
 */
#pragma once
#include "../base/ImageProcessor.h"

namespace arstro
{
    class VideoProcessor
    {
    public:
        VideoProcessor() = default;
        explicit VideoProcessor(ImageProcessor *proc) : mProc(proc) {}

        void setProcessor(ImageProcessor *p) { mProc = p; }

        /** Re-arm parameter smoothing so values interpolate across frames. */
        void enableSmoothing(bool b)
        {
            if (mProc)
                mProc->setSmoothEnable(b);
        }

        void reset() { mFrameIndex = 0; }
        long frameIndex() const { return mFrameIndex; }

        /** Apply the processor to one frame and advance the frame index. */
        Image processFrame(const Image &frame)
        {
            Image out;
            if (mProc)
                mProc->apply(frame, out);
            else
                out = frame.clone();
            ++mFrameIndex;
            return out;
        }

    private:
        ImageProcessor *mProc = nullptr;
        long mFrameIndex = 0;
    };
}
