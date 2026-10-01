#pragma once
#include "security.hpp"
#include <condition_variable>
#include <mutex>
#include <optional>
#include <vector>

namespace ocpp {
using Parameters = std::vector<std::optional<std::string>>;
struct SqlResult {
    Json rows = Json::array();
    std::uint64_t inserted = 0, affected = 0;
};
class Database {
    struct Slot {
        void *connection = nullptr;
        bool busy = false;
    };
    Config config_;
    std::vector<Slot> slots_;
    std::mutex mutex_;
    std::condition_variable available_;

  public:
    class Lease {
        Database *pool_;
        std::size_t index_;
        bool transaction_ = false, broken_ = false;
        friend class Database;
        Lease(Database *pool, std::size_t index) : pool_(pool), index_(index) {}

      public:
        ~Lease();
        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
        SqlResult execute(std::string_view sql, const Parameters &parameters = {});
        void begin();
        void commit();
    };
    explicit Database(Config config);
    ~Database();
    Lease acquire();
};
std::int64_t integer(const Json &value);
} // namespace ocpp
