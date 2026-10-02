#pragma once
#include "ocpp/config/config.hpp"
#include <set>
namespace ocpp {
bool outbound_action(std::string_view action);
bool incoming_action(std::string_view action);
bool incoming201_action(std::string_view action);
void validate201_command(const Schemas &schemas, const Config &config, std::string_view action,
                         const Json &payload);
void validate201_response(const Schemas &schemas, std::string_view action, const Json &request,
                          const Json &response);
void validate_command(const Schemas &schemas, const Config &config, std::string_view action,
                      const Json &payload);
std::string transfer_origin(std::string_view location);
} // namespace ocpp
