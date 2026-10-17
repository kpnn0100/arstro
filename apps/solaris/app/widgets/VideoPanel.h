/*
 *  solaris_ui — VideoPanel: the song's picture — the content of the Video window (R-VIDEO-1, R-WIN; docs/discussion.md
 *  Round 18).
 *
 *  Opened from View › Show Video. It draws the frame at the model's `video.time` — the picture's seconds at the heard
 *  position, which the service computes through the tempo map (law 13) — letterboxed in its box, black around it. The
 *  frame is the HOST's: a decoder the window's host owns (`frameSource`; the core carries no codec, R-SVC-4), asked
 *  only when the time, the file or the box changed. Outside the picture's span it says where the song is — "Before the
 *  picture", "After the picture" — and with none, or one the host cannot read, says that. A foot line names the file,
 *  its size and rate, and the picture's time (mono: it changes as it plays).
 *
 *  Nothing snaps (§1): the picture fades in over the words and out again (`pictureAmount`); a jump — the time bar put
 *  somewhere else, a different file — cross-dissolves from the frame that was to the frame that is (`dissolveAmount`,
 *  160 ms, linear); while playing, frame follows frame — that is the picture's own motion; the words cross-fade when
 *  they change. `frameTime` is the drawn frame's own time.
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_ui
{
    /** A frame as the host decoded it: RGBA, rows top first, fitted to the box it was asked for. */
    struct VideoImage
    {
        int width = 0, height = 0;
        double time = -1;          // the frame's own time in the picture (seconds)
        std::vector<uint8_t> rgba;
    };
    /** The host's decoder: the frame showing `t` seconds into `file`, fitted to boxW × boxH. */
    using VideoFrameSource = std::function<bool(const std::string &file, double t, int boxW, int boxH, VideoImage &out)>;

    class VideoPanel : public artboard::Segment
    {
    public:
        static constexpr double kFootH = 26.0;     // space::u(8), as a window's title
        static constexpr double kJumpS = 0.5;      // a time change larger than this is a jump, not playing

        VideoPanel();
        void bind(const solaris::AppModel &m);
        void layout() {}
        void advance(double nowMs) override;

        VideoFrameSource frameSource;

        // what a test reads (LIVE, as drawn)
        double pictureAmount() const { return mShow.value(); }      // 0: the words; 1: the picture
        double dissolveAmount() const { return mDissolve.value(); } // 0 … 1 across a jump (1 at rest)
        const std::string &words() const { return mWords; }        // what is said when no picture shows
        double wordsAmount() const { return mWordsMix.value(); }    // the words' cross-fade, 1 at rest
        double frameTime() const { return mHave ? mFrameTime : -1.0; }
        int asks() const { return mAsks; }                          // how often the host was asked for a frame
        artboard::Rect boxRect() const;                             // the picture's box (local): the panel less its foot
        artboard::Rect pictureRect() const;                         // where the frame is drawn, letterboxed

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        void syncImages(artboard::IRenderTarget &t) const;
        solaris::VideoModel mVideo;
        double mPosition = 0;
        // the frame drawn, and the one a jump dissolves from
        std::vector<uint8_t> mPixels, mPrevPixels;
        int mW = 0, mH = 0, mPrevW = 0, mPrevH = 0;
        double mFrameTime = -1;
        bool mHave = false;
        // what was last asked of the host
        std::string mAskedFile;
        double mAskedT = -2;
        int mAskedW = 0, mAskedH = 0, mAsks = 0;
        // the eased state
        bool mShowWanted = false, mShowApplied = false, mJumped = false;
        artboard::AnimatedProperty mShow{0.0}, mDissolve{1.0}, mWordsMix{1.0};
        std::string mWords, mOldWords;
        // the images as registered with the target that last drew them
        mutable artboard::IRenderTarget *mOwner = nullptr;
        mutable int mCurId = 0, mPrevId = 0, mCurW = 0, mCurH = 0;
        mutable bool mDirty = false, mPrevDirty = false;
    };
}
}
