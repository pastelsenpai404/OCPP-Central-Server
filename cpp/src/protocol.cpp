#include "ocpp/protocol.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace ocpp {
bool station_id_valid(std::string_view id) {
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-';
    });
}
Message parse(std::string_view wire) {
    if (wire.empty() || wire.size() > 65536)
        throw ProtocolError("FormationViolation", "Message size limit");
    Json document;
    try {
        std::vector<std::set<std::string>> keys(34);
        document = Json::parse(wire, [&](int depth, Json::parse_event_t event, Json &value) {
            if (depth > 32)
                throw ProtocolError("FormationViolation", "Nesting limit");
            if (event == Json::parse_event_t::object_start)
                keys.at(static_cast<std::size_t>(depth + 1)).clear();
            if (event == Json::parse_event_t::key &&
                !keys.at(static_cast<std::size_t>(depth)).insert(value.get<std::string>()).second)
                throw ProtocolError("FormationViolation", "Duplicate member");
            return true;
        });
    } catch (const ProtocolError &) {
        throw;
    } catch (const std::exception &) {
        throw ProtocolError("FormationViolation", "Invalid JSON");
    }
    if (!document.is_array() || document.size() < 3 || !document[0].is_number_integer() ||
        !document[1].is_string())
        throw ProtocolError("FormationViolation", "Invalid envelope");
    auto id = document[1].get<std::string>();
    if (id.empty() || id.size() > 36 ||
        std::any_of(id.begin(), id.end(), [](unsigned char c) { return c < 32 || c > 126; }))
        throw ProtocolError("FormationViolation", "Invalid message ID");
    const auto type_number = document[0].get<std::int64_t>();
    if (type_number == 2 && document.size() == 4 && document[2].is_string() &&
        document[3].is_object()) {
        auto action = document[2].get<std::string>();
        if (action.empty() || action.size() > 64)
            throw ProtocolError("FormationViolation", "Invalid action");
        return {2, std::move(id), std::move(action), std::move(document[3]), {}};
    }
    if (type_number == 3 && document.size() == 3 && document[2].is_object())
        return {3, std::move(id), {}, std::move(document[2]), {}};
    if (type_number == 4 && document.size() == 5 && document[2].is_string() &&
        document[3].is_string() && document[4].is_object()) {
        const auto code = document[2].get<std::string>();
        const std::set<std::string> valid{"NotImplemented",
                                          "NotSupported",
                                          "InternalError",
                                          "ProtocolError",
                                          "SecurityError",
                                          "FormationViolation",
                                          "PropertyConstraintViolation",
                                          "OccurrenceConstraintViolation",
                                          "TypeConstraintViolation",
                                          "GenericError"};
        if (!valid.contains(code) || document[3].get_ref<const std::string &>().size() > 256)
            throw ProtocolError("FormationViolation", "Invalid CALLERROR");
        return {4, std::move(id), {}, std::move(document[4]), code};
    }
    throw ProtocolError("FormationViolation", "Invalid envelope");
}
Json error(std::string_view id, std::string_view code, std::string_view description) {
    return Json::array({4, id, code, description, Json::object()});
}
std::string utc_now() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const auto t = std::chrono::system_clock::to_time_t(seconds);
    std::tm result{};
#ifdef _WIN32
    gmtime_s(&result, &t);
#else
    gmtime_r(&t, &result);
