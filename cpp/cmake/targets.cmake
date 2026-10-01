# Shared public types; implementation dependencies belong to their owning module.
add_library(ocpp_types INTERFACE)
target_include_directories(ocpp_types INTERFACE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(ocpp_types INTERFACE nlohmann_json::nlohmann_json nlohmann_json_schema_validator)

add_library(ocpp_protocol STATIC src/protocol/message.cpp src/protocol/v16/commands.cpp
  src/protocol/v201/commands.cpp src/protocol/legacy/soap.cpp)
target_link_libraries(ocpp_protocol PUBLIC ocpp_types nlohmann_json_schema_validator PRIVATE pugixml::pugixml)
add_library(ocpp_security STATIC src/security/credentials.cpp src/security/certificates.cpp)
target_link_libraries(ocpp_security PUBLIC ocpp_types)
add_library(ocpp_runtime STATIC src/runtime/executor.cpp)
target_link_libraries(ocpp_runtime PUBLIC ocpp_types)
add_library(ocpp_http STATIC src/transport/http_client.cpp)
target_link_libraries(ocpp_http PUBLIC ocpp_protocol)
add_library(ocpp_config STATIC src/config/environment.cpp)
target_link_libraries(ocpp_config PUBLIC ocpp_types PRIVATE ocpp_protocol ocpp_security ocpp_http)
add_library(ocpp_persistence STATIC src/persistence/mariadb.cpp)
target_include_directories(ocpp_persistence PRIVATE ${MARIADB_INCLUDE_DIR})
target_link_libraries(ocpp_persistence PUBLIC ocpp_types PRIVATE ${MARIADB_LIBRARY})
add_library(ocpp_billing STATIC src/billing/settlement.cpp)
target_link_libraries(ocpp_billing PUBLIC ocpp_protocol)
add_library(ocpp_application STATIC src/application/ocpp16.cpp src/application/ocpp201.cpp
  src/application/command_tasks.cpp src/application/queries.cpp src/application/billing.cpp src/application/access.cpp)
target_link_libraries(ocpp_application PUBLIC ocpp_protocol ocpp_persistence PRIVATE ocpp_security ocpp_billing)
if(WIN32)
  target_link_libraries(ocpp_security PRIVATE crypt32 bcrypt)
  target_link_libraries(ocpp_http PRIVATE winhttp)
else()
  target_link_libraries(ocpp_security PRIVATE OpenSSL::Crypto)
  target_link_libraries(ocpp_http PRIVATE CURL::libcurl)
endif()
# Compatibility aggregation for executables, not a second implementation library.
add_library(ocpp_core INTERFACE)
target_link_libraries(ocpp_core INTERFACE ocpp_application ocpp_config ocpp_runtime ocpp_http ocpp_security ocpp_billing)

# Compile reflected Drogon controllers directly to keep static registrations alive.
add_executable(ocpp_server src/main.cpp src/server/bootstrap.cpp src/server/runtime.cpp
  src/server/routes.cpp src/server/maintenance.cpp src/server/http/responses.cpp
  src/server/http/requests.cpp src/server/http/admin_routes.cpp src/server/soap/central_system_routes.cpp
  src/server/websocket/station_socket.cpp src/server/commands/dispatcher.cpp)
target_include_directories(ocpp_server PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(ocpp_server PRIVATE ocpp_core Drogon::Drogon)
add_executable(ocpp_tests tests/unit/core_tests.cpp)
target_link_libraries(ocpp_tests PRIVATE ocpp_core)
add_executable(ocpp_benchmark tests/performance/benchmark.cpp)
target_link_libraries(ocpp_benchmark PRIVATE ocpp_core)
foreach(target ocpp_protocol ocpp_security ocpp_runtime ocpp_http ocpp_config ocpp_persistence
  ocpp_billing ocpp_application ocpp_server ocpp_tests ocpp_benchmark)
  ocpp_harden(${target})
endforeach()

