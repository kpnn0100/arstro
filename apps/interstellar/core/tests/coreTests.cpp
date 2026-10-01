/*
 *  interstellar_core_tests — L2: the real service, headless (arstro.rule §4).
 *
 *  **The suite's reason to exist is one test**: `test_a_colour_edit_reaches_a_real_cmp`. Two
 *  previous builds of this app passed every test they had while their colour authority was a fake,
 *  so a suite that cannot prove a parameter landed in a `.cmp` is a suite that proves nothing about
 *  this app.
 *
 *  Plain assert(), with NDEBUG undefined first: a Release build otherwise turns every assertion
 *  into `((void)0)` and the suite prints [PASS] while checking nothing (cosmo's D-43 — which bit
 *  this repo again on `gene_tests`' first run, and the first thing the restored assertions caught
 *  was a bug in the test's own fixture).
 */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "Rack.h"
#include "core/EditSession.h"
#include "core/ThreadBudget.h"
#include "engine/EditParamsIO.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace arstro;
using namespace arstro::interstellar;

namespace
{
    std::string scratch()
    {
        // Sandboxed, because CosmoService's constructor reads the recents index: without this the
        // suite is driven by whatever projects the developer happens to have opened.
        const char *base = std::getenv("INTERSTELLAR_TEST_DIR");
        std::string dir = base ? base : "/tmp/interstellar-tests";
        std::filesystem::create_directories(dir);
        return dir;
    }

    /** A decoder that answers every path with a flat colour, so a project loads with no codec, no
     *  file and no display. The analogue of cosmo's own FakeDecoder — and note what it is NOT: it
     *  is a fake *decoder*, never a fake *rack*. The thing under test is the real CosmoService. */
    class FakeDecoder : public cosmo::IImageDecoder
    {
    public:
        cosmo::DecodedImage decodeFile(const std::string &path) override
        {
            cosmo::DecodedImage d;
            d.width = 32;
            d.height = 24;
            d.rgba.assign((size_t)d.width * d.height * 4, 128);
            for (size_t i = 3; i < d.rgba.size(); i += 4) d.rgba[i] = 255;
            const auto slash = path.find_last_of('/');
            d.name = slash == std::string::npos ? path : path.substr(slash + 1);
            return d;
        }
    };

    /** The smallest correct `.cmp`: two images under one group, so stacking has something to
     *  stack. Written rather than fixtured, so the test carries its own input. */
    std::string writeFakeCmp(const std::string &dir)
    {
        const std::string path = dir + "/rack.cmp";
        std::ofstream f(path);
        f << "cosmoworkspace=1\n";
        f << "#group\nname=Day interiors\nparent=-1\n";
        f << "#image\nname=a.jpg\npath=" << dir << "/a.jpg\nparent=0\n";
        f << "#image\nname=b.jpg\npath=" << dir << "/b.jpg\nparent=0\n";
        return path;
    }

    struct Fixture
    {
        cosmo::ThreadBudget budget{50};
        Rack rack{budget};
        std::vector<std::string> events;

        Fixture()
        {
            rack.setDecoderFactory([] { return std::unique_ptr<cosmo::IImageDecoder>(new FakeDecoder()); });
            rack.subscribe([this](const cosmo::Event &e) { events.push_back(cosmo::formatEvent(e)); });
        }
        bool sawEventPrefix(const std::string &p) const
        {
            for (const auto &e : events)
                if (e.rfind(p, 0) == 0) return true;
            return false;
        }
    };

    /** Read a scalar straight out of a `.cmp` on disk — deliberately NOT through any Interstellar
     *  code, because the claim under test is that the FILE changed. A reader that shared code with
     *  the writer could agree with it and both be wrong. */
    bool readParamFromCmp(const std::string &path, const std::string &key, double &out, int which = 0)
    {
        std::ifstream f(path);
        if (!f) return false;
        std::string line;
        int seen = 0;
        while (std::getline(f, line))
        {
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            if (line.substr(0, eq) != key) continue;
            if (seen++ != which) continue;
            out = std::atof(line.c_str() + eq + 1);
            return std::isfinite(out);
        }
        return false;
    }

    // ─────────────────────────────────────────────────────────────────────────────────────────

    void test_the_rack_opens_a_cmp_and_reports_its_tree()
    {
        Fixture f;
        const std::string dir = scratch();
        const std::string cmp = writeFakeCmp(dir);
        std::string err;
        if (!f.rack.openProject(cmp, err)) { std::fprintf(stderr, "open: %s\n", err.c_str()); assert(false); }

        const auto nodes = f.rack.nodes();
        // One group with two images under it — the shape R-RACK-4's stacking needs.
        assert(nodes.size() == 3);
        int groups = 0, sources = 0;
        for (const auto &n : nodes) (n.group ? groups : sources)++;
        assert(groups == 1 && sources == 2);
        assert(f.rack.imageCount() == 2);
        assert(f.sawEventPrefix("[evt] project.opened"));
        std::printf("[PASS] the rack opens a .cmp and reports %zu nodes\n", nodes.size());
    }

