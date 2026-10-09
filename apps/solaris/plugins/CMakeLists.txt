# Solaris's instruments as VST3 plugins: their OWN editor, in the Arstro look (R-VST-7).
#
# The plugins themselves are the DSP repo's (core/DigitalSignalProcessing/apps/vst3: the registry type
# wrapped, no copy of its sound). Their editor needs Artboard, cosmo's widgets and Solaris's DevicePanel,
# which that repo does not carry — so the umbrella compiles it INTO them here, through the one seam the
# DSP repo leaves (its Editor.h: `createView` → `createEditor` when ARSTRO_VST3_EDITOR is defined). Built
# from the DSP repo alone the plugins offer no editor and a host draws its generic parameter view.
if(NOT TARGET arstro_vst3_synth OR NOT TARGET solaris_app)
  message(STATUS "solaris plugins: no editor (needs the VST3 plugins, ARSTRO_DSP_VST3=ON, and solaris_app)")
  return()
endif()
find_package(X11)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SL_CAIRO_XLIB IMPORTED_TARGET cairo-xlib)
if(NOT X11_FOUND OR NOT SL_CAIRO_XLIB_FOUND)
  message(STATUS "solaris plugins: no editor (needs X11 and cairo-xlib)")
  return()
endif()
set(SL_VST3_DIR ${CMAKE_SOURCE_DIR}/core/DigitalSignalProcessing/apps/vst3)

# the editor: platform-free (tested headless); linked into a plugin MODULE, so everything under it is PIC
add_library(solaris_instrument_editor STATIC
  InstrumentEditor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/service/RegistryModel.cpp)
target_include_directories(solaris_instrument_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../core/service)
target_link_libraries(solaris_instrument_editor PUBLIC solaris_app arstro_dsp)
set_target_properties(solaris_instrument_editor solaris_app artboard_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

foreach(plugin arstro_vst3_synth arstro_vst3_drums)
  target_sources(${plugin} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/X11View.cpp)
  target_compile_definitions(${plugin} PRIVATE ARSTRO_VST3_EDITOR=1)
  target_include_directories(${plugin} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${SL_VST3_DIR} ${X11_INCLUDE_DIR})
  target_link_libraries(${plugin} PRIVATE solaris_instrument_editor PkgConfig::SL_CAIRO_XLIB ${X11_LIBRARIES})
endforeach()

# R-VST-8: the editor over the REAL controller, headless — a drag is the host's edit with the shared
# mapping's value; a host change eases in; the frames written for a look
add_executable(solaris_plugin_editor_tests tests/editorTests.cpp ${SL_VST3_DIR}/RegistryPlugin.cpp)
target_include_directories(solaris_plugin_editor_tests PRIVATE ${SL_VST3_DIR})
target_link_libraries(solaris_plugin_editor_tests PRIVATE solaris_instrument_editor vst3_sdk)
add_test(NAME solaris_plugin_editor COMMAND solaris_plugin_editor_tests)
