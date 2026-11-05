#include "AssistProcess.h"
#include "core/ProjectStore.h"
#include "HttpLite.h"
#include "ntwb/Json.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char **environ;

namespace arstro
{
namespace cosmo_v2
{
    using arstro::cosmo::AssistLaunch;
    using arstro::cosmo::AssistOutput;
    using arstro::ntwb::Json;

    const char *const ProcessAssistant::kAllowedTools =
        "mcp__cosmo,Skill,Read,Glob,Grep,Write,Edit,TodoWrite,Bash(python3:*),Bash(ls:*),Bash(mkdir:*)";

    namespace
    {
        /** Words of a command line: whitespace-separated, a "double-quoted" run kept whole. */
        std::vector<std::string> words(const std::string &s)
        {
            std::vector<std::string> out;
            std::string cur;
            bool quoted = false, any = false;
            for (char ch : s)
            {
                if (ch == '"') { quoted = !quoted; any = true; continue; }
                if (!quoted && (ch == ' ' || ch == '\t'))
                {
                    if (any) out.push_back(cur);
                    cur.clear();
                    any = false;
                    continue;
                }
                cur += ch;
                any = true;
            }
            if (any) out.push_back(cur);
            return out;
        }

        void setNonBlocking(int fd)
        {
            const int fl = fcntl(fd, F_GETFL, 0);
            if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        }

        /** The last `n` bytes of `s`, from a line start when there is one. */
        std::string tail(const std::string &s, size_t n = 600)
        {
            if (s.size() <= n) return s;
            std::string t = s.substr(s.size() - n);
            const size_t nl = t.find('\n');
            return nl != std::string::npos ? t.substr(nl + 1) : t;
        }

        std::string trim(std::string s)
        {
            while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\r' || s.back() == '\t')) s.pop_back();
            size_t i = 0;
            while (i < s.size() && (s[i] == '\n' || s[i] == ' ' || s[i] == '\t')) ++i;
            return s.substr(i);
        }

        /** "mcp__cosmo__cosmo_run" -> "cosmo_run"; other names as they are. */
        std::string shortTool(const std::string &name)
        {
            const size_t i = name.rfind("__");
            return name.rfind("mcp__", 0) == 0 && i != std::string::npos ? name.substr(i + 2) : name;
        }

        /** One line saying what a tool call was for — the first meaningful argument, cut short. */
        std::string toolBrief(const std::string &name, const Json &input)
        {
            std::string what;
            for (const char *k : {"script", "command", "skill", "file_path", "path", "pattern", "description", "node"})
                if (input.has(k))
                {
                    const Json &v = input[k];
                    what = v.isString() ? v.asString() : v.dump();
                    break;
                }
            for (char &ch : what) if (ch == '\n') ch = ';';
            if (what.size() > 140) what = what.substr(0, 137) + "...";
            const std::string n = shortTool(name);
            return what.empty() ? n : n + ": " + what;
        }

        std::string dirOf(const std::string &p)
        {
            const size_t s = p.rfind('/');
            return s == std::string::npos ? std::string(".") : p.substr(0, s);
        }
    }

    ProcessAssistant::ProcessAssistant(Config c) : mCfg(std::move(c)) {}

    ProcessAssistant::~ProcessAssistant() { stop(); }

    bool ProcessAssistant::isClaude(const std::string &agent)
    {
        const std::vector<std::string> w = words(agent);
        if (w.empty()) return true;   // the default
        const std::string prog = w[0].substr(w[0].rfind('/') == std::string::npos ? 0 : w[0].rfind('/') + 1);
        return prog == "claude";
    }

    std::string ProcessAssistant::name() const
    {
        if (!mDisplayName.empty()) return mDisplayName;
        if (mLaunch.agent.rfind("local:", 0) == 0)
        {
            const size_t bar = mLaunch.agent.find('|');
            return "Local model" + (bar == std::string::npos ? std::string() : " (" + mLaunch.agent.substr(bar + 1) + ")");
        }
        if (mLaunch.agent.rfind("acp:", 0) == 0)
        {
            const std::vector<std::string> w = words(mLaunch.agent.substr(4));
            return w.empty() ? std::string("ACP agent") : w[0].substr(w[0].rfind('/') == std::string::npos ? 0 : w[0].rfind('/') + 1);
        }
        if (mClaude || isClaude(mLaunch.agent)) return "Claude Code";
        const std::vector<std::string> w = words(mLaunch.agent);
        return w.empty() ? std::string("agent") : w[0].substr(w[0].rfind('/') == std::string::npos ? 0 : w[0].rfind('/') + 1);
    }

