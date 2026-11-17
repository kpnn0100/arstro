#include "AssistConversation.h"

namespace arstro
{
namespace cosmo
{
    AssistConversation::AssistConversation(AssistModel &model, Hooks hooks) : mModel(model), mHooks(std::move(hooks)) {}

    std::vector<std::string> AssistConversation::splitDirs(const std::string &s)
    {
        std::vector<std::string> out;
        for (size_t at = 0; at <= s.size();)
        {
            const size_t colon = s.find(':', at);
            const std::string dir = s.substr(at, colon == std::string::npos ? std::string::npos : colon - at);
            if (!dir.empty()) out.push_back(dir);
            if (colon == std::string::npos) break;
            at = colon + 1;
        }
        return out;
    }

    void AssistConversation::say(const std::string &role, const std::string &text)
    {
        mModel.messages.push_back({role, text});
        if (mHooks.changed) mHooks.changed();
        if (mHooks.emitMessage) mHooks.emitMessage(role + " " + text);
    }

    void AssistConversation::state(const std::string &s)
    {
        if (mModel.state == s) return;
        mModel.state = s;
        if (mHooks.changed) mHooks.changed();
        if (mHooks.emitState) mHooks.emitState(s + (mModel.agent.empty() ? std::string() : " agent=" + mModel.agent));
    }

    bool AssistConversation::start(std::string &err)
    {
        if (mAssistant) { err = "the agent is already running (assist stop first)"; return false; }
        if (!mFactory) { err = mHooks.noRunner; return false; }
        const AssistLaunch l = mHooks.launch ? mHooks.launch() : AssistLaunch();
        mAssistant = mFactory();
        std::string why;
        const bool started = mAssistant && mAssistant->start(l, why);
        // Asked AFTER start: which agent it is depends on the setting start() just read.
        mModel.agent = mAssistant ? mAssistant->name() : std::string();
        if (!started)
        {
            mAssistant.reset();
            err = why.empty() ? std::string("the agent could not be started") : why;
            say("error", err);
            state("failed");
            return false;
        }
        state("starting");
        return true;
    }

    bool AssistConversation::send(const std::string &text, std::string &err)
    {
        if (!mAssistant && !start(err)) return false;   // sending with none running starts one
        if (!mAssistant->send(text, err)) return false;
        say("user", text);
        state("busy");
        return true;
    }

    void AssistConversation::stop()
    {
        if (!mAssistant) return;   // nothing to stop is not a failure: the state is already off
        mAssistant->stop();
        mAssistant.reset();
        if (mAgentOpen)
        {
            mAgentOpen = false;
            if (mHooks.emitMessage) mHooks.emitMessage("agent " + mModel.messages.back().text);
        }
        say("info", "stopped");
        state("off");
    }

    bool AssistConversation::agents(int &found, std::string &err)
    {
        if (!mDetector) { err = "this front end cannot look for agents"; return false; }
        mModel.choices = mDetector();
        if (mHooks.changed) mHooks.changed();
        found = 0;
        for (const auto &a : mModel.choices) found += a.installed ? 1 : 0;
        return true;
    }

    void AssistConversation::pump()
    {
        if (!mAssistant) return;
        std::vector<AssistOutput> out;
        mAssistant->poll(out);
        for (const AssistOutput &o : out)
        {
            // A streamed message is closed by anything that is not more of it - and only then is
            // it an event, so the log and the socket get the whole message once, not every chunk.
            if (o.kind != AssistOutput::Kind::TextDelta && mAgentOpen)
            {
                mAgentOpen = false;
                if (mHooks.emitMessage) mHooks.emitMessage("agent " + mModel.messages.back().text);
            }
            switch (o.kind)
            {
                case AssistOutput::Kind::TextDelta:
                    if (!mAgentOpen)
                    {
                        mModel.messages.push_back({"agent", std::string()});
                        mAgentOpen = true;
                    }
                    mModel.messages.back().text += o.text;
                    if (mHooks.changed) mHooks.changed();   // the view re-reads; no event per chunk
                    break;
                case AssistOutput::Kind::Ready:
                    mModel.agent = mAssistant->name();   // an ACP agent names itself in initialize
                    if (!o.text.empty()) say("info", o.text);
                    if (mModel.state == "starting") state("ready");
                    break;
                case AssistOutput::Kind::Text: say("agent", o.text); break;
                case AssistOutput::Kind::Tool: say("tool", o.text); break;
                case AssistOutput::Kind::Done:
                    if (!o.text.empty()) say("info", o.text);
                    state("ready");
                    break;
                case AssistOutput::Kind::Failed: say("error", o.text); break;
                case AssistOutput::Kind::Exited:
                    mAgentOpen = false;
                    mAssistant.reset();
                    if (!o.text.empty()) say("error", o.text);
                    state(o.text.empty() ? "off" : "failed");
                    return;   // `out` may hold nothing after an exit, and the assistant is gone
            }
        }
    }
}
}
