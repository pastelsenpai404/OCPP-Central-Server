#pragma once
#include "billing/domain/quote.hpp"
#include <functional>
#include <string_view>
namespace httplib {
class Server;
}

namespace billing::transport {
using QuoteHandler = std::optional<domain::Quote> (*)(std::int32_t, std::int32_t) noexcept;
using RouteInstaller = std::function<void(httplib::Server &)>;
int run_http_server(std::string_view service, unsigned default_port, QuoteHandler quote,
                    RouteInstaller install = {});
} // namespace billing::transport
