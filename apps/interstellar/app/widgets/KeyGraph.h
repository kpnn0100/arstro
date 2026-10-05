/*
 *  interstellar_v1 — KeyGraph: the graph editor (R-ANIM-4).
 *
 *  The animated parameters of the selection, one at a time, as their value curves over time — the
 *  very function the render path evaluates (model/Anim.h), sampled across the plot. Chips in the
 *  header pick the curve; the plot shows its keys as diamonds and, for the selected key, its bezier
 *  handles. Everything here is direct manipulation dispatched as one line on release:
 *
 *    * drag a key — time and value (`key set <address> --at <t> --to <t'> --value <v>`), kept
 *      between its neighbours;
 *    * drag a handle — the side's speed (the handle's slope) and influence (how far into the
 *      segment it reaches): `key set … --speed-out s --influence-out i` (or -in);
 *    * double-click the plot — a key at that time, on the curve (`key add <address> --at <t>`);
 *    * right-click a key — `onKeyContext`, where the app opens the presets, "Speed & Influence…"
 *      (typed numbers) and Delete.
 *
 *  The time axis is the host's: Grade lines it up with the reference-frame track (source time over
 *  the whole source), the Cut inspector spans the clip's own footage time. The value axis fits the
 *  selected curve (its keys and any overshoot) with a margin, EASED when it changes — and frozen
 *  while a drag is in flight, so the curve under the pointer never rescales away from it. Switching
 *  curves cross-fades the plot.
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include "../../model/Anim.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    namespace anim = interstellar::anim;

    class KeyGraph : public artboard::Segment
    {
    public:
        static constexpr double kHeaderH = 22.75;

        KeyGraph();

        /** The curves `animIds` (from `m.anims`) over the window [t0, t1] on their clock. */
        void bind(const interstellar::AppModel &m, const std::vector<std::string> &animIds, double t0, double t1);
        /** Pin the plot's horizontal span to the host's axis, in local x (x0 < 0: the default padding). */
        void setPlotSpan(double x0, double x1) { mSpanX0 = x0; mSpanX1 = x1; }
        /** Without the chip header (a host that lists the parameters itself — the Cut key lane). */
        void setHeaderShown(bool on) { mHeaderH = on ? kHeaderH : 0.0; }
        /** What the plot says when the parameter has no curve yet. */
        void setEmptyText(const std::string &s) { mEmptyText = s; }
        void advance(double nowMs) override;

        std::function<bool(const std::string &)> onCommand;
        /** A right-click on key `t` of `address`, at `world` — the app opens the key's menu. */
        std::function<void(const std::string &address, double t, artboard::Point world)> onKeyContext;

        // ── read-only geometry and state, for tests and the host ──
        int curveCount() const { return (int)mCurves.size(); }
        int selected() const { return mSel; }
        std::string selectedAddress() const { return mSel >= 0 && mSel < (int)mCurves.size() ? mCurves[(size_t)mSel].address : std::string(); }
        int selectedKey() const { return mSelKey; }
        artboard::Rect plotRect() const;
        artboard::Rect chipRect(int i) const;
        artboard::Point keyPoint(int key) const;               // local, on the selected curve
        artboard::Point handlePoint(int key, bool out) const;  // local; meaningful for a bezier side
        double rangeLo() const { return mLo.value(); }          // the LIVE eased value axis
        double rangeHi() const { return mHi.value(); }
        double rangeLoTarget() const { return mLoT; }
        double timeAtX(double x) const;
        double xAt(double t) const;
        double yAt(double v) const;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;

    private:
        std::vector<anim::Key> keysOf(int curve) const;
        const std::vector<anim::Key> &liveKeys() const;   // the drag's preview, or the model's
        void fitRange(bool place);
        std::string label(int curve) const;
        int keyAt(const artboard::Point &p) const;
        int handleAt(const artboard::Point &p, bool &out) const;

        std::vector<interstellar::AnimModel> mCurves;
        std::vector<std::string> mIds;
        int mSel = 0, mSelKey = -1;
        double mT0 = 0, mT1 = 1, mNow = 0;
        double mSpanX0 = -1, mSpanX1 = -1;
        double mHeaderH = kHeaderH;
        std::string mEmptyText = "Nothing animated here \xE2\x80\x94 click \xE2\x97\x87 beside a parameter to key it";
        artboard::AnimatedProperty mLo{0.0}, mHi{1.0}, mSwitch{1.0};
        double mLoT = 0, mHiT = 1, mLoL = 0, mHiL = 1;
        bool mRangePlaced = false, mSwitchWanted = false;
        std::string mShownId;
        cosmo_v2::HoverFade mChipHover;
        struct Drag
        {
            int kind = 0;               // 0 none · 1 key · 2 in handle · 3 out handle
            int key = -1;
            bool moved = false;
            std::vector<anim::Key> keys;   // the live preview
        } mDrag;
        mutable std::vector<anim::Key> mModelKeys;
    };
}
}
