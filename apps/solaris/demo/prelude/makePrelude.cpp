/*
 *  The Prelude, made in the window (Solaris R-SVC-10/11).
 *
 *  J. S. Bach's Prelude in C major, BWV 846 (1722, public domain), made the way a person makes it: through
 *  the window's gestures — clicks, drags, keys and the wheel at the geometry the app draws — never a
 *  command line. The app is the real one (App + SolarisService, `app/tests/Rig.h`): what this program does
 *  is what a hand on a mouse does, minus GTK. Every gesture is counted, so the UX audit
 *  (`docs/ux-audit.md`) can say what making the song cost and what each fix saved.
 *
 *      solaris_demo_prelude <out-dir> [--shots]
 *
 *  It writes <out-dir>/prelude.slp and prelude.wav (rendered from the File menu), and prints the gesture
 *  count by kind. `measure.py` diffs the song against `score.txt` and measures the render.
 */
#include "../../app/tests/Rig.h"
#include "../../app/widgets/Browser.h"
#include "../../app/widgets/HomeScreen.h"
#include "../../app/widgets/PianoRoll.h"
#include "../../app/widgets/Timeline.h"
#include "../../app/widgets/FloatWindow.h"
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using arstro::solaris_ui::PianoRoll;
using sltest::cx;
using sltest::cy;
using sltest::world;

namespace
{
    struct Note { int pitch; double at, length; };

    std::vector<Note> readScore(const std::string &path)
    {
        std::vector<Note> v;
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream s(line);
            Note n{};
            s >> n.pitch >> n.at >> n.length;
            v.push_back(n);
        }
        return v;
    }

    /** The hand: every gesture goes through here, counted by kind. */
    struct Hand
    {
        sltest::Rig &r;
        std::map<std::string, int> count;
        bool shots = false;
        std::string out;
        int shot = 0;

        void click(double x, double y, const char *what) { ++count[what]; r.click(x, y); r.frame(); }
        void click(const artboard::Rect &rc, const char *what) { click(cx(rc), cy(rc), what); }
        void dclick(double x, double y, const char *what) { ++count[what]; r.click(x, y); r.click(x, y); r.frame(); }
        void drag(double x0, double y0, double x1, double y1, const char *what) { ++count[what]; r.drag(x0, y0, x1, y1, 8); r.frame(); }
        void key(int code, bool ctrl, const char *what) { ++count[what]; r.key(code, ctrl); r.frame(); }
        void wheel(double x, double y, double notches, bool ctrl, const char *what) { ++count[what]; r.app->wheel(x, y, notches, ctrl); r.frame(); }
        void look(const std::string &name)
        {
            r.settle();
            if (shots) r.write(out + "/" + std::to_string(++shot) + "-" + name + ".png");
        }
        int total() const
        {
            int n = 0;
            for (const auto &kv : count) n += kv.second;
            return n;
        }
    };
}

int main(int argc, char **argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: solaris_demo_prelude <out-dir> [--shots]\n"); return 2; }
    const std::string out = argv[1];
    const std::string here = std::string(__FILE__).substr(0, std::string(__FILE__).rfind('/'));
    const std::vector<Note> score = readScore(here + "/score.txt");
    sltest::Rig r("prelude", 1440, 900);
    Hand h{r};
    h.out = out;
    for (int i = 2; i < argc; ++i) h.shots |= std::string(argv[i]) == "--shots";

    // 1. Home › New song — the host's picker answers with a name
    r.app->onPickSongToCreate = [&] { r.app->newSongPicked(out + "/prelude.slp"); };
    r.settle();
    h.click(world(r.app->home(), r.app->home().actionRect(0)), "click");
    h.look("new-song");

    // 2. the piano: Browser › Instruments, its row dragged onto the lanes
    auto &b = r.app->project().browser();
    auto &tl = r.app->project().timeline();
    h.click(world(b, b.tabRect(arstro::solaris_ui::Browser::kInstruments)), "click");
    r.settle();
    int pianoRow = -1;
    for (int i = 0; i < b.rowCount(); ++i)
        if (b.row(i).kind == "instrument" && b.row(i).value == "piano") pianoRow = i;
    if (pianoRow < 0) { std::fprintf(stderr, "no Piano in the browser\n"); return 1; }
    const artboard::Rect pr = world(b, b.rowRect(pianoRow)), lanes = world(tl, tl.rulerRect());
    h.drag(cx(pr), cy(pr), lanes.x + 40.0, lanes.bottom() + 40.0, "drag");
    h.look("piano-dropped");
    const auto &m = r.svc->model();
    if (m.clips.empty()) { std::fprintf(stderr, "the drop made no clip\n"); return 1; }
    const std::string ac = m.clips[0].id, pt = m.clips[0].pattern;

    // 3. its MIDI clip double-clicked: the roll
    const artboard::Rect c = world(tl, tl.clipRect(ac));
    h.dclick(c.x + 10.0, cy(c), "double-click");
    h.look("roll-open");
    PianoRoll &roll = *r.app->project().windows().roll(pt);

    // 4. the first bars, note by note, as the roll allows today: a click adds a note of the LAST length;
    //    a length is changed by dragging the note's right edge (and becomes the last length)
    const artboard::Rect rw = world(roll, artboard::Rect{0, 0, 0, 0});
    double last = 0.25;
    int entered = 0;
    for (const Note &n : score)
    {
        if (n.at >= 8.0) break; // two bars, to see what it costs
        const artboard::Rect g = world(roll, roll.gridRect());
        // the pitch row must be in view: the wheel scrolls (a notch ≈ 3 rows)
        for (int k = 0; k < 40; ++k)
        {
            const double y = rw.y + roll.pitchToY(n.pitch);
            if (y >= g.y + 4.0 && y + PianoRoll::kNoteH <= g.bottom() - 4.0) break;
            h.wheel(cx(g), cy(g), y < g.y ? 1.0 : -1.0, false, "wheel");
            r.settle();
        }
        const double y = rw.y + roll.pitchToY(n.pitch) + PianoRoll::kNoteH * 0.5;
        h.click(rw.x + roll.beatToX(n.at) + 2.0, y, "click");
        r.settle();
        if (n.length != last)
        {
            const artboard::Rect nr = world(roll, roll.noteRect(n.pitch, n.at));
            h.drag(nr.right() - 2.0, cy(nr), rw.x + roll.beatToX(n.at + n.length) - 1.0, cy(nr), "drag");
            r.settle();
            last = n.length;
        }
        ++entered;
    }
    h.look("two-bars");

    std::printf("entered %d notes with %d gestures:", entered, h.total());
    for (const auto &kv : h.count) std::printf(" %s %d", kv.first.c_str(), kv.second);
    std::printf("\n");
    const auto &pm = r.svc->model();
    for (const auto &p : pm.patterns)
        if (p.id == pt) std::printf("pattern %s: %zu notes, length %g\n", pt.c_str(), p.notes.size(), p.length);
    return 0;
}
