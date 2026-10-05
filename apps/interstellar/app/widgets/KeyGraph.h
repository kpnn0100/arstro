/*
 *  interstellar_v1 — KeyGraph: the graph editor (R-ANIM-4, R-ANIM-7).
 *
 *  The animated properties the host chose, drawn TOGETHER over time — each curve normalised to its
 *  own fitted range (its keys and any overshoot, with a margin, eased when it changes and frozen
 *  while a drag is in flight), the FRONT curve on top with its value axis, the others quieter; a
 *  shape curve (R-ANIM-6) is a row of keys. Every curve is the very function the render path
 *  evaluates (model/Anim.h), sampled across the plot.
 *
 *  Keys are selected across curves — a click, Shift to add or remove, a box dragged on empty plot —
 *  and edited by direct manipulation, one line dispatched on release:
 *    * one key dragged: time and value (`key set <address> --at <t> --to <t'> --value <v>`), kept
 *      between its neighbours; a shape key moves in time only;
 *    * a selection of several dragged: all move in time together (`key shift --keys … --by dt`, one
 *      undo step);
 *    * the front key's bezier handle: its slope is the side's speed, its reach the influence
 *      (`key set … --speed-out s --influence-out i`, or -in);
 *    * double-click the plot: a key on the front curve there (`key add <address> --at <t>`);
 *    * right-click a key: `onKeyContext` (the app's key menu: presets, typed sides, copy, delete);
 *      right-click empty plot: `onPlotContext` (paste).
 *
 *  The time axis is the host's: the Cut key lane spans the selected clip on the timeline (its footage
 *  clock — a source's time inside the clip is the same seconds).
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include "../../model/Anim.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
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

        /** The curves `animIds` (from `m.anims`) over the window [t0, t1] on their clock; the first
         *  is the front unless the host named another (setFront). */
        void bind(const interstellar::AppModel &m, const std::vector<std::string> &animIds, double t0, double t1);
        /** Pin the plot's horizontal span to the host's axis, in local x (x0 < 0: the default padding). */
        void setPlotSpan(double x0, double x1) { mSpanX0 = x0; mSpanX1 = x1; }
        /** Without the chip header (a host that lists the parameters itself — the Cut key lane). */
        void setHeaderShown(bool on) { mHeaderH = on ? kHeaderH : 0.0; }
        /** What the plot says when nothing chosen is animated yet. */
        void setEmptyText(const std::string &s) { mEmptyText = s; }
        /** The host's "now" on the curves' clock (the key lane: the playhead inside the clip). */
        void setNow(double t) { mNowOverride = t; mHasNowOverride = true; }
        /** Bring the curve at `address` to the front (the host's last-chosen property). */
        void setFront(const std::string &address);
        void advance(double nowMs) override;

        std::function<bool(const std::string &)> onCommand;
        /** A right-click on key `t` of `address`, at `world` — the app opens the key's menu. */
        std::function<void(const std::string &address, double t, artboard::Point world)> onKeyContext;
        /** A right-click on empty plot at time `t` — the app offers paste. */
        std::function<void(double t, artboard::Point world)> onPlotContext;

        // ── the curves and the selection ──
        int curveCount() const { return (int)mCurves.size(); }
        int selected() const { return mSel; }   // the front curve's index
        std::string selectedAddress() const { return mSel >= 0 && mSel < (int)mCurves.size() ? mCurves[(size_t)mSel].address : std::string(); }
        int selectedKey() const;                // the front curve's key when exactly it is selected, else -1
        int selectionCount() const { return (int)mSelection.size(); }
        bool isSelected(int curve, int key) const;
        /** The selection as `key shift|copy --keys` takes it: "address@t,…". */
        std::string selectionList() const;
        void clearSelection() { mSelection.clear(); }
        /** Select key `key` of curve `curve` (add = keep the rest). */
        void selectKey(int curve, int key, bool add);
        bool shapeShown() const { return mSel >= 0 && mSel < (int)mCurves.size() && mCurves[(size_t)mSel].shape; }

        // ── geometry, for tests and the host ──
        artboard::Rect plotRect() const;
        artboard::Rect chipRect(int i) const;
        artboard::Point keyPoint(int key) const { return keyPointOf(mSel, key); }   // local, the front curve
        artboard::Point keyPointOf(int curve, int key) const;
        artboard::Point handlePoint(int key, bool out) const;  // local; the front curve's bezier side
        double rangeLo() const;                                 // the front curve's LIVE eased value axis
        double rangeHi() const;
        double rangeLoTarget() const;
        double timeAtX(double x) const;
        double xAt(double t) const;
        double yAt(double v) const { return yOf(mSel, v); }
        bool boxing() const { return mDrag.kind == 5; }
        artboard::Rect boxRect() const;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;

    private:
        struct Range
        {
            artboard::AnimatedProperty lo{0.0}, hi{1.0};
            double loT = 0, hiT = 1, loL = 0, hiL = 1;
            bool placed = false;
        };
        std::vector<anim::Key> keysOf(int curve) const;
        const std::vector<anim::Key> &liveKeys(int curve) const;   // the drag's preview, or the model's
        void fitRange(int curve);
        double yOf(int curve, double v) const;
        int shapeRank(int curve) const;
        std::string label(int curve) const;
        bool keyAt(const artboard::Point &p, int &curve, int &key) const;
        int handleAt(const artboard::Point &p, bool &out) const;
        std::pair<std::string, long long> keyId(int curve, int key) const;

        std::vector<interstellar::AnimModel> mCurves;
        std::map<std::string, Range> mRanges;          // by curve id
        int mSel = 0;
        std::string mFront;                            // the front curve's address, as the host asked
        double mT0 = 0, mT1 = 1, mNow = 0;
        double mSpanX0 = -1, mSpanX1 = -1;
        double mHeaderH = kHeaderH;
        double mNowOverride = 0.0;
        bool mHasNowOverride = false;
        std::string mEmptyText = "Nothing animated here \xE2\x80\x94 click \xE2\x97\x87 beside a property to key it";
        artboard::AnimatedProperty mSwitch{1.0};
        std::string mShownId;
        cosmo_v2::HoverFade mChipHover;
        std::set<std::pair<std::string, long long>> mSelection;   // (address, time in ms)
        struct Drag
        {
            int kind = 0;               // 0 none · 1 one key · 2 in handle · 3 out handle · 4 a selection in time · 5 a box
            int curve = -1, key = -1;
            bool moved = false;
            artboard::Point start, now;
            double dt = 0.0;            // kind 4: the selection's time offset
            std::map<int, std::vector<anim::Key>> keys;   // the live preview, per curve
        } mDrag;
        mutable std::vector<anim::Key> mModelKeys;
    };
}
}
