#include "ocpp/security.hpp"
#include <cstdlib>
#include <set>

namespace ocpp {
bool constant_equal(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    volatile unsigned char difference = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        difference =
            static_cast<unsigned char>(difference | static_cast<unsigned char>(a[i] ^ b[i]));
    return difference == 0;
}
bool strong_secret(std::string_view s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
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
    config.db_name = env("OCPP_DB_NAME");
    config.db_user = env("OCPP_DB_USER");
    config.db_password = env("OCPP_DB_PASSWORD");
    const auto host = env("OCPP_DB_HOST", false);
    if (!host.empty())
        config.db_host = host;
    config.db_port = static_cast<unsigned short>(bounded_env("OCPP_DB_PORT", 3306, 65535));
    config.db_ca = env("OCPP_DB_CA", false);
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
RateLimit::RateLimit(double capacity, double per_second)
    : tokens_(capacity), capacity_(capacity), per_second_(per_second),
      last_(std::chrono::steady_clock::now()) {}
bool RateLimit::allow() {
    const auto now = std::chrono::steady_clock::now();
    tokens_ = std::min(capacity_,
                       tokens_ + std::chrono::duration<double>(now - last_).count() * per_second_);
    last_ = now;
    if (tokens_ < 1)
        return false;
    tokens_ -= 1;
    return true;
}
} // namespace ocpp
