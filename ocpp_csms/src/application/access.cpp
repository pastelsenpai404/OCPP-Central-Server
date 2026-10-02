#include "ocpp/application/central_system.hpp"

namespace ocpp {
static std::string string(const Json &j, const char *key) {
    return j.at(key).get<std::string>();
}
static std::optional<std::string> optional(const Json &j, const char *key) {
    if (!j.contains(key) || j[key].is_null())
        return std::nullopt;
    return j[key].is_string() ? j[key].get<std::string>() : j[key].dump();
}
bool Service::station_registered(const std::string &station) {
    auto db = database_.acquire();
    return !db.execute("SELECT charge_box_pk FROM charge_box WHERE charge_box_id=?", {station})
                .rows.empty();
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
