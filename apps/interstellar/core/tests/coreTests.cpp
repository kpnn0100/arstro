/*
 *  interstellar_core_tests — L2: the real service, driven headlessly (arstro.rule §4).
 *
 *  No display, no codec, no clicking. A fake rack is a map of doubles, so the whole application
 *  — the .isp round trip, the cut, the address space, automation, bindings and the compositor —
 *  is provable in milliseconds. That is what the `RackAccess` seam bought and it is why the
 *  architecture was amended to keep it.
 *
 *  Plain assert(), matching the neighbouring suites. NDEBUG is undefined first for cosmo's D-43
 *  reason: a Release build would otherwise turn every assertion into `((void)0)` and the suite
 *  would print [PASS] while checking nothing. It bit gene_tests on its first run, in this very
 *  session, and it was checked here by deliberately breaking an assertion.
 */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "Automation.h"
#include "BindingGraph.h"
#include "Composite.h"
#include "Evaluator.h"
#include "ParamRegistry.h"
#include "Project.h"
#include "Timeline.h"
#include "service/AppModelCodec.h"
#include "service/InterstellarService.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace arstro::interstellar;

namespace
{
    /** A rack that is a map of doubles. Everything the evaluator needs from the hosted Cosmo
     *  service, with none of the cost — this is the fake the plan calls the most valuable test
     *  asset in the project, for the frame source's sibling reason. */
    class FakeRack : public RackAccess
    {
    public:
        std::vector<Node> mNodes;
        std::map<std::string, std::map<std::string, double>> mParams;
        bool mPinned = false;
        int writes = 0;

        std::vector<Node> nodes() const override { return mNodes; }
        bool getParam(const std::string &n, const std::string &leaf, double &out) const override
        {
            auto it = mParams.find(n);
            if (it == mParams.end()) return false;
            auto p = it->second.find(leaf);
            if (p == it->second.end()) return false;
            out = p->second;
            return true;
        }
        bool setParam(const std::string &n, const std::string &leaf, double v, std::string &err) override
        {
            if (mPinned) { err = "the rack is pinned at cafe123 — it is read-only"; return false; }
            if (!mParams.count(n)) { err = "no such rack node: " + n; return false; }
            mParams[n][leaf] = v;
            ++writes;
            return true;
        }
        bool isPinned() const override { return mPinned; }
        std::string pinCommit() const override { return "cafe123"; }

        void addNode(const std::string &id, const std::string &name, bool group, int parent, int depth)
        {
            Node n;
            n.id = id; n.cosmoName = name; n.group = group; n.parent = parent; n.depth = depth;
            mNodes.push_back(n);
            // Every scalar Cosmo parameter starts at its registry default, as a real slot does.
            for (const auto &e : ParamRegistry::all())
                if (e.obj == ParamRegistry::ObjKind::Rack &&
                    e.owner == ParamRegistry::Owner::Cosmo &&
                    e.type == ParamRegistry::Type::Float)
                    mParams[id][e.key] = e.def;
        }
    };

    /** A frame source that ENCODES ITS FRAME INDEX IN THE PIXELS, so a test can assert that the
     *  right source frame reached the right output frame — through speed changes, transitions and
     *  the cache — with no codec, no media file and no tolerance.
     *
     *  This is the synthetic counterpart of the burned-in frame counters used to verify the real
     *  FFmpeg path, and it is the cheaper of the two: it needs nothing installed. */
    class CountingFrameSource : public IFrameSource
    {
    public:
        static int decodes;          // how many frames were actually produced — the cache's proof
        static long long lastAsked;

        bool open(const std::string &path, Info &out) override
        {
            if (path.find("missing") != std::string::npos) return false;
            mInfo.width = 8;
            mInfo.height = 8;
            mInfo.fps = 24.0;
            mInfo.frames = 240;
            out = mInfo;
            return true;
        }
        bool frameAt(long long frame, Raster &out) override
        {
            ++decodes;
            lastAsked = frame;
            if (frame < 0) frame = 0;
            if (frame >= mInfo.frames) frame = mInfo.frames - 1;
            out.allocate(mInfo.width, mInfo.height, 255);
            // The index in the red channel, so `frameIndexOf` can read it back out of a
            // composited frame.
            for (size_t i = 0; i < out.rgba.size(); i += 4)
            {
                out.rgba[i + 0] = (uint8_t)(frame & 0xFF);
                out.rgba[i + 1] = 0;
                out.rgba[i + 2] = 0;
                out.rgba[i + 3] = 255;
            }
            return true;
        }

    private:
        Info mInfo;
    };
    int CountingFrameSource::decodes = 0;
    long long CountingFrameSource::lastAsked = -1;

    /** The frame index a composited raster reports, read from its centre pixel. */
    int frameIndexOf(const Raster &r)
    {
        if (r.empty()) return -1;
        const size_t mid = ((size_t)(r.height / 2) * r.width + r.width / 2) * 4;
        return mid + 3 < r.rgba.size() ? r.rgba[mid] : -1;
    }

    struct Fixture
    {
        FakeRack rack;
        InterstellarService svc;
        std::vector<std::string> events;

        Fixture() : svc([&] {
            InterstellarService::Hooks h;
            h.rack = &rack;
            h.makeFrameSource = []() -> std::unique_ptr<IFrameSource> {
                return std::unique_ptr<IFrameSource>(new CountingFrameSource());
            };
            return h;
        }())
        {
            svc.subscribe([this](const Event &e) { events.push_back(formatEvent(e)); });
        }

        bool run(const std::string &line)
        {
            std::string err;
            return svc.dispatchText(line, err);
        }
        void must(const std::string &line)
        {
            if (!run(line))
            {
                std::fprintf(stderr, "command failed: %s\n  %s\n", line.c_str(),
                             svc.model().lastError.c_str());
                assert(false);
            }
        }
        bool sawEventPrefix(const std::string &prefix) const
        {
            for (const auto &e : events)
                if (e.rfind(prefix, 0) == 0) return true;
            return false;
        }
    };

