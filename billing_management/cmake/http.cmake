include(FetchContent)
find_package(Threads REQUIRED)
# Header-only dependencies, pinned to reviewed source revisions.
FetchContent_Declare(billing_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG 4f3f9ef19be83ae97a5d9a059432dc00e445b7ab
  SOURCE_SUBDIR unused)
FetchContent_Declare(billing_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG 55f93686c01528224f448c19128836e7df245f72
  SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(billing_httplib billing_json)
add_library(billing_http_dependencies INTERFACE)
target_include_directories(billing_http_dependencies SYSTEM INTERFACE
  ${billing_httplib_SOURCE_DIR} ${billing_json_SOURCE_DIR}/single_include)
target_link_libraries(billing_http_dependencies INTERFACE Threads::Threads)
target_compile_definitions(billing_http_dependencies INTERFACE
  CPPHTTPLIB_HEADER_MAX_LENGTH=8192 CPPHTTPLIB_REQUEST_URI_MAX_LENGTH=2048)
if(WIN32)
  target_link_libraries(billing_http_dependencies INTERFACE ws2_32)
endif()
