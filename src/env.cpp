#include "cattle_climate/env.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace cattle_climate {
namespace {

double clamp_percent(double value) {
    return std::max(0.0, std::min(100.0, value));
}

double range_error(double value, const OptimalRange& range) {
    if (value < range.min_value) {
        return range.min_value - value;
    }
    if (value > range.max_value) {
        return value - range.max_value;
    }
    return 0.0;
}

bool is_in_range(double value, const OptimalRange& range) {
    return value >= range.min_value && value <= range.max_value;
}

struct PlotSeries {
    std::string label;
    std::vector<double> values;
    double min_value{};
    double max_value{};
};

double safe_min_value(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    return *std::min_element(values.begin(), values.end());
}

double safe_max_value(const std::vector<double>& values) {
    if (values.empty()) {
        return 1.0;
    }
    return *std::max_element(values.begin(), values.end());
}

double expand_if_flat_min(double min_value, double max_value) {
    if (std::abs(max_value - min_value) < 1e-12) {
        return min_value - 0.5;
    }
    return min_value;
}

double expand_if_flat_max(double min_value, double max_value) {
    if (std::abs(max_value - min_value) < 1e-12) {
        return max_value + 0.5;
    }
    return max_value;
}

std::string svg_polyline(const std::vector<double>& xs, const std::vector<double>& ys,
                         double x0, double y0, double width, double height,
                         double x_min, double x_max, double y_min, double y_max) {
    std::ostringstream out;
    out << "<polyline fill=\"none\" stroke=\"black\" stroke-width=\"1.5\" points=\"";
    for (std::size_t i = 0; i < xs.size() && i < ys.size(); ++i) {
        const double x_norm = (x_max > x_min) ? (xs[i] - x_min) / (x_max - x_min) : 0.0;
        const double y_norm = (y_max > y_min) ? (ys[i] - y_min) / (y_max - y_min) : 0.0;
        const double px = x0 + x_norm * width;
        const double py = y0 + height - y_norm * height;
        out << px << ',' << py << ' ';
    }
    out << "\"/>";
    return out.str();
}

} // namespace

CattleVitalEnv::CattleVitalEnv(SimulationSettings settings, RLEnvironmentOptions options)
    : base_settings_(std::move(settings)), options_(std::move(options)) {
    if (options_.max_episode_steps == 0) {
        throw std::runtime_error("RLEnvironmentOptions.max_episode_steps must be greater than zero.");
    }
    static_cast<void>(reset());
}

RLObservation CattleVitalEnv::reset() {
    const double initial_humidity_ratio = humidity_ratio_from_rh(
        base_settings_.initial_indoor_temp_c,
        base_settings_.initial_indoor_relative_humidity,
        base_settings_.climate.atmospheric_pressure_pa);

    current_state_ = SimulationState{
        0.0,
        base_settings_.climate.outside_temp_c,
        base_settings_.climate.outside_relative_humidity,
        base_settings_.climate.reference_wind_speed_m_s,
        local_wind_speed_m_s(base_settings_.climate),
        base_settings_.climate.outside_wind_direction_deg,
        base_settings_.climate.outside_co2_ppm,
        base_settings_.climate.direct_radiation_w_m2,
        base_settings_.climate.diffuse_radiation_w_m2,
        base_settings_.initial_indoor_temp_c,
        base_settings_.initial_indoor_relative_humidity,
        initial_humidity_ratio,
        StepDiagnostics{}
    };

    previous_action_ = RLAction{};
    done_ = false;
    step_index_ = 0;
    cumulative_damper_energy_kwh_ = 0.0;
    cumulative_fan_energy_kwh_ = 0.0;
    cumulative_heater_energy_kwh_ = 0.0;
    cumulative_damper_resource_units_ = 0.0;
    cumulative_fan_resource_units_ = 0.0;
    cumulative_heater_resource_units_ = 0.0;
    history_.clear();
    return make_observation(current_state_);
}

