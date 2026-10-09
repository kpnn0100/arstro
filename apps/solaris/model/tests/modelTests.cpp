// solaris_model tests — the .slp document (R-FMT-1…4, R-MIX-3/4, R-CLIP-2).
//
// The cheapest test in the project and the one that catches most format regressions: canonical
// text parses and serializes back to the same bytes. Then what is refused (structure) and what
// is repaired (numbers), each named the way a user will read it.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../Format.h"
#include "../Project.h"
#include <cassert>
#include <cstdio>
#include <string>

using namespace arstro::solaris;

namespace
{
    int passed = 0;
    void pass(const char *name)
    {
        std::printf("[PASS] %s\n", name);
        ++passed;
    }
    bool contains(const std::string &s, const std::string &part) { return s.find(part) != std::string::npos; }

    // A canonical document using every node type, unknown keys, an unknown node and comments.
    const char *kCanonical =
        "arstro-project = 1\n"
        "app        = solaris\n"
        "id         = prj_1\n"
        "name       = \"Night Drive\"\n"
        "timebase   = beats\n"
        "bpm        = 128.0\n"
        "sig        = 4/4\n"
        "ppq        = 960\n"
        "sampleRate = 48000\n"
        "masterGain = -1.5\n"
        "masterOut  = prt_1,prt_2\n"
        "futureKey  = kept\n"
        "; a note to myself\n"
        "\n"
        "#aport id=prt_1 name=Main dir=out channels=2 order=0\n"
        "#aport id=prt_2 name=Phones dir=out channels=2 order=1\n"
        "\n"
        "#amixer id=mx_1 name=Sources order=0\n"
        "#amixer id=mx_2 name=Buses order=1\n"
        "\n"
        "#atrack id=ch_1 name=Main kind=bus mixer=mx_2 order=0 gain=0.0 pan=0.0\n"
        "#atrack id=ch_2 name=kick kind=audio mixer=mx_1 order=0 gain=-3.0 pan=0.0 out=ch_1 colour=2\n"
        "#atrack id=ch_3 name=Lead kind=instrument mixer=mx_1 order=1 gain=0.0 pan=-0.25 mute=true out=ch_1  ; the hook\n"
        "; lead is muted while I mix the drums\n"
        "#atrack id=ch_4 name=Verb kind=bus mixer=mx_2 order=1 gain=0.0 pan=0.0 out=prt_1 shiny=yes\n"
        "\n"
        "#asend id=sd_1 from=ch_3 to=ch_4 gain=-12.0 pre=false\n"
        "#asend id=sd_2 from=ch_2 to=prt_2 gain=0.0 pre=true\n"
        "\n"
        "#arack track=ch_3\n"
        "  #aeffect id=dv_1 type=synth osc1.wave=saw filter.cutoff=1200.0\n"
        "  #aeffect id=dv_2 type=reverb bypass=true mix=0.2\n"
        "#arack track=master\n"
        "  #aeffect id=dv_3 type=compressor threshold=-12.0\n"
        "\n"
        "#alane id=ln_1 name=Drums order=0 colour=3\n"
        "#alane id=ln_2 name=\"Lead line\" order=1\n"
        "\n"
        "#apattern id=pt_1 name=hook length=4.0\n"
        "  #note pitch=60 at=0.0 length=0.5 vel=100\n"
        "  #note pitch=64 at=0.0 length=0.5 vel=90\n"
        "  #note pitch=67 at=1.5 length=0.25 vel=127\n"
        "\n"
        "#aclip id=ac_1 name=kick track=ch_2 lane=ln_1 src=\"samples/kick 01.wav\" at=0.0 in=0.0 out=0.512 gain=-2.0 fadeOut=0.125\n"
        "#aclip id=ac_2 name=hook track=ch_3 lane=ln_2 pattern=pt_1 at=4.0 length=16.0\n"
        "#aclip id=ac_3 track=ch_3 pattern=pt_1 at=20.0\n"
        "\n"
        "#aauto id=au_1 name=\"Lead · Cutoff\" unit=Hz min=20.0 max=20000.0 from=dv_1.filter.cutoff\n"
        "  #point at=0.0 value=400.0\n"
        "  #point at=4.0 value=1600.0 shape=smooth\n"
        "\n"
        "#abind address=dv_1.filter.cutoff formula=\"=au_1 * 2\"\n"
        "\n"
        "#xfuture id=xf_1 colour=7\n"
        "  detail=\"kept as written\"\n";
}

