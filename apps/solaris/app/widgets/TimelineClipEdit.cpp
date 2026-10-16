// solaris_ui — Timeline: audio editing on a clip (R-EDIT-1). Its fades by the two handles under its name, its gain by
// the line across it — each dragged follows the pointer, let go it is ONE `set`; the fades drawn as HEARD, so an
// overlap on its lane shows its crossfade. Its menu's lines: split at the time bar, reverse, consolidate the clips it
// runs into on its lane, slice. Every value drawn is eased (a refused drag eases back).
#include "Timeline.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;

    namespace
    {
        constexpr double kPpq = 960.0;
        constexpr double kBodyTop = 15.0;      // under the name: where the handles sit and the fades begin
        constexpr double kBodyFoot = 3.0;
        constexpr double kHandleGrip = 6.5;    // space::u(2), as a clip's end
        constexpr double kHandleGripY = 4.875; // space::u(1.5): above a press at the clip's middle
        constexpr double kHandleHalf = 2.5;    // a 5 px square
        constexpr double kGainGrip = 3.25;     // space::u(1)
        constexpr double kAmpMax = 3.9810717055349722; // +12 dB, a clip's loudest: the line at its body's top
        Color fade(Color c, double a) { c.a *= a; return c; }
        double toTick(double b) { return std::round(b * kPpq) / kPpq; }
        double ampOf(double db) { return db <= -119.999 ? 0.0 : std::pow(10.0, db / 20.0); }
        double dbOf(double amp) { return amp <= 1e-6 ? -120.0 : std::clamp(20.0 * std::log10(amp), -120.0, 12.0); }
        std::string dbText(double db)
        {
            char b[32];
            std::snprintf(b, sizeof b, "%.1f", std::round(db * 10.0) / 10.0);
            return std::string(std::strcmp(b, "-0.0") == 0 ? "0.0" : b);
        }
        /** An overlap's part of a heard fade: what the model hears beyond the clip's own, else nothing. */
        double overlapPart(double heard, double own) { return heard > own + 1e-9 ? heard : 0.0; }
    }

    bool Timeline::clipEditable(const ClipLive &l, const Rect &r) const
    {
        return !l.gone && l.v.c.kind == "audio" && r.w > 4.0 * kEdgeGrip && l.frac.value() > 0.999 && r.h > kBodyTop + kBodyFoot + 8.0;
    }

    void Timeline::placeClipEdit(ClipLive &l)
    {
        const auto &c = l.v.c;
        l.fin.set(c.fadeIn);
        l.fout.set(c.fadeOut);
        l.hin.set(c.fadeInHeard);
        l.hout.set(c.fadeOutHeard);
        l.amp.set(ampOf(c.gain));
        l.rev.set(c.reverse ? 1.0 : 0.0);
        l.finLast = c.fadeIn;
        l.foutLast = c.fadeOut;
        l.hinLast = c.fadeInHeard;
        l.houtLast = c.fadeOutHeard;
        l.ampLast = ampOf(c.gain);
        l.revLast = c.reverse ? 1.0 : 0.0;
        l.tagFrom.clear();
        l.tagTo = clipTag(c);
        l.tagT.set(1.0);
    }

    std::string Timeline::clipTag(const solaris::ClipModel &c)
    {
        auto num = [](double v) {
            char b[32];
            std::snprintf(b, sizeof b, "%.6g", std::round(v * 1000.0) / 1000.0);
            return std::string(b);
        };
        std::string s;
        if (c.warp && c.bpm > 0) s = num(c.bpm) + " bpm";
        if (c.pitch != 0.0) s += (s.empty() ? "" : " \xC2\xB7 ") + std::string(c.pitch > 0 ? "+" : "\xE2\x88\x92") + num(std::fabs(c.pitch)) + " st";
        return s;
    }

    double Timeline::clipTagAlpha(const std::string &ac, const std::string &text) const
    {
        const ClipLive *l = live(ac);
        if (!l || text.empty()) return 0.0;
        const double t = l->tagT.value();
        return (l->tagTo == text ? t : 0.0) + (l->tagFrom == text ? 1.0 - t : 0.0);
    }

    void Timeline::advanceClipEdit(ClipLive &l, double nowMs)
    {
        const auto &c = l.v.c;
        auto ease = [nowMs](AnimatedProperty &p, double &last, double want) {
            if (want != last) { p.animateTo(want, motion::kSelectMs, Easing::EaseOutCubic, nowMs); last = want; }
            p.update(nowMs);
        };
        ease(l.fin, l.finLast, c.fadeIn);
        ease(l.fout, l.foutLast, c.fadeOut);
        ease(l.hin, l.hinLast, c.fadeInHeard);
        ease(l.hout, l.houtLast, c.fadeOutHeard);
        ease(l.amp, l.ampLast, ampOf(c.gain));
        ease(l.rev, l.revLast, c.reverse ? 1.0 : 0.0);
        const std::string tag = clipTag(c);
        if (tag != l.tagTo)
        {
            // from what is SHOWN: a change mid-fade leaves the nearer of the two
            l.tagFrom = l.tagT.value() >= 0.5 ? l.tagTo : l.tagFrom;
            l.tagTo = tag;
            l.tagT.set(0.0);
            l.tagT.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
        }
        l.tagT.update(nowMs);
    }

    double Timeline::clipFadeLive(const std::string &ac, bool out) const
    {
        if (mEditMoved && mEditClip == ac && mEditZone == (out ? 2 : 1)) return mEditValue;
        const ClipLive *l = live(ac);
        return l ? (out ? l->fout.value() : l->fin.value()) : 0.0;
    }

    double Timeline::clipHeardFadeLive(const std::string &ac, bool out) const
    {
        const ClipLive *l = live(ac);
        if (!l) return 0.0;
        if (mEditMoved && mEditClip == ac && mEditZone == (out ? 2 : 1)) // the pointer's, with what the overlap holds
            return std::max(mEditValue, out ? overlapPart(l->v.c.fadeOutHeard, l->v.c.fadeOut) : overlapPart(l->v.c.fadeInHeard, l->v.c.fadeIn));
        return out ? l->hout.value() : l->hin.value();
    }

    double Timeline::clipGainLive(const std::string &ac) const
    {
        if (mEditMoved && mEditClip == ac && mEditZone == 3) return mEditValue;
        const ClipLive *l = live(ac);
        return l ? dbOf(l->amp.value()) : 0.0;
    }

    double Timeline::clipReversedLive(const std::string &ac) const
    {
        const ClipLive *l = live(ac);
        return l ? l->rev.value() : 0.0;
    }

    double Timeline::gainLineY(const std::string &ac) const
    {
        const ClipLive *l = live(ac);
        const Rect r = clipRect(ac);
        if (!l || l->v.c.kind != "audio" || r.w <= 0) return std::nan("");
        const double top = r.y + kBodyTop, bottom = r.bottom() - kBodyFoot;
        const double amp = mEditMoved && mEditClip == ac && mEditZone == 3 ? ampOf(mEditValue) : l->amp.value();
        return bottom - (bottom - top) * std::clamp(amp / kAmpMax, 0.0, 1.0);
    }

    Rect Timeline::fadeHandleRect(const std::string &ac, bool out) const
    {
        const ClipLive *l = live(ac);
        const Rect r = clipRect(ac);
        if (!l || !clipEditable(*l, r)) return Rect{};
        const double ppb = mPpb.value(), f = clipFadeLive(ac, out);
        const double x = out ? r.right() - f * ppb : r.x + f * ppb;
        return Rect{x - kHandleHalf, r.y + kBodyTop - kHandleHalf, 2.0 * kHandleHalf, 2.0 * kHandleHalf};
    }

    int Timeline::clipEditZoneAt(const Point &p, std::string &id) const
    {
        id = clipAt(p);
        const ClipLive *l = live(id);
        const Rect r = clipRect(id);
        if (!l || !clipEditable(*l, r)) return 0;
        const double ppb = mPpb.value(), hy = r.y + kBodyTop;
        const double xi = r.x + l->fin.value() * ppb, xo = r.right() - l->fout.value() * ppb;
        if (std::fabs(p.y - hy) <= kHandleGripY)
        {
            // the nearer handle where both are in reach (a clip faded all through)
            const double di = std::fabs(p.x - xi), d0 = std::fabs(p.x - xo);
            if (di <= kHandleGrip && di <= d0) return 1;
            if (d0 <= kHandleGrip) return 2;
        }
        const double gy = gainLineY(id);
        if (std::fabs(p.y - gy) <= kGainGrip && p.x > xi + kHandleGrip && p.x < std::min(xo - kHandleGrip, r.right() - kEdgeGrip)) return 3;
        return 0;
    }

    bool Timeline::clipEditGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Down:
        {
            mEditMoved = false;
            mEditZone = clipEditZoneAt(local, mEditClip);
            if (mEditZone == 0) { mEditClip.clear(); return false; }
            const ClipLive *l = live(mEditClip);
            mEditValue = mEditZone == 1 ? l->v.c.fadeIn : mEditZone == 2 ? l->v.c.fadeOut : l->v.c.gain;
            mPressClip.clear();
            mEdgeClip.clear();
            return true;
        }
        case Gesture::Type::DragStart:
        case Gesture::Type::Drag:
        {
            if (mEditZone == 0) return false;
            const ClipLive *l = live(mEditClip);
            if (!l) { mEditZone = 0; return true; }
            if (!mEditMoved) selectClip(mEditClip);
            mEditMoved = true;
            // the pointer is the animation: the value it points at — from the clip as DRAWN, where the handle is — as the
            // service will store it
            const Rect r = clipRect(mEditClip);
            const double len = liveLength(*l), ppb = std::max(1e-9, mPpb.value());
            if (mEditZone == 1) mEditValue = toTick(std::clamp((local.x - r.x) / ppb, 0.0, std::max(0.0, len - l->v.c.fadeOut)));
            else if (mEditZone == 2) mEditValue = toTick(std::clamp((r.right() - local.x) / ppb, 0.0, std::max(0.0, len - l->v.c.fadeIn)));
            else
            {
                const double top = r.y + kBodyTop, bottom = r.bottom() - kBodyFoot;
                const double amp = std::clamp((bottom - local.y) / std::max(1.0, bottom - top), 0.0, 1.0) * kAmpMax;
                mEditValue = std::round(dbOf(amp) * 10.0) / 10.0;
            }
            return true;
        }
        case Gesture::Type::Drop:
        case Gesture::Type::Up:
        {
            if (mEditZone == 0) return false;
            const int zone = mEditZone;
            mEditZone = 0;
            if (!mEditMoved) { mEditClip.clear(); return false; } // a press with no drag: the click is the clip's
            mEditMoved = false;
            std::string send;
            for (auto &l : mLive)
                if (!l.gone && l.v.c.id == mEditClip)
                {
                    // where it was let go — the pointer was the animation — then ONE line; a refusal eases it back
                    const auto &c = l.v.c;
                    if (zone == 1)
                    {
                        l.fin.set(mEditValue);
                        l.finLast = mEditValue;
                        l.hinLast = std::max(mEditValue, overlapPart(c.fadeInHeard, c.fadeIn));
                        l.hin.set(l.hinLast);
                        if (std::fabs(mEditValue - c.fadeIn) > 1e-9) send = "set " + c.id + ".fadeIn=" + beatText(mEditValue);
                    }
                    else if (zone == 2)
                    {
                        l.fout.set(mEditValue);
                        l.foutLast = mEditValue;
                        l.houtLast = std::max(mEditValue, overlapPart(c.fadeOutHeard, c.fadeOut));
                        l.hout.set(l.houtLast);
                        if (std::fabs(mEditValue - c.fadeOut) > 1e-9) send = "set " + c.id + ".fadeOut=" + beatText(mEditValue);
                    }
                    else
                    {
                        l.amp.set(ampOf(mEditValue));
                        l.ampLast = ampOf(mEditValue);
                        if (std::fabs(mEditValue - c.gain) > 1e-9) send = "set " + c.id + ".gain=" + dbText(mEditValue);
                    }
                    break;
                }
            mEditClip.clear();
            if (!send.empty() && onCommand) onCommand(send); // after the loop: the model it brings back re-keys mLive
            return true;
        }
        default:
            return false;
        }
    }

    void Timeline::paintClipEdit(IRenderTarget &t, const ClipView &v, const Rect &r, double a, const Color &hue) const
    {
        const ClipLive *l = live(v.c.id);
        const double ppb = mPpb.value(), top = r.y + kBodyTop, bottom = r.bottom() - kBodyFoot;
        // the gain line: its height the amplitude, 0 dB a quarter of the way up (+12 dB at the top)
        const double gy = l ? gainLineY(v.c.id) : bottom - (bottom - top) * std::clamp(ampOf(v.c.gain) / kAmpMax, 0.0, 1.0);
        const bool gainHeld = mEditMoved && mEditClip == v.c.id && mEditZone == 3;
        t.setStroke(fade(hue, (gainHeld ? 1.0 : 0.6) * a), gainHeld ? 1.5 : 1.0);
        t.beginPath(); t.moveTo(r.x + 4.0, gy); t.lineTo(r.right() - 4.0, gy); t.strokePath();
        // the fades as heard (a crossfade an overlap makes included): what each takes away shaded, a line along it
        const double hin = l ? clipHeardFadeLive(v.c.id, false) : v.c.fadeInHeard, hout = l ? clipHeardFadeLive(v.c.id, true) : v.c.fadeOutHeard;
        if (hin * ppb > 0.5)
        {
            const double x1 = std::min(r.right(), r.x + hin * ppb);
            t.setFill(fade(palette::background(), 0.45 * a));
            t.beginPath(); t.moveTo(r.x, top); t.lineTo(x1, top); t.lineTo(r.x, bottom); t.closePath(); t.fillPath();
            t.setStroke(fade(palette::foreground(), 0.55 * a), 1.0);
            t.beginPath(); t.moveTo(r.x, bottom); t.lineTo(x1, top); t.strokePath();
        }
        if (hout * ppb > 0.5)
        {
            const double x0 = std::max(r.x, r.right() - hout * ppb);
            t.setFill(fade(palette::background(), 0.45 * a));
            t.beginPath(); t.moveTo(x0, top); t.lineTo(r.right(), top); t.lineTo(r.right(), bottom); t.closePath(); t.fillPath();
            t.setStroke(fade(palette::foreground(), 0.55 * a), 1.0);
            t.beginPath(); t.moveTo(x0, top); t.lineTo(r.right(), bottom); t.strokePath();
        }
        // the handles — the clip's own fades — where it is wide enough to take them
        if (l && clipEditable(*l, r))
            for (bool out : {false, true})
            {
                const Rect h = fadeHandleRect(v.c.id, out);
                const bool held = mEditMoved && mEditClip == v.c.id && mEditZone == (out ? 2 : 1);
                drawRoundedRect(t, h, radius::hairline(), Paint::filled(fade(palette::foreground(), (held ? 1.0 : 0.7) * a)));
            }
        // reversed at the right, its tempo and pitch at the left: just above its gain line (below it when the line is
        // high), never on it
        const double base = gy - 3.25 - 9.0 >= top ? gy - 3.25 : std::min(bottom, gy + 3.25 + 9.0 * 0.7);
        const double rev = l ? l->rev.value() : (v.c.reverse ? 1.0 : 0.0);
        double rightUsed = 0.0;
        if (rev > 0.001)
        {
            const std::string s = "reversed";
            const double w = t.measureText(s, 9.0, font::sans());
            if (r.w > w + 60.0)
            {
                t.setFill(fade(palette::mutedForeground(), rev * a));
                t.drawText(s, r.right() - w - 6.0, base, 9.0, font::sans());
                rightUsed = w + 12.0;
            }
        }
        const std::string from = l ? l->tagFrom : std::string(), to = l ? l->tagTo : clipTag(v.c);
        const double tt = l ? l->tagT.value() : 1.0;
        const double tagX = std::max(std::max(r.x, kHeaderW), r.x + hin * ppb) + 5.0; // past its fade in, never on its line
        for (int k = 0; k < 2; ++k)
        {
            const std::string &s = k == 0 ? from : to;
            const double al = k == 0 ? 1.0 - tt : tt;
            if (s.empty() || al <= 0.001) continue;
            const double room = r.right() - 5.0 - rightUsed - tagX;
            if (room < 24.0) continue;
            t.setFill(fade(palette::foreground(), 0.7 * al * a));
            t.drawText(interstellar_v1::textfit::ellipsize(t, s, room, 9.0, font::mono()), tagX, base, 9.0, font::mono());
        }
    }

    void Timeline::clipEditItems(const solaris::ClipModel &c, std::vector<cosmo_v2::ContextMenu::Item> &items, const Point &world)
    {
        if (c.kind != "audio") return;
        const std::string id = c.id;
        const double pos = mPosition;
        if (pos > c.at + 1e-9 && pos < c.at + c.length - 1e-9) // R-EDIT-1: where the time bar is
            items.push_back({"Split at the time bar", [this, id, pos] { if (onCommand) onCommand("clip split " + id + " --at " + beatText(toTick(pos))); }});
        const bool rev = c.reverse;
        items.push_back({rev ? "Play forwards" : "Reverse", [this, id, rev] { if (onCommand) onCommand("set " + id + ".reverse=" + (rev ? "false" : "true")); }});
        // consolidate: the clips it runs into on its lane through its strip — touching or overlapping, one after another
        std::vector<const ClipView *> run;
        for (const auto &v : mClips)
            if (v.c.id == id) run.push_back(&v);
        for (bool grew = true; grew;)
        {
            grew = false;
            for (const auto &v : mClips)
            {
                if (v.c.kind != "audio" || v.c.lane != c.lane || v.c.track != c.track) continue;
                if (std::find(run.begin(), run.end(), &v) != run.end()) continue;
                for (const ClipView *x : run)
                    if (v.c.at <= x->c.at + x->c.length + 1e-9 && x->c.at <= v.c.at + v.c.length + 1e-9) { run.push_back(&v); grew = true; break; }
            }
        }
        std::sort(run.begin(), run.end(), [](const ClipView *x, const ClipView *y) { return x->c.at < y->c.at || (x->c.at == y->c.at && x->c.id < y->c.id); });
        std::string ids;
        for (const ClipView *x : run) ids += " " + x->c.id;
        items.push_back({run.size() > 1 ? "Consolidate " + std::to_string(run.size()) + " clips" : std::string("Consolidate"),
                         [this, ids] { if (onCommand) onCommand("clip consolidate" + ids); }});
        // R-WARP-1: the song's tempo, its own typed, its pitch
        const bool warped = c.warp;
        items.push_back({warped ? "Unwarp" : "Warp to the song's tempo", [this, id, warped] { if (onCommand) onCommand("set " + id + ".warp=" + (warped ? "false" : "true")); }});
        if (onRename)
        {
            char b[32];
            std::snprintf(b, sizeof b, "%.6g", c.bpm);
            const std::string now = c.bpm > 0 ? std::string(b) : std::string();
            items.push_back({"Tempo\xE2\x80\xA6", [this, id, now, world] {
                                 onRename(now, world, [this, id](const std::string &n) { if (onCommand && !n.empty()) onCommand("set " + id + ".bpm=" + n); });
                             }});
        }
        std::vector<cosmo_v2::ContextMenu::Item> pitch;
        for (int st : {12, 7, 5, 1, 0, -1, -5, -7, -12})
        {
            const std::string label = (st > 0 ? "+" : st < 0 ? "\xE2\x88\x92" : "") + std::to_string(std::abs(st)) + (st == 0 ? " (as recorded)" : " st") +
                                      (std::fabs(c.pitch - st) < 1e-9 ? "  \xC2\xB7 now" : "");
            pitch.push_back({label, [this, id, st] { if (onCommand) onCommand("set " + id + ".pitch=" + std::to_string(st)); }});
        }
        items.push_back({"Pitch \xE2\x96\xB8", [this, pitch, world] { if (onMenu) onMenu(pitch, world); }});
        if (c.loop) return; // a looping clip is not sliced (unloop it first)
        std::vector<cosmo_v2::ContextMenu::Item> slice;
        auto every = [this, id](double b) { return [this, id, b] { if (onCommand) onCommand("clip slice " + id + " --every " + beatText(b)); }; };
        slice.push_back({"Every bar", every(mBeatsPerBar)});
        slice.push_back({"Every beat", every(1.0)});
        slice.push_back({"Every half beat", every(0.5)});
        if (c.takes.empty()) slice.push_back({"At its hits", [this, id] { if (onCommand) onCommand("clip slice " + id + " --transients"); }});
        slice.push_back({"To a kit, a slice a beat", [this, id] { if (onCommand) onCommand("clip slice " + id + " --every 1 --kit"); }});
        items.push_back({"Slice \xE2\x96\xB8", [this, slice, world] { if (onMenu) onMenu(slice, world); }});
    }
}
}
