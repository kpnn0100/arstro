/*
 *  interstellar_model_tests — the `.isp` fixed point and the version/rebase table (R-TEST-4's
 *  first and third load-bearing suites).
 *
 *  L1: pure logic, no display, no decoder, no Cosmo. Every test that edits a document re-checks
 *  that the result is still a byte-exact fixed point — an operation that leaves a document which
 *  cannot be written and read back unchanged has corrupted it, whatever else it got right.
 *
 *  The test that carries the whole version model is `derivedEditRecordsDeltaNotCopy`: it COUNTS
 *  the project's clips across an edit to an inherited clip. A copy-on-edit implementation passes
 *  every resolve-level check (the derived timeline does show the new value) and fails only there.
 */
#ifdef NDEBUG
#undef NDEBUG   // a Release build would otherwise compile every assert away and print PASS
#endif
#include <cassert>

#include "Arrange.h"
#include "Project.h"
#include "Versions.h"
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

using namespace arstro::interstellar;

namespace
{
    int gChecks = 0;
#define CHECK(x) do { ++gChecks; assert(x); } while (0)

    // Every node type, canonical. Raw Solaris nodes carry children, breakpoints, odd spacing and
    // comments; known nodes carry unknown keys; an unknown node type rides along at the end.
    const char *kEvery = R"ISP(; Japan MV — every node type, canonical
arstro-project = 1
app            = interstellar
id             = prj_mv01
name           = "Japan MV"
fps            = 24.0
width          = 3840
height         = 2160
par            = 1.0
colorspace     = rec709
timebase       = seconds ; see docs/audio-format.md §1
sampleRate     = 48000
masterGain     = 0.0
current        = tl_1
; the editor's last tab — presentation, never what a render reads
futureKey      = "kept verbatim"

#rack id=rk_1 path=japan18.cmp branch=main writeBranch=main

#rackobj id=ro_1 name=gr1 node=cn_1 kind=group weight=1.0
#rackobj id=ro_2 name=s_day01 node=cn_41 kind=source weight=0.75
  media=footage/DSC01.MOV frame=4.250
#rackobj id=ro_3 name=s_night node=cn_42 kind=source weight=1.0
  media="footage/night shot.mov" frame=0.000 lens=wide

#timeline id=tl_1 name=main order=0
; main is the default base
#timeline id=tl_2 name=social30 base=tl_1 colour=follow cut=follow order=1
#timeline id=tl_3 name=delivery base=tl_1 colour=pin@8f2c1ab cut=frozen order=2

#tldrop timeline=tl_2 node=clp_3 ; the base's clip is not in this version

#tlset timeline=tl_2 node=clp_2 out=14.000 at=4.000

#tlgrade timeline=tl_2 node=ro_2 exposure=0.4 contrast=-0.1

#track id=trk_1 name=v0 timeline=tl_1 kind=video order=0 opacity=1.0 blend=normal
#track id=trk_2 name=v1 timeline=tl_1 kind=video order=1 mute=true opacity=0.5 blend=screen
#track id=trk_3 name=dialog timeline=tl_1 kind=audio order=2 gain=-3.0

#clip id=clp_1 name=shotA track=trk_1 order=0 src=ro_2
  at=0.000 in=12.400 out=16.600 speed=1.0 fit=contain opacity=1.0 blend=normal
  geom.x=0.0 geom.y=0.0 geom.scale=1.0 geom.rotation=0.0
  geom.anchor.x=0.5 geom.anchor.y=0.5
  geom.crop.x=0.0 geom.crop.y=0.0 geom.crop.w=1.0 geom.crop.h=1.0
#clip id=clp_2 name=shotB track=trk_1 order=1 src=ro_3 ; a key from a newer build
  at=4.200 in=0.000 out=10.000 speed=2.0 fit=cover opacity=0.9 blend=normal
  geom.x=0.1 geom.y=-0.05 geom.scale=1.25 geom.rotation=3.5
  geom.anchor.x=0.5 geom.anchor.y=0.5
  geom.crop.x=0.0 geom.crop.y=0.0 geom.crop.w=1.0 geom.crop.h=1.0 stabilise=true
#clip id=clp_3 name=shotC track=trk_1 order=2 src=ro_2
  at=9.200 in=20.000 out=24.000 speed=1.0 fit=contain opacity=1.0 blend=normal
  geom.x=0.0 geom.y=0.0 geom.scale=1.0 geom.rotation=0.0
  geom.anchor.x=0.5 geom.anchor.y=0.5
  geom.crop.x=0.0 geom.crop.y=0.0 geom.crop.w=1.0 geom.crop.h=1.0
#clip id=clp_9 name=tag track=trk_1 timeline=tl_2 order=3 src=ro_2
  at=13.200 in=0.000 out=2.000 speed=1.0 fit=contain opacity=1.0 blend=normal
  geom.x=0.0 geom.y=0.0 geom.scale=1.0 geom.rotation=0.0
  geom.anchor.x=0.5 geom.anchor.y=0.5
  geom.crop.x=0.0 geom.crop.y=0.0 geom.crop.w=1.0 geom.crop.h=1.0

#transition id=tr_1 name=tr_ab track=trk_1 between=clp_1,clp_2 kind=dissolve dur=0.500

#marker id=mk_1 name=chorus timeline=tl_1 at=48.000 note="chorus in"

#atrack id=atr_1 name=bed timeline=tl_1 kind=audio order=0 gain=0.0 pan=0.0 mute=false solo=false
#atrack id=atr_2 name=bass   kind=instrument order=1 gain=-3.0
#atrack id=atr_3 name=drumbus kind=bus order=10

#aclip id=ac_1 name=bed_a track=atr_1 src=res:9c1f at=0.000 in=0.000 out=184.500
  gain=0.0 fadeIn=0.250 fadeOut=1.000 loop=false
#aclip id=ac_2 name=bassline track=atr_2 at=0.000 length=8.000
  #note pitch=36 at=0.0 length=0.5 vel=100
  #note pitch=43 at=2.0 length=1.0 vel=90   ; the fifth

#arack track=atr_2
  #aeffect id=afx_1 type=synth  osc=saw cutoff=0.4 res=0.2 env.a=0.01 env.r=0.3
  ; the chain is file order: a signal chain IS a sequence
  #aeffect id=afx_2 type=eq     low=+2 mid=-1 high=+1
  #aeffect id=afx_3 type=reverb mix=0.18 size=0.7

#aauto id=au_1 node=afx_1 param=cutoff interp=linear
  0.000 = 0.40
  4.000 = 0.80
  8.000 = 0.40

#asend id=sd_1 from=atr_1 to=atr_3 gain=-12.0 pre=false

#fx id=fx_1 node=ro_2 type=denoise radius=2 strength=0.6
#fx id=fx_2 node=ro_2 type=blend radius=1 shutter=180.0
#fx id=fx_3 clip=clp_1 type=freeze at=2.000

#bind id=bn_1 target=gr1.exposure expr="sin(t) * 0.2"
)ISP";

    // A small two-version project the version tests start from. Deliberately NOT canonical in
    // layout (one-line clips, some keys missing), so every load also exercises canonicalisation.
    const char *kBase = R"ISP(arstro-project = 1
