// solaris_service tests — the tables (L1) and the REAL service driven through text lines with a
// fake decoder and WAV writer (L2) (R-SVC-1…3, R-MIX-2/3/4/7/8/9/10, R-CLIP-2/3, R-RENDER-1…3,
// R-API-1). Nothing reaches past `dispatchText`: what a test can do, the CLI and the UI can do.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "ApiDoc.h"
#include "AppModelCodec.h"
#include "Command.h"
#include "Event.h"
#include "Format.h"
#include "SolarisService.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

using namespace arstro::solaris;
namespace fs = std::filesystem;

namespace
{
    int passed = 0;
    void pass(const char *name)
    {
        std::printf("[PASS] %s\n", name);
        ++passed;
    }
    bool contains(const std::string &s, const std::string &part) { return s.find(part) != std::string::npos; }

    std::string scratch()
    {
        const char *t = std::getenv("SOLARIS_TEST_DIR");
        fs::path d = t ? fs::path(t) : fs::temp_directory_path() / "solaris_service_tests";
        fs::create_directories(d);
        return d.string();
    }

    // The fake host: any path decodes to half a second of a 1 kHz sine at 0.5, except one that
    // says "missing"; written WAVs are kept in memory.
    // A clock device that keeps what it is given: frames land in `sink`, counted in `frames`
    // (read the sink only after `transport stop` has joined the player).
    struct Capture
    {
        std::vector<float> sink;
        std::atomic<long long> frames{0};
    };
    struct FakeOut : IAudioOut
    {
        std::shared_ptr<Capture> cap;
        bool open(const std::string &, int, int, int, std::string &) override { return true; }
        bool write(const float *x, int n) override
        {
            cap->sink.insert(cap->sink.end(), x, x + 2 * n);
            cap->frames.fetch_add(n);
            std::this_thread::sleep_for(std::chrono::microseconds(300)); // a fast, but not runaway, device
            return true;
        }
        double latency() override { return 0.0; }
    };

    struct Fake
    {
        std::string settings, recents; // "" = not persisted
        std::shared_ptr<Capture> capture = std::make_shared<Capture>();
        bool canPlay = true;
        std::map<std::string, std::vector<std::vector<float>>> written;
        std::map<std::string, int> bits;
        SolarisService::Host host()
        {
            SolarisService::Host h;
            h.decodeAudio = [](const std::string &path, int rate, engine::Pcm &out, std::string &err) {
                if (contains(path, "missing")) { err = "no such file"; return false; }
                out.channels = 2;
                out.frames = rate / 2;
                out.samples.resize((size_t)out.frames * 2);
                for (long long i = 0; i < out.frames; ++i)
                    out.samples[(size_t)i * 2] = out.samples[(size_t)i * 2 + 1] = (float)(0.5 * std::sin(2 * M_PI * 1000.0 * i / rate));
                return true;
            };
            h.writeWav = [this](const std::string &path, const std::vector<std::vector<float>> &ch, int, int b, std::string &) {
                written[path] = ch;
                bits[path] = b;
                return true;
            };
            h.listDir = [](const std::string &path, std::vector<BrowserEntry> &out, std::string &err) {
                if (contains(path, "nowhere")) { err = "no such folder"; return false; }
                out = {BrowserEntry{"kick.wav", path + "/kick.wav", "audio"}, BrowserEntry{"Loops", path + "/Loops", "dir"},
                       BrowserEntry{"notes.txt", path + "/notes.txt", "other"}, BrowserEntry{"a.slp", path + "/a.slp", "song"}};
                return true;
            };
            h.listDevices = [](std::vector<DeviceInfo> &out, std::string &) {
                out = {DeviceInfo{"card_a", "Speakers", "out", 2, 48000}, DeviceInfo{"card_b", "Headphones", "out", 2, 44100},
                       DeviceInfo{"mic", "Mic", "in", 1, 48000}};
                return true;
            };
            h.settingsPath = settings;
            h.recentsPath = recents;
            if (canPlay)
                h.audioOut = [this] {
                    auto o = std::make_unique<FakeOut>();
                    o->cap = capture;
                    return std::unique_ptr<IAudioOut>(std::move(o));
                };
            return h;
        }
    };

    struct Run
    {
        Fake fake;
        SolarisService svc;
        std::vector<std::string> events;
        Run() : Run(std::string(), std::string()) {}
        Run(const std::string &settings, const std::string &recents) : fake{settings, recents}, svc(fake.host())
        {
            svc.subscribe([this](const Event &e) { events.push_back(formatEvent(e)); });
        }
        std::string ok(const std::string &line)
        {
            std::string err;
            if (!svc.dispatchText(line, err)) std::printf("    refused `%s`: %s\n", line.c_str(), err.c_str());
            assert(err.empty());
            return svc.output();
        }
        std::string no(const std::string &line)
        {
            std::string err;
            const bool okay = svc.dispatchText(line, err);
            if (okay) std::printf("    accepted `%s` (expected a refusal)\n", line.c_str());
            assert(!okay && !err.empty());
            return err;
        }
        std::string text() const { return serializeProject(*svc.project()); }
        const StripModel *strip(const std::string &id) const
        {
            for (const auto &s : svc.model().strips)
                if (s.id == id) return &s;
            return nullptr;
        }
        const ClipModel *clip(const std::string &id) const
        {
            for (const auto &c : svc.model().clips)
                if (c.id == id) return &c;
            return nullptr;
        }
    };

    std::string freshSong(const std::string &name)
    {
        const std::string p = scratch() + "/" + name + ".slp";
        fs::remove(p);
        return p;
    }
}

// ── L1: the tables ───────────────────────────────────────────────────────────────────────────

static void test_grammar_table_parses_and_rejects()
{
    std::set<std::string> verbs;
    for (const auto &s : commandSpecs())
    {
        assert(verbs.insert(s.verb).second);                         // one row per verb
        assert(!s.summary.empty() && s.requirement.rfind("R-", 0) == 0);
    }
    std::string err;
    Command c = parseCommand("clip add --src \"my kick.wav\" --at=4 --lane ln_1", err);
    assert(err.empty() && c.kind == Command::Kind::ClipAdd && c.flag("src") == "my kick.wav" && c.flag("at") == "4");
    assert(formatCommand(c) == "clip add --src \"my kick.wav\" --at 4 --lane ln_1");
    c = parseCommand("send add ch_2 --to ch_1 --pre", err);
    assert(err.empty() && c.has("pre") && c.arg(0) == "ch_2");
    c = parseCommand("set ch_2.gain=-3 dv_1.filter.cutoff=800", err);
    assert(err.empty() && c.fields.size() == 2 && c.fields[1].first == "dv_1.filter.cutoff");
    parseCommand("strp add", err);
    assert(contains(err, "unknown command") && contains(err, "strip add"));
    parseCommand("clip add --sorce a.wav", err);
    assert(contains(err, "does not take --sorce") && contains(err, "--src"));
    parseCommand("send add ch_2 --pre=1", err);
    assert(contains(err, "is a switch"));
    parseCommand("clip add --src", err);
    assert(contains(err, "needs a value"));
    parseCommand("route", err);
    assert(contains(err, "too few arguments"));
    parseCommand("set gain", err);
    assert(contains(err, "expected <address>=<value>"));
    c = parseCommand("   # a comment", err);
    assert(err.empty() && c.kind == Command::Kind::None);
    pass("the grammar is a table: each verb once; unknown verbs and flags refused with the nearest (R-SVC-3)");
}

