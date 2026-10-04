#pragma once
#include "billing/management/application/ocpp_registry.hpp"
namespace billing::management {
class OcppClient final : public OcppRegistry {
    std::string token_;
    std::string host_;
    unsigned port_ = 5003;

  public:
    OcppClient();
    bool enabled() const {
        return !token_.empty();
    }
    RegistryResult register_charger(const std::string &code) override;
};
} // namespace billing::management
