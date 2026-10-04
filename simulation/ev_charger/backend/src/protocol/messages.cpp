#include <chrono>
#include <ctime>
#include <iomanip>
#include <map>
#include <set>
#include <simulation/simulator.hpp>
#include <sstream>
#include <stdexcept>
namespace simulation {
Json parse(const std::string &text) {
    std::map<int, std::set<std::string>> keys;
    return Json::parse(text, [&](int depth, Json::parse_event_t event, Json &value) {
        if (depth > 16)
            throw std::invalid_argument("JSON nesting limit exceeded");
        if (event == Json::parse_event_t::object_start)
            keys.erase(depth + 1);
        if (event == Json::parse_event_t::key &&
            !keys[depth].insert(value.get<std::string>()).second)
            throw std::invalid_argument("Duplicate JSON member");
        if (event == Json::parse_event_t::object_end)
            keys.erase(depth + 1);
        return true;
    });
}
std::string timestamp() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
std::string basic(const std::string &value) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out = "Basic ";
    unsigned int accumulator = 0;
    int bits = -6;
    for (unsigned char c : value) {
        accumulator = (accumulator << 8) | c;
        bits += 8;
        while (bits >= 0) {
            out += alphabet[(accumulator >> bits) & 63];
            bits -= 6;
        }
    }
    if (bits > -6)
        out += alphabet[((accumulator << 8) >> (bits + 8)) & 63];
    while ((out.size() - 6) % 4)
        out += '=';
    return out;
}
Json boot(const std::string &protocol) {
    if (protocol == "ocpp2.0.1")
        return {{"reason", "PowerUp"},
                {"chargingStation",
                 {{"model", "Virtual EVSE"},
                  {"vendorName", "EV Simulation"},
                  {"firmwareVersion", "0.1.0"}}}};
    return {{"chargePointVendor", "EV Simulation"},
            {"chargePointModel", "Virtual EVSE"},
            {"firmwareVersion", "0.1.0"}};
}
Json meter(const std::string &protocol, double wh, double watts, double soc) {
    Json samples = Json::array();
    for (const auto &item : {std::pair{"Energy.Active.Import.Register", wh},
                             std::pair{"Power.Active.Import", watts}, std::pair{"SoC", soc}}) {
        if (protocol == "ocpp2.0.1")
            samples.push_back(
                {{"value", item.second},
                 {"measurand", item.first},
                 {"unitOfMeasure",
                  {{"unit", std::string(item.first) == "SoC"                   ? "Percent"
                            : std::string(item.first) == "Power.Active.Import" ? "W"
                                                                               : "Wh"}}}});
        else
            samples.push_back(
                {{"value", std::to_string(item.second)},
                 {"measurand", item.first},
                 {"unit", std::string(item.first) == "SoC"                   ? "Percent"
                          : std::string(item.first) == "Power.Active.Import" ? "W"
                                                                             : "Wh"}});
    }
    return {{"timestamp", timestamp()}, {"sampledValue", samples}};
}
} // namespace simulation
