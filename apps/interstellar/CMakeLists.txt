# Interstellar — a video colour tool with an editor attached. See REQUIREMENTS.md.
#
# Gated on cosmo_core because the RACK IS a hosted CosmoService: without Cosmo there is no colour
# authority, and an Interstellar that cannot grade is not a degraded Interstellar — it is the thing
# two previous builds shipped by mistake.
if(TARGET cosmo_core)
  add_subdirectory(model)    # the .isp + versions: no Cosmo, no pixels
  add_subdirectory(render)   # the frame path: no project model
  add_subdirectory(core)     # the service: rack + model + render
  add_subdirectory(host)
  add_subdirectory(cli)
  if(TARGET artboard_core)
    add_subdirectory(app)    # the UI over AppHooks — links no service at all
  endif()
else()
  message(STATUS "interstellar: skipped (needs cosmo_core — the rack hosts a real CosmoService)")
endif()

# The GTK window: the app's hooks bound to the service. The ONLY OS code in the GUI.
if(TARGET interstellar_app AND TARGET interstellar_host)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(INTERSTELLAR_GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
  add_executable(interstellar ${CMAKE_CURRENT_SOURCE_DIR}/linux_main.cpp)
  target_link_libraries(interstellar PRIVATE interstellar_app interstellar_host PkgConfig::INTERSTELLAR_GTK3)
endif()

# The real app bound to the real service, rendered headlessly — the GTK host's wiring, tested.
if(TARGET interstellar_app AND TARGET interstellar_host)
  add_executable(interstellar_live_shots ${CMAKE_CURRENT_SOURCE_DIR}/tests/liveShots.cpp)
  target_link_libraries(interstellar_live_shots PRIVATE interstellar_app interstellar_host)
  find_program(INTERSTELLAR_FFMPEG_FOR_LIVE ffmpeg)
  if(INTERSTELLAR_FFMPEG_FOR_LIVE)
    add_test(NAME interstellar_live
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/live.sh" "$<TARGET_FILE:interstellar-cc>"
              "$<TARGET_FILE:interstellar_live_shots>" "${INTERSTELLAR_FFMPEG_FOR_LIVE}"
              "${CMAKE_CURRENT_BINARY_DIR}/live")
  endif()
endif()