static void test_canonical_numbers_times_and_strings()
{
    assert(canonicalNumber(0.0) == "0.0");
    assert(canonicalNumber(-0.0) == "0.0");
    assert(canonicalNumber(1.0) == "1.0");
    assert(canonicalNumber(-3.5) == "-3.5");
    assert(canonicalNumber(0.1) == "0.1");
    assert(canonicalNumber(1.0 / 3.0) == "0.3333333333333333");
    assert(canonicalNumber(1e-9) == "1e-09");
    assert(canonicalBeats(0.5) == "0.5");
    assert(canonicalBeats(1.0 / 960.0) == "0.001");          // one tick: 0.001·960 = 0.96 → rounds back to 1
    assert(canonicalBeats(1.00001) == "1.0");                // rounded to the tick
    assert(canonicalBeats(2.0 / 3.0) == "0.667");            // 640 ticks
    assert(canonicalSeconds(0.5123456789) == "0.512346");
    assert(toTick(0.0010001) == 1.0 / 960.0);
    assert(quoteIfNeeded("kick") == "kick");
    assert(quoteIfNeeded("kick 01") == "\"kick 01\"");
    assert(quoteIfNeeded("") == "\"\"");
    assert(quoteIfNeeded("a\"b") == "\"a'b\"");              // no escapes in the format: a quote becomes '
    double d = 0;
    assert(parseNumber("1.5", d) && d == 1.5);
    assert(!parseNumber("1.5x", d) && !parseNumber("", d));
    bool b = false;
    assert(parseBool("1", b) && b && parseBool("false", b) && !b && !parseBool("yes", b));
    const LineTokens t = tokenizeLine("#aclip id=ac_1 name=\"a b; c\" lone ; trailing note");
    assert(t.type == "aclip" && t.fields.size() == 2 && t.fields[1].second == "a b; c");
    assert(t.bare.size() == 1 && t.comment == "trailing note");
    pass("canonical numbers, ticks, seconds and quoting");
}

static void test_canonical_text_is_a_fixed_point()
{
    Project p;
    std::string err;
    ParseReport rep;
    assert(parseProject(kCanonical, p, err, &rep));
    assert(err.empty() && rep.repaired == 0);
    const std::string out = serializeProject(p);
    if (out != kCanonical) std::printf("--- got:\n%s\n--- want:\n%s\n", out.c_str(), kCanonical);
    assert(out == kCanonical);
    // what was read
    assert(p.header.name == "Night Drive" && p.header.bpm == 128.0 && p.header.masterGain == -1.5);
    assert(p.header.masterOut.size() == 2 && p.header.unknown.size() == 1);
    assert(p.strips.size() == 4 && p.strip("ch_3")->mute && p.strip("ch_3")->pan == -0.25);
    assert(p.strip("ch_3")->remarks.inlineComment == "the hook" && p.strip("ch_3")->remarks.after.size() == 1);
    assert(p.strip("ch_4")->unknown.size() == 1 && p.strip("ch_4")->unknown[0].first == "shiny");
    assert(p.rack("ch_3")->devices.size() == 2 && p.rack("ch_3")->devices[1].bypass);
    assert(p.rack("ch_3")->devices[0].params.size() == 2 && p.rack("ch_3")->devices[0].params[1].second == "1200.0");
    assert(p.rack("master") && p.device("dv_3"));
    assert(p.pattern("pt_1")->notes.size() == 3);
    assert(p.clip("ac_1")->src == "samples/kick 01.wav" && p.clip("ac_1")->out == 0.512);
    assert(p.clip("ac_1")->fadeOut == 0.125 && p.clip("ac_1")->isAudio() && !p.clip("ac_2")->isAudio());
    assert(p.raw.size() == 1 && p.raw[0].lines.size() == 2); // an unknown node, kept verbatim
    assert(p.automations.size() == 1 && p.automation("au_1")->points.size() == 2 && p.automation("au_1")->points[1].shape == "smooth");
    assert(p.automation("au_1")->max == 20000.0 && p.binding("dv_1.filter.cutoff")->formula == "=au_1 * 2");
    pass("canonical text: parse -> serialize is a byte-exact fixed point (every node, unknowns, comments)");
}

