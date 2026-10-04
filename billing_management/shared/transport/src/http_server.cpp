#include "billing/transport/http_server.hpp"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <httplib.h>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>

namespace billing::transport {
namespace {
std::string environment(const char *key) {
#ifdef _WIN32
    char *value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, key) != 0)
        throw std::runtime_error("Environment read failed");
    std::unique_ptr<char, decltype(&std::free)> allocation(value, std::free);
#else
    const char *value = std::getenv(key);
#endif
    return value ? value : "";
}
bool equal_secret(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    unsigned char difference = 0;
    for (std::size_t i = 0; i < left.size(); ++i)
        difference |= static_cast<unsigned char>(left[i] ^ right[i]);
    return difference == 0;
}
void error(httplib::Response &response, int status, const char *message) {
    response.status = status;
    response.set_content(nlohmann::json{{"error", message}}.dump(), "application/json");
}
} // namespace

int run_http_server(std::string_view service, unsigned default_port, QuoteHandler quote,
                    RouteInstaller install) {
    try {
        unsigned port = default_port;
        const auto port_value = environment("BILLING_PORT");
        if (!port_value.empty()) {
            const auto parsed =
                std::from_chars(port_value.data(), port_value.data() + port_value.size(), port);
            if (parsed.ec != std::errc{} || parsed.ptr != port_value.data() + port_value.size() ||
                port < 1024 || port > 65535)
                throw std::runtime_error("Invalid BILLING_PORT");
        }
        const auto token = environment("BILLING_API_TOKEN");
        if (token.size() != 64 || !std::all_of(token.begin(), token.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            }))
            throw std::runtime_error(
                "BILLING_API_TOKEN must be a random 64-character lowercase hex secret");
        auto host = environment("BILLING_API_HOST");
        if (host.empty())
            host = "127.0.0.1:" + std::to_string(port);
        const auto origin = environment("BILLING_ALLOWED_ORIGIN");
        const auto reader_token = environment("BILLING_READER_TOKEN");
        const auto operator_token = environment("BILLING_OPERATOR_TOKEN");
        for (const auto &other : {reader_token, operator_token}) {
            if (!other.empty() && (other.size() != 64 || other == token ||
                                   !std::all_of(other.begin(), other.end(), [](char c) {
                                       return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                                   })))
                throw std::runtime_error("Invalid optional role token");
        }
        if (!reader_token.empty() && reader_token == operator_token)
            throw std::runtime_error("Role tokens must be distinct");
        if (host.find_first_of("\r\n /\\") != std::string::npos ||
            origin.find_first_of("\r\n") != std::string::npos)
            throw std::runtime_error("Invalid host/origin configuration");
        httplib::Server server;
        server.new_task_queue = [] { return new httplib::ThreadPool(2, 2, 32); };
        server.set_payload_max_length(4096);
        server.set_read_timeout(5, 0);
        server.set_write_timeout(5, 0);
        server.set_keep_alive_timeout(5);
        server.set_keep_alive_max_count(20);
        server.set_pre_routing_handler([&](const auto &request, auto &response) {
            response.set_header("Cache-Control", "no-store");
            response.set_header("X-Content-Type-Options", "nosniff");
            if (request.get_header_value("Host") != host) {
                error(response, 400, "Invalid host");
                return httplib::Server::HandlerResponse::Handled;
            }
            const auto request_origin = request.get_header_value("Origin");
            if (!request_origin.empty()) {
                if (origin.empty() || request_origin != origin) {
                    error(response, 403, "Origin denied");
                    return httplib::Server::HandlerResponse::Handled;
                }
                response.set_header("Access-Control-Allow-Origin", origin);
                response.set_header("Vary", "Origin");
            }
            if (install && request.path.starts_with("/api/v1/management")) {
                if (request.method == "OPTIONS") {
                    const auto method = request.get_header_value("Access-Control-Request-Method");
                    if (method != "GET" && method != "POST" && method != "PATCH" &&
                        method != "DELETE") {
                        error(response, 405, "Method denied");
                    } else {
                        response.status = 204;
                        response.set_header("Access-Control-Allow-Methods",
                                            "GET, POST, PATCH, DELETE");
                        response.set_header("Access-Control-Allow-Headers",
                                            "Authorization, Content-Type, Idempotency-Key");
                    }
                    return httplib::Server::HandlerResponse::Handled;
                }
                const auto auth = request.get_header_value("Authorization");
                const bool admin = equal_secret(auth, "Bearer " + token);
                const bool operator_role =
                    !operator_token.empty() && equal_secret(auth, "Bearer " + operator_token);
                const bool reader =
                    !reader_token.empty() && equal_secret(auth, "Bearer " + reader_token);
                if (!admin && !operator_role && !reader) {
                    error(response, 401, "Authentication required");
                    return httplib::Server::HandlerResponse::Handled;
                }
                if ((reader && request.method != "GET") ||
                    (!admin && (request.method == "DELETE" ||
                                request.path.find("/admins") != std::string::npos ||
                                request.path.find("/settings") != std::string::npos ||
                                request.path.find("/restore") != std::string::npos))) {
                    error(response, 403, "Insufficient role");
                    return httplib::Server::HandlerResponse::Handled;
                }
                response.set_header("X-Billing-Role", admin           ? "admin"
                                                      : operator_role ? "operator"
                                                                      : "reader");
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });
        server.Get("/health/ready", [service](const auto &, auto &response) {
            response.set_content(
                nlohmann::json{{"ready", true}, {"service", service}, {"mode", "sandbox"}}.dump(),
                "application/json");
        });
        server.Options("/api/v1/quote", [](const auto &request, auto &response) {
            if (request.get_header_value("Access-Control-Request-Method") != "POST") {
                error(response, 405, "Method denied");
                return;
            }
            response.status = 204;
            response.set_header("Access-Control-Allow-Methods", "POST");
            response.set_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
        });
        server.Post("/api/v1/quote", [&](const auto &request, auto &response) {
            if (!equal_secret(request.get_header_value("Authorization"), "Bearer " + token)) {
                response.set_header("WWW-Authenticate", "Bearer");
                error(response, 401, "Authentication required");
                return;
            }
            const auto content_type = request.get_header_value("Content-Type");
            if (content_type != "application/json" &&
                content_type != "application/json; charset=utf-8") {
                error(response, 415, "JSON required");
                return;
            }
            try {
                std::set<std::string> keys;
                const auto input = nlohmann::json::parse(
                    request.body, [&keys](int level, auto event, auto &value) {
                        if (level > 8)
                            throw std::invalid_argument("Too deep");
                        if (level == 1 && event == nlohmann::json::parse_event_t::key &&
                            !keys.insert(value.template get<std::string>()).second)
                            throw std::invalid_argument("Duplicate key");
                        return true;
                    });
                if (!input.is_object() || input.size() != 2 || !input.contains("energy_wh") ||
                    !input.contains("satang_per_kwh") || !input["energy_wh"].is_number_integer() ||
                    !input["satang_per_kwh"].is_number_integer() || input["energy_wh"] < 0 ||
                    input["energy_wh"] > 1'000'000'000 || input["satang_per_kwh"] < 0 ||
                    input["satang_per_kwh"] > 1'000'000) {
                    error(response, 400, "Invalid quote inputs");
                    return;
                }
                const auto result = quote(input["energy_wh"].template get<std::int32_t>(),
                                          input["satang_per_kwh"].template get<std::int32_t>());
                if (!result) {
                    error(response, 400, "Invalid quote inputs");
                    return;
                }
                response.set_content(nlohmann::json{{"mode", "sandbox"},
                                                    {"currency", "THB"},
                                                    {"subtotal_satang", result->subtotal_satang}}
                                         .dump(),
                                     "application/json");
            } catch (const std::exception &) {
                error(response, 400, "Invalid JSON");
            }
        });
        if (install)
            install(server);
        std::cout << service << " sandbox HTTP listening on loopback:" << port << '\n';
        if (!server.listen("127.0.0.1", static_cast<int>(port)))
            throw std::runtime_error("HTTP listener failed");
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
} // namespace billing::transport
