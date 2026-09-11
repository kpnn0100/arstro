/*
 *  interstellar_core — InterstellarService: THE application, with no front end attached (R-SVC-1).
 *
 *  `dispatch(Command)` / `pump(nowMs)` / `model()` is the whole surface. The GUI, the CLI, the
 *  control socket and the test harness are four peers over this one object, and the GUI has no
 *  privileged path — which is not a migration here but a precondition of the first commit
 *  (cosmo's R-SVC is still unwinding 105 direct session calls against 7 dispatches).
 *
 *  The rack — the hosted Cosmo project that owns every source's colour — reaches this class
 *  through the `RackAccess` seam, injected. That is what lets the whole application be driven
 *  headlessly in milliseconds, and it is the one place the architecture doc was AMENDED during
 *  implementation (see AppModel.h's note on R-COSMO-4).
 */
#pragma once
#include "../Automation.h"
#include "../BindingGraph.h"
#include "../Composite.h"
#include "../Evaluator.h"
#include "../ParamRegistry.h"
#include "../Project.h"
#include "../RackAccess.h"
#include "../Timeline.h"
#include "AppModel.h"
#include "AppModelCodec.h"
#include "Command.h"
#include "Event.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
    /** A decoded source frame. The host supplies these; the core owns no codec (R-SVC-7). */
    class IFrameSource
    {
    public:
        struct Info { int width = 0, height = 0; double fps = 24.0; long long frames = 0; };
        virtual ~IFrameSource() = default;
        virtual bool open(const std::string &path, Info &out) = 0;
        virtual bool frameAt(long long frame, Raster &out) = 0;
    };

    class IFrameWriter
    {
    public:
        virtual ~IFrameWriter() = default;
        /** `frames` is how many will be written, so a sequence writer can decide its naming
         *  BEFORE the first one rather than after. Without it the first frame of a sequence
         *  landed unnumbered and the rest numbered, which a golden comparison cannot use. */
        virtual bool begin(const std::string &path, int w, int h, double fps, long long frames) = 0;
        virtual bool write(const Raster &frame) = 0;
        virtual bool end() = 0;
    };

    class InterstellarService
    {
    public:
        struct Hooks
        {
            /** One source object per media path. The core never opens a file itself. */
            std::function<std::unique_ptr<IFrameSource>()> makeFrameSource;
            std::function<std::unique_ptr<IFrameWriter>(const std::string &)> makeFrameWriter;
            /** The rack: the hosted Cosmo project. Optional — without one, colour addresses
             *  resolve to nothing and everything else still works, which is what makes the
             *  timeline testable on its own. */
            RackAccess *rack = nullptr;
        };

        explicit InterstellarService(Hooks hooks);

        bool dispatch(const Command &c);
        /** Parse then dispatch. A blank or `#`-commented line is a successful no-op, so a
         *  script file reads naturally. */
        bool dispatchText(const std::string &line, std::string &err);
        void pump(double nowMs);
        const AppModel &model() const { return mModel; }
        void subscribe(std::function<void(const Event &)> sink) { mSinks.push_back(std::move(sink)); }

        /** Front-end verbs the service answers but does not print (R-CLI): the model is the
         *  service's, the printing is the front end's. */
        std::string statePrint(const DumpOptions &o) const { return formatModel(mModel, o); }
        std::string apiDocument(bool json) const;
        /** `eval <address> --at t [--explain]`, as text a CLI can print directly. */
        bool evalAddress(const std::string &address, double t, bool explain, std::string &out) const;
        std::vector<LintFinding> lint() const;

        /** Render one composited frame at `t` into `out`. The whole pipeline, in order
         *  (binding.md §6). False when there is nothing to draw. */
        bool renderFrame(double t, Raster &out);

        bool quitRequested() const { return mQuit; }
        const Project &project() const { return mProject; }
        Project &project() { return mProject; }

    private:
        void emit(Event::Kind k, const std::string &text = {}, int a = 0, int b = 0, double ms = 0);
        bool reject(const std::string &why);
        void refreshModel();
        bool applySet(const Command &c);
        bool setAddress(const std::string &address, const std::string &value, std::string &err);

        Hooks mHooks;
        Project mProject;
        Timeline mTimeline{mProject};
        Automation mAutomation{mProject};
        BindingGraph mBindings;
        AppModel mModel;
        std::vector<std::function<void(const Event &)>> mSinks;
        std::string mPath;
        bool mQuit = false;
        unsigned mFrameSeq = 0;
    };
}
}
