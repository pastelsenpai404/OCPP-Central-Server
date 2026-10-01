#include "server/runtime.hpp"
#include <fstream>

namespace ocpp::server {
std::unique_ptr<Runtime> runtime;
Runtime::Runtime(Config c, const std::filesystem::path &path)
    : config(std::move(c)), schemas(path), schemas201(path / "2.0.1", true), database(config),
      service(database, schemas, &schemas201), executor(config.workers, 256) {
    for (const auto &[version, count] :
         std::map<std::string, std::size_t>{{"1.2", 36}, {"1.5", 48}, {"1.6", 56}}) {
        legacy_schemas.emplace(version, std::make_unique<Schemas>(path / version, false, count));
        soap_codecs.emplace(version, std::make_unique<SoapCodec>(path / version / "soap.registry"));
    }
    for (const auto &version : {"1.2", "1.5", "1.6", "2.0.1"}) {
        const auto directory = std::string_view(version) == "1.6" ? path : path / version;
        ocpp::Json entries = ocpp::Json::array();
        for (const auto &file : std::filesystem::directory_iterator(directory)) {
            if (file.path().extension() != ".json")
                continue;
            auto action = file.path().stem().string();
            if (std::string_view(version) == "2.0.1") {
                if (!action.ends_with("Request"))
                    continue;
                action.resize(action.size() - 7);
                if (incoming201_action(action) && action != "DataTransfer")
                    continue;
            } else if (!outbound_action(action))
                continue;
            ocpp::Json schema;
            std::ifstream input(
                (std::string_view(version) == "1.2" || std::string_view(version) == "1.5")
                    ? path / (action + ".json")
                    : file.path());
            input >> schema;
            entries.push_back(
                {{"action", action},
                 {"schema", schema},
                 {"requiredRole", action == "CertificateSigned" || action == "InstallCertificate" ||
                                          action == "DeleteCertificate" ||
                                          action == "SetNetworkProfile"
                                      ? 3
                                      : 2}});
        }
        command_catalog[version] = std::move(entries);
    }
}
std::shared_ptr<Session> Runtime::find(const std::string &id) {
    std::lock_guard lock(mutex);
    const auto it = sessions.find(id);
    return it == sessions.end() ? nullptr : it->second;
}
bool Runtime::admit_api() {
    std::lock_guard lock(mutex);
    return api_rate.allow();
}
} // namespace ocpp::server
