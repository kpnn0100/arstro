# solaris_core — THE application: the service (grammar, events, model, codec, API document) over
# the model and the engine. No codec, no device API, no OS path (R-SVC-4).
add_library(solaris_core STATIC
  Compile.cpp
  PluginDevice.cpp
  Surface.cpp
  WorkerPool.cpp
  Formula.cpp
  Bindings.cpp
  Settings.cpp
  Player.cpp
  Follower.cpp
  DriftIn.cpp
  Auditioner.cpp
  NotePreview.cpp
  service/Json.cpp
  service/Command.cpp
  service/Event.cpp
  service/AppModelCodec.cpp
  service/ApiDoc.cpp
  service/SolarisService.cpp
  service/ServiceEdit.cpp
  service/ServiceModel.cpp
  service/ServiceRender.cpp
  service/ServiceMachine.cpp
  service/ServiceTransport.cpp
  service/ServiceAuto.cpp
  service/ServiceCompose.cpp
  service/ServiceArrange.cpp
  service/ServiceVersions.cpp
  service/ServiceFiles.cpp
  service/ServiceAudioEdit.cpp
  service/ServicePresets.cpp
  service/ServiceLibrary.cpp
  service/ServicePlugins.cpp
  service/ServiceControl.cpp
  service/ServiceSurface.cpp
  service/Notation.cpp
  service/RegistryModel.cpp
  service/Keymap.cpp
  service/ServiceMidi.cpp
  service/ServiceInput.cpp)
target_include_directories(solaris_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/service)
find_package(Threads REQUIRED)
target_link_libraries(solaris_core PUBLIC solaris_model solaris_engine Threads::Threads)

add_executable(solaris_service_tests tests/serviceTests.cpp)
target_link_libraries(solaris_service_tests PRIVATE solaris_core)
add_test(NAME solaris_service COMMAND solaris_service_tests)
