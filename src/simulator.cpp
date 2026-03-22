#include "cattle_climate/simulator.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace cattle_climate {
namespace {
constexpr double kCpAir = 1005.0;
constexpr double kSecondsPerHour = 3600.0;
constexpr double kMinIndoorTempC = -60.0;
constexpr double kMaxIndoorTempC = 80.0;
constexpr double kMaxIndoorHumidityRatio = 0.08;

double clamp_indoor_temperature_c(double value) {
    return std::clamp(value, kMinIndoorTempC, kMaxIndoorTempC);
}

double clamp_indoor_humidity_ratio(double value) {
    return std::clamp(value, 0.0, kMaxIndoorHumidityRatio);
}
}

namespace {

double clamp01(double x) {
    return std::max(0.0, std::min(1.0, x));
}

double compute_min_value(const std::vector<double>& values) {
    return values.empty() ? 0.0 : *std::min_element(values.begin(), values.end());
}

double compute_max_value(const std::vector<double>& values) {
    return values.empty() ? 1.0 : *std::max_element(values.begin(), values.end());
}

void expand_degenerate_range(double& min_v, double& max_v) {
    if (max_v - min_v < 1e-9) {
        min_v -= 0.5;
        max_v += 0.5;
    }
}

std::string svg_polyline(const std::vector<double>& xs, const std::vector<double>& ys, double min_y, double max_y,
                         double left, double top, double width, double height, const std::string& color) {
    std::ostringstream out;
    out << "<polyline fill=\"none\" stroke=\"" << color << "\" stroke-width=\"2\" points=\"";
    for (std::size_t i = 0; i < xs.size() && i < ys.size(); ++i) {
        const double x = left + clamp01(xs[i]) * width;
        const double y_norm = (ys[i] - min_y) / (max_y - min_y);
        const double y = top + height - clamp01(y_norm) * height;
        out << x << ',' << y;
        if (i + 1 < xs.size() && i + 1 < ys.size()) {
            out << ' ';
        }
    }
    out << "\"/>";
    return out.str();
}

void write_svg_panel(std::ofstream& svg, const std::string& title, const std::vector<double>& xs, const std::vector<double>& ys,
                     double min_y, double max_y, double left, double top, double width, double height,
                     const std::string& color, const std::string& unit) {
    svg << "<rect x=\"" << left << "\" y=\"" << top << "\" width=\"" << width << "\" height=\"" << height
        << "\" fill=\"white\" stroke=\"#cccccc\"/>\n";
    svg << "<text x=\"" << (left + 8.0) << "\" y=\"" << (top + 20.0)
        << "\" font-size=\"14\" font-family=\"Arial\">" << title << "</text>\n";
    svg << "<text x=\"" << (left + 8.0) << "\" y=\"" << (top + 38.0)
        << "\" font-size=\"12\" font-family=\"Arial\">min=" << std::fixed << std::setprecision(2) << min_y
        << " " << unit << ", max=" << max_y << " " << unit << "</text>\n";
    const double plot_left = left + 55.0;
    const double plot_top = top + 45.0;
    const double plot_width = width - 70.0;
    const double plot_height = height - 70.0;
    svg << "<line x1=\"" << plot_left << "\" y1=\"" << (plot_top + plot_height)
        << "\" x2=\"" << (plot_left + plot_width) << "\" y2=\"" << (plot_top + plot_height)
        << "\" stroke=\"#444\"/>\n";
    svg << "<line x1=\"" << plot_left << "\" y1=\"" << plot_top
        << "\" x2=\"" << plot_left << "\" y2=\"" << (plot_top + plot_height)
        << "\" stroke=\"#444\"/>\n";
    svg << svg_polyline(xs, ys, min_y, max_y, plot_left, plot_top, plot_width, plot_height, color) << "\n";
    svg << "<text x=\"" << (plot_left - 40.0) << "\" y=\"" << (plot_top + 12.0)
        << "\" font-size=\"11\" font-family=\"Arial\">" << max_y << "</text>\n";
    svg << "<text x=\"" << (plot_left - 40.0) << "\" y=\"" << (plot_top + plot_height)
        << "\" font-size=\"11\" font-family=\"Arial\">" << min_y << "</text>\n";
    svg << "<text x=\"" << (plot_left + plot_width - 60.0) << "\" y=\"" << (plot_top + plot_height + 20.0)
        << "\" font-size=\"11\" font-family=\"Arial\">time [min]</text>\n";
}

} // namespace