    std::string ProcessAssistant::findProgram(const std::string &program)
    {
        if (program.empty()) return std::string();
        if (program.find('/') != std::string::npos) return access(program.c_str(), X_OK) == 0 ? program : std::string();
        const char *path = std::getenv("PATH");
        std::string p = path ? path : "/usr/local/bin:/usr/bin:/bin";
        for (size_t at = 0; at <= p.size();)
        {
            const size_t colon = p.find(':', at);
            const std::string dir = p.substr(at, colon == std::string::npos ? std::string::npos : colon - at);
            if (!dir.empty())
            {
                const std::string cand = dir + "/" + program;
                struct stat st{};
                if (stat(cand.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(cand.c_str(), X_OK) == 0) return cand;
            }
            if (colon == std::string::npos) break;
            at = colon + 1;
        }
        return std::string();
    }

    std::string ProcessAssistant::findCosmoCc()
    {
        char self[4096];
        const ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
        if (n > 0)
        {
            self[n] = 0;
            const std::string dir = dirOf(self);
            for (const std::string &cand : {dir + "/cosmo-cc", dir + "/cli/cosmo-cc"})
                if (access(cand.c_str(), X_OK) == 0) return cand;
        }
        return findProgram("cosmo-cc");
    }

    std::vector<std::string> ProcessAssistant::defaultSkills()
    {
        std::string root;
        if (const char *e = std::getenv("COSMO_SKILLS_DIR")) root = e;
#ifdef COSMO_SKILLS_DIR
        if (root.empty()) root = COSMO_SKILLS_DIR;
#endif
        // R-ASSIST-4 (amended 2026-11-05): the colour-grading ENTRY skill and every skill it loads
        // (`arstro.color.grading.<part>`, e.g. `.style` - what a user means by a look in their own words).
        // The entry comes first: `cosmo_grade` runs its editor.
        std::vector<std::string> out;
        const std::string grading = root + "/arstro.color.grading";
        if (root.empty() || access((grading + "/SKILL.md").c_str(), R_OK) != 0) return out;
        out.push_back(grading);
        namespace fs = std::filesystem;
        std::error_code ec;
        std::vector<std::string> parts;
        for (const auto &e : fs::directory_iterator(root, ec))
        {
            const std::string name = e.path().filename().string();
            if (name.rfind("arstro.color.grading.", 0) == 0 && fs::exists(e.path() / "SKILL.md", ec))
                parts.push_back(e.path().string());
        }
        std::sort(parts.begin(), parts.end());
        out.insert(out.end(), parts.begin(), parts.end());
        return out;
    }

    ProcessAssistant::Config ProcessAssistant::configFor(const std::string &socket)
    {
        Config c;
        c.cosmoCc = findCosmoCc();
        c.controlSocket = socket;
        c.workspace = arstro::cosmo::ProjectStore::configDir() + "/agent";
        c.bundledSkills = defaultSkills();
        return c;
    }

    std::string ProcessAssistant::agentsMd(const AssistLaunch &l, const std::vector<std::string> &skills)
    {
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
        if (!skills.empty())
        {
            s += "\n## Skills\n\nRead a skill's SKILL.md before doing what it covers, and follow it. Its scripts run "
                 "from this directory and reach this window through the COSMO_CONTROL environment variable.\n";
            for (const auto &k : skills)
                s += "- " + k + ": skills/" + k + "/SKILL.md" +
                     (k == "arstro.color.grading"
                          ? " - THE ENTRY POINT for colour: grading in the photographer's own taste, cinematic, a mood or "
                            "look, correcting, matching a set, many photos at once; it says which skill below to load"
                      : k == "arstro.color.grading.style"
                          ? " - what the user MEANS by a look in their own words, any language (warm, moody, tong Han, "
                            "vintage film but brighter, the night ones neon) and the edit that answers it"
                          : std::string()) + "\n";
        }
        s += "\n## How to answer\n\nThe user reads your replies in a small panel beside the photo: answer briefly and "
             "plainly. Look at a photo before and after you change it.\n";
        return s;
    }

    std::vector<cosmo::AgentChoice> ProcessAssistant::detect(const std::string &current)
    {
        struct Preset { const char *id, *name, *agent, *program, *hint; };
        // The agents cosmo knows how to start (R-ASSIST-9). The ACP command lines are the agents' own
        // documented ACP modes; a user whose version differs edits the setting.
        static const Preset presets[] = {
            {"claude", "Claude Code", "claude", "claude", "install: claude.com/claude-code"},
            {"gemini", "Gemini CLI", "acp:gemini --experimental-acp", "gemini", "install: npm i -g @google/gemini-cli"},
            {"opencode", "OpenCode", "acp:opencode acp", "opencode", "install: opencode.ai"},
            {"goose", "Goose", "acp:goose acp", "goose", "install: block.github.io/goose"},
            {"qwen", "Qwen Code", "acp:qwen --experimental-acp", "qwen", "install: npm i -g @qwen-code/qwen-code"},
            {"codex", "Codex", "acp:codex-acp", "codex-acp", "install: npm i -g @zed-industries/codex-acp"},
        };
        std::vector<cosmo::AgentChoice> out;
        bool currentListed = current.empty();
        for (const Preset &p : presets)
        {
            cosmo::AgentChoice c;
            c.id = p.id;
            c.name = p.name;
            c.agent = p.agent;
            c.installed = !findProgram(p.program).empty();
            c.note = c.installed ? std::string("installed") : std::string("not found - ") + p.hint;
            if (current == c.agent) currentListed = true;
            out.push_back(c);
        }
        // Local model servers: a refused port answers at once; a live one in a few ms.
        struct Server { const char *id, *name, *base, *list; bool ollama; };
        static const Server servers[] = {
            {"ollama", "Ollama", "http://127.0.0.1:11434", "/api/tags", true},
            {"lmstudio", "LM Studio", "http://127.0.0.1:1234", "/v1/models", false},
        };
        for (const Server &sv : servers)
        {
            const HttpResult r = httpRequest("GET", std::string(sv.base) + sv.list, std::string(), 250);
            std::string perr;
            const Json j = r.status == 200 ? Json::parse(r.body, perr) : Json();
            const Json &items = sv.ollama ? j["models"] : j["data"];
            size_t shown = 0;
            for (size_t i = 0; i < items.size() && shown < 6; ++i)
            {
                const std::string model = items.at(i)[sv.ollama ? "name" : "id"].asString();
                if (model.empty()) continue;
                cosmo::AgentChoice c;
                c.id = std::string(sv.id) + ":" + model;
                c.name = std::string(sv.name) + " - " + model;
                c.agent = "local:" + std::string(sv.base) + "/v1|" + model;
                c.installed = true;
                c.note = "local model, cosmo's own agent loop";
                if (current == c.agent) currentListed = true;
                out.push_back(c);
                ++shown;
            }
            if (shown == 0 && sv.ollama)
            {
                cosmo::AgentChoice c;
                c.id = sv.id;
                c.name = "Ollama (local model)";
                c.agent = "local:http://127.0.0.1:11434/v1|qwen2.5:7b";
                c.installed = false;
                c.note = "not running - install: ollama.com, then: ollama pull qwen2.5:7b";
                if (current == c.agent) currentListed = true;
                out.push_back(c);
            }
        }
        cosmo::AgentChoice custom;
        custom.id = "custom";
        custom.name = "Custom agent";
        custom.agent = currentListed ? std::string("acp:") : current;
        custom.installed = true;
        custom.note = "acp:<command> any ACP agent - local:<url>|<model> - cmd:<command>";
        out.push_back(custom);
        return out;
    }

    std::string ProcessAssistant::systemPrompt(const AssistLaunch &l)
    {
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
    }

    std::vector<std::string> ProcessAssistant::claudeArgv(const AssistLaunch &l, const Config &c)
    {
        std::vector<std::string> a = words(l.agent.empty() ? std::string("claude") : l.agent);
        for (const char *x : {"-p", "--input-format", "stream-json", "--output-format", "stream-json", "--verbose"})
            a.push_back(x);
        a.push_back("--mcp-config");
        a.push_back(c.workspace + "/cosmo-mcp.json");
        a.push_back("--append-system-prompt");
        a.push_back(systemPrompt(l));
        a.push_back("--allowedTools");
        a.push_back(kAllowedTools);
        for (const auto &w : words(l.args)) a.push_back(w);
        return a;
    }

    std::string ProcessAssistant::fillPrompt(const std::string &command, const std::string &prompt, bool &used)
    {
        std::string q = "'";
        for (char ch : prompt)
        {
            if (ch == '\'') q += "'\\''";
            else q += ch;
        }
        q += "'";
        std::string out = command;
        used = false;
        for (size_t at = out.find("{prompt}"); at != std::string::npos; at = out.find("{prompt}", at + q.size()))
        {
            out.replace(at, 8, q);
            used = true;
        }
        return out;
    }

    void ProcessAssistant::parseClaudeLine(const std::string &line, std::vector<AssistOutput> &out)
    {
        if (line.empty() || line[0] != '{') return;
        std::string err;
        const Json o = Json::parse(line, err);
        if (!err.empty()) return;
        const std::string type = o["type"].asString();
        if (type == "system" && o["subtype"].asString() == "init")
        {
            std::string what = "Claude Code";
            if (o.has("claude_code_version")) what += " " + o["claude_code_version"].asString();
            if (o.has("model")) what += " - " + o["model"].asString();
            out.push_back({AssistOutput::Kind::Ready, what});
        }
        else if (type == "assistant")
        {
            const Json &content = o["message"]["content"];
            for (size_t i = 0; i < content.size(); ++i)
            {
                const Json &c = content.at(i);
                const std::string kind = c["type"].asString();
                if (kind == "text")
                {
                    const std::string t = trim(c["text"].asString());
                    if (!t.empty()) out.push_back({AssistOutput::Kind::Text, t});
                }
                else if (kind == "tool_use")
                    out.push_back({AssistOutput::Kind::Tool, toolBrief(c["name"].asString(), c["input"])});
            }
        }
        else if (type == "result")
        {
            if (o["is_error"].asBool())
                out.push_back({AssistOutput::Kind::Failed, trim(o["result"].asString().empty() ? o["subtype"].asString()
                                                                                              : o["result"].asString())});
            char buf[96];
            const double s = o["duration_ms"].asNumber() / 1000.0;
            if (o.has("total_cost_usd"))
                std::snprintf(buf, sizeof(buf), "done in %.0f s - $%.3f", s, o["total_cost_usd"].asNumber());
            else
                std::snprintf(buf, sizeof(buf), "done in %.0f s", s);
            out.push_back({AssistOutput::Kind::Done, buf});
        }
    }

    bool ProcessAssistant::prepareWorkspace(std::string &err)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path skills = fs::path(mCfg.workspace) / ".claude" / "skills";
        fs::create_directories(skills, ec);
        if (ec) { err = "cannot create the agent's workspace " + mCfg.workspace + ": " + ec.message(); return false; }
        // R-ASSIST-4: the bundled skill and the user's extra ones, linked by name — a link, so an
        // updated skill is used the next time without copying anything.
        std::vector<std::string> dirs = mCfg.bundledSkills;
        dirs.insert(dirs.end(), mLaunch.skillDirs.begin(), mLaunch.skillDirs.end());
        // R-ASSIST-10: linked twice - `.claude/skills/<name>` where Claude Code looks, `skills/<name>`
        // where AGENTS.md points every other agent.
        const fs::path generic = fs::path(mCfg.workspace) / "skills";
        fs::create_directories(generic, ec);
        mSkillNames.clear();
        for (std::string d : dirs)
        {
            while (d.size() > 1 && d.back() == '/') d.pop_back();
            if (!fs::exists(fs::path(d) / "SKILL.md", ec)) continue;
            const std::string name = fs::path(d).filename().string();
            mSkillNames.push_back(name);
            for (const fs::path &link : {skills / name, generic / name})
            {
                fs::remove(link, ec);
                fs::create_directory_symlink(fs::absolute(d, ec), link, ec);
            }
        }
        // The instructions file the agents read: AGENTS.md (Codex, OpenCode, Goose, ...), and the
        // same text under the names Gemini CLI and Qwen Code look for.
        const std::string md = agentsMd(mLaunch, mSkillNames);
        for (const char *f : {"AGENTS.md", "GEMINI.md", "QWEN.md"})
        {
            std::ofstream o(mCfg.workspace + "/" + f, std::ios::trunc);
            o << md;
        }
        // R-ASSIST-3: cosmo's MCP tools, attached to THIS application.
        Json server = Json::object();
        server.set("command", mCfg.cosmoCc);
        Json args = Json::array();
        args.push("mcp");
        args.push("--attach");
        args.push(mCfg.controlSocket);
        server.set("args", std::move(args));
        Json servers = Json::object();
        servers.set("cosmo", std::move(server));
        Json cfg = Json::object();
        cfg.set("mcpServers", std::move(servers));
        std::ofstream f(mCfg.workspace + "/cosmo-mcp.json", std::ios::trunc);
        f << cfg.dumpPretty() << "\n";
        if (!f) { err = "cannot write " + mCfg.workspace + "/cosmo-mcp.json"; return false; }
        return true;
    }

