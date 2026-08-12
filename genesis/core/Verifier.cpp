#include "Verifier.h"
#include "BaseCatalog.h"
#include "Runtime.h"
#include "codegen/CppEmitter.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <algorithm>

#ifdef GENESIS_ARTBOARD_INCLUDE
#define GENESIS_STR2(x) #x
#define GENESIS_STR(x) GENESIS_STR2(x)
#endif

namespace genesis
{
    namespace
    {
        /** The serializer, as source, so the generated harness carries an IDENTICAL copy.
         *  One text, two compilations — the format can never drift between the sides. */
        const char *kSerializerSource = R"SRC(
static const char *opName(artboard::DrawOp::Kind k)
{
    switch (k)
    {
    case artboard::DrawOp::Kind::Save: return "save";
    case artboard::DrawOp::Kind::Restore: return "restore";
    case artboard::DrawOp::Kind::SetTransform: return "xform";
    case artboard::DrawOp::Kind::ClipRect: return "clipRect";
    case artboard::DrawOp::Kind::ClipPath: return "clipPath";
    case artboard::DrawOp::Kind::PushLayer: return "pushLayer";
    case artboard::DrawOp::Kind::PopLayer: return "popLayer";
    case artboard::DrawOp::Kind::SetFill: return "fill";
    case artboard::DrawOp::Kind::SetRadialFill: return "radial";
    case artboard::DrawOp::Kind::SetLinearFill: return "linear";
    case artboard::DrawOp::Kind::SetStroke: return "stroke";
    case artboard::DrawOp::Kind::BeginPath: return "begin";
    case artboard::DrawOp::Kind::MoveTo: return "moveTo";
    case artboard::DrawOp::Kind::LineTo: return "lineTo";
    case artboard::DrawOp::Kind::QuadTo: return "quadTo";
    case artboard::DrawOp::Kind::CubicTo: return "cubicTo";
    case artboard::DrawOp::Kind::ClosePath: return "close";
    case artboard::DrawOp::Kind::FillPath: return "fillPath";
    case artboard::DrawOp::Kind::StrokePath: return "strokePath";
    case artboard::DrawOp::Kind::DrawText: return "text";
    case artboard::DrawOp::Kind::RegisterImage: return "regImage";
    case artboard::DrawOp::Kind::UpdateImage: return "updImage";
    case artboard::DrawOp::Kind::DrawImage: return "drawImage";
    case artboard::DrawOp::Kind::ReleaseImage: return "relImage";
    }
    return "?";
}

static std::string genesisOpsToText(const std::vector<artboard::DrawOp> &ops)
{
    std::ostringstream o;
    char buf[512];
    for (const auto &op : ops)
    {
        std::snprintf(buf, sizeof buf,
                      "%s %.10g %.10g %.10g %.10g %.10g %.10g | %.10g %.10g %.10g %.10g | "
                      "%.10g %.10g %.10g %.10g | %.10g | %.10g %.10g %.10g %.10g %.10g %.10g | %s",
                      opName(op.kind), op.args[0], op.args[1], op.args[2], op.args[3], op.args[4], op.args[5],
                      op.color.r, op.color.g, op.color.b, op.color.a,
                      op.color2.r, op.color2.g, op.color2.b, op.color2.a, op.width,
                      op.transform.a, op.transform.b, op.transform.c, op.transform.d,
                      op.transform.e, op.transform.f, op.text.c_str());
        o << buf << "\n";
    }
    return o.str();
}
)SRC";

        bool writeFile(const std::string &path, const std::string &text, std::string *error)
        {
            std::ofstream f(path, std::ios::binary);
            if (!f)
            {
                if (error) *error = "cannot write " + path;
                return false;
            }
            f << text;
            return true;
        }

