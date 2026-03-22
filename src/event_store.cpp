#include "cattle_climate/event_store.hpp"

#include <sqlite3.h>

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace cattle_climate {
namespace {

struct SqliteHandle {
    sqlite3* db{nullptr};
    explicit SqliteHandle(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
            const std::string msg = db ? sqlite3_errmsg(db) : "unknown sqlite open error";
            if (db) sqlite3_close(db);
            throw std::runtime_error("Could not open SQLite database: " + path + " : " + msg);
        }
    }
    ~SqliteHandle() { if (db) sqlite3_close(db); }
};

void exec_or_throw(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string msg = err ? err : "sqlite exec failed";
        sqlite3_free(err);
        throw std::runtime_error(msg);
    }
}

void exec_ignore_duplicate_column(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string msg = err ? err : "sqlite exec failed";
        sqlite3_free(err);
        if (msg.find("duplicate column name") == std::string::npos) throw std::runtime_error(msg);
    }
}

void bind_text(sqlite3_stmt* stmt, int index, const std::string& value) {
    if (sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) throw std::runtime_error("sqlite bind text failed");
}
void bind_double(sqlite3_stmt* stmt, int index, double value) {
    if (sqlite3_bind_double(stmt, index, value) != SQLITE_OK) throw std::runtime_error("sqlite bind double failed");
}

std::string table_name_for_mode(const std::string& mode_filter) {
    return mode_filter == "run" ? "realtime_controller_events" : "controller_events";
}

void create_table(sqlite3* db, const std::string& table_name) {
    exec_or_throw(db,
        "CREATE TABLE IF NOT EXISTS " + table_name + " ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "event_datetime TEXT NOT NULL,"
        "mode TEXT NOT NULL,"
        "damper_percent REAL NOT NULL,"
        "fan_percent REAL NOT NULL,"
        "heater_percent REAL NOT NULL,"
        "damper_energy_kwh REAL NOT NULL,"
        "fan_energy_kwh REAL NOT NULL,"
        "heater_energy_kwh REAL NOT NULL,"
        "total_energy_kwh REAL NOT NULL,"
        "predicted_reward REAL NOT NULL,"
        "mean_body_weight_kg REAL NOT NULL,"
        "herd_size REAL NOT NULL,"
        "outdoor_temp_c REAL NOT NULL,"
        "indoor_temp_c REAL NOT NULL,"
        "indoor_relative_humidity REAL NOT NULL,"
        "indoor_airflow_m3_s REAL NOT NULL,"
        "outdoor_wind_speed_m_s REAL NOT NULL,"
        "solar_radiation_w_m2 REAL NOT NULL,"
        "internal_heat_generated_w REAL NOT NULL,"
        "lct_c REAL NOT NULL,"
        "uct_c REAL NOT NULL"
        ");");
    exec_or_throw(db, "CREATE INDEX IF NOT EXISTS idx_" + table_name + "_dt ON " + table_name + "(event_datetime);");
    exec_or_throw(db, "CREATE INDEX IF NOT EXISTS idx_" + table_name + "_mode_dt ON " + table_name + "(mode, event_datetime);");
    exec_ignore_duplicate_column(db, "ALTER TABLE " + table_name + " ADD COLUMN indoor_airflow_m3_s REAL NOT NULL DEFAULT 0.0;");
}

