#include "PresetPanel.h"
#include "../Chrome.h"
#include "../CosmoTheme.h"
#include <algorithm>
#include <filesystem>
#include <set>

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kHeaderH = 30.0;
        constexpr double kRowH = 22.0;
        constexpr double kIndent = 14.0;
        constexpr double kIndentStep = 14.0;
    }

    PresetPanel::PresetPanel(const Color &accent) : mAccent(accent) { clipToBounds = true; }

    std::vector<PresetPanel::Node> PresetPanel::scanDir(const std::string &absDir, const std::string &relPrefix)
    {
        std::vector<Node> out;
        std::error_code ec;
        if (!std::filesystem::is_directory(absDir, ec)) return out;
        for (const auto &e : std::filesystem::directory_iterator(absDir, ec))
        {
            if (e.is_directory(ec))
            {
                Node n;
                n.folder = true;
                n.name = e.path().filename().string();
                n.relPath = relPrefix.empty() ? n.name : relPrefix + "/" + n.name;
                n.kids = scanDir(e.path().string(), n.relPath);
                out.push_back(std::move(n));
            }
            else if (e.path().extension() == ".apf")
            {
                Node n;
                n.folder = false;
                n.name = e.path().stem().string();
                n.relPath = relPrefix.empty() ? n.name : relPrefix + "/" + n.name;
                out.push_back(std::move(n));
            }
        }
        std::sort(out.begin(), out.end(), [](const Node &a, const Node &b) {
            if (a.folder != b.folder) return a.folder;  // folders before presets
            return a.name < b.name;
        });
        return out;
    }

    void PresetPanel::setRoot(const std::string &dir)
    {
        mRootDir = dir;
        // Remember which folders were open so a rescan (e.g. after Save Preset)
        // doesn't collapse the tree the user was browsing.
        std::set<std::string> expanded;
        std::function<void(const std::vector<Node> &)> collect = [&](const std::vector<Node> &nodes) {
            for (const auto &n : nodes) { if (n.folder && n.expanded) expanded.insert(n.relPath); collect(n.kids); }
        };
        collect(mRoot);

        mRoot = scanDir(dir, "");

        std::function<void(std::vector<Node> &)> restore = [&](std::vector<Node> &nodes) {
            for (auto &n : nodes) { if (n.folder) { n.expanded = expanded.count(n.relPath) != 0; restore(n.kids); } }
        };
        restore(mRoot);
        mScroll = 0.0;
    }

    void PresetPanel::flatten(const std::vector<Node> &nodes, int depth, std::vector<FlatRow> &out) const
    {
        for (const auto &n : nodes)
        {
            out.push_back({n.name, n.relPath, depth, n.folder, n.expanded});
            if (n.folder && n.expanded) flatten(n.kids, depth + 1, out);
        }
    }

    PresetPanel::Node *PresetPanel::nodeAtRow(std::vector<Node> &nodes, int &counter, int target)
    {
        for (auto &n : nodes)
        {
            if (counter == target) return &n;
            ++counter;
            if (n.folder && n.expanded)
                if (Node *found = nodeAtRow(n.kids, counter, target)) return found;
        }
        return nullptr;
    }

    void PresetPanel::layout(double h) { height.set(h); }

    double PresetPanel::clampScroll(double s) const
    {
        std::vector<FlatRow> rows;
        flatten(mRoot, 0, rows);
        const double contentH = rows.size() * kRowH;
        const double viewport = height.value() - kHeaderH;
        const double maxScroll = contentH > viewport ? contentH - viewport : 0.0;
        return s < 0.0 ? 0.0 : (s > maxScroll ? maxScroll : s);
    }

    void PresetPanel::scrollBy(double wheelDelta)
    {
        mScroll = clampScroll(mScroll - wheelDelta * kRowH);
    }

    bool PresetPanel::handleGesture(const Gesture &g, const Point &local)
    {
        using T = Gesture::Type;
        if (g.type == T::DragStart)
        {
            mDragging = true;
            mDragStartY = g.pos.y;
            mDragStartScroll = mScroll;
            return true;
        }
        if (g.type == T::Drag)
        {
            if (mDragging) mScroll = clampScroll(mDragStartScroll - (g.pos.y - mDragStartY));
            return true;
        }
        if (g.type == T::Up || g.type == T::Drop) { mDragging = false; return true; }
        if (g.type != T::Click && g.type != T::DoubleClick && g.type != T::RightClick)
            return Segment::handleGesture(g, local);

        std::vector<FlatRow> rows;
        flatten(mRoot, 0, rows);
        const int i = (int)((local.y - kHeaderH + mScroll) / kRowH);
        if (local.y < kHeaderH || i < 0 || i >= (int)rows.size())
            return true;  // still inside the panel; just not on a row

        if (rows[i].folder)
        {
            if (g.type == T::Click)
            {
                int counter = 0;
                if (Node *n = nodeAtRow(mRoot, counter, i)) n->expanded = !n->expanded;
            }
        }
        else if (g.type == T::DoubleClick)
        {
            mSelected = rows[i].relPath;
            if (onApply) onApply(rows[i].relPath);
        }
        else if (g.type == T::RightClick)
        {
            if (onContext) onContext(rows[i].relPath, g.pos.x, g.pos.y);
        }
        return true;
    }

    void PresetPanel::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        // The whole panel is clipped to its own (possibly mid-animation, narrowing)
        // bounds -- otherwise text could briefly spill past the edge while the
        // open/close slide is in progress.
        t.save();
        t.clipRect(0, 0, w, h);
        drawRoundedRect(t, Rect{0, 0, w, h}, radius::panel(), Paint::filledStroked(palette::panel(), palette::line(), 1.0));

        t.setFill(palette::ink());
        for (double ox : {0.0, 0.4}) t.drawText("PRESETS", 14.0 + ox, 20.0, 11.5);
        t.setStroke(palette::line(), 1.0);
        t.beginPath(); t.moveTo(0, kHeaderH); t.lineTo(w, kHeaderH); t.strokePath();

        std::vector<FlatRow> rows;
        flatten(mRoot, 0, rows);
        if (rows.empty())
        {
            t.setFill(palette::faint());
            t.drawText("no presets", 14.0, kHeaderH + 22.0, 10.0);
            t.restore();
            return;
        }

        for (int i = 0; i < (int)rows.size(); ++i)
        {
            const double y = kHeaderH + i * kRowH - mScroll;
            if (y < kHeaderH - kRowH || y > h) continue;  // cheap vertical cull
            const FlatRow &r = rows[i];
            const double indent = kIndent + r.depth * kIndentStep;
            const double cy = y + kRowH * 0.5;
            if (r.folder)
            {
                t.setFill(palette::muted());
                t.beginPath();
                if (r.expanded)
                { t.moveTo(indent - 4, cy - 3); t.lineTo(indent + 4, cy - 3); t.lineTo(indent, cy + 4); }
                else
                { t.moveTo(indent - 3, cy - 4); t.lineTo(indent - 3, cy + 4); t.lineTo(indent + 4, cy); }
                t.closePath();
                t.fillPath();
                t.setFill(palette::ink());
                t.drawText(r.name, indent + 12.0, cy + 4.0, 10.5);
            }
            else
            {
                const bool selected = !mSelected.empty() && r.relPath == mSelected;
                if (selected)
                {
                    drawRoundedRect(t, Rect{2.0, y + 1.0, w - 10.0, kRowH - 2.0}, radius::control(),
                                    Paint::filled(Color{mAccent.r, mAccent.g, mAccent.b, 0.18f}));
                    t.setFill(mAccent);
                }
                else
                    t.setFill(palette::muted());
                t.drawText(r.name, indent, cy + 4.0, 10.0);
            }
        }
        t.restore();
    }
}
}