static void test_automation_sketch_and_binding_refusals()
{
    // the suite schema's sketch — indented `<beats> = <value>` lines — reads as points, once
    const char *sketch =
        "arstro-project = 1\napp = solaris\nid = prj_3\n"
        "#amixer id=mx_1 name=Sources order=0\n"
        "#atrack id=ch_1 name=Lead kind=instrument mixer=mx_1\n"
        "#arack track=ch_1\n"
        "  #aeffect id=dv_1 type=synth\n"
        "#aauto id=au_1 node=dv_1 param=filter.cutoff interp=hold\n"
        "  0.000 = 400\n"
        "  4.000 = 1600\n";
    Project p;
    std::string err;
    ParseReport rep;
    assert(parseProject(sketch, p, err, &rep));
    const Automation *a = p.automation("au_1");
    assert(a && a->from == "dv_1.filter.cutoff" && a->points.size() == 2 && a->points[1].value == 1600.0 && a->points[0].shape == "hold");
    assert(a->unknown.empty() && rep.notes.size() == 1);
    Project q;
    assert(parseProject(serializeProject(p), q, err) && serializeProject(q) == serializeProject(p)); // normalised once
    // one binding per address, a formula starts with `=`, and it drives something that exists
    p.bindings.push_back(Binding{"dv_1.filter.cutoff", "=au_1", {}, {}});
    assert(validateProject(p).empty());
    p.bindings.push_back(Binding{"dv_1.filter.cutoff", "=500", {}, {}});
    p.bindings.push_back(Binding{"ch_9.gain", "=0", {}, {}});
    p.bindings.push_back(Binding{"ch_1.pan", "0.5", {}, {}});
    const auto e = validateProject(p);
    bool two = false, nowhere = false, notFormula = false;
    for (const auto &x : e)
    {
        two |= contains(x, "two bindings drive dv_1.filter.cutoff");
        nowhere |= contains(x, "`ch_9.gain`, which is no strip");
        notFormula |= contains(x, "ch_1.pan's binding `0.5` is not a formula");
    }
    assert(two && nowhere && notFormula);
    pass("#aauto + #point and #abind: the schema sketch normalises once; one binding per address, on something real (R-AUTO-1/4)");
}

static void test_a_new_project_has_the_default_mixers()
{
    const Project p = newProject("prj_9", "Song", 124.0, "4/4", 48000);
    assert(p.mixers.size() == 2 && p.mixers[0].name == "Sources" && p.mixers[1].name == "Buses");
    assert(p.strips.size() == 1 && p.strips[0].name == "Main" && p.strips[0].kind == "bus" && p.strips[0].mixer == "mx_2");
    assert(p.strips[0].out.empty());                         // Main feeds the master
    assert(p.ports.size() == 1 && p.header.masterOut == std::vector<std::string>{"prt_1"});
    assert(validateProject(p).empty());
    Project q;
    std::string err;
    const std::string text = serializeProject(p);
    assert(parseProject(text, q, err));
    assert(serializeProject(q) == text);
    pass("a new project: Sources, Buses with Main → master, port Main (R-MIX-3)");
}

