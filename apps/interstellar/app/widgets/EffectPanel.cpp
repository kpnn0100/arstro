#include "EffectPanel.h"
#include "KeyState.h"
#include "CommandLine.h"
#include "TextFit.h"
#include "../../../cosmo/widgets/SectionHeader.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kPadX = 9.75;
        constexpr double kRowGap = 6.5;
    }

    EffectPanel::EffectPanel() { clipToBounds = true; }

    std::vector<EffectPanel::Row> &EffectPanel::rowsFor(const interstellar::EffectModel &e)
    {
        auto it = mRowsByType.find(e.type);
        if (it != mRowsByType.end()) return it->second;
        std::vector<Row> rows;
        auto make = [&](const std::string &key, const std::string &label, double min, double max, bool percent) {
            Row r;
            r.key = key;
            r.percent = percent;
            r.slider = std::make_shared<cosmo_v2::SliderRow>(label, percent ? min * 100.0 : min, percent ? max * 100.0 : max, 0.0);
            r.slider->onChange = [this, key, percent](double v) {
                if (mId.empty() || !onCommand) return;
                onCommand("set " + cmd::quote(mId + "." + key + "=" + cmd::num(percent ? v / 100.0 : v)));
            };
            r.slider->visible = false;
            // R-ANIM-3: a diamond keys this parameter at the reference frame
            r.slider->setKeyGutter(true);
            r.slider->onKeyClick = [this, key] {
                if (mId.empty() || !onCommand) return;
                onCommand(keys::toggle(mId + "." + key, mKeyStates.count(key) ? mKeyStates[key] : 0, mKeyNow));
            };
            addChild(r.slider);
            rows.push_back(r);
        };
        make("mix", "Mix", 0.0, 1.0, true);
        for (const auto &p : e.params) make(p.key, p.label + (p.unit == "px" ? " (px)" : p.unit == "deg" ? " (\xC2\xB0)" : ""), p.min, p.max, p.unit.empty());
        return mRowsByType.emplace(e.type, std::move(rows)).first->second;
    }

    void EffectPanel::bind(const interstellar::AppModel &m, const std::string &id, bool interacting)
    {
        const interstellar::EffectModel *e = nullptr;
        for (const auto &x : m.effects) if (x.id == id) e = &x;
        mId = e ? e->id : std::string();
        mType = e ? e->type : std::string();
        mLabel = e ? e->label : std::string();
        mNode = e ? e->nodeBind : std::string();
        for (auto &kv : mRowsByType)
            for (auto &r : kv.second) r.slider->visible = e && kv.first == e->type;
        if (!e) return;
        auto &rows = rowsFor(*e);
        mKeyNow = keys::sourceNow(m, e->node);
        mKeyStates.clear();
        for (auto &r : rows)
        {
            mKeyStates[r.key] = keys::state(m, e->id, r.key, mKeyNow);
            r.slider->setKeyState(mKeyStates[r.key]);
            r.slider->visible = true;
            if (interacting) continue;   // a gesture in flight outranks the model
            double v = 0.0;
            if (r.key == "mix") v = e->mix;
            else for (const auto &p : e->params) if (p.key == r.key) v = p.value;
            r.slider->setValue(r.percent ? v * 100.0 : v);
        }
    }

    std::shared_ptr<cosmo_v2::SliderRow> EffectPanel::slider(const std::string &key) const
    {
        const auto it = mRowsByType.find(mType);
        if (it == mRowsByType.end()) return nullptr;
        for (const auto &r : it->second) if (r.key == key) return r.slider;
        return nullptr;
    }

    void EffectPanel::layout()
    {
        const double w = width.value();
        const auto it = mRowsByType.find(mType);
        if (it == mRowsByType.end()) return;
        double y = kHeaderH;
        for (auto &r : it->second)
        {
            r.slider->x.set(kPadX);
            r.slider->y.set(y);
            r.slider->width.set(std::max(0.0, w - 2 * kPadX));
            r.slider->height.set(cosmo_v2::SliderRow::kRowHeight);
            r.slider->layout();
            y += cosmo_v2::SliderRow::kRowHeight + kRowGap;
        }
    }

    void EffectPanel::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::card()));
        if (mId.empty()) return;
        std::string title = mLabel;
        for (char &c : title) c = (char)std::toupper((unsigned char)c);
        cosmo_v2::drawSectionHeader(t, kPadX, 0.0, w - 2 * kPadX, title.c_str());
        // which node, and the address root — what a script would type
        const std::string where = mId + (mNode.empty() ? std::string() : " \xC2\xB7 on " + mNode);
        const double tw = t.measureText(where, 9.0, font::mono());
        t.setFill(palette::mutedForeground());
        t.drawText(where, std::max(kPadX, w - kPadX - tw), textfit::baseline(kHeaderH * 0.5, 9.0), 9.0, font::mono());
    }
}
}
