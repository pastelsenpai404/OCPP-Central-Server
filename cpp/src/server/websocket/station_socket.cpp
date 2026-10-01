#include "server/runtime.hpp"
#include <drogon/HttpFilter.h>
#include <drogon/WebSocketController.h>

namespace ocpp::server {
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
                          "ocpp::server::StationAuth");
    WS_ADD_PATH_VIA_REGEX("^/ocpp/[A-Za-z0-9_-]{1,64}$", "ocpp::server::StationAuth");
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

} // namespace ocpp::server
