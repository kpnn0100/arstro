#include "HttpLite.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace arstro
{
namespace cosmo_v2
{
    namespace
    {
        bool waitFd(int fd, short events, int timeoutMs)
        {
            pollfd p{fd, events, 0};
            for (;;)
            {
                const int r = ::poll(&p, 1, timeoutMs);
                if (r < 0 && errno == EINTR) continue;
                return r > 0;
            }
        }

        /** The body of a `Transfer-Encoding: chunked` response. */
        std::string unchunk(const std::string &raw)
        {
            std::string out;
            size_t at = 0;
            while (at < raw.size())
            {
                const size_t nl = raw.find("\r\n", at);
                if (nl == std::string::npos) break;
                const size_t n = std::strtoul(raw.substr(at, nl - at).c_str(), nullptr, 16);
                if (n == 0) break;
                out.append(raw, nl + 2, n);
                at = nl + 2 + n + 2;
            }
            return out;
        }
    }

    HttpResult httpRequest(const std::string &method, const std::string &url, const std::string &body, int timeoutMs)
    {
        HttpResult r;
        if (url.rfind("http://", 0) != 0) { r.error = "only http:// endpoints (a local server) are supported: " + url; return r; }
        std::string rest = url.substr(7);
        const size_t slash = rest.find('/');
        const std::string hostPort = rest.substr(0, slash);
        const std::string path = slash == std::string::npos ? std::string("/") : rest.substr(slash);
        const size_t colon = hostPort.rfind(':');
        const std::string host = colon == std::string::npos ? hostPort : hostPort.substr(0, colon);
        const std::string port = colon == std::string::npos ? std::string("80") : hostPort.substr(colon + 1);

        addrinfo hints{}, *ai = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), port.c_str(), &hints, &ai) != 0 || !ai) { r.error = "cannot resolve " + host; return r; }
        int fd = -1;
        for (addrinfo *p = ai; p && fd < 0; p = p->ai_next)
        {
            fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (fd < 0) continue;
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
            if (::connect(fd, p->ai_addr, p->ai_addrlen) != 0 && errno != EINPROGRESS) { ::close(fd); fd = -1; continue; }
            int soErr = 0;
            socklen_t len = sizeof(soErr);
            if (!waitFd(fd, POLLOUT, timeoutMs) || getsockopt(fd, SOL_SOCKET, SO_ERROR, &soErr, &len) != 0 || soErr != 0)
            {
                ::close(fd);
                fd = -1;
            }
        }
        freeaddrinfo(ai);
        if (fd < 0) { r.error = "nothing is listening at " + hostPort; return r; }

        std::string req = method + " " + path + " HTTP/1.1\r\nHost: " + hostPort + "\r\nConnection: close\r\nAccept: application/json\r\n";
        if (!body.empty())
            req += "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
        req += "\r\n" + body;
        for (size_t off = 0; off < req.size();)
        {
            if (!waitFd(fd, POLLOUT, timeoutMs)) { ::close(fd); r.error = "timed out sending to " + hostPort; return r; }
            const ssize_t n = ::send(fd, req.data() + off, req.size() - off, MSG_NOSIGNAL);
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
            if (n <= 0) { ::close(fd); r.error = std::string("send: ") + std::strerror(errno); return r; }
            off += (size_t)n;
        }
        std::string raw;
        char buf[65536];
        for (;;)
        {
            if (!waitFd(fd, POLLIN, timeoutMs)) { ::close(fd); r.error = "timed out waiting for " + hostPort; return r; }
            const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
            if (n <= 0) break;
            raw.append(buf, (size_t)n);
            // Stop as soon as a Content-Length body is complete — some servers keep the socket.
            const size_t he = raw.find("\r\n\r\n");
            if (he != std::string::npos)
            {
                std::string head = raw.substr(0, he);
                for (char &c : head) c = (char)std::tolower((unsigned char)c);
                const size_t cl = head.find("content-length:");
                if (cl != std::string::npos && raw.size() - (he + 4) >= std::strtoul(head.c_str() + cl + 15, nullptr, 10)) break;
            }
        }
        ::close(fd);
        const size_t he = raw.find("\r\n\r\n");
        if (raw.rfind("HTTP/", 0) != 0 || he == std::string::npos) { r.error = "not an HTTP answer from " + hostPort; return r; }
        r.status = std::atoi(raw.c_str() + raw.find(' ') + 1);
        std::string head = raw.substr(0, he);
        for (char &c : head) c = (char)std::tolower((unsigned char)c);
        r.body = raw.substr(he + 4);
        if (head.find("transfer-encoding: chunked") != std::string::npos) r.body = unchunk(r.body);
        return r;
    }
}
}
