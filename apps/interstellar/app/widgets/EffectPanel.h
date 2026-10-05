/*
 *  interstellar_v1 — EffectPanel: the parameters of the effect selected in the IMAGE PROCESSING
 *  list (R-FX-5), as cosmo's own `SliderRow`s — Mix first, then the plugin's parameters from the
 *  catalog the model publishes (label, range, unit). A 0..1 parameter shows as 0..100 %.
 *
 *  Every drag is one command line, `set <ef>.<key>=<value>` (engine units). Slider rows are built
 *  per plugin TYPE the first time that type is shown and kept (a Segment never drops a child);
 *  the rows of other types are culled. While the pointer is down in here, `bind` does not re-seed
 *  the sliders — a gesture in flight outranks the model.
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "../../../cosmo/widgets/SliderRow.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    class EffectPanel : public artboard::Segment
    {
    public:
        static constexpr double kHeaderH = 29.25;

        EffectPanel();
        /** Show effect `id` of the model (nothing when it is not there). */
        void bind(const interstellar::AppModel &m, const std::string &id, bool interacting);
        void layout();

        const std::string &effect() const { return mId; }
        /** The slider for `key` ("mix" or a parameter key) of the effect shown, or nullptr. */
        std::shared_ptr<cosmo_v2::SliderRow> slider(const std::string &key) const;

        std::function<void(const std::string &line)> onCommand;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        struct Row
        {
            std::string key;
            bool percent = false;                  // a 0..1 parameter, shown 0..100
            std::shared_ptr<cosmo_v2::SliderRow> slider;
        };
        std::vector<Row> &rowsFor(const interstellar::EffectModel &e);

        std::map<std::string, std::vector<Row>> mRowsByType;   // type → its rows (Mix first)
        std::string mId, mType, mLabel, mNode;
    };
}
}
