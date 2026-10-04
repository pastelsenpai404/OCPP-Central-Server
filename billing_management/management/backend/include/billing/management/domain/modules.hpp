#pragma once
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
namespace billing::management {
using Json = nlohmann::json;
struct Problem : std::runtime_error {
    int status;
    Problem(int code, const std::string &message) : std::runtime_error(message), status(code) {}
};
Json modules();
Json module(const std::string &kind);
void validate(const std::string &kind, const Json &data);
std::string text(const Json &data, const char *key, std::size_t maximum = 200);
std::int64_t integer(const Json &data, const char *key, std::int64_t minimum, std::int64_t maximum);
void keys(const Json &data, std::initializer_list<const char *> allowed);
} // namespace billing::management