        std::string readFile(const std::string &path)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f) return std::string();
            std::ostringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }

        bool exists(const std::string &path)
        {
            struct stat st;
            return ::stat(path.c_str(), &st) == 0;
        }

        std::string shellQuote(const std::string &s) { return "'" + s + "'"; }

        int runCommand(const std::string &cmd, std::string &output)
        {
            const std::string full = cmd + " 2>&1";
            FILE *p = ::popen(full.c_str(), "r");
            if (!p) return -1;
            char buf[512];
            while (std::fgets(buf, sizeof buf, p))
                output += buf;
            const int rc = ::pclose(p);
            return rc == -1 ? -1 : (rc / 256);
        }

        std::string numText(double d)
        {
            char buf[40];
            std::snprintf(buf, sizeof buf, "%.10g", d);
            return buf;
        }

        /** Statement that applies one event to the component, for the harness. */
        std::string eventCall(const VerifyEvent &e, const std::string &base)
        {
            if (e.action == "start") return "c.start(t);";
            if (e.action == "stop") return "c.stop(t);";
            if (e.action == "progress") return "c.setValue(" + numText(e.arg) + ");";
            if (e.action == "indeterminate") return "c.setIndeterminate(" + std::string(e.arg != 0 ? "true" : "false") + ");";
            if (e.action == "press")
                return "c.onGesture({artboard::Gesture::Type::Down, {c.width.value()*0.5, c.height.value()*0.5}, {0,0}, artboard::PointerButton::Left});";
            if (e.action == "release")
                return "c.onGesture({artboard::Gesture::Type::Click, {c.width.value()*0.5, c.height.value()*0.5}, {0,0}, artboard::PointerButton::Left});";
            if (e.action == "check")
                return "c.onGesture({artboard::Gesture::Type::Click, {c.width.value()*0.5, c.height.value()*0.5}, {0,0}, artboard::PointerButton::Left});";
            if (e.action == "slider")
                return "c.onGesture({artboard::Gesture::Type::DragStart, {c.width.value()*" + numText(e.arg) +
                       ", c.height.value()*0.5}, {0,0}, artboard::PointerButton::Left});";
            if (e.action == "hover")
                return std::string("artboard::Segment::setHovered(") + (e.arg != 0 ? "&c" : "nullptr") + ");";
            if (e.action == "resize")
                return "c.width.set(" + numText(e.arg) + "); c.height.set(" + numText(e.arg) + ");";
            (void)base;
            return "";
        }
    }

    std::string opsToText(const std::vector<artboard::DrawOp> &ops)
    {
        // Kept byte-identical to kSerializerSource's genesisOpsToText: the harness compiles
        // that copy, this one runs in-process, and the two outputs are compared directly.
        std::ostringstream o;
        char buf[512];
        auto name = [](artboard::DrawOp::Kind k) -> const char * {
            switch (k)
            {
            case artboard::DrawOp::Kind::Save: return "save";
            case artboard::DrawOp::Kind::Restore: return "restore";
            case artboard::DrawOp::Kind::SetTransform: return "xform";
            case artboard::DrawOp::Kind::ClipRect: return "clipRect";
            case artboard::DrawOp::Kind::ClipPath: return "clipPath";
            case artboard::DrawOp::Kind::PushLayer: return "pushLayer";
            case artboard::DrawOp::Kind::PopLayer: return "popLayer";
            case artboard::DrawOp::Kind::SetFill: return "fill";
            case artboard::DrawOp::Kind::SetRadialFill: return "radial";
            case artboard::DrawOp::Kind::SetLinearFill: return "linear";
            case artboard::DrawOp::Kind::SetStroke: return "stroke";
            case artboard::DrawOp::Kind::BeginPath: return "begin";
            case artboard::DrawOp::Kind::MoveTo: return "moveTo";
            case artboard::DrawOp::Kind::LineTo: return "lineTo";
            case artboard::DrawOp::Kind::QuadTo: return "quadTo";
            case artboard::DrawOp::Kind::CubicTo: return "cubicTo";
            case artboard::DrawOp::Kind::ClosePath: return "close";
            case artboard::DrawOp::Kind::FillPath: return "fillPath";
            case artboard::DrawOp::Kind::StrokePath: return "strokePath";
            case artboard::DrawOp::Kind::DrawText: return "text";
            case artboard::DrawOp::Kind::RegisterImage: return "regImage";
            case artboard::DrawOp::Kind::UpdateImage: return "updImage";
            case artboard::DrawOp::Kind::DrawImage: return "drawImage";
            case artboard::DrawOp::Kind::ReleaseImage: return "relImage";
            }
            return "?";
        };
        for (const auto &op : ops)
        {
            std::snprintf(buf, sizeof buf,
                          "%s %.10g %.10g %.10g %.10g %.10g %.10g | %.10g %.10g %.10g %.10g | "
                          "%.10g %.10g %.10g %.10g | %.10g | %.10g %.10g %.10g %.10g %.10g %.10g | %s",
                          name(op.kind), op.args[0], op.args[1], op.args[2], op.args[3], op.args[4], op.args[5],
                          op.color.r, op.color.g, op.color.b, op.color.a,
                          op.color2.r, op.color2.g, op.color2.b, op.color2.a, op.width,
                          op.transform.a, op.transform.b, op.transform.c, op.transform.d,
                          op.transform.e, op.transform.f, op.text.c_str());
            o << buf << "\n";
        }
        return o.str();
    }

    VerifyPlan VerifyPlan::defaultFor(const Document &doc)
    {
        VerifyPlan p;
        p.width = doc.designW;
        p.height = doc.designH;
        p.sampleMs = {0, 60, 150, 300, 600, 900, 1200, 1500, 1800};
        if (doc.base == "VisualLoop")
        {
            p.events.push_back({0, "start", 0, ""});
            p.events.push_back({1300, "stop", 0, ""});
        }
        else if (doc.base == "ProgressIndicator")
        {
            p.events.push_back({0, "progress", 0.0, ""});
            p.events.push_back({200, "progress", 0.4, ""});
            p.events.push_back({800, "progress", 1.0, ""});
            p.events.push_back({1300, "indeterminate", 1, ""});
        }
        else if (doc.base == "Button")
        {
            p.events.push_back({100, "hover", 1, ""});
            p.events.push_back({300, "press", 0, ""});
            p.events.push_back({700, "release", 0, ""});
            p.events.push_back({1400, "hover", 0, ""});
        }
        else if (doc.base == "Checkbox")
        {
            p.events.push_back({100, "hover", 1, ""});
            p.events.push_back({400, "check", 0, ""});
            p.events.push_back({1200, "check", 0, ""});
        }
        // Every plan resizes: a component that only works at its design size fails R4.
        p.events.push_back({1600, "resize", doc.designW * 1.5, ""});
        return p;
    }

    VerifyConfig VerifyConfig::defaults()
    {
        VerifyConfig c;
#ifdef GENESIS_ARTBOARD_INCLUDE
        c.artboardInclude = GENESIS_STR(GENESIS_ARTBOARD_INCLUDE);
        c.artboardSrc = GENESIS_STR(GENESIS_ARTBOARD_SRC);
        c.artboardLibDir = GENESIS_STR(GENESIS_ARTBOARD_LIBDIR);
#endif
        const char *tmp = std::getenv("TMPDIR");
        c.workDir = std::string(tmp && *tmp ? tmp : "/tmp") + "/genesis-verify";
        return c;
    }

    bool VerifyConfig::usable(std::string *why) const
    {
        if (artboardInclude.empty() || artboardLibDir.empty())
        {
            if (why) *why = "Genesis was built without the Artboard paths needed to compile a component";
            return false;
        }
        if (!exists(artboardLibDir + "/libartboard_core.a"))
        {
            if (why) *why = "libartboard_core.a not found in " + artboardLibDir + " — build Artboard first";
            return false;
        }
        std::string out;
        if (runCommand(compiler + " --version", out) != 0)
        {
            if (why) *why = "no C++ compiler ('" + compiler + "') on PATH";
            return false;
        }
        if (why) why->clear();
        return true;
    }

    std::string VerifyResult::summary() const
    {
        if (!available)
            return "verify unavailable — " + error;
        if (matched)
            return "verified: " + std::to_string(comparedFrames) + " frames, " +
                   std::to_string(comparedOps) + " ops, preview == compiled";
        if (!error.empty())
            return "verify failed — " + error;
        return "MISMATCH: " + std::to_string(differences.size()) + " difference(s) across " +
               std::to_string(comparedFrames) + " frames";
    }

    namespace
    {
        std::string buildHarness(const Document &doc, const VerifyPlan &plan)
        {
            std::ostringstream o;
            o << "// Genesis verification harness — generated, transient.\n";
            o << "#include \"" << doc.name << ".h\"\n";
            o << "#include <cstdio>\n#include <sstream>\n#include <string>\n#include <vector>\n\n";
            o << kSerializerSource << "\n";
            o << "int main()\n{\n";
            o << "    " << doc.nameSpace << "::" << doc.name << " c;\n";
            o << "    c.width.set(" << numText(plan.width > 0 ? plan.width : doc.designW) << ");\n";
            o << "    c.height.set(" << numText(plan.height > 0 ? plan.height : doc.designH) << ");\n";
            o << "    artboard::RecordingTarget target;\n";

            // One merged, ordered timeline of events and samples, so both sides see the
            // component in exactly the same state at exactly the same frames.
            struct Entry { double t; bool isSample; std::string stmt; };
            std::vector<Entry> timeline;
            for (const auto &e : plan.events)
                timeline.push_back({e.atMs, false, eventCall(e, doc.base)});
            for (double s : plan.sampleMs)
                timeline.push_back({s, true, ""});
            std::stable_sort(timeline.begin(), timeline.end(),
                             [](const Entry &a, const Entry &b) { return a.t < b.t; });

            for (const auto &e : timeline)
            {
                o << "    {\n        const double t = " << numText(e.t) << ";\n";
                o << "        c.advance(t);\n";
                if (e.isSample)
                {
                    o << "        target.clear();\n        c.render(target);\n";
                    o << "        std::printf(\"#frame %.10g\\n%s\", t, genesisOpsToText(target.ops()).c_str());\n";
                }
                else if (!e.stmt.empty())
                    o << "        " << e.stmt << "\n";
                o << "        (void)t;\n    }\n";
            }
            o << "    return 0;\n}\n";
            return o.str();
        }

        /** Run the interpreter through the same timeline and produce the same text. */
        std::string runPreview(const Document &doc, const VerifyPlan &plan, std::string *error)
        {
            Runtime rt;
            if (!rt.build(doc, error))
                return std::string();
            rt.setSize(plan.width > 0 ? plan.width : doc.designW,
                       plan.height > 0 ? plan.height : doc.designH);

            struct Entry { double t; bool isSample; const VerifyEvent *ev; };
            std::vector<Entry> timeline;
            for (const auto &e : plan.events)
                timeline.push_back({e.atMs, false, &e});
            for (double s : plan.sampleMs)
                timeline.push_back({s, true, nullptr});
            std::stable_sort(timeline.begin(), timeline.end(),
                             [](const Entry &a, const Entry &b) { return a.t < b.t; });

            std::ostringstream out;
            artboard::RecordingTarget target;
            for (const auto &e : timeline)
            {
                rt.advance(e.t);
                if (e.isSample)
                {
                    target.clear();
                    rt.root()->render(target);
                    char head[64];
                    std::snprintf(head, sizeof head, "#frame %.10g\n", e.t);
                    out << head << opsToText(target.ops());
                    continue;
                }
                const VerifyEvent &ev = *e.ev;
                if (ev.action == "start") rt.loopStart();
                else if (ev.action == "stop") rt.loopStop();
                else if (ev.action == "progress") rt.setProgress(ev.arg);
                else if (ev.action == "indeterminate") rt.setIndeterminate(ev.arg != 0);
                else if (ev.action == "press") rt.setPressed(true);
                else if (ev.action == "release") rt.setPressed(false);
                else if (ev.action == "check") rt.setChecked(!(ev.arg != 0));
                else if (ev.action == "slider") rt.setSliderValue(ev.arg);
                else if (ev.action == "hover") rt.setHovered(ev.arg != 0);
                else if (ev.action == "resize") rt.setSize(ev.arg, ev.arg);
                else if (ev.action == "signal") rt.fire(ev.name);
            }
            return out.str();
        }

        std::vector<std::string> splitLines(const std::string &s)
        {
            std::vector<std::string> out;
            std::istringstream in(s);
            std::string line;
            while (std::getline(in, line))
                out.push_back(line);
            return out;
        }

        /** Field-wise numeric comparison, so a last-bit difference is not reported as a
         *  semantic one but any real divergence is. */
        bool sameOpLine(const std::string &a, const std::string &b, std::string &field,
                        std::string &pa, std::string &pb)
        {
            if (a == b) return true;
            std::istringstream ia(a), ib(b);
            std::string ta, tb;
            int index = 0;
            while (ia >> ta)
            {
                if (!(ib >> tb)) { field = "length"; pa = a; pb = b; return false; }
                ++index;
                if (ta == tb) continue;
                char *ea = nullptr, *eb = nullptr;
                const double va = std::strtod(ta.c_str(), &ea);
                const double vb = std::strtod(tb.c_str(), &eb);
                const bool bothNumeric = ea && *ea == 0 && eb && *eb == 0 && !ta.empty() && !tb.empty();
                if (bothNumeric)
                {
                    const double scale = std::max(1.0, std::max(std::fabs(va), std::fabs(vb)));
                    if (std::fabs(va - vb) <= 1e-9 * scale)
                        continue;
                }
                field = "token " + std::to_string(index);
                pa = ta;
                pb = tb;
                return false;
            }
            if (ib >> tb) { field = "length"; pa = a; pb = b; return false; }
            return true;
        }
    }

    VerifyResult verify(const Document &doc, const VerifyPlan &plan, const VerifyConfig &cfg)
    {
        VerifyResult r;
        std::string why;
        if (!cfg.usable(&why))
        {
            r.error = why;
            return r;
        }
        const EmittedCode code = emitCpp(doc);
        if (!code.ok())
        {
            r.available = true;
            r.error = "cannot emit: " + code.error;
            return r;
        }

        const std::string dir = cfg.workDir;
        ::mkdir(dir.c_str(), 0755);
        std::string ioError;
        if (!writeFile(dir + "/" + code.headerName, code.header, &ioError) ||
            !writeFile(dir + "/" + code.sourceName, code.source, &ioError) ||
            !writeFile(dir + "/harness.cpp", buildHarness(doc, plan), &ioError))
        {
            r.available = true;
            r.error = ioError;
            return r;
        }

        // -O0 with contraction off: the compiled arithmetic must follow the SAME order the
        // interpreter uses, or a fused multiply-add would show up as a false mismatch.
        std::ostringstream cmd;
        cmd << cfg.compiler << " -std=c++17 -O0 -ffp-contract=off"
            << " -I" << shellQuote(dir)
            << " -I" << shellQuote(cfg.artboardInclude)
            << " -I" << shellQuote(cfg.artboardSrc)
            << " -o " << shellQuote(dir + "/harness")
            << " " << shellQuote(dir + "/harness.cpp")
            << " " << shellQuote(dir + "/" + code.sourceName)
            << " -L" << shellQuote(cfg.artboardLibDir) << " -lartboard_core";
        std::string compileOut;
        const int rc = runCommand(cmd.str(), compileOut);
        r.compilerOutput = compileOut;
        if (rc != 0)
        {
            r.available = true;
            r.error = "the generated code did not compile";
            return r;
        }

        std::string compiledText;
        if (runCommand(shellQuote(dir + "/harness"), compiledText) != 0)
        {
            r.available = true;
            r.error = "the compiled component crashed while being driven";
            r.compilerOutput = compiledText;
            return r;
        }

        std::string previewError;
        const std::string previewText = runPreview(doc, plan, &previewError);
        if (!previewError.empty())
        {
            r.available = true;
            r.error = "preview failed: " + previewError;
            return r;
        }

        const std::vector<std::string> a = splitLines(previewText);
        const std::vector<std::string> b = splitLines(compiledText);
        r.available = true;
        double frame = 0.0;
        int opIndex = 0;
        const size_t n = std::max(a.size(), b.size());
        for (size_t i = 0; i < n; ++i)
        {
            const std::string la = i < a.size() ? a[i] : std::string();
            const std::string lb = i < b.size() ? b[i] : std::string();
            if (!la.empty() && la[0] == '#')
            {
                ++r.comparedFrames;
                opIndex = 0;
                std::sscanf(la.c_str(), "#frame %lf", &frame);
                if (la != lb)
                    r.differences.push_back({frame, -1, "frame marker", la, lb});
                continue;
            }
            ++r.comparedOps;
            std::string field, pa, pb;
            if (!sameOpLine(la, lb, field, pa, pb))
                r.differences.push_back({frame, opIndex, field, pa, pb});
            ++opIndex;
            if (r.differences.size() >= 40)
                break;   // enough to diagnose; the rest would be noise
        }
        r.matched = r.differences.empty();
        return r;
    }
}