static void test_event_and_model_tables()
{
    std::set<std::string> names;
    for (const auto &e : eventSpecs()) assert(names.insert(e.name).second);
    Event e(Event::Kind::ParamsChanged);
    e.with("address", "ch_2.gain").with("value", "-3.0");
    assert(formatEvent(e) == "[evt] params.changed address=ch_2.gain value=-3.0");
    std::set<std::string> paths;
    for (const auto &f : appModelFields()) assert(paths.insert(f.path).second);
    pass("event names unique; an event formats as its log line");
}

// ── L2: the real service ─────────────────────────────────────────────────────────────────────

static void test_a_new_song_has_its_mixers_and_round_trips()
{
    Run r;
    const std::string path = freshSong("new");
    r.ok("project new " + path + " --bpm 128 --name \"Night Drive\"");
    const AppModel &m = r.svc.model();
    assert(m.screen == "project" && m.projectName == "Night Drive" && m.bpm == 128.0 && !m.dirty);
    assert(m.mixers.size() == 2 && m.mixers[0].name == "Sources" && m.mixers[1].name == "Buses");
    assert(m.mixers[1].strips == std::vector<std::string>{"ch_1"} && r.strip("ch_1")->name == "Main" && r.strip("ch_1")->out == "master");
    assert(r.no("project new " + path) == path + " already exists — open it, or choose another name");
    const std::string before = r.text();
    r.ok("project close");
    assert(r.svc.model().screen == "home" && !r.svc.project());
    r.ok("project open " + path);
    assert(r.text() == before);
    assert(r.no("strip add --kind synth") == "--kind must be audio, instrument or bus");
    assert(contains(r.no("project new " + freshSong("x") + " --bpm 5"), "--bpm"));
    pass("project new: Sources, Buses with Main → master; saved, closed, reopened byte-identical");
}

static void test_every_sample_file_gets_its_own_strip()
{
    Run r;
    r.ok("project new " + freshSong("samples"));
    assert(r.ok("clip add --src kick.wav --at 0") == "ac_1\n");
    const StripModel *kick = r.strip("ch_2");
    assert(kick && kick->name == "kick" && kick->kind == "audio" && kick->mixer == "mx_1" && kick->out == "ch_1"); // Sources → Main
    assert(r.clip("ac_1")->lane == "ln_1" && r.svc.model().lanes[0].name == "kick");
    r.ok("clip add --src hat.wav --at 1");
    assert(r.strip("ch_3") && r.strip("ch_3")->name == "hat");          // a new file: a new strip
    r.ok("clip add --src kick.wav --at 4 --lane ln_2");
    assert(r.clip("ac_3")->track == "ch_2" && r.svc.model().strips.size() == 3); // the same file: the same strip
    assert(r.strip("ch_2")->clipCount == 2 && r.strip("ch_2")->fromLanes == std::vector<std::string>({"ln_1", "ln_2"}));
    assert(r.strip("ch_1")->fromStrips == std::vector<std::string>({"ch_2", "ch_3"}));        // fed by (R-MIX-8)
    assert(std::fabs(r.clip("ac_1")->length - 0.5 * 120 / 60) < 1e-9);                       // half a second at 120 bpm = 1 beat
    assert(contains(r.no("clip add --src missing.wav"), "cannot read missing.wav"));
    assert(contains(r.no("clip add --strip ch_2"), "notes need an instrument strip"));
    assert(r.no("clip add --at 3") == "clip add needs --src <file> (audio), --strip <instrument strip> or --instrument <type> (notes)");
    pass("every sample file gets its own strip on Sources → Main; the same file reuses it; fed-by is computed (R-MIX-2/3/8)");
}

static void test_an_instrument_drop_is_one_command_and_a_refusal_says_nothing_changed()
{
    Run r;
    r.ok("project new " + freshSong("instdrop"));
    r.events.clear();
    assert(r.ok("clip add --instrument drums --at 2 --length 4") == "ac_1\n"); // what dropping "Drum Machine" sends
    const StripModel *d = r.strip("ch_2");
    assert(d && d->kind == "instrument" && d->name == "Drum Machine" && d->mixer == "mx_1" && d->out == "ch_1");
    assert(d->devices.size() == 1 && d->devices[0].type == "drums");
    const ClipModel *c = r.clip("ac_1");
    assert(c && c->track == "ch_2" && c->kind == "note" && c->at == 2.0 && c->length == 4.0 && c->lane == "ln_1");
    assert(r.events.size() == 2 && contains(r.events[0], "what=strip.added node=ch_2") && contains(r.events[1], "what=clip.added node=ac_1"));

    const std::string before = r.text();
    r.events.clear();
    assert(contains(r.no("clip add --instrument flute"), "`flute` is not an instrument (instruments: synth drums)"));
    assert(contains(r.no("clip add --instrument synth --strip ch_2"), "without --src or --strip"));
    assert(contains(r.no("clip add --instrument synth --length -1"), "--length"));   // the strip was made, then the clip refused
    assert(r.text() == before && r.events.size() == 3);                                // three rejections, no change
    r.ok("clip add --src kick.wav");
    r.events.clear();
    assert(contains(r.no("clip add --src snare.wav --in -1"), "--in"));              // likewise: a new strip, then a refusal
    for (const auto &e : r.events) assert(!contains(e, "project.changed"));            // nothing said it changed…
    assert(r.svc.model().strips.size() == 3);                                           // …and nothing did
    // a strip's colour is its own or comes from its id, so one deleted recolours nobody else
    const int kick = r.strip("ch_3")->colour;
    assert(kick >= 0 && r.strip("ch_2")->colour != kick);
    r.ok("strip delete ch_2 --with-clips");
    assert(r.strip("ch_3")->colour == kick);
    pass("clip add --instrument: a new instrument strip and an empty note clip in ONE command; a refused edit emits no change (R-BROWSE-3)");
}

