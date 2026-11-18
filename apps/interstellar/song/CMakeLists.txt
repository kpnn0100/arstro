# interstellar_song — a Solaris song hosted inside Interstellar (R-SONG). The ONE target that sees
# Solaris's headers: solaris_core is linked PRIVATE, so its include path (an AppModel.h, a Command.h,
# an Event.h, a Json.h — all names Interstellar's service also has) never reaches anything that links
# this. Interstellar sees SongHost.h alone.
add_library(interstellar_song STATIC SongHost.cpp)
target_include_directories(interstellar_song PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(interstellar_song PRIVATE solaris_core)
target_compile_features(interstellar_song PUBLIC cxx_std_17)

# interstellar_song_ui — Solaris's OWN window for a hosted song, on Interstellar's Song page (R-SONG-7). Solaris's
# front end is linked PRIVATE for the same reason (its App.h, Theme.h and AppHooks.h are names Interstellar's front
# end also has); its public header names only Artboard's types and Interstellar's EmbeddedView. Only the window
# (and the headless shots of it) link this: the service, the CLI and the tests of the core never see a widget.
if(TARGET solaris_app)
  add_library(interstellar_song_ui STATIC SongView.cpp)
  target_include_directories(interstellar_song_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                                                        ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/Artboard/src)
  target_link_libraries(interstellar_song_ui PRIVATE solaris_app interstellar_song)
  target_link_libraries(interstellar_song_ui PUBLIC artboard_core)
endif()
