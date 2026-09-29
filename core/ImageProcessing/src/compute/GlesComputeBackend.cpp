#include "GlesComputeBackend.h"
#ifdef ARSTRO_GLES_COMPUTE

#include "../analysis/Histogram.h"
#include "../base/ColorSpace.h"
#include "../base/Image.h"
#include "../engine/EditParams.h"
#include "GlComputeShared.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <dlfcn.h>

#if defined(__linux__) && !defined(__ANDROID__)
#include <fcntl.h>
#include <unistd.h>
#define ARSTRO_GLES_TRY_GBM 1
#endif

#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace arstro
{
namespace
{
    // ── ES 3.1 entry points, loaded — never linked (R-GPU-7) ──
    // On desktop Linux this backend shares a process with the desktop GL backend's libGL,
    // and both libraries export glGetString, glDispatchCompute… Linked directly, whichever
    // loads first wins every call, and on a libmali board that is glvnd's libGL dispatching
    // into a context it does not own. So every entry point comes from eglGetProcAddress,
    // which resolves against the EGL that made the context current. dlsym(RTLD_DEFAULT) is
    // the fallback for an EGL without EGL_KHR_get_all_proc_addresses — on Android that finds
    // the libGLESv3 the app links. The prototypes are used only through decltype, so the
    // header costs no link dependency.
    struct GlesFns
    {
#define GLES_FN(n) decltype(&::gl##n) n = nullptr;
        GLES_FN(GetString) GLES_FN(GetIntegerv) GLES_FN(CreateShader) GLES_FN(ShaderSource)
        GLES_FN(CompileShader) GLES_FN(GetShaderiv) GLES_FN(CreateProgram) GLES_FN(AttachShader)
        GLES_FN(LinkProgram) GLES_FN(GetProgramiv) GLES_FN(DeleteShader) GLES_FN(UseProgram)
        GLES_FN(GenBuffers) GLES_FN(BindBuffer) GLES_FN(BufferData) GLES_FN(BindBufferBase)
        GLES_FN(MapBufferRange) GLES_FN(UnmapBuffer) GLES_FN(DispatchCompute) GLES_FN(MemoryBarrier)
        GLES_FN(GetUniformLocation) GLES_FN(Uniform1i) GLES_FN(Uniform1f) GLES_FN(Uniform3f)
#undef GLES_FN

        static void *resolve(const char *name)
        {
            if (void *p = reinterpret_cast<void *>(eglGetProcAddress(name))) return p;
            return dlsym(RTLD_DEFAULT, name);
        }

        bool load()
        {
#define GLES_LOAD(n) n = reinterpret_cast<decltype(n)>(resolve("gl" #n)); if (!n) return false;
            GLES_LOAD(GetString) GLES_LOAD(GetIntegerv) GLES_LOAD(CreateShader) GLES_LOAD(ShaderSource)
            GLES_LOAD(CompileShader) GLES_LOAD(GetShaderiv) GLES_LOAD(CreateProgram) GLES_LOAD(AttachShader)
            GLES_LOAD(LinkProgram) GLES_LOAD(GetProgramiv) GLES_LOAD(DeleteShader) GLES_LOAD(UseProgram)
            GLES_LOAD(GenBuffers) GLES_LOAD(BindBuffer) GLES_LOAD(BufferData) GLES_LOAD(BindBufferBase)
            GLES_LOAD(MapBufferRange) GLES_LOAD(UnmapBuffer) GLES_LOAD(DispatchCompute) GLES_LOAD(MemoryBarrier)
            GLES_LOAD(GetUniformLocation) GLES_LOAD(Uniform1i) GLES_LOAD(Uniform1f) GLES_LOAD(Uniform3f)
#undef GLES_LOAD
            return true;
        }
    };

    bool hasExtension(const char *list, const char *ext)
    {
        if (!list) return false;
        const size_t n = std::strlen(ext);
        for (const char *p = list; (p = std::strstr(p, ext)) != nullptr; p += n)
            if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0')) return true;
        return false;
    }

    bool initialised(EGLDisplay dpy)
    {
        EGLint maj = 0, min = 0;
        return dpy != EGL_NO_DISPLAY && eglInitialize(dpy, &maj, &min);
    }

#ifdef ARSTRO_GLES_TRY_GBM
    // GBM over a DRM node: the only headless platform ARM's libmali offers (it has neither
    // Mesa's surfaceless platform nor EGL_EXT_device_base, and its default display needs an
    // X server). libgbm is dlopen'ed so a box without it loses only this fallback. The
    // device (and its fd) must outlive the display, and the display lives for the process,
    // so neither is ever released.
    EGLDisplay gbmDisplay(PFNEGLGETPLATFORMDISPLAYEXTPROC getPD)
    {
        void *lib = dlopen("libgbm.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return EGL_NO_DISPLAY;
        using CreateDevice = void *(*)(int);
        using DestroyDevice = void (*)(void *);
        auto create = reinterpret_cast<CreateDevice>(dlsym(lib, "gbm_create_device"));
        auto destroy = reinterpret_cast<DestroyDevice>(dlsym(lib, "gbm_device_destroy"));
        if (!create) return EGL_NO_DISPLAY;
        // Render nodes first: they need no DRM master and are what a compute context wants.
        static const char *const kNodes[] = {"/dev/dri/renderD128", "/dev/dri/renderD129",
                                             "/dev/dri/renderD130", "/dev/dri/renderD131",
                                             "/dev/dri/card0", "/dev/dri/card1"};
        for (const char *node : kNodes)
        {
            const int fd = open(node, O_RDWR | O_CLOEXEC);
            if (fd < 0) continue;
            void *dev = create(fd);
            if (dev)
            {
                EGLDisplay dpy = getPD ? getPD(EGL_PLATFORM_GBM_KHR, dev, nullptr)
                                       : eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(dev));
                if (initialised(dpy)) return dpy;
                if (destroy) destroy(dev);
            }
            close(fd);
        }
        return EGL_NO_DISPLAY;
    }
#endif

    // The one EGL display this backend uses, found once per process: Mesa's surfaceless
    // platform, then the default display (an X11/Wayland session, or Android), then GBM.
    EGLDisplay glesDisplay()
    {
        static EGLDisplay dpy = EGL_NO_DISPLAY;
        static std::once_flag once;
        std::call_once(once, []
        {
            const char *clientExt = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
            auto getPD = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
            if (getPD && hasExtension(clientExt, "EGL_MESA_platform_surfaceless"))
            {
                EGLDisplay d = getPD(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
                if (initialised(d)) { dpy = d; return; }
            }
            {
                EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
                if (initialised(d)) { dpy = d; return; }
            }
#ifdef ARSTRO_GLES_TRY_GBM
            if (getPD && hasExtension(clientExt, "EGL_KHR_platform_gbm")) { dpy = gbmDisplay(getPD); return; }
            if (!clientExt) dpy = gbmDisplay(nullptr);  // EGL 1.4 without client extensions
#endif
        });
        return dpy;
    }

    // An ES 3.1 context made current on the CALLING thread: surfaceless where the display
    // offers EGL_KHR_surfaceless_context (a GBM display may have no pbuffer configs), else
    // over a 1x1 pbuffer, which every mobile EGL supports.
    bool makeGlesContext(EGLDisplay &dpy, EGLContext &ctx, EGLSurface &pbuf)
    {
        ctx = EGL_NO_CONTEXT;
        pbuf = EGL_NO_SURFACE;
        dpy = glesDisplay();
        if (dpy == EGL_NO_DISPLAY) return false;
        if (!eglBindAPI(EGL_OPENGL_ES_API)) return false;
        const bool surfaceless = hasExtension(eglQueryString(dpy, EGL_EXTENSIONS), "EGL_KHR_surfaceless_context");
        const EGLint cfgAttr[] = {EGL_SURFACE_TYPE, surfaceless ? 0 : EGL_PBUFFER_BIT,
                                  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                  EGL_NONE};
        EGLConfig cfg = nullptr; EGLint n = 0;
        if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || n == 0) return false;
        const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
        ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
        if (ctx == EGL_NO_CONTEXT) return false;
        if (!surfaceless)
        {
            const EGLint pbAttr[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
            pbuf = eglCreatePbufferSurface(dpy, cfg, pbAttr);
            if (pbuf == EGL_NO_SURFACE) { eglDestroyContext(dpy, ctx); ctx = EGL_NO_CONTEXT; return false; }
        }
        if (!eglMakeCurrent(dpy, pbuf, pbuf, ctx))
        {
            if (pbuf != EGL_NO_SURFACE) eglDestroySurface(dpy, pbuf);
            pbuf = EGL_NO_SURFACE;
            eglDestroyContext(dpy, ctx); ctx = EGL_NO_CONTEXT;
            return false;
        }
        return true;
    }

    void releaseGlesContext(EGLDisplay dpy, EGLContext ctx, EGLSurface pbuf)
    {
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (pbuf != EGL_NO_SURFACE) eglDestroySurface(dpy, pbuf);
        eglDestroyContext(dpy, ctx);  // leave the display initialised (driver refcounts)
    }

    // One-time capability probe (any thread): create a throwaway context, check it is
    // really ES >= 3.1 and every entry point resolves, tear it down, cache the yes/no.
    // Separate from the persistent working context (thread-affine, built in process()).
    bool probeGlesCompute()
    {
        EGLDisplay dpy; EGLContext ctx; EGLSurface pbuf;
        if (!makeGlesContext(dpy, ctx, pbuf)) return false;
        GlesFns gl;
        bool ok = gl.load();
        if (ok)
        {
            GLint maj = 0, min = 0;
            gl.GetIntegerv(GL_MAJOR_VERSION, &maj);
            gl.GetIntegerv(GL_MINOR_VERSION, &min);
            ok = maj > 3 || (maj == 3 && min >= 1);
        }
        releaseGlesContext(dpy, ctx, pbuf);
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

            gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, mSrc);
            gl.BufferData(GL_SHADER_STORAGE_BUFFER, bytes, src.data(), GL_DYNAMIC_COPY);
            gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, mLin);
            gl.BufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_COPY);
            gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, mEnc);
            gl.BufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_COPY);
            gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mSrc);
            gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mLin);
            gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, mEnc);

            gl.UseProgram(mProg);
            gl.Uniform1i(mLoc.count, w * h);
            gl.Uniform1i(mLoc.ch, ch);
            gl.Uniform1i(mLoc.colorCh, colorCh);
            gl.Uniform1f(mLoc.exp, expGain);
            gl.Uniform1f(mLoc.slope, slope);
            gl.Uniform1f(mLoc.pivot, 0.18f);  // Contrast::kPivot
            gl.Uniform3f(mLoc.wb, (float)gr, (float)gg, (float)gb);

            const GLuint groups = (GLuint)(((size_t)w * h + 255) / 256);
            gl.DispatchCompute(groups, 1, 1);
            gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

            Image linImg(w, h, ch, ColorSpace::LinearSRGB);
            Image encImg(w, h, ch, ColorSpace::EncodedSRGB);
            if (!readBack(mLin, bytes, linImg.data())) return false;
            if (!readBack(mEnc, bytes, encImg.data())) return false;

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
        bool readBack(GLuint buf, GLsizeiptr bytes, void *dst)
        {
            gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
            void *p = gl.MapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bytes, GL_MAP_READ_BIT);
            if (!p) return false;
            std::memcpy(dst, p, (size_t)bytes);
            gl.UnmapBuffer(GL_SHADER_STORAGE_BUFFER);
            return true;
        }

        bool ensureReady()
        {
            if (mReady) return true;
            if (mFailed) return false;
            if (!makeGlesContext(mDpy, mCtx, mPbuf) || !gl.load()) { mFailed = true; return false; }

            GLuint sh = gl.CreateShader(GL_COMPUTE_SHADER);
            const char *src = kShaderSrc.c_str();
            gl.ShaderSource(sh, 1, &src, nullptr);
            gl.CompileShader(sh);
            GLint ok = 0;
            gl.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok) { mFailed = true; return false; }
            mProg = gl.CreateProgram();
            gl.AttachShader(mProg, sh);
            gl.LinkProgram(mProg);
            gl.GetProgramiv(mProg, GL_LINK_STATUS, &ok);
            gl.DeleteShader(sh);
            if (!ok) { mFailed = true; return false; }

            gl.GenBuffers(1, &mSrc);
            gl.GenBuffers(1, &mLin);
            gl.GenBuffers(1, &mEnc);
            mLoc.count = gl.GetUniformLocation(mProg, "uCount");
            mLoc.ch = gl.GetUniformLocation(mProg, "uCh");
            mLoc.colorCh = gl.GetUniformLocation(mProg, "uColorCh");
            mLoc.exp = gl.GetUniformLocation(mProg, "uExp");
            mLoc.slope = gl.GetUniformLocation(mProg, "uSlope");
            mLoc.pivot = gl.GetUniformLocation(mProg, "uPivot");
            mLoc.wb = gl.GetUniformLocation(mProg, "uWb");
            mReady = true;
            return true;
        }

        GlesFns gl;
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
