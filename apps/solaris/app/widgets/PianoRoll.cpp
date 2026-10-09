#include "PianoRoll.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    namespace
    {
        constexpr double kSnaps[5] = {1.0, 0.5, 0.25, 0.125, 0.0};  // 1/4 · 1/8 · 1/16 · 1/32 · off
        const char *kSnapLabels[5] = {"1/4", "1/8", "1/16", "1/32", "Off"};
        constexpr double kChipW = 35.75;  // space::u(11)
        constexpr double kModeW = 52.0;   // space::u(16)
        constexpr double kQuantW = 71.5;  // space::u(22)
        constexpr double kEdge = 5.0;     // a note's right edge, grabbed to resize
        constexpr double kTol = 0.5 / 960.0;
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string beats(double b)
        {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", std::round(b * 960.0) / 960.0);
            return buf;
        }
        bool black(int p)
        {
            const int k = ((p % 12) + 12) % 12;
            return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
        }
    }

    PianoRoll::PianoRoll(std::string patternId) : mPattern(std::move(patternId)) { clipToBounds = true; }

    std::string PianoRoll::key(int pitch, double at)
    {
        char b[48];
        std::snprintf(b, sizeof b, "%d@%lld", pitch, (long long)std::llround(at * 960.0));
        return b;
    }

    std::string PianoRoll::title() const
    {
        const std::string name = mModel.name.empty() ? mPattern : mModel.name;
        return "Piano Roll \xE2\x80\x94 " + name + (mStripName.empty() || mStripName == name ? "" : " \xC2\xB7 " + mStripName);
    }

    std::string PianoRoll::noteName(int pitch) const
    {
        for (const auto &n : mNames)
            if (n.note == pitch) return n.name;
        if (!mNames.empty()) return std::string(); // a kit: only its pads have names
        if (pitch % 12 == 0) return "C" + std::to_string(pitch / 12 - 1); // C4 = 60
        return std::string();
    }

    double PianoRoll::snapped(double beat) const
    {
        if (mSnap <= 0) return std::max(0.0, std::round(beat * 960.0) / 960.0);
        return std::max(0.0, std::round(beat / mSnap) * mSnap);
    }

    void PianoRoll::bind(const solaris::AppModel &m)
    {
        const solaris::PatternModel *pt = nullptr;
        for (const auto &p : m.patterns)
            if (p.id == mPattern) pt = &p;
        mPresent = pt != nullptr;
        if (!pt) return;
        mModel = *pt;
        mStripName.clear();
        for (size_t i = 0; i < m.strips.size(); ++i)
            if (m.strips[i].id == pt->strip) { mColour = m.strips[i].colour; mStripName = m.strips[i].name; }
        mNames.clear();
        for (const auto &t : m.deviceTypes)
            if (t.name == pt->instrument) mNames = t.noteNames;
        // keyed: a note the model still has stays; a new one fades in; a gone one fades out where it was
        for (auto &l : mNotes) l.gone = true;
        for (const auto &n : pt->notes)
        {
            const std::string k = key(n.pitch, n.at);
            NoteLive *l = nullptr;
            for (auto &x : mNotes)
                if (key(x.n.pitch, x.n.at) == k) l = &x;
            if (!l) { mNotes.emplace_back(); l = &mNotes.back(); }
            l->n = n;
            l->gone = false;
        }
        // the step rows: a kit's pads, else the pitches in use (and middle C)
        mStepPitches.clear();
        if (!mNames.empty())
            for (const auto &n : mNames) mStepPitches.push_back(n.note);
        else
        {
            std::set<int> ps = {60};
            for (const auto &n : pt->notes) ps.insert(n.pitch);
            mStepPitches.assign(ps.rbegin(), ps.rend()); // high at the top, as the keys are
        }
    }

    // ── geometry ────────────────────────────────────────────────────────────────────────────

    Rect PianoRoll::gridRect() const { return Rect{kKeysW, kToolH, std::max(0.0, width.value() - kKeysW), std::max(0.0, height.value() - kToolH - kVelH)}; }
    Rect PianoRoll::velRect() const { return Rect{kKeysW, height.value() - kVelH, std::max(0.0, width.value() - kKeysW), kVelH}; }
    double PianoRoll::beatToX(double b) const { return kKeysW + b * mPpb.value() - mScrollX.value(); }
    double PianoRoll::xToBeat(double x) const { return (x - kKeysW + mScrollX.value()) / std::max(1e-6, mPpb.value()); }
    double PianoRoll::pitchToY(int p) const { return kToolH + (127 - p) * kNoteH - mScrollY.value(); }
    int PianoRoll::yToPitch(double y) const { return std::clamp(127 - (int)std::floor((y - kToolH + mScrollY.value()) / kNoteH), 0, 127); }

    Rect PianoRoll::noteRect(int pitch, double at) const
    {
        for (const auto &l : mNotes)
            if (!l.gone && l.n.pitch == pitch && std::fabs(l.n.at - at) < kTol)
                return Rect{beatToX(at), pitchToY(pitch) + 1.0, std::max(3.0, l.n.length * mPpb.value() - 1.0), kNoteH - 2.0};
        return Rect{};
    }

    double PianoRoll::noteAlpha(int pitch, double at) const
    {
        for (const auto &l : mNotes)
            if (l.n.pitch == pitch && std::fabs(l.n.at - at) < kTol) return l.placed ? l.alpha.value() : 0.0;
        return 0.0;
    }

    Rect PianoRoll::heldRect() const
    {
        if (mDrag != Drag::Move && mDrag != Drag::Resize) return Rect{};
        return Rect{beatToX(mLive.at), pitchToY(mLive.pitch) + 1.0, std::max(3.0, mLive.length * mPpb.value() - 1.0), kNoteH - 2.0};
    }

    Rect PianoRoll::snapRect(int i) const { return Rect{space::padX() + 34.0 + i * (kChipW + 3.25), 4.875, kChipW, kToolH - 9.75}; }
    Rect PianoRoll::modeRect(int m) const
    {
        const Rect last = snapRect(4);
        return Rect{last.right() + 19.5 + m * kModeW, 4.875, kModeW, kToolH - 9.75};
    }
    Rect PianoRoll::quantizeRect() const { return Rect{modeRect(1).right() + 13.0, 4.875, kQuantW, kToolH - 9.75}; }
    Rect PianoRoll::endRect() const
    {
        const double x = beatToX(mDrag == Drag::End ? mLiveEnd : mEnd.value());
        return Rect{x - 4.875, kToolH, 9.75, 13.0};
    }
    Rect PianoRoll::stepCell(int row, int step) const
    {
        const double w = 0.25 * mPpb.value();
        return Rect{beatToX(step * 0.25) + 1.0, kToolH + 6.5 + row * kStepH + 2.0, std::max(2.0, w - 2.0), kStepH - 4.0};
    }

    void PianoRoll::layout()
    {
        const Rect g = gridRect();
        const double content = std::max(mModel.length, mEnd.value()) + 8.0;
        mScrollX.setExtent(kKeysW, g.w, content * mPpbTarget);
        mScrollY.setExtent(kToolH, g.h, 128.0 * kNoteH);
        if (!mScrolledIn && g.h > 0)
        {
            // first placement: the notes in view (or middle C)
            int lo = 60, hi = 60;
            for (const auto &n : mModel.notes) { lo = std::min(lo, n.pitch); hi = std::max(hi, n.pitch); }
            const double centre = (127 - (lo + hi) * 0.5) * kNoteH;
            mScrollY.reveal(std::max(0.0, centre - g.h * 0.5), g.h);
            mScrolledIn = true;
        }
    }

    // ── time ────────────────────────────────────────────────────────────────────────────────

    void PianoRoll::advance(double nowMs)
    {
        mNowMs = nowMs;
        const bool fadeIn = mEver && !reducedMotion();
        for (auto &l : mNotes)
        {
            const double want = l.gone ? 0.0 : 1.0;
            if (!l.placed) { l.alpha.set(fadeIn && !l.instant ? 0.0 : 1.0); l.aLast = l.alpha.value(); l.placed = true; }
            if (want != l.aLast)
            {
                if (l.instant && l.gone) l.alpha.set(0.0); // dragged away: the pointer already showed where it went
                else l.alpha.animateTo(want, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                l.aLast = want;
            }
            l.alpha.update(nowMs);
        }
        mNotes.erase(std::remove_if(mNotes.begin(), mNotes.end(), [](const NoteLive &l) { return l.gone && !l.alpha.isAnimating() && l.alpha.value() <= 0.001; }),
                     mNotes.end());
        const bool steps = mMode == Steps;
        if (!mModeInit) { mModeAmt.set(steps ? 1.0 : 0.0); mModeLast = steps; mModeInit = true; }
        else if (steps != mModeLast)
        {
            mModeAmt.animateTo(steps ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mModeLast = steps;
            if (steps) mNotesPpb = mPpbTarget; // Notes keeps its own zoom, and gets it back eased
            else
            {
                mPpbTarget = mNotesPpb;
                mPpb.animateTo(mPpbTarget, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            }
        }
        // Steps zooms to fit the pattern — cells a pointer cannot miss — following the window as it resizes, eased
        if (steps && gridRect().w > 0)
        {
            const double fit = std::clamp((gridRect().w - 13.0) / std::max(1.0, mModel.length), 64.0, 160.0);
            if (std::fabs(fit - mPpbTarget) > 0.5)
            {
                mPpbTarget = fit;
                if (mEver) mPpb.animateTo(fit, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                else mPpb.set(fit); // first placement: nowhere to travel from
                mScrollX.scrollBy(-mScrollX.target());
            }
        }
        mModeAmt.update(nowMs);
        if (mEndLast < 0) { mEnd.set(mModel.length); mEndLast = mModel.length; }
        else if (mModel.length != mEndLast) { mEnd.animateTo(mModel.length, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs); mEndLast = mModel.length; }
        mEnd.update(nowMs);
        mPpb.update(nowMs);
        mScrollX.advance(nowMs);
        mScrollY.advance(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        if (mPresent) mEver = true;
        Segment::advance(nowMs);
    }

    // ── input ───────────────────────────────────────────────────────────────────────────────

    const PianoRoll::NoteLive *PianoRoll::noteAt(const Point &p, bool &edge) const
    {
        for (auto it = mNotes.rbegin(); it != mNotes.rend(); ++it)
        {
            if (it->gone) continue;
            const Rect r = noteRect(it->n.pitch, it->n.at);
            if (r.contains(p))
            {
                edge = p.x >= r.right() - kEdge;
                return &*it;
            }
        }
        edge = false;
        return nullptr;
    }

    bool PianoRoll::contextClick(Point world)
    {
        const Point p = toLocal(world);
        if (mMode != Notes || !gridRect().contains(p)) return false;
        bool edge = false;
        const NoteLive *n = noteAt(p, edge);
        if (!n) return false;
        send("note delete " + mPattern + " --pitch " + std::to_string(n->n.pitch) + " --at " + beats(n->n.at)); // FL's right-click
        return true;
    }

    bool PianoRoll::handleGesture(const Gesture &g, const Point &local)
    {
        const Rect grid = gridRect(), vel = velRect();
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            int h = -1;
            for (int i = 0; i < 5; ++i)
                if (snapRect(i).contains(local)) h = i;
            for (int m = 0; m < 2; ++m)
                if (modeRect(m).contains(local)) h = 10 + m;
            if (quantizeRect().contains(local)) h = 20;
            mHover.setHovered(h);
            return true;
        }
        case Gesture::Type::Down:
        {
            mDrag = Drag::None;
            if (endRect().contains(local)) { mDrag = Drag::End; mLiveEnd = mEnd.value(); return true; }
            if (mMode == Notes && grid.contains(local))
            {
                bool edge = false;
                if (const NoteLive *n = noteAt(local, edge))
                {
                    mDrag = edge ? Drag::Resize : Drag::Move;
                    mPressNote = n->n;
                    mLive = n->n;
                    mPressKey = key(n->n.pitch, n->n.at);
                    mGrabBeat = xToBeat(local.x) - n->n.at;
                    mGrabPitch = yToPitch(local.y) - n->n.pitch;
                }
                return true;
            }
            if (vel.contains(local))
                for (auto it = mNotes.rbegin(); it != mNotes.rend(); ++it)
                    if (!it->gone && std::fabs(beatToX(it->n.at) + 1.5 - local.x) <= 5.0)
                    {
                        mDrag = Drag::Velocity;
                        mPressNote = it->n;
                        mLive = it->n;
                        mPressKey = key(it->n.pitch, it->n.at);
                        break;
                    }
            return true;
        }
        case Gesture::Type::DragStart:
        case Gesture::Type::Drag:
            switch (mDrag)
            {
            case Drag::Move:
                mLive.at = snapped(xToBeat(local.x) - mGrabBeat);
                mLive.pitch = std::clamp(yToPitch(local.y) - (int)mGrabPitch, 0, 127);
                break;
            case Drag::Resize:
            {
                const double step = mSnap > 0 ? mSnap : 1.0 / 16.0;
                mLive.length = std::max(step, snapped(xToBeat(local.x)) - mLive.at);
                break;
            }
            case Drag::Velocity:
                mLive.vel = std::clamp((int)std::lround((vel.bottom() - 6.0 - local.y) / std::max(1.0, kVelH - 12.0) * 127.0), 1, 127);
                break;
            case Drag::End:
                mLiveEnd = std::max(0.25, snapped(xToBeat(local.x)));
                break;
            default:
                break;
            }
            return true;
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
        {
            const Drag d = mDrag;
            mDrag = Drag::None;
            const std::string id = mPattern, from = " --pitch " + std::to_string(mPressNote.pitch) + " --at " + beats(mPressNote.at);
            if (d == Drag::Move && (mLive.at != mPressNote.at || mLive.pitch != mPressNote.pitch))
            {
                // where the pointer let go: the new note appears there at once, the old one goes at once
                for (auto &l : mNotes)
                    if (key(l.n.pitch, l.n.at) == mPressKey) l.instant = true;
                NoteLive nl;
                nl.n = mLive;
                nl.instant = true;
                mNotes.push_back(nl);
                send("note move " + id + from + " --to-pitch " + std::to_string(mLive.pitch) + " --to-at " + beats(mLive.at));
            }
            else if (d == Drag::Resize && mLive.length != mPressNote.length)
            {
                mLastLength = mLive.length;
                for (auto &l : mNotes)
                    if (key(l.n.pitch, l.n.at) == mPressKey) l.n.length = mLive.length;
                send("note move " + id + from + " --length " + beats(mLive.length));
            }
            else if (d == Drag::Velocity && mLive.vel != mPressNote.vel)
            {
                for (auto &l : mNotes)
                    if (key(l.n.pitch, l.n.at) == mPressKey) l.n.vel = mLive.vel;
                send("note move " + id + from + " --vel " + std::to_string(mLive.vel));
            }
            else if (d == Drag::End && mLiveEnd != mEnd.value())
            {
                mEnd.set(mLiveEnd);
                mEndLast = mLiveEnd;
                send("set " + id + ".length=" + beats(mLiveEnd));
            }
            return true;
        }
        case Gesture::Type::Click:
        {
            for (int i = 0; i < 5; ++i)
                if (snapRect(i).contains(local)) { mSnap = kSnaps[i]; return true; }
            for (int m = 0; m < 2; ++m)
                if (modeRect(m).contains(local)) { mMode = (Mode)m; return true; }
            if (quantizeRect().contains(local))
            {
                if (!onMenu) return true;
                const std::string grid = beats(mSnap > 0 ? mSnap : 0.25), id = mPattern;
                onMenu({{"Quantize (straight)", [this, id, grid] { send("pattern quantize " + id + " --grid " + grid); }},
                        {"Quantize, swing 25%", [this, id, grid] { send("pattern quantize " + id + " --grid " + grid + " --swing 0.25"); }},
                        {"Quantize, swing 50%", [this, id, grid] { send("pattern quantize " + id + " --grid " + grid + " --swing 0.5"); }}},
                       g.pos);
                return true;
            }
            if (mMode == Steps)
            {
                for (int r = 0; r < stepRows(); ++r)
                    for (int s = 0; s * 0.25 < std::max(mModel.length, 0.25); ++s)
                        if (stepCell(r, s).contains(local))
                        {
                            const int pitch = mStepPitches[(size_t)r];
                            bool on = false;
                            for (const auto &n : mModel.notes) on |= n.pitch == pitch && std::fabs(n.at - s * 0.25) < kTol;
                            send(std::string(on ? "note delete " : "note add ") + mPattern + " --pitch " + std::to_string(pitch) + " --at " + beats(s * 0.25) +
                                 (on ? "" : " --length 0.25"));
                            return true;
                        }
                return true;
            }
            if (grid.contains(local))
            {
                bool edge = false;
                if (noteAt(local, edge)) return true; // a click on a note adds nothing
                const double at = mSnap > 0 ? std::max(0.0, std::floor(xToBeat(local.x) / mSnap) * mSnap) : snapped(xToBeat(local.x));
                send("note add " + mPattern + " --pitch " + std::to_string(yToPitch(local.y)) + " --at " + beats(at) + " --length " + beats(mLastLength));
            }
            return true;
        }
        case Gesture::Type::DoubleClick:
        case Gesture::Type::RightClick:
        {
            if (mMode != Notes || !grid.contains(local)) return true;
            bool edge = false;
            if (const NoteLive *n = noteAt(local, edge))
                send("note delete " + mPattern + " --pitch " + std::to_string(n->n.pitch) + " --at " + beats(n->n.at));
            return true;
        }
        case Gesture::Type::Scroll:
            if (g.ctrl && mMode == Steps) return true; // Steps' zoom is the fit
            if (g.ctrl)
            {
                const double anchor = xToBeat(local.x);
                const double next = std::clamp(mPpbTarget * (g.delta.y < 0 ? 1.25 : 0.8), 8.0, 400.0);
                if (next == mPpbTarget) return true;
                mPpbTarget = next;
                mPpb.animateTo(next, motion::kCatchUpMs, Easing::EaseOutCubic, mNowMs);
                layout();
                mScrollX.scrollBy(anchor * next - (local.x - kKeysW) - mScrollX.target());
                return true;
            }
            if (g.shift || mMode == Steps) return mScrollX.scrollBy(g.delta.y);
            return mScrollY.scrollBy(g.delta.y);
        default:
            return true;
        }
    }

    // ── paint ───────────────────────────────────────────────────────────────────────────────

    void PianoRoll::paintNotes(IRenderTarget &t, double a) const
    {
        const Rect grid = gridRect();
        const double W = width.value();
        t.save();
        t.clipRect(0, grid.y, W, grid.h);
        // pitch rows: black keys a shade darker; C rows marked
        for (int p = 127; p >= 0; --p)
        {
            const double y = pitchToY(p);
            if (y + kNoteH < grid.y || y > grid.bottom()) continue;
            if (black(p)) drawRoundedRect(t, Rect{grid.x, y, grid.w, kNoteH}, 0.0, Paint::filled(fade(palette::whiteAlpha(0.025), a)));
            if (p % 12 == 0)
            {
                t.setStroke(fade(palette::whiteAlpha(0.07), a), 1.0);
                t.beginPath(); t.moveTo(grid.x, std::round(y + kNoteH) + 0.5); t.lineTo(W, std::round(y + kNoteH) + 0.5); t.strokePath();
            }
            // the keys
            const Rect k{0, y, kKeysW - 1.0, kNoteH};
            drawRoundedRect(t, Rect{0, y + 0.5, kKeysW - 1.0, kNoteH - 1.0}, 0.0,
                            Paint::filled(fade(black(p) ? palette::canvasBg() : palette::secondary(), a)));
            const std::string nm = noteName(p);
            if (!nm.empty())
            {
                t.setFill(fade(mNames.empty() ? palette::mutedForeground() : palette::foreground(), a));
                t.drawText(textfit::ellipsize(t, nm, kKeysW - 8.0, 9.0, font::sans()), 4.0, textfit::baseline(k.y + k.h * 0.5, 9.0), 9.0, font::sans());
            }
        }
        // the notes, keyed; the dragged one is the pointer's
        t.clipRect(grid.x, grid.y, grid.w, grid.h);
        const Color base = surface::track(mColour);
        for (const auto &l : mNotes)
        {
            const double na = (l.placed ? l.alpha.value() : 0.0) * a;
            if (na <= 0.001) continue;
            const bool dragged = (mDrag == Drag::Move || mDrag == Drag::Resize || mDrag == Drag::Velocity) && !l.gone && key(l.n.pitch, l.n.at) == mPressKey;
            const solaris::NoteModel n = dragged ? mLive : l.n;
            const Rect r{beatToX(n.at), pitchToY(n.pitch) + 1.0, std::max(3.0, n.length * mPpb.value() - 1.0), kNoteH - 2.0};
            if (r.right() < grid.x || r.x > grid.right() || r.bottom() < grid.y || r.y > grid.bottom()) continue;
            const Color c = lerpColor(base, palette::white(), 0.12 + 0.28 * (n.vel / 127.0));
            drawRoundedRect(t, r, radius::control(), Paint::filledStroked(fade(c, na * (dragged ? 1.0 : 0.9)), fade(palette::whiteAlpha(0.35), na), 1.0));
        }
        t.restore();
    }

    void PianoRoll::paintSteps(IRenderTarget &t, double a) const
    {
        const Rect grid = gridRect();
        t.save();
        t.clipRect(0, grid.y, width.value(), grid.h);
        const Color base = surface::track(mColour);
        for (int r = 0; r < stepRows(); ++r)
        {
            const double y = kToolH + 6.5 + r * kStepH;
            const int pitch = mStepPitches[(size_t)r];
            std::string nm = noteName(pitch);
            if (nm.empty()) nm = std::to_string(pitch);
            t.setFill(fade(palette::foreground(), a));
            t.drawText(textfit::ellipsize(t, nm, kKeysW - 8.0, 10.0, font::sans()), 6.0, textfit::baseline(y + kStepH * 0.5, 10.0), 10.0, font::sans());
            for (int s = 0; s * 0.25 < std::max(mModel.length, 0.25) - kTol; ++s)
            {
                const Rect c = stepCell(r, s);
                if (c.right() < grid.x || c.x > grid.right()) continue;
                bool on = false;
                int v = 100;
                for (const auto &n : mModel.notes)
                    if (n.pitch == pitch && std::fabs(n.at - s * 0.25) < kTol) { on = true; v = n.vel; }
                const bool beat = s % 4 == 0;
                drawRoundedRect(t, c, radius::control(),
                                Paint::filled(fade(on ? lerpColor(base, palette::white(), 0.15 + 0.25 * v / 127.0) : palette::whiteAlpha(beat ? 0.08 : 0.045), a)));
            }
        }
        t.restore();
    }

    void PianoRoll::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value();
        const Rect grid = gridRect(), vel = velRect();
        drawRoundedRect(t, Rect{0, 0, W, H}, 0.0, Paint::filled(surface::stageBg()));
        // the beat grid, shared by both modes
        t.save();
        t.clipRect(grid.x, grid.y, grid.w, H - grid.y);
        const double ppb = mPpb.value();
        for (double b = std::max(0.0, std::floor(xToBeat(grid.x))); b <= xToBeat(W) + 1.0; b += 0.25)
        {
            const bool beat = std::fmod(b, 1.0) == 0.0, bar = std::fmod(b, 4.0) == 0.0;
            if (!beat && ppb < 40.0) continue;
            const double x = std::round(beatToX(b)) + 0.5;
            t.setStroke(palette::whiteAlpha(bar ? 0.12 : beat ? 0.06 : 0.025), 1.0);
            t.beginPath(); t.moveTo(x, grid.y); t.lineTo(x, H); t.strokePath();
        }
        t.restore();

        const double m = mModeAmt.value();
        if (m < 0.999) paintNotes(t, 1.0 - m);
        if (m > 0.001) paintSteps(t, m);

        // past the pattern's end: dimmed; the end itself a handle
        {
            const double ex = beatToX(mDrag == Drag::End ? mLiveEnd : mEnd.value());
            if (ex < W)
            {
                t.save();
                t.clipRect(grid.x, grid.y, grid.w, H - grid.y);
                drawRoundedRect(t, Rect{std::max(grid.x, ex), grid.y, W - std::max(grid.x, ex), H - grid.y}, 0.0, Paint::filled(surface::scrim(1.0)));
                t.restore();
            }
            t.setStroke(palette::primary(), 1.5);
            t.beginPath(); t.moveTo(ex, grid.y); t.lineTo(ex, H); t.strokePath();
            drawRoundedRect(t, endRect(), radius::control(), Paint::filled(palette::primary()));
        }

        // the velocity lane
        drawRoundedRect(t, vel, 0.0, Paint::filled(palette::muted()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, vel.y + 0.5); t.lineTo(W, vel.y + 0.5); t.strokePath();
        t.setFill(palette::mutedForeground());
        t.drawText("VELOCITY", 6.0, textfit::baseline(vel.y + 12.0, 8.0), 8.0, font::sansSemiBold(), 0.13 * 8.0);
        t.save();
        t.clipRect(vel.x, vel.y, vel.w, vel.h);
        const Color base = surface::track(mColour);
        for (const auto &l : mNotes)
        {
            const double na = l.placed ? l.alpha.value() : 0.0;
            if (na <= 0.001) continue;
            const bool dragged = mDrag != Drag::None && mDrag != Drag::End && !l.gone && key(l.n.pitch, l.n.at) == mPressKey;
            const solaris::NoteModel n = dragged ? mLive : l.n;
            const double x = beatToX(n.at) + 1.5, top = vel.bottom() - 6.0 - (n.vel / 127.0) * (kVelH - 12.0);
            t.setStroke(fade(base, na), 2.0);
            t.beginPath(); t.moveTo(x, vel.bottom() - 6.0); t.lineTo(x, top); t.strokePath();
            drawCircle(t, x, top, 3.0, Paint::filled(fade(lerpColor(base, palette::white(), 0.3), na)));
        }
        t.restore();

        // the toolbar
        drawRoundedRect(t, Rect{0, 0, W, kToolH}, 0.0, Paint::filled(surface::headerBg()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, kToolH - 0.5); t.lineTo(W, kToolH - 0.5); t.strokePath();
        t.setFill(palette::mutedForeground());
        t.drawText("SNAP", space::padX(), textfit::baseline(kToolH * 0.5, 8.0), 8.0, font::sansSemiBold(), 0.13 * 8.0);
        auto chip = [&](const Rect &r, const char *label, bool on, int hoverId) {
            const double hv = mHover.amount(hoverId);
            drawRoundedRect(t, r, radius::control(), Paint::filled(on ? palette::primary() : lerpColor(palette::secondary(), palette::popover(), hv)));
            t.setFill(on ? palette::primaryForeground() : palette::secondaryForeground());
            t.drawText(label, r.x + (r.w - t.measureText(label, 10.0, font::sans())) * 0.5, textfit::baseline(r.y + r.h * 0.5, 10.0), 10.0, font::sans());
        };
        for (int i = 0; i < 5; ++i) chip(snapRect(i), kSnapLabels[i], mSnap == kSnaps[i], i);
        chip(modeRect(0), "Notes", mMode == Notes, 10);
        chip(modeRect(1), "Steps", mMode == Steps, 11);
        chip(quantizeRect(), "Quantize\xE2\x80\xA6", false, 20);
        const std::string len = beats(mModel.length) + " beats";
        t.setFill(palette::mutedForeground());
        const double lx = std::max(quantizeRect().right() + 13.0, W - space::padX() - t.measureText(len, 10.0, font::mono()));
        t.drawText(len, lx, textfit::baseline(kToolH * 0.5, 10.0), 10.0, font::mono());
        if (m < 0.999) mScrollY.drawBar(t, W - 5.0, 1.0 - m);
    }
}
}
