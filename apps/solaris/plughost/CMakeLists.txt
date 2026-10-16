# solaris-plughost — other makers' VST3 plugins, out of Solaris's process (R-VST-6; discussion.md Round 9): one per
# plugin instance, and one per bundle read while scanning, so a module that crashes takes only this process with it.
# Built only where the DSP library found the VST3 SDK (its `vst3_hosting`): elsewhere Solaris says it cannot host.
if(TARGET vst3_hosting)
  set(_S ${VST3_SDK_ROOT})
  add_executable(solaris-plughost main.cpp ${_S}/public.sdk/source/vst/hosting/plugprovider.cpp ${_S}/public.sdk/source/common/memorystream.cpp)
  find_package(X11 REQUIRED) # N26: a plugin's own editor in a window of its own
  find_package(Threads REQUIRED)
  target_link_libraries(solaris-plughost PRIVATE vst3_hosting X11::X11 Threads::Threads)
  target_compile_options(solaris-plughost PRIVATE -Wno-deprecated-declarations)
else()
  message(STATUS "solaris-plughost: no VST3 SDK — Solaris will scan and host no plugins")
endif()