app = interstellar
id = prj_t
name = t
fps = 24
#rack id=rk_1 path=t.cmp branch=main
#rackobj id=ro_1 name=gr1 node=cn_1 kind=group weight=1.0
#rackobj id=ro_2 name=s_a node=cn_2 kind=source weight=1.0 media=a.mov frame=0.0
#timeline id=tl_1 name=main order=0
#timeline id=tl_2 name=social base=tl_1 colour=follow cut=follow order=1
#track id=trk_1 name=v0 timeline=tl_1 kind=video order=0
#clip id=clp_1 name=c1 track=trk_1 order=0 src=ro_2 at=0.0 in=0.0 out=4.0
#clip id=clp_2 name=c2 track=trk_1 order=1 src=ro_2 at=4.0 in=10.0 out=14.0
#clip id=clp_3 name=c3 track=trk_1 order=2 src=ro_2 at=8.0 in=20.0 out=24.0
#transition id=tr_1 track=trk_1 between=clp_1,clp_2 kind=dissolve dur=0.5
#marker id=mk_1 name=m1 timeline=tl_1 at=2.0
)ISP";

    Project load(const std::string &text)
    {
        Project p;
        std::string err;
        const bool ok = p.parse(text, err);
        if (!ok) std::fprintf(stderr, "parse failed: %s\n", err.c_str());
        CHECK(ok);
        return p;
    }

    /** The document an operation left must still be a byte-exact fixed point. */
    void rt(const Project &p)
    {
        std::string err;
        const bool ok = Project::roundTripsExactly(p.serialize(), err);
        if (!ok) std::fprintf(stderr, "round trip failed: %s\n--- document ---\n%s\n", err.c_str(), p.serialize().c_str());
        CHECK(ok);
    }

    bool refused(const std::string &text, const std::string &needle1, const std::string &needle2 = "")
    {
        Project p;
        std::string err;
        if (p.parse(text, err)) return false;
        const bool named = err.find(needle1) != std::string::npos && (needle2.empty() || err.find(needle2) != std::string::npos);
        if (!named) std::fprintf(stderr, "refused, but the message does not name '%s' '%s': %s\n", needle1.c_str(),
                                 needle2.c_str(), err.c_str());
        return named;
    }

    ResolvedTimeline res(const Project &p, const std::string &tl)
    {
        ResolvedTimeline r;
        std::string err;
        const bool ok = resolve(p, tl, r, err);
        if (!ok) std::fprintf(stderr, "resolve failed: %s\n", err.c_str());
        CHECK(ok);
        return r;
    }

    const Clip *rclip(const ResolvedTimeline &r, const std::string &id)
    {
        for (const auto &c : r.clips) if (c.id == id) return &c;
        return nullptr;
    }
    bool has(const ResolvedTimeline &r, const std::string &id) { return r.provenance.count(id) > 0; }
    Provenance prov(const ResolvedTimeline &r, const std::string &id) { return r.provenance.at(id); }
    bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }
    /** The source time a clip shows at timeline time t — what "frames stay put" means. */
    double sourceAt(const Clip &c, double t) { return c.in + (t - c.at) * c.speed; }

    // ─────────────────────────────────────────────────────────────────────────────────────────

    void roundTripEveryNode()
    {
        std::string err;
        int repaired = -1;
        Project p;
        CHECK(p.parse(kEvery, err, &repaired));
        CHECK(repaired == 0);
        const std::string out = p.serialize();
        if (out != kEvery) Project::roundTripsExactly(kEvery, err), std::fprintf(stderr, "%s\n", err.c_str());
        CHECK(out == kEvery);                          // byte-exact
        CHECK(Project::roundTripsExactly(kEvery, err));

        // typed where implemented …
        CHECK(p.hasRack && p.rack.writeBranch == "main");
        CHECK(p.rackObjs.size() == 3 && p.timelines.size() == 3 && p.tracks.size() == 3 && p.clips.size() == 4);
        CHECK(p.audioTracks.size() == 1 && p.audioClips.size() == 1 && p.effects.size() == 3);
        CHECK(p.drops.size() == 1 && p.sets.size() == 1 && p.grades.size() == 1);
        CHECK(p.timeline("tl_3")->pinned() && p.timeline("tl_3")->pinCommit() == "8f2c1ab" && p.timeline("tl_3")->frozen());
        CHECK(p.clip("clp_9")->timeline == "tl_2");
        CHECK(near(p.clip("clp_2")->geom.y, -0.05) && near(p.clip("clp_2")->duration(), 5.0));
        // … unknown keys, comments …
        CHECK(p.clip("clp_2")->unknown.size() == 1 && p.clip("clp_2")->unknown[0].first == "stabilise");
        CHECK(p.rackObj("ro_3")->unknown.size() == 1 && p.rackObj("ro_3")->media == "footage/night shot.mov");
        CHECK(p.headerUnknown.size() == 1 && p.headerUnknown[0].second == "kept verbatim");
        CHECK(p.preamble.size() == 1 && p.headerNotes.at("timebase").inlineComment.find("audio-format") != std::string::npos);
        // … and preserved, verbatim, where not.
        CHECK(p.raw.size() == 7);
        const RawNode *arack = nullptr;
        for (const auto &r : p.raw) if (r.type == "arack") arack = &r;
        CHECK(arack && arack->id.empty() && arack->lines.size() == 5);
        CHECK(arack->lines[1] == "  #aeffect id=afx_1 type=synth  osc=saw cutoff=0.4 res=0.2 env.a=0.01 env.r=0.3");
        int effects = 0;
        for (const auto &c : arack->children) effects += c.type == "aeffect";
        CHECK(effects == 3);
        CHECK(p.rawNode("au_1")->children.size() == 3 && p.rawNode("au_1")->children[1].text == "4.000 = 0.80");
        CHECK(p.kindOf("afx_2") == NodeKind::Raw);   // a child's id is an id of the document too

        const auto bad = p.unrenderable();
        CHECK(bad.size() == 7);
        auto listed = [&](const std::string &s) {
            for (const auto &b : bad) if (b.find(s) == 0) return true;
            return false;
        };
        CHECK(listed("#atrack atr_2 (kind=instrument") && listed("#atrack atr_3 (kind=bus"));
        CHECK(listed("#aclip ac_2 (note clip, 2 #note)") && listed("#arack track=atr_2 (processor chain, 3 #aeffect)"));
        CHECK(listed("#aauto au_1 (automation, 3 breakpoints)") && listed("#asend sd_1") && listed("#bind bn_1"));
        Project pure = load(kBase);
        CHECK(pure.unrenderable().empty());
    }

    void messyInputCanonicalises()
    {
        // Out of order, padded, CRLF, numbers in any spelling, references by bind name.
        const std::string messy =
            "app=interstellar\r\nfps = 24\r\narstro-project = 1\r\ncurrent = main\r\n"
            "#clip id=clp_1 track=v0 src=s_a at=4.2 in=+0 out=1e1   speed=1\r\n"
            "#marker id=mk_1 timeline=main at=1\r\n"
            "#track id=trk_1 name=v0 timeline=main kind=video\r\n"
            "#rackobj id=ro_2 name=s_a node=cn_2\r\n"
            "#timeline id=tl_1 name=main\r\n";
        Project p = load(messy);
        CHECK(p.current == "tl_1" && p.clip("clp_1")->track == "trk_1" && p.clip("clp_1")->src == "ro_2");
        const std::string once = p.serialize();
        std::string err;
        CHECK(Project::roundTripsExactly(once, err));    // canonical after ONE pass
        CHECK(once.find("fps            = 24.0\n") != std::string::npos);
        CHECK(once.find("at=4.200 in=0.000 out=10.000 speed=1.0") != std::string::npos);
        CHECK(once.find("current        = tl_1\n") != std::string::npos);
        CHECK(once.find('\r') == std::string::npos);
        CHECK(once.find("#rackobj") < once.find("#timeline") && once.find("#timeline") < once.find("#track") &&
              once.find("#track") < once.find("#clip") && once.find("#clip") < once.find("#marker"));
        // Numbers: shortest text that reads back bit-identically; integral keeps ".0"; locale-free.
        CHECK(canonicalNumber(0.1) == "0.1" && canonicalNumber(24) == "24.0" && canonicalNumber(-0.0) == "0.0");
        CHECK(canonicalNumber(1.0 / 3.0) == "0.3333333333333333" && canonicalTime(12.4) == "12.400");
        for (double v : {0.1, 1.0 / 3.0, 1e-9, 123456.789, 6.02e23, -2.5})
        {
            double back = 0;
            CHECK(parseNumber(canonicalNumber(v), back) && back == v);
        }
        CHECK(quoteIfNeeded("a b") == "\"a b\"" && quoteIfNeeded("x;y") == "\"x;y\"" && quoteIfNeeded("plain") == "plain");
        CHECK(quoteIfNeeded("") == "\"\"" && quoteIfNeeded("say \"hi\"") == "\"say \\\"hi\\\"\"");
    }

    void colourOnClipRefused()
    {
        const std::string head = "arstro-project = 1\n#rackobj id=ro_1 name=s node=n\n#timeline id=tl_1\n"
                                 "#track id=trk_1 timeline=tl_1 kind=video\n";
        for (const char *key : {"exposure", "grade", "grade.lift", "LUT", "params", "curve.r", "mixer", "temp", "tint",
                                "saturation", "dehaze", "texture", "look"})
        {
            const std::string text = head + "#clip id=clp_1 track=trk_1 src=ro_1 in=0 out=1\n  " + key + "=0.5\n";
            CHECK(refused(text, std::string("'") + key + "'"));
        }
        // the same key smuggled in as a version override is refused too
        CHECK(refused(head + "#clip id=clp_1 track=trk_1 src=ro_1 in=0 out=1\n#timeline id=tl_2 base=tl_1\n"
                             "#tlset timeline=tl_2 node=clp_1 exposure=0.3\n",
                      "'exposure'", "#tlgrade"));
        // and through setField, naming the key
        Project p = load(kBase);
        std::string err;
        CHECK(!setField(p, "tl_2", "clp_1", "exposure", "0.4", err) && err.find("'exposure'") != std::string::npos);
        CHECK(!setField(p, "tl_1", "clp_1", "grade.gain", "0.4", err) && err.find("'grade.gain'") != std::string::npos);
        CHECK(p.sets.empty());
        // a NON-colour key on a clip is fine and preserved
        Project q = load(head + "#clip id=clp_1 track=trk_1 src=ro_1 in=0 out=1 stabilise=true\n");
        CHECK(q.clip("clp_1")->unknown.size() == 1);
    }

    void nanRepairedAndCounted()
    {
        std::string text = kBase;
        text += "masterGain = nan\n";                      // a header line AFTER nodes is illegal …
        Project bad;
        std::string err;
        CHECK(!bad.parse(text, err));                       // … structurally, so put it in the header:
        std::string t2 = kBase;
        t2.insert(t2.find("#rack"), "masterGain = inf\n");
        t2.replace(t2.find("weight=1.0 media"), 10, "weight=nan");
        t2 += "#clip id=clp_8 track=trk_1 src=ro_2 at=20.0 in=5.0 out=nan opacity=-inf\n";
        t2 += "#tlgrade timeline=tl_2 node=ro_2 exposure=nan\n";
        int repaired = 0;
        Project p;
        CHECK(p.parse(t2, err, &repaired));
        // masterGain, weight, out, opacity, the out-widening (a repaired out must not become a
        // refusal), and the grade delta.
        CHECK(repaired == 6);
        CHECK(p.masterGain == 0.0 && p.rackObj("ro_2")->weight == 1.0 && p.clip("clp_8")->opacity == 1.0);
        CHECK(p.clip("clp_8")->in < p.clip("clp_8")->out && near(p.clip("clp_8")->duration(), 1.0 / 24.0));
        CHECK(p.tlgrade("tl_2", "ro_2")->deltas[0].second == 0.0);
        rt(p);
        // An unrepaired no-frames clip is still a structural refusal.
        CHECK(refused(std::string(kBase) + "#clip id=clp_8 track=trk_1 src=ro_2 in=5.0 out=5.0\n", "clp_8", "no frames"));
    }

    void structuralRefusals()
    {
        const std::string base = kBase;
        CHECK(refused(base + "#rack id=rk_2 path=other.cmp\n", "exactly one #rack"));
        CHECK(refused("arstro-project = 1\n#timeline id=tl_a base=tl_b\n#timeline id=tl_b base=tl_a\n", "tl_a", "tl_b"));
        CHECK(refused("arstro-project = 1\n#timeline id=tl_a base=tl_c\n#timeline id=tl_b base=tl_a\n#timeline id=tl_c base=tl_b\n",
                      "tl_a -> tl_c -> tl_b -> tl_a"));
        CHECK(refused(base + "#clip id=clp_8 track=trk_1 src=ro_2 in=6.0 out=5.0\n", "in >= out"));
        CHECK(refused(base + "#clip id=clp_8 track=trk_1 src=ro_2 in=0.0 out=5.0 speed=0\n", "speed=0"));
        // a src naming nothing lists the rack's bind names
        CHECK(refused(base + "#clip id=clp_8 track=trk_1 src=ro_9 in=0 out=1\n", "src=ro_9", "gr1, s_a"));
        CHECK(refused(base + "#marker id=clp_1 timeline=tl_1\n", "duplicate id clp_1"));
        CHECK(refused(base + "#marker id=mk_2 name=frame timeline=tl_1\n", "reserved word: frame"));
        CHECK(refused(base + "#marker id=mk_2 name=9lives timeline=tl_1\n", "starts with a letter"));
        CHECK(refused(base + "#marker id=mk_2 name=c1 timeline=tl_1\n", "bind name c1 is used by both"));
        CHECK(refused(base + "#marker id=mk_2 name=clp_2 timeline=tl_1\n", "already the id of"));
        CHECK(refused(base + "#timeline id=tl_9 base=tl_7\n", "base=tl_7 names no #timeline"));
        CHECK(refused(base + "#clip id=clp_8 track=trk_7 src=ro_2 in=0 out=1\n", "track=trk_7 names no #track"));
        CHECK(refused(base + "#tlgrade timeline=tl_2 node=clp_1 exposure=0.1\n", "a colour override names a #rackobj"));
        CHECK(refused(base + "#tlset timeline=tl_2 node=ro_2 weight=0.5\n", "colour is #tlgrade"));
        CHECK(refused(base + "#tlset timeline=tl_2 node=clp_1 name=other\n", "cannot override 'name'"));
        CHECK(refused(base + "#tlset timeline=tl_2 node=clp_1 out=3\n#tlset timeline=tl_2 node=clp_1 in=1\n", "two #tlset"));
        CHECK(refused(base + "#transition id=tr_2 track=trk_1 between=clp_2,clp_3 dur=4.5\n", "longer than clp_2"));
        CHECK(refused(base + "#timeline id=tl_9 base=tl_1 colour=sometimes\n", "follow or pin@"));
        CHECK(refused(base + "#clip id=clp_8 track=trk_1 src=ro_2 in=0 out=1\n  0.000 = 0.40\n", "expected key=value"));
        CHECK(refused(base + "#clip id=clp_8 track=trk_1 src=ro_2 in=0 out=1\n  #note pitch=3\n", "cannot have child nodes"));
        CHECK(refused(base + "loose=line\n", "indented under its #node"));
        CHECK(refused("arstro-project = 1\n  #rack id=rk_1 path=x.cmp\n", "column 0"));
        // A delta whose target is missing is NOT refused — it dangles, and the project opens.
        Project p = load(base + "#tlset timeline=tl_2 node=clp_77 out=3.000\n");
        CHECK(p.sets.size() == 1);
    }

    void resolveInheritance()
    {
        Project p = load(kBase);
        ResolvedTimeline r2 = res(p, "tl_2");
        CHECK(r2.clips.size() == 3 && r2.tracks.size() == 1 && r2.transitions.size() == 1 && r2.markers.size() == 1);
        for (const char *id : {"trk_1", "clp_1", "clp_2", "clp_3", "tr_1", "mk_1"}) CHECK(prov(r2, id) == Provenance::Inherited);
        CHECK(r2.dangling.empty());
        ResolvedTimeline r1 = res(p, "main");                         // by bind name
        for (const auto &kv : r1.provenance) CHECK(kv.second == Provenance::Local);

        // A #tlset on ONE field leaves every other field inheriting …
        Project q = load(std::string(kBase) + "#tlset timeline=tl_2 node=clp_2 out=13.000\n");
        r2 = res(q, "tl_2");
        CHECK(near(rclip(r2, "clp_2")->out, 13.0) && near(rclip(r2, "clp_2")->in, 10.0));
        CHECK(prov(r2, "clp_2") == Provenance::Overridden && prov(r2, "clp_1") == Provenance::Inherited);
        // … so a later base edit to an UNTOUCHED field still arrives, and the override still wins.
        std::string err;
        CHECK(setField(q, "tl_1", "clp_2", "in", "11.0", err));
        CHECK(q.sets.size() == 1 && q.sets[0].fields.size() == 1);   // the base edit was in place
        r2 = res(q, "tl_2");
        CHECK(near(rclip(r2, "clp_2")->in, 11.0) && near(rclip(r2, "clp_2")->out, 13.0));
        CHECK(near(rclip(res(q, "tl_1"), "clp_2")->out, 14.0));
        rt(q);

        // A key from a newer build inside a #tlset is carried verbatim and applied as an unknown key.
        Project f = load(std::string(kBase) + "#tlset timeline=tl_2 node=clp_1 wobble=3 out=3.5\n");
        const ResolvedTimeline rf = res(f, "tl_2");
        const Clip *fc = rclip(rf, "clp_1");
        CHECK(fc && near(fc->out, 3.5) && fc->unknown.size() == 1 && fc->unknown[0].second == "3");
        CHECK(f.serialize().find("#tlset timeline=tl_2 node=clp_1 wobble=3 out=3.500\n") != std::string::npos);
        rt(f);

        // #tldrop removes — and what hung on the dropped clip goes with it, without dangling.
        Project d = load(std::string(kBase) + "#tldrop timeline=tl_2 node=clp_2\n");
        r2 = res(d, "tl_2");
        CHECK(!has(r2, "clp_2") && !has(r2, "tr_1") && has(r2, "clp_1") && r2.dangling.empty());
        CHECK(has(res(d, "tl_1"), "clp_2"));
        // Dropping a track takes the base's clips on it, as a consequence rather than a dangle.
        Project dt = load(std::string(kBase) + "#tldrop timeline=tl_2 node=trk_1\n");
        r2 = res(dt, "tl_2");
        CHECK(r2.clips.empty() && r2.transitions.empty() && r2.dangling.empty() && r2.markers.size() == 1);

        // A local addition appears as Local, and only in its own version.
        Project l = load(std::string(kBase) + "#clip id=clp_9 name=c9 track=trk_1 timeline=tl_2 src=ro_2 at=12.0 in=0 out=2\n");
        CHECK(prov(res(l, "tl_2"), "clp_9") == Provenance::Local);
        CHECK(!has(res(l, "tl_1"), "clp_9"));
        // A grandchild sees both the base's and its parent's nodes as inherited.
        Timeline reel;
        reel.id = "tl_3";
        reel.name = "reel";
        reel.base = "tl_2";
        l.timelines.push_back(reel);
        ResolvedTimeline r3 = res(l, "tl_3");
        CHECK(prov(r3, "clp_9") == Provenance::Inherited && prov(r3, "clp_1") == Provenance::Inherited);
        rt(l);
    }

    void derivedEditRecordsDeltaNotCopy()
    {
        Project p = load(kBase);
        const size_t clips = p.clips.size(), tracks = p.tracks.size();
        std::string err;
        CHECK(setField(p, "tl_2", "clp_2", "out", "13.5", err));
        CHECK(p.clips.size() == clips && p.tracks.size() == tracks);   // NOT a copy (R-G-3)
        CHECK(p.sets.size() == 1);
        const TlSet *s = p.tlset("tl_2", "clp_2");
        CHECK(s && s->fields.size() == 1 && s->fields[0].first == "out" && s->fields[0].second == "13.500");
        CHECK(p.clip("clp_2")->out == 14.0);                            // the base is untouched
        CHECK(near(rclip(res(p, "tl_2"), "clp_2")->out, 13.5));
        CHECK(prov(res(p, "tl_2"), "clp_2") == Provenance::Overridden);
        CHECK(near(rclip(res(p, "tl_1"), "clp_2")->out, 14.0));

        // A second field on the same node joins the same #tlset; still no copy.
        CHECK(setField(p, "social", "c2", "opacity", "0.5", err));     // by bind names
        CHECK(p.clips.size() == clips && p.sets.size() == 1 && p.tlset("tl_2", "clp_2")->fields.size() == 2);
        // Setting a field back to the base's value removes the override — no divergence, no delta.
        CHECK(setField(p, "tl_2", "clp_2", "out", "14", err));
        CHECK(p.tlset("tl_2", "clp_2")->fields.size() == 1 && p.tlset("tl_2", "clp_2")->fields[0].first == "opacity");
        CHECK(setField(p, "tl_2", "clp_2", "opacity", "1.0", err));
        CHECK(p.sets.empty());
        // Inherited tracks and markers record deltas the same way.
        CHECK(setField(p, "tl_2", "trk_1", "mute", "true", err) && setField(p, "tl_2", "mk_1", "at", "3", err));
        CHECK(p.sets.size() == 2 && p.tracks.size() == tracks && !p.track("trk_1")->mute);
        rt(p);
    }

    void ownedEditsAndRefusals()
    {
        Project p = load(kBase);
        std::string err;
        CHECK(setField(p, "tl_1", "clp_2", "speed", "2", err));
        CHECK(p.sets.empty() && p.clip("clp_2")->speed == 2.0);       // owned: edited in place
        CHECK(!setField(p, "tl_1", "clp_2", "colourfulness", "1", err) && err.find("editable:") != std::string::npos);
        CHECK(!setField(p, "tl_1", "clp_2", "name", "x", err) && err.find("rename") != std::string::npos);
        CHECK(!setField(p, "tl_1", "trk_1", "kind", "audio", err) && err.find("cannot be edited") != std::string::npos);
        CHECK(!setField(p, "tl_1", "clp_2", "out", "nan", err) && err.find("finite number") != std::string::npos);
        CHECK(!setField(p, "tl_1", "clp_2", "out", "9.0", err) && err.find("no frames") != std::string::npos);
        CHECK(!setField(p, "tl_1", "clp_2", "speed", "0", err) && err.find("speed 0") != std::string::npos);
        CHECK(!setField(p, "tl_1", "clp_2", "track", "trk_9", err) && err.find("not a video track") != std::string::npos);
        CHECK(!setField(p, "tl_1", "clp_2", "src", "ro_9", err) && err.find("s_a") != std::string::npos);
        CHECK(!setField(p, "tl_1", "clp_1", "out", "0.3", err) && err.find("tr_1") != std::string::npos); // consume
        CHECK(!setField(p, "tl_7", "clp_1", "out", "3", err) && err.find("no such timeline") != std::string::npos);
        CHECK(p.clip("clp_2")->out == 14.0 && p.clip("clp_1")->out == 4.0);   // refusals changed nothing
        // a node a version dropped is not editable in it
        CHECK(dropNode(p, "tl_2", "clp_3", err));
        CHECK(!setField(p, "tl_2", "clp_3", "out", "21", err) && err.find("dropped") != std::string::npos);
        rt(p);
    }

    void splitInheritedClip()
    {
        Project p = load(kBase);
        const size_t clips = p.clips.size();
        std::string err;
        NodeId right;
        CHECK(arrange::split(p, "tl_2", "clp_2", 6.0, right, err));
        CHECK(p.clips.size() == clips + 1);                           // ONE new node: the right half
        CHECK(p.tlset("tl_2", "clp_2") && p.tlset("tl_2", "clp_2")->fields.size() == 1);
        CHECK(p.tlset("tl_2", "clp_2")->fields[0].first == "out" && p.tlset("tl_2", "clp_2")->fields[0].second == "12.000");
        const Clip *r = p.clip(right);
        CHECK(r && r->timeline == "tl_2" && r->track == "trk_1" && r->at == 6.0 && r->in == 12.0 && r->out == 14.0);
        CHECK(p.clip("clp_2")->out == 14.0);                          // base untouched
        ResolvedTimeline r2 = res(p, "tl_2");
        CHECK(prov(r2, "clp_2") == Provenance::Overridden && prov(r2, right) == Provenance::Local);
        CHECK(near(sourceAt(*rclip(r2, "clp_2"), 5.0), 11.0) && near(sourceAt(*rclip(r2, right), 7.0), 13.0));
        CHECK(!has(res(p, "tl_1"), right));
        rt(p);

        // Splitting the OUTGOING clip of a transition moves the transition to the right half —
        // as a delta on the inherited transition.
        CHECK(arrange::split(p, "tl_2", "clp_1", 3.0, right, err));
        CHECK(p.tlset("tl_2", "tr_1") && p.tlset("tl_2", "tr_1")->fields[0].second == right + ",clp_2");
        CHECK(p.transition("tr_1")->clipA == "clp_1");
        CHECK(res(p, "tl_2").dangling.empty());
        rt(p);
        // Refused, not clamped: outside the clip, or leaving the transition no room.
        CHECK(!arrange::split(p, "tl_2", "clp_3", 8.0, right, err) && err.find("not inside") != std::string::npos);
        CHECK(!arrange::split(p, "tl_2", "clp_2", 4.2, right, err) && err.find("longer than the left half") != std::string::npos);
        CHECK(p.clips.size() == clips + 2);
    }

    void danglingAndRebase()
    {
        Project p = load(kBase);
        std::string err;
        CHECK(setField(p, "tl_2", "clp_3", "out", "23.0", err));      // the derived version overrides clp_3 …
        CHECK(dropNode(p, "tl_1", "clp_3", err));                     // … and the base deletes it
        CHECK(!p.clip("clp_3") && p.tlset("tl_2", "clp_3"));          // erased in the base; the delta stays
        ResolvedTimeline r2 = res(p, "tl_2");
        CHECK(r2.dangling.size() == 1);
        CHECK(r2.dangling[0].target == "clp_3" && r2.dangling[0].kind == "tlset" &&
              r2.dangling[0].delta == "tlset:tl_2:clp_3" && r2.dangling[0].why.find("no longer has clp_3") != std::string::npos);
        rt(p);                                                        // a dangling delta never makes a file unopenable
        // freshId never hands out an id a dangling delta still points at
        Project q = p;
        CHECK(q.freshId("clp_") == "clp_4");

        RebaseReport dry = rebase(p, "tl_2", true, true);
        CHECK(dry.pruned == 1 && dry.dangling.size() == 1 && p.tlset("tl_2", "clp_3"));   // dry run changes nothing
        CHECK(dry.message.find("would prune 1") != std::string::npos);
        RebaseReport report = rebase(p, "tl_2", false, false);
        CHECK(report.pruned == 0 && p.tlset("tl_2", "clp_3"));                             // reported, not pruned
        report = rebase(p, "tl_2", true, false);
        CHECK(report.pruned == 1 && !p.tlset("tl_2", "clp_3"));
        CHECK(res(p, "tl_2").dangling.empty());
        rt(p);

        // A version's OWN clip on a track the base deleted is reported — and never pruned.
        Project o = load(std::string(kBase) + "#track id=trk_2 name=v1 timeline=tl_1 kind=video order=1\n"
                                              "#clip id=clp_9 track=trk_2 timeline=tl_2 src=ro_2 in=0 out=2\n");
        CHECK(dropNode(o, "tl_1", "trk_2", err));
        CHECK(o.clip("clp_9"));                                       // another version's content survives
        r2 = res(o, "tl_2");
        CHECK(r2.dangling.size() == 1 && r2.dangling[0].kind == "clip" && prov(r2, "clp_9") == Provenance::Dangling);
        CHECK(rebase(o, "tl_2", true, false).pruned == 0 && o.clip("clp_9"));
        rt(o);

        // A conflict: the base trims a clip so the version's override leaves it no frames.
        Project c = load(std::string(kBase) + "#tlset timeline=tl_2 node=clp_2 in=13.500\n");
        CHECK(setField(c, "tl_1", "clp_2", "out", "13.0", err));
        r2 = res(c, "tl_2");
        CHECK(r2.dangling.size() == 1 && r2.dangling[0].kind == "conflict" && prov(r2, "clp_2") == Provenance::Dangling);
        CHECK(rebase(c, "tl_2", true, false).pruned == 0);
        rt(c);
    }

    void gradeDeltasNearestWins()
    {
        Project p = load(std::string(kBase) + "#timeline id=tl_3 name=reel base=tl_2 order=2\n");
        std::string err;
        CHECK(setGrade(p, "tl_1", "ro_2", "exposure", 0.1, err) && setGrade(p, "tl_1", "ro_2", "contrast", 0.2, err));
        CHECK(setGrade(p, "tl_1", "ro_2", "tint", 0.05, err));
        CHECK(setGrade(p, "tl_2", "ro_2", "exposure", 0.3, err));
        CHECK(setGrade(p, "tl_3", "s_a", "contrast", 0.5, err) && setGrade(p, "reel", "ro_2", "temp", -1.0, err));
        auto asMap = [](const std::vector<std::pair<std::string, double>> &v) {
            std::map<std::string, double> m(v.begin(), v.end());
            return m;
        };
        auto g3 = gradeDeltas(p, "tl_3", "ro_2");
        CHECK(g3.size() == 4 && g3[0].first == "contrast");          // nearest first
        auto m3 = asMap(g3);
        CHECK(m3["exposure"] == 0.3 && m3["contrast"] == 0.5 && m3["temp"] == -1.0 && m3["tint"] == 0.05);
        auto m2 = asMap(gradeDeltas(p, "tl_2", "ro_2"));
        CHECK(m2.size() == 3 && m2["exposure"] == 0.3 && m2["contrast"] == 0.2);
        CHECK(asMap(gradeDeltas(p, "tl_1", "ro_2"))["exposure"] == 0.1);
        CHECK(gradeDeltas(p, "tl_3", "gr1").empty());
        // A zero delta still wins: it is "no change against the rack", overriding the base's +0.3.
        CHECK(setGrade(p, "tl_3", "ro_2", "exposure", 0.0, err));
        CHECK(asMap(gradeDeltas(p, "tl_3", "ro_2"))["exposure"] == 0.0);
        CHECK(clearGrade(p, "tl_3", "ro_2", "exposure", err));
        CHECK(asMap(gradeDeltas(p, "tl_3", "ro_2"))["exposure"] == 0.3);   // inherits again
        CHECK(!clearGrade(p, "tl_3", "ro_2", "lift", err) && err.find("contrast") != std::string::npos);

        // A pin is read-only for colour, refused naming the commit — and it stops the walk.
        CHECK(pinColour(p, "tl_2", "8f2c1ab", err));
        CHECK(!setGrade(p, "tl_2", "ro_2", "exposure", 0.9, err) && err.find("8f2c1ab") != std::string::npos);
        CHECK(!clearGrade(p, "tl_2", "ro_2", "", err));
        auto pinned = asMap(gradeDeltas(p, "tl_3", "ro_2"));
        CHECK(pinned.count("exposure") && !pinned.count("tint"));     // tl_1's tint is behind the pin
        CHECK(!pinColour(p, "tl_1", "abc", err));                     // a root has no base colour
        rt(p);
        CHECK(unpinColour(p, "tl_2", err));
        CHECK(asMap(gradeDeltas(p, "tl_3", "ro_2")).count("tint"));
        CHECK(clearGrade(p, "tl_3", "ro_2", "", err) && !p.tlgrade("tl_3", "ro_2"));
        CHECK(!setGrade(p, "tl_2", "clp_1", "exposure", 0.1, err) && err.find("bind names") != std::string::npos);
        rt(p);
    }

    void freezeThenThaw()
    {
        Project p = load(kBase);
        std::string err;
        NodeId local, tr;
        CHECK(setField(p, "tl_2", "clp_2", "out", "13.0", err));      // an override …
        CHECK(dropNode(p, "tl_2", "clp_3", err));                     // … a drop …
        CHECK(arrange::addClip(p, "tl_2", "trk_1", "s_a", 0.0, 2.0, 7.0, "", local, err));   // … a local on an inherited track
        CHECK(arrange::addTransition(p, "tl_2", "clp_2", local, "dissolve", 0.5, tr, err)); // … between inherited and local
        CHECK(p.clip(local)->timeline == "tl_2" && p.transition(tr)->timeline == "tl_2");
        const std::string before = p.serialize();
        const ResolvedTimeline r0 = res(p, "tl_2");
        rt(p);

        CHECK(freezeCut(p, "tl_2", err));
        CHECK(p.timeline("tl_2")->frozen());
        for (const auto &s : p.sets) CHECK(s.timeline != "tl_2");     // folded into the copies
        for (const auto &d : p.drops) CHECK(d.timeline != "tl_2");
        ResolvedTimeline rf = res(p, "tl_2");
        for (const auto &kv : rf.provenance) CHECK(kv.second == Provenance::Local);
        CHECK(rf.clips.size() == r0.clips.size() && rf.transitions.size() == r0.transitions.size());
        for (const auto &c : rf.clips)
        {
            const Clip *o = rclip(r0, c.from.empty() ? c.id : c.from);
            CHECK(o && o->at == c.at && o->in == c.in && o->out == c.out);
        }
        CHECK(p.clip(local)->timeline.empty() || p.clip(local)->timeline == "tl_2");
        CHECK(p.track(p.clip(local)->track)->timeline == "tl_2");   // re-homed onto the local copy of v0
        // Frozen means a base re-cut no longer arrives.
        Project q = p;
        CHECK(setField(q, "tl_1", "clp_1", "out", "3.0", err));
        for (const auto &c : res(q, "tl_2").clips)
            if (c.from == "clp_1") CHECK(c.out == 4.0);
        rt(p);
        CHECK(!freezeCut(p, "tl_2", err) && err.find("already frozen") != std::string::npos);
        CHECK(diff(p, "tl_2").find("override  clp_2") != std::string::npos);   // what a thaw would record

        CHECK(thawCut(p, "tl_2", err));
        CHECK(!p.timeline("tl_2")->frozen());
        CHECK(p.serialize() == before);                               // the same deltas, byte for byte
        ResolvedTimeline rt2 = res(p, "tl_2");
        CHECK(rt2.clips.size() == r0.clips.size() && rt2.provenance == r0.provenance);
        for (const auto &c : rt2.clips)
            CHECK(rclip(r0, c.id) && rclip(r0, c.id)->out == c.out && rclip(r0, c.id)->track == c.track);
        rt(p);

        // An edit made WHILE frozen comes back as a delta, a base clip deleted meanwhile leaves
        // its copy as plain local content.
        CHECK(freezeCut(p, "tl_2", err));
        NodeId copyOf1;
        for (const auto &c : p.clips) if (c.from == "clp_1") copyOf1 = c.id;
        CHECK(setField(p, "tl_2", copyOf1, "opacity", "0.25", err));
        CHECK(thawCut(p, "tl_2", err));
        CHECK(p.tlset("tl_2", "clp_1") && p.tlset("tl_2", "clp_1")->fields[0].second == "0.25");
        CHECK(!p.clip(copyOf1));
        rt(p);
    }

    void freezeCarriesDescendants()
    {
        // tl_3 derives from tl_2. Freezing tl_2 replaces the nodes tl_3 sees with tl_2's copies;
        // tl_3's own override and its own clip must follow, or a freeze upstream would silently
        // break every version downstream of it.
        Project p = load(std::string(kBase) + "#timeline id=tl_3 name=reel base=tl_2 order=2\n");
        std::string err;
        NodeId local;
        CHECK(setField(p, "tl_3", "clp_1", "opacity", "0.5", err));
        CHECK(arrange::addClip(p, "tl_3", "trk_1", "s_a", 0.0, 1.0, 20.0, "", local, err));
        CHECK(p.clip(local)->timeline == "tl_3");
        const std::string before = p.serialize();
        CHECK(freezeCut(p, "tl_2", err));
        NodeId copy;
        for (const auto &c : p.clips) if (c.from == "clp_1") copy = c.id;
        CHECK(!copy.empty() && p.tlset("tl_3", copy) && !p.tlset("tl_3", "clp_1"));
        ResolvedTimeline r3 = res(p, "tl_3");
        CHECK(r3.dangling.empty() && near(rclip(r3, copy)->opacity, 0.5) && prov(r3, local) == Provenance::Local);
        CHECK(p.clip(local)->timeline == "tl_3" && p.track(p.clip(local)->track)->timeline == "tl_2");
        rt(p);
        CHECK(thawCut(p, "tl_2", err));
        CHECK(p.serialize() == before);
        CHECK(res(p, "tl_3").dangling.empty());
        rt(p);
    }

    void renameRewritesRefs()
    {
        std::string doc = kBase;
        doc.insert(doc.find("#rack"), "current = main\n");               // a header ref by bind name
        doc += "#clip id=clp_8 name=c8 track=v0 src=s_a at=20.0 in=0 out=1\n";   // refs by bind name
        doc += "#atrack id=atr_2 name=bass kind=instrument order=1\n";
        doc += "#aauto id=au_1 node=s_a param=weight\n  0.000 = 0.5\n";          // a PRESERVED ref by name
        doc += "#asend id=sd_1 from=atr_2 to=atr_2 gain=-6.0\n";
        Project p = load(doc);
        CHECK(p.current == "tl_1" && p.clip("clp_8")->src == "ro_2" && p.clip("clp_8")->track == "trk_1");
        std::string err;
        const std::string before = p.serialize();
        CHECK(!p.rename("ro_2", "gr1", err) && err.find("already used") != std::string::npos);
        CHECK(!p.rename("ro_2", "2day", err));
        CHECK(!p.rename("ro_2", "frame", err) && err.find("reserved") != std::string::npos);
        CHECK(!p.rename("ro_2", "clp_1", err));                       // an id is taken too
        CHECK(!p.rename("rk_1", "rack1", err) && err.find("no bind name") != std::string::npos);
        CHECK(p.serialize() == before);                               // refusals change nothing

        CHECK(p.rename("ro_2", "day01", err));
        const std::string after = p.serialize();
        CHECK(after.find("s_a") == std::string::npos);                // EVERY reference followed
        CHECK(after.find("#rackobj id=ro_2 name=day01") != std::string::npos);
        CHECK(after.find("#aauto id=au_1 node=day01 param=weight") != std::string::npos);
        CHECK(p.clip("clp_8")->src == "ro_2" && p.idForRef("day01") == "ro_2" && p.idForRef("s_a").empty());
        rt(p);
        // A preserved node can be renamed too; only the name token of its verbatim text changes.
        CHECK(p.rename("atr_2", "bassline", err));
        CHECK(p.serialize().find("#atrack id=atr_2 name=bassline kind=instrument order=1") != std::string::npos);
        CHECK(p.rename("tl_2", "social30s", err) && p.idForRef("social30s") == "tl_2");
        rt(p);
    }

    void arrangeOps()
    {
        std::string err;
        Project p = load(kBase);
        // trim head: in and at move together, so the remaining frames stay put
        const Clip c2 = *p.clip("clp_2");
        CHECK(arrange::trim(p, "tl_1", "clp_2", arrange::Edge::Head, 5.0, err));
        const Clip *c = p.clip("clp_2");
        CHECK(c->at == 5.0 && c->in == 11.0 && c->out == 14.0);
        for (double t : {5.5, 6.0, 7.25}) CHECK(near(sourceAt(*c, t), sourceAt(c2, t)));
        CHECK(arrange::trim(p, "tl_1", "clp_2", arrange::Edge::Tail, 7.0, err) && p.clip("clp_2")->out == 13.0);
        // refused, not clamped
        CHECK(!arrange::trim(p, "tl_1", "clp_2", arrange::Edge::Head, 7.0, err) && err.find("no frames") != std::string::npos);
        CHECK(!arrange::trim(p, "tl_1", "clp_2", arrange::Edge::Tail, 5.0, err) && err.find("no frames") != std::string::npos);
        CHECK(!arrange::trim(p, "tl_1", "clp_2", arrange::Edge::Tail, 5.4, err) && err.find("tr_1") != std::string::npos);
        CHECK(!arrange::trim(p, "tl_1", "clp_3", arrange::Edge::Head, -10.0, err));
        CHECK(p.clip("clp_2")->in == 11.0 && p.clip("clp_2")->out == 13.0);
        rt(p);

        // every op on an inherited clip records deltas and never grows the clip count
        const size_t clips = p.clips.size();
        CHECK(arrange::trim(p, "tl_2", "clp_3", arrange::Edge::Head, 9.0, err));
        CHECK(p.clips.size() == clips && p.clip("clp_3")->at == 8.0);
        CHECK(p.tlset("tl_2", "clp_3")->fields.size() == 2);          // in and at, as one edit
        CHECK(arrange::move(p, "tl_2", "clp_3", 15.0, "", err));
        CHECK(p.tlset("tl_2", "clp_3")->fields.size() == 2 && near(rclip(res(p, "tl_2"), "clp_3")->at, 15.0));
        CHECK(!arrange::move(p, "tl_2", "clp_3", -1.0, "", err));
        CHECK(p.clips.size() == clips);
        rt(p);

        // roll: two adjacent clips, the cut moves, the total length does not
        Project r = load(kBase);
        CHECK(arrange::roll(r, "tl_2", "clp_1", "clp_2", 3.0, err));
        ResolvedTimeline rr = res(r, "tl_2");
        CHECK(near(rclip(rr, "clp_1")->out, 3.0) && near(rclip(rr, "clp_2")->in, 9.0) && near(rclip(rr, "clp_2")->at, 3.0));
        CHECK(near(rclip(rr, "clp_1")->duration() + rclip(rr, "clp_2")->duration(), 8.0));
        CHECK(r.clips.size() == 3 && r.sets.size() == 2);
        CHECK(!arrange::roll(r, "tl_2", "clp_1", "clp_2", 0.0, err) && err.find("no frames") != std::string::npos);
        CHECK(!arrange::roll(r, "tl_2", "clp_1", "clp_3", 5.0, err) && err.find("adjacent") != std::string::npos);
        CHECK(r.sets.size() == 2 && near(rclip(res(r, "tl_2"), "clp_1")->out, 3.0));   // refusal left nothing behind
        rt(r);

        // slip: the source window slides, position and length kept
        CHECK(arrange::slip(r, "tl_1", "clp_3", 1.5, err));
        CHECK(r.clip("clp_3")->in == 21.5 && r.clip("clp_3")->out == 25.5 && r.clip("clp_3")->at == 8.0);
        CHECK(!arrange::slip(r, "tl_1", "clp_3", -30.0, err) && err.find("first frame") != std::string::npos);
        rt(r);

        // remove + ripple in a derived version: a drop, and later clips shift left — by delta
        Project d = load(kBase);
        CHECK(arrange::remove(d, "tl_2", "clp_2", true, err));
        CHECK(d.clips.size() == 3 && d.tldrop("tl_2", "clp_2"));
        ResolvedTimeline rd = res(d, "tl_2");
        CHECK(!has(rd, "clp_2") && !has(rd, "tr_1") && near(rclip(rd, "clp_3")->at, 4.0) && rd.dangling.empty());
        CHECK(d.clip("clp_3")->at == 8.0 && has(res(d, "tl_1"), "clp_2"));
        rt(d);
        // remove without ripple in the base: owned, erased with the transition that touched it
        CHECK(arrange::remove(d, "tl_1", "clp_1", false, err));
        CHECK(!d.clip("clp_1") && !d.transition("tr_1"));
        rt(d);

        // addTransition refuses one longer than a neighbour
        Project t = load(kBase);
        NodeId id;
        CHECK(!arrange::addTransition(t, "tl_1", "clp_2", "clp_3", "dissolve", 5.0, id, err) &&
              err.find("longer than clp_2") != std::string::npos);
        CHECK(!arrange::addTransition(t, "tl_1", "clp_3", "clp_2", "dissolve", 1.0, id, err));
        CHECK(arrange::addTransition(t, "tl_2", "clp_2", "clp_3", "dip", 1.0, id, err));
        CHECK(t.transition(id)->timeline == "tl_2" && !has(res(t, "tl_1"), id));
        rt(t);

        // addTrack / addClip in a derived version are local, and invisible to the base
        NodeId trk, clip;
        CHECK(arrange::addTrack(t, "tl_2", "video", "", trk, err) && t.track(trk)->name == "v1" && t.track(trk)->order == 1);
        CHECK(arrange::addClip(t, "tl_2", trk, "s_a", 1.0, 3.0, 0.0, "", clip, err));
        CHECK(t.clip(clip)->timeline.empty() && t.clip(clip)->name == "s_a_1");   // its track is tl_2's own
        CHECK(!arrange::addClip(t, "tl_2", trk, "nope", 1.0, 3.0, 0.0, "", clip, err) && err.find("s_a") != std::string::npos);
        CHECK(!arrange::addClip(t, "tl_2", trk, "s_a", 3.0, 3.0, 0.0, "", clip, err) && err.find("no frames") != std::string::npos);
        CHECK(!arrange::addTrack(t, "tl_2", "smell", "", trk, err));
        CHECK(!has(res(t, "tl_1"), trk));
        rt(t);
    }

    void frozenEditsAreLocal()
    {
        Project p = load(kBase);
        std::string err;
        CHECK(freezeCut(p, "tl_2", err));
        NodeId copy;
        for (const auto &c : p.clips) if (c.from == "clp_2") copy = c.id;
        const size_t clips = p.clips.size();
        CHECK(setField(p, "tl_2", copy, "out", "13.0", err));
        CHECK(p.sets.empty() && p.clips.size() == clips && p.clip(copy)->out == 13.0);   // owned now
        CHECK(!setField(p, "tl_2", "clp_2", "out", "13.0", err));    // the base's clip is not in a frozen cut
        rt(p);
    }

    void diffReadsLikeAChangeList()
    {
        Project p = load(kBase);
        std::string err;
        NodeId local;
        CHECK(diff(p, "tl_2").find("no changes") != std::string::npos);
        CHECK(setField(p, "tl_2", "clp_2", "out", "13.0", err) && dropNode(p, "tl_2", "clp_3", err));
        CHECK(arrange::addClip(p, "tl_2", "trk_1", "s_a", 0.0, 1.0, 9.0, "tagline", local, err));
        CHECK(setGrade(p, "tl_2", "s_a", "exposure", 0.4, err));
        const std::string d = diff(p, "social");
        CHECK(d.find("a version of tl_1 (main)") != std::string::npos);
        CHECK(d.find("drop      clp_3 (c3)") != std::string::npos);
        CHECK(d.find("override  clp_2 (c2): out 13.000 (base 14.000)") != std::string::npos);
        CHECK(d.find("add       clip " + local + " (tagline)") != std::string::npos);
        CHECK(d.find("grade     ro_2 (s_a): exposure +0.4") != std::string::npos);
        CHECK(diff(p, "tl_1").find("a root") != std::string::npos);
    }

    void saveAndLoad()
    {
        Project p = load(kEvery);
        std::string err;
        const std::string path = "interstellar_model_test.isp";
        CHECK(p.save(path, err));
        Project q;
        CHECK(q.load(path, err) && q.serialize() == kEvery);
        std::remove(path.c_str());
        CHECK(!q.load("/nonexistent/dir/x.isp", err) && err.find("cannot read") != std::string::npos);
    }

    void freshIdNeverReuses()
    {
        Project p = load(kBase);
        const NodeId a = p.freshId("clp_");
        const NodeId b = p.freshId("clp_");
        CHECK(a == "clp_4" && b == "clp_5");                          // a and b were never added …
        CHECK(p.freshId("clp_") == "clp_6");                          // … and still are not reissued
        CHECK(p.freshId("zz_") == "zz_1");
        CHECK(p.freshName("Tokyo Night") == "Tokyo_Night" && p.freshName("c1") == "c12");
        std::string why;
        CHECK(Project::nameIsLegal("_ok9", why) && !Project::nameIsLegal(std::string(33, 'a'), why));
    }
}

int main()
{
    roundTripEveryNode();
    messyInputCanonicalises();
    colourOnClipRefused();
    nanRepairedAndCounted();
    structuralRefusals();
    resolveInheritance();
    derivedEditRecordsDeltaNotCopy();
    ownedEditsAndRefusals();
    splitInheritedClip();
    danglingAndRebase();
    gradeDeltasNearestWins();
    freezeThenThaw();
    freezeCarriesDescendants();
    renameRewritesRefs();
    arrangeOps();
    frozenEditsAreLocal();
    diffReadsLikeAChangeList();
    saveAndLoad();
    freshIdNeverReuses();
    std::printf("interstellar_model_tests: PASS (%d checks, 19 groups)\n", gChecks);
    return 0;
}
