#include "ocpp/billing/settlement.hpp"
#include <charconv>
#include <chrono>
#include <limits>
#include <set>
namespace ocpp {
namespace {
[[noreturn]] void invalid() {
    throw ProtocolError("PropertyConstraintViolation", "Invalid sandbox billing input");
}
std::int64_t bounded(const Json &request, const char *name, std::int64_t maximum) {
    if (!request.contains(name) || !request.at(name).is_number_integer())
        invalid();
    const auto value = request.at(name).get<std::int64_t>();
    if (value < 0 || value > maximum)
        invalid();
    return value;
}
std::int64_t meter(const Json &reading) {
    if (!reading.is_object() || reading.size() != 2 || !reading.contains("value") ||
        !reading.at("value").is_string() || !reading.contains("unit") ||
        !reading.at("unit").is_string())
        invalid();
    const auto &value = reading.at("value").get_ref<const std::string &>();
    const auto &unit = reading.at("unit").get_ref<const std::string &>();
    if (value.empty() || value.size() > 32 || (unit != "Wh" && unit != "kWh"))
        invalid();
    const auto decimal = value.find('.');
    const auto whole = std::string_view(value).substr(0, decimal);
    const auto fraction =
        decimal == value.npos ? std::string_view() : std::string_view(value).substr(decimal + 1);
    const std::size_t digits = unit == "Wh" ? 3 : 6;
    if (whole.empty() || fraction.size() > digits || (decimal != value.npos && fraction.empty()))
        invalid();
    for (const auto text : {whole, fraction})
        if (std::any_of(text.begin(), text.end(), [](char c) { return c < '0' || c > '9'; }))
            invalid();
    std::int64_t integer = 0;
    const auto parsed = std::from_chars(whole.data(), whole.data() + whole.size(), integer);
    if (parsed.ec != std::errc{} || parsed.ptr != whole.data() + whole.size() ||
        integer > 1000000000)
        invalid();
    const std::int64_t factor = unit == "Wh" ? 1000 : 1000000;
    std::int64_t part = 0;
    for (std::size_t i = 0; i < digits; ++i)
        part = part * 10 + (i < fraction.size() ? fraction[i] - '0' : 0);
    const auto result = integer * factor + part;
    if (result > 1000000000000LL)
        invalid();
    return result;
}
std::int64_t timestamp(const Json &request, const char *name) {
    if (!request.contains(name) || !request.at(name).is_string())
        invalid();
    const auto sql = sql_time(request.at(name).get<std::string>());
    auto number = [&](std::size_t start, std::size_t count) {
        int result = 0;
        for (std::size_t i = start; i < start + count; ++i)
            result = result * 10 + sql[i] - '0';
        return result;
    };
    using namespace std::chrono;
    const auto date = year_month_day{year{number(0, 4)}, month{static_cast<unsigned>(number(5, 2))},
                                     day{static_cast<unsigned>(number(8, 2))}};
    const auto time = sys_days{date} + hours{number(11, 2)} + minutes{number(14, 2)} +
                      seconds{number(17, 2)} + microseconds{number(20, 6)};
    return duration_cast<microseconds>(time.time_since_epoch()).count();
}
std::int64_t ceiling(std::int64_t numerator, std::int64_t denominator) {
    return numerator / denominator + (numerator % denominator != 0 ? 1 : 0);
}
} // namespace
Json sandbox_settlement(const Json &request) {
    const std::set<std::string> fields{"caseId",
                                       "currency",
                                       "paidMinor",
                                       "pricePerKwhMinor",
                                       "parkingPerMinuteMinor",
                                       "parkingGraceSeconds",
                                       "meterStart",
                                       "meterStop",
                                       "chargeEndedAt",
                                       "departedAt"};
    if (!request.is_object() || request.size() != fields.size())
        invalid();
    for (const auto &[key, value] : request.items()) {
        (void)value;
        if (!fields.contains(key))
            invalid();
    }
    if (!request.at("caseId").is_string() ||
        !station_id_valid(request.at("caseId").get<std::string>()) ||
        !request.at("currency").is_string() || request.at("currency").get<std::string>() != "THB")
        invalid();
    const auto paid = bounded(request, "paidMinor", 1000000000000LL);
    const auto rate = bounded(request, "pricePerKwhMinor", 1000000);
    const auto parking_rate = bounded(request, "parkingPerMinuteMinor", 1000000);
    const auto grace = bounded(request, "parkingGraceSeconds", 86400);
    const auto start = meter(request.at("meterStart")), stop = meter(request.at("meterStop"));
    const auto ended = timestamp(request, "chargeEndedAt"),
               departed = timestamp(request, "departedAt");
    if (stop < start || departed < ended || departed - ended > 366LL * 86400 * 1000000)
        invalid();
    const auto energy = stop - start;
    const auto energy_cost = ceiling(energy * rate, 1000000);
    const auto parking_micros = std::max<std::int64_t>(0, departed - ended - grace * 1000000);
    const auto parking_minutes = ceiling(parking_micros, 60000000);
    const auto parking_cost = parking_minutes * parking_rate;
    const auto total = energy_cost + parking_cost;
    return {{"mode", "sandbox"},
            {"caseId", request.at("caseId")},
            {"currency", "THB"},
            {"energyMilliWh", energy},
            {"energyCostMinor", energy_cost},
            {"parkingBilledMinutes", parking_minutes},
            {"parkingCostMinor", parking_cost},
            {"totalMinor", total},
            {"refundMinor", std::max<std::int64_t>(0, paid - total)},
            {"outstandingMinor", std::max<std::int64_t>(0, total - paid)},
            {"paymentExecuted", false}};
}
} // namespace ocpp
