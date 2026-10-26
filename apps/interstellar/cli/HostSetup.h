/*
 *  interstellar-cc — the host every face of the CLI builds the service with (R-SVC-1, R-SVC-4).
 *
 *  The codecs (FFmpeg decode / encode, the PNG writer), the text drawer and the machine's paths
 *  (recents, settings, presets — the SAME files the window uses, so a setting made from a shell, a
 *  browser or the window is one setting). `interstellar-cc` and `interstellar-cc ntwb serve` both call
 *  it, so the browser drives exactly the service the shell drives. Holds no behaviour.
 */
#pragma once
#include "InterstellarService.h"

namespace arstro
{
namespace interstellar_cli
{
    interstellar::InterstellarService::Host makeHost();
}
}