    /** The scenario the whole design is organised around: two groups in the rack, a two-shot
     *  cut, one shape driving two parameters, one binding. */
    void seedProject(Fixture &f)
    {
        f.rack.addNode("cn_1", "Day interiors", true, -1, 0);
        f.rack.addNode("cn_41", "DSC01.MOV", false, 0, 1);
        f.rack.addNode("cn_58", "DSC02.MOV", false, 0, 1);
        f.must("project new /tmp/isp-test.isp --fps 24 --res 1920x1080");
        f.must("rack import /tmp/japan18.cmp");
        // The bind names are Interstellar's, derived from Cosmo's own names.
        f.must("rename day_interiors gr1");
        f.must("rename dsc01_mov s_day01");
        f.must("rename dsc02_mov s_day02");
        f.must("track add --kind video --name v0");
        f.must("clip add --track v0 --src rack:cn_41 --in 12.4 --out 16.6 --at 0 --name clp_a");
        f.must("clip add --track v0 --src rack:cn_58 --in 88.0 --out 91.1 --at 4.2 --name clp_b");
    }

    // ─────────────────────────────────────────────────────────────────────────────────────────

    void test_isp_roundtrip_is_a_fixed_point()
    {
        Fixture f;
        seedProject(f);
        f.must("auto new ac_push --dur 2.0 --points 0=0,1=1 --ease easeInOut");
        f.must("auto link ac_push -> gr1.basic.exposure --at 2.0 --from 0 --to 0.8");
        f.must("bind gr1.opacity = clamp(gr1.basic.exposure / 2 + 0.5, 0, 1)");
        f.must("marker add --at 48 --name chorus --note \"chorus in\"");
        f.must("transition add --between clp_a,clp_b --kind dissolve --dur 0.5");

        const std::string once = f.svc.project().serialize();
        std::string err;
        // The P1 gate and the cheapest test in the project: parse -> serialize is a fixed point,
        // byte for byte, or "no change" is not "no diff" and a content hash is meaningless.
        assert(Project::roundTripsExactly(once, err));
        Project again;
        assert(again.parse(once, err));
        assert(again.serialize() == once);
        // And the reparsed project is the same project, not merely the same text.
        assert(again.clips.size() == 2);
        assert(again.autoClips.size() == 1);
        assert(again.autoLinks.size() == 1);
        assert(again.bindings.size() == 1);
        assert(again.rackObjs.size() == 3);
        std::printf("[PASS] .isp parse -> serialize is a byte-exact fixed point\n");
    }

    void test_unknown_keys_and_comments_survive_a_round_trip()
    {
        // Nebula's forward compatibility: a newer file must open in an older build without
        // losing data (R-FMT-4).
        const char *text =
            "arstro-project = 1\napp = interstellar\nid = prj_x\nname = x\nfps = 24.0\n"
            "width = 640\nheight = 480\npar = 1.0\ncolorspace = rec709\n"
            "futureHeaderKey = 7\n"
            "#track id=trk_1 name=v0 kind=video order=0 someFutureKey=hello\n";
        Project p;
        std::string err;
        assert(p.parse(text, err));
        const std::string out = p.serialize();
        assert(out.find("futureHeaderKey = 7") != std::string::npos);
        assert(out.find("someFutureKey=hello") != std::string::npos);
        assert(Project::roundTripsExactly(out, err));
        std::printf("[PASS] unknown keys are preserved verbatim and still round-trip\n");
    }

    void test_a_clip_may_not_carry_colour()
    {
        // R-CUT-2. Asserted, because "it is rejected" is a claim about a code path nothing else
        // exercises — and ignoring the key instead would silently discard a user's edit.
        const char *text =
            "arstro-project = 1\napp = interstellar\nid = p\nname = n\nfps = 24.0\n"
            "width = 64\nheight = 48\npar = 1.0\ncolorspace = rec709\n"
            "#track id=trk_1 name=v0 kind=video order=0\n"
            "#clip id=clp_1 name=c1 track=trk_1 src=rack:cn_1 at=0 in=0 out=1 grade=exposure:0.3\n";
        Project p;
        std::string err;
        assert(!p.parse(text, err));
        assert(err.find("carries no colour") != std::string::npos);
        assert(err.find("grade") != std::string::npos);
        std::printf("[PASS] a colour field on a clip is REFUSED, naming it: %s\n", err.c_str());
    }

    void test_a_nan_is_repaired_not_refused()
    {
        // The other half of the same decision: a structural error refuses, a numeric corruption
        // repairs and reports (cosmo's D-36 — one stray nan must not make a project unopenable).
        const char *text =
            "arstro-project = 1\napp = interstellar\nid = p\nname = n\nfps = 24.0\n"
            "width = 64\nheight = 48\npar = 1.0\ncolorspace = rec709\n"
            "#track id=trk_1 name=v0 kind=video order=0 opacity=nan\n";
        Project p;
        std::string err;
        int repaired = 0;
        assert(p.parse(text, err, &repaired));
        assert(repaired == 1);
        assert(p.tracks[0].opacity == 1.0);
        std::printf("[PASS] a non-finite value is repaired to its default and counted\n");
    }

    void test_the_cut_operations()
    {
        Fixture f;
        seedProject(f);
        // One script for the whole of R-CUT-3, which is the point: every cut operation is a
        // Command, so the timeline is editable with no display (R-CUT-7).
        f.must("clip split clp_a --at 2.0");
        assert(f.svc.project().clips.size() == 3);
        const Clip *a = f.svc.project().clip("clp_a");
        assert(a && std::fabs(a->out - 14.4) < 1e-9);   // 12.4 + 2.0 at speed 1

        f.must("clip move clp_b --at 6.0");
        assert(std::fabs(f.svc.project().clip("clp_b")->at - 6.0) < 1e-9);

        f.must("clip slip clp_b --by 1.0");
        assert(std::fabs(f.svc.project().clip("clp_b")->in - 89.0) < 1e-9);
        assert(std::fabs(f.svc.project().clip("clp_b")->out - 92.1) < 1e-9);  // length preserved

        f.must("clip trim clp_b --out 91.0");
        assert(std::fabs(f.svc.project().clip("clp_b")->out - 91.0) < 1e-9);

        const size_t before = f.svc.project().clips.size();
        f.must("clip delete clp_b");
        assert(f.svc.project().clips.size() == before - 1);

        // A trim that would leave no frames is refused, not clamped: clamping would silently
        // produce a clip the user did not ask for.
        assert(!f.run("clip trim clp_a --out 12.0"));
        assert(f.svc.model().lastError.find("no frames") != std::string::npos);
        std::printf("[PASS] add/split/move/slip/trim/delete, and an empty trim is refused\n");
    }

