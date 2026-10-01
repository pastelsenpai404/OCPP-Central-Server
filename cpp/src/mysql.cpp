#include "ocpp/mysql.hpp"
#include <charconv>
#include <memory>
#include <mysql.h>

namespace ocpp {
std::int64_t integer(const Json &value) {
    if (value.is_number_integer())
        return value.get<std::int64_t>();
    if (!value.is_string())
        throw std::runtime_error("Database integer type");
    const auto &text = value.get_ref<const std::string &>();
    std::int64_t result = 0;
    const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (code != std::errc{} || end != text.data() + text.size())
        throw std::runtime_error("Database integer range");
    return result;
}
Database::Database(Config config) : config_(std::move(config)), slots_(config_.workers + 2) {
    static const int initialized = mysql_library_init(0, nullptr, nullptr);
    if (initialized)
        throw std::runtime_error("Database library initialization failed");
}
Database::~Database() {
    for (auto &s : slots_)
        close_slot(s);
}
void Database::close_slot(Slot &slot) {
    for (auto &prepared : slot.statements)
        mysql_stmt_close(static_cast<MYSQL_STMT *>(prepared.statement));
    slot.statements.clear();
    if (slot.connection)
        mysql_close(static_cast<MYSQL *>(slot.connection));
    slot.connection = nullptr;
}
Database::Lease Database::acquire() {
    struct ThreadState {
        ThreadState() {
            mysql_thread_init();
        }
        ~ThreadState() {
            mysql_thread_end();
        }
    };
    thread_local ThreadState state;
    static_cast<void>(state);
    std::unique_lock lock(mutex_);
    if (!available_.wait_for(lock, std::chrono::seconds(1), [&] {
            return std::any_of(slots_.begin(), slots_.end(), [](const Slot &s) { return !s.busy; });
        }))
        throw std::runtime_error("Database capacity exhausted");
    std::size_t index = 0;
    while (slots_[index].busy)
        ++index;
    slots_[index].busy = true;
    lock.unlock();
    auto &slot = slots_[index];
    try {
        if (!slot.connection) {
            std::unique_ptr<MYSQL, decltype(&mysql_close)> connection(mysql_init(nullptr),
                                                                      mysql_close);
            if (!connection)
                throw std::runtime_error("Database allocation failed");
            unsigned int timeout = 5;
            mysql_options(connection.get(), MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
            mysql_options(connection.get(), MYSQL_OPT_READ_TIMEOUT, &timeout);
            mysql_options(connection.get(), MYSQL_OPT_WRITE_TIMEOUT, &timeout);
            my_bool reconnect = 0;
            if (mysql_options(connection.get(), MYSQL_OPT_RECONNECT, &reconnect))
                throw std::runtime_error("Database reconnect policy failed");
            mysql_options(connection.get(), MYSQL_SET_CHARSET_NAME, "utf8mb4");
            if (!config_.db_ca.empty()) {
                my_bool yes = 1;
                mysql_options(connection.get(), MYSQL_OPT_SSL_ENFORCE, &yes);
                mysql_options(connection.get(), MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &yes);
                mysql_options(connection.get(), MYSQL_OPT_SSL_CA, config_.db_ca.c_str());
            }
            if (!mysql_real_connect(connection.get(), config_.db_host.c_str(),
                                    config_.db_user.c_str(), config_.db_password.c_str(),
                                    config_.db_name.c_str(), config_.db_port, nullptr, 0))
                throw std::runtime_error("Database connection failed");
            if (!config_.db_ca.empty() && !mysql_get_ssl_cipher(connection.get()))
                throw std::runtime_error("Database TLS required");
            if (mysql_query(connection.get(), "SET SESSION time_zone = '+00:00'") ||
                mysql_query(
                    connection.get(),
                    "SET SESSION sql_mode = 'STRICT_ALL_TABLES,NO_ZERO_DATE,NO_ZERO_IN_DATE'"))
                throw std::runtime_error("Database session configuration failed");
            slot.connection = connection.release();
        }
        return Lease(this, index);
    } catch (...) {
        {
            std::lock_guard guard(mutex_);
            slot.busy = false;
        }
        available_.notify_one();
        throw;
    }
}
Database::Lease::~Lease() {
    auto &slot = pool_->slots_[index_];
    if (transaction_ && mysql_query(static_cast<MYSQL *>(slot.connection), "ROLLBACK"))
        broken_ = true;
    if (broken_ && slot.connection) {
        Database::close_slot(slot);
    }
    {
        std::lock_guard lock(pool_->mutex_);
        slot.busy = false;
    }
    pool_->available_.notify_one();
}
void Database::Lease::begin() {
    execute("START TRANSACTION");
    transaction_ = true;
}
void Database::Lease::commit() {
    execute("COMMIT");
    transaction_ = false;
}
SqlResult Database::Lease::execute(std::string_view sql, const Parameters &parameters) {
    auto &slot = pool_->slots_[index_];
    auto *connection = static_cast<MYSQL *>(slot.connection);
    auto failure = [&] {
        broken_ = true;
        throw std::runtime_error("Database statement failed");
    };
    if (sql.size() > 65536)
        throw std::logic_error("SQL statement size limit");
    auto found = std::find_if(slot.statements.begin(), slot.statements.end(),
                              [&](const Slot::Prepared &p) { return p.sql == sql; });
    MYSQL_STMT *handle = nullptr;
    if (found != slot.statements.end()) {
        handle = static_cast<MYSQL_STMT *>(found->statement);
        slot.statements.splice(slot.statements.begin(), slot.statements, found);
    } else {
        std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)> candidate(
            mysql_stmt_init(connection), mysql_stmt_close);
        if (!candidate ||
            mysql_stmt_prepare(candidate.get(), sql.data(), static_cast<unsigned long>(sql.size())))
            failure();
        if (slot.statements.size() >= 64) {
            mysql_stmt_close(static_cast<MYSQL_STMT *>(slot.statements.back().statement));
            slot.statements.pop_back();
        }
        slot.statements.push_front({std::string(sql), candidate.get()});
        handle = candidate.release();
    }
    // Cache owns the handle. Every execution releases buffered results, including
    // exception paths; parameters and output buffers are rebound on each use.
    const auto release = [this](MYSQL_STMT *s) {
        if (mysql_stmt_field_count(s) && mysql_stmt_free_result(s))
            broken_ = true;
    };
    std::unique_ptr<MYSQL_STMT, decltype(release)> stmt(handle, release);
    if (mysql_stmt_param_count(stmt.get()) != parameters.size())
        throw std::logic_error("SQL parameter count mismatch");
    std::vector<MYSQL_BIND> bind(parameters.size());
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        auto &b = bind[i];
        if (!parameters[i])
            b.buffer_type = MYSQL_TYPE_NULL;
        else {
            b.buffer_type = MYSQL_TYPE_STRING;
            b.buffer = const_cast<char *>(parameters[i]->data());
            b.buffer_length = static_cast<unsigned long>(parameters[i]->size());
        }
    }
    if (!bind.empty() && mysql_stmt_bind_param(stmt.get(), bind.data()))
        failure();
    my_bool yes = 1;
    mysql_stmt_attr_set(stmt.get(), STMT_ATTR_UPDATE_MAX_LENGTH, &yes);
    if (mysql_stmt_execute(stmt.get()))
        failure();
    SqlResult result;
    result.inserted = mysql_stmt_insert_id(stmt.get());
    result.affected = mysql_stmt_affected_rows(stmt.get());
    if (!mysql_stmt_field_count(stmt.get()))
        return result;
    if (mysql_stmt_store_result(stmt.get()))
        failure();
    if (mysql_stmt_num_rows(stmt.get()) > 1000)
        throw std::runtime_error("Query row limit exceeded");
    std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)> metadata(
        mysql_stmt_result_metadata(stmt.get()), mysql_free_result);
    if (!metadata)
        failure();
    const auto count = mysql_num_fields(metadata.get());
    const auto *fields = mysql_fetch_fields(metadata.get());
    std::vector<MYSQL_BIND> output(count);
    std::vector<std::vector<char>> buffers(count);
    std::vector<unsigned long> lengths(count);
    std::vector<my_bool> nulls(count), errors(count);
    for (unsigned i = 0; i < count; ++i) {
        if (fields[i].max_length > 1024 * 1024)
            throw std::runtime_error("Query field limit exceeded");
        buffers[i].resize(fields[i].max_length + 1);
        output[i].buffer_type = MYSQL_TYPE_STRING;
        output[i].buffer = buffers[i].data();
        output[i].buffer_length = static_cast<unsigned long>(buffers[i].size());
        output[i].length = &lengths[i];
        output[i].is_null = &nulls[i];
        output[i].error = &errors[i];
    }
    if (mysql_stmt_bind_result(stmt.get(), output.data()))
        failure();
    int status = 0;
    while ((status = mysql_stmt_fetch(stmt.get())) == 0) {
        Json row = Json::object();
        for (unsigned i = 0; i < count; ++i)
            row[fields[i].name] =
                nulls[i] ? Json(nullptr) : Json(std::string(buffers[i].data(), lengths[i]));
        result.rows.push_back(std::move(row));
    }
    if (status != MYSQL_NO_DATA)
        failure();
    return result;
}
} // namespace ocpp