void insert_record(sqlite3* db, const std::string& table_name, const EventLogRecord& record) {
    const std::string sql =
        "INSERT INTO " + table_name + " ("
        "event_datetime,mode,damper_percent,fan_percent,heater_percent,"
        "damper_energy_kwh,fan_energy_kwh,heater_energy_kwh,total_energy_kwh,"
        "predicted_reward,mean_body_weight_kg,herd_size,outdoor_temp_c,indoor_temp_c,"
        "indoor_relative_humidity,indoor_airflow_m3_s,outdoor_wind_speed_m_s,solar_radiation_w_m2,"
        "internal_heat_generated_w,lct_c,uct_c) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) throw std::runtime_error("Could not prepare event insert statement.");
    try {
        bind_text(stmt, 1, record.event_datetime); bind_text(stmt, 2, record.mode);
        bind_double(stmt, 3, record.damper_percent); bind_double(stmt, 4, record.fan_percent); bind_double(stmt, 5, record.heater_percent);
        bind_double(stmt, 6, record.damper_energy_kwh); bind_double(stmt, 7, record.fan_energy_kwh); bind_double(stmt, 8, record.heater_energy_kwh);
        bind_double(stmt, 9, record.total_energy_kwh); bind_double(stmt, 10, record.predicted_reward); bind_double(stmt, 11, record.mean_body_weight_kg);
        bind_double(stmt, 12, record.herd_size); bind_double(stmt, 13, record.outdoor_temp_c); bind_double(stmt, 14, record.indoor_temp_c);
        bind_double(stmt, 15, record.indoor_relative_humidity); bind_double(stmt, 16, record.indoor_airflow_m3_s); bind_double(stmt, 17, record.outdoor_wind_speed_m_s);
        bind_double(stmt, 18, record.solar_radiation_w_m2); bind_double(stmt, 19, record.internal_heat_generated_w); bind_double(stmt, 20, record.lct_c); bind_double(stmt, 21, record.uct_c);
        if (sqlite3_step(stmt) != SQLITE_DONE) throw std::runtime_error("Could not insert event record into SQLite.");
    } catch (...) { sqlite3_finalize(stmt); throw; }
    sqlite3_finalize(stmt);
}

} // namespace

void initialize_event_store(const EventStoreSettings& settings) {
    const auto pos = settings.sqlite_path.find_last_of('/');
    if (pos != std::string::npos) {
        const std::string dir = settings.sqlite_path.substr(0, pos);
        if (!dir.empty()) std::system((std::string("mkdir -p \"") + dir + "\"").c_str());
    }
    SqliteHandle db(settings.sqlite_path);
    create_table(db.db, "controller_events");
    create_table(db.db, "realtime_controller_events");
}

void log_event_record(const EventStoreSettings& settings, const EventLogRecord& record) {
    initialize_event_store(settings);
    SqliteHandle db(settings.sqlite_path);
    insert_record(db.db, "controller_events", record);
}

void log_realtime_event_record(const EventStoreSettings& settings, const EventLogRecord& record) {
    initialize_event_store(settings);
    SqliteHandle db(settings.sqlite_path);
    insert_record(db.db, "realtime_controller_events", record);
}

