#include "AgentRunner.h"
#include "Machine.h"
#include "service/SolarisService.h"
#include "AssistProcess.h"   // cosmo's, compiled in place (cosmo R-ASSIST-13)
#include <cstdlib>
#include <filesystem>

namespace arstro
{
namespace solaris_host
{
    namespace
    {
        using arstro::cosmo::AssistLaunch;
        using arstro::cosmo_v2::AssistProfile;

        /** What makes the agent Solaris's (R-AGENT-3, R-AGENT-5). */
        AssistProfile solarisAssistProfile()
        {
            AssistProfile p;
            p.appId = "solaris";
            p.brand = "Solaris";
            p.mcpServer = "solaris";
            p.acpClientName = "solaris";
            p.user = "musician";
            p.ccName = "solaris-cc";
            p.attachArgs = {"mcp", "--attach"}; // R-MCP-2
            // $SOLARIS_CC names the CLI outright (an install that keeps it elsewhere, a test); else beside
            // this executable or on PATH
            p.findCc = [] {
                const char *e = std::getenv("SOLARIS_CC");
                return e && *e ? std::string(e) : arstro::cosmo_v2::ProcessAssistant::findCc("solaris-cc");
            };
            // no `local:` agent loop ships with Solaris: localAgentArgs stays empty, the kind is refused
            p.envPrefix = "SOLARIS";
            p.skillsDirEnv = "SOLARIS_SKILLS_DIR";
#ifdef SOLARIS_SKILLS_DIR
            p.skillsDirDefault = SOLARIS_SKILLS_DIR;
#endif
            p.skills = {{"arstro.solaris.compose.melody", false}, {"arstro.solaris.sound.design", false}};
            p.skillBlurbs = {
                {"arstro.solaris.compose.melody", " - writing a melody or a whole sung song the user will like: read its taste notes first"},
                {"arstro.solaris.sound.design", " - building the sounds of a track section by section (future bass and neighbours)"},
            };
            p.allowedTools = "mcp__solaris,Skill,Read,Glob,Grep,Write,Edit,TodoWrite,Bash(python3:*),Bash(ls:*),Bash(mkdir:*)";
            std::string settings, recents;
            machinePaths(settings, recents);
            p.workspace = (std::filesystem::path(settings).parent_path() / "agent").string();

            AssistProfile names;
            names.envPrefix = p.envPrefix;
            names.skillBlurbs = p.skillBlurbs;
            auto where = [](const AssistLaunch &l) {
                return l.project.empty() ? std::string("No song is open yet in the user's window. ")
                                         : "The song open in the user's window is " + l.project + " (" + std::to_string(l.photos) + " mixer strips). ";
            };
            p.agentsMd = [names, where](const AssistLaunch &l, const std::vector<std::string> &skills) {
                std::string s = "# You are inside Solaris\n\n"
                                "You are running INSIDE Solaris, the Arstro digital audio workstation, as the musician's assistant. ";
                s += where(l) + "\n";
                s += "\n## Your tools\n\nSolaris's MCP server `solaris` acts on that open song LIVE - every line appears in the "
                     "user's window as one step of their undo. Do not `project new` or `project open` unless the user asks: "
                     "that replaces what is on their screen.\n"
                     "- solaris_api: the command grammar, every device and its parameters with ranges - read it before your first edit\n"
                     "- solaris_state: the song as it is now (strips, lanes, clips, patterns, devices)\n"
                     "- solaris_run: run command lines, e.g. \"clip add --instrument drums --at 0 --length 16\\npattern steps pt_1 --pitch kick x---x---x---x---\"\n"
                     "A command that makes something prints its id first (ac_ clip, ch_ strip, dv_ device, pt_ pattern). "
                     "Check your work: `ls`, `show <id>`, `audit`, and `render --out <file.wav> --measure` for loudness and peak. "
                     "You cannot hear; measure, and say what you measured.\n";
                s += arstro::cosmo_v2::assistSkillsSection(names, skills);
                s += "\n## How to answer\n\nThe user reads your replies in a small panel beside the song: answer briefly and "
                     "plainly, and say what you changed in musical words (bars, instruments), not ids.\n";
                return s;
            };
            p.systemPrompt = [where](const AssistLaunch &l) {
                return "You are running INSIDE Solaris, the Arstro digital audio workstation, as the musician's assistant. " + where(l) +
                       "Your `solaris` MCP tools act on that open song LIVE: every line appears in the user's window as one step of "
                       "their undo, so do not `project new` or `project open` unless the user asks. Read solaris_api before your "
                       "first edit. You cannot hear: check your work with `ls`, `show <id>`, `audit` and `render --out <file.wav> "
                       "--measure`, and say what you measured. The user reads your replies in a small panel beside the song: "
                       "answer briefly and plainly, in musical words.";
            };
            return p;
        }
    }

    void installAgent(solaris::SolarisService &svc, std::function<std::string()> controlSocket)
    {
        using arstro::cosmo_v2::ProcessAssistant;
        svc.setAssistantFactory([controlSocket]() -> std::unique_ptr<arstro::cosmo::IAssistant> {
            const AssistProfile p = solarisAssistProfile();
            return std::make_unique<ProcessAssistant>(p, ProcessAssistant::configFor(p, controlSocket ? controlSocket() : std::string()));
        });
        solaris::SolarisService *s = &svc;
        svc.setAgentDetector([s] { return ProcessAssistant::detect(solarisAssistProfile(), s->model().settings.agent); });
    }
}
}
