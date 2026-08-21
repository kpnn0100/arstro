/*
 *  cosmo — Android (NativeActivity) host.
 *
 *  The platform seam for the Android build (the counterpart of cosmo/linux_main.cpp).
 *  Owns the app lifecycle, an EGL/GLES surface on the ANativeWindow, and a Cairo image
 *  surface the phone UI (arstro::cosmo_touch::PhoneApp) renders into via the reused
 *  artboard::CairoTarget; the ARGB buffer is blitted to the screen as a textured quad.
 *  Touch/lifecycle events from android_native_app_glue are translated to the app.
 */
#include "CairoTarget.h"
#include "PhoneApp.h"
#include "core/decode/AndroidImageDecoder.h"

#include <android/log.h>
#include <android/asset_manager.h>
#include <android/input.h>
#include <android_native_app_glue.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cairo/cairo.h>

#include <cmath>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "cosmo", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "cosmo", __VA_ARGS__)

namespace
{
double nowMsMonotonic()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

// ─── Fonts: extract bundled TTFs from APK assets and register them with CairoTarget ──
void extractAssetTo(AAssetManager *am, const char *assetPath, const std::string &outPath)
{
    AAsset *a = AAssetManager_open(am, assetPath, AASSET_MODE_BUFFER);
    if (!a) { LOGE("asset missing: %s", assetPath); return; }
    const void *buf = AAsset_getBuffer(a);
    off_t len = AAsset_getLength(a);
    if (buf && len > 0)
    {
        FILE *f = std::fopen(outPath.c_str(), "wb");
        if (f) { std::fwrite(buf, 1, (size_t)len, f); std::fclose(f); }
    }
    AAsset_close(a);
}

void registerFonts(android_app *app)
{
    AAssetManager *am = app->activity->assetManager;
    const std::string dir = app->activity->internalDataPath ? app->activity->internalDataPath : ".";
    struct FontFile { const char *asset; const char *family; };
    const FontFile fonts[] = {
        {"fonts/Roboto-Regular.ttf",        "Roboto"},
        {"fonts/Roboto-Medium.ttf",         "Roboto Medium"},
        {"fonts/Roboto-SemiBold.ttf",       "Roboto SemiBold"},
        {"fonts/JetBrainsMono-Regular.ttf", "JetBrains Mono"},
        {"fonts/JetBrainsMono-Medium.ttf",  "JetBrains Mono Medium"},
    };
    for (const auto &ff : fonts)
    {
        std::string base = ff.asset; base = base.substr(base.find_last_of('/') + 1);
        extractAssetTo(am, ff.asset, dir + "/" + base);
        artboard::CairoTarget::registerFontFile(ff.family, dir + "/" + base);
    }
    LOGI("fonts registered");
}

arstro::cosmo::DecodedImage decodeAsset(android_app *app, const char *assetName)
{
    const std::string dir = app->activity->internalDataPath ? app->activity->internalDataPath : ".";
    const std::string out = dir + "/" + assetName;
    extractAssetTo(app->activity->assetManager, assetName, out);
    arstro::cosmo::AndroidImageDecoder dec;
    arstro::cosmo::DecodedImage img = dec.decodeFile(out);
    LOGI("decoded %s -> %dx%d ok=%d", assetName, img.width, img.height, (int)img.ok());
    return img;
}

// ─── GLES blit of a Cairo ARGB32 (premultiplied BGRA, strided) buffer ────────────────
const char *kVert = R"(#version 300 es
layout(location=0) in vec2 aPos;
out vec2 vUv;
void main(){ vUv = aPos*0.5 + 0.5; vUv.y = 1.0 - vUv.y; gl_Position = vec4(aPos,0.0,1.0); }
)";
const char *kFrag = R"(#version 300 es
precision mediump float;
in vec2 vUv;
uniform sampler2D uTex;
out vec4 frag;
void main(){ vec4 c = texture(uTex, vUv); frag = vec4(c.bgr, 1.0); }  // cairo is BGRA
)";

GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, nullptr, log); LOGE("shader: %s", log); }
    return s;
}

struct Renderer
{
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLSurface surf = EGL_NO_SURFACE;
    EGLContext ctx = EGL_NO_CONTEXT;
    int w = 0, h = 0;
    GLuint prog = 0, tex = 0, vbo = 0;