    void test_a_typo_is_refused_naming_the_nearest_candidate()
    {
        // cosmo's D-59 made impossible. `set exposre=1.2` returning success is how an agent
        // builds everything after it on a state that never changed — and this address space is
        // hundreds of names deep, so the typo is weekly rather than rare.
        Fixture f;
        seedProject(f);
        assert(!f.run("set gr1.basic.exposer=0.4"));
        const std::string e = f.svc.model().lastError;
        assert(e.find("no such parameter") != std::string::npos);
        assert(e.find("basic.exposure") != std::string::npos);   // names the nearest candidate
        assert(f.sawEventPrefix("[evt] command.rejected"));

        assert(!f.run("set gr7.basic.exposure=0.4"));
        assert(f.svc.model().lastError.find("no such object") != std::string::npos);
        assert(f.svc.model().lastError.find("gr1") != std::string::npos);

        // A bare token that is not an assignment is refused too, rather than swallowed.
        assert(!f.run("set gr1.basic.exposure"));
        std::printf("[PASS] a typo is refused and the nearest candidate is named\n");
    }

    void test_a_colour_edit_reaches_the_rack()
    {
        // R-COSMO-3, at the seam: `set gr1.basic.exposure` must become a WRITE to the hosted
        // Cosmo project, not a value stored in the .isp. The .isp carries no colour at all.
        Fixture f;
        seedProject(f);
        assert(f.rack.writes == 0);
        f.must("set gr1.basic.exposure=0.2");
        assert(f.rack.writes == 1);
        assert(std::fabs(f.rack.mParams["cn_1"]["exposure"] - 0.2) < 1e-9);
        // And nothing colour-shaped appeared in the project text.
        const std::string isp = f.svc.project().serialize();
        assert(isp.find("exposure") == std::string::npos);
        assert(f.sawEventPrefix("[evt] param.changed gr1.basic.exposure"));
        std::printf("[PASS] a colour edit writes THROUGH to the rack; the .isp stays colour-free\n");
    }

    void test_a_pinned_rack_refuses_every_colour_write()
    {
        // R-COSMO-5 / R-VCS-6: a pin that yields is not a pin.
        Fixture f;
        seedProject(f);
        f.rack.mPinned = true;
        assert(!f.run("set gr1.basic.exposure=0.2"));
        assert(f.svc.model().lastError.find("pinned") != std::string::npos);
        assert(f.rack.writes == 0);
        // An Interstellar-owned parameter on a rack object is NOT colour and stays writable —
        // the grade weight lives in the .isp.
        f.must("set gr1.opacity=0.5");
        std::printf("[PASS] a pinned rack refuses colour writes and still allows the grade weight\n");
    }

    void test_one_shape_drives_two_parameters()
    {
        // *"multi param can use the same automation"* — the requirement that shaped the whole
        // automation model (R-AUTO-2). One shape, two addresses, two unit ranges; move one
        // breakpoint and BOTH resolved values change.
        Fixture f;
        seedProject(f);
        f.must("auto new ac_push --dur 2.0 --points 0=0,1=1 --ease linear");
        f.must("auto link ac_push -> gr1.basic.exposure --at 2.0 --dur 2.0 --from 0 --to 0.8");
        f.must("auto link ac_push -> clp_a.geom.scale --at 2.0 --dur 2.0 --from 1.0 --to 1.08");

        std::string out;
        auto valueAt = [&](const char *addr, double t) {
            double v = 0;
            std::string txt;
            assert(f.svc.evalAddress(addr, t, false, txt));
            // Parse the number back out of the printed form, so the test asserts what a user
            // would actually read.
            const auto eq = txt.find("= ");
            v = std::atof(txt.c_str() + eq + 2);
            return v;
        };
        // Midway through the shape: linear, so half of each range.
        assert(std::fabs(valueAt("gr1.basic.exposure", 3.0) - 0.4) < 1e-6);
        assert(std::fabs(valueAt("clp_a.geom.scale", 3.0) - 1.04) < 1e-6);
        // Outside it: each parameter is its STATIC value (R-AUTO-5).
        assert(std::fabs(valueAt("gr1.basic.exposure", 0.5) - 0.0) < 1e-6);
        assert(std::fabs(valueAt("clp_a.geom.scale", 0.5) - 1.0) < 1e-6);

        // Now move ONE breakpoint of the shared shape and watch BOTH follow. The shape's end
        // value drops from 1.0 to 0.5, so half way along it is 0.25 rather than 0.5 — and each
        // parameter maps that into its own range: 0.25 of 0..0.8 EV, and 0.25 of 1.0..1.08.
        f.must("auto point ac_push --at 2.0 --value 0.5");
        assert(std::fabs(valueAt("gr1.basic.exposure", 3.0) - 0.2) < 1e-6);
        assert(std::fabs(valueAt("clp_a.geom.scale", 3.0) - 1.02) < 1e-6);
        std::printf("[PASS] one shape drives two parameters, and one breakpoint moves both\n");
    }

