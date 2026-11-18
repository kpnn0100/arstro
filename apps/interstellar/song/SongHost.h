/*
 *  interstellar_song — a Solaris song hosted inside Interstellar (R-SONG), the way the rack hosts a
 *  Cosmo project (R-RACK-1).
 *
 *  The ONE adapter between the two apps' cores. It links Solaris's service privately and exposes only
 *  the types below, so nothing in Interstellar's core includes a Solaris header: the two apps both have
 *  an `AppModel.h`, a `Command.h`, an `Event.h` and a `Json.h`, and a translation unit that saw both
 *  would see whichever its include path found first. Here it sees Solaris's; the rest of Interstellar
 *  sees this file. (Solaris's AppModel is named below only by a forward declaration: the Song page's
 *  bridge — the one other place that sees Solaris — draws Solaris's own window from it.)
 *
 *  What it holds is a real `SolarisService` with the song open — the whole DAW project, every version,
 *  strip, device and note — so an edit is a Solaris command line, checked by Solaris's own grammar and
 *  recorded in the song's own history, and a save is Solaris's `project save`, writing the `.slp` the
 *  user opened (never a copy, never an `.isp`).
 *
 *  What a clip HEARS is the song played live (R-SONG-2, amended 2026-11-18): a `Program` is a version's
 *  mix compiled by Solaris (`compileVersion`, what `render --version` compiles) from the song as it
 *  stands; a `Stream` is an engine built from it that renders block by block as Interstellar reads.
 *  No render, no file and no wave in between — so an edit is heard from the next block read, and the
 *  samples are the ones `solaris-cc render` writes.
 */
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris { struct AppModel; }   // Solaris's: complete only where Solaris's headers are seen
namespace interstellar
{
    class IAudioOut;   // Interstellar's own sound output (core/AudioOut.h)

    class SongHost
    {
    public:
        /** A section of the arrangement: a marker and where its section ends (Solaris R-ARR-1). */
        struct Section
        {
            std::string id, name;
            double fromBeat = 0, toBeat = 0;     // beats
            double fromSec = 0, toSec = 0;       // where they are heard, through the song's tempo map
        };
        /** A version of the song (Solaris R-VER); "main" is the root and always first. */
        struct Version
        {
            std::string id, name, base;
            bool frozen = false;
            std::string problem;                 // why it does not stand as a song ("" when it does)
        };
        /** A lane of the arrangement as a clip draws it (R-SONG-8): its clips' spans, seconds into the song. */
        struct Lane
        {
            std::string id, name;
            int colour = -1;                     // Solaris's lane colour index (−1 = its default)
            std::vector<std::pair<double, double>> spans;
        };
        struct Facts
        {
            std::string path, name;              // the .slp, absolute; the song's name
            double bpm = 0, lengthBeats = 0, lengthSeconds = 0;
            int rate = 48000;                    // the song's sample rate
            std::string open = "main";           // the version open in the hosted song
            std::vector<Version> versions;       // main first
            std::vector<Section> sections;       // the open version's, by time
            std::vector<Lane> lanes;             // the open version's, in order
            bool dirty = false;
            std::string lastError;
        };
        /** What a clip plays: a version's mix, compiled from the song as it stood, and the span of it heard (a
         *  section, or the whole song). Immutable — shared with any thread. */
        struct Program;
        /** A Program played: its own engine. Reads are sequential in the common case (playback, a delivery)
         *  and continue the engine; a read elsewhere seeks it (instruments silent from there, as Solaris's own
         *  seek). One thread at a time. */
        class Stream
        {
        public:
            virtual ~Stream() = default;
            virtual int rate() const = 0;          // the song's own
            virtual double seconds() const = 0;    // the span
            /** `frames` stereo frames from frame `start` of the span into `stereo` (2 × frames floats); silence
             *  before the span and past its end. */
            virtual void read(long long start, int frames, float *stereo) = 0;
        };
        /** Decode a file the song names (a sample) to interleaved stereo at `rate`. */
        using Decode = std::function<bool(const std::string &path, int rate, std::vector<float> &stereo, long long &frames, std::string &err)>;
        /** Interstellar's sound output — what the Song page's own transport plays through (Solaris's player). */
        using AudioOut = std::function<std::unique_ptr<IAudioOut>()>;

        explicit SongHost(Decode decode, AudioOut audioOut = AudioOut());
        ~SongHost();
        SongHost(const SongHost &) = delete;
        SongHost &operator=(const SongHost &) = delete;

        /** Open `path` (a Solaris song). False + err when it is not one. */
        bool open(const std::string &path, std::string &err);
        /** One Solaris command line on the song — its grammar, its refusals, one step of its history.
         *  `output` is what the line printed; `events` its event lines, formatted as Solaris formats them. */
        bool run(const std::string &line, std::string &output, std::vector<std::string> &events, std::string &err);
        /** Write the song back to its own file (Solaris's `project save`). */
        bool save(std::string &err);
        /** The transport, the meters and the file watch (a save from a Solaris window, Solaris R-COLLAB-1). Cheap. */
        void pump();

        Facts facts() const;
        /** Another version's facts — its own sections, lanes and length — without opening it here. */
        bool factsOf(const std::string &version, Facts &out, std::string &err) const;
        /** The span a clip of `version` ("" or "main" = main) hears: the whole song, or one `section` (a marker's
         *  id or name). False + err when the version or the section is not the song's, or it is empty. */
        bool span(const std::string &version, const std::string &section, double &seconds, std::string &err) const;
        /** A fingerprint of the song's text (every version's): it changes exactly when what a clip hears could. */
        unsigned long long fingerprint() const;
        /** The hosted service's model revision: moves on any change, the playhead's too (cheap; the fingerprint is
         *  what says whether the song itself changed). */
        long long revision() const;
        /** `version` and `span` compiled now. Null + err as `span`, or when Solaris cannot compile it. */
        std::shared_ptr<const Program> program(const std::string &version, const std::string &section, std::string &err) const;
        /** A stream of `p`, its engine built on THIS thread. Null + err when the engine refuses the graph. */
        static std::unique_ptr<Stream> stream(const std::shared_ptr<const Program> &p, std::string &err);

        /** The hosted service's model, for Solaris's own window drawn inside Interstellar (the Song page). */
        const solaris::AppModel &model() const;
        const std::string &path() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> d;
    };
}
}
