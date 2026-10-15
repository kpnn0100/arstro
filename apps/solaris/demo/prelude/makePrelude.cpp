/*
 *  The Prelude, made in the window (Solaris R-SVC-10/11).
 *
 *  J. S. Bach's Prelude in C major, BWV 846 (1722, public domain), made the way a person makes it: through
 *  the window's gestures — clicks, drags, keys and the wheel at the geometry the app draws — never a
 *  command line. The app is the real one (App + SolarisService, `app/tests/Rig.h`): what this program does
 *  is what a hand on a mouse does, minus GTK. Every gesture is counted, so the UX audit
 *  (`docs/ux-audit.md`) can say what making the song cost.
 *
 *  How a person makes it with the roll as it now is (attempt 2, after H6–H10a):
 *    the tempo dragged in the song bar; the Piano dragged from the browser; its clip's roll opened, moved
 *    and resized by its corner so all 46 rows show, zoomed out a little; the first half bar entered with
 *    the length chip (1/2 for the bass and tenor, the tenor's end pulled to 1¾, 1/16 for the figure);
 *    Ctrl+A, Ctrl+D — the first bar. Then each bar: a box over the bar before, Ctrl+D (a copy after it),
 *    and only the voices that change — a box over the voice's row, then ↑/↓ for a semitone or a drag for
 *    more (a voice moves onto a row only after that row's voice has left it, as a careful hand orders it).
 *    Bars 33–35 — two runs over the pedal and the last chord — clicked in. A reverb from "+ Effect"; Ctrl+S;
 *    File › Render….
 *
 *      solaris_demo_prelude <out-dir> [--shots]
 *
 *  Writes <out-dir>/prelude.slp and prelude.wav and prints the gestures by kind. `measure.py` diffs the
 *  song against `score.txt` and measures the render.
 */