    void test_overlapping_links_on_one_address_are_refused()
    {
        // R-AUTO-4: two producers for one value is a picture that depends on evaluation order,
        // and a renderer whose output depends on evaluation order cannot be tested.
        Fixture f;
        seedProject(f);
        f.must("auto new ac_a --dur 2.0 --points 0=0,1=1");
        f.must("auto link ac_a -> gr1.basic.exposure --at 1.0 --dur 2.0 --from 0 --to 1");
        assert(!f.run("auto link ac_a -> gr1.basic.exposure --at 2.0 --dur 2.0 --from 0 --to 1"));
        assert(f.svc.model().lastError.find("two authorities") != std::string::npos);
        // Adjacent, not overlapping, is fine.
        f.must("auto link ac_a -> gr1.basic.exposure --at 3.0 --dur 2.0 --from 0 --to 1");
        std::printf("[PASS] overlapping links are refused; adjacent ones are allowed\n");
    }

    void test_a_non_automatable_address_is_refused_with_the_reason()
    {
        Fixture f;
        seedProject(f);
        f.must("auto new ac_a --dur 1.0 --points 0=0,1=1");
        assert(!f.run("auto link ac_a -> clp_a.blend --at 0 --from 0 --to 1"));
        assert(f.svc.model().lastError.find("cannot be automated") != std::string::npos);
        std::printf("[PASS] a structural parameter cannot be automated, and says why\n");
    }

    void test_the_user_binding_example()
    {
        // R-BIND-1's own example, end to end: bind gr1.opacity to gr1.basic.exposure.
        Fixture f;
        seedProject(f);
        f.must("bind gr1.opacity = clamp(gr1.basic.exposure / 2 + 0.5, 0, 1)");

        auto opacityNow = [&]() {
            std::string txt;
            assert(f.svc.evalAddress("gr1.opacity", 0.0, false, txt));
            return std::atof(txt.c_str() + txt.find("= ") + 2);
        };
        f.must("set gr1.basic.exposure=0.8");
        assert(std::fabs(opacityNow() - 0.9) < 1e-6);
        f.must("set gr1.basic.exposure=4.0");
        assert(std::fabs(opacityNow() - 1.0) < 1e-6);    // clamps high
        f.must("set gr1.basic.exposure=-4.0");
        assert(std::fabs(opacityNow() - 0.0) < 1e-6);    // clamps low

        // A bound parameter is not directly writable: the write would be overwritten on the next
        // frame, so it is refused rather than silently lost (R-EVAL-1).
        assert(!f.run("set gr1.opacity=0.25"));
        assert(f.svc.model().lastError.find("driven by a binding") != std::string::npos);
        std::printf("[PASS] bind gr1.opacity = clamp(gr1.basic.exposure / 2 + 0.5, 0, 1)\n");
    }

    void test_a_cycle_is_refused_naming_both_ends()
    {
        Fixture f;
        seedProject(f);
        f.must("bind s_day01.basic.exposure = gr1.basic.exposure + 1");
        assert(!f.run("bind gr1.basic.exposure = s_day01.basic.exposure - 1"));
        const std::string e = f.svc.model().lastError;
        assert(e.find("cycle") != std::string::npos);
        assert(e.find("gr1.basic.exposure") != std::string::npos);
        assert(e.find("s_day01.basic.exposure") != std::string::npos);
        // And the refusal left the graph exactly as it was: the first binding still works.
        std::string txt;
        assert(f.svc.evalAddress("s_day01.basic.exposure", 0.0, false, txt));
        std::printf("[PASS] a cycle is refused naming both ends: %s\n", e.c_str());
    }

    void test_a_binding_and_a_link_cannot_share_a_parameter()
    {
        // R-BIND-5, enforced rather than resolved by a precedence rule nobody would remember.
        Fixture f;
        seedProject(f);
        f.must("auto new ac_a --dur 1.0 --points 0=0,1=1");
        f.must("auto link ac_a -> gr1.basic.exposure --at 0 --from 0 --to 1");
        assert(!f.run("bind gr1.basic.exposure = 0.5"));
        assert(f.svc.model().lastError.find("ONE producer") != std::string::npos);
        std::printf("[PASS] a parameter cannot have both a binding and a link\n");
    }

    void test_a_binding_reads_an_automation_shape()
    {
        // The explicit way to have a curve AND a calculation: the expression reads the shape's
        // output, so which drives which is written down in the project.
        Fixture f;
        seedProject(f);
        f.must("auto new ac_push --dur 2.0 --points 0=0,1=1 --ease linear");
        f.must("bind clp_a.geom.scale = 1 + ac_push.value * 0.08");
        std::string txt;
        assert(f.svc.evalAddress("clp_a.geom.scale", 1.0, false, txt));
        const double v = std::atof(txt.c_str() + txt.find("= ") + 2);
        assert(std::fabs(v - 1.04) < 1e-6);   // half way along a 2 s linear shape
        std::printf("[PASS] a binding can read an automation shape's output\n");
    }

    void test_the_explain_trace_agrees_with_the_value_it_explains()
    {
        // The defect this guards (found by running `--explain` while writing the debug skill):
        // `ac_push.value` was resolved by a special case inside the Gene scope, so the number the
        // expression USED and the number the trace PRINTED came from two different places — and
        // they disagreed. A binding resolving to 1.08 printed its own input as 0.0, which is
        // exactly the "a value the user cannot account for" failure `--explain` exists to remove.
        Fixture f;
        seedProject(f);
        f.must("auto new ac_push --dur 2.0 --points 0=0,1=1 --ease linear");
        f.must("bind clp_a.geom.scale = 1 + ac_push.value * 0.08");
        std::string txt;
        assert(f.svc.evalAddress("clp_a.geom.scale", 2.0, true, txt));
        // The result, and the input the trace claims produced it.
        const double result = std::atof(txt.c_str() + txt.find("= ") + 2);
        const auto depAt = txt.find("ac_push.value = ");
        assert(depAt != std::string::npos);
        const double dep = std::atof(txt.c_str() + depAt + 16);
        // The trace must be ARITHMETICALLY consistent with the value it explains.
        assert(std::fabs(result - (1.0 + dep * 0.08)) < 1e-9);
        assert(std::fabs(dep - 1.0) < 1e-9);
        std::printf("[PASS] the --explain trace agrees with the value it explains (%.3f from %.3f)\n",
                    result, dep);
    }