RLStepResult CattleVitalEnv::step(const RLAction& action) {
    if (done_) {
        throw std::runtime_error("Cannot call step() on a finished episode. Call reset() first.");
    }

    SimulationSettings step_settings = base_settings_;
    step_settings.initial_indoor_temp_c = current_state_.indoor_temp_c;
    step_settings.initial_indoor_relative_humidity = std::clamp(current_state_.indoor_relative_humidity, 0.0, 1.0);
    step_settings.n_steps = 1;

    const SingleZoneSimulator simulator(step_settings);
    const SimulationResult rollout = simulator.estimate(action_to_command(action));
    if (rollout.states.empty()) {
        throw std::runtime_error("Simulator returned an empty rollout in RL step().");
    }

    current_state_ = rollout.states.back();
    current_state_.minute = (static_cast<double>(step_index_ + 1) * step_settings.timestep_seconds) / 60.0;

    cumulative_damper_energy_kwh_ += rollout.summary.cumulative_damper_electrical_energy_kwh;
    cumulative_fan_energy_kwh_ += rollout.summary.cumulative_fan_electrical_energy_kwh;
    cumulative_heater_energy_kwh_ += rollout.summary.cumulative_heater_electrical_energy_kwh;
    cumulative_damper_resource_units_ += rollout.summary.cumulative_damper_resource_units;
    cumulative_fan_resource_units_ += rollout.summary.cumulative_fan_resource_units;
    cumulative_heater_resource_units_ += rollout.summary.cumulative_heater_resource_units;

    const RLObservation observation = make_observation(current_state_);
    const double reward = compute_reward(observation.vital_status, action, previous_action_, current_state_);

    ++step_index_;
    const bool truncated = step_index_ >= options_.max_episode_steps;
    const bool terminated = false;
    done_ = truncated || terminated;

    RLStepInfo info{};
    info.step_index = step_index_;
    info.cumulative_damper_energy_kwh = cumulative_damper_energy_kwh_;
    info.cumulative_fan_energy_kwh = cumulative_fan_energy_kwh_;
    info.cumulative_heater_energy_kwh = cumulative_heater_energy_kwh_;
    info.cumulative_total_energy_kwh = cumulative_damper_energy_kwh_ + cumulative_fan_energy_kwh_ + cumulative_heater_energy_kwh_;
    info.cumulative_damper_resource_units = cumulative_damper_resource_units_;
    info.cumulative_fan_resource_units = cumulative_fan_resource_units_;
    info.cumulative_heater_resource_units = cumulative_heater_resource_units_;
    info.reward = reward;
    info.applied_action = action;
    info.simulator_state = current_state_;
    history_.push_back(info);
    previous_action_ = action;

    return RLStepResult{observation, reward, terminated, truncated, info};
}

RLObservation CattleVitalEnv::make_observation(const SimulationState& state) const {
    RLObservation observation{};
    observation.minute = state.minute;
    observation.indoor_temp_c = state.indoor_temp_c;
    observation.indoor_relative_humidity = state.indoor_relative_humidity;
    observation.indoor_humidity_ratio_kg_per_kg = state.indoor_humidity_ratio_kg_per_kg;
    observation.total_airflow_m3_s = state.diagnostics.total_ventilation_flow_m3_s;
    observation.average_airflow_m3_s = state.diagnostics.average_airflow_m3_s;
    observation.outdoor_temp_c = state.outside_temp_c;
    observation.outdoor_relative_humidity = state.outside_relative_humidity;
    observation.outdoor_reference_wind_speed_m_s = state.outside_reference_wind_speed_m_s;
    observation.outdoor_direct_radiation_w_m2 = state.outside_direct_radiation_w_m2;
    observation.outdoor_diffuse_radiation_w_m2 = state.outside_diffuse_radiation_w_m2;
    observation.damper_power_w = state.diagnostics.damper_electrical_power_w;
    observation.fan_power_w = state.diagnostics.fan_electrical_power_w;
    observation.heater_power_w = state.diagnostics.heater_electrical_power_w;
    observation.vital_status = evaluate_vital_status(state);
    return observation;
}

