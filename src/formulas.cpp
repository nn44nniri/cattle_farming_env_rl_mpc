#include "cattle_climate/formulas.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cattle_climate {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kSigma = 5.670374419e-8;
constexpr double kSmall = 1e-9;
constexpr double kWaterVaporRatio = 0.62198;
constexpr double kSpecificGasDryAir = 287.05;
constexpr double kGravity = 9.81;
constexpr double kCpAir = 1005.0;
constexpr double kMinPhysicalTempC = -100.0;
constexpr double kMaxPhysicalTempC = 100.0;

double clamp_physical_temperature_c(double temp_c) {
    return std::clamp(temp_c, kMinPhysicalTempC, kMaxPhysicalTempC);
}
}

DerivedGeometry derive_geometry(const RoomGeometry& room) {
    DerivedGeometry g{};
    g.volume_m3 = room.length_m * room.width_m * room.height_m;
    g.wall_area_m2 = 2.0 * room.height_m * (room.length_m + room.width_m);
    g.roof_area_m2 = room.length_m * room.width_m;
    g.floor_area_m2 = g.roof_area_m2;
    g.total_window_area_m2 = static_cast<double>(room.window_count) * room.window_area_each_m2;
    g.total_opening_area_m2 = room.opening_count * room.opening_height_m * room.opening_width_m;

    const double opaque_wall_area = std::max(0.0, g.wall_area_m2 - g.total_window_area_m2);
    g.ua_w_k = opaque_wall_area * room.wall_u_value_w_m2k
             + g.roof_area_m2 * room.roof_u_value_w_m2k
             + g.floor_area_m2 * room.floor_u_value_w_m2k
             + g.total_window_area_m2 * room.window_u_value_w_m2k;
    return g;
}

double deg_to_rad(double deg) { return deg * kPi / 180.0; }
double rad_to_deg(double rad) { return rad * 180.0 / kPi; }

double sky_temperature_c(double outdoor_air_temp_c) {
    // Origin: Bring, Sahlin & Vuolle (1999), climate model.
    // Formula (1) - Sky temperature:
    // T_sky = T_air - 5
    // Variables: T_sky = effective sky temperature [°C], T_air = outdoor air temperature [°C].
    return outdoor_air_temp_c - 5.0;
}

double saturation_pressure_pa(double temp_c) {
    // Origin: Bring, Sahlin & Vuolle (1999), climate model.
    // Formulae (5) and (6) - Saturation pressure below/above 0 °C.
    // Variables: T = dry-bulb temperature [°C], P_sat = saturation vapor pressure [Pa].
    const double safe_temp_c = clamp_physical_temperature_c(temp_c);
    const double temp_k = safe_temp_c + 273.15;

    double ln_pws = 0.0;
    if (safe_temp_c < 0.0) {
        ln_pws = -5.6745359e3 / temp_k
               + 6.3925247
               - 9.677843e-3 * temp_k
               + 0.62215701e-6 * temp_k * temp_k
               + 2.0747825e-9 * temp_k * temp_k * temp_k
               - 9.484024e-13 * temp_k * temp_k * temp_k * temp_k
               + 4.1635019 * std::log(temp_k);
    } else {
        ln_pws = -5.8002206e3 / temp_k
               + 1.3914993
               - 4.8640239e-2 * temp_k
               + 4.1764768e-5 * temp_k * temp_k
               - 1.4452093e-8 * temp_k * temp_k * temp_k
               + 6.5459673 * std::log(temp_k);
    }
    return std::exp(ln_pws);
}

double vapor_partial_pressure_pa(double temp_c, double rh) {
    // Origin: Bring, Sahlin & Vuolle (1999), climate model.
    // Formula (3) - Vapor pressure:
    // P_vap = P_sat * RelHum
    // Variables: P_vap = vapor partial pressure [Pa], P_sat = saturation pressure [Pa], RelHum = relative humidity [-].
    const double rh_clamped = std::clamp(rh, 0.0, 1.0);
    return saturation_pressure_pa(temp_c) * rh_clamped;
}

