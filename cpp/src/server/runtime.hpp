#pragma once
#include "ocpp/application/central_system.hpp"
#include "ocpp/protocol/commands.hpp"
#include "ocpp/protocol/legacy/soap.hpp"
#include "ocpp/runtime/executor.hpp"
#include "ocpp/security/credentials.hpp"
#include "ocpp/transport/http_client.hpp"
#include <atomic>
#include <drogon/WebSocketConnection.h>
#include <drogon/drogon.h>

namespace ocpp::server {
using namespace ocpp;
using HttpCallback = std::function<void(const drogon::HttpResponsePtr &)>;
using Completion = std::function<void(int, ocpp::Json)>;
struct Pending {
    std::string action;
    ocpp::Json request;
    std::chrono::steady_clock::time_point deadline;
    Completion complete;
};
struct CommandQueueExpired {};
struct Session {
    std::string station;
    std::string protocol;
    std::weak_ptr<drogon::WebSocketConnection> connection;
    std::mutex mutex;
    RateLimit rate{40, 20};
    std::map<std::string, Pending> pending;
    std::atomic<unsigned> inflight{0};
    bool closed = false;
};
struct Runtime {
    Config config;
    Schemas schemas;
    Schemas schemas201;
    std::map<std::string, std::unique_ptr<Schemas>> legacy_schemas;
    std::map<std::string, std::unique_ptr<SoapCodec>> soap_codecs;
    ocpp::Json command_catalog = ocpp::Json::object();
    Database database;
    Service service;
    std::mutex mutex;
    std::map<std::string, std::shared_ptr<Session>> sessions;
    RateLimit api_rate{50, 20};
    std::atomic<std::uint64_t> frames{0}, errors{0}, overloads{0};
    // Destroy executor first, so queued jobs drain while other members still exist.
    Executor outbound_executor{2, 64};
    Executor executor;
    Runtime(Config config, const std::filesystem::path &schemas);
    std::shared_ptr<Session> find(const std::string &station);
    bool admit_api();
};

extern std::unique_ptr<Runtime> runtime;
drogon::HttpResponsePtr http(int status, const ocpp::Json &body);
void failure(const HttpCallback &callback, int status, const char *message);
void soap_failure(const HttpCallback &callback, int status, const char *message,
                  std::string_view relates_to = {});
bool authorize(const drogon::HttpRequestPtr &request, const HttpCallback &callback, int role);
ocpp::Json request_json(const drogon::HttpRequestPtr &request);
std::string station_from_path(const std::string &path);
void check_command(const std::string &station, const std::string &action,
                   const ocpp::Json &payload);
void command(const std::string &station, const std::string &action, ocpp::Json payload,
             const std::string &id, Completion complete);
void register_routes();
void register_station_transport();
void register_admin_routes();
void register_soap_routes();
void register_maintenance();
} // namespace ocpp::server
