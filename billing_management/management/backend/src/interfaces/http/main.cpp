#include "billing/management/application/quote_preview.hpp"
#include "billing/management/infrastructure/database.hpp"
#include "billing/management/infrastructure/ocpp_client.hpp"
#include "billing/management/interfaces/routes.hpp"
#include "billing/transport/http_server.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

int main() {
    try {
#ifdef _WIN32
        char *raw = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&raw, &length, "BILLING_DATABASE_PATH") != 0)
            throw std::runtime_error("Environment read failed");
        std::unique_ptr<char, decltype(&std::free)> allocation(raw, std::free);
#else
        const char *raw = std::getenv("BILLING_DATABASE_PATH");
#endif
        billing::management::Database database(raw && *raw ? raw : "billing-management.sqlite3");
        billing::management::Backoffice service(database);
        billing::management::OcppClient registry;
        std::jthread sync_worker([&](std::stop_token stop) {
            while (!stop.stop_requested()) {
                bool worked = false;
                if (registry.enabled()) {
                    try {
                        worked = service.sync_charger(registry);
                    } catch (const std::exception &) {
                        std::cerr << "OCPP sync unavailable; pending jobs retained\n";
                    }
                }
                for (int i = 0; i < (worked ? 2 : 20) && !stop.stop_requested(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        return billing::transport::run_http_server(
            "billing-management", 5500, billing::management::application::quote_preview,
            [&](httplib::Server &server) { billing::management::install_routes(server, service); });
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
