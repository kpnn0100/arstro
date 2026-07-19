#include "GlComputeBackend.h"
#ifdef ARSTRO_GL_COMPUTE

#include "../analysis/Histogram.h"
#include "../base/ColorSpace.h"
#include "../base/Image.h"
#include "../engine/EditParams.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>

#include <cmath>
#include <cstring>
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

    // Compute shader: exposure -> contrast -> white balance (per channel, linear),
    // then the sRGB encode — reproducing the CPU point ops (Exposure/Contrast/
    // WhiteBalance) and color::srgbEncode exactly. Writes BOTH the linear result
    // (for the pre-curve/pre-mixer histogram taps, which equal the final image when
    // the later stages are identity) and the encoded result (final output).
    const char *kShaderSrc = R"GLSL(#version 430
layout(local_size_x = 256) in;
layout(std430, binding = 0) readonly  buffer Src { float src[]; };
layout(std430, binding = 1) writeonly buffer Lin { float lin[]; };
layout(std430, binding = 2) writeonly buffer Enc { float enc[]; };
uniform int   uCount;
uniform int   uCh;
uniform int   uColorCh;
uniform float uExp;
uniform float uSlope;
uniform float uPivot;
uniform vec3  uWb;
float srgb(float v) {
    if (v <= 0.0) return 0.0;
    if (v >= 1.0) return 1.0;
    if (v <= 0.0031308) return 12.92 * v;
    return 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uCount)) return;
    uint base = i * uint(uCh);
    for (int c = 0; c < uCh; ++c) {
        float x = src[base + uint(c)];
        if (c < uColorCh) {              // colour channels: exposure, contrast, WB
            x = x * uExp;
            x = (x - uPivot) * uSlope + uPivot;
            if (uColorCh >= 3) x = x * uWb[c];   // WB is a no-op on grayscale (matches CPU)
        }
        lin[base + uint(c)] = x;
        enc[base + uint(c)] = (c < uColorCh) ? srgb(x) : x;  // alpha passes through un-encoded
    }
}
)GLSL";

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
            if (!supports(p)) return false;      // outside the ported subset -> CPU
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
        // True only when the edit is entirely within the GPU-ported subset:
        // exposure / contrast / temperature / tint may vary; EVERY other stage must
        // be at its default (identity), so decline -> CPU covers the rest.
        static bool supports(const EditParams &p)
        {
            const bool toneRegionsId = p.highlights == 0 && p.shadows == 0 && p.whites == 0 && p.blacks == 0;
            const bool presenceId = p.vibrance == 0 && p.saturation == 0 && p.texture == 0 && p.clarity == 0;
            const bool effectsId = p.dehaze == 0 && p.grainAmount == 0;
            const bool detailId = p.sharpenAmount == 0 && p.nrLuminance == 0 && p.nrColor == 0;
            const bool lensId = p.lensDistortion == 0 && p.lensCA == 0 && p.lensVignette == 0;
            const bool geomId = p.rotation == 0 && p.quarterTurns == 0 &&
                                p.cropX == 0 && p.cropY == 0 && p.cropW == 1 && p.cropH == 1;
            auto curveIsId = [](const std::vector<CurvePoint> &c)
            {
                return c.size() == 2 && c[0].x == 0 && c[0].y == 0 && c[1].x == 1 && c[1].y == 1;
            };
            const bool curveId = curveIsId(p.curve) && curveIsId(p.curveChannel[0]) &&
                                 curveIsId(p.curveChannel[1]) && curveIsId(p.curveChannel[2]);
            const bool mixerId = p.mixer[0].empty() && p.mixer[1].empty() && p.mixer[2].empty();
            bool gradeId = p.balance == 0 && !p.remapEnable;
            for (int r = 0; r < 3 && gradeId; ++r)
                gradeId = p.grade[r].hue == 0 && p.grade[r].sat == 0 && p.grade[r].lum == 0;
            return toneRegionsId && presenceId && effectsId && detailId && lensId && geomId &&
                   curveId && mixerId && gradeId && p.masks.empty();
        }

        bool ensureReady()
        {
            if (mReady) return true;
            if (mFailed) return false;
            if (!makeGlContext(mDpy, mCtx) || !g.load()) { mFailed = true; return false; }

            GLuint sh = g.CreateShader(GL_COMPUTE_SHADER);
            g.ShaderSource(sh, 1, &kShaderSrc, nullptr);
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
