/*
 *  R-ASSIST-13 — the agent module is built from an app's profile, not from cosmo.
 *
 *  A fake app "demo" (MCP server `demo`, a `demo-cc` that does not exist, DEMO_* environment, its own
 *  skill) runs the SAME ProcessAssistant and AssistConversation cosmo runs, and nothing it generates
 *  says "cosmo": the MCP config, Claude Code's argv and allowed tools, AGENTS.md, the system prompt,
 *  the permission policy, the environment a one-shot agent sees. This target links NO cosmo_core -
 *  which is the other half of the claim: a sibling app can compile these files in place.
 *
 *  No agent, no network: the one process started is `sh -c echo`.
 */
#include "AssistProcess.h"
#include "core/assist/AssistConversation.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

using namespace arstro::cosmo;
using arstro::cosmo_v2::AssistProfile;
using arstro::cosmo_v2::ProcessAssistant;
namespace fs = std::filesystem;

namespace
{
    std::string slurp(const std::string &path)
    {
        std::ifstream f(path);
        std::stringstream s;
        s << f.rdbuf();
        return s.str();
    }

    bool has(const std::string &hay, const std::string &needle) { return hay.find(needle) != std::string::npos; }

    /** No spelling of the other app's name. */
    bool cosmoFree(const std::string &s) { return !has(s, "cosmo") && !has(s, "Cosmo") && !has(s, "COSMO"); }

    AssistProfile demoProfile(const std::string &root)
    {
        AssistProfile p;
        p.appId = "demo";
        p.brand = "Demo";
        p.mcpServer = "demo";
        p.acpClientName = "demo";
        p.user = "musician";
        p.ccName = "demo-cc";
        p.findCc = [] { return std::string("/opt/demo/bin/demo-cc"); };   // a fake: never run by this test
        p.envPrefix = "DEMO";
        p.skillsDirEnv = "DEMO_SKILLS_DIR";
        p.skillsDirDefault = root + "/skills-src";
        p.skills = {{"demo.skill", true}, {"demo.absent", false}};
        p.skillBlurbs = {{"demo.skill", " - the demo's entry skill"}};
        p.allowedTools = "mcp__demo,Skill,Read";
        p.workspace = root + "/agent";
        return p;   // agentsMd / systemPrompt left empty: the module's plain defaults
    }

