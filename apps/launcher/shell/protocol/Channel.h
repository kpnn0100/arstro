/*
 *  arstro-shell — Channel: one end of the app socket.
 *
 *  Framing, partial reads and descriptor passing in one place, used unchanged by the shell and by
 *  the app SDK, so the two cannot disagree about how a message is cut. Non-blocking: `pump()`
 *  reads what is there and returns the complete messages.
 *
 *  POSIX only today. The Windows port replaces this file (a named pipe and a named file mapping);
 *  nothing above it knows which it is talking through.
 */
#pragma once
#include "Wire.h"
#include <string>
#include <vector>

namespace arstro
{
namespace wire
{
    struct Message
    {
        Msg type = Msg::Hello;
        std::vector<uint8_t> data;
        int fd = -1;  // a descriptor that arrived with this message; the receiver owns it

        template <class T> bool as(T &out) const
        {
            if (data.size() != sizeof(T)) return false;
            __builtin_memcpy(&out, data.data(), sizeof(T));
            return true;
        }
        std::string text() const { return std::string(data.begin(), data.end()); }
    };

    class Channel
    {
    public:
        Channel() = default;
        explicit Channel(int fd) : mFd(fd) {}
        Channel(const Channel &) = delete;
        Channel &operator=(const Channel &) = delete;
        Channel(Channel &&o) noexcept;
        Channel &operator=(Channel &&o) noexcept;
        ~Channel();

        int fd() const { return mFd; }
        bool open() const { return mFd >= 0; }
        void close();

        bool sendRaw(Msg type, const void *data, uint32_t size, int passFd = -1);
        template <class T> bool send(Msg type, const T &v, int passFd = -1) { return sendRaw(type, &v, (uint32_t)sizeof(T), passFd); }
        bool send(Msg type) { return sendRaw(type, nullptr, 0); }
        bool sendText(Msg type, const std::string &s) { return sendRaw(type, s.data(), (uint32_t)s.size()); }

        /** Read whatever has arrived. False once the peer has gone (messages already complete are
         *  still returned). */
        bool pump(std::vector<Message> &out);

    private:
        int mFd = -1;
        std::vector<uint8_t> mIn;
        std::vector<int> mFds;  // descriptors received, waiting for the message they belong to
    };

    /** Listen on a filesystem socket path (unlinks a stale one first). -1 on failure. */
    int listenOn(const std::string &path);
    /** Accept one waiting connection, non-blocking. -1 if there is none. */
    int acceptOn(int listenFd);
    int connectTo(const std::string &path);
    /** An anonymous shared-memory file of `size` bytes. -1 on failure. */
    int createShm(size_t size);
}
}
