#include "server/bootstrap.hpp"
#include "server/runtime.hpp"
#include <iostream>

namespace ocpp::server {
int run_server(int argc, char **argv) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: ocpp_server <schema-directory>\n";
            return 2;
        }
        runtime = std::make_unique<Runtime>(Config::environment(), argv[1]);
        runtime->service.verify_schema();
        runtime->service.recover_commands();
        std::cerr << "Runtime initialized\n";
        auto &app = drogon::app();
        app.setLogLevel(trantor::Logger::kWarn)
            .setThreadNum(runtime->config.workers)
            .addListener(runtime->config.bind, runtime->config.port)
            .setMaxConnectionNum(12000)
            .setMaxConnectionNumPerIP(12000)
            .setClientMaxBodySize(65536)
            .setClientMaxMemoryBodySize(65536)
            .setClientMaxWebSocketMessageSize(65536)
            .setIdleConnectionTimeout(90)
            .setKeepaliveRequestsNumber(100)
            .setPipeliningRequestsNumber(2)
            .setServerHeaderField("");
        app.disableSession();
        register_routes();
        std::cerr << "Routes initialized; starting listener\n";
        app.run();
        runtime.reset();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Startup failed: " << e.what() << '\n';
        return 1;
    }
}

} // namespace ocpp::server