static void test_hand_written_text_normalises_once()
{
    // aligned spacing, a continuation line, 1/0 booleans, inline notes on a clip, an un-ticked time
    const char *hand =
        "arstro-project = 1\napp = solaris\nid = prj_2\n"
        "#amixer   id=mx_1   name=Sources   order=0\n"
        "#amixer id=mx_2 name=Buses order=1\n"
        "#atrack id=ch_1 name=Main kind=bus mixer=mx_2\n"
        "#atrack id=ch_2 name=Bass kind=instrument mixer=mx_1 out=ch_1\n"
        "    mute=1 gain=-6\n"
        "#aclip id=ac_1 name=bassline track=ch_2 at=0.00104 length=8\n"
        "  #note pitch=36 at=0.0 length=0.5 vel=100\n"
        "  #note pitch=43 at=2.0 length=1.0 vel=90\n";
    Project p;
    std::string err;
    ParseReport rep;
    assert(parseProject(hand, p, err, &rep));
    assert(p.strip("ch_2")->mute && p.strip("ch_2")->gain == -6.0);
    assert(p.clip("ac_1")->at == 1.0 / 960.0);                  // rounded to the tick
    assert(p.clip("ac_1")->pattern == "pt_1" && p.pattern("pt_1")->notes.size() == 2);
    assert(p.pattern("pt_1")->length == 8.0);
    assert(rep.notes.size() == 1 && contains(rep.notes[0], "became pattern pt_1"));
    const std::string once = serializeProject(p);
    Project q;
    assert(parseProject(once, q, err));
    assert(serializeProject(q) == once);                         // canonical from then on
    assert(contains(once, "#atrack id=ch_2 name=Bass kind=instrument mixer=mx_1 order=0 gain=-6.0 pan=0.0 mute=true out=ch_1"));
    pass("hand-written text: spacing, continuations, inline notes → a pattern, then canonical");
}

static void test_routing_only_goes_forward()
{
    Project p = newProject("prj", "x", 120, "4/4", 48000);
    Strip a;
    a.id = "ch_2"; a.name = "kick"; a.kind = "audio"; a.mixer = "mx_1"; a.out = "ch_1";
    p.strips.push_back(a);
    assert(validateProject(p).empty());                          // Sources → Buses: forward
    // a bus on Buses feeding a strip on Sources: backward
    p.strip("ch_1")->out = "ch_2";
    auto e = validateProject(p);
    assert(e.size() == 1 && contains(e[0], "ch_1 (Main, on Buses) output → ch_2 (kick, on Sources)") && contains(e[0], "R-MIX-4"));
    // the same mixer is not forward either
    p.strip("ch_1")->out.clear();
    Strip b = a;
    b.id = "ch_3"; b.name = "snare"; b.out = "ch_2";
    p.strips.push_back(b);
    e = validateProject(p);
    assert(e.size() == 1 && contains(e[0], "ch_3 (snare, on Sources) output → ch_2 (kick, on Sources)"));
    p.strip("ch_3")->out = "ch_3";
    assert(contains(validateProject(p)[0], "goes to itself"));
    p.strip("ch_3")->out = "master";
    assert(validateProject(p).empty());
    // a send obeys the same rule, and may go straight to a port
    p.sends.push_back(Send{"sd_1", "ch_1", "ch_2", -6.0, false, {}, {}});
    e = validateProject(p);
    assert(e.size() == 1 && contains(e[0], "send sd_1"));
    p.sends[0].to = "prt_1";
    assert(validateProject(p).empty());
    // and a file that routes backward is refused on load, naming both ends
    p.strip("ch_1")->out = "ch_2";
    Project q;
    std::string err;
    assert(!parseProject(serializeProject(p), q, err));
    assert(contains(err, "ch_1 (Main, on Buses)") && contains(err, "R-MIX-4"));
    pass("routing only goes forward: same or earlier mixer refused, naming both ends (R-MIX-4)");
}