SingleZoneSimulator::SingleZoneSimulator(SimulationSettings settings) : settings_(std::move(settings)) {
    settings_.climate.building_height_m = settings_.room.height_m;
}

SimulationResult SingleZoneSimulator::estimate(const ActuatorCommand& command) const {
    SimulationResult result{};
    result.states.reserve(static_cast<std::size_t>(settings_.n_steps) + 1U);

    const DerivedGeometry geometry = derive_geometry(settings_.room);
    double indoor_temp_c = clamp_indoor_temperature_c(settings_.initial_indoor_temp_c);
    double indoor_humidity_ratio = clamp_indoor_humidity_ratio(humidity_ratio_from_rh(
        settings_.initial_indoor_temp_c,
        settings_.initial_indoor_relative_humidity,
        settings_.climate.atmospheric_pressure_pa));

    double airflow_sum = 0.0;
    double damper_energy_kwh = 0.0;
    double fan_energy_kwh = 0.0;
    double heater_energy_kwh = 0.0;
    double damper_resource_units = 0.0;
    double fan_resource_units = 0.0;
    double heater_resource_units = 0.0;

    for (int step = 0; step <= settings_.n_steps; ++step) {
        const double minute = static_cast<double>(step) * settings_.timestep_seconds / 60.0;
        const bool fan_active = command.fans_on && minute < command.fans_active_minutes;
        const bool damper_active = command.dampers_on && minute < command.dampers_active_minutes;
        const bool heater_active = command.heaters_on && minute < command.heaters_active_minutes;

        const double fan_fraction = fan_active ? std::clamp(command.fan_activity_percent / 100.0, 0.0, 1.0) : 0.0;
        const double damper_fraction = damper_active ? std::clamp(command.damper_activity_percent / 100.0, 0.0, 1.0) : 0.0;
        const double heater_fraction = heater_active ? std::clamp(command.heater_activity_percent / 100.0, 0.0, 1.0) : 0.0;

        const double mechanical_flow_m3_s = settings_.actuators.fan_max_flow_m3_s * fan_fraction;
        const double heater_gain_w = local_unit_heat_output_w(heater_fraction, settings_.actuators.heater_max_power_w);

        RoomGeometry room_step = settings_.room;
        room_step.opening_height_m = settings_.room.opening_height_m * damper_fraction;
        room_step.opening_width_m = settings_.room.opening_width_m;

        StepDiagnostics d = evaluate_step_diagnostics(
            room_step,
            settings_.climate,
            indoor_temp_c,
            indoor_humidity_ratio,
            mechanical_flow_m3_s,
            heater_gain_w,
            settings_.actuators.damper_discharge_coefficient,
            (step == 0) ? 0.0 : (airflow_sum / static_cast<double>(step)));

        d.damper_electrical_power_w = actuator_electrical_power_w(damper_fraction, settings_.actuators.damper_max_electrical_power_w);
        d.fan_electrical_power_w = actuator_electrical_power_w(fan_fraction, settings_.actuators.fan_max_electrical_power_w);
        d.heater_electrical_power_w = std::min(
            actuator_electrical_power_w(heater_fraction, settings_.actuators.heater_max_electrical_power_w),
            local_unit_electric_power_w(d.heater_gain_w, settings_.actuators.heater_cop));
        d.damper_resource_rate_per_hour = actuator_resource_rate_per_hour(damper_fraction, settings_.actuators.damper_max_resource_rate_per_hour);
        d.fan_resource_rate_per_hour = actuator_resource_rate_per_hour(fan_fraction, settings_.actuators.fan_max_resource_rate_per_hour);
        d.heater_resource_rate_per_hour = actuator_resource_rate_per_hour(heater_fraction, settings_.actuators.heater_max_resource_rate_per_hour);

        airflow_sum += d.total_ventilation_flow_m3_s;
        d.average_airflow_m3_s = airflow_sum / static_cast<double>(step + 1);

        indoor_temp_c = clamp_indoor_temperature_c(indoor_temp_c);
        indoor_humidity_ratio = clamp_indoor_humidity_ratio(indoor_humidity_ratio);

        const double indoor_rh = rh_from_humidity_ratio(
            indoor_temp_c,
            indoor_humidity_ratio,
            settings_.climate.atmospheric_pressure_pa);

        result.states.push_back(SimulationState{
            minute,
            settings_.climate.outside_temp_c,
            settings_.climate.outside_relative_humidity,
            settings_.climate.reference_wind_speed_m_s,
            d.local_wind_speed_m_s,
            settings_.climate.outside_wind_direction_deg,
            settings_.climate.outside_co2_ppm,
            settings_.climate.direct_radiation_w_m2,
            settings_.climate.diffuse_radiation_w_m2,
            indoor_temp_c,
            indoor_rh,
            indoor_humidity_ratio,
            d,
        });

        if (step == settings_.n_steps) {
            break;
        }

        const double dt_hours = settings_.timestep_seconds / kSecondsPerHour;
        damper_energy_kwh += d.damper_electrical_power_w * dt_hours / 1000.0;
        fan_energy_kwh += d.fan_electrical_power_w * dt_hours / 1000.0;
        heater_energy_kwh += d.heater_electrical_power_w * dt_hours / 1000.0;
        damper_resource_units += d.damper_resource_rate_per_hour * dt_hours;
        fan_resource_units += d.fan_resource_rate_per_hour * dt_hours;
        heater_resource_units += d.heater_resource_rate_per_hour * dt_hours;

        const double rho_in = moist_air_density_kg_m3(indoor_temp_c, settings_.climate.atmospheric_pressure_pa);
        const double rho_out = moist_air_density_kg_m3(settings_.climate.outside_temp_c, settings_.climate.atmospheric_pressure_pa);
        const double rho_ref = 0.5 * (rho_in + rho_out);
        const double zone_air_mass_kg = std::max(1.0, rho_in * geometry.volume_m3);

        const double q_net_w = d.envelope_loss_w
                             + d.ventilation_loss_w
                             + d.solar_gain_w
                             + d.heater_gain_w
                             + settings_.internal_loads.base_sensible_gains_w;
        indoor_temp_c = clamp_indoor_temperature_c(indoor_temp_c + (q_net_w / (zone_air_mass_kg * kCpAir)) * settings_.timestep_seconds);

        const double mass_flow_kg_s = rho_ref * d.total_ventilation_flow_m3_s;
        const double dry_air_mass_kg = std::max(1.0, zone_air_mass_kg / (1.0 + indoor_humidity_ratio));
        const double humidity_source_kg_s = settings_.internal_loads.base_moisture_generation_kg_s;
        indoor_humidity_ratio = clamp_indoor_humidity_ratio(
            indoor_humidity_ratio +
                ((terminal_scalar_transport_kg_s(mass_flow_kg_s, d.outdoor_humidity_ratio_kg_per_kg, indoor_humidity_ratio) + humidity_source_kg_s)
                 / dry_air_mass_kg) * settings_.timestep_seconds);
    }

    const SimulationState& last = result.states.back();
    result.summary.final_outside_temp_c = last.outside_temp_c;
    result.summary.final_outside_relative_humidity = last.outside_relative_humidity;
    result.summary.final_outside_reference_wind_speed_m_s = last.outside_reference_wind_speed_m_s;
    result.summary.final_outside_local_wind_speed_m_s = last.outside_local_wind_speed_m_s;
    result.summary.final_outside_wind_direction_deg = last.outside_wind_direction_deg;
    result.summary.final_outside_co2_ppm = last.outside_co2_ppm;
    result.summary.final_outside_direct_radiation_w_m2 = last.outside_direct_radiation_w_m2;
    result.summary.final_outside_diffuse_radiation_w_m2 = last.outside_diffuse_radiation_w_m2;
    result.summary.final_inside_temp_c = last.indoor_temp_c;
    result.summary.final_relative_humidity = last.indoor_relative_humidity;
    result.summary.final_airflow_m3_s = last.diagnostics.total_ventilation_flow_m3_s;
    result.summary.average_airflow_m3_s = last.diagnostics.average_airflow_m3_s;
    result.summary.cumulative_damper_electrical_energy_kwh = damper_energy_kwh;
    result.summary.cumulative_fan_electrical_energy_kwh = fan_energy_kwh;
    result.summary.cumulative_heater_electrical_energy_kwh = heater_energy_kwh;
    result.summary.cumulative_damper_resource_units = damper_resource_units;
    result.summary.cumulative_fan_resource_units = fan_resource_units;
    result.summary.cumulative_heater_resource_units = heater_resource_units;
    return result;
}