    void test_rename_rewrites_every_expression()
    {
        // R-PARAM-2: a rename that leaves a stale name in an expression is worse than one that
        // refuses, so the rewrite is atomic and covers automation targets too.
        Fixture f;
        seedProject(f);
        f.must("auto new ac_a --dur 1.0 --points 0=0,1=1");
        f.must("auto link ac_a -> gr1.basic.exposure --at 0 --from 0 --to 1");
        f.must("bind clp_a.opacity = gr1.opacity * 0.5");
        f.must("rename gr1 daylight");

        const Project &p = f.svc.project();
        assert(p.autoLinks[0].target == "daylight.basic.exposure");
        assert(p.bindings[0].expr == "daylight.opacity * 0.5");
        // And `gr1` no longer resolves, while `daylight` does.
        assert(!f.run("set gr1.opacity=0.5"));
        f.must("set daylight.opacity=0.5");
        std::printf("[PASS] a rename rewrites automation targets AND expressions, atomically\n");
    }

    void test_a_rename_does_not_corrupt_a_longer_name()
    {
        // The textual rewrite is on whole identifiers only: renaming `gr1` must not touch
        // `gr10`, and must not touch a function name that contains it.
        Fixture f;
        seedProject(f);
        f.must("rename s_day02 gr10");
        f.must("bind clp_a.opacity = gr10.opacity * 0.5 + min(1, 1)");
        f.must("rename gr1 z");
        assert(f.svc.project().bindings[0].expr == "gr10.opacity * 0.5 + min(1, 1)");
        std::printf("[PASS] renaming gr1 leaves gr10 and min() alone\n");
    }

    void test_the_boundary_lint_fires_and_says_how_big_the_step_is()
    {
        // R-AUTO-5. A hard change the user asked for is legitimate; one they did not notice is a
        // defect they will blame on the renderer. And a warning nobody has seen printed is a
        // warning that does not work — so the TEXT is asserted, not just the count.
        Fixture f;
        seedProject(f);
        f.must("auto new ac_step --dur 1.0 --points 0=1,1=1");
        f.must("auto link ac_step -> gr1.basic.exposure --at 2.0 --from 0.5 --to 0.5");
        const auto findings = f.svc.lint();
        bool found = false;
        for (const auto &x : findings)
            if (x.address == "gr1.basic.exposure" && x.detail.find("steps by 0.5") != std::string::npos)
                found = true;
        assert(found);
        // With a fade there is no step and no finding.
        f.must("auto unlink al");
        f.must("auto link ac_step -> gr1.basic.exposure --at 2.0 --from 0.5 --to 0.5 --fade-in 6");
        for (const auto &x : f.svc.lint())
            assert(x.detail.find("steps by") == std::string::npos);
        std::printf("[PASS] the boundary lint fires with the size of the step, and a fade silences it\n");
    }

    void test_the_evaluation_order_is_automation_then_bindings()
    {
        // R-EVAL-2. If bindings ran first, an expression reading an automated address would see
        // a stale number — and the picture would differ. This is the test that pins the order.
        Fixture f;
        seedProject(f);
        f.must("auto new ac_e --dur 2.0 --points 0=0,1=1 --ease linear");
        f.must("auto link ac_e -> gr1.basic.exposure --at 0 --dur 2.0 --from 0 --to 1");
        f.must("bind clp_a.opacity = gr1.basic.exposure");

        Evaluator ev(f.svc.project(), &f.rack, Automation(const_cast<Project &>(f.svc.project())),
                     BindingGraph());
        std::string txt;
        assert(f.svc.evalAddress("clp_a.opacity", 1.0, false, txt));
        const double v = std::atof(txt.c_str() + txt.find("= ") + 2);
        // Half way along the shape the automated exposure is 0.5, and the binding must see THAT
        // rather than the static 0.
        assert(std::fabs(v - 0.5) < 1e-6);
        std::printf("[PASS] a binding sees the AUTOMATED value, so automation runs first\n");
    }

    void test_grade_weight_scales_a_groups_reach()
    {
        // The user's `gr1.opacity` is a continuous bypass: the weight with which a node's own
        // offsets apply to its descendants. At 0 a group contributes nothing; at 1, everything.
        Fixture f;
        seedProject(f);
        f.must("set gr1.basic.exposure=1.0");
        Evaluator ev(f.svc.project(), &f.rack, Automation(const_cast<Project &>(f.svc.project())),
                     BindingGraph());
        const ResolvedValues rv = ev.resolve(0.0);
        const auto full = ev.effectiveRackParams("cn_41", rv);
        assert(std::fabs(full.at("basic.exposure") - 1.0) < 1e-6);

        f.must("set gr1.opacity=0.5");
        const auto half = ev.effectiveRackParams("cn_41", ev.resolve(0.0));
        assert(std::fabs(half.at("basic.exposure") - 0.5) < 1e-6);

        f.must("set gr1.opacity=0");
        const auto none = ev.effectiveRackParams("cn_41", ev.resolve(0.0));
        assert(std::fabs(none.at("basic.exposure") - 0.0) < 1e-6);
        std::printf("[PASS] the grade weight scales a group's reach onto its children\n");
    }

    void test_active_clips_and_source_frames()
    {
        Fixture f;
        seedProject(f);
        Evaluator ev(f.svc.project(), &f.rack, Automation(const_cast<Project &>(f.svc.project())),
                     BindingGraph());
        // clp_a covers [0, 4.2); clp_b starts at 4.2.
        assert(ev.activeAt(0.0).size() == 1);
        assert(ev.activeAt(1.0)[0].clip->name == "clp_a");
        assert(ev.activeAt(5.0)[0].clip->name == "clp_b");
        assert(ev.activeAt(100.0).empty());
        // The source frame is nearest-neighbour from the clip-local time: 1 s into a clip whose
        // in-point is 12.4 s, at 24 fps, is frame 321.
        assert(ev.activeAt(1.0)[0].sourceFrame == (long long)std::floor(13.4 * 24.0));
        std::printf("[PASS] active clips and their source frames\n");
    }

