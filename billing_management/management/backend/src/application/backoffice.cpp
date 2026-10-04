#include "billing/management/application/backoffice.hpp"
#include "billing/domain/quote.hpp"
#include <charconv>
#include <regex>
#include <set>
namespace billing::management {
namespace {
std::int64_t identifier(const std::string &value) {
    std::int64_t id = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), id);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || id <= 0)
        throw Problem(400, "Invalid ID");
    return id;
}
Json data_only(Json record) {
    for (const auto *key : {"id", "version", "created_at", "updated_at", "deleted_at"})
        record.erase(key);
    return record;
}
std::string date(const Json &input, const char *key) {
    const auto value = text(input, key, 10);
    if (!std::regex_match(value, std::regex("[0-9]{4}-[0-9]{2}-[0-9]{2}")))
        throw Problem(400, "Use YYYY-MM-DD dates");
    const int y = std::stoi(value.substr(0, 4)), m = std::stoi(value.substr(5, 2)),
              d = std::stoi(value.substr(8, 2));
    const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (y < 2000 || y > 2200 || m < 1 || m > 12 || d < 1 ||
        d > days[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)))
        throw Problem(400, "Invalid calendar date");
    return value;
}
bool read_only(const std::string &kind) {
    return kind == "sessions" || kind == "bills" || kind == "tax-invoices" ||
           kind == "wallet-events" || kind == "refunds";
}
} // namespace
void Backoffice::references(const std::string &kind, const Json &data) {
    const auto spec = module(kind);
    for (const auto &f : spec["fields"]) {
        if (f["type"] == "reference")
            db_.get(f["reference"], data[f["key"].get<std::string>()]);
    }
    if (kind == "connectors") {
        if (db_.connector_exists(data["charger_id"], data["connector_number"]))
            throw Problem(409, "Connector number already exists on charger");
    }
}
std::int64_t Backoffice::balance(std::int64_t customer) {
    return db_.wallet_balance(customer);
}
Json Backoffice::handle(const std::string &method, const std::string &path, const Json &input,
                        const std::string &actor, const std::string &key, const std::string &search,
                        int offset) {
    std::lock_guard lock(mutex_);
    if (search.size() > 200 || offset < 0 || offset > 1'000'000)
        throw Problem(400, "Invalid pagination or search");
    const bool mutation = method != "GET";
    const bool financial = mutation && (path.starts_with("/wallet/") || path == "/sessions" ||
                                        path.ends_with("/refund") || path == "/bills" ||
                                        path.ends_with("/pay") || path.ends_with("/issue-tax"));
    if (financial && (key.size() < 16 || key.size() > 100 ||
                      !std::regex_match(key, std::regex("[a-zA-Z0-9_-]+"))))
        throw Problem(400, "Financial actions require a 16-100 character Idempotency-Key");
    if (!mutation)
        return operation(method, path, input, actor, search, offset);
    db_.begin();
    try {
        const auto payload = method + path + input.dump();
        if (financial) {
            const auto previous = db_.previous_request(key);
            if (!previous.is_null()) {
                if (previous["payload"] != payload)
                    throw Problem(409, "Idempotency key already used for another request");
                const auto result = Json::parse(previous["result"].get<std::string>());
                db_.commit();
                return result;
            }
        }
        auto result = operation(method, path, input, actor, search, offset);
        if (financial)
            db_.remember_request(key, payload, result);
        db_.commit();
        return result;
    } catch (...) {
        db_.rollback();
        throw;
    }
}
bool Backoffice::sync_charger(OcppRegistry &registry) {
    Json job;
    {
        std::lock_guard lock(mutex_);
        job = db_.next_charger_job();
    }
    if (job.is_null())
        return false;
    RegistryResult result;
    try {
        result = registry.register_charger(job["code"]);
    } catch (const std::exception &) {
        result.error = "csms_unavailable";
    }
    std::lock_guard lock(mutex_);
    db_.begin();
    try {
        db_.finish_charger_job(job["charger_id"], result.registered, result.credentials_configured,
                               result.error);
        if (result.registered)
            db_.audit("integration", "ocpp-registered", "chargers", job["charger_id"]);
        db_.commit();
    } catch (...) {
        db_.rollback();
        throw;
    }
    return true;
}
Json Backoffice::operation(const std::string &method, const std::string &path, const Json &input,
                           const std::string &actor, const std::string &search, int offset) {
    if (method == "GET" && path == "/me")
        return {{"role", actor}, {"mode", "sandbox"}, {"currency", "THB"}};
    if (method == "GET" && path == "/modules")
        return modules();
    if (method == "GET" && path == "/overview")
        return db_.overview();
    if (method == "GET" && path == "/audit")
        return db_.audit_page(offset);
    if (method == "GET" && path == "/trash")
        return db_.trash_page(offset);
    if (method == "POST" && (path == "/wallet/top-up" || path == "/wallet/withdraw")) {
        keys(input, {"customer_id", "amount_satang", "reference"});
        const auto customer = integer(input, "customer_id", 1, 1'000'000'000);
        const auto account = db_.get("customers", customer);
        if (account["status"] != "active")
            throw Problem(409, "Customer is suspended");
        const auto amount = integer(input, "amount_satang", 1, 100'000'000);
        const auto reference = text(input, "reference");
        const auto signed_amount = path == "/wallet/withdraw" ? -amount : amount;
        const auto after = balance(customer) + signed_amount;
        if (after < 0 || after > 1'000'000'000'000)
            throw Problem(409, "Insufficient balance or wallet limit reached");
        auto event = db_.insert("wallet-events",
                                {{"customer_id", customer},
                                 {"amount_satang", signed_amount},
                                 {"balance_satang", after},
                                 {"reference", reference},
                                 {"type", path == "/wallet/withdraw" ? "withdraw" : "top-up"},
                                 {"mode", "sandbox"}});
        db_.audit(actor, "wallet", "wallet-events", event["id"]);
        return event;
    }
    if (method == "GET" && path.starts_with("/wallet/") &&
        path.substr(8).find('/') == std::string::npos) {
        const auto customer = identifier(path.substr(8));
        db_.get("customers", customer);
        return {{"customer_id", customer},
                {"balance_satang", balance(customer)},
                {"items", db_.wallet_history(customer)}};
    }
    if (method == "POST" && path == "/sessions") {
        keys(input, {"customer_id", "connector_id", "energy_wh", "reference"});
        const auto customer = integer(input, "customer_id", 1, 1'000'000'000),
                   connector = integer(input, "connector_id", 1, 1'000'000'000);
        const auto user = db_.get("customers", customer), plug = db_.get("connectors", connector);
        const auto charger = db_.get("chargers", plug["charger_id"]),
                   station = db_.get("stations", charger["station_id"]);
        if (user["status"] != "active" || plug["status"] != "available" ||
            station["status"] != "active")
            throw Problem(409, "Customer, connector or station is unavailable");
        const auto tariff = db_.get("tariffs", station["tariff_id"]);
        const auto energy = integer(input, "energy_wh", 1, 1'000'000'000);
        const auto quoted = billing::domain::preview_quote(
            static_cast<std::int32_t>(energy), tariff["satang_per_kwh"].get<std::int32_t>());
        if (!quoted)
            throw Problem(400, "Invalid quote");
        auto session = db_.insert("sessions", {{"customer_id", customer},
                                               {"connector_id", connector},
                                               {"station_id", station["id"]},
                                               {"energy_wh", energy},
                                               {"satang_per_kwh", tariff["satang_per_kwh"]},
                                               {"vat_percent", tariff["vat_percent"]},
                                               {"subtotal_satang", quoted->subtotal_satang},
                                               {"reference", text(input, "reference")},
                                               {"status", "completed"},
                                               {"mode", "sandbox"}});
        db_.audit(actor, "record-session", "sessions", session["id"]);
        return session;
    }
    std::smatch matched;
    if (method == "POST" &&
        std::regex_match(path, matched, std::regex("/sessions/([0-9]+)/refund"))) {
        keys(input, {"amount_satang", "reason"});
        const auto id = identifier(matched[1]);
        const auto session = db_.get("sessions", id);
        db_.get("customers", session["customer_id"]);
        if (db_.is_billed(id))
            throw Problem(
                409, "Session already billed; refund must be handled by a credit-note workflow");
        const auto refunded = db_.refund_total(id);
        const auto amount = integer(input, "amount_satang", 1, 100'000'000);
        if (amount > session["subtotal_satang"].get<std::int64_t>() - refunded)
            throw Problem(409, "Refund exceeds remaining session amount");
        auto refund = db_.insert("refunds", {{"session_id", id},
                                             {"customer_id", session["customer_id"]},
                                             {"amount_satang", amount},
                                             {"reason", text(input, "reason")},
                                             {"mode", "sandbox"}});
        // Postpaid sessions have not debited a wallet. Adjust the future invoice only;
        // crediting a wallet here as well would compensate the customer twice.
        db_.audit(actor, "refund", "refunds", refund["id"]);
        return refund;
    }
    if (method == "POST" && path == "/bills") {
        keys(input, {"customer_id", "period_from", "period_to"});
        const auto customer = integer(input, "customer_id", 1, 1'000'000'000);
        const auto account = db_.get("customers", customer);
        const auto from = date(input, "period_from"), to = date(input, "period_to");
        if (from > to)
            throw Problem(400, "Invalid billing period");
        const auto rows = db_.unbilled_sessions(customer, from, to);
        if (rows.empty())
            throw Problem(409, "No unbilled sessions in selected UTC period");
        if (rows.size() > 500)
            throw Problem(400, "Use a shorter billing period (maximum 500 sessions)");
        Json lines = Json::array();
        std::int64_t subtotal = 0, vat = 0;
        for (const auto &row : rows) {
            const auto &s = row;
            const auto refund = db_.refund_total(row["id"]);
            const auto net = s["subtotal_satang"].get<std::int64_t>() - refund;
            const auto tax = (net * s["vat_percent"].get<std::int64_t>() + 50) / 100;
            subtotal += net;
            vat += tax;
            lines.push_back({{"session_id", row["id"]},
                             {"energy_wh", s["energy_wh"]},
                             {"subtotal_satang", net},
                             {"refund_satang", refund},
                             {"vat_satang", tax},
                             {"vat_percent", s["vat_percent"]}});
        }
        auto bill = db_.insert("bills", {{"customer_id", customer},
                                         {"customer_snapshot", data_only(account)},
                                         {"period_from", from},
                                         {"period_to", to},
                                         {"lines", lines},
                                         {"subtotal_satang", subtotal},
                                         {"vat_satang", vat},
                                         {"total_satang", subtotal + vat},
                                         {"status", "issued"},
                                         {"mode", "sandbox"}});
        for (const auto &row : rows)
            db_.attach_bill(row["id"], bill["id"]);
        db_.audit(actor, "generate-bill", "bills", bill["id"]);
        return bill;
    }
    if (method == "POST" && std::regex_match(path, matched, std::regex("/bills/([0-9]+)/pay"))) {
        keys(input, {"method", "reference"});
        const auto id = identifier(matched[1]);
        auto bill = db_.get("bills", id);
        if (bill["status"] != "issued")
            throw Problem(409, "Bill is already paid");
        const auto payment_method = text(input, "method"), reference = text(input, "reference");
        if (payment_method != "wallet" && payment_method != "manual")
            throw Problem(400, "Use wallet or manual payment");
        const auto amount = bill["total_satang"].get<std::int64_t>();
        if (payment_method == "wallet") {
            db_.get("customers", bill["customer_id"]);
            const auto after = balance(bill["customer_id"]) - amount;
            if (after < 0)
                throw Problem(409, "Insufficient wallet balance");
            db_.insert("wallet-events", {{"customer_id", bill["customer_id"]},
                                         {"amount_satang", -amount},
                                         {"balance_satang", after},
                                         {"type", "bill-payment"},
                                         {"bill_id", id},
                                         {"reference", reference},
                                         {"mode", "sandbox"}});
        }
        bill = data_only(bill);
        bill["status"] = "paid";
        bill["payment_method"] = payment_method;
        bill["payment_reference"] = reference;
        db_.save(id, bill);
        db_.audit(actor, "pay-bill", "bills", id);
        return db_.get("bills", id);
    }
    if (method == "POST" &&
        std::regex_match(path, matched, std::regex("/bills/([0-9]+)/issue-tax"))) {
        keys(input, {});
        const auto id = identifier(matched[1]);
        const auto bill = db_.get("bills", id);
        if (bill["status"] != "paid")
            throw Problem(409, "Pay bill before issuing sandbox tax invoice");
        const auto existing = db_.tax_for_bill(id);
        if (!existing.is_null())
            return existing;
        auto tax = db_.insert("tax-invoices", {{"bill_id", id},
                                               {"number", "TEST-TAX-" + std::to_string(id)},
                                               {"bill_snapshot", bill},
                                               {"customer_id", bill["customer_id"]},
                                               {"total_satang", bill["total_satang"]},
                                               {"status", "issued"},
                                               {"mode", "sandbox"}});
        db_.audit(actor, "issue-tax", "tax-invoices", tax["id"]);
        return tax;
    }
    if (method == "POST" &&
        std::regex_match(path, matched, std::regex("/chargers/([0-9]+)/sync"))) {
        keys(input, {});
        const auto id = identifier(matched[1]);
        const auto charger = db_.get("chargers", id);
        db_.queue_charger(id, charger["code"]);
        db_.audit(actor, "ocpp-sync-requested", "chargers", id);
        return db_.charger_sync(id);
    }
    if (!std::regex_match(path, matched, std::regex("/([a-z-]+)(?:/([0-9]+))?(?:/(restore))?")))
        throw Problem(404, "Route not found");
    const auto kind = matched[1].str();
    const bool financial = read_only(kind);
    if (!financial)
        module(kind);
    const auto id = matched[2].matched ? identifier(matched[2]) : 0;
    if (method == "GET") {
        auto result = id ? db_.get(kind, id) : db_.list(kind, false, search, offset);
        if (kind == "chargers") {
            auto enrich = [&](Json &charger) {
                const auto sync = db_.charger_sync(charger["id"]);
                charger["ocpp_sync_status"] = sync["state"];
                charger["ocpp_credentials_configured"] = sync["credentials_configured"];
                charger["ocpp_last_error"] = sync.value("last_error", std::string());
                charger["ocpp_synced_at"] = sync.value("synced_at", Json());
            };
            if (id)
                enrich(result);
            else
                for (auto &charger : result["items"])
                    enrich(charger);
        }
        return result;
    }
    if (financial)
        throw Problem(405, "Financial records can only be changed through business actions");
    if (method == "POST" && matched[3].matched && id) {
        keys(input, {"version"});
        auto record = db_.get(kind, id, true);
        if (record["deleted_at"].is_null() ||
            integer(input, "version", 1, 1'000'000'000) != record["version"])
            throw Problem(409, "Record changed or already restored");
        references(kind, data_only(record));
        db_.archive(id, false);
        db_.audit(actor, "restore", kind, id);
        return db_.get(kind, id);
    }
    if (method == "POST" && !id) {
        validate(kind, input);
        references(kind, input);
        auto record = db_.insert(kind, input);
        if (kind == "chargers")
            db_.queue_charger(record["id"], input["code"]);
        db_.audit(actor, "create", kind, record["id"]);
        return record;
    }
    if (method == "PATCH" && id) {
        const auto current = db_.get(kind, id);
        auto data = input;
        const auto version = integer(data, "version", 1, 1'000'000'000);
        data.erase("version");
        if (version != current["version"])
            throw Problem(409, "Record changed; reload before editing");
        validate(kind, data);
        if (kind == "chargers" && data["code"] != current["code"])
            throw Problem(409, "ChargeBoxId cannot be changed after creation");
        if (kind == "connectors" && (data["charger_id"] != current["charger_id"] ||
                                     data["connector_number"] != current["connector_number"]))
            throw Problem(409, "Connector identity cannot be changed");
        if (kind != "connectors")
            references(kind, data);
        db_.save(id, data);
        db_.audit(actor, "update", kind, id);
        return db_.get(kind, id);
    }
    if (method == "DELETE" && id) {
        keys(input, {"version"});
        const auto current = db_.get(kind, id);
        if (integer(input, "version", 1, 1'000'000'000) != current["version"])
            throw Problem(409, "Record changed; reload before archiving");
        const std::string reference = kind == "companies"    ? "company_id"
                                      : kind == "projects"   ? "project_id"
                                      : kind == "stations"   ? "station_id"
                                      : kind == "tariffs"    ? "tariff_id"
                                      : kind == "chargers"   ? "charger_id"
                                      : kind == "connectors" ? "connector_id"
                                      : kind == "customers"  ? "customer_id"
                                                             : "";
        if (!reference.empty() && db_.has_references(reference, id))
            throw Problem(409, "Record is referenced; archive related records first. Financial "
                               "history is protected.");
        db_.archive(id, true);
        db_.audit(actor, "archive", kind, id);
        return {{"archived", true}};
    }
    throw Problem(405, "Method not allowed");
}
} // namespace billing::management
