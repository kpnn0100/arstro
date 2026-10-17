// Timeline — the song's picture (R-VIDEO-1; docs/discussion.md Round 18): a row between the ruler and the lanes, its span
// the picture's beats as the model puts them (through the tempo map — the lane needs none), thumbnails at the model's
// points a tile apart, decoded by the host a few a frame and kept. It grows in and out; a move made elsewhere eases there;
// dragged, the span is the pointer's — ONE `set <vd>.at=<beat>` on release.
#include "Timeline.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    namespace
    {
        constexpr double kPpq = 960.0; // a tick (R-TIME-1), as Timeline.cpp rounds
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
        long long keyOf(double time) { return std::llround(time * 1000.0); }
    }

    double Timeline::videoAtLive() const { return mVideoDragging ? mVideoDragAt : mVideoAt.value(); }
    double Timeline::videoShift() const { return videoAtLive() - mVideo.at; }

    Rect Timeline::videoSpanRect() const
    {
        if (mVideoShown.empty()) return Rect{0, 0, 0, 0};
        const double a = videoAtLive(), b = mVideoEnd.value() + (a - mVideoAt.value());
        const double x0 = beatToX(a), x1 = beatToX(b);
        return Rect{x0, videoTop() + kThumbPad, std::max(0.0, x1 - x0), std::max(0.0, mVideoH.value() - 2.0 * kThumbPad)};
    }

    double Timeline::thumbW() const
    {
        const double h = kVideoH - 2.0 * kThumbPad;
        return mVideo.width > 0 && mVideo.height > 0 ? h * mVideo.width / mVideo.height : h * 16.0 / 9.0;
    }

    std::vector<std::pair<double, const solaris::VideoThumbModel *>> Timeline::thumbTiles() const
    {
        // from the span's start, every point at least a tile past the last chosen — the same tiles wherever it scrolls
        std::vector<std::pair<double, const solaris::VideoThumbModel *>> out;
        const double shift = videoShift(), tw = thumbW(), W = width.value();
        double last = -std::numeric_limits<double>::infinity();
        for (const auto &th : mVideo.thumbs)
        {
            const double x = beatToX(th.beat + shift);
            if (x < last + tw - 0.5) continue;
            last = x;
            if (x + tw < kHeaderW || x > W) continue; // off screen: chosen, not drawn
            out.push_back({x, &th});
        }
        return out;
    }

    double Timeline::thumbAmount(double time) const
    {
        const auto it = mThumbs.find(keyOf(time));
        return it == mThumbs.end() ? -1.0 : it->second.a.value();
    }

    void Timeline::advanceVideo(double nowMs)
    {
        const bool want = !mVideo.id.empty();
        if (want != mVideoApplied)
        {
            mVideoH.animateTo(want ? kVideoH : 0.0, motion::kSlideMs, Easing::EaseOutCubic, nowMs);
            mVideoApplied = want;
        }
        mVideoH.update(nowMs);
        if (want)
        {
            if (mVideo.id != mVideoShown) // a picture arriving: placed where the model puts it — nowhere to slide from
            {
                mVideoShown = mVideo.id;
                mVideoAt.set(mVideo.at);
                mVideoEnd.set(mVideo.end);
                mVideoAtLast = mVideo.at;
                mVideoEndLast = mVideo.end;
            }
            // moved elsewhere (a shell, an undo, the tempo map): it eases there
            if (std::fabs(mVideo.at - mVideoAtLast) > 1e-9)
            {
                mVideoAt.animateTo(mVideo.at, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
                mVideoAtLast = mVideo.at;
            }
            if (std::fabs(mVideo.end - mVideoEndLast) > 1e-9)
            {
                mVideoEnd.animateTo(mVideo.end, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
                mVideoEndLast = mVideo.end;
            }
        }
        else if (mVideoH.value() <= 0.001)
            mVideoShown.clear(); // gone, and the row closed over it
        mVideoAt.update(nowMs);
        mVideoEnd.update(nowMs);
        // the thumbnails: another file, or another height, starts over
        const int th = (int)std::lround(kVideoH - 2.0 * kThumbPad);
        if (mVideo.path != mThumbFile || th != mThumbH)
        {
            for (auto &kv : mThumbs)
                if (kv.second.id) mThumbRelease.push_back(kv.second.id);
            mThumbs.clear();
            mThumbFile = mVideo.path;
            mThumbH = th;
        }
        // the tiles on screen not yet asked for — a few a frame, left to right
        if (want && !mVideo.offline && videoThumb)
        {
            int asked = 0;
            const int tw = (int)std::lround(thumbW());
            for (const auto &tile : thumbTiles())
            {
                const long long key = keyOf(tile.second->time);
                if (mThumbs.count(key)) continue;
                if (asked++ >= kThumbAsksPerFrame) break;
                Thumb &tb = mThumbs[key];
                VideoImage img;
                tb.ok = videoThumb(mVideo.path, tile.second->time, tw, th, img) && img.width > 0 && img.height > 0 &&
                        img.rgba.size() == (size_t)img.width * (size_t)img.height * 4;
                if (tb.ok)
                {
                    tb.rgba = std::move(img.rgba);
                    tb.w = img.width;
                    tb.h = img.height;
                    tb.a.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); // in as it arrives
                }
            }
        }
        for (auto &kv : mThumbs) kv.second.a.update(nowMs);
    }

    void Timeline::paintVideo(IRenderTarget &t) const
    {
        mThumbsDrawn = 0;
        const double h = mVideoH.value();
        if (mThumbOwner != &t) // another target: the ids held mean nothing there
        {
            mThumbOwner = &t;
            mThumbRelease.clear();
            for (const auto &kv : mThumbs) kv.second.id = 0;
        }
        for (int id : mThumbRelease) t.releaseImage(id);
        mThumbRelease.clear();
        if (h <= 0.5 || mVideoShown.empty()) return;
        const double W = width.value(), y0 = videoTop();
        t.save();
        t.clipRect(0, y0, W, h);
        // its ground, and its header: "Video", the file
        drawRoundedRect(t, Rect{kHeaderW, y0, W - kHeaderW, h}, 0.0, Paint::filled(surface::laneBg()));
        drawRoundedRect(t, Rect{0, y0, kHeaderW, h}, 0.0, Paint::filled(surface::headerBg()));
        t.setFill(palette::foreground());
        t.drawText(tr("Video"), space::padX(), y0 + 15.0, 11.0, font::sans());
        t.setFill(palette::mutedForeground());
        t.drawText(textfit::ellipsize(t, fileName(mVideo.src), kHeaderW - 2.0 * space::padX(), 10.0, font::sans()), space::padX(), y0 + 29.0, 10.0,
                   font::sans());
        // its span: the thumbnails, each fading in as the host decodes it; its name over them
        const Rect sr = videoSpanRect();
        t.save();
        t.clipRect(kHeaderW, y0, W - kHeaderW, h);
        drawRoundedRect(t, sr, radius::control(), Paint::filled(palette::card()));
        t.save();
        t.clipRect(sr.x, sr.y, sr.w, sr.h);
        const double tw = thumbW();
        for (const auto &tile : thumbTiles())
        {
            const auto it = mThumbs.find(keyOf(tile.second->time));
            if (it == mThumbs.end() || !it->second.ok || it->second.a.value() <= 0.001) continue;
            const Thumb &tb = it->second;
            if (!tb.id) tb.id = t.registerImage(tb.rgba.data(), tb.w, tb.h); // once: a thumbnail is kept
            if (!tb.id) continue;
            t.pushLayer(tb.a.value());
            t.drawImage(tb.id, Rect{tile.first, sr.y, tw, sr.h});
            t.popLayer();
            ++mThumbsDrawn;
        }
        t.restore();
        drawRoundedRect(t, sr, radius::control(), Paint::stroked(mVideoDragging ? palette::primary() : palette::border(), 1.0));
        const std::string name = textfit::ellipsize(t, fileName(mVideo.src) + (mVideo.offline ? "  \xC2\xB7  " + tr("cannot be read") : std::string()), std::max(0.0, sr.w - 12.0), 9.0,
                                                    font::sans());
        if (!name.empty())
        {
            const double nw = t.measureText(name, 9.0, font::sans());
            const double nx = std::max(sr.x, kHeaderW) + 3.0; // pinned at the left edge while the span runs on off-screen
            drawRoundedRect(t, Rect{nx, sr.y + 2.0, nw + 8.0, 13.0}, radius::control(), Paint::filled(surface::scrim(1.0)));
            t.setFill(palette::foreground());
            t.drawText(name, nx + 4.0, sr.y + 2.0 + 6.5 + 9.0 * 0.35, 9.0, font::sans());
        }
        t.restore();
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, y0 + h - 0.5); t.lineTo(W, y0 + h - 0.5); t.strokePath();
        t.restore();
    }

    bool Timeline::videoGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Down:
        {
            mVideoPressed = false;
            const Rect sr = videoSpanRect();
            if (mVideo.id.empty() || local.x < kHeaderW || local.y < videoTop() || local.y >= top() || !sr.contains(local)) return false;
            mVideoPressed = true;
            mVideoGrab = xToBeat(local.x) - mVideoAt.value(); // where it is DRAWN: a drag never teleports
            mVideoDragAt = mVideoAt.value();
            return true;
        }
        case Gesture::Type::DragStart:
        case Gesture::Type::Drag:
        {
            if (!mVideoPressed) return false;
            mVideoDragging = true;
            const double b = xToBeat(local.x) - mVideoGrab, step = snapStep(); // on the grid you see — before the song too
            mVideoDragAt = step > 0 ? std::round(b / step) * step : std::round(b * kPpq) / kPpq;
            return true;
        }
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
        {
            if (!mVideoPressed) return false;
            mVideoPressed = false;
            if (!mVideoDragging) return false; // a press not moved: nothing
            mVideoDragging = false;
            if (std::fabs(mVideoDragAt - mVideo.at) <= 0.5 / kPpq) return true;
            // where it was let go — the pointer was the animation — then the one line
            const double d = mVideoDragAt - mVideoAt.value();
            mVideoAt.set(mVideoDragAt);
            mVideoAtLast = mVideoDragAt;
            mVideoEnd.set(mVideoEnd.value() + d);
            mVideoEndLast = mVideoEnd.value();
            if (onCommand) onCommand("set " + mVideo.id + ".at=" + beatText(mVideoDragAt));
            return true;
        }
        default:
            return mVideoPressed; // the press's own: nothing under it hears it
        }
    }
}
}
