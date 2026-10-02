#include "GradeDeck.h"
#include "CommandLine.h"
#include "Glyphs.h"
#include "TextFit.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kPadX = 9.75;
        constexpr int kThumbEdge = 172;      // two cosmo cells' worth of pixels: sharp at 2x
        constexpr int kFrameEdge = 160;
        Color fade(Color c, double a) { c.a *= a; return c; }
    }

    GradeDeck::GradeDeck()
    {
        clipToBounds = true;
        mStrip = std::make_shared<cosmo_v2::Filmstrip>();
        // Cosmo's filmstrip selection: Shift-click a range, Ctrl-click to toggle.
        mStrip->onSelect = [this](int cell, bool shift, bool ctrl) {
            if (cell < 0 || cell >= (int)mRack.size()) return;
            const std::string b = cmd::quote(mRack[cell].bindName);
            if (shift) emit("rack select " + b + " --range");
            else if (ctrl) emit("rack select " + b + " --add");
            else
            {
                int selectedCount = 0;
                for (const auto &r : mRack) selectedCount += r.selected;
                if (cell != mSelected || selectedCount > 1) emit("rack select " + b);
            }
        };
        mStrip->onContext = [this](int cell, double x, double y) {
            if (cell >= 0 && cell < (int)mRack.size() && onContext)
                onContext(cell, Point{x, y});   // cosmo's filmstrip reports the gesture's world point
        };
        addChild(mStrip);
    }

    void GradeDeck::bind(const interstellar::AppModel &m)
    {
        mRack = m.rack;
        mFps = m.fps > 0 ? m.fps : 24.0;
        // Rebuild the cells (and their thumbnails) only when the rack's SHAPE or media changed —
        // the strip's thumb pool is indexed by slot and must be refilled in lockstep.
        std::string key;
        for (const auto &n : m.rack) key += n.rackObj + "|" + n.media + "|" + cmd::num(n.frame) + "|" + (n.pending ? "p" : "") + (n.failed ? "f" : "") + ";";
        const bool rebuild = key != mStructureKey;
        if (rebuild) mStrip->clearThumbs();
        std::vector<cosmo_v2::Filmstrip::Cell> cells;
        int slot = 0;
        for (int i = 0; i < (int)m.rack.size(); ++i)
        {
            const auto &n = m.rack[i];
            cosmo_v2::Filmstrip::Cell c;
            c.group = n.group;
            c.node = n.node;
            c.name = n.cosmoName.empty() ? n.bindName : n.cosmoName;
            c.bypassed = n.bypass;
            c.loading = n.pending;
            if (n.group)
            {
                for (const auto &k : m.rack) if (k.parent == i) ++c.count;
            }
            else if (!n.pending)   // a decoding cell has no thumb, so cosmo's spinner shows through
            {
                c.thumbSlot = slot++;
                if (rebuild)
                {
                    interstellar::Raster r;
                    const bool got = !n.failed && thumbnail && thumbnail(n.media, n.frame, kThumbEdge, r) && !r.empty();
                    if (!got)
                    {
                        // an honest plate: no pixels were offered, so none are invented
                        r.allocate(16, 9, 0);
                        for (size_t p = 0; p < r.rgba.size(); p += 4) { r.rgba[p] = r.rgba[p + 1] = r.rgba[p + 2] = 0x1A; r.rgba[p + 3] = 255; }
                    }
                    mStrip->addThumb(r.rgba.data(), r.width, r.height);
                }
            }
            cells.push_back(c);
        }
        mStructureKey = key;
        mStrip->setCells(cells);
        mSelected = m.selectedRack;
        if (mSelected >= 0 && mSelected < (int)cells.size())
        {
        {
            std::vector<int> sel;
            for (int k = 0; k < (int)mRack.size(); ++k)
                if (mRack[(size_t)k].selected) sel.push_back(k);
            if (std::find(sel.begin(), sel.end(), mSelected) == sel.end()) sel.push_back(mSelected);
            mStrip->setSelection(sel, mSelected);
        }
            mStrip->scrollCellIntoView(mSelected);
        }
        else
            mStrip->setSelection({}, -1);

        // the selected source's reference frame
        mSelVideo = false;
        mReason.clear();
        mSelMedia.clear();
        if (mSelected >= 0 && mSelected < (int)m.rack.size())
        {
            const auto &n = m.rack[mSelected];
            mSelName = n.cosmoName.empty() ? n.bindName : n.cosmoName;
            if (n.group) mReason = "A group grades every source under it \xE2\x80\x94 no reference frame to pick.";
            else if (!n.video) mReason = "A still is graded as it is \xE2\x80\x94 no reference frame to pick.";
            else if (n.failed) mReason = "Offline \xE2\x80\x94 the reference frame cannot be read.";
            else
            {
                mSelVideo = true;
                mSelMedia = n.media;
                mSrcFps = n.mediaFps > 0 ? n.mediaFps : mFps;
                // the WHOLE source once the service has opened it; until then, what the cut uses
                double dur = n.mediaDuration;
                if (dur <= 0.0) for (const auto &c : m.clips) if (c.src == n.rackObj) dur = std::max(dur, c.out);
                if (dur <= 0.0) dur = std::max(m.duration, n.frame + 1.0);
                mSourceDur = std::max(1.0 / mSrcFps, dur);
                if (!mDragging) mFrameTarget = n.frame;   // a gesture in flight outranks the model
            }
        }
        else
            mReason = m.rack.empty() ? std::string() : "Select a source to choose the frame it is graded on.";
        mSelectorWanted = mSelVideo;
    }

    void GradeDeck::layout()
    {
        mStrip->x.set(0.0);
        mStrip->y.set(kHeaderH);
        mStrip->width.set(width.value());
        mStrip->height.set(cosmo_v2::Filmstrip::kHeight);
    }

    bool GradeDeck::hasFrameStrip() const { return height.value() - bandTop() - kBandLineH - 8.0 >= kMinStripH; }

    Rect GradeDeck::frameTrackRect() const
    {
        const double top = bandTop();
        if (hasFrameStrip())
        {
            const double h = std::min(kMaxStripH, height.value() - top - kBandLineH - 8.0);
            return Rect{kPadX, top + kBandLineH, std::max(0.0, width.value() - 2 * kPadX), h};
        }
        const double x0 = kPadX + 205.0;   // after the label, ‹ timecode ›, measured generously
        return Rect{x0, top, std::max(0.0, width.value() - kPadX - x0), std::max(0.0, std::min(kBandLineH, height.value() - top))};
    }

    double GradeDeck::frameToX(double t) const
    {
        const Rect r = frameTrackRect();
        return r.x + std::clamp(t / mSourceDur, 0.0, 1.0) * r.w;
    }

    void GradeDeck::refreshFrames()
    {
        // One thumbnail per strip cell, across the source's length. Rebuilt only when the media, the
        // cell count (a resize) or the length changed — the hook is promised "once per (path, t)".
        if (!mSelVideo || !hasFrameStrip()) return;
        const Rect r = frameTrackRect();
        const double cellW = r.h * 16.0 / 9.0;
        const int n = std::max(1, (int)std::floor(r.w / std::max(8.0, cellW)));
        const std::string key = mSelMedia + "|" + std::to_string(n) + "|" + cmd::num(mSourceDur);
        const bool retry = mFramesRetry && key == mFramesKey && (int)mFrames.size() == n;
        mFramesRetry = false;
        if (key == mFramesKey && !retry) return;
        if (!retry)
        {
            mFramesKey = key;
            if (!mFramesMedia.empty() && mFramesMedia != mSelMedia) mStripFadePending = true;   // a content change, not a resize
            mFramesMedia = mSelMedia;
            mFrames.assign(n, ImageSlot{});
            mFrameAlpha.clear();
            for (int i = 0; i < n; ++i) mFrameAlpha.emplace_back(new AnimatedProperty(1.0));
            mFrameFade.assign(n, false);
        }
        if (!thumbnail) return;
        for (int i = 0; i < n; ++i)
        {
            if (mFrames[i].has()) continue;
            interstellar::Raster ras;
            if (thumbnail(mSelMedia, (i + 0.5) / n * mSourceDur, kFrameEdge, ras) && !ras.empty())
            {
                mFrames[i].set(ras);
                // A frame the host had to decode lands later than its neighbours: it fades in.
                if (retry) { mFrameAlpha[i]->set(0.0); mFrameFade[i] = true; }
            }
        }
    }

    /** A source time on the source's own frame grid, inside the source (the last frame starts one
     *  frame before the end). */
    double GradeDeck::snapToFrame(double t) const
    {
        const double last = std::max(0.0, std::floor(mSourceDur * mSrcFps - 1e-6) / mSrcFps);
        return std::clamp(std::round(t * mSrcFps) / mSrcFps, 0.0, last);
    }

    void GradeDeck::preview(double t)
    {
        if (mSelected < 0 || mSelected >= (int)mRack.size()) return;
        if (std::fabs(t - mFrameLast) > 1e-9 || t < 0) { if (onPreview) onPreview(mRack[mSelected].bindName, t); }
    }

    /** ‹ › — one frame at the source's own rate, committed at once: the exact frame. */
    void GradeDeck::step(int dir)
    {
        if (!mSelVideo || mSelected < 0 || mSelected >= (int)mRack.size()) return;
        const double from = mRack[mSelected].frame;
        const double to = snapToFrame(from + dir / mSrcFps);
        if (std::fabs(to - snapToFrame(from)) < 1e-9) return;   // already at that end
        emit("rack frame " + cmd::quote(mRack[mSelected].bindName) + " --at " + cmd::seconds(to, mSrcFps));
    }

    bool GradeDeck::handleGesture(const Gesture &g, const Point &local)
    {
        const Rect tr = frameTrackRect();
        auto timeAt = [&](double x) { return tr.w > 0 ? std::clamp((x - tr.x) / tr.w, 0.0, 1.0) * mSourceDur : 0.0; };
        auto stepAt = [&](const Point &p) {
            if (!mSelVideo || mSelectorAmt.value() < 0.5) return 0;
            return mStepRect[0].contains(p) ? -1 : (mStepRect[1].contains(p) ? 1 : 0);
        };
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            mTrackHovered = mSelVideo && tr.contains(local);
            const int sd = stepAt(local);
            mStepHover.setHovered(sd < 0 ? 0 : (sd > 0 ? 1 : -1));
            return true;
        }
        case Gesture::Type::Click:
            if (const int sd = stepAt(local)) { step(sd); return true; }
            break;
        case Gesture::Type::Down:
            if (mSelVideo && tr.contains(local))
            {
                mDragging = true;
                const double t = snapToFrame(timeAt(local.x));
                preview(t);
                mFrameTarget = t;
                mShownFrame.set(mFrameTarget);   // direct manipulation: under the pointer
                mFrameLast = mFrameTarget;
            }
            return true;
        case Gesture::Type::DragStart:
        case Gesture::Type::Drag:
            if (mDragging)
            {
                const double t = snapToFrame(timeAt(local.x));   // land on a frame of the source
                preview(t);                                       // the monitor shows it, graded
                mFrameTarget = t;
                mShownFrame.set(mFrameTarget);
                mFrameLast = mFrameTarget;
            }
            return true;
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
            if (mDragging)
            {
                mDragging = false;
                if (mSelected >= 0 && mSelected < (int)mRack.size())
                {
                    emit("rack frame " + cmd::quote(mRack[mSelected].bindName) + " --at " + cmd::seconds(mFrameTarget, mSrcFps));
                    if (onPreview) onPreview(mRack[mSelected].bindName, -1.0);   // the commit is what shows now
                }
            }
            return true;
        case Gesture::Type::Scroll:
            // cosmo's strip scrolls through scrollBy(notches × one cell) and does not take the
            // gesture itself, so it bubbles here. Elsewhere in the deck there is nothing to scroll.
            if (local.y >= kHeaderH && local.y <= kHeaderH + cosmo_v2::Filmstrip::kHeight)
            {
                mStrip->scrollBy(-g.delta.y / shell::wheelNotchPx() * cosmo_v2::Filmstrip::kWheelStep);
                return true;
            }
            return false;
        default:
            break;
        }
        return Segment::handleGesture(g, local);
    }

    void GradeDeck::advance(double nowMs)
    {
        for (size_t i = 0; i < mFrameAlpha.size(); ++i)
        {
            if (i < mFrameFade.size() && mFrameFade[i])
            {
                mFrameAlpha[i]->animateTo(1.0, motion::kScrollMs, Easing::EaseOutCubic, nowMs);
                mFrameFade[i] = false;
            }
            mFrameAlpha[i]->update(nowMs);
        }
        if (!mSelectorInit)
        {
            mSelectorAmt.set(mSelectorWanted ? 1.0 : 0.0);
            mSelectorApplied = mSelectorWanted;
            mShownFrame.set(mFrameTarget);
            mFrameLast = mFrameTarget;
            mSelectorInit = true;
        }
        if (mSelectorWanted != mSelectorApplied)
        {
            mSelectorAmt.animateTo(mSelectorWanted ? 1.0 : 0.0, motion::kScrollMs, Easing::EaseOutCubic, nowMs);
            mSelectorApplied = mSelectorWanted;
        }
        mSelectorAmt.update(nowMs);
        if (!mDragging && mFrameTarget != mFrameLast)
        {
            mShownFrame.animateTo(mFrameTarget, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
            mFrameLast = mFrameTarget;
        }
        mShownFrame.update(nowMs);
        if (!isHovered()) mTrackHovered = false;
        if (mTrackHovered != mTrackHoverApplied)
        {
            mTrackHover.animateTo(mTrackHovered ? 1.0 : 0.0, motion::kHoverMs, Easing::EaseOutCubic, nowMs);
            mTrackHoverApplied = mTrackHovered;
        }
        mTrackHover.update(nowMs);
        if (!isHovered()) mStepHover.clear();
        mStepHover.advance(nowMs);
        refreshFrames();
        if (mStripFadePending)
        {
            mStripFade.set(0.0);
            mStripFade.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mStripFadePending = false;
        }
        mStripFade.update(nowMs);
        Segment::advance(nowMs);
    }

    void GradeDeck::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(surface::deckBg()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, 0.5); t.lineTo(w, 0.5); t.strokePath();

        // header row: SOURCES · n ───
        const double hy = kHeaderH * 0.5;
        t.setFill(palette::mutedForeground());
        t.drawText("SOURCES", kPadX, textfit::baseline(hy, 9.0), 9.0, font::sansSemiBold(), 0.13 * 9.0);
        const double lw = t.measureText("SOURCES", 9.0, font::sansSemiBold(), 0.13 * 9.0);
        int sources = 0;
        for (const auto &n : mRack) if (!n.group) ++sources;
        const std::string count = std::to_string(sources);
        t.setFill(fade(palette::mutedForeground(), 0.7));
        t.drawText(count, kPadX + lw + 6.0, textfit::baseline(hy, 9.0), 9.0, font::mono());
        const double rx = kPadX + lw + 6.0 + t.measureText(count, 9.0, font::mono()) + 6.5;
        if (w - kPadX > rx)
        {
            t.setStroke(palette::border(), 1.0);
            t.beginPath(); t.moveTo(rx, hy); t.lineTo(w - kPadX, hy); t.strokePath();
        }
        if (mRack.empty())
        {
            const std::string s = "Sources appear here as footage joins the rack.";
            t.setFill(palette::mutedForeground());
            t.drawText(s, (w - t.measureText(s, 11.0, font::sans())) * 0.5, kHeaderH + cosmo_v2::Filmstrip::kHeight * 0.5 + 4.0, 11.0, font::sans());
        }

        // the reference-frame band
        const double top = bandTop();
        if (h - top < 12.0) return;
        const double lineCy = top + std::min(kBandLineH, h - top) * 0.5;
        const double sa = mSelectorAmt.value();
        const Rect tr = frameTrackRect();
        const bool strip = hasFrameStrip();
        if (sa > 0.001)
        {
            // label line: REF FRAME  ‹ 00:00:02:12 › · <source> ……… length
            t.setFill(fade(palette::mutedForeground(), sa));
            t.drawText("REF FRAME", kPadX, textfit::baseline(lineCy, 9.0), 9.0, font::sansSemiBold(), 0.13 * 9.0);
            const double lx = kPadX + t.measureText("REF FRAME", 9.0, font::sansSemiBold(), 0.13 * 9.0) + 6.5;
            // ‹ › step one source frame (R-RACK-3): 16 px targets around the mono timecode
            constexpr double kStep = 16.0;
            const std::string tc = cmd::timecode(mShownFrame.value(), mSrcFps);
            const double tcW = t.measureText(tc, 10.0, font::mono());
            mStepRect[0] = Rect{lx, lineCy - kStep * 0.5, kStep, kStep};
            mStepRect[1] = Rect{lx + kStep + 3.0 + tcW + 3.0, lineCy - kStep * 0.5, kStep, kStep};
            for (int k = 0; k < 2; ++k)
            {
                const double hvk = mStepHover.amount(k);
                if (hvk > 0.001) drawRoundedRect(t, mStepRect[k], radius::control(), Paint::filled(fade(palette::hoverWash(hvk), sa)));
                const Color gc = fade(lerpColor(palette::mutedForeground(), palette::foreground(), hvk), sa);
                const Rect gr{mStepRect[k].x + 3.0, mStepRect[k].y + 3.0, kStep - 6.0, kStep - 6.0};
                if (k == 0) glyph::chevronLeft(t, gr, gc); else glyph::chevronRightSmall(t, gr, gc);
            }
            t.setFill(fade(palette::foreground(), sa));
            t.drawText(tc, lx + kStep + 3.0, textfit::baseline(lineCy, 10.0), 10.0, font::mono());
            const double nx = mStepRect[1].right() + 8.0;
            const std::string len = "of " + cmd::timecode(mSourceDur, mSrcFps);
            const double lenW = t.measureText(len, 9.0, font::mono());
            if (strip)
            {
                t.setFill(fade(palette::mutedForeground(), 0.8 * sa));
                t.drawText(len, w - kPadX - lenW, textfit::baseline(lineCy, 9.0), 9.0, font::mono());
                t.setFill(fade(palette::mutedForeground(), sa));
                t.drawText(textfit::ellipsize(t, mSelName + (mDragging ? " \xC2\xB7 previewing in the monitor \xE2\x80\x94 release to grade on this frame"
                                                                    : " \xC2\xB7 drag to seek, \xE2\x80\xB9 \xE2\x80\xBA for the exact frame"),
                                              w - kPadX - lenW - 12.0 - nx, 10.0, font::sans()),
                           nx, textfit::baseline(lineCy, 10.0), 10.0, font::sans());
            }
            const double hv = mTrackHover.value();
            const double x = frameToX(mShownFrame.value());
            if (strip)
            {
                // the strip of frames: thumbnails across the source, dimmed except under the marker
                const int n = (int)mFrames.size();
                drawRoundedRect(t, tr, radius::control(), Paint::filled(fade(surface::thumbPlaceholder(), sa)));
                t.save();
                t.clipRect(tr.x, tr.y, tr.w, tr.h);
                for (int i = 0; i < n; ++i)
                {
                    const Rect cell{tr.x + tr.w * i / n, tr.y, tr.w / n - 1.0, tr.h};
                    if (mFrames[i].has())
                    {
                        const double fa = i < (int)mFrameAlpha.size() ? mFrameAlpha[(size_t)i]->value() : 1.0;
                        t.pushLayer(sa * mStripFade.value() * (0.55 + 0.25 * hv) * fa);
                        mFrames[i].drawCover(t, cell);
                        t.popLayer();
                    }
                    else
                        glyph::film(t, Rect{cell.x + cell.w * 0.5 - 8.0, cell.y + cell.h * 0.5 - 6.0, 16.0, 12.0}, fade(palette::mutedForeground(), 0.4 * sa), 1.0);
                }
                t.restore();
                drawRoundedRect(t, tr, radius::control(), Paint::stroked(fade(lerpColor(palette::border(), palette::whiteAlpha(0.2), hv), sa), 1.0));
                // the slider's thumb: the reference frame, in the accent — a line through the strip
                // and cosmo's white slider knob on it, growing a little under the pointer
                drawRoundedRect(t, Rect{x - 1.0, tr.y - 3.0, 2.0, tr.h + 6.0}, radius::hairline(), Paint::filled(fade(palette::primary(), sa)));
                drawRoundedRect(t, Rect{x - 5.0, tr.y - 5.0, 10.0, 6.0}, radius::hairline(), Paint::filled(fade(palette::primary(), sa)));
                const double knob = 5.0 + 1.0 * std::max(hv, mDragging ? 1.0 : 0.0);
                drawCircle(t, x, tr.y + tr.h * 0.5, knob + 1.5, Paint::filled(fade(palette::primary(), sa)));
                drawCircle(t, x, tr.y + tr.h * 0.5, knob, Paint::filled(fade(palette::white(), sa)));
            }
            else
            {
                const double cy = tr.y + tr.h * 0.5;
                const double th = 3.0 + hv;
                drawRoundedRect(t, Rect{tr.x, cy - th * 0.5, tr.w, th}, radius::pill(), Paint::filled(fade(palette::secondary(), sa)));
                if (x - tr.x >= th)
                    drawRoundedRect(t, Rect{tr.x, cy - th * 0.5, x - tr.x, th}, radius::pill(), Paint::filled(fade(palette::primary(), sa)));
                drawCircle(t, x, cy, 4.5 + hv, Paint::filled(fade(palette::white(), sa)));
            }
        }
        if (sa < 0.999 && !mReason.empty())
        {
            const double ra = 1.0 - sa;
            const double cy = strip ? tr.y + tr.h * 0.5 : lineCy;
            const std::string s = textfit::ellipsize(t, mReason, w - 2 * kPadX, 10.0, font::sans());
            t.setFill(fade(palette::mutedForeground(), ra));
            t.drawText(s, (w - t.measureText(s, 10.0, font::sans())) * 0.5, textfit::baseline(cy, 10.0), 10.0, font::sans());
        }
    }

    void GradeDeck::onOverlay(IRenderTarget &t) const
    {
        // OFFLINE chips over the cells of missing sources (cosmo's strip has no word for it)
        t.save();
        t.clipRect(0, kHeaderH, width.value(), cosmo_v2::Filmstrip::kHeight);
        for (int i = 0; i < (int)mRack.size(); ++i)
        {
            if (!mRack[i].failed) continue;
            const double x = mStrip->cellXForTest(i);
            const double cellY = kHeaderH + (cosmo_v2::Filmstrip::kHeight - 62.0) * 0.5;
            const std::string s = "OFFLINE";
            const double tw = t.measureText(s, 7.5, font::sansSemiBold());
            const Rect chip{x + (86.0 - tw - 8.0) * 0.5, cellY + 20.0, tw + 8.0, 13.0};
            drawRoundedRect(t, chip, radius::hairline(), Paint::filledStroked(surface::scrim(0.8), palette::destructive(), 1.0));
            t.setFill(palette::destructive());
            t.drawText(s, chip.x + 4.0, textfit::baseline(chip.y + chip.h * 0.5, 7.5), 7.5, font::sansSemiBold());
        }
        t.restore();
    }
}
}
