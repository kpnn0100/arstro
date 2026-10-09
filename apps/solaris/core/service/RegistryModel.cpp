#include "RegistryModel.h"

namespace arstro
{
namespace solaris
{
    DeviceModel deviceModelOf(const DeviceType &t, const std::vector<double> &values)
    {
        DeviceModel m;
        m.type = t.name;
        m.label = t.label;
        m.instrument = t.kind == DeviceKind::Instrument;
        m.takesSample = t.takesSample;
        // Every registry parameter, stored or not: the UI draws a control per parameter (R-UI-5).
        for (size_t i = 0; i < t.params.size(); ++i)
        {
            const ParamSpec &spec = t.params[i];
            ParamModel pm;
            pm.name = spec.name;
            pm.label = spec.label;
            pm.unit = spec.unit;
            pm.min = spec.min;
            pm.max = spec.max;
            pm.def = spec.def;
            pm.choices = spec.choices;
            pm.logScale = spec.logScale;
            pm.integer = spec.integer;
            pm.value = i < values.size() ? values[i] : spec.def;
            pm.text = paramToText(spec, pm.value);
            m.params.push_back(pm);
        }
        return m;
    }
}
}