    artboard::CairoTarget target;
    cairo_surface_t *cairoSurf = nullptr;
    cairo_t *cr = nullptr;
    std::unique_ptr<arstro::cosmo_touch::PhoneApp> app;

    bool initEgl(ANativeWindow *win)
    {
        dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        eglInitialize(dpy, nullptr, nullptr);
        const EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                  EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLConfig cfg; EGLint n = 0;
        eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n);
        EGLint vid; eglGetConfigAttrib(dpy, cfg, EGL_NATIVE_VISUAL_ID, &vid);
        ANativeWindow_setBuffersGeometry(win, 0, 0, vid);
        surf = eglCreateWindowSurface(dpy, cfg, win, nullptr);
        const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
        if (!eglMakeCurrent(dpy, surf, surf, ctx)) { LOGE("eglMakeCurrent failed"); return false; }
        eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
        eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);

        prog = glCreateProgram();
        glAttachShader(prog, compile(GL_VERTEX_SHADER, kVert));
        glAttachShader(prog, compile(GL_FRAGMENT_SHADER, kFrag));
        glBindAttribLocation(prog, 0, "aPos");
        glLinkProgram(prog);
        const float quad[] = {-1,-1, 1,-1, -1,1, 1,1};
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        ensureCairo();
        LOGI("EGL ready %dx%d (GL_VERSION=%s)", w, h, (const char *)glGetString(GL_VERSION));
        return true;
    }

    void ensureCairo()
    {
        if (cairoSurf && cairo_image_surface_get_width(cairoSurf) == w &&
            cairo_image_surface_get_height(cairoSurf) == h) return;
        if (cr) { cairo_destroy(cr); cr = nullptr; }
        if (cairoSurf) { cairo_surface_destroy(cairoSurf); cairoSurf = nullptr; }
        cairoSurf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cr = cairo_create(cairoSurf);
        target.setContext(cr);
    }

    void drawFrame()
    {
        if (ctx == EGL_NO_CONTEXT || !app) return;
        // Re-bind the window context every frame: the engine's GPU-availability probe runs
        // on this (main) thread and ends by releasing EGL to NO_CONTEXT, which would leave
        // our blit with no current context (black screen). Cheap and robust to re-assert it.
        eglMakeCurrent(dpy, surf, surf, ctx);
        ensureCairo();
        cairo_save(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR); cairo_paint(cr);
        cairo_restore(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        app->render(target, nowMsMonotonic());
        cairo_surface_flush(cairoSurf);

        unsigned char *data = cairo_image_surface_get_data(cairoSurf);
        int stride = cairo_image_surface_get_stride(cairoSurf);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

        glViewport(0, 0, w, h);
        glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(prog);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(glGetUniformLocation(prog, "uTex"), 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        eglSwapBuffers(dpy, surf);
    }

    void teardown()
    {
        app.reset();
        if (cr) { cairo_destroy(cr); cr = nullptr; }
        if (cairoSurf) { cairo_surface_destroy(cairoSurf); cairoSurf = nullptr; }
        target.setContext(nullptr);
        if (dpy != EGL_NO_DISPLAY)
        {
            eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (ctx != EGL_NO_CONTEXT) eglDestroyContext(dpy, ctx);
            if (surf != EGL_NO_SURFACE) eglDestroySurface(dpy, surf);
            eglTerminate(dpy);
        }
        dpy = EGL_NO_DISPLAY; ctx = EGL_NO_CONTEXT; surf = EGL_NO_SURFACE;
    }
};

struct AppState
{
    Renderer *r = nullptr;
    bool hasFocus = false;
    // long-press synthesis (touch has no right-button; hold -> RightClick)
    bool touchDown = false, moved = false, longFired = false;
    float downX = 0, downY = 0;
    double downT = 0;
};

void onCmd(android_app *app, int32_t cmd)
{
    auto *st = static_cast<AppState *>(app->userData);
    switch (cmd)
    {
    case APP_CMD_INIT_WINDOW:
        if (app->window)
        {
            st->r = new Renderer();
            st->r->initEgl(app->window);
            st->r->app = std::make_unique<arstro::cosmo_touch::PhoneApp>((double)st->r->w, (double)st->r->h);
            // Build a real multi-image project (same EditSession model as desktop cosmo).
            for (const char *nm : {"sample.jpg", "sample2.jpg", "sample3.jpg"})
            {
                arstro::cosmo::DecodedImage img = decodeAsset(app, nm);
                if (img.ok()) st->r->app->addProjectImage(img.rgba.data(), img.width, img.height, img.name);
            }
            st->r->app->finishProject("Sample Project");
            // show/hide the soft keyboard on request (search field, rename, ...)
            st->r->app->onKeyboard = [app](bool show) {
                if (show) ANativeActivity_showSoftInput(app->activity, ANATIVEACTIVITY_SHOW_SOFT_INPUT_FORCED);
                else ANativeActivity_hideSoftInput(app->activity, 0);
            };
            LOGI("GPU compute backend available=%d (GLES 3.1)", (int)st->r->app->gpuAvailable());
            st->hasFocus = true;
        }
        break;
    case APP_CMD_TERM_WINDOW:
        st->hasFocus = false;
        if (st->r) { st->r->teardown(); delete st->r; st->r = nullptr; }
        break;
    case APP_CMD_GAINED_FOCUS: st->hasFocus = true; break;
    case APP_CMD_LOST_FOCUS:   st->hasFocus = false; break;
    default: break;
    }
}

// Touch -> PhoneApp::pointer (single primary pointer; the Artboard input HAL is
// single-pointer, so multi-finger is collapsed to pointer 0).
int32_t onInput(android_app *app, AInputEvent *ev)
{
    auto *st = static_cast<AppState *>(app->userData);
    if (!st->r || !st->r->app) return 0;
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_KEY)
    {
        if (AKeyEvent_getAction(ev) != AKEY_EVENT_ACTION_DOWN) return 1;
        int32_t kc = AKeyEvent_getKeyCode(ev);
        bool shift = AKeyEvent_getMetaState(ev) & AMETA_SHIFT_ON;
        if (kc == AKEYCODE_DEL) { st->r->app->backspace(); return 1; }
        if (kc == AKEYCODE_ENTER) { ANativeActivity_hideSoftInput(app->activity, 0); return 1; }
        if (kc == AKEYCODE_SPACE) { st->r->app->charInput(' '); return 1; }
        if (kc >= AKEYCODE_A && kc <= AKEYCODE_Z) { char c = (char)('a' + (kc - AKEYCODE_A)); if (shift) c = (char)('A' + (kc - AKEYCODE_A)); st->r->app->charInput((unsigned)c); return 1; }
        if (kc >= AKEYCODE_0 && kc <= AKEYCODE_9) { st->r->app->charInput((unsigned)('0' + (kc - AKEYCODE_0))); return 1; }
        return 0;   // let the system handle BACK etc.
    }
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return 0;
    int32_t action = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
    float x = AMotionEvent_getX(ev, 0), y = AMotionEvent_getY(ev, 0);
    double t = nowMsMonotonic();
    int kind = -1;
    if (action == AMOTION_EVENT_ACTION_DOWN) { kind = 0; st->touchDown = true; st->moved = false; st->longFired = false; st->downX = x; st->downY = y; st->downT = t; }
    else if (action == AMOTION_EVENT_ACTION_MOVE) { kind = 1; if (std::hypot(x - st->downX, y - st->downY) > 16.f) st->moved = true; }
    else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) { kind = 2; st->touchDown = false; }
    if (kind < 0) return 1;
    st->r->app->pointer(kind, (double)x, (double)y, 0, t, false, false, false);
    return 1;
}
}  // namespace

void android_main(android_app *app)
{
    AppState st;
    app->userData = &st;
    app->onAppCmd = onCmd;
    app->onInputEvent = onInput;
    registerFonts(app);

    while (true)
    {
        int events;
        android_poll_source *source;
        while (ALooper_pollOnce(st.hasFocus ? 0 : -1, nullptr, &events, (void **)&source) >= 0)
        {
            if (source) source->process(app, source);
            if (app->destroyRequested) { if (st.r) { st.r->teardown(); delete st.r; } return; }
        }
        if (st.hasFocus && st.r) st.r->drawFrame();
        if (st.touchDown && !st.moved && !st.longFired && st.r && st.r->app && nowMsMonotonic() - st.downT > 500.0)
        { st.r->app->longPress(st.downX, st.downY); st.longFired = true; }
    }
}
