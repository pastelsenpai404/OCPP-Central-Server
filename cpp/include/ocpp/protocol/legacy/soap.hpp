#pragma once
#include "ocpp/protocol/message.hpp"

namespace ocpp {
// Typed WSDL conversion. The common service operates on a canonical 1.6 payload.
Json legacy_request(std::string_view version, std::string_view action, Json payload);
Json legacy_response(std::string_view version, std::string_view action, Json payload);
Json legacy_command(std::string_view version, std::string_view action, Json payload);
Json canonical_response(std::string_view version, std::string_view action, Json payload);
std::string soap_fault(bool sender, std::string_view reason, std::string_view relates_to = {});
struct SoapCall {
    std::string station;
    std::string message_id;
    std::string action;
    Json payload;
};
class SoapCodec {
    Json registry_;

  public:
    explicit SoapCodec(const std::filesystem::path &registry);
    SoapCall decode(std::string_view xml, bool response = false) const;
    std::string encode(std::string_view action, const Json &payload, std::string_view relates_to,
                       bool response = true, std::string_view station = {}) const;
};
} // namespace ocpp
