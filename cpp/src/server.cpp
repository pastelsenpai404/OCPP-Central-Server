#include "ocpp/executor.hpp"
#include "ocpp/service.hpp"
#include <atomic>
#include <charconv>
#include <drogon/HttpFilter.h>
#include <drogon/WebSocketController.h>
#include <drogon/drogon.h>
#include <iostream>
#include <set>

namespace {
using namespace ocpp;
using HttpCallback = std::function<void(const drogon::HttpResponsePtr &)>;
using Completion = std::function<void(int, ocpp::Json)>;
drogon::HttpResponsePtr http(int status, const ocpp::Json &body) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(static_cast<drogon::HttpStatusCode>(status));
    response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    response->setBody(body.dump());
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("X-Content-Type-Options", "nosniff");
    return response;
}
struct Pending {
    std::string action;
    std::chrono::steady_clock::time_point deadline;
    Completion complete;
};
struct Session {
    std::string station;
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
    Database database;
    Service service;
    std::mutex mutex;
    std::map<std::string, std::shared_ptr<Session>> sessions;
    RateLimit api_rate{50, 20};
    std::atomic<std::uint64_t> frames{0}, errors{0}, overloads{0};
    // Destroy executor first, so queued jobs drain while other members still exist.
    Executor executor;
    Runtime(Config c, const std::filesystem::path &path)
        : config(std::move(c)), schemas(path), database(config), service(database, schemas),
          executor(config.workers, 256) {}
    std::shared_ptr<Session> find(const std::string &id) {
        std::lock_guard lock(mutex);
        const auto it = sessions.find(id);
        return it == sessions.end() ? nullptr : it->second;
    }
    bool admit_api() {
        std::lock_guard lock(mutex);
        return api_rate.allow();
    }
};
std::unique_ptr<Runtime> runtime;
void failure(const HttpCallback &cb, int code, const char *message) {
    cb(http(code, {{"error", message}}));
}
std::string station_from_path(const std::string &path) {
    return path.substr(path.find_last_of('/') + 1);
}
const std::set<std::string> outbound{"CancelReservation",
                                     "ChangeAvailability",
                                     "ChangeConfiguration",
                                     "ClearCache",
                                     "ClearChargingProfile",
                                     "DataTransfer",
                                     "GetCompositeSchedule",
                                     "GetConfiguration",
                                     "GetDiagnostics",
                                     "GetLocalListVersion",
                                     "RemoteStartTransaction",
                                     "RemoteStopTransaction",
                                     "ReserveNow",
                                     "Reset",
                                     "SendLocalList",
                                     "SetChargingProfile",
                                     "TriggerMessage",
                                     "UnlockConnector",
                                     "UpdateFirmware"};
void check_command(const std::string &action, const ocpp::Json &payload) {
    if (!outbound.contains(action))
        throw ProtocolError("NotSupported", "Unknown outbound action");
    runtime->schemas.validate(action, payload);
    if (action == "GetDiagnostics" || action == "UpdateFirmware")
        throw ProtocolError("NotSupported", "Download/upload URL commands require an approved "
                                            "destination policy; disabled in this build");
}
void command(const std::string &station, const std::string &action, ocpp::Json payload,
             Completion complete) {
    auto session = runtime->find(station);
    if (!session) {
        complete(409, {{"error", "station_offline"}});
        return;
    }
    const auto id = drogon::utils::getUuid();
    const auto completion = std::make_shared<Completion>(std::move(complete));
    if (!runtime->executor.submit(station, [session, station, action, payload = std::move(payload),
                                            id, completion]() mutable {
            try {
                runtime->service.audit(action, station, id);
                std::lock_guard lock(session->mutex);
                auto connection = session->connection.lock();
                if (session->closed || !connection || !connection->connected()) {
                    (*completion)(409, {{"error", "station_offline"}});
                    return;
                }
                if (session->pending.size() >= 16) {
                    (*completion)(429, {{"error", "command_capacity"}});
                    return;
                }
                session->pending.emplace(
                    id, Pending{action, std::chrono::steady_clock::now() + std::chrono::seconds(30),
                                *completion});
                connection->send(ocpp::Json::array({2, id, action, payload}).dump());
            } catch (const std::exception &) {
                (*completion)(503, {{"error", "command_unavailable"}});
            }
        })) {
        runtime->overloads++;
        (*completion)(503, {{"error", "server_busy"}});
    }
}
} // namespace

