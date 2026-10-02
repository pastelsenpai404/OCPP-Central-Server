#include "ocpp/application/central_system.hpp"
#include "ocpp/protocol/commands.hpp"

namespace ocpp {
namespace {
std::optional<std::string> parameter(const Json &payload, const char *field) {
    if (!payload.contains(field))
        return std::nullopt;
    return payload.at(field).is_string() ? payload.at(field).get<std::string>()
                                         : payload.at(field).dump();
}
void owned_transaction(Database::Lease &db, const std::string &station, const Json &id) {
    const auto rows =
        db.execute("SELECT t.transaction_pk FROM transaction_start t JOIN connector c ON "
                   "c.connector_pk=t.connector_pk "
                   "LEFT JOIN transaction_stop s ON s.transaction_pk=t.transaction_pk "
                   "WHERE t.transaction_pk=? AND c.charge_box_id=? AND s.transaction_pk IS NULL",
                   {id.dump(), station})
            .rows;
    if (rows.empty())
        throw ProtocolError("SecurityError", "No active transaction owned by this station");
}
} // namespace
void Service::prepare_command(const std::string &station, const std::string &action,
                              const std::string &id, const Json &payload,
                              const std::string &protocol) {
    auto db = database_.acquire();
    db.begin();
    if (db.execute("SELECT charge_box_pk FROM charge_box WHERE charge_box_id=? FOR UPDATE",
                   {station})
            .rows.empty())
        throw ProtocolError("SecurityError", "Unregistered station");
    const bool serialized = action == "ReserveNow" || action == "CancelReservation" ||
                            action == "SendLocalList" || action == "GetLocalListVersion" ||
                            action == "SetChargingProfile" || action == "ClearChargingProfile";
    if (serialized &&
        !db.execute("SELECT command_id FROM cpp_command_task WHERE station_id=? AND "
                    "state='Pending' AND action IN "
                    "('ReserveNow','CancelReservation','SendLocalList','GetLocalListVersion','"
                    "SetChargingProfile','ClearChargingProfile') LIMIT 1",
                    {station})
             .rows.empty())
        throw ProtocolError("OccurrenceConstraintViolation",
                            "A state-changing command is already pending");
    if (protocol == "ocpp2.0.1")
        prepare201(db, station, action, payload);
    else {
        if (action == "RemoteStopTransaction")
            owned_transaction(db, station, payload.at("transactionId"));
        if (action == "SetChargingProfile" &&
            payload.at("csChargingProfiles").contains("transactionId"))
            owned_transaction(db, station, payload.at("csChargingProfiles").at("transactionId"));
        if (action == "ReserveNow") {
            const auto c = connector(db, station, integer(payload.at("connectorId")));
            const auto tag = payload.at("idTag").get<std::string>();
            if (db.execute("SELECT id_tag FROM ocpp_tag WHERE id_tag=?", {tag}).rows.empty())
                throw ProtocolError("PropertyConstraintViolation",
                                    "Reservation requires a registered tag");
            const auto reservation = payload.at("reservationId").dump();
            if (!db.execute("SELECT reservation_pk FROM reservation WHERE reservation_pk=?",
                            {reservation})
                     .rows.empty())
                throw ProtocolError("PropertyConstraintViolation",
                                    "Reservation ID is already in use");
            db.execute(
                "INSERT INTO "
                "reservation(reservation_pk,connector_pk,id_tag,start_datetime,expiry_datetime,"
                "status) "
                "VALUES(?,?,?,UTC_TIMESTAMP(6),?,'Pending')",
                {reservation, c, tag, sql_time(payload.at("expiryDate").get<std::string>())});
        } else if (action == "CancelReservation") {
            if (db.execute("SELECT r.reservation_pk FROM reservation r JOIN connector c ON "
                           "c.connector_pk=r.connector_pk "
                           "WHERE r.reservation_pk=? AND c.charge_box_id=? AND r.status IN "
                           "('Accepted','Unknown')",
                           {payload.at("reservationId").dump(), station})
                    .rows.empty())
                throw ProtocolError("SecurityError",
                                    "No accepted reservation owned by this station");
        } else if (action == "SendLocalList") {
            const auto rows =
                db.execute("SELECT local_list_version FROM cpp_station_state WHERE station_id=?",
                           {station})
                    .rows;
            if (payload.at("updateType") == "Differential" &&
                (rows.empty() || integer(rows[0]["local_list_version"]) < 0))
                throw ProtocolError("PropertyConstraintViolation",
                                    "Fetch or send a full local list first");
            if (!rows.empty() &&
                integer(rows[0]["local_list_version"]) >= integer(payload.at("listVersion")))
                throw ProtocolError("PropertyConstraintViolation",
                                    "Local-list version must increase");
        }
    }
    db.execute(
        "INSERT INTO cpp_command_task(command_id,station_id,action,protocol,request_body,state) "
        "VALUES(?,?,?,?,?,'Pending')",
        {id, station, action, protocol, payload.dump()});
    db.execute("INSERT INTO cpp_audit(action,station_id,command_id) VALUES(?,?,?)",
               {action, station, id});
    db.commit();
}
void Service::finish_command(const std::string &id, int http_status, const Json &result) {
    auto db = database_.acquire();
    db.begin();
    const auto tasks = db.execute("SELECT station_id,action,protocol,request_body,state FROM "
                                  "cpp_command_task WHERE command_id=? FOR UPDATE",
                                  {id})
                           .rows;
    if (tasks.empty() || tasks[0]["state"] != "Pending") {
        db.commit();
        return;
    }
    const auto station = tasks[0]["station_id"].get<std::string>();
    const auto action = tasks[0]["action"].get<std::string>();
    const auto request = Json::parse(tasks[0]["request_body"].get<std::string>());
    const bool successful = http_status == 200;
    const auto response_status = result.value("status", std::string("Accepted"));
    const bool accepted =
        successful && (response_status == "Accepted" || response_status == "RebootRequired" ||
                       response_status == "Unlocked" || response_status == "Scheduled" ||
                       response_status == "AcceptedCanceled");
    const auto result_error = result.value("error", std::string());
    const bool uncertain =
        http_status == 504 || (http_status == 409 && result_error == "station_disconnected") ||
        (http_status == 502 &&
         (result_error == "invalid_station_response" || result_error == "invalid_soap_response"));
    const auto state =
        uncertain ? "Uncertain" : (successful ? (accepted ? "Succeeded" : "Rejected") : "Failed");
    db.execute(
        "UPDATE cpp_command_task SET "
        "state=?,http_status=?,response_body=?,completed_at=UTC_TIMESTAMP(6) WHERE command_id=?",
        {state, std::to_string(http_status), result.dump(), id});
    db.execute("INSERT INTO cpp_station_state(station_id) VALUES(?) ON DUPLICATE KEY UPDATE "
               "station_id=station_id",
               {station});
    if (tasks[0]["protocol"] == "ocpp2.0.1")
        finish201(db, station, action, request, http_status, result);
    else if (action == "ReserveNow") {
        db.execute(
            "UPDATE reservation r JOIN connector c ON c.connector_pk=r.connector_pk SET r.status=? "
            "WHERE r.reservation_pk=? AND c.charge_box_id=? AND r.status='Pending'",
            {accepted ? "Accepted" : (uncertain ? "Unknown" : "Rejected"),
             request.at("reservationId").dump(), station});
    } else if (action == "CancelReservation" && accepted) {
        db.execute(
            "UPDATE reservation r JOIN connector c ON c.connector_pk=r.connector_pk SET "
            "r.status='Cancelled' "
            "WHERE r.reservation_pk=? AND c.charge_box_id=? AND r.status IN ('Accepted','Unknown')",
            {request.at("reservationId").dump(), station});
    } else if (action == "SetChargingProfile" && accepted) {
        const auto &p = request.at("csChargingProfiles");
        const auto connector_id = request.at("connectorId").dump();
        db.execute("DELETE FROM cpp_profile_assignment WHERE station_id=? AND connector_id=? "
                   "AND (profile_id=? OR (purpose=? AND stack_level=?))",
                   {station, connector_id, p.at("chargingProfileId").dump(),
                    p.at("chargingProfilePurpose").get<std::string>(), p.at("stackLevel").dump()});
        db.execute("INSERT INTO "
                   "cpp_profile_assignment(station_id,connector_id,profile_id,purpose,stack_level,"
                   "transaction_id,profile_body) "
                   "VALUES(?,?,?,?,?,?,?)",
                   {station, connector_id, p.at("chargingProfileId").dump(),
                    p.at("chargingProfilePurpose").get<std::string>(), p.at("stackLevel").dump(),
                    parameter(p, "transactionId"), p.dump()});
    } else if (action == "ClearChargingProfile" && accepted) {
        std::string sql = "DELETE FROM cpp_profile_assignment WHERE station_id=?";
        Parameters parameters{station};
        for (const auto &[field, column] :
             std::vector<std::pair<std::string, std::string>>{{"id", "profile_id"},
                                                              {"connectorId", "connector_id"},
                                                              {"chargingProfilePurpose", "purpose"},
                                                              {"stackLevel", "stack_level"}}) {
            if (request.contains(field)) {
                sql += " AND " + column + "=?";
                parameters.push_back(parameter(request, field.c_str()));
            }
        }
        db.execute(sql, parameters);
    } else if (action == "SendLocalList" && accepted) {
        if (request.at("updateType") == "Full")
            db.execute("DELETE FROM cpp_local_authorization WHERE station_id=?", {station});
        if (request.contains("localAuthorizationList")) {
            for (const auto &entry : request.at("localAuthorizationList")) {
                const auto tag = entry.at("idTag").get<std::string>();
                if (!entry.contains("idTagInfo"))
                    db.execute(
                        "DELETE FROM cpp_local_authorization WHERE station_id=? AND id_tag=?",
                        {station, tag});
                else
                    db.execute("INSERT INTO cpp_local_authorization(station_id,id_tag,info_body) "
                               "VALUES(?,?,?) "
                               "ON DUPLICATE KEY UPDATE info_body=VALUES(info_body)",
                               {station, tag, entry.at("idTagInfo").dump()});
            }
        }
        db.execute("UPDATE cpp_station_state SET local_list_version=? WHERE station_id=?",
                   {request.at("listVersion").dump(), station});
    } else if (action == "GetLocalListVersion" && successful) {
        db.execute("UPDATE cpp_station_state SET local_list_version=? WHERE station_id=?",
                   {result.at("listVersion").dump(), station});
    } else if (action == "GetConfiguration" && successful) {
        db.execute("UPDATE cpp_station_state SET configuration_body=? WHERE station_id=?",
                   {result.dump(), station});
    } else if (action == "GetInstalledCertificateIds" && successful) {
        db.execute("UPDATE cpp_station_state SET certificate_ids_body=? WHERE station_id=?",
                   {result.dump(), station});
    } else if (action == "GetCompositeSchedule" && accepted) {
        db.execute("UPDATE cpp_station_state SET composite_schedule_body=? WHERE station_id=?",
                   {result.dump(), station});
    }
    db.execute("INSERT INTO cpp_audit(action,station_id,command_id) VALUES(?,?,?)",
               {"Command:" + std::string(state), station, id});
    db.commit();
}
void Service::verify_schema() {
    auto db = database_.acquire();
    const auto rows =
        db.execute("SELECT COUNT(*) AS tables_present FROM information_schema.tables WHERE "
                   "table_schema=DATABASE() AND table_name IN ("
                   "'address','charge_box','ocpp_tag','connector','connector_status','transaction_"
                   "start','transaction_stop','connector_meter_value','reservation','settings',"
                   "'cpp_ocpp_replay','cpp_event_outbox','cpp_audit','cpp_command_task','cpp_"
                   "station_state','cpp_local_authorization','cpp_profile_assignment','cpp_"
                   "security_event','cpp_certificate_request',"
                   "'cpp201_transaction','cpp201_transaction_event','cpp201_replay','cpp201_evse_"
                   "status','cpp201_report','cpp201_meter','cpp201_id_token','cpp201_device_"
                   "variable','cpp201_local_authorization','cpp201_reservation','cpp201_profile',"
                   "'cpp_legacy_replay','cpp_billing_sandbox','cpp201_station')")
            .rows;
    if (rows.size() != 1 || integer(rows[0]["tables_present"]) != 33)
        throw std::runtime_error("Required OCPP migrations or legacy schema are missing");
}
void Service::recover_commands() {
    auto db = database_.acquire();
    db.begin();
    // Never resend ambiguous commands after a crash: the charger may have executed them.
    db.execute("UPDATE reservation SET status='Unknown' WHERE status='Pending'");
    db.execute("UPDATE cpp201_reservation SET state='Unknown' WHERE state='Pending'");
    db.execute("UPDATE cpp_command_task SET "
               "state='Uncertain',http_status=503,response_body=?,completed_at=UTC_TIMESTAMP(6) "
               "WHERE state='Pending'",
               {Json({{"error", "server_restarted"}}).dump()});
    db.commit();
}
Json Service::task(const std::string &id) {
    if (id.size() != 36)
        throw ProtocolError("PropertyConstraintViolation", "Invalid command ID");
    auto db = database_.acquire();
    auto rows = db.execute("SELECT "
                           "command_id,station_id,action,request_body,response_body,state,http_"
                           "status,created_at,completed_at "
                           "FROM cpp_command_task WHERE command_id=?",
                           {id})
                    .rows;
    if (rows.empty())
        throw ProtocolError("PropertyConstraintViolation", "Unknown command ID");
    if (rows[0]["action"] == "SetNetworkProfile")
        rows[0]["request_body"] = Json({{"redacted", true}}).dump();
    return rows[0];
}
Json Service::station_state(const std::string &id) {
    if (!station_id_valid(id))
        throw ProtocolError("PropertyConstraintViolation", "Invalid station ID");
    auto db = database_.acquire();
    return {
        {"station", id},
        {"state", db.execute("SELECT * FROM cpp_station_state WHERE station_id=?", {id}).rows},
        {"profiles", db.execute("SELECT connector_id,profile_id,purpose,stack_level,profile_body "
                                "FROM cpp_profile_assignment WHERE station_id=? LIMIT 256",
                                {id})
                         .rows},
        {"localAuthorizationList",
         db.execute(
               "SELECT id_tag,info_body FROM cpp_local_authorization WHERE station_id=? LIMIT 1000",
               {id})
             .rows}};
}
} // namespace ocpp
