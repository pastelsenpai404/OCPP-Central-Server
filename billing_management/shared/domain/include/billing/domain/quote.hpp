#pragma once
#include <cstdint>
#include <optional>

namespace billing::domain {
struct Quote {
    std::int64_t subtotal_satang;
};

// Sandbox energy subtotal only, excluding tax, discounts and payment fees.
// Quantities use integers: Wh and satang/kWh. Round half up once at the end.
[[nodiscard]] std::optional<Quote> preview_quote(std::int32_t energy_wh,
                                                std::int32_t satang_per_kwh) noexcept;
} // namespace billing::domain
