#include "Channel.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace arstro
{
namespace wire
{
    static void setNonBlocking(int fd)
    {
        const int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        fcntl(fd, F_SETFD, FD_CLOEXEC);  // a spawned app must not inherit another app's socket
    }

    Channel::Channel(Channel &&o) noexcept : mFd(o.mFd), mIn(std::move(o.mIn)), mFds(std::move(o.mFds)) { o.mFd = -1; }
    Channel &Channel::operator=(Channel &&o) noexcept
    {
        if (this != &o)
        {
            close();
            mFd = o.mFd;
            mIn = std::move(o.mIn);
            mFds = std::move(o.mFds);
            o.mFd = -1;
        }
        return *this;
    }
    Channel::~Channel() { close(); }

    void Channel::close()
    {
        if (mFd >= 0) ::close(mFd);
        mFd = -1;
        for (int fd : mFds) ::close(fd);
        mFds.clear();
    }

    bool Channel::sendRaw(Msg type, const void *data, uint32_t size, int passFd)
    {
        if (mFd < 0) return false;
        std::vector<uint8_t> buf(sizeof(Header) + size);
        Header h{(uint32_t)type, size};
        std::memcpy(buf.data(), &h, sizeof h);
        if (size) std::memcpy(buf.data() + sizeof h, data, size);

        size_t sent = 0;
        while (sent < buf.size())
        {
            msghdr msg{};
            iovec iov{buf.data() + sent, buf.size() - sent};
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;
            char ctrl[CMSG_SPACE(sizeof(int))];
            if (passFd >= 0 && sent == 0)
            {
                std::memset(ctrl, 0, sizeof ctrl);
                msg.msg_control = ctrl;
                msg.msg_controllen = sizeof ctrl;
                cmsghdr *c = CMSG_FIRSTHDR(&msg);
                c->cmsg_level = SOL_SOCKET;
                c->cmsg_type = SCM_RIGHTS;
                c->cmsg_len = CMSG_LEN(sizeof(int));
                std::memcpy(CMSG_DATA(c), &passFd, sizeof(int));
            }
            const ssize_t n = ::sendmsg(mFd, &msg, MSG_NOSIGNAL);
            if (n < 0)
            {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // The peer is slow to read. Messages are small and a frame is paced by
                    // FrameDone, so waiting a moment is right; dropping half a message is not.
                    fd_set w;
                    FD_ZERO(&w);
                    FD_SET(mFd, &w);
                    timeval tv{0, 200000};
                    if (::select(mFd + 1, nullptr, &w, nullptr, &tv) <= 0) return false;
                    continue;
                }
                return false;
            }
            sent += (size_t)n;
        }
        return true;
    }

    bool Channel::pump(std::vector<Message> &out)
    {
        bool alive = mFd >= 0;
        while (alive)
        {
            uint8_t buf[8192];
            msghdr msg{};
            iovec iov{buf, sizeof buf};
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;
            char ctrl[CMSG_SPACE(sizeof(int) * 4)];
            msg.msg_control = ctrl;
            msg.msg_controllen = sizeof ctrl;
            const ssize_t n = ::recvmsg(mFd, &msg, MSG_CMSG_CLOEXEC);
            if (n < 0)
            {
                if (errno == EINTR) continue;
                if (errno != EAGAIN && errno != EWOULDBLOCK) alive = false;
                break;
            }
            if (n == 0) { alive = false; break; }
            for (cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c))
                if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS)
                {
                    const size_t count = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                    for (size_t i = 0; i < count; ++i)
                    {
                        int fd;
                        std::memcpy(&fd, CMSG_DATA(c) + i * sizeof(int), sizeof(int));
                        mFds.push_back(fd);
                    }
                }
            mIn.insert(mIn.end(), buf, buf + n);
        }
        size_t at = 0;
        while (mIn.size() - at >= sizeof(Header))
        {
            Header h;
            std::memcpy(&h, mIn.data() + at, sizeof h);
            if (h.size > (1u << 20)) { alive = false; mIn.clear(); at = 0; break; }  // not our protocol
            if (mIn.size() - at < sizeof h + h.size) break;
            Message m;
            m.type = (Msg)h.type;
            m.data.assign(mIn.begin() + at + sizeof h, mIn.begin() + at + sizeof h + h.size);
            if (m.type == Msg::Buffer && !mFds.empty())
            {
                m.fd = mFds.front();
                mFds.erase(mFds.begin());
            }
            out.push_back(std::move(m));
            at += sizeof h + h.size;
        }
        if (at) mIn.erase(mIn.begin(), mIn.begin() + at);
        return alive;
    }

    int listenOn(const std::string &path)
    {
        sockaddr_un addr{};
        if (path.size() >= sizeof addr.sun_path) return -1;
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        ::unlink(path.c_str());
        addr.sun_family = AF_UNIX;
        std::strcpy(addr.sun_path, path.c_str());
        if (::bind(fd, (sockaddr *)&addr, sizeof addr) < 0 || ::listen(fd, 16) < 0)
        {
            ::close(fd);
            return -1;
        }
        setNonBlocking(fd);
        return fd;
    }

    int acceptOn(int listenFd)
    {
        const int fd = ::accept(listenFd, nullptr, nullptr);
        if (fd >= 0) setNonBlocking(fd);
        return fd;
    }

    int connectTo(const std::string &path)
    {
        sockaddr_un addr{};
        if (path.size() >= sizeof addr.sun_path) return -1;
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        addr.sun_family = AF_UNIX;
        std::strcpy(addr.sun_path, path.c_str());
        if (::connect(fd, (sockaddr *)&addr, sizeof addr) < 0)
        {
            ::close(fd);
            return -1;
        }
        setNonBlocking(fd);
        return fd;
    }

    int createShm(size_t size)
    {
        const int fd = ::memfd_create("arstro-surface", MFD_CLOEXEC);
        if (fd < 0) return -1;
        if (::ftruncate(fd, (off_t)size) < 0)
        {
            ::close(fd);
            return -1;
        }
        return fd;
    }
}
}
