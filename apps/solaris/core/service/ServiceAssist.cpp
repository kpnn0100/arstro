/*
 *  solaris_core — the user's own agent (R-AGENT-1/2).
 *
 *  The conversation is cosmo's AssistConversation, compiled in place (cosmo R-ASSIST-13): the state
 *  machine, the messages, a streamed reply kept as one growing message. This file is only what is
 *  Solaris's: the four verbs, the two events in this service's k=v form, the launch built from the
 *  machine's settings and the open song, and the refusal's words. The PROCESS is the host's
 *  (setAssistantFactory): the core makes no OS call (R-SVC-4).
 */
#include "SolarisService.h"

namespace arstro
{
namespace solaris
{
    using arstro::cosmo::AssistConversation;

    AssistConversation::Hooks SolarisService::assistHooks()
    {
        AssistConversation::Hooks h;
        h.changed = [this] { mModel.assist = mAssistModel; ++mModel.revision; }; // a view rebinds on the revision
        // The conversation hands over cosmo's payloads ("ready agent=Claude Code", "agent Warm.");
        // here they are split into this service's fields - the same facts, in Solaris's event form.
        h.emitState = [this](const std::string &payload) {
            const size_t sp = payload.find(' ');
            const std::string rest = sp == std::string::npos ? std::string() : payload.substr(sp + 1);
            emit(Event(Event::Kind::AssistState).with("state", payload.substr(0, sp)).with("agent", rest.rfind("agent=", 0) == 0 ? rest.substr(6) : mAssistModel.agent));
        };
        h.emitMessage = [this](const std::string &payload) {
            const size_t sp = payload.find(' ');
            emit(Event(Event::Kind::AssistMessage).with("role", payload.substr(0, sp)).with("text", sp == std::string::npos ? std::string() : payload.substr(sp + 1)));
        };
        h.launch = [this] {
            arstro::cosmo::AssistLaunch l;
            l.agent = mSettings.agent;
            l.args = mSettings.agentArgs;
            l.skillDirs = AssistConversation::splitDirs(mSettings.agentSkills);
            l.project = mOpen ? mModel.projectPath : std::string();
            l.photos = mOpen ? (int)mModel.strips.size() : 0; // what the song holds: its strips
            l.allowAll = mSettings.agentAllowAll;
            return l;
        };
        h.noRunner = "this front end cannot run an agent — the window can (`solaris --control`)";
        return h;
    }

    bool SolarisService::assistCommand(const Command &c, std::string &err)
    {
        using K = Command::Kind;
        std::string why;
        switch (c.kind)
        {
        case K::AssistStart:
            if (!mAssist.start(why)) { err = "assist: " + why; return false; }
            return true;
        case K::AssistSend:
        {
            std::string text;
            for (const auto &a : c.args) text += (text.empty() ? "" : " ") + a;
            if (!mAssist.send(text, why)) { err = "assist: " + why; return false; }
            return true;
        }
        case K::AssistStop:
            mAssist.stop();
            return true;
        case K::AssistAgents:
        {
            int found = 0;
            if (!mAssist.agents(found, why)) { err = "assist agents: " + why; return false; }
            mModel.assist = mAssistModel;
            mOutput.clear();
            for (const auto &ch : mAssistModel.choices) mOutput += std::string(ch.installed ? "installed  " : "missing    ") + ch.name + "  " + ch.agent + "\n";
            return true;
        }
        default:
            return false;
        }
    }
}
}
