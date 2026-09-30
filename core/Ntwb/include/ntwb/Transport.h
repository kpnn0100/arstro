/*
 *  Arstro Ntwb — Transport: the ONE platform seam (Artboard's lesson: keep the HAL tiny).
 *
 *  Everything above it — framing, validation, the client's dispatch — is platform-free
 *  and runs unchanged over any byte stream. Adapters:
 *
 *    * UnixSocketTransport — POSIX AF_UNIX stream socket, what Arstro Remote hands an app
 *      in NTWB_SOCKET. (Windows: an AF_UNIX socket also exists since Windows 10 1803; the
 *      adapter is not built there yet and connect() reports that instead of failing oddly.)
 *    * MemoryTransport — an in-process pipe pair, so the client is tested without a socket,
 *      the way Artboard's RecordingTarget tests drawing without a screen.
 */
#pragma once
#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace arstro
{
namespace ntwb
{
    class ITransport
    {
    public:
        virtual ~ITransport() = default;
        /** Write all bytes (may block while the peer is slow). False when the stream is closed. */
        virtual bool send(const std::string &bytes) = 0;
        /** Read what is available, waiting at most `timeoutMs` (0 = poll). Returns the number
         *  of bytes, 0 when nothing arrived, -1 when the stream is closed. */
        virtual long recv(char *buf, size_t cap, int timeoutMs) = 0;
        virtual void close() = 0;
        virtual bool isOpen() const = 0;
    };

    class UnixSocketTransport : public ITransport
    {
    public:
        ~UnixSocketTransport() override;
        bool connect(const std::string &path, std::string &err);
        bool send(const std::string &bytes) override;
        long recv(char *buf, size_t cap, int timeoutMs) override;
        void close() override;
        bool isOpen() const override { return mFd >= 0; }

    private:
        int mFd = -1;
        std::mutex mSendLock;
    };

    /** Two connected ends in memory. `pair()` returns (app side, host side). */
    class MemoryTransport : public ITransport
    {
    public:
        static std::pair<std::shared_ptr<MemoryTransport>, std::shared_ptr<MemoryTransport>> pair();
        bool send(const std::string &bytes) override;
        long recv(char *buf, size_t cap, int timeoutMs) override;
        void close() override;
        bool isOpen() const override;

    private:
        struct Shared
        {
            std::mutex m;
            std::string toA, toB;
            bool closed = false;
        };
        std::shared_ptr<Shared> mShared;
        bool mIsA = true;
    };
}
}
