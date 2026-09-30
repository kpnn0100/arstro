/*
 *  Arstro Ntwb — Protocol: NTWB 1.0.0 as C++ tables, validation and framing.
 *
 *  NTWB (native-to-web bridge) is defined ONCE, in Arstro Remote's
 *  server/arstro_remote/ntwb/spec.py, which generates docs/ntwb/api.json and API.md. This
 *  file is a second representation of that definition — the one thing a protocol rule says
 *  must never drift — so it is not trusted, it is TESTED: `spec/api.json` here is a vendored
 *  copy of the generated catalogue, and ntwb_tests compares every message, direction, field,
 *  type and required-flag below against it (NR-2). Change the protocol in spec.py, re-vendor
 *  api.json, then change this table until the test passes — never the other way round.
 *
 *  Platform-free: framing and validation only, no sockets (those are Transport.h).
 */
#pragma once
#include "ntwb/Json.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace ntwb
{
    constexpr const char *kVersion = "1.0.0";
    constexpr int kMajor = 1;
    constexpr uint8_t kFrameJson = 1;
    constexpr uint8_t kFrameBlob = 2;
    constexpr uint32_t kMaxFrame = 64u << 20;
    constexpr uint32_t kMaxBlobHeader = 16u << 10;

    /** Direction codes, exactly as the spec names them. */
    enum Dir : unsigned { A2H = 1, H2A = 2, C2H = 4, H2C = 8 };

    struct FieldSpec
    {
        const char *name;
        const char *type;       // "str" "int" "num" "bool" "obj" "any" "list[str]" "id" "name" "version" "enum:a|b"
        bool required;
    };
    struct MessageSpec
    {
        const char *name;
        unsigned dirs;          // Dir bits
        std::vector<FieldSpec> fields;
    };

    const std::vector<MessageSpec> &messages();
    const MessageSpec *findMessage(const std::string &name);
    const std::vector<FieldSpec> &blobHeaderFields();
    /** The environment variables a launched app receives (NTWB_SOCKET, NTWB_TOKEN, ...). */
    const std::vector<std::string> &environment();
    const char *dirName(Dir d);           // "a2h" ...

    /** Check one message travelling in `dir`: known type, allowed direction, every field
     *  known, required ones present, types right. Returns "" when valid, else the reason —
     *  the same sentences the host sends back in `error` (NTWB-06). */
    std::string validate(const Json &msg, Dir dir);
    std::string validateBlobHeader(const Json &hdr, Dir dir);

    // ── framing: [type u8][length u32 BE][payload] ──────────────────────────────────────
    std::string encodeJson(const Json &msg);
    std::string encodeBlob(const Json &header, const uint8_t *data, size_t n);

    struct Frame
    {
        bool blob = false;
        Json json;              // the message, or the blob header
        std::string data;       // blob bytes
    };

    /** Incremental decoder: feed bytes, take complete frames. A broken stream (unknown
     *  frame type, oversized frame, bad JSON) sets `error()` and stops producing frames. */
    class Decoder
    {
    public:
        void feed(const char *p, size_t n);
        bool next(Frame &out);
        const std::string &error() const { return mError; }

    private:
        std::string mBuf;
        size_t mPos = 0;
        std::string mError;
    };
}
}
