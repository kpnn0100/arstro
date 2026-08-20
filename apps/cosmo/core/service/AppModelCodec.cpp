#include "AppModelCodec.h"
#include "engine/EditParamsIO.h"
#include <sstream>

namespace arstro
{
namespace cosmo
{
    const char *screenName(Screen s)
    {
        switch (s)
        {
            case Screen::Splash: return "splash";
            case Screen::Home: return "home";
            case Screen::Loading: return "loading";
            case Screen::Editor: return "editor";
        }
        return "unknown";
    }

    namespace
    {
        std::string jsonEscape(const std::string &s)
        {
            std::string o;
            for (char c : s)
            {
                if (c == '"' || c == '\\') { o += '\\'; o += c; }
                else if (c == '\n') o += "\\n";
                else o += c;
            }
            return o;
        }

        void kv(std::ostringstream &o, bool json, const char *key, const std::string &val, bool quote = true)
        {
            if (json) o << "  \"" << key << "\": " << (quote ? "\"" + jsonEscape(val) + "\"" : val) << ",\n";
            else o << key << '=' << val << '\n';
        }
        void kvi(std::ostringstream &o, bool json, const char *key, long long val)
        {
            kv(o, json, key, std::to_string(val), false);
        }
        void kvb(std::ostringstream &o, bool json, const char *key, bool val)
        {
            kv(o, json, key, val ? (json ? "true" : "1") : (json ? "false" : "0"), false);
        }
    }

    std::string formatModel(const AppModel &m, const ModelDumpOptions &o)
    {
        std::ostringstream s;
        const bool j = o.json;
        if (j) s << "{\n";

        // Volatile-by-nature fields, excluded from a stable dump: two front ends showing the
        // same state legitimately disagree about how many revisions or frames it took.
        if (!o.stable)
        {
            kvi(s, j, "revision", m.revision);
            kvi(s, j, "frameSeq", m.frameSeq);
        }
        kv(s, j, "screen", screenName(m.screen));
        kv(s, j, "projectPath", m.projectPath);
        kv(s, j, "projectName", m.projectName);
        kvb(s, j, "dirty", m.dirty);
        kvi(s, j, "imageCount", m.imageCount);
        kvi(s, j, "selectedNode", m.selectedNode);
        kvi(s, j, "currentSlot", m.currentSlot);
        kvi(s, j, "editGroup", m.editGroup);

        kvi(s, j, "historyNodes", m.history.nodes);
        kvi(s, j, "historyCurrent", m.history.current);
        kvb(s, j, "canUndo", m.history.canUndo);
        kvb(s, j, "canRedo", m.history.canRedo);

        kvb(s, j, "loadActive", m.load.active);
        kvi(s, j, "loadDone", m.load.done);
        kvi(s, j, "loadTotal", m.load.total);
        kvi(s, j, "loadWorkers", m.load.workers);

        kvb(s, j, "exportActive", m.exports.active);
        kvi(s, j, "exportDone", m.exports.done);
        kvi(s, j, "exportTotal", m.exports.total);
        kvi(s, j, "exportFailures", m.exports.failures);

        kvi(s, j, "settingsPreviewEdge", m.settings.previewEdge);
        kvi(s, j, "settingsThreads", m.settings.threads);
        kvb(s, j, "settingsUseGpu", m.settings.useGpu);
        kvi(s, j, "settingsCpuPercent", m.settings.cpuPercent);
        kvi(s, j, "settingsUiScale", m.settings.uiScale);
        kvb(s, j, "settingsTouchUi", m.settings.touchUi);

        kvi(s, j, "budgetPercent", m.budget.percent);
        kvi(s, j, "budgetTotal", m.budget.total);
        kvi(s, j, "budgetEngineThreads", m.budget.engineThreads);
        kvi(s, j, "budgetDecodeWorkers", m.budget.decodeWorkers);
        // The measured peak depends on real thread scheduling, so it can differ between two
        // runs of the identical commands. Never part of a comparison.
        if (!o.stable) kvi(s, j, "budgetPeakDecode", m.budget.peakDecode);

        kvb(s, j, "gpuAvailable", m.gpuAvailable);
        kvb(s, j, "gpuActive", m.gpuActive);
        if (!m.lastError.empty()) kv(s, j, "lastError", m.lastError);

        // ── the tree, one line per node, parent by node id so it reads as a tree ──
        if (j) s << "  \"nodes\": [\n";
        else s << "nodes=" << m.nodes.size() << '\n';
        for (size_t i = 0; i < m.nodes.size(); ++i)
        {
            const NodeModel &n = m.nodes[i];
            const char *state = n.group ? "group" : (n.slot >= 0 ? "image" : (n.pending ? "pending" : "failed"));
            if (j)
            {
                s << "    {\"node\": " << n.node << ", \"parent\": " << n.parent << ", \"depth\": " << n.depth
                  << ", \"kind\": \"" << state << "\", \"name\": \"" << jsonEscape(n.name) << "\", \"slot\": "
                  << n.slot << ", \"bypass\": " << (n.bypass ? "true" : "false") << ", \"selected\": "
                  << (n.selected ? "true" : "false") << "}" << (i + 1 < m.nodes.size() ? "," : "") << '\n';
            }
            else
            {
                s << "  node=" << n.node << " parent=" << n.parent << " depth=" << n.depth
                  << " kind=" << state << " slot=" << n.slot << " bypass=" << (n.bypass ? 1 : 0)
                  << " selected=" << (n.selected ? 1 : 0) << " name=" << n.name << '\n';
            }
        }
        if (j) s << "  ]";

        if (o.params)
        {
            // BOTH, and always labelled: `params` is the EFFECTIVE value the engine renders
            // (the edit target's own, with every ancestor group's stacked on top) and
            // `ownParams` is what the panels edit and what `set` writes. Printing only the
            // effective one is how three separate defects hid — D-28, D-31 and the "the curve
            // switched to the green one" report all turn on exactly this distinction, and every
            // one of them was diagnosed by hand-reasoning about which of the two a value was,
            // because a dump could not say. `ownParams` is emitted only when it DIFFERS: with
            // no groups the two are equal and a second identical block is noise.
            const std::string eff = serializeParams(m.params);
            const std::string own = m.hasEditTarget ? serializeParams(m.ownParams) : std::string();
            if (j)
            {
                s << ",\n  \"params\": \"" << jsonEscape(eff) << "\"";
                if (!own.empty() && own != eff)
                    s << ",\n  \"ownParams\": \"" << jsonEscape(own) << "\"";
            }
            else
            {
                s << "params:\n" << eff;
                if (!own.empty() && own != eff) s << "ownParams:\n" << own;
            }
        }
        if (j) s << "\n}\n";
        // Every scalar key emits its own trailing comma, and `"nodes"` always follows at
        // least one of them, so the object closes correctly without a fix-up pass.
        return s.str();
    }
}
}
