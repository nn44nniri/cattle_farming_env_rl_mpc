#include "cattle_climate/settings.hpp"

#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace cattle_climate {
namespace {

std::string read_text_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Unable to open settings file: " + path);
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string extract_object(const std::string& json, const std::string& key) {
    const std::regex key_regex("\\\"" + key + "\\\"\\s*:\\s*\\{");
    std::smatch match;
    if (!std::regex_search(json, match, key_regex)) {
        throw std::runtime_error("Missing JSON object: " + key);
    }
    const std::size_t start = static_cast<std::size_t>(match.position(0) + match.length(0) - 1);
    int depth = 0;
    for (std::size_t i = start; i < json.size(); ++i) {
        if (json[i] == '{') {
            ++depth;
        } else if (json[i] == '}') {
            --depth;
            if (depth == 0) {
                return json.substr(start, i - start + 1);
            }
        }
    }
    throw std::runtime_error("Unclosed JSON object: " + key);
}

double extract_number(const std::string& json, const std::string& key, double default_value) {
    const std::regex r("\\\"" + key + "\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)");
    std::smatch match;
    if (std::regex_search(json, match, r)) {
        return std::stod(match[1].str());
    }
    return default_value;
}

int extract_int(const std::string& json, const std::string& key, int default_value) {
    return static_cast<int>(extract_number(json, key, static_cast<double>(default_value)));
}

std::string extract_string(const std::string& json, const std::string& key, const std::string& default_value) {
    const std::regex r("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    if (std::regex_search(json, match, r)) {
        return match[1].str();
    }
    return default_value;
}

} // namespace

