#pragma once

#include <string>
#include <vector>

#include "cattle_climate/settings.hpp"

namespace cattle_climate {

struct SimulationState {
    double minute{};
    double outside_temp_c{};
    double outside_relative_humidity{};
    double outside_reference_wind_speed_m_s{};
    double outside_local_wind_speed_m_s{};
    double outside_wind_direction_deg{};
    double outside_co2_ppm{};
    double outside_direct_radiation_w_m2{};
    double outside_diffuse_radiation_w_m2{};
    double indoor_temp_c{};
    double indoor_relative_humidity{};
    double indoor_humidity_ratio_kg_per_kg{};
    StepDiagnostics diagnostics{};
};

struct SimulationSummary {
    double final_outside_temp_c{};
    double final_outside_relative_humidity{};
    double final_outside_reference_wind_speed_m_s{};
    double final_outside_local_wind_speed_m_s{};
    double final_outside_wind_direction_deg{};
    double final_outside_co2_ppm{};
    double final_outside_direct_radiation_w_m2{};
    double final_outside_diffuse_radiation_w_m2{};
    double final_inside_temp_c{};
    double final_relative_humidity{};
    double final_airflow_m3_s{};
    double average_airflow_m3_s{};
    double cumulative_damper_electrical_energy_kwh{};
    double cumulative_fan_electrical_energy_kwh{};
    double cumulative_heater_electrical_energy_kwh{};
    double cumulative_damper_resource_units{};
    double cumulative_fan_resource_units{};
    double cumulative_heater_resource_units{};
};

struct SimulationResult {
    std::vector<SimulationState> states;
    SimulationSummary summary{};
};

class SingleZoneSimulator {
public:
    explicit SingleZoneSimulator(SimulationSettings settings);
    [[nodiscard]] SimulationResult estimate(const ActuatorCommand& command) const;
    [[nodiscard]] const SimulationSettings& settings() const noexcept { return settings_; }

private:
    SimulationSettings settings_;
};

std::string write_csv_report(const SimulationResult& result, const std::string& path);
std::string write_svg_graph_report(const SimulationResult& result, const std::string& path, double max_minutes);

} // namespace cattle_climate