VitalStatus CattleVitalEnv::evaluate_vital_status(const SimulationState& state) const {
    VitalStatus status{};
    status.indoor_temp_error = range_error(state.indoor_temp_c, options_.targets.indoor_temp_c);
    status.indoor_relative_humidity_error = range_error(state.indoor_relative_humidity, options_.targets.indoor_relative_humidity);
    status.airflow_error = range_error(state.diagnostics.total_ventilation_flow_m3_s, options_.targets.airflow_m3_s);
    status.indoor_temp_in_range = is_in_range(state.indoor_temp_c, options_.targets.indoor_temp_c);
    status.indoor_relative_humidity_in_range = is_in_range(state.indoor_relative_humidity, options_.targets.indoor_relative_humidity);
    status.airflow_in_range = is_in_range(state.diagnostics.total_ventilation_flow_m3_s, options_.targets.airflow_m3_s);
    status.in_range_fraction = (static_cast<double>(status.indoor_temp_in_range)
                              + static_cast<double>(status.indoor_relative_humidity_in_range)
                              + static_cast<double>(status.airflow_in_range)) / 3.0;
    return status;
}

double CattleVitalEnv::compute_reward(const VitalStatus& status, const RLAction& current_action, const RLAction& previous_action,
                                      const SimulationState& state) const {
    const double comfort_penalty =
        options_.reward_weights.temperature * status.indoor_temp_error
        + options_.reward_weights.humidity * (status.indoor_relative_humidity_error * 100.0)
        + options_.reward_weights.airflow * status.airflow_error;

    const double dt_hours = base_settings_.timestep_seconds / 3600.0;
    const double step_energy_kwh =
        (state.diagnostics.damper_electrical_power_w
        + state.diagnostics.fan_electrical_power_w
        + state.diagnostics.heater_electrical_power_w) * dt_hours / 1000.0;

    const double control_change_penalty = options_.reward_weights.control_change * (
        std::abs(current_action.damper_percent - previous_action.damper_percent)
        + std::abs(current_action.fan_percent - previous_action.fan_percent)
        + std::abs(current_action.heater_percent - previous_action.heater_percent));

    const double bonus = options_.reward_weights.in_range_bonus * status.in_range_fraction;
    return bonus - comfort_penalty - (options_.reward_weights.energy * step_energy_kwh) - control_change_penalty;
}

ActuatorCommand CattleVitalEnv::action_to_command(const RLAction& action) const {
    const double dt_minutes = base_settings_.timestep_seconds / 60.0;
    const double active_minutes = 2.0 * dt_minutes;
    const double damper = clamp_percent(action.damper_percent);
    const double fan = clamp_percent(action.fan_percent);
    const double heater = clamp_percent(action.heater_percent);

    return ActuatorCommand{
        damper > 0.0,
        fan > 0.0,
        heater > 0.0,
        damper,
        fan,
        heater,
        damper > 0.0 ? active_minutes : 0.0,
        fan > 0.0 ? active_minutes : 0.0,
        heater > 0.0 ? active_minutes : 0.0
    };
}

std::string write_rl_csv_report(const std::vector<RLStepInfo>& history, const std::string& path) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Could not open RL CSV report path for writing: " + path);
    }

    out << "step,minute,reward,indoor_temp_c,indoor_relative_humidity,airflow_m3_s,avg_airflow_m3_s,";
    out << "damper_action_percent,fan_action_percent,heater_action_percent,";
    out << "damper_power_w,fan_power_w,heater_power_w,cumulative_total_energy_kwh\n";

    out << std::fixed << std::setprecision(6);
    for (const auto& info : history) {
        out << info.step_index << ','
            << info.simulator_state.minute << ','
            << info.reward << ','
            << info.simulator_state.indoor_temp_c << ','
            << info.simulator_state.indoor_relative_humidity << ','
            << info.simulator_state.diagnostics.total_ventilation_flow_m3_s << ','
            << info.simulator_state.diagnostics.average_airflow_m3_s << ','
            << info.applied_action.damper_percent << ','
            << info.applied_action.fan_percent << ','
            << info.applied_action.heater_percent << ','
            << info.simulator_state.diagnostics.damper_electrical_power_w << ','
            << info.simulator_state.diagnostics.fan_electrical_power_w << ','
            << info.simulator_state.diagnostics.heater_electrical_power_w << ','
            << info.cumulative_total_energy_kwh << '\n';
    }
    return path;
}

