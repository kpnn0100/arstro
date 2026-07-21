#include "TouchIcons.h"
#include <cmath>

namespace arstro
{
namespace cosmo_touch
{
namespace icon
{
namespace
{
    // map a 24x24 SVG coordinate into the target box
    struct Map
    {
        const Rect &b;
        double x(double u) const { return b.x + u / 24.0 * b.w; }
        double y(double v) const { return b.y + v / 24.0 * b.h; }
        double s() const { return (b.w + b.h) / 48.0; }  // uniform scale for radii
    };

    void poly(IRenderTarget &t, const Map &m, std::initializer_list<std::pair<double, double>> pts,
              const Color &c, double sw, bool close = false)
    {
        t.setStroke(c, sw);
        t.beginPath();
        bool first = true;
        for (auto &p : pts) { if (first) { t.moveTo(m.x(p.first), m.y(p.second)); first = false; } else t.lineTo(m.x(p.first), m.y(p.second)); }
        if (close) t.closePath();
        t.strokePath();
    }

    void dot(IRenderTarget &t, const Map &m, double u, double v, double r, const Color &c)
    {
        double cx = m.x(u), cy = m.y(v), rr = r * m.s();
        const double k = 0.5523;
        t.setFill(c);
        t.beginPath();
        t.moveTo(cx + rr, cy);
        t.cubicTo(cx + rr, cy + rr * k, cx + rr * k, cy + rr, cx, cy + rr);
        t.cubicTo(cx - rr * k, cy + rr, cx - rr, cy + rr * k, cx - rr, cy);
        t.cubicTo(cx - rr, cy - rr * k, cx - rr * k, cy - rr, cx, cy - rr);
        t.cubicTo(cx + rr * k, cy - rr, cx + rr, cy - rr * k, cx + rr, cy);
        t.closePath();
        t.fillPath();
    }

