#include "server/runtime.hpp"

namespace ocpp::server {
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
} // namespace ocpp::server
