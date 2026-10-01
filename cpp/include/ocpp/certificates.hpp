#pragma once
#include <string_view>
namespace ocpp {
// Verifies PKCS#10 proof of possession. Issuer policy and identity review remain separate.
bool valid_csr(std::string_view pem);
} // namespace ocpp