static void test_a_relative_src_is_found_in_the_songs_folder()
{
    Run r;
    const std::string dir = scratch() + "/relsong";
    fs::remove_all(dir);
    fs::create_directories(dir + "/samples");
    std::ofstream(dir + "/samples/snare.wav") << "x";                   // the fake decoder reads any path that exists or not
    r.ok("project new " + dir + "/song.slp");
    r.ok("clip add --src " + dir + "/samples/snare.wav");
    assert(r.clip("ac_1")->src == "samples/snare.wav");                 // stored relative to the song
    r.ok("clip add --src samples/snare.wav --at 4");                    // what the browser's Song tab hands back
    assert(r.clip("ac_2")->track == r.clip("ac_1")->track);            // the SAME file: the same strip
    assert(r.svc.model().strips.size() == 2 && r.svc.model().deviceTypes.size() == 9 && r.svc.model().deviceTypes[0].kind == "instrument");
    pass("a relative src names a file in the song's folder first — the Song tab re-places a sound with one `clip add`");
}

static void test_note_move_and_quantize_edit_the_pattern()
{
    Run r;
    r.ok("project new " + freshSong("roll"));
    r.ok("clip add --instrument drums --at 0 --length 4");                     // ch_2, pt_1
    r.ok("note add pt_1 --pitch 36 --at 0");
    r.ok("note add pt_1 --pitch 38 --at 1 --length 0.5 --vel 90");
    // one gesture, one line: move in time and pitch, resize, re-velocity
    r.ok("note move pt_1 --pitch 38 --at 1 --to-pitch 40 --to-at 1.5");
    r.ok("note move pt_1 --pitch 40 --at 1.5 --length 1 --vel 120");
    const auto &n = r.svc.model().patterns[0].notes;
    assert(n.size() == 2 && n[1].pitch == 40 && n[1].at == 1.5 && n[1].length == 1.0 && n[1].vel == 120);
    assert(contains(r.no("note move pt_1 --pitch 40 --at 1.5 --to-pitch 36 --to-at 0"), "a note is already at 36"));
    assert(contains(r.no("note move pt_1 --pitch 41 --at 1.5 --length 2"), "no note 41"));
    assert(contains(r.no("note move pt_1 --pitch 40 --at 1.5"), "needs --to-pitch, --to-at, --length and/or --vel"));
    // quantize: onto the grid; swing pushes every second step late; notes landing together merge
    r.ok("note add pt_1 --pitch 42 --at 0.27");
    r.ok("note add pt_1 --pitch 42 --at 0.49");
    r.ok("note add pt_1 --pitch 42 --at 0.51 --vel 127");
    r.ok("pattern quantize pt_1 --grid 0.25 --swing 0.5");
    std::vector<double> hats;
    int loud = 0;
    for (const auto &x : r.svc.model().patterns[0].notes)
        if (x.pitch == 42) { hats.push_back(x.at); loud = std::max(loud, x.vel); }
    // 0.27 → step 1 (odd, +0.125) = 0.375; 0.49 and 0.51 → step 2 (even) = 0.5, merged, the louder kept
    assert(hats == std::vector<double>({0.375, 0.5}) && loud == 127);
    assert(contains(r.no("pattern quantize pt_1 --swing 0.9"), "--swing must be 0 to 0.75"));
    // what a piano roll needs: the kit's key names, the pattern's strip and instrument
    const auto &m = r.svc.model();
    const auto &pm = m.patterns[0];
    assert(pm.strip == "ch_2" && pm.instrument == "drums");
    bool kick = false;
    for (const auto &t : m.deviceTypes)
        for (const auto &nn : t.noteNames) kick |= t.name == "drums" && nn.note == 36 && nn.name == "Kick";
    assert(kick);
    pass("note move: one line moves, resizes, re-velocities, refused onto a note; pattern quantize with swing merges collisions; the kit's keys are named (R-ROLL-2/4)");
}

static void test_strip_relink_moves_every_clip_in_one_edit()
{
    Run r;
    r.ok("project new " + freshSong("relink"));
    r.ok("clip add --instrument synth --at 0 --length 4");                 // ch_2
    r.ok("clip duplicate " + r.svc.model().clips[0].id);                    // a second clip, linked
    r.ok("strip add --kind instrument --instrument synth --name Lead");     // ch_3: the new line
    r.ok("strip add --kind bus --name Verb");                               // ch_4
    r.ok("strip add --kind audio --name Vox");                              // ch_5
    assert(r.ok("strip relink ch_2 --to ch_3") == "2\n");
    for (const auto &c : r.svc.model().clips) assert(c.track == "ch_3");
    assert(r.svc.model().patterns[0].strip == "ch_3");
    assert(contains(r.no("strip relink ch_3 --to ch_4"), "a bus plays no clips"));
    assert(contains(r.no("strip relink ch_3 --to ch_5"), "need an instrument strip"));
    assert(contains(r.no("strip relink ch_2 --to ch_3"), "has no clips to move"));
    assert(contains(r.no("strip relink ch_3 --to ch_3"), "already play through"));
    assert(contains(r.no("strip relink ch_3 --to ch_9"), "no strip `ch_9`"));
    assert(contains(r.no("strip relink ch_3"), "needs --to"));
    // ONE edit: one undo puts every clip back
    assert(r.ok("undo") == "strip relink ch_2\n");
    for (const auto &c : r.svc.model().clips) assert(c.track == "ch_2");
    pass("strip relink: every clip of a strip to another line in one edit (one undo); refused onto a bus, across kinds, onto itself, with nothing to move (R-MIX-14)");
}

static void test_undo_and_redo_every_edit()
{
    Run r;
    const std::string song = freshSong("undo");
    r.ok("project new " + song);
    const std::string empty = r.text();
    r.ok("clip add --instrument drums --at 0 --length 4");                        // step 1
    const std::string withDrums = r.text();
    for (const char *g : {"-1", "-2", "-3"}) r.ok(std::string("set ch_2.gain=") + g); // a fader dragged: ONE step
    const std::string dragged = r.text();
    r.ok("set ch_2.pan=0.5");                                                       // step 3
    const auto &m = r.svc.model();
    assert(m.undoDepth == 3 && m.undoLabel == "set ch_2.pan" && m.redoDepth == 0);
    r.events.clear();
    assert(r.ok("undo") == "set ch_2.pan\n" && r.text() == dragged);
    assert(r.events.size() == 1 && contains(r.events[0], "what=undo node=\"set ch_2.pan\""));
    assert(r.ok("undo") == "set ch_2.gain\n" && r.text() == withDrums);           // the whole drag, in one
    assert(r.ok("redo") == "set ch_2.gain\n" && r.text() == dragged);
    r.ok("undo");
    assert(r.ok("undo") == "clip add\n" && r.text() == empty);
    assert(contains(r.no("undo"), "nothing to undo"));
    assert(r.ok("redo") == "clip add\n" && r.text() == withDrums && r.svc.model().redoDepth == 2);
    // a new edit clears what redo could put back
    r.ok("lane add Extra");
    assert(r.svc.model().redoDepth == 0 && contains(r.no("redo"), "nothing to redo"));
    // not edits: a machine setting, the transport, a save
    const int depth = r.svc.model().undoDepth;
    r.ok("settings set bufferSize=256");
    r.ok("transport seek 4");
    r.ok("project save");
    assert(r.svc.model().undoDepth == depth);
    // automation is undone like anything else
    r.ok("auto create ch_2.gain");
    r.ok("undo");
    assert(r.svc.model().automations.empty() && r.svc.model().bindings.empty());
    // another song: its own history
    r.ok("project open " + song);
    assert(r.svc.model().undoDepth == 0 && contains(r.no("undo"), "nothing to undo"));
    pass("undo / redo: every edit, byte-exact; a dragged fader is one step; a new edit clears redo; settings, transport and save are not edits (R-EDM-1)");
}

