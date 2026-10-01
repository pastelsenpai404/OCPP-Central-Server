#include "ocpp/application/central_system.hpp"
#include "ocpp/protocol/commands.hpp"
#include "ocpp/security/certificates.hpp"
#include <set>

namespace ocpp {
namespace {
std::optional<std::string> value(const Json &p, const char *name) {
    if (!p.contains(name))
        return std::nullopt;
    return p.at(name).is_string() ? p.at(name).get<std::string>() : p.at(name).dump();
}
void device_variable(Database::Lease &db, const std::string &station, const Json &component,
                     const Json &variable, const Json &attribute, const Json &metadata) {
    const auto evse = component.value("evse", Json::object());
    db.execute("INSERT INTO "
               "cpp201_device_variable(station_id,component_name,component_instance,evse_id,"
               "connector_id,variable_name,variable_instance,attribute_type,value,metadata_body) "
               "VALUES(?,?,?,?,?,?,?,?,?,?) ON DUPLICATE KEY UPDATE "
               "value=VALUES(value),metadata_body=VALUES(metadata_body)",
               {station, component.at("name").get<std::string>(),
                component.value("instance", std::string()), std::to_string(evse.value("id", 0)),
                std::to_string(evse.value("connectorId", 0)),
                variable.at("name").get<std::string>(), variable.value("instance", std::string()),
                attribute.value("type", std::string("Actual")), value(attribute, "value"),
                metadata.dump()});
}
void report(Database::Lease &db, const std::string &station, const Message &request) {
    const auto &p = request.payload;
    if (p.contains("requestId") && p.contains("seqNo")) {
        const auto existing =
            db.execute("SELECT report_body FROM cpp201_report WHERE station_id=? AND action=? AND "
                       "request_id=? AND seq_no=?",
                       {station, request.action, p.at("requestId").dump(), p.at("seqNo").dump()})
                .rows;
        if (!existing.empty()) {
            if (existing[0]["report_body"] != p.dump())
                throw ProtocolError("ProtocolError", "Conflicting report chunk");
            return;
        }
    }
    db.execute(
        "INSERT INTO cpp201_report(station_id,action,request_id,seq_no,final_chunk,report_body) "
        "VALUES(?,?,?,?,?,?)",
        {station, request.action, value(p, "requestId"), value(p, "seqNo"),
         p.value("tbc", false) ? "0" : "1", p.dump()});
}
void meter201(Database::Lease &db, const std::string &station, int evse, const Json &meters,
              const std::optional<std::string> &transaction) {
    const std::string prefix =
        "INSERT INTO cpp201_meter(station_id,evse_id,transaction_id,sample_timestamp,meter_body) "
        "VALUES";
    std::string sql = prefix;
    Parameters parameters;
    for (const auto &meter : meters) {
        if (!parameters.empty())
            sql += ',';
        sql += "(?,?,?,?,?)";
        const Parameters row{station, std::to_string(evse), transaction,
                             sql_time(meter.at("timestamp").get<std::string>()), meter.dump()};
        parameters.insert(parameters.end(), row.begin(), row.end());
        if (parameters.size() == 320) {
            db.execute(sql, parameters);
            sql = prefix;
            parameters.clear();
        }
    }
    if (!parameters.empty())
        db.execute(sql, parameters);
}
} // namespace
Json Service::token_info201(Database::Lease &db, const Json &token) {
    const auto rows =
        db.execute("SELECT status,expiry_timestamp,group_token_body,"
                   "(expiry_timestamp IS NOT NULL AND expiry_timestamp<UTC_TIMESTAMP(6)) expired "
                   "FROM cpp201_id_token WHERE token=? AND token_type=?",
                   {token.at("idToken").get<std::string>(), token.at("type").get<std::string>()})
            .rows;
    if (rows.empty()) {
        if (token.at("type") == "ISO14443" || token.at("type") == "ISO15693") {
            const auto legacy = tag_info(db, token.at("idToken").get<std::string>());
            Json result = {{"status", legacy.at("status")}};
            if (legacy.contains("expiryDate"))
                result["cacheExpiryDateTime"] = legacy.at("expiryDate");
            return result;
        }
        return {{"status", "Unknown"}};
    }
    const auto &row = rows[0];
    Json info = {{"status", integer(row.at("expired")) ? Json("Expired") : row.at("status")}};
    if (!row.at("expiry_timestamp").is_null()) {
        auto timestamp = row.at("expiry_timestamp").get<std::string>();
        timestamp[10] = 'T';
        info["cacheExpiryDateTime"] = timestamp + 'Z';
    }
    if (!row.at("group_token_body").is_null())
        info["groupIdToken"] = Json::parse(row.at("group_token_body").get<std::string>());
    return info;
}
Json Service::call201(const std::string &station, const Message &request) {
    if (!schemas201_ || request.type != 2 || !station_id_valid(station) ||
        !incoming201_action(request.action))
        throw ProtocolError("NotImplemented", "Unsupported OCPP 2.0.1 action");
    schemas201_->validate(request.action, request.payload);
    auto db = database_.acquire();
    db.begin();
    const auto registered =
        db.execute("SELECT registration_status FROM charge_box WHERE charge_box_id=? FOR UPDATE",
                   {station})
            .rows;
    if (registered.empty())
        throw ProtocolError("SecurityError", "Unregistered station");
    if (request.action != "BootNotification" && registered[0]["registration_status"] != "Accepted")
        throw ProtocolError("SecurityError", "Station is not accepted");
    const auto body = Json::array({2, request.id, request.action, request.payload}).dump();
    const bool durable = request.action != "Authorize" && request.action != "BootNotification" &&
                         request.action != "Heartbeat";
    if (durable) {
        const auto previous = db.execute("SELECT request_body,response_body FROM cpp201_replay "
                                         "WHERE station_id=? AND message_id=?",
                                         {station, request.id})
                                  .rows;
        if (!previous.empty()) {
            if (previous[0]["request_body"] != body)
                throw ProtocolError("ProtocolError", "Message ID reused with different content");
            auto response = Json::parse(previous[0]["response_body"].get<std::string>());
            if (request.action == "TransactionEvent" && request.payload.contains("idToken"))
                response[2]["idTokenInfo"] = token_info201(db, request.payload.at("idToken"));
            db.commit();
            return response;
        }
    }
    const auto response = Json::array({3, request.id, dispatch201(db, station, request)});
    schemas201_->validate(request.action, response[2], true);
    if (durable)
        db.execute("INSERT INTO cpp201_replay(station_id,message_id,request_body,response_body) "
                   "VALUES(?,?,?,?)",
                   {station, request.id, body, response.dump()});
    db.commit();
    return response;
}
Json Service::dispatch201(Database::Lease &db, const std::string &station, const Message &request) {
    const auto &p = request.payload;
    const auto &action = request.action;
    Json response = Json::object();
    bool publish = true;
    if (action == "BootNotification") {
        const auto &s = p.at("chargingStation");
        db.execute(
            "INSERT INTO cpp201_station(station_id,charging_station_body,boot_reason) "
            "VALUES(?,?,?) ON DUPLICATE KEY UPDATE "
            "charging_station_body=VALUES(charging_station_body),boot_reason=VALUES(boot_reason)",
            {station, s.dump(), p.at("reason").get<std::string>()});
        db.execute("UPDATE charge_box SET "
                   "ocpp_protocol='ocpp2.0.1',charge_point_vendor=LEFT(?,20),charge_point_model="
                   "LEFT(?,20),charge_point_serial_number=LEFT(?,25),fw_version=?,last_heartbeat_"
                   "timestamp=UTC_TIMESTAMP(6) WHERE charge_box_id=?",
                   {s.at("vendorName").get<std::string>(), s.at("model").get<std::string>(),
                    value(s, "serialNumber"), value(s, "firmwareVersion"), station});
        const auto status =
            db.execute("SELECT registration_status FROM charge_box WHERE charge_box_id=?",
                       {station})
                .rows[0]["registration_status"];
        const auto settings =
            db.execute("SELECT heartbeat_interval_in_seconds FROM settings LIMIT 1").rows;
        const auto interval =
            settings.empty() || settings[0]["heartbeat_interval_in_seconds"].is_null()
                ? 60
                : integer(settings[0]["heartbeat_interval_in_seconds"]);
        response = {{"status", status},
                    {"currentTime", utc_now()},
                    {"interval", interval > 0 && interval <= 86400 ? interval : 60}};
    } else if (action == "Heartbeat") {
        db.execute(
            "UPDATE charge_box SET last_heartbeat_timestamp=UTC_TIMESTAMP(6) WHERE charge_box_id=?",
            {station});
        response = {{"currentTime", utc_now()}};
        publish = false;
    } else if (action == "Authorize") {
        response = {{"idTokenInfo", token_info201(db, p.at("idToken"))}};
        publish = false;
        if (p.contains("certificate")) {
            response["certificateStatus"] = "NoCertificateAvailable";
            response["idTokenInfo"]["status"] = "Invalid";
        }
    } else if (action == "TransactionEvent") {
        const auto &info = p.at("transactionInfo");
        const auto tx = info.at("transactionId").get<std::string>();
        const auto seq = p.at("seqNo").dump();
        const auto previous = db.execute("SELECT event_body FROM cpp201_transaction_event WHERE "
                                         "station_id=? AND transaction_id=? AND seq_no=?",
                                         {station, tx, seq})
                                  .rows;
        if (!previous.empty()) {
            if (previous[0]["event_body"] != p.dump())
                throw ProtocolError("ProtocolError", "Conflicting transaction sequence number");
            publish = false;
        } else {
            db.execute("INSERT INTO "
                       "cpp201_transaction_event(station_id,transaction_id,seq_no,event_type,event_"
                       "timestamp,event_body) VALUES(?,?,?,?,?,?)",
                       {station, tx, seq, p.at("eventType").get<std::string>(),
                        sql_time(p.at("timestamp").get<std::string>()), p.dump()});
            const auto evse = p.value("evse", Json::object());
            const auto start =
                p.at("eventType") == "Started" ? value(p, "timestamp") : std::nullopt;
            const auto end = p.at("eventType") == "Ended" ? value(p, "timestamp") : std::nullopt;
            db.execute("INSERT INTO "
                       "cpp201_transaction(station_id,transaction_id,evse_id,connector_id,token_"
                       "body,charging_state,start_timestamp,end_timestamp,last_seq_no,latest_body) "
                       "VALUES(?,?,?,?,?,?,?,?,?,?) ON DUPLICATE KEY UPDATE "
                       "start_timestamp=COALESCE(start_timestamp,VALUES(start_timestamp)),end_"
                       "timestamp=COALESCE(end_timestamp,VALUES(end_timestamp)),"
                       "evse_id=COALESCE(evse_id,VALUES(evse_id)),connector_id=COALESCE(connector_"
                       "id,VALUES(connector_id)),"
                       "token_body=COALESCE(token_body,VALUES(token_body)),charging_state=IF("
                       "VALUES(last_seq_no)>=last_seq_no,VALUES(charging_state),charging_state),"
                       "latest_body=IF(VALUES(last_seq_no)>=last_seq_no,VALUES(latest_body),latest_"
                       "body),last_seq_no=GREATEST(last_seq_no,VALUES(last_seq_no))",
                       {station, tx, value(evse, "id"), value(evse, "connectorId"),
                        p.contains("idToken") ? std::optional<std::string>(p.at("idToken").dump())
                                              : std::nullopt,
                        value(info, "chargingState"),
                        start ? std::optional<std::string>(sql_time(*start)) : std::nullopt,
                        end ? std::optional<std::string>(sql_time(*end)) : std::nullopt, seq,
                        p.dump()});
            if (p.contains("meterValue"))
                meter201(db, station, evse.value("id", 0), p.at("meterValue"), tx);
            if (p.at("eventType") == "Ended")
                db.execute("DELETE FROM cpp201_profile WHERE station_id=? AND purpose='TxProfile' "
                           "AND JSON_UNQUOTE(JSON_EXTRACT(profile_body,'$.transactionId'))=?",
                           {station, tx});
            if (p.at("eventType") == "Started" && p.contains("reservationId")) {
                const auto rows =
                    db.execute("SELECT request_body,expiry_timestamp FROM cpp201_reservation WHERE "
                               "station_id=? AND reservation_id=? AND state IN "
                               "('Accepted','Pending','Unknown')",
                               {station, p.at("reservationId").dump()})
                        .rows;
                if (!rows.empty()) {
                    const auto reserved = Json::parse(rows[0]["request_body"].get<std::string>());
                    if (p.contains("idToken") && p.at("idToken") == reserved.at("idToken") &&
                        sql_time(p.at("timestamp").get<std::string>()) <=
                            rows[0]["expiry_timestamp"].get<std::string>())
                        db.execute("UPDATE cpp201_reservation SET state='Used' WHERE station_id=? "
                                   "AND reservation_id=?",
                                   {station, p.at("reservationId").dump()});
                }
            }
        }
        if (p.contains("idToken"))
            response["idTokenInfo"] = token_info201(db, p.at("idToken"));
    } else if (action == "StatusNotification") {
        if (integer(p.at("evseId")) <= 0 || integer(p.at("connectorId")) <= 0)
            throw ProtocolError("PropertyConstraintViolation",
                                "Physical EVSE and connector required");
        db.execute("INSERT INTO "
                   "cpp201_evse_status(station_id,evse_id,connector_id,connector_status,status_"
                   "timestamp) VALUES(?,?,?,?,?) "
                   "ON DUPLICATE KEY UPDATE "
                   "connector_status=IF(VALUES(status_timestamp)>=status_timestamp,VALUES("
                   "connector_status),connector_status),status_timestamp=GREATEST(status_timestamp,"
                   "VALUES(status_timestamp))",
                   {station, p.at("evseId").dump(), p.at("connectorId").dump(),
                    p.at("connectorStatus").get<std::string>(),
                    sql_time(p.at("timestamp").get<std::string>())});
    } else if (action == "MeterValues") {
        meter201(db, station, p.at("evseId").get<int>(), p.at("meterValue"), std::nullopt);
    } else if (action == "ReservationStatusUpdate") {
        db.execute("UPDATE cpp201_reservation SET state=? WHERE station_id=? AND reservation_id=? "
                   "AND state IN ('Pending','Accepted','Unknown')",
                   {p.at("reservationUpdateStatus").get<std::string>(), station,
                    p.at("reservationId").dump()});
    } else if (action == "SecurityEventNotification") {
        db.execute(
            "INSERT INTO cpp_security_event(station_id,event_type,event_timestamp,technical_info) "
            "VALUES(?,?,?,?)",
            {station, p.at("type").get<std::string>(),
             sql_time(p.at("timestamp").get<std::string>()), value(p, "techInfo")});
    } else if (action == "FirmwareStatusNotification" || action == "LogStatusNotification") {
        db.execute("INSERT INTO cpp_station_state(station_id) VALUES(?) ON DUPLICATE KEY UPDATE "
                   "station_id=station_id",
                   {station});
        if (action == "FirmwareStatusNotification")
            db.execute("UPDATE cpp_station_state SET firmware_status=?,firmware_request_id=? WHERE "
                       "station_id=?",
                       {p.at("status").get<std::string>(), value(p, "requestId"), station});
        else
            db.execute(
                "UPDATE cpp_station_state SET log_status=?,log_request_id=? WHERE station_id=?",
                {p.at("status").get<std::string>(), value(p, "requestId"), station});
    } else if (action == "SignCertificate") {
        const auto csr = p.at("csr").get<std::string>();
        if (!valid_csr(csr))
            response = {{"status", "Rejected"}};
        else {
            db.execute("INSERT INTO cpp_certificate_request(station_id,request_body) VALUES(?,?)",
                       {station, Json({{"protocol", "ocpp2.0.1"}, {"payload", p}}).dump()});
            response = {{"status", "Accepted"}};
        }
    } else if (action == "GetCertificateStatus") {
        // No OCSP result may be invented when a trusted responder is unavailable.
        response = {{"status", "Failed"}, {"statusInfo", {{"reasonCode", "NoTrustedResponder"}}}};
        report(db, station, request);
    } else if (action == "Get15118EVCertificate") {
        response = {{"status", "Failed"},
                    {"exiResponse", ""},
                    {"statusInfo", {{"reasonCode", "NoContractProvider"}}}};
        report(db, station, request);
    } else if (action == "DataTransfer") {
        report(db, station, request);
        response = {{"status", "Accepted"}};
    } else {
        report(db, station, request);
        if (action == "NotifyReport" && p.contains("reportData"))
            for (const auto &entry : p.at("reportData"))
                for (const auto &attribute : entry.at("variableAttribute"))
                    device_variable(db, station, entry.at("component"), entry.at("variable"),
                                    attribute, entry);
        if (action == "NotifyEVChargingNeeds" || action == "NotifyEVChargingSchedule")
            response = {{"status", "Accepted"}};
    }
    if (publish)
        db.execute("INSERT INTO cpp_event_outbox(station_id,action,payload) VALUES(?,?,?)",
                   {station, "ocpp2.0.1:" + action, p.dump()});
    return response;
}
void Service::prepare201(Database::Lease &db, const std::string &station, const std::string &action,
                         const Json &p) {
    if (action == "RequestStopTransaction" ||
        (action == "SetChargingProfile" && p.at("chargingProfile").contains("transactionId"))) {
        const auto tx = action == "RequestStopTransaction"
                            ? p.at("transactionId").get<std::string>()
                            : p.at("chargingProfile").at("transactionId").get<std::string>();
        if (db.execute("SELECT transaction_id FROM cpp201_transaction WHERE station_id=? AND "
                       "transaction_id=? AND end_timestamp IS NULL",
                       {station, tx})
                .rows.empty())
            throw ProtocolError("SecurityError", "No active transaction owned by this station");
    }
    if (action == "ReserveNow") {
        const auto id = p.at("id").dump();
        if (!db.execute("SELECT reservation_id FROM cpp201_reservation WHERE station_id=? AND "
                        "reservation_id=?",
                        {station, id})
                 .rows.empty())
            throw ProtocolError("PropertyConstraintViolation", "Reservation ID already in use");
        db.execute("INSERT INTO "
                   "cpp201_reservation(station_id,reservation_id,evse_id,expiry_timestamp,request_"
                   "body) VALUES(?,?,?,?,?)",
                   {station, id, value(p, "evseId"),
                    sql_time(p.at("expiryDateTime").get<std::string>()), p.dump()});
    } else if (action == "CancelReservation") {
        if (db.execute("SELECT reservation_id FROM cpp201_reservation WHERE station_id=? AND "
                       "reservation_id=? AND state IN ('Accepted','Unknown')",
                       {station, p.at("reservationId").dump()})
                .rows.empty())
            throw ProtocolError("SecurityError", "No accepted reservation owned by this station");
    } else if (action == "SendLocalList") {
        const auto rows =
            db.execute("SELECT local_list_version FROM cpp_station_state WHERE station_id=?",
                       {station})
                .rows;
        if (p.at("updateType") == "Differential" &&
            (rows.empty() || integer(rows[0]["local_list_version"]) < 0))
            throw ProtocolError("PropertyConstraintViolation",
                                "Fetch or send a full local list first");
        if (!rows.empty() &&
            integer(rows[0]["local_list_version"]) >= integer(p.at("versionNumber")))
            throw ProtocolError("PropertyConstraintViolation", "Local-list version must increase");
    }
}
void Service::finish201(Database::Lease &db, const std::string &station, const std::string &action,
                        const Json &request, int status, const Json &result) {
    const bool accepted =
        status == 200 && result.value("status", std::string("Accepted")) == "Accepted";
    const auto error = result.value("error", std::string());
    const bool uncertain =
        status == 504 || status == 409 || (status == 502 && error == "invalid_station_response");
    if (action == "ReserveNow")
        db.execute(
            "UPDATE cpp201_reservation SET state=? WHERE station_id=? AND reservation_id=? AND "
            "state='Pending'",
            {accepted ? "Accepted" : (uncertain ? "Unknown" : "Rejected"), station,
             request.at("id").dump()});
    else if (action == "CancelReservation" && accepted)
        db.execute("UPDATE cpp201_reservation SET state='Cancelled' WHERE station_id=? AND "
                   "reservation_id=?",
                   {station, request.at("reservationId").dump()});
    else if (action == "SetChargingProfile" && accepted) {
        const auto &p = request.at("chargingProfile");
        db.execute("DELETE FROM cpp201_profile WHERE station_id=? AND evse_id=? AND (profile_id=? "
                   "OR (purpose=? AND stack_level=?))",
                   {station, request.at("evseId").dump(), p.at("id").dump(),
                    p.at("chargingProfilePurpose").get<std::string>(), p.at("stackLevel").dump()});
        db.execute("INSERT INTO "
                   "cpp201_profile(station_id,evse_id,profile_id,purpose,stack_level,profile_body) "
                   "VALUES(?,?,?,?,?,?)",
                   {station, request.at("evseId").dump(), p.at("id").dump(),
                    p.at("chargingProfilePurpose").get<std::string>(), p.at("stackLevel").dump(),
                    p.dump()});
    } else if (action == "ClearChargingProfile" && accepted) {
        std::string sql = "DELETE FROM cpp201_profile WHERE station_id=?";
        Parameters parameters{station};
        if (request.contains("chargingProfileId")) {
            sql += " AND profile_id=?";
            parameters.push_back(request.at("chargingProfileId").dump());
        }
        if (request.contains("chargingProfileCriteria")) {
            const auto &criteria = request.at("chargingProfileCriteria");
            for (const auto &[field, column] : std::vector<std::pair<std::string, std::string>>{
                     {"evseId", "evse_id"},
                     {"chargingProfilePurpose", "purpose"},
                     {"stackLevel", "stack_level"}})
                if (criteria.contains(field)) {
                    sql += " AND " + column + "=?";
                    parameters.push_back(value(criteria, field.c_str()));
                }
        }
        db.execute(sql, parameters);
    } else if (status == 200 && (action == "GetVariables" || action == "SetVariables")) {
        const auto &results =
            result.at(action == "GetVariables" ? "getVariableResult" : "setVariableResult");
        const auto &requests =
            request.at(action == "GetVariables" ? "getVariableData" : "setVariableData");
        for (const auto &entry : results) {
            if (entry.at("attributeStatus") != "Accepted")
                continue;
            Json attribute = {{"type", entry.value("attributeType", std::string("Actual"))}};
            if (action == "GetVariables")
                attribute["value"] = entry.at("attributeValue");
            else {
                for (const auto &sent : requests)
                    if (sent.at("component") == entry.at("component") &&
                        sent.at("variable") == entry.at("variable") &&
                        sent.value("attributeType", std::string("Actual")) ==
                            attribute.at("type").get<std::string>())
                        attribute["value"] = sent.at("attributeValue");
                if (!attribute.contains("value"))
                    throw ProtocolError("ProtocolError", "Uncorrelated variable result");
            }
            device_variable(db, station, entry.at("component"), entry.at("variable"), attribute,
                            entry);
        }
    } else if (status == 200 && action == "GetLocalListVersion")
        db.execute("UPDATE cpp_station_state SET local_list_version=? WHERE station_id=?",
                   {result.at("versionNumber").dump(), station});
    else if (accepted && action == "SendLocalList") {
        if (request.at("updateType") == "Full")
            db.execute("DELETE FROM cpp201_local_authorization WHERE station_id=?", {station});
        if (request.contains("localAuthorizationList")) {
            for (const auto &entry : request.at("localAuthorizationList")) {
                const auto &token = entry.at("idToken");
                if (!entry.contains("idTokenInfo"))
                    db.execute("DELETE FROM cpp201_local_authorization WHERE station_id=? AND "
                               "token=? AND token_type=?",
                               {station, token.at("idToken").get<std::string>(),
                                token.at("type").get<std::string>()});
                else
                    db.execute(
                        "INSERT INTO "
                        "cpp201_local_authorization(station_id,token,token_type,info_body) "
                        "VALUES(?,?,?,?) ON DUPLICATE KEY UPDATE info_body=VALUES(info_body)",
                        {station, token.at("idToken").get<std::string>(),
                         token.at("type").get<std::string>(), entry.at("idTokenInfo").dump()});
            }
        }
        db.execute("UPDATE cpp_station_state SET local_list_version=? WHERE station_id=?",
                   {request.at("versionNumber").dump(), station});
    }
}
void Service::upsert_token201(const Json &p) {
    if (!schemas201_)
        throw ProtocolError("NotSupported", "OCPP 2.0.1 is unavailable");
    schemas201_->validate("Authorize", {{"idToken", p.at("idToken")}});
    schemas201_->validate("Authorize", {{"idTokenInfo", p.at("idTokenInfo")}}, true);
    const auto &token = p.at("idToken");
    const auto &info = p.at("idTokenInfo");
    auto expiry = value(info, "cacheExpiryDateTime");
    if (expiry)
        expiry = sql_time(*expiry);
    auto db = database_.acquire();
    db.begin();
    db.execute(
        "INSERT INTO cpp201_id_token(token,token_type,status,expiry_timestamp,group_token_body) "
        "VALUES(?,?,?,?,?) "
        "ON DUPLICATE KEY UPDATE "
        "status=VALUES(status),expiry_timestamp=VALUES(expiry_timestamp),group_token_body=VALUES("
        "group_token_body)",
        {token.at("idToken").get<std::string>(), token.at("type").get<std::string>(),
         info.at("status").get<std::string>(), expiry,
         info.contains("groupIdToken") ? std::optional<std::string>(info.at("groupIdToken").dump())
                                       : std::nullopt});
    db.execute("INSERT INTO cpp_audit(action,station_id,command_id) VALUES('UpsertIdToken','','')");
    db.commit();
}
} // namespace ocpp
