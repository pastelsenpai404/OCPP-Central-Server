#include "admin_assets.hpp"
#include "server/runtime.hpp"
#include <charconv>

namespace ocpp::server {
void register_admin_routes() {
    auto &app = drogon::app();
    // Registry-only integration: authentication still requires configured station credentials.
    // Keep the existing /chargepoints provisioning contract unchanged.
    app.registerHandler(
        "/api/v1/admin/chargepoints/register",
        [](const drogon::HttpRequestPtr &req, HttpCallback &&cb) {
            if (!authorize(req, cb, 3))
                return;
            try {
                const auto p = request_json(req);
                if (!p.is_object() || p.size() != 1 || !p.contains("chargeBoxId") ||
                    !p["chargeBoxId"].is_string()) {
                    failure(cb, 400, "invalid_request");
                    return;
                }
                const auto id = p["chargeBoxId"].get<std::string>();
                if (!station_id_valid(id)) {
                    failure(cb, 400, "invalid_station_id");
                    return;
                }
                const bool configured = runtime->config.station_secrets.contains(id);
                const auto callback = std::make_shared<HttpCallback>(std::move(cb));
                if (!runtime->executor.submit(id, [id, configured, callback] {
                        try {
                            runtime->service.provision_station(id);
                            (*callback)(http(201, {{"chargeBoxId", id},
                                                   {"registryOnly", true},
                                                   {"credentialsConfigured", configured}}));
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
    for (const auto &path : {std::string("/"), std::string("/admin"), std::string("/admin/"),
                             std::string("/admin/app.js"), std::string("/admin/styles.css")}) {
        app.registerHandler(
            path,
            [path](const drogon::HttpRequestPtr &, HttpCallback &&cb) {
                auto response = drogon::HttpResponse::newHttpResponse();
                response->setStatusCode(drogon::k200OK);
                const bool js = path.ends_with(".js"), css = path.ends_with(".css");
                response->addHeader("Content-Type", js    ? "text/javascript; charset=utf-8"
                                                    : css ? "text/css; charset=utf-8"
                                                          : "text/html; charset=utf-8");
                response->addHeader("Cache-Control", "no-store");
                response->addHeader("X-Content-Type-Options", "nosniff");
                response->addHeader("Referrer-Policy", "no-referrer");
                response->addHeader(
                    "Content-Security-Policy",
                    "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; "
                    "frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
                response->setBody(std::string(js ? ui::js : css ? ui::css : ui::html));
                cb(response);
            },
            {drogon::Get});
    }
    app.registerHandler(
        "/api/v1/admin/session",
        [](const drogon::HttpRequestPtr &req, HttpCallback &&cb) {
            if (!authorize(req, cb, 1))
                return;
            const int role = runtime->config.role(req->getHeader("authorization"));
            ocpp::Json stations = ocpp::Json::array(), configured = ocpp::Json::array();
            {
                std::lock_guard lock(runtime->mutex);
                for (const auto &[id, session] : runtime->sessions)
                    stations.push_back({{"station", id}, {"protocol", session->protocol}});
            }
            if (role == 3)
                for (const auto &[id, secret] : runtime->config.station_secrets.items()) {
                    static_cast<void>(secret);
                    configured.push_back(id);
                }
            cb(http(200, {{"role", role},
                          {"onlineStations", stations},
                          {"configuredStations", configured},
                          {"soapStations", [&] {
                               ocpp::Json ids = ocpp::Json::array();
                               for (const auto &[id, endpoint] :
                                    runtime->config.soap_endpoints.items()) {
                                   static_cast<void>(endpoint);
                                   ids.push_back(id);
                               }
                               return ids;
                           }()}}));
        },
        {drogon::Get});
    app.registerHandler("/api/v1/admin/commands",
                        [](const drogon::HttpRequestPtr &req, HttpCallback &&cb) {
                            if (!authorize(req, cb, 1))
                                return;
                            cb(http(200, runtime->command_catalog));
                        },
                        {drogon::Get});
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
                            resource == "deviceModel201" || resource == "billingSandbox" ||
                            resource == "idTokens")
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
    app.registerHandler(
        "/api/v1/idTokens",
        [](const drogon::HttpRequestPtr &req, HttpCallback &&cb) {
            if (!authorize(req, cb, 3))
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
            if (!runtime->executor.submit("api-read", [offset, callback] {
                    try {
                        (*callback)(http(200, runtime->service.overview("idTokens", offset)));
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
}
} // namespace ocpp::server