class StationAuth : public drogon::HttpFilter<StationAuth> {
  public:
    void doFilter(const drogon::HttpRequestPtr &request, drogon::FilterCallback &&reject,
                  drogon::FilterChainCallback &&next) override {
        const auto station = station_from_path(request->path());
        const auto &config = runtime->config;
        if (!station_id_valid(station) || !config.station_secrets.contains(station) ||
            !request->getHeader("origin").empty()) {
            reject(http(401, {{"error", "unauthorized"}}));
            return;
        }
        // Require an exact, unambiguous protocol. Other legacy versions are not advertised.
        if (request->getHeader("sec-websocket-protocol") != "ocpp1.6") {
            reject(http(400, {{"error", "unsupported_subprotocol"}}));
            return;
        }
        const auto expected =
            "Basic " + drogon::utils::base64Encode(
                           station + ":" + config.station_secrets[station].get<std::string>());
        if (!constant_equal(request->getHeader("authorization"), expected)) {
            reject(http(401, {{"error", "unauthorized"}}));
            return;
        }
        auto callbacks =
            std::make_shared<std::pair<drogon::FilterCallback, drogon::FilterChainCallback>>(
                std::move(reject), std::move(next));
        if (!runtime->executor.submit(station, [station, callbacks] {
                try {
                    if (!runtime->service.station_registered(station)) {
                        callbacks->first(http(401, {{"error", "unauthorized"}}));
                        return;
                    }
                    if (runtime->find(station)) {
                        callbacks->first(http(409, {{"error", "station_already_connected"}}));
                        return;
                    }
                    callbacks->second();
                } catch (const std::exception &) {
                    callbacks->first(http(503, {{"error", "database_unavailable"}}));
                }
            }))
            callbacks->first(http(503, {{"error", "server_busy"}}));
    }
};
class OcppSocket : public drogon::WebSocketController<OcppSocket> {
  public:
    WS_PATH_LIST_BEGIN
    WS_ADD_PATH_VIA_REGEX("^/(steve|develop)/websocket/CentralSystemService/[A-Za-z0-9_-]{1,64}$",
                          "StationAuth");
    WS_ADD_PATH_VIA_REGEX("^/ocpp/[A-Za-z0-9_-]{1,64}$", "StationAuth");
    WS_PATH_LIST_END
    void handleNewConnection(const drogon::HttpRequestPtr &request,
                             const drogon::WebSocketConnectionPtr &connection) override {
        auto session = std::make_shared<Session>();
        session->station = station_from_path(request->path());
        session->connection = connection;
        connection->setContext(session);
        connection->setPingMessage("", std::chrono::seconds(30));
        bool admitted = false;
        {
            std::lock_guard lock(runtime->mutex);
            if (runtime->sessions.size() < 10000)
                admitted = runtime->sessions.emplace(session->station, session).second;
        }
        if (!admitted)
            connection->shutdown(drogon::CloseCode::kViolation, "Duplicate session or capacity");
    }
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr &connection) override {
        auto session = connection->getContext<Session>();
        if (!session)
            return;
        std::map<std::string, Pending> pending;
        {
            std::lock_guard lock(session->mutex);
            session->closed = true;
            pending.swap(session->pending);
        }
        {
            std::lock_guard lock(runtime->mutex);
            const auto it = runtime->sessions.find(session->station);
            if (it != runtime->sessions.end() && it->second == session)
                runtime->sessions.erase(it);
        }
        for (auto &[id, p] : pending) {
            static_cast<void>(id);
            p.complete(409, {{"error", "station_disconnected"}});
        }
    }
    void handleNewMessage(const drogon::WebSocketConnectionPtr &connection, std::string &&wire,
                          const drogon::WebSocketMessageType &type) override {
        if (type == drogon::WebSocketMessageType::Ping ||
            type == drogon::WebSocketMessageType::Pong ||
            type == drogon::WebSocketMessageType::Close)
            return;
        auto session = connection->getContext<Session>();
        if (!session)
            return;
        if (type != drogon::WebSocketMessageType::Text) {
            connection->shutdown(drogon::CloseCode::kInvalidMessage, "Text frames required");
            return;
        }
        {
            std::lock_guard lock(session->mutex);
            if (session->closed || !session->rate.allow()) {
                connection->shutdown(drogon::CloseCode::kViolation, "Message rate limit");
                return;
            }
        }
        if (session->inflight.fetch_add(1) >= 8) {
            session->inflight--;
            runtime->overloads++;
            connection->shutdown(drogon::CloseCode::kViolation, "Queue limit");
            return;
        }
        runtime->frames++;
        if (!runtime->executor.submit(session->station, [session, wire = std::move(wire)] {
                struct Release {
                    std::atomic<unsigned> &count;
                    ~Release() {
                        count--;
                    }
                } release{session->inflight};
                auto socket = session->connection.lock();
                if (!socket || !socket->connected())
                    return;
                std::optional<Message> message;
                try {
                    message = parse(wire);
                    if (message->type == 2) {
                        auto result = runtime->service.call(session->station, *message);
                        socket->send(result.dump());
                    } else {
                        std::optional<Pending> p;
                        {
                            std::lock_guard lock(session->mutex);
                            const auto found = session->pending.find(message->id);
                            if (found != session->pending.end()) {
                                p = std::move(found->second);
                                session->pending.erase(found);
                            }
                        }
                        if (!p)
                            throw ProtocolError("ProtocolError", "Uncorrelated response");
                        if (message->type == 4)
                            p->complete(502, {{"error", "station_call_error"},
                                              {"code", message->error_code}});
                        else {
                            try {
                                runtime->schemas.validate(p->action, message->payload, true);
                                p->complete(200, message->payload);
                            } catch (const std::exception &) {
                                p->complete(502, {{"error", "invalid_station_response"}});
                            }
                        }
                    }
                } catch (const ProtocolError &e) {
                    runtime->errors++;
                    if (message && message->type == 2)
                        socket->send(error(message->id, e.code, e.what()).dump());
                    else
                        socket->shutdown(drogon::CloseCode::kProtocolError, "Invalid OCPP message");
                } catch (const std::exception &) {
                    runtime->errors++;
                    if (message && message->type == 2)
                        socket->send(
                            error(message->id, "InternalError", "Service temporarily unavailable")
                                .dump());
                    else
                        socket->shutdown(drogon::CloseCode::kUnexpectedCondition,
                                         "Service unavailable");
                }
            })) {
            session->inflight--;
            runtime->overloads++;
            connection->shutdown(drogon::CloseCode::kViolation, "Server busy");
        }
    }
};

