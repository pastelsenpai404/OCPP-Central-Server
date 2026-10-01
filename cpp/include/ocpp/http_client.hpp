#pragma once
#include <string>
#include <string_view>
namespace ocpp {
struct HttpResult {
    int status;
    std::string body;
};
// Exact origin must already have been authorized by the caller. No redirects/proxy.
HttpResult post_http(const std::string &url, std::string_view content_type,
                     const std::string &authorization, const std::string &body);
std::string soap_origin(std::string_view url);
} // namespace ocpp
