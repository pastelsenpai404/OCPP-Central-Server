#pragma once
#include "ocpp/protocol/message.hpp"
#include <set>

namespace ocpp {
struct Config {
    std::string bind = "127.0.0.1";
    unsigned short port = 5003;
    unsigned workers = 4;
    std::string db_host = "127.0.0.1", db_name, db_user, db_password, db_ca;
    unsigned short db_port = 3306;
    Json station_secrets;
    std::set<std::string> transfer_origins;
    std::set<std::string> soap_origins;
    Json soap_endpoints = Json::object();
    std::string read_token, operator_token, admin_token;
    static Config environment();
    int role(std::string_view authorization) const;
};
} // namespace ocpp
