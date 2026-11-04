#include "AgentPanel.h"
#include "../Theme.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;
    using cosmo::AgentChoice;

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
        constexpr double kRowH = 39.0;        // ladder 12: a choice is two lines, name and note
        constexpr double kRowGap = 3.25;      // ladder 1
        constexpr double kListHeadH = 22.75;  // ladder 7: the section header above the choices

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
            else if (pickerShown())
            {
                // On the choices: back to the chat if there is one, else look again.
                if (!mShown.empty()) mPickWanted = false;
                else if (onDetect) onDetect();
            }
            else mPickWanted = true;
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
        field->onEnter = [this] { if (pickerShown()) connect(); else send(); };
        mField = field;
        addChild(mField);

        mSend = std::make_shared<PillButton>("Send");
        mSend->idleBox = {Paint::filled(palette::primary()), radius::control()};
        mSend->idleText = {palette::primaryForeground(), 10.0, font::sansMedium()};
        mSend->activeText = mSend->idleText;
        mSend->onClick = [this] {
            // On the choices it is Connect; while the agent works it is Stop - the one thing worth
            // doing then; otherwise Send.
            if (pickerShown()) connect();
            else if (mModel.state == "busy") { if (onStop) onStop(); }
            else send();   // while STARTING too: the runner queues it until the agent can take it
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

    const md::Layout *AgentPanel::messageLayout(size_t i) const
    {
        if (i >= mShown.size() || mShown[i].wrappedAt < 0) return nullptr;
        const std::string &r = mShown[i].role;
        return r == "user" || r == "error" || r == "tool" || r == "info" ? nullptr : &mShown[i].layout;
    }

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
        // R-ASSIST-9: a streamed message GROWS in place - re-wrapped, not faded in again.
        for (size_t i = 0; i < mShown.size(); ++i)
            if (mShown[i].text != m.messages[i].text)
            {
                mShown[i].text = m.messages[i].text;
                mShown[i].wrappedAt = -1.0;
            }
        for (size_t i = mShown.size(); i < m.messages.size(); ++i)
            mShown.push_back({m.messages[i].role, m.messages[i].text, -1.0});
        // The choices: kept selected by setting across a re-detection.
        bool same = m.choices.size() == mChoices.size();
        for (size_t i = 0; same && i < m.choices.size(); ++i) same = m.choices[i].agent == mChoices[i].agent;
        if (!same)
        {
            const std::string was = mSelectedChoice >= 0 && mSelectedChoice < (int)mChoices.size() ? mChoices[mSelectedChoice].agent
                                                                                                    : mCurrentAgent;
            mChoices = m.choices;
            mSelectedChoice = -1;
            for (size_t i = 0; i < mChoices.size() && mSelectedChoice < 0; ++i)
                if (mChoices[i].agent == was) mSelectedChoice = (int)i;
            for (size_t i = 0; i < mChoices.size() && mSelectedChoice < 0; ++i)
                if (mChoices[i].installed) mSelectedChoice = (int)i;
            if (pickerShown() && mSelectedChoice >= 0) { mField->text = mChoices[mSelectedChoice].agent; mField->caretToEnd(); }
        }
        if (running(m.state)) mPickWanted = false;   // connected: the chat
        refreshButtons();
    }

    void AgentPanel::refreshButtons()
    {
        const bool pick = pickerShown();
        mStartStop->setLabel(running(mModel.state) ? "Stop" : pick ? (mShown.empty() ? "Refresh" : "Chat") : "Agents");
        mSend->setLabel(pick ? "Connect" : mModel.state == "busy" ? "Stop" : "Send");
    }

    bool AgentPanel::pickerShown() const { return !running(mModel.state) && (mShown.empty() || mPickWanted); }

    void AgentPanel::selectChoice(int i)
    {
        if (i < 0 || i >= (int)mChoices.size()) return;
        mSelectedChoice = i;
        mField->text = mChoices[i].agent;   // shown, and editable: a custom command, another model
        mField->caretToEnd();
    }

    void AgentPanel::connect()
    {
        std::string agent = mField->text;
        while (!agent.empty() && (agent.back() == ' ' || agent.back() == '\t')) agent.pop_back();
        if (agent.empty() || !onConnect) return;
        mPickWanted = false;
        onConnect(agent);
    }

    double AgentPanel::choicesTop() const { return messagesRect().y + kGap + kListHeadH - mPickScroll.value(); }

    Rect AgentPanel::choiceRect(int i) const
    {
        return Rect{kPadX, choicesTop() + i * (kRowH + kRowGap), std::max(40.0, width.value() - 2 * kPadX), kRowH};
    }

    int AgentPanel::choiceAt(const Point &local) const
    {
        if (!messagesRect().contains(local)) return -1;
        for (int i = 0; i < (int)mChoices.size(); ++i)
            if (choiceRect(i).contains(local)) return i;
        return -1;
    }

    bool AgentPanel::handleGesture(const Gesture &g, const Point &local)
    {
        if (mPick.value() > 0.5)
        {
            const int i = choiceAt(local);
            if (g.type == Gesture::Type::Move) mRowHover.setHovered(i);
            if (g.type == Gesture::Type::Click && i >= 0) { selectChoice(i); return true; }
        }
        return Segment::handleGesture(g, local);
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

        // R-ASSIST-11: the choices and the chat cross-fade (180 ms); the field swaps what it holds -
        // the chat draft is kept while it shows the agent's command.
        const bool pick = pickerShown();
        if (pick != mPickShownApplied)
        {
            if (pick)
            {
                mDraft = mField->text;
                mField->text = mSelectedChoice >= 0 ? mChoices[mSelectedChoice].agent : mCurrentAgent;
                if (!mDetectAsked && mChoices.empty() && onDetect) { mDetectAsked = true; onDetect(); }
            }
            else
                mField->text = mDraft;
            mField->caretToEnd();
            mPick.animateTo(pick ? 1.0 : 0.0, 180.0, Easing::EaseOutCubic, nowMs);
            mPickShownApplied = pick;
            refreshButtons();
        }
        mPick.update(nowMs);
        if (!isHovered()) mRowHover.clear();
        mRowHover.advance(nowMs);
        mPickScrollTarget = std::clamp(mPickScrollTarget, 0.0, std::max(0.0, mPickContentH - messagesRect().h));
        if (mPickScrollTarget != mPickScrollLast)
        {
            mPickScroll.animateTo(mPickScrollTarget, 180.0, Easing::EaseOutCubic, nowMs);
            mPickScrollLast = mPickScrollTarget;
        }
        mPickScroll.update(nowMs);

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
        const bool live = mModel.state == "busy" || !mField->text.empty();   // Connect needs a command too
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
        // R-ASSIST-11: on the choices the header asks the question; the two titles cross-fade with the
        // body rather than swap in one frame.
        const double hp = mPick.value();
        auto hf = [](Color c, double a) { c.a *= a; return c; };
        t.setFill(hf(palette::foreground(), 1.0 - hp));
        t.drawText(name, nameX, cy + 11.0 * 0.35, 11.0, font::sansMedium());
        t.setFill(hf(palette::mutedForeground(), 1.0 - hp));
        t.drawText(word, nameX + t.measureText(name, 11.0, font::sansMedium()) + kGap, cy + kSmallPx * 0.35, kSmallPx, font::sans());
        t.setFill(hf(palette::foreground(), hp));
        t.drawText(ellipsize(t, "Choose your agent", 11.0, font::sansMedium(), std::max(0.0, room)), nameX, cy + 11.0 * 0.35, 11.0,
                   font::sansMedium());
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
            if (mIntroAt != textW || mIntroAgent != agent)
            {
                mIntroLines = wrap(t,
                    "Ask " + agent + " to grade this project \xE2\x80\x94 for example \xE2\x80\x9Cmake these cinematic\xE2\x80\x9D.\n"
                    "The colour-grading skill is loaded. Every edit lands here, one undoable step at a time.",
                    kTextPx, font::sans(), textW);
                mIntroAt = textW;
                mIntroAgent = agent;
            }
            const std::vector<std::string> &intro = mIntroLines;
            y = box.y + std::max(kGap, (box.h - intro.size() * kLineH) * 0.4);
            Color introC = palette::mutedForeground();
            introC.a *= 1.0 - mPick.value();
            t.setFill(introC);
            for (const auto &l : intro) { t.drawText(l, kPadX, y + kLineH * 0.5 + kTextPx * 0.35, kTextPx, font::sans()); y += kLineH; }
            mContentH = 0.0;
        }
        const double chatA = 1.0 - mPick.value();   // R-ASSIST-11: the chat fades out as the choices fade in
        for (const Shown &m : mShown)
        {
            const double a = alphaOf(m) * chatA;
            auto fade = [a](Color c) { c.a *= a; return c; };
            if (m.role == "tool" || m.role == "info")
            {
                // One muted line: what the agent used, or what it is / what a turn took.
                const bool tool = m.role == "tool";
                const std::string f = tool ? font::mono() : font::sans();
                if (m.wrappedAt != textW)
                {
                    // A tool's title is the agent's words too ("Read `edit.py`"): its markup goes.
                    m.lines = {ellipsize(t, (tool ? "\xC2\xB7 " + md::plain(m.text) : m.text), kSmallPx, f, textW)};
                    m.wrappedAt = textW;
                }
                const std::string &line = m.lines[0];
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
            if (!user && !error)
            {
                // R-ASSIST-12: the agent's reply as it meant it - bold, lists, code, tables - not its
                // Markdown. Parsed and laid out once per width and per streamed change, like the wrap.
                if (m.wrappedAt != textW)
                {
                    m.layout = md::layout(t, md::parse(m.text), textW);
                    m.wrappedAt = textW;
                }
                md::paint(t, m.layout, kPadX, y, md::themeInks(a), box.y, box.y + box.h);
                y += m.layout.height + kGap;
                continue;
            }
            const double inner = user ? textW - 2 * kBubblePad : textW;
            if (m.wrappedAt != textW)
            {
                m.lines = wrap(t, m.text, kTextPx, font::sans(), inner);
                m.wrappedAt = textW;
            }
            const std::vector<std::string> &lines = m.lines;
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
            t.setFill(Color{palette::mutedForeground().r, palette::mutedForeground().g, palette::mutedForeground().b, p * chatA});
            t.drawText(agent + (mModel.state == "starting" ? " is starting\xE2\x80\xA6" : " is working\xE2\x80\xA6"), kPadX,
                       y + kSmallLineH * 0.5 + kSmallPx * 0.35, kSmallPx, font::sans());
            y += kSmallLineH + kGap;
        }
        if (!mShown.empty() || working(mModel.state)) mContentH = y + mScroll.value() - box.y;

        // ── R-ASSIST-11: the choices, fading in over the chat ──
        const double pa = mPick.value();
        if (pa > 0.01)
        {
            auto pf = [pa](Color c) { c.a *= pa; return c; };
            const double top = box.y + kGap - mPickScroll.value();
            t.setFill(pf(palette::mutedForeground()));
            t.drawText("AGENTS ON THIS MACHINE", kPadX, top + kListHeadH * 0.5 + kLabelPx * 0.35, kLabelPx,
                       font::sansSemiBold(), 0.13 * kLabelPx);
            double endY = top + kListHeadH;
            if (mChoices.empty())
            {
                // LOADING, in words: the list is being looked for, not empty.
                t.drawText("Looking for agents\xE2\x80\xA6", kPadX, endY + kLineH * 0.5 + kTextPx * 0.35, kTextPx, font::sans());
                endY += kLineH;
            }
            const double rowW = std::max(40.0, w - 2 * kPadX);
            const double noteW = rowW - 22.75 - 6.5;
            if (mNotesAt != noteW || mNotesFor != mChoices.size())
            {
                mNoteLines.clear();
                for (const auto &c : mChoices) mNoteLines.push_back(ellipsize(t, c.note, kSmallPx, font::sans(), noteW));
                mNotesAt = noteW;
                mNotesFor = mChoices.size();
            }
            for (int i = 0; i < (int)mChoices.size(); ++i)
            {
                const Rect r = choiceRect(i);
                endY = r.y + r.h + kRowGap;
                if (r.y + r.h < box.y || r.y > box.y + box.h) continue;   // culled (R6)
                const bool sel = i == mSelectedChoice;
                const double hv = mRowHover.amount(i);
                if (sel) drawRoundedRect(t, r, radius::control(), Paint::filled(pf(palette::secondary())));
                else if (hv > 0.0) drawRoundedRect(t, r, radius::control(), Paint::filled(pf(palette::hoverWash(hv))));
                const double cx = r.x + 9.75, cy = r.y + r.h * 0.5;
                drawCircle(t, cx, cy, 4.875, Paint::filledStroked(Color{0, 0, 0, 0}, pf(sel ? palette::primary() : palette::mutedForeground()), 1.0));
                if (sel) drawCircle(t, cx, cy, 2.6, Paint::filled(pf(palette::primary())));
                const AgentChoice &c = mChoices[(size_t)i];
                t.setFill(pf(c.installed ? palette::foreground() : palette::mutedForeground()));
                t.drawText(c.name, r.x + 22.75, r.y + 13.0 + 11.0 * 0.35, 11.0, font::sansMedium());
                t.setFill(pf(palette::mutedForeground()));
                t.drawText(mNoteLines[(size_t)i], r.x + 22.75, r.y + 27.0 + kSmallPx * 0.35, kSmallPx, font::sans());
            }
            if (mHintAt != rowW)
            {
                mHintLines = wrap(t, "Pick one and Connect. The command below is editable: acp:<command> runs any ACP agent, "
                                     "local:<url>|<model> a model on Ollama or LM Studio, cmd:<command> any program.",
                                  kSmallPx, font::sans(), rowW);
                mHintAt = rowW;
            }
            endY += kGap;
            t.setFill(pf(palette::mutedForeground()));
            for (const auto &l : mHintLines)
            {
                t.drawText(l, kPadX, endY + kSmallLineH * 0.5 + kSmallPx * 0.35, kSmallPx, font::sans());
                endY += kSmallLineH;
            }
            mPickContentH = endY + mPickScroll.value() - box.y + kGap;
        }

        // R6: an indicator only when there is more to see.
        auto indicator = [&](double content, double offset, double alpha) {
            const double maxS = std::max(0.0, content - box.h);
            if (maxS <= 0.5 || alpha <= 0.01) return;
            const double trackH = box.h - 2 * kGap;
            const double thumbH = std::max(19.5, trackH * box.h / content);
            const double ty = box.y + kGap + (trackH - thumbH) * std::clamp(offset / maxS, 0.0, 1.0);
            drawRoundedRect(t, Rect{w - 4.875, ty, 3.25, thumbH}, radius::pill(), Paint::filled(palette::whiteAlpha(0.18 * alpha)));
        };
        indicator(mContentH, mScroll.value(), 1.0 - pa);
        indicator(mPickContentH, mPickScroll.value(), pa);
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
        if (pickerShown())
        {
            mPickScrollTarget = std::clamp(mPickScrollTarget - delta, 0.0, std::max(0.0, mPickContentH - messagesRect().h));
            return;
        }
        const double maxS = maxScroll();
        mScrollTarget = std::clamp(mScrollTarget - delta, 0.0, maxS);
        mStick = mScrollTarget >= maxS - kStickSlack;   // back at the end: follow the newest again
    }
}
}
