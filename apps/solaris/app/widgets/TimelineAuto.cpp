// Timeline — the AUTOMATION rows (R-AUTO-6): curves drawn, points edited, every gesture one command.
#include "Timeline.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;

    namespace
    {
        constexpr double kPad = 6.5;       // space::u(2): the curve keeps clear of the row's edges
        constexpr double kHandle = 4.0;    // a point's radius as drawn
        constexpr double kGrab = 7.0;      // … and as hit
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string num(double v)
        {
            char b[32];
            std::snprintf(b, sizeof b, "%.6g", v);
            return b;
        }
        std::string beats(double b) { return Timeline::beatText(b); } // to the tick
        bool logShown(const solaris::AutomationModel &m) { return (m.unit == "Hz" || m.unit == "ms") && m.min > 0 && m.max > m.min; }
    }

    std::vector<solaris::AutoPointModel> Timeline::shownPoints(const AutoLive &l) const
    {
        const double f = l.t.value();
        if (f >= 1.0 || l.from.size() != l.to.size()) return f >= 0.5 || l.from.empty() ? l.to : l.from;
        std::vector<solaris::AutoPointModel> out = l.to;
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i].at = l.from[i].at + (l.to[i].at - l.from[i].at) * f;
            out[i].value = l.from[i].value + (l.to[i].value - l.from[i].value) * f;
        }
        return out;
    }

    double Timeline::valueToY(const AutoLive &l, double v, const Rect &row) const
    {
        const auto &m = l.model;
        double u = 0.0;
        if (logShown(m)) u = std::log(std::max(v, m.min) / m.min) / std::log(m.max / m.min);
        else if (m.max > m.min) u = (v - m.min) / (m.max - m.min);
        u = std::clamp(u, 0.0, 1.0);
        return row.bottom() - kPad - u * (row.h - 2.0 * kPad);
    }

    double Timeline::yToValue(const AutoLive &l, double y, const Rect &row) const
    {
        const auto &m = l.model;
        const double u = std::clamp((row.bottom() - kPad - y) / std::max(1.0, row.h - 2.0 * kPad), 0.0, 1.0);
        if (logShown(m)) return m.min * std::pow(m.max / m.min, u);
        return m.min + u * (m.max - m.min);
    }

    Rect Timeline::autoRowRect(const std::string &au) const
    {
        for (size_t k = 0; k < mAutoIds.size(); ++k)
            if (mAutoIds[k] == au) return rowRect((int)(mRows.size() + k)); // after the lanes, in the same eased list
        return Rect{};
    }

    std::string Timeline::autoAt(double y) const
    {
        for (const auto &id : mAutoIds)
        {
            const Rect r = autoRowRect(id);
            if (y >= r.y && y < r.bottom()) return id;
        }
        return std::string();
    }

    Point Timeline::autoPointAt(const std::string &au, int i) const
    {
        const auto it = mAutos.find(au);
        if (it == mAutos.end()) return Point{-1, -1};
        const auto pts = shownPoints(it->second);
        if (i < 0 || i >= (int)pts.size()) return Point{-1, -1};
        const Rect row = autoRowRect(au);
        return Point{beatToX(pts[(size_t)i].at), valueToY(it->second, pts[(size_t)i].value, row)};
    }

    double Timeline::autoValueAt(const std::string &au, double y) const
    {
        const auto it = mAutos.find(au);
        return it == mAutos.end() ? 0.0 : yToValue(it->second, y, autoRowRect(au));
    }

    double Timeline::autoEase(const std::string &au) const
    {
        const auto it = mAutos.find(au);
        return it == mAutos.end() ? 1.0 : it->second.t.value();
    }

    Point Timeline::autoDragPoint() const
    {
        const auto it = mAutos.find(mAutoPress);
        if (!mAutoDragging || it == mAutos.end()) return Point{-1, -1};
        return Point{beatToX(mAutoAt), valueToY(it->second, mAutoValue, autoRowRect(mAutoPress))};
    }

    int Timeline::pointNear(const std::string &au, const Point &local) const
    {
        const auto it = mAutos.find(au);
        if (it == mAutos.end()) return -1;
        const auto pts = shownPoints(it->second);
        int best = -1;
        double bestD = kGrab;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const Point p = autoPointAt(au, (int)i);
            const double d = std::hypot(p.x - local.x, p.y - local.y);
            if (d <= bestD) { best = (int)i; bestD = d; }
        }
        return best;
    }

    void Timeline::paintCurve(IRenderTarget &t, const AutoLive &l, const std::vector<solaris::AutoPointModel> &pts, const Rect &row, double a,
                              bool handles) const
    {
        if (pts.empty() || a <= 0.001) return;
        const double W = width.value();
        const Color line = fade(palette::primary(), a);
        // the curve: before the first point it holds the first value, after the last the last
        t.beginPath();
        t.moveTo(kHeaderW, valueToY(l, pts.front().value, row));
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const double x = beatToX(pts[i].at), y = valueToY(l, pts[i].value, row);
            t.lineTo(x, y);
            if (i + 1 < pts.size())
            {
                const double x2 = beatToX(pts[i + 1].at), y2 = valueToY(l, pts[i + 1].value, row);
                if (pts[i].shape == "hold") t.lineTo(x2, y);
                else if (pts[i].shape == "smooth")
                    for (int k = 1; k < 12; ++k)
                    {
                        const double f = k / 12.0, s = f * f * (3.0 - 2.0 * f);
                        t.lineTo(x + (x2 - x) * f, y + (y2 - y) * s);
                    }
            }
        }
        t.lineTo(W, valueToY(l, pts.back().value, row));
        t.setStroke(line, 1.5);
        t.strokePath();
        if (!handles) return;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const double x = beatToX(pts[i].at), y = valueToY(l, pts[i].value, row);
            if (x < kHeaderW - kHandle || x > W + kHandle) continue;
            const bool dragged = mAutoDragging && mAutoPress == l.model.id && mAutoPoint == (int)i;
            drawCircle(t, x, y, kHandle + (dragged ? 1.0 : 0.0), Paint::filledStroked(fade(palette::popover(), a), line, 1.5));
        }
    }

    void Timeline::paintAutomation(IRenderTarget &t) const
    {
        for (const auto &row : mRowMotion.rows())
        {
            if (row.data.automation.empty()) continue;
            const auto it = mAutos.find(row.data.automation);
            if (it == mAutos.end()) continue;
            const AutoLive &l = it->second;
            const Rect r{0.0, kRulerH + row.liveY() - mScrollY.value(), width.value(), kRowH};
            const double a = row.liveAlpha();
            if (r.bottom() < kRulerH || r.y > height.value() || a <= 0.001) continue;
            // the range's two ends, faint
            t.setStroke(fade(palette::whiteAlpha(0.06), a), 1.0);
            for (double y : {r.y + kPad, r.bottom() - kPad})
            {
                t.beginPath(); t.moveTo(kHeaderW, std::round(y) + 0.5); t.lineTo(width.value(), std::round(y) + 0.5); t.strokePath();
            }
            std::vector<solaris::AutoPointModel> pts = shownPoints(l);
            if (mAutoDragging && mAutoPress == l.model.id && mAutoPoint >= 0 && mAutoPoint < (int)pts.size())
            {
                pts[(size_t)mAutoPoint].at = mAutoAt; // the pointer is the animation
                pts[(size_t)mAutoPoint].value = mAutoValue;
                std::stable_sort(pts.begin(), pts.end(), [](const auto &x, const auto &y) { return x.at < y.at; });
            }
            const double f = l.t.value();
            if (f < 1.0 && l.from.size() != l.to.size())
            {
                // a point more or fewer: the two curves cross-fade
                paintCurve(t, l, l.from, r, a * (1.0 - f), false);
                paintCurve(t, l, l.to, r, a * f, true);
            }
            else paintCurve(t, l, pts, r, a, true);
        }
    }

    bool Timeline::autoGesture(const Gesture &g, const Point &local)
    {
        const bool inRows = local.x >= kHeaderW && local.y >= kRulerH;
        switch (g.type)
        {
        case Gesture::Type::Down:
        {
            mAutoPress.clear();
            mAutoPoint = -1;
            mAutoDragging = false;
            if (!inRows) return false;
            const std::string au = autoAt(local.y);
            if (au.empty()) return false;
            mAutoPress = au;
            mAutoPoint = pointNear(au, local);
            return true;
        }
        case Gesture::Type::DragStart:
            if (mAutoPress.empty() || mAutoPoint < 0) return !mAutoPress.empty();
            mAutoDragging = true;
            [[fallthrough]];
        case Gesture::Type::Drag:
        {
            if (!mAutoDragging) return !mAutoPress.empty();
            const auto it = mAutos.find(mAutoPress);
            if (it == mAutos.end()) return true;
            mAutoAt = snap(xToBeat(local.x)); // the grid you see (R-UI-10)
            mAutoValue = yToValue(it->second, local.y, autoRowRect(mAutoPress));
            return true;
        }
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
        {
            if (mAutoPress.empty()) return false;
            if (mAutoDragging)
            {
                const auto it = mAutos.find(mAutoPress);
                const auto pts = it != mAutos.end() ? it->second.to : std::vector<solaris::AutoPointModel>{};
                if (mAutoPoint >= 0 && mAutoPoint < (int)pts.size() && onCommand)
                {
                    // it stays where it was let go: the drawn curve starts from there
                    AutoLive &l = it->second;
                    l.from = l.to;
                    l.from[(size_t)mAutoPoint].at = mAutoAt;
                    l.from[(size_t)mAutoPoint].value = mAutoValue;
                    std::stable_sort(l.from.begin(), l.from.end(), [](const auto &x, const auto &y) { return x.at < y.at; });
                    l.to = l.from;
                    const std::string line = "auto point move " + mAutoPress + " --at " + beats(pts[(size_t)mAutoPoint].at) + " --to " + beats(mAutoAt) +
                                             " --value " + num(mAutoValue);
                    mAutoDragging = false;
                    mAutoPress.clear();
                    onCommand(line);
                    return true;
                }
            }
            mAutoDragging = false;
            const bool was = !mAutoPress.empty();
            mAutoPress.clear();
            return was;
        }
        case Gesture::Type::Click:
        {
            if (!inRows) return false;
            const std::string au = autoAt(local.y);
            if (au.empty()) return false;
            if (pointNear(au, local) >= 0) return true; // a click on a point selects nothing, adds nothing
            const auto it = mAutos.find(au);
            const double at = snap(xToBeat(local.x)); // the grid you see (R-UI-10)
            const double v = yToValue(it->second, local.y, autoRowRect(au));
            if (onCommand) onCommand("auto point add " + au + " --at " + beats(at) + " --value " + num(v));
            return true;
        }
        case Gesture::Type::DoubleClick:
        {
            if (!inRows) return false;
            const std::string au = autoAt(local.y);
            if (au.empty()) return false;
            const int i = pointNear(au, local);
            const auto it = mAutos.find(au);
            if (i >= 0 && it != mAutos.end() && i < (int)it->second.to.size() && onCommand)
                onCommand("auto point delete " + au + " --at " + beats(it->second.to[(size_t)i].at));
            return true;
        }
        case Gesture::Type::RightClick:
        {
            if (!inRows) return false;
            const std::string au = autoAt(local.y);
            if (au.empty() || !onMenu) return !au.empty();
            const int i = pointNear(au, local);
            const auto it = mAutos.find(au);
            std::vector<cosmo_v2::ContextMenu::Item> items;
            if (i >= 0 && it != mAutos.end() && i < (int)it->second.to.size())
            {
                const std::string at = beats(it->second.to[(size_t)i].at), now = it->second.to[(size_t)i].shape;
                for (const char *s : {"linear", "hold", "smooth"})
                {
                    std::string label = std::string(s);
                    label[0] = (char)std::toupper((unsigned char)label[0]);
                    if (now == s) label += "  \xC2\xB7 now";
                    items.push_back({label, [this, au, at, s] { if (onCommand) onCommand("auto point shape " + au + " --at " + at + " --shape " + s); }});
                }
                items.push_back({"Delete Point", [this, au, at] { if (onCommand) onCommand("auto point delete " + au + " --at " + at); }});
            }
            items.push_back({"Copy ID", [this, au] { if (onCopy) onCopy(au); }});                 // R-UI-11
            items.push_back({"Copy as Formula", [this, au] { if (onCopy) onCopy("=" + au); }});   // what a formula reads it by
            items.push_back({"Delete Automation", [this, au] { if (onCommand) onCommand("auto delete " + au + " --unbind"); }});
            onMenu(items, g.pos);
            return true;
        }
        default:
            return !mAutoPress.empty();
        }
    }
}
}