static void test_formulas_bind_numbers_and_refuse_what_cannot_be_read()
{
    Run r;
    r.ok("project new " + freshSong("formulas"));
    r.ok("clip add --instrument synth --at 0 --length 8");             // ch_2, its synth dv_1, pt_1
    // automation FROM a property: one command, named after its owner and parameter, ranged as it, bound to it
    assert(r.ok("auto create dv_1.filter.cutoff") == "au_1\n");
    {
        const auto &m = r.svc.model();
        assert(m.automations.size() == 1);
        const auto &a = m.automations[0];
        assert(a.name == "Basic Synth \xC2\xB7 Cutoff" && a.unit == "Hz" && a.min == 20.0 && a.max == 20000.0 && a.from == "dv_1.filter.cutoff");
        assert(a.points.size() == 2 && a.points[0].at == 0.0 && a.points[1].at >= 8.0 && a.points[0].value == a.points[1].value);
        assert(a.usedBy == std::vector<std::string>{"dv_1.filter.cutoff"});
        assert(m.bindings.size() == 1 && m.bindings[0].formula == "=au_1" && m.bindings[0].ok);
        const auto &dv = r.strip("ch_2")->devices[0];
        bool bound = false;
        for (const auto &pm : dv.params) bound |= pm.name == "filter.cutoff" && pm.formula == "=au_1";
        assert(bound && dv.lastChanged == "filter.cutoff");
    }
    assert(r.ok("get dv_1.filter.cutoff") == "=au_1\n");                   // `get` prints what decides it
    // a formula with spaces, the clock, a link
    r.ok("set ch_2.pan=\"=0.25 * sin(beat * pi)\"");
    assert(r.ok("get ch_2.pan") == "=0.25 * sin(beat * pi)\n" && r.strip("ch_2")->panFormula == "=0.25 * sin(beat * pi)");
    assert(contains(r.no("set project.masterGain==ch_2.gain - 3"), "a formula with spaces is quoted"));
    r.ok("set project.masterGain=\"=ch_2.gain - 3\"");
    // what cannot be read is refused, naming it — and a refused line changes nothing and says nothing
    const std::string before = r.text();
    r.events.clear();
    assert(contains(r.no("set ch_2.gain==au_9"), "reads `au_9`, which is no automation"));
    assert(contains(r.no("set ch_2.gain==1+"), "the formula ends"));
    assert(contains(r.no("set ch_2.gain==foo(1)"), "unknown function `foo`"));
    assert(contains(r.no("set ch_2.gain==min(1)"), "`min` takes 2 values"));
    assert(contains(r.no("set dv_1.osc1.wave==1"), "is a choice"));
    assert(contains(r.no("set ch_2.mute==1"), "not a number a formula can drive"));
    assert(contains(r.no("set ch_2.gain==project.masterGain"), "a loop of links (")); // masterGain already reads ch_2.gain
    assert(r.text() == before);
    for (const auto &e : r.events) assert(!contains(e, "params.changed") && !contains(e, "project.changed"));
    // a plain number clears the formula and sets the value; `bind clear` keeps the value
    r.ok("set ch_2.pan=0.3");
    assert(r.strip("ch_2")->panFormula.empty() && r.strip("ch_2")->pan == 0.3 && r.ok("get ch_2.pan") == "0.3\n");
    r.ok("bind clear project.masterGain");
    assert(r.svc.model().masterGainFormula.empty() && contains(r.no("bind clear project.masterGain"), "has no formula"));
    // points: added sorted and clamped to the range, moved, shaped; refusals name the beat
    r.ok("auto point add au_1 --at 4 --value 99999 --shape smooth");
    assert(r.svc.model().automations[0].points.size() == 3 && r.svc.model().automations[0].points[1].value == 20000.0);
    assert(contains(r.no("auto point move au_1 --at 3 --value 1"), "no point at beat 3.0"));
    r.ok("auto point move au_1 --at 4 --to 2 --value 400");
    r.ok("auto point shape au_1 --at 2 --shape hold");
    assert(contains(r.no("auto point shape au_1 --at 2 --shape bounce"), "linear, hold or smooth"));
    const auto &pts = r.svc.model().automations[0].points;
    assert(pts[1].at == 2.0 && pts[1].value == 400.0 && pts[1].shape == "hold");
    // eval: the value at a beat, and why
    const std::string ex = r.ok("eval dv_1.filter.cutoff --at 3 --explain");
    assert(contains(ex, "dv_1.filter.cutoff = 400.0 Hz at beat 3.0") && contains(ex, "au_1 = 400.0") && contains(ex, "automation \""));
    // an automation read by a formula is not deleted out from under it
    assert(contains(r.no("auto delete au_1"), "read by dv_1.filter.cutoff"));
    // deleting what a formula drives takes the formula with it
    r.ok("strip delete ch_2 --with-clips");
    assert(r.svc.model().bindings.empty() && r.svc.model().automations[0].usedBy.empty());
    r.ok("auto delete au_1");
    assert(r.svc.model().automations.empty());
    pass("formulas: auto create names, ranges and binds; links, the clock, quoting; unreadable formulas and loops refused; numbers clear; eval explains (R-AUTO-1…5, 8)");
}

