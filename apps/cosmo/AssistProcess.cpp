#include "AssistProcess.h"
#include "core/ProjectStore.h"
#include "ntwb/Json.h"

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
        std::vector<std::string> out;
        const std::string grading = root + "/arstro.color.grading";
        if (!root.empty() && access((grading + "/SKILL.md").c_str(), R_OK) == 0) out.push_back(grading);
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
        for (std::string d : dirs)
        {
            while (d.size() > 1 && d.back() == '/') d.pop_back();
            if (!fs::exists(fs::path(d) / "SKILL.md", ec)) continue;
            const fs::path link = skills / fs::path(d).filename();
            fs::remove(link, ec);
            fs::create_directory_symlink(fs::absolute(d, ec), link, ec);
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
        mClaude = isClaude(mLaunch.agent);
        const std::vector<std::string> w = words(mLaunch.agent);
        if (w.empty() || findProgram(w[0]).empty())
        {
            err = mClaude ? "Claude Code is not installed (`claude` was not found on PATH) - install it, or choose "
                            "another agent in Settings"
                          : "the agent `" + (w.empty() ? std::string() : w[0]) + "` was not found on PATH";
            return false;
        }
        if (mCfg.cosmoCc.empty()) { err = "cosmo-cc was not found next to cosmo or on PATH"; return false; }
        if (mCfg.controlSocket.empty()) { err = "the control socket is not open, so the agent could not reach cosmo"; return false; }
        if (!prepareWorkspace(err)) return false;
        mStarted = true;
        mStopping = false;
        // Claude Code runs for the whole conversation; any other agent is started per message.
        return mClaude ? spawn(claudeArgv(mLaunch, mCfg), err) : true;
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
            if (!mClaude && mPendingStdin.empty()) { close(mIn); mIn = -1; }   // one message, then EOF
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
        if (mClaude)
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
        if (!mClaude && !mAnnounced)
        {
            mAnnounced = true;
            out.push_back({AssistOutput::Kind::Ready, name() + " - one message at a time"});
        }
        if (mPid < 0) return;
        readPipes();
        if (mClaude)
        {
            for (size_t nl; (nl = mOutBuf.find('\n')) != std::string::npos;)
            {
                const std::string line = mOutBuf.substr(0, nl);
                mOutBuf.erase(0, nl + 1);
                parseClaudeLine(line, out);
            }
        }
        int status = 0;
        if (waitpid(mPid, &status, WNOHANG) == mPid)
        {
            readPipes();   // what it wrote just before it went
            if (mClaude)
                for (size_t nl; (nl = mOutBuf.find('\n')) != std::string::npos;)
                {
                    const std::string line = mOutBuf.substr(0, nl);
                    mOutBuf.erase(0, nl + 1);
                    parseClaudeLine(line, out);
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
