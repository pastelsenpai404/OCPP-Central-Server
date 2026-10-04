#pragma once
#include <string>
namespace billing::management {
struct RegistryResult {
    bool registered = false;
    bool credentials_configured = false;
    std::string error;
};
class OcppRegistry {
  public:
    virtual ~OcppRegistry() = default;
    virtual RegistryResult register_charger(const std::string &code) = 0;
};
} // namespace billing::management