SimulationSettings load_settings_from_json(const std::string& path) {
    const std::string text = read_text_file(path);
    SimulationSettings s{};

    const std::string room = extract_object(text, "room");
    s.room.length_m = extract_number(room, "length_m", s.room.length_m);
    s.room.width_m = extract_number(room, "width_m", s.room.width_m);
    s.room.height_m = extract_number(room, "height_m", s.room.height_m);
    s.room.wall_u_value_w_m2k = extract_number(room, "wall_u_value_w_m2k", s.room.wall_u_value_w_m2k);
    s.room.roof_u_value_w_m2k = extract_number(room, "roof_u_value_w_m2k", s.room.roof_u_value_w_m2k);
    s.room.floor_u_value_w_m2k = extract_number(room, "floor_u_value_w_m2k", s.room.floor_u_value_w_m2k);
    s.room.window_u_value_w_m2k = extract_number(room, "window_u_value_w_m2k", s.room.window_u_value_w_m2k);
    s.room.window_count = extract_int(room, "window_count", s.room.window_count);
    s.room.window_area_each_m2 = extract_number(room, "window_area_each_m2", s.room.window_area_each_m2);
    s.room.opening_count = extract_number(room, "opening_count", s.room.opening_count);
    s.room.opening_height_m = extract_number(room, "opening_height_m", s.room.opening_height_m);
    s.room.opening_width_m = extract_number(room, "opening_width_m", s.room.opening_width_m);
    s.room.leakage_area_m2 = extract_number(room, "leakage_area_m2", s.room.leakage_area_m2);

    const std::string climate = extract_object(text, "climate");
    s.climate.outside_temp_c = extract_number(climate, "outside_temp_c", s.climate.outside_temp_c);
    s.climate.outside_relative_humidity = extract_number(climate, "outside_relative_humidity", s.climate.outside_relative_humidity);
    s.climate.outside_co2_ppm = extract_number(climate, "outside_co2_ppm", s.climate.outside_co2_ppm);
    s.climate.atmospheric_pressure_pa = extract_number(climate, "atmospheric_pressure_pa", s.climate.atmospheric_pressure_pa);
    s.climate.reference_wind_speed_m_s = extract_number(climate, "reference_wind_speed_m_s", s.climate.reference_wind_speed_m_s);
    s.climate.outside_wind_direction_deg = extract_number(climate, "outside_wind_direction_deg", s.climate.outside_wind_direction_deg);
    s.climate.reference_height_m = extract_number(climate, "reference_height_m", s.climate.reference_height_m);
    s.climate.a0_coeff = extract_number(climate, "a0_coeff", s.climate.a0_coeff);
    s.climate.a_exp = extract_number(climate, "a_exp", s.climate.a_exp);
    s.climate.facade_pressure_coefficient = extract_number(climate, "facade_pressure_coefficient", s.climate.facade_pressure_coefficient);
    s.climate.direct_radiation_w_m2 = extract_number(climate, "direct_radiation_w_m2", s.climate.direct_radiation_w_m2);
    s.climate.diffuse_radiation_w_m2 = extract_number(climate, "diffuse_radiation_w_m2", s.climate.diffuse_radiation_w_m2);
    s.climate.ground_reflectance = extract_number(climate, "ground_reflectance", s.climate.ground_reflectance);
    s.climate.latitude_deg = extract_number(climate, "latitude_deg", s.climate.latitude_deg);
    s.climate.longitude_deg = extract_number(climate, "longitude_deg", s.climate.longitude_deg);
    s.climate.standard_meridian_deg = extract_number(climate, "standard_meridian_deg", s.climate.standard_meridian_deg);
    s.climate.day_of_year = extract_int(climate, "day_of_year", s.climate.day_of_year);
    s.climate.clock_time_hours = extract_number(climate, "clock_time_hours", s.climate.clock_time_hours);
    s.climate.wall_azimuth_deg = extract_number(climate, "wall_azimuth_deg", s.climate.wall_azimuth_deg);
    s.climate.wall_tilt_deg = extract_number(climate, "wall_tilt_deg", s.climate.wall_tilt_deg);
    s.climate.window_transmitted_solar_coefficient = extract_number(climate, "window_transmitted_solar_coefficient", s.climate.window_transmitted_solar_coefficient);
    s.climate.glazing_solar_coefficient = extract_number(climate, "glazing_solar_coefficient", s.climate.glazing_solar_coefficient);
    s.climate.shading_coefficient = extract_number(climate, "shading_coefficient", s.climate.shading_coefficient);
    s.climate.sky_emissivity = extract_number(climate, "sky_emissivity", s.climate.sky_emissivity);

    const std::string internal_loads = extract_object(text, "internal_loads");
    s.internal_loads.base_sensible_gains_w = extract_number(internal_loads, "base_sensible_gains_w", s.internal_loads.base_sensible_gains_w);
    s.internal_loads.base_moisture_generation_kg_s = extract_number(internal_loads, "base_moisture_generation_kg_s", s.internal_loads.base_moisture_generation_kg_s);
    s.internal_loads.base_co2_generation_kg_s = extract_number(internal_loads, "base_co2_generation_kg_s", s.internal_loads.base_co2_generation_kg_s);

    const std::string actuators = extract_object(text, "actuator_settings");
    s.actuators.fan_max_flow_m3_s = extract_number(actuators, "fan_max_flow_m3_s", s.actuators.fan_max_flow_m3_s);
    s.actuators.damper_discharge_coefficient = extract_number(actuators, "damper_discharge_coefficient", s.actuators.damper_discharge_coefficient);
    s.actuators.heater_max_power_w = extract_number(actuators, "heater_max_power_w", s.actuators.heater_max_power_w);
    s.actuators.heater_cop = extract_number(actuators, "heater_cop", s.actuators.heater_cop);
    s.actuators.damper_max_electrical_power_w = extract_number(actuators, "damper_max_electrical_power_w", s.actuators.damper_max_electrical_power_w);
    s.actuators.fan_max_electrical_power_w = extract_number(actuators, "fan_max_electrical_power_w", s.actuators.fan_max_electrical_power_w);
    s.actuators.heater_max_electrical_power_w = extract_number(actuators, "heater_max_electrical_power_w", s.actuators.heater_max_electrical_power_w);
    s.actuators.damper_max_resource_rate_per_hour = extract_number(actuators, "damper_max_resource_rate_per_hour", s.actuators.damper_max_resource_rate_per_hour);
    s.actuators.fan_max_resource_rate_per_hour = extract_number(actuators, "fan_max_resource_rate_per_hour", s.actuators.fan_max_resource_rate_per_hour);
    s.actuators.heater_max_resource_rate_per_hour = extract_number(actuators, "heater_max_resource_rate_per_hour", s.actuators.heater_max_resource_rate_per_hour);
    s.actuators.damper_resource_unit = extract_string(actuators, "damper_resource_unit", s.actuators.damper_resource_unit);
    s.actuators.fan_resource_unit = extract_string(actuators, "fan_resource_unit", s.actuators.fan_resource_unit);
    s.actuators.heater_resource_unit = extract_string(actuators, "heater_resource_unit", s.actuators.heater_resource_unit);

    const std::string simulation = extract_object(text, "simulation");
    s.initial_indoor_temp_c = extract_number(simulation, "initial_indoor_temp_c", s.initial_indoor_temp_c);
    s.initial_indoor_relative_humidity = extract_number(simulation, "initial_indoor_relative_humidity", s.initial_indoor_relative_humidity);
    s.timestep_seconds = extract_number(simulation, "timestep_seconds", s.timestep_seconds);
    s.n_steps = extract_int(simulation, "n_steps", s.n_steps);

    s.climate.building_height_m = s.room.height_m;
    return s;
}

} // namespace cattle_climate
