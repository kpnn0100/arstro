#pragma once
/*
 *  What Solaris and a `solaris-plughost --serve` share (R-VST-6; docs/discussion.md Round 9): a block of memory per plugin
 *  instance — inputs, note events and parameter changes in, outputs back — handed over with an eventfd each way ("go",
 *  "done"); and the base64 a plugin's state crosses the control socket as. Plain data, no SDK types: the host includes
 *  it without the SDK.
 */
#include <cstdint>
#include <string>

namespace arstro
{
namespace plughost
{
    constexpr int kMaxBlock = 4096, kMaxEvents = 512, kMaxParams = 512, kChannels = 2;

    struct NoteEvent
    {
        int32_t offset = 0, on = 1, pitch = 60;
        float velocity = 1.0f;     // 0 … 1
    };
    struct ParamChange
    {
        uint32_t id = 0;
        int32_t offset = 0;
        double value = 0.0;        // normalised 0 … 1
    };
    struct Shared
    {
        int32_t frames = 0;        // this block's length (≤ kMaxBlock); 0: only the reset, if any
        int32_t reset = 0;         // 1: every note off, every tail cleared — before this block
        int32_t nEvents = 0, nParams = 0;
        NoteEvent events[kMaxEvents];
        ParamChange params[kMaxParams];
        float in[kChannels][kMaxBlock];
        float out[kChannels][kMaxBlock];
        int32_t status = 0;        // 0: processed; else the plugin refused this block
    };

    inline std::string toBase64(const std::string &in)
    {
        static const char *k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        size_t i = 0;
        for (; i + 2 < in.size(); i += 3)
        {
            const uint32_t v = ((uint32_t)(uint8_t)in[i] << 16) | ((uint32_t)(uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2];
            out += k[v >> 18]; out += k[(v >> 12) & 63]; out += k[(v >> 6) & 63]; out += k[v & 63];
        }
        if (i + 1 == in.size())
        {
            const uint32_t v = (uint32_t)(uint8_t)in[i] << 16;
            out += k[v >> 18]; out += k[(v >> 12) & 63]; out += "==";
        }
        else if (i + 2 == in.size())
        {
            const uint32_t v = ((uint32_t)(uint8_t)in[i] << 16) | ((uint32_t)(uint8_t)in[i + 1] << 8);
            out += k[v >> 18]; out += k[(v >> 12) & 63]; out += k[(v >> 6) & 63]; out += '=';
        }
        return out;
    }

    inline bool fromBase64(const std::string &in, std::string &out)
    {
        auto val = [](char c) -> int {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        };
        out.clear();
        uint32_t acc = 0;
        int bits = 0;
        for (char c : in)
        {
            if (c == '=') break;
            const int v = val(c);
            if (v < 0) return false;
            acc = (acc << 6) | (uint32_t)v;
            bits += 6;
            if (bits >= 8) { bits -= 8; out += (char)((acc >> bits) & 0xFF); }
        }
        return true;
    }
}
}
