#include "VideoPanel.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    namespace
    {
        Color fade(Color c, double a)
        {
            c.a *= a;
            return c;
        }
        std::string fileName(const std::string &path)
        {
            const size_t slash = path.find_last_of('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }
        // the picture's time as m:ss.mmm (mono: it changes as it plays)
        std::string clock(double s)
        {
            if (s < 0) return "\xE2\x80\x94";
            char buf[32];
            const int m = (int)(s / 60.0);
            std::snprintf(buf, sizeof buf, "%d:%06.3f", m, s - m * 60.0);
            return buf;
        }
        void fillBox(IRenderTarget &t, const Rect &r, Color c) // a square-cornered fill: the stage, the foot, its rule
        {
            t.setFill(c);
            t.beginPath();
            t.moveTo(r.x, r.y);
            t.lineTo(r.x + r.w, r.y);
            t.lineTo(r.x + r.w, r.y + r.h);
            t.lineTo(r.x, r.y + r.h);
            t.closePath();
            t.fillPath();
        }
        Rect fit(int w, int h, const Rect &box)
        {
            if (w <= 0 || h <= 0 || box.w <= 0 || box.h <= 0) return box;
            const double k = std::min(box.w / w, box.h / h);
            const double fw = w * k, fh = h * k;
            return Rect{box.x + (box.w - fw) * 0.5, box.y + (box.h - fh) * 0.5, fw, fh};
        }
    }

    VideoPanel::VideoPanel() { clipToBounds = true; }

    Rect VideoPanel::boxRect() const { return Rect{0, 0, width.value(), std::max(0.0, height.value() - kFootH)}; }
    Rect VideoPanel::pictureRect() const { return fit(mW, mH, boxRect()); }

    void VideoPanel::bind(const solaris::AppModel &m)
    {
        mVideo = m.video;
        mPosition = m.transport.position;
    }

    void VideoPanel::advance(double nowMs)
    {
        // what is said when no picture shows: none, unreadable, or where the song is against its span
        std::string words = mWords;
        // words (Words.h): they cross-fade as they change; the window dissolves them as the language does
        if (mVideo.id.empty()) words = ::arstro::solaris_ui::say("No picture in this song");
        else if (mVideo.offline) words = ::arstro::solaris_ui::say("Cannot read {1}", fileName(mVideo.src));
        else if (mVideo.time < 0) words = mPosition < mVideo.at ? ::arstro::solaris_ui::say("Before the picture") : ::arstro::solaris_ui::say("After the picture");
        if (words != mWords)
        {
            if (mWords.empty()) mWordsMix.set(1.0); // the first words: placed, nowhere to travel from
            else
            {
                mOldWords = mWords;
                mWordsMix.set(0.0);
                mWordsMix.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            }
            mWords = words;
        }
        // the frame: asked of the host when the time, the file or the box changed — never twice for the same
        const bool inside = !mVideo.id.empty() && !mVideo.offline && mVideo.time >= 0 && frameSource;
        const Rect box = boxRect();
        const int bw = (int)std::lround(box.w), bh = (int)std::lround(box.h);
        if (inside && bw > 0 && bh > 0 && (mVideo.path != mAskedFile || std::fabs(mVideo.time - mAskedT) > 1e-9 || bw != mAskedW || bh != mAskedH))
        {
            const bool jump = mHave && (mVideo.path != mAskedFile || std::fabs(mVideo.time - mAskedT) > kJumpS);
            VideoImage img;
            ++mAsks;
            if (frameSource(mVideo.path, mVideo.time, bw, bh, img) && img.width > 0 && img.height > 0 &&
                img.rgba.size() == (size_t)img.width * (size_t)img.height * 4)
            {
                if (jump && mShow.value() > 0.001)
                {
                    // a jump: the frame that was stays, and the new one dissolves in over it (it shows nothing new yet)
                    mPrevPixels.swap(mPixels);
                    mPrevW = mW;
                    mPrevH = mH;
                    mPrevDirty = true;
                    mDissolve.set(0.0);
                    mDissolve.animateTo(1.0, motion::kDissolveMs, Easing::Linear, nowMs);
                }
                mPixels = std::move(img.rgba);
                mW = img.width;
                mH = img.height;
                mFrameTime = img.time;
                mHave = true;
                mDirty = true;
            }
            mAskedFile = mVideo.path;
            mAskedT = mVideo.time;
            mAskedW = bw;
            mAskedH = bh;
        }
        mShowWanted = inside && mHave;
        if (mShowWanted != mShowApplied)
        {
            mShow.animateTo(mShowWanted ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mShowApplied = mShowWanted;
        }
        mShow.update(nowMs);
        mDissolve.update(nowMs);
        mWordsMix.update(nowMs);
        Segment::advance(nowMs);
    }

    void VideoPanel::syncImages(IRenderTarget &t) const
    {
        if (mOwner != &t)
        {
            // a different target: the ids held mean nothing there — start over in this one
            mOwner = &t;
            mCurId = mPrevId = 0;
            mDirty = mHave;
            if (!mPrevPixels.empty() && mDissolve.value() < 1.0) mPrevId = t.registerImage(mPrevPixels.data(), mPrevW, mPrevH);
            mPrevDirty = false;
        }
        if (mPrevDirty) // a jump: the frame that was becomes the one dissolved from
        {
            if (mPrevId) t.releaseImage(mPrevId);
            mPrevId = mCurId;
            mCurId = 0;
            mPrevDirty = false;
        }
        if (mDirty && mHave)
        {
            if (mCurId && mCurW == mW && mCurH == mH) t.updateImage(mCurId, mPixels.data(), mW, mH); // playing: one upload a frame
            else
            {
                if (mCurId) t.releaseImage(mCurId);
                mCurId = t.registerImage(mPixels.data(), mW, mH);
                mCurW = mW;
                mCurH = mH;
            }
            mDirty = false;
        }
        if (mPrevId && mDissolve.value() >= 1.0)
        {
            t.releaseImage(mPrevId);
            mPrevId = 0;
        }
    }

    void VideoPanel::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        const Rect box = boxRect();
        fillBox(t, box, palette::canvasBg());
        syncImages(t);
        const double show = mShow.value();
        if (show > 0.001 && mCurId)
        {
            t.save();
            t.clipRect(box.x, box.y, box.w, box.h);
            t.pushLayer(show);
            if (mPrevId && mDissolve.value() < 1.0)
            {
                t.drawImage(mPrevId, fit(mPrevW, mPrevH, box));
                t.pushLayer(mDissolve.value());
                t.drawImage(mCurId, pictureRect());
                t.popLayer();
            }
            else
                t.drawImage(mCurId, pictureRect());
            t.popLayer();
            t.restore();
        }
        // the words, under the picture: where the song is against it, or why there is none
        const double wa = 1.0 - show;
        if (wa > 0.001)
        {
            const double cy = box.y + box.h * 0.5;
            auto say = [&](const std::string &words, double a) {
                if (a <= 0.001 || words.empty()) return;
                const std::string fitS = textfit::ellipsize(t, ::arstro::solaris_ui::shown(words), box.w - 2 * space::padX(), 12.0, font::sans());
                const double tw = t.measureText(fitS, 12.0, font::sans());
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(fitS, (box.w - tw) * 0.5, cy + 12.0 * 0.35, 12.0, font::sans());
            };
            say(mOldWords, wa * (1.0 - mWordsMix.value()));
            say(mWords, wa * mWordsMix.value());
        }
        // the foot: the file, its size and rate; the picture's time
        const Rect foot{0, h - kFootH, w, kFootH};
        fillBox(t, foot, palette::card());
        fillBox(t, Rect{0, foot.y, w, 1.0}, palette::border());
        const double by = foot.y + kFootH * 0.5 + 10.0 * 0.35;
        const std::string time = clock(mVideo.id.empty() ? -1.0 : mVideo.time);
        const double tw = t.measureText(time, 10.0, font::mono());
        t.setFill(mVideo.time >= 0 ? palette::foreground() : palette::mutedForeground());
        t.drawText(time, w - space::padX() - tw, by, 10.0, font::mono());
        std::string about = "\xE2\x80\x94";
        if (!mVideo.id.empty())
        {
            about = fileName(mVideo.src);
            if (!mVideo.offline)
            {
                char buf[64];
                std::snprintf(buf, sizeof buf, " \xC2\xB7 %d\xC3\x97%d \xC2\xB7 %g fps", mVideo.width, mVideo.height, std::round(mVideo.fps * 100.0) / 100.0);
                about += buf;
            }
        }
        t.setFill(palette::mutedForeground());
        t.drawText(textfit::ellipsize(t, about, w - 3 * space::padX() - tw, 10.0, font::sans()), space::padX(), by, 10.0, font::sans());
    }
}
}
