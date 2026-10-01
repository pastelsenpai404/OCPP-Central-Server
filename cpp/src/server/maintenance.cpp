#include "server/runtime.hpp"

namespace ocpp::server {
void register_maintenance() {
    auto &app = drogon::app();
    app.registerPreSendingAdvice([](const drogon::HttpRequestPtr &req,
                                    const drogon::HttpResponsePtr &response) {
        if (response->statusCode() == drogon::k101SwitchingProtocols)
            response->addHeader("Sec-WebSocket-Protocol", req->getHeader("sec-websocket-protocol"));
        response->addHeader("X-Content-Type-Options", "nosniff");
    });
    app.getLoop()->runEvery(1.0, [] {
        std::vector<std::shared_ptr<Session>> sessions;
        {
            std::lock_guard lock(runtime->mutex);
            for (const auto &[id, s] : runtime->sessions) {
                static_cast<void>(id);
                sessions.push_back(s);
            }
        }
        const auto now = std::chrono::steady_clock::now();
        for (const auto &s : sessions) {
            std::vector<Completion> expired;
            {
                std::lock_guard lock(s->mutex);
                for (auto it = s->pending.begin(); it != s->pending.end();) {
                    if (it->second.deadline <= now) {
                        expired.push_back(std::move(it->second.complete));
                        it = s->pending.erase(it);
                    } else
                        ++it;
                }
            }
            for (auto &done : expired)
                done(504, {{"error", "station_timeout"}});
        }
    });
}
} // namespace ocpp::server
