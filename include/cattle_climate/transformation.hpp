#pragma once

#include <string>
#include <vector>

#include "cattle_climate/env.hpp"
#include "cattle_climate/weather_dataset.hpp"

namespace cattle_climate {

struct GrpcNodeSettings {
    bool enabled{false};
    std::string endpoint{"unix:///tmp/cattle.sock"};
    std::string service;
    std::string method;
    std::string request_timeout_ms{"2000"};
    std::string request_json_path;
    std::string response_json_path;
};

struct TransformationSettings {
    GrpcNodeSettings ligaps_beef_weights{};
    GrpcNodeSettings ligaps_beef_optimizer{};
    GrpcNodeSettings sensors_interface{};
    GrpcNodeSettings actuators_interface{};
    GrpcNodeSettings estimator_interface{};
};

struct HerdCowGrpcRecord {
    std::string cow_id;
    std::string breed;
    double body_weight_kg{0.0};
    double gain_when_comfortable_kg_per_day{0.10};
    double loss_when_stressed_kg_per_day{0.08};
};

struct HerdWeightSnapshot {
    std::string effective_datetime;
    std::vector<HerdCowGrpcRecord> cows;
};

struct SensorSnapshot {
    std::string effective_datetime;
    bool has_outdoor_temp_c{false};
    double outdoor_temp_c{0.0};
    bool has_relative_humidity{false};
    double relative_humidity{0.0};
    bool has_wind_speed_m_s{false};
    double wind_speed_m_s{0.0};
    bool has_direct_radiation_w_m2{false};
    double direct_radiation_w_m2{0.0};
    bool has_diffuse_radiation_w_m2{false};
    double diffuse_radiation_w_m2{0.0};
    bool has_precipitation_mm{false};
    double precipitation_mm{0.0};
    bool has_cloud_cover_fraction{false};
    double cloud_cover_fraction{0.0};
};



struct LiGAPSClimateRequest {
    std::string node_name;
    std::string query_date;
    std::string mode_filter;
};

struct LiGAPSDayNightClimateDispatch {
    std::string generated_at;
    std::string query_date;
    std::string source_sqlite_path;
    std::string mode_filter;
    struct PeriodSummary {
        std::string period_label;
        int sample_count{0};
        double mean_indoor_temp_c{0.0};
        double mean_indoor_relative_humidity{0.0};
        double mean_indoor_airflow_m3_s{0.0};
        double mean_internal_heat_generated_w{0.0};
    };
    std::vector<PeriodSummary> periods;
};

struct EstimatorRequest {
    std::string node_name;
    std::string start_datetime;
    std::string end_datetime;
    int interval_minutes{60};
    std::string mode_filter;
};

struct EstimatorAggregateDispatch {
    std::string generated_at;
    std::string start_datetime;
    std::string end_datetime;
    int interval_minutes{60};
    std::string mode_filter;
    std::string source_sqlite_path;
    struct Row {
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
    std::vector<Row> rows;
};

struct ActuatorDispatchRecord {
    std::string effective_datetime;
    RLAction action{};
    double predicted_reward{0.0};
    double mean_body_weight_kg{0.0};
    double herd_size{0.0};
    double lct_c{0.0};
    double uct_c{0.0};
};

std::vector<HerdWeightSnapshot> load_weight_snapshots_from_bridge(const std::string& path);
bool lookup_weight_snapshot_for_datetime(std::vector<HerdCowGrpcRecord>& herd, const std::vector<HerdWeightSnapshot>& snapshots, const std::string& current_datetime);
std::vector<SensorSnapshot> load_sensor_snapshots_from_bridge(const std::string& path);
bool lookup_sensor_snapshot_for_datetime(SensorSnapshot& snapshot, const std::vector<SensorSnapshot>& snapshots, const std::string& current_datetime);
WeatherSample apply_sensor_snapshot_to_weather(const WeatherSample& base, const SensorSnapshot* snapshot);
std::string write_actuator_dispatch_record(const std::string& path, const ActuatorDispatchRecord& record, const GrpcNodeSettings& node);
EstimatorRequest load_estimator_request(const std::string& path);
std::string write_estimator_dispatch_record(const std::string& path, const EstimatorAggregateDispatch& dispatch, const GrpcNodeSettings& node);
LiGAPSClimateRequest load_ligaps_climate_request(const std::string& path);
std::string write_ligaps_climate_dispatch_record(const std::string& path, const LiGAPSDayNightClimateDispatch& dispatch, const GrpcNodeSettings& node);

} // namespace cattle_climate