double humidity_ratio_from_vapor_pressure(double pressure_pa, double vapor_pressure_pa) {
    // Origin: Bring, Sahlin & Vuolle (1999), climate model.
    // Formula (7) - Humidity ratio:
    // HumRat = 0.62198 * p_vap / (p - p_vap)
    // Variables: p_vap = vapor partial pressure [Pa], p = total air pressure [Pa], HumRat = humidity ratio [kg/kg dry air].
    return kWaterVaporRatio * vapor_pressure_pa / std::max(kSmall, pressure_pa - vapor_pressure_pa);
}

double humidity_ratio_from_rh(double temp_c, double rh, double pressure_pa) {
    // Origin: Bring, Sahlin & Vuolle (1999), climate model.
    // Formula (4) - HumAir = HumRat(P_air, P_vap), using Formula (3) and Formula (7).
    // Variables: P_air = atmospheric pressure [Pa], P_vap = vapor pressure [Pa], HumAir = humidity ratio [kg/kg dry air].
    return humidity_ratio_from_vapor_pressure(pressure_pa, vapor_partial_pressure_pa(temp_c, rh));
}

double rh_from_humidity_ratio(double temp_c, double humidity_ratio, double pressure_pa) {
    const double p_vap = pressure_pa * humidity_ratio / (kWaterVaporRatio + humidity_ratio);
    const double p_sat = saturation_pressure_pa(temp_c);
    return std::clamp(p_vap / std::max(kSmall, p_sat), 0.0, 1.0);
}

double moist_air_density_kg_m3(double temp_c, double pressure_pa) {
    const double temp_k = std::max(1.0, temp_c + 273.15);
    return pressure_pa / (kSpecificGasDryAir * temp_k);
}

double local_wind_speed_m_s(const ClimateInputs& climate) {
    // Origin: Bring, Sahlin & Vuolle (1999), climate model.
    // Formula (8) - Local wind speed at building height:
    // WindVel = a0_coeff * WindVel_ref * (Height / Height_ref)^a_exp
    // Variables: WindVel = local wind speed [m/s], WindVel_ref = reference wind speed [m/s],
    // Height = building height [m], Height_ref = meteorological reference height [m],
    // a0_coeff = terrain coefficient [-], a_exp = terrain exponent [-].
    const double ratio = std::max(kSmall, climate.building_height_m) / std::max(kSmall, climate.reference_height_m);
    return climate.a0_coeff * climate.reference_wind_speed_m_s * std::pow(ratio, climate.a_exp);
}

double equation_of_time_minutes(int day_of_year) {
    // Origin: Bring, Sahlin & Vuolle (1999), climate model.
    // Formulae (10) and (11) - Equation of time.
    // Variables: n = day of year [-], E_t = equation of time [min].
    const double b = 2.0 * kPi * (static_cast<double>(day_of_year) - 81.0) / 364.0;
    return 9.87 * std::sin(2.0 * b) - 7.53 * std::cos(b) - 1.5 * std::sin(b);
}

double solar_time_hours(const ClimateInputs& climate) {
    // Origin: Bring, Sahlin & Vuolle (1999), climate model.
    // Formula (9) - Solar time.
    // SolarTime = ClockTime + (4*(Longitude - StdMeridian) + EqTime)/60
    // Variables: ClockTime = local clock time [h], Longitude = site longitude [deg], StdMeridian = time-zone meridian [deg], EqTime = equation of time [min].
    return climate.clock_time_hours + (4.0 * (climate.longitude_deg - climate.standard_meridian_deg) + equation_of_time_minutes(climate.day_of_year)) / 60.0;
}

