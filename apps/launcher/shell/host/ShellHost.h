/*
 *  arstro-shell — ShellHost: the front end that owns the screen (R-HOST-1..8).
 *
 *  It is a CLIENT of ShellService like any other front end: it turns touches into `touch`
 *  commands, draws what the model says, and does the things only a host may do — start and reap
 *  app processes, serve the app socket, read and write the settings file, talk to SDL.
 *
 *  Drawing is two-stage on purpose. The shell's own surfaces are drawn with Artboard through
 *  Cairo into textures, and only when they change. Every frame the GPU then composites those
 *  textures and the apps' buffers at the poses the Scene computed. A transition is therefore a
 *  handful of textured quads moving — the cost does not depend on what is on the surfaces — which
 *  is what makes full-screen motion affordable on a Pi.
 *
 *  Compositing has two back ends behind one primitive (`quad`): SDL's GPU renderer for a screen,
 *  and Cairo into a plain image when there is no renderer (headless). Everything above `quad` —
 *  the service, the scene, the views, the app protocol, the poses — is the same code in both, so
 *  a PNG from a headless run shows the layout and the motion a screen would. (SDL's own software
 *  renderer was tried for headless and rejected: it does not rasterise textured triangle fans
 *  correctly, so rounded cards came out torn.)
 */