    void test_a_non_cosmo_profile_carries_its_own_names(const std::string &root)
    {
        // The skills directory holds the demo's skill, a part of it, and cosmo's - which is not asked for.
        for (const char *k : {"demo.skill", "demo.skill.part", "arstro.color.grading"})
        {
            fs::create_directories(root + "/skills-src/" + k);
            std::ofstream(root + "/skills-src/" + k + "/SKILL.md") << "# " << k << "\n";
        }
        unsetenv("DEMO_SKILLS_DIR");
        unsetenv("COSMO_CONTROL");
        const AssistProfile p = demoProfile(root);
        const std::string sock = root + "/demo.sock";

        const ProcessAssistant::Config c = ProcessAssistant::configFor(p, sock);
        assert(c.cc == "/opt/demo/bin/demo-cc" && c.controlSocket == sock && c.workspace == root + "/agent");
        assert(c.bundledSkills.size() == 2 && "the entry skill and its part; not the absent one, not cosmo's");
        assert(has(c.bundledSkills[0], "/demo.skill") && has(c.bundledSkills[1], "/demo.skill.part"));

        // Claude Code's argv: the demo's MCP config and the demo's allowed tools.
        AssistLaunch l;
        l.agent = "claude";
        l.project = "/songs/a.demo";
        const std::vector<std::string> argv = ProcessAssistant::claudeArgv(p, l, c);
        std::string mcpConfig, allowed, prompt;
        for (size_t i = 0; i + 1 < argv.size(); ++i)
        {
            if (argv[i] == "--mcp-config") mcpConfig = argv[i + 1];
            if (argv[i] == "--allowedTools") allowed = argv[i + 1];
            if (argv[i] == "--append-system-prompt") prompt = argv[i + 1];
        }
        assert(mcpConfig == root + "/agent/demo-mcp.json");
        assert(allowed == "mcp__demo,Skill,Read");
        assert(has(prompt, "INSIDE Demo") && has(prompt, "`demo` MCP tools") && has(prompt, "/songs/a.demo") && cosmoFree(prompt));

        // The workspace, prepared by start() - a one-shot agent, so nothing is spawned yet.
        ProcessAssistant a(p, c);
        l.agent = "cmd:echo control=$DEMO_CONTROL cc=$DEMO_CC project=$DEMO_PROJECT other=${COSMO_CONTROL-none}";
        std::string err;
        assert(a.start(l, err) && err.empty());

        const std::string mcp = slurp(root + "/agent/demo-mcp.json");
        assert(has(mcp, "\"mcpServers\"") && has(mcp, "\"demo\"") && has(mcp, "/opt/demo/bin/demo-cc"));
        assert(has(mcp, "\"mcp\"") && has(mcp, "\"--attach\"") && has(mcp, sock));
        assert(cosmoFree(mcp));
        assert(!fs::exists(root + "/agent/cosmo-mcp.json"));

        const std::string md = slurp(root + "/agent/AGENTS.md");
        assert(has(md, "# You are inside Demo") && has(md, "MCP server `demo`") && has(md, "the musician's assistant"));
        assert(has(md, "DEMO_CONTROL") && has(md, "- demo.skill: skills/demo.skill/SKILL.md - the demo's entry skill"));
        assert(has(md, "- demo.skill.part: skills/demo.skill.part/SKILL.md\n"));
        assert(cosmoFree(md) && !has(md, "color.grading") && !has(md, "photographer"));
        assert(slurp(root + "/agent/GEMINI.md") == md && slurp(root + "/agent/QWEN.md") == md);
        assert(fs::exists(root + "/agent/skills/demo.skill/SKILL.md") && fs::exists(root + "/agent/.claude/skills/demo.skill.part/SKILL.md"));
        assert(!fs::exists(root + "/agent/skills/arstro.color.grading"));

        // The ACP server entry (session/new) is the demo's too.
        const std::string named = ProcessAssistant::mcpServerJson(p, c, true);
        assert(has(named, "\"name\":\"demo\"") && has(named, "demo-cc") && cosmoFree(named));

        // R-ASSIST-5: the app's OWN tools are the pre-approved ones - the demo's, not cosmo's.
        assert(ProcessAssistant::permitted("other", "demo_run: set x=1", "", {}, p, l, c));
        assert(!ProcessAssistant::permitted("other", "cosmo_run: set exposure=1", "", {}, p, l, c));
        assert(ProcessAssistant::permitted("edit", "write", "", {root + "/agent/plan.json"}, p, l, c));
        assert(!ProcessAssistant::permitted("edit", "write", "", {"/etc/passwd"}, p, l, c));

        // The environment the agent finds the app through is DEMO_*, and there is no COSMO_*.
        assert(a.send("hello", err));
        std::vector<AssistOutput> out;
        std::string answer;
        bool done = false;
        for (int i = 0; i < 500 && !done; ++i)
        {
            out.clear();
            a.poll(out);
            for (const auto &o : out)
            {
                if (o.kind == AssistOutput::Kind::Text) answer = o.text;
                if (o.kind == AssistOutput::Kind::Done) done = true;
            }
            if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        assert(done && "the one-shot agent finished");
        assert(answer == "control=" + sock + " cc=/opt/demo/bin/demo-cc project=/songs/a.demo other=none");
        a.stop();

        // An app that ships no agent loop of its own has no `local:` kind: refused in its own name, not offered.
        ProcessAssistant b(p, c);
        l.agent = "local:http://127.0.0.1:1/v1|some-model";
        err.clear();
        assert(!b.start(l, err) && has(err, "Demo has no agent loop") && cosmoFree(err));
        for (const AgentChoice &ch : ProcessAssistant::detect(p, std::string()))
            assert(ch.agent.rfind("local:", 0) != 0 && cosmoFree(ch.note));

        // A missing CLI is named as the demo's.
        AssistProfile lost = p;
        lost.findCc = [] { return std::string(); };
        ProcessAssistant d(lost, ProcessAssistant::configFor(lost, sock));
        l.agent = "cmd:echo";
        err.clear();
        assert(!d.start(l, err) && err == "demo-cc was not found next to demo or on PATH");
        printf("[PASS] a_non_cosmo_profile_carries_its_own_names\n");
    }

    // The conversation needs no application either: a model, two event sinks and a launch builder.
    void test_the_conversation_runs_without_a_service()
    {
        struct Fake : IAssistant
        {
            std::vector<AssistOutput> *queue;
            AssistLaunch *seen;
            bool start(const AssistLaunch &l, std::string &) override { *seen = l; return true; }
            bool send(const std::string &, std::string &) override { return true; }
            void stop() override {}
            void poll(std::vector<AssistOutput> &out) override { out.swap(*queue); queue->clear(); }
            std::string name() const override { return "Fake"; }
        };
        AssistModel model;
        std::vector<std::string> events;
        std::vector<AssistOutput> queue;
        AssistLaunch seen;
        int revision = 0;
        AssistConversation::Hooks h;
        h.changed = [&revision] { ++revision; };
        h.emitState = [&events](const std::string &p) { events.push_back("state " + p); };
        h.emitMessage = [&events](const std::string &p) { events.push_back("message " + p); };
        h.launch = [] { AssistLaunch l; l.agent = "fake"; l.skillDirs = AssistConversation::splitDirs("/a::/b"); return l; };
        h.noRunner = "the demo cannot run an agent here";
        AssistConversation conv(model, h);

        std::string err;
        assert(!conv.start(err) && err == "the demo cannot run an agent here" && model.state == "off");
        conv.setFactory([&] { auto f = std::make_unique<Fake>(); f->queue = &queue; f->seen = &seen; return std::unique_ptr<IAssistant>(std::move(f)); });
        assert(conv.send("hi", err) && conv.running() && model.state == "busy" && model.agent == "Fake");
        assert(seen.agent == "fake" && seen.skillDirs.size() == 2 && seen.skillDirs[1] == "/b");
        queue = {{AssistOutput::Kind::TextDelta, "Hel"}, {AssistOutput::Kind::TextDelta, "lo"}, {AssistOutput::Kind::Done, ""}};
        conv.pump();
        assert(model.messages.size() == 2 && model.messages[1].text == "Hello" && model.state == "ready");
        const std::vector<std::string> want = {"state starting agent=Fake", "message user hi", "state busy agent=Fake",
                                               "message agent Hello", "state ready agent=Fake"};
        assert(events == want && revision > 0);
        int found = 0;
        assert(!conv.agents(found, err));
        conv.stop();
        assert(!conv.running() && model.state == "off" && model.messages.back().text == "stopped");
        printf("[PASS] the_conversation_runs_without_a_service\n");
    }
}

int main()
{
    // Not named after cosmo: the paths under it are in the files this test reads for that word.
    std::string tmpl = (fs::temp_directory_path() / "assist-profile-XXXXXX").string();
    const char *dir = mkdtemp(&tmpl[0]);
    assert(dir);
    const std::string root = dir;
    test_a_non_cosmo_profile_carries_its_own_names(root);
    test_the_conversation_runs_without_a_service();
    std::error_code ec;
    fs::remove_all(root, ec);
    printf("cosmo_assist_profile: all passed\n");
    return 0;
}
