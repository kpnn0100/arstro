/*
 *  Arstro Mcp — Stdio: the library's one OS seam. MCP's stdio transport owns stdout, and an
 *  app is a lot of code that was written to print (a RAW decoder's warning, a library's
 *  banner). One stray line on stdout breaks the client's JSON-RPC stream, so before serving,
 *  the real stdout is duplicated to a private descriptor for the protocol and descriptor 1 is
 *  pointed at stderr — whatever else prints lands in the log, where it belongs.
 */
#include "mcp/Server.h"
#include <cstdio>
#include <iostream>
#include <streambuf>
#ifdef _WIN32
#include <io.h>
#define arstro_dup _dup
#define arstro_dup2 _dup2
#define arstro_fdopen _fdopen
#else
#include <unistd.h>
#define arstro_dup dup
#define arstro_dup2 dup2
#define arstro_fdopen fdopen
#endif

namespace arstro
{
namespace mcp
{
    namespace
    {
        /** A streambuf over a FILE*, so `Server::serve` can write to the private descriptor. */
        class FileBuf : public std::streambuf
        {
        public:
            explicit FileBuf(FILE *f) : mF(f) {}
        protected:
            int_type overflow(int_type c) override
            {
                if (c == traits_type::eof()) return traits_type::not_eof(c);
                return std::fputc(c, mF) == EOF ? traits_type::eof() : c;
            }
            std::streamsize xsputn(const char *s, std::streamsize n) override
            {
                return (std::streamsize)std::fwrite(s, 1, (size_t)n, mF);
            }
            int sync() override { return std::fflush(mF) == 0 ? 0 : -1; }
        private:
            FILE *mF;
        };
    }

    int serveStdio(Server &server)
    {
        std::fflush(stdout);
        std::cout.flush();
        const int proto = arstro_dup(1);
        FILE *out = proto >= 0 ? arstro_fdopen(proto, "w") : nullptr;
        if (!out) return server.serve(std::cin, std::cout);   // cannot protect it; serve anyway
        arstro_dup2(2, 1);                                     // everything else that prints -> stderr
        FileBuf buf(out);
        std::ostream os(&buf);
        const int rc = server.serve(std::cin, os);
        std::fflush(out);
        return rc;
    }
}
}
