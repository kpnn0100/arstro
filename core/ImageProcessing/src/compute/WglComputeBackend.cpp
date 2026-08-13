/*
 *  Arstro ImageProcessing Library
 *
 *  WglComputeBackend: the native-Windows implementation of the desktop OpenGL 4.3
 *  compute accelerator declared in GlComputeBackend.h (createGlComputeAccelerator(),
 *  same macro ARSTRO_GL_COMPUTE, same accepted subset and per-pixel GLSL — shared via
 *  GlComputeShared.h so the two platform backends can never drift). Only context
 *  creation differs from the Linux/EGL sibling (GlComputeBackend.cpp): Windows has no
 *  EGL, so a GL 4.3 core context is created the native way, via WGL over a hidden
 *  message-only window. No swapchain/visible surface is needed — this is a compute-
 *  only context, the window merely gives WGL a device context to attach to.
 *
 *  Windows' <GL/gl.h> is frozen at GL 1.1 (pre-shader era), so every GL 4.3 entry
 *  point used here — and the handful of enum/typedef tokens that go with them — is
 *  declared locally and resolved at runtime via wglGetProcAddress, same pattern as
 *  the EGL backend's eglGetProcAddress loader.
 *
 *  Compiled only when the umbrella defines ARSTRO_GL_COMPUTE on Windows (opengl32 /
 *  gdi32 / user32 linked); otherwise this file is empty and the factory in
 *  ComputeBackend.cpp falls back to CPU.
 */
#include "GlComputeBackend.h"
#if defined(ARSTRO_GL_COMPUTE) && defined(_WIN32)

