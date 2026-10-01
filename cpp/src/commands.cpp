#include "ocpp/commands.hpp"
#include <cmath>

namespace ocpp {
bool outbound_action(std::string_view action) {
    static const std::set<std::string_view> actions{"CancelReservation",
                                                    "ChangeAvailability",
                                                    "ChangeConfiguration",
                                                    "ClearCache",
                                                    "ClearChargingProfile",
                                                    "DataTransfer",
                                                    "GetCompositeSchedule",
                                                    "GetConfiguration",
                                                    "GetDiagnostics",
                                                    "GetLocalListVersion",
                                                    "RemoteStartTransaction",
                                                    "RemoteStopTransaction",
                                                    "ReserveNow",
                                                    "Reset",
                                                    "SendLocalList",
                                                    "SetChargingProfile",
                                                    "TriggerMessage",
                                                    "UnlockConnector",
                                                    "UpdateFirmware",
                                                    "CertificateSigned",
                                                    "DeleteCertificate",
                                                    "ExtendedTriggerMessage",
                                                    "GetInstalledCertificateIds",
                                                    "GetLog",
                                                    "InstallCertificate",
                                                    "SignedUpdateFirmware"};
    return actions.contains(action);
}
bool incoming_action(std::string_view action) {
    static const std::set<std::string_view> actions{"Authorize",
                                                    "BootNotification",
                                                    "DataTransfer",
                                                    "DiagnosticsStatusNotification",
                                                    "FirmwareStatusNotification",
                                                    "Heartbeat",
                                                    "MeterValues",
                                                    "StartTransaction",
                                                    "StatusNotification",
                                                    "StopTransaction",
                                                    "LogStatusNotification",
                                                    "SecurityEventNotification",
                                                    "SignCertificate",
                                                    "SignedFirmwareStatusNotification"};
    return actions.contains(action);
}
std::string transfer_origin(std::string_view location) {
    // Chargers perform these transfers. Restrict operator-controlled URLs to explicitly
    // configured HTTPS origins; never accept credentials, redirects or alternate schemes.
    if (!location.starts_with("https://") || location.size() > 512)
        throw ProtocolError("SecurityError", "HTTPS transfer URL required");
    for (const auto c : location)
        if (static_cast<unsigned char>(c) <= 32 || static_cast<unsigned char>(c) >= 127 ||
            c == '\\' || c == '#' || c == '@')
            throw ProtocolError("SecurityError", "Invalid transfer URL");
    const auto end = location.find_first_of("/?", 8);
    const auto authority = location.substr(8, end == std::string_view::npos ? end : end - 8);
    if (authority.empty() || authority.size() > 253 || authority.front() == '.' ||
        authority.back() == '.' || authority.find('%') != std::string_view::npos)
        throw ProtocolError("SecurityError", "Invalid transfer authority");
    for (const auto c : authority)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ':'))
            throw ProtocolError("SecurityError", "Use a canonical lowercase host");
    const auto colon = authority.find(':');
    if (colon != std::string_view::npos) {
        const auto port = authority.substr(colon + 1);
        if (port.empty() || port.size() > 5 || port.front() == '0' ||
            !std::all_of(port.begin(), port.end(), [](char c) { return c >= '0' && c <= '9'; }) ||
            std::stoul(std::string(port)) > 65535)
            throw ProtocolError("SecurityError", "Invalid transfer port");
    }
    return "https://" + std::string(authority);
}
void validate_command(const Schemas &schemas, const Config &config, std::string_view action,
                      const Json &payload) {
    if (!outbound_action(action))
        throw ProtocolError("NotSupported", "Unknown outbound action");
    schemas.validate(action, payload);
    const Json *location = nullptr;
    if (action == "GetDiagnostics" || action == "UpdateFirmware")
        location = &payload.at("location");
    else if (action == "GetLog")
        location = &payload.at("log").at("remoteLocation");
    else if (action == "SignedUpdateFirmware")
        location = &payload.at("firmware").at("location");
    if (location &&
        !config.transfer_origins.contains(transfer_origin(location->get<std::string>())))
        throw ProtocolError("SecurityError", "Transfer origin is not configured");
    if (action == "ReserveNow" &&
        sql_time(payload.at("expiryDate").get<std::string>()) <= sql_time(utc_now()))
        throw ProtocolError("PropertyConstraintViolation",
                            "Reservation expiry must be in the future");
    if (action == "UnlockConnector" && payload.at("connectorId").get<std::int64_t>() == 0)
        throw ProtocolError("PropertyConstraintViolation", "Unlock requires a physical connector");
    if (action == "SendLocalList") {
        if (payload.at("listVersion").get<std::int64_t>() < 0 ||
            payload.at("listVersion").get<std::int64_t>() > 2147483647)
            throw ProtocolError("PropertyConstraintViolation", "Invalid local-list version");
        std::set<std::string> tags;
        if (payload.contains("localAuthorizationList")) {
            if (payload.at("localAuthorizationList").size() > 1000)
                throw ProtocolError("OccurrenceConstraintViolation", "Local-list limit");
            for (const auto &entry : payload.at("localAuthorizationList")) {
                if (!tags.insert(entry.at("idTag").get<std::string>()).second ||
                    (payload.at("updateType") == "Full" && !entry.contains("idTagInfo")))
                    throw ProtocolError("PropertyConstraintViolation",
                                        "Duplicate or incomplete local-list entry");
                if (entry.contains("idTagInfo") && entry.at("idTagInfo").contains("expiryDate"))
                    static_cast<void>(
                        sql_time(entry.at("idTagInfo").at("expiryDate").get<std::string>()));
            }
        }
    }
    if (action == "SetChargingProfile" ||
        (action == "RemoteStartTransaction" && payload.contains("chargingProfile"))) {
        const auto &p =
            payload.at(action == "SetChargingProfile" ? "csChargingProfiles" : "chargingProfile");
        for (const auto *field : {"chargingProfileId", "transactionId", "stackLevel"})
            if (p.contains(field) && (p.at(field).get<std::int64_t>() < 0 ||
                                      p.at(field).get<std::int64_t>() > 2147483647))
                throw ProtocolError("PropertyConstraintViolation", "Profile integer range");
        if (p.at("chargingProfilePurpose") == "ChargePointMaxProfile" &&
            payload.value("connectorId", 0) != 0)
            throw ProtocolError("PropertyConstraintViolation",
                                "Station maximum profile requires connector zero");
        if (action == "RemoteStartTransaction" && p.at("chargingProfilePurpose") != "TxProfile")
            throw ProtocolError("PropertyConstraintViolation",
                                "Remote-start profile must be TxProfile");
        if (action == "SetChargingProfile" && p.at("chargingProfilePurpose") == "TxProfile" &&
            !p.contains("transactionId"))
            throw ProtocolError("PropertyConstraintViolation",
                                "Transaction profile requires an active transaction ID");
        if (p.at("chargingProfileKind") == "Recurring" && !p.contains("recurrencyKind"))
            throw ProtocolError("PropertyConstraintViolation",
                                "Recurring profile requires recurrence");
        for (const auto *field : {"validFrom", "validTo"})
            if (p.contains(field))
                static_cast<void>(sql_time(p.at(field).get<std::string>()));
        if (p.contains("validFrom") && p.contains("validTo") &&
            sql_time(p.at("validFrom").get<std::string>()) >=
                sql_time(p.at("validTo").get<std::string>()))
            throw ProtocolError("PropertyConstraintViolation", "Invalid profile validity interval");
        const auto &schedule = p.at("chargingSchedule");
        if (schedule.contains("startSchedule"))
            static_cast<void>(sql_time(schedule.at("startSchedule").get<std::string>()));
        if (p.at("chargingProfileKind") == "Absolute" && !schedule.contains("startSchedule"))
            throw ProtocolError("PropertyConstraintViolation",
                                "Absolute profile requires startSchedule");
        if (schedule.contains("duration") &&
            (schedule.at("duration").get<std::int64_t>() <= 0 ||
             schedule.at("duration").get<std::int64_t>() > 31536000))
            throw ProtocolError("PropertyConstraintViolation", "Invalid schedule duration");
        const auto &periods = schedule.at("chargingSchedulePeriod");
        if (periods.empty() || periods.size() > 256)
            throw ProtocolError("OccurrenceConstraintViolation", "Schedule period limit");
        std::int64_t previous = -1;
        for (const auto &period : periods) {
            const auto start = period.at("startPeriod").get<std::int64_t>();
            const auto limit = period.at("limit").get<double>();
            if (start <= previous || start > 31536000 || (previous == -1 && start != 0) ||
                !std::isfinite(limit) || limit < 0 || limit > 100000000 ||
                (period.contains("numberPhases") && (period.at("numberPhases").get<int>() < 1 ||
                                                     period.at("numberPhases").get<int>() > 3)) ||
                (schedule.contains("duration") &&
                 start >= schedule.at("duration").get<std::int64_t>()))
                throw ProtocolError("PropertyConstraintViolation", "Invalid schedule period");
            previous = start;
        }
    }
}
} // namespace ocpp
