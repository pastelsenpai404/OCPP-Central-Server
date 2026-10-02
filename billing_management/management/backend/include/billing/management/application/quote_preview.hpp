#pragma once
#include "billing/domain/quote.hpp"

namespace billing::management::application {
[[nodiscard]] std::optional<domain::Quote> quote_preview(std::int32_t energy_wh,
                                                        std::int32_t satang_per_kwh) noexcept;
} // namespace billing::management::application
