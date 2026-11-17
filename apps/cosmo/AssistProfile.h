/*
 *  The agent module's one parameter: which APPLICATION the agent is running inside (R-ASSIST-13).
 *
 *  ProcessAssistant (the agent as a process), AssistConversation (the conversation in the service)
 *  and AgentPanel (the Agent tab) are shared by cosmo, Interstellar and Solaris - compiled in place
 *  from apps/cosmo, as the sibling apps already compile cosmo's widgets. Everything that differs
 *  between the apps is HERE, as plain data and two text builders: the names (the app, its MCP
 *  server, its `<app>-cc`), how the agent's tools attach, the environment its skills' scripts read,
 *  which skills it gets, what it is told about where it is, and what it may do without asking.
 *
 *  `cosmoAssistProfile()` (CosmoAssistProfile.h) is cosmo's; a sibling app writes its own. This
 *  header depends on the seam alone (core/assist/Assistant.h) - no session, no ProjectStore.
 */
#pragma once
#include "core/assist/Assistant.h"
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    struct AssistProfile
    {
        // ── names ──
        /** The app, lower case: "cosmo". Names the workspace's MCP config (`<appId>-mcp.json`) and
         *  the app in an error ("... could not reach cosmo"). */
        std::string appId;
        /** The app as the user reads it: "Cosmo". The ACP client's title, the first-message
         *  context's heading. */
        std::string brand;
        /** The MCP server's name as the agent sees it: "cosmo" -> tools `mcp__cosmo__...`. Also the
         *  word that marks the app's own tools in an ACP permission request (R-ASSIST-5). */
        std::string mcpServer;
        /** The ACP `clientInfo.name` ("cosmo") and `.version` ("2"); its title is `brand`. */
        std::string acpClientName;
        std::string acpClientVersion = "1";
        /** Who the agent is talking to, for the first ACP message's "[The <user> says]": "photographer". */
        std::string user = "user";

        // ── the app's CLI: the agent's hands ──
        /** The CLI's file name: "cosmo-cc". Looked for beside this executable (and in its cli/),
         *  then on PATH - unless `findCc` says otherwise. */
        std::string ccName;
        /** Where that CLI is, "" when it is not installed. Empty = ProcessAssistant::findCc(ccName). */
        std::function<std::string()> findCc;
        /** What the CLI is run with to serve the app's MCP tools attached to THIS application; the
         *  control socket's path is appended: {"mcp", "--attach"} (R-ASSIST-3). */
        std::vector<std::string> attachArgs{"mcp", "--attach"};
        /** The `local:<url>|<model>` kind: the CLI's own ACP agent loop, run as
         *  `<cc> <localAgentArgs...> --endpoint <url> --model <model>` ({"agent"} in cosmo).
         *  EMPTY = this app ships none: `local:` is refused with why and not offered by detect(). */
        std::vector<std::string> localAgentArgs;

        // ── the environment the agent (and its skills' scripts) finds the app through ──
        /** "COSMO" -> COSMO_CONTROL (the socket), COSMO_CC (the CLI), COSMO_PROJECT (the open project). */
        std::string envPrefix;

        // ── skills (R-ASSIST-4) ──
        /** The environment variable naming the directory skills are looked for in ("COSMO_SKILLS_DIR"),
         *  and the directory used when it is unset (a developer build's `.claude/skills`; "" = none). */
        std::string skillsDirEnv;
        std::string skillsDirDefault;
        /** The skills the agent gets by default, in order - the entry skill first. `withParts` also
         *  takes every `<name>.<part>` directory beside it (sorted), each skill it loads. A skill that
         *  is not there is skipped, with its parts. */
        struct Skill
        {
            std::string name;
            bool withParts = false;
        };
        std::vector<Skill> skills;
        /** What AGENTS.md says after a skill's path: {skill name, " - what it is for"}. */
        std::vector<std::pair<std::string, std::string>> skillBlurbs;

        // ── what the agent is told ──
        /** The workspace's AGENTS.md (R-ASSIST-10) - also an ACP agent's first-message context.
         *  `skills` are the names linked into the workspace. Empty = a plain default built from the
         *  names above (ProcessAssistant::agentsMd). */
        std::function<std::string(const arstro::cosmo::AssistLaunch &, const std::vector<std::string> &skills)> agentsMd;
        /** Appended to Claude Code's system prompt. Empty = a plain default from the names above. */
        std::function<std::string(const arstro::cosmo::AssistLaunch &)> systemPrompt;

        // ── what it may do without asking (R-ASSIST-5) ──
        /** Claude Code's `--allowedTools`: "mcp__cosmo,Skill,Read,...". */
        std::string allowedTools;

        // ── where it runs ──
        /** The agent's working directory, created on start: "<config>/agent". */
        std::string workspace;

        std::string controlEnv() const { return envPrefix + "_CONTROL"; }
        std::string ccEnv() const { return envPrefix + "_CC"; }
        std::string projectEnv() const { return envPrefix + "_PROJECT"; }
        std::string mcpConfigName() const { return appId + "-mcp.json"; }
    };

    /** AGENTS.md's "## Skills" section, the same words in every app: where each linked skill is, its
     *  blurb, and the environment variable its scripts reach the window through. "" for no skills. */
    inline std::string assistSkillsSection(const AssistProfile &p, const std::vector<std::string> &skills)
    {
        if (skills.empty()) return std::string();
        std::string s = "\n## Skills\n\nRead a skill's SKILL.md before doing what it covers, and follow it. Its scripts run "
                        "from this directory and reach this window through the " + p.controlEnv() + " environment variable.\n";
        for (const auto &k : skills)
        {
            std::string blurb;
            for (const auto &b : p.skillBlurbs)
                if (b.first == k) blurb = b.second;
            s += "- " + k + ": skills/" + k + "/SKILL.md" + blurb + "\n";
        }
        return s;
    }
}
}
