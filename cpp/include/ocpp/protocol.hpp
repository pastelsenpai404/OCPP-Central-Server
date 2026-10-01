#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ocpp {
using Json = nlohmann::json;
struct ProtocolError : std::runtime_error {
    std::string code;
    ProtocolError(std::string c, std::string message)
        : std::runtime_error(std::move(message)), code(std::move(c)) {}
};
struct Message {
    int type;
    std::string id;
    std::string action;
    Json payload;
    std::string error_code;
};
Message parse(std::string_view wire);
Json error(std::string_view id, std::string_view code, std::string_view description);
bool station_id_valid(std::string_view id);
std::string utc_now();
std::string sql_time(std::string_view rfc3339);
class Schemas {
    std::map<std::string, std::unique_ptr<nlohmann::json_schema::json_validator>> validators_;
    bool version201_;
    bool legacy_;

  public:
    explicit Schemas(const std::filesystem::path &directory, bool version201 = false,
                     std::size_t expected_count = 0);
    void validate(std::string_view action, const Json &payload, bool response = false) const;
    bool contains(std::string_view action, bool response = false) const;
};
} // namespace ocpp
