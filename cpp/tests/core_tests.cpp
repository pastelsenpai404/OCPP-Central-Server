#include "ocpp/executor.hpp"
#include "ocpp/security.hpp"
#include <atomic>
#include <future>
#include <iostream>
#include <mutex>

namespace {
unsigned assertions = 0;
void check(bool condition, const char *label) {
    ++assertions;
    if (!condition)
        throw std::runtime_error(label);
}
template <class F> void rejects(F &&f, const char *label) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, label);
}
} // namespace
int main(int argc, char **argv) {
    using namespace ocpp;
    try {
        if (argc != 2)
            throw std::runtime_error("Schema path required");
        const Schemas schemas(argv[1]);
        const auto heartbeat = parse("[2,\"id\",\"Heartbeat\",{}]");
        check(heartbeat.type == 2 && heartbeat.id == "id", "CALL parse");
        schemas.validate(heartbeat.action, heartbeat.payload);
        check(true, "Heartbeat schema");
        check(parse("[3,\"id\",{}]").type == 3, "CALLRESULT parse");
        check(parse("[4,\"id\",\"SecurityError\",\"denied\",{}]").type == 4, "CALLERROR parse");
        for (const auto *wire :
             {"{}", "[]", "[2,\"id\",\"Heartbeat\"]", "[2,\"id\",\"Heartbeat\",{},0]",
              "[2,\"\",\"Heartbeat\",{}]", "[2.0,\"id\",\"Heartbeat\",{}]", "[3,\"id\",[]]",
              "[9,\"id\",{}]", "[4,\"id\",\"Invalid\",\"x\",{}]",
              "[2,\"id\",\"BootNotification\",{\"a\":1,\"a\":2}]",
              "[2,\"id\",\"Heartbeat\",{}] trailing"})
            rejects([&] { parse(wire); }, "Hostile envelope rejection");
        rejects([&] { parse(std::string(65537, ' ')); }, "Message size limit");
        std::string deep = "[2,\"id\",\"A\",{\"x\":";
        deep += std::string(40, '[');
        deep += '0';
        deep += std::string(40, ']');
        deep += "}]";
        rejects([&] { parse(deep); }, "Depth limit");
        rejects([&] { parse("[2,\"id\",\"A\",{\"nested\":{\"x\":1,\"x\":2}}]"); },
                "Nested duplicate key");
        check(parse("[2,\"id\",\"A\",{\"a\":{\"x\":1},\"b\":{\"x\":2}}]").payload.size() == 2,
              "Independent object keys");
        schemas.validate("BootNotification",
                         {{"chargePointVendor", "test"}, {"chargePointModel", "model"}});
        check(true, "Boot schema");
        rejects([&] { schemas.validate("BootNotification", {{"chargePointModel", "x"}}); },
                "Missing required field");
        rejects([&] { schemas.validate("Heartbeat", {{"extra", 1}}); }, "Additional properties");
        rejects(
            [&] {
                schemas.validate("BootNotification", {{"chargePointVendor", std::string(21, 'a')},
                                                      {"chargePointModel", "x"}});
            },
            "String bounds");
        rejects(
            [&] {
                schemas.validate(
                    "StatusNotification",
                    {{"connectorId", -1}, {"errorCode", "NoError"}, {"status", "Available"}});
            },
            "Negative connector");
        rejects(
            [&] {
                schemas.validate(
                    "StatusNotification",
                    {{"connectorId", 1}, {"errorCode", "NoError"}, {"status", "garbage"}});
            },
            "Enum validation");
        rejects(
            [&] { schemas.validate("RemoteStopTransaction", {{"transactionId", 2147483648LL}}); },
            "Integer overflow");
        rejects([&] { schemas.validate("MissingAction", Json::object()); }, "Unsupported action");
        schemas.validate("RemoteStartTransaction", {{"idTag", "test"}, {"connectorId", 1}});
        check(true, "Outbound schema");
        schemas.validate("RemoteStartTransaction", {{"status", "Accepted"}}, true);
        check(true, "Response schema");
        rejects([&] { schemas.validate("RemoteStartTransaction", {{"status", "anything"}}, true); },
                "Response enum");
        check(sql_time("2026-10-02T01:02:03Z") == "2026-10-02 01:02:03.000000", "UTC timestamp");
        check(sql_time("2026-10-02T09:02:03.123+08:00") == "2026-10-02 01:02:03.123000",
              "Timezone timestamp");
        check(sql_time("2026-10-02T00:02:03-01:00") == "2026-10-02 01:02:03.000000",
              "Negative offset");
        for (const auto *timestamp :
             {"2026-02-30T01:02:03Z", "2026-10-02T25:02:03Z", "2026-10-02T01:62:03Z",
              "2026-10-02T01:02:03", "2026-10-02T01:02:03.Z", "2026-10-02T01:02:03+99:00",
              "1969-10-02T01:02:03Z"})
            rejects([&] { sql_time(timestamp); }, "Timestamp constraints");
        check(station_id_valid("CP_01-test"), "Station ID accepted");
        for (const auto *id : {"", "../cp", "cp/other", "cp%2fother", "cp?x", "cp\r\n"})
            check(!station_id_valid(id), "Station ID rejected");
        check(constant_equal("abc", "abc") && !constant_equal("abc", "abd") &&
                  !constant_equal("abc", "ab"),
              "Credential equality");
        check(strong_secret(std::string(64, 'a')) && !strong_secret("short") &&
                  !strong_secret(std::string(64, 'z')),
              "Secret syntax");
        Config config;
        config.read_token = std::string(64, '1');
        config.operator_token = std::string(64, '2');
        config.admin_token = std::string(64, '3');
        check(config.role("Bearer " + config.read_token) == 1 &&
                  config.role("Bearer " + config.operator_token) == 2 &&
                  config.role("Bearer " + config.admin_token) == 3 &&
                  config.role("Bearer unknown") == 0,
              "RBAC");
        RateLimit rate(2, 0);
        check(rate.allow() && rate.allow() && !rate.allow(), "Rate limit");
        {
            Executor executor(1, 2);
            std::promise<void> started, release;
            auto future = release.get_future().share();
            check(executor.submit("station",
                                  [&] {
                                      started.set_value();
                                      future.wait();
                                  }),
                  "Worker submission");
            started.get_future().wait();
            std::vector<int> order;
            std::mutex mutex;
            check(executor.submit("station",
                                  [&] {
                                      std::lock_guard lock(mutex);
                                      order.push_back(1);
                                  }),
                  "Queued first");
            check(executor.submit("station",
                                  [&] {
                                      std::lock_guard lock(mutex);
                                      order.push_back(2);
                                  }),
                  "Queued second");
            check(!executor.submit("station", [] {}), "Backpressure at capacity");
            release.set_value();
            // Destruction drains accepted jobs before their captured variables die.
            std::promise<void> done;
            while (!executor.submit("station", [&] { done.set_value(); }))
                std::this_thread::yield();
            done.get_future().wait();
            check(order == std::vector<int>{1, 2}, "Per-station FIFO ordering");
        }
        std::cout << "PASS: " << assertions << " assertions\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
