# solaris_core — THE application: the service (grammar, events, model, codec, API document) over
# the model and the engine. No codec, no device API, no OS path (R-SVC-4).
add_library(solaris_core STATIC
  Compile.cpp
  Settings.cpp
  service/Json.cpp
  service/Command.cpp
  service/Event.cpp
  service/AppModelCodec.cpp
  service/ApiDoc.cpp
  service/SolarisService.cpp
  service/ServiceEdit.cpp
  service/ServiceModel.cpp
  service/ServiceRender.cpp
  service/ServiceMachine.cpp)
target_include_directories(solaris_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/service)
target_link_libraries(solaris_core PUBLIC solaris_model solaris_engine)

add_executable(solaris_service_tests tests/serviceTests.cpp)
target_link_libraries(solaris_service_tests PRIVATE solaris_core)
add_test(NAME solaris_service COMMAND solaris_service_tests)
