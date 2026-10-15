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
        constexpr double kPlayW = 29.25;  // space::u(9): the roll's ▶
        constexpr double kLenW = 91.0;    // space::u(28): "Length 1/16 ▾"
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
        // the roll's clip (R-ROLL-6): the one it was opened from while it lasts, else the first in time
        const solaris::ClipModel *cl = nullptr;
        for (const auto &c : m.clips)
            if (c.pattern == mPattern && c.id == mClipWanted) cl = &c;
        if (!cl)
            for (const auto &c : m.clips)
                if (c.pattern == mPattern && (!cl || c.at < cl->at)) cl = &c;
        mClip = cl ? cl->id : std::string();
        mClipTrack = cl ? cl->track : std::string();
        mClipAt = cl ? cl->at : 0.0;
        mClipLen = cl ? cl->length : 0.0;
        mPlaying = m.transport.playing;
        mLooping = !mClip.empty() && m.transport.loopClip == mClip;
        // where the clip is heard, in the pattern (a clip longer than its pattern loops it)
        mHeadBeat = -1;
        if (mPlaying && cl && m.transport.position >= mClipAt && m.transport.position < mClipAt + mClipLen)
        {
            const double in = m.transport.position - mClipAt;
            mHeadBeat = pt->length > 0 ? std::fmod(in, pt->length) : in;
        }
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
        // a selected note gone (deleted, undone, moved by a shell) is no longer selected
        {
            std::set<std::string> present;
            for (const auto &n : pt->notes) present.insert(key(n.pitch, n.at));
            for (auto it = mSel.begin(); it != mSel.end();) it = present.count(*it) ? std::next(it) : mSel.erase(it);
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

    Rect PianoRoll::playRect() const { return Rect{space::padX(), 4.875, kPlayW, kToolH - 9.75}; }
    Rect PianoRoll::snapRect(int i) const { return Rect{playRect().right() + 13.0 + 34.0 + i * (kChipW + 3.25), 4.875, kChipW, kToolH - 9.75}; }

    Rect PianoRoll::keyRect(int pitch) const { return Rect{0, pitchToY(pitch), kKeysW - 1.0, kNoteH}; }
    double PianoRoll::keyLit(int pitch) const { return pitch == mKeyLitPitch ? mKeyLight.value() : 0.0; }

    void PianoRoll::preview(int pitch, int vel, double length)
    {
        if (mClipTrack.empty() || pitch < 0 || pitch > 127) return; // on no track: nothing to sound through
        send("note preview " + mClipTrack + " " + std::to_string(pitch) + " --vel " + std::to_string(std::clamp(vel, 1, 127)) + " --length " +
             beats(std::clamp(length, 0.125, 1.0)));
    }

    // ── the selection (R-ROLL-8) ──────────────────────────────────────────────────────────────────

    bool PianoRoll::selected(int pitch, double at) const { return mSel.count(key(pitch, at)) > 0; }

    double PianoRoll::selectAmount(int pitch, double at) const
    {
        const std::string k = key(pitch, at);
        for (const auto &l : mNotes)
            if (!l.gone && key(l.n.pitch, l.n.at) == k) return l.sel.value();
        return 0.0;
    }

    Rect PianoRoll::boxRect() const
    {
        if (mDrag != Drag::Box) return Rect{};
        return Rect{std::min(mBoxA.x, mBoxB.x), std::min(mBoxA.y, mBoxB.y), std::fabs(mBoxB.x - mBoxA.x), std::fabs(mBoxB.y - mBoxA.y)};
    }

    std::string PianoRoll::tokens(const std::vector<solaris::NoteModel> &ns, double by, int semi, bool full) const
    {
        std::string s;
        for (const auto &n : ns)
        {
            if (!s.empty()) s += " ";
            s += std::to_string(n.pitch + semi) + "@" + beats(n.at + by);
            if (full) s += ":" + beats(n.length) + ":" + std::to_string(n.vel);
        }
        return "\"" + s + "\"";
    }

    std::vector<solaris::NoteModel> PianoRoll::selectedNotes() const
    {
        std::vector<solaris::NoteModel> v;
        for (const auto &n : mModel.notes)
            if (mSel.count(key(n.pitch, n.at))) v.push_back(n);
        return v;
    }

    void PianoRoll::moveSelection(double by, int semi)
    {
        const auto ns = selectedNotes();
        if (ns.empty() || (std::fabs(by) < kTol && semi == 0)) return;
        if (!sendOk("notes move " + mPattern + " " + tokens(ns) + " --by " + beats(by) + " --semi " + std::to_string(semi))) return;
        // where they went: there at once (the pointer or the key showed it), the old places gone at once
        std::set<std::string> next;
        for (const auto &n : ns)
        {
            for (auto &l : mNotes)
                if (key(l.n.pitch, l.n.at) == key(n.pitch, n.at)) l.instant = true;
            NoteLive nl;
            nl.n = n;
            nl.n.at = std::max(0.0, n.at + by);
            nl.n.pitch = n.pitch + semi;
            nl.instant = true;
            nl.sel.set(1.0);
            nl.selLast = true;
            mNotes.push_back(nl);
            next.insert(key(nl.n.pitch, nl.n.at));
        }
        mSel = next;
    }

    void PianoRoll::addSelected(const std::vector<solaris::NoteModel> &ns)
    {
        if (ns.empty()) return;
        if (!sendOk("notes add " + mPattern + " " + tokens(ns, 0.0, 0, true))) return;
        mSel.clear();
        for (const auto &n : ns) mSel.insert(key(n.pitch, n.at));
    }

    bool PianoRoll::key(const KeyEvent &e)
    {
        if (e.type != KeyEvent::Type::Down || mMode != Notes) return false;
        if (e.ctrl && e.keyCode == 'A')
        {
            mSel.clear();
            for (const auto &n : mModel.notes) mSel.insert(key(n.pitch, n.at));
            return true;
        }
        if (e.ctrl && e.keyCode == 'V')
        {
            if (!mClipboard || mClipboard->empty()) return false;
            addSelected(*mClipboard); // at the same beats, selected: ready to be dragged where it goes
            return true;
        }
        if (mSel.empty()) return false;
        if (e.keyCode == 27) { mSel.clear(); return true; }  // Escape lets go
        if (e.keyCode == 46 || e.keyCode == 8)               // Delete / Backspace
        {
            if (sendOk("notes delete " + mPattern + " " + tokens(selectedNotes()))) mSel.clear();
            return true;
        }
        if (e.keyCode == 38 || e.keyCode == 40) { moveSelection(0.0, (e.keyCode == 38 ? 1 : -1) * (e.shift ? 12 : 1)); return true; }
        if (e.keyCode == 37 || e.keyCode == 39) { moveSelection((e.keyCode == 39 ? 1.0 : -1.0) * (mSnap > 0 ? mSnap : 0.25), 0); return true; }
        if (e.ctrl && e.keyCode == 'C')
        {
            if (mClipboard) *mClipboard = selectedNotes();
            return true;
        }
        if (e.ctrl && e.keyCode == 'D')
        {
            // a copy right after the selection: its span — the earliest start to the latest end — up to the snap
            auto ns = selectedNotes();
            double lo = 1e18, hi = 0.0;
            for (const auto &n : ns) { lo = std::min(lo, n.at); hi = std::max(hi, n.at + n.length); }
            double span = hi - lo;
            if (mSnap > 0) span = std::ceil(span / mSnap - 1e-9) * mSnap;
            for (auto &n : ns) n.at += span;
            addSelected(ns);
            return true;
        }
        return false;
    }

    bool PianoRoll::togglePlay()
    {
        if (mClip.empty()) return false; // on no track: nothing to sound through
        send(mLooping && mPlaying ? std::string("transport stop") : "transport play --clip " + mClip);
        return true;
    }
    Rect PianoRoll::modeRect(int m) const
    {
        const Rect last = snapRect(4);
        return Rect{last.right() + 19.5 + m * kModeW, 4.875, kModeW, kToolH - 9.75};
    }
    Rect PianoRoll::quantizeRect() const { return Rect{modeRect(1).right() + 13.0, 4.875, kQuantW, kToolH - 9.75}; }
    Rect PianoRoll::lengthRect() const { return Rect{quantizeRect().right() + 6.5, 4.875, kLenW, kToolH - 9.75}; }

    std::string PianoRoll::lengthName(double b)
    {
        // note values, a beat being a quarter (as the snap chips name them)
        const struct { double beats; const char *name; } kNames[] = {{0.125, "1/32"}, {0.25, "1/16"}, {0.5, "1/8"}, {1.0, "1/4"}, {2.0, "1/2"}, {4.0, "1/1"}};
        for (const auto &n : kNames)
            if (std::fabs(b - n.beats) < kTol) return n.name;
        return beats(b) + " beats";
    }
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
            const bool sel = !l.gone && mSel.count(key(l.n.pitch, l.n.at)) > 0; // R-ROLL-8: lit, eased
            if (sel != l.selLast)
            {
                l.sel.animateTo(sel ? 1.0 : 0.0, motion::kHoverMs, Easing::EaseOutCubic, nowMs);
                l.selLast = sel;
            }
            l.sel.update(nowMs);
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
        const bool held = mDrag == Drag::Keys && mKeyPitch >= 0;
        if (held && mKeyPitch != mKeyLitPitch) { mKeyLitPitch = mKeyPitch; mKeyLight.set(0.0); mKeyLastHeld = false; } // another key: its own light
        if (held != mKeyLastHeld)
        {
            mKeyLight.animateTo(held ? 1.0 : 0.0, held ? motion::kHoverMs : motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mKeyLastHeld = held;
        }
        mKeyLight.update(nowMs);
        const bool head = mHeadBeat >= 0.0;
        if (head != mHeadLast)
        {
            mHeadAlpha.animateTo(head ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mHeadLast = head;
        }
        mHeadAlpha.update(nowMs);
        if (head && mDrag == Drag::None)
        {
            // keep it in view: past the grid's right edge (or wrapped back before its left), a page on
            const Rect g = gridRect();
            const double x = kKeysW + mHeadBeat * mPpbTarget - mScrollX.target();
            if (g.w > 40.0 && (x > g.right() - 13.0 || x < g.x)) mScrollX.scrollBy(x - (g.x + 13.0));
        }
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
                // the resize zone is the right 5 px — or the right third of a short note, so a sixteenth zoomed out
                // (5 px wide) keeps a body to grab and move (D-7)
                edge = p.x >= r.right() - std::min(kEdge, r.w / 3.0);
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

    void PianoRoll::track(const Point &local)
    {
        // where the pointer has a gesture in flight — every Drag, and once more at release, so a drag shorter
        // than the recognizer's threshold (a sixteenth zoomed out is 6 px) still lands where it was let go (D-6)
        const Rect vel = velRect(), grid = gridRect();
        (void)grid;
        switch (mDrag)
        {
            case Drag::Move:
                mLive.at = snapped(xToBeat(local.x) - mGrabBeat);
                mLive.pitch = std::clamp(yToPitch(local.y) - (int)mGrabPitch, 0, 127);
                if (mLive.pitch != mHeardPitch) { mHeardPitch = mLive.pitch; preview(mLive.pitch, mLive.vel, mLive.length); } // each row it enters
                break;
            case Drag::MoveSel:
            {
                // the whole selection is the pointer's: kept at or after beat 0 and inside 0–127
                double lo = 1e18;
                int pmin = 127, pmax = 0;
                for (const auto &n : selectedNotes()) { lo = std::min(lo, n.at); pmin = std::min(pmin, n.pitch); pmax = std::max(pmax, n.pitch); }
                const double at = snapped(xToBeat(local.x) - mGrabBeat);
                const int pitch = yToPitch(local.y) - (int)mGrabPitch;
                mSelDBeat = std::max(at - mPressNote.at, -lo);
                mSelDSemi = std::clamp(pitch - mPressNote.pitch, -pmin, 127 - pmax);
                const int heard = mPressNote.pitch + mSelDSemi;
                if (heard != mHeardPitch) { mHeardPitch = heard; preview(heard, mPressNote.vel, mPressNote.length); }
                break;
            }
            case Drag::Box:
                mBoxB = local;
                break;
            case Drag::Keys:
            {
                const int p = std::clamp(yToPitch(std::clamp(local.y, grid.y, grid.bottom() - 1.0)), 0, 127);
                if (p != mKeyPitch) { mKeyPitch = p; preview(p, 100, 0.5); }
                break;
            }
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
            if (lengthRect().contains(local)) h = 21;
            if (playRect().contains(local) && !mClip.empty()) h = 30;
            mHover.setHovered(h);
            return true;
        }
        case Gesture::Type::Down:
        {
            mDrag = Drag::None;
            mPressPt = local;
            mSwallowClick = false;
            if (endRect().contains(local)) { mDrag = Drag::End; mLiveEnd = mEnd.value(); return true; }
            if (mMode == Notes && grid.contains(local) && g.ctrl)
            {
                // R-ROLL-8 (amended, U-10): Ctrl is selecting — a box, wherever it starts, even on a note; Ctrl never
                // moves a note (a Ctrl+click on one selects it: the box of no size touches it)
                mDrag = Drag::Box;
                mBoxA = mBoxB = local;
                mBoxAdd = g.shift;
                return true;
            }
            if (mMode == Notes && grid.contains(local))
            {
                bool edge = false;
                if (const NoteLive *n = noteAt(local, edge))
                {
                    const bool inSel = !edge && mSel.size() > 1 && mSel.count(key(n->n.pitch, n->n.at)) > 0;
                    mDrag = inSel ? Drag::MoveSel : edge ? Drag::Resize : Drag::Move;
                    mSelDBeat = 0.0;
                    mSelDSemi = 0;
                    mPressNote = n->n;
                    mLive = n->n;
                    mPressKey = key(n->n.pitch, n->n.at);
                    mGrabBeat = xToBeat(local.x) - n->n.at;
                    mGrabPitch = yToPitch(local.y) - n->n.pitch;
                    mHeardPitch = n->n.pitch;
                    preview(n->n.pitch, n->n.vel, n->n.length); // a note pressed is heard, as in every roll
                }
                return true;
            }
            if (mMode == Notes && local.x < kKeysW && local.y >= grid.y && local.y < grid.bottom())
            {
                mDrag = Drag::Keys; // a key pressed sounds; swept, each key it enters
                mKeyPitch = yToPitch(local.y);
                preview(mKeyPitch, 100, 0.5);
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
            track(local);
            return true;
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
        {
            // D-6: a drag shorter than the recognizer's threshold still lands where it was let go — but a click (no
            // movement at all) moves nothing, so an off-grid note is not re-snapped by being clicked
            if (mDrag != Drag::None && mDrag != Drag::Box && std::hypot(local.x - mPressPt.x, local.y - mPressPt.y) > 0.5)
            {
                track(local);
                mSwallowClick = true; // under the threshold the recognizer calls it a Click too — it was a drag
            }
            const Drag d = mDrag;
            mDrag = Drag::None;
            const std::string id = mPattern, from = " --pitch " + std::to_string(mPressNote.pitch) + " --at " + beats(mPressNote.at);
            if (d == Drag::MoveSel)
            {
                moveSelection(mSelDBeat, mSelDSemi);
                mSelDBeat = 0.0;
                mSelDSemi = 0;
                return true;
            }
            if (d == Drag::Box)
            {
                const Rect b{std::min(mBoxA.x, mBoxB.x), std::min(mBoxA.y, mBoxB.y), std::fabs(mBoxB.x - mBoxA.x), std::fabs(mBoxB.y - mBoxA.y)};
                if (!mBoxAdd) mSel.clear();
                for (const auto &n : mModel.notes)
                {
                    const Rect r = noteRect(n.pitch, n.at);
                    if (r.x < b.right() && r.right() > b.x && r.y < b.bottom() && r.bottom() > b.y) mSel.insert(key(n.pitch, n.at));
                }
                return true;
            }
            if (d == Drag::Move && (mLive.at != mPressNote.at || mLive.pitch != mPressNote.pitch))
            {
                // where the pointer let go: the new note appears there at once, the old one goes at once
                for (auto &l : mNotes)
                    if (key(l.n.pitch, l.n.at) == mPressKey) l.instant = true;
                NoteLive nl;
                nl.n = mLive;
                nl.instant = true;
                mNotes.push_back(nl);
                if (sendOk("note move " + id + from + " --to-pitch " + std::to_string(mLive.pitch) + " --to-at " + beats(mLive.at)))
                    mSel = {key(mLive.pitch, mLive.at)}; // the note moved is what is selected
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
            if (mSwallowClick) { mSwallowClick = false; return true; } // D-6: the short drag just landed
            if (playRect().contains(local)) { togglePlay(); return true; }
            for (int i = 0; i < 5; ++i)
                if (snapRect(i).contains(local)) { mSnap = kSnaps[i]; return true; }
            for (int m = 0; m < 2; ++m)
                if (modeRect(m).contains(local)) { mMode = (Mode)m; return true; }
            if (lengthRect().contains(local))
            {
                // R-ROLL-10: the length a click makes — the last one (the default), or a note value
                if (!onMenu) return true;
                std::vector<cosmo_v2::ContextMenu::Item> items;
                items.push_back({"Last used (" + lengthName(mLastLength) + ")", [this] { mLengthChoice = 0.0; }});
                for (double b : {0.125, 0.25, 0.5, 1.0, 2.0, 4.0})
                    items.push_back({lengthName(b), [this, b] { mLengthChoice = b; }});
                onMenu(std::move(items), g.pos);
                return true;
            }
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
                            if (!on) preview(pitch, 100, 0.25);
                            return true;
                        }
                return true;
            }
            if (grid.contains(local))
            {
                bool edge = false;
                if (g.ctrl) return true; // a Ctrl+click: its box (of no size) already selected what it touched
                if (const NoteLive *n = noteAt(local, edge))
                {
                    // R-ROLL-8: a click selects the note; Shift adds or removes it
                    const std::string k = key(n->n.pitch, n->n.at);
                    if (g.shift) { if (!mSel.erase(k)) mSel.insert(k); }
                    else mSel = {k};
                    return true;
                }
                mSel.clear();
                const double at = mSnap > 0 ? std::max(0.0, std::floor(xToBeat(local.x) / mSnap) * mSnap) : snapped(xToBeat(local.x));
                const int pitch = yToPitch(local.y);
                send("note add " + mPattern + " --pitch " + std::to_string(pitch) + " --at " + beats(at) + " --length " + beats(clickLength()));
                preview(pitch, 100, clickLength()); // R-ROLL-7: the note made is heard
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
            if (const double lit = keyLit(p); lit > 0.001) // a key pressed: it sounds, lit while held
                drawRoundedRect(t, Rect{0, y + 0.5, kKeysW - 1.0, kNoteH - 1.0}, 0.0, Paint::filled(fade(palette::primaryAlpha(0.55), a * lit)));
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
            const bool carried = mDrag == Drag::MoveSel && !l.gone && mSel.count(key(l.n.pitch, l.n.at)) > 0;
            solaris::NoteModel n = dragged ? mLive : l.n;
            if (carried) { n.at += mSelDBeat; n.pitch += mSelDSemi; } // the selection is the pointer's
            const Rect r{beatToX(n.at), pitchToY(n.pitch) + 1.0, std::max(3.0, n.length * mPpb.value() - 1.0), kNoteH - 2.0};
            if (r.right() < grid.x || r.x > grid.right() || r.bottom() < grid.y || r.y > grid.bottom()) continue;
            const double sel = l.sel.value();
            const Color c = lerpColor(base, palette::white(), 0.12 + 0.28 * (n.vel / 127.0) + 0.25 * sel);
            drawRoundedRect(t, r, radius::control(),
                            Paint::filledStroked(fade(c, na * (dragged || carried ? 1.0 : 0.9)), fade(lerpColor(palette::whiteAlpha(0.35), palette::primary(), sel), na), 1.0 + 0.5 * sel));
        }
        if (mDrag == Drag::Box)
        {
            const Rect b = boxRect();
            drawRoundedRect(t, b, radius::control(), Paint::filledStroked(palette::primaryAlpha(0.12), palette::primaryAlpha(0.7), 1.0));
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

        // the playhead (R-ROLL-6): where the clip is heard
        if (mHeadAlpha.value() > 0.001 && mHeadBeat >= 0.0)
        {
            const double x = std::round(beatToX(mHeadBeat)) + 0.5;
            if (x >= grid.x && x <= W)
            {
                t.setStroke(fade(surface::playhead(), mHeadAlpha.value()), 1.0);
                t.beginPath(); t.moveTo(x, grid.y); t.lineTo(x, H); t.strokePath();
            }
        }

        // the toolbar
        drawRoundedRect(t, Rect{0, 0, W, kToolH}, 0.0, Paint::filled(surface::headerBg()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, kToolH - 0.5); t.lineTo(W, kToolH - 0.5); t.strokePath();
        t.setFill(palette::mutedForeground());
        {
            // ▶ / ■: the roll's play — lit while its clip loops; dimmed with nothing to sound through
            const Rect r = playRect();
            const bool on = mLooping && mPlaying, can = !mClip.empty();
            const double hv = mHover.amount(30);
            drawRoundedRect(t, r, radius::control(), Paint::filled(on ? palette::primary() : lerpColor(palette::secondary(), palette::popover(), hv)));
            const Color ink = fade(on ? palette::primaryForeground() : palette::foreground(), can ? 1.0 : 0.35);
            const double cx = r.x + r.w * 0.5, cy = r.y + r.h * 0.5;
            t.setFill(ink);
            t.beginPath();
            if (on) { t.moveTo(cx - 4.0, cy - 4.0); t.lineTo(cx + 4.0, cy - 4.0); t.lineTo(cx + 4.0, cy + 4.0); t.lineTo(cx - 4.0, cy + 4.0); }
            else { t.moveTo(cx - 3.5, cy - 5.0); t.lineTo(cx + 5.0, cy); t.lineTo(cx - 3.5, cy + 5.0); }
            t.closePath();
            t.fillPath();
        }
        t.drawText("SNAP", playRect().right() + 13.0, textfit::baseline(kToolH * 0.5, 8.0), 8.0, font::sansSemiBold(), 0.13 * 8.0);
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
        {
            // R-ROLL-10: the length a click makes, named; "▾" says it is a menu
            const std::string lbl = "Length " + lengthName(clickLength()) + (mLengthChoice > 0 ? "" : "*") + " \xE2\x96\xBE";
            const Rect lr = lengthRect();
            const double hv = mHover.amount(21);
            drawRoundedRect(t, lr, radius::control(), Paint::filled(lerpColor(palette::secondary(), palette::popover(), hv)));
            t.setFill(palette::secondaryForeground());
            t.drawText(textfit::ellipsize(t, lbl, lr.w - 8.0, 10.0, font::sans()), lr.x + 6.0, textfit::baseline(lr.y + lr.h * 0.5, 10.0), 10.0, font::sans());
        }
        const std::string len = beats(mModel.length) + " beats";
        t.setFill(palette::mutedForeground());
        const double lx = std::max(lengthRect().right() + 13.0, W - space::padX() - t.measureText(len, 10.0, font::mono()));
        t.drawText(len, lx, textfit::baseline(kToolH * 0.5, 10.0), 10.0, font::mono());
        if (m < 0.999) mScrollY.drawBar(t, W - 5.0, 1.0 - m);
    }
}
}
