// R-VST-6's test of a scan: a "plugin" whose module aborts the moment it is loaded — the helper reading it must die,
// and the scan go on without it.
#include <cstdlib>
__attribute__((constructor)) static void boom() { std::abort(); }
