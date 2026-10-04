#pragma once
#include "billing/management/domain/modules.hpp"
namespace billing::management {
// Business-facing persistence port. SQL, connections and filesystem ownership stay in the adapter.
class Store {
  public:
    virtual ~Store() = default;
    virtual void begin() = 0;
    virtual void commit() = 0;
    virtual void rollback() = 0;
    virtual Json get(const std::string &, std::int64_t, bool archived = false) = 0;
    virtual Json insert(const std::string &, const Json &) = 0;
    virtual Json list(const std::string &, bool archived = false, const std::string &search = "",
                      int offset = 0) = 0;
    virtual void audit(const std::string &, const std::string &, const std::string &,
                       std::int64_t) = 0;
    virtual void save(std::int64_t, const Json &) = 0;
    virtual void archive(std::int64_t, bool) = 0;
    virtual Json overview() = 0;
    virtual Json audit_page(int) = 0;
    virtual Json trash_page(int) = 0;
    virtual std::int64_t wallet_balance(std::int64_t) = 0;
    virtual Json wallet_history(std::int64_t) = 0;
    virtual bool connector_exists(std::int64_t, std::int64_t) = 0;
    virtual bool is_billed(std::int64_t) = 0;
    virtual std::int64_t refund_total(std::int64_t) = 0;
    virtual Json unbilled_sessions(std::int64_t, const std::string &, const std::string &) = 0;
    virtual void attach_bill(std::int64_t, std::int64_t) = 0;
    virtual Json tax_for_bill(std::int64_t) = 0;
    virtual bool has_references(const std::string &, std::int64_t) = 0;
    virtual Json previous_request(const std::string &) = 0;
    virtual void remember_request(const std::string &, const std::string &, const Json &) = 0;
    virtual void queue_charger(std::int64_t, const std::string &) = 0;
    virtual Json next_charger_job() = 0;
    virtual void finish_charger_job(std::int64_t, bool, bool, const std::string &) = 0;
    virtual Json charger_sync(std::int64_t) = 0;
};
} // namespace billing::management
