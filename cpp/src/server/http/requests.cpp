#include "server/runtime.hpp"

namespace ocpp::server {
bool authorize(const drogon::HttpRequestPtr &req, const HttpCallback &cb, int role) {
    if (runtime->config.role(req->getHeader("authorization")) < role) {
        failure(cb, 401, "unauthorized");
        return false;
    }
    const auto &origin = req->getHeader("origin");
    const auto expected_origin =
        std::string(req->isOnSecureConnection() ? "https://" : "http://") + req->getHeader("host");
    if ((!origin.empty() && origin != expected_origin) ||
        req->getHeader("sec-fetch-site") == "cross-site") {
        failure(cb, 403, "browser_origin_not_allowed");
        return false;
    }
    if (!runtime->admit_api()) {
        failure(cb, 429, "request_rate_limit");
        return false;
    }
    return true;
}
ocpp::Json request_json(const drogon::HttpRequestPtr &req) {
    if (req->getHeader("content-type").find("application/json") != 0)
        throw ProtocolError("FormationViolation", "JSON content type required");
    // Reuse the bounded parser, including duplicate-key and depth rejection.
    const auto body = std::string(req->body());
    auto parsed = parse("[2,\"api\",\"Api\"," + body + "]");
    return parsed.payload;
}

} // namespace ocpp::server