std::vector<EstimatorAggregateRow> query_estimator_aggregate(const EventStoreSettings& settings, const std::string& start_datetime, const std::string& end_datetime, int interval_minutes, const std::string& mode_filter) {
    initialize_event_store(settings);
    SqliteHandle db(settings.sqlite_path);
    const std::string sql =
        "SELECT "
        "datetime(((strftime('%s', event_datetime) / (? * 60)) * (? * 60)), 'unixepoch') AS interval_start,"
        "datetime((((strftime('%s', event_datetime) / (? * 60)) * (? * 60)) + (? * 60)), 'unixepoch') AS interval_end,"
        "COUNT(*), SUM(damper_energy_kwh), SUM(fan_energy_kwh), SUM(heater_energy_kwh), SUM(total_energy_kwh), AVG(mean_body_weight_kg), AVG(indoor_temp_c) "
        "FROM " + table_name_for_mode(mode_filter) + " WHERE event_datetime >= ? AND event_datetime <= ? AND (? = '' OR mode = ?) GROUP BY interval_start, interval_end ORDER BY interval_start;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db.db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) throw std::runtime_error("Could not prepare estimator aggregate query.");
    std::vector<EstimatorAggregateRow> out;
    try {
        sqlite3_bind_int(stmt, 1, interval_minutes); sqlite3_bind_int(stmt, 2, interval_minutes); sqlite3_bind_int(stmt, 3, interval_minutes); sqlite3_bind_int(stmt, 4, interval_minutes); sqlite3_bind_int(stmt, 5, interval_minutes);
        bind_text(stmt, 6, start_datetime); bind_text(stmt, 7, end_datetime);
        const std::string effective_mode = mode_filter == "run" ? "" : mode_filter;
        bind_text(stmt, 8, effective_mode); bind_text(stmt, 9, effective_mode);
        while (true) { int rc = sqlite3_step(stmt); if (rc == SQLITE_DONE) break; if (rc != SQLITE_ROW) throw std::runtime_error("Could not read estimator aggregate rows.");
            EstimatorAggregateRow row{}; row.interval_start = reinterpret_cast<const char*>(sqlite3_column_text(stmt,0)); row.interval_end = reinterpret_cast<const char*>(sqlite3_column_text(stmt,1)); row.decision_count = sqlite3_column_int(stmt,2); row.damper_energy_kwh = sqlite3_column_double(stmt,3); row.fan_energy_kwh = sqlite3_column_double(stmt,4); row.heater_energy_kwh = sqlite3_column_double(stmt,5); row.total_energy_kwh = sqlite3_column_double(stmt,6); row.mean_body_weight_kg = sqlite3_column_double(stmt,7); row.mean_indoor_temp_c = sqlite3_column_double(stmt,8); out.push_back(row);} 
    } catch (...) { sqlite3_finalize(stmt); throw; }
    sqlite3_finalize(stmt); return out;
}

std::vector<DayNightClimateAggregate> query_day_night_climate_aggregate(const EventStoreSettings& settings, const std::string& query_date, const std::string& mode_filter) {
    initialize_event_store(settings);
    SqliteHandle db(settings.sqlite_path);
    const std::string sql =
        "SELECT substr(event_datetime, 1, 10) AS query_date, CASE WHEN CAST(substr(event_datetime, 12, 2) AS INTEGER) >= 6 AND CAST(substr(event_datetime, 12, 2) AS INTEGER) < 18 THEN 'day' ELSE 'night' END AS period_label, COUNT(*), AVG(indoor_temp_c), AVG(indoor_relative_humidity), AVG(indoor_airflow_m3_s), AVG(internal_heat_generated_w) FROM " + table_name_for_mode(mode_filter) + " WHERE substr(event_datetime, 1, 10) = ? AND (? = '' OR mode = ?) GROUP BY query_date, period_label ORDER BY CASE period_label WHEN 'day' THEN 0 ELSE 1 END;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db.db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) throw std::runtime_error("Could not prepare day/night climate aggregate query.");
    std::vector<DayNightClimateAggregate> out;
    try {
        const std::string effective_mode = mode_filter == "run" ? "" : mode_filter;
        bind_text(stmt, 1, query_date); bind_text(stmt, 2, effective_mode); bind_text(stmt, 3, effective_mode);
        while (true) { int rc = sqlite3_step(stmt); if (rc == SQLITE_DONE) break; if (rc != SQLITE_ROW) throw std::runtime_error("Could not read day/night climate aggregate rows."); DayNightClimateAggregate row{}; row.query_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt,0)); row.period_label = reinterpret_cast<const char*>(sqlite3_column_text(stmt,1)); row.sample_count = sqlite3_column_int(stmt,2); row.mean_indoor_temp_c = sqlite3_column_double(stmt,3); row.mean_indoor_relative_humidity = sqlite3_column_double(stmt,4); row.mean_indoor_airflow_m3_s = sqlite3_column_double(stmt,5); row.mean_internal_heat_generated_w = sqlite3_column_double(stmt,6); out.push_back(row);} 
    } catch (...) { sqlite3_finalize(stmt); throw; }
    sqlite3_finalize(stmt); return out;
}

} // namespace cattle_climate
