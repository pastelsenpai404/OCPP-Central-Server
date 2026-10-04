#include "billing/management/domain/modules.hpp"
#include <algorithm>
#include <set>
namespace billing::management {
namespace {
Json field(const char *key, const char *label, const char *type = "text", bool required = false,
           const char *reference = "", Json options = Json::array()) {
    return {{"key", key},           {"label", label},         {"type", type},
            {"required", required}, {"reference", reference}, {"options", options}};
}
Json definition(const char *key, const char *label, Json fields) {
    return {{"key", key}, {"label", label}, {"fields", fields}};
}
} // namespace
Json modules() {
    return Json::array(
        {definition("companies", "บริษัท",
                    {field("code", "รหัส", "text", true), field("name", "ชื่อบริษัท", "text", true),
                     field("tax_id", "เลขผู้เสียภาษี"), field("email", "อีเมล"),
                     field("phone", "โทรศัพท์"), field("address", "ที่อยู่")}),
         definition("projects", "โครงการ",
                    {field("code", "รหัส", "text", true), field("name", "ชื่อโครงการ", "text", true),
                     field("company_id", "บริษัท", "reference", true, "companies"),
                     field("status", "สถานะ", "select", true, "", {"draft", "active", "closed"})}),
         definition("tariffs", "อัตราค่าชาร์จ",
                    {field("code", "รหัส", "text", true), field("name", "ชื่ออัตรา", "text", true),
                     field("satang_per_kwh", "สตางค์ / kWh", "number", true),
                     field("vat_percent", "VAT %", "number", true)}),
         definition("stations", "สถานี",
                    {field("code", "รหัส", "text", true), field("name", "ชื่อสถานี", "text", true),
                     field("project_id", "โครงการ", "reference", true, "projects"),
                     field("tariff_id", "อัตราค่าชาร์จ", "reference", true, "tariffs"),
                     field("address", "ที่อยู่"), field("opening_hours", "เวลาเปิดบริการ"),
                     field("status", "สถานะ", "select", true, "",
                           {"planned", "active", "maintenance", "closed"})}),
         definition(
             "chargers", "เครื่องชาร์จ",
             {field("code", "รหัสเครื่อง", "text", true), field("name", "ชื่อเครื่อง", "text", true),
              field("station_id", "สถานี", "reference", true, "stations"), field("model", "รุ่น"),
              field("protocol", "OCPP", "select", true, "", {"ocpp1.6", "ocpp2.0.1"}),
              field("status", "สถานะที่บันทึก", "select", true, "",
                    {"offline", "available", "charging", "faulted"})}),
         definition("connectors", "หัวชาร์จ",
                    {field("code", "รหัส", "text", true), field("name", "ชื่อหัวชาร์จ", "text", true),
                     field("charger_id", "เครื่องชาร์จ", "reference", true, "chargers"),
                     field("connector_number", "หมายเลขหัว", "number", true),
                     field("power_kw", "กำลัง kW", "number", true),
                     field("type", "ชนิด", "select", true, "", {"CCS2", "Type2", "CHAdeMO"}),
                     field("status", "สถานะที่บันทึก", "select", true, "",
                           {"available", "charging", "faulted", "unavailable"})}),
         definition("customers", "ลูกค้า",
                    {field("code", "รหัส", "text", true), field("name", "ชื่อลูกค้า", "text", true),
                     field("email", "อีเมล"), field("phone", "โทรศัพท์"),
                     field("tax_id", "เลขผู้เสียภาษี"), field("address", "ที่อยู่"),
                     field("status", "สถานะ", "select", true, "", {"active", "suspended"})}),
         definition(
             "maintenance", "งานซ่อมบำรุง",
             {field("code", "เลขงาน", "text", true), field("name", "หัวข้องาน", "text", true),
              field("station_id", "สถานี", "reference", true, "stations"),
              field("assignee", "ผู้รับผิดชอบ"), field("notes", "รายละเอียด"),
              field("status", "สถานะ", "select", true, "", {"open", "in_progress", "resolved"})}),
         definition(
             "admins", "ทีมดูแลระบบ",
             {field("code", "รหัส", "text", true), field("name", "ชื่อ", "text", true),
              field("email", "อีเมล"),
              field("role", "บทบาทในทะเบียน", "select", true, "", {"reader", "operator", "admin"}),
              field("status", "สถานะ", "select", true, "", {"active", "disabled"})}),
         definition("settings", "ตั้งค่าธุรกิจ",
                    {field("code", "รหัส", "text", true), field("name", "ชื่อการตั้งค่า", "text", true),
                     field("value", "ค่า", "text", true), field("notes", "รายละเอียด")})});
}
Json module(const std::string &kind) {
    for (const auto &entry : modules())
        if (entry["key"] == kind)
            return entry;
    throw Problem(404, "Unknown module");
}
std::string text(const Json &data, const char *key, std::size_t maximum) {
    if (!data.contains(key) || !data[key].is_string())
        throw Problem(400, std::string("Text required: ") + key);
    const auto value = data[key].get<std::string>();
    if (value.empty() || value.size() > maximum ||
        value.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
        std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32; }))
        throw Problem(400, std::string("Invalid text: ") + key);
    return value;
}
std::int64_t integer(const Json &data, const char *key, std::int64_t minimum,
                     std::int64_t maximum) {
    if (!data.contains(key) || !data[key].is_number_integer() || data[key] < minimum ||
        data[key] > maximum)
        throw Problem(400, std::string("Invalid integer: ") + key);
    return data[key].get<std::int64_t>();
}
void keys(const Json &data, std::initializer_list<const char *> allowed) {
    if (!data.is_object())
        throw Problem(400, "JSON object required");
    for (auto it = data.begin(); it != data.end(); ++it)
        if (std::none_of(allowed.begin(), allowed.end(),
                         [&](const auto *key) { return it.key() == key; }))
            throw Problem(400, "Unknown field: " + it.key());
}
void validate(const std::string &kind, const Json &data) {
    const auto spec = module(kind);
    if (!data.is_object())
        throw Problem(400, "JSON object required");
    std::set<std::string> accepted;
    for (const auto &f : spec["fields"]) {
        const auto key = f["key"].get<std::string>();
        accepted.insert(key);
        if (!data.contains(key) || data[key] == "") {
            if (f["required"] == true)
                throw Problem(400, "Required field: " + key);
            continue;
        }
        if (f["type"] == "reference")
            integer(data, key.c_str(), 1, 1'000'000'000);
        else if (f["type"] == "number") {
            auto max = 1'000'000;
            if (key == "vat_percent")
                max = 25;
            if (key == "connector_number")
                max = 128;
            if (key == "power_kw")
                max = 1000;
            integer(data, key.c_str(), key == "connector_number" || key == "power_kw" ? 1 : 0, max);
        } else {
            const auto value =
                text(data, key.c_str(), key == "notes" || key == "address" ? 1000 : 200);
            if (f["type"] == "select" &&
                std::find(f["options"].begin(), f["options"].end(), value) == f["options"].end())
                throw Problem(400, "Invalid selection: " + key);
            if (key == "code" &&
                (value.size() > 64 || !std::all_of(value.begin(), value.end(), [](unsigned char c) {
                     return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '-' || c == '_';
                 })))
                throw Problem(400, "Code must use letters, numbers, - or _");
        }
    }
    for (auto it = data.begin(); it != data.end(); ++it)
        if (!accepted.contains(it.key()))
            throw Problem(400, "Unknown field: " + it.key());
}
} // namespace billing::management
