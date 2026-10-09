/*
 *  solaris_core — SolarisService: THE application (R-SVC-1, arstro.rule §1).
 *
 *  One way in: a text line in the grammar (`dispatchText`), parsed by the table in Command.cpp.
 *  One way out: the `AppModel` (plain data, refreshed after every change) and the `Event` stream
 *  (every line also the log). `output()` is what the last command printed — `get`, `audit`,
 *  `matrix print`, `state print`, `api`.
 *
 *  The service carries no codec, no device API and no OS path (R-SVC-4): decoding a file, writing
 *  a WAV, listing a folder and the settings/recents files are the host's, injected as `Host`. A
 *  host that leaves one out gets a refusal naming it, never a silent no-op.
 *
 *  Every edit is all-or-nothing: the project is copied before a mutating command, the command
 *  runs, the result is validated (R-FMT-4, R-MIX-4), and a failure restores the copy and refuses
 *  with the validation's sentence — a refused command changes nothing.
 */
#pragma once
#include "AppModel.h"
#include "AudioOut.h"
#include "Command.h"
#include "Event.h"
#include "MixGraph.h"
#include "Player.h"
#include "Auditioner.h"
#include "Project.h"
#include "Settings.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
    class SolarisService
    {
    public:
        struct Host
        {
            /** Decode `path` to PCM at `rate` (1 or 2 channels). False + `err` if it cannot. */
            std::function<bool(const std::string &path, int rate, engine::Pcm &out, std::string &err)> decodeAudio;
            /** Write `channels` (equal lengths) as a WAV at `rate`; bits 24 = PCM, 32 = float. */
            std::function<bool(const std::string &path, const std::vector<std::vector<float>> &channels, int rate,
                               int bits, std::string &err)> writeWav;
            /** The entries of a folder (no "." / ".."). False + `err` if it cannot be listed. */
            std::function<bool(const std::string &path, std::vector<BrowserEntry> &out, std::string &err)> listDir;
            /** The machine's audio devices, inputs and outputs. */
            std::function<bool(std::vector<DeviceInfo> &out, std::string &err)> listDevices;
            /** A stream to the clock device (R-PLAY-1). Empty = this build cannot play. */
            std::function<std::unique_ptr<IAudioOut>()> audioOut;
            std::string settingsPath; // the machine's settings file ("" = not persisted)
            std::string recentsPath;  // the recent songs, one path per line ("" = not persisted)
        };

        explicit SolarisService(Host host = Host());
        ~SolarisService();
        SolarisService(const SolarisService &other);   // a copy never plays (getAddress probes on one)

        bool dispatchText(const std::string &line, std::string &err);
        bool dispatch(const Command &c, std::string &err);

        const AppModel &model() const { return mModel; }
        /** Bring the transport and meters into the model, free engines the player handed back. A
         *  host calls it on a timer while playing; `wait` calls it; it emits nothing. */
        void pump();
        bool auditionCommand(const Command &c, std::string &err);
        const std::string &output() const { return mOutput; }
        void subscribe(std::function<void(const Event &)> sink) { mSinks.push_back(std::move(sink)); }
        /** The open document (null on Home) — for tests and the host's title bar. */
        const Project *project() const { return mOpen ? &mProject : nullptr; }

    private:
        // dispatch groups (ServiceEdit.cpp, ServiceModel.cpp, ServiceRender.cpp)
        bool projectCommand(const Command &c, std::string &err);
        bool setAddress(const std::string &address, const std::string &value, std::string &stored, std::string &err);
        bool getAddress(const std::string &address, std::string &value, std::string &err) const;
        bool mixCommand(const Command &c, std::string &err);
        bool clipCommand(const Command &c, std::string &err);
        bool render(const Command &c, std::string &err);
        bool machineCommand(const Command &c, std::string &err);
        bool transportCommand(const Command &c, std::string &err);
        bool autoCommand(const Command &c, std::string &err);   // ServiceAuto.cpp
        bool historyCommand(const Command &c, std::string &err); // undo / redo
        std::string labelOf(const Command &c) const;
        bool evalCommand(const Command &c, std::string &err);
        double songEndBeats() const;
        std::vector<std::string> readersOf(const std::string &name) const; // addresses whose formula reads `name`
        void dropBindingsOf(const std::set<std::string> &nodes);         // the formulas driving these nodes go
        void liveUpdate(const Command &c);            // after an edit while playing: live messages or an engine swap
        bool buildLive(std::unique_ptr<engine::Engine> &out, std::string &err);
        void stopPlayer();
        void loadMachine();
        bool saveSettings(std::string &err);
        void touchRecent(const std::string &path);
        void refreshModel();
        std::vector<std::string> audit() const;
        std::string matrixText(bool json) const;

        // helpers
        std::string resolvePath(const std::string &src) const;      // a clip's src → a path the host opens
        std::string relativePath(const std::string &file) const;    // a file → what a clip stores
        std::shared_ptr<const engine::Pcm> pcmFor(const std::string &src);
        /** R-EDM-8: a sampler's sound — resolved as a clip's src is, and refused if it cannot be read. */
        bool resolveSample(const std::string &given, std::string &stored, std::string &err);
        std::string defaultOutFor(const std::string &mixerId) const; // the first bus on a later mixer
        std::string firstMixer() const;
        std::string secondMixer() const;
        void emit(const Event &e);
        void changed(const std::string &what, const std::string &node);
        bool requireOpen(std::string &err) const;

        Host mHost;
        Project mProject;
        bool mOpen = false;
        std::string mPath;
        AppModel mModel;
        std::string mOutput;
        std::vector<std::function<void(const Event &)>> mSinks;
        std::vector<Event> mPending; // an edit's events, emitted only once the whole command has landed
        bool mBindingsTouched = false; // a formula or an automation changed: playback needs a new engine
        struct Step { Project project; std::string label; };
        std::vector<Step> mUndo, mRedo;  // the song before each edit that landed (R-EDM-1)
        std::string mCoalesce;           // the newest step's merge key: `set` of the same addresses
        static constexpr size_t kHistory = 200;
        std::map<std::string, std::string> mLastChanged; // device id → the parameter last written (R-WIN-2)
        std::map<std::string, std::shared_ptr<const engine::Pcm>> mPcm; // by resolved path, at mPcmRate
        std::set<std::string> mOffline;                                 // resolved paths that would not decode
        int mPcmRate = 0;
        std::vector<std::string> mAudit, mLastRenderPeaks;
        Settings mSettings;
        std::vector<std::string> mRecents;
        std::vector<RecentModel> mRecentCards; // parsed from the files: rebuilt only when stale
        bool mRecentsStale = true;
        std::vector<DeviceInfo> mDevices;
        BrowserModel mBrowser;
        std::unique_ptr<Player> mPlayer;
        std::unique_ptr<Auditioner> mAudition;   // R-EDM-9: the browser's preview, its own stream
        std::vector<std::string> mLiveStrips;                         // engine strip index → strip id
        std::map<std::string, std::pair<int, int>> mLiveDevices;      // device id → (strip index, rack index)
        double mPosition = 0, mLoopFrom = 0, mLoopTo = 0;             // beats
    };
}
}