static void test_an_automated_gain_renders_its_curve()
{
    Run r;
    const std::string dir = scratch();
    const std::string song = freshSong("autorender");
    r.ok("project new " + song + " --bpm 120");
    r.ok("clip add --src tone.wav --at 0 --length 8");                 // a 1 kHz sine at 0.5, looped for 8 beats
    r.ok("auto create ch_2.gain");
    r.ok("auto point move au_1 --at 0 --value 0");
    r.ok("auto point add au_1 --at 8 --value -20");
    const std::string out = dir + "/auto.wav";
    r.ok("render --out " + out);
    const auto &L = r.fake.written[out][0];
    auto dbAround = [&](long long s) {
        double sum = 0;
        for (long long i = s - 1200; i < s + 1200; ++i) sum += (double)L[(size_t)i] * L[(size_t)i];
        return 20.0 * std::log10(std::sqrt(sum / 2400.0) / (0.5 / std::sqrt(2.0)));
    };
    const double b2 = dbAround(48000), b6 = dbAround(144000);
    if (std::fabs(b2 + 5.0) > 0.1 || std::fabs(b6 + 15.0) > 0.1) std::printf("    beat 2: %.3f dB, beat 6: %.3f dB\n", b2, b6);
    assert(std::fabs(b2 + 5.0) < 0.1 && std::fabs(b6 + 15.0) < 0.1);   // the curve, measured (R-AUTO-7)
    // the file keeps it, and a hand-edited formula that cannot be read is INERT, said in the model and the audit
    r.ok("project save");
    std::ifstream in(song);
    std::stringstream ss;
    ss << in.rdbuf();
    assert(contains(ss.str(), "#aauto id=au_1") && contains(ss.str(), "#point at=8.0 value=-20.0") && contains(ss.str(), "#abind address=ch_2.gain formula=\"=au_1\""));
    // a hand-edited formula that cannot be read opens, plays its own value, and says why — in the model and the audit
    std::string text = ss.str();
    text.replace(text.find("formula=\"=au_1\""), std::string("formula=\"=au_1\"").size(), "formula=\"=au_1 * nope\"");
    { std::ofstream(song) << text; }
    r.ok("project open " + song);
    const auto &bm = r.svc.model().bindings;
    assert(bm.size() == 1 && !bm[0].ok && contains(bm[0].problem, "reads `nope`"));
    const std::string audit = r.ok("audit");
    assert(contains(audit, "binding ch_2.gain: formula") && contains(audit, "inert: its own value plays"));
    assert(!contains(audit, "automation au_1"));                       // a formula names it, even an inert one
    r.ok("auto add --name Spare");
    assert(contains(r.ok("audit"), "automation au_2 (Spare) moves nothing — no formula reads it"));
    pass("an automated gain renders its curve: −5 dB at beat 2, −15 dB at beat 6 of a 0 → −20 dB ramp, within 0.1 dB; saved as #aauto/#abind; an unreadable formula is inert and audited");
}

static void test_routing_only_goes_forward_and_refusals_change_nothing()
{
    Run r;
    r.ok("project new " + freshSong("routes"));
    r.ok("clip add --src kick.wav");
    const std::string before = r.text();
    const std::string err = r.no("route ch_1 --to ch_2");               // Main (Buses) → kick (Sources)
    assert(contains(err, "ch_1 (Main, on Buses) output → ch_2 (kick, on Sources)") && contains(err, "R-MIX-4"));
    assert(r.text() == before);                                       // all-or-nothing
    assert(contains(r.no("send add ch_1 --to ch_2"), "R-MIX-4"));
    assert(contains(r.no("mixer move mx_2 --to 0"), "R-MIX-4"));      // Buses before Sources would turn every route back
    assert(r.text() == before);
    assert(contains(r.no("mixer delete mx_1"), "still holds ch_2"));
    r.ok("mixer add Stems");
    r.ok("strip add --kind bus --name Drums --mixer mx_3");
    assert(r.strip("ch_3")->out == "master");                         // nothing later than Stems: master
    r.ok("mixer move mx_3 --to 1");                                   // Sources, Stems, Buses
    r.ok("route ch_3 --to ch_1");                                     // Stems → Buses: forward
    r.ok("route ch_2 --to ch_3");
    assert(r.strip("ch_2")->out == "ch_3" && r.svc.model().mixers[1].name == "Stems");
    r.ok("route ch_2 --to prt_1");                                    // straight to a port
    r.ok("route ch_2 --to master");
    assert(r.strip("ch_2")->out == "master");
    // what a matrix or a picker may offer — the same rule, published (Sources, Stems, Buses)
    assert(r.strip("ch_2")->targets == std::vector<std::string>({"ch_3", "ch_1", "master", "prt_1"}));
    assert(r.strip("ch_3")->targets == std::vector<std::string>({"ch_1", "master", "prt_1"}));
    assert(r.strip("ch_1")->targets == std::vector<std::string>({"master", "prt_1"}));
    const std::vector<std::string> offered = r.strip("ch_2")->targets;                 // a copy: each command rebuilds the model
    for (const auto &t : offered) r.ok("route ch_2 --to " + t);                       // every one offered is taken
    pass("routing only goes forward — route, send, mixer move — and a refused command changes nothing; `targets` offers exactly that (R-MIX-4)");
}

static void test_set_and_get_through_the_registry()
{
    Run r;
    r.ok("project new " + freshSong("params"));
    assert(r.ok("strip add --kind instrument --instrument synth --name Lead") == "ch_2\n");
    const StripModel *lead = r.strip("ch_2");
    assert(lead->devices.size() == 1 && lead->devices[0].type == "synth" && lead->devices[0].instrument);
    r.events.clear();
    r.ok("set dv_1.filter.cutoff=800 dv_1.osc1.wave=sine ch_2.gain=-6");
    assert(r.events.size() == 3 && r.events[0] == "[evt] params.changed address=dv_1.filter.cutoff value=800.0");
    assert(r.events[1] == "[evt] params.changed address=dv_1.osc1.wave value=sine");
    assert(r.ok("get dv_1.filter.cutoff") == "800.0\n" && r.ok("get dv_1.osc1.wave") == "sine\n");
    assert(r.ok("get dv_1.filter.res") == "0.25\n");                   // not stored = the registry default
    assert(r.ok("set dv_1.filter.cutoff=99999").empty() && r.ok("get dv_1.filter.cutoff") == "20000.0\n"); // clamped by the spec
    assert(contains(r.no("set dv_1.filter.cutof=1"), "no field `filter.cutof` (did you mean: filter.cutoff?)"));
    assert(contains(r.no("set dv_1.osc1.wave=saww"), "one of: sine, saw, square, triangle"));
    assert(contains(r.no("set ch_2.gian=1"), "did you mean: gain?"));
    assert(contains(r.no("set ch_2.gain=40"), "between -120.0 and 12.0"));
    assert(contains(r.no("set ch_9.gain=1"), "no node `ch_9`"));
    // a line is atomic: the second address fails, so the first does not land and nothing is announced
    r.events.clear();
    r.no("set ch_2.gain=-1 ch_2.pan=7");
    assert(r.ok("get ch_2.gain") == "-6.0\n");
    for (const auto &e : r.events) assert(!contains(e, "params.changed"));
    assert(contains(r.svc.model().lastError, "pan must be between"));
    assert(contains(r.no("get ch_2.nothing"), "no field `nothing`"));
    r.ok("device add ch_2 --type eq");
    r.ok("set dv_2.peak2.gain=4.5 dv_2.bypass=true");
    const DeviceModel &eq = r.strip("ch_2")->devices[1];
    assert(eq.bypass && eq.label == "EQ");
    bool seen = false;
    for (const auto &p : eq.params)
        if (p.name == "peak2.gain") { seen = true; assert(p.value == 4.5 && p.text == "4.5" && p.unit == "dB" && p.max == 18.0); }
    assert(seen);
    assert(contains(r.no("device add ch_2 --type drums"), "ONE instrument"));
    assert(contains(r.no("device remove dv_1"), "keeps its instrument"));
    assert(contains(r.no("device add ch_2 --type flanger"), "no device type `flanger`"));
    r.ok("device add master --type compressor");
    assert(r.svc.model().masterDevices.size() == 1);
    pass("set/get read every device parameter from the DSP registry: clamped, choices by name, typos refused, lines atomic (R-DSP-2/3, R-SVC-3)");
}

