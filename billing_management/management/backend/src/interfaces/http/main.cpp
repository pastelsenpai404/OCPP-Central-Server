#include "billing/management/application/quote_preview.hpp"
#include "billing/transport/http_server.hpp"

int main() {
    return billing::transport::run_http_server("billing-management", 5500,
                                              billing::management::application::quote_preview);
}