double solar_declination_rad(int day_of_year) {
    // Origin: Bring, Sahlin & Vuolle (1999), climate model.
    // Formula (13) - Solar declination.
    // Variables: n = day of year [-], delta = solar declination [rad].
    return deg_to_rad(23.45) * std::sin(2.0 * kPi * (284.0 + static_cast<double>(day_of_year)) / 365.0);
}

double solar_elevation_rad(const ClimateInputs& climate) {
    // Origin: Bring, Sahlin & Vuolle (1999), climate model.
    // Formula (12) - Solar elevation.
    // Variables: phi = latitude [rad], delta = declination [rad], omega = hour angle [rad], beta = solar elevation [rad].
    const double phi = deg_to_rad(climate.latitude_deg);
    const double delta = solar_declination_rad(climate.day_of_year);
    const double omega = deg_to_rad(15.0 * (solar_time_hours(climate) - 12.0));
    const double sin_beta = std::sin(phi) * std::sin(delta) + std::cos(phi) * std::cos(delta) * std::cos(omega);
    return std::asin(std::clamp(sin_beta, -1.0, 1.0));
}

double solar_azimuth_rad(const ClimateInputs& climate) {
    // Origin: Bring, Sahlin & Vuolle (1999), climate model.
    // Formula (14) - Solar azimuth.
    // Variables: alpha = solar azimuth [rad], phi = latitude [rad], delta = declination [rad], omega = hour angle [rad], beta = elevation [rad].
    const double phi = deg_to_rad(climate.latitude_deg);
    const double delta = solar_declination_rad(climate.day_of_year);
    const double beta = solar_elevation_rad(climate);
    const double omega = deg_to_rad(15.0 * (solar_time_hours(climate) - 12.0));
    const double cos_alpha = (std::sin(delta) * std::cos(phi) - std::cos(delta) * std::sin(phi) * std::cos(omega)) / std::max(kSmall, std::cos(beta));
    const double alpha = std::acos(std::clamp(cos_alpha, -1.0, 1.0));
    return (omega >= 0.0) ? alpha : -alpha;
}

double incident_angle_rad(const ClimateInputs& climate) {
    // Origin: Bring, Sahlin & Vuolle (1999), façade model.
    // Formula (23) - Incident angle on a sloping surface.
    // Variables: theta = angle of incidence [rad], beta = solar elevation [rad], alpha = solar azimuth [rad],
    // gamma = surface azimuth [rad], tilt = surface tilt [rad].
    const double beta = solar_elevation_rad(climate);
    const double alpha = solar_azimuth_rad(climate);
    const double gamma = deg_to_rad(climate.wall_azimuth_deg);
    const double tilt = deg_to_rad(climate.wall_tilt_deg);
    const double cos_theta = std::sin(beta) * std::cos(tilt)
                           + std::cos(beta) * std::sin(tilt) * std::cos(alpha - gamma);
    return std::acos(std::clamp(cos_theta, -1.0, 1.0));
}

double direct_radiation_on_surface_w_m2(const ClimateInputs& climate) {
    // Origin: Bring, Sahlin & Vuolle (1999), façade model.
    // Formula (24) - Direct radiation on sloping surface: I_DirWall = I_DirNorm * cos(theta)
    // Formula (25) - Direct radiation on horizontal surface uses solar elevation.
    // Variables: I_DirNorm = direct normal radiation [W/m^2], theta = incidence angle [rad].
    return std::max(0.0, climate.direct_radiation_w_m2 * std::cos(incident_angle_rad(climate)));
}

double diffuse_radiation_on_surface_w_m2(const ClimateInputs& climate) {
    // Origin: Bring, Sahlin & Vuolle (1999), façade model.
    // Formula (26) - Isotropic diffuse radiation on a tilted surface.
    // Variables: I_diff = diffuse horizontal radiation [W/m^2], tilt = surface tilt [rad].
    const double tilt = deg_to_rad(climate.wall_tilt_deg);
    return climate.diffuse_radiation_w_m2 * 0.5 * (1.0 + std::cos(tilt));
}