std::string write_rl_svg_graph_report(const std::vector<RLStepInfo>& history, const std::string& path, double max_minutes) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Could not open RL SVG report path for writing: " + path);
    }

    std::vector<double> minutes;
    std::vector<double> temp_c;
    std::vector<double> rh;
    std::vector<double> airflow;
    std::vector<double> reward;

    for (const auto& info : history) {
        if (max_minutes > 0.0 && info.simulator_state.minute > max_minutes) {
            break;
        }
        minutes.push_back(info.simulator_state.minute);
        temp_c.push_back(info.simulator_state.indoor_temp_c);
        rh.push_back(info.simulator_state.indoor_relative_humidity);
        airflow.push_back(info.simulator_state.diagnostics.total_ventilation_flow_m3_s);
        reward.push_back(info.reward);
    }

    if (minutes.empty()) {
        throw std::runtime_error("RL SVG report requested with empty history.");
    }

    const double x_min = 0.0;
    const double x_max = (max_minutes > 0.0) ? max_minutes : minutes.back();
    const double width = 1200.0;
    const double height = 900.0;
    const double left = 80.0;
    const double right = 30.0;
    const double top = 40.0;
    const double block_h = 170.0;
    const double plot_w = width - left - right;
    const double plot_h = 110.0;

    const std::vector<PlotSeries> series = {
        {"Indoor temperature [C]", temp_c, expand_if_flat_min(safe_min_value(temp_c), safe_max_value(temp_c)), expand_if_flat_max(safe_min_value(temp_c), safe_max_value(temp_c))},
        {"Indoor relative humidity [-]", rh, expand_if_flat_min(safe_min_value(rh), safe_max_value(rh)), expand_if_flat_max(safe_min_value(rh), safe_max_value(rh))},
        {"Airflow [m3/s]", airflow, expand_if_flat_min(safe_min_value(airflow), safe_max_value(airflow)), expand_if_flat_max(safe_min_value(airflow), safe_max_value(airflow))},
        {"Reward [-]", reward, expand_if_flat_min(safe_min_value(reward), safe_max_value(reward)), expand_if_flat_max(safe_min_value(reward), safe_max_value(reward))}
    };

    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << ' ' << height << "\">\n";
    out << "<rect x=\"0\" y=\"0\" width=\"" << width << "\" height=\"" << height << "\" fill=\"white\" stroke=\"none\"/>\n";
    out << "<text x=\"" << width / 2.0 << "\" y=\"24\" text-anchor=\"middle\" font-size=\"20\" font-family=\"Arial\">RL environment rollout report</text>\n";

    for (std::size_t i = 0; i < series.size(); ++i) {
        const double y_block = top + static_cast<double>(i) * block_h;
        const double y_plot = y_block + 30.0;
        out << "<text x=\"" << left << "\" y=\"" << y_block + 16.0 << "\" font-size=\"14\" font-family=\"Arial\">" << series[i].label << "</text>\n";
        out << "<rect x=\"" << left << "\" y=\"" << y_plot << "\" width=\"" << plot_w << "\" height=\"" << plot_h << "\" fill=\"none\" stroke=\"black\" stroke-width=\"1\"/>\n";
        out << "<text x=\"" << left - 8.0 << "\" y=\"" << y_plot + 10.0 << "\" text-anchor=\"end\" font-size=\"11\" font-family=\"Arial\">" << std::fixed << std::setprecision(3) << series[i].max_value << "</text>\n";
        out << "<text x=\"" << left - 8.0 << "\" y=\"" << y_plot + plot_h << "\" text-anchor=\"end\" font-size=\"11\" font-family=\"Arial\">" << std::fixed << std::setprecision(3) << series[i].min_value << "</text>\n";
        out << svg_polyline(minutes, series[i].values, left, y_plot, plot_w, plot_h, x_min, x_max, series[i].min_value, series[i].max_value) << "\n";
        out << "<text x=\"" << left << "\" y=\"" << y_plot + plot_h + 18.0 << "\" font-size=\"11\" font-family=\"Arial\">0 min</text>\n";
        out << "<text x=\"" << left + plot_w << "\" y=\"" << y_plot + plot_h + 18.0 << "\" text-anchor=\"end\" font-size=\"11\" font-family=\"Arial\">" << std::fixed << std::setprecision(2) << x_max << " min</text>\n";
    }

    out << "</svg>\n";
    return path;
}

} // namespace cattle_climate
