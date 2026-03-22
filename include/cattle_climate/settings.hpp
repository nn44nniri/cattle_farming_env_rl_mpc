#pragma once

#include <string>

#include "cattle_climate/formulas.hpp"

namespace cattle_climate {

struct ActuatorCommand {
    bool dampers_on{false};
    bool fans_on{false};
    bool heaters_on{false};
    double damper_activity_percent{0.0};
    double fan_activity_percent{0.0};
    double heater_activity_percent{0.0};
    double dampers_active_minutes{0.0};
    double fans_active_minutes{0.0};
    double heaters_active_minutes{0.0};
};

struct ActuatorSettings {
    double fan_max_flow_m3_s{0.3};
    double damper_discharge_coefficient{0.62};
    double heater_max_power_w{5000.0};
    double heater_cop{-1.0};
    double damper_max_electrical_power_w{50.0};
    double fan_max_electrical_power_w{750.0};
    double heater_max_electrical_power_w{5000.0};
    double damper_max_resource_rate_per_hour{0.05};
    double fan_max_resource_rate_per_hour{0.75};
    double heater_max_resource_rate_per_hour{5.0};
    std::string damper_resource_unit{"kWh"};
    std::string fan_resource_unit{"kWh"};
    std::string heater_resource_unit{"kWh"};
};

struct SimulationSettings {
    RoomGeometry room;
    ClimateInputs climate;
    InternalLoads internal_loads;
    ActuatorSettings actuators;
    double initial_indoor_temp_c{15.0};
    double initial_indoor_relative_humidity{0.60};
    double timestep_seconds{60.0};
    int n_steps{10};
};

SimulationSettings load_settings_from_json(const std::string& path);

} // namespace cattle_climate
