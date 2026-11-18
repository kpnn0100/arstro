# Interstellar — a video colour tool with an editor attached. See REQUIREMENTS.md.
#
# Gated on cosmo_core because the RACK IS a hosted CosmoService: without Cosmo there is no colour
# authority, and an Interstellar that cannot grade is not a degraded Interstellar — it is the thing
# two previous builds shipped by mistake.
if(TARGET cosmo_core)
  if(TARGET solaris_core)
    add_subdirectory(song)   # R-SONG: a Solaris song hosted here — the one target that sees Solaris
  endif()
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

# R-DLV-1/2: the CLI draws burn-ins and captions in the app's typeface — the app library's text drawer
if(TARGET interstellar_app AND TARGET interstellar-cc)
  target_link_libraries(interstellar-cc PRIVATE interstellar_app)
  target_compile_definitions(interstellar-cc PRIVATE INTERSTELLAR_HAVE_TEXT=1)
endif()

# The GTK window: the app's hooks bound to the service. The ONLY OS code in the GUI.
if(TARGET interstellar_app AND TARGET interstellar_host)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(INTERSTELLAR_GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
  add_executable(interstellar ${CMAKE_CURRENT_SOURCE_DIR}/linux_main.cpp)
  target_link_libraries(interstellar PRIVATE interstellar_app interstellar_host PkgConfig::INTERSTELLAR_GTK3)
  if(TARGET interstellar_song_ui)   # R-SONG-7: the Song page's Solaris window
    target_link_libraries(interstellar PRIVATE interstellar_song_ui)
    target_compile_definitions(interstellar PRIVATE INTERSTELLAR_HAVE_SONG_UI=1)
  endif()
endif()

# Every codec and profile Deliver offers, through the real encoders, read back with ffprobe —
# and the colour of an H.264 render against the still (R-RENDER-6, D-9).
if(TARGET interstellar-cc AND TARGET interstellar_host)
  find_program(INTERSTELLAR_FFMPEG_FOR_CODECS ffmpeg)
  find_program(INTERSTELLAR_FFPROBE_FOR_CODECS ffprobe)
  if(INTERSTELLAR_FFMPEG_FOR_CODECS AND INTERSTELLAR_FFPROBE_FOR_CODECS)
    add_test(NAME interstellar_render_codecs
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/render_codecs.sh" "$<TARGET_FILE:interstellar-cc>"
              "${INTERSTELLAR_FFMPEG_FOR_CODECS}" "${INTERSTELLAR_FFPROBE_FOR_CODECS}"
              "${CMAKE_CURRENT_BINARY_DIR}/render_codecs")
  endif()
endif()

# The real app bound to the real service, rendered headlessly — the GTK host's wiring, tested.
if(TARGET interstellar_app AND TARGET interstellar_host)
  add_executable(interstellar_live_shots ${CMAKE_CURRENT_SOURCE_DIR}/tests/liveShots.cpp)
  target_link_libraries(interstellar_live_shots PRIVATE interstellar_app interstellar_host)
  if(TARGET interstellar_song_ui)   # R-SONG-7: the Song page drawn with Solaris's real window
    target_link_libraries(interstellar_live_shots PRIVATE interstellar_song_ui)
    target_compile_definitions(interstellar_live_shots PRIVATE INTERSTELLAR_HAVE_SONG_UI=1)
  endif()
  find_program(INTERSTELLAR_FFMPEG_FOR_LIVE ffmpeg)
  if(INTERSTELLAR_FFMPEG_FOR_LIVE)
    set(INTERSTELLAR_LIVE_SOLCC "")
    if(TARGET interstellar_song_ui AND TARGET solaris-cc)   # R-SONG-7: and the Song page, with a song made by Solaris
      set(INTERSTELLAR_LIVE_SOLCC "$<TARGET_FILE:solaris-cc>")
    endif()
    add_test(NAME interstellar_live
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/live.sh" "$<TARGET_FILE:interstellar-cc>"
              "$<TARGET_FILE:interstellar_live_shots>" "${INTERSTELLAR_FFMPEG_FOR_LIVE}"
              "${CMAKE_CURRENT_BINARY_DIR}/live" ${INTERSTELLAR_LIVE_SOLCC})
  endif()
endif()
