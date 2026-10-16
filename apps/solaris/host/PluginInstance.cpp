#include "PluginInstance.h"
#include "../plughost/PlugShared.h"
#include "Plugins.h"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace arstro
{
namespace solaris_host
{
    namespace
    {
        using plughost::Shared;

        class OutOfProcess : public solaris::IPluginInstance
        {
        public:
            ~OutOfProcess() override
            {
                if (mCtl >= 0 && !offline()) { const char q[] = "quit\n"; (void)!send(mCtl, q, sizeof q - 1, MSG_NOSIGNAL); } // a dead one is no signal of ours
                if (offline()) reap(); // already killed: only its status to collect
                if (mPid > 0)
                {
                    for (int i = 0; i < 100; ++i) // 1 s to leave, then it is made to
                    {
                        if (waitpid(mPid, nullptr, WNOHANG) == mPid) { mPid = 0; break; }
                        usleep(10000);
                    }
                    if (mPid > 0) { kill(mPid, SIGKILL); waitpid(mPid, nullptr, 0); }
                }
                if (mShared) munmap(mShared, sizeof(Shared));
                for (int fd : {mCtl, mGo, mDone}) if (fd >= 0) close(fd);
            }

            bool start(const std::string &bundle, const std::string &uid, double rate, int maxBlock, std::string &err)
            {
                const std::string helper = plughostPath();
                if (helper.empty() || access(helper.c_str(), X_OK) != 0) { err = "this build has no plugin host (the VST3 SDK was not found)"; return false; }
                int sv[2];
                if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0) { err = "cannot make a socket pair"; return false; }
                const int mem = memfd_create("solaris-plug", MFD_CLOEXEC);
                const int go = eventfd(0, EFD_CLOEXEC), done = eventfd(0, EFD_CLOEXEC);
                if (mem < 0 || go < 0 || done < 0 || ftruncate(mem, sizeof(Shared)) != 0) { err = "cannot share memory with it"; return false; }
                mShared = (Shared *)mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, mem, 0);
                if (mShared == MAP_FAILED) { mShared = nullptr; err = "cannot map the shared block"; return false; }
                new (mShared) Shared();
                posix_spawn_file_actions_t fa;
                posix_spawn_file_actions_init(&fa);
                posix_spawn_file_actions_adddup2(&fa, sv[1], 3); // dup2 clears close-on-exec on the copy
                posix_spawn_file_actions_adddup2(&fa, mem, 4);
                posix_spawn_file_actions_adddup2(&fa, go, 5);
                posix_spawn_file_actions_adddup2(&fa, done, 6);
                posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0); // its chatter is not ours
                std::vector<std::string> args{helper, "--serve", bundle, uid, std::to_string((long long)rate), std::to_string(maxBlock)};
                std::vector<char *> argv;
                for (auto &a : args) argv.push_back(a.data());
                argv.push_back(nullptr);
                const int rc = posix_spawn(&mPid, helper.c_str(), &fa, nullptr, argv.data(), environ);
                posix_spawn_file_actions_destroy(&fa);
                close(sv[1]);
                close(mem);
                mCtl = sv[0];
                mGo = go;
                mDone = done;
                if (rc != 0) { mPid = 0; err = "cannot start " + helper + ": " + std::strerror(rc); return false; }
                // its hello: ready, its parameters, end — or why not
                std::string line;
                if (!readLine(line, 15.0)) { err = "it did not start in 15 s"; return false; }
                std::vector<std::string> f = split(line);
                if (f[0] == "error") { err = f.size() > 1 ? f[1] : "it would not start"; return false; }
                if (f[0] != "ready" || f.size() < 4) { err = "it said `" + line + "`"; return false; }
                mInstrument = f[1] == "instrument";
                mLatency = std::atoi(f[2].c_str());
                for (;;)
                {
                    if (!readLine(line, 15.0)) { err = "it stopped while naming its parameters"; return false; }
                    f = split(line);
                    if (f[0] == "end") break;
                    if (f[0] == "param" && f.size() >= 6)
                        mParams.push_back(solaris::PluginParamInfo{(uint32_t)std::strtoul(f[1].c_str(), nullptr, 10), f[4], f[5], std::atof(f[3].c_str()), std::atoi(f[2].c_str())});
                }
                return true;
            }

            bool isInstrument() const override { return mInstrument; }
            int latency() const override { return mLatency; }
            const std::vector<solaris::PluginParamInfo> &params() const override { return mParams; }
            bool offline() const override { return mReason.load() != 0; }
            std::string why() const override
            {
                switch (mReason.load())
                {
                case 1:
                    reap();
                    return "its process ended" + mEnded;
                case 2: return "it overran its budget";
                case 3: return "it refused a block";
                default: return std::string();
                }
            }

            bool process(const float *const *in, float *const *out, int frames, bool reset, const solaris::PluginNote *notes, int nNotes,
                         const solaris::PluginChange *changes, int nChanges, double budgetSeconds) override
            {
                if (mReason.load() != 0) return false;
                Shared &sh = *mShared;
                const int n = std::max(0, std::min(frames, plughost::kMaxBlock));
                sh.frames = n;
                sh.reset = reset ? 1 : 0;
                sh.nEvents = std::min(nNotes, plughost::kMaxEvents);
                for (int k = 0; k < sh.nEvents; ++k) sh.events[k] = plughost::NoteEvent{notes[k].offset, notes[k].on ? 1 : 0, notes[k].pitch, notes[k].velocity};
                sh.nParams = std::min(nChanges, plughost::kMaxParams);
                for (int k = 0; k < sh.nParams; ++k) sh.params[k] = plughost::ParamChange{changes[k].id, changes[k].offset, changes[k].value};
                for (int c = 0; c < 2; ++c)
                    if (in && in[c]) std::memcpy(sh.in[c], in[c], sizeof(float) * (size_t)n);
                    else std::memset(sh.in[c], 0, sizeof(float) * (size_t)n);
                const uint64_t one = 1;
                if (write(mGo, &one, sizeof one) != sizeof one) { gone(); return false; }
                // the answer, within the budget; the helper's death ends the wait at once
                const long long ns = (long long)(std::max(1e-4, budgetSeconds) * 1e9);
                timespec ts{(time_t)(ns / 1000000000LL), (long)(ns % 1000000000LL)};
                pollfd p[2] = {{mDone, POLLIN, 0}, {mCtl, POLLIN, 0}};
                for (;;)
                {
                    const int r = ppoll(p, 2, &ts, nullptr);
                    if (r < 0 && errno == EINTR) continue;
                    if (r == 0) { offlineFor(2); return false; } // overran
                    if (p[0].revents & POLLIN) break;
                    if (p[1].revents & (POLLHUP | POLLERR)) { gone(); return false; }
                    if (p[1].revents & POLLIN) { gone(); return false; } // it spoke out of turn: treat as gone
                }
                uint64_t v = 0;
                if (read(mDone, &v, sizeof v) != sizeof v) { gone(); return false; }
                if (sh.status != 0) { offlineFor(3); return false; }
                for (int c = 0; c < 2; ++c) std::memcpy(out[c], sh.out[c], sizeof(float) * (size_t)n);
                return true;
            }

            bool getState(std::string &base64, std::string &err) override
            {
                if (offline()) { err = "it is offline: " + why(); return false; }
                std::string line;
                if (!ask("state", line, err)) return false;
                const auto f = split(line);
                if (f[0] != "state" || f.size() < 2) { err = f.size() > 1 ? f[1] : "it gave no state"; return false; }
                base64 = f[1];
                return true;
            }
            bool setState(const std::string &base64, std::string &err) override
            {
                if (offline()) { err = "it is offline: " + why(); return false; }
                std::string line;
                if (!ask("setstate\t" + base64, line, err)) return false;
                const auto f = split(line);
                if (f[0] != "ok") { err = f.size() > 1 ? f[1] : "it refused the state"; return false; }
                return true;
            }

        private:
            /** Offline for good: the helper is not used again, so it is ended now (a frozen or spinning one must not stay
             *  behind) — on the audio thread too, so no allocation: why() finds out how it ended. */
            void offlineFor(int reason)
            {
                if (mPid > 0) kill(mPid, SIGKILL); // one already ending keeps the status it ended with
                mReason.store(reason);
            }
            void gone() { offlineFor(1); }
            /** How it ended — off the audio thread (why(), the destructor); it was killed, so the wait is short. */
            void reap() const
            {
                int status = 0;
                if (mPid > 0 && waitpid(mPid, &status, 0) == mPid)
                {
                    mPid = 0;
                    if (WIFSIGNALED(status)) mEnded = " (" + std::string(strsignal(WTERMSIG(status))) + ")";
                }
            }
            static std::vector<std::string> split(const std::string &line)
            {
                std::vector<std::string> f;
                size_t a = 0;
                for (size_t t; (t = line.find('\t', a)) != std::string::npos; a = t + 1) f.push_back(line.substr(a, t - a));
                f.push_back(line.substr(a));
                return f;
            }
            bool readLine(std::string &line, double seconds)
            {
                const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
                for (;;)
                {
                    const auto eol = mPending.find('\n');
                    if (eol != std::string::npos) { line = mPending.substr(0, eol); mPending.erase(0, eol + 1); return true; }
                    const int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now()).count();
                    if (left <= 0) return false;
                    pollfd p{mCtl, POLLIN, 0};
                    if (poll(&p, 1, left) <= 0) continue;
                    char buf[65536];
                    const ssize_t r = read(mCtl, buf, sizeof buf);
                    if (r <= 0) { gone(); return false; }
                    mPending.append(buf, (size_t)r);
                }
            }
            bool ask(const std::string &cmd, std::string &answer, std::string &err)
            {
                const std::string l = cmd + "\n";
                size_t at = 0;
                while (at < l.size())
                {
                    const ssize_t w = send(mCtl, l.data() + at, l.size() - at, MSG_NOSIGNAL);
                    if (w <= 0) { gone(); err = "it is gone"; return false; }
                    at += (size_t)w;
                }
                if (!readLine(answer, 15.0)) { err = "it did not answer `" + cmd.substr(0, cmd.find('\t')) + "` in 15 s"; return false; }
                return true;
            }

            mutable pid_t mPid = 0;
            int mCtl = -1, mGo = -1, mDone = -1;
            Shared *mShared = nullptr;
            bool mInstrument = false;
            int mLatency = 0;
            std::vector<solaris::PluginParamInfo> mParams;
            std::string mPending;
            mutable std::string mEnded;
            std::atomic<int> mReason{0}; // 0 playing; 1 gone; 2 overran; 3 refused
        };
    }

    std::unique_ptr<solaris::IPluginInstance> loadPlugin(const std::string &bundle, const std::string &uid, double rate, int maxBlock, std::string &err)
    {
        auto p = std::make_unique<OutOfProcess>();
        if (!p->start(bundle, uid, rate, maxBlock, err)) return nullptr;
        return p;
    }
}
}
