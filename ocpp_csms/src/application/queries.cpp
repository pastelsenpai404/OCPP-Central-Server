#include "ocpp/application/central_system.hpp"

namespace ocpp {
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
              "connector_id,s.stop_timestamp,s.stop_value,s.stop_reason,"
              "CASE WHEN s.transaction_pk IS NULL THEN 'Active' ELSE 'Ended' END AS state "
              "FROM transaction_start t JOIN connector c ON c.connector_pk=t.connector_pk "
              "LEFT JOIN transaction_stop s ON s.transaction_pk=t.transaction_pk AND "
              "s.event_timestamp=(SELECT MAX(latest.event_timestamp) FROM transaction_stop latest "
              "WHERE latest.transaction_pk=t.transaction_pk) "
              "ORDER BY t.transaction_pk DESC LIMIT 100 OFFSET ?";
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
    else if (resource == "tasks")
        sql = "SELECT command_id,station_id,action,state,http_status,created_at,completed_at FROM "
              "cpp_command_task ORDER BY created_at DESC LIMIT 100 OFFSET ?";
    else if (resource == "securityEvents")
        sql = "SELECT id,station_id,event_type,event_timestamp,technical_info,received_at FROM "
              "cpp_security_event ORDER BY id DESC LIMIT 100 OFFSET ?";
    else if (resource == "certificateRequests")
        sql = "SELECT id,station_id,state,created_at FROM cpp_certificate_request ORDER BY id DESC "
              "LIMIT 100 OFFSET ?";
    else if (resource == "transactions201")
        sql = "SELECT "
              "station_id,transaction_id,evse_id,connector_id,charging_state,start_timestamp,end_"
              "timestamp,last_seq_no FROM cpp201_transaction ORDER BY "
              "COALESCE(end_timestamp,start_timestamp) DESC LIMIT 100 OFFSET ?";
    else if (resource == "stations201")
        sql = "SELECT station_id,charging_station_body,boot_reason,updated_at FROM cpp201_station "
              "ORDER BY station_id LIMIT 100 OFFSET ?";
    else if (resource == "reports201")
        sql = "SELECT id,station_id,action,request_id,seq_no,final_chunk,report_body FROM "
              "cpp201_report ORDER BY id DESC LIMIT 100 OFFSET ?";
    else if (resource == "deviceModel201")
        sql = "SELECT * FROM cpp201_device_variable ORDER BY "
              "station_id,component_name,variable_name LIMIT 100 OFFSET ?";
    else if (resource == "evseStatus201")
        sql = "SELECT * FROM cpp201_evse_status ORDER BY station_id,evse_id,connector_id LIMIT 100 "
              "OFFSET ?";
    else if (resource == "reservations201")
        sql = "SELECT * FROM cpp201_reservation ORDER BY expiry_timestamp DESC LIMIT 100 OFFSET ?";
    else if (resource == "billingSandbox")
        sql = "SELECT case_id,request_body,response_body,created_at FROM cpp_billing_sandbox ORDER "
              "BY created_at DESC LIMIT 100 OFFSET ?";
    else if (resource == "profiles201")
        sql = "SELECT * FROM cpp201_profile ORDER BY station_id,evse_id,profile_id LIMIT 100 "
              "OFFSET ?";
    else if (resource == "idTokens")
        sql = "SELECT token,token_type,status,expiry_timestamp,group_token_body FROM "
              "cpp201_id_token ORDER BY token LIMIT 100 OFFSET ?";
    else
        throw ProtocolError("NotSupported", "Unknown resource");
    auto db = database_.acquire();
    return db.execute(sql, {std::to_string(offset)}).rows;
}
} // namespace ocpp
