#include "ocpp/security/credentials.hpp"
#include <algorithm>

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