#endif
    std::ostringstream out;
    out << std::put_time(&result, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
std::string sql_time(std::string_view text) {
    using namespace std::chrono;
    auto bad = []() { throw ProtocolError("PropertyConstraintViolation", "Invalid timestamp"); };
    if (text.size() < 20 || text.size() > 35 || text[4] != '-' || text[7] != '-' ||
        (text[10] != 'T' && text[10] != 't') || text[13] != ':' || text[16] != ':')
        bad();
    auto number = [&](std::size_t start, std::size_t length) {
        int n = 0;
        for (std::size_t i = start; i < start + length; ++i) {
            if (i >= text.size() || text[i] < '0' || text[i] > '9')
                bad();
            n = n * 10 + (text[i] - '0');
        }
        return n;
    };
    const year_month_day date{year{number(0, 4)}, month{static_cast<unsigned>(number(5, 2))},
                              day{static_cast<unsigned>(number(8, 2))}};
    const int h = number(11, 2), m = number(14, 2), s = number(17, 2);
    if (!date.ok() || int(date.year()) < 1970 || int(date.year()) > 2099 || h > 23 || m > 59 ||
        s > 59)
        bad();
    std::size_t p = 19;
    std::string fraction;
    if (text[p] == '.') {
        ++p;
        while (p < text.size() && text[p] >= '0' && text[p] <= '9') {
            if (fraction.size() < 6)
                fraction += text[p];
            ++p;
        }
        if (fraction.empty())
            bad();
    }
    int offset = 0;
    if (p + 1 == text.size() && (text[p] == 'Z' || text[p] == 'z')) {
    } else if (p + 6 == text.size() && (text[p] == '+' || text[p] == '-') && text[p + 3] == ':') {
        const int oh = number(p + 1, 2), om = number(p + 4, 2);
        if (oh > 23 || om > 59)
            bad();
        offset = (oh * 60 + om) * (text[p] == '+' ? 1 : -1);
    } else
        bad();
    const auto point = sys_days{date} + hours{h} + minutes{m} + seconds{s} - minutes{offset};
    const auto t = system_clock::to_time_t(point);
    std::tm result{};
#ifdef _WIN32
    gmtime_s(&result, &t);
#else
    gmtime_r(&t, &result);
#endif
    fraction.resize(6, '0');
    std::ostringstream out;
    out << std::put_time(&result, "%Y-%m-%d %H:%M:%S") << '.' << fraction;
    return out.str();
}
Schemas::Schemas(const std::filesystem::path &directory, bool version201,
                 std::size_t expected_count)
    : version201_(version201), legacy_(expected_count != 0) {
    for (const auto &file : std::filesystem::directory_iterator(directory)) {
        if (file.path().extension() != ".json")
            continue;
        Json schema;
        std::ifstream input(file.path());
        input >> schema;
        // OCPP's draft-04 schemas use no $refs or boolean exclusive bounds.
        // These constraints are identical in draft-07. Keep originals on disk.
        schema.erase("$schema");
        schema.erase("id");
        auto validator = std::make_unique<nlohmann::json_schema::json_validator>(
            nullptr, nlohmann::json_schema::default_string_format_check);
        validator->set_root_schema(schema);
        auto name = file.path().stem().string();
        if (version201 && name.ends_with("Request"))
            name.resize(name.size() - 7);
        validators_.emplace(std::move(name), std::move(validator));
    }
    if (validators_.size() != (expected_count ? expected_count : (version201 ? 128 : 78)))
        throw std::runtime_error("Incomplete bundled protocol schema set");
}
bool Schemas::contains(std::string_view action, bool response) const {
    return validators_.contains(std::string(action) + (response ? "Response" : ""));
}
void Schemas::validate(std::string_view action, const Json &payload, bool response) const {
    const auto found = validators_.find(std::string(action) + (response ? "Response" : ""));
    if (found == validators_.end())
        throw ProtocolError("NotImplemented", "Unknown action");
    try {
        found->second->validate(payload);
    } catch (const std::exception &) {
        throw ProtocolError("PropertyConstraintViolation", "Payload does not match schema");
    }
    if (version201_) {
        std::function<void(const Json &)> bounds = [&](const Json &value) {
            if (value.is_array() && value.size() > 512)
                throw ProtocolError("OccurrenceConstraintViolation", "Array item limit");
            if (value.is_object() || value.is_array()) {
                for (auto it = value.begin(); it != value.end(); ++it) {
                    if (value.is_object() && it.value().is_number_integer() &&
                        (it.key() == "seqNo" || it.key() == "evseId" || it.key() == "connectorId" ||
                         it.key() == "requestId" || it.key() == "remoteStartId")) {
                        const auto n = it.value().get<std::int64_t>();
                        if (n < 0 || n > 2147483647)
                            throw ProtocolError("PropertyConstraintViolation", "Integer range");
                    }
                    if (value.is_object() && it.key() == "timestamp" && it.value().is_string())
                        static_cast<void>(sql_time(it.value().get<std::string>()));
                    bounds(it.value());
                }
            }
        };
        bounds(payload);
        return;
    }
    // Additional finite resource and business integer bounds, absent in some OCA schemas.
    for (const auto *name :
         {"connectorId", "transactionId", "meterStart", "meterStop", "reservationId"}) {
        if (payload.contains(name) && (payload[name].get<std::int64_t>() < 0 ||
                                       payload[name].get<std::int64_t>() > 2147483647))
            throw ProtocolError("PropertyConstraintViolation", "Integer range");
    }
    if (payload.contains("timestamp"))
        static_cast<void>(sql_time(payload["timestamp"].get<std::string>()));
    if (legacy_)
        return; // Canonical conversion is bounded again by the common service validator.
    for (const auto *field : {"meterValue", "transactionData"}) {
        if (!payload.contains(field))
            continue;
        const auto &values = payload[field];
        if (values.size() > 128)
            throw ProtocolError("OccurrenceConstraintViolation", "Meter value limit");
        std::size_t total = 0;
        for (const auto &value : values) {
            static_cast<void>(sql_time(value.at("timestamp").get<std::string>()));
            total += value.at("sampledValue").size();
            if (total > 512)
                throw ProtocolError("OccurrenceConstraintViolation", "Sample limit");
            for (const auto &sample : value.at("sampledValue"))
                if (sample.at("value").get_ref<const std::string &>().size() > 1024)
                    throw ProtocolError("PropertyConstraintViolation", "Sample value limit");
        }
    }
}
} // namespace ocpp
