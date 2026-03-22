#pragma once

#include <string>
#include <vector>

#include "cattle_climate/env.hpp"
#include "cattle_climate/settings.hpp"
#include "cattle_climate/simulator.hpp"
#include "cattle_climate/thermal_comfort.hpp"
#include "cattle_climate/weather_dataset.hpp"
#include "cattle_climate/transformation.hpp"
#include "cattle_climate/event_store.hpp"

namespace cattle_climate {


struct DailyWeightRule {
    double gain_when_comfortable_kg_per_day{0.10};
    double loss_when_stressed_kg_per_day{0.08};
};

struct MPCWeights {
    double weight_gain_reward{200.0};
    double comfort_penalty{150.0};
    double energy_penalty{20.0};
    double humidity_penalty{10.0};
    double wind_penalty{8.0};
    double actuator_change_penalty{0.10};
};

struct CattleSettings {
    std::string cow_id{"default_cow"};
    std::string breed{"charolais"};
    int cow_count{1};
    double initial_body_weight_kg{450.0};
    DailyWeightRule weight_rule{};
};

struct TrainingSettings {
    std::string dataset_path{"dataset/Alvand_36_19_50_06_1356998400_1735689599_68cd492551fde7000939a233.csv"};
    std::string start_datetime{"2013-01-01 00:00:00"};
    std::string end_datetime{"2022-12-31 23:00:00"};
    int steps_per_episode{96};
    int horizon_steps{4};
    int episodes{10};
    int seed{12345};
    std::string model_output_path{"models/cattle_farming_env_rl_mpc_model.json"};
};

struct ValidationSettings {
    std::string start_datetime{"2023-01-01 00:00:00"};
    std::string end_datetime{"2023-03-31 23:00:00"};
    bool automatic_weight_update{true};
    std::string manual_weight_file{};
    std::string csv_output_path{"output/validation_rollout.csv"};
    std::string svg_output_path{"output/validation_rollout.svg"};
};

struct RuntimeServiceSettings {
    std::string model_path{"models/cattle_farming_env_rl_mpc_model.json"};
    int polling_interval_seconds{5};
    int max_cycles{0};
    std::string stop_file_path{"/tmp/cattle_farming_env_rl_mpc.stop"};
};

struct GrpcWeightFeedSettings {
    bool enabled{false};
    std::string endpoint{"localhost:50051"};
    std::string service{"cattle.weights.WeightFeedService"};
    std::string method{"GetLatestHerdSnapshot"};
    std::string snapshot_json_path{};
    int request_timeout_ms{2000};
};

struct ProjectSettings {
    SimulationSettings simulator{};
    CattleSettings cattle{};
    std::vector<BreedPreset> breed_presets{};
    std::vector<CattleSettings> training_breed_profiles{};
    TrainingSettings training{};
    ValidationSettings validation{};
    MPCWeights reward_weights{};
    GrpcWeightFeedSettings grpc_weight_feed{};
    TransformationSettings transformation{};
    EventStoreSettings event_store{};
    RuntimeServiceSettings runtime_service{};
};

struct ControllerModel {
    BreedPreset default_breed{};
    std::vector<std::string> trained_breeds{};
    MPCWeights weights{};
    CattleSettings cattle{};
    int horizon_steps{4};
};

struct ValidationRecord {
    std::size_t step_index{};
    std::string dt_iso;
    double minute{};
    double body_weight_kg{};
    double herd_size{};
    double outdoor_temp_c{};
    double outdoor_relative_humidity{};
    double outdoor_wind_speed_m_s{};
    double solar_radiation_w_m2{};
    double precipitation_mm{};
    double cloud_cover_fraction{};
    double indoor_temp_c{};
    double indoor_relative_humidity{};
    double indoor_airflow_m3_s{};
    double internal_heat_generated_w{};
    double lct_c{};
    double uct_c{};
    double reward{};
    double weight_delta_kg{};
    RLAction action{};
    SimulationState state{};
};

ProjectSettings load_project_settings_from_json(const std::string& path);
ControllerModel load_controller_model(const std::string& path);
void save_controller_model(const ControllerModel& model, const std::string& path);
int train_controller(const ProjectSettings& project);
int validate_controller(const ProjectSettings& project, const std::string& model_path,
                        bool automatic_weight_update, const std::string& manual_weight_file,
                        const std::string& start_datetime, const std::string& end_datetime,
                        const std::string& csv_path, const std::string& svg_path,
                        bool use_simulat_weights);
int operate_controller(const ProjectSettings& project, const std::string& model_path,
                       const std::string& start_datetime, const std::string& end_datetime,
                       bool use_real_weights);
int export_estimator_report(const ProjectSettings& project, const std::string& start_datetime,
                            const std::string& end_datetime, int interval_minutes,
                            const std::string& mode_filter, const std::string& output_path);
int export_ligaps_climate_report(const ProjectSettings& project, const std::string& query_date,
                                 const std::string& mode_filter, const std::string& output_path);
int run_controller_service(const ProjectSettings& project, const std::string& model_path);
int stop_controller_service(const ProjectSettings& project);
std::vector<double> load_manual_daily_weights(const std::string& path);
std::string write_validation_csv(const std::vector<ValidationRecord>& history, const std::string& path);
std::string write_validation_svg(const std::vector<ValidationRecord>& history, const std::string& path);

} // namespace cattle_climate
