#include "Navigator.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace shell
{
    void Navigator::velocity(double &vx, double &vy) const
    {
        vx = vy = 0.0;
        if (mSamples.size() < 2) return;
        const TouchSample &last = mSamples.back();
        // The oldest sample still inside the window: a finger that stopped before lifting has
        // only near-identical samples in it, so its speed reads as zero — which is exactly how
        // "pause, then lift" is told from "flick".
        const TouchSample *first = &last;
        for (auto it = mSamples.rbegin(); it != mSamples.rend(); ++it)
        {
            if (last.tMs - it->tMs > gesture::kVelocityWindowMs) break;
            first = &*it;
        }
        const double dt = (last.tMs - first->tMs) / 1000.0;
        if (dt <= 1e-4) return;
        vx = (last.x - first->x) / dt;
        vy = (last.y - first->y) / dt;
    }

    GestureResult Navigator::decide(double w, double h) const
    {
        (void)w;
        double vx, vy;
        velocity(vx, vy);
        const double up = -mGesture.dy;
        switch (mGesture.kind)
        {
        case GestureKind::Home:
            if (up < gesture::kMinTravel) return GestureResult::Cancelled;
            if (mNavAtDown == NavState::Home) return GestureResult::Recents;  // nothing to go home from
            if (mNavAtDown == NavState::Recents) return GestureResult::Home;
            if (-vy > gesture::kFlingSpeed) return GestureResult::Home;
            if (up > h * gesture::kHomeFraction) return GestureResult::Home;
            if (up > h * gesture::kRecentsFraction) return GestureResult::Recents;
            return GestureResult::Cancelled;
        case GestureKind::Back:
            return std::fabs(mGesture.dx) >= gesture::kBackCommit ? GestureResult::Back
                                                                  : GestureResult::Cancelled;
        case GestureKind::ShadeOpen:
            if (vy > gesture::kShadeFling) return GestureResult::ShadeOpened;
            if (vy < -gesture::kShadeFling) return GestureResult::Cancelled;
            return mGesture.progress >= gesture::kShadeCommit ? GestureResult::ShadeOpened
                                                              : GestureResult::Cancelled;
        case GestureKind::ShadeClose:
            if (-vy > gesture::kShadeFling) return GestureResult::ShadeClosed;
            if (vy > gesture::kShadeFling) return GestureResult::Cancelled;
            return mGesture.progress >= gesture::kShadeCommit ? GestureResult::ShadeClosed
                                                              : GestureResult::Cancelled;
        default:
            return GestureResult::Cancelled;
        }
    }

    Navigator::Feed Navigator::feed(const TouchSample &s, NavState nav, bool shadeOpen, double w, double h)
    {
        Feed out;
        if (s.kind == TouchSample::Kind::Down)
        {
            mPressed = true;
            mDownX = s.x;
            mDownY = s.y;
            mNavAtDown = nav;
            mSamples.clear();
            mSamples.push_back(s);
            mGesture = GestureModel{};
            mEdge = 0;
            // One candidate per press, most specific zone first. The shade outranks the rest
            // while it is open: it covers the whole screen, so nothing under it may be reached.
            if (shadeOpen)
                mCandidate = s.y >= h - gesture::kShadeCloseZone ? GestureKind::ShadeClose : GestureKind::None;
            else if (s.y <= gesture::kTopZone)
                mCandidate = GestureKind::ShadeOpen;
            else if (s.y >= h - gesture::kBottomZone)
                mCandidate = GestureKind::Home;
            else if (nav == NavState::App && (s.x <= gesture::kSideZone || s.x >= w - gesture::kSideZone))
            {
                mCandidate = GestureKind::Back;
                mEdge = s.x <= gesture::kSideZone ? -1 : 1;
            }
            else
                mCandidate = GestureKind::None;
            return out;
        }
        if (!mPressed) return out;

        mSamples.push_back(s);
        if (mSamples.size() > 64) mSamples.erase(mSamples.begin());
        const double dx = s.x - mDownX, dy = s.y - mDownY;

        if (mGesture.kind == GestureKind::None && mCandidate != GestureKind::None &&
            s.kind == TouchSample::Kind::Move)
        {
            // Claim only on travel in the gesture's own direction, and only when that direction
            // dominates — a sideways scroll that starts on the bottom edge stays the app's.
            bool claim = false;
            switch (mCandidate)
            {
            case GestureKind::Home:
            case GestureKind::ShadeClose: claim = -dy > gesture::kSlop && -dy > std::fabs(dx); break;
            case GestureKind::ShadeOpen: claim = dy > gesture::kSlop && dy > std::fabs(dx); break;
            case GestureKind::Back: claim = dx * -mEdge > gesture::kSlop && std::fabs(dx) > std::fabs(dy); break;
            default: break;
            }
            if (claim)
            {
                mGesture.kind = mCandidate;
                mGesture.startX = mDownX;
                mGesture.startY = mDownY;
                mGesture.edge = mEdge;
                out.claimedNow = true;
            }
        }

        if (mGesture.kind != GestureKind::None)
        {
            out.consumed = true;
            mGesture.dx = dx;
            mGesture.dy = dy;
            double p = 0.0;
            switch (mGesture.kind)
            {
            case GestureKind::Home: p = -dy / (h * gesture::kHomeFraction); break;
            case GestureKind::Back: p = std::fabs(dx) / gesture::kBackCommit; break;
            case GestureKind::ShadeOpen: p = dy / (h * 0.6); break;
            case GestureKind::ShadeClose: p = -dy / (h * 0.6); break;
            default: break;
            }
            mGesture.progress = std::clamp(p, 0.0, 1.0);
            mGesture.hint = decide(w, h);
        }

        if (s.kind == TouchSample::Kind::Up || s.kind == TouchSample::Kind::Cancel)
        {
            if (mGesture.kind != GestureKind::None)
            {
                out.ended = true;
                out.result = s.kind == TouchSample::Kind::Up ? decide(w, h) : GestureResult::Cancelled;
            }
            mPressed = false;
            mCandidate = GestureKind::None;
            mGesture = GestureModel{};
            mSamples.clear();
        }
        return out;
    }
}
}
