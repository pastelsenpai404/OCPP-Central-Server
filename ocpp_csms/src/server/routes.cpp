#include "server/runtime.hpp"

namespace ocpp::server {
void register_routes() {
    register_station_transport();
    register_soap_routes();
    register_admin_routes();
    register_maintenance();
}
} // namespace ocpp::server