std::string write_csv_report(const SimulationResult& result, const std::string& path) {
    std::ofstream csv(path);
    csv << "minute,outside_temp_c,outside_rh,outside_reference_wind_speed_m_s,outside_local_wind_speed_m_s,"
           "outside_wind_direction_deg,outside_co2_ppm,outside_direct_radiation_w_m2,outside_diffuse_radiation_w_m2,"
           "inside_temp_c,inside_rh,total_airflow_m3_s,average_airflow_m3_s,"
           "heater_gain_w,envelope_loss_w,ventilation_loss_w,damper_power_w,fan_power_w,heater_power_w,"
           "damper_resource_rate_per_hour,fan_resource_rate_per_hour,heater_resource_rate_per_hour\n";
    for (const auto& state : result.states) {
        csv << state.minute << ','
            << state.outside_temp_c << ','
            << state.outside_relative_humidity << ','
            << state.outside_reference_wind_speed_m_s << ','
            << state.outside_local_wind_speed_m_s << ','
            << state.outside_wind_direction_deg << ','
            << state.outside_co2_ppm << ','
            << state.outside_direct_radiation_w_m2 << ','
            << state.outside_diffuse_radiation_w_m2 << ','
            << state.indoor_temp_c << ','
            << state.indoor_relative_humidity << ','
            << state.diagnostics.total_ventilation_flow_m3_s << ','
            << state.diagnostics.average_airflow_m3_s << ','
            << state.diagnostics.heater_gain_w << ','
            << state.diagnostics.envelope_loss_w << ','
            << state.diagnostics.ventilation_loss_w << ','
            << state.diagnostics.damper_electrical_power_w << ','
            << state.diagnostics.fan_electrical_power_w << ','
            << state.diagnostics.heater_electrical_power_w << ','
            << state.diagnostics.damper_resource_rate_per_hour << ','
            << state.diagnostics.fan_resource_rate_per_hour << ','
            << state.diagnostics.heater_resource_rate_per_hour << '\n';
    }
    return path;
}


