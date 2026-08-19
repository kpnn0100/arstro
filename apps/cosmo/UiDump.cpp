#include "UiDump.h"
#include "UiInspectable.h"
#include <cstdio>
#include <sstream>
#include <typeinfo>
#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#include <cstdlib>
#endif

namespace arstro
{
namespace cosmo_v2
{
    namespace
    {
        /** `arstro::cosmo_v2::HomeScreen` rather than `N6arstro8cosmo_v210HomeScreenE`. RTTI is
         *  what makes this useful without every widget having to declare a name it would then
         *  forget to update. */
        std::string typeName(const artboard::Segment &s)
        {
            const char *raw = typeid(s).name();
#if defined(__GNUC__) || defined(__clang__)
            int status = 0;
            char *dem = abi::__cxa_demangle(raw, nullptr, nullptr, &status);
            if (status == 0 && dem)
            {
                std::string out(dem);
                std::free(dem);
                // Trim the namespace: every node is in one of two, and the noise costs more
                // than the precision buys at 60 lines a dump.
                const auto pos = out.rfind("::");
                return pos == std::string::npos ? out : out.substr(pos + 2);
            }
#endif
            return raw;
        }

        /** A widget's own account of what it drew, when it offers one (UiInspectable). */
        std::string inspect(const artboard::Segment &s)
        {
            const auto *i = dynamic_cast<const UiInspectable *>(&s);
            return i ? i->uiDetail() : std::string();
        }

        std::string jsonEscape(const std::string &in)
        {
            std::string out;
            for (char ch : in)
            {
                if (ch == '"' || ch == '\\') { out += '\\'; out += ch; }
                else if (ch == '\n') out += "\\n";
                else out += ch;
            }
            return out;
        }

        std::string num(double v)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f", v);
            return buf;
        }

        void walk(std::ostringstream &o, const artboard::Segment &s, int depth,
                  const UiDumpOptions &opt, bool json, bool &first)
        {
            if (depth > opt.maxDepth) return;
            if (opt.visibleOnly && s.isFadedOut()) return;

            const artboard::Transform w = s.worldTransform();
            // The world transform's translation is where the node actually ended up, which is
            // the question being asked — a local x/y says nothing when a parent moved.
            const double wx = w.e, wy = w.f;
            const std::string t = typeName(s);

            if (json)
            {
                if (!first) o << ",\n";
                first = false;
                o << "    {\"type\":\"" << t << "\",\"depth\":" << depth
                  << ",\"x\":" << num(wx) << ",\"y\":" << num(wy)
                  << ",\"w\":" << num(s.width.value()) << ",\"h\":" << num(s.height.value())
                  << ",\"visible\":" << (s.visible ? "true" : "false")
                  << ",\"opacity\":" << num(s.opacity.value())
                  << ",\"shown\":" << (s.isFadedOut() ? "false" : "true")
                  << ",\"hover\":" << (s.isHovered() ? "true" : "false")
                  << ",\"enabled\":" << (s.enabled ? "true" : "false")
                  << ",\"clip\":" << (s.clipToBounds ? "true" : "false")
                  << ",\"children\":" << s.childCount();
                const std::string detail = inspect(s);
                if (!detail.empty()) o << ",\"detail\":\"" << jsonEscape(detail) << "\"";
                o << "}";
            }
            else
            {
                for (int i = 0; i < depth; ++i) o << "  ";
                o << t << "  @" << num(wx) << "," << num(wy)
                  << "  " << num(s.width.value()) << "x" << num(s.height.value())
                  << "  op=" << num(s.opacity.value())
                  << (s.visible ? "" : " hidden")
                  << (s.isFadedOut() ? " SHOWN=no" : "")
                  << (s.isHovered() ? " hover" : "")
                  << (s.enabled ? "" : " disabled")
                  << (s.clipToBounds ? " clip" : "")
                  << (s.childCount() ? "  kids=" + std::to_string(s.childCount()) : "")
                  << "\n";
            const std::string detail = inspect(s);
            if (!detail.empty())
            {
                for (int i = 0; i <= depth; ++i) o << "  ";
                o << "· " << detail << "\n";
            }
            }
            for (const auto &c : s.children())
                if (c) walk(o, *c, depth + 1, opt, json, first);
        }
    }

    const artboard::Segment *findSegmentByType(const artboard::Segment &root, const char *type)
    {
        if (typeName(root) == type) return &root;
        for (const auto &c : root.children())
            if (c)
                if (const artboard::Segment *hit = findSegmentByType(*c, type)) return hit;
        return nullptr;
    }

    std::string dumpSegmentTree(const artboard::Segment &root, const std::string &label,
                                const UiDumpOptions &o)
    {
        std::ostringstream out;
        bool first = true;
        if (o.json)
        {
            out << "{\n  \"root\": \"" << label << "\",\n  \"nodes\": [\n";
            walk(out, root, 0, o, true, first);
            out << "\n  ]\n}\n";
        }
        else
        {
            out << "ui-root " << label << "\n";
            walk(out, root, 0, o, false, first);
        }
        return out.str();
    }
}
}
