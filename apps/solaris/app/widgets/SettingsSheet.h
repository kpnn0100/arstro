/*
 *  solaris_ui — SettingsSheet: the machine's settings, in cosmo's modal style (R-SET-1, R-SET-5, R-SET-6).
 *
 *  Cosmo's modal LOOK — the scrim, the popover card, the 150/120 ms fade, the Done button — around a RAIL
 *  of sections (Audio, Playback, New songs, Sample folders, Interface, Keyboard) and ONE page, the chosen
 *  section's (R-SET-5): a section clicked cross-fades its page in while the rail's highlight slides to
 *  it, and a page taller than the card scrolls on its own. A setting that is one value of a list is a
 *  combo box — Artboard's `ComboBox`, its field drawn here so a value arriving from the model
 *  cross-fades (`SettingCombo`); an on/off setting is a toggle whose knob eases (`SettingToggle`)
 *  (R-SET-6). The sample folders and the keymap (R-SET-4) are lists, drawn by their pages: a key chip
 *  clicked LISTENS for the next key (Escape cancels, Backspace removes it), "+" listens for one more,
 *  Reset gives every changed action its defaults.
 *
 *  Every control is a command line (`onCommand`): `settings set output=<id>`, `folder add <path>`,
 *  `settings set key.<action>=…` — the sheet holds no setting of its own; it draws `AppModel::settings`
 *  and `devices` when they change, so a change made from a shell shows on its page too.
 *
 *  Drawn in the normal pass (the App renders it after the screens): its own paint, then the pages and
 *  their controls; an open combo's list in the overlay pass, over everything. The self-drawn parts
 *  are MEASURED, so their boxes are computed in the paint — measurement is only valid during a render
 *  (design rule R5) — and kept for the next hit test.
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "../../../interstellar/app/widgets/EasedScroll.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include "../../../../core/Artboard/src/ui/concrete/ComboBox.h"
#include "../../../../core/Artboard/src/ui/concrete/ToggleSwitch.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_ui
{
    /** Artboard's ComboBox — its behaviour (the list, its scroll, a pick) untouched — with the field drawn
     *  in cosmo's tokens and its label CROSS-FADED when the value changes, whoever changed it (FR-41). */
    class SettingCombo : public artboard::ComboBox
    {
    public:
        SettingCombo();
        void advance(double nowMs) override;
        /** Close the list (a click elsewhere, Escape, the sheet closing) — as a click on the field does. */
        void close();
        /** LIVE: how far the label has cross-faded to the value it shows now (1 = settled). */
        double labelAmount() const { return mFade.value(); }
        const std::string &shown() const { return mTo; }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        std::string mFrom, mTo;
        artboard::AnimatedProperty mFade{1.0};
        bool mPlaced = false;
    };

    /** Artboard's ToggleSwitch, its knob drawn here from an eased copy of `on()` — so a value set from the
     *  model (`setOn`) slides like a click does (design rule §1). */
    class SettingToggle : public artboard::ToggleSwitch
    {
    public:
        SettingToggle();
        void advance(double nowMs) override;
        double amount() const { return mKnob.value(); } // LIVE: 0 off … 1 on

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        artboard::AnimatedProperty mKnob{0.0};
        double mLast = -1.0;
        bool mPlaced = false;
    };

    class SettingsSheet : public artboard::Segment
    {
    public:
        enum Row { kOutput = 0, kInput, kRate, kBuffer, kMetronome, kClickLevel, kPreviewLevel, kNewBpm, kNewSig, kMotion, kRows };
        enum Section { kAudio = 0, kPlayback, kNewSongs, kFolders, kInterface, kKeyboard, kSections };
        static constexpr double kCardW = 720.0, kCardH = 520.0;
        static constexpr double kRailW = 164.0;   // the sections' column
        static constexpr double kPad = 24.0;
        static constexpr double kChipH = 26.0;    // the keymap's chips, "Add folder…"
        static constexpr double kRowH = 56.0;     // a setting: its name and note on the left, its control on the right
        static constexpr double kComboW = 220.0, kControlH = 28.0;

        SettingsSheet();
        void bind(const solaris::AppModel &m);
        void show();
        void close();
        bool isOpen() const { return mOpen && !mClosing; }
        /** The LIVE eased appearance (0 shut … 1 open) — what a test reads to tell a fade from a cut. */
        double appearAmount() const { return mAppear.value(); }

        std::function<void(const std::string &line)> onCommand;
        std::function<void()> onAddFolder;

        // R-SET-5: the sections — geometry in window coordinates, from the last layout
        void showSection(int s); // as a click on the rail
        int section() const { return mSection; }
        artboard::Rect railRect(int s) const;
        double pageAmount(int s) const;            // LIVE: a page's opacity in the cross-fade
        double railHighlightY() const { return mRailY.value(); } // LIVE: the highlight's top
        static const char *sectionTitle(int s);
        static int sectionOf(int row);

        // R-SET-6: the controls
        artboard::Rect controlRect(int row) const; // the combo's field or the toggle
        int optionCount(int row) const;
        std::string optionLabel(int row, int i) const;
        artboard::Rect optionRect(int row, int i) const; // a row of the OPEN list
        bool comboOpen(int row) const;
        double labelAmount(int row) const;         // LIVE: a combo's label cross-fade (1 = settled)
        std::string shownValue(int row) const;     // the label a combo shows now
        double toggleAmount(int row) const;        // LIVE: a toggle's knob

        // geometry, from the last paint (a test renders a frame first)
        artboard::Rect doneRect() const { return mDone; }
        artboard::Rect addFolderRect() const { return mAddFolder; }
        artboard::Rect folderRemoveRect(int i) const { return i >= 0 && i < (int)mRemove.size() ? mRemove[(size_t)i] : artboard::Rect{}; }
        /** Scroll (eased) the CURRENT page so a rect from the last paint is inside it. */
        void revealRect(const artboard::Rect &r);

        // R-SET-4, the Keyboard page — geometry from the last paint; `i` indexes the action's keys in the model
        artboard::Rect keyChipRect(const std::string &action, int i) const;
        artboard::Rect keyAddRect(const std::string &action) const;
        artboard::Rect keyResetRect() const { return mKeyReset; }
        /** The action a click is listening for ("" when none) and the slot the next key lands in. */
        const std::string &listeningFor() const { return mListenAction; }
        int listeningSlot() const { return mListenSlot; }
        /** LIVE, eased: a slot's listening look, and a combo chip's presence (it grows in and shrinks out). */
        double listenAmount(const std::string &action, int slot) const;
        double keyChipAmount(const std::string &action, const std::string &combo) const;

        void advance(double nowMs) override;
        bool handleKey(const artboard::KeyEvent &e) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &) const override { return mOpen && !mClosing; }

    private:
        class Page; // a section's page: clipped, cross-faded, its own scroll; painted by `paintPage`
        friend class Page;
        struct Chip
        {
            std::string label, value;
        };
        struct KeyChipLive
        {
            std::string combo;
            artboard::AnimatedProperty amt{0.0};
            bool gone = false, placed = false;
            double last = -1.0;
        };
        struct KeyRowLive
        {
            std::string id;
            std::vector<KeyChipLive> chips;     // the model's keys in order, and the ones leaving where they were
            artboard::AnimatedProperty changed{0.0}; // not its defaults: a dot beside it
            bool placed = false;
            double last = -1.0;
        };
        struct Eased
        {
            artboard::AnimatedProperty p{0.0};
            double last = 0.0;
        };

        artboard::Rect cardRect() const;
        artboard::Rect pageRect() const;
        void layoutPages();
        void paintPage(artboard::IRenderTarget &t, int s, const Page &page) const;
        void paintRows(artboard::IRenderTarget &t, int s, double w, double oy) const;
        void paintFolders(artboard::IRenderTarget &t, double w, double oy, double px, double py) const;
        void paintKeyboard(artboard::IRenderTarget &t, double w, double oy, double px, double py) const;
        std::string current(int row) const; // the row's setting, as its options' values spell it
        void closeCombos();
        bool anyComboOpen() const;
        int hoverId(const artboard::Point &p) const;
        void listen(const std::string &action, int slot);
        void stopListening() { mListenAction.clear(); mListenSlot = -1; }
        const solaris::KeyActionModel *keyModel(const std::string &id) const;
        static std::string slotKey(const std::string &action, int slot) { return action + "#" + std::to_string(slot); }

        std::vector<Chip> mChips[kRows];       // a combo's options: what it shows, what it sends
        std::shared_ptr<SettingCombo> mCombos[kRows];
        std::shared_ptr<SettingToggle> mToggles[kRows];
        std::vector<std::shared_ptr<Page>> mPages;
        bool mBound = false;
        std::vector<std::string> mFolders;
        std::string mOutput, mInput;
        int mRate = 48000, mBuffer = 256;
        double mLatencyMs = 0;
        solaris::SettingsModel mSettings;

        bool mOpen = false, mClosing = false, mShowWanted = false, mCloseWanted = false;
        double mNowMs = 0.0;
        artboard::AnimatedProperty mAppear{0.0};
        int mSection = kAudio;
        bool mSectionPlaced = false;
        artboard::AnimatedProperty mRailY{0.0};
        double mRailLast = -1.0;
        cosmo_v2::HoverFade mHover;
        // measured in the last paint
        mutable std::vector<artboard::Rect> mRemove;
        mutable artboard::Rect mDone, mAddFolder;

        // R-SET-4
        std::vector<KeyRowLive> mKeyRows;
        bool mKeysEver = false;
        std::string mListenAction;
        int mListenSlot = -1;
        std::map<std::string, Eased> mListen;  // "<action>#<slot>" → its listening look
        Eased mResetLive;
        bool mResetPlaced = false;
        mutable std::map<std::string, std::vector<artboard::Rect>> mKeyChipRects;
        mutable std::map<std::string, artboard::Rect> mKeyAddRects;
        mutable artboard::Rect mKeyReset;
    };
}
}