double ground_reflected_radiation_w_m2(const ClimateInputs& climate) {
    // Origin: Bring, Sahlin & Vuolle (1999), façade model.
    // Formula (27) - Ground-reflected radiation on a tilted surface.
    // Variables: rho_g = ground reflectance [-], I_tot_h = total horizontal radiation [W/m^2], tilt = surface tilt [rad].
    const double tilt = deg_to_rad(climate.wall_tilt_deg);
    const double total_horizontal = std::max(0.0, climate.direct_radiation_w_m2 * std::sin(std::max(0.0, solar_elevation_rad(climate))) + climate.diffuse_radiation_w_m2);
    return climate.ground_reflectance * total_horizontal * 0.5 * (1.0 - std::cos(tilt));
}

double external_convective_coefficient_w_m2k(double local_wind_speed_m_s_value) {
    // Origin: Bring, Sahlin & Vuolle (1999), façade model.
    // Formula (35) - External convective heat transfer coefficient.
    // Variables: h_c = external convection coefficient [W/(m^2.K)], V = local wind speed [m/s].
    return 5.8 + 4.1 * std::max(0.0, local_wind_speed_m_s_value);
}

double wind_pressure_pa(double local_wind_speed_m_s_value, double air_density_kg_m3, double pressure_coefficient) {
    // Origin: Bring, Sahlin & Vuolle (1999), façade model.
    // Formula (36) - Façade pressure term:
    // DeltaP = 0.5 * rho_air * V^2 * PressCoeff
    // Variables: rho_air = air density [kg/m^3], V = local wind speed [m/s], PressCoeff = façade pressure coefficient [-].
    return 0.5 * air_density_kg_m3 * local_wind_speed_m_s_value * local_wind_speed_m_s_value * pressure_coefficient;
}

double exterior_convective_heat_w(double h_w_m2k, double area_m2, double outdoor_temp_c, double wall_temp_c) {
    // Origin: Bring, Sahlin & Vuolle (1999), wall surface model.
    // Formula (37) - Exterior convective heat transfer:
    // Q_conv = h_c * A * (T_air - T_wall)
    // Variables: h_c = convection coefficient [W/(m^2.K)], A = area [m^2], T_air = outdoor temperature [°C], T_wall = wall surface temperature [°C].
    return h_w_m2k * area_m2 * (outdoor_temp_c - wall_temp_c);
}

double absorbed_solar_radiation_w(double absorptivity, double area_m2, double direct_w_m2, double diffuse_w_m2) {
    // Origin: Bring, Sahlin & Vuolle (1999), wall surface model.
    // Formula (38) - Absorbed solar radiation:
    // Q_abs = alpha * A * (I_dir + I_diff)
    // Variables: alpha = solar absorptivity [-], A = area [m^2], I_dir/I_diff = direct and diffuse radiation [W/m^2].
    return absorptivity * area_m2 * (direct_w_m2 + diffuse_w_m2);
}

double longwave_exchange_w(double emissivity, double area_m2, double surface_temp_c, double sky_temp_c_value) {
    // Origin: Bring, Sahlin & Vuolle (1999), wall surface model.
    // Formula (39) - Long-wave exchange with sky/ground.
    // Variables: epsilon = emissivity [-], A = area [m^2], T_surf = surface temperature [°C], T_sky = sky temperature [°C].
    const double ts = surface_temp_c + 273.15;
    const double tsky = sky_temp_c_value + 273.15;
    return emissivity * kSigma * area_m2 * (std::pow(tsky, 4) - std::pow(ts, 4));
}

double exterior_wall_heat_balance_w(double convective_w, double absorbed_solar_w, double longwave_w) {
    // Origin: Bring, Sahlin & Vuolle (1999), wall surface model.
    // Formula (40) - Total exterior wall heat balance:
    // Q_wall = Q_conv + Q_abs + Q_lw
    // Variables: Q_conv = convective heat [W], Q_abs = absorbed solar heat [W], Q_lw = net long-wave heat [W].
    return convective_w + absorbed_solar_w + longwave_w;
}