    void test_blend_modes_and_geometry()
    {
        // L1 numeric: the blend maths on known operands, so a wrong mode is caught without a
        // rendered frame.
        assert(std::fabs(Composite::blendChannel(Blend::Multiply, 0.5, 0.5) - 0.25) < 1e-9);
        assert(std::fabs(Composite::blendChannel(Blend::Screen, 0.5, 0.5) - 0.75) < 1e-9);
        assert(std::fabs(Composite::blendChannel(Blend::Add, 0.8, 0.5) - 1.0) < 1e-9);
        assert(std::fabs(Composite::blendChannel(Blend::Difference, 0.8, 0.5) - 0.3) < 1e-9);

        // And a placed layer actually lands where the geometry says: a half-scale contain fit of
        // a white source into a black raster fills the middle and leaves the corners black.
        Raster src;
        src.allocate(8, 8, 255);
        Raster out;
        out.allocate(64, 64, 0);
        Composite::Layer l;
        l.source = &src;
        l.geom.scale = 0.5;
        l.fit = Fit::Contain;
        Composite::placeLayer(l, out);
        auto px = [&](int x, int y) { return out.rgba[((size_t)y * 64 + x) * 4]; };
        assert(px(32, 32) == 255);   // centre covered
        assert(px(1, 1) == 0);       // corner untouched
        std::printf("[PASS] blend modes on known operands, and a scaled layer lands centred\n");
    }

    void test_a_composited_frame_has_the_project_raster()
    {
        Fixture f;
        seedProject(f);
        Raster frame;
        assert(f.svc.renderFrame(1.0, frame));
        assert(frame.width == 1920 && frame.height == 1080);
        assert(f.svc.model().frameLayers == 1);
        // Nothing at all past the last clip: an empty frame is reported as empty rather than as
        // a black picture somebody has to guess about.
        Raster empty;
        assert(!f.svc.renderFrame(500.0, empty));
        std::printf("[PASS] a composited frame is the project raster; past the end reports empty\n");
    }

    void test_the_right_source_frame_reaches_the_right_output_frame()
    {
        // The claim every other video claim rests on. A clip starting at t=2 with in=1.0 must, at
        // t=3.0, show the source frame at local time 2.0 — frame 48 at 24 fps.
        Fixture f;
        f.must("project new /tmp/isp-src.isp --fps 24 --res 64x64");
        f.must("rack add /tmp/counting.mov");
        f.must("track add --kind video --name v0");
        f.must("clip add --track v0 --src rack:counting --in 1.0 --out 4.0 --at 2.0 --name clp_a");

        Raster frame;
        assert(f.svc.renderFrame(2.0, frame, -1));
        assert(frameIndexOf(frame) == 24);          // local 1.0 -> frame 24
        assert(f.svc.renderFrame(3.0, frame, -1));
        assert(frameIndexOf(frame) == 48);          // local 2.0 -> frame 48
        assert(f.svc.renderFrame(4.0, frame, -1));
        assert(frameIndexOf(frame) == 72);
        std::printf("[PASS] the right source frame reaches the right output frame\n");
    }

    void test_speed_selects_source_frames()
    {
        // R-CUT-6: speed SAMPLES, it does not interpolate — so 2x advances the source twice as
        // fast and the test says which frame it expects rather than trusting a ratio.
        Fixture f;
        f.must("project new /tmp/isp-speed.isp --fps 24 --res 64x64");
        f.must("rack add /tmp/counting.mov");
        f.must("track add --kind video --name v0");
        f.must("clip add --track v0 --src rack:counting --in 0 --out 4.0 --at 0 --name clp_a");
        f.must("set clp_a.speed=2");
        Raster frame;
        assert(f.svc.renderFrame(1.0, frame, -1));
        assert(frameIndexOf(frame) == 48);          // 1 s in at 2x -> source second 2 -> frame 48
        std::printf("[PASS] speed selects source frames by sampling\n");
    }

    void test_a_transition_holds_the_outgoing_clip_and_crossfades()
    {
        // D-6, found by rendering real video and LOOKING at the dissolve: the outgoing clip ended
        // exactly at the cut, so there was nothing to dissolve FROM — the incoming clip faded up
        // over black at 17% and the picture went dark. A dissolve has to hold the outgoing side
        // past its own out-point (R-CUT-4a).
        Fixture f;
        f.must("project new /tmp/isp-tr.isp --fps 24 --res 64x64");
        f.must("rack add /tmp/counting.mov");
        f.must("track add --kind video --name v0");
        f.must("clip add --track v0 --src rack:counting --in 0 --out 2.0 --at 0 --name shotA");
        f.must("clip add --track v0 --src rack:counting --in 4.0 --out 6.0 --at 2.0 --name shotB");
        f.must("transition add --between shotA,shotB --kind dissolve --dur 0.5");

        Evaluator ev(f.svc.project(), &f.rack, Automation(const_cast<Project &>(f.svc.project())),
                     BindingGraph());
        // At the cut, BOTH clips are live: the outgoing one is held past its out-point.
        const auto cut = ev.activeAt(2.0);
        assert(cut.size() == 2);
        bool sawHeld = false;
        for (const auto &a : cut)
        {
            if (a.clip->name == "shotA")
            {
                assert(a.heldByTransition);
                assert(std::fabs(a.transitionWeight - 1.0) < 1e-6);   // full at the start
                sawHeld = true;
            }
            else assert(std::fabs(a.transitionWeight - 0.0) < 1e-6);  // incoming starts at zero
        }
        assert(sawHeld);
        // Half way through, the two weights sum to 1 — a crossfade, not a fade to black, which is
        // exactly what the defect got wrong.
        double sum = 0;
        for (const auto &a : ev.activeAt(2.25)) sum += a.transitionWeight;
        assert(std::fabs(sum - 1.0) < 1e-6);
        // And past the transition only the incoming clip remains.
        const auto after = ev.activeAt(2.6);
        assert(after.size() == 1 && after[0].clip->name == "shotB");
        std::printf("[PASS] a transition holds the outgoing clip and the weights crossfade\n");
    }

