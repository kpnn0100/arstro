/*
 *  solaris_ui — InstrumentEditor: a Solaris instrument's own editor inside a VST3 host (R-VST-7).
 *
 *  Platform-free: the plugin's X11 view (`X11View.cpp`) feeds it pointer input, a size and a render
 *  target each frame; the tests feed it the same with no window. Its content IS Solaris's DevicePanel —
 *  generated from the registry (law 2) — for a model of ONE device built by `deviceModelOf` from the
 *  values the host holds, with the song's controls off (`songControls`). So a Basic Synth looks the same
 *  in Solaris's window and in any other host's.
 *
 *  The panel speaks text, as it does in Solaris (`set dv_1.<name>=<value>`); the editor turns each line
 *  into the host's edit through `ParamAccess` — `begin` once per parameter per press, `perform` per step,
 *  `end` when the pointer lets go (a click on a choice or a double-click's reset: all three at once).
 *  The plugin's side of `ParamAccess` normalises with the ONE shared mapping (REQ-device-7).
 *
 *  A value the HOST changes (automation, a preset, undo) arrives as a new model revision: the slider's
 *  thumb springs there (Artboard's Slider eases a programmatic value) and the row it changed is lit
 *  (R-WIN-2's last change, by anyone). While the pointer is down a gesture in flight outranks the host.
 */
#pragma once
#include "widgets/DevicePanel.h"
#include "input/GestureRecognizer.h"
#include "device/Device.h"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_ui
{
    /** What the editor edits: one device's values, in ENGINEERING units, through the host. */
    struct ParamAccess
    {
        virtual ~ParamAccess() = default;
        virtual double value(int index) const = 0;
        virtual void begin(int index) = 0;
        virtual void perform(int index, double value) = 0;
        virtual void end(int index) = 0;
        /** A key heard through the plugin (a pad clicked) — velocity 0 is its note-off. */
        virtual void play(int pitch, int velocity) { (void)pitch; (void)velocity; }
    };

    /** A kit's pads (R-VST-7, Drum Machine): one per key the registry names, in its order. A press plays the
     *  pad through the plugin and picks it — the panel scrolls to the parameters that shape it (the registry's
     *  `notePrefixes`); the release is its note-off. The pick and the hit's flash ease (design rule §1). */
    class PadGrid : public artboard::Segment
    {
    public:
        static constexpr int kColumns = 5;
        static constexpr double kPadH = 39.0;   // space::u(12)
        static constexpr double kGap = 6.5;     // space::u(2)
        static constexpr double kPad = 9.75;    // space::u(3): the panel's padding
        PadGrid(const DeviceType &type, ParamAccess &access);
        double contentHeight() const;           // what the grid needs at its width
        int padCount() const { return (int)mPads.size(); }
        artboard::Rect padRect(int i) const;    // local
        const std::string &padName(int i) const { return mPads[(size_t)i].name; }
        int padNote(int i) const { return mPads[(size_t)i].note; }
        const std::string &padPrefix(int i) const { return mPads[(size_t)i].prefix; }
        int picked() const { return mPicked; }
        double pickAmount(int i) const { return mPads[(size_t)i].pick.value(); }   // LIVE
        double flashAmount(int i) const { return mPads[(size_t)i].flash.value(); } // LIVE
        std::function<void(const std::string &prefix)> onPick;
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Pad
        {
            int note = 0;
            std::string name, prefix;
            artboard::AnimatedProperty pick{0.0}, flash{0.0};
            bool pickLast = false, hit = false;
        };
        int padAt(const artboard::Point &p) const;
        ParamAccess &mAccess;
        std::vector<Pad> mPads;
        int mPicked = -1, mHeld = -1;
        double mNowMs = 0.0;
    };

    class InstrumentEditor
    {
    public:
        // the size a host opens it at, and the least it may be resized to (R4: it lays out from its size)
        static constexpr double kWidth = 520.0, kHeight = 600.0, kMinWidth = 360.0, kMinHeight = 320.0;
        static constexpr const char *kDevice = "dv_1"; // the one device of its model

        InstrumentEditor(const DeviceType &type, ParamAccess &access);
        void setSize(double w, double h);
        double width() const { return mW; }
        double height() const { return mH; }

        /** kind 0 press, 1 move, 2 release; button 0 left, 2 right — as Solaris's App takes them. */
        void pointer(int kind, double x, double y, int button, double timeMs, bool shift = false, bool ctrl = false);
        void wheel(double x, double y, double notches, double timeMs);
        /** One frame: the host's values in, the panel advanced, laid out and drawn. */
        void render(artboard::IRenderTarget &t, double nowMs);

        DevicePanel &panel() { return *mPanel; }
        /** A kit's pads, over the panel (nullptr for a melodic instrument). */
        PadGrid *pads() { return mPads.get(); }
        const solaris::AppModel &model() const { return mModel; }
        const DeviceType &type() const { return mType; }

    private:
        void refresh();
        bool command(const std::string &line);

        const DeviceType &mType;
        ParamAccess &mAccess;
        std::shared_ptr<artboard::Segment> mRoot;
        std::shared_ptr<DevicePanel> mPanel;
        std::shared_ptr<PadGrid> mPads;
        artboard::GestureRecognizer mRecognizer;
        solaris::AppModel mModel;
        std::vector<double> mShown;     // the values the model was built from
        std::string mLast;              // the parameter changed last, by anyone (the lit row)
        std::set<int> mBegun;           // edits begun in this press
        std::map<int, double> mSent;    // … and the value each was last told, so a step that moved nothing says nothing
        bool mDown = false, mBound = false;
        double mW = kWidth, mH = kHeight;
    };
}
}
