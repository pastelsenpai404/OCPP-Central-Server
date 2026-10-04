include(FetchContent)
find_package(Threads REQUIRED)
FetchContent_Declare(sim_http GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG 4f3f9ef19be83ae97a5d9a059432dc00e445b7ab SOURCE_SUBDIR unused)
FetchContent_Declare(sim_json GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG 55f93686c01528224f448c19128836e7df245f72 SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(sim_http sim_json)
add_library(sim_dependencies INTERFACE)
target_include_directories(sim_dependencies SYSTEM INTERFACE ${sim_http_SOURCE_DIR} ${sim_json_SOURCE_DIR}/single_include)
target_link_libraries(sim_dependencies INTERFACE Threads::Threads)
target_compile_definitions(sim_dependencies INTERFACE CPPHTTPLIB_HEADER_MAX_LENGTH=8192
  CPPHTTPLIB_REQUEST_URI_MAX_LENGTH=2048 CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH=65536)
if(WIN32)
  target_link_libraries(sim_dependencies INTERFACE ws2_32 winhttp)
else()
  find_package(OpenSSL 3 REQUIRED)
  target_compile_definitions(sim_dependencies INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT)
  target_link_libraries(sim_dependencies INTERFACE OpenSSL::SSL OpenSSL::Crypto)
endif()