double window_shading_multiplier_total_heat(double shading_coefficient) {
    // Origin: Bring, Sahlin & Vuolle (1999), window model.
    // Formula (41) - Shading multiplier for total heat load.
    // Variables: SC = shading coefficient [-].
    return std::clamp(shading_coefficient, 0.0, 1.0);
}

double window_shading_multiplier_direct_sw(double shading_coefficient) {
    // Origin: Bring, Sahlin & Vuolle (1999), window model.
    // Formula (42) - Shading multiplier for direct short-wave transmission.
    // Variables: SSC = direct short-wave shading coefficient [-].
    return std::clamp(shading_coefficient, 0.0, 1.0);
}

double reference_transmitted_solar_gain_w(double window_area_m2, double direct_w_m2, double diffuse_w_m2) {
    // Origin: Bring, Sahlin & Vuolle (1999), window model.
    // Formula (43) - Reference transmitted solar gain.
    // Variables: A_glz = glazing area [m^2], I_dir/I_diff = incident solar radiation [W/m^2].
    return window_area_m2 * (direct_w_m2 + diffuse_w_m2);
}

double transmitted_shortwave_radiation_w(double reference_gain_w, double transmitted_solar_coefficient) {
    // Origin: Bring, Sahlin & Vuolle (1999), window model.
    // Formula (44) - Transmitted short-wave radiation:
    // R_Thru = R_ThruRef * SSC
    // Variables: R_ThruRef = reference transmitted gain [W], SSC = transmitted solar coefficient [-].
    return reference_gain_w * std::clamp(transmitted_solar_coefficient, 0.0, 1.0);
}

double indirect_window_load_w(double reference_gain_w, double solar_coefficient, double transmitted_solar_coefficient) {
    // Origin: Bring, Sahlin & Vuolle (1999), window model.
    // Formula (45) - Indirect zone load from window absorption.
    // Variables: R_ThruRef = reference gain [W], SC = solar coefficient [-], SSC = transmitted solar coefficient [-].
    return reference_gain_w * std::max(0.0, solar_coefficient - transmitted_solar_coefficient);
}

double absorbed_in_window_w(double reference_gain_w, double solar_coefficient) {
    // Origin: Bring, Sahlin & Vuolle (1999), window model.
    // Formula (46) - Radiation absorbed in glazing.
    // Variables: R_abs = absorbed radiation in window [W], R_ThruRef = reference gain [W], SC = solar coefficient [-].
    return reference_gain_w * std::clamp(1.0 - solar_coefficient, 0.0, 1.0);
}

double shaded_u_value_multiplier(double shading_coefficient) {
    // Origin: Bring, Sahlin & Vuolle (1999), window model.
    // Formula (47) - U-value multiplier under shading.
    // Variables: f_U = U-value multiplier [-], SC = shading coefficient [-].
    return 1.0 + 0.1 * (1.0 - std::clamp(shading_coefficient, 0.0, 1.0));
}



double local_unit_heat_output_w(double control_signal, double max_thermal_power_w) {
    // Origin: Bring, Sahlin & Vuolle (1999), local unit model.
    // Formula (83) - Local convective heating/cooling unit power:
    // Q_LocUnit = CtrLocUnit * Q_LocMax
    // Variables: CtrLocUnit = control signal [-], Q_LocMax = maximum unit thermal power [W].
    return std::clamp(control_signal, 0.0, 1.0) * std::max(0.0, max_thermal_power_w);
}

