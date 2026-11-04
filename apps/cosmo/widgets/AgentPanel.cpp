#include "AgentPanel.h"
#include "../Theme.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kLabelPx = 9.0;      // section-header micro label (the type ramp's 9 SemiBold)
        constexpr double kTextPx = 11.0;      // message body
        constexpr double kSmallPx = 10.0;     // tool lines (mono), info lines
        constexpr double kLineH = 15.6;       // 11 px * 1.42
        constexpr double kSmallLineH = 13.0;  // ladder 4
        constexpr double kLabelH = 13.0;      // ladder 4
        constexpr double kBubblePad = 6.5;    // ladder 2: the user's message sits on a card
        constexpr double kStickSlack = 2.0;   // within this of the bottom counts as "at the bottom"

        Color stateColor(const std::string &s)
        {
            if (s == "ready") return palette::success();
            if (s == "starting" || s == "busy") return palette::primary();
            if (s == "failed") return palette::destructive();
            return palette::mutedForeground();
        }
        std::string stateWord(const std::string &s) { return s == "busy" ? std::string("working") : s; }
        bool running(const std::string &s) { return s == "starting" || s == "ready" || s == "busy"; }
        bool working(const std::string &s) { return s == "starting" || s == "busy"; }

        std::string upper(std::string s)
        {
            for (char &c : s) c = (char)std::toupper((unsigned char)c);
            return s;
        }

        /** Greedy word wrap with the target's real metrics (R5) - paragraphs on '\n', a word wider
         *  than the line broken at a codepoint boundary. */
        std::vector<std::string> wrap(const IRenderTarget &t, const std::string &text, double px,
                                      const std::string &font, double maxW)
        {
            std::vector<std::string> out;
            size_t at = 0;
            while (at <= text.size())
            {
                const size_t nl = text.find('\n', at);
                const std::string para = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
                std::string line;
                size_t i = 0;
                while (i <= para.size())
                {
                    const size_t sp = para.find(' ', i);
                    std::string word = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
                    const std::string cand = line.empty() ? word : line + " " + word;
                    if (t.measureText(cand, px, font) <= maxW || (line.empty() && word.empty()))
                        line = cand;
                    else
                    {
                        if (!line.empty()) out.push_back(line);
                        // A single word wider than the line: cut it where it stops fitting.
                        while (t.measureText(word, px, font) > maxW && word.size() > 1)
                        {
                            size_t cut = word.size();
                            while (cut > 1 && t.measureText(word.substr(0, cut), px, font) > maxW)
                            {
                                --cut;
                                while (cut > 1 && ((unsigned char)word[cut] & 0xC0) == 0x80) --cut;
                            }
                            out.push_back(word.substr(0, cut));
                            word = word.substr(cut);
                        }
                        line = word;
                    }
                    if (sp == std::string::npos) break;
                    i = sp + 1;
                }
                out.push_back(line);
                if (nl == std::string::npos) break;
                at = nl + 1;
            }
            return out;
        }

        std::string ellipsize(const IRenderTarget &t, const std::string &s, double px, const std::string &font, double maxW)
        {
            if (t.measureText(s, px, font) <= maxW) return s;
            std::string cut = s;
            while (!cut.empty() && t.measureText(cut + "\xE2\x80\xA6", px, font) > maxW)
            {
                cut.pop_back();
                while (!cut.empty() && ((unsigned char)cut.back() & 0xC0) == 0x80) cut.pop_back();
            }
            return cut + "\xE2\x80\xA6";
        }

        /** The message box: Enter sends; everything else is the TextBox's. */
        class PromptBox : public TextBox
        {
        public:
            using TextBox::TextBox;
            std::function<void()> onEnter;

        protected:
            bool handleKey(const KeyEvent &e) override
            {
                if (e.type == KeyEvent::Type::Down && e.keyCode == 13)
                {
                    if (onEnter) onEnter();
                    return true;
                }
                return TextBox::handleKey(e);
            }
        };
    }

    AgentPanel::AgentPanel()
    {
        clipToBounds = true;

        mStartStop = std::make_shared<PillButton>("Start");
        mStartStop->idleBox = {Paint::filledStroked(palette::secondary(), palette::border(), 1.0), radius::control()};
        mStartStop->idleText = {palette::foreground(), 10.0, font::sans()};
        mStartStop->activeText = {palette::white(), 10.0, font::sans()};
        mStartStop->onClick = [this] {
            if (running(mModel.state)) { if (onStop) onStop(); }
            else if (onStart) onStart();
        };
        addChild(mStartStop);

        TextBoxStyle st;
        st.idle = {Paint::filledStroked(palette::input(), palette::border(), 1.0), radius::control()};
        st.focused = {Paint::filledStroked(palette::input(), palette::ring(), 1.0), radius::control()};
        st.text = {palette::foreground(), 11.0, font::sans()};
        st.placeholder = {palette::mutedForeground(), 11.0, font::sans()};
        st.caretColor = palette::primary();
        auto field = std::make_shared<PromptBox>(st);
        field->placeholder = "Ask your agent\xE2\x80\xA6";
        field->onEnter = [this] { send(); };
        mField = field;
        addChild(mField);

        mSend = std::make_shared<PillButton>("Send");
        mSend->idleBox = {Paint::filled(palette::primary()), radius::control()};
        mSend->idleText = {palette::primaryForeground(), 10.0, font::sansMedium()};
        mSend->activeText = mSend->idleText;
        mSend->onClick = [this] {
            // While the agent works the button is Stop: the one thing worth doing then.
            if (working(mModel.state)) { if (onStop) onStop(); }
            else send();
        };
        addChild(mSend);

        mDotFrom = mDotTo = stateColor("off");
        refreshButtons();
    }

    void AgentPanel::send()
    {
        std::string text = mField->text;
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
        if (text.empty() || !onSend) return;
        onSend(text);
        mField->text.clear();
        mField->caretToEnd();
        mStick = true;   // the user just spoke: follow the answer
    }

    bool AgentPanel::isTyping() const { return mField && mField->hasFocus(); }

    void AgentPanel::typeForTest(const std::string &text)
    {
        mField->text = text;
        mField->caretToEnd();
    }

    void AgentPanel::setConversation(const cosmo::AssistModel &m)
    {
        mModel = m;
        // Messages only ever append (the service never edits one), so the shown list grows by the
        // tail; a SHORTER model is a new conversation and starts over.
        if (m.messages.size() < mShown.size()) mShown.clear();
        for (size_t i = mShown.size(); i < m.messages.size(); ++i)
            mShown.push_back({m.messages[i].role, m.messages[i].text, -1.0});
        refreshButtons();
    }

    void AgentPanel::refreshButtons()
    {
        mStartStop->setLabel(running(mModel.state) ? "Stop" : "Start");
        mSend->setLabel(working(mModel.state) ? "Stop" : "Send");
    }

    Rect AgentPanel::messagesRect() const
    {
        const double w = width.value(), h = height.value();
        return Rect{0, kHeaderH, w, std::max(0.0, h - kHeaderH - kComposerH)};
    }
    Rect AgentPanel::sendRect() const { return Rect{mSend->x.value(), mSend->y.value(), mSend->width.value(), mSend->height.value()}; }
    Rect AgentPanel::startStopRect() const
    {
        return Rect{mStartStop->x.value(), mStartStop->y.value(), mStartStop->width.value(), mStartStop->height.value()};
    }
    double AgentPanel::maxScroll() const { return std::max(0.0, mContentH - messagesRect().h); }

    void AgentPanel::layout()
    {
        const double w = width.value(), h = height.value();
        const double bw = 46.0, bh = 19.5;   // ladder 6 high: a header-row button
        mStartStop->x.set(w - kPadX - bw);
        mStartStop->y.set((kHeaderH - bh) * 0.5);
        mStartStop->width.set(bw);
        mStartStop->height.set(bh);
        const double fy = h - kComposerH + (kComposerH - kFieldH) * 0.5;
        mField->x.set(kPadX);
        mField->y.set(fy);
        mField->width.set(std::max(0.0, w - 2 * kPadX - kGap - kSendW));
        mField->height.set(kFieldH);
        mSend->x.set(w - kPadX - kSendW);
        mSend->y.set(fy);
        mSend->width.set(kSendW);
        mSend->height.set(kFieldH);
    }

    double AgentPanel::alphaOf(const Shown &m) const
    {
        if (m.appearMs < 0) return 0.0;
        if (reducedMotion()) return 1.0;
        const double p = std::clamp((mNowMs - m.appearMs) / kFadeMs, 0.0, 1.0);
        return 1.0 - std::pow(1.0 - p, 3.0);   // EaseOutCubic
    }
    double AgentPanel::newestAlpha() const { return mShown.empty() ? 1.0 : alphaOf(mShown.back()); }

    void AgentPanel::advance(double nowMs)
    {
        mNowMs = nowMs;
        for (auto &m : mShown)
            if (m.appearMs < 0) m.appearMs = nowMs;   // a setter has no clock: the fade starts here

        // The dot cross-fades from wherever it is to the new state's colour.
        if (mModel.state != mDotState)
        {
            mDotFrom = lerpColor(mDotFrom, mDotTo, mDotMix.value());
            mDotTo = stateColor(mModel.state);
            mDotState = mModel.state;
            mDotMix.set(0.0);
            mDotMix.animateTo(1.0, 180.0, Easing::EaseOutCubic, nowMs);
        }
        mDotMix.update(nowMs);

        // Follow the newest message while the user is at the bottom; the content height is the one
        // the last paint measured, so a change of it is what "something new arrived" looks like here.
        const double maxS = maxScroll();
        if (mStick) mScrollTarget = maxS;
        mScrollTarget = std::clamp(mScrollTarget, 0.0, maxS);
        if (mScrollTarget != mScrollLastTarget)
        {
            mScroll.animateTo(mScrollTarget, 180.0, Easing::EaseOutCubic, nowMs);
            mScrollLastTarget = mScrollTarget;
        }
        mScroll.update(nowMs);
        mLastContentH = mContentH;

        // Send dims (and stops taking clicks) while there is nothing to send; as Stop it is live.
        const bool live = working(mModel.state) || !mField->text.empty();
        if ((int)live != mSendLive)
        {
            if (mSendLive < 0) mSend->opacity.set(live ? 1.0 : 0.45);   // first placement: nothing to travel from
            else mSend->opacity.animateTo(live ? 1.0 : 0.45, 120.0, Easing::EaseOutCubic, nowMs);
            mSendLive = (int)live;
        }
        mSend->enabled = live;

        layout();
        Segment::advance(nowMs);
    }

    void AgentPanel::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::card()));

        // ── header: dot, agent, state word ──
        const double cy = kHeaderH * 0.5;
        const Color dot = lerpColor(mDotFrom, mDotTo, mDotMix.value());
        double pulse = 1.0;
        if (working(mModel.state) && !reducedMotion()) pulse = 0.55 + 0.45 * (0.5 + 0.5 * std::sin(mNowMs / 1400.0 * 6.2831853));
        drawCircle(t, kPadX + 3.25, cy, 3.25, Paint::filled(Color{dot.r, dot.g, dot.b, dot.a * pulse}));
        const std::string agent = mModel.agent.empty() ? std::string("Claude Code") : mModel.agent;
        const double nameX = kPadX + 13.0;
        const double room = mStartStop->x.value() - kGap - nameX;
        const std::string word = stateWord(mModel.state);
        const double wordW = t.measureText(word, kSmallPx, font::sans());
        const std::string name = ellipsize(t, agent, 11.0, font::sansMedium(), std::max(0.0, room - wordW - kGap));
        t.setFill(palette::foreground());
        t.drawText(name, nameX, cy + 11.0 * 0.35, 11.0, font::sansMedium());
        t.setFill(palette::mutedForeground());
        t.drawText(word, nameX + t.measureText(name, 11.0, font::sansMedium()) + kGap, cy + kSmallPx * 0.35, kSmallPx, font::sans());
        t.setStroke(palette::border(), 1.0);
        t.beginPath();
        t.moveTo(0, kHeaderH - 0.5);
        t.lineTo(w, kHeaderH - 0.5);
        t.strokePath();

        // ── the conversation, laid out where it is measured ──
        const Rect box = messagesRect();
        const double textW = std::max(40.0, w - 2 * kPadX);
        t.save();
        t.clipRect(box.x, box.y, box.w, box.h);
        double y = box.y + kGap - mScroll.value();
        if (mShown.empty())
        {
            // EMPTY is a state with words, not a blank (§7): what to do, and what is ready.
            const std::vector<std::string> intro = wrap(t,
                "Ask " + agent + " to grade this project \xE2\x80\x94 for example \xE2\x80\x9Cmake these cinematic\xE2\x80\x9D.\n"
                "The colour-grading skill is loaded. Every edit lands here, one undoable step at a time.",
                kTextPx, font::sans(), textW);
            y = box.y + std::max(kGap, (box.h - intro.size() * kLineH) * 0.4);
            t.setFill(palette::mutedForeground());
            for (const auto &l : intro) { t.drawText(l, kPadX, y + kLineH * 0.5 + kTextPx * 0.35, kTextPx, font::sans()); y += kLineH; }
            mContentH = 0.0;
        }
        for (const Shown &m : mShown)
        {
            const double a = alphaOf(m);
            auto fade = [a](Color c) { c.a *= a; return c; };
            if (m.role == "tool" || m.role == "info")
            {
                // One muted line: what the agent used, or what it is / what a turn took.
                const bool tool = m.role == "tool";
                const std::string f = tool ? font::mono() : font::sans();
                const std::string line = ellipsize(t, (tool ? "\xC2\xB7 " : "") + m.text, kSmallPx, f, textW);
                t.setFill(fade(palette::mutedForeground()));
                t.drawText(line, kPadX, y + kSmallLineH * 0.5 + kSmallPx * 0.35, kSmallPx, f);
                y += kSmallLineH + kGap * 0.5;
                continue;
            }
            const bool user = m.role == "user", error = m.role == "error";
            const std::string label = user ? "YOU" : error ? "ERROR" : upper(agent);
            t.setFill(fade(error ? palette::destructive() : palette::mutedForeground()));
            t.drawText(label, kPadX, y + kLabelH * 0.5 + kLabelPx * 0.35, kLabelPx, font::sansSemiBold(), 0.13 * kLabelPx);
            y += kLabelH;
            const double inner = user ? textW - 2 * kBubblePad : textW;
            const std::vector<std::string> lines = wrap(t, m.text, kTextPx, font::sans(), inner);
            const double bodyH = lines.size() * kLineH;
            double tx = kPadX;
            if (user)
            {
                drawRoundedRect(t, Rect{kPadX, y, textW, bodyH + 2 * kBubblePad}, radius::control(),
                                Paint::filled(fade(palette::secondary())));
                tx += kBubblePad;
                y += kBubblePad;
            }
            t.setFill(fade(error ? palette::destructive() : palette::foreground()));
            for (const auto &l : lines)
            {
                t.drawText(l, tx, y + kLineH * 0.5 + kTextPx * 0.35, kTextPx, font::sans());
                y += kLineH;
            }
            if (user) y += kBubblePad;
            y += kGap;
        }
        if (working(mModel.state))
        {
            // LOADING is a state too: the agent is at work, said in words, pulsing (R-G-1).
            const double p = reducedMotion() ? 1.0 : 0.45 + 0.35 * (0.5 + 0.5 * std::sin(mNowMs / 1400.0 * 6.2831853));
            t.setFill(Color{palette::mutedForeground().r, palette::mutedForeground().g, palette::mutedForeground().b, p});
            t.drawText(agent + (mModel.state == "starting" ? " is starting\xE2\x80\xA6" : " is working\xE2\x80\xA6"), kPadX,
                       y + kSmallLineH * 0.5 + kSmallPx * 0.35, kSmallPx, font::sans());
            y += kSmallLineH + kGap;
        }
        if (!mShown.empty() || working(mModel.state)) mContentH = y + mScroll.value() - box.y;

        // R6: an indicator only when there is more to see.
        const double maxS = std::max(0.0, mContentH - box.h);
        if (maxS > 0.5)
        {
            const double trackH = box.h - 2 * kGap;
            const double thumbH = std::max(19.5, trackH * box.h / mContentH);
            const double ty = box.y + kGap + (trackH - thumbH) * std::clamp(mScroll.value() / maxS, 0.0, 1.0);
            drawRoundedRect(t, Rect{w - 4.875, ty, 3.25, thumbH}, radius::pill(), Paint::filled(palette::whiteAlpha(0.18)));
        }
        t.restore();

        // ── composer band ──
        t.setStroke(palette::border(), 1.0);
        t.beginPath();
        t.moveTo(0, h - kComposerH + 0.5);
        t.lineTo(w, h - kComposerH + 0.5);
        t.strokePath();
    }

    void AgentPanel::scrollBy(double delta)
    {
        const double maxS = maxScroll();
        mScrollTarget = std::clamp(mScrollTarget - delta, 0.0, maxS);
        mStick = mScrollTarget >= maxS - kStickSlack;   // back at the end: follow the newest again
    }
}
}