    bool ProcessAssistant::start(const AssistLaunch &launch, std::string &err)
    {
        mLaunch = launch;
        if (mLaunch.agent.empty()) mLaunch.agent = "claude";
        // R-ASSIST-9: the kind is the setting's prefix; a bare command is Claude Code or a one-shot.
        std::vector<std::string> acpArgv;
        mClaude = mAcp = false;
        if (mLaunch.agent.rfind("acp:", 0) == 0)
        {
            mAcp = true;
            acpArgv = words(mLaunch.agent.substr(4));
            for (const auto &a : words(mLaunch.args)) acpArgv.push_back(a);
        }
        else if (mLaunch.agent.rfind("local:", 0) == 0)
        {
            const std::string spec = mLaunch.agent.substr(6);
            const size_t bar = spec.find('|');
            if (bar == std::string::npos || bar + 1 >= spec.size())
            {
                err = "a local model is local:<url>|<model>, e.g. local:http://127.0.0.1:11434/v1|qwen2.5:7b";
                return false;
            }
            mAcp = true;
            acpArgv = {mCfg.cosmoCc, "agent", "--endpoint", spec.substr(0, bar), "--model", spec.substr(bar + 1)};
        }
        else if (mLaunch.agent.rfind("cmd:", 0) == 0)
            mLaunch.agent = mLaunch.agent.substr(4);
        else
            mClaude = isClaude(mLaunch.agent);
        mPersistent = mClaude || mAcp;

        const std::vector<std::string> w = mAcp ? acpArgv : words(mLaunch.agent);
        if (w.empty() || findProgram(w[0]).empty())
        {
            std::string hint;
            for (const auto &c : detect(std::string()))
                if (!c.installed && c.agent == launch.agent) hint = " - " + c.note.substr(c.note.find(" - ") == std::string::npos ? 0 : c.note.find(" - ") + 3);
            err = mClaude ? "Claude Code is not installed (`claude` was not found on PATH) - install it, or choose "
                            "another agent"
                          : "the agent `" + (w.empty() ? std::string() : w[0]) + "` was not found on PATH" + hint;
            return false;
        }
        if (mCfg.cosmoCc.empty()) { err = "cosmo-cc was not found next to cosmo or on PATH"; return false; }
        if (mCfg.controlSocket.empty()) { err = "the control socket is not open, so the agent could not reach cosmo"; return false; }
        if (!prepareWorkspace(err)) return false;
        mStarted = true;
        mStopping = false;
        mAnnounced = false;
        if (mAcp)
        {
            if (!spawn(acpArgv, err)) return false;
            mInitId = acpCall("initialize",
                              "{\"protocolVersion\":1,\"clientCapabilities\":{\"fs\":{\"readTextFile\":false,"
                              "\"writeTextFile\":false},\"terminal\":false},\"clientInfo\":{\"name\":\"cosmo\","
                              "\"title\":\"Cosmo\",\"version\":\"2\"}}");
            readPipes();
            return true;
        }
        // Claude Code runs for the whole conversation; a one-shot agent is started per message.
        return mClaude ? spawn(claudeArgv(mLaunch, mCfg), err) : true;
    }

