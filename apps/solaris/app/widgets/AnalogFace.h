/*
 *  solaris_ui — AnalogFace: an instrument's parameters as a hardware synth panel (R-UI-12) — the body of
 *  its DevicePanel in Solaris's window and in the plugins' own editor (R-VST-7), so a Basic Synth looks the
 *  same in either.
 *
 *  Still GENERATED from the registry (law 2, R-UI-5): nothing here knows a synth from a drum machine.
 *  Every parameter group (the prefix before a name's dot) is a MODULE, titled by the registry's group label
 *  (`devices[].groups`, REQ-device-10), in the registry's order with the ungrouped last; a number is a KNOB
 *  (a 270° dial, its arc in the accent from the minimum — or from zero for a range that spans it — detents
 *  for a whole-number range of a few steps); a choice is a SWITCH (a row of keys, each the choice's shape
 *  or its name, the chosen one lit by a light that slides). A module whose parameters say so gets a
 *  SCREEN: a `wave` choice draws the oscillator (its level, its octave, its unison voices spread by the
 *  detune); `cutoff` + `res` draw the filter's response (the DSP library's own documented prototype,
 *  `responseDb`, measured against the filter by the plugin tests); `attack` `decay` `sustain` `release`
 *  draw the envelope (linear stages, as the library's ADSR). A parameter the library adds tomorrow is a
 *  knob in its module with no code here.
 *
 *  Modules flow left to right and wrap with the width (design rule R4); a row's modules share its height
 *  and its spare width. Every value is drawn eased (design rule §1): a value set anywhere turns its knob
 *  there and every screen redraws through the turning knob; the light of a switch slides, so a screen
 *  morphs between shapes; the last change is lit (R-WIN-2). A drag is direct manipulation — the knob is
 *  the pointer's — and each step that moved the value is ONE `set` line through the panel, as a slider's
 *  was. A bound parameter (a formula, an automation) names its binding under its knob, follows its live
 *  value and takes no drag (its menu clears it). Show IDs puts each address over its label (R-UI-11).
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_ui
{
    class DevicePanel;

    class AnalogFace : public artboard::Segment
    {
    public:
        // the module ladder (space::u = 3.25 px)
        static constexpr double kDial = 39.0;       // u(12): a knob's diameter
        static constexpr double kCellW = 65.0;      // u(20): a knob's column
        static constexpr double kCellH = 78.0;      // u(24): its dial, its label, its value, and air under them
        static constexpr double kKeysH = 22.75;     // u(7): a switch's keys
        static constexpr double kSwitchH = 45.5;    // u(14): the keys and the label under them
        static constexpr double kKeyGap = 3.25;     // u(1)
        static constexpr double kKeyMinW = 29.25;   // u(9)
        static constexpr double kKeyMaxW = 42.25;   // u(13)
        static constexpr double kScreenH = 58.5;    // u(18): a module's screen
        static constexpr double kTitleH = 26.0;     // u(8)
        static constexpr double kPad = 9.75;        // u(3)
        static constexpr double kGap = 6.5;         // u(2): between modules, and a screen's margin
        static constexpr int kMaxCols = 4;
        static constexpr double kTurnPx = 195.0;    // u(60): a drag this far is the whole turn; Shift is 5× finer
        enum class Screen { None, Wave, Filter, Envelope };

        explicit AnalogFace(DevicePanel &owner);
        void build(const solaris::DeviceModel &d);
        void bind(const solaris::DeviceModel &d, const std::vector<solaris::BindingModel> &bindings);
        /** The modules flowed into `width`, in content coordinates (before scrolling); the content's height. */
        double layoutFor(double width);
        double contentHeight() const { return mContentH; }
        double scroll = 0.0;                        // the panel's eased scroll, set each frame
        void advance(double nowMs) override;

        // ── what a test aims at and reads (LOCAL to the face, scrolled) ──
        int controlCount() const { return (int)mControls.size(); }
        int controlIndex(const std::string &param) const;
        std::string controlParam(int i) const;
        bool isKnob(int i) const { return i >= 0 && i < controlCount() && mControls[(size_t)i].knob; }
        artboard::Rect controlRect(int i) const;    // the whole cell
        artboard::Rect dialRect(int i) const;       // a knob's dial; a switch's keys
        artboard::Rect keyRect(int i, int choice) const;
        double contentTop(int i) const;             // unscrolled, for revealing it
        int sectionCount() const { return (int)mSections.size(); }
        std::string sectionTitle(int s) const { return mSections[(size_t)s].title; }
        std::string sectionPrefix(int s) const { return mSections[(size_t)s].prefix; }
        artboard::Rect sectionRect(int s) const;
        Screen sectionScreen(int s) const { return mSections[(size_t)s].screen; }
        double sectionTop(const std::string &prefix) const; // unscrolled; −1 when there is none
        double turnLive(const std::string &param) const;    // a knob's DRAWN turn 0…1 (eased)
        double turnTarget(const std::string &param) const;  // … and where it is going
        double lightLive(const std::string &param) const;   // a switch's light, as a choice index (slides)
        double litAmount(const std::string &param) const;   // the last change's light (eased)
        std::string valueText(const std::string &param) const;
        int controlAt(const artboard::Point &local) const;  // −1: none
        bool dragging() const { return mDrag >= 0; }

        /** The filter screen's curve: the DSP library's documented analog prototype of its state-variable
         *  filter — StateVariableFilter (S4) LP 1/D, BP s/D, HP s²/D, Notch (s²+1)/D with D = s² + s/Q + 1,
         *  s = j·w, w = f/fc — at (S5)'s Q(res) = 0.7071·2^(5.5·res). Public: the plugin tests measure the
         *  real filter against it, so the screen cannot drift from the sound. */
        static double responseDb(const std::string &mode, double w, double res);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Control
        {
            solaris::ParamModel spec;               // value, formula — the model's
            int section = 0;
            bool knob = true;                       // a number; else a switch (a choice)
            artboard::Rect cell;                    // content coordinates
            double turn = 0.0, turnLast = 0.0;      // a knob: where it is going (0…1) …
            artboard::AnimatedProperty shown{0.0};  // … and where it is DRAWN
            artboard::AnimatedProperty light{0.0};  // a switch: the light, as a choice index
            int choice = 0, choiceLast = 0;
            artboard::AnimatedProperty lit{0.0};
            bool placed = false, litLast = false;
            bool bound = false;                     // a formula or an automation decides it
        };
        struct Section
        {
            std::string prefix, title;
            Screen screen = Screen::None;
            std::vector<int> controls;              // in the registry's order
            int knobs = 0, switches = 0, cols = 1, rows = 0;
            double naturalW = 0.0, naturalH = 0.0;
            artboard::Rect box;                     // content coordinates
        };
        int find(const std::string &section, const std::string &suffix, int s) const; // a control of section s by its name's tail
        double liveValue(int i) const;              // a control's DRAWN value, engineering units
        void paintKnob(artboard::IRenderTarget &t, const Control &c, int i, const artboard::Rect &cell) const;
        void paintSwitch(artboard::IRenderTarget &t, const Control &c, int i, const artboard::Rect &cell) const;
        void paintScreen(artboard::IRenderTarget &t, const Section &s, int si, const artboard::Rect &r) const;
        void send(const Control &c, const std::string &value) const;

        DevicePanel &mOwner;
        std::vector<Control> mControls;
        std::vector<Section> mSections;
        std::string mLastChanged;
        double mContentH = 0.0, mWidth = 0.0, mNowMs = 0.0;
        int mPress = -1, mDrag = -1;
        double mPressY = 0.0, mPressTurn = 0.0;
        std::string mSent;                          // the last value a drag sent (a step that moved nothing says nothing)
        cosmo_v2::HoverFade mHover;
    };
}
}
