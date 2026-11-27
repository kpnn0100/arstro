/*
 *  arstro-shell — the app protocol (R-APP-1..6).
 *
 *  What an app process and the shell say to each other over one local stream socket. An app
 *  never touches the display: it draws into a shared-memory buffer and says "commit"; the shell
 *  copies that into a texture and composites it. That one seam is what lets the shell animate an
 *  app's window on the GPU while the app itself is paused, slow, or gone.
 *
 *  Frames: a fixed header, then `size` bytes of payload. Plain structs, host byte order — both
 *  ends are the same machine. A file descriptor travels with the message that names it
 *  (SCM_RIGHTS). Text payloads are not NUL-terminated.
 *
 *  Pacing: the app owns the buffer until it commits, the shell owns it until it answers with
 *  FrameDone, so the two never write and read it at once and an app can never draw faster than
 *  the screen shows.
 */
#pragma once
#include <cstdint>

namespace arstro
{
namespace wire
{
    constexpr uint32_t kVersion = 1;

    enum class Msg : uint32_t
    {
        // app → shell
        Hello = 1,      // Hello
        Buffer,         // BufferInfo + one fd: the app's shared pixel buffer
        Commit,         // (empty) the buffer holds a finished frame
        BackResult,     // BackResult
        Request,        // text: one shell command line (apps with the "system" permission)
        Notify,         // text: "title\nbody"
        Finish,         // (empty) the app asks to be closed
        // shell → app
        Configure = 64, // Configure: size, density, insets, accent — sent first and on every change
        FrameDone,      // FrameDone: the committed frame is on screen; the buffer is the app's again
        Touch,          // TouchEvent
        Lifecycle,      // Lifecycle
        Back,           // (empty) the user asked to go back; answer with BackResult
        ShellEvent,     // text: one event line (apps with the "system" permission)
    };

    struct Header
    {
        uint32_t type;
        uint32_t size;
    };

    struct Hello
    {
        uint32_t version;
        int32_t task;       // from $ARSTRO_TASK
        char appId[96];
    };

    struct Configure
    {
        int32_t width, height;         // pixels
        float scale;                   // pixels per dp
        int32_t insetTop, insetBottom; // pixels the shell draws over (status bar, gesture pill)
        uint32_t accent;               // 0xRRGGBB
        uint32_t reducedMotion;        // 0 | 1
    };

    /** Pixels are 32-bit premultiplied ARGB in native byte order (Cairo's ARGB32). */
    struct BufferInfo
    {
        int32_t width, height, stride;
    };

    struct FrameDone
    {
        double timeMs;
    };

    enum class TouchPhase : int32_t { Down, Move, Up, Cancel };

    struct TouchEvent
    {
        TouchPhase phase;
        int32_t id;
        float x, y;      // pixels, in the app's buffer
        double timeMs;
    };

    enum class LifecycleState : int32_t { Resume, Pause, Stop };

    struct Lifecycle
    {
        LifecycleState state;
    };

    struct BackResult
    {
        int32_t handled;
    };
}
}