    void test_the_frame_cache_serves_a_second_visit_and_not_a_stale_one()
    {
        Fixture f;
        f.must("project new /tmp/isp-cache.isp --fps 24 --res 64x64");
        f.must("rack add /tmp/counting.mov");
        f.must("track add --kind video --name v0");
        f.must("clip add --track v0 --src rack:counting --in 0 --out 4.0 --at 0 --name clp_a");

        CountingFrameSource::decodes = 0;
        Raster frame;
        f.svc.renderFrame(1.0, frame, 256);
        const int afterFirst = CountingFrameSource::decodes;
        assert(afterFirst > 0);
        f.svc.renderFrame(1.0, frame, 256);
        // A second visit to the same time, with nothing changed, must decode NOTHING.
        assert(CountingFrameSource::decodes == afterFirst);
        assert(f.svc.frameCache().hits() > 0);

        // And a parameter change must NOT serve the old frame. This is the staleness the key's
        // parameter hash exists to prevent, and it is indistinguishable from a rendering bug.
        f.must("set clp_a.geom.scale=1.5");
        f.svc.renderFrame(1.0, frame, 256);
        assert(CountingFrameSource::decodes > afterFirst);
        std::printf("[PASS] the cache serves a repeat visit and refuses to serve a stale frame\n");
    }

    void test_an_unopenable_source_reads_as_offline_not_as_a_stall()
    {
        // R-RACK-5. And it must be a real answer from the decoder rather than a guess about
        // whether a rack is attached.
        Fixture f;
        f.must("project new /tmp/isp-off.isp --fps 24 --res 64x64");
        f.must("rack add /tmp/missing-clip.mov");
        f.must("track add --kind video --name v0");
        f.must("clip add --track v0 --src rack:missing_clip --in 0 --out 2.0 --at 0 --name clp_a");
        bool offline = false;
        for (const auto &c : f.svc.model().clips)
            if (c.name == "clp_a") offline = c.srcOffline;
        assert(offline);
        // The project still renders — a marked placeholder, not a refusal.
        Raster frame;
        assert(f.svc.renderFrame(1.0, frame, -1));
        std::printf("[PASS] an unopenable source reads as offline and the project still renders\n");
    }

    void test_a_clip_src_may_name_a_source_by_its_bind_name()
    {
        // Node ids are assigned by the writer and are not guessable: the first `rack add` produced
        // cn_1 and cn_3, because naming a source consumes an id too. A user knows the bind name.
        Fixture f;
        f.must("project new /tmp/isp-name.isp --fps 24 --res 64x64");
        f.must("rack add /tmp/one.mov /tmp/two.mov");
        f.must("track add --kind video --name v0");
        f.must("clip add --track v0 --src rack:two --in 0 --out 1.0 --at 0 --name clp_a");
        // Stored canonically as rack:<node>, so the project text is unambiguous.
        const Clip *c = f.svc.project().clip("clp_a");
        assert(c && c->src.rfind("rack:cn_", 0) == 0);
        assert(!f.svc.project().mediaForSrc(c->src).empty());
        // An unknown source is REFUSED, and the message lists what the rack has.
        assert(!f.run("clip add --track v0 --src rack:three --in 0 --out 1 --at 2"));
        assert(f.svc.model().lastError.find("no such source") != std::string::npos);
        assert(f.svc.model().lastError.find("two") != std::string::npos);
        std::printf("[PASS] --src takes a bind name, stores a node id, and refuses an unknown\n");
    }

    void test_two_services_dump_the_same_state()
    {
        // R-SVC-9: identical commands must produce byte-identical STABLE text, whatever each
        // service did in between and however many times it pumped.
        Fixture a, b;
        seedProject(a);
        seedProject(b);
        for (int i = 0; i < 40; ++i) b.svc.pump(i * 16.0);
        a.must("set gr1.basic.exposure=0.2");
        b.must("set gr1.basic.exposure=0.2");
        DumpOptions o;
        o.stable = true;
        const std::string da = a.svc.statePrint(o), db = b.svc.statePrint(o);
        if (da != db) std::fprintf(stderr, "--- A ---\n%s\n--- B ---\n%s\n", da.c_str(), db.c_str());
        assert(da == db);
        // And the unstable dump DIFFERS, which is what proves the exclusion list is doing work
        // rather than being decorative.
        DumpOptions u;
        assert(a.svc.statePrint(u) != b.svc.statePrint(u));
        std::printf("[PASS] two services agree on the stable dump and differ on the unstable one\n");
    }

