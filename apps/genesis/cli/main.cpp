/*
 *  genesis-cc — the headless Genesis compiler.
 *
 *  Turns a `.genesis` document into a `.h`/`.cpp` pair, with no GUI and no display, so a
 *  build can regenerate components and CI can assert that the checked-in generated sources
 *  are current. It is also the only way to run `--verify` in a pipeline.
 */
#include "BaseCatalog.h"
#include "Document.h"
#include "Runtime.h"
#include "Verifier.h"
#include "codegen/CppEmitter.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
    int usage(int code)
    {
        std::cout <<
            "genesis-cc — compile a Genesis component to C++\n"
            "\n"
            "  genesis-cc <file.genesis> [-o <dir>] [--verify] [--check] [--print]\n"
            "  genesis-cc --new <Base> <Name> [-o <dir>]\n"
            "  genesis-cc --bases\n"
            "\n"
            "  -o <dir>    where to write <Name>.h/.cpp (default: the document's directory)\n"
            "  --check     validate only; exit 1 on any error (warnings are printed)\n"
            "  --print     write the generated code to stdout instead of to files\n"
            "  --verify    compile the generated class and diff its op stream against the\n"
            "              interpreter's; exit 1 on a mismatch\n"
            "  --stale     exit 1 if the files on disk differ from what would be generated\n"
            "  --trace <id>[.<field>]\n"
            "              run the preview and print the LIVE value of every field of <id> (or of\n"
            "              one field) frame by frame, with where each value came from. This is the\n"
            "              answer to \"the expression says one thing and the shape is elsewhere\":\n"
            "              binding / animating / releasing / owned tells you which.\n"
            "  --signal <name>@<ms>\n"
            "              fire a signal during a --trace; repeatable\n"
            "  --until <ms>  how long to trace (default 3000)\n"
            "  --new       write a starter document for <Base> and exit\n"
            "  --bases     list the authorable base classes and their signals\n";
        return code;
    }

    std::string dirOf(const std::string &path)
    {
        const size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
    }

    std::string readFile(const std::string &p)
    {
        std::ifstream f(p, std::ios::binary);
        if (!f) return std::string();
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    int reportDiagnostics(const genesis::Document &doc)
    {
        int errors = 0;
        for (const auto &d : doc.validate())
        {
            std::cerr << (d.isError() ? "error: " : "warning: ") << d.where << ": " << d.message << "\n";
            if (d.isError()) ++errors;
        }
        return errors;
    }
}

namespace
{
    /** Print the live value of a shape's fields frame by frame. An expression only says what a
     *  field SHOULD be; when a shape is not where its binding claims, the question is what the
     *  value is and who last wrote it — so each column carries both. */
    int trace(const genesis::Document &doc, const std::string &target,
              const std::vector<std::pair<std::string, double>> &signals, double untilMs)
    {
        std::string shapeId = target, only;
        const size_t dot = target.find('.');
        if (dot != std::string::npos)
        {
            shapeId = target.substr(0, dot);
            only = target.substr(dot + 1);
        }
        const genesis::Shape *s = doc.findShape(shapeId);
        if (!s)
        {
            std::cerr << "error: no shape '" << shapeId << "' in this document\n";
            return 1;
        }
        std::vector<const genesis::FieldDef *> cols;
        for (const auto *fd : genesis::fieldsFor(s->kind))
        {
            if (fd->type != genesis::FieldType::Number) continue;
            if (!only.empty() && only != fd->name) continue;
            cols.push_back(fd);
        }
        if (cols.empty())
        {
            std::cerr << "error: '" << target << "' names no numeric field\n";
            return 1;
        }

        genesis::Runtime rt;
        std::string err;
        if (!rt.build(doc, &err))
        {
            std::cerr << "error: " << err << "\n";
            return 1;
        }
        rt.setSize(doc.designW, doc.designH);

        std::cout << shapeId << " — value and where it came from, at " << doc.designW << "x"
                  << doc.designH << "\n";
        for (const auto *fd : cols)
            std::cout << "  " << fd->name << " = " << s->effectiveField(fd->name) << "\n";
        std::cout << "\n" << std::setw(8) << "ms";
        for (const auto *fd : cols)
            std::cout << std::setw(13) << fd->name;
        std::cout << "\n";

        size_t next = 0;
        std::vector<std::pair<std::string, double>> fires = signals;
        std::sort(fires.begin(), fires.end(),
                  [](const std::pair<std::string, double> &a, const std::pair<std::string, double> &b) {
                      return a.second < b.second;
                  });
        for (double t = 0.0; t <= untilMs; t += 16.0)
        {
            rt.advance(t);
            while (next < fires.size() && fires[next].second <= t)
            {
                rt.fire(fires[next].first);
                std::cout << std::setw(8) << (long long)t << "  <- fire " << fires[next].first << "\n";
                ++next;
                rt.advance(t);
            }
            // Every 5th frame, so a 3s trace is readable without losing the shape of the motion.
            if ((long long)(t / 16.0) % 5 != 0) continue;
            std::cout << std::setw(8) << (long long)t;
            for (const auto *fd : cols)
            {
                double v = 0.0;
                genesis::Runtime::Source from = genesis::Runtime::Source::Binding;
                if (!rt.fieldValue(shapeId, fd->name, v, &from))
                {
                    std::cout << std::setw(13) << "-";
                    continue;
                }
                char buf[32];
                std::snprintf(buf, sizeof buf, "%.4g%c", v, genesis::Runtime::sourceName(from)[0]);
                std::cout << std::setw(13) << buf;
            }
            std::cout << "\n";
        }
        std::cout << "\n(suffix: b = its binding, a = animating, r = being released back to its "
                     "binding, t = resting on\n a completed track's target, still re-evaluated "
                     "every frame, o = owned by motion and standing still)\n";
        return 0;
    }
}

