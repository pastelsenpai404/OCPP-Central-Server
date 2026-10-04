#pragma once
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
namespace simulation {
using Json = nlohmann::json;
Json parse(const std::string &text);
std::string timestamp();
std::string basic(const std::string &value);
Json boot(const std::string &protocol);
Json meter(const std::string &protocol, double wh, double watts, double soc);
class Simulator {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    Simulator(std::string endpoint, Json secrets, std::string ca);
    ~Simulator();
    Json snapshot();
    Json events();
    Json create(const Json &input);
    Json action(const std::string &id, const Json &input);
};
} // namespace simulation