namespace {
bool authorize(const drogon::HttpRequestPtr &req, const HttpCallback &cb, int role) {
    if (runtime->config.role(req->getHeader("authorization")) < role) {
        failure(cb, 401, "unauthorized");
        return false;
    }
    if (!req->getHeader("origin").empty()) {
        failure(cb, 403, "browser_origin_not_allowed");
        return false;
    }
    if (!runtime->admit_api()) {
        failure(cb, 429, "request_rate_limit");
        return false;
    }
    return true;
}
ocpp::Json request_json(const drogon::HttpRequestPtr &req) {
    if (req->getHeader("content-type").find("application/json") != 0)
        throw ProtocolError("FormationViolation", "JSON content type required");
    // Reuse the bounded parser, including duplicate-key and depth rejection.
    const auto body = std::string(req->body());
    auto parsed = parse("[2,\"api\",\"Api\"," + body + "]");
    return parsed.payload;
}
void register_routes() {
    auto &app = drogon::app();
    app.registerHandler("/health/live",
                        [](const drogon::HttpRequestPtr &, HttpCallback &&cb) {
                            cb(http(200, {{"status", "live"}}));
                        },
                        {drogon::Get});
    app.registerHandler("/health/ready",
                        [](const drogon::HttpRequestPtr &, HttpCallback &&cb) {
                            const auto callback = std::make_shared<HttpCallback>(std::move(cb));
                            if (!runtime->executor.submit("health", [callback] {
                                    try {
                                        auto db = runtime->database.acquire();
                                        db.execute(
                                            "SELECT station_id FROM cpp_ocpp_replay LIMIT 1");
                                        (*callback)(http(200, {{"status", "ready"}}));
                                    } catch (const std::exception &e) {
                                        LOG_ERROR << "Readiness failed: " << e.what();
                                        failure(*callback, 503, "not_ready");
                                    }
                                }))
                                failure(*callback, 503, "server_busy");
                        },
                        {drogon::Get});
    app.registerHandler("/api/v1/metrics",
                        [](const drogon::HttpRequestPtr &req, HttpCallback &&cb) {
                            if (!authorize(req, cb, 1))
                                return;
                            std::size_t sessions = 0;
                            {
                                std::lock_guard lock(runtime->mutex);
                                sessions = runtime->sessions.size();
                            }
                            cb(http(200, {{"frames", runtime->frames.load()},
                                          {"errors", runtime->errors.load()},
                                          {"overloads", runtime->overloads.load()},
                                          {"sessions", sessions}}));
                        },
                        {drogon::Get});
    app.registerHandler(
        "/api/v1/{1}",
        [](const drogon::HttpRequestPtr &req, HttpCallback &&cb, std::string resource) {
            if (!authorize(req, cb, resource == "audit" ? 3 : 1))
                return;
            unsigned offset = 0;
            const auto input = req->getParameter("offset");
            if (!input.empty()) {
                auto [end, ec] = std::from_chars(input.data(), input.data() + input.size(), offset);
                if (ec != std::errc{} || end != input.data() + input.size()) {
                    failure(cb, 400, "invalid_offset");
                    return;
                }
            }
            const auto callback = std::make_shared<HttpCallback>(std::move(cb));
            if (!runtime->executor.submit("api-read", [resource, offset, callback] {
                    try {
                        (*callback)(http(200, runtime->service.overview(resource, offset)));
                    } catch (const ProtocolError &) {
                        failure(*callback, 400, "invalid_resource_or_offset");
                    } catch (const std::exception &) {
                        failure(*callback, 503, "database_unavailable");
                    }
                }))
                failure(*callback, 503, "server_busy");
        },
        {drogon::Get});
    app.registerHandler("/api/v1/chargepoints/{1}/commands/{2}",
                        [](const drogon::HttpRequestPtr &req, HttpCallback &&cb,
                           std::string station, std::string action) {
                            if (!authorize(req, cb, 2))
                                return;
                            try {
                                if (!station_id_valid(station))
                                    throw ProtocolError("PropertyConstraintViolation",
                                                        "Invalid station");
                                auto payload = request_json(req);
                                check_command(action, payload);
                                auto callback = std::make_shared<HttpCallback>(std::move(cb));
                                command(station, action, std::move(payload),
                                        [callback](int status, ocpp::Json result) {
                                            (*callback)(http(status, result));
                                        });
                            } catch (const ProtocolError &) {
                                failure(cb, 400, "invalid_or_disabled_command");
                            } catch (const std::exception &) {
                                failure(cb, 400, "invalid_json");
                            }
                        },
                        {drogon::Post});
    app.registerHandler("/api/v1/chargepoints",
                        [](const drogon::HttpRequestPtr &req, HttpCallback &&cb) {
                            if (!authorize(req, cb, 3))
                                return;
                            try {
                                const auto p = request_json(req);
                                const auto id = p.at("chargeBoxId").get<std::string>();
                                if (!station_id_valid(id) ||
                                    !runtime->config.station_secrets.contains(id)) {
                                    failure(cb, 400, "credential_configuration_required");
                                    return;
                                }
                                const auto callback = std::make_shared<HttpCallback>(std::move(cb));
                                if (!runtime->executor.submit(id, [id, callback] {
                                        try {
                                            runtime->service.provision_station(id);
                                            (*callback)(http(201, {{"chargeBoxId", id}}));
                                        } catch (const std::exception &) {
                                            failure(*callback, 503, "provision_failed");
                                        }
                                    }))
                                    failure(*callback, 503, "server_busy");
                            } catch (const std::exception &) {
                                failure(cb, 400, "invalid_request");
                            }
                        },
                        {drogon::Post});
    app.registerHandler(
        "/api/v1/ocppTags",
        [](const drogon::HttpRequestPtr &req, HttpCallback &&cb) {
            if (!authorize(req, cb, 3))
                return;
            try {
                auto p = request_json(req);
                auto callback = std::make_shared<HttpCallback>(std::move(cb));
                if (!runtime->executor.submit("tag-admin", [p = std::move(p), callback] {
                        try {
                            runtime->service.upsert_tag(p);
                            (*callback)(http(200, {{"status", "updated"}}));
                        } catch (const ProtocolError &) {
                            failure(*callback, 400, "invalid_tag");
                        } catch (const std::exception &) {
                            failure(*callback, 503, "update_failed");
                        }
                    }))
                    failure(*callback, 503, "server_busy");
            } catch (const std::exception &) {
                failure(cb, 400, "invalid_request");
            }
        },
        {drogon::Post});
    app.registerPreSendingAdvice(
        [](const drogon::HttpRequestPtr &, const drogon::HttpResponsePtr &response) {
            if (response->statusCode() == drogon::k101SwitchingProtocols)
                response->addHeader("Sec-WebSocket-Protocol", "ocpp1.6");
            response->addHeader("X-Content-Type-Options", "nosniff");
        });
    app.getLoop()->runEvery(1.0, [] {
        std::vector<std::shared_ptr<Session>> sessions;
        {
            std::lock_guard lock(runtime->mutex);
            for (const auto &[id, s] : runtime->sessions) {
                static_cast<void>(id);
                sessions.push_back(s);
            }
        }
        const auto now = std::chrono::steady_clock::now();
        for (const auto &s : sessions) {
            std::vector<Completion> expired;
            {
                std::lock_guard lock(s->mutex);
                for (auto it = s->pending.begin(); it != s->pending.end();) {
                    if (it->second.deadline <= now) {
                        expired.push_back(std::move(it->second.complete));
                        it = s->pending.erase(it);
                    } else
                        ++it;
                }
            }
            for (auto &done : expired)
                done(504, {{"error", "station_timeout"}});
        }
    });
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: ocpp_server <schema-directory>\n";
            return 2;
        }
        runtime = std::make_unique<Runtime>(Config::environment(), argv[1]);
        std::cerr << "Runtime initialized\n";
        auto &app = drogon::app();
        app.setLogLevel(trantor::Logger::kWarn)
            .setThreadNum(runtime->config.workers)
            .addListener(runtime->config.bind, runtime->config.port)
            .setMaxConnectionNum(12000)
            .setMaxConnectionNumPerIP(12000)
            .setClientMaxBodySize(65536)
            .setClientMaxMemoryBodySize(65536)
            .setClientMaxWebSocketMessageSize(65536)
            .setIdleConnectionTimeout(90)
            .setKeepaliveRequestsNumber(100)
            .setPipeliningRequestsNumber(2)
            .setServerHeaderField("");
        app.disableSession();
        register_routes();
        std::cerr << "Routes initialized; starting listener\n";
        app.run();
        runtime.reset();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Startup failed: " << e.what() << '\n';
        return 1;
    }
}
