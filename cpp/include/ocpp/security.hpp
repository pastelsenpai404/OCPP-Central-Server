#pragma once
#include "protocol.hpp"
#include <chrono>
#include <string>
#include <string_view>

namespace ocpp {
bool constant_equal(std::string_view a, std::string_view b) noexcept;
bool strong_secret(std::string_view secret);
struct Config {
    std::string bind = "127.0.0.1";
    unsigned short port = 5003;
    unsigned workers = 4;
    std::string db_host = "127.0.0.1", db_name, db_user, db_password, db_ca;
    unsigned short db_port = 3306;
    Json station_secrets;
    std::string read_token, operator_token, admin_token;
    static Config environment();
    int role(std::string_view authorization) const;
};
class RateLimit {
    double tokens_, capacity_, per_second_;
    std::chrono::steady_clock::time_point last_;

  public:
    RateLimit(double capacity, double per_second);
    bool allow();
};
} // namespace ocpp