#pragma once
#include "../core/AppRegistry.h"
#include "../core/ShellService.h"
#include "../protocol/Channel.h"
#include "Scene.h"
#include "Views.h"
#include "adapter/native/CairoTarget.h"
#include <SDL3/SDL.h>
#include <cairo/cairo.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace shell
{
    /** A Cairo-drawn picture held as a GPU texture. */
    struct Layer
    {
        cairo_surface_t *surface = nullptr;
        SDL_Texture *texture = nullptr;
        int w = 0, h = 0;
        bool ensure(SDL_Renderer *r, int width, int height);  // true if it was (re)created; r may be null
        cairo_t *begin();   // cleared to transparent
        void end(cairo_t *cr);
        void release();
    };

    struct HostOptions
    {
        double scale = 0.0;               // pixels per dp; 0 = derive from the output size
        std::vector<std::string> appRoots;
        std::string configPath;           // "" = settings are not persisted
        std::string socketPath;           // the app socket; "" = pick one in the runtime dir
        std::string controlPath;          // "" = no control socket
    };

    class ShellHost
    {
    public:
        /** `renderer` null = headless: frames are composited with Cairo into `canvas()`. */
        ShellHost(SDL_Renderer *renderer, const HostOptions &options);
        ~ShellHost();

        ShellService &service() { return mService; }
        const Scene &scene() const { return mScene; }

        /** The output changed size (pixels). */
        void resize(int pxW, int pxH);
        /** One touch sample in output pixels. */
        void touch(TouchSample::Kind kind, double pxX, double pxY, double tMs);
        /** Advance everything to `nowMs` and draw one frame into the renderer (no present). */
        void frame(double nowMs);
        /** Call after the frame has been presented: tells the apps their frames are on screen. */
        void presented(double nowMs);

        /**
         *  One line of the control grammar: any service command, or a host verb —
         *    state | scene | tap <x> <y> | swipe <x0> <y0> <x1> <y1> <ms> | clock <HH:MM> <date…> |
         *    shot <file.png>
         *  Returns the reply text ("ok", an "error: …" line, or the dump asked for).
         */
        std::string runLine(const std::string &line);

        /** A screenshot was asked for with `shot`: the path, once; the caller writes the file. */
        std::string takeShotRequest();
        /** True while a scripted tap or swipe is still playing. */
        bool synthBusy() const { return !mSynth.empty(); }
        bool quitRequested() const { return mQuit; }
        double scale() const { return mScale; }
        /** Headless only: the composited frame. */
        cairo_surface_t *canvas() const { return mCanvas; }

    private:
        /** Something `quad` can draw: a GPU texture, or a Cairo surface when headless. */
        struct Pic
        {
            SDL_Texture *texture = nullptr;
            cairo_surface_t *surface = nullptr;
            double w = 0, h = 0;
            Pic() = default;
            Pic(const Layer &l) : texture(l.texture), surface(l.surface), w(l.w), h(l.h) {}
            bool empty() const { return !texture && !surface; }
        };
        struct Client
        {
            wire::Channel channel;
            int task = 0;
            bool system = false;
            uint8_t *pixels = nullptr;
            size_t mapped = 0;
            int w = 0, h = 0, stride = 0;
            bool committed = false;   // a frame is waiting to be uploaded
            bool owesDone = false;    // a FrameDone is owed after the next present
            bool dead = false;
        };
        struct TaskView
        {
            int pid = 0;
            SDL_Texture *texture = nullptr;  // the app's last frame — kept while it is paused
            cairo_surface_t *frame = nullptr; // the same, headless
            int texW = 0, texH = 0;
            artboard::Spring reveal{0.0};    // 0 = splash .. 1 = the app's own picture
            Layer header, splash;
            bool headerDrawn = false;
            int idleFrames = 0;
        };
        struct Synth { double at; TouchSample::Kind kind; double x, y; };

        void onEvent(const Event &e);
        void dispatch(const std::string &line);  // safe from inside an event: deferred
        void runDeferred();
        void pumpClients();
        void handleMessage(Client &c, const wire::Message &m);
        void configure(Client &c);
        void spawn(int task, const AppInfo &app);
        void reap();
        Client *clientFor(int task);
        void loadSettings();
        void saveSettings();
        void pumpControl();
        int iconImage(const AppInfo &app);
        void applyTheme();
        void drawStaticSprites();
        void drawTaskSprites(TaskView &tv, const AppInfo *app);
        void renderViews(double nowMs);
        void composite();
        void quad(const Pic &pic, const artboard::Rect &dstPx, const artboard::Rect &uv, double radiusPx,
                  double alpha, const artboard::Color &solid = artboard::Color{1, 1, 1, 1});
        void releaseTask(TaskView &tv);
        void timeStrings(std::string &time, std::string &date) const;
        void cancelTarget();

        SDL_Renderer *mRenderer;
        cairo_surface_t *mCanvas = nullptr;
        cairo_t *mCanvasCr = nullptr;
        HostOptions mOptions;
        ShellService mService;
        Scene mScene;
        artboard::CairoTarget mTarget;
        int mPxW = 0, mPxH = 0;
        double mScale = 1.0, mNow = 0.0, mLast = -1.0;
        bool mQuit = false;

        // views and their layers
        std::shared_ptr<HomeView> mHome;
        std::shared_ptr<StatusBarView> mStatus;
        std::shared_ptr<ShadeView> mShade;
        artboard::GestureRecognizer mHomeInput, mShadeInput;
        Layer mHomeLayer, mStatusLayer, mShadeLayer, mWallpaper, mPill, mBackBubble, mEmptyText;
        bool mSpritesDrawn = false, mViewsFresh = true;
        bool mHomeStale = true, mStatusStale = true, mShadeStale = true;
        artboard::Spring mDim{0.0}, mScaleSpring{1.0};

        // input
        enum class Target { None, Home, Shade, App, Recents };
        Target mTarget_ = Target::None;
        int mTargetTask = 0;
        std::vector<Synth> mSynth;
        double mSynthBase = 0.0;

        // apps
        int mListen = -1;
        std::string mSocketPath;
        std::vector<std::unique_ptr<Client>> mClients;
        std::map<int, TaskView> mTasks;
        std::map<std::string, int> mIconImages;
        std::vector<std::string> mDeferred;
        bool mInEvent = false;

        // control socket
        int mControlListen = -1;
        struct ControlPeer { int fd; std::string in; bool events = false; };
        std::vector<ControlPeer> mControl;
        std::string mShot;
        std::string mFixedTime, mFixedDate;
    };

    /** Write an ARGB32 pixel block as a PNG. */
    bool writePng(const std::string &path, const void *pixels, int w, int h, int stride);
}
}
