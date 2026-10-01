#include "server/runtime.hpp"

namespace ocpp::server {
void register_soap_routes() {
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
}
} // namespace ocpp::server
