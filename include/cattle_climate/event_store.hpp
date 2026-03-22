#pragma once

#include <string>
#include <vector>

#include "cattle_climate/settings.hpp"

namespace cattle_climate {

struct EventStoreSettings {
    std::string sqlite_path{"output/cattle_farming_env_rl_mpc.sqlite"};
};

struct EventLogRecord {
    std::string event_datetime;
    std::string mode;
    double damper_percent{0.0};
    double fan_percent{0.0};
    double heater_percent{0.0};
    double damper_energy_kwh{0.0};
    double fan_energy_kwh{0.0};
    double heater_energy_kwh{0.0};
    double total_energy_kwh{0.0};
    double predicted_reward{0.0};
    double mean_body_weight_kg{0.0};
    double herd_size{0.0};
    double outdoor_temp_c{0.0};
    double indoor_temp_c{0.0};
    double indoor_relative_humidity{0.0};
    double indoor_airflow_m3_s{0.0};
    double outdoor_wind_speed_m_s{0.0};
    double solar_radiation_w_m2{0.0};
    double internal_heat_generated_w{0.0};
    double lct_c{0.0};
    double uct_c{0.0};
};


struct DayNightClimateAggregate {
    std::string query_date;
    std::string period_label;
    int sample_count{0};
    double mean_indoor_temp_c{0.0};
    double mean_indoor_relative_humidity{0.0};
    double mean_indoor_airflow_m3_s{0.0};
    double mean_internal_heat_generated_w{0.0};
};

struct EstimatorAggregateRow {
    std::string interval_start;
    std::string interval_end;
    int decision_count{0};
    double damper_energy_kwh{0.0};
    double fan_energy_kwh{0.0};
    double heater_energy_kwh{0.0};
    double total_energy_kwh{0.0};
    double mean_body_weight_kg{0.0};
    double mean_indoor_temp_c{0.0};
};

void initialize_event_store(const EventStoreSettings& settings);
void log_event_record(const EventStoreSettings& settings, const EventLogRecord& record);
void log_realtime_event_record(const EventStoreSettings& settings, const EventLogRecord& record);
std::vector<EstimatorAggregateRow> query_estimator_aggregate(const EventStoreSettings& settings,
                                                             const std::string& start_datetime,
                                                             const std::string& end_datetime,
                                                             int interval_minutes,
                                                             const std::string& mode_filter);
std::vector<DayNightClimateAggregate> query_day_night_climate_aggregate(const EventStoreSettings& settings,
                                                                        const std::string& query_date,
                                                                        const std::string& mode_filter);

} // namespace cattle_climate
