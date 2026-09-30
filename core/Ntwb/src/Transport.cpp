/*
 *  Arstro Ntwb — the transport adapters. The only file of this library with OS calls.
 */
#include "ntwb/Transport.h"
#include <cstring>
#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace arstro
{
namespace ntwb
{
    UnixSocketTransport::~UnixSocketTransport() { close(); }

#ifndef _WIN32
    bool UnixSocketTransport::connect(const std::string &path, std::string &err)
    {
        close();
        sockaddr_un addr{};
        if (path.empty() || path.size() >= sizeof(addr.sun_path))
        {
            err = "NTWB socket path is empty or too long (" + std::to_string(path.size()) + " bytes)";
            return false;
        }
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) { err = std::string("socket: ") + std::strerror(errno); return false; }
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        if (::connect(fd, (sockaddr *)&addr, sizeof addr) != 0)
        {
            err = "cannot connect to " + path + ": " + std::strerror(errno);
            ::close(fd);
            return false;
        }
        // A host that goes away mid-write must not kill the app with SIGPIPE.
        std::signal(SIGPIPE, SIG_IGN);
        mFd = fd;
        return true;
    }

    bool UnixSocketTransport::send(const std::string &bytes)
    {
        std::lock_guard<std::mutex> lk(mSendLock);
        size_t off = 0;
        while (mFd >= 0 && off < bytes.size())
        {
            const ssize_t n = ::send(mFd, bytes.data() + off, bytes.size() - off, MSG_NOSIGNAL);
            if (n > 0) { off += (size_t)n; continue; }
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            return false;
        }
        return mFd >= 0;
    }

    long UnixSocketTransport::recv(char *buf, size_t cap, int timeoutMs)
    {
        if (mFd < 0) return -1;
        pollfd p{mFd, POLLIN, 0};
        const int r = ::poll(&p, 1, timeoutMs);
        if (r == 0) return 0;
        if (r < 0) return errno == EINTR ? 0 : -1;
        const ssize_t n = ::recv(mFd, buf, cap, 0);
        if (n > 0) return (long)n;
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) return 0;
        return -1;
    }

    void UnixSocketTransport::close()
    {
        if (mFd >= 0)
        {
            ::shutdown(mFd, SHUT_RDWR);
            ::close(mFd);
            mFd = -1;
        }
    }
#else
    bool UnixSocketTransport::connect(const std::string &, std::string &err)
    {
        err = "the NTWB Unix-socket transport is not built on Windows yet";
        return false;
    }
    bool UnixSocketTransport::send(const std::string &) { return false; }
    long UnixSocketTransport::recv(char *, size_t, int) { return -1; }
    void UnixSocketTransport::close() {}
#endif

    // ── MemoryTransport ─────────────────────────────────────────────────────────────────
    std::pair<std::shared_ptr<MemoryTransport>, std::shared_ptr<MemoryTransport>> MemoryTransport::pair()
    {
        auto shared = std::make_shared<Shared>();
        auto a = std::make_shared<MemoryTransport>();
        auto b = std::make_shared<MemoryTransport>();
        a->mShared = shared;
        b->mShared = shared;
        a->mIsA = true;
        b->mIsA = false;
        return {a, b};
    }

    bool MemoryTransport::send(const std::string &bytes)
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        if (mShared->closed) return false;
        (mIsA ? mShared->toB : mShared->toA) += bytes;
        return true;
    }

    long MemoryTransport::recv(char *buf, size_t cap, int)
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        std::string &in = mIsA ? mShared->toA : mShared->toB;
        if (in.empty()) return mShared->closed ? -1 : 0;
        const size_t n = in.size() < cap ? in.size() : cap;
        std::memcpy(buf, in.data(), n);
        in.erase(0, n);
        return (long)n;
    }

    void MemoryTransport::close()
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        mShared->closed = true;
    }

    bool MemoryTransport::isOpen() const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        return !mShared->closed;
    }
}
}
