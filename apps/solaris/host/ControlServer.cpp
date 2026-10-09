#include "ControlServer.h"
#include "../../cosmo/ControlChannel.h"
#include "Command.h"
#include "Event.h"
#include "SolarisService.h"
#include <cmath>
#include <cstdlib>

namespace arstro
{
namespace solaris_host
{
    namespace
    {
        /** `wait`'s seconds when the HOST can honour them; anything else goes to the service, which
         *  refuses it with its own sentence ("wait takes seconds, 0 … 3600") without sleeping. */
        bool heldSeconds(const std::string &text, double &out)
        {
            if (text.empty()) return false;
            char *end = nullptr;
            const double v = std::strtod(text.c_str(), &end);
            if (!end || *end != '\0' || !std::isfinite(v) || v < 0.0 || v > 3600.0) return false;
            out = v;
            return true;
        }
    }

    ControlServer::ControlServer(solaris::SolarisService &svc) : mSvc(svc), mChannel(std::make_shared<cosmo_v2::ControlChannel>())
    {
        // R-SVC-2: one event stream, one format — the socket sees the line --watch prints. The
        // service has no unsubscribe, so the sink holds the channel weakly and goes quiet with us.
        std::weak_ptr<cosmo_v2::ControlChannel> weak = mChannel;
        mSvc.subscribe([weak](const solaris::Event &e) {
            if (auto ch = weak.lock()) ch->broadcast(solaris::formatEvent(e));
        });
    }

    ControlServer::~ControlServer() { close(); }

    bool ControlServer::open(const std::string &path, std::string &err) { return mChannel->open(path, err); }
    void ControlServer::close()
    {
        if (mChannel) mChannel->close();
        mQueue.clear();
        mHoldUntil = -1.0;
    }
    bool ControlServer::isOpen() const { return mChannel->isOpen(); }
    const std::string &ControlServer::path() const { return mChannel->path(); }
    int ControlServer::clientCount() const { return mChannel->clientCount(); }

    int ControlServer::poll(double nowS)
    {
        if (!mChannel->isOpen()) return 0;
        // the channel collects every complete line first, so dispatching (which broadcasts) is safe
        mChannel->poll([this](const std::string &line) { mQueue.push_back(line); });

        if (mHoldUntil >= 0.0)
        {
            if (nowS < mHoldUntil) return 0;
            mHoldUntil = -1.0;
            mChannel->broadcast(kOk + mHeldLine);
        }
        int ran = 0;
        while (!mQueue.empty())
        {
            const std::string line = mQueue.front();
            mQueue.pop_front();
            ++ran;
            std::string err;
            const solaris::Command c = solaris::parseCommand(line, err);
            if (err.empty() && c.kind == solaris::Command::Kind::None)
            {
                mChannel->broadcast(kOk + line); // a blank line or a comment: nothing ran, nothing printed
                continue;
            }
            double seconds = 0.0;
            if (err.empty() && c.kind == solaris::Command::Kind::Wait && heldSeconds(c.arg(0), seconds))
            {
                mHoldUntil = nowS + seconds;
                mHeldLine = line;
                break; // the rest of the queue waits its turn
            }
            // THE door: the same dispatchText a click and solaris-cc use; it emits the events
            // (broadcast above as they happen), so the answer below comes after them
            if (!mSvc.dispatchText(line, err))
            {
                mChannel->broadcast(kRefused + err);
                continue;
            }
            const std::string &out = mSvc.output();
            size_t start = 0;
            while (start < out.size())
            {
                size_t nl = out.find('\n', start);
                if (nl == std::string::npos) nl = out.size();
                mChannel->broadcast(kOut + out.substr(start, nl - start));
                start = nl + 1;
            }
            mChannel->broadcast(kOk + line);
        }
        return ran;
    }
}
}
