#include "ocpp/service.hpp"
#include <limits>

namespace ocpp {
static std::string string(const Json &j, const char *key) {
    return j.at(key).get<std::string>();
}
static std::optional<std::string> optional(const Json &j, const char *key) {
    if (!j.contains(key) || j[key].is_null())
        return std::nullopt;
    return j[key].is_string() ? j[key].get<std::string>() : j[key].dump();
}
Json Service::tag_info(Database::Lease &db, const std::string &tag) {
    const auto rows = db.execute("SELECT parent_id_tag, expiry_date, max_active_transaction_count, "
                                 "(expiry_date IS NOT NULL AND expiry_date < UTC_TIMESTAMP(6)) AS "
                                 "expired FROM ocpp_tag WHERE id_tag = ?",
                                 {tag})
                          .rows;
    if (rows.empty())
        return {{"status", "Invalid"}};
    const auto &r = rows[0];
    Json result = {{"status", integer(r["max_active_transaction_count"]) == 0 ? "Blocked"
                              : integer(r["expired"]) != 0                    ? "Expired"
                                                                              : "Accepted"}};
    if (!r["parent_id_tag"].is_null())
        result["parentIdTag"] = r["parent_id_tag"];
    // Dates returned as SQL datetime are converted to canonical OCPP UTC.
    if (!r["expiry_date"].is_null()) {
        auto time = r["expiry_date"].get<std::string>();
        time[10] = 'T';
        result["expiryDate"] = time + 'Z';
    }
    return result;
}
std::string Service::connector(Database::Lease &db, const std::string &station, std::int64_t id) {
    db.execute("INSERT INTO connector(charge_box_id, connector_id) VALUES(?, ?) ON DUPLICATE KEY "
               "UPDATE connector_pk=connector_pk",
               {station, std::to_string(id)});
    const auto rows =
        db.execute(
              "SELECT connector_pk FROM connector WHERE charge_box_id = ? AND connector_id = ?",
              {station, std::to_string(id)})
            .rows;
    if (rows.size() != 1)
        throw std::runtime_error("Connector lookup failed");
    return rows[0]["connector_pk"].get<std::string>();
}
void Service::meters(Database::Lease &db, const std::string &station, const std::string &c,
                     const Json &values, const std::optional<std::string> &tx) {
    if (tx) {
        const auto rows =
            db.execute(
                  "SELECT t.connector_pk FROM transaction_start t JOIN connector c ON "
                  "c.connector_pk=t.connector_pk WHERE t.transaction_pk=? AND c.charge_box_id=?",
                  {*tx, station})
                .rows;
        if (rows.empty() || rows[0]["connector_pk"] != c)
            throw ProtocolError("SecurityError", "Transaction ownership mismatch");
    }
    for (const auto &value : values) {
        const auto timestamp = sql_time(string(value, "timestamp"));
        for (const auto &sample : value.at("sampledValue")) {
            db.execute(
                "INSERT INTO "
                "connector_meter_value(connector_pk,transaction_pk,value_timestamp,value,reading_"
                "context,format,measurand,location,unit,phase) VALUES(?,?,?,?,?,?,?,?,?,?)",
                {c, tx, timestamp, string(sample, "value"),
                 sample.value("context", "Sample.Periodic"), sample.value("format", "Raw"),
                 sample.value("measurand", "Energy.Active.Import.Register"),
                 sample.value("location", "Outlet"), sample.value("unit", "Wh"),
                 optional(sample, "phase")});
        }
    }
}
bool Service::station_registered(const std::string &station) {
    auto db = database_.acquire();
    return !db.execute("SELECT charge_box_pk FROM charge_box WHERE charge_box_id=?", {station})
                .rows.empty();
}
Json Service::call(const std::string &station, const Message &request) {
    if (request.type != 2 || !station_id_valid(station))
        throw ProtocolError("SecurityError", "Invalid request context");
    schemas_.validate(request.action, request.payload);
    auto db = database_.acquire();
    db.begin();
    const auto registered =
        db.execute("SELECT registration_status FROM charge_box WHERE charge_box_id=? FOR UPDATE",
                   {station})
            .rows;
    if (registered.empty())
        throw ProtocolError("SecurityError", "Unregistered station");
    if (request.action != "BootNotification" && registered[0]["registration_status"] != "Accepted")
        throw ProtocolError("SecurityError", "Station registration is not accepted");
    const auto body = Json::array({2, request.id, request.action, request.payload}).dump();
    // Authorization and current-time responses must reflect current state, even
    // if a charger reuses an ID after reconnecting. Never cache an Accepted tag.
    const bool durable = request.action != "Authorize" && request.action != "Heartbeat" &&
                         request.action != "BootNotification";
    if (durable) {
        const auto previous = db.execute("SELECT request_body,response_body FROM cpp_ocpp_replay "
                                         "WHERE station_id=? AND message_id=?",
                                         {station, request.id})
                                  .rows;
        if (!previous.empty()) {
            if (previous[0]["request_body"] != body)
                throw ProtocolError("ProtocolError", "Message ID reused with different content");
            auto response = Json::parse(previous[0]["response_body"].get<std::string>());
            db.commit();
            return response;
        }
    }
    auto response = Json::array({3, request.id, dispatch(db, station, request)});
    schemas_.validate(request.action, response[2], true);
    if (durable)
        db.execute("INSERT INTO cpp_ocpp_replay(station_id,message_id,request_body,response_body) "
                   "VALUES(?,?,?,?)",
                   {station, request.id, body, response.dump()});
    db.commit();
    return response;
}
Json Service::dispatch(Database::Lease &db, const std::string &station, const Message &call) {
    const auto &p = call.payload;
    const auto &action = call.action;
    Json response = Json::object();
    bool publish = true;
    Json event = p;
    if (action == "BootNotification") {
        const auto rows =
            db.execute("SELECT registration_status FROM charge_box WHERE charge_box_id=?",
                       {station})
                .rows;
        db.execute(
            "UPDATE charge_box SET "
            "ocpp_protocol='ocpp1.6',charge_point_vendor=?,charge_point_model=?,charge_point_"
            "serial_number=?,charge_box_serial_number=?,fw_version=?,iccid=?,imsi=?,meter_type=?,"
            "meter_serial_number=?,last_heartbeat_timestamp=UTC_TIMESTAMP(6) WHERE charge_box_id=?",
            {string(p, "chargePointVendor"), string(p, "chargePointModel"),
             optional(p, "chargePointSerialNumber"), optional(p, "chargeBoxSerialNumber"),
             optional(p, "firmwareVersion"), optional(p, "iccid"), optional(p, "imsi"),
             optional(p, "meterType"), optional(p, "meterSerialNumber"), station});
        const auto settings =
            db.execute("SELECT heartbeat_interval_in_seconds FROM settings LIMIT 1").rows;
        auto interval = settings.empty() || settings[0]["heartbeat_interval_in_seconds"].is_null()
                            ? 60
                            : integer(settings[0]["heartbeat_interval_in_seconds"]);
        if (interval < 1 || interval > 86400)
            interval = 60;
        response = {{"status", rows[0]["registration_status"]},
                    {"currentTime", utc_now()},
                    {"interval", interval}};
    } else if (action == "Heartbeat") {
        db.execute(
            "UPDATE charge_box SET last_heartbeat_timestamp=UTC_TIMESTAMP(6) WHERE charge_box_id=?",
            {station});
        response = {{"currentTime", utc_now()}};
        publish = false;
    } else if (action == "Authorize") {
        response = {{"idTagInfo", tag_info(db, string(p, "idTag"))}};
    } else if (action == "StatusNotification") {
        const auto c = connector(db, station, integer(p["connectorId"]));
        db.execute("INSERT INTO "
                   "connector_status(connector_pk,status_timestamp,status,error_code,error_info,"
                   "vendor_id,vendor_error_code) VALUES(?,?,?,?,?,?,?)",
                   {c, sql_time(p.value("timestamp", utc_now())), string(p, "status"),
                    string(p, "errorCode"), optional(p, "info"), optional(p, "vendorId"),
                    optional(p, "vendorErrorCode")});
    } else if (action == "DiagnosticsStatusNotification") {
        db.execute(
            "UPDATE charge_box SET diagnostics_status=?,diagnostics_timestamp=UTC_TIMESTAMP(6) "
            "WHERE charge_box_id=?",
            {string(p, "status"), station});
    } else if (action == "FirmwareStatusNotification") {
        db.execute("UPDATE charge_box SET fw_update_status=?,fw_update_timestamp=UTC_TIMESTAMP(6) "
                   "WHERE charge_box_id=?",
                   {string(p, "status"), station});
    } else if (action == "DataTransfer") {
        // The legacy implementation also accepted all vendor data transfers.
        response = {{"status", "Accepted"}};
    } else if (action == "StartTransaction") {
        if (integer(p["connectorId"]) == 0)
            throw ProtocolError("PropertyConstraintViolation",
                                "Transaction requires a physical connector");
        const auto c = connector(db, station, integer(p["connectorId"]));
        const auto tag = string(p, "idTag");
        const auto auth = tag_info(db, tag);
        db.execute("INSERT INTO ocpp_tag(id_tag,max_active_transaction_count) VALUES(?,0) ON "
                   "DUPLICATE KEY UPDATE id_tag=id_tag",
                   {tag});
        const auto timestamp = sql_time(string(p, "timestamp"));
        const auto meter = std::to_string(integer(p["meterStart"]));
        const auto previous =
            db.execute(
                  "SELECT transaction_pk FROM transaction_start WHERE connector_pk=? AND id_tag=? "
                  "AND start_timestamp=? AND start_value=? ORDER BY transaction_pk LIMIT 1",
                  {c, tag, timestamp, meter})
                .rows;
        std::uint64_t tx = 0;
        if (previous.empty())
            tx = db.execute("INSERT INTO "
                            "transaction_start(connector_pk,id_tag,start_timestamp,start_value) "
                            "VALUES(?,?,?,?)",
                            {c, tag, timestamp, meter})
                     .inserted;
        else {
            tx = static_cast<std::uint64_t>(integer(previous[0]["transaction_pk"]));
            publish = false;
        }
        if (tx > 2147483647)
            throw std::runtime_error("OCPP transaction ID exhausted");
        if (p.contains("reservationId"))
            db.execute("UPDATE reservation SET transaction_pk=?,status='Used' WHERE "
                       "reservation_pk=? AND connector_pk=? AND id_tag=? AND status='Accepted'",
                       {std::to_string(tx), std::to_string(integer(p["reservationId"])), c, tag});
        if (publish) {
            db.execute(
                "INSERT INTO connector_status(connector_pk,status_timestamp,status,error_code) "
                "SELECT ?,?,'Charging','NoError' FROM charge_box WHERE charge_box_id=? AND "
                "insert_connector_status_after_transaction_msg=1",
                {c, timestamp, station});
        }
        response = {{"transactionId", tx}, {"idTagInfo", auth}};
        event["transactionId"] = tx;
        event["connectorPk"] = integer(c);
    } else if (action == "StopTransaction") {
        const auto tx = std::to_string(integer(p["transactionId"]));
        const auto owned = db.execute("SELECT t.connector_pk,t.start_value FROM transaction_start "
                                      "t JOIN connector c ON c.connector_pk=t.connector_pk WHERE "
                                      "t.transaction_pk=? AND c.charge_box_id=? FOR UPDATE",
                                      {tx, station})
                               .rows;
        if (owned.empty())
            throw ProtocolError("SecurityError", "Transaction ownership mismatch");
        const auto meter = std::to_string(integer(p["meterStop"]));
        const auto timestamp = sql_time(string(p, "timestamp"));
        const auto stops = db.execute("SELECT stop_value,stop_timestamp FROM transaction_stop "
                                      "WHERE transaction_pk=? ORDER BY event_timestamp LIMIT 1",
                                      {tx})
                               .rows;
        if (!stops.empty()) {
            auto stored = stops[0]["stop_timestamp"].get<std::string>();
            if (stored.size() == 19)
                stored += '.';
            stored.resize(26, '0');
            if (stops[0]["stop_value"] != meter || stored != timestamp)
                throw ProtocolError("ProtocolError", "Conflicting transaction stop");
            publish = false;
        } else {
            db.execute("INSERT INTO "
                       "transaction_stop(transaction_pk,event_actor,stop_timestamp,stop_value,stop_"
                       "reason) VALUES(?,'station',?,?,?)",
                       {tx, timestamp, meter, optional(p, "reason")});
            if (p.contains("transactionData"))
                meters(db, station, owned[0]["connector_pk"].get<std::string>(),
                       p["transactionData"], tx);
            db.execute(
                "INSERT INTO connector_status(connector_pk,status_timestamp,status,error_code) "
                "SELECT ?,?,'Finishing','NoError' FROM charge_box WHERE charge_box_id=? AND "
                "insert_connector_status_after_transaction_msg=1",
                {owned[0]["connector_pk"].get<std::string>(), timestamp, station});
        }
        if (p.contains("idTag"))
            response["idTagInfo"] = tag_info(db, string(p, "idTag"));
    } else if (action == "MeterValues") {
        const auto c = connector(db, station, integer(p["connectorId"]));
        meters(db, station, c, p.at("meterValue"), optional(p, "transactionId"));
    } else
        throw ProtocolError("NotImplemented", "Action is not implemented");
    if (publish)
        db.execute("INSERT INTO cpp_event_outbox(station_id,action,payload) VALUES(?,?,?)",
                   {station, action, event.dump()});
    return response;
}
Json Service::overview(std::string_view resource, unsigned offset) {
    if (offset > 1000000)
        throw ProtocolError("PropertyConstraintViolation", "Pagination limit");
    std::string sql;
    if (resource == "chargepoints")
        sql = "SELECT "
              "charge_box_pk,charge_box_id,registration_status,ocpp_protocol,charge_point_vendor,"
              "charge_point_model,last_heartbeat_timestamp FROM charge_box ORDER BY charge_box_pk "
              "LIMIT 100 OFFSET ?";
    else if (resource == "transactions")
        sql = "SELECT "
              "t.transaction_pk,t.id_tag,t.start_timestamp,t.start_value,c.charge_box_id,c."
              "connector_id FROM transaction_start t JOIN connector c ON "
              "c.connector_pk=t.connector_pk ORDER BY t.transaction_pk DESC LIMIT 100 OFFSET ?";
    else if (resource == "ocppTags")
        sql = "SELECT ocpp_tag_pk,id_tag,parent_id_tag,expiry_date,max_active_transaction_count "
              "FROM ocpp_tag ORDER BY ocpp_tag_pk LIMIT 100 OFFSET ?";
    else if (resource == "reservations")
        sql = "SELECT "
              "reservation_pk,connector_pk,transaction_pk,id_tag,start_datetime,expiry_datetime,"
              "status FROM reservation ORDER BY reservation_pk DESC LIMIT 100 OFFSET ?";
    else if (resource == "connectorStatus")
        sql = "SELECT c.charge_box_id,c.connector_id,s.status_timestamp,s.status,s.error_code FROM "
              "connector_status s JOIN connector c ON c.connector_pk=s.connector_pk ORDER BY "
              "s.status_timestamp DESC LIMIT 100 OFFSET ?";
    else if (resource == "audit")
        sql = "SELECT id,action,station_id,command_id,created_at FROM cpp_audit ORDER BY id DESC "
              "LIMIT 100 OFFSET ?";
    else
        throw ProtocolError("NotSupported", "Unknown resource");
    auto db = database_.acquire();
    return db.execute(sql, {std::to_string(offset)}).rows;
}
void Service::provision_station(const std::string &id) {
    if (!station_id_valid(id))
        throw ProtocolError("PropertyConstraintViolation", "Invalid station ID");
    auto db = database_.acquire();
    db.begin();
    db.execute("INSERT INTO charge_box(charge_box_id,registration_status) VALUES(?,'Accepted') ON "
               "DUPLICATE KEY UPDATE charge_box_id=charge_box_id",
               {id});
    db.execute(
        "INSERT INTO cpp_audit(action,station_id,command_id) VALUES('ProvisionStation',?,'')",
        {id});
    db.commit();
}
void Service::upsert_tag(const Json &p) {
    if (!p.is_object() || !p.contains("idTag") || !p["idTag"].is_string() ||
        string(p, "idTag").empty() || string(p, "idTag").size() > 20 ||
        !p.contains("maxActiveTransactions") || !p["maxActiveTransactions"].is_number_integer())
        throw ProtocolError("PropertyConstraintViolation", "Invalid tag");
    const auto limit = integer(p["maxActiveTransactions"]);
    if (limit < -1 || limit > 1000)
        throw ProtocolError("PropertyConstraintViolation", "Invalid tag limit");
    auto expiry = optional(p, "expiryDate");
    if (expiry)
        expiry = sql_time(*expiry);
    auto parent = optional(p, "parentIdTag");
    if (parent && (parent->empty() || parent->size() > 20))
        throw ProtocolError("PropertyConstraintViolation", "Invalid parent tag");
    auto db = database_.acquire();
    db.begin();
    db.execute(
        "INSERT INTO ocpp_tag(id_tag,parent_id_tag,expiry_date,max_active_transaction_count) "
        "VALUES(?,?,?,?) ON DUPLICATE KEY UPDATE "
        "parent_id_tag=VALUES(parent_id_tag),expiry_date=VALUES(expiry_date),max_active_"
        "transaction_count=VALUES(max_active_transaction_count)",
        {string(p, "idTag"), parent, expiry, std::to_string(limit)});
    db.execute("INSERT INTO cpp_audit(action,station_id,command_id) VALUES('UpsertTag','','')");
    db.commit();
}
void Service::audit(std::string_view action, const std::string &station, std::string_view id) {
    auto db = database_.acquire();
    db.execute("INSERT INTO cpp_audit(action,station_id,command_id) VALUES(?,?,?)",
               {std::string(action), station, std::string(id)});
}
} // namespace ocpp
