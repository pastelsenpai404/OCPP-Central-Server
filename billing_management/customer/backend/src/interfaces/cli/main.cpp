#include "billing/customer/application/quote_preview.hpp"
#include <charconv>
#include <iostream>
#include <string_view>
#include <system_error>

namespace {
bool parse_integer(std::string_view text, std::int32_t &value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: billing_customer_backend ENERGY_WH SATANG_PER_KWH\n"
                     "Sandbox quote only; no database or payment operations.\n";
        return 2;
    }
    std::int32_t energy{}, rate{};
    if (!parse_integer(argv[1], energy) || !parse_integer(argv[2], rate)) {
        std::cerr << "Inputs must be bounded integers.\n";
        return 2;
    }
    const auto quote = billing::customer::application::quote_preview(energy, rate);
    if (!quote) {
        std::cerr << "Inputs outside allowed range.\n";
        return 2;
    }
    std::cout << "{\"mode\":\"sandbox\",\"currency\":\"THB\",\"subtotal_satang\":"
              << quote->subtotal_satang << "}\n";
    return 0;
}
