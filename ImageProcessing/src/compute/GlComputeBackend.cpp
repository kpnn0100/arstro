#include "GlComputeBackend.h"
#ifdef ARSTRO_GL_COMPUTE

#include "../analysis/Histogram.h"
#include "../base/ColorSpace.h"
#include "../base/Image.h"
#include "../engine/EditParams.h"
#include "GlComputeShared.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace arstro
{
namespace
{
    // ── GL 4.3 entry points, loaded via eglGetProcAddress (works for GL on EGL) ──
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
#define LOADGL(field, type, name)                       \
    field = reinterpret_cast<type>(eglGetProcAddress(name)); \
    if (!field) return false;
            LOADGL(GetString, PFNGLGETSTRINGPROC, "glGetString")
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

    // Surfaceless EGL + a GL 4.3 core context, made current on the CALLING thread.
    bool makeGlContext(EGLDisplay &dpy, EGLContext &ctx)
    {
        auto getPD = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
        dpy = EGL_NO_DISPLAY;
        if (getPD) dpy = getPD(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
        if (dpy == EGL_NO_DISPLAY) dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (dpy == EGL_NO_DISPLAY) return false;
        EGLint maj = 0, min = 0;
        if (!eglInitialize(dpy, &maj, &min)) return false;
        if (!eglBindAPI(EGL_OPENGL_API)) return false;
        const EGLint cfgAttr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
        EGLConfig cfg = nullptr; EGLint n = 0;
        eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n);
        const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 3,
                                  EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
        ctx = eglCreateContext(dpy, n ? cfg : EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctxAttr);
        if (ctx == EGL_NO_CONTEXT) return false;
        if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
        {
            eglDestroyContext(dpy, ctx);
            ctx = EGL_NO_CONTEXT;
            return false;
        }
        return true;
    }

    // One-time capability probe (any thread): create a throwaway GL 4.3 compute
    // context, load the entry points, tear it down, cache the yes/no. Separate from
    // the persistent working context (which is thread-affine and built in process()).
    bool probeGlCompute()
    {
        EGLDisplay dpy; EGLContext ctx;
        if (!makeGlContext(dpy, ctx)) return false;
        GlFns fns;
        const bool ok = fns.load();
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(dpy, ctx);  // leave the display initialised (Mesa refcounts)
        return ok;
    }

    // The GLSL for this (desktop GL 4.3) backend: the shared body under a #version 430
    // header. The GLES 3.1 backend uses the same body under a #version 310 es header,
    // so the per-pixel math cannot drift (see GlComputeShared.h).
    const std::string kShaderSrc = std::string("#version 430\n") + glcompute::shaderBody();

    class GlComputeBackend : public IComputeBackend
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
            // stage after WhiteBalance at identity (see supports()). Histograms run
            // on the CPU exactly as renderInto does, so they match bit-for-bit.
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
            if (!makeGlContext(mDpy, mCtx) || !g.load()) { mFailed = true; return false; }

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
        EGLDisplay mDpy = EGL_NO_DISPLAY;
        EGLContext mCtx = EGL_NO_CONTEXT;
        GLuint mProg = 0, mSrc = 0, mLin = 0, mEnc = 0;
        struct { GLint count, ch, colorCh, exp, slope, pivot, wb; } mLoc{};
        bool mReady = false;
        bool mFailed = false;
    };
}  // namespace

    std::unique_ptr<IComputeBackend> createGlComputeAccelerator()
    {
        return std::unique_ptr<IComputeBackend>(new GlComputeBackend());
    }
}  // namespace arstro

#endif  // ARSTRO_GL_COMPUTE
