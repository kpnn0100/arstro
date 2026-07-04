/*
 *  Cosmo by arstro — PresetPanel: a preset BROWSER docked to the left edge of the
 *  window (as opposed to PresetBar's Save/Import/Export actions, or the Preset
 *  menu's flat top-level quick-apply list). Shows every .apf under the preset
 *  directory as an expandable folder tree (a "/" in a saved preset's name nests
 *  it into a subfolder -- see CosmoApp::savePreset); click a folder to expand or
 *  collapse it, double-click a preset to apply it to the current image.
 *
 *  Presentational + self-scanning: setRoot(dir) re-reads the directory (keeping
 *  whichever folders were already expanded); the host doesn't otherwise feed it.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class PresetPanel : public artboard::Segment
    {
    public:
        explicit PresetPanel(const artboard::Color &accent);

        /** Rescan `dir` for a folder/`.apf` tree (empty dir -> an empty panel). */
        void setRoot(const std::string &dir);
        /** Height only -- width is driven by the host (animated open/close), and
         *  content reads it live from width.value() every paint. */
        void layout(double h);
        /** Scroll the tree by a mouse-wheel delta (positive = up). */
        void scrollBy(double wheelDelta);

        /** A preset was double-clicked; `name` is relative to the preset dir, "/"-
         *  separated, no extension (e.g. "Portraits/Warm") -- ready for applyPreset().
         *  The applied preset is highlighted with the accent colour until another
         *  one is picked (or the tree is rescanned and it's no longer present). */
        std::function<void(const std::string &)> onApply;
        /** Right-clicked a preset row (never a folder); `name` like onApply's, plus
         *  the world position to show a context menu (e.g. host offers "Delete"). */
        std::function<void(const std::string &, double, double)> onContext;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;

    private:
        struct Node
        {
            bool folder = false;
            std::string name;      // display name (no extension)
            std::string relPath;   // "/"-joined path from the preset dir root
            std::vector<Node> kids;  // folders only
            bool expanded = false;
        };
        struct FlatRow { std::string name, relPath; int depth = 0; bool folder = false; bool expanded = false; };

        static std::vector<Node> scanDir(const std::string &absDir, const std::string &relPrefix);
        void flatten(const std::vector<Node> &nodes, int depth, std::vector<FlatRow> &out) const;
        static Node *nodeAtRow(std::vector<Node> &nodes, int &counter, int target);
        double clampScroll(double s) const;

        artboard::Color mAccent;
        std::string mRootDir;
        std::vector<Node> mRoot;
        std::string mSelected;  // relPath of the last-applied preset (highlighted)
        double mScroll = 0.0;
        bool mDragging = false;
        double mDragStartY = 0.0, mDragStartScroll = 0.0;
    };
}
}
