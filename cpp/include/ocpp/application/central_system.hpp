#pragma once
#include "ocpp/persistence/database.hpp"
namespace ocpp {
class Service {
    Database &database_;
    const Schemas &schemas_;
    const Schemas *schemas201_;
    Json dispatch(Database::Lease &db, const std::string &station, const Message &call);
    Json tag_info(Database::Lease &db, const std::string &tag);
    std::string connector(Database::Lease &db, const std::string &station, std::int64_t id);
    void meters(Database::Lease &db, const std::string &station, const std::string &connector,
                const Json &values, const std::optional<std::string> &tx);
    Json dispatch201(Database::Lease &db, const std::string &station, const Message &call);
    Json token_info201(Database::Lease &db, const Json &token);
    void prepare201(Database::Lease &db, const std::string &station, const std::string &action,
                    const Json &payload);
    void finish201(Database::Lease &db, const std::string &station, const std::string &action,
                   const Json &request, int status, const Json &result);

  public:
    Service(Database &database, const Schemas &schemas, const Schemas *schemas201 = nullptr)
        : database_(database), schemas_(schemas), schemas201_(schemas201) {}
    Json call(const std::string &station, const Message &request,
              const std::string &protocol = "ocpp1.6");
    Json call201(const std::string &station, const Message &request);
    void upsert_token201(const Json &token);
    bool station_registered(const std::string &station);
    Json overview(std::string_view resource, unsigned offset);
    void provision_station(const std::string &id);
    void upsert_tag(const Json &payload);
    void audit(std::string_view action, const std::string &station, std::string_view command_id);
    void prepare_command(const std::string &station, const std::string &action,
                         const std::string &id, const Json &payload,
                         const std::string &protocol = "ocpp1.6");
    void finish_command(const std::string &id, int http_status, const Json &result);
    void recover_commands();
    void verify_schema();
    Json task(const std::string &id);
    Json station_state(const std::string &id);
    Json settle_sandbox(const Json &request);
};
} // namespace ocpp
