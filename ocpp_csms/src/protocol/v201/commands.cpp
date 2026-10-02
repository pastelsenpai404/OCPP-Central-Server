#include "ocpp/protocol/commands.hpp"
#include <set>

namespace ocpp {
bool incoming201_action(std::string_view action) {
    static const std::set<std::string_view> actions{"Authorize",
                                                    "BootNotification",
                                                    "ClearedChargingLimit",
                                                    "DataTransfer",
                                                    "FirmwareStatusNotification",
                                                    "Get15118EVCertificate",
                                                    "GetCertificateStatus",
                                                    "Heartbeat",
                                                    "LogStatusNotification",
                                                    "MeterValues",
                                                    "NotifyChargingLimit",
                                                    "NotifyCustomerInformation",
                                                    "NotifyDisplayMessages",
                                                    "NotifyEVChargingNeeds",
                                                    "NotifyEVChargingSchedule",
                                                    "NotifyEvent",
                                                    "NotifyMonitoringReport",
                                                    "NotifyReport",
                                                    "PublishFirmwareStatusNotification",
                                                    "ReportChargingProfiles",
                                                    "ReservationStatusUpdate",
                                                    "SecurityEventNotification",
                                                    "SignCertificate",
                                                    "StatusNotification",
                                                    "TransactionEvent"};
    return actions.contains(action);
}
void validate201_command(const Schemas &schemas, const Config &config, std::string_view action,
                         const Json &p) {
    if (!schemas.contains(action) || (incoming201_action(action) && action != "DataTransfer"))
        throw ProtocolError("NotSupported", "Unsupported OCPP 2.0.1 command direction");
    schemas.validate(action, p);
    const Json *url = nullptr;
    if (action == "UpdateFirmware")
        url = &p.at("firmware").at("location");
    else if (action == "GetLog")
        url = &p.at("log").at("remoteLocation");
    else if (action == "PublishFirmware")
        url = &p.at("location");
    if (url && !config.transfer_origins.contains(transfer_origin(url->get<std::string>())))
        throw ProtocolError("SecurityError", "Transfer origin is not configured");
    if (action == "SetNetworkProfile") {
        const auto &network = p.at("connectionData");
        const auto &url_value = network.at("ocppCsmsUrl").get_ref<const std::string &>();
        if (!url_value.starts_with("wss://") || network.at("securityProfile").get<int>() < 2 ||
            network.at("securityProfile").get<int>() > 3)
            throw ProtocolError("SecurityError",
                                "Network profiles must use WSS and security profile 2 or 3");
    }
    if (action == "ReserveNow" &&
        sql_time(p.at("expiryDateTime").get<std::string>()) <= sql_time(utc_now()))
        throw ProtocolError("PropertyConstraintViolation",
                            "Reservation expiry must be in the future");
    if (action == "SendLocalList") {
        if (p.at("versionNumber").get<std::int64_t>() < 0 ||
            p.at("versionNumber").get<std::int64_t>() > 2147483647)
            throw ProtocolError("PropertyConstraintViolation", "Invalid local-list version");
        std::set<std::pair<std::string, std::string>> tokens;
        if (p.contains("localAuthorizationList"))
            for (const auto &entry : p.at("localAuthorizationList")) {
                const auto &token = entry.at("idToken");
                if (!tokens
                         .emplace(token.at("idToken").get<std::string>(),
                                  token.at("type").get<std::string>())
                         .second ||
                    (p.at("updateType") == "Full" && !entry.contains("idTokenInfo")))
                    throw ProtocolError("PropertyConstraintViolation", "Invalid local-list entry");
            }
    }
    if (action == "SetChargingProfile" ||
        (action == "RequestStartTransaction" && p.contains("chargingProfile"))) {
        const auto &profile = p.at("chargingProfile");
        if (profile.at("id").get<std::int64_t>() < 0 ||
            profile.at("stackLevel").get<std::int64_t>() < 0)
            throw ProtocolError("PropertyConstraintViolation", "Invalid profile ID or stack");
        if (action == "RequestStartTransaction" &&
            profile.at("chargingProfilePurpose") != "TxProfile")
            throw ProtocolError("PropertyConstraintViolation",
                                "Remote-start profile must be TxProfile");
        if (profile.at("chargingProfilePurpose") == "ChargingStationMaxProfile" &&
            p.value("evseId", 0) != 0)
            throw ProtocolError("PropertyConstraintViolation",
                                "Maximum profile requires station scope");
        if (profile.at("chargingProfileKind") == "Recurring" && !profile.contains("recurrencyKind"))
            throw ProtocolError("PropertyConstraintViolation",
                                "Recurring profile requires recurrence");
        for (const auto &schedule : profile.at("chargingSchedule")) {
            if (schedule.contains("startSchedule"))
                static_cast<void>(sql_time(schedule.at("startSchedule").get<std::string>()));
            if (schedule.at("chargingSchedulePeriod").empty() ||
                schedule.at("chargingSchedulePeriod").size() > 256)
                throw ProtocolError("OccurrenceConstraintViolation", "Schedule period limit");
            std::int64_t previous = -1;
            for (const auto &period : schedule.at("chargingSchedulePeriod")) {
                const auto start = period.at("startPeriod").get<std::int64_t>();
                if ((previous == -1 && start != 0) || start <= previous ||
                    period.at("limit").get<double>() < 0 || start > 31536000)
                    throw ProtocolError("PropertyConstraintViolation", "Invalid schedule period");
                previous = start;
            }
        }
    }
}
void validate201_response(const Schemas &schemas, std::string_view action, const Json &request,
                          const Json &response) {
    schemas.validate(action, response, true);
    if (action != "GetVariables" && action != "SetVariables")
        return;
    const bool get = action == "GetVariables";
    const auto &sent = request.at(get ? "getVariableData" : "setVariableData");
    const auto &results = response.at(get ? "getVariableResult" : "setVariableResult");
    auto key = [](const Json &entry) {
        return Json::array({entry.at("component"), entry.at("variable"),
                            entry.value("attributeType", std::string("Actual"))})
            .dump();
    };
    std::multiset<std::string> expected;
    for (const auto &entry : sent)
        expected.insert(key(entry));
    for (const auto &entry : results) {
        const auto match = expected.find(key(entry));
        if (match == expected.end() ||
            (get && entry.at("attributeStatus") == "Accepted" && !entry.contains("attributeValue")))
            throw ProtocolError("ProtocolError", "Uncorrelated or incomplete variable response");
        expected.erase(match);
    }
    if (!expected.empty())
        throw ProtocolError("ProtocolError", "Missing variable result");
}
} // namespace ocpp
