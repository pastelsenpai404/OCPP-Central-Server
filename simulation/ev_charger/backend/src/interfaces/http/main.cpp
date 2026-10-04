#include <cstdlib>
#include <fstream>
#include <httplib.h>
#include <iostream>
#include <regex>
#include <simulation/simulator.hpp>
#include <stdexcept>
namespace {
std::string env(const char *key, const std::string &fallback = "") {
#ifdef _WIN32
    char *value = nullptr;
    std::size_t size = 0;
    _dupenv_s(&value, &size, key);
    const std::string result = value ? value : fallback;
    std::free(value);
    return result;
#else
    const char *value = std::getenv(key);
    return value ? std::string(value) : fallback;
#endif
}
bool equal(const std::string &a, const std::string &b) {
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}
void json(httplib::Response &res, const simulation::Json &value, int status = 200) {
    res.status = status;
    res.set_content(value.dump(), "application/json");
}
} // namespace
int main() {
    try {
        const auto token = env("SIM_ADMIN_TOKEN");
        if (!std::regex_match(token, std::regex("[a-f0-9]{64}")))
            throw std::runtime_error("SIM_ADMIN_TOKEN must be 64 lowercase hex characters");
        const auto portText = env("SIM_PORT", "5600");
        if (!std::regex_match(portText, std::regex("[0-9]{4,5}")))
            throw std::runtime_error("Invalid port");
        int port = std::stoi(portText);
        if (port < 1024 || port > 65535)
            throw std::runtime_error("Invalid port");
        const auto host = env("SIM_API_HOST", "127.0.0.1:" + portText),
                   origin = env("SIM_PUBLIC_ORIGIN", "http://" + host);
        const auto endpoint = env("SIM_CSMS_URL");
        if (!endpoint.empty() &&
            !std::regex_match(
                endpoint, std::regex("(wss://[A-Za-z0-9.-]+(:[0-9]+)?|ws://"
                                     "(127\\.0\\.0\\.1|localhost)(:[0-9]+)?)/[A-Za-z0-9/_-]*/")))
            throw std::runtime_error("CSMS endpoint must be a WSS URL prefix or loopback WS prefix "
                                     "ending /; credentials in URL forbidden");
        auto secrets = simulation::Json::parse(env("SIM_STATION_SECRETS", "{}"));
        if (!secrets.is_object() || secrets.size() > 4)
            throw std::runtime_error("Invalid station secrets");
        for (const auto &[key, value] : secrets.items())
            if (!std::regex_match(key, std::regex("SIM[A-Z0-9_-]{1,24}")) || !value.is_string() ||
                !std::regex_match(value.get<std::string>(), std::regex("[a-f0-9]{64}")))
                throw std::runtime_error("Invalid station credential");
        simulation::Simulator simulator(endpoint, secrets, env("SIM_CA_FILE"));
        httplib::Server server;
        server.new_task_queue = [] { return new httplib::ThreadPool(4, 0, 32); };
        server.set_payload_max_length(8192);
        server.set_read_timeout(5, 0);
        server.set_write_timeout(5, 0);
        server.set_keep_alive_max_count(50);
        server.set_pre_routing_handler([&](const httplib::Request &req, httplib::Response &res) {
            res.set_header("Cache-Control", "no-store");
            res.set_header("X-Content-Type-Options", "nosniff");
            if (req.get_header_value("Host") != host) {
                json(res, {{"error", "Invalid host"}}, 400);
                return httplib::Server::HandlerResponse::Handled;
            }
            if (req.path.starts_with("/api/")) {
                if (!equal(req.get_header_value("Authorization"), "Bearer " + token)) {
                    json(res, {{"error", "Authentication required"}}, 401);
                    return httplib::Server::HandlerResponse::Handled;
                }
                if (req.method != "GET" && req.get_header_value("Origin") != origin) {
                    json(res, {{"error", "Invalid origin"}}, 403);
                    return httplib::Server::HandlerResponse::Handled;
                }
                if (req.method == "POST" &&
                    req.get_header_value("Content-Type") != "application/json") {
                    json(res, {{"error", "Use application/json"}}, 415);
                    return httplib::Server::HandlerResponse::Handled;
                }
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });
        server.Get("/health/ready", [](const auto &, auto &res) {
            json(res, {{"status", "ready"}, {"service", "ev-charger-simulation"}});
        });
        server.Get("/api/v1/chargers",
                   [&](const auto &, auto &res) { json(res, simulator.snapshot()); });
        server.Get("/api/v1/events",
                   [&](const auto &, auto &res) { json(res, simulator.events()); });
        const auto mutate = [&](const httplib::Request &req, httplib::Response &res, bool create) {
            try {
                auto body = simulation::parse(req.body);
                json(res,
                     create ? simulator.create(body) : simulator.action(req.matches[1].str(), body),
                     create ? 201 : 202);
            } catch (const std::exception &) {
                json(res, {{"error", "Invalid request, station state or configured capability"}},
                     400);
            }
        };
        server.Post("/api/v1/chargers",
                    [&](const auto &req, auto &res) { mutate(req, res, true); });
        server.Post(R"(/api/v1/chargers/(SIM[A-Z0-9_-]{1,24})/actions)",
                    [&](const auto &req, auto &res) { mutate(req, res, false); });
        const auto web = env("SIM_WEB_ROOT");
        if (!web.empty()) {
            if (!server.set_mount_point("/", web))
                throw std::runtime_error("Frontend build missing");
            std::ifstream file(web + "/csp.txt");
            std::string csp;
            std::getline(file, csp);
            if (csp.empty() || csp.find('\r') != std::string::npos)
                throw std::runtime_error("Frontend CSP file missing");
            server.set_default_headers({{"Content-Security-Policy", csp}});
        }
        server.set_exception_handler([](const auto &, auto &res, std::exception_ptr) {
            json(res, {{"error", "Internal error"}}, 500);
        });
        std::cout << "EV simulator listening on 127.0.0.1:" << port
                  << " (maximum 4 stations, in-memory state)\n";
        if (!server.listen("127.0.0.1", port))
            throw std::runtime_error("Cannot bind loopback port");
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