#include "../analysis/Histogram.h"
#include "../base/ColorSpace.h"
#include "../base/Image.h"
#include "../engine/EditParams.h"
#include "GlComputeShared.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace arstro
{
namespace
{
    // ── GL 4.3 types/tokens <GL/gl.h> doesn't provide (it's frozen at GL 1.1) ──
    using GLchar = char;
    using GLsizeiptr = ptrdiff_t;
    using GLintptr = ptrdiff_t;

    constexpr GLenum GL_COMPUTE_SHADER = 0x91B9;
    constexpr GLenum GL_SHADER_STORAGE_BUFFER = 0x90D2;
    constexpr GLenum GL_DYNAMIC_COPY = 0x88EA;
    constexpr GLbitfield GL_MAP_READ_BIT = 0x0001;
    constexpr GLbitfield GL_SHADER_STORAGE_BARRIER_BIT = 0x00002000;
    constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
    constexpr GLenum GL_LINK_STATUS = 0x8B82;

    typedef const GLubyte *(APIENTRY *PFNGLGETSTRINGPROC)(GLenum name);
    typedef GLuint(APIENTRY *PFNGLCREATESHADERPROC)(GLenum type);
    typedef void(APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
    typedef void(APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint shader);
    typedef void(APIENTRY *PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint *params);
    typedef GLuint(APIENTRY *PFNGLCREATEPROGRAMPROC)(void);
    typedef void(APIENTRY *PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
    typedef void(APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint program);
    typedef void(APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint *params);
    typedef void(APIENTRY *PFNGLDELETESHADERPROC)(GLuint shader);
    typedef void(APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint program);
    typedef void(APIENTRY *PFNGLGENBUFFERSPROC)(GLsizei n, GLuint *buffers);
    typedef void(APIENTRY *PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
    typedef void(APIENTRY *PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
    typedef void(APIENTRY *PFNGLBINDBUFFERBASEPROC)(GLenum target, GLuint index, GLuint buffer);
    typedef void *(APIENTRY *PFNGLMAPBUFFERRANGEPROC)(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
    typedef GLboolean(APIENTRY *PFNGLUNMAPBUFFERPROC)(GLenum target);
    typedef void(APIENTRY *PFNGLDISPATCHCOMPUTEPROC)(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z);
    typedef void(APIENTRY *PFNGLMEMORYBARRIERPROC)(GLbitfield barriers);
    typedef GLint(APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar *name);
    typedef void(APIENTRY *PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
    typedef void(APIENTRY *PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
    typedef void(APIENTRY *PFNGLUNIFORM3FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);

    typedef HGLRC(WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext, const int *attribList);
    constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091;
    constexpr int WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
    constexpr int WGL_CONTEXT_PROFILE_MASK_ARB = 0x9126;
    constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x00000001;

    // ── GL 4.3 entry points, loaded via wglGetProcAddress (glGetString is linked
    // directly: pre-1.2 functions are not guaranteed to resolve through
    // wglGetProcAddress on every ICD) ──
    struct GlFns
    {
        PFNGLGETSTRINGPROC GetString = nullptr;
        PFNGLCREATESHADERPROC CreateShader = nullptr;
        PFNGLSHADERSOURCEPROC ShaderSource = nullptr;
        PFNGLCOMPILESHADERPROC CompileShader = nullptr;
        PFNGLGETSHADERIVPROC GetShaderiv = nullptr;
        PFNGLCREATEPROGRAMPROC CreateProgram = nullptr;
        PFNGLATTACHSHADERPROC AttachShader = nullptr;
        PFNGLLINKPROGRAMPROC LinkProgram = nullptr;
        PFNGLGETPROGRAMIVPROC GetProgramiv = nullptr;
        PFNGLDELETESHADERPROC DeleteShader = nullptr;
        PFNGLUSEPROGRAMPROC UseProgram = nullptr;
        PFNGLGENBUFFERSPROC GenBuffers = nullptr;
        PFNGLBINDBUFFERPROC BindBuffer = nullptr;
        PFNGLBUFFERDATAPROC BufferData = nullptr;
        PFNGLBINDBUFFERBASEPROC BindBufferBase = nullptr;
        PFNGLMAPBUFFERRANGEPROC MapBufferRange = nullptr;
        PFNGLUNMAPBUFFERPROC UnmapBuffer = nullptr;
        PFNGLDISPATCHCOMPUTEPROC DispatchCompute = nullptr;
        PFNGLMEMORYBARRIERPROC MemoryBarrier = nullptr;
        PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation = nullptr;
        PFNGLUNIFORM1IPROC Uniform1i = nullptr;
        PFNGLUNIFORM1FPROC Uniform1f = nullptr;
        PFNGLUNIFORM3FPROC Uniform3f = nullptr;

        bool load()
        {
            GetString = &::glGetString;
#define LOADGL(field, type, name)                            \
    field = reinterpret_cast<type>(wglGetProcAddress(name)); \
    if (!field) return false;
            LOADGL(CreateShader, PFNGLCREATESHADERPROC, "glCreateShader")
            LOADGL(ShaderSource, PFNGLSHADERSOURCEPROC, "glShaderSource")
            LOADGL(CompileShader, PFNGLCOMPILESHADERPROC, "glCompileShader")
            LOADGL(GetShaderiv, PFNGLGETSHADERIVPROC, "glGetShaderiv")
            LOADGL(CreateProgram, PFNGLCREATEPROGRAMPROC, "glCreateProgram")
            LOADGL(AttachShader, PFNGLATTACHSHADERPROC, "glAttachShader")
            LOADGL(LinkProgram, PFNGLLINKPROGRAMPROC, "glLinkProgram")
            LOADGL(GetProgramiv, PFNGLGETPROGRAMIVPROC, "glGetProgramiv")
            LOADGL(DeleteShader, PFNGLDELETESHADERPROC, "glDeleteShader")
            LOADGL(UseProgram, PFNGLUSEPROGRAMPROC, "glUseProgram")
            LOADGL(GenBuffers, PFNGLGENBUFFERSPROC, "glGenBuffers")
            LOADGL(BindBuffer, PFNGLBINDBUFFERPROC, "glBindBuffer")
            LOADGL(BufferData, PFNGLBUFFERDATAPROC, "glBufferData")
            LOADGL(BindBufferBase, PFNGLBINDBUFFERBASEPROC, "glBindBufferBase")
            LOADGL(MapBufferRange, PFNGLMAPBUFFERRANGEPROC, "glMapBufferRange")
            LOADGL(UnmapBuffer, PFNGLUNMAPBUFFERPROC, "glUnmapBuffer")
            LOADGL(DispatchCompute, PFNGLDISPATCHCOMPUTEPROC, "glDispatchCompute")
            LOADGL(MemoryBarrier, PFNGLMEMORYBARRIERPROC, "glMemoryBarrier")
            LOADGL(GetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC, "glGetUniformLocation")
            LOADGL(Uniform1i, PFNGLUNIFORM1IPROC, "glUniform1i")
            LOADGL(Uniform1f, PFNGLUNIFORM1FPROC, "glUniform1f")
            LOADGL(Uniform3f, PFNGLUNIFORM3FPROC, "glUniform3f")
#undef LOADGL
            return true;
        }
    };

    // A hidden, message-only window: just enough to get a device context WGL can
    // attach a pixel format and GL context to. Never shown, never pumps messages
    // (compute-only, no WM_* handling needed).
    bool createDummyWindow(HWND &hwnd, HDC &hdc)
    {
        static const wchar_t *kClassName = L"ArstroWglComputeWindow";
        static bool classRegistered = false;
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        if (!classRegistered)
        {
            WNDCLASSW wc{};
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = hInst;
            wc.lpszClassName = kClassName;
            if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return false;
            classRegistered = true;
        }
        hwnd = CreateWindowExW(0, kClassName, L"", WS_POPUP, 0, 0, 1, 1,
                                HWND_MESSAGE, nullptr, hInst, nullptr);
        if (!hwnd) return false;
        hdc = GetDC(hwnd);
        if (!hdc) { DestroyWindow(hwnd); hwnd = nullptr; return false; }
        return true;
    }

    void destroyGlContext(HWND hwnd, HDC hdc, HGLRC ctx)
    {
        if (ctx) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(ctx); }
        if (hwnd && hdc) ReleaseDC(hwnd, hdc);
        if (hwnd) DestroyWindow(hwnd);
    }

    // A hidden-window WGL context requesting GL 4.3 core, made current on the
    // CALLING thread. wglCreateContextAttribsARB itself is only reachable once a
    // (throwaway) legacy context is current, so one is created, used solely to
    // resolve that extension, and discarded — standard WGL bootstrap dance.
    bool makeGlContext(HWND &hwnd, HDC &hdc, HGLRC &ctx)
    {
        hwnd = nullptr; hdc = nullptr; ctx = nullptr;
        if (!createDummyWindow(hwnd, hdc)) return false;

        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.iLayerType = PFD_MAIN_PLANE;
        const int fmt = ChoosePixelFormat(hdc, &pfd);
        if (!fmt || !SetPixelFormat(hdc, fmt, &pfd))
        { destroyGlContext(hwnd, hdc, nullptr); hwnd = nullptr; hdc = nullptr; return false; }

        HGLRC legacy = wglCreateContext(hdc);
        if (!legacy) { destroyGlContext(hwnd, hdc, nullptr); hwnd = nullptr; hdc = nullptr; return false; }
        if (!wglMakeCurrent(hdc, legacy))
        {
            wglDeleteContext(legacy);
            destroyGlContext(hwnd, hdc, nullptr); hwnd = nullptr; hdc = nullptr;
            return false;
        }

        auto createAttribs = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
            wglGetProcAddress("wglCreateContextAttribsARB"));
        if (!createAttribs)
        {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(legacy);
            destroyGlContext(hwnd, hdc, nullptr); hwnd = nullptr; hdc = nullptr;
            return false;
        }

        const int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0
        };
        ctx = createAttribs(hdc, nullptr, attribs);
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(legacy);
        if (!ctx) { destroyGlContext(hwnd, hdc, nullptr); hwnd = nullptr; hdc = nullptr; return false; }

        if (!wglMakeCurrent(hdc, ctx))
        {
            wglDeleteContext(ctx);
            destroyGlContext(hwnd, hdc, nullptr); hwnd = nullptr; hdc = nullptr; ctx = nullptr;
            return false;
        }
        return true;
    }

    // One-time capability probe (any thread): create a throwaway GL 4.3 compute
    // context, load the entry points, tear it down, cache the yes/no. Separate from
    // the persistent working context (thread-affine, built in process()).
    bool probeGlCompute()
    {
        HWND hwnd; HDC hdc; HGLRC ctx;
        if (!makeGlContext(hwnd, hdc, ctx)) return false;
        GlFns fns;
        const bool ok = fns.load();
        destroyGlContext(hwnd, hdc, ctx);
        return ok;
    }

    // Same shader body as the EGL backend under the same #version 430 header — see
    // GlComputeShared.h; the per-pixel math cannot drift between the two.
    const std::string kShaderSrc = std::string("#version 430\n") + glcompute::shaderBody();

    class WglComputeBackend : public IComputeBackend
    {
    public:
        const char *name() const override { return "OpenGL"; }
        Kind kind() const override { return Kind::Gpu; }

        bool available() const override
        {
            static const bool ok = probeGlCompute();  // thread-safe one-time init
            return ok;
        }

        bool process(const Image &src, const EditParams &p, ComputeResult &out) override
        {
            if (!glcompute::computeSupports(p)) return false;  // outside the ported subset -> CPU
            if (!ensureReady()) return false;    // lazy context/program on this (worker) thread

            const int w = src.width(), h = src.height(), ch = src.channels();
            if (w <= 0 || h <= 0 || ch < 1) return false;
            const int colorCh = ch >= 3 ? 3 : ch;
            const size_t nFloats = (size_t)w * h * ch;
            const GLsizeiptr bytes = (GLsizeiptr)(nFloats * sizeof(float));

            // Uniforms — reuse the CPU math exactly (WB gains via the same free fn).
            const float expGain = (float)std::pow(2.0, (double)p.exposure);
            const float slope = 1.0f + p.contrast / 100.0f;
            Pixel gr = 1, gg = 1, gb = 1;
            color::kelvinToRgbGain((Pixel)p.temp, (Pixel)p.tint, gr, gg, gb);

            g.BindBuffer(GL_SHADER_STORAGE_BUFFER, mSrc);
            g.BufferData(GL_SHADER_STORAGE_BUFFER, bytes, src.data(), GL_DYNAMIC_COPY);
            g.BindBuffer(GL_SHADER_STORAGE_BUFFER, mLin);
            g.BufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_COPY);
            g.BindBuffer(GL_SHADER_STORAGE_BUFFER, mEnc);
            g.BufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_COPY);
            g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mSrc);
            g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mLin);
            g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, mEnc);

            g.UseProgram(mProg);
            g.Uniform1i(mLoc.count, w * h);
            g.Uniform1i(mLoc.ch, ch);
            g.Uniform1i(mLoc.colorCh, colorCh);
            g.Uniform1f(mLoc.exp, expGain);
            g.Uniform1f(mLoc.slope, slope);
            g.Uniform1f(mLoc.pivot, 0.18f);  // Contrast::kPivot
            g.Uniform3f(mLoc.wb, (float)gr, (float)gg, (float)gb);

            const GLuint groups = (GLuint)(((size_t)w * h + 255) / 256);
            g.DispatchCompute(groups, 1, 1);
            g.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            Image linImg(w, h, ch, ColorSpace::LinearSRGB);
            Image encImg(w, h, ch, ColorSpace::EncodedSRGB);
            g.BindBuffer(GL_SHADER_STORAGE_BUFFER, mLin);
            if (void *pl = g.MapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bytes, GL_MAP_READ_BIT))
            {
                std::memcpy(linImg.data(), pl, (size_t)bytes);
                g.UnmapBuffer(GL_SHADER_STORAGE_BUFFER);
            }
            else return false;
            g.BindBuffer(GL_SHADER_STORAGE_BUFFER, mEnc);
            if (void *pe = g.MapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bytes, GL_MAP_READ_BIT))
            {
                std::memcpy(encImg.data(), pe, (size_t)bytes);
                g.UnmapBuffer(GL_SHADER_STORAGE_BUFFER);
            }
            else return false;

            // Taps equal the final image because the accelerated subset leaves every
            // stage after WhiteBalance at identity (see computeSupports). Histograms
            // run on the CPU exactly as renderInto does, so they match bit-for-bit.
            out.preCurveHist = Histogram::compute(linImg);
            out.preMixerHue = Histogram::computeHue(linImg);
            out.finalHist = Histogram::compute(encImg);
            out.processed = std::move(encImg);
            return true;
        }

    private:
        bool ensureReady()
        {
            if (mReady) return true;
            if (mFailed) return false;
            if (!makeGlContext(mHwnd, mHdc, mCtx) || !g.load()) { mFailed = true; return false; }

            GLuint sh = g.CreateShader(GL_COMPUTE_SHADER);
            const char *src = kShaderSrc.c_str();
            g.ShaderSource(sh, 1, &src, nullptr);
            g.CompileShader(sh);
            GLint ok = 0;
            g.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok) { mFailed = true; return false; }
            mProg = g.CreateProgram();
            g.AttachShader(mProg, sh);
            g.LinkProgram(mProg);
            g.GetProgramiv(mProg, GL_LINK_STATUS, &ok);
            g.DeleteShader(sh);
            if (!ok) { mFailed = true; return false; }

            g.GenBuffers(1, &mSrc);
            g.GenBuffers(1, &mLin);
            g.GenBuffers(1, &mEnc);
            mLoc.count = g.GetUniformLocation(mProg, "uCount");
            mLoc.ch = g.GetUniformLocation(mProg, "uCh");
            mLoc.colorCh = g.GetUniformLocation(mProg, "uColorCh");
            mLoc.exp = g.GetUniformLocation(mProg, "uExp");
            mLoc.slope = g.GetUniformLocation(mProg, "uSlope");
            mLoc.pivot = g.GetUniformLocation(mProg, "uPivot");
            mLoc.wb = g.GetUniformLocation(mProg, "uWb");
            mReady = true;
            return true;
        }

        GlFns g{};
        HWND mHwnd = nullptr;
        HDC mHdc = nullptr;
        HGLRC mCtx = nullptr;
        GLuint mProg = 0, mSrc = 0, mLin = 0, mEnc = 0;
        struct { GLint count, ch, colorCh, exp, slope, pivot, wb; } mLoc{};
        bool mReady = false;
        bool mFailed = false;
    };
}  // namespace

    std::unique_ptr<IComputeBackend> createGlComputeAccelerator()
    {
        return std::unique_ptr<IComputeBackend>(new WglComputeBackend());
    }
}  // namespace arstro

#endif  // ARSTRO_GL_COMPUTE && _WIN32
