#pragma once
#include "billing/domain/quote.hpp"
#include <string_view>

namespace billing::transport {
using QuoteHandler = std::optional<domain::Quote> (*)(std::int32_t, std::int32_t) noexcept;
int run_http_server(std::string_view service, unsigned default_port, QuoteHandler quote);
} // namespace billing::transport
