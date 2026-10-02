#include "billing/customer/application/quote_preview.hpp"
#include "billing/transport/http_server.hpp"

int main() {
    return billing::transport::run_http_server("billing-customer", 5501,
                                              billing::customer::application::quote_preview);
}