    void ring(IRenderTarget &t, const Map &m, double u, double v, double r, const Color &c, double sw)
    {
        double cx = m.x(u), cy = m.y(v), rr = r * m.s();
        const double k = 0.5523;
        t.setStroke(c, sw);
        t.beginPath();
        t.moveTo(cx + rr, cy);
        t.cubicTo(cx + rr, cy + rr * k, cx + rr * k, cy + rr, cx, cy + rr);
        t.cubicTo(cx - rr * k, cy + rr, cx - rr, cy + rr * k, cx - rr, cy);
        t.cubicTo(cx - rr, cy - rr * k, cx - rr * k, cy - rr, cx, cy - rr);
        t.cubicTo(cx + rr * k, cy - rr, cx + rr, cy - rr * k, cx + rr, cy);
        t.closePath();
        t.strokePath();
    }
}  // namespace

void back(IRenderTarget &t, const Rect &box, const Color &c, double sw)
{
    poly(t, Map{box}, {{15, 18}, {9, 12}, {15, 6}}, c, sw);
}

void undo(IRenderTarget &t, const Rect &box, const Color &c, double sw)
{
    Map m{box};
    poly(t, m, {{9, 14}, {4, 9}, {9, 4}}, c, sw);
    // M20 20 v-7 a4 4 0 0 0 -4 -4 H4  (draw the hook with a quad for the corner)
    t.setStroke(c, sw);
    t.beginPath();
    t.moveTo(m.x(20), m.y(20));
    t.lineTo(m.x(20), m.y(13));
    t.quadTo(m.x(20), m.y(9), m.x(16), m.y(9));
    t.lineTo(m.x(4), m.y(9));
    t.strokePath();
}

void redo(IRenderTarget &t, const Rect &box, const Color &c, double sw)
{
    Map m{box};
    poly(t, m, {{15, 14}, {20, 9}, {15, 4}}, c, sw);
    t.setStroke(c, sw);
    t.beginPath();
    t.moveTo(m.x(4), m.y(20));
    t.lineTo(m.x(4), m.y(13));
    t.quadTo(m.x(4), m.y(9), m.x(8), m.y(9));
    t.lineTo(m.x(20), m.y(9));
    t.strokePath();
}

void more(IRenderTarget &t, const Rect &box, const Color &c, double)
{
    Map m{box};
    dot(t, m, 12, 12, 1.4, c);
    dot(t, m, 19, 12, 1.4, c);
    dot(t, m, 5, 12, 1.4, c);
}

void chevronUp(IRenderTarget &t, const Rect &box, const Color &c, double sw)
{
    poly(t, Map{box}, {{18, 15}, {12, 9}, {6, 15}}, c, sw);
}

void plusCircle(IRenderTarget &t, const Rect &box, const Color &c, double sw)
{
    Map m{box};
    ring(t, m, 12, 12, 10, c, sw);
    poly(t, m, {{12, 8}, {12, 16}}, c, sw);
    poly(t, m, {{8, 12}, {16, 12}}, c, sw);
}

void folderOpen(IRenderTarget &t, const Rect &box, const Color &c, double sw)
{
    // simplified open-folder: M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z
    Map m{box};
    poly(t, m, {{2, 19}, {2, 5}, {9, 5}, {11, 8}, {22, 8}, {22, 19}, {2, 19}}, c, sw, true);
}

void importDown(IRenderTarget &t, const Rect &box, const Color &c, double sw)
{
    Map m{box};
    poly(t, m, {{21, 15}, {21, 19}, {3, 19}, {3, 15}}, c, sw);
    poly(t, m, {{7, 8}, {12, 3}, {17, 8}}, c, sw);
    poly(t, m, {{12, 3}, {12, 15}}, c, sw);
}

void search(IRenderTarget &t, const Rect &box, const Color &c, double sw)
{
    Map m{box};
    ring(t, m, 11, 11, 7, c, sw);
    poly(t, m, {{16, 16}, {21, 21}}, c, sw);
}

void tabBasic(IRenderTarget &t, const Rect &box, const Color &c, double sw)
{
    // aperture: circle + 8 short rays (design's basic glyph)
    Map m{box};
    ring(t, m, 12, 12, 3, c, sw);
    poly(t, m, {{3, 12}, {4, 12}}, c, sw);
    poly(t, m, {{20, 12}, {21, 12}}, c, sw);
    poly(t, m, {{12, 3}, {12, 4}}, c, sw);
    poly(t, m, {{12, 20}, {12, 21}}, c, sw);
    poly(t, m, {{5.6, 5.6}, {6.3, 6.3}}, c, sw);
    poly(t, m, {{17.7, 17.7}, {18.4, 18.4}}, c, sw);
    poly(t, m, {{5.6, 18.4}, {6.3, 17.7}}, c, sw);
    poly(t, m, {{17.7, 6.3}, {18.4, 5.6}}, c, sw);
}

void tabMask(IRenderTarget &t, const Rect &box, const Color &c, double sw)
{
    // shield: M12 22 s8 -4 8 -10 V5 l-8 -3 -8 3 v7 c0 6 8 10 8 10 z
    Map m{box};
    t.setStroke(c, sw);
    t.beginPath();
    t.moveTo(m.x(12), m.y(22));
    t.quadTo(m.x(20), m.y(18), m.x(20), m.y(12));
    t.lineTo(m.x(20), m.y(5));
    t.lineTo(m.x(12), m.y(2));
    t.lineTo(m.x(4), m.y(5));
    t.lineTo(m.x(4), m.y(12));
    t.quadTo(m.x(4), m.y(18), m.x(12), m.y(22));
    t.closePath();
    t.strokePath();
}

void tabCurve(IRenderTarget &t, const Rect &box, const Color &c, double sw)
{
    // M3 20 C6 20 6 4 12 4 C18 4 18 20 21 20
    Map m{box};
    t.setStroke(c, sw);
    t.beginPath();
    t.moveTo(m.x(3), m.y(20));
    t.cubicTo(m.x(6), m.y(20), m.x(6), m.y(4), m.x(12), m.y(4));
    t.cubicTo(m.x(18), m.y(4), m.x(18), m.y(20), m.x(21), m.y(20));
    t.strokePath();
}

void tabGrade(IRenderTarget &t, const Rect &box, const Color &c, double sw)
{
    Map m{box};
    ring(t, m, 12, 12, 4, c, sw);
    poly(t, m, {{12, 2}, {12, 4}}, c, sw);
    poly(t, m, {{12, 20}, {12, 22}}, c, sw);
    poly(t, m, {{2, 12}, {4, 12}}, c, sw);
    poly(t, m, {{20, 12}, {22, 12}}, c, sw);
    poly(t, m, {{4.9, 4.9}, {6.3, 6.3}}, c, sw);
    poly(t, m, {{17.7, 17.7}, {19.1, 19.1}}, c, sw);
    poly(t, m, {{4.9, 19.1}, {6.3, 17.7}}, c, sw);
    poly(t, m, {{17.7, 6.3}, {19.1, 4.9}}, c, sw);
}

void tabXform(IRenderTarget &t, const Rect &box, const Color &c, double sw)
{
    // play triangle: M5 3 l14 9 -14 9 V3 z
    poly(t, Map{box}, {{5, 3}, {19, 12}, {5, 21}}, c, sw, true);
}
}
}
}
