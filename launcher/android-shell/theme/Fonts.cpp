/*
 *  arstro-android-shell — Fonts implementation (M2.2). See Fonts.h.
 */
#include "theme/Fonts.h"

#include <fontconfig/fontconfig.h>
#include <cstdio>
#include <string>

// Baked by CMake to the android-shell source dir, so assets/fonts is found independent of the
// build dir or CWD (mirrors cosmo's COSMO_SOURCE_DIR).
#ifndef ANDROID_SHELL_SOURCE_DIR
#define ANDROID_SHELL_SOURCE_DIR "."
#endif

namespace arstro
{
namespace androidshell
{
    namespace
    {
        void reg(const std::string &path)
        {
            if (!FcConfigAppFontAddFile(FcConfigGetCurrent(), (const FcChar8 *)path.c_str()))
                std::fprintf(stderr,
                             "arstro-android-shell: font not registered (missing? add the TTF): %s\n",
                             path.c_str());
        }
    }

    void registerBundledFonts()
    {
        const std::string dir = std::string(ANDROID_SHELL_SOURCE_DIR) + "/assets/fonts";
        // Roboto (Apache-2.0). Static weights are distinct family names (FR-22): the Medium file
        // registers as its own family "Roboto Medium". Roboto Flex covers the large clock.
        reg(dir + "/Roboto/Roboto-Regular.ttf");
        reg(dir + "/Roboto/Roboto-Medium.ttf");
        reg(dir + "/RobotoFlex/RobotoFlex-Regular.ttf");
    }

} // namespace androidshell
} // namespace arstro
