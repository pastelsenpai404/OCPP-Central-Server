#include "ocpp/config/config.hpp"
#include "ocpp/protocol/commands.hpp"
#include "ocpp/security/credentials.hpp"
#include "ocpp/transport/http_client.hpp"
#include <algorithm>
#include <cstdlib>
#include <memory>

namespace ocpp {
static std::string env(const char *key, bool required = true) {
#ifdef _WIN32
    char *value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, key) != 0)
        throw std::runtime_error("Environment read failed");
    std::unique_ptr<char, decltype(&std::free)> allocation(value, std::free);
#else
    const char *value = std::getenv(key);
#endif
    if (!value || !*value) {
        if (required)
            throw std::runtime_error(std::string("Missing environment variable: ") + key);
        return {};
    }
    return value;
}
static unsigned bounded_env(const char *key, unsigned fallback, unsigned maximum) {
    const auto value = env(key, false);
    if (value.empty())
        return fallback;
    if (value.size() > 5 ||
        !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
        throw std::runtime_error(std::string("Invalid ") + key);
    const auto n = std::stoul(value);
    if (n == 0 || n > maximum)
        throw std::runtime_error(std::string("Invalid ") + key);
    return static_cast<unsigned>(n);
}
Config Config::environment() {
    Config config;
    config.port = static_cast<unsigned short>(bounded_env("OCPP_PORT", 5003, 65535));
    config.workers = bounded_env("OCPP_WORKERS", 4, 32);
    config.public_origin = env("OCPP_PUBLIC_ORIGIN", false);
    if (!config.public_origin.empty() &&
        transfer_origin(config.public_origin) != config.public_origin)
        throw std::runtime_error("OCPP_PUBLIC_ORIGIN must be an exact HTTPS origin without a path");
    config.db_name = env("OCPP_DB_NAME");
    config.db_user = env("OCPP_DB_USER");
    config.db_password = env("OCPP_DB_PASSWORD");
    const auto host = env("OCPP_DB_HOST", false);
    if (!host.empty())
        config.db_host = host;
    config.db_port = static_cast<unsigned short>(bounded_env("OCPP_DB_PORT", 3306, 65535));
    config.db_ca = env("OCPP_DB_CA", false);
    const auto origins = env("OCPP_TRANSFER_ORIGINS", false);
    if (!origins.empty()) {
        if (origins.size() > 8192)
            throw std::runtime_error("Transfer origin configuration too large");
        const auto entries = Json::parse(origins);
        if (!entries.is_array() || entries.size() > 32)
            throw std::runtime_error("Invalid transfer origins");
        for (const auto &entry : entries) {
            if (!entry.is_string())
                throw std::runtime_error("Invalid transfer origin");
            const auto origin = entry.get<std::string>();
            if (transfer_origin(origin) != origin)
                throw std::runtime_error("Configure an exact HTTPS origin without a path");
            config.transfer_origins.insert(origin);
        }
    }
    if (config.db_host != "127.0.0.1" && config.db_host != "localhost" && config.db_ca.empty())
        throw std::runtime_error("Remote database requires OCPP_DB_CA");
    config.read_token = env("OCPP_READ_TOKEN");
    config.operator_token = env("OCPP_OPERATOR_TOKEN");
    config.admin_token = env("OCPP_ADMIN_TOKEN");
    for (const auto *s : {&config.read_token, &config.operator_token, &config.admin_token})
        if (!strong_secret(*s))
            throw std::runtime_error("API tokens must be 32 random bytes encoded as lowercase hex");
    if (config.read_token == config.operator_token || config.read_token == config.admin_token ||
        config.operator_token == config.admin_token)
        throw std::runtime_error("Roles require distinct tokens");
    const auto secrets = env("OCPP_STATION_SECRETS");
    if (secrets.size() > 1024 * 1024)
        throw std::runtime_error("Station credential configuration too large");
    config.station_secrets = Json::parse(secrets);
    if (!config.station_secrets.is_object() || config.station_secrets.empty() ||
        config.station_secrets.size() > 10000)
        throw std::runtime_error("Invalid station credentials");
    std::set<std::string> unique;
    for (auto it = config.station_secrets.begin(); it != config.station_secrets.end(); ++it)
        if (!station_id_valid(it.key()) || !it.value().is_string() ||
            !strong_secret(it.value().get<std::string>()) ||
            !unique.insert(it.value().get<std::string>()).second)
            throw std::runtime_error(
                "Each station needs an ID and a distinct random 32-byte hex secret");
    const auto soap_origins = env("OCPP_SOAP_ORIGINS", false);
    if (!soap_origins.empty()) {
        if (soap_origins.size() > 8192)
            throw std::runtime_error("SOAP origin configuration too large");
        const auto entries = Json::parse(soap_origins);
        if (!entries.is_array() || entries.size() > 32)
            throw std::runtime_error("Invalid SOAP origins");
        for (const auto &entry : entries) {
            const auto origin = entry.get<std::string>();
            if (soap_origin(origin) != origin)
                throw std::runtime_error("SOAP origin must be exact");
            config.soap_origins.insert(origin);
        }
    }
    const auto endpoints = env("OCPP_SOAP_ENDPOINTS", false);
    if (!endpoints.empty()) {
        if (endpoints.size() > 1024 * 1024)
            throw std::runtime_error("SOAP endpoint configuration too large");
        config.soap_endpoints = Json::parse(endpoints);
        if (!config.soap_endpoints.is_object() || config.soap_endpoints.size() > 10000)
            throw std::runtime_error("Invalid SOAP endpoints");
        for (const auto &[station, endpoint] : config.soap_endpoints.items()) {
            if (!config.station_secrets.contains(station) || !endpoint.is_object() ||
                endpoint.size() != 2 || !endpoint.contains("version") || !endpoint.contains("url"))
                throw std::runtime_error("Invalid SOAP endpoint");
            const auto version = endpoint.at("version").get<std::string>();
            if (version != "1.2" && version != "1.5" && version != "1.6")
                throw std::runtime_error("Invalid SOAP version");
            if (!config.soap_origins.contains(soap_origin(endpoint.at("url").get<std::string>())))
                throw std::runtime_error("SOAP endpoint origin is not configured");
        }
    }
    return config;
}
int Config::role(std::string_view authorization) const {
    if (!authorization.starts_with("Bearer "))
        return 0;
    const auto token = authorization.substr(7);
    if (token.size() != 64)
        return 0;
    const bool admin = constant_equal(token, admin_token),
               op = constant_equal(token, operator_token), read = constant_equal(token, read_token);
    return admin ? 3 : op ? 2 : read ? 1 : 0;
}
bool Config::browser_origin_allowed(std::string_view origin, std::string_view host,
                                    bool secure) const {
    if (!public_origin.empty()) {
        return host == std::string_view(public_origin).substr(8) &&
               (origin.empty() || origin == public_origin);
    }
    return origin.empty() ||
           origin == std::string(secure ? "https://" : "http://") + std::string(host);
}
} // namespace ocpp