double local_unit_electric_power_w(double thermal_power_w, double coefficient_of_performance) {
    // Origin: Bring, Sahlin & Vuolle (1999), local unit model.
    // Formula (84) - Electric power via coefficient of performance:
    // P_el = Q_LocUnit / COP
    // Variables: P_el = electric input power [W], Q_LocUnit = thermal unit output [W], COP = coefficient of performance [-].
    if (thermal_power_w <= 0.0) {
        return 0.0;
    }
    if (coefficient_of_performance > 0.0) {
        return thermal_power_w / coefficient_of_performance;
    }
    return thermal_power_w;
}

double actuator_electrical_power_w(double control_signal, double max_electrical_power_w) {
    // Simulator accounting extension.
    // Linear actuator-electricity estimate used for dampers and fans.
    // Variables: control signal [-], P_max = maximum actuator electric power [W].
    return std::clamp(control_signal, 0.0, 1.0) * std::max(0.0, max_electrical_power_w);
}

double actuator_resource_rate_per_hour(double control_signal, double max_resource_rate_per_hour) {
    // Simulator accounting extension.
    // Linear resource-use estimate used for actuator operating cost/fuel/electricity tracking.
    // Variables: control signal [-], R_max = maximum resource-use rate [unit/h].
    return std::clamp(control_signal, 0.0, 1.0) * std::max(0.0, max_resource_rate_per_hour);
}

double terminal_heat_transport_w(double mass_flow_kg_s, double supply_temp_c, double zone_temp_c) {
    // Origin: Bring, Sahlin & Vuolle (1999), terminal model.
    // Formula (96) - Heat transport:
    // Q = m * c_p * (T_supply - T_zone)
    // Variables: m = air mass flow [kg/s], c_p = specific heat [J/(kg.K)], T_supply/T_zone = temperatures [°C].
    return mass_flow_kg_s * kCpAir * (supply_temp_c - zone_temp_c);
}

double terminal_scalar_transport_kg_s(double mass_flow_kg_s, double upstream_scalar, double downstream_scalar) {
    // Origin: Bring, Sahlin & Vuolle (1999), terminal model.
    // Formulae (97) and (98) - Scalar transport for contaminants/humidity:
    // scalar_flow = m * scalar
    // Variables: m = air mass flow [kg/s], scalar = transported concentration or humidity ratio [kg/kg or equivalent].
    return mass_flow_kg_s * (upstream_scalar - downstream_scalar);
}

double leak_pressure_difference_pa(double p_in_pa, double rho_in, double z_in_m, double p_out_pa, double rho_out, double z_out_m, double rho_path, double dz_m) {
    // Origin: Bring, Sahlin & Vuolle (1999), leak model.
    // Formula (102) - Pressure difference across leak path:
    // dp = P_in - rho_in*g*z_in - P_out + rho_out*g*z_out - rho_path*g*dz
    // Variables: P_in/P_out = static pressures [Pa], rho_in/rho_out/rho_path = air densities [kg/m^3], z_in/z_out/dz = elevations [m].
    return p_in_pa - rho_in * kGravity * z_in_m - p_out_pa + rho_out * kGravity * z_out_m - rho_path * kGravity * dz_m;
}

double leakage_flow_m3_s(double leakage_area_m2, double delta_p_pa, double rho_air_kg_m3, double discharge_coefficient) {
    // Origin: Bring, Sahlin & Vuolle (1999), leak model.
    // Formulae (99) to (101) - Positive/negative/linearized leak flow around pressure difference.
    // This compact implementation uses a symmetric discharge relation as a lightweight closure.
    // Variables: A_leak = leakage area [m^2], dp = pressure difference [Pa], rho = air density [kg/m^3], C_d = discharge coefficient [-].
    if (leakage_area_m2 <= 0.0 || rho_air_kg_m3 <= 0.0) {
        return 0.0;
    }
    const double magnitude = discharge_coefficient * leakage_area_m2 * std::sqrt(2.0 * std::abs(delta_p_pa) / std::max(kSmall, rho_air_kg_m3));
    return (delta_p_pa >= 0.0) ? magnitude : -magnitude;
}

