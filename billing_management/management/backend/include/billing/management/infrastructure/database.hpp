#pragma once
#include "billing/management/application/store.hpp"
struct sqlite3;
namespace billing::management {
// Private SQLite adapter, owned by the composition root. Application serializes business
// transactions.
class Database final : public Store {
    sqlite3 *handle_{};

  public:
    explicit Database(const std::string &path);
    ~Database() override;
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    Json get(const std::string &kind, std::int64_t id, bool archived = false) override;
    Json insert(const std::string &kind, const Json &data) override;
    Json list(const std::string &kind, bool archived = false, const std::string &search = "",
              int offset = 0) override;
    void audit(const std::string &actor, const std::string &action, const std::string &kind,
               std::int64_t id) override;
    void begin() override;
    void commit() override;
    void rollback() override;
    void save(std::int64_t, const Json &) override;
    void archive(std::int64_t, bool) override;
    Json overview() override;
    Json audit_page(int) override;
    Json trash_page(int) override;
    std::int64_t wallet_balance(std::int64_t) override;
    Json wallet_history(std::int64_t) override;
    bool connector_exists(std::int64_t, std::int64_t) override;
    bool is_billed(std::int64_t) override;
    std::int64_t refund_total(std::int64_t) override;
    Json unbilled_sessions(std::int64_t, const std::string &, const std::string &) override;
    void attach_bill(std::int64_t, std::int64_t) override;
    Json tax_for_bill(std::int64_t) override;
    bool has_references(const std::string &, std::int64_t) override;
    Json previous_request(const std::string &) override;
    void remember_request(const std::string &, const std::string &, const Json &) override;
    void queue_charger(std::int64_t, const std::string &) override;
    Json next_charger_job() override;
    void finish_charger_job(std::int64_t, bool, bool, const std::string &) override;
    Json charger_sync(std::int64_t) override;

  private:
    Json query(const std::string &sql, const Json &parameters = Json::array());
    void execute(const std::string &sql, const Json &parameters = Json::array());
};
} // namespace billing::management
