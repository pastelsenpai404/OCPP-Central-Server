#include "billing/domain/quote.hpp"

namespace billing::domain {
std::optional<Quote> preview_quote(std::int32_t energy_wh,
                                   std::int32_t satang_per_kwh) noexcept {
    if (energy_wh < 0 || energy_wh > 1'000'000'000 ||
        satang_per_kwh < 0 || satang_per_kwh > 1'000'000) {
        return std::nullopt;
    }
    const auto numerator = static_cast<std::int64_t>(energy_wh) * satang_per_kwh;
    return Quote{(numerator + 500) / 1000};
}
} // namespace billing::domain