double large_vertical_opening_flow_m3_s(double discharge_coefficient, double opening_area_m2, double bottom_pressure_pa, double top_pressure_pa, double rho_air_kg_m3) {
    // Origin: Bring, Sahlin & Vuolle (1999), large vertical opening model.
    // Formulae (107) to (123) - Bottom/top pressure differences, neutral level, one-way/two-way net mass flow.
    // This compact implementation collapses the pressure profile to an average driving pressure.
    // Variables: A_open = opening area [m^2], dp_bottom/dp_top = opening-end pressures [Pa], rho = air density [kg/m^3], C_d = discharge coefficient [-].
    const double dp_avg = 0.5 * (bottom_pressure_pa + top_pressure_pa);
    if (opening_area_m2 <= 0.0 || rho_air_kg_m3 <= 0.0) {
        return 0.0;
    }
    const double magnitude = discharge_coefficient * opening_area_m2 * std::sqrt(2.0 * std::abs(dp_avg) / std::max(kSmall, rho_air_kg_m3));
    return (dp_avg >= 0.0) ? magnitude : -magnitude;
}

double zone_mean_radiant_exchange_w(double h_lw_w_m2k, double total_surface_area_m2, double mean_surface_temp_c, double mean_radiant_temp_c) {
    // Origin: Bring, Sahlin & Vuolle (1999), simplified zone model.
    // Formula (92) - Mean radiant exchange:
    // Q_Rad2Zone = sum(h_Lw * A_i * (T_surf,i - T_mrt))
    // Variables: h_Lw = long-wave coefficient [W/(m^2.K)], A_i = surface areas [m^2], T_surf/T_mrt = temperatures [°C].
    return h_lw_w_m2k * total_surface_area_m2 * (mean_surface_temp_c - mean_radiant_temp_c);
}

