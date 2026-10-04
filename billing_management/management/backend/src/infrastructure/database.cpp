#include "billing/management/infrastructure/database.hpp"
#include <memory>
#include <sqlite3.h>
namespace billing::management {
namespace {
void check(int code) {
    if (code == SQLITE_OK || code == SQLITE_DONE || code == SQLITE_ROW)
        return;
    if ((code & 255) == SQLITE_CONSTRAINT)
        throw Problem(409, "Duplicate code or conflicting record");
    throw Problem(503, "Database operation failed");
}
Json unpack(const Json &row) {
    auto value = Json::parse(row["data"].get<std::string>());
    for (const auto *key : {"id", "version", "created_at", "updated_at", "deleted_at"})
        value[key] = row[key];
    return value;
}
} // namespace
Database::Database(const std::string &path) {
    const int code = sqlite3_open_v2(
        path.c_str(), &handle_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (code != SQLITE_OK) {
        sqlite3_close(handle_);
        handle_ = nullptr;
        throw Problem(503, "Cannot open BILLING_DATABASE_PATH");
    }
    sqlite3_busy_timeout(handle_, 5000);
    try {
        if (query("PRAGMA user_version")[0]["user_version"] > 1)
            throw Problem(503, "Database schema is newer than this executable");
        execute("PRAGMA journal_mode=WAL");
        execute("PRAGMA synchronous=FULL");
        execute(
            "CREATE TABLE IF NOT EXISTS records(id INTEGER PRIMARY KEY,kind TEXT NOT NULL,data "
            "TEXT NOT NULL CHECK(json_valid(data)),version INTEGER NOT NULL DEFAULT 1,created_at "
            "TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%SZ','now')),updated_at TEXT NOT NULL "
            "DEFAULT(strftime('%Y-%m-%dT%H:%M:%SZ','now')),deleted_at TEXT)");
        execute("CREATE INDEX IF NOT EXISTS records_kind ON records(kind,deleted_at,id)");
        execute("CREATE INDEX IF NOT EXISTS records_customer ON "
                "records(kind,json_extract(data,'$.customer_id'),id)");
        execute("CREATE INDEX IF NOT EXISTS records_session ON "
                "records(kind,json_extract(data,'$.session_id'),id)");
        execute("CREATE UNIQUE INDEX IF NOT EXISTS tax_bill ON "
                "records(json_extract(data,'$.bill_id')) WHERE kind='tax-invoices'");
        execute("CREATE UNIQUE INDEX IF NOT EXISTS connector_identity ON "
                "records(json_extract(data,'$.charger_id'),json_extract(data,'$.connector_number'))"
                " WHERE kind='connectors' AND deleted_at IS NULL");
        execute("CREATE UNIQUE INDEX IF NOT EXISTS record_codes ON "
                "records(kind,json_extract(data,'$.code')) WHERE deleted_at IS NULL");
        execute("CREATE TABLE IF NOT EXISTS audit(id INTEGER PRIMARY KEY,actor TEXT NOT "
                "NULL,action TEXT NOT NULL,kind TEXT NOT NULL,record_id INTEGER NOT "
                "NULL,created_at TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%SZ','now')))");
        execute("CREATE TABLE IF NOT EXISTS requests(key TEXT PRIMARY KEY,payload TEXT NOT "
                "NULL,result TEXT NOT NULL CHECK(json_valid(result)))");
        execute("CREATE TABLE IF NOT EXISTS invoice_sessions(session_id INTEGER PRIMARY KEY "
                "REFERENCES records(id),bill_id INTEGER NOT NULL REFERENCES records(id))");
        execute("PRAGMA user_version=1");
        execute("CREATE UNIQUE INDEX IF NOT EXISTS charger_codes_all ON "
                "records(json_extract(data,'$.code')) WHERE kind='chargers'");
        execute("CREATE TABLE IF NOT EXISTS ocpp_outbox(charger_id INTEGER PRIMARY KEY REFERENCES "
                "records(id),code TEXT NOT NULL UNIQUE,state TEXT NOT NULL DEFAULT "
                "'pending',attempts INTEGER NOT NULL DEFAULT 0,next_attempt INTEGER NOT NULL "
                "DEFAULT 0,credentials_configured INTEGER NOT NULL DEFAULT 0,last_error TEXT NOT "
                "NULL DEFAULT '',synced_at TEXT)");
        execute("INSERT OR IGNORE INTO ocpp_outbox(charger_id,code) SELECT "
                "id,json_extract(data,'$.code') FROM records WHERE kind='chargers' AND deleted_at "
                "IS NULL");
    } catch (...) {
        sqlite3_close(handle_);
        handle_ = nullptr;
        throw;
    }
}
Database::~Database() {
    sqlite3_close(handle_);
}
Json Database::query(const std::string &sql, const Json &parameters) {
    sqlite3_stmt *raw = nullptr;
    check(sqlite3_prepare_v2(handle_, sql.c_str(), -1, &raw, nullptr));
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw, sqlite3_finalize);
    int index = 1;
    for (const auto &value : parameters) {
        if (value.is_null())
            check(sqlite3_bind_null(raw, index));
        else if (value.is_number_integer())
            check(sqlite3_bind_int64(raw, index, value.get<std::int64_t>()));
        else {
            const auto s = value.get<std::string>();
            check(sqlite3_bind_text(raw, index, s.c_str(), static_cast<int>(s.size()),
                                    SQLITE_TRANSIENT));
        }
        ++index;
    }
    Json result = Json::array();
    int code;
    while ((code = sqlite3_step(raw)) == SQLITE_ROW) {
        Json row = Json::object();
        for (int column = 0; column < sqlite3_column_count(raw); ++column) {
            const auto *key = sqlite3_column_name(raw, column);
            const auto type = sqlite3_column_type(raw, column);
            if (type == SQLITE_NULL)
                row[key] = nullptr;
            else if (type == SQLITE_INTEGER)
                row[key] = sqlite3_column_int64(raw, column);
            else
                row[key] = reinterpret_cast<const char *>(sqlite3_column_text(raw, column));
        }
        result.push_back(row);
    }
    check(code);
    return result;
}
void Database::execute(const std::string &sql, const Json &parameters) {
    query(sql, parameters);
}
Json Database::get(const std::string &kind, std::int64_t id, bool archived) {
    auto rows = query("SELECT * FROM records WHERE kind=? AND id=?" +
                          std::string(archived ? "" : " AND deleted_at IS NULL"),
                      {kind, id});
    if (rows.empty())
        throw Problem(404, "Record not found");
    return unpack(rows[0]);
}
Json Database::insert(const std::string &kind, const Json &data) {
    execute("INSERT INTO records(kind,data) VALUES(?,?)", {kind, data.dump()});
    return get(kind, sqlite3_last_insert_rowid(handle_));
}
Json Database::list(const std::string &kind, bool archived, const std::string &search, int offset) {
    Json items = Json::array();
    const auto where = std::string("kind=? AND deleted_at IS ") + (archived ? "NOT NULL" : "NULL") +
                       " AND (?='' OR instr(lower(data),lower(?))>0)";
    for (const auto &row :
         query("SELECT * FROM records WHERE " + where + " ORDER BY id DESC LIMIT 100 OFFSET ?",
               {kind, search, search, offset}))
        items.push_back(unpack(row));
    return {{"items", items},
            {"total", query("SELECT count(*) AS n FROM records WHERE " + where,
                            {kind, search, search})[0]["n"]},
            {"limit", 100},
            {"offset", offset}};
}
void Database::audit(const std::string &actor, const std::string &action, const std::string &kind,
                     std::int64_t id) {
    execute("INSERT INTO audit(actor,action,kind,record_id) VALUES(?,?,?,?)",
            {actor, action, kind, id});
}
void Database::begin() {
    execute("BEGIN IMMEDIATE");
}
void Database::commit() {
    execute("COMMIT");
}
void Database::rollback() {
    execute("ROLLBACK");
}
void Database::save(std::int64_t id, const Json &data) {
    execute("UPDATE records SET "
            "data=?,version=version+1,updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') WHERE id=?",
            {data.dump(), id});
}
void Database::archive(std::int64_t id, bool archived) {
    execute("UPDATE records SET deleted_at=" +
                std::string(archived ? "strftime('%Y-%m-%dT%H:%M:%SZ','now')" : "NULL") +
                ",version=version+1,updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') WHERE id=?",
            {id});
}
Json Database::overview() {
    Json counts = Json::object();
    for (const auto &row :
         query("SELECT kind,count(*) AS n FROM records WHERE deleted_at IS NULL GROUP BY kind"))
        counts[row["kind"].get<std::string>()] = row["n"];
    return {{"mode", "sandbox"},
            {"counts", counts},
            {"totals", query("SELECT coalesce(sum(json_extract(data,'$.energy_wh')),0) AS "
                             "energy_wh,coalesce(sum(json_extract(data,'$.subtotal_satang')),0) AS "
                             "subtotal_satang FROM records WHERE kind='sessions'")[0]},
            {"refund_satang", query("SELECT coalesce(sum(json_extract(data,'$.amount_satang')),0) "
                                    "AS n FROM records WHERE kind='refunds'")[0]["n"]},
            {"recent", query("SELECT * FROM audit ORDER BY id DESC LIMIT 8")}};
}
Json Database::audit_page(int offset) {
    return {{"items", query("SELECT * FROM audit ORDER BY id DESC LIMIT 100 OFFSET ?", {offset})},
            {"total", query("SELECT count(*) AS n FROM audit")[0]["n"]}};
}
Json Database::trash_page(int offset) {
    Json items = Json::array();
    for (const auto &row : query("SELECT id,kind,data,version,deleted_at FROM records WHERE "
                                 "deleted_at IS NOT NULL ORDER BY id DESC LIMIT 100 OFFSET ?",
                                 {offset})) {
        auto item = row;
        item["data"] = Json::parse(row["data"].get<std::string>());
        items.push_back(item);
    }
    return {
        {"items", items},
        {"total", query("SELECT count(*) AS n FROM records WHERE deleted_at IS NOT NULL")[0]["n"]}};
}
std::int64_t Database::wallet_balance(std::int64_t customer) {
    return query("SELECT coalesce(sum(json_extract(data,'$.amount_satang')),0) AS balance FROM "
                 "records WHERE kind='wallet-events' AND json_extract(data,'$.customer_id')=?",
                 {customer})[0]["balance"]
        .get<std::int64_t>();
}
Json Database::wallet_history(std::int64_t customer) {
    return query("SELECT id,data,created_at FROM records WHERE kind='wallet-events' AND "
                 "json_extract(data,'$.customer_id')=? ORDER BY id DESC LIMIT 100",
                 {customer});
}
bool Database::connector_exists(std::int64_t charger, std::int64_t number) {
    return !query(
                "SELECT id FROM records WHERE kind='connectors' AND deleted_at IS NULL AND "
                "json_extract(data,'$.charger_id')=? AND json_extract(data,'$.connector_number')=?",
                {charger, number})
                .empty();
}
bool Database::is_billed(std::int64_t id) {
    return !query("SELECT session_id FROM invoice_sessions WHERE session_id=?", {id}).empty();
}
std::int64_t Database::refund_total(std::int64_t id) {
    return query("SELECT coalesce(sum(json_extract(data,'$.amount_satang')),0) AS n FROM records "
                 "WHERE kind='refunds' AND json_extract(data,'$.session_id')=?",
                 {id})[0]["n"]
        .get<std::int64_t>();
}
Json Database::unbilled_sessions(std::int64_t customer, const std::string &from,
                                 const std::string &to) {
    Json items = Json::array();
    for (const auto &row : query(
             "SELECT * FROM records WHERE kind='sessions' AND json_extract(data,'$.customer_id')=? "
             "AND substr(created_at,1,10)>=? AND substr(created_at,1,10)<=? AND id NOT IN(SELECT "
             "session_id FROM invoice_sessions) ORDER BY id LIMIT 501",
             {customer, from, to}))
        items.push_back(unpack(row));
    return items;
}
void Database::attach_bill(std::int64_t session, std::int64_t bill) {
    execute("INSERT INTO invoice_sessions(session_id,bill_id) VALUES(?,?)", {session, bill});
}
Json Database::tax_for_bill(std::int64_t bill) {
    const auto rows = query(
        "SELECT * FROM records WHERE kind='tax-invoices' AND json_extract(data,'$.bill_id')=?",
        {bill});
    return rows.empty() ? Json() : unpack(rows[0]);
}
bool Database::has_references(const std::string &reference, std::int64_t id) {
    return !query("SELECT id FROM records WHERE deleted_at IS NULL AND json_extract(data,?)=? "
                  "LIMIT 1",
                  {"$." + reference, id})
                .empty();
}
Json Database::previous_request(const std::string &key) {
    const auto rows = query("SELECT payload,result FROM requests WHERE key=?", {key});
    return rows.empty() ? Json() : rows[0];
}
void Database::remember_request(const std::string &key, const std::string &payload,
                                const Json &result) {
    execute("INSERT INTO requests(key,payload,result) VALUES(?,?,?)",
            {key, payload, result.dump()});
}
void Database::queue_charger(std::int64_t id, const std::string &code) {
    execute("INSERT INTO ocpp_outbox(charger_id,code) VALUES(?,?) ON CONFLICT(charger_id) DO "
            "UPDATE SET state='pending',next_attempt=0,last_error=''",
            {id, code});
}
Json Database::next_charger_job() {
    const auto rows = query(
        "SELECT o.* FROM ocpp_outbox o JOIN records r ON r.id=o.charger_id WHERE r.deleted_at IS "
        "NULL AND o.state!='synced' AND o.next_attempt<=CAST(strftime('%s','now') AS INTEGER) "
        "ORDER BY o.next_attempt,o.charger_id LIMIT 1");
    return rows.empty() ? Json() : rows[0];
}
void Database::finish_charger_job(std::int64_t id, bool registered, bool configured,
                                  const std::string &error) {
    execute("UPDATE ocpp_outbox SET "
            "state=?,attempts=attempts+1,credentials_configured=?,last_error=?,synced_at=CASE WHEN "
            "?=1 THEN strftime('%Y-%m-%dT%H:%M:%SZ','now') ELSE synced_at "
            "END,next_attempt=CAST(strftime('%s','now') AS INTEGER)+min(60,(1 << "
            "min(attempts+1,6))) WHERE charger_id=?",
            {registered ? "synced" : "retrying", registered && configured ? 1 : 0, error,
             registered ? 1 : 0, id});
}
Json Database::charger_sync(std::int64_t id) {
    const auto rows = query("SELECT state,attempts,credentials_configured,last_error,synced_at "
                            "FROM ocpp_outbox WHERE charger_id=?",
                            {id});
    return rows.empty() ? Json{{"state", "pending"}, {"credentials_configured", false}} : rows[0];
}
} // namespace billing::management