    void test_command_text_roundtrips()
    {
        // R-SVC-5: format(parse(x)) is a fixed point for every documented line, so the grammar
        // has ONE codec and a script, the journal and the socket cannot drift.
        const char *lines[] = {
            "project new /tmp/a.isp --fps 24 --res 1920x1080",
            "project open /tmp/a.isp",
            "project save",
            "project close",
            "rack import /tmp/a.cmp --branch main",
            "rack new",
            "rack pin cafe123",
            "rack unpin",
            "rack group new \"Tokyo Night\"",
            "rack duplicate cn_41",
            "rack select cn_41",
            "set gr1.basic.exposure=0.2",
            "get gr1.basic.exposure",
            "track add --kind video --name v0",
            "clip add --track v0 --src rack:cn_41 --in 12.4 --out 16.6 --at 0",
            "clip split clp_a --at 2",
            "clip move clp_a --at 4",
            "clip delete clp_a --ripple 1",
            "clip roll clp_a clp_b --by 0.5",
            "clip slip clp_a --by 0.5",
            "transition add --between clp_a,clp_b --kind dissolve --dur 0.5",
            "marker add --at 48 --name chorus",
            "rename gr1 daylight",
            "auto new ac_push --dur 2 --points 0=0,1=1",
            "auto point ac_push --at 1 --value 0.5",
            "auto link ac_push -> gr1.basic.exposure --at 4 --from 0 --to 0.8",
            "auto unlink al_1",
            "auto lanes gr1",
            "bind gr1.opacity = clamp(gr1.basic.exposure / 2 + 0.5, 0, 1)",
            "bind delete gr1.opacity",
            "bind list",
            "playhead next-cut",
            "play",
            "pause",
            "render --out /tmp/o.ppm --range 0:2",
            "export-still --out /tmp/f.ppm --at 1",
            "lint",
            "settings set proxyEdge=1280 cpuPercent=50",
            "state print --stable 1",
            "api --json 1",
            "wait render.finished --timeout 120s",
            "quit"};
        for (const char *line : lines)
        {
            std::string err;
            const Command c = parseCommand(line, err);
            if (!c.valid())
            {
                std::fprintf(stderr, "did not parse: %s  (%s)\n", line, err.c_str());
                assert(false);
            }
            const std::string back = formatCommand(c);
            const Command again = parseCommand(back, err);
            assert(again.valid());
            if (formatCommand(again) != back)
            {
                std::fprintf(stderr, "not a fixed point:\n  in:  %s\n  out: %s\n  again: %s\n",
                             line, back.c_str(), formatCommand(again).c_str());
                assert(false);
            }
        }
        std::printf("[PASS] every documented command line round-trips through one codec\n");
    }

    void test_every_command_kind_has_a_grammar_and_a_hint()
    {
        // A behaviour with no command name fails this test rather than being noticed later by a
        // human reading the grammar — and every name carries its argument hint, GENERATED
        // beside it, because cosmo's hand-maintained hints are missing for 8 of 30.
        const auto &names = commandNames();
        const auto &specs = commandSpecs();
        assert(names.size() == specs.size());
        for (const auto &sp : specs)
        {
            assert(sp.name && *sp.name);
            assert(sp.what && *sp.what);
            std::string err;
            // Every name must at least be RECOGNISED by the parser — it may want arguments,
            // but it must not come back "unknown command".
            const Command c = parseCommand(sp.name, err);
            const bool recognised = c.valid() || err.rfind("unknown", 0) != 0;
            if (!recognised) std::fprintf(stderr, "unrecognised grammar entry: %s\n", sp.name);
            assert(recognised);
        }
        std::printf("[PASS] %zu command names, each with a generated argument hint\n", names.size());
    }

    void test_the_api_document_covers_the_whole_address_space()
    {
        Fixture f;
        seedProject(f);
        const std::string doc = f.svc.apiDocument(false);
        // It is generated from the same tables the parser and the registry use, so it cannot
        // describe a command the app lacks nor omit one it has (R-SVC-10).
        assert(doc.find("auto link") != std::string::npos);
        assert(doc.find("basic.exposure") != std::string::npos);
        assert(doc.find("geom.crop.w") != std::string::npos);
        assert(doc.find("cosmo") != std::string::npos);          // the owner column
        const std::string json = f.svc.apiDocument(true);
        assert(json.find("\"parameters\"") != std::string::npos);
        assert(json.find("\"automatable\"") != std::string::npos);
        std::printf("[PASS] the API document is generated and covers commands, events and parameters\n");
    }

    void test_an_address_space_that_a_ui_can_enumerate()
    {
        Fixture f;
        seedProject(f);
        const auto addrs = ParamRegistry::addresses(f.svc.project(), &f.rack);
        bool hasGr1 = false, hasClip = false, hasProject = false;
        for (const auto &a : addrs)
        {
            if (a == "gr1.basic.exposure") hasGr1 = true;
            if (a == "clp_a.geom.scale") hasClip = true;
            if (a == "project.playhead") hasProject = true;
        }
        assert(hasGr1 && hasClip && hasProject);
        // Completion is not a nicety at this size — it is a requirement (R-UI-5).
        assert(addrs.size() > 100);
        std::printf("[PASS] %zu addresses exist and are enumerable for completion\n", addrs.size());
    }
}

int main()
{
    test_isp_roundtrip_is_a_fixed_point();
    test_unknown_keys_and_comments_survive_a_round_trip();
    test_a_clip_may_not_carry_colour();
    test_a_nan_is_repaired_not_refused();
    test_the_cut_operations();
    test_a_typo_is_refused_naming_the_nearest_candidate();
    test_a_colour_edit_reaches_the_rack();
    test_a_pinned_rack_refuses_every_colour_write();
    test_one_shape_drives_two_parameters();
    test_overlapping_links_on_one_address_are_refused();
    test_a_non_automatable_address_is_refused_with_the_reason();
    test_the_user_binding_example();
    test_a_cycle_is_refused_naming_both_ends();
    test_a_binding_and_a_link_cannot_share_a_parameter();
    test_a_binding_reads_an_automation_shape();
    test_the_explain_trace_agrees_with_the_value_it_explains();
    test_rename_rewrites_every_expression();
    test_a_rename_does_not_corrupt_a_longer_name();
    test_the_boundary_lint_fires_and_says_how_big_the_step_is();
    test_the_evaluation_order_is_automation_then_bindings();
    test_grade_weight_scales_a_groups_reach();
    test_active_clips_and_source_frames();
    test_blend_modes_and_geometry();
    test_a_composited_frame_has_the_project_raster();
    test_the_right_source_frame_reaches_the_right_output_frame();
    test_speed_selects_source_frames();
    test_a_transition_holds_the_outgoing_clip_and_crossfades();
    test_the_frame_cache_serves_a_second_visit_and_not_a_stale_one();
    test_an_unopenable_source_reads_as_offline_not_as_a_stall();
    test_a_clip_src_may_name_a_source_by_its_bind_name();
    test_two_services_dump_the_same_state();
    test_command_text_roundtrips();
    test_every_command_kind_has_a_grammar_and_a_hint();
    test_the_api_document_covers_the_whole_address_space();
    test_an_address_space_that_a_ui_can_enumerate();
    std::printf("\nall interstellar core tests passed\n");
    return 0;
}
