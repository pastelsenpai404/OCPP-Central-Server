#pragma once
#include "protocol.hpp"
namespace ocpp {
// Integer fixed-point arithmetic. Sandbox only: never calls a payment provider.
Json sandbox_settlement(const Json &request);
} // namespace ocpp