#include "../../app/tests/Rig.h"
#include "../../app/widgets/Browser.h"
#include "../../app/widgets/FloatWindow.h"
#include "../../app/widgets/HomeScreen.h"
#include "../../app/widgets/MixerDock.h"
#include "../../app/widgets/PianoRoll.h"
#include "../../app/widgets/SongBar.h"
#include "../../app/widgets/Timeline.h"
#include <algorithm>
#include <array>
#include <cmath>
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
    int midi(const std::string &name)
    {
        static const std::map<char, int> N = {{'C', 0}, {'D', 2}, {'E', 4}, {'F', 5}, {'G', 7}, {'A', 9}, {'B', 11}};
        size_t i = 1;
        int acc = 0;
        while (i < name.size() && (name[i] == '#' || name[i] == 'b')) acc += name[i++] == '#' ? 1 : -1;
        return 12 * (std::atoi(name.c_str() + i) + 1) + N.at(name[0]) + acc;
    }
    std::vector<int> chord(const std::string &s)
    {
        std::vector<int> v;
        std::istringstream in(s);
        std::string w;
        while (in >> w) v.push_back(midi(w));
        return v;
    }
    // the score, as `score.py` has it — bars 1–32 (bass, tenor, the figure's three)
    const char *kChords[32] = {
        "C4 E4 G4 C5 E5", "C4 D4 A4 D5 F5", "B3 D4 G4 D5 F5", "C4 E4 G4 C5 E5", "C4 E4 A4 E5 A5", "C4 D4 F#4 A4 D5",
        "B3 D4 G4 D5 G5", "B3 C4 E4 G4 C5", "A3 C4 E4 G4 C5", "D3 A3 D4 F#4 C5", "G3 B3 D4 G4 B4", "G3 Bb3 E4 G4 C#5",
        "F3 A3 D4 A4 D5", "F3 Ab3 D4 F4 B4", "E3 G3 C4 G4 C5", "E3 F3 A3 C4 F4", "D3 F3 A3 C4 F4", "G2 D3 G3 B3 F4",
        "C3 E3 G3 C4 E4", "C3 G3 Bb3 C4 E4", "F2 F3 A3 C4 E4", "F#2 C3 A3 C4 Eb4", "Ab2 F3 B3 C4 D4", "G2 F3 G3 B3 D4",
        "G2 E3 G3 C4 E4", "G2 D3 G3 C4 F4", "G2 D3 G3 B3 F4", "G2 Eb3 A3 C4 F#4", "G2 E3 G3 C4 G4", "G2 D3 G3 C4 F4",
        "G2 D3 G3 B3 F4", "C2 C3 G3 Bb3 E4"};
    const char *kRuns[2][3] = {{"C2", "C3", "F3 A3 C4 F4 C4 A3 C4 A3 F3 A3 F3 D3 F3 D3"},
                               {"C2", "B2", "G4 B4 D5 F5 D5 B4 D5 B4 G4 B4 D4 F4 E4 D4"}};
    const char *kFinal = "C2 C3 E4 G4 C5";

    /** The hand: every gesture goes through here, counted by kind. */
    struct Hand
    {
        sltest::Rig &r;
        std::map<std::string, int> count;
        bool shots = false;
        std::string out;
        int shot = 0;

        void wait() { r.pump(260.0); }
        void click(double x, double y, const char *what = "click") { ++count[what]; r.click(x, y); wait(); }
        void click(const artboard::Rect &rc, const char *what = "click") { click(cx(rc), cy(rc), what); }
        void dclick(double x, double y) { ++count["double-click"]; r.click(x, y); r.click(x, y); wait(); }
        void drag(double x0, double y0, double x1, double y1, bool ctrl = false, const char *what = "drag")
        {
            ++count[what];
            r.app->pointer(0, x0, y0, 0, r.now, false, false, ctrl);
            r.frame();
            for (int k = 1; k <= 8; ++k)
            {
                r.app->pointer(1, x0 + (x1 - x0) * k / 8.0, y0 + (y1 - y0) * k / 8.0, 0, r.now, false, false, ctrl);
                r.frame();
            }
            r.app->pointer(2, x1, y1, 0, r.now, false, false, ctrl);
            wait();
        }
        void key(int code, bool ctrl = false, bool shift = false, const char *what = "key")
        {
            ++count[what];
            artboard::KeyEvent e;
            e.type = artboard::KeyEvent::Type::Down;
            e.keyCode = code;
            e.ctrl = ctrl;
            e.shift = shift;
            r.app->key(e);
            wait();
        }
        /** A flick of the wheel: `notches` at once is one gesture. */
        void wheel(double x, double y, double notches, bool ctrl, bool shift)
        {
            ++count["wheel"];
            for (int k = 0; k < (int)std::fabs(notches); ++k) r.app->wheel(x, y, notches > 0 ? 1.0 : -1.0, ctrl, shift);
            r.settle();
        }
        void look(const std::string &name)
        {
            r.settle();
            if (shots) r.write(out + "/" + (shot < 9 ? "0" : "") + std::to_string(++shot) + "-" + name + ".png");
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
    sltest::Rig r("prelude", 1440, 900);
    Hand h{r};
    h.out = out;
    for (int i = 2; i < argc; ++i) h.shots |= std::string(argv[i]) == "--shots";
    // the host's pickers: a name answered as a person types it
    r.app->onPickSongToCreate = [&] { r.app->newSongPicked(out + "/prelude.slp"); };
    r.app->onPickSave = [&](const std::string &, const std::string &, std::function<void(const std::string &)> done) { done(out + "/prelude.wav"); };
    r.settle();

    // 1. Home › New song
    h.click(world(r.app->home(), r.app->home().actionRect(0)));
    h.look("new-song");

    // 2. the tempo, 66: the song bar's readout dragged down 54 steps (R-UI-13)
    auto &bar = r.app->project().bar();
    {
        const artboard::Rect b = world(bar, bar.hitRect(arstro::solaris_ui::SongBar::kBpm));
        h.drag(cx(b), cy(b), cx(b), cy(b) + 54.0 * arstro::solaris_ui::SongBar::kBpmStepPx);
    }
    if (r.svc->model().bpm != 66.0) { std::fprintf(stderr, "the tempo is %g, not 66\n", r.svc->model().bpm); return 1; }

    // 3. the Piano: Browser › Instruments, its row dragged onto the lanes
    auto &b = r.app->project().browser();
    auto &tl = r.app->project().timeline();
    h.click(world(b, b.tabRect(arstro::solaris_ui::Browser::kInstruments)));
    int pianoRow = -1;
    for (int i = 0; i < b.rowCount(); ++i)
        if (b.row(i).kind == "instrument" && b.row(i).value == "piano") pianoRow = i;
    if (pianoRow < 0) { std::fprintf(stderr, "no Piano in the browser\n"); return 1; }
    {
        // dropped at the very start of the lanes: the song begins on beat 0
        const artboard::Rect pr = world(b, b.rowRect(pianoRow)), ruler = world(tl, tl.rulerRect());
        const double x0 = world(tl, artboard::Rect{tl.beatToX(0.0), 0, 0, 0}).x;
        h.drag(cx(pr), cy(pr), x0 + 3.0, ruler.bottom() + 40.0);
    }
    if (r.svc->model().clips.empty()) { std::fprintf(stderr, "the drop made no clip\n"); return 1; }
    const std::string ac = r.svc->model().clips[0].id, pt = r.svc->model().clips[0].pattern, ch = r.svc->model().clips[0].track;
    h.look("piano-dropped");

    // 4. its clip double-clicked: the roll; moved to the left and resized by its corner (R-WIN-5) so every row
    //    from C2 to A5 shows; zoomed out three notches (about ten bars in view)
    {
        const artboard::Rect c = world(tl, tl.clipRect(ac));
        h.dclick(c.x + 10.0, cy(c));
    }
    auto &wl = r.app->project().windows();
    auto &w = *wl.window("roll:" + pt);
    PianoRoll &roll = *wl.roll(pt);
    {
        const artboard::Rect t = world(w, w.titleRect());
        const artboard::Rect layer = world(wl, artboard::Rect{0, 0, wl.width.value(), wl.height.value()});
        h.drag(t.x + 60.0, cy(t), layer.x + 8.0 + 60.0, layer.y + 8.0 + cy(t) - t.y);
        const artboard::Rect g = world(w, w.gripRect());
        h.drag(cx(g), cy(g), layer.right() - 8.0, layer.bottom() - 8.0);
    }
    {
        const artboard::Rect g = world(roll, roll.gridRect());
        h.wheel(cx(g), cy(g), -3.0, true, false); // Ctrl: zoom out
    }
    const artboard::Rect rw = world(roll, artboard::Rect{0, 0, 0, 0});
    auto X = [&](double beat) { return rw.x + roll.beatToX(beat); };
    auto Y = [&](int pitch) { return rw.y + roll.pitchToY(pitch); };
    // bring beats [a, b) and pitches [lo, hi] into the grid: a flick of the wheel each way it needs
    auto show = [&](double a, double bEnd, int lo, int hi) {
        for (int k = 0; k < 8; ++k)
        {
            const artboard::Rect g = world(roll, roll.gridRect());
            double dx = 0, dy = 0;
            if (X(bEnd) > g.right() - 6.0) dx = -std::ceil((X(bEnd) - (g.right() - 6.0)) / 40.0);
            else if (X(a) < g.x + 4.0) dx = std::ceil((g.x + 4.0 - X(a)) / 40.0);
            if (Y(lo) + PianoRoll::kNoteH > g.bottom() - 4.0) dy = -std::ceil((Y(lo) + PianoRoll::kNoteH - (g.bottom() - 4.0)) / 40.0);
            else if (Y(hi) < g.y + 4.0) dy = std::ceil((g.y + 4.0 - Y(hi)) / 40.0);
            if (dx == 0 && dy == 0) return;
            if (dx != 0) h.wheel(cx(g), cy(g), dx, false, true);
            if (dy != 0) h.wheel(cx(g), cy(g), dy, false, false);
        }
    };
    auto setLength = [&](int item) { // the length chip's menu: 0 Last · 1 1/32 · 2 1/16 · 3 1/8 · 4 1/4 · 5 1/2 · 6 1/1
        h.click(world(roll, roll.lengthRect()));
        h.click(r.app->menu().itemRect(item));
    };
    auto note = [&](int pitch, double at) { // a click on the grid: a note of the chosen length
        show(at, at + 0.25, pitch, pitch);
        h.click(X(at) + 2.0, Y(pitch) + PianoRoll::kNoteH * 0.5);
    };
    auto pullEnd = [&](int pitch, double at, double to) { // its right edge dragged: the tenor's 1¾
        show(to - 0.5, to + 0.5, pitch, pitch); // its end in view first, as a hand scrolls to it
        const artboard::Rect n = world(roll, roll.noteRect(pitch, at));
        h.drag(n.right() - 2.0, cy(n), X(to) - 1.0, cy(n));
    };
    auto box = [&](double a, double bEnd, int lo, int hi) { // Ctrl+drag: a box over these beats and rows
        show(a, bEnd, lo, hi);
        h.drag(X(a) + 1.5, Y(hi) + 2.0, X(bEnd) - 2.0, Y(lo) + PianoRoll::kNoteH - 2.0, true);
    };

    // 5. the first half bar, then the first bar
    const std::vector<int> c1 = chord(kChords[0]);
    setLength(5); // 1/2
    note(c1[0], 0.0);
    note(c1[1], 0.25);
    pullEnd(c1[1], 0.25, 2.0);
    setLength(2); // 1/16
    for (int k = 0; k < 6; ++k) note(c1[2 + k % 3], 0.5 + 0.25 * k);
    h.key('A', true);
    h.key('D', true);
    h.look("first-bar");

    // 6. bars 2–32: the bar before, again; then only the voices that change
    const int lo = midi("C2") - 1, hi = midi("A5") + 1;
    for (int bi = 1; bi < 32; ++bi)
    {
        const double at = bi * 4.0;
        const size_t barMark = r.sent.size();
        box(at - 4.0, at, lo, hi);
        h.key('D', true);
        std::vector<int> now = chord(kChords[bi - 1]);
        const std::vector<int> want = chord(kChords[bi]);
        for (int guard = 0; guard < 12; ++guard)
        {
            int v = -1;
            for (int u = 0; u < 5 && v < 0; ++u)
            {
                if (now[(size_t)u] == want[(size_t)u]) continue;
                bool taken = false;
                for (int o = 0; o < 5; ++o) taken |= o != u && now[(size_t)o] == want[(size_t)u];
                if (!taken) v = u;
            }
            if (v < 0) // every voice that moves waits on another: one goes aside an octave first
                for (int u = 0; u < 5 && v < 0; ++u)
                    if (now[(size_t)u] != want[(size_t)u]) v = u + 100;
            if (v < 0) break;
            const bool aside = v >= 100;
            v %= 100;
            const int from = now[(size_t)v], to = aside ? from + 12 : want[(size_t)v];
            box(at, at + 4.0, from, from);
            const int d = to - from;
            if (std::abs(d) == 1) h.key(d > 0 ? 38 : 40);
            else if (std::abs(d) == 12) h.key(d > 0 ? 38 : 40, false, true);
            else
            {
                // the selection dragged by one of its notes, straight up or down
                const double nat = at + (v == 0 ? 0.0 : v == 1 ? 0.25 : 0.5 + 0.25 * (v - 2));
                const artboard::Rect n = world(roll, roll.noteRect(from, nat));
                show(nat, nat + 0.25, std::min(from, to), std::max(from, to));
                const artboard::Rect n2 = world(roll, roll.noteRect(from, nat));
                h.drag(n2.x + 2.0, cy(n2), n2.x + 2.0, cy(n2) - d * PianoRoll::kNoteH);
                (void)n;
            }
            now[(size_t)v] = to;
        }
        if (bi == 3 || bi == 15 || bi == 31) h.look("bar-" + std::to_string(bi + 1));
        // the bar as the score has it — or stop and say what the hand sent for it
        {
            std::vector<std::array<double, 3>> got, want;
            for (const auto &p : r.svc->model().patterns)
                if (p.id == pt)
                    for (const auto &n : p.notes)
                        if (n.at >= at - 1e-9 && n.at < at + 4.0 - 1e-9) got.push_back({(double)n.pitch, n.at, n.length});
            const std::vector<int> c = chord(kChords[bi]);
            for (int half = 0; half < 2; ++half)
            {
                const double t = at + 2.0 * half;
                want.push_back({(double)c[0], t, 2.0});
                want.push_back({(double)c[1], t + 0.25, 1.75});
                for (int k = 0; k < 6; ++k) want.push_back({(double)c[(size_t)(2 + k % 3)], t + 0.5 + 0.25 * k, 0.25});
            }
            std::sort(got.begin(), got.end(), [](auto a, auto b) { return a[1] != b[1] ? a[1] < b[1] : a[0] < b[0]; });
            std::sort(want.begin(), want.end(), [](auto a, auto b) { return a[1] != b[1] ? a[1] < b[1] : a[0] < b[0]; });
            if (got != want)
            {
                std::fprintf(stderr, "bar %d is not the score: %zu notes (want %zu)\n", bi + 1, got.size(), want.size());
                for (const auto &n : got) std::fprintf(stderr, "  got  %g@%g:%g\n", n[0], n[1], n[2]);
                for (size_t i = barMark; i < r.sent.size(); ++i) std::fprintf(stderr, "  sent %s\n", r.sent[i].c_str());
                if (!r.svc->model().lastError.empty()) std::fprintf(stderr, "  last error: %s\n", r.svc->model().lastError.c_str());
                return 1;
            }
        }
    }

    // 7. bars 33–35: the runs over the pedal, and the last chord, clicked in
    for (int k = 0; k < 2; ++k)
    {
        const double at = (32 + k) * 4.0;
        setLength(6); // 1/1
        note(midi(kRuns[k][0]), at);
        note(midi(kRuns[k][1]), at + 0.25);
        pullEnd(midi(kRuns[k][1]), at + 0.25, at + 4.0);
        setLength(2); // 1/16
        const std::vector<int> run = chord(kRuns[k][2]);
        for (size_t i = 0; i < run.size(); ++i) note(run[i], at + 0.5 + 0.25 * (double)i);
    }
    setLength(6);
    for (int p : chord(kFinal)) note(p, 34 * 4.0);
    h.look("the-end");

    // 8. a reverb on the piano's strip: the roll closed (it covers the dock, enlarged), then "+ Effect" › Reverb
    h.click(world(w, w.closeRect()));
    {
        auto &d = r.app->project().dock();
        h.click(world(d, d.chipRect(ch, d.chipSlots(ch) - 1)));
        int rev = -1;
        for (int i = 0; i < r.app->menu().itemCount(); ++i)
            if (r.app->menu().item(i).label == "Reverb") rev = i;
        if (rev < 0) { std::fprintf(stderr, "no Reverb in + Effect\n"); return 1; }
        h.click(r.app->menu().itemRect(rev));
    }

    // 9. Ctrl+S, then File › Render…
    {
        const artboard::Rect lanes = world(tl, tl.rulerRect());
        h.click(lanes.x + 40.0, lanes.bottom() + 300.0); // a press on the lanes: the keys are the song's again
        h.key('S', true);
        auto &ms = bar.menus();
        h.click(world(ms, ms.titleRect(0)));
        h.click(world(ms, ms.itemRect(0, 4)));
    }
    h.look("rendered");

    for (const auto &l : r.sent)
        if (l.rfind("render ", 0) == 0 || l.rfind("project save", 0) == 0) std::printf("sent: %s\n", l.c_str());
    std::printf("the Prelude made with %d gestures:", h.total());
    for (const auto &kv : h.count) std::printf(" %s %d", kv.first.c_str(), kv.second);
    std::printf("\n");
    for (const auto &p : r.svc->model().patterns)
        if (p.id == pt) std::printf("pattern %s: %zu notes, %g beats; tempo %g bpm\n", pt.c_str(), p.notes.size(), p.length, r.svc->model().bpm);
    if (!r.svc->model().lastError.empty()) std::printf("last error: %s\n", r.svc->model().lastError.c_str());
    return 0;
}
