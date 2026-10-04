#include "billing/management/infrastructure/ocpp_client.hpp"
#include "billing/management/domain/modules.hpp"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <httplib.h>
#include <memory>
namespace billing::management {
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
} // namespace
OcppClient::OcppClient() {
    token_ = environment("BILLING_OCPP_TOKEN");
    host_ = environment("BILLING_OCPP_HOST");
    if (!token_.empty() &&
        (token_.size() != 64 || !std::all_of(token_.begin(), token_.end(), [](char c) {
             return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         })))
        throw std::runtime_error("Invalid BILLING_OCPP_TOKEN");
    if (host_.empty())
        host_ = "127.0.0.1:5003";
    if (host_.size() > 253 ||
        host_.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:") !=
            std::string::npos)
        throw std::runtime_error("Invalid BILLING_OCPP_HOST");
    const auto port = environment("BILLING_OCPP_PORT");
    if (!port.empty()) {
        const auto parsed = std::from_chars(port.data(), port.data() + port.size(), port_);
        if (parsed.ec != std::errc{} || parsed.ptr != port.data() + port.size() || port_ < 1024 ||
            port_ > 65535)
            throw std::runtime_error("Invalid BILLING_OCPP_PORT");
    }
}
RegistryResult OcppClient::register_charger(const std::string &code) {
    if (!enabled())
        return {false, false, "integration_not_configured"};
    // Fixed loopback destination prevents a browser-supplied URL from leaking server credentials.
    httplib::Client client("127.0.0.1", static_cast<int>(port_));
    client.set_connection_timeout(2, 0);
    client.set_read_timeout(3, 0);
    client.set_write_timeout(3, 0);
    client.set_follow_location(false);
    const httplib::Headers headers = {{"Host", host_}, {"Authorization", "Bearer " + token_}};
    const auto result = client.Post("/api/v1/admin/chargepoints/register", headers,
                                    Json{{"chargeBoxId", code}}.dump(), "application/json");
    if (!result)
        return {false, false, "csms_unavailable"};
    if (result->status != 200 && result->status != 201)
        return {false, false, "csms_rejected"};
    try {
        const auto response = Json::parse(result->body);
        if (response.value("chargeBoxId", std::string()) != code ||
            response.value("registryOnly", false) != true)
            return {false, false, "invalid_csms_response"};
        return {true, response.value("credentialsConfigured", false), ""};
    } catch (const std::exception &) {
        return {false, false, "invalid_csms_response"};
    }
}
} // namespace billing::management
