/*
 *  cosmo host — the user's own agent, run as a process beside cosmo (R-ASSIST-2..6).
 *
 *  The service owns the conversation; this owns the PROCESS: fork/exec, pipes read without
 *  blocking, the agent's wire protocol, and stopping it with everything it started. Host-side
 *  because a process is an OS object (cosmo_core makes no OS call), and shared by every front end
 *  that can run one: the window and `cosmo-cc serve`.
 *
 *  Two kinds of agent (R-ASSIST-2):
 *  - **Claude Code** (`agent=claude`, the default) — one process for the whole conversation,
 *    spoken to in its streaming-JSON protocol (`-p --input-format stream-json --output-format
 *    stream-json`), so it keeps its context between messages. It is given cosmo's MCP tools
 *    attached to THIS application (`cosmo-cc mcp --attach <socket>`, R-ASSIST-3), runs in cosmo's
 *    own workspace whose `.claude/skills` holds the bundled colour-grading skill (R-ASSIST-4),
 *    and may use only the tools named in `kAllowedTools` unless the user widens it (R-ASSIST-5).
 *  - **Any other command** — run once per message through `sh -c`, `{prompt}` replaced by the
 *    message (shell-quoted), or the message on its stdin when the command has no `{prompt}`; its
 *    output is the answer. It finds cosmo through the same environment.
 *
 *  - **An ACP agent** (`agent=acp:<command>`, R-ASSIST-9) — one process for the conversation, spoken
 *    to as an Agent Client Protocol CLIENT: `initialize`, `session/new` with cosmo's MCP server in it,
 *    `session/prompt` per message, `session/update`s shown as they stream, `session/request_permission`
 *    answered by the R-ASSIST-5 policy. Gemini CLI, OpenCode, Goose, Qwen Code, Codex/Claude adapters…
 *  - **A local model** (`agent=local:<url>|<model>`) — the ACP agent cosmo ships for it,
 *    `cosmo-cc agent --endpoint <url> --model <model>`, over the same client.
 *
 *  Every kind runs in a workspace whose AGENTS.md (also GEMINI.md, QWEN.md) tells it where it is and
 *  where the colour-grading skill is (R-ASSIST-10), with the skill linked in as `skills/<name>` and,
 *  for Claude Code, `.claude/skills/<name>`.
 *
 *  Either way the process gets COSMO_CONTROL (the socket), COSMO_CC (the `cosmo-cc` to attach
 *  with) and COSMO_PROJECT (the open project), which is how the skill's own scripts drive the open
 *  project rather than a copy (R-ASSIST-4). POSIX only, like the control socket.
 */
