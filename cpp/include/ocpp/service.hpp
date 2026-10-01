#pragma once
#include "mysql.hpp"
namespace ocpp {
class Service {
    Database &database_;
    const Schemas &schemas_;
    Json dispatch(Database::Lease &db, const std::string &station, const Message &call);
    Json tag_info(Database::Lease &db, const std::string &tag);
    std::string connector(Database::Lease &db, const std::string &station, std::int64_t id);
    void meters(Database::Lease &db, const std::string &station, const std::string &connector,
                const Json &values, const std::optional<std::string> &tx);

  public:
    Service(Database &database, const Schemas &schemas) : database_(database), schemas_(schemas) {}
    Json call(const std::string &station, const Message &request);
    bool station_registered(const std::string &station);
    Json overview(std::string_view resource, unsigned offset);
    void provision_station(const std::string &id);
    void upsert_tag(const Json &payload);
    void audit(std::string_view action, const std::string &station, std::string_view command_id);
};
} // namespace ocpp