std::string write_svg_graph_report(const SimulationResult& result, const std::string& path, double max_minutes) {
    std::ofstream svg(path);
    if (!svg) {
        throw std::runtime_error("Unable to open graph file for writing: " + path);
    }

    std::vector<double> minutes;
    std::vector<double> inside_temp;
    std::vector<double> outside_temp;
    std::vector<double> inside_rh_pct;
    std::vector<double> airflow;

    const double horizon = max_minutes > 0.0 ? max_minutes : (result.states.empty() ? 0.0 : result.states.back().minute);
    for (const auto& state : result.states) {
        if (state.minute > horizon) {
            break;
        }
        minutes.push_back(horizon > 0.0 ? (state.minute / horizon) : 0.0);
        inside_temp.push_back(state.indoor_temp_c);
        outside_temp.push_back(state.outside_temp_c);
        inside_rh_pct.push_back(state.indoor_relative_humidity * 100.0);
        airflow.push_back(state.diagnostics.total_ventilation_flow_m3_s);
    }

    double min_temp = std::min(compute_min_value(inside_temp), compute_min_value(outside_temp));
    double max_temp = std::max(compute_max_value(inside_temp), compute_max_value(outside_temp));
    expand_degenerate_range(min_temp, max_temp);
    double min_rh = compute_min_value(inside_rh_pct);
    double max_rh = compute_max_value(inside_rh_pct);
    expand_degenerate_range(min_rh, max_rh);
    double min_flow = compute_min_value(airflow);
    double max_flow = compute_max_value(airflow);
    expand_degenerate_range(min_flow, max_flow);

    const double width = 960.0;
    const double height = 820.0;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << ' ' << height << "\">\n";
    svg << "<rect width=\"100%\" height=\"100%\" fill=\"#f8f9fb\"/>\n";
    svg << "<text x=\"24\" y=\"30\" font-size=\"20\" font-family=\"Arial\">Key parameter graph</text>\n";
    svg << "<text x=\"24\" y=\"50\" font-size=\"12\" font-family=\"Arial\">Time horizon: 0 to " << horizon << " minutes</text>\n";
    write_svg_panel(svg, "Indoor temperature [C]", minutes, inside_temp, min_temp, max_temp, 20.0, 70.0, 920.0, 170.0, "#d62728", "C");
    write_svg_panel(svg, "Outdoor temperature [C]", minutes, outside_temp, min_temp, max_temp, 20.0, 260.0, 920.0, 170.0, "#1f77b4", "C");
    write_svg_panel(svg, "Indoor relative humidity [%]", minutes, inside_rh_pct, min_rh, max_rh, 20.0, 450.0, 920.0, 170.0, "#2ca02c", "%");
    write_svg_panel(svg, "Total airflow [m3/s]", minutes, airflow, min_flow, max_flow, 20.0, 640.0, 920.0, 160.0, "#9467bd", "m3/s");
    svg << "</svg>\n";
    return path;
}

} // namespace cattle_climate