static void test_patterns_are_shared_by_their_clips()
{
    Run r;
    r.ok("project new " + freshSong("patterns"));
    r.ok("strip add --kind instrument --instrument drums --name Drums");
    assert(r.ok("clip add --strip ch_2 --at 0 --length 16") == "ac_1\n");
    assert(r.clip("ac_1")->pattern == "pt_1" && r.clip("ac_1")->length == 16.0);
    r.ok("clip duplicate ac_1");
    assert(r.clip("ac_2")->at == 16.0 && r.clip("ac_2")->pattern == "pt_1" && r.clip("ac_2")->linked == 2);
    r.ok("note add pt_1 --pitch 36 --at 0");
    r.ok("note add pt_1 --pitch 42 --at 0.5 --vel 70");
    r.ok("note add pt_1 --pitch 36 --at 0 --vel 90");                 // the same note again replaces, not stacks
    assert(r.svc.model().patterns[0].notes.size() == 2 && r.svc.model().patterns[0].notes[0].vel == 90);
    r.ok("clip unique ac_2");
    assert(r.clip("ac_2")->pattern == "pt_2" && r.clip("ac_1")->linked == 1);
    r.ok("note delete pt_2 --pitch 42 --at 0.5");
    assert(r.svc.model().patterns[0].notes.size() == 2 && r.svc.model().patterns[1].notes.size() == 1);
    assert(contains(r.no("note delete pt_2 --pitch 42 --at 0.5"), "no note 42"));
    assert(contains(r.no("clip unique ac_9"), "no clip"));
    pass("a duplicated note clip plays the same pattern (linked); `clip unique` gives it its own (R-CLIP-2/3)");
}

static void test_solo_mute_matrix_and_audit()
{
    Run r;
    r.ok("project new " + freshSong("mix"));
    r.ok("clip add --src kick.wav");                                   // ch_2 → Main
    r.ok("clip add --src hat.wav");                                    // ch_3 → Main
    r.ok("strip add --kind bus --name Verb");                          // ch_4 on Buses → master
    r.ok("send add ch_3 --to ch_4 --gain -6 --pre");
    r.ok("set ch_2.solo=true");
    const AppModel &m = r.svc.model();
    auto audible = [&](const std::string &id) { return r.strip(id)->audible; };
    assert(audible("ch_2") && audible("ch_1") && !audible("ch_3") && !audible("ch_4")); // the solo's path stays alive
    r.ok("set ch_2.solo=false ch_3.solo=true");
    assert(audible("ch_3") && audible("ch_1") && audible("ch_4") && !audible("ch_2"));   // its send's return too
    r.ok("set ch_3.solo=false ch_2.mute=true");
    assert(!audible("ch_2") && audible("ch_3"));
    const std::string mx = r.ok("matrix print");
    assert(contains(mx, "strip\tch_1\tch_4\tmaster\tprt_1"));
    assert(contains(mx, "ch_3\t●\t-6.0pre\t\t"));
    assert(contains(r.ok("matrix print --json"), "\"ch_4\": \"-6.0pre\""));
    const std::string a = r.ok("audit");
    assert(contains(a, "silent: clip ac_1 plays through ch_2 (kick), which is muted"));
    assert(contains(a, "single input: bus ch_4 (Verb) is fed by one strip only"));
    assert(m.audit.size() >= 2);
    r.ok("strip add --kind audio --name Empty");
    assert(contains(r.ok("audit"), "unused: ch_5 (Empty) has no clips"));
    pass("solo keeps the soloed path audible; mute silences; the matrix and the audit read the mix (R-MIX-7/9/10)");
}

static void test_render_writes_the_mix_deterministically()
{
    Run r;
    const std::string dir = scratch();
    r.ok("project new " + freshSong("render") + " --bpm 120");
    r.ok("strip add --kind instrument --instrument drums --name Drums");
    r.ok("clip add --strip ch_2 --at 1 --length 4");                   // one bar of kicks from beat 1
    r.ok("note add pt_1 --pitch 36 --at 0");
    r.ok("note add pt_1 --pitch 36 --at 2");
    r.ok("set pt_1.length=4");
    r.events.clear();
    const std::string out = dir + "/mix.wav";
    r.ok("render --out " + out + " --stems ch_2,ch_1 --ports");
    assert(r.fake.written.count(out) && r.fake.written.count(dir + "/mix.ch_2.wav") && r.fake.written.count(dir + "/mix.Main.wav"));
    assert(r.fake.bits[out] == 24);
    const auto &L = r.fake.written[out][0];
    // beat 1 at 120 bpm = 24000 samples: silence before, the kick from there
    float before = 0, after = 0;
    for (int i = 0; i < 24000; ++i) before = std::max(before, std::fabs(L[i]));
    for (int i = 24000; i < 26400; ++i) after = std::max(after, std::fabs(L[i]));
    assert(before == 0.0f && after > 0.05f);
    // the tail ran past the end (beat 5 = 120000) and then stopped, under the 10 s cap
    assert(L.size() > 120000 && L.size() < 120000 + 480000);
    assert(contains(r.events.back(), "[evt] render.finished out=" + out));
    // the same song renders the same samples; --to is exact
    r.ok("render --out " + dir + "/again.wav");
    assert(r.fake.written[dir + "/again.wav"] == r.fake.written[out]);
    r.ok("render --out " + dir + "/bar.wav --from 1 --to 3 --bits 32f");
    assert(r.fake.written[dir + "/bar.wav"][0].size() == 48000 && r.fake.bits[dir + "/bar.wav"] == 32);
    assert(contains(r.no("render --out x.wav --stems ch_9"), "no strip"));
    assert(r.no("render") == "render needs --out <file.wav>");
    pass("render: the mixdown, stems and ports; silent before the first note, the tail to −90 dBFS, byte-identical twice (R-RENDER-1…3)");
}

