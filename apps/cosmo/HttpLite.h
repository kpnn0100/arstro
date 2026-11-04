/*
 *  cosmo host — HttpLite: one HTTP/1.1 request to a LOCAL server (R-ASSIST-9).
 *
 *  What a local model needs and nothing more: Ollama, LM Studio, llama.cpp and vLLM all serve an
 *  OpenAI-compatible API on plain http://localhost. No TLS, by design — a remote endpoint with an
 *  API key is a cloud model, and the user's own agent CLI already talks to those. POSIX sockets,
 *  a connect timeout, a body by Content-Length, chunked or to close. Blocking: its callers are a
 *  separate process (`cosmo-cc agent`) or a probe with a short timeout.
 */
#pragma once
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    struct HttpResult
    {
        int status = 0;          // 0 = no answer (see `error`)
        std::string body;
        std::string error;
    };

    /** `method` `url` ("http://host:port/path") with `body` (sent as application/json when
     *  non-empty). `timeoutMs` bounds the connect and every read. */
    HttpResult httpRequest(const std::string &method, const std::string &url, const std::string &body, int timeoutMs);
}
}