#pragma once
#include "core/assist/Assistant.h"
#include <string>
#include <sys/types.h>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class ProcessAssistant : public arstro::cosmo::IAssistant
    {
    public:
        struct Config
        {
            std::string cosmoCc;                     // the `cosmo-cc` the agent's MCP server runs
            std::string controlSocket;               // this application's control socket (R-SVC-8)
            std::string workspace;                   // where the agent runs: <config>/agent
            std::vector<std::string> bundledSkills;  // skill directories loaded by default (R-ASSIST-4)
        };

        /** R-ASSIST-5: what Claude Code may do without asking — cosmo's tools, reading, the skill's
         *  scripts and its plan files. Nothing else is pre-approved (`agentArgs=` widens it). */
        static const char *const kAllowedTools;

        explicit ProcessAssistant(Config c);
        ~ProcessAssistant() override;

        bool start(const arstro::cosmo::AssistLaunch &launch, std::string &err) override;
        bool send(const std::string &text, std::string &err) override;
        void stop() override;
        void poll(std::vector<arstro::cosmo::AssistOutput> &out) override;
        std::string name() const override;

        // ── exposed so a test can read them without starting anything ──
        /** True when `agent`'s program is Claude Code. */
        static bool isClaude(const std::string &agent);
        /** R-ASSIST-9: what this machine has — the agents cosmo knows how to talk to, whether each is
         *  installed, and a running Ollama / LM Studio with its models. `current` (the user's own
         *  setting) is kept as the "Custom" choice. Local servers are probed with a 250 ms timeout. */
        static std::vector<arstro::cosmo::AgentChoice> detect(const std::string &current);
        /** What the workspace's AGENTS.md says (R-ASSIST-10) - also an ACP agent's first-message context. */
        static std::string agentsMd(const arstro::cosmo::AssistLaunch &l, const std::vector<std::string> &skills);
        /** R-ASSIST-5: may an ACP agent do what `toolCall` describes? `what` names it for the refusal. */
        static bool permitted(const std::string &kind, const std::string &title, const std::string &command,
                              const std::vector<std::string> &paths, const arstro::cosmo::AssistLaunch &l,
                              const Config &c);
        /** What the agent is told about where it is (appended to Claude Code's system prompt). */
        static std::string systemPrompt(const arstro::cosmo::AssistLaunch &l);
        /** Claude Code's argv, the program first. */
        static std::vector<std::string> claudeArgv(const arstro::cosmo::AssistLaunch &l, const Config &c);
        /** One line of Claude Code's stream-JSON output -> what the conversation shows. */
        static void parseClaudeLine(const std::string &line, std::vector<arstro::cosmo::AssistOutput> &out);
        /** `command` with `{prompt}` replaced by `prompt`, single-quoted for `sh -c`. */
        static std::string fillPrompt(const std::string &command, const std::string &prompt, bool &usedPlaceholder);
        /** The program on PATH (or as given, when it has a slash); "" when there is none. */
        static std::string findProgram(const std::string &program);
        /** The `cosmo-cc` beside this executable (or in its cli/), else on PATH. */
        static std::string findCosmoCc();
        /** The skills an agent gets by default (R-ASSIST-4): `arstro.color.grading` from
         *  $COSMO_SKILLS_DIR, else the directory this build was configured with. */
        static std::vector<std::string> defaultSkills();
        /** A Config for an application whose control socket is `socket`: cosmo-cc, the workspace
         *  `<config>/agent`, the default skills. */
        static Config configFor(const std::string &socket);

    private:
        enum class Proto { Claude, Acp, OneShot };
        void acpSend(const std::string &json);
        int acpCall(const std::string &method, const std::string &paramsJson);
        void handleAcpLine(const std::string &line, std::vector<arstro::cosmo::AssistOutput> &out);
        void acpPrompt(const std::string &text);
        bool prepareWorkspace(std::string &err);
        bool spawn(const std::vector<std::string> &argv, std::string &err);
        void readPipes();
        void reap(int status, std::vector<arstro::cosmo::AssistOutput> &out);

        Config mCfg;
        arstro::cosmo::AssistLaunch mLaunch;
        bool mClaude = false;      // Proto::Claude
        bool mAcp = false;         // Proto::Acp
        bool mPersistent = false;  // one process for the conversation (Claude, ACP)
        std::string mDisplayName;  // what the agent called itself (ACP agentInfo), or the program
        // ── ACP client state (R-ASSIST-9) ──
        int mRpcId = 0, mInitId = -1, mNewId = -1, mPromptId = -1;
        std::string mSession;                       // "" until session/new answered
        std::vector<std::string> mAcpQueue;         // messages sent before the session existed
        bool mContextSent = false;                  // the first prompt carries AGENTS.md's context
        double mPromptStartMs = 0.0;
        std::vector<std::pair<std::string, std::string>> mToolTitles;   // toolCallId -> title (for updates)
        std::vector<std::string> mSkillNames;
        bool mStarted = false;
        bool mAnnounced = false;     // the one-shot kind says Ready once, on the first poll
        bool mStopping = false;
        pid_t mPid = -1;
        int mIn = -1, mOut = -1, mErr = -1;
        std::string mOutBuf, mErrBuf, mOneShotText, mPendingStdin;
    };
}
}