static void test_the_dump_and_the_document()
{
    Run r;
    r.ok("project new " + freshSong("dump") + " --name Song");
    r.ok("clip add --src kick.wav");
    r.ok("strip add --kind instrument --instrument synth");
    r.ok("clip add --strip ch_3");
    r.ok("note add pt_1 --pitch 60 --at 0");
    r.ok("send add ch_2 --to prt_1");
    r.ok("device add master --type eq");
    const std::string stable = r.ok("state print --json --stable"), full = r.ok("state print --json");
    assert(!contains(stable, "\"revision\"") && contains(full, "\"revision\""));
    // every key the codec writes is a field the API document lists (the drift test of R-API-1)
    std::set<std::string> listed;
    for (const auto &f : appModelFields())
    {
        const auto dot = f.path.rfind('.');
        std::string last = dot == std::string::npos ? f.path : f.path.substr(dot + 1);
        if (last.size() > 2 && last.compare(last.size() - 2, 2, "[]") == 0) last.resize(last.size() - 2);
        listed.insert(last);
    }
    std::istringstream in(full);
    std::string line;
    while (std::getline(in, line))
    {
        const auto q = line.find('"');
        const auto c = line.find("\": ");
        if (q == std::string::npos || c == std::string::npos || c < q) continue;
        const std::string key = line.substr(q + 1, c - q - 1);
        if (!listed.count(key)) std::printf("    `%s` is written but not documented\n", key.c_str());
        assert(listed.count(key));
    }
    // two runs of the same lines give the same stable dump
    Run r2;
    r2.ok("project new " + freshSong("dump2") + " --name Song");
    r2.ok("clip add --src kick.wav");
    r2.ok("strip add --kind instrument --instrument synth");
    r2.ok("clip add --strip ch_3");
    r2.ok("note add pt_1 --pitch 60 --at 0");
    r2.ok("send add ch_2 --to prt_1");
    r2.ok("device add master --type eq");
    std::string s2 = r2.ok("state print --json --stable");
    const auto strip = [](const std::string &s) { // the two songs' FILE PATHS differ (and so their recents); nothing else may
        std::istringstream in(s);
        std::string line, out;
        while (std::getline(in, line))
            if (!contains(line, "\"projectPath\"") && !contains(line, "\"path\"")) out += line + "\n";
        return out;
    };
    assert(strip(stable) == strip(s2));
    const std::string json = r.ok("api --json"), md = r.ok("api --md");
    for (const auto &s : commandSpecs()) assert(contains(json, "\"verb\": \"" + s.verb + "\"") && contains(md, s.verb));
    for (const char *t : {"synth", "drums", "compressor", "eq", "reverb", "delay", "chorus", "drive", "filter"})
        assert(contains(json, std::string("\"type\": \"") + t + "\"") && contains(md, std::string("### `") + t + "`"));
    assert(contains(md, "| `filter.cutoff` | 20.0 … 20000.0 Hz | 2400.0 Hz |"));
    assert(r.ok("state print") == "screen project · Song · 120.0 bpm · 2 mixers · 3 strips · 2 lanes · 2 clips · unsaved\n");
    pass("state print --stable is deterministic and every key is documented; api lists every verb and every DSP parameter (R-API-1)");
}

static void test_the_machine_settings_folders_devices_and_recents()
{
    const std::string settings = scratch() + "/settings.txt", recents = scratch() + "/recents";
    fs::remove(settings);
    fs::remove(recents);
    {
        Run r(settings, recents);
        r.ok("settings set sampleRate=44100 bufferSize=512 output=card_a port.Phones=card_b:0");
        assert(r.svc.model().settings.sampleRate == 44100 && r.svc.model().settings.latencyMs == 11.6);
        assert(r.svc.model().settings.ports == std::vector<std::string>{"Phones=card_b:0"});
        assert(contains(r.no("settings set sampleRat=1"), "did you mean: sampleRate?"));
        assert(contains(r.no("settings set bufferSize=7"), "32 … 8192"));
        assert(contains(r.no("settings set port.Main=card_a"), "<device>:<first channel>"));
        r.ok("folder add /music/Samples");
        r.ok("folder add /music/Loops");
        r.ok("folder move /music/Loops --to 0");
        assert((r.svc.model().settings.folders == std::vector<std::string>{"/music/Loops", "/music/Samples"}));
        assert(contains(r.no("folder add /nowhere"), "cannot list /nowhere"));
        assert(contains(r.no("folder add /music/Loops"), "already a sample folder"));
        r.ok("folder remove /music/Loops");
        assert(r.ok("browse /music/Samples") == "dir\tLoops\nsong\ta.slp\naudio\tkick.wav\nother\tnotes.txt\n");
        assert(r.svc.model().browser.entries[0].kind == "dir");          // folders first
        assert(contains(r.ok("devices list"), "out\tcard_b\t2 ch · 44100 Hz\tHeadphones"));
        assert(r.svc.model().devices.size() == 3);
        // a new song starts at the machine's rate; it lands on Home's recent list
        const std::string a = freshSong("recent_a"), b = freshSong("recent_b");
        r.ok("project new " + a + " --name First");
        assert(r.svc.model().sampleRate == 44100);
        r.ok("project new " + b + " --name Second");
        r.ok("project close");
        const auto &cards = r.svc.model().recents;
        assert(cards.size() == 2 && cards[0].name == "Second" && cards[1].name == "First" && cards[0].strips == 1);
    }
    // the machine's side survives the process: a second service reads the same files
    Run again(settings, recents);
    assert(again.svc.model().settings.output == "card_a" && again.svc.model().settings.folders == std::vector<std::string>{"/music/Samples"});
    assert(again.svc.model().recents.size() == 2);
    again.ok("recents remove " + again.svc.model().recents[0].path);
    assert(again.svc.model().recents.size() == 1 && again.svc.model().recents[0].name == "First");
    fs::remove(again.svc.model().recents[0].path);
    Run third(settings, recents);
    assert(third.svc.model().recents[0].missing);                         // a deleted song is shown as missing
    pass("the machine: settings, sample folders, browse, devices, recents — saved, and read back by a second process (R-SET, R-HOME-1, R-BROWSE-1)");
}

namespace
{
    void waitFrames(Run &r, long long n)
    {
        for (int i = 0; i < 2000 && r.fake.capture->frames.load() < n; ++i) r.ok("wait 0.005");
        assert(r.fake.capture->frames.load() >= n);
    }
    float peakOf(const std::vector<float> &v, size_t from, size_t to)
    {
        float p = 0;
        for (size_t i = from; i < to && i < v.size(); ++i) p = std::max(p, std::fabs(v[i]));
        return p;
    }
}

