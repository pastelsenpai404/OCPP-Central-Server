#include "server/runtime.hpp"

namespace ocpp::server {
drogon::HttpResponsePtr http(int status, const ocpp::Json &body) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(static_cast<drogon::HttpStatusCode>(status));
    response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    response->setBody(body.dump());
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("X-Content-Type-Options", "nosniff");
    return response;
}
void failure(const HttpCallback &cb, int code, const char *message) {
    cb(http(code, {{"error", message}}));
}
void soap_failure(const HttpCallback &cb, int code, const char *message,
                  std::string_view relates_to) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(static_cast<drogon::HttpStatusCode>(code));
    response->addHeader("Content-Type", "application/soap+xml; charset=utf-8");
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("X-Content-Type-Options", "nosniff");
    response->setBody(soap_fault(code < 500 && code != 429, message, relates_to));
    cb(response);
}

} // namespace ocpp::server
