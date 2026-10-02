#include "billing/domain/quote.hpp"
#include <emscripten/emscripten.h>

// The bounded integer result is exactly representable by a JavaScript Number.
// A negative result indicates invalid inputs; never treat it as a monetary value.
extern "C" EMSCRIPTEN_KEEPALIVE double billing_preview_satang(int energy_wh,
                                                             int satang_per_kwh) {
    const auto quote = billing::domain::preview_quote(energy_wh, satang_per_kwh);
    return quote ? static_cast<double>(quote->subtotal_satang) : -1.0;
}
