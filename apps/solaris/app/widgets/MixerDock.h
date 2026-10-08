/*
 *  solaris_ui — MixerDock: the mixer, docked under the lanes (R-UI-3, R-MIX, R-UI-5, R-UI-7).
 *
 *  A tab per mixer page (the song's order) and a **Matrix** tab; "+" adds a mixer. A page is a row
 *  of strip cards in processing order, the **master** pinned at the right. A card, top to bottom:
 *  its colour and name ("audio · 3 clips", "bus · fed by 4"); its rack as chips (a chip opens the
 *  device's panel, "+ Effect" offers the registry's effects); its sends; pan; the fader (dB, a
 *  fader law that puts 0 dB at 70 %) beside an L/R meter; mute and solo; and where it goes ("→
 *  Main" — a click offers exactly `strips[].targets`, the service's forward-only list). A strip a
 *  solo silences dims. Strips feeding the same bus are grouped under a header naming it, and a
 *  click FOLDS the group into that header (R-MIX-12) — thirty one-shots become one column.
 *
 *  The Matrix (R-MIX-9): a row per strip, grouped by mixer; a column per strip that can receive
 *  (any strip not on the first mixer), the master and every output port. A cell shows the main
 *  output (●) or a send's level (and P when pre-fader); a cell routing could not take (backward,
 *  R-MIX-4) is hatched. A click on an open cell adds a send, on a send offers pre/post, main
 *  output and remove; a vertical drag on a send sets its level.
 *
 *  Everything is a command line (`onCommand`): `set <ch>.gain|pan|mute|solo=…`,
 *  `set project.masterGain=…`, `route`, `send add|delete`, `set <sd>.gain|pre=…`, `device add`,
 *  `mixer add|delete`, `set <mx|ch>.name=…`. Folding, the shown tab, scroll and the dock's height
 *  are the view's.
 *
 *  Nothing snaps (§1). Every value the model sets is drawn through an eased copy — a gain or a pan
 *  set from a shell travels, mute and solo fills fade, a colour cross-fades, meters rise in 40 ms
 *  and fall in 300. A dragged fader, pan or send follows the pointer exactly (the one exemption)
 *  and the line it sends lands where it was let go. Cards are keyed: one that arrives grows from
 *  nothing, one that goes shrinks away, one that moves shrinks where it was and grows where it is;
 *  a fold eases the members' width. Tabs are keyed the same way; the highlight slides; pages
 *  cross-fade.
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "DevicePanel.h"
#include "../../../interstellar/app/widgets/EasedScroll.h"
#include "../../../cosmo/widgets/ContextMenu.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_ui
{
    class MixerDock : public artboard::Segment
    {
    public:
        static constexpr double kTabsH = 29.25;      // space::u(9): the browser's tab bar
        static constexpr double kStripW = 84.5;      // space::u(26)
        static constexpr double kMasterW = 97.5;     // space::u(30)
        static constexpr double kFoldW = 29.25;      // space::u(9): an open group's header
        static constexpr double kMinBodyH = 370.5;   // space::u(114): shorter, and the page scrolls (the fader keeps 100 px)
        static constexpr double kRowHeadW = 117.0;   // the matrix's row header = the lanes' header column
        static constexpr double kColHeadH = 39.0;    // space::u(12)
        static constexpr double kCellW = 61.75;      // space::u(19)
        static constexpr double kCellH = 26.0;       // space::u(8)

        MixerDock();
        void bind(const solaris::AppModel &m, bool interacting);
        void layout();
        void advance(double nowMs) override;

        // tabs: 0 … mixers−1 a mixer page, then the matrix
        int tabCount() const { return (int)mMixers.size() + 1; }
        int tab() const { return mTab; }
        bool onMatrix() const { return mTab == (int)mMixers.size(); }
        void setTab(int t);
        artboard::Rect tabRect(int t) const;
        artboard::Rect addMixerRect() const;
        artboard::Rect toggleRect() const;
        double tabHighlightX() const { return mHiX.value(); }  // LIVE
        double pageAmount(int t) const;                         // a page's LIVE opacity

        // a strip's card on the page shown ("master" = the master's)
        artboard::Rect cardRect(const std::string &id) const;
        artboard::Rect faderRect(const std::string &id) const;
        double faderLive(const std::string &id) const;          // the thumb's LIVE position, 0…1
        artboard::Rect panRect(const std::string &id) const;
        double panLive(const std::string &id) const;
        artboard::Rect muteRect(const std::string &id) const;
        artboard::Rect soloRect(const std::string &id) const;
        double muteAmount(const std::string &id) const;
        artboard::Rect outRect(const std::string &id) const;
        artboard::Rect chipRect(const std::string &id, int slot) const; // the slot's chip (a device, "+N", "+ Effect")
        int chipSlots(const std::string &id) const;
        artboard::Rect sendRect(const std::string &id, int i) const;
        artboard::Rect foldRect(const std::string &bus) const;
        double foldAmount(const std::string &bus) const;
        artboard::Rect cellRect(const std::string &from, const std::string &to) const; // the matrix
        double meterLive(const std::string &id, int channel) const;
        DevicePanel &panel() { return *mPanel; }

        /** The fader law: 0 dB at 0.708, +6 at the top, −∞ at the bottom (gain ∝ position², +6 dB headroom). */
        static double faderPos(double dB);
        static double faderDb(double pos);

        std::function<bool(const std::string &line)> onCommand;
        std::function<void(std::vector<cosmo_v2::ContextMenu::Item> items, artboard::Point world)> onMenu;
        std::function<void(const std::string &current, artboard::Point world, std::function<void(const std::string &)> done)> onRename;
        std::function<void()> onToggle;              // the chevron: fold the dock away or bring it back
        std::function<void(double worldY)> onResize; // the top edge dragged (direct manipulation)

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        /** A strip's (or the master's) picture: each value the model sets, eased. */
        struct Live
        {
            artboard::AnimatedProperty fader{0.0}, pan{0.0}, mute{0.0}, solo{0.0}, dim{0.0}, colT{1.0};
            artboard::AnimatedProperty meter[2] = {artboard::AnimatedProperty{0.0}, artboard::AnimatedProperty{0.0}};
            double faderLast = 0, panLast = 0, meterLast[2] = {0, 0};
            bool muteLast = false, soloLast = false, dimLast = false, placed = false;
            artboard::Color colFrom, colTo;
            int colLast = -1;
        };
        /** A column on a page: a strip, or the header of the strips feeding one bus. Keyed. */
        struct Card
        {
            std::string key, id;   // id: the strip; for a header, the bus
            bool header = false;
            solaris::StripModel snap; // what it showed last: a card that goes is drawn from it as it shrinks
            double inLast = 0.0;
            artboard::AnimatedProperty in{0.0}; // presence: grows in, shrinks out
            bool placed = false, gone = false;
        };
        struct Page
        {
            std::vector<Card> cards;
            double sx = 0.0;                    // its scroll when it stopped being shown
            artboard::AnimatedProperty alpha{0.0};
            bool want = false, last = false, placed = false;
        };
        /** A tab, keyed by mixer id ("" = the matrix): eased x and presence. */
        struct Tab
        {
            std::string key, label;
            artboard::AnimatedProperty x{0.0}, in{0.0};
            double xWant = 0, xLast = 0, inLast = 0;
            bool placed = false, gone = false;
        };
        struct Toggle { artboard::AnimatedProperty a{0.0}; bool last = false, placed = false; };
        enum class Part { None, Tab, AddMixer, ToggleDock, Resize, Header, Chip, Send, Pan, Fader, Mute, Solo, Out, Fold, Cell };
        struct Hit
        {
            Part part = Part::None;
            std::string id;        // a strip, "master", a bus (Fold); the row's strip (Cell)
            std::string to;        // Cell: the column
            int index = -1;        // Tab, Chip slot, Send
        };

        const solaris::StripModel *strip(const std::string &id) const;
        std::string labelOf(const std::string &target) const;
        void syncCards();
        void syncTabs();
        double cardWidth(const Card &c) const;
        double pageLeft() const { return 0.0; }
        double stripsRight() const;        // where the master begins
        double bodyTop() const;            // the cards' top (scrolled)
        double bodyH() const;
        const Page *shownPage() const;
        std::string pageKey(int t) const;
        bool cardX(const std::string &key, double &x, double &w) const;
        Hit hitAt(const artboard::Point &p) const;
        void openOut(const std::string &id, artboard::Point world);
        void openAddEffect(const std::string &id, artboard::Point world);
        void openSend(const solaris::SendModel &sd, const std::string &from, artboard::Point world);
        bool send(const std::string &line) { return onCommand ? onCommand(line) : false; }
        Live &live(const std::string &id) { return mLive[id]; }
        const Live *liveOf(const std::string &id) const;
        // painting
        void paintPage(artboard::IRenderTarget &t, const std::string &key, double alpha) const;
        void paintCard(artboard::IRenderTarget &t, const solaris::StripModel &s, double x, double w, double alpha) const;
        void paintHeader(artboard::IRenderTarget &t, const std::string &bus, const std::string &mixer, double x, double w, double alpha) const;
        void paintMaster(artboard::IRenderTarget &t, double alpha) const;
        void paintMatrix(artboard::IRenderTarget &t, double alpha) const;
        void paintFader(artboard::IRenderTarget &t, const artboard::Rect &zone, const Live &l, double alpha) const;

        solaris::AppModel mModel;
        std::vector<solaris::MixerModel> mMixers;
        std::map<std::string, Live> mLive;
        std::map<std::string, Page> mPages;      // by mixer id; "" = the matrix
        std::vector<Tab> mTabs;
        std::map<std::string, Toggle> mFold;     // by bus id: true = folded (the view's)
        std::map<std::string, Toggle> mCells;    // the matrix: "from>to" — a main output's dot, a send's presence
        mutable std::map<std::string, double> mTabTextW; // measured in the paint, read by the next layout
        int mTab = 0;
        bool mInteracting = false, mBound = false, mEver = false;
        double mNowMs = 0.0;
        artboard::AnimatedProperty mHiX{0.0}, mHiW{0.0};
        bool mHiInit = false;
        double mHiXLast = 0, mHiWLast = 0;
        interstellar_v1::EasedScroll mScrollX, mScrollY, mMxX, mMxY;
        cosmo_v2::HoverFade mHover;
        std::shared_ptr<DevicePanel> mPanel;
        // a drag in flight
        Hit mPress;
        bool mDragging = false;
        double mGrab = 0.0, mStart = 0.0;
        std::string mLastSent;
    };
}
}
