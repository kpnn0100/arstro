/*
 *  cosmo — AgentPanel: the right column's "Agent" tab, where the photographer talks to their own
 *  agent (R-ASSIST-7).
 *
 *  The agent is the user's (Claude Code by default), run by the host beside the window, editing the
 *  open project through cosmo's own tools with the colour-grading skill loaded (R-ASSIST-1..6). This
 *  panel is only its VIEW: it shows `AppModel::assist` and sends `assist start | send | stop` — the
 *  conversation lives in the service, so the window, `cosmo-cc attach` and the log all see the same
 *  lines (R-SVC-4).
 *
 *  Layout, top to bottom, at the column's 324 px:
 *    header    29.25   the agent's name, a state dot + word, and Start/Stop
 *    messages  flex    self-drawn, wrapped with the real font metrics, clipped AND scrolled (R6)
 *    composer  39      a text field (Enter sends) and Send - the ActionBar's height, so the column's
 *                      bottom edge reads as one band whichever tab is open
 *
 *  Why self-drawn messages rather than one Segment per message: a conversation only grows, Segment
 *  has no removeChild, and wrapped text needs the render target to measure — so the list is laid out
 *  where it is measured, in paint, and the measured height is what the scroll clamps against.
 *
 *  Motion (R-G-1): a new message fades in (180 ms), the list eases to the newest message unless the
 *  user has scrolled up to read, the state dot cross-fades its colour (180 ms), and the working line
 *  pulses while the agent works. All through AnimatedProperty / the frame clock; reducedMotion()
 *  collapses the hand-rolled ones.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "../core/service/AppModel.h"
#include "PillButton.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class AgentPanel : public artboard::Segment
    {
    public:
        static constexpr double kHeaderH = 29.25;     // ladder 9
        static constexpr double kComposerH = 39.0;    // = ActionBar::kHeight
        static constexpr double kPadX = 9.75;         // ladder 3
        static constexpr double kFieldH = 26.0;       // ladder 8
        static constexpr double kSendW = 52.0;
        static constexpr double kGap = 6.5;           // ladder 2: between messages
        static constexpr double kFadeMs = 180.0;

        AgentPanel();

        /** The model's conversation. A pure push (no callback fires); the next advance() eases
         *  whatever changed — new messages in, the list toward the newest, the dot's colour. */
        void setConversation(const cosmo::AssistModel &m);

        std::function<void(const std::string &)> onSend;   // -> assist send <text>
        std::function<void()> onStart;                     // -> assist start
        std::function<void()> onStop;                      // -> assist stop

        /** Whether the message field has the keyboard - App::isTextEditing() asks, so its plain-key
         *  shortcuts (o, s, Delete) do not fire while the user types to the agent. */
        bool isTyping() const;

        // ── for tests and shots ──
        /** The conversation's box: below the header, above the composer. */
        artboard::Rect messagesRect() const;
        double scrollOffset() const { return mScroll.value(); }        // the LIVE eased value
        double scrollTarget() const { return mScrollTarget; }
        double contentHeight() const { return mContentH; }
        double maxScroll() const;
        /** The newest message's opacity right now (1 at rest, < 1 mid fade-in). */
        double newestAlpha() const;
        void typeForTest(const std::string &text);
        artboard::Rect sendRect() const;
        artboard::Rect startStopRect() const;

        /** The wheel, as the column hands it to every tab: px, positive = toward the top. Moves
         *  the TARGET only; advance() eases there (R6). */
        void scrollBy(double delta);

        void layout();
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Shown
        {
            std::string role, text;
            double appearMs = -1.0;   // when it started fading in; -1 = not yet (set in advance)
            // Wrapped (or ellipsized) ONCE per width: measuring every word of every message on
            // every frame was the panel's share of the lag while an agent worked (R-ASSIST-8).
            mutable std::vector<std::string> lines;
            mutable double wrappedAt = -1.0;
        };
        void send();
        void refreshButtons();
        double alphaOf(const Shown &m) const;

        cosmo::AssistModel mModel;
        std::vector<Shown> mShown;

        std::shared_ptr<PillButton> mStartStop, mSend;
        std::shared_ptr<artboard::TextBox> mField;

        artboard::AnimatedProperty mScroll{0.0};
        double mScrollTarget = 0.0, mScrollLastTarget = 0.0;
        bool mStick = true;            // follow the newest message until the user scrolls up
        mutable double mContentH = 0.0;   // measured in paint (wrapping needs the font)
        double mLastContentH = 0.0;

        // The state dot: the colour eases from the old state's to the new one's.
        artboard::Color mDotFrom, mDotTo;
        artboard::AnimatedProperty mDotMix{1.0};
        std::string mDotState = "off";

        double mNowMs = 0.0;
        mutable std::vector<std::string> mIntroLines;   // the empty state's sentence, wrapped once per width
        mutable double mIntroAt = -1.0;
        mutable std::string mIntroAgent;
        int mSendLive = -1;   // the last applied Send dimming (-1 = never), so it eases only on a change
    };
}
}
