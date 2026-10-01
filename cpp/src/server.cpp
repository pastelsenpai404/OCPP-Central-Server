#include "ocpp/commands.hpp"
#include "ocpp/executor.hpp"
#include "ocpp/http_client.hpp"
#include "ocpp/legacy.hpp"
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
    Database database;
    Service service;
    std::mutex mutex;
    std::map<std::string, std::shared_ptr<Session>> sessions;
    RateLimit api_rate{50, 20};
    std::atomic<std::uint64_t> frames{0}, errors{0}, overloads{0};
    // Destroy executor first, so queued jobs drain while other members still exist.
    Executor outbound_executor{2, 64};
    Executor executor;
    Runtime(Config c, const std::filesystem::path &path)
        : config(std::move(c)), schemas(path), schemas201(path / "2.0.1", true), database(config),
          service(database, schemas, &schemas201), executor(config.workers, 256) {
        for (const auto &[version, count] :
             std::map<std::string, std::size_t>{{"1.2", 36}, {"1.5", 48}, {"1.6", 56}}) {
            legacy_schemas.emplace(version,
                                   std::make_unique<Schemas>(path / version, false, count));
            soap_codecs.emplace(version,
                                std::make_unique<SoapCodec>(path / version / "soap.registry"));
        }
    }
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
void soap_failure(const HttpCallback &cb, int code, const char *message,
                  std::string_view relates_to = {}) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(static_cast<drogon::HttpStatusCode>(code));
    response->addHeader("Content-Type", "application/soap+xml; charset=utf-8");
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("X-Content-Type-Options", "nosniff");
    response->setBody(soap_fault(code < 500 && code != 429, message, relates_to));
    cb(response);
}
std::string station_from_path(const std::string &path) {
    return path.substr(path.find_last_of('/') + 1);
}
void check_command(const std::string &station, const std::string &action,
                   const ocpp::Json &payload) {
    const auto session = runtime->find(station);
    if (session && session->protocol == "ocpp2.0.1")
        validate201_command(runtime->schemas201, runtime->config, action, payload);
    else {
        if (session && session->protocol != "ocpp1.6") {
            const auto version = session->protocol.substr(4);
            runtime->legacy_schemas.at(version)->validate(action,
                                                          legacy_command(version, action, payload));
        }
        if (!session && runtime->config.soap_endpoints.contains(station)) {
            const auto version =
                runtime->config.soap_endpoints.at(station).at("version").get<std::string>();
            runtime->legacy_schemas.at(version)->validate(action,
                                                          legacy_command(version, action, payload));
        }
        validate_command(runtime->schemas, runtime->config, action, payload);
    }
}
void command(const std::string &station, const std::string &action, ocpp::Json payload,
             const std::string &id, Completion complete) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    auto session = runtime->find(station);
    if (!session) {
        if (runtime->config.soap_endpoints.contains(station)) {
            const auto completion = std::make_shared<Completion>(std::move(complete));
            if (!runtime->outbound_executor.submit(station, [station, action,
                                                             payload = std::move(payload), id,
                                                             completion, deadline] {
                    if (std::chrono::steady_clock::now() >= deadline) {
                        (*completion)(503, {{"error", "command_queue_expired"}});
                        return;
                    }
                    bool prepared = false;
                    int status = 503;
                    ocpp::Json result = {{"error", "soap_command_unavailable"}};
                    try {
                        const auto &endpoint = runtime->config.soap_endpoints.at(station);
                        const auto version = endpoint.at("version").get<std::string>();
                        runtime->service.prepare_command(station, action, id, payload,
                                                         "soap" + version);
                        prepared = true;
                        if (std::chrono::steady_clock::now() >= deadline)
                            throw CommandQueueExpired{};
                        const auto message_id = "urn:uuid:" + id;
                        const auto wire = runtime->soap_codecs.at(version)->encode(
                            action, legacy_command(version, action, payload), message_id, false,
                            station);
                        const auto authorization =
                            "Basic " +
                            drogon::utils::base64Encode(
                                station + ":" +
                                runtime->config.station_secrets.at(station).get<std::string>());
                        const auto reply =
                            post_http(endpoint.at("url").get<std::string>(),
                                      "application/soap+xml; charset=utf-8", authorization, wire);
                        if (reply.status != 200)
                            throw ProtocolError("ProtocolError",
                                                "SOAP device returned an HTTP error");
                        auto response = runtime->soap_codecs.at(version)->decode(reply.body, true);
                        if (response.action != action ||
                            (!response.message_id.empty() && response.message_id != message_id) ||
                            (!response.station.empty() && response.station != station))
                            throw ProtocolError("ProtocolError", "Uncorrelated SOAP response");
                        runtime->legacy_schemas.at(version)->validate(action, response.payload,
                                                                      true);
                        result = canonical_response(version, action, std::move(response.payload));
                        runtime->schemas.validate(action, result, true);
                        status = 200;
                    } catch (const CommandQueueExpired &) {
                        status = 503;
                        result = {{"error", "command_queue_expired"}};
                    } catch (const ProtocolError &) {
                        status = prepared ? 502 : 400;
                        result = {{"error", prepared ? "invalid_soap_response"
                                                     : "command_precondition_failed"}};
                    } catch (const std::exception &) {
                        status = prepared ? 504 : 503;
                        result = {{"error",
                                   prepared ? "soap_result_unknown" : "soap_command_unavailable"}};
                    }
                    if (prepared) {
                        try {
                            runtime->service.finish_command(id, status, result);
                        } catch (const std::exception &) {
                            status = 503;
                            result = {{"error", "command_result_not_persisted"}};
                        }
                    }
                    (*completion)(status, result);
                }))
                (*completion)(503, {{"error", "server_busy"}});
            return;
        }
        complete(409, {{"error", "station_offline"}});
        return;
    }
    const auto completion = std::make_shared<Completion>(std::move(complete));
    if (!runtime->executor.submit(station, [session, station, action, payload = std::move(payload),
                                            id, completion, deadline]() mutable {
            if (std::chrono::steady_clock::now() >= deadline) {
                (*completion)(503, {{"error", "command_queue_expired"}});
                return;
            }
            try {
                runtime->service.prepare_command(station, action, id, payload, session->protocol);
                const Completion persisted = [station, id, completion](int status,
                                                                       ocpp::Json result) {
                    auto body = std::make_shared<ocpp::Json>(std::move(result));
                    if (!runtime->executor.submit(station, [id, status, body, completion] {
                            try {
                                runtime->service.finish_command(id, status, *body);
                                (*completion)(status, *body);
                            } catch (const std::exception &) {
                                (*completion)(503, {{"error", "command_result_not_persisted"}});
                            }
                        }))
                        (*completion)(503, {{"error", "server_busy"}});
                };
                if (std::chrono::steady_clock::now() >= deadline) {
                    persisted(503, {{"error", "command_queue_expired"}});
                    return;
                }
                std::lock_guard lock(session->mutex);
                auto connection = session->connection.lock();
                if (session->closed || !connection || !connection->connected()) {
                    persisted(409, {{"error", "station_offline"}});
                    return;
                }
                if (session->pending.size() >= 16) {
                    persisted(429, {{"error", "command_capacity"}});
                    return;
                }
                session->pending.emplace(id, Pending{action, payload, deadline, persisted});
                const auto wire_payload =
                    session->protocol == "ocpp1.2" || session->protocol == "ocpp1.5"
                        ? legacy_command(session->protocol.substr(4), action, payload)
                        : payload;
                connection->send(ocpp::Json::array({2, id, action, wire_payload}).dump());
            } catch (const ProtocolError &) {
                (*completion)(400, {{"error", "command_precondition_failed"}});
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
        // Require an exact, unambiguous protocol.
        const auto protocol = request->getHeader("sec-websocket-protocol");
        if (protocol != "ocpp1.2" && protocol != "ocpp1.5" && protocol != "ocpp1.6" &&
            protocol != "ocpp2.0.1") {
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
        session->protocol = request->getHeader("sec-websocket-protocol");
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
                        const bool legacy =
                            session->protocol == "ocpp1.2" || session->protocol == "ocpp1.5";
                        if (legacy) {
                            runtime->legacy_schemas.at(session->protocol.substr(4))
                                ->validate(message->action, message->payload);
                            message->payload =
                                legacy_request(session->protocol.substr(4), message->action,
                                               std::move(message->payload));
                        }
                        auto result = session->protocol == "ocpp2.0.1"
                                          ? runtime->service.call201(session->station, *message)
                                          : runtime->service.call(session->station, *message,
                                                                  session->protocol);
                        if (legacy) {
                            result[2] = legacy_response(session->protocol.substr(4),
                                                        message->action, std::move(result[2]));
                            runtime->legacy_schemas.at(session->protocol.substr(4))
                                ->validate(message->action, result[2], true);
                        }
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
                                if (session->protocol == "ocpp1.2" ||
                                    session->protocol == "ocpp1.5") {
                                    runtime->legacy_schemas.at(session->protocol.substr(4))
                                        ->validate(p->action, message->payload, true);
                                    message->payload =
                                        canonical_response(session->protocol.substr(4), p->action,
                                                           std::move(message->payload));
                                }
                                if (session->protocol == "ocpp2.0.1")
                                    validate201_response(runtime->schemas201, p->action, p->request,
                                                         message->payload);
                                else
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
    for (const auto &[version, suffix] : std::map<std::string, std::string>{
             {"1.2", "12"}, {"1.5", "15"}, {"1.6", "16"}, {"auto", ""}}) {
        for (const auto &prefix :
             {std::string(), std::string("/services"), std::string("/steve/services"),
              std::string("/develop/services")})
            app.registerHandler(
                prefix + (version == "auto" ? "/CentralSystemService"
                                            : "/CentralSystemServiceOCPP" + suffix),
                [configured_version = version](const drogon::HttpRequestPtr &req,
                                               HttpCallback &&cb) {
                    std::string relates_to;
                    try {
                        if (!req->getHeader("origin").empty()) {
                            soap_failure(cb, 403, "browser_origin_not_allowed");
                            return;
                        }
                        if (!runtime->admit_api()) {
                            soap_failure(cb, 429, "request_rate_limit");
                            return;
                        }
                        if (!req->getHeader("content-type").starts_with("application/soap+xml")) {
                            soap_failure(cb, 415, "soap_12_required");
                            return;
                        }
                        auto version = configured_version;
                        std::optional<SoapCall> decoded;
                        if (version == "auto") {
                            for (const auto &candidate : {"1.2", "1.5", "1.6"}) {
                                try {
                                    decoded =
                                        runtime->soap_codecs.at(candidate)->decode(req->body());
                                    version = candidate;
                                    break;
                                } catch (const ProtocolError &) {
                                }
                            }
                            if (!decoded)
                                throw ProtocolError("FormationViolation", "Unknown SOAP namespace");
                        } else
                            decoded = runtime->soap_codecs.at(version)->decode(req->body());
                        auto call = std::move(*decoded);
                        relates_to = call.message_id;
                        if (!runtime->config.station_secrets.contains(call.station)) {
                            soap_failure(cb, 401, "unauthorized", relates_to);
                            return;
                        }
                        const auto expected =
                            "Basic " + drogon::utils::base64Encode(
                                           call.station + ":" +
                                           runtime->config.station_secrets.at(call.station)
                                               .get<std::string>());
                        if (!constant_equal(req->getHeader("authorization"), expected)) {
                            soap_failure(cb, 401, "unauthorized", relates_to);
                            return;
                        }
                        runtime->legacy_schemas.at(version)->validate(call.action, call.payload);
                        auto callback = std::make_shared<HttpCallback>(std::move(cb));
                        const auto station = call.station;
                        if (!runtime->executor.submit(station, [version, call = std::move(call),
                                                                callback]() mutable {
                                try {
                                    const auto message_id = call.message_id.empty()
                                                                ? drogon::utils::getUuid()
                                                                : call.message_id;
                                    Message message{2,
                                                    message_id,
                                                    call.action,
                                                    legacy_request(version, call.action,
                                                                   std::move(call.payload)),
                                                    {}};
                                    auto result = runtime->service.call(call.station, message,
                                                                        "soap" + version);
                                    result[2] =
                                        legacy_response(version, call.action, std::move(result[2]));
                                    runtime->legacy_schemas.at(version)->validate(call.action,
                                                                                  result[2], true);
                                    const auto wire = runtime->soap_codecs.at(version)->encode(
                                        call.action, result[2], call.message_id);
                                    auto response = drogon::HttpResponse::newHttpResponse();
                                    response->setStatusCode(drogon::k200OK);
                                    response->addHeader("Content-Type",
                                                        "application/soap+xml; charset=utf-8");
                                    response->addHeader("Cache-Control", "no-store");
                                    response->addHeader("X-Content-Type-Options", "nosniff");
                                    response->setBody(wire);
                                    (*callback)(response);
                                } catch (const ProtocolError &) {
                                    soap_failure(*callback, 400, "invalid_ocpp_request",
                                                 call.message_id);
                                } catch (const std::exception &) {
                                    soap_failure(*callback, 503, "service_unavailable",
                                                 call.message_id);
                                }
                            }))
                            soap_failure(*callback, 503, "server_busy");
                    } catch (const ProtocolError &) {
                        soap_failure(cb, 400, "invalid_soap_request", relates_to);
                    } catch (const std::exception &) {
                        soap_failure(cb, 503, "service_unavailable", relates_to);
                    }
                },
                {drogon::Post});
    }
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
                                        runtime->service.verify_schema();
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
            if (!authorize(req, cb,
                           (resource == "audit" || resource == "securityEvents" ||
                            resource == "certificateRequests" || resource == "reports201" ||
                            resource == "deviceModel201" || resource == "billingSandbox")
                               ? 3
                               : (resource == "tasks" ? 2 : 1)))
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
    app.registerHandler(
        "/api/v1/billing/sandbox/settle",
        [](const drogon::HttpRequestPtr &req, HttpCallback &&cb) {
            if (!authorize(req, cb, 3))
                return;
            try {
                auto request = request_json(req);
                auto callback = std::make_shared<HttpCallback>(std::move(cb));
                if (!runtime->executor.submit(
                        "billing-sandbox", [request = std::move(request), callback] {
                            try {
                                (*callback)(http(200, runtime->service.settle_sandbox(request)));
                            } catch (const ProtocolError &) {
                                failure(*callback, 400, "invalid_or_conflicting_sandbox_case");
                            } catch (const std::exception &) {
                                failure(*callback, 503, "sandbox_unavailable");
                            }
                        }))
                    failure(*callback, 503, "server_busy");
            } catch (const ProtocolError &) {
                failure(cb, 400, "invalid_sandbox_case");
            }
        },
        {drogon::Post});
    app.registerHandler("/api/v1/chargepoints/{1}/commands/{2}",
                        [](const drogon::HttpRequestPtr &req, HttpCallback &&cb,
                           std::string station, std::string action) {
                            const bool certificate_command =
                                action == "CertificateSigned" || action == "InstallCertificate" ||
                                action == "DeleteCertificate" || action == "SetNetworkProfile";
                            if (!authorize(req, cb, certificate_command ? 3 : 2))
                                return;
                            try {
                                if (!station_id_valid(station))
                                    throw ProtocolError("PropertyConstraintViolation",
                                                        "Invalid station");
                                auto payload = request_json(req);
                                check_command(station, action, payload);
                                auto callback = std::make_shared<HttpCallback>(std::move(cb));
                                const auto id = drogon::utils::getUuid();
                                command(station, action, std::move(payload), id,
                                        [callback, id](int status, ocpp::Json result) {
                                            auto response = http(status, result);
                                            response->addHeader("X-OCPP-Command-ID", id);
                                            (*callback)(response);
                                        });
                            } catch (const ProtocolError &) {
                                failure(cb, 400, "invalid_command_or_transfer_origin");
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
    // Exact POST paths shadow the generic GET route in Drogon. Register both
    // methods on those paths so station/tag reads remain reachable.
    for (const auto &resource : {std::string("chargepoints"), std::string("ocppTags")}) {
        app.registerHandler(
            "/api/v1/" + resource,
            [resource](const drogon::HttpRequestPtr &req, HttpCallback &&cb) {
                if (!authorize(req, cb, 1))
                    return;
                unsigned offset = 0;
                const auto input = req->getParameter("offset");
                if (!input.empty()) {
                    const auto [end, ec] =
                        std::from_chars(input.data(), input.data() + input.size(), offset);
                    if (ec != std::errc{} || end != input.data() + input.size()) {
                        failure(cb, 400, "invalid_offset");
                        return;
                    }
                }
                auto callback = std::make_shared<HttpCallback>(std::move(cb));
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
    }
    app.registerHandler("/api/v1/tasks/{1}",
                        [](const drogon::HttpRequestPtr &req, HttpCallback &&cb, std::string id) {
                            if (!authorize(req, cb, 2))
                                return;
                            auto callback = std::make_shared<HttpCallback>(std::move(cb));
                            if (!runtime->executor.submit("api-read", [id, callback] {
                                    try {
                                        (*callback)(http(200, runtime->service.task(id)));
                                    } catch (const ProtocolError &) {
                                        failure(*callback, 404, "task_not_found");
                                    } catch (const std::exception &) {
                                        failure(*callback, 503, "database_unavailable");
                                    }
                                }))
                                failure(*callback, 503, "server_busy");
                        },
                        {drogon::Get});
    app.registerHandler("/api/v1/idTokens",
                        [](const drogon::HttpRequestPtr &req, HttpCallback &&cb) {
                            if (!authorize(req, cb, 3))
                                return;
                            try {
                                auto payload = request_json(req);
                                auto callback = std::make_shared<HttpCallback>(std::move(cb));
                                if (!runtime->executor.submit(
                                        "token-admin", [payload = std::move(payload), callback] {
                                            try {
                                                runtime->service.upsert_token201(payload);
                                                (*callback)(http(200, {{"status", "updated"}}));
                                            } catch (const std::exception &) {
                                                failure(*callback, 400, "invalid_token");
                                            }
                                        }))
                                    failure(*callback, 503, "server_busy");
                            } catch (const std::exception &) {
                                failure(cb, 400, "invalid_json");
                            }
                        },
                        {drogon::Post});
    app.registerHandler("/api/v1/chargepoints/{1}/state",
                        [](const drogon::HttpRequestPtr &req, HttpCallback &&cb, std::string id) {
                            if (!authorize(req, cb, 1))
                                return;
                            auto callback = std::make_shared<HttpCallback>(std::move(cb));
                            if (!runtime->executor.submit("api-read", [id, callback] {
                                    try {
                                        (*callback)(http(200, runtime->service.station_state(id)));
                                    } catch (const ProtocolError &) {
                                        failure(*callback, 400, "invalid_station");
                                    } catch (const std::exception &) {
                                        failure(*callback, 503, "database_unavailable");
                                    }
                                }))
                                failure(*callback, 503, "server_busy");
                        },
                        {drogon::Get});
    app.registerPreSendingAdvice([](const drogon::HttpRequestPtr &req,
                                    const drogon::HttpResponsePtr &response) {
        if (response->statusCode() == drogon::k101SwitchingProtocols)
            response->addHeader("Sec-WebSocket-Protocol", req->getHeader("sec-websocket-protocol"));
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
        runtime->service.verify_schema();
        runtime->service.recover_commands();
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
