// SolarisService — automation, formulas and `eval` (R-AUTO-1…8). An automation is a curve that
// moves nothing until a formula reads it; a formula is what decides an address's value.
#include "SolarisService.h"
#include "Bindings.h"
#include "Compile.h"
#include "Format.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    namespace
    {
        bool numberFlag(const Command &c, const std::string &name, double &out, std::string &err)
        {
            const std::string v = c.flag(name);
            if (!parseNumber(v, out) || !std::isfinite(out)) { err = "--" + name + " must be a number, got `" + v + "`"; return false; }
            return true;
        }
        bool shapeOk(const std::string &s, std::string &err)
        {
            if (s == "linear" || s == "hold" || s == "smooth" || s == "bezier") return true;
            err = "a point's shape is linear, hold, smooth or bezier — not `" + s + "`";
            return false;
        }
        /** R-AUTO-10: a bezier point's handles from the flags given; the others kept. */
        bool handleFlags(const Command &c, AutoPoint &pt, bool &any, std::string &err)
        {
            static const char *speeds[] = {"speed-in", "speed-out"}, *infls[] = {"influence-in", "influence-out"};
            double *speed[] = {&pt.speedIn, &pt.speedOut}, *infl[] = {&pt.inflIn, &pt.inflOut};
            any = false;
            for (int k = 0; k < 2; ++k)
            {
                if (c.has(speeds[k]))
                {
                    if (!numberFlag(c, speeds[k], *speed[k], err)) return false;
                    any = true;
                }
                if (c.has(infls[k]))
                {
                    double v = 0;
                    if (!numberFlag(c, infls[k], v, err)) return false;
                    if (!(v > 0 && v <= 100))
                    {
                        err = std::string("--") + infls[k] + " is how far the handle reaches into the segment, 0 < x ≤ 100 %, not `" + c.flag(infls[k]) + "`";
                        return false;
                    }
                    *infl[k] = v;
                    any = true;
                }
            }
            return true;
        }
        AutoPoint *pointAt(Automation &a, double at)
        {
            const double t = toTick(at);
            for (auto &pt : a.points)
                if (std::fabs(pt.at - t) < 1e-9) return &pt;
            return nullptr;
        }
        void sortPoints(Automation &a)
        {
            std::stable_sort(a.points.begin(), a.points.end(), [](const AutoPoint &x, const AutoPoint &y) { return x.at < y.at; });
        }
    }

    double SolarisService::songEndBeats() const
    {
        double end = 0;
        for (const auto &c : mProject.clips) end = std::max(end, c.at + clipLengthBeats(mProject, c));
        return std::max(end, 4.0 * std::max(1, std::atoi(mProject.header.sig.c_str())));
    }

    std::vector<std::string> SolarisService::readersOf(const std::string &name) const
    {
        std::vector<std::string> out;
        for (const auto &b : mProject.bindings)
        {
            ParsedFormula f;
            std::string err;
            if (!parseFormula(b.formula, f, err)) continue;
            if (std::find(f.names.begin(), f.names.end(), name) != f.names.end()) out.push_back(b.address);
        }
        return out;
    }

    void SolarisService::dropBindingsOf(const std::set<std::string> &nodes)
    {
        auto &bs = mProject.bindings;
        const auto gone = std::remove_if(bs.begin(), bs.end(), [&](const Binding &b) {
            return nodes.count(b.address.substr(0, b.address.find('.'))) > 0;
        });
        if (gone != bs.end())
        {
            bs.erase(gone, bs.end());
            mBindingsTouched = true;
        }
    }

    bool SolarisService::autoCommand(const Command &c, std::string &err)
    {
        Project &p = mProject;
        switch (c.kind)
        {
        case K::AutoCreate:
        {
            const std::string address = c.arg(0);
            AddressSpec spec;
            if (!describeAddress(p, address, spec, err)) return false;
            Automation a;
            a.id = p.nextId("au");
            a.name = spec.owner + " \xC2\xB7 " + spec.label; // "Bass · Cutoff"
            a.unit = spec.unit;
            a.min = spec.lo;
            a.max = spec.hi;
            a.from = address;
            a.points.push_back(AutoPoint{0.0, spec.value, "linear", {}});
            a.points.push_back(AutoPoint{toTick(songEndBeats()), spec.value, "linear", {}});
            p.automations.push_back(a);
            const std::string formula = "=" + a.id;
            if (Binding *b = p.binding(address)) b->formula = formula; // automating replaces a formula (undoable)
            else p.bindings.push_back(Binding{address, formula, {}, {}});
            mBindingsTouched = true;
            if (p.device(spec.node)) mLastChanged[spec.node] = spec.field;
            changed("auto.added", a.id);
            changed("binding.set", address);
            mOutput = a.id + "\n";
            return true;
        }
        case K::AutoAdd:
        {
            Automation a;
            a.id = p.nextId("au");
            a.name = c.flag("name", "Automation " + a.id.substr(3));
            a.unit = c.flag("unit");
            if (c.has("min") && !numberFlag(c, "min", a.min, err)) return false;
            if (c.has("max") && !numberFlag(c, "max", a.max, err)) return false;
            if (!(a.min < a.max)) { err = "--min must be below --max"; return false; }
            p.automations.push_back(a);
            mBindingsTouched = true;
            changed("auto.added", a.id);
            mOutput = a.id + "\n";
            return true;
        }
        case K::AutoDelete:
        {
            const std::string id = c.arg(0);
            if (!p.automation(id)) { err = "no automation `" + id + "`"; return false; }
            const auto readers = readersOf(id);
            if (!readers.empty() && !c.has("unbind"))
            {
                err = id + " is read by ";
                for (size_t i = 0; i < readers.size(); ++i) err += (i ? ", " : "") + readers[i];
                err += " — clear those bindings, or add --unbind";
                return false;
            }
            for (const auto &r : readers)
                p.bindings.erase(std::remove_if(p.bindings.begin(), p.bindings.end(), [&](const Binding &b) { return b.address == r; }), p.bindings.end());
            p.automations.erase(std::remove_if(p.automations.begin(), p.automations.end(), [&](const Automation &a) { return a.id == id; }),
                                p.automations.end());
            mBindingsTouched = true;
            changed("auto.deleted", id);
            return true;
        }
        case K::AutoPointAdd:
        case K::AutoPointMove:
        case K::AutoPointDelete:
        case K::AutoPointShape:
        {
            Automation *a = p.automation(c.arg(0));
            if (!a) { err = "no automation `" + c.arg(0) + "`"; return false; }
            if (!c.has("at")) { err = "a point is named by its beat: --at <beats>"; return false; }
            double at = 0;
            if (!numberFlag(c, "at", at, err)) return false;
            if (at < 0) { err = "--at cannot be negative"; return false; }
            if (c.kind == K::AutoPointAdd)
            {
                if (!c.has("value")) { err = "auto point add needs --value"; return false; }
                AutoPoint pt;
                pt.at = toTick(at);
                if (!numberFlag(c, "value", pt.value, err)) return false;
                pt.value = std::min(std::max(pt.value, a->min), a->max);
                pt.shape = c.flag("shape", "linear");
                if (!shapeOk(pt.shape, err)) return false;
                if (AutoPoint *same = pointAt(*a, at)) *same = pt;
                else a->points.push_back(pt);
            }
            else
            {
                AutoPoint *pt = pointAt(*a, at);
                if (!pt) { err = a->id + " has no point at beat " + canonicalBeats(toTick(at)); return false; }
                if (c.kind == K::AutoPointDelete)
                    a->points.erase(a->points.begin() + (pt - a->points.data()));
                else if (c.kind == K::AutoPointShape)
                {
                    // one line per gesture: the shape and, for a bezier point, its handles (R-AUTO-10)
                    const std::string s = c.flag("shape", pt->shape);
                    if (!shapeOk(s, err)) return false;
                    AutoPoint next = *pt;
                    bool handles = false;
                    if (!handleFlags(c, next, handles, err)) return false;
                    if (handles && s != "bezier")
                    {
                        err = "--speed-*/--influence-* shape a BEZIER point's handles; the point at beat " + canonicalBeats(pt->at) + " is " + s +
                              " — add --shape bezier";
                        return false;
                    }
                    if (!c.has("shape") && !handles) { err = "auto point shape needs --shape (or a bezier point's --speed-*/--influence-*)"; return false; }
                    next.shape = s;
                    if (s != "bezier") // the file keeps handles only on a bezier point: so does the song
                    {
                        const AutoPoint flat;
                        next.speedIn = flat.speedIn;
                        next.inflIn = flat.inflIn;
                        next.speedOut = flat.speedOut;
                        next.inflOut = flat.inflOut;
                    }
                    *pt = next;
                }
                else
                {
                    if (!c.has("to") && !c.has("value")) { err = "auto point move needs --to and/or --value"; return false; }
                    double to = at, v = pt->value;
                    if (c.has("to") && !numberFlag(c, "to", to, err)) return false;
                    if (c.has("value") && !numberFlag(c, "value", v, err)) return false;
                    if (to < 0) { err = "--to cannot be negative"; return false; }
                    const AutoPoint *other = pointAt(*a, to);
                    if (other && other != pt) { err = a->id + " already has a point at beat " + canonicalBeats(toTick(to)); return false; }
                    pt->at = toTick(to);
                    pt->value = std::min(std::max(v, a->min), a->max);
                }
            }
            sortPoints(*a);
            mBindingsTouched = true;
            changed("auto.points", a->id);
            return true;
        }
        case K::BindClear:
        {
            const std::string address = c.arg(0);
            if (!p.binding(address)) { err = address + " has no formula"; return false; }
            p.bindings.erase(std::remove_if(p.bindings.begin(), p.bindings.end(), [&](const Binding &b) { return b.address == address; }),
                             p.bindings.end());
            mBindingsTouched = true;
            const auto dot = address.find('.');
            if (p.device(address.substr(0, dot))) mLastChanged[address.substr(0, dot)] = address.substr(dot + 1);
            changed("binding.cleared", address);
            return true;
        }
        default:
            return false;
        }
    }

    bool SolarisService::evalCommand(const Command &c, std::string &err)
    {
        if (!requireOpen(err)) return false;
        const std::string address = c.arg(0);
        AddressSpec spec;
        if (!describeAddress(mProject, address, spec, err)) return false;
        double at = 0;
        if (c.has("at") && !numberFlag(c, "at", at, err)) return false;
        const CompileResult cr = compile(mProject, [](const std::string &) { return std::shared_ptr<const engine::Pcm>(); });
        const engine::MixGraph &g = cr.graph;
        const long long sample = std::llround(at * g.clock.samplesPerBeat);
        std::vector<double> vars(engine::kClockSlots + g.curves.size() + g.binds.size(), 0.0), values(g.binds.size(), 0.0);
        for (size_t b = 0; b < g.binds.size(); ++b) values[b] = g.binds[b].own;
        engine::evaluateBinds(g.clock, g.curves, g.binds, sample, vars.data(), values.data());
        int bi = -1;
        for (size_t b = 0; b < cr.bindAddresses.size(); ++b)
            if (cr.bindAddresses[b] == address) bi = (int)b;
        const double value = bi >= 0 ? values[(size_t)bi] : spec.value;
        std::ostringstream o;
        o << address << " = " << canonicalNumber(value) << (spec.unit.empty() ? "" : " " + spec.unit) << " at beat " << canonicalBeats(toTick(at));
        if (!c.has("explain")) { mOutput = o.str() + "\n"; return true; }
        o << "\n";
        const Binding *b = mProject.binding(address);
        if (!b) o << "  its own value (no formula)\n";
        else
        {
            o << "  formula " << b->formula << (bi < 0 ? "  — INERT, its own value plays" : "") << "\n";
            if (bi < 0)
            {
                std::vector<std::string> problems;
                orderBindings(mProject, problems);
                for (const auto &x : problems)
                    if (x.compare(0, address.size() + 1, address + ":") == 0) o << "  why: " << x.substr(address.size() + 2) << "\n";
            }
            ParsedFormula f;
            std::string why;
            if (parseFormula(b->formula, f, why))
            {
                static const char *clock[] = {"beat", "bar", "bpm", "t"};
                bool used[engine::kClockSlots] = {false, false, false, false};
                for (const auto &op : f.expr.ops) // the clock words the PROGRAM reads, not letters in the text
                    if (op.kind == engine::ExprOp::Var && op.index >= 0 && op.index < engine::kClockSlots) used[op.index] = true;
                for (int k = 0; k < engine::kClockSlots; ++k)
                    if (used[k]) o << "  " << clock[k] << " = " << canonicalNumber(vars[(size_t)k]) << "\n";
                for (const auto &name : f.names)
                {
                    double v = 0;
                    std::string what;
                    const auto ci = std::find(cr.curveIds.begin(), cr.curveIds.end(), name);
                    const auto bj = std::find(cr.bindAddresses.begin(), cr.bindAddresses.end(), name);
                    if (ci != cr.curveIds.end())
                    {
                        v = vars[(size_t)(engine::kClockSlots + (ci - cr.curveIds.begin()))];
                        what = "automation \"" + mProject.automation(name)->name + "\"";
                    }
                    else if (bj != cr.bindAddresses.end())
                    {
                        v = values[(size_t)(bj - cr.bindAddresses.begin())];
                        what = "a link — its formula " + mProject.binding(name)->formula;
                    }
                    else
                    {
                        AddressSpec other;
                        describeAddress(mProject, name, other, why);
                        v = other.value;
                        what = "a link — its own value";
                    }
                    o << "  " << name << " = " << canonicalNumber(v) << "   (" << what << ")\n";
                }
            }
        }
        mOutput = o.str();
        return true;
    }
}
}
