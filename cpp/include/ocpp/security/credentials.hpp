#pragma once
#include <chrono>
#include <set>
#include <string>
#include <string_view>

namespace ocpp {
bool constant_equal(std::string_view a, std::string_view b) noexcept;
bool strong_secret(std::string_view secret);
class RateLimit {
    double tokens_, capacity_, per_second_;
    std::chrono::steady_clock::time_point last_;

  public:
    RateLimit(double capacity, double per_second);
    bool allow();
};
} // namespace ocpp
