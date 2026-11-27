#include "Scene.h"
#include "../kit/Theme.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace shell
{
    namespace
    {
        double lerp(double a, double b, double t) { return a + (b - a) * t; }
        double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
        constexpr double kSlop = 8.0;
    }

    Scene::Card *Scene::card(int task)
    {
        for (Card &c : mCards)
            if (c.task == task && !c.dying) return &c;
        return nullptr;
    }

    double Scene::presenceOf(int task) const
    {
        for (const Card &c : mCards)
            if (c.task == task) return c.presence.value();
        return 0.0;
    }

    double Scene::pitch(double w) const
    {
        const double z = mZoom.value();
        return w * lerp(1.0, kRecentsScale, z) + lerp(40.0, kRecentsGap, z);
    }

    artboard::Rect Scene::rowRect(const Card &c, double w, double h) const
    {
        const double z = mZoom.value(), s = lerp(1.0, kRecentsScale, z);
        const double cw = w * s, ch = h * s;
        // Older cards sit to the left, as on Android; the row slides under a fixed centre.
        const double cx = w * 0.5 - (c.slot.value() - mScroll.value()) * pitch(w) + mLiftX.value();
        const double cy = h * 0.5 + lerp(0.0, 12.0, z) + mLiftY.value() + c.dismiss.value();
        return {cx - cw * 0.5, cy - ch * 0.5, cw, ch};
    }

    void Scene::cancelTouch()
    {
        if (mDrag == Drag::Dismiss)
            if (Card *c = card(mDragTask)) c->dismiss.setTarget(0.0);
        mDrag = Drag::None;
    }

    void Scene::recentsTouch(const TouchSample &s, const Model &m, std::vector<std::string> &commands)
    {
        const double w = m.screenW, h = m.screenH;
        if (s.kind == TouchSample::Kind::Down)
        {
            mDrag = Drag::Pending;
            mDragTask = 0;
            mDownX = mLastX = s.x;
            mDownY = mLastY = s.y;
            mLastT = s.tMs;
            mVelX = mVelY = 0.0;
            mDownScroll = mScroll.value();
            for (const Card &c : mCards)
                if (!c.dying && rowRect(c, w, h).contains({s.x, s.y})) mDragTask = c.task;
            return;
        }
        if (mDrag == Drag::None) return;
        const double dx = s.x - mDownX, dy = s.y - mDownY;
        if (s.tMs > mLastT)
        {
            const double dt = (s.tMs - mLastT) / 1000.0;
            // A light low-pass: one sample of touch noise must not decide a fling.
            mVelX = lerp(mVelX, (s.x - mLastX) / dt, 0.5);
            mVelY = lerp(mVelY, (s.y - mLastY) / dt, 0.5);
            mLastX = s.x;
            mLastY = s.y;
            mLastT = s.tMs;
        }
        if (s.kind == TouchSample::Kind::Move)
        {
            if (mDrag == Drag::Pending && std::hypot(dx, dy) > kSlop)
                mDrag = (std::fabs(dy) > std::fabs(dx) && mDragTask && dy < 0) ? Drag::Dismiss : Drag::Scroll;
            if (mDrag == Drag::Scroll)
            {
                const double n = std::max(0, (int)m.tasks.size() - 1);
                double raw = mDownScroll + dx / pitch(w);
                if (raw < 0) raw *= 0.35;                 // rubber band past either end
                if (raw > n) raw = n + (raw - n) * 0.35;
                mScroll.reset(raw);
                mScrollTarget = raw;
            }
            else if (mDrag == Drag::Dismiss)
            {
                if (Card *c = card(mDragTask)) c->dismiss.reset(std::min(0.0, dy));
            }
            return;
        }
        // Up or Cancel
        const Drag drag = mDrag;
        mDrag = Drag::None;
        if (s.kind == TouchSample::Kind::Cancel) { cancelTouch(); return; }
        if (drag == Drag::Pending)
        {
            // A tap: a card comes to the front; empty space leaves recents.
            commands.push_back(mDragTask ? "task focus " + std::to_string(mDragTask) : std::string("nav back"));
        }
        else if (drag == Drag::Scroll)
        {
            const double n = std::max(0, (int)m.tasks.size() - 1);
            // Land on a card: where the fling would carry the row, rounded to the nearest.
            mScrollTarget = std::clamp(std::round(mScroll.value() + mVelX / pitch(w) * 0.18), 0.0, n);
        }
        else if (drag == Drag::Dismiss)
        {
            if (Card *c = card(mDragTask))
            {
                if (dy < -h * 0.16 || mVelY < -700.0) commands.push_back("app close " + std::to_string(c->task));
                else c->dismiss.setTarget(0.0);
            }
        }
    }

    void Scene::advance(const Model &m, double dt, const IconRect &iconRect)
    {
        const double w = m.screenW, h = m.screenH;
        mW = w;
        mH = h;
        const NavState nav = m.nav;

        // ── reconcile the cards with the task list ───────────────────────────────────────────
        for (size_t i = 0; i < m.tasks.size(); ++i)
        {
            const Task &t = m.tasks[i];
            Card *c = card(t.id);
            if (!c)
            {
                Card fresh;
                fresh.task = t.id;
                fresh.app = t.appId;
                fresh.slot.reset((double)i);  // a first placement: nowhere to travel from
                mCards.push_back(fresh);
                c = &mCards.back();
            }
            c->slot.setTarget((double)i);
        }
        for (Card &c : mCards)
            if (!c.dying && !m.task(c.task))
            {
                // The task is gone but its card is still on screen: it finishes its exit — carried
                // on upward if it was flicked — and is erased only once it is invisible.
                c.dying = true;
                c.toIcon = false;
                c.dir = -1;
                c.dismiss.setTarget(c.dismiss.value() - h * 0.35);
            }

        // ── a change of front: decide HOW each card travels, once, at the moment of the change ──
        if (mFirst || nav != mPrevNav || m.focusedTask != mPrevFocused)
        {
            const bool noneVisible = std::none_of(mCards.begin(), mCards.end(),
                                                  [](const Card &c) { return c.presence.value() > 0.02; });
            if (nav == NavState::App)
            {
                if (mPrevNav == NavState::Home && noneVisible) mZoom.reset(0.0);
                for (Card &c : mCards)
                {
                    c.toIcon = false;
                    c.dir = 1;
                    if (c.task == m.focusedTask && mPrevNav == NavState::Home && !c.dying)
                        c.toIcon = iconRect && iconRect(c.app, c.icon);  // it grows out of its icon
                }
            }
            else if (nav == NavState::Recents)
            {
                if (mPrevNav == NavState::Home && noneVisible) mZoom.reset(1.0);
                for (Card &c : mCards) { c.toIcon = false; c.dir = 1; }
                mScrollTarget = 0.0;
            }
            else  // Home
            {
                for (Card &c : mCards)
                {
                    c.toIcon = false;
                    c.dir = -1;
                    if (c.task == mPrevFocused && mPrevNav == NavState::App && !c.dying)
                        c.toIcon = iconRect && iconRect(c.app, c.icon);  // it flies back into its icon
                }
            }
            if (nav != NavState::Recents) mScrollTarget = 0.0;
            mPrevNav = nav;
            mPrevFocused = m.focusedTask;
            mFirst = false;
        }

        // ── where everything is heading ──────────────────────────────────────────────────────
        if (nav == NavState::Recents) mZoom.setTarget(1.0);
        else if (nav == NavState::App) mZoom.setTarget(0.0);
        mHome.setTarget(nav == NavState::Home ? 1.0 : 0.0);
        mShade.setTarget(m.shadeOpen ? 1.0 : 0.0);
        mBack.setTarget(0.0);
        mBackPull.setTarget(0.0);
        mLiftX.setTarget(0.0);
        mLiftY.setTarget(0.0);
        if (mDrag != Drag::Scroll) mScroll.setTarget(mScrollTarget);
        mEmpty.setTarget(nav == NavState::Recents && m.tasks.empty() ? 1.0 : 0.0);
        for (Card &c : mCards)
            if (!(mDrag == Drag::Dismiss && c.task == mDragTask))
                c.presence.setTarget(c.dying || nav == NavState::Home ? 0.0 : 1.0);

        // ── a gesture in flight: the finger is the animation ─────────────────────────────────
        const GestureModel &g = m.gesture;
        const double up = -g.dy;
        if (g.kind == GestureKind::Home)
        {
            if (nav == NavState::App)
            {
                mZoom.reset(clamp01(up / (h * 0.30)));
                mLiftY.reset(-std::max(0.0, up) * 0.30);
                mLiftX.reset(g.dx * 0.30);
                mHome.setTarget(g.hint == GestureResult::Home ? 1.0 : 0.0);
            }
            else if (nav == NavState::Home)
            {
                if (!mGestureSeen)
                {
                    mZoom.reset(1.0);
                    for (Card &c : mCards) { c.toIcon = false; c.dir = 1; }
                }
                const double p = clamp01(up / 160.0);
                for (Card &c : mCards)
                    if (!c.dying) c.presence.reset(p);
                mHome.reset(1.0 - p);
            }
            else
            {
                const double p = 1.0 - clamp01(up / 220.0);
                for (Card &c : mCards)
                    if (!c.dying) { c.dir = -1; c.presence.reset(p); }
                mHome.setTarget(g.hint == GestureResult::Home ? 1.0 : 0.0);
            }
            mGestureSeen = true;
        }
        else
        {
            mGestureSeen = false;
            if (g.kind == GestureKind::ShadeOpen) mShade.reset(clamp01(g.dy / (h * 0.6)));
            else if (g.kind == GestureKind::ShadeClose) mShade.reset(1.0 - clamp01(up / (h * 0.6)));
            else if (g.kind == GestureKind::Back)
            {
                mBack.reset(g.progress);
                mBackPull.reset(std::min(std::fabs(g.dx), 72.0));
                mBackEdge = g.edge;
                mBackY = g.startY;
            }
        }

        // ── move ─────────────────────────────────────────────────────────────────────────────
        const double nav_w = kit::motion::springNav(), fade_w = kit::motion::springFade();
        mZoom.advance(dt, nav_w);
        mHome.advance(dt, fade_w);
        mShade.advance(dt, nav_w);
        mScroll.advance(dt, nav_w);
        mLiftX.advance(dt, nav_w);
        mLiftY.advance(dt, nav_w);
        mBack.advance(dt, fade_w);
        mBackPull.advance(dt, nav_w);
        mEmpty.advance(dt, fade_w);
        mAnimating = g.kind != GestureKind::None || mDrag != Drag::None || mZoom.isMoving() || mHome.isMoving() ||
                     mShade.isMoving() || mScroll.isMoving() || mLiftX.isMoving(0.05) || mLiftY.isMoving(0.05) ||
                     mBack.isMoving() || mEmpty.isMoving();
        for (Card &c : mCards)
        {
            c.presence.advance(dt, nav_w);
            c.slot.advance(dt, nav_w);
            c.dismiss.advance(dt, nav_w);
            mAnimating = mAnimating || c.presence.isMoving() || c.slot.isMoving() || c.dismiss.isMoving(0.05);
        }
        mCards.erase(std::remove_if(mCards.begin(), mCards.end(),
                                    [](const Card &c) { return c.dying && c.presence.value() < 0.004; }),
                     mCards.end());

        // ── poses ────────────────────────────────────────────────────────────────────────────
        mPoses.clear();
        const double z = mZoom.value();
        const CardPose *front = nullptr;
        for (const Card &c : mCards)
        {
            const double p = clamp01(c.presence.value());
            CardPose pose;
            pose.task = c.task;
            pose.app = c.app;
            artboard::Rect row = rowRect(c, w, h);
            double radius = lerp(0.0, kit::radius::tile(), clamp01(z * 3.0));
            if (c.toIcon)
            {
                // The container travels between the row and the icon, fading as it goes, and has
                // faded out exactly when it reaches the icon. The ramp is this long on purpose: a
                // shorter one (p / 0.6) was measured dropping 0.07 of alpha in one 8 ms frame where
                // the spring is fastest — a pop with a curve on it (sceneTests, never_pops).
                const double k = 1.0 - p;
                pose.rect = {lerp(row.x, c.icon.x, k), lerp(row.y, c.icon.y, k), lerp(row.w, c.icon.w, k), lerp(row.h, c.icon.h, k)};
                pose.radius = lerp(std::max(radius, kit::radius::tile() * clamp01(k * 4.0)), c.icon.w * kit::radius::icon(), k);
                pose.alpha = clamp01(p / 0.85);
                pose.iconMix = k;
            }
            else
            {
                const double u = lerp(0.94, 1.0, p);  // uniform
                const double cx = row.x + row.w * 0.5, cy = row.y + row.h * 0.5 + c.dir * (1.0 - p) * 48.0;
                pose.rect = {cx - row.w * u * 0.5, cy - row.h * u * 0.5, row.w * u, row.h * u};
                pose.radius = radius;
                pose.alpha = p;
            }
            // A flicked card also fades with the distance it has been thrown.
            if (c.dismiss.value() < 0.0) pose.alpha *= clamp01(1.0 + c.dismiss.value() / (h * 0.45));
            // The name chip belongs to the recents row: it is gone well before a card reaches its icon.
            pose.header = clamp01(z * 2.0 - 1.0) * pose.alpha * clamp01(1.0 - pose.iconMix * 4.0);
            if (pose.alpha <= 0.004) continue;
            if (pose.rect.right() < -8 || pose.rect.x > w + 8 || pose.rect.bottom() < -8 || pose.rect.y > h + 8) continue;
            mPoses.push_back(pose);
        }
        // The focused card is drawn last, so while the row zooms it is the one on top.
        std::stable_sort(mPoses.begin(), mPoses.end(), [&](const CardPose &a, const CardPose &b) {
            return (a.task == m.focusedTask) < (b.task == m.focusedTask);
        });
        (void)front;
    }
}
}
