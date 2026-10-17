/*
 *  solaris_ui — SessionView: the session grid, in the lanes' place (View › Session; R-LAUNCH-1, docs/discussion.md
 *  Round 17).
 *
 *  A column per strip that can hold a slot (instrument and audio strips, in processing order: its colour and name), a row
 *  per scene (its name and ▶, at the left), a slot's name in its cell. Every gesture is ONE line: a slot clicked is
 *  `launch <sl>`; a scene's ▶ `launch <sc>`; a column's ■ `launch stop <ch>`; Back to Arrangement `launch back`; an empty
 *  cell offers the song's patterns (an instrument's column) or the strip's clips (an audio column) → `slot add`; a
 *  slot's or a scene's right-click Rename… and Delete; "+ Scene" `scene add`. It draws only the model: what the
 *  service says plays (`transport.launched`).
 *
 *  Nothing snaps (§1): cells are keyed by slot id — a slot playing fills with its strip's colour, eased in and out, its
 *  loop's progress a bar along its foot (the model's, frame by frame, like the playhead); a queued one pulses until it
 *  lands (a timing, not a tween), its pulse fading in and out. The grid clips and scrolls both ways (the wheel; Shift for
 *  sideways), eased.
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "../../../interstellar/app/widgets/EasedScroll.h"
#include "../../../cosmo/widgets/ContextMenu.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_ui
{
    class SessionView : public artboard::Segment
    {
    public:
        static constexpr double kHeadH = 32.0, kSceneW = 136.0, kColW = 132.0, kRowH = 40.0, kFootH = 36.0;

        SessionView();
        void bind(const solaris::AppModel &m);

        std::function<bool(const std::string &line)> onCommand;
        std::function<void(std::vector<cosmo_v2::ContextMenu::Item> items, artboard::Point world)> onMenu;
        std::function<void(const std::string &current, artboard::Point world, std::function<void(const std::string &)> done)> onRename;

        // what a test aims at (local coordinates, as laid out now) and the LIVE eased values it catches mid-way
        artboard::Rect cellRect(const std::string &scene, const std::string &strip) const;
        artboard::Rect sceneLaunchRect(const std::string &scene) const;
        artboard::Rect stopRect(const std::string &strip) const;
        artboard::Rect backRect() const;
        artboard::Rect addSceneRect() const;
        double playAmount(const std::string &slot) const;    // 0 … 1: filled as playing
        double queuedAmount(const std::string &slot) const;  // 0 … 1: its pulse's strength (waiting to land)
        double scrollY() const { return mScrollY.value(); }

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Live
        {
            artboard::AnimatedProperty play{0.0}, queued{0.0};
            bool playWas = false, queuedWas = false, placed = false;
        };
        std::vector<solaris::StripModel> mCols;         // the strips a slot can be for, in processing order
        std::vector<solaris::SceneModel> mScenes;
        std::vector<solaris::SlotModel> mSlots;
        std::vector<solaris::PatternModel> mPatterns;
        std::vector<solaris::ClipModel> mClips;
        std::vector<solaris::LaunchModel> mLaunched;
        std::map<std::string, Live> mLive;              // by slot id
        interstellar_v1::EasedScroll mScrollX, mScrollY;
        cosmo_v2::HoverFade mHover;
        double mNowMs = 0.0;
        const solaris::SlotModel *slotAt(const std::string &scene, const std::string &strip) const;
        int colOf(const std::string &strip) const;
        int rowOf(const std::string &scene) const;
        int hoverId(const artboard::Point &p) const;
        void emptyCellMenu(const std::string &scene, const solaris::StripModel &strip, artboard::Point world);
        bool send(const std::string &line) { return onCommand && onCommand(line); }
    };
}
}