StepDiagnostics evaluate_step_diagnostics(
    const RoomGeometry& room,
    const ClimateInputs& climate,
    double indoor_temp_c,
    double indoor_humidity_ratio,
    double mechanical_flow_m3_s,
    double heater_gain_w,
    double damper_discharge_coefficient,
    double running_airflow_average_m3_s) {
    const DerivedGeometry geometry = derive_geometry(room);
    const double rho_in = moist_air_density_kg_m3(indoor_temp_c, climate.atmospheric_pressure_pa);
    const double rho_out = moist_air_density_kg_m3(climate.outside_temp_c, climate.atmospheric_pressure_pa);
    const double rho_ref = 0.5 * (rho_in + rho_out);

    StepDiagnostics d{};
    d.sky_temp_c = sky_temperature_c(climate.outside_temp_c);
    d.outdoor_humidity_ratio_kg_per_kg = humidity_ratio_from_rh(
        climate.outside_temp_c,
        climate.outside_relative_humidity,
        climate.atmospheric_pressure_pa);
    d.indoor_humidity_ratio_kg_per_kg = indoor_humidity_ratio;
    d.local_wind_speed_m_s = local_wind_speed_m_s(climate);
    d.wind_pressure_pa = wind_pressure_pa(d.local_wind_speed_m_s, rho_ref, climate.facade_pressure_coefficient);

    d.solar.equation_of_time_minutes = equation_of_time_minutes(climate.day_of_year);
    d.solar.solar_time_hours = solar_time_hours(climate);
    d.solar.declination_rad = solar_declination_rad(climate.day_of_year);
    d.solar.elevation_rad = solar_elevation_rad(climate);
    d.solar.azimuth_rad = solar_azimuth_rad(climate);
    d.solar.incident_angle_rad = incident_angle_rad(climate);
    d.solar.direct_on_surface_w_m2 = direct_radiation_on_surface_w_m2(climate);
    d.solar.diffuse_on_surface_w_m2 = diffuse_radiation_on_surface_w_m2(climate);
    d.solar.reflected_on_surface_w_m2 = ground_reflected_radiation_w_m2(climate);

    const double h_ext = external_convective_coefficient_w_m2k(d.local_wind_speed_m_s);
    const double wall_temp_c = 0.5 * (indoor_temp_c + climate.outside_temp_c);
    const double q_conv = exterior_convective_heat_w(h_ext, geometry.wall_area_m2 + geometry.roof_area_m2, climate.outside_temp_c, wall_temp_c);
    const double q_abs = absorbed_solar_radiation_w(0.6, geometry.wall_area_m2 + geometry.roof_area_m2, d.solar.direct_on_surface_w_m2, d.solar.diffuse_on_surface_w_m2 + d.solar.reflected_on_surface_w_m2);
    const double q_lw = longwave_exchange_w(climate.sky_emissivity, geometry.wall_area_m2 + geometry.roof_area_m2, wall_temp_c, d.sky_temp_c);
    (void)exterior_wall_heat_balance_w(q_conv, q_abs, q_lw);

    const double sc = window_shading_multiplier_total_heat(climate.shading_coefficient);
    const double ssc = window_shading_multiplier_direct_sw(climate.shading_coefficient);
    const double r_ref = reference_transmitted_solar_gain_w(geometry.total_window_area_m2, d.solar.direct_on_surface_w_m2, d.solar.diffuse_on_surface_w_m2);
    const double r_thru = transmitted_shortwave_radiation_w(r_ref, climate.window_transmitted_solar_coefficient * ssc);
    const double r_indir = indirect_window_load_w(r_ref, climate.glazing_solar_coefficient * sc, climate.window_transmitted_solar_coefficient * ssc);
    (void)absorbed_in_window_w(r_ref, climate.glazing_solar_coefficient * sc);
    (void)shaded_u_value_multiplier(climate.shading_coefficient);

    const double p_in = climate.atmospheric_pressure_pa + d.wind_pressure_pa;
    const double p_out = climate.atmospheric_pressure_pa;
    d.leak_delta_p_pa = leak_pressure_difference_pa(p_in, rho_in, room.height_m * 0.5, p_out, rho_out, room.height_m * 0.5, rho_ref, room.height_m * 0.1);
    d.leakage_flow_m3_s = leakage_flow_m3_s(room.leakage_area_m2, d.leak_delta_p_pa, rho_ref, damper_discharge_coefficient);

    const double dp_bottom = d.wind_pressure_pa;
    const double dp_top = d.wind_pressure_pa + (rho_out - rho_in) * kGravity * room.opening_height_m;
    d.large_opening_flow_m3_s = large_vertical_opening_flow_m3_s(damper_discharge_coefficient, geometry.total_opening_area_m2, dp_bottom, dp_top, rho_ref);

    d.mechanical_ventilation_flow_m3_s = std::max(0.0, mechanical_flow_m3_s);
    d.total_ventilation_flow_m3_s = std::max(0.0, d.mechanical_ventilation_flow_m3_s + std::max(0.0, d.large_opening_flow_m3_s) + std::max(0.0, d.leakage_flow_m3_s));
    d.average_airflow_m3_s = running_airflow_average_m3_s;

    d.solar_gain_w = r_thru + r_indir;
    // Lightweight single-zone closure: keep one envelope loss path from the room UA only.
    // The exterior surface balance functions above remain implemented and documented, but are not
    // added again here to avoid double counting heat loss relative to the lumped envelope term.
    d.envelope_loss_w = geometry.ua_w_k * (climate.outside_temp_c - indoor_temp_c);

    const double mass_flow_kg_s = rho_ref * d.total_ventilation_flow_m3_s;
    d.ventilation_loss_w = terminal_heat_transport_w(mass_flow_kg_s, climate.outside_temp_c, indoor_temp_c);
    d.heater_gain_w = heater_gain_w;
    return d;
}

} // namespace cattle_climate