    // ── ACP client (R-ASSIST-9) ────────────────────────────────────────────────────────

    void ProcessAssistant::acpSend(const std::string &json) { mPendingStdin += json + "\n"; }

    int ProcessAssistant::acpCall(const std::string &method, const std::string &paramsJson)
    {
        const int id = ++mRpcId;
        acpSend("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"method\":\"" + method + "\",\"params\":" +
                paramsJson + "}");
        return id;
    }

    void ProcessAssistant::acpPrompt(const std::string &text)
    {
        // ACP has no system prompt: the first message carries what AGENTS.md says, so an agent that
        // does not read that file still knows where it is and where the skill is (R-ASSIST-10).
        std::string t = text;
        if (!mContextSent)
        {
            t = "[Context from Cosmo - the app you are running in]\n" + agentsMd(mLaunch, mSkillNames) +
                "\n[The photographer says]\n" + text;
            mContextSent = true;
        }
        Json block = Json::object();
        block.set("type", "text");
        block.set("text", t);
        Json prompt = Json::array();
        prompt.push(std::move(block));
        Json p = Json::object();
        p.set("sessionId", mSession);
        p.set("prompt", std::move(prompt));
        mPromptId = acpCall("session/prompt", p.dump());
        mPromptStartMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    bool ProcessAssistant::permitted(const std::string &kind, const std::string &title, const std::string &command,
                                     const std::vector<std::string> &paths, const AssistLaunch &l, const Config &c)
    {
        if (l.allowAll) return true;
        std::string t = title;
        for (char &ch : t) ch = (char)std::tolower((unsigned char)ch);
        if (t.find("cosmo") != std::string::npos) return true;                     // cosmo's own tools
        if (kind == "read" || kind == "search" || kind == "think" || kind == "fetch") return true;
        if (kind == "execute")
        {
            std::string cmd = trim(command.empty() ? title : command);
            for (const char *ok : {"python3 ", "python ", "ls", "mkdir ", "cat ", "head ", "tail "})
                if (cmd.rfind(ok, 0) == 0) return true;                            // the skill's scripts, looking around
            return false;
        }
        if (kind == "edit")
        {
            const std::string projectDir = l.project.empty() ? std::string() : dirOf(l.project);
            if (paths.empty()) return false;
            for (const auto &p : paths)
                if (p.rfind(c.workspace, 0) != 0 && (projectDir.empty() || p.rfind(projectDir, 0) != 0)) return false;
            return true;                                                           // plan files beside the project
        }
        return false;
    }

    void ProcessAssistant::handleAcpLine(const std::string &line, std::vector<AssistOutput> &out)
    {
        if (line.empty() || line[0] != '{') return;   // an agent's stray log line is not the protocol
        std::string perr;
        const Json o = Json::parse(line, perr);
        if (!perr.empty()) return;
        if (o.has("method"))
        {
            const std::string m = o["method"].asString();
            const Json &params = o["params"];
            if (!o.has("id"))
            {
                if (m != "session/update") return;
                const Json &u = params["update"];
                const std::string k = u["sessionUpdate"].asString();
                if (k == "agent_message_chunk" && u["content"]["type"].asString() == "text")
                    out.push_back({AssistOutput::Kind::TextDelta, u["content"]["text"].asString()});
                else if (k == "tool_call")
                {
                    std::string title = trim(u["title"].asString());
                    if (title.empty()) title = u["kind"].asString();
                    mToolTitles.push_back({u["toolCallId"].asString(), title});
                    out.push_back({AssistOutput::Kind::Tool, title.size() > 140 ? title.substr(0, 137) + "..." : title});
                }
                else if (k == "tool_call_update" && u["status"].asString() == "failed")
                {
                    std::string title = u["title"].asString();
                    for (const auto &tt : mToolTitles)
                        if (tt.first == u["toolCallId"].asString() && title.empty()) title = tt.second;
                    out.push_back({AssistOutput::Kind::Failed, (title.empty() ? std::string("a tool") : title) + " failed"});
                }
                return;
            }
            // A request from the agent: it waits for our answer, so every one gets one.
            const std::string id = o["id"].dump();
            if (m == "session/request_permission")
            {
                const Json &tc = params["toolCall"];
                std::string title = tc["title"].asString(), kind = tc["kind"].asString();
                for (const auto &tt : mToolTitles)
                    if (tt.first == tc["toolCallId"].asString() && title.empty()) title = tt.second;
                std::string command;
                if (tc["rawInput"].has("command"))
                    command = tc["rawInput"]["command"].isString() ? tc["rawInput"]["command"].asString() : tc["rawInput"]["command"].dump();
                std::vector<std::string> paths;
                const Json &loc = tc["locations"];
                for (size_t i = 0; i < loc.size(); ++i) paths.push_back(loc.at(i)["path"].asString());
                const bool ok = permitted(kind, title, command, paths, mLaunch, mCfg);
                std::string chosen;
                const Json &opts = params["options"];
                for (const char *want : ok ? std::initializer_list<const char *>{"allow_once", "allow_always"}
                                           : std::initializer_list<const char *>{"reject_once", "reject_always"})
                    for (size_t i = 0; i < opts.size() && chosen.empty(); ++i)
                        if (opts.at(i)["kind"].asString() == want) chosen = opts.at(i)["optionId"].asString();
                Json outcome = Json::object();
                if (chosen.empty()) outcome.set("outcome", "cancelled");
                else { outcome.set("outcome", "selected"); outcome.set("optionId", chosen); }
                Json result = Json::object();
                result.set("outcome", std::move(outcome));
                acpSend("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + result.dump() + "}");
                if (!ok)
                    out.push_back({AssistOutput::Kind::Failed,
                                   "refused: " + (title.empty() ? kind : title) +
                                       (command.empty() ? std::string() : " (" + command + ")") +
                                       " - to allow everything: settings set agentAllowAll=1"});
                return;
            }
            acpSend("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"error\":{\"code\":-32601,\"message\":\"cosmo does not provide " +
                    m + "\"}}");
            return;
        }
        // A response to one of ours.
        const int id = (int)o["id"].asInt(-1);
        if (o.has("error"))
        {
            const std::string msg = o["error"]["message"].asString();
            const long long code = o["error"]["code"].asInt();
            if (id == mInitId || id == mNewId)
            {
                const std::string why = code == -32000
                    ? name() + " needs you to sign in first: run it once in a terminal, then Connect again"
                    : name() + " refused to start a session: " + msg;
                out.push_back({AssistOutput::Kind::Exited, why});
                stop();
                return;
            }
            if (id == mPromptId)
            {
                mPromptId = -1;
                out.push_back({AssistOutput::Kind::Failed, msg});
                out.push_back({AssistOutput::Kind::Done, std::string()});
            }
            return;
        }
        const Json &r = o["result"];
        if (id == mInitId)
        {
            const Json &info = r["agentInfo"];
            const std::string t = info["title"].asString().empty() ? info["name"].asString() : info["title"].asString();
            if (!t.empty()) mDisplayName = t;
            // cosmo's own MCP server, attached to THIS window, handed over in the session - no config file.
            Json server = Json::object();
            server.set("name", "cosmo");
            server.set("command", mCfg.cosmoCc);
            Json args = Json::array();
            args.push("mcp");
            args.push("--attach");
            args.push(mCfg.controlSocket);
            server.set("args", std::move(args));
            server.set("env", Json::array());
            Json servers = Json::array();
            servers.push(std::move(server));
            Json p = Json::object();
            p.set("cwd", mCfg.workspace);
            p.set("mcpServers", std::move(servers));
            mNewId = acpCall("session/new", p.dump());
        }
        else if (id == mNewId)
        {
            mSession = r["sessionId"].asString();
            out.push_back({AssistOutput::Kind::Ready, name() + " - connected over ACP"});
            if (!mAcpQueue.empty()) { acpPrompt(mAcpQueue.front()); mAcpQueue.erase(mAcpQueue.begin()); }
        }
        else if (id == mPromptId)
        {
            mPromptId = -1;
            const std::string stop = r["stopReason"].asString();
            const double now = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if (stop == "refusal" || stop == "max_tokens" || stop == "max_turn_requests")
                out.push_back({AssistOutput::Kind::Failed, name() + " stopped: " + stop});
            char buf[64];
            std::snprintf(buf, sizeof(buf), "done in %.0f s", (now - mPromptStartMs) / 1000.0);
            out.push_back({AssistOutput::Kind::Done, buf});
            if (!mAcpQueue.empty()) { acpPrompt(mAcpQueue.front()); mAcpQueue.erase(mAcpQueue.begin()); }
        }
    }

    bool ProcessAssistant::spawn(const std::vector<std::string> &argv, std::string &err)
    {
        const std::string prog = findProgram(argv[0]);
        if (prog.empty()) { err = argv[0] + " was not found on PATH"; return false; }
        int in[2], out[2], er[2];
        if (pipe(in) != 0 || pipe(out) != 0 || pipe(er) != 0) { err = std::string("pipe: ") + std::strerror(errno); return false; }

        // Everything the child needs is built BEFORE fork: between fork and exec a child of a
        // threaded process may only make async-signal-safe calls, so no allocation there.
        std::vector<std::string> env;
        for (char **e = environ; *e; ++e)
        {
            const std::string kv = *e;
            if (kv.rfind("COSMO_CONTROL=", 0) == 0 || kv.rfind("COSMO_CC=", 0) == 0 || kv.rfind("COSMO_PROJECT=", 0) == 0)
                continue;
            env.push_back(kv);
        }
        env.push_back("COSMO_CONTROL=" + mCfg.controlSocket);
        env.push_back("COSMO_CC=" + mCfg.cosmoCc);
        env.push_back("COSMO_PROJECT=" + mLaunch.project);
        std::vector<char *> cargv, cenv;
        for (const auto &a : argv) cargv.push_back(const_cast<char *>(a.c_str()));
        cargv.push_back(nullptr);
        for (const auto &e : env) cenv.push_back(const_cast<char *>(e.c_str()));
        cenv.push_back(nullptr);
        const std::string cwd = mCfg.workspace;

        const pid_t pid = fork();
        if (pid < 0)
        {
            err = std::string("fork: ") + std::strerror(errno);
            for (int fd : {in[0], in[1], out[0], out[1], er[0], er[1]}) close(fd);
            return false;
        }
        if (pid == 0)
        {
            setpgid(0, 0);   // its own group, so stop() ends whatever it started too (R-ASSIST-6)
            dup2(in[0], 0);
            dup2(out[1], 1);
            dup2(er[1], 2);
            for (int fd = 3; fd < 1024; ++fd) close(fd);   // not the window's sockets, not the GPU
            if (chdir(cwd.c_str()) != 0) _exit(126);
            execve(prog.c_str(), cargv.data(), cenv.data());
            _exit(127);
        }
        setpgid(pid, pid);
        close(in[0]);
        close(out[1]);
        close(er[1]);
        mPid = pid;
        mIn = in[1];
        mOut = out[0];
        mErr = er[0];
        setNonBlocking(mOut);
        setNonBlocking(mErr);
        setNonBlocking(mIn);
        signal(SIGPIPE, SIG_IGN);   // an agent that died must be a Failed line, not a dead window
        mOutBuf.clear();
        mErrBuf.clear();
        mOneShotText.clear();
        return true;
    }

    bool ProcessAssistant::send(const std::string &text, std::string &err)
    {
        if (!mStarted) { err = "the agent is not running"; return false; }
        if (mAcp)
        {
            if (mPid < 0) { err = "the agent has exited"; return false; }
            // One prompt per ACP turn: a message while it answers waits for the turn to end.
            if (mSession.empty() || mPromptId >= 0) mAcpQueue.push_back(text);
            else acpPrompt(text);
            readPipes();
            return true;
        }
        if (mClaude)
        {
            if (mPid < 0 || mIn < 0) { err = "the agent has exited"; return false; }
            Json content = Json::object();
            content.set("role", "user");
            content.set("content", text);
            Json msg = Json::object();
            msg.set("type", "user");
            msg.set("message", std::move(content));
            mPendingStdin += msg.dump() + "\n";
        }
        else
        {
            if (mPid >= 0) { err = "the agent is still answering the last message"; return false; }
            bool used = false;
            const std::string cmd = fillPrompt(mLaunch.agent + (mLaunch.args.empty() ? "" : " " + mLaunch.args), text, used);
            if (!spawn({"sh", "-c", cmd}, err)) return false;
            if (!used) mPendingStdin = text + "\n";
        }
        // Written in poll() as the pipe takes it - a long message must not block the window.
        readPipes();
        return true;
    }

    void ProcessAssistant::readPipes()
    {
        if (mIn >= 0 && !mPendingStdin.empty())
        {
            const ssize_t n = write(mIn, mPendingStdin.data(), mPendingStdin.size());
            if (n > 0) mPendingStdin.erase(0, (size_t)n);
            if (!mPersistent && mPendingStdin.empty()) { close(mIn); mIn = -1; }   // one message, then EOF
        }
        char buf[65536];
        for (int *fd : {&mOut, &mErr})
        {
            if (*fd < 0) continue;
            for (;;)
            {
                const ssize_t n = read(*fd, buf, sizeof(buf));
                if (n > 0) { (fd == &mOut ? mOutBuf : mErrBuf).append(buf, (size_t)n); continue; }
                if (n == 0) { close(*fd); *fd = -1; }
                break;   // EAGAIN: nothing more now
            }
        }
        if (mErrBuf.size() > 16384) mErrBuf = tail(mErrBuf, 8192);
    }

    void ProcessAssistant::reap(int status, std::vector<AssistOutput> &out)
    {
        const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        const std::string why = trim(tail(mErrBuf));
        mPid = -1;
        for (int *fd : {&mIn, &mOut, &mErr})
            if (*fd >= 0) { close(*fd); *fd = -1; }
        if (mPersistent)
        {
            mStarted = false;
            std::string text;
            if (!mStopping)
                text = "the agent exited" + (code > 0 ? " with code " + std::to_string(code) : std::string()) +
                       (why.empty() ? std::string() : ": " + why);
            out.push_back({AssistOutput::Kind::Exited, text});
            return;
        }
        const std::string answer = trim(mOneShotText + mOutBuf);
        mOneShotText.clear();
        mOutBuf.clear();
        mErrBuf.clear();
        if (!answer.empty()) out.push_back({AssistOutput::Kind::Text, answer});
        if (code != 0)
            out.push_back({AssistOutput::Kind::Failed,
                           "the agent exited with code " + std::to_string(code) + (why.empty() ? std::string() : ": " + why)});
        out.push_back({AssistOutput::Kind::Done, std::string()});
    }

    void ProcessAssistant::poll(std::vector<AssistOutput> &out)
    {
        if (!mStarted) return;
        // Ready as soon as it can take a message (R-ASSIST-11): Claude Code says nothing until its
        // first message, and waiting for its hello left the tab at "starting" forever. An ACP agent
        // is ready when its session exists (handleAcpLine).
        if (!mAcp && !mAnnounced)
        {
            mAnnounced = true;
            out.push_back({AssistOutput::Kind::Ready, mClaude ? std::string() : name() + " - one message at a time"});
        }
        if (mPid < 0) return;
        readPipes();
        if (mPersistent)
        {
            for (size_t nl; mPid > 0 && (nl = mOutBuf.find('\n')) != std::string::npos;)
            {
                const std::string line = mOutBuf.substr(0, nl);
                mOutBuf.erase(0, nl + 1);
                if (mAcp) handleAcpLine(line, out);
                else parseClaudeLine(line, out);
            }
            if (mPid < 0) return;   // a refused session stopped it
            readPipes();            // answers handleAcpLine queued (permission, session/new)
        }
        int status = 0;
        if (waitpid(mPid, &status, WNOHANG) == mPid)
        {
            readPipes();   // what it wrote just before it went
            if (mPersistent)
                for (size_t nl; (nl = mOutBuf.find('\n')) != std::string::npos;)
                {
                    const std::string line = mOutBuf.substr(0, nl);
                    mOutBuf.erase(0, nl + 1);
                    if (mAcp) handleAcpLine(line, out);
                    else parseClaudeLine(line, out);
                }
            reap(status, out);
        }
    }

    void ProcessAssistant::stop()
    {
        if (mPid > 0)
        {
            mStopping = true;
            if (mIn >= 0) { close(mIn); mIn = -1; }
            kill(-mPid, SIGTERM);
            int status = 0;
            // A short grace, then the group is killed: stopping must not hang the window on an
            // agent that ignores SIGTERM (R-ASSIST-6).
            for (int i = 0; i < 50; ++i)
            {
                if (waitpid(mPid, &status, WNOHANG) == mPid) { mPid = -1; break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (mPid > 0)
            {
                kill(-mPid, SIGKILL);
                waitpid(mPid, &status, 0);
                mPid = -1;
            }
        }
        for (int *fd : {&mIn, &mOut, &mErr})
            if (*fd >= 0) { close(*fd); *fd = -1; }
        mStarted = false;
    }
}
}
