#include "GlesComputeBackend.h"
#ifdef ARSTRO_GLES_COMPUTE

#include "../analysis/Histogram.h"
#include "../base/ColorSpace.h"
#include "../base/Image.h"
#include "../engine/EditParams.h"
#include "GlComputeShared.h"

#include <EGL/egl.h>
#include <GLES3/gl31.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace arstro
{
namespace
{
    // ES 3.1 exposes glDispatchCompute / SSBOs / glMapBufferRange as CORE entry points
    // (linked from libGLESv3), so — unlike the desktop backend — no eglGetProcAddress
    // loader is needed; the functions are called directly.

    // An ES 3.1 context over a 1x1 pbuffer, made current on the CALLING thread. (A tiny
    // pbuffer is used rather than a surfaceless context: it is universally supported,
    // whereas EGL_KHR_surfaceless_context is optional on mobile GPUs.)
    bool makeGlesContext(EGLDisplay &dpy, EGLContext &ctx, EGLSurface &pbuf)
    {
        dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (dpy == EGL_NO_DISPLAY) return false;
        EGLint maj = 0, min = 0;
        if (!eglInitialize(dpy, &maj, &min)) return false;
        if (!eglBindAPI(EGL_OPENGL_ES_API)) return false;
        const EGLint cfgAttr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                  EGL_NONE};
        EGLConfig cfg = nullptr; EGLint n = 0;
        if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || n == 0) return false;
        const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
        ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
        if (ctx == EGL_NO_CONTEXT) return false;
        const EGLint pbAttr[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        pbuf = eglCreatePbufferSurface(dpy, cfg, pbAttr);
        if (pbuf == EGL_NO_SURFACE) { eglDestroyContext(dpy, ctx); ctx = EGL_NO_CONTEXT; return false; }
        if (!eglMakeCurrent(dpy, pbuf, pbuf, ctx))
        {
            eglDestroySurface(dpy, pbuf); pbuf = EGL_NO_SURFACE;
            eglDestroyContext(dpy, ctx); ctx = EGL_NO_CONTEXT;
            return false;
        }
        return true;
    }

    // One-time capability probe (any thread): create a throwaway ES 3.1 context, tear it
    // down, cache the yes/no. Separate from the persistent working context (thread-affine,
    // built in process()).
    bool probeGlesCompute()
    {
        EGLDisplay dpy; EGLContext ctx; EGLSurface pbuf;
        if (!makeGlesContext(dpy, ctx, pbuf)) return false;
        const bool ok = (glGetString(GL_VERSION) != nullptr);
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(dpy, pbuf);
        eglDestroyContext(dpy, ctx);  // leave the display initialised (driver refcounts)
        return ok;
    }

    const std::string kShaderSrc =
        std::string("#version 310 es\nprecision highp float;\nprecision highp int;\n") + glcompute::shaderBody();

    class GlesComputeBackend : public IComputeBackend
    {
    public:
        const char *name() const override { return "OpenGL ES"; }
        Kind kind() const override { return Kind::Gpu; }

        bool available() const override
        {
            static const bool ok = probeGlesCompute();  // thread-safe one-time init
            return ok;
        }

        bool process(const Image &src, const EditParams &p, ComputeResult &out) override
        {
            if (!glcompute::computeSupports(p)) return false;  // outside the ported subset -> CPU
            if (!ensureReady()) return false;                   // lazy context/program on this thread

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

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, mSrc);
            glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, src.data(), GL_DYNAMIC_COPY);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, mLin);
            glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_COPY);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, mEnc);
            glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_COPY);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mSrc);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mLin);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, mEnc);

            glUseProgram(mProg);
            glUniform1i(mLoc.count, w * h);
            glUniform1i(mLoc.ch, ch);
            glUniform1i(mLoc.colorCh, colorCh);
            glUniform1f(mLoc.exp, expGain);
            glUniform1f(mLoc.slope, slope);
            glUniform1f(mLoc.pivot, 0.18f);  // Contrast::kPivot
            glUniform3f(mLoc.wb, (float)gr, (float)gg, (float)gb);

            const GLuint groups = (GLuint)(((size_t)w * h + 255) / 256);
            glDispatchCompute(groups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            Image linImg(w, h, ch, ColorSpace::LinearSRGB);
            Image encImg(w, h, ch, ColorSpace::EncodedSRGB);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, mLin);
            if (void *pl = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bytes, GL_MAP_READ_BIT))
            {
                std::memcpy(linImg.data(), pl, (size_t)bytes);
                glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
            }
            else return false;
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, mEnc);
            if (void *pe = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bytes, GL_MAP_READ_BIT))
            {
                std::memcpy(encImg.data(), pe, (size_t)bytes);
                glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
            }
            else return false;

            // Taps equal the final image because the accelerated subset leaves every stage
            // after WhiteBalance at identity (computeSupports). Histograms run on the CPU
            // exactly as renderInto does, so they match bit-for-bit.
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
            if (!makeGlesContext(mDpy, mCtx, mPbuf)) { mFailed = true; return false; }

            GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
            const char *src = kShaderSrc.c_str();
            glShaderSource(sh, 1, &src, nullptr);
            glCompileShader(sh);
            GLint ok = 0;
            glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok) { mFailed = true; return false; }
            mProg = glCreateProgram();
            glAttachShader(mProg, sh);
            glLinkProgram(mProg);
            glGetProgramiv(mProg, GL_LINK_STATUS, &ok);
            glDeleteShader(sh);
            if (!ok) { mFailed = true; return false; }

            glGenBuffers(1, &mSrc);
            glGenBuffers(1, &mLin);
            glGenBuffers(1, &mEnc);
            mLoc.count = glGetUniformLocation(mProg, "uCount");
            mLoc.ch = glGetUniformLocation(mProg, "uCh");
            mLoc.colorCh = glGetUniformLocation(mProg, "uColorCh");
            mLoc.exp = glGetUniformLocation(mProg, "uExp");
            mLoc.slope = glGetUniformLocation(mProg, "uSlope");
            mLoc.pivot = glGetUniformLocation(mProg, "uPivot");
            mLoc.wb = glGetUniformLocation(mProg, "uWb");
            mReady = true;
            return true;
        }

        EGLDisplay mDpy = EGL_NO_DISPLAY;
        EGLContext mCtx = EGL_NO_CONTEXT;
        EGLSurface mPbuf = EGL_NO_SURFACE;
        GLuint mProg = 0, mSrc = 0, mLin = 0, mEnc = 0;
        struct { GLint count, ch, colorCh, exp, slope, pivot, wb; } mLoc{};
        bool mReady = false;
        bool mFailed = false;
    };
}  // namespace

    std::unique_ptr<IComputeBackend> createGlesComputeAccelerator()
    {
        return std::unique_ptr<IComputeBackend>(new GlesComputeBackend());
    }
}  // namespace arstro

#endif  // ARSTRO_GLES_COMPUTE