int main(int argc, char **argv)
{
    std::string input, outDir, traceTarget;
    bool check = false, print = false, doVerify = false, stale = false;
    std::vector<std::pair<std::string, double>> traceSignals;
    double traceUntil = 3000.0;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") return usage(0);
        if (a == "--bases")
        {
            for (const auto &b : genesis::bases())
            {
                std::cout << b.name << "  (" << b.cppClass << ")\n    " << b.summary
                          << "\n    draw: " << b.mustDraw << "\n    signals:";
                for (const auto &s : b.signals)
                    std::cout << " " << s.name << (s.expected ? "*" : "");
                std::cout << "\n    reads:";
                for (const auto &r : b.reads)
                    std::cout << " base." << r.name;
                std::cout << "\n\n";
            }
            std::cout << "(* = the export checklist warns when nothing reacts to it)\n";
            return 0;
        }
        if (a == "--new")
        {
            if (i + 2 >= argc) return usage(2);
            const std::string base = argv[++i], name = argv[++i];
            if (!genesis::findBase(base))
            {
                std::cerr << "error: unknown base '" << base << "' (try --bases)\n";
                return 1;
            }
            std::string dir = ".";
            for (int k = i + 1; k < argc; ++k)
                if (std::string(argv[k]) == "-o" && k + 1 < argc) dir = argv[k + 1];
            const genesis::Document d = genesis::Document::starter(base, name);
            std::string err;
            const std::string path = dir + "/" + name + ".genesis";
            if (!d.save(path, &err))
            {
                std::cerr << "error: " << err << "\n";
                return 1;
            }
            std::cout << "wrote " << path << "\n";
            return 0;
        }
        if (a == "-o") { if (i + 1 >= argc) return usage(2); outDir = argv[++i]; continue; }
        if (a == "--check") { check = true; continue; }
        if (a == "--print") { print = true; continue; }
        if (a == "--verify") { doVerify = true; continue; }
        if (a == "--stale") { stale = true; continue; }
        if (a == "--trace") { if (i + 1 >= argc) return usage(2); traceTarget = argv[++i]; continue; }
        if (a == "--until") { if (i + 1 >= argc) return usage(2); traceUntil = std::atof(argv[++i]); continue; }
        if (a == "--signal")
        {
            if (i + 1 >= argc) return usage(2);
            const std::string spec = argv[++i];
            const size_t at = spec.find('@');
            traceSignals.emplace_back(spec.substr(0, at),
                                      at == std::string::npos ? 0.0 : std::atof(spec.c_str() + at + 1));
            continue;
        }
        if (!a.empty() && a[0] == '-') { std::cerr << "error: unknown option " << a << "\n"; return usage(2); }
        input = a;
    }
    if (input.empty()) return usage(2);

    std::string err;
    const genesis::Document doc = genesis::Document::load(input, &err);
    if (!err.empty())
    {
        std::cerr << "error: " << err << "\n";
        return 1;
    }
    const int errors = reportDiagnostics(doc);
    if (check)
    {
        std::cout << (errors ? "not exportable\n" : "ok\n");
        return errors ? 1 : 0;
    }
    if (errors)
    {
        std::cerr << "error: " << errors << " error(s); nothing generated\n";
        return 1;
    }

    if (!traceTarget.empty())
        return trace(doc, traceTarget, traceSignals, traceUntil);

    const genesis::EmittedCode code = genesis::emitCpp(doc);
    if (!code.ok())
    {
        std::cerr << "error: " << code.error << "\n";
        return 1;
    }
    if (print)
    {
        std::cout << code.header << "\n// ─────────────────────────\n\n" << code.source;
        return 0;
    }

    const std::string dir = outDir.empty() ? dirOf(input) : outDir;
    if (stale)
    {
        const bool same = readFile(dir + "/" + code.headerName) == code.header &&
                          readFile(dir + "/" + code.sourceName) == code.source;
        std::cout << (same ? "up to date\n" : "STALE: regenerate with genesis-cc\n");
        if (!same) return 1;
    }
    else
    {
        if (!genesis::writeEmitted(code, dir, &err))
        {
            std::cerr << "error: " << err << "\n";
            return 1;
        }
        std::cout << "wrote " << dir << "/" << code.headerName << " and " << code.sourceName << "\n";
    }

    if (doVerify)
    {
        const genesis::VerifyResult v =
            genesis::verify(doc, genesis::VerifyPlan::defaultFor(doc), genesis::VerifyConfig::defaults());
        std::cout << v.summary() << "\n";
        if (!v.available)
        {
            std::cerr << "error: verification could not run — treat this as UNVERIFIED, not as a pass\n";
            return 1;
        }
        for (const auto &d : v.differences)
            std::cout << "  at " << d.atMs << "ms op " << d.opIndex << " " << d.field
                      << ": preview=" << d.preview << " compiled=" << d.compiled << "\n";
        if (!v.matched)
        {
            if (!v.compilerOutput.empty())
                std::cerr << v.compilerOutput << "\n";
            return 1;
        }
    }
    return 0;
}