static void test_structural_errors_are_refused_with_names()
{
    auto refuse = [](const std::string &body, const std::string &want) {
        Project p;
        std::string err;
        const std::string text = "arstro-project = 1\napp = solaris\nid = prj\n"
                                 "#amixer id=mx_1 name=Sources order=0\n#amixer id=mx_2 name=Buses order=1\n" + body;
        const bool ok = parseProject(text, p, err);
        if (ok || !contains(err, want)) std::printf("    want `%s`, got `%s`\n", want.c_str(), err.c_str());
        assert(!ok && contains(err, want));
    };
    refuse("#atrack id=ch_1 name=a kind=audio\n#aclip id=ch_1 track=ch_1 src=a.wav in=0 out=1\n", "two nodes share the id `ch_1`");
    refuse("#atrack id=master name=a kind=bus\n", "`master` is reserved");
    refuse("#atrack id=ch_1 name=a kind=synth\n", "kind `synth`");
    refuse("#atrack id=ch_1 name=a kind=audio mixer=mx_9\n", "mixer `mx_9`");
    refuse("#atrack id=ch_1 name=a kind=audio out=nowhere\n", "`nowhere`, which is no strip");
    refuse("#atrack id=ch_1 name=a kind=instrument\n#aclip id=ac_1 track=ch_1 src=a.wav in=0 out=1\n", "needs an audio strip");
    refuse("#atrack id=ch_1 name=a kind=audio\n#apattern id=pt_1 length=4\n#aclip id=ac_1 track=ch_1 pattern=pt_1\n", "needs an instrument strip");
    refuse("#atrack id=ch_1 name=a kind=instrument\n#aclip id=ac_1 track=ch_1 pattern=pt_7\n", "pattern `pt_7`, which does not exist");
    refuse("#atrack id=ch_1 name=a kind=instrument\n#aclip id=ac_1 track=ch_1\n", "neither a src nor a pattern");
    refuse("#atrack id=ch_1 name=a kind=audio\n#aclip id=ac_1 track=ch_1 src=a.wav in=2 out=1\n", "≥ out");
    refuse("#atrack id=ch_1 name=a kind=audio\n#aclip id=ac_1 track=ch_1 lane=ln_4 src=a.wav in=0 out=1\n", "lane `ln_4`");
    refuse("#atrack id=ch_1 name=a kind=audio\n#aclip id=ac_1 track=ch_1 src=a.wav at=-1 in=0 out=1\n", "starts before the song");
    refuse("#aclip id=ac_1 track=ch_9 src=a.wav in=0 out=1\n", "`ch_9`, which is no strip");
    refuse("#arack track=ch_5\n  #aeffect id=dv_1 type=eq\n", "a rack belongs to `ch_5`");
    refuse("#atrack id=ch_1 name=a kind=bus\n#arack track=ch_1\n#arack track=ch_1\n", "two racks belong to `ch_1`");
    refuse("#arack track=master\n  #aeffect id=dv_1\n", "device dv_1 has no type");
    refuse("#aeffect id=dv_1 type=eq\n", "#aeffect outside an #arack");
    refuse("#note pitch=60\n", "#note outside");
    refuse("#aport id=prt_1 name=In dir=in channels=2\n#atrack id=ch_1 name=a kind=bus out=prt_1\n", "an INPUT port");
    refuse("#aport id=prt_1 name=X dir=sideways\n", "dir `sideways`");
    refuse("#aport id=prt_1 name=X channels=0\n", "0 channels");
    refuse("#asend id=sd_1 from=ch_8 to=master\n", "sends from `ch_8`");
    refuse("#apattern id=pt_1 length=0\n", "pattern pt_1 has no length");
    refuse("this is not a line\n", "cannot read");

    Project p;
    std::string err;
    assert(!parseProject("arstro-project = 1\napp = interstellar\n", p, err) && contains(err, "`interstellar` project"));
    assert(!parseProject("arstro-project = 1\napp = solaris\ntimebase = seconds\n", p, err) && contains(err, "counts in beats"));
    assert(!parseProject("arstro-project = 1\napp = solaris\nppq = 480\n", p, err) && contains(err, "960 ticks"));
    assert(!parseProject("arstro-project = 2\n", p, err) && contains(err, "unsupported"));
    assert(!parseProject("app = solaris\n", p, err) && contains(err, "not an arstro project"));
    assert(!parseProject("arstro-project = 1\nbogus header\n", p, err) && contains(err, "key = value"));
    assert(!parseProject("arstro-project = 1\nmasterOut = prt_9\n", p, err) && contains(err, "`prt_9`, which is no port"));
    pass("structural errors are refused, each naming what and where (R-FMT-4)");
}

