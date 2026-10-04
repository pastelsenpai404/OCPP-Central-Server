#pragma once
#include "billing/management/application/ocpp_registry.hpp"
#include "billing/management/application/store.hpp"
#include <mutex>
namespace billing::management {
class Backoffice {
    Store &db_;
    std::mutex mutex_;
    Json operation(const std::string &method, const std::string &path, const Json &input,
                   const std::string &actor, const std::string &search, int offset);
    void references(const std::string &kind, const Json &data);
    std::int64_t balance(std::int64_t customer);

  public:
    explicit Backoffice(Store &store) : db_(store) {}
    bool sync_charger(OcppRegistry &registry);
    Json handle(const std::string &method, const std::string &path, const Json &input,
                const std::string &actor, const std::string &key = "",
                const std::string &search = "", int offset = 0);
};
} // namespace billing::management
