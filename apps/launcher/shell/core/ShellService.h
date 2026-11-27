/*
 *  arstro-shell — ShellService: THE application (R-CORE-1..5).
 *
 *  Commands in, a plain-data Model and an Event stream out. It decides everything a second front
 *  end would otherwise have to decide again: which task is in front, what a gesture means, when a
 *  task is paused, whether a setting's value is legal. It draws nothing, opens no file, starts no
 *  process — when a process must start it says so with an event (`task.spawn`) and the host does it.
 *
 *  Single-threaded: every call comes from the host's one loop.
 */
#pragma once
#include "Command.h"
#include <functional>

namespace arstro
{
namespace shell
{
    class ShellService
    {
    public:
        using Sink = std::function<void(const Event &)>;
        /** Reads the installed bundles. Injected because reading a disk is the host's job. */
        using Scanner = std::function<std::vector<AppInfo>()>;

        ShellService();

        const Model &model() const { return mModel; }
        void subscribe(Sink sink) { mSinks.push_back(std::move(sink)); }
        void setScanner(Scanner s) { mScanner = std::move(s); }

        /** Apply one command. False = rejected: nothing changed, `lastError` says why. */
        bool dispatch(const Command &c);
        /** Parse and apply one text line; an unparseable line is rejected like any other. */
        bool run(const std::string &line);

        /** What the Navigator made of the last `touch` command — the host needs it to know
         *  whether to deliver that sample to the app or cancel the touch there. */
        const Navigator::Feed &lastTouch() const { return mLastTouch; }

        /** The model as text, stable across runs: no revision counter, no gesture in flight. */
        std::string dump() const;
        /** `key=value` lines for every setting — what the host writes to disk. */
        std::string settingsText() const;

    private:
        bool reject(const std::string &why);
        void emit(const std::string &name, Fields fields = {});
        void changed() { ++mModel.revision; }
        Task *task(int id);
        void setNav(NavState nav, int focused);
        void syncTaskStates();
        void setShade(bool open);
        bool applySetting(const std::string &key, const std::string &value, std::string &error);
        void applyGesture(GestureKind kind, GestureResult result);
        void back();

        Model mModel;
        Navigator mNavigator;
        Navigator::Feed mLastTouch;
        std::vector<Sink> mSinks;
        Scanner mScanner;
        int mNextTask = 1;
        int mNextNotification = 1;
    };

    /** The API document, generated from the same tables the parser uses. */
    std::string apiMarkdown();
    std::string apiJson();
}
}
