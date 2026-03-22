#pragma once

#include <string>

namespace cattle_climate {

struct ClimateInputs {
    double outside_temp_c{};
    double outside_relative_humidity{}; // 0..1
    double outside_co2_ppm{420.0};
    double atmospheric_pressure_pa{101325.0};
    double reference_wind_speed_m_s{};
    double outside_wind_direction_deg{270.0};
    double building_height_m{};
    double reference_height_m{10.0};
    double a0_coeff{1.0};
    double a_exp{0.15};
    double facade_pressure_coefficient{0.6};
    double direct_radiation_w_m2{};
    double diffuse_radiation_w_m2{};
    double ground_reflectance{0.2};
    double latitude_deg{45.0};
    double longitude_deg{-73.0};
    double standard_meridian_deg{-75.0};
    int day_of_year{80};
    double clock_time_hours{12.0};
    double wall_azimuth_deg{180.0};
    double wall_tilt_deg{90.0};
    double window_transmitted_solar_coefficient{0.35};
    double glazing_solar_coefficient{0.50};
    double shading_coefficient{1.0};
    double sky_emissivity{0.90};
};

struct RoomGeometry {
    double length_m{};
    double width_m{};
    double height_m{};
    double wall_u_value_w_m2k{0.6};
    double roof_u_value_w_m2k{0.6};
    double floor_u_value_w_m2k{0.3};
    double window_u_value_w_m2k{2.4};
    int window_count{};
    double window_area_each_m2{};
    double opening_count{1.0};
    double opening_height_m{0.5};
    double opening_width_m{0.1};
    double leakage_area_m2{0.01};
};

struct InternalLoads {
    double base_sensible_gains_w{};
    double base_moisture_generation_kg_s{};
    double base_co2_generation_kg_s{};
};

struct DerivedGeometry {
    double volume_m3{};
    double wall_area_m2{};
    double roof_area_m2{};
    double floor_area_m2{};
    double total_window_area_m2{};
    double total_opening_area_m2{};
    double ua_w_k{};
};

struct SolarGeometryDiagnostics {
    double equation_of_time_minutes{};
    double solar_time_hours{};
    double declination_rad{};
    double elevation_rad{};
    double azimuth_rad{};
    double incident_angle_rad{};
    double direct_on_surface_w_m2{};
    double diffuse_on_surface_w_m2{};
    double reflected_on_surface_w_m2{};
};

struct StepDiagnostics {
    double sky_temp_c{};
    double outdoor_humidity_ratio_kg_per_kg{};
    double indoor_humidity_ratio_kg_per_kg{};
    double local_wind_speed_m_s{};
    double wind_pressure_pa{};
    double leak_delta_p_pa{};
    double leakage_flow_m3_s{};
    double large_opening_flow_m3_s{};
    double mechanical_ventilation_flow_m3_s{};
    double total_ventilation_flow_m3_s{};
    double solar_gain_w{};
    double envelope_loss_w{};
    double ventilation_loss_w{};
    double heater_gain_w{};
    double average_airflow_m3_s{};
    double damper_electrical_power_w{};
    double fan_electrical_power_w{};
    double heater_electrical_power_w{};
    double damper_resource_rate_per_hour{};
    double fan_resource_rate_per_hour{};
    double heater_resource_rate_per_hour{};
    SolarGeometryDiagnostics solar{};
};

DerivedGeometry derive_geometry(const RoomGeometry& room);

double deg_to_rad(double deg);
double rad_to_deg(double rad);
double sky_temperature_c(double outdoor_air_temp_c);
double saturation_pressure_pa(double temp_c);
double vapor_partial_pressure_pa(double temp_c, double rh);
double humidity_ratio_from_vapor_pressure(double pressure_pa, double vapor_pressure_pa);
double humidity_ratio_from_rh(double temp_c, double rh, double pressure_pa);
double rh_from_humidity_ratio(double temp_c, double humidity_ratio, double pressure_pa);
double moist_air_density_kg_m3(double temp_c, double pressure_pa = 101325.0);

double local_wind_speed_m_s(const ClimateInputs& climate);
double equation_of_time_minutes(int day_of_year);
double solar_time_hours(const ClimateInputs& climate);
double solar_declination_rad(int day_of_year);
double solar_elevation_rad(const ClimateInputs& climate);
double solar_azimuth_rad(const ClimateInputs& climate);
double incident_angle_rad(const ClimateInputs& climate);
double direct_radiation_on_surface_w_m2(const ClimateInputs& climate);
double diffuse_radiation_on_surface_w_m2(const ClimateInputs& climate);
double ground_reflected_radiation_w_m2(const ClimateInputs& climate);
double external_convective_coefficient_w_m2k(double local_wind_speed_m_s_value);
double wind_pressure_pa(double local_wind_speed_m_s_value, double air_density_kg_m3, double pressure_coefficient);

double exterior_convective_heat_w(double h_w_m2k, double area_m2, double outdoor_temp_c, double wall_temp_c);
double absorbed_solar_radiation_w(double absorptivity, double area_m2, double direct_w_m2, double diffuse_w_m2);
double longwave_exchange_w(double emissivity, double area_m2, double surface_temp_c, double sky_temp_c);
double exterior_wall_heat_balance_w(double convective_w, double absorbed_solar_w, double longwave_w);

double window_shading_multiplier_total_heat(double shading_coefficient);
double window_shading_multiplier_direct_sw(double shading_coefficient);
double reference_transmitted_solar_gain_w(double window_area_m2, double direct_w_m2, double diffuse_w_m2);
double transmitted_shortwave_radiation_w(double reference_gain_w, double transmitted_solar_coefficient);
double indirect_window_load_w(double reference_gain_w, double solar_coefficient, double transmitted_solar_coefficient);
double absorbed_in_window_w(double reference_gain_w, double solar_coefficient);
double shaded_u_value_multiplier(double shading_coefficient);

double local_unit_heat_output_w(double control_signal, double max_thermal_power_w);
double local_unit_electric_power_w(double thermal_power_w, double coefficient_of_performance);
double actuator_electrical_power_w(double control_signal, double max_electrical_power_w);
double actuator_resource_rate_per_hour(double control_signal, double max_resource_rate_per_hour);

double terminal_heat_transport_w(double mass_flow_kg_s, double supply_temp_c, double zone_temp_c);
double terminal_scalar_transport_kg_s(double mass_flow_kg_s, double upstream_scalar, double downstream_scalar);
double leak_pressure_difference_pa(double p_in_pa, double rho_in, double z_in_m, double p_out_pa, double rho_out, double z_out_m, double rho_path, double dz_m);
double leakage_flow_m3_s(double leakage_area_m2, double delta_p_pa, double rho_air_kg_m3, double discharge_coefficient);
double large_vertical_opening_flow_m3_s(double discharge_coefficient, double opening_area_m2, double bottom_pressure_pa, double top_pressure_pa, double rho_air_kg_m3);
double zone_mean_radiant_exchange_w(double h_lw_w_m2k, double total_surface_area_m2, double mean_surface_temp_c, double mean_radiant_temp_c);

StepDiagnostics evaluate_step_diagnostics(
    const RoomGeometry& room,
    const ClimateInputs& climate,
    double indoor_temp_c,
    double indoor_humidity_ratio,
    double mechanical_flow_m3_s,
    double heater_gain_w,
    double damper_discharge_coefficient,
    double running_airflow_average_m3_s);

} // namespace cattle_climate
