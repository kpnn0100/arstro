#include "Bindings.h"
#include "Compile.h"
#include "device/Device.h"
#include <algorithm>
#include <set>

namespace arstro
{
namespace solaris
{
    bool describeAddress(const Project &p, const std::string &address, AddressSpec &out, std::string &err)
    {
        out = AddressSpec{};
        const auto dot = address.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= address.size())
        {
            err = "`" + address + "` is not an address (<node>.<field>)";
            return false;
        }
        out.node = address.substr(0, dot);
        out.field = address.substr(dot + 1);
        const std::string &f = out.field;
        if (out.node == "project")
        {
            if (f != "masterGain") { err = "project." + f + " is not a number a formula can drive (project.masterGain is)"; return false; }
            out.kind = AddressSpec::MasterGain;
            out.owner = "Master";
            out.label = "Gain";
            out.unit = "dB";
            out.lo = -120;
            out.hi = 12;
            out.value = p.header.masterGain;
            return true;
        }
        if (const Strip *s = p.strip(out.node))
        {
            out.owner = s->name;
            if (f == "gain") { out.kind = AddressSpec::StripGain; out.label = "Gain"; out.unit = "dB"; out.lo = -120; out.hi = 12; out.value = s->gain; return true; }
            if (f == "pan") { out.kind = AddressSpec::StripPan; out.label = "Pan"; out.lo = -1; out.hi = 1; out.value = s->pan; return true; }
            err = out.node + "." + f + " is not a number a formula can drive (a strip's gain and pan are)";
            return false;
        }
        for (const auto &sd : p.sends)
            if (sd.id == out.node)
            {
                if (f != "gain") { err = out.node + "." + f + " is not a number a formula can drive (a send's gain is)"; return false; }
                const Strip *from = p.strip(sd.from);
                out.kind = AddressSpec::SendGain;
                out.owner = from ? from->name : sd.from;
                const Strip *to = p.strip(sd.to);
                out.label = "Send to " + (to ? to->name : sd.to);
                out.unit = "dB";
                out.lo = -120;
                out.hi = 12;
                out.value = sd.gain;
                return true;
            }
        for (const auto &r : p.racks)
            for (const auto &d : r.devices)
            {
                if (d.id != out.node) continue;
                const DeviceType *t = DeviceRegistry::find(d.type);
                if (!t) { err = out.node + " is a `" + d.type + "`, which this build does not know"; return false; }
                const int i = t->paramIndex(f);
                if (i < 0)
                {
                    err = out.node + " (" + t->label + ") has no parameter `" + f + "`";
                    return false;
                }
                const ParamSpec &ps = t->params[(size_t)i];
                if (ps.isChoice()) { err = out.node + "." + f + " is a choice, not a number a formula can drive"; return false; }
                out.kind = AddressSpec::DeviceParam;
                out.masterRack = r.track == "master";
                const Strip *s = p.strip(r.track);
                out.owner = out.masterRack ? std::string("Master") : (s ? s->name : r.track);
                out.label = ps.label;
                out.unit = ps.unit;
                out.lo = ps.min;
                out.hi = ps.max;
                out.integer = ps.integer;
                out.paramIndex = i;
                out.value = ps.def;
                for (const auto &kv : d.params)
                    if (kv.first == f) parseParam(ps, kv.second, out.value);
                return true;
            }
        err = "`" + out.node + "` is no strip, send, device or `project`";
        return false;
    }

    std::vector<OrderedBinding> orderBindings(const Project &p, std::vector<std::string> &problems)
    {
        std::vector<OrderedBinding> all;
        for (const auto &b : p.bindings)
        {
            OrderedBinding ob;
            ob.binding = &b;
            std::string err;
            if (!describeAddress(p, b.address, ob.spec, err)) { problems.push_back(b.address + ": " + err); continue; }
            if (!parseFormula(b.formula, ob.formula, err)) { problems.push_back(b.address + ": formula `" + b.formula + "`: " + err); continue; }
            bool ok = true;
            for (const auto &name : ob.formula.names)
            {
                if (p.automation(name)) { ob.reads.push_back(name); continue; }
                AddressSpec other;
                std::string why;
                if (describeAddress(p, name, other, why)) { ob.reads.push_back(name); continue; }
                problems.push_back(b.address + ": formula `" + b.formula + "` reads `" + name + "`, which is no automation and no number (" + why + ")");
                ok = false;
                break;
            }
            if (ok) all.push_back(std::move(ob));
        }
        // a link reads an earlier binding: Kahn's order, ties in file order
        std::map<std::string, size_t> at;
        for (size_t i = 0; i < all.size(); ++i) at[all[i].binding->address] = i;
        std::vector<int> pending(all.size(), 0);
        std::vector<std::vector<size_t>> readers(all.size());
        for (size_t i = 0; i < all.size(); ++i)
            for (const auto &name : all[i].reads)
            {
                const auto it = at.find(name);
                if (it == at.end()) continue; // an automation or an unbound address: a value already known
                ++pending[i];
                readers[it->second].push_back(i);
            }
        std::vector<OrderedBinding> out;
        std::vector<bool> done(all.size(), false);
        for (bool progress = true; progress;)
        {
            progress = false;
            for (size_t i = 0; i < all.size(); ++i)
                if (!done[i] && pending[i] == 0)
                {
                    done[i] = true;
                    progress = true;
                    for (size_t r : readers[i]) --pending[r];
                    out.push_back(all[i]);
                }
        }
        std::string loop;
        for (size_t i = 0; i < all.size(); ++i)
            if (!done[i]) loop += (loop.empty() ? "" : " → ") + all[i].binding->address;
        for (size_t i = 0; i < all.size(); ++i)
            if (!done[i]) problems.push_back(all[i].binding->address + ": a loop of links (" + loop + ") — a value cannot depend on itself");
        return out;
    }

    bool checkBinding(const Project &p, const std::string &address, const std::string &formula, std::string &err)
    {
        Project q = p;
        if (Binding *b = q.binding(address)) b->formula = formula;
        else q.bindings.push_back(Binding{address, formula, {}, {}});
        std::vector<std::string> problems;
        orderBindings(q, problems);
        for (const auto &x : problems)
            if (x.compare(0, address.size() + 1, address + ":") == 0)
            {
                err = x.substr(address.size() + 2);
                return false;
            }
        return true;
    }
}
}
