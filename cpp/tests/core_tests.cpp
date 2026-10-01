#include "ocpp/billing.hpp"
#include "ocpp/certificates.hpp"
#include "ocpp/commands.hpp"
#include "ocpp/executor.hpp"
#include "ocpp/http_client.hpp"
#include "ocpp/legacy.hpp"
#include "ocpp/security.hpp"
#include <atomic>
#include <fstream>
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
        Json bill = {{"caseId", "test"},
                     {"currency", "THB"},
                     {"paidMinor", 1000},
                     {"pricePerKwhMinor", 800},
                     {"parkingPerMinuteMinor", 20},
                     {"parkingGraceSeconds", 60},
                     {"meterStart", {{"value", "10.000"}, {"unit", "kWh"}}},
                     {"meterStop", {{"value", "11.000"}, {"unit", "kWh"}}},
                     {"chargeEndedAt", "2026-10-02T01:00:00Z"},
                     {"departedAt", "2026-10-02T01:01:00Z"}};
        auto settlement = sandbox_settlement(bill);
        check(settlement["energyCostMinor"] == 800 && settlement["parkingCostMinor"] == 0 &&
                  settlement["refundMinor"] == 200 && settlement["paymentExecuted"] == false,
              "Sandbox energy and grace interval");
        bill["departedAt"] = "2026-10-02T09:01:00.000001+08:00";
        check(sandbox_settlement(bill)["parkingCostMinor"] == 20,
              "One microsecond past grace bills one parking minute");
        bill["meterStop"] = {{"value", "10000.001"}, {"unit", "Wh"}};
        check(sandbox_settlement(bill)["energyCostMinor"] == 1,
              "Fixed-point minor currency rounding");
        bill["meterStop"] = {{"value", "9.000"}, {"unit", "kWh"}};
        rejects([&] { sandbox_settlement(bill); }, "Meter rollback requires reconciliation");
        bill["meterStop"] = {{"value", "11.0000001"}, {"unit", "kWh"}};
        rejects([&] { sandbox_settlement(bill); }, "Unsupported meter precision rejected");
        bill["meterStop"] = {{"value", "1000000000000000000000"}, {"unit", "kWh"}};
        rejects([&] { sandbox_settlement(bill); }, "Financial overflow rejected");
        auto fixture = [&](const char *name) {
            std::ifstream file(std::filesystem::path(argv[1]).parent_path() / "tests/fixtures" /
                               name);
            if (!file)
                throw std::runtime_error("CSR fixture missing");
            return std::string(std::istreambuf_iterator<char>(file), {});
        };
        check(valid_csr(fixture("csr-rsa2048.pem")), "RSA CSR proof of possession");
        check(valid_csr(fixture("csr-p256.pem")), "P256 CSR proof of possession");
        check(!valid_csr(fixture("csr-weak-rsa1024.pem")), "Weak CSR key rejected");
        check(!valid_csr(fixture("csr-tampered.pem")), "Forged CSR signature rejected");
        check(!valid_csr(fixture("csr-rsa2048.pem") + fixture("csr-p256.pem")),
              "Multiple CSR objects rejected");
        check(
            !valid_csr(
                "-----BEGIN CERTIFICATE REQUEST-----\ninvalid\n-----END CERTIFICATE REQUEST-----"),
            "Malformed CSR rejected");
        check(soap_origin("http://192.168.1.10:8080/ocpp") == "http://192.168.1.10:8080",
              "Explicit private SOAP origin");
        for (const auto *url :
             {"http://8.8.8.8/service", "http://localhost/service", "http://127.1/service",
              "http://127.00.0.1/service", "http://169.254.169.254/metadata",
              "http://192.168.1.1@evil/service"})
            rejects([&] { soap_origin(url); }, "SOAP URL ambiguity or public plaintext rejected");
        const Schemas schemas201(std::filesystem::path(argv[1]) / "2.0.1", true);
        const Schemas schemas12(std::filesystem::path(argv[1]) / "1.2", false, 36);
        const Schemas schemas15(std::filesystem::path(argv[1]) / "1.5", false, 48);
        const SoapCodec soap(std::filesystem::path(argv[1]) / "1.5" / "soap.registry");
        const auto fault = soap_fault(false, "Unavailable <internal>", "urn:uuid:a&b");
        check(fault.find("<s:Value>s:Receiver</s:Value>") != std::string::npos &&
                  fault.find("xml:lang=\"en\"") != std::string::npos,
              "SOAP server fault code and language");
        check(fault.find("Unavailable &lt;internal&gt;") != std::string::npos &&
                  fault.find("urn:uuid:a&amp;b") != std::string::npos,
              "SOAP fault text and correlation XML escaping");
        const std::string xml =
            "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
            "xmlns:c=\"urn://Ocpp/Cs/2012/06/\" "
            "xmlns:a=\"http://www.w3.org/2005/08/"
            "addressing\"><s:Header><c:chargeBoxIdentity>CP_TEST</"
            "c:chargeBoxIdentity><a:MessageID>urn:uuid:abc</a:MessageID><a:Action>/MeterValues</"
            "a:Action></s:Header><s:Body><c:meterValuesRequest><c:connectorId>1</"
            "c:connectorId><c:values><c:timestamp>2026-10-02T00:00:00Z</c:timestamp><c:value "
            "unit=\"Amp\">7.5</c:value></c:values></c:meterValuesRequest></s:Body></s:Envelope>";
        const auto decoded = soap.decode(xml);
        schemas15.validate(decoded.action, decoded.payload);
        const auto normalized = legacy_request("1.5", decoded.action, decoded.payload);
        schemas.validate(decoded.action, normalized);
        check(decoded.station == "CP_TEST" && decoded.message_id == "urn:uuid:abc" &&
                  normalized["meterValue"][0]["sampledValue"][0]["unit"] == "A",
              "Typed SOAP samples and legacy units");
        check(soap.encode("Heartbeat", {{"currentTime", "2026-10-02T00:00:00Z"}}, "urn:uuid:abc")
                      .find("RelatesTo") != std::string::npos,
              "SOAP WS-Addressing correlation");
        auto duplicate = xml;
        duplicate.insert(duplicate.find("<c:values>"), "<c:connectorId>2</c:connectorId>");
        rejects([&] { soap.decode(duplicate); }, "SOAP duplicate scalar");
        auto wrong_ns = xml;
        wrong_ns.replace(wrong_ns.find("urn://Ocpp/Cs/2012/06/"), 22, "urn:foreign");
        rejects([&] { soap.decode(wrong_ns); }, "SOAP namespace substitution");
        rejects(
            [&] {
                soap.decode("<!DOCTYPE s:Envelope [<!ENTITY ex SYSTEM 'file:///secret'>]>" + xml);
            },
            "SOAP DTD rejection");
        rejects([&] { soap.decode("<?processing instruction?>" + xml); },
                "SOAP processing instruction rejection");
        Json old_meter = {
            {"connectorId", 1},
            {"values", Json::array({{{"timestamp", "2026-10-02T00:00:00Z"}, {"value", 123}}})}};
        schemas12.validate("MeterValues", old_meter);
        check(legacy_request("1.2", "MeterValues",
                             old_meter)["meterValue"][0]["sampledValue"][0]["value"] == "123",
              "OCPP 1.2 integer meters");
        schemas201.validate("BootNotification",
                            {{"reason", "PowerUp"},
                             {"chargingStation", {{"vendorName", "vendor"}, {"model", "model"}}}});
        check(schemas201.contains("RequestStartTransaction"), "OCPP 2.0.1 action prefix retained");
        schemas201.validate("TransactionEvent",
                            {{"eventType", "Started"},
                             {"timestamp", "2026-10-02T00:00:00Z"},
                             {"triggerReason", "Authorized"},
                             {"seqNo", 0},
                             {"transactionInfo", {{"transactionId", "tx-201"}}}});
        check(true, "OCPP 2.0.1 transaction schema with internal references");
        rejects(
            [&] {
                schemas201.validate("TransactionEvent",
                                    {{"eventType", "Started"},
                                     {"timestamp", "2026-10-02T00:00:00Z"},
                                     {"triggerReason", "Authorized"},
                                     {"seqNo", -1},
                                     {"transactionInfo", {{"transactionId", "tx-201"}}}});
            },
            "Negative transaction sequence");
        Config policy;
        policy.transfer_origins.insert("https://transfers.example.test");
        validate_command(schemas, policy, "UpdateFirmware",
                         {{"location", "https://transfers.example.test/firmware.bin"},
                          {"retrieveDate", "2026-10-02T00:00:00Z"}});
        check(true, "Allowed firmware origin");
        for (const auto *url :
             {"http://transfers.example.test/a", "https://transfers.example.test@127.0.0.1/a",
              "https://transfers.example.test.evil/a", "https://transfers.example.test\\@evil/a",
              "https://transfers.example.test/a#fragment"})
            rejects(
                [&] {
                    validate_command(schemas, policy, "UpdateFirmware",
                                     {{"location", url}, {"retrieveDate", "2026-10-02T00:00:00Z"}});
                },
                "Transfer URL policy bypass");
        rejects(
            [&] {
                validate_command(schemas, policy, "SendLocalList",
                                 {{"listVersion", 1},
                                  {"updateType", "Full"},
                                  {"localAuthorizationList", Json::array({{{"idTag", "TAG"}}})}});
            },
            "Full local list requires token information");
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
