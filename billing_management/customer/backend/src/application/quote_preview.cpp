#include "billing/customer/application/quote_preview.hpp"

namespace billing::customer::application {
std::optional<domain::Quote> quote_preview(std::int32_t energy_wh,
                                         std::int32_t satang_per_kwh) noexcept {
    return domain::preview_quote(energy_wh, satang_per_kwh);
}
} // namespace billing::customer::application
