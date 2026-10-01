#include "ocpp/legacy.hpp"
#include <charconv>
#include <cmath>
#include <fstream>
#include <pugixml.hpp>
#include <set>
#include <sstream>

namespace ocpp {
namespace {
constexpr std::string_view soap_ns = "http://www.w3.org/2003/05/soap-envelope";
constexpr std::string_view wsa_ns = "http://www.w3.org/2005/08/addressing";
[[noreturn]] void bad() {
    throw ProtocolError("FormationViolation", "Invalid SOAP envelope");
}
std::string local(std::string_view name) {
    return std::string(name.substr(name.find(':') == name.npos ? 0 : name.find(':') + 1));
}
std::string ns(pugi::xml_node node, std::string_view name) {
    const auto colon = name.find(':');
    const auto attribute =
        colon == name.npos ? "xmlns" : "xmlns:" + std::string(name.substr(0, colon));
    for (auto n = node; n; n = n.parent())
        if (auto a = n.attribute(attribute.c_str()))
            return a.value();
    return {};
}
void structure(pugi::xml_node node, unsigned depth = 0) {
    if (depth > 32)
        bad();
    if (node.type() == pugi::node_doctype || node.type() == pugi::node_pi)
        bad();
    std::set<std::string> attributes;
    for (auto a : node.attributes())
        if (!attributes.insert(a.name()).second)
            bad();
    for (auto child : node.children())
        structure(child, depth + 1);
}
const Json &resolved(const Json &schema, const Json &registry) {
    if (schema.contains("$ref"))
        return registry.at("definitions").at(schema.at("$ref").get<std::string>().substr(14));
    return schema;
}
Json typed(std::string_view text, const Json &schema) {
    const auto type = schema.at("type").get<std::string>();
    if (type == "string")
        return std::string(text);
    if (type == "boolean") {
        if (text == "true" || text == "1")
            return true;
        if (text == "false" || text == "0")
            return false;
        bad();
    }
    // Numeric XML lexical spaces are permitted by XSD collapse rules.
    const auto first = text.find_first_not_of(" \r\n\t");
    const auto last = text.find_last_not_of(" \r\n\t");
    if (first == text.npos)
        bad();
    text = text.substr(first, last - first + 1);
    if (text.starts_with('+'))
        text.remove_prefix(1);
    if (type == "integer") {
        std::int64_t value = 0;
        const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
        if (r.ec != std::errc{} || r.ptr != text.data() + text.size())
            bad();
        return value;
    }
    if (type == "number") {
        double value = 0;
        const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
        if (r.ec != std::errc{} || r.ptr != text.data() + text.size() || !std::isfinite(value))
            bad();
        return value;
    }
    bad();
}
std::string text_value(pugi::xml_node node) {
    std::string result;
    for (auto child : node.children())
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata)
            result += child.value();
    return result;
}
Json read_value(pugi::xml_node node, const Json &original, const Json &registry,
                const std::string &space) {
    const auto &schema = resolved(original, registry);
    if (schema.at("type") != "object") {
        for (auto child : node.children())
            if (child.type() == pugi::node_element)
                bad();
        for (auto a : node.attributes())
            if (std::string_view(a.name()) != "xmlns" &&
                !std::string_view(a.name()).starts_with("xmlns:"))
                bad();
        return typed(text_value(node), schema);
    }
    Json result = Json::object();
    const Json *metadata = schema.contains("x-xmlFields") ? &schema.at("x-xmlFields") : nullptr;
    if (original.contains("$ref")) {
        const auto key = original.at("$ref").get<std::string>().substr(14);
        if (registry.at("xmlFields").contains(key))
            metadata = &registry.at("xmlFields").at(key);
    }
    auto field_kind = [&](const std::string &key, const char *kind) {
        if (!metadata)
            return false;
        for (const auto &f : *metadata)
            if (f.at("name") == key)
                return f.value(kind, false);
        return false;
    };
    const auto &properties = schema.at("properties");
    for (auto attribute : node.attributes()) {
        const std::string key = attribute.name();
        if (key == "xmlns" || key.starts_with("xmlns:"))
            continue;
        if (!properties.contains(key) || !field_kind(key, "attribute"))
            bad();
        result[key] = typed(attribute.value(), resolved(properties.at(key), registry));
    }
    if (properties.contains("value") && field_kind("value", "text"))
        result["value"] = typed(text_value(node), resolved(properties.at("value"), registry));
    for (auto child : node.children()) {
        if (child.type() != pugi::node_element) {
            if ((child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) &&
                !field_kind("value", "text") &&
                std::string_view(child.value()).find_first_not_of(" \r\n\t") !=
                    std::string_view::npos)
                bad();
            continue;
        }
        const auto key = local(child.name());
        if (ns(child, child.name()) != space || !properties.contains(key) ||
            field_kind(key, "attribute") || field_kind(key, "text"))
            bad();
        const auto &p = properties.at(key);
        if (p.value("type", std::string()) == "array") {
            if (!result.contains(key))
                result[key] = Json::array();
            if (result[key].size() >= 512)
                bad();
            result[key].push_back(read_value(child, p.at("items"), registry, space));
        } else {
            if (result.contains(key))
                bad();
            result[key] = read_value(child, p, registry, space);
        }
    }
    return result;
}
std::string scalar(const Json &value) {
    return value.is_string() ? value.get<std::string>() : value.dump();
}
void write_value(pugi::xml_node node, const Json &value, const Json &original,
                 const Json &registry) {
    const auto &schema = resolved(original, registry);
    if (schema.at("type") != "object") {
        node.text().set(scalar(value).c_str());
        return;
    }
    const auto key = original.contains("$ref") ? original.at("$ref").get<std::string>().substr(14)
                                               : std::string();
    const auto fields = schema.contains("x-xmlFields")
                            ? schema.at("x-xmlFields")
                            : registry.at("xmlFields").value(key, Json::array());
    for (const auto &field : fields) {
        const auto name = field.at("name").get<std::string>();
        if (!value.contains(name))
            continue;
        const auto &v = value.at(name);
        if (field.value("attribute", false))
            node.append_attribute(name.c_str()).set_value(scalar(v).c_str());
        else if (field.value("text", false))
            node.text().set(scalar(v).c_str());
        else {
            const auto &p = schema.at("properties").at(name);
            if (field.value("array", false))
                for (const auto &item : v)
                    write_value(node.append_child(("c:" + name).c_str()), item, p.at("items"),
                                registry);
            else
                write_value(node.append_child(("c:" + name).c_str()), v, p, registry);
        }
    }
}
Json meter_values(std::string_view version, const Json &values) {
    Json result = Json::array();
    for (const auto &v : values) {
        Json samples;
        if (version == "1.2")
            samples = Json::array({{{"value", v.at("value").dump()}}});
        else {
            samples = v.at("value");
            for (auto &s : samples)
                if (s.contains("unit")) {
                    if (s["unit"] == "Amp")
                        s["unit"] = "A";
                    if (s["unit"] == "Volt")
                        s["unit"] = "V";
                }
        }
        result.push_back({{"timestamp", v.at("timestamp")}, {"sampledValue", samples}});
    }
    return result;
}
void rename(Json &payload, const char *from, const char *to) {
    if (payload.contains(from)) {
        payload[to] = std::move(payload[from]);
        payload.erase(from);
    }
}
} // namespace
Json legacy_request(std::string_view version, std::string_view action, Json p) {
    if (version == "1.6") {
        if (action == "MeterValues" && !p.contains("meterValue"))
            p["meterValue"] = Json::array();
        return p;
    }
    if (action == "MeterValues") {
        p["meterValue"] = meter_values(version, p.value("values", Json::array()));
        p.erase("values");
    }
    if (action == "StopTransaction" && p.contains("transactionData")) {
        Json values = Json::array();
        for (const auto &data : p["transactionData"])
            for (const auto &v : meter_values(version, data.value("values", Json::array())))
                values.push_back(v);
        p["transactionData"] = std::move(values);
    }
    if (action == "StatusNotification") {
        if (p.at("status") == "Occupied")
            p["status"] = "Charging";
        if (p.at("errorCode") == "Mode3Error")
            p["errorCode"] = "EVCommunicationError";
    }
    return p;
}
Json legacy_response(std::string_view version, std::string_view action, Json p) {
    if (version != "1.6" && action == "BootNotification")
        rename(p, "interval", "heartbeatInterval");
    return p;
}
Json legacy_command(std::string_view version, std::string_view action, Json p) {
    if (version == "1.5" && action == "SendLocalList")
        rename(p, "localAuthorizationList", "localAuthorisationList");
    return p;
}
Json canonical_response(std::string_view version, std::string_view action, Json p) {
    if (version != "1.6" && action == "UnlockConnector") {
        if (p.at("status") == "Accepted")
            p["status"] = "Unlocked";
        else if (p.at("status") == "Rejected")
            p["status"] = "UnlockFailed";
    }
    return p;
}
std::string soap_fault(bool sender, std::string_view reason, std::string_view relates_to) {
    pugi::xml_document document;
    auto envelope = document.append_child("s:Envelope");
    envelope.append_attribute("xmlns:s") = soap_ns.data();
    envelope.append_attribute("xmlns:a") = wsa_ns.data();
    auto header = envelope.append_child("s:Header");
    header.append_child("a:Action").text().set("http://www.w3.org/2005/08/addressing/soap/fault");
    if (!relates_to.empty())
        header.append_child("a:RelatesTo").text().set(std::string(relates_to).c_str());
    auto fault = envelope.append_child("s:Body").append_child("s:Fault");
    fault.append_child("s:Code").append_child("s:Value").text().set(sender ? "s:Sender"
                                                                           : "s:Receiver");
    auto text = fault.append_child("s:Reason").append_child("s:Text");
    text.append_attribute("xml:lang") = "en";
    text.text().set(std::string(reason).c_str());
    std::ostringstream output;
    document.save(output, "", pugi::format_raw, pugi::encoding_utf8);
    return output.str();
}
SoapCodec::SoapCodec(const std::filesystem::path &path) {
    std::ifstream file(path);
    file >> registry_;
}
SoapCall SoapCodec::decode(std::string_view xml, bool response) const {
    if (xml.empty() || xml.size() > 65536)
        bad();
    pugi::xml_document document;
    if (!document.load_buffer(xml.data(), xml.size(),
                              pugi::parse_default | pugi::parse_doctype | pugi::parse_pi,
                              pugi::encoding_utf8))
        bad();
    structure(document);
    unsigned roots = 0;
    for (auto node : document.children())
        if (node.type() == pugi::node_element)
            ++roots;
    if (roots != 1)
        bad();
    const auto envelope = document.document_element();
    if (local(envelope.name()) != "Envelope" || ns(envelope, envelope.name()) != soap_ns)
        bad();
    pugi::xml_node header, body;
    for (auto n : envelope.children())
        if (n.type() == pugi::node_element) {
            if (ns(n, n.name()) != soap_ns)
                bad();
            if (local(n.name()) == "Header" && !header)
                header = n;
            else if (local(n.name()) == "Body" && !body)
                body = n;
            else
                bad();
        }
    if (!header || !body)
        bad();
    SoapCall call;
    std::string action_header;
    const auto space = registry_.at("namespaces").at(response ? "cp" : "cs").get<std::string>();
    std::set<std::string> headers;
    for (auto n : header.children())
        if (n.type() == pugi::node_element) {
            const auto name = local(n.name()), uri = ns(n, n.name());
            if (!headers.insert(uri + "#" + name).second)
                bad();
            if (name == "chargeBoxIdentity" && uri == space && call.station.empty())
                call.station = n.text().get();
            else if (name == (response ? "RelatesTo" : "MessageID") && uri == wsa_ns &&
                     call.message_id.empty())
                call.message_id = n.text().get();
            else if (name == "Action" && uri == wsa_ns && action_header.empty())
                action_header = n.text().get();
            else if (uri == wsa_ns && (name == "From" || name == "ReplyTo" ||
                                       name == "To")) { /* Never use untrusted routing addresses. */
            } else
                bad();
        }
    pugi::xml_node payload;
    for (auto n : body.children())
        if (n.type() == pugi::node_element) {
            if (payload)
                bad();
            payload = n;
        }
    if (!payload || ns(payload, payload.name()) != space ||
        (!response && !station_id_valid(call.station)) ||
        (response && !call.station.empty() && !station_id_valid(call.station)))
        bad();
    const auto name = local(payload.name());
    if (!registry_.at("elements").contains(name) ||
        !name.ends_with(response ? "Response" : "Request"))
        bad();
    const auto type = registry_.at("elements").at(name).get<std::string>();
    call.action = type.substr(0, type.size() - (response ? 8 : 7));
    const auto expected_action = call.action + (response ? "Response" : "");
    if (!action_header.empty() && action_header != "/" + expected_action &&
        action_header != expected_action)
        bad();
    if (call.message_id.size() > 128 ||
        call.message_id.find_first_of("\r\n\t") != std::string::npos)
        bad();
    call.payload = read_value(payload, Json{{"$ref", "#/definitions/" + type}}, registry_, space);
    return call;
}
std::string SoapCodec::encode(std::string_view action, const Json &payload,
                              std::string_view relates_to, bool response,
                              std::string_view station) const {
    std::string type = std::string(action) + (response ? "Response" : "Request");
    if (!registry_.at("definitions").contains(type))
        throw ProtocolError("NotImplemented", "Unsupported SOAP action");
    pugi::xml_document document;
    auto envelope = document.append_child("s:Envelope");
    envelope.append_attribute("xmlns:s") = soap_ns.data();
    envelope.append_attribute("xmlns:a") = wsa_ns.data();
    envelope.append_attribute("xmlns:c") =
        registry_.at("namespaces").at(response ? "cs" : "cp").get<std::string>().c_str();
    auto header = envelope.append_child("s:Header");
    if (!station.empty())
        header.append_child("c:chargeBoxIdentity").text().set(std::string(station).c_str());
    header.append_child("a:Action")
        .text()
        .set(("/" + std::string(action) + (response ? "Response" : "")).c_str());
    if (!relates_to.empty())
        header.append_child(response ? "a:RelatesTo" : "a:MessageID")
            .text()
            .set(std::string(relates_to).c_str());
    auto body = envelope.append_child("s:Body");
    auto name = type;
    name[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
    write_value(body.append_child(("c:" + name).c_str()), payload,
                Json{{"$ref", "#/definitions/" + type}}, registry_);
    std::ostringstream output;
    document.save(output, "", pugi::format_raw, pugi::encoding_utf8);
    return output.str();
}
} // namespace ocpp