static void test_numeric_corruption_is_repaired_and_counted()
{
    const char *text =
        "arstro-project = 1\napp = solaris\nid = prj\nbpm = nan\n"
        "#atrack id=ch_1 name=a kind=instrument gain=inf pan=5 mute=maybe\n"
        "#apattern id=pt_1 length=4\n"
        "  #note pitch=300 at=0 length=1 vel=0\n"
        "#aclip id=ac_1 track=ch_1 pattern=pt_1 at=abc\n";
    Project p;
    std::string err;
    ParseReport rep;
    assert(parseProject(text, p, err, &rep));
    assert(p.header.bpm == 120.0);                               // nan → the default
    assert(p.strip("ch_1")->gain == 0.0 && !p.strip("ch_1")->mute);
    assert(p.strip("ch_1")->pan == 1.0);                         // out of range: clamped
    assert(p.pattern("pt_1")->notes[0].pitch == 127 && p.pattern("pt_1")->notes[0].vel == 1);
    assert(p.clip("ac_1")->at == 0.0);
    assert(rep.repaired == 4);                                   // bpm, gain, mute, at
    pass("numeric corruption is repaired to the default and counted, never refused (R-FMT-4)");
}

static void test_ids_order_and_lookups()
{
    Project p = newProject("prj", "x", 120, "4/4", 48000);
    assert(p.nextId("ch") == "ch_2" && p.nextId("ac") == "ac_1" && p.nextId("mx") == "mx_3");
    Strip s;
    s.id = "ch_7"; s.name = "x"; s.mixer = "mx_1";
    p.strips.push_back(s);
    assert(p.nextId("ch") == "ch_8");                            // never reuses a number
    Strip t = s;
    t.id = "ch_x"; t.order = -1;
    p.strips.push_back(t);
    assert(p.nextId("ch") == "ch_8");                            // a non-numeric tail is ignored
    const auto order = p.stripsInOrder();
    assert(order[0]->id == "ch_x" && order[1]->id == "ch_7" && order[2]->id == "ch_1"); // mixer order, then strip order
    assert(p.rankOf(*p.strip("ch_1")) == std::make_pair(1, 0));
    Strip loose;
    loose.id = "ch_9";
    assert(p.mixerOf(loose)->id == "mx_1");                       // no mixer = the first
    assert(!p.clip("nope") && !p.device("nope") && !p.rack("nope") && !p.send("nope"));
    Project empty;
    assert(empty.mixerOf(loose) == nullptr && empty.rankOf(loose) == std::make_pair(0, 0));
    pass("ids never reuse a number; processing order is mixer order then strip order");
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_canonical_numbers_times_and_strings();
    test_canonical_text_is_a_fixed_point();
    test_automation_sketch_and_binding_refusals();
    test_a_new_project_has_the_default_mixers();
    test_hand_written_text_normalises_once();
    test_routing_only_goes_forward();
    test_structural_errors_are_refused_with_names();
    test_numeric_corruption_is_repaired_and_counted();
    test_ids_order_and_lookups();
    std::printf("\n%d passed, 0 failed\n", passed);
    return 0;
}
