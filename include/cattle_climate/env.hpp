#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "cattle_climate/simulator.hpp"

namespace cattle_climate {

struct OptimalRange {
    double min_value{};
    double max_value{};
};

struct VitalTargets {
    OptimalRange indoor_temp_c{8.0, 20.0};
    OptimalRange indoor_relative_humidity{0.50, 0.75};
    OptimalRange airflow_m3_s{0.05, 1.50};
};

struct RewardWeights {
    double temperature{2.0};
    double humidity{1.5};
    double airflow{1.0};
    double energy{0.35};
    double control_change{0.02};
    double in_range_bonus{0.50};
};

struct RLEnvironmentOptions {
    VitalTargets targets{};
    RewardWeights reward_weights{};
    std::size_t max_episode_steps{60};
};

struct RLAction {
    double damper_percent{0.0};
    double fan_percent{0.0};
    double heater_percent{0.0};
};

struct VitalStatus {
    double indoor_temp_error{};
    double indoor_relative_humidity_error{};
    double airflow_error{};
    bool indoor_temp_in_range{false};
    bool indoor_relative_humidity_in_range{false};
    bool airflow_in_range{false};
    double in_range_fraction{};
};

struct RLObservation {
    double minute{};
    double indoor_temp_c{};
    double indoor_relative_humidity{};
    double indoor_humidity_ratio_kg_per_kg{};
    double total_airflow_m3_s{};
    double average_airflow_m3_s{};
    double outdoor_temp_c{};
    double outdoor_relative_humidity{};
    double outdoor_reference_wind_speed_m_s{};
    double outdoor_direct_radiation_w_m2{};
    double outdoor_diffuse_radiation_w_m2{};
    double damper_power_w{};
    double fan_power_w{};
    double heater_power_w{};
    VitalStatus vital_status{};
};

struct RLStepInfo {
    std::size_t step_index{};
    double cumulative_damper_energy_kwh{};
    double cumulative_fan_energy_kwh{};
    double cumulative_heater_energy_kwh{};
    double cumulative_total_energy_kwh{};
    double cumulative_damper_resource_units{};
    double cumulative_fan_resource_units{};
    double cumulative_heater_resource_units{};
    double reward{};
    RLAction applied_action{};
    SimulationState simulator_state{};
};

struct RLStepResult {
    RLObservation observation{};
    double reward{};
    bool terminated{false};
    bool truncated{false};
    RLStepInfo info{};
};

class CattleVitalEnv {
public:
    CattleVitalEnv(SimulationSettings settings, RLEnvironmentOptions options = {});

    [[nodiscard]] RLObservation reset();
    [[nodiscard]] RLStepResult step(const RLAction& action);
    [[nodiscard]] const SimulationSettings& settings() const noexcept { return base_settings_; }
    [[nodiscard]] const RLEnvironmentOptions& options() const noexcept { return options_; }
    [[nodiscard]] std::size_t step_index() const noexcept { return step_index_; }
    [[nodiscard]] bool done() const noexcept { return done_; }
    [[nodiscard]] const std::vector<RLStepInfo>& history() const noexcept { return history_; }

private:
    [[nodiscard]] RLObservation make_observation(const SimulationState& state) const;
    [[nodiscard]] VitalStatus evaluate_vital_status(const SimulationState& state) const;
    [[nodiscard]] double compute_reward(const VitalStatus& status, const RLAction& current_action, const RLAction& previous_action,
                                        const SimulationState& state) const;
    [[nodiscard]] ActuatorCommand action_to_command(const RLAction& action) const;

    SimulationSettings base_settings_{};
    RLEnvironmentOptions options_{};
    SimulationState current_state_{};
    RLAction previous_action_{};
    bool done_{false};
    std::size_t step_index_{0};
    double cumulative_damper_energy_kwh_{0.0};
    double cumulative_fan_energy_kwh_{0.0};
    double cumulative_heater_energy_kwh_{0.0};
    double cumulative_damper_resource_units_{0.0};
    double cumulative_fan_resource_units_{0.0};
    double cumulative_heater_resource_units_{0.0};
    std::vector<RLStepInfo> history_{};
};

std::string write_rl_csv_report(const std::vector<RLStepInfo>& history, const std::string& path);
std::string write_rl_svg_graph_report(const std::vector<RLStepInfo>& history, const std::string& path, double max_minutes);

} // namespace cattle_climate
