#include "GlComputeBackend.h"
#if defined(ARSTRO_GL_COMPUTE) && !defined(_WIN32)

#include "../analysis/Histogram.h"
#include "../base/ColorSpace.h"
#include "../base/Image.h"
#include "../engine/EditParams.h"
#include "GlComputeShared.h"
#include "GlPipelineShaders.h"
#include "../color/ColorMixer.h"
#include "../tone/ToneCurve.h"
#include "../transform/Crop.h"
#include "../transform/Rotate.h"
#include "../engine/MaskStack.h"
#include <algorithm>
#include <map>

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
        PFNGLUNIFORM4FPROC Uniform4f = nullptr;
        PFNGLUNIFORM3IPROC Uniform3i = nullptr;
        PFNGLUNIFORM1UIPROC Uniform1ui = nullptr;

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
            LOADGL(Uniform4f, PFNGLUNIFORM4FPROC, "glUniform4f")
            LOADGL(Uniform3i, PFNGLUNIFORM3IPROC, "glUniform3i")
            LOADGL(Uniform1ui, PFNGLUNIFORM1UIPROC, "glUniform1ui")
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

        void setWantIntermediateTaps(bool preCurve, bool preMixer) override
        {
            mWantPreCurve = preCurve;
            mWantPreMixer = preMixer;
        }

        /** R-GPU (Interstellar R-GPU-1): the MULTI-PASS pipeline. Every stage of the edit must be
         *  one this backend has ported (glcompute::pipelineSupports) — then the whole frame stays
         *  on the GPU from upload to encode; anything else declines to the CPU reference. */
        bool process(const Image &src, const EditParams &p, ComputeResult &out) override
        {
            if (!glcompute::pipelineSupports(p)) return false;   // outside the ported stages -> CPU
            if (!ensureReady()) return false;    // lazy context on this (worker) thread
            if (src.width() <= 0 || src.height() <= 0 || src.channels() < 1 || src.channels() > 4) return false;
            // The crop and the quarter turns are exact copies of pixels: Cosmo's own stages do them
            // here, before the upload, so they are the CPU's by construction. The free angle is a
            // resample and runs on the GPU below.
            const bool cropId = p.cropX == 0 && p.cropY == 0 && p.cropW == 1 && p.cropH == 1;
            const Image *in = &src;
            Image cropped, turned;
            if (!cropId) { mCropStage.setRect(p.cropX, p.cropY, p.cropW, p.cropH); mCropStage.process(*in, cropped); in = &cropped; }
            if ((p.quarterTurns & 3) != 0) { mRotateStage.setAngle(0); mRotateStage.setQuarterTurns(p.quarterTurns); mRotateStage.process(*in, turned); in = &turned; }
            const int w = in->width(), h = in->height(), ch = in->channels();
            if (w <= 0 || h <= 0) return false;
            const size_t n = (size_t)w * h;
            const GLsizeiptr imgBytes = (GLsizeiptr)(n * ch * sizeof(float)), planeBytes = (GLsizeiptr)(n * sizeof(float));

            // the CPU's own derived tables: the same stage objects, configured the same way
            mCurveStage.setLogScale(p.curveLog);
            mCurveStage.setPoints(p.curve);
            for (int c = 0; c < 3; ++c) mCurveStage.setChannelPoints(c, p.curveChannel[c]);
            mMixerStage.setCurve(ColorMixer::Hue, p.mixer[0]);
            mMixerStage.setCurve(ColorMixer::Sat, p.mixer[1]);
            mMixerStage.setCurve(ColorMixer::Lum, p.mixer[2]);
            mMixerStage.setSpread(p.mixerSpread / 100.f);

            alloc(mImg[0], imgBytes, in->data());
            alloc(mImg[1], imgBytes, nullptr);
            int cur = 0;
            auto imagePass = [&](const char *name, const std::string &srcGlsl) -> GLuint {
                const GLuint prog = program(name, srcGlsl);
                if (!prog) return 0;
                g.UseProgram(prog);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mImg[cur]);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mImg[1 - cur]);
                common(prog, w, h, ch);
                return prog;
            };
            auto run = [&](GLuint prog) { (void)prog; dispatch(n); cur = 1 - cur; };

            // Rotate's free angle (the same inverse map and bilinear sample)
            if (std::fabs((double)p.rotation * 3.14159265358979323846 / 180.0) >= 1e-9)
            {
                const GLuint pr = imagePass("rotate", glpipe::rotate());
                if (!pr) return false;
                const double a = (double)p.rotation * 3.14159265358979323846 / 180.0;
                g.Uniform1f(loc(pr, "uCos"), (float)std::cos(a));
                g.Uniform1f(loc(pr, "uSin"), (float)std::sin(a));
                run(pr);
            }
            // LensCorrection
            if (p.lensDistortion != 0.f || p.lensCA != 0.f || p.lensVignette != 0.f)
            {
                const GLuint pr = imagePass("lens", glpipe::lens());
                if (!pr) return false;
                g.Uniform1f(loc(pr, "uKDist"), p.lensDistortion / 100.f * 0.4f);
                g.Uniform1f(loc(pr, "uKCA"), p.lensCA / 100.f * 0.03f);
                g.Uniform1f(loc(pr, "uVig"), p.lensVignette / 100.f);
                run(pr);
            }
            // NoiseReduction: colour (blurred chroma planes), then luminance (the bilateral)
            const float nrColor = p.nrColor / 100.f, nrLum = p.nrLuminance / 100.f;
            if (nrColor > 0.f && ch >= 3)
            {
                alloc(mM, planeBytes * 3, nullptr);
                alloc(mMB, planeBytes * 3, nullptr);
                const GLuint pc = program("nr_chroma", glpipe::nrChroma());
                if (!pc) return false;
                g.UseProgram(pc);
                common(pc, w, h, ch);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mImg[cur]);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, mM);
                dispatch(n);
                for (int c = 0; c < 3; ++c)
                    if (!gaussian(mM, (int)(c * n), mMB, (int)(c * n), 1.0f + nrColor * 4.0f, w, h)) return false;
                const GLuint pr = imagePass("nr_chroma_apply", glpipe::nrChromaApply());
                if (!pr) return false;
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, mMB);
                g.Uniform1f(loc(pr, "uAmt"), nrColor);
                run(pr);
            }
            if (nrLum > 0.f)
            {
                if (!lumPlanes(w, h, ch, cur, planeBytes, n)) return false;
                const GLuint pr = imagePass("nr_lum", glpipe::nrLum());
                if (!pr) return false;
                const float sigmaS = 0.8f + nrLum * 2.2f;
                int rad = (int)std::ceil(sigmaS * 2.f); if (rad < 1) rad = 1; if (rad > 4) rad = 4;
                const float sigmaR = 0.02f + nrLum * 0.13f;
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, mL);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, mP);
                g.Uniform1i(loc(pr, "uRad"), rad);
                g.Uniform1f(loc(pr, "uInvS2"), 1.f / (2.f * sigmaS * sigmaS));
                g.Uniform1f(loc(pr, "uInvR2"), 1.f / (2.f * sigmaR * sigmaR));
                run(pr);
            }
            // Exposure · Contrast · ToneRegions · WhiteBalance
            {
                const GLuint pr = imagePass("point_pre", glpipe::pointPre());
                if (!pr) return false;
                Pixel gr = 1, gg = 1, gb = 1;
                color::kelvinToRgbGain((Pixel)p.temp, (Pixel)p.tint, gr, gg, gb);
                double ev = p.exposure;
                double gain = std::pow(2.0, ev < -400.0 ? -400.0 : (ev > 400.0 ? 400.0 : ev));
                if (!(gain > 0.0)) gain = 0.0; else if (gain > 1e30) gain = 1e30;   // Exposure::update
                g.Uniform1f(loc(pr, "uExp"), (float)gain);
                g.Uniform1f(loc(pr, "uSlope"), 1.0f + p.contrast / 100.0f);
                g.Uniform1f(loc(pr, "uPivot"), 0.18f);   // Contrast::kPivot
                g.Uniform4f(loc(pr, "uRegions"), p.highlights / 100.f, p.shadows / 100.f, p.whites / 100.f, p.blacks / 100.f);
                g.Uniform3f(loc(pr, "uWb"), (float)gr, (float)gg, (float)gb);
                run(pr);
            }
            if (mWantPreCurve) out.preCurveHist = Histogram::compute(readImage(mImg[cur], w, h, ch, ColorSpace::LinearSRGB));
            // ToneCurve
            if (!mCurveStage.isIdentity())
            {
                std::vector<float> lut((size_t)4 * ToneCurve::kLut);
                std::copy_n(mCurveStage.masterLut(), ToneCurve::kLut, lut.begin());
                for (int c = 0; c < 3; ++c) std::copy_n(mCurveStage.channelLut(c), ToneCurve::kLut, lut.begin() + (size_t)(c + 1) * ToneCurve::kLut);
                alloc(mLut, (GLsizeiptr)(lut.size() * sizeof(float)), lut.data());
                const GLuint pr = imagePass("curve", glpipe::curve());
                if (!pr) return false;
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, mLut);
                g.Uniform1i(loc(pr, "uLog"), mCurveStage.logScale() ? 1 : 0);
                run(pr);
            }
            // Texture, then Clarity: the luminance plane, its exact Gaussian, the combine
            auto localPass = [&](float sigma, float k, bool midGate) -> bool {
                alloc(mL, planeBytes, nullptr);
                alloc(mP, planeBytes, nullptr);
                alloc(mT, planeBytes, nullptr);
                alloc(mPB, planeBytes, nullptr);
                const GLuint pl = program("lum", glpipe::lum());
                if (!pl) return false;
                g.UseProgram(pl);
                common(pl, w, h, ch);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mImg[cur]);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, mL);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, mP);
                dispatch(n);
                if (!gaussian(mP, 0, mPB, 0, sigma, w, h)) return false;
                const GLuint pr = imagePass("local", glpipe::local());
                if (!pr) return false;
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, mL);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, mP);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, mPB);
                g.Uniform1f(loc(pr, "uK"), k);
                g.Uniform1i(loc(pr, "uMidGate"), midGate ? 1 : 0);
                run(pr);
                return true;
            };
            if (p.texture != 0.f && !localPass(2.0f, p.texture / 100.f * 0.85f, false)) return false;
            if (p.clarity != 0.f && !localPass(std::min(40.f, std::max(8.f, std::min(w, h) * 0.02f)), p.clarity / 100.f * 0.6f, true)) return false;
            // Vibrance
            if ((p.vibrance != 0.f || p.saturation != 0.f) && ch >= 3)
            {
                const GLuint pr = imagePass("vibrance", glpipe::vibrance());
                if (!pr) return false;
                g.Uniform1f(loc(pr, "uVib"), p.vibrance / 100.f);
                g.Uniform1f(loc(pr, "uSat"), p.saturation / 100.f);
                run(pr);
            }
            if (mWantPreMixer) out.preMixerHue = Histogram::computeHue(readImage(mImg[cur], w, h, ch, ColorSpace::LinearSRGB));
            // ColorMixer (and its spread: the CPU's own fastBlurPlane, box cascade or exact Gaussian)
            if (!mMixerStage.isIdentity() && ch >= 3)
            {
                std::vector<float> lut((size_t)3 * ColorMixer::kLut);
                for (int c = 0; c < 3; ++c) std::copy_n(mMixerStage.lut(c), ColorMixer::kLut, lut.begin() + (size_t)c * ColorMixer::kLut);
                alloc(mLut, (GLsizeiptr)(lut.size() * sizeof(float)), lut.data());
                alloc(mM, planeBytes * 3, nullptr);
                const GLuint pw = program("mix_weights", glpipe::mixWeights());
                if (!pw) return false;
                g.UseProgram(pw);
                common(pw, w, h, ch);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mImg[cur]);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, mLut);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, mM);
                g.Uniform3i(loc(pw, "uFlat"), mMixerStage.flat(0) ? 1 : 0, mMixerStage.flat(1) ? 1 : 0, mMixerStage.flat(2) ? 1 : 0);
                dispatch(n);
                const float sigma = mMixerStage.spread() * ColorMixer::kSpreadFraction * (float)std::min(w, h);
                const bool spread = mMixerStage.spread() > 0.f && sigma >= ColorMixer::kMinSpreadSigma;
                if (spread)
                {
                    alloc(mMB, planeBytes * 3, nullptr);
                    for (int c = 0; c < 3; ++c)
                        if (!fastBlur(mM, (int)(c * n), mMB, (int)(c * n), sigma, w, h, mMixerStage.flat(c))) return false;
                }
                const GLuint pr = imagePass("mix_apply", glpipe::mixApply());
                if (!pr) return false;
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, mM);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, spread ? mMB : mM);
                g.Uniform1i(loc(pr, "uSpread"), spread ? 1 : 0);
                run(pr);
            }
            // ColorGrading
            bool gradeId = p.balance == 0 && !p.remapEnable;
            for (int r = 0; r < 3 && gradeId; ++r) gradeId = p.grade[r].hue == 0 && p.grade[r].sat == 0 && p.grade[r].lum == 0;
            if (!gradeId && ch >= 3)
            {
                const GLuint pr = imagePass("grading", glpipe::grading());
                if (!pr) return false;
                g.Uniform3f(loc(pr, "uHue"), p.grade[0].hue, p.grade[1].hue, p.grade[2].hue);
                g.Uniform3f(loc(pr, "uSat"), p.grade[0].sat / 100.f, p.grade[1].sat / 100.f, p.grade[2].sat / 100.f);
                g.Uniform3f(loc(pr, "uLum"), p.grade[0].lum / 100.f, p.grade[1].lum / 100.f, p.grade[2].lum / 100.f);
                g.Uniform1f(loc(pr, "uBalance"), p.balance / 100.f);
                g.Uniform1i(loc(pr, "uRemap"), p.remapEnable ? 1 : 0);
                g.Uniform4f(loc(pr, "uRemapP"), p.remapSrc, p.remapRange, p.remapDst, p.remapStrength);
                run(pr);
            }
            // Dehaze: the atmospheric light (a GPU reduction the CPU finishes), then the transform
            if (p.dehaze != 0.f)
            {
                const GLuint groups = (GLuint)((n + 255) / 256);
                alloc(mPart, (GLsizeiptr)(groups * sizeof(float)), nullptr);
                const GLuint pre = program("dehaze_reduce", glpipe::dehazeReduce());
                if (!pre) return false;
                g.UseProgram(pre);
                common(pre, w, h, ch);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mImg[cur]);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, mPart);
                dispatch(n);
                float A = 0.1f;   // Dehaze's floor
                g.BindBuffer(GL_SHADER_STORAGE_BUFFER, mPart);
                if (const float *pp = (const float *)g.MapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)(groups * sizeof(float)), GL_MAP_READ_BIT))
                {
                    for (GLuint k = 0; k < groups; ++k) A = std::max(A, pp[k]);
                    g.UnmapBuffer(GL_SHADER_STORAGE_BUFFER);
                }
                else return false;
                const GLuint pr = imagePass("dehaze", glpipe::dehaze());
                if (!pr) return false;
                g.Uniform1f(loc(pr, "uA"), A);
                g.Uniform1f(loc(pr, "uAmount"), p.dehaze / 100.f);
                run(pr);
            }
            // Sharpen: the high-pass of the encoded luminance, gated by its blurred edge strength
            if (p.sharpenAmount != 0.f)
            {
                if (!lumPlanes(w, h, ch, cur, planeBytes, n)) return false;
                alloc(mPB, planeBytes, nullptr);
                alloc(mD, planeBytes, nullptr);
                alloc(mDB, planeBytes, nullptr);
                if (!gaussianOrCopy(mP, mPB, p.sharpenRadius, w, h)) return false;
                const GLuint pd = program("sharpen_detail", glpipe::sharpenDetail());
                if (!pd) return false;
                g.UseProgram(pd);
                common(pd, w, h, ch);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, mP);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, mPB);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, mD);
                dispatch(n);
                if (!gaussianOrCopy(mD, mDB, p.sharpenRadius, w, h)) return false;
                const GLuint pr = imagePass("sharpen", glpipe::sharpen());
                if (!pr) return false;
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, mL);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, mP);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, mPB);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, mDB);
                g.Uniform1f(loc(pr, "uAmount"), p.sharpenAmount / 100.f);
                g.Uniform1f(loc(pr, "uMasking"), p.sharpenMasking / 100.f);
                run(pr);
            }
            // Grain (EditEngine never reseeds it: the stage's default seed, 1)
            if (p.grainAmount > 0.f)
            {
                const GLuint pr = imagePass("grain", glpipe::grain());
                if (!pr) return false;
                g.Uniform1f(loc(pr, "uCell"), 1.f + p.grainSize / 100.f * 3.f);
                g.Uniform1f(loc(pr, "uStrength"), p.grainAmount / 100.f * 0.15f);
                g.Uniform1ui(loc(pr, "uSeed"), 1u);
                run(pr);
            }
            Image enc;
            if (!p.masks.empty())
            {
                // masks: Cosmo's own MaskStack on the linear result, then its encode — the CPU's last two steps
                enc = readImage(mImg[cur], w, h, ch, ColorSpace::LinearSRGB);
                if (enc.empty()) return false;
                applyMaskStack(enc, p.masks);
                color::encodeInPlace(enc);
            }
            else
            {
                const GLuint pr = imagePass("encode", glpipe::encode());
                if (!pr) return false;
                run(pr);
                enc = readImage(mImg[cur], w, h, ch, ColorSpace::EncodedSRGB);
                if (enc.empty()) return false;
            }
            out.finalHist = Histogram::compute(enc);
            out.processed = std::move(enc);
            return true;
        }

    private:
        void alloc(GLuint &buf, GLsizeiptr bytes, const void *data)
        {
            if (!buf) g.GenBuffers(1, &buf);
            g.BindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
            g.BufferData(GL_SHADER_STORAGE_BUFFER, bytes, data, GL_DYNAMIC_COPY);
        }
        void common(GLuint prog, int w, int h, int ch)
        {
            g.Uniform1i(loc(prog, "uW"), w);
            g.Uniform1i(loc(prog, "uH"), h);
            g.Uniform1i(loc(prog, "uCh"), ch);
        }
        GLint loc(GLuint prog, const char *name) { return g.GetUniformLocation(prog, name); }
        void dispatch(size_t n)
        {
            g.DispatchCompute((GLuint)((n + 255) / 256), 1, 1);
            g.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
        GLuint program(const char *name, const std::string &src)
        {
            auto it = mProgs.find(name);
            if (it != mProgs.end()) return it->second;
            GLuint sh = g.CreateShader(GL_COMPUTE_SHADER);
            const char *s = src.c_str();
            g.ShaderSource(sh, 1, &s, nullptr);
            g.CompileShader(sh);
            GLint ok = 0;
            g.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            GLuint prog = 0;
            if (ok)
            {
                prog = g.CreateProgram();
                g.AttachShader(prog, sh);
                g.LinkProgram(prog);
                g.GetProgramiv(prog, GL_LINK_STATUS, &ok);
                if (!ok) prog = 0;
            }
            g.DeleteShader(sh);
            mProgs[name] = prog;   // a failure is remembered: that pass declines from now on
            return prog;
        }
        Image readImage(GLuint buf, int w, int h, int ch, ColorSpace cs)
        {
            Image img(w, h, ch, cs);
            const GLsizeiptr bytes = (GLsizeiptr)((size_t)w * h * ch * sizeof(float));
            g.BindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
            void *mp = g.MapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bytes, GL_MAP_READ_BIT);
            if (!mp) return Image();
            std::memcpy(img.data(), mp, (size_t)bytes);
            g.UnmapBuffer(GL_SHADER_STORAGE_BUFFER);
            return img;
        }
        /** The luminance plane and its sRGB encode of the current image (Texture, Clarity, NR, Sharpen). */
        bool lumPlanes(int w, int h, int ch, int cur, GLsizeiptr planeBytes, size_t n)
        {
            alloc(mL, planeBytes, nullptr);
            alloc(mP, planeBytes, nullptr);
            const GLuint pl = program("lum", glpipe::lum());
            if (!pl) return false;
            g.UseProgram(pl);
            common(pl, w, h, ch);
            g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mImg[cur]);
            g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, mL);
            g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, mP);
            dispatch(n);
            return true;
        }
        /** gaussianBlurPlane's own rule for sigma <= 0: a copy. */
        bool gaussianOrCopy(GLuint in, GLuint out, float sigma, int w, int h)
        {
            if (sigma > 0.f) return gaussian(in, 0, out, 0, sigma, w, h);
            const GLuint pr = program("box_h", glpipe::boxH());
            if (!pr) return false;
            g.UseProgram(pr); common(pr, w, h, 1);
            g.Uniform1i(loc(pr, "uR"), 0); g.Uniform1i(loc(pr, "uInOff"), 0); g.Uniform1i(loc(pr, "uOutOff"), 0);
            g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, in); g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, out);
            dispatch((size_t)w * h);
            return true;
        }
        /** spatial::gaussianBlurPlane, exactly: the same kernel (radius ceil(3σ), normalised), clamped
         *  edges, horizontal then vertical. */
        bool gaussian(GLuint in, int inOff, GLuint out, int outOff, float sigma, int w, int h)
        {
            const int r = std::max(1, (int)std::ceil(sigma * 3.f));
            std::vector<float> k((size_t)(2 * r + 1));
            float sum = 0.f;
            const float inv2s2 = 1.f / (2.f * sigma * sigma);
            for (int i = -r; i <= r; ++i) { const float v = std::exp(-(float)(i * i) * inv2s2); k[(size_t)(i + r)] = v; sum += v; }
            for (auto &v : k) v /= sum;
            alloc(mK, (GLsizeiptr)(k.size() * sizeof(float)), k.data());
            const size_t n = (size_t)w * h;
            alloc(mT2, (GLsizeiptr)(n * sizeof(float)), nullptr);
            for (int pass = 0; pass < 2; ++pass)
            {
                const GLuint pr = program(pass == 0 ? "gauss_h" : "gauss_v", pass == 0 ? glpipe::gaussH() : glpipe::gaussV());
                if (!pr) return false;
                g.UseProgram(pr);
                common(pr, w, h, 1);
                g.Uniform1i(loc(pr, "uR"), r);
                g.Uniform1i(loc(pr, "uInOff"), pass == 0 ? inOff : 0);
                g.Uniform1i(loc(pr, "uOutOff"), pass == 0 ? 0 : outOff);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, pass == 0 ? in : mT2);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, pass == 0 ? mT2 : out);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, mK);
                dispatch(n);
            }
            return true;
        }
        /** spatial::fastBlurPlane, exactly: the exact Gaussian below σ 4, else Kovesi's three box
         *  passes with the CPU's own widths. */
        bool fastBlur(GLuint in, int inOff, GLuint out, int outOff, float sigma, int w, int h, bool skip)
        {
            if (skip) return true;   // a flat channel: its plane is zero and never read blurred
            if (sigma < 4.f) return gaussian(in, inOff, out, outOff, sigma, w, h);
            const float ideal = std::sqrt(12.f * sigma * sigma / 3.f + 1.f);
            int wl = (int)std::floor(ideal); if (wl % 2 == 0) --wl; if (wl < 1) wl = 1;
            const int wu = wl + 2;
            const float mIdeal = (12.f * sigma * sigma - 3.f * (float)(wl * wl) - 12.f * (float)wl - 9.f) / (-4.f * (float)wl - 4.f);
            int m = (int)(mIdeal + 0.5f); if (m < 0) m = 0; if (m > 3) m = 3;
            const size_t n = (size_t)w * h;
            alloc(mT2, (GLsizeiptr)(n * sizeof(float)), nullptr);
            alloc(mT3, (GLsizeiptr)(n * sizeof(float)), nullptr);
            // the first pass reads the source plane; each later one reads the previous; the last writes out
            GLuint from = in;
            int fromOff = inOff;
            int passesLeft = 0;
            for (int pass = 0; pass < 3; ++pass) passesLeft += (((pass < m ? wl : wu) - 1) / 2) >= 1;
            if (passesLeft == 0)
            {
                // nothing to blur: copy through one radius-0 box
                const GLuint pr = program("box_h", glpipe::boxH());
                if (!pr) return false;
                g.UseProgram(pr); common(pr, w, h, 1);
                g.Uniform1i(loc(pr, "uR"), 0); g.Uniform1i(loc(pr, "uInOff"), inOff); g.Uniform1i(loc(pr, "uOutOff"), outOff);
                g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, in); g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, out);
                dispatch(n);
                return true;
            }
            for (int pass = 0; pass < 3; ++pass)
            {
                const int r = ((pass < m ? wl : wu) - 1) / 2;
                if (r < 1) continue;
                --passesLeft;
                for (int dir = 0; dir < 2; ++dir)
                {
                    const GLuint pr = program(dir == 0 ? "box_h" : "box_v", dir == 0 ? glpipe::boxH() : glpipe::boxV());
                    if (!pr) return false;
                    g.UseProgram(pr);
                    common(pr, w, h, 1);
                    g.Uniform1i(loc(pr, "uR"), r);
                    const bool last = passesLeft == 0 && dir == 1;
                    const GLuint to = dir == 0 ? mT2 : (last ? out : mT3);
                    g.Uniform1i(loc(pr, "uInOff"), dir == 0 ? fromOff : 0);
                    g.Uniform1i(loc(pr, "uOutOff"), last ? outOff : 0);
                    g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, dir == 0 ? from : mT2);
                    g.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, to);
                    dispatch(n);
                    if (dir == 1) { from = mT3; fromOff = 0; }
                }
            }
            return true;
        }

        bool ensureReady()
        {
            if (mReady) return true;
            if (mFailed) return false;
            if (!makeGlContext(mDpy, mCtx) || !g.load()) { mFailed = true; return false; }
            mReady = true;
            return true;
        }

        GlFns g{};
        EGLDisplay mDpy = EGL_NO_DISPLAY;
        EGLContext mCtx = EGL_NO_CONTEXT;
        std::map<std::string, GLuint> mProgs;
        GLuint mImg[2] = {0, 0}, mL = 0, mP = 0, mT = 0, mPB = 0, mT2 = 0, mT3 = 0, mK = 0, mLut = 0, mM = 0, mMB = 0, mD = 0, mDB = 0, mPart = 0;
        Crop mCropStage;
        Rotate mRotateStage;
        ToneCurve mCurveStage;
        ColorMixer mMixerStage;
        bool mWantPreCurve = true, mWantPreMixer = true;
        bool mReady = false;
        bool mFailed = false;
    };
}  // namespace

    std::unique_ptr<IComputeBackend> createGlComputeAccelerator()
    {
        return std::unique_ptr<IComputeBackend>(new GlComputeBackend());
    }
}  // namespace arstro

#endif  // ARSTRO_GL_COMPUTE && !_WIN32