static void test_live_playback_is_the_offline_render()
{
    Run r;
    r.ok("project new " + freshSong("live") + " --bpm 120");
    r.ok("strip add --kind instrument --instrument drums");
    r.ok("clip add --strip ch_2 --length 8");
    r.ok("note add pt_1 --pitch 36 --at 0");
    r.ok("note add pt_1 --pitch 42 --at 0.5");
    r.ok("note add pt_1 --pitch 38 --at 1");
    r.ok("strip add --kind instrument --instrument synth");
    r.ok("clip add --strip ch_3 --length 8");
    r.ok("note add pt_2 --pitch 48 --at 0 --length 2");
    r.ok("strip add --kind bus --name Verb");
    r.ok("device add ch_4 --type reverb");
    r.ok("send add ch_3 --to ch_4 --gain -6");
    r.events.clear();
    r.ok("transport play");
    assert(r.svc.model().transport.playing && contains(r.events.back(), "[evt] transport.changed playing=1 position=0.0 loop=off"));
    waitFrames(r, 96000);
    r.ok("transport stop");
    assert(!r.svc.model().transport.playing && r.svc.model().transport.position >= 4.0); // 96000 frames = 4 beats heard
    r.ok("render --out " + scratch() + "/live.wav --ports --to 4");
    const auto &port = r.fake.written[scratch() + "/live.Main.wav"];
    const auto &live = r.fake.capture->sink;
    for (size_t i = 0; i < 96000; ++i)
    {
        if (live[2 * i] != port[0][i] || live[2 * i + 1] != port[1][i]) std::printf("    frame %zu: live %g offline %g\n", i, live[2 * i], port[0][i]);
        assert(live[2 * i] == port[0][i] && live[2 * i + 1] == port[1][i]);
    }
    assert(peakOf(live, 0, 2 * 96000) > 0.05f);
    pass("live playback on the clock device is the offline render, sample for sample, over 4 beats (R-PLAY-1)");
}

static void test_edits_while_playing_are_heard()
{
    Run r;
    r.ok("project new " + freshSong("liveedit") + " --bpm 120");
    r.ok("transport play");                                     // an empty song: silence…
    waitFrames(r, 12000);
    r.ok("clip add --src tone.wav --length 64");                // …a STRUCTURAL edit: a new engine swapped in
    const long long added = r.fake.capture->frames.load();
    waitFrames(r, added + 24000);
    r.ok("set ch_2.gain=-120");                                 // a LIVE edit: a message, the next block
    const long long muted = r.fake.capture->frames.load();
    waitFrames(r, muted + 24000);
    r.ok("transport stop");
    const auto &v = r.fake.capture->sink;
    assert(peakOf(v, 0, 2 * 11000) == 0.0f);                    // before the clip existed
    assert(peakOf(v, 2 * (added + 4096), 2 * (added + 12000)) > 0.3f);  // the tone, after the swap
    assert(peakOf(v, 2 * (muted + 4096), 2 * (muted + 24000)) < 1e-5f); // gone after −120 dB
    assert(contains(r.no("transport play --from -1"), "--from"));
    Run cannot;
    cannot.fake.canPlay = false;
    SolarisService mute(cannot.fake.host());
    std::string err;
    mute.dispatchText("project new " + freshSong("noout"), err);
    assert(!mute.dispatchText("transport play", err) && err == "this build cannot play audio (no output stream)");
    pass("while playing: a clip added is heard (an engine swap); a gain change is heard at the next block (R-PLAY-1)");
}

static void test_loop_and_seek()
{
    Run r;
    r.ok("project new " + freshSong("loop") + " --bpm 120");
    r.ok("strip add --kind instrument --instrument drums");
    r.ok("clip add --strip ch_2 --length 8");
    r.ok("note add pt_1 --pitch 36 --at 0");                    // one kick at beat 0 of a 4-beat pattern
    r.ok("transport loop 0 1");                                 // loop the first beat: a kick every beat
    r.ok("transport play");
    waitFrames(r, 4 * 24000 + 1000);
    r.ok("transport stop");
    const auto &v = r.fake.capture->sink;
    for (int k = 1; k <= 3; ++k)
    {
        const size_t at = (size_t)k * 24000;
        assert(peakOf(v, 2 * at, 2 * (at + 480)) > 10.0f * peakOf(v, 2 * (at - 2400), 2 * at)); // the kick again, each beat
    }
    r.ok("transport loop off");
    assert(r.svc.model().transport.loopTo == 0.0);
    r.ok("transport seek 6");
    assert(r.svc.model().transport.position == 6.0);
    assert(contains(r.no("transport loop 3 2"), "to > from"));
    pass("loop repeats its span while playing; seek moves the transport (R-TIME-4)");
}

static void test_a_refusal_is_an_event_and_lands_in_lastError()
{
    Run r;
    r.events.clear();
    assert(r.no("strip add") == "no song is open (project new <path.slp> or project open <path.slp>)");
    assert(r.events.size() == 1 && contains(r.events[0], "[evt] command.rejected line=\"strip add\""));
    assert(r.svc.model().lastError == "no song is open (project new <path.slp> or project open <path.slp>)");
    r.no("flurb");
    assert(contains(r.svc.model().lastError, "unknown command: flurb"));
    pass("a refusal is a command.rejected event AND lastError — inspectable by a front end that was not listening");
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_grammar_table_parses_and_rejects();
    test_event_and_model_tables();
    test_a_new_song_has_its_mixers_and_round_trips();
    test_every_sample_file_gets_its_own_strip();
    test_an_instrument_drop_is_one_command_and_a_refusal_says_nothing_changed();
    test_a_relative_src_is_found_in_the_songs_folder();
    test_note_move_and_quantize_edit_the_pattern();
    test_strip_relink_moves_every_clip_in_one_edit();
    test_undo_and_redo_every_edit();
    test_formulas_bind_numbers_and_refuse_what_cannot_be_read();
    test_an_automated_gain_renders_its_curve();
    test_routing_only_goes_forward_and_refusals_change_nothing();
    test_set_and_get_through_the_registry();
    test_patterns_are_shared_by_their_clips();
    test_solo_mute_matrix_and_audit();
    test_render_writes_the_mix_deterministically();
    test_the_dump_and_the_document();
    test_the_machine_settings_folders_devices_and_recents();
    test_live_playback_is_the_offline_render();
    test_edits_while_playing_are_heard();
    test_loop_and_seek();
    test_a_refusal_is_an_event_and_lands_in_lastError();
    std::printf("\n%d passed, 0 failed\n", passed);
    return 0;
}
