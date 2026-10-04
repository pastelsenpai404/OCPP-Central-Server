#include "billing/management/interfaces/routes.hpp"
#include <charconv>
#include <set>
namespace billing::management {
void install_routes(httplib::Server &server, Backoffice &service) {
    auto handler = [&service](const httplib::Request &request, httplib::Response &response) {
        try {
            Json input = Json::object();
            if (request.method != "GET") {
                const auto type = request.get_header_value("Content-Type");
                if (type != "application/json" && type != "application/json; charset=utf-8")
                    throw Problem(415, "JSON required");
                std::map<int, std::set<std::string>> keys_at_depth;
                input = Json::parse(
                    request.body, [&](int depth, Json::parse_event_t event, Json &value) {
                        if (depth > 8)
                            throw Problem(400, "JSON too deep");
                        if (event == Json::parse_event_t::object_start)
                            keys_at_depth[depth + 1].clear();
                        if (event == Json::parse_event_t::key &&
                            !keys_at_depth[depth].insert(value.get<std::string>()).second)
                            throw Problem(400, "Duplicate JSON key");
                        return true;
                    });
                if (!input.is_object())
                    throw Problem(400, "JSON object required");
            }
            int offset = 0;
            if (request.has_param("offset")) {
                const auto raw = request.get_param_value("offset");
                const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), offset);
                if (parsed.ec != std::errc{} || parsed.ptr != raw.data() + raw.size())
                    throw Problem(400, "Invalid offset");
            }
            auto result = service.handle(
                request.method, request.path.substr(std::string("/api/v1/management").size()),
                input, response.get_header_value("X-Billing-Role"),
                request.get_header_value("Idempotency-Key"), request.get_param_value("q"), offset);
            response.set_content(result.dump(), "application/json");
        } catch (const Problem &e) {
            response.status = e.status;
            response.set_content(Json{{"error", e.what()}}.dump(), "application/json");
        } catch (const Json::exception &) {
            response.status = 400;
            response.set_content("{\"error\":\"Invalid JSON or field type\"}", "application/json");
        } catch (const std::exception &) {
            response.status = 500;
            response.set_content("{\"error\":\"Internal operation failed\"}", "application/json");
        }
    };
    const std::string path = R"(/api/v1/management/[a-z0-9/-]+)";
    server.Get(path, handler);
    server.Post(path, handler);
    server.Patch(path, handler);
    server.Delete(path, handler);
}
} // namespace billing::management