    void test_a_colour_edit_reaches_a_real_cmp()
    {
        // ── THE GATE (R-RACK-2). Everything else in this app is downstream of this passing. ──
        Fixture f;
        const std::string dir = scratch();
        const std::string cmp = writeFakeCmp(dir);
        std::string err;
        assert(f.rack.openProject(cmp, err));

        // Grade the GROUP, so what is proven is the thing the app is for: a group's offset,
        // stacked onto its members (R-RACK-4), written into the project.
        int group = -1;
        for (const auto &n : f.rack.nodes())
            if (n.group) group = n.id;
        assert(group >= 0);

        if (!f.rack.setParam(group, "exposure", "0.2", err))
        { std::fprintf(stderr, "set: %s\n", err.c_str()); assert(false); }
        assert(f.rack.save(err));

        // The file on disk, read by code that shares nothing with the writer.
        double onDisk = 0;
        assert(readParamFromCmp(cmp, "exposure", onDisk));
        assert(std::fabs(onDisk - 0.2) < 1e-6);
        std::printf("[PASS] *** set exposure=0.2 reached the .cmp on disk (read back %.3f) ***\n",
                    onDisk);
    }

    void test_a_second_service_sees_the_edit_the_first_one_made()
    {
        // The other half of R-RACK-2: not merely "a number appears in the file", but "COSMO reads
        // it back as the same edit". A second service is the closest thing this suite has to
        // opening the project in Cosmo, which is what the user would actually do.
        const std::string dir = scratch();
        const std::string cmp = writeFakeCmp(dir);
        int group = -1;
        {
            Fixture a;
            std::string err;
            assert(a.rack.openProject(cmp, err));
            for (const auto &n : a.rack.nodes())
                if (n.group) group = n.id;
            assert(a.rack.setParam(group, "exposure", "0.45", err));
            assert(a.rack.setParam(group, "temp", "5200", err));
            assert(a.rack.save(err));
        }
        Fixture b;
        std::string err;
        assert(b.rack.openProject(cmp, err));
        double exposure = 0, temp = 0;
        assert(b.rack.getParam(group, "exposure", exposure));
        assert(b.rack.getParam(group, "temp", temp));
        assert(std::fabs(exposure - 0.45) < 1e-6);
        assert(std::fabs(temp - 5200.0) < 1e-3);
        std::printf("[PASS] a second CosmoService reads the edit back (exposure %.3f, temp %.0f)\n",
                    exposure, temp);
    }

    void test_a_group_offset_stacks_onto_its_members()
    {
        // R-RACK-4: grouping IS the blending mechanism. A member's EFFECTIVE params must carry its
        // group's offset — through Cosmo's own composeParams, which is why this is a test of the
        // seam rather than of arithmetic Interstellar wrote.
        Fixture f;
        const std::string dir = scratch();
        const std::string cmp = writeFakeCmp(dir);
        std::string err;
        assert(f.rack.openProject(cmp, err));

        int group = -1, member = -1;
        for (const auto &n : f.rack.nodes())
        {
            if (n.group) group = n.id;
            else if (member < 0) member = n.id;
        }
        assert(group >= 0 && member >= 0);

        EditParams before;
        assert(f.rack.effectiveParams(member, before));
        assert(std::fabs(before.exposure) < 1e-9);

        assert(f.rack.setParam(group, "exposure", "0.5", err));

        EditParams after;
        assert(f.rack.effectiveParams(member, after));
        assert(std::fabs(after.exposure - 0.5) < 1e-6);
        // And the member's OWN value is untouched: the offset belongs to the group.
        double own = 1.0;
        assert(f.rack.getParam(member, "exposure", own));
        assert(std::fabs(own) < 1e-9);
        std::printf("[PASS] a group's +0.5 EV stacks onto its member (own %.3f, effective %.3f)\n",
                    own, after.exposure);
    }

    void test_a_refused_edit_says_why()
    {
        // R-SVC-3. A command that reports success on a key it did not understand is worse than one
        // that crashes, because everything after it is built on a state that never changed.
        Fixture f;
        std::string err;
        // No project open yet.
        assert(!f.rack.setParam(0, "exposure", "0.2", err));
        assert(err.find("no rack project") != std::string::npos);

        const std::string cmp = writeFakeCmp(scratch());
        assert(f.rack.openProject(cmp, err));
        // A node that does not exist.
        assert(!f.rack.setParam(9999, "exposure", "0.2", err));
        assert(!err.empty());
        std::printf("[PASS] a refused edit says why: \"%s\"\n", err.c_str());
    }
}

int main()
{
    test_the_rack_opens_a_cmp_and_reports_its_tree();
    test_a_colour_edit_reaches_a_real_cmp();
    test_a_second_service_sees_the_edit_the_first_one_made();
    test_a_group_offset_stacks_onto_its_members();
    test_a_refused_edit_says_why();
    std::printf("\nall interstellar core tests passed\n");
    return 0;
}
