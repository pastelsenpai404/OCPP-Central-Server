#include "billing/domain/quote.hpp"
#include <iostream>
#include <limits>

int main() {
    using billing::domain::preview_quote;
    unsigned failures = 0;
    const auto expect = [&failures](bool condition) {
        if (!condition) ++failures;
    };
    expect(preview_quote(1500, 750)->subtotal_satang == 1125);
    expect(preview_quote(1, 499)->subtotal_satang == 0);
    expect(preview_quote(1, 500)->subtotal_satang == 1);
    expect(preview_quote(0, 750)->subtotal_satang == 0);
    expect(preview_quote(1000, 0)->subtotal_satang == 0);
    expect(!preview_quote(-1, 750));
    expect(!preview_quote(1000, -1));
    expect(!preview_quote(1'000'000'001, 750));
    expect(!preview_quote(1000, 1'000'001));
    expect(!preview_quote(std::numeric_limits<int>::max(), 750));
    expect(preview_quote(1'000'000'000, 1'000'000)->subtotal_satang == 1'000'000'000'000LL);
    if (failures) std::cerr << failures << " quote checks failed\n";
    return failures ? 1 : 0;
}
