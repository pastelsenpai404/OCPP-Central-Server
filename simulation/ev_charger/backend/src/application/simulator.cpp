#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <map>
#include <mutex>
#include <regex>
#include <simulation/physics.hpp>
#include <simulation/simulator.hpp>
#include <simulation/transport.hpp>
#include <stdexcept>
#include <thread>
namespace simulation {
namespace {
using Clock = std::chrono::steady_clock;
struct Charger {
    std::string id, protocol = "ocpp1.6", mode = "mock", status = "Available", fault = "NoError",
                    tag = "TEST001", tx201;
    Battery battery;
    int speed = 1, transaction = 0, seq = 0, heartbeat = 30;
    bool plugged = false, paused = false, network = true, online = false, registered = false,
         available = true, uncertain = false;
    bool starting = false, stopRequested = false, authorizationPending = false;
    std::string stopReason = "Local";
    std::unique_ptr<Transport> socket;
    struct Pending {
        std::string action;
        Clock::time_point time;
    };
    std::map<std::string, Pending> pending;
    std::deque<Json> outbox;
    Clock::time_point last = Clock::now(), lastMeter = last, lastHeartbeat = last, reconnect = last;
    Json view() const {
        return {{"id", id},
                {"protocol", protocol},
                {"mode", mode},
                {"status", status},
                {"fault", fault},
                {"plugged", plugged},
                {"online", online},
                {"registered", registered},
                {"network", network},
                {"available", available},
                {"uncertain", uncertain},
                {"transaction", protocol == "ocpp1.6" ? std::to_string(transaction) : tx201},
                {"soc", battery.soc},
                {"targetSoc", battery.target_soc},
                {"capacityKwh", battery.capacity_wh / 1000},
                {"powerKw", battery.power_w / 1000},
                {"energyKwh", battery.meter_wh / 1000},
                {"gridKw", battery.grid_w / 1000},
                {"maxKw", battery.station_w / 1000},
                {"temperature", battery.temperature_c},
                {"speed", speed},
                {"paused", paused},
                {"elapsedSeconds", battery.elapsed_seconds},
                {"queued", outbox.size()}};
    }
};
double number(const Json &j, const char *key, double fallback, double min, double max) {
    const auto value = j.value(key, fallback);
    if (!std::isfinite(value) || value < min || value > max)
        throw std::invalid_argument(std::string("Invalid ") + key);
    return value;
}
} // namespace
struct Simulator::Impl {
    std::mutex mutex;
    std::atomic<bool> running{true};
    std::thread worker;
    std::map<std::string, Charger> chargers;
    std::deque<Json> logs;
    std::deque<std::pair<std::string, Json>> actions;
    std::string endpoint, ca;
    Json secrets;
    unsigned long long serial = 0;
    Impl(std::string e, Json s, std::string c)
        : endpoint(std::move(e)), ca(std::move(c)), secrets(std::move(s)) {
        worker = std::thread([this] { run(); });
    }
    ~Impl() {
        running = false;
        if (worker.joinable())
            worker.join();
    }
    void log(const Charger &c, const std::string &type, const Json &message) {
        logs.push_back({{"time", timestamp()},
                        {"station", c.id},
                        {"type", type},
                        {"message", message},
                        {"sequence", ++serial}});
        if (logs.size() > 300)
            logs.pop_front();
    }
    void reply(Charger &c, const std::string &action, const Json &response) {
        if (action == "BootNotification") {
            c.registered = response.value("status", "") == "Accepted";
            c.heartbeat = std::clamp(response.value("interval", 30), 5, 86400);
            if (c.registered) {
                status(c);
                while (!c.outbox.empty()) {
                    auto item = std::move(c.outbox.front());
                    c.outbox.pop_front();
                    call(c, item[0], item[1]);
                }
            }
        } else if (action == "Authorize") {
            if (!c.authorizationPending)
                return;
            c.authorizationPending = false;
            const auto info = response.value(c.protocol == "ocpp1.6" ? "idTagInfo" : "idTokenInfo",
                                             Json::object());
            if (info.value("status", "") == "Accepted" && c.plugged && c.fault == "NoError" &&
                c.available)
                start(c);
            else
                log(c, "notice", "Authorization rejected or connector unavailable");
        } else if (action == "StartTransaction" || action == "TransactionEvent:Started") {
            c.starting = false;
            const auto info = response.value(c.protocol == "ocpp1.6" ? "idTagInfo" : "idTokenInfo",
                                             Json{{"status", "Accepted"}});
            if (info.value("status", "") == "Accepted") {
                if (c.protocol == "ocpp1.6") {
                    c.transaction = response.at("transactionId").get<int>();
                    if (c.transaction <= 0) {
                        c.uncertain = true;
                        throw std::runtime_error("Invalid transaction acknowledgement");
                    }
                }
                c.status = "Charging";
                c.battery.charging = true;
                c.lastMeter = Clock::now();
                if (c.stopRequested) {
                    c.stopRequested = false;
                    stop(c, c.stopReason);
                } else if (!c.plugged || c.fault != "NoError" || !c.available)
                    stop(c, !c.plugged ? "EVDisconnected" : "Other");
                else
                    status(c);
            } else {
                c.transaction = 0;
                c.tx201.clear();
                log(c, "notice", "Start denied");
            }
        }
    }
    void call(Charger &c, const std::string &action, const Json &body) {
        if (!c.online || (!c.registered && action != "BootNotification")) {
            if (action == "MeterValues" || action == "TransactionEvent" ||
                action == "StopTransaction") {
                if (c.outbox.size() < 128)
                    c.outbox.push_back(Json::array({action, body}));
                else
                    log(c, "error", "Offline queue full; sample discarded");
            }
            return;
        }
        if (c.pending.size() >= 32) {
            log(c, "error", "Too many outstanding OCPP requests");
            return;
        }
        const auto id = std::to_string(++serial);
        const auto wire = Json::array({2, id, action, body});
        log(c, "out", wire);
        if (c.mode == "mock") {
            Json response = Json::object();
            if (action == "BootNotification")
                response = {{"status", "Accepted"}, {"currentTime", timestamp()}, {"interval", 30}};
            if (action == "Heartbeat")
                response = {{"currentTime", timestamp()}};
            if (action == "Authorize" || action == "StartTransaction") {
                response[c.protocol == "ocpp1.6" ? "idTagInfo" : "idTokenInfo"] = {
                    {"status", c.tag.starts_with("DENY") ? "Invalid" : "Accepted"}};
                if (action == "StartTransaction")
                    response["transactionId"] = static_cast<int>(serial % 1000000000 + 1);
            }
            log(c, "in", Json::array({3, id, response}));
            reply(c,
                  action == "TransactionEvent" ? action + ":" + body.value("eventType", "")
                                               : action,
                  response);
        } else if (c.socket && c.socket->send(wire.dump()))
            c.pending.emplace(id, Charger::Pending{action == "TransactionEvent"
                                                       ? action + ":" + body.value("eventType", "")
                                                       : action,
                                                   Clock::now()});
        else {
            if (action == "StartTransaction" ||
                (action == "TransactionEvent" && body.value("eventType", "") == "Started")) {
                c.uncertain = true;
                c.battery.charging = false;
                log(c, "error",
                    "Start send failed; delivery uncertain. Reconcile CSMS before another start.");
            }
            disconnect(c);
        }
    }
    void status(Charger &c) {
        const auto error = c.fault == "EmergencyStop" || c.fault == "PowerLoss" ? "OtherError"
                           : c.fault == "OverTemperature"                       ? "HighTemperature"
                                                                                : c.fault;
        if (c.protocol == "ocpp1.6")
            call(c, "StatusNotification",
                 {{"connectorId", 1},
                  {"errorCode", error},
                  {"status", c.status},
                  {"timestamp", timestamp()}});
        else
            call(c, "StatusNotification",
                 {{"timestamp", timestamp()},
                  {"connectorStatus", c.fault != "NoError" ? "Faulted"
                                      : !c.available       ? "Unavailable"
                                      : c.plugged          ? "Occupied"
                                                           : "Available"},
                  {"evseId", 1},
                  {"connectorId", 1}});
    }
    Json transactionEvent(Charger &c, const std::string &type, const std::string &reason) {
        return {{"eventType", type},
                {"timestamp", timestamp()},
                {"triggerReason", reason},
                {"seqNo", c.seq++},
                {"transactionInfo",
                 {{"transactionId", c.tx201},
                  {"chargingState", c.paused          ? "SuspendedEVSE"
                                    : type == "Ended" ? "Idle"
                                                      : "Charging"}}},
                {"evse", {{"id", 1}, {"connectorId", 1}}},
                {"idToken", {{"idToken", c.tag}, {"type", "ISO14443"}}},
                {"meterValue", Json::array({meter(c.protocol, c.battery.meter_wh, c.battery.power_w,
                                                  c.battery.soc)})}};
    }
    void start(Charger &c) {
        if (c.battery.charging || c.transaction || !c.tx201.empty() || c.uncertain || c.starting)
            return;
        for (const auto &[id, p] : c.pending) {
            (void)id;
            if (p.action == "StartTransaction")
                return;
        }
        c.status = "Preparing";
        c.starting = true;
        c.stopRequested = false;
        if (c.protocol == "ocpp1.6")
            call(c, "StartTransaction",
                 {{"connectorId", 1},
                  {"idTag", c.tag},
                  {"meterStart", static_cast<int>(c.battery.meter_wh)},
                  {"timestamp", timestamp()}});
        else {
            c.tx201 = "SIM-" + c.id + "-" + std::to_string(++serial);
            c.seq = 0;
            call(c, "TransactionEvent", transactionEvent(c, "Started", "Authorized"));
        }
    }
    void stop(Charger &c, const std::string &reason) {
        c.authorizationPending = false;
        if (c.starting) {
            c.stopRequested = true;
            c.stopReason = reason;
            c.battery.charging = false;
            c.battery.power_w = 0;
            return;
        }
        if (!c.battery.charging && !c.transaction && c.tx201.empty())
            return;
        c.battery.charging = false;
        c.battery.power_w = 0;
        c.paused = false;
        // End state first, so the reply cannot be mistaken for a start acknowledgement.
        c.status = c.fault != "NoError" ? "Faulted" : c.plugged ? "Finishing" : "Available";
        if (c.protocol == "ocpp1.6")
            call(c, "StopTransaction",
                 {{"transactionId", c.transaction},
                  {"meterStop", static_cast<int>(c.battery.meter_wh)},
                  {"timestamp", timestamp()},
                  {"reason", reason},
                  {"idTag", c.tag}});
        else
            call(c, "TransactionEvent",
                 transactionEvent(c, "Ended",
                                  reason == "EmergencyStop" ? "AbnormalCondition"
                                                            : "StopAuthorized"));
        c.transaction = 0;
        c.tx201.clear();
        status(c);
    }
    void disconnect(Charger &c) {
        if (c.starting)
            c.uncertain = true;
        c.authorizationPending = false;
        for (const auto &[id, p] : c.pending) {
            (void)id;
            if (p.action == "StartTransaction" || p.action == "TransactionEvent:Started") {
                c.uncertain = true;
                c.battery.charging = false;
                log(c, "error",
                    "Start acknowledgement lost. Reconcile with CSMS before resetting this "
                    "station.");
            }
        }
        c.pending.clear();
        c.online = false;
        c.registered = false;
        if (c.socket) {
            c.socket->close();
        }
        c.socket.reset();
        c.reconnect = Clock::now() + std::chrono::seconds(5);
    }
    void apply(Charger &c, const Json &input) {
        const auto action = input.at("action").get<std::string>();
        if (action == "plug") {
            if (c.available && c.fault == "NoError") {
                c.plugged = true;
                c.status = "Preparing";
                status(c);
            }
        } else if (action == "unplug") {
            c.plugged = false;
            stop(c, "EVDisconnected");
            c.status = c.fault == "NoError" ? "Available" : "Faulted";
            status(c);
        } else if (action == "start") {
            if (c.plugged && c.registered && c.available && c.fault == "NoError" && !c.uncertain &&
                !c.battery.charging) {
                if (c.authorizationPending || c.starting)
                    return;
                c.tag = input.value("tag", "TEST001");
                c.authorizationPending = true;
                call(c, "Authorize",
                     c.protocol == "ocpp1.6"
                         ? Json{{"idTag", c.tag}}
                         : Json{{"idToken", {{"idToken", c.tag}, {"type", "ISO14443"}}}});
            } else
                log(c, "notice",
                    "Start requires a plugged vehicle, accepted boot, available connector and no "
                    "fault");
        } else if (action == "stop")
            stop(c, "Local");
        else if (action == "pause" || action == "resume") {
            if (c.transaction || !c.tx201.empty()) {
                c.paused = action == "pause";
                c.battery.charging = !c.paused;
                c.status = c.paused ? "SuspendedEVSE" : "Charging";
                status(c);
            }
        } else if (action == "network") {
            c.network = input.at("enabled").get<bool>();
            if (!c.network)
                disconnect(c);
            else
                c.reconnect = Clock::now();
        } else if (action == "fault") {
            c.fault = input.at("fault").get<std::string>();
            c.battery.faulted = c.fault != "NoError";
            if (c.battery.faulted) {
                stop(c, c.fault == "EmergencyStop" ? "EmergencyStop" : "Other");
                c.status = "Faulted";
            } else {
                c.status = c.plugged ? "Preparing" : "Available";
            }
            status(c);
        } else if (action == "configure") {
            c.speed = input.value("speed", c.speed);
            c.battery.grid_w = input.value("gridKw", c.battery.grid_w / 1000) * 1000;
            c.battery.target_soc = input.value("targetSoc", c.battery.target_soc);
        } else if (action == "availability") {
            c.available = input.at("enabled").get<bool>();
            if (!c.available)
                stop(c, "Other");
            c.status = c.available ? (c.plugged ? "Preparing" : "Available") : "Unavailable";
            status(c);
        } else if (action == "reset") {
            if (c.uncertain) {
                log(c, "notice",
                    "Uncertain session requires reconciliation; remove and recreate station after "
                    "checking CSMS");
                return;
            }
            stop(c, "SoftReset");
            disconnect(c);
            c.reconnect = Clock::now();
        }
        log(c, "action", input);
    }
    void incoming(Charger &c, const std::string &text) {
        try {
            auto message = parse(text);
            if (!message.is_array() || message.empty())
                throw std::runtime_error("Invalid frame");
            log(c, "in", message);
            if (message[0] == 3 && message.size() == 3 && message[1].is_string() &&
                message[2].is_object()) {
                auto found = c.pending.find(message[1].get<std::string>());
                if (found != c.pending.end()) {
                    auto name = found->second.action;
                    c.pending.erase(found);
                    reply(c, name, message[2]);
                }
            } else if (message[0] == 4 && message.size() == 5 && message[1].is_string()) {
                if (c.starting) {
                    c.uncertain = true;
                    c.battery.charging = false;
                }
                c.authorizationPending = false;
                c.pending.erase(message[1].get<std::string>());
                log(c, "error", "CSMS returned CALLERROR");
            } else if (message[0] == 2 && message.size() == 4 && message[1].is_string() &&
                       message[2].is_string() && message[3].is_object()) {
                const auto name = message[2].get<std::string>();
                const auto &body = message[3];
                Json result;
                bool supported = true;
                if (name == "RemoteStartTransaction" || name == "RequestStartTransaction") {
                    const auto tag = name == "RemoteStartTransaction"
                                         ? body.at("idTag").get<std::string>()
                                         : body.at("idToken").at("idToken").get<std::string>();
                    bool accepted = c.plugged && c.available && c.fault == "NoError" &&
                                    !c.battery.charging && !c.uncertain;
                    result = {{"status", accepted ? "Accepted" : "Rejected"}};
                    if (accepted)
                        actions.emplace_back(c.id, Json{{"action", "start"}, {"tag", tag}});
                } else if (name == "RemoteStopTransaction" || name == "RequestStopTransaction") {
                    bool match =
                        name == "RemoteStopTransaction"
                            ? body.value("transactionId", 0) == c.transaction && c.transaction != 0
                            : body.value("transactionId", "") == c.tx201 && !c.tx201.empty();
                    result = {{"status", match ? "Accepted" : "Rejected"}};
                    if (match)
                        actions.emplace_back(c.id, Json{{"action", "stop"}});
                } else if (name == "Reset") {
                    result = {{"status", c.uncertain ? "Rejected" : "Accepted"}};
                    if (!c.uncertain)
                        actions.emplace_back(c.id, Json{{"action", "reset"}});
                } else if (name == "ChangeAvailability") {
                    result = {{"status", "Accepted"}};
                    actions.emplace_back(
                        c.id,
                        Json{{"action", "availability"},
                             {"enabled",
                              body.value(c.protocol == "ocpp1.6" ? "type" : "operationalStatus",
                                         "") == "Operative"}});
                } else if (name == "UnlockConnector") {
                    result = {{"status", c.battery.charging ? "UnlockFailed" : "Unlocked"}};
                    if (!c.battery.charging)
                        actions.emplace_back(c.id, Json{{"action", "unplug"}});
                } else if (name == "SetChargingProfile") {
                    // Support a single absolute power limit. Timed, recurring and multi-period
                    // profiles are rejected explicitly.
                    const auto profile =
                        body.at(c.protocol == "ocpp1.6" ? "csChargingProfiles" : "chargingProfile");
                    auto schedule = profile.at("chargingSchedule");
                    if (schedule.is_array()) {
                        if (schedule.size() != 1)
                            throw std::runtime_error("Unsupported schedules");
                        schedule = schedule[0];
                    }
                    auto periods = schedule.at("chargingSchedulePeriod");
                    bool accepted =
                        schedule.value("chargingRateUnit", "") == "W" && periods.size() == 1 &&
                        periods[0].value("startPeriod", -1) == 0 &&
                        profile.value("chargingProfileKind", "") == "Absolute" &&
                        !schedule.contains("duration") && !schedule.contains("startSchedule") &&
                        !profile.contains("validFrom") && !profile.contains("validTo");
                    double watts = number(periods[0], "limit", 0, 0, 350000);
                    result = {{"status", accepted ? "Accepted" : "Rejected"}};
                    if (accepted)
                        actions.emplace_back(
                            c.id, Json{{"action", "configure"}, {"gridKw", watts / 1000}});
                } else if (name == "ClearChargingProfile") {
                    result = {{"status", "Accepted"}};
                    actions.emplace_back(c.id, Json{{"action", "configure"},
                                                    {"gridKw", c.battery.station_w / 1000}});
                } else if (name == "TriggerMessage") {
                    const auto requested = body.value("requestedMessage", "");
                    bool ok = requested == "Heartbeat" || requested == "StatusNotification" ||
                              requested == "BootNotification";
                    result = {{"status", ok ? "Accepted" : "NotImplemented"}};
                    if (ok) {
                        if (requested == "StatusNotification")
                            status(c);
                        else
                            call(c, requested,
                                 requested == "Heartbeat" ? Json::object() : boot(c.protocol));
                    }
                } else
                    supported = false;
                if (actions.size() > 32) {
                    actions.pop_back();
                    result = {{"status", "Rejected"}};
                }
                const auto response =
                    supported ? Json::array({3, message[1], result})
                              : Json::array({4, message[1], "NotImplemented",
                                             "Command is outside simulator capabilities",
                                             Json::object()});
                if (c.socket) {
                    c.socket->send(response.dump());
                }
                log(c, "out", response);
            } else
                throw std::runtime_error("Invalid frame");
        } catch (const std::exception &) {
            log(c, "error", "Invalid OCPP response or command");
            disconnect(c);
        }
    }
    void run() {
        while (running) {
            {
                std::lock_guard lock(mutex);
                while (!actions.empty()) {
                    auto item = std::move(actions.front());
                    actions.pop_front();
                    auto found = chargers.find(item.first);
                    if (found != chargers.end()) {
                        try {
                            apply(found->second, item.second);
                        } catch (const std::exception &) {
                            log(found->second, "error", "Invalid action");
                        }
                    }
                }
                for (auto &[id, c] : chargers) {
                    (void)id;
                    auto now = Clock::now();
                    double elapsed = std::chrono::duration<double>(now - c.last).count();
                    c.last = now;
                    c.battery.tick(std::min(60.0, elapsed * c.speed));
                    if (c.battery.charging && c.battery.soc >= c.battery.target_soc)
                        stop(c, "Local");
                    if (c.network && !c.online && now >= c.reconnect) {
                        c.reconnect = now + std::chrono::seconds(5);
                        if (c.mode == "mock")
                            c.online = true;
                        else {
                            c.socket = websocket(
                                endpoint + c.id, c.protocol,
                                basic(c.id + ":" + secrets.at(c.id).get<std::string>()), ca);
                            c.online = c.socket->connect();
                        }
                        if (c.online) {
                            log(c, "notice", "Connected");
                            call(c, "BootNotification", boot(c.protocol));
                        }
                    }
                    if (c.socket && c.online) {
                        std::string wire;
                        auto read = c.socket->read(wire);
                        if (read == Read::Text)
                            incoming(c, wire);
                        else if (read == Read::Closed)
                            disconnect(c);
                    }
                    if (c.online && !c.registered && c.pending.empty() &&
                        now - c.lastHeartbeat >= std::chrono::seconds(c.heartbeat)) {
                        c.lastHeartbeat = now;
                        call(c, "BootNotification", boot(c.protocol));
                    }
                    if (c.registered &&
                        now - c.lastHeartbeat >= std::chrono::seconds(c.heartbeat)) {
                        c.lastHeartbeat = now;
                        call(c, "Heartbeat", Json::object());
                    }
                    if (!c.starting && !c.uncertain && (c.transaction || !c.tx201.empty()) &&
                        now - c.lastMeter >= std::chrono::seconds(5)) {
                        c.lastMeter = now;
                        if (c.protocol == "ocpp1.6")
                            call(c, "MeterValues",
                                 {{"connectorId", 1},
                                  {"transactionId", c.transaction},
                                  {"meterValue",
                                   Json::array({meter(c.protocol, c.battery.meter_wh,
                                                      c.battery.power_w, c.battery.soc)})}});
                        else
                            call(c, "TransactionEvent",
                                 transactionEvent(c, "Updated", "MeterValuePeriodic"));
                    }
                    for (const auto &[key, p] : c.pending) {
                        (void)key;
                        if (now - p.time > std::chrono::seconds(30)) {
                            log(c, "error", "OCPP request timeout");
                            disconnect(c);
                            break;
                        }
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};
Simulator::Simulator(std::string endpoint, Json secrets, std::string ca)
    : impl_(std::make_unique<Impl>(std::move(endpoint), std::move(secrets), std::move(ca))) {}
Simulator::~Simulator() = default;
Json Simulator::snapshot() {
    std::lock_guard lock(impl_->mutex);
    Json list = Json::array();
    for (const auto &[id, c] : impl_->chargers) {
        (void)id;
        list.push_back(c.view());
    }
    return {{"chargers", list}, {"realEnabled", !impl_->endpoint.empty()}, {"maxChargers", 4}};
}
Json Simulator::events() {
    std::lock_guard lock(impl_->mutex);
    return Json(impl_->logs);
}
Json Simulator::create(const Json &input) {
    if (!input.is_object())
        throw std::invalid_argument("Expected object");
    Charger c;
    c.id = input.at("id").get<std::string>();
    c.protocol = input.value("protocol", "ocpp1.6");
    c.mode = input.value("mode", "mock");
    if (!std::regex_match(c.id, std::regex("SIM[A-Z0-9_-]{1,24}")))
        throw std::invalid_argument("Station ID must begin SIM and use uppercase ASCII");
    if (c.protocol != "ocpp1.6" && c.protocol != "ocpp2.0.1")
        throw std::invalid_argument("Unsupported protocol");
    if (c.mode != "mock" && c.mode != "real")
        throw std::invalid_argument("Unsupported mode");
    c.battery.capacity_wh = number(input, "capacityKwh", 60, 1, 250) * 1000;
    c.battery.soc = number(input, "soc", 20, 0, 99);
    c.battery.target_soc = number(input, "targetSoc", 80, c.battery.soc + 0.1, 100);
    c.battery.station_w = number(input, "maxKw", 22, 0.1, 350) * 1000;
    c.battery.vehicle_w = number(input, "vehicleKw", 11, 0.1, 350) * 1000;
    c.battery.grid_w = c.battery.station_w;
    std::lock_guard lock(impl_->mutex);
    if (c.mode == "real" && (impl_->endpoint.empty() || !impl_->secrets.contains(c.id)))
        throw std::invalid_argument(
            "Real station requires server-side endpoint and station secret");
    if (impl_->chargers.size() >= 4 || impl_->chargers.contains(c.id))
        throw std::invalid_argument("Duplicate station or fleet limit reached");
    auto view = c.view();
    impl_->chargers.emplace(c.id, std::move(c));
    return view;
}
Json Simulator::action(const std::string &id, const Json &input) {
    if (!input.is_object())
        throw std::invalid_argument("Expected object");
    const auto action = input.at("action").get<std::string>();
    static const std::vector<std::string> names = {
        "plug",    "unplug", "start",     "stop",         "pause", "resume",
        "network", "fault",  "configure", "availability", "reset", "remove"};
    if (std::find(names.begin(), names.end(), action) == names.end())
        throw std::invalid_argument("Unknown action");
    if (action == "start") {
        auto tag = input.value("tag", "TEST001");
        if (tag.empty() || tag.size() > 20 || !std::regex_match(tag, std::regex("[A-Za-z0-9_-]+")))
            throw std::invalid_argument("Invalid tag");
    }
    if (action == "fault") {
        const auto f = input.at("fault").get<std::string>();
        if (f != "NoError" && f != "EmergencyStop" && f != "GroundFailure" &&
            f != "OverTemperature" && f != "PowerLoss")
            throw std::invalid_argument("Unknown fault");
    }
    if (action == "network" || action == "availability") {
        if (!input.at("enabled").is_boolean())
            throw std::invalid_argument("Expected boolean");
    }
    if (action == "configure") {
        number(input, "gridKw", 22, 0, 350);
        number(input, "targetSoc", 80, 1, 100);
        const auto speed = input.value("speed", 1);
        if (speed != 1 && speed != 10 && speed != 60)
            throw std::invalid_argument("Speed must be 1, 10 or 60");
    }
    std::lock_guard lock(impl_->mutex);
    auto found = impl_->chargers.find(id);
    if (found == impl_->chargers.end())
        throw std::invalid_argument("Station not found");
    if (action == "remove") {
        auto &c = found->second;
        if (c.uncertain) {
            if (!input.value("reconciled", false))
                throw std::invalid_argument("Reconcile uncertain session with CSMS before removal");
        } else if (c.battery.charging || c.transaction || !c.tx201.empty() || c.starting)
            throw std::invalid_argument("Stop session before removing station");
        impl_->chargers.erase(found);
        return {{"removed", true}};
    }
    if (impl_->actions.size() >= 32)
        throw std::invalid_argument("Action queue full");
    impl_->actions.emplace_back(id, input);
    return {{"queued", true}};
}
} // namespace simulation
