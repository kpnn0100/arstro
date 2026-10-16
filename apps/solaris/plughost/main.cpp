/*
 *  solaris-plughost — a VST3 plugin, out of Solaris's process (R-VST-6; docs/discussion.md Round 9).
 *
 *  `solaris-plughost --scan <bundle>`: the bundle's audio classes, a line each on stdout —
 *      class <TAB> <uid> <TAB> instrument|effect <TAB> <name> <TAB> <vendor>
 *  and exit 0; a bundle that cannot be read: `error <TAB> <why>`, exit 1. Whatever the module does while it is loaded
 *  (crash, hang) happens HERE — the host reads the lines, the exit and the signal, and goes on to the next bundle.
 */
#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include <cstdio>
#include <cstring>
#include <string>

using namespace Steinberg;

namespace
{
    std::string clean(std::string s)
    {
        for (auto &ch : s)
            if (ch == '\t' || ch == '\n' || ch == '\r') ch = ' ';
        return s;
    }

    int scan(const char *bundle)
    {
        std::string why;
        auto module = VST3::Hosting::Module::create(bundle, why);
        if (!module)
        {
            std::printf("error\t%s\n", clean(why.empty() ? "not a VST3 module" : why).c_str());
            return 1;
        }
        int n = 0;
        for (const auto &ci : module->getFactory().classInfos())
        {
            if (ci.category() != kVstAudioEffectClass) continue;
            const std::string sub = ci.subCategoriesString();
            const bool instrument = sub.find("Instrument") != std::string::npos;
            std::printf("class\t%s\t%s\t%s\t%s\n", ci.ID().toString().c_str(), instrument ? "instrument" : "effect", clean(ci.name()).c_str(),
                        clean(ci.vendor()).c_str());
            ++n;
        }
        if (n == 0) { std::printf("error\tno audio processor in it\n"); return 1; }
        return 0;
    }
}

int main(int argc, char **argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--scan") == 0) return scan(argv[2]);
    std::fprintf(stderr, "usage: solaris-plughost --scan <bundle.vst3>\n");
    return 2;
}
