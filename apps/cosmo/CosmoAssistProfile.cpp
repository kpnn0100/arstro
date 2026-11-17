#include "CosmoAssistProfile.h"
#include "core/ProjectStore.h"

namespace arstro
{
namespace cosmo_v2
{
    using arstro::cosmo::AssistLaunch;

    AssistProfile cosmoAssistProfile()
    {
        AssistProfile p;
        p.appId = "cosmo";
        p.brand = "Cosmo";
        p.mcpServer = "cosmo";
        p.acpClientName = "cosmo";
        p.acpClientVersion = "2";
        p.user = "photographer";
        p.ccName = "cosmo-cc";
        p.attachArgs = {"mcp", "--attach"};
        p.localAgentArgs = {"agent"};   // R-ASSIST-9: `cosmo-cc agent --endpoint <url> --model <model>`
        p.envPrefix = "COSMO";
        p.skillsDirEnv = "COSMO_SKILLS_DIR";
#ifdef COSMO_SKILLS_DIR
        p.skillsDirDefault = COSMO_SKILLS_DIR;
#endif
        // R-ASSIST-4 (amended 2026-11-05): the colour-grading ENTRY skill and every skill it loads
        // (`arstro.color.grading.<part>`). The entry comes first: `cosmo_grade` runs its editor.
        p.skills = {{"arstro.color.grading", true}};
        p.skillBlurbs = {
            {"arstro.color.grading",
             " - THE ENTRY POINT for colour: grading in the photographer's own taste, cinematic, a mood or "
             "look, correcting, matching a set, many photos at once; it says which skill below to load"},
            {"arstro.color.grading.style",
             " - what the user MEANS by a look in their own words, any language (warm, moody, tong Han, "
             "vintage film but brighter, the night ones neon) and the edit that answers it"},
        };
        // R-ASSIST-5: what Claude Code may do without asking - cosmo's tools, reading, the skill's
        // scripts and its plan files. Nothing else is pre-approved (`agentArgs=` widens it).
        p.allowedTools = "mcp__cosmo,Skill,Read,Glob,Grep,Write,Edit,TodoWrite,Bash(python3:*),Bash(ls:*),Bash(mkdir:*)";
        p.workspace = arstro::cosmo::ProjectStore::configDir() + "/agent";

        // The skills section is the shared one; it needs the names above, not the builders below.
        AssistProfile names;
        names.envPrefix = p.envPrefix;
        names.skillBlurbs = p.skillBlurbs;
        p.agentsMd = [names](const AssistLaunch &l, const std::vector<std::string> &skills) {
            std::string s = "# You are inside Cosmo\n\n"
                            "You are running INSIDE Cosmo, the Arstro photo editor, as the photographer's assistant. ";
            s += l.project.empty() ? "No project is open yet in the user's window.\n"
                                   : "The project open in the user's window is " + l.project + " (" + std::to_string(l.photos) + " photos).\n";
            s += "\n## Your tools\n\nCosmo's MCP server `cosmo` acts on that open project LIVE - every edit appears in the "
                 "user's window as one undoable step. Do not open another project unless the user asks.\n"
                 "- cosmo_api: the command grammar and every adjustment key - read it before your first edit\n"
                 "- cosmo_state: the project, its photos (address them by `node`), the selection, the history\n"
                 "- cosmo_preview: LOOK at a photo, as edited or as shot (before=true)\n"
                 "- cosmo_analyze: measure a photo - exposure, colour cast, hues, sky/skin/foliage - and its camera data\n"
                 "- cosmo_run: run command lines, e.g. \"select 2\\nset exposure=0.3 temp=6000\"\n";
            s += assistSkillsSection(names, skills);
            s += "\n## How to answer\n\nThe user reads your replies in a small panel beside the photo: answer briefly and "
                 "plainly. Look at a photo before and after you change it.\n";
            return s;
        };
        p.systemPrompt = [](const AssistLaunch &l) {
            std::string s =
                "You are running INSIDE Cosmo, the Arstro photo editor, as the photographer's assistant. ";
            if (l.project.empty())
                s += "No project is open yet in the user's window. ";
            else
                s += "The project open in the user's window is " + l.project + " (" + std::to_string(l.photos) +
                     " photos). ";
            s += "Your `cosmo` MCP tools act on that open project LIVE: every edit appears in the user's window as "
                 "one undoable step, so do not `project open` another project unless the user asks. "
                 "For colour grading - cinematic, a mood or a look, correcting, matching a set, editing many photos - "
                 "use the arstro.color.grading skill; its scripts reach this same window through the COSMO_CONTROL "
                 "environment variable. LOOK at a photo (cosmo_preview) before and after you change it. "
                 "The user reads your replies in a small panel beside the photo: answer briefly and plainly.";
            return s;
        };
        return p;
    }
}
}
