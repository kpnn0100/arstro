/*
 *  The conversation with the user's agent, as the SERVICE keeps it (R-ASSIST-1/6, R-ASSIST-13).
 *
 *  What `assist start | send | stop | agents` do and what a pump reads back from the agent: the
 *  state machine (off -> starting -> ready -> busy -> ready, failed), the messages, a streamed reply
 *  kept as ONE growing message and ONE event. It was CosmoService's own code until R-ASSIST-13;
 *  it is a class so Interstellar's and Solaris's services run the same conversation instead of a
 *  copy of it.
 *
 *  It knows no application: the state lives in the AssistModel the service hands it (a member of
 *  that service's model, so every front end reads it where it always did), and everything else
 *  comes through Hooks - how to emit the two events, how to build the launch from the app's
 *  settings and open project, how to make the agent (the host's factory). It includes the seam
 *  header alone and makes no OS call.
 */
#pragma once
#include "Assistant.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class AssistConversation
    {
    public:
        using AssistantFactory = std::function<std::unique_ptr<IAssistant>()>;
        using AgentDetector = std::function<std::vector<AgentChoice>()>;

        struct Hooks
        {
            /** The model changed (the service bumps its revision). Called BEFORE the event that
             *  reports the change, as the service always did. */
            std::function<void()> changed;
            /** Emit `assist.state` / `assist.message` with this payload ("ready agent=Claude Code",
             *  "agent Warm light."). The line shapes are frozen (R-SVC-4). */
            std::function<void(const std::string &payload)> emitState;
            std::function<void(const std::string &payload)> emitMessage;
            /** What the service knows that the agent needs: the user's settings (agent, args,
             *  skill directories, allow-all) and the open project. Asked on every start. */
            std::function<AssistLaunch()> launch;
            /** Why `start` is refused when the host installed no factory - the app's own words
             *  ("this front end cannot run an agent - the window and `cosmo-cc serve` can"). */
            std::string noRunner = "this front end cannot run an agent";
        };

        /** `model` is the service's (AppModel::assist) and must outlive this. */
        AssistConversation(AssistModel &model, Hooks hooks);

        /** The host's way of running the user's agent; none = `start` is refused with why. */
        void setFactory(AssistantFactory f) { mFactory = std::move(f); }
        /** R-ASSIST-9: the host's look at this machine (PATH, local model servers). */
        void setDetector(AgentDetector d) { mDetector = std::move(d); }

        bool running() const { return mAssistant != nullptr; }

        /** `assist start`. False + `err` (already running, no runner, the agent would not start -
         *  the last also lands in the conversation as an `error` message and state `failed`). */
        bool start(std::string &err);
        /** `assist send <text>`: starts an agent first when none is running. */
        bool send(const std::string &text, std::string &err);
        /** `assist stop`. Nothing to stop is not a failure. */
        void stop();
        /** `assist agents`: fills `model.choices`. False + `err` when the host gave no detector;
         *  `found` = how many of the choices are installed. */
        bool agents(int &found, std::string &err);
        /** What the agent did since the last pump, into the model and the events. NEVER blocks. */
        void pump();

        /** `a:b:c` -> {a, b, c}: the `agentSkills=` setting as AssistLaunch::skillDirs. */
        static std::vector<std::string> splitDirs(const std::string &colonSeparated);

    private:
        void say(const std::string &role, const std::string &text);
        void state(const std::string &s);

        AssistModel &mModel;
        Hooks mHooks;
        AssistantFactory mFactory;
        AgentDetector mDetector;
        bool mAgentOpen = false;   // the last agent message is still being streamed (TextDelta)
        std::unique_ptr<IAssistant> mAssistant;   // R-ASSIST-6: destroyed (and so stopped) with the service
    };
}
}
