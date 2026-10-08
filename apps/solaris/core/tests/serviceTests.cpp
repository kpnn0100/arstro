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
#include <cassert>
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
    struct Fake
    {
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
            return h;
        }
    };

    struct Run
    {
        Fake fake;
        SolarisService svc;
        std::vector<std::string> events;
        Run() : svc(fake.host())
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
    assert(r.no("clip add --at 3") == "clip add needs --src <file> (audio) or --strip <instrument strip> (notes)");
    pass("every sample file gets its own strip on Sources → Main; the same file reuses it; fed-by is computed (R-MIX-2/3/8)");
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
    pass("routing only goes forward — route, send, mixer move — and a refused command changes nothing (R-MIX-4)");
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
    const auto strip = [](std::string s) { // the paths differ by name; nothing else may
        const auto a = s.find("\"projectPath\""), b = s.find('\n', a);
        return s.erase(a, b - a);
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
    test_routing_only_goes_forward_and_refusals_change_nothing();
    test_set_and_get_through_the_registry();
    test_patterns_are_shared_by_their_clips();
    test_solo_mute_matrix_and_audit();
    test_render_writes_the_mix_deterministically();
    test_the_dump_and_the_document();
    test_a_refusal_is_an_event_and_lands_in_lastError();
    std::printf("\n%d passed, 0 failed\n", passed);
    return 0;
}
