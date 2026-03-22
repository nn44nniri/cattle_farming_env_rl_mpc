#include "cattle_climate/rl_mpc.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "cattle_climate/env.hpp"

namespace cattle_climate {
namespace {

struct HerdCow {
    std::string cow_id;
    BreedPreset breed;
    double body_weight_kg{0.0};
    DailyWeightRule weight_rule{};
};

struct HerdSnapshot {
    std::string effective_datetime;
    std::vector<HerdCow> cows;
};

std::string read_text_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Unable to open file: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string extract_object_optional(const std::string& json, const std::string& key) {
    const std::regex key_regex("\\\"" + key + "\\\"\\s*:\\s*\\{");
    std::smatch match;
    if (!std::regex_search(json, match, key_regex)) return {};
    const std::size_t start = static_cast<std::size_t>(match.position(0) + match.length(0) - 1);
    int depth = 0;
    for (std::size_t i = start; i < json.size(); ++i) {
        if (json[i] == '{') ++depth;
        else if (json[i] == '}') {
            --depth;
            if (depth == 0) return json.substr(start, i - start + 1);
        }
    }
    throw std::runtime_error("Unclosed JSON object: " + key);
}

std::string extract_array_optional(const std::string& json, const std::string& key) {
    const std::regex key_regex("\\\"" + key + "\\\"\\s*:\\s*\\[");
    std::smatch match;
    if (!std::regex_search(json, match, key_regex)) return {};
    const std::size_t start = static_cast<std::size_t>(match.position(0) + match.length(0) - 1);
    int depth = 0;
    for (std::size_t i = start; i < json.size(); ++i) {
        if (json[i] == '[') ++depth;
        else if (json[i] == ']') {
            --depth;
            if (depth == 0) return json.substr(start, i - start + 1);
        }
    }
    throw std::runtime_error("Unclosed JSON array: " + key);
}

std::vector<std::string> split_top_level_objects(const std::string& array_text) {
    std::vector<std::string> out;
    if (array_text.size() < 2) return out;
    int obj_depth = 0;
    int arr_depth = 0;
    std::size_t current_start = std::string::npos;
    bool in_string = false;
    char prev = '\0';
    for (std::size_t i = 0; i < array_text.size(); ++i) {
        const char ch = array_text[i];
        if (ch == '"' && prev != '\\') in_string = !in_string;
        if (!in_string) {
            if (ch == '[') ++arr_depth;
            else if (ch == ']') --arr_depth;
            else if (ch == '{') {
                ++obj_depth;
                if (obj_depth == 1) current_start = i;
            } else if (ch == '}') {
                --obj_depth;
                if (obj_depth == 0 && current_start != std::string::npos) {
                    out.push_back(array_text.substr(current_start, i - current_start + 1));
                    current_start = std::string::npos;
                }
            }
        }
        prev = ch;
    }
    return out;
}

std::vector<std::string> extract_array_of_objects(const std::string& json, const std::string& key) {
    const std::string arr = extract_array_optional(json, key);
    if (arr.empty()) return {};
    return split_top_level_objects(arr);
}

std::vector<std::string> extract_string_array(const std::string& json, const std::string& key) {
    std::vector<std::string> out;
    const std::string arr = extract_array_optional(json, key);
    if (arr.empty()) return out;
    const std::regex r("\\\"([^\\\"]*)\\\"");
    auto begin = std::sregex_iterator(arr.begin(), arr.end(), r);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) out.push_back((*it)[1].str());
    return out;
}

double extract_number(const std::string& json, const std::string& key, double default_value) {
    if (json.empty()) return default_value;
    const std::regex r("\\\"" + key + "\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)");
    std::smatch match;
    if (std::regex_search(json, match, r)) return std::stod(match[1].str());
    return default_value;
}

std::string extract_string(const std::string& json, const std::string& key, const std::string& default_value) {
    if (json.empty()) return default_value;
    const std::regex r("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    if (std::regex_search(json, match, r)) return match[1].str();
    return default_value;
}

int extract_int(const std::string& json, const std::string& key, int default_value) {
    return static_cast<int>(extract_number(json, key, default_value));
}

bool extract_bool(const std::string& json, const std::string& key, bool default_value) {
    if (json.empty()) return default_value;
    const std::regex r_num("\\\"" + key + "\\\"\\s*:\\s*([01])");
    std::smatch match;
    if (std::regex_search(json, match, r_num)) return match[1].str() == "1";
    const std::regex r_word("\\\"" + key + "\\\"\\s*:\\s*(true|false)");
    if (std::regex_search(json, match, r_word)) return match[1].str() == "true";
    return default_value;
}

std::string normalize_datetime_prefix(const std::string& text) {
    std::string out;
    for (char ch : text) {
        if ((ch >= '0' && ch <= '9') || ch == '-' || ch == ':' || ch == ' ') {
            out.push_back(ch);
            if (out.size() == 19) break;
        }
    }
    return out;
}

std::size_t resolve_datetime_index(const WeatherDataset& dataset, const std::string& datetime_text, bool is_end) {
    if (dataset.empty()) throw std::runtime_error("Weather dataset is empty.");
    if (datetime_text.empty()) return is_end ? dataset.size() - 1 : 0;
    const std::string target = normalize_datetime_prefix(datetime_text);
    if (target.size() != 19) throw std::runtime_error("Datetime must use the format YYYY-MM-DD HH:MM:SS: " + datetime_text);

    std::size_t first_match = dataset.size();
    std::size_t last_match = dataset.size();
    for (std::size_t i = 0; i < dataset.size(); ++i) {
        const std::string sample_dt = normalize_datetime_prefix(dataset.at(i).dt_iso);
        if (sample_dt == target) {
            if (first_match == dataset.size()) first_match = i;
            last_match = i;
        }
    }
    if (first_match == dataset.size()) throw std::runtime_error("Requested datetime not found in dataset: " + datetime_text);
    return is_end ? last_match : first_match;
}

SimulationSettings settings_from_weather(const SimulationSettings& base, const WeatherSample& sample, const SimulationState* current_state) {
    SimulationSettings s = base;
    s.climate.outside_temp_c = sample.outdoor_temp_c;
    s.climate.outside_relative_humidity = sample.relative_humidity;
    s.climate.reference_wind_speed_m_s = sample.wind_speed_m_s;
    s.climate.direct_radiation_w_m2 = sample.direct_radiation_w_m2;
    s.climate.diffuse_radiation_w_m2 = sample.diffuse_radiation_w_m2;
    s.climate.day_of_year = std::max(1, std::min(365, (sample.month - 1) * 30 + sample.day));
    s.climate.clock_time_hours = static_cast<double>(sample.hour);
    if (current_state != nullptr) {
        s.initial_indoor_temp_c = current_state->indoor_temp_c;
        s.initial_indoor_relative_humidity = std::clamp(current_state->indoor_relative_humidity, 0.0, 1.0);
    }
    s.n_steps = 1;
    return s;
}

ActuatorCommand to_command(const RLAction& action, double timestep_seconds) {
    const double active_minutes = timestep_seconds / 60.0;
    return ActuatorCommand{
        action.damper_percent > 0.0,
        action.fan_percent > 0.0,
        action.heater_percent > 0.0,
        action.damper_percent,
        action.fan_percent,
        action.heater_percent,
        action.damper_percent > 0.0 ? active_minutes : 0.0,
        action.fan_percent > 0.0 ? active_minutes : 0.0,
        action.heater_percent > 0.0 ? active_minutes : 0.0
    };
}

double range_error(double value, double lo, double hi) {
    if (value < lo) return lo - value;
    if (value > hi) return value - hi;
    return 0.0;
}

std::vector<HerdCow> make_default_herd(const ProjectSettings& project) {
    std::vector<HerdCow> herd;
    const auto& profiles = project.training_breed_profiles.empty() ? std::vector<CattleSettings>{project.cattle} : project.training_breed_profiles;
    std::size_t reserve_count = 0;
    for (const auto& c : profiles) reserve_count += static_cast<std::size_t>(std::max(1, c.cow_count));
    herd.reserve(reserve_count);
    std::size_t index = 0;
    for (const auto& c : profiles) {
        const int count = std::max(1, c.cow_count);
        const std::string base_id = c.cow_id.empty() ? ("cow_" + std::to_string(++index)) : c.cow_id;
        for (int n = 0; n < count; ++n) {
            HerdCow cow{};
            cow.cow_id = count == 1 ? base_id : (base_id + "_" + std::to_string(n + 1));
            cow.breed = breed_from_name(c.breed);
            cow.body_weight_kg = c.initial_body_weight_kg;
            cow.weight_rule = c.weight_rule;
            herd.push_back(cow);
        }
    }
    return herd;
}

double comfort_weight_delta_kg(const TNZRange& tnz, const DailyWeightRule& rule, double timestep_hours) {
    const double day_fraction = timestep_hours / 24.0;
    return tnz.within_tnz ? (rule.gain_when_comfortable_kg_per_day * day_fraction)
                          : (-rule.loss_when_stressed_kg_per_day * day_fraction);
}

TNZRange tnz_for_cow(const HerdCow& cow, const WeatherSample& sample, const SimulationState& state) {
    ComfortInputs comfort{};
    comfort.ambient_temp_c = state.indoor_temp_c;
    comfort.relative_humidity = state.indoor_relative_humidity;
    comfort.wind_speed_m_s = sample.wind_speed_m_s;
    comfort.precipitation_mm = sample.precipitation_mm;
    comfort.cloud_cover_fraction = sample.cloud_cover_fraction;
    comfort.solar_radiation_w_m2 = sample.direct_radiation_w_m2 + sample.diffuse_radiation_w_m2;
    comfort.body_weight_kg = cow.body_weight_kg;
    comfort.heat_production_factor = cow.breed.heat_production_factor;
    return compute_tnz_range(cow.breed, comfort);
}

double step_reward_for_cow(const ControllerModel& model, const HerdCow& cow, const WeatherSample& sample, const SimulationState& state,
                           const RLAction& action, const RLAction& previous_action) {
    const TNZRange tnz = tnz_for_cow(cow, sample, state);
    const double timestep_hours = 1.0;
    const double weight_delta = comfort_weight_delta_kg(tnz, cow.weight_rule, timestep_hours);
    const double energy_kwh = (state.diagnostics.damper_electrical_power_w + state.diagnostics.fan_electrical_power_w + state.diagnostics.heater_electrical_power_w) / 1000.0 * timestep_hours;
    const double comfort_pen = range_error(state.indoor_temp_c, tnz.lct_c, tnz.uct_c);
    const double humidity_pen = range_error(state.indoor_relative_humidity, cow.breed.optimal_relative_humidity_min, cow.breed.optimal_relative_humidity_max);
    const double wind_pen = range_error(sample.wind_speed_m_s, cow.breed.optimal_wind_min_m_s, cow.breed.optimal_wind_max_m_s);
    const double actuator_delta = std::abs(action.damper_percent - previous_action.damper_percent)
        + std::abs(action.fan_percent - previous_action.fan_percent)
        + std::abs(action.heater_percent - previous_action.heater_percent);

    return model.weights.weight_gain_reward * weight_delta
        - model.weights.comfort_penalty * comfort_pen
        - model.weights.energy_penalty * energy_kwh
        - model.weights.humidity_penalty * humidity_pen
        - model.weights.wind_penalty * wind_pen
        - model.weights.actuator_change_penalty * actuator_delta;
}

std::vector<RLAction> candidate_actions() {
    const std::vector<double> levels{0.0, 50.0, 100.0};
    std::vector<RLAction> actions;
    for (double d : levels) for (double f : levels) for (double h : levels) actions.push_back(RLAction{d, f, h});
    return actions;
}

std::tuple<RLAction, double> choose_action(const ControllerModel& model,
                                           const SimulationSettings& base_settings,
                                           const WeatherDataset& dataset,
                                           int dataset_index,
                                           const SimulationState* current_state,
                                           const std::vector<HerdCow>& herd,
                                           const RLAction& previous_action) {
    const auto actions = candidate_actions();
    RLAction best = actions.front();
    double best_score = -std::numeric_limits<double>::infinity();

    for (const auto& action : actions) {
        double score = 0.0;
        SimulationState temp_state = current_state ? *current_state : SimulationState{};
        bool has_state = current_state != nullptr;
        std::vector<HerdCow> predicted_herd = herd;
        RLAction prev = previous_action;
        for (int h = 0; h < model.horizon_steps; ++h) {
            const int idx = std::min<int>(dataset_index + h, static_cast<int>(dataset.size()) - 1);
            const auto& sample = dataset.at(static_cast<std::size_t>(idx));
            const SimulationSettings step_settings = settings_from_weather(base_settings, sample, has_state ? &temp_state : nullptr);
            const SingleZoneSimulator simulator(step_settings);
            const SimulationResult result = simulator.estimate(to_command(action, step_settings.timestep_seconds));
            temp_state = result.states.back();
            has_state = true;

            double step_total = 0.0;
            for (auto& cow : predicted_herd) {
                step_total += step_reward_for_cow(model, cow, sample, temp_state, action, prev);
                const TNZRange tnz = tnz_for_cow(cow, sample, temp_state);
                cow.body_weight_kg += comfort_weight_delta_kg(tnz, cow.weight_rule, 1.0);
            }
            const double herd_denom = static_cast<double>(std::max<std::size_t>(1, predicted_herd.size()));
            score += step_total / herd_denom;
            prev = action;
        }
        if (score > best_score) {
            best_score = score;
            best = action;
        }
    }
    return {best, best_score};
}

void update_weights(ControllerModel& model, double avg_comfort_error, double avg_energy_kwh, double avg_weight_delta) {
    if (avg_comfort_error > 0.5) model.weights.comfort_penalty *= 1.05;
    else model.weights.comfort_penalty *= 0.995;

    if (avg_energy_kwh > 3.0) model.weights.energy_penalty *= 1.03;
    else model.weights.energy_penalty *= 0.995;

    if (avg_weight_delta < 0.0) model.weights.weight_gain_reward *= 1.02;
    else model.weights.weight_gain_reward *= 0.998;

    model.weights.comfort_penalty = std::clamp(model.weights.comfort_penalty, 20.0, 1000.0);
    model.weights.energy_penalty = std::clamp(model.weights.energy_penalty, 1.0, 200.0);
    model.weights.weight_gain_reward = std::clamp(model.weights.weight_gain_reward, 20.0, 1000.0);
}

void print_training_progress(int episode, int total_episodes, int step, int total_steps,
                             double running_reward, double running_comfort, double running_energy,
                             double mean_weight, std::size_t herd_size) {
    const int width = 30;
    const double fraction = total_steps > 0 ? static_cast<double>(step) / static_cast<double>(total_steps) : 1.0;
    const int filled = static_cast<int>(std::round(fraction * width));
    std::ostringstream bar;
    bar << "\repisode " << episode << '/' << total_episodes << " [";
    for (int i = 0; i < width; ++i) bar << (i < filled ? '#' : '-');
    bar << "] " << std::fixed << std::setprecision(1) << (fraction * 100.0) << "% "
        << step << '/' << total_steps
        << " reward=" << std::setprecision(3) << running_reward
        << " comfort=" << running_comfort
        << " energy=" << running_energy
        << " mean_bw=" << std::setprecision(2) << mean_weight
        << " herd=" << herd_size << "   ";
    std::cout << bar.str() << std::flush;
    if (step >= total_steps) std::cout << "\n";
}

EventLogRecord make_event_log_record(const std::string& mode, const std::string& dt_iso, const RLAction& action,
                                   const SimulationState& state, const WeatherSample& sample,
                                   double predicted_reward, double mean_bw, double herd_size,
                                   double lct_c, double uct_c, double timestep_seconds, double internal_heat_w) {
    EventLogRecord rec{};
    rec.event_datetime = normalize_datetime_prefix(dt_iso);
    rec.mode = mode;
    rec.damper_percent = action.damper_percent;
    rec.fan_percent = action.fan_percent;
    rec.heater_percent = action.heater_percent;
    const double hours = std::max(0.0, timestep_seconds) / 3600.0;
    rec.damper_energy_kwh = state.diagnostics.damper_electrical_power_w / 1000.0 * hours;
    rec.fan_energy_kwh = state.diagnostics.fan_electrical_power_w / 1000.0 * hours;
    rec.heater_energy_kwh = state.diagnostics.heater_electrical_power_w / 1000.0 * hours;
    rec.total_energy_kwh = rec.damper_energy_kwh + rec.fan_energy_kwh + rec.heater_energy_kwh;
    rec.predicted_reward = predicted_reward;
    rec.mean_body_weight_kg = mean_bw;
    rec.herd_size = herd_size;
    rec.outdoor_temp_c = sample.outdoor_temp_c;
    rec.indoor_temp_c = state.indoor_temp_c;
    rec.indoor_relative_humidity = state.indoor_relative_humidity;
    rec.indoor_airflow_m3_s = state.diagnostics.total_ventilation_flow_m3_s;
    rec.outdoor_wind_speed_m_s = sample.wind_speed_m_s;
    rec.solar_radiation_w_m2 = sample.direct_radiation_w_m2 + sample.diffuse_radiation_w_m2;
    rec.internal_heat_generated_w = internal_heat_w;
    rec.lct_c = lct_c;
    rec.uct_c = uct_c;
    return rec;
}

std::vector<double> collect_series(const std::vector<ValidationRecord>& history, const std::string& key) {
    std::vector<double> out;
    out.reserve(history.size());
    for (const auto& r : history) {
        if (key == "minute") out.push_back(r.minute);
        else if (key == "outdoor_temp") out.push_back(r.outdoor_temp_c);
        else if (key == "indoor_temp") out.push_back(r.indoor_temp_c);
        else if (key == "lct") out.push_back(r.lct_c);
        else if (key == "uct") out.push_back(r.uct_c);
        else if (key == "rh") out.push_back(r.indoor_relative_humidity);
        else if (key == "wind") out.push_back(r.outdoor_wind_speed_m_s);
        else if (key == "solar") out.push_back(r.solar_radiation_w_m2);
        else if (key == "heat") out.push_back(r.internal_heat_generated_w);
        else if (key == "damper") out.push_back(r.action.damper_percent);
        else if (key == "fan") out.push_back(r.action.fan_percent);
        else if (key == "heater") out.push_back(r.action.heater_percent);
        else if (key == "reward") out.push_back(r.reward);
    }
    return out;
}

double minv(const std::vector<double>& v) {
    return v.empty() ? 0.0 : *std::min_element(v.begin(), v.end());
}

double maxv(const std::vector<double>& v) {
    return v.empty() ? 1.0 : *std::max_element(v.begin(), v.end());
}

std::string polyline(const std::vector<double>& x, const std::vector<double>& y,
                     double left, double top, double width, double height,
                     double xmin, double xmax, double ymin, double ymax,
                     const std::string& color) {
    std::ostringstream ss;
    ss << "<polyline fill=\"none\" stroke=\"" << color << "\" stroke-width=\"1.5\" points=\"";
    const double yr = (ymax - ymin == 0.0) ? 1.0 : (ymax - ymin);
    const double xr = (xmax - xmin == 0.0) ? 1.0 : (xmax - xmin);
    for (std::size_t i = 0; i < x.size() && i < y.size(); ++i) {
        const double px = left + width * (x[i] - xmin) / xr;
        const double py = top + height - height * (y[i] - ymin) / yr;
        if (i) ss << ' ';
        ss << px << ',' << py;
    }
    ss << "\"/>";
    return ss.str();
}

void ensure_parent(const std::string& path) {
    const auto pos = path.find_last_of('/');
    if (pos == std::string::npos) return;
    const std::string dir = path.substr(0, pos);
    if (dir.empty()) return;
    std::system((std::string("mkdir -p \"") + dir + "\"").c_str());
}

std::vector<HerdSnapshot> load_herd_snapshots(const std::string& path, const DailyWeightRule& default_rule) {
    std::vector<HerdSnapshot> snapshots;
    if (path.empty()) return snapshots;
    const std::string text = read_text_file(path);
    for (const auto& record_text : extract_array_of_objects(text, "records")) {
        HerdSnapshot snapshot{};
        snapshot.effective_datetime = normalize_datetime_prefix(extract_string(record_text, "effective_datetime", ""));
        for (const auto& cow_text : extract_array_of_objects(record_text, "cows")) {
            HerdCow cow{};
            cow.cow_id = extract_string(cow_text, "cow_id", "unknown_cow");
            cow.breed = breed_from_name(extract_string(cow_text, "breed", "charolais"));
            cow.body_weight_kg = extract_number(cow_text, "body_weight_kg", 450.0);
            cow.weight_rule.gain_when_comfortable_kg_per_day = extract_number(cow_text, "gain_when_comfortable_kg_per_day", default_rule.gain_when_comfortable_kg_per_day);
            cow.weight_rule.loss_when_stressed_kg_per_day = extract_number(cow_text, "loss_when_stressed_kg_per_day", default_rule.loss_when_stressed_kg_per_day);
            snapshot.cows.push_back(cow);
        }
        if (!snapshot.effective_datetime.empty() && !snapshot.cows.empty()) snapshots.push_back(snapshot);
    }
    std::sort(snapshots.begin(), snapshots.end(), [](const HerdSnapshot& a, const HerdSnapshot& b) {
        return a.effective_datetime < b.effective_datetime;
    });
    return snapshots;
}

bool apply_snapshot_for_datetime(std::vector<HerdCow>& herd, const std::vector<HerdSnapshot>& snapshots, const std::string& current_datetime) {
    if (snapshots.empty()) return false;
    const std::string target = normalize_datetime_prefix(current_datetime);
    const HerdSnapshot* best = nullptr;
    for (const auto& s : snapshots) {
        if (s.effective_datetime <= target) best = &s;
        else break;
    }
    if (best == nullptr) return false;

    std::vector<HerdCow> merged = herd;
    for (const auto& incoming : best->cows) {
        auto it = std::find_if(merged.begin(), merged.end(), [&](const HerdCow& existing) {
            return existing.cow_id == incoming.cow_id;
        });
        if (it == merged.end()) merged.push_back(incoming);
        else {
            it->breed = incoming.breed;
            it->body_weight_kg = incoming.body_weight_kg;
            it->weight_rule = incoming.weight_rule;
        }
    }
    herd = merged;
    return true;
}

double mean_body_weight(const std::vector<HerdCow>& herd) {
    if (herd.empty()) return 0.0;
    double sum = 0.0;
    for (const auto& cow : herd) sum += cow.body_weight_kg;
    return sum / static_cast<double>(herd.size());
}

std::pair<double, double> mean_tnz_bounds(const std::vector<HerdCow>& herd, const WeatherSample& sample, const SimulationState& state, const BreedPreset& fallback_breed) {
    if (herd.empty()) {
        ComfortInputs comfort{};
        comfort.ambient_temp_c = state.indoor_temp_c;
        comfort.relative_humidity = state.indoor_relative_humidity;
        comfort.wind_speed_m_s = sample.wind_speed_m_s;
        comfort.precipitation_mm = sample.precipitation_mm;
        comfort.cloud_cover_fraction = sample.cloud_cover_fraction;
        comfort.solar_radiation_w_m2 = sample.direct_radiation_w_m2 + sample.diffuse_radiation_w_m2;
        comfort.body_weight_kg = 450.0;
        comfort.heat_production_factor = fallback_breed.heat_production_factor;
        const TNZRange tnz = compute_tnz_range(fallback_breed, comfort);
        return {tnz.lct_c, tnz.uct_c};
    }
    double lct = 0.0;
    double uct = 0.0;
    for (const auto& cow : herd) {
        const TNZRange tnz = tnz_for_cow(cow, sample, state);
        lct += tnz.lct_c;
        uct += tnz.uct_c;
    }
    const double denom = static_cast<double>(herd.size());
    return {lct / denom, uct / denom};
}



std::vector<HerdCow> herd_from_grpc_records(const std::vector<HerdCowGrpcRecord>& records, const DailyWeightRule& default_rule) {
    std::vector<HerdCow> herd;
    herd.reserve(records.size());
    for (const auto& r : records) {
        HerdCow cow{};
        cow.cow_id = r.cow_id;
        cow.breed = breed_from_name(r.breed);
        cow.body_weight_kg = r.body_weight_kg;
        cow.weight_rule.gain_when_comfortable_kg_per_day = r.gain_when_comfortable_kg_per_day <= 0.0 ? default_rule.gain_when_comfortable_kg_per_day : r.gain_when_comfortable_kg_per_day;
        cow.weight_rule.loss_when_stressed_kg_per_day = r.loss_when_stressed_kg_per_day <= 0.0 ? default_rule.loss_when_stressed_kg_per_day : r.loss_when_stressed_kg_per_day;
        herd.push_back(cow);
    }
    return herd;
}

void load_herd_from_transformation(std::vector<HerdCow>& herd,
                                   const TransformationSettings& transformation,
                                   const DailyWeightRule& default_rule,
                                   const std::string& current_datetime) {
    if (!transformation.ligaps_beef_weights.enabled || transformation.ligaps_beef_weights.response_json_path.empty()) return;
    const auto snapshots = load_weight_snapshots_from_bridge(transformation.ligaps_beef_weights.response_json_path);
    std::vector<HerdCowGrpcRecord> records;
    if (lookup_weight_snapshot_for_datetime(records, snapshots, current_datetime)) {
        herd = herd_from_grpc_records(records, default_rule);
    }
}

WeatherSample resolve_operational_weather(const WeatherSample& dataset_sample,
                                          const TransformationSettings& transformation,
                                          const std::string& current_datetime) {
    if (!transformation.sensors_interface.enabled || transformation.sensors_interface.response_json_path.empty()) return dataset_sample;
    const auto sensor_snapshots = load_sensor_snapshots_from_bridge(transformation.sensors_interface.response_json_path);
    SensorSnapshot snapshot{};
    if (!lookup_sensor_snapshot_for_datetime(snapshot, sensor_snapshots, current_datetime)) return dataset_sample;
    return apply_sensor_snapshot_to_weather(dataset_sample, &snapshot);
}

std::string now_datetime_string() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t tt = system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&tt, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

bool file_exists(const std::string& path) {
    if (path.empty()) return false;
    std::ifstream in(path);
    return static_cast<bool>(in);
}

std::string service_temp_path(const std::string& leaf) {
    const std::string dir = "/tmp/cattle_farming_env_rl_mpc_service";
    std::system((std::string("mkdir -p ") + dir).c_str());
    return dir + "/" + leaf;
}

} // namespace

ProjectSettings load_project_settings_from_json(const std::string& path) {
    ProjectSettings p{};
    p.simulator = load_settings_from_json(path);
    const std::string text = read_text_file(path);
    for (const auto& breed_text : extract_array_of_objects(text, "breed_presets")) {
        BreedPreset b{};
        b.name = extract_string(breed_text, "name", b.name);
        b.lct_ref_c = extract_number(breed_text, "lct_ref_c", b.lct_ref_c);
        b.uct_ref_c = extract_number(breed_text, "uct_ref_c", b.uct_ref_c);
        b.optimal_relative_humidity_min = extract_number(breed_text, "optimal_relative_humidity_min", b.optimal_relative_humidity_min);
        b.optimal_relative_humidity_max = extract_number(breed_text, "optimal_relative_humidity_max", b.optimal_relative_humidity_max);
        b.optimal_wind_min_m_s = extract_number(breed_text, "optimal_wind_min_m_s", b.optimal_wind_min_m_s);
        b.optimal_wind_max_m_s = extract_number(breed_text, "optimal_wind_max_m_s", b.optimal_wind_max_m_s);
        b.heat_production_factor = extract_number(breed_text, "heat_production_factor", b.heat_production_factor);
        b.coat_reflectivity = extract_number(breed_text, "coat_reflectivity", b.coat_reflectivity);
        b.coat_depth_m = extract_number(breed_text, "coat_depth_m", b.coat_depth_m);
        b.area_factor = extract_number(breed_text, "area_factor", b.area_factor);
        b.cbsmax = extract_number(breed_text, "cbsmax", b.cbsmax);
        b.lasmax_a = extract_number(breed_text, "lasmax_a", b.lasmax_a);
        b.lasmax_b = extract_number(breed_text, "lasmax_b", b.lasmax_b);
        b.rbcsf = extract_number(breed_text, "rbcsf", b.rbcsf);
        b.reference_skin_temp_c = extract_number(breed_text, "reference_skin_temp_c", b.reference_skin_temp_c);
        b.metabolic_multiplier = extract_number(breed_text, "metabolic_multiplier", b.metabolic_multiplier);
        p.breed_presets.push_back(b);
    }
    set_breed_registry(p.breed_presets);
    const std::string cattle = extract_object_optional(text, "cattle");
    const std::string training = extract_object_optional(text, "training");
    const std::string validation = extract_object_optional(text, "validation");
    const std::string reward = extract_object_optional(text, "reward_weights");
    const std::string grpc = extract_object_optional(text, "grpc_weight_feed");
    const std::string transformation = extract_object_optional(text, "transformation");
    const std::string ligaps = extract_object_optional(transformation, "ligaps_beef_weights");
    const std::string ligaps_optimizer = extract_object_optional(transformation, "ligaps_beef_optimizer");
    const std::string sensors = extract_object_optional(transformation, "sensors_interface");
    const std::string actuators = extract_object_optional(transformation, "actuators_interface");
    const std::string estimator = extract_object_optional(transformation, "estimator_interface");
    const std::string event_store = extract_object_optional(text, "event_store");
    const std::string runtime_service = extract_object_optional(text, "runtime_service");

    p.cattle.cow_id = extract_string(cattle, "cow_id", p.cattle.cow_id);
    p.cattle.breed = extract_string(cattle, "breed", p.cattle.breed);
    p.cattle.cow_count = extract_int(cattle, "cow_count", p.cattle.cow_count);
    p.cattle.initial_body_weight_kg = extract_number(cattle, "initial_body_weight_kg", p.cattle.initial_body_weight_kg);
    p.cattle.weight_rule.gain_when_comfortable_kg_per_day = extract_number(cattle, "gain_when_comfortable_kg_per_day", p.cattle.weight_rule.gain_when_comfortable_kg_per_day);
    p.cattle.weight_rule.loss_when_stressed_kg_per_day = extract_number(cattle, "loss_when_stressed_kg_per_day", p.cattle.weight_rule.loss_when_stressed_kg_per_day);

    for (const auto& profile_text : extract_array_of_objects(text, "training_breed_profiles")) {
        CattleSettings c{};
        c.cow_id = extract_string(profile_text, "cow_id", c.cow_id);
        c.breed = extract_string(profile_text, "breed", c.breed);
        c.cow_count = extract_int(profile_text, "cow_count", c.cow_count);
        c.initial_body_weight_kg = extract_number(profile_text, "initial_body_weight_kg", c.initial_body_weight_kg);
        c.weight_rule.gain_when_comfortable_kg_per_day = extract_number(profile_text, "gain_when_comfortable_kg_per_day", c.weight_rule.gain_when_comfortable_kg_per_day);
        c.weight_rule.loss_when_stressed_kg_per_day = extract_number(profile_text, "loss_when_stressed_kg_per_day", c.weight_rule.loss_when_stressed_kg_per_day);
        p.training_breed_profiles.push_back(c);
    }

    p.training.dataset_path = extract_string(training, "dataset_path", p.training.dataset_path);
    p.training.start_datetime = extract_string(training, "start_datetime", p.training.start_datetime);
    p.training.end_datetime = extract_string(training, "end_datetime", p.training.end_datetime);
    p.training.steps_per_episode = extract_int(training, "steps_per_episode", p.training.steps_per_episode);
    p.training.horizon_steps = extract_int(training, "horizon_steps", p.training.horizon_steps);
    p.training.episodes = extract_int(training, "episodes", p.training.episodes);
    p.training.seed = extract_int(training, "seed", p.training.seed);
    p.training.model_output_path = extract_string(training, "model_output_path", p.training.model_output_path);

    p.validation.start_datetime = extract_string(validation, "start_datetime", p.validation.start_datetime);
    p.validation.end_datetime = extract_string(validation, "end_datetime", p.validation.end_datetime);
    p.validation.automatic_weight_update = extract_bool(validation, "automatic_weight_update", p.validation.automatic_weight_update);
    p.validation.manual_weight_file = extract_string(validation, "manual_weight_file", p.validation.manual_weight_file);
    p.validation.csv_output_path = extract_string(validation, "csv_output_path", p.validation.csv_output_path);
    p.validation.svg_output_path = extract_string(validation, "svg_output_path", p.validation.svg_output_path);

    p.reward_weights.weight_gain_reward = extract_number(reward, "weight_gain_reward", p.reward_weights.weight_gain_reward);
    p.reward_weights.comfort_penalty = extract_number(reward, "comfort_penalty", p.reward_weights.comfort_penalty);
    p.reward_weights.energy_penalty = extract_number(reward, "energy_penalty", p.reward_weights.energy_penalty);
    p.reward_weights.humidity_penalty = extract_number(reward, "humidity_penalty", p.reward_weights.humidity_penalty);
    p.reward_weights.wind_penalty = extract_number(reward, "wind_penalty", p.reward_weights.wind_penalty);
    p.reward_weights.actuator_change_penalty = extract_number(reward, "actuator_change_penalty", p.reward_weights.actuator_change_penalty);

    p.grpc_weight_feed.enabled = extract_bool(grpc, "enabled", p.grpc_weight_feed.enabled);
    p.grpc_weight_feed.endpoint = extract_string(grpc, "endpoint", p.grpc_weight_feed.endpoint);
    p.grpc_weight_feed.service = extract_string(grpc, "service", p.grpc_weight_feed.service);
    p.grpc_weight_feed.method = extract_string(grpc, "method", p.grpc_weight_feed.method);
    p.grpc_weight_feed.snapshot_json_path = extract_string(grpc, "snapshot_json_path", p.grpc_weight_feed.snapshot_json_path);
    p.grpc_weight_feed.request_timeout_ms = extract_int(grpc, "request_timeout_ms", p.grpc_weight_feed.request_timeout_ms);

    p.transformation.ligaps_beef_weights.enabled = extract_bool(ligaps, "enabled", p.grpc_weight_feed.enabled);
    p.transformation.ligaps_beef_weights.endpoint = extract_string(ligaps, "endpoint", p.grpc_weight_feed.endpoint);
    p.transformation.ligaps_beef_weights.service = extract_string(ligaps, "service", "cattle.farming.transformation.v1.LiGAPSBeefWeightService");
    p.transformation.ligaps_beef_weights.method = extract_string(ligaps, "method", "GetLatestWeightsByDate");
    p.transformation.ligaps_beef_weights.request_timeout_ms = extract_string(ligaps, "request_timeout_ms", std::to_string(p.grpc_weight_feed.request_timeout_ms));
    p.transformation.ligaps_beef_weights.request_json_path = extract_string(ligaps, "request_json_path", "config/ligaps_beef_request.json");
    p.transformation.ligaps_beef_weights.response_json_path = extract_string(ligaps, "response_json_path", p.grpc_weight_feed.snapshot_json_path);

    p.transformation.ligaps_beef_optimizer.enabled = extract_bool(ligaps_optimizer, "enabled", false);
    p.transformation.ligaps_beef_optimizer.endpoint = extract_string(ligaps_optimizer, "endpoint", "unix:///tmp/ligaps-beef-optimizer.sock");
    p.transformation.ligaps_beef_optimizer.service = extract_string(ligaps_optimizer, "service", "cattle.farming.transformation.v1.LiGAPSBeefOptimizerService");
    p.transformation.ligaps_beef_optimizer.method = extract_string(ligaps_optimizer, "method", "GetIndoorClimateByDate");
    p.transformation.ligaps_beef_optimizer.request_timeout_ms = extract_string(ligaps_optimizer, "request_timeout_ms", "2000");
    p.transformation.ligaps_beef_optimizer.request_json_path = extract_string(ligaps_optimizer, "request_json_path", "config/ligaps_beef_optimizer_request.json");
    p.transformation.ligaps_beef_optimizer.response_json_path = extract_string(ligaps_optimizer, "response_json_path", "output/ligaps_beef_optimizer_climate_report.json");

    p.transformation.sensors_interface.enabled = extract_bool(sensors, "enabled", false);
    p.transformation.sensors_interface.endpoint = extract_string(sensors, "endpoint", "unix:///tmp/sensors-interface.sock");
    p.transformation.sensors_interface.service = extract_string(sensors, "service", "cattle.farming.transformation.v1.SensorsInterfaceService");
    p.transformation.sensors_interface.method = extract_string(sensors, "method", "GetLatestSensorFrame");
    p.transformation.sensors_interface.request_timeout_ms = extract_string(sensors, "request_timeout_ms", "2000");
    p.transformation.sensors_interface.request_json_path = extract_string(sensors, "request_json_path", "config/sensors_interface_request.json");
    p.transformation.sensors_interface.response_json_path = extract_string(sensors, "response_json_path", "config/sensors_interface_snapshot.json");

    p.transformation.actuators_interface.enabled = extract_bool(actuators, "enabled", false);
    p.transformation.actuators_interface.endpoint = extract_string(actuators, "endpoint", "unix:///tmp/actuators-interface.sock");
    p.transformation.actuators_interface.service = extract_string(actuators, "service", "cattle.farming.transformation.v1.ActuatorsInterfaceService");
    p.transformation.actuators_interface.method = extract_string(actuators, "method", "PushActuatorDispatch");
    p.transformation.actuators_interface.request_timeout_ms = extract_string(actuators, "request_timeout_ms", "2000");
    p.transformation.actuators_interface.request_json_path = extract_string(actuators, "request_json_path", "config/actuators_interface_request.json");
    p.transformation.actuators_interface.response_json_path = extract_string(actuators, "response_json_path", "output/actuators_dispatch.json");

    p.transformation.estimator_interface.enabled = extract_bool(estimator, "enabled", false);
    p.transformation.estimator_interface.endpoint = extract_string(estimator, "endpoint", "unix:///tmp/estimator-interface.sock");
    p.transformation.estimator_interface.service = extract_string(estimator, "service", "cattle.farming.transformation.v1.EstimatorInterfaceService");
    p.transformation.estimator_interface.method = extract_string(estimator, "method", "PushEnergyAggregateReport");
    p.transformation.estimator_interface.request_timeout_ms = extract_string(estimator, "request_timeout_ms", "2000");
    p.transformation.estimator_interface.request_json_path = extract_string(estimator, "request_json_path", "config/estimator_interface_request.json");
    p.transformation.estimator_interface.response_json_path = extract_string(estimator, "response_json_path", "output/estimator_energy_report.json");

    p.event_store.sqlite_path = extract_string(event_store, "sqlite_path", p.event_store.sqlite_path);
    p.runtime_service.model_path = extract_string(runtime_service, "model_path", p.training.model_output_path);
    p.runtime_service.polling_interval_seconds = extract_int(runtime_service, "polling_interval_seconds", p.runtime_service.polling_interval_seconds);
    p.runtime_service.max_cycles = extract_int(runtime_service, "max_cycles", p.runtime_service.max_cycles);
    p.runtime_service.stop_file_path = extract_string(runtime_service, "stop_file_path", p.runtime_service.stop_file_path);

    return p;
}

void save_controller_model(const ControllerModel& model, const std::string& path) {
    ensure_parent(path);
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Could not open model output path: " + path);
    out << "{\n";
    out << "  \"default_breed\": \"" << model.default_breed.name << "\",\n";
    out << "  \"trained_breeds\": [";
    for (std::size_t i = 0; i < model.trained_breeds.size(); ++i) {
        if (i) out << ", ";
        out << "\"" << model.trained_breeds[i] << "\"";
    }
    out << "],\n";
    out << "  \"horizon_steps\": " << model.horizon_steps << ",\n";
    out << "  \"initial_body_weight_kg\": " << model.cattle.initial_body_weight_kg << ",\n";
    out << "  \"gain_when_comfortable_kg_per_day\": " << model.cattle.weight_rule.gain_when_comfortable_kg_per_day << ",\n";
    out << "  \"loss_when_stressed_kg_per_day\": " << model.cattle.weight_rule.loss_when_stressed_kg_per_day << ",\n";
    out << "  \"weights\": {\n";
    out << "    \"weight_gain_reward\": " << model.weights.weight_gain_reward << ",\n";
    out << "    \"comfort_penalty\": " << model.weights.comfort_penalty << ",\n";
    out << "    \"energy_penalty\": " << model.weights.energy_penalty << ",\n";
    out << "    \"humidity_penalty\": " << model.weights.humidity_penalty << ",\n";
    out << "    \"wind_penalty\": " << model.weights.wind_penalty << ",\n";
    out << "    \"actuator_change_penalty\": " << model.weights.actuator_change_penalty << "\n";
    out << "  }\n";
    out << "}\n";
}

ControllerModel load_controller_model(const std::string& path) {
    const std::string text = read_text_file(path);
    ControllerModel m{};
    m.default_breed = breed_from_name(extract_string(text, "default_breed", extract_string(text, "breed", "charolais")));
    m.trained_breeds = extract_string_array(text, "trained_breeds");
    if (m.trained_breeds.empty()) m.trained_breeds.push_back(m.default_breed.name);
    m.horizon_steps = extract_int(text, "horizon_steps", 4);
    m.cattle.initial_body_weight_kg = extract_number(text, "initial_body_weight_kg", 450.0);
    m.cattle.weight_rule.gain_when_comfortable_kg_per_day = extract_number(text, "gain_when_comfortable_kg_per_day", 0.10);
    m.cattle.weight_rule.loss_when_stressed_kg_per_day = extract_number(text, "loss_when_stressed_kg_per_day", 0.08);
    const std::string w = extract_object_optional(text, "weights");
    m.weights.weight_gain_reward = extract_number(w, "weight_gain_reward", 200.0);
    m.weights.comfort_penalty = extract_number(w, "comfort_penalty", 150.0);
    m.weights.energy_penalty = extract_number(w, "energy_penalty", 20.0);
    m.weights.humidity_penalty = extract_number(w, "humidity_penalty", 10.0);
    m.weights.wind_penalty = extract_number(w, "wind_penalty", 8.0);
    m.weights.actuator_change_penalty = extract_number(w, "actuator_change_penalty", 0.1);
    return m;
}

int train_controller(const ProjectSettings& project) {
    initialize_event_store(project.event_store);
    const WeatherDataset dataset = WeatherDataset::load_csv(project.training.dataset_path);
    ControllerModel model{};
    model.default_breed = breed_from_name(project.cattle.breed);
    model.weights = project.reward_weights;
    model.cattle = project.cattle;
    model.horizon_steps = project.training.horizon_steps;

    const auto training_herd_template = make_default_herd(project);
    std::set<std::string> trained_breed_names;
    for (const auto& cow : training_herd_template) trained_breed_names.insert(cow.breed.name);
    model.trained_breeds.assign(trained_breed_names.begin(), trained_breed_names.end());
    if (!training_herd_template.empty()) {
        model.default_breed = training_herd_template.front().breed;
        model.cattle.initial_body_weight_kg = training_herd_template.front().body_weight_kg;
        model.cattle.weight_rule = training_herd_template.front().weight_rule;
    }

    const int start = static_cast<int>(resolve_datetime_index(dataset, project.training.start_datetime, false));
    const int dataset_end = static_cast<int>(resolve_datetime_index(dataset, project.training.end_datetime, true));
    if (dataset_end < start) throw std::runtime_error("Training end_datetime is earlier than start_datetime.");
    const int selected_steps = dataset_end - start + 1;
    const int train_steps = project.training.steps_per_episode > 0 ? std::min(project.training.steps_per_episode, selected_steps) : selected_steps;

    for (int episode = 0; episode < project.training.episodes; ++episode) {
        SimulationState current{};
        bool has_state = false;
        RLAction previous{};
        std::vector<HerdCow> herd = training_herd_template;
        double reward_sum = 0.0, comfort_sum = 0.0, energy_sum = 0.0, weight_sum = 0.0;

        print_training_progress(episode + 1, project.training.episodes, 0, train_steps, 0.0, 0.0, 0.0, mean_body_weight(herd), herd.size());
        for (int step = 0; step < train_steps; ++step) {
            const int idx = start + step;
            const auto& sample = dataset.at(static_cast<std::size_t>(idx));
            const auto [action, predicted_score] = choose_action(model, project.simulator, dataset, idx, has_state ? &current : nullptr, herd, previous);
            (void)predicted_score;
            const SimulationSettings sim_settings = settings_from_weather(project.simulator, sample, has_state ? &current : nullptr);
            const SingleZoneSimulator simulator(sim_settings);
            const SimulationResult result = simulator.estimate(to_command(action, sim_settings.timestep_seconds));
            current = result.states.back();
            has_state = true;
            current.minute = static_cast<double>(step) * sim_settings.timestep_seconds / 60.0;

            double reward_total = 0.0;
            double comfort_total = 0.0;
            double weight_delta_total = 0.0;
            for (auto& cow : herd) {
                reward_total += step_reward_for_cow(model, cow, sample, current, action, previous);
                const TNZRange tnz = tnz_for_cow(cow, sample, current);
                comfort_total += range_error(current.indoor_temp_c, tnz.lct_c, tnz.uct_c);
                const double weight_delta = comfort_weight_delta_kg(tnz, cow.weight_rule, 1.0);
                cow.body_weight_kg += weight_delta;
                weight_delta_total += weight_delta;
            }
            const double herd_denom = static_cast<double>(std::max<std::size_t>(1, herd.size()));
            reward_sum += reward_total / herd_denom;
            comfort_sum += comfort_total / herd_denom;
            const double step_energy = (current.diagnostics.damper_electrical_power_w + current.diagnostics.fan_electrical_power_w + current.diagnostics.heater_electrical_power_w) / 1000.0;
            energy_sum += step_energy;
            weight_sum += weight_delta_total / herd_denom;
            previous = action;
            const double step_denom = static_cast<double>(step + 1);
            print_training_progress(episode + 1, project.training.episodes, step + 1, train_steps,
                                    reward_sum / step_denom, comfort_sum / step_denom, energy_sum / step_denom,
                                    mean_body_weight(herd), herd.size());
        }

        const double denom = static_cast<double>(std::max(1, train_steps));
        update_weights(model, comfort_sum / denom, energy_sum / denom, weight_sum / denom);
        std::cout << "episode=" << (episode + 1)
                  << " reward_sum=" << reward_sum
                  << " avg_comfort_error=" << (comfort_sum / denom)
                  << " avg_energy_kwh=" << (energy_sum / denom)
                  << " mean_body_weight_kg=" << mean_body_weight(herd)
                  << " herd_size=" << herd.size()
                  << " trained_breeds=";
        for (std::size_t i = 0; i < model.trained_breeds.size(); ++i) {
            if (i) std::cout << '+';
            std::cout << model.trained_breeds[i];
        }
        std::cout << " comfort_penalty=" << model.weights.comfort_penalty
                  << " energy_penalty=" << model.weights.energy_penalty
                  << '\n';
    }

    save_controller_model(model, project.training.model_output_path);
    std::cout << "model_saved=" << project.training.model_output_path << '\n';
    return 0;
}

std::vector<double> load_manual_daily_weights(const std::string& path) {
    std::vector<double> out;
    if (path.empty()) return out;
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Could not open manual weight file: " + path);
    std::string line;
    while (std::getline(in, line)) {
        const auto pos = line.find('#');
        if (pos != std::string::npos) line = line.substr(0, pos);
        std::stringstream ss(line);
        double v = 0.0;
        if (ss >> v) out.push_back(v);
    }
    return out;
}

std::string write_validation_csv(const std::vector<ValidationRecord>& history, const std::string& path) {
    ensure_parent(path);
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Could not open validation CSV output: " + path);
    out << "step,datetime,minute,body_weight_kg,herd_size,outdoor_temp_c,outdoor_rh,outdoor_wind_m_s,solar_w_m2,precip_mm,cloud_fraction,indoor_temp_c,indoor_rh,indoor_airflow_m3_s,internal_heat_w,lct_c,uct_c,reward,weight_delta_kg,damper_percent,fan_percent,heater_percent\n";
    out << std::fixed << std::setprecision(6);
    for (const auto& r : history) {
        out << r.step_index << ',' << r.dt_iso << ',' << r.minute << ',' << r.body_weight_kg << ',' << r.herd_size << ','
            << r.outdoor_temp_c << ',' << r.outdoor_relative_humidity << ',' << r.outdoor_wind_speed_m_s << ','
            << r.solar_radiation_w_m2 << ',' << r.precipitation_mm << ',' << r.cloud_cover_fraction << ','
            << r.indoor_temp_c << ',' << r.indoor_relative_humidity << ',' << r.indoor_airflow_m3_s << ','
            << r.internal_heat_generated_w << ',' << r.lct_c << ',' << r.uct_c << ',' << r.reward << ','
            << r.weight_delta_kg << ',' << r.action.damper_percent << ',' << r.action.fan_percent << ',' << r.action.heater_percent << '\n';
    }
    return path;
}

std::string write_validation_svg(const std::vector<ValidationRecord>& history, const std::string& path) {
    ensure_parent(path);
    if (history.empty()) throw std::runtime_error("Validation SVG requires non-empty history.");
    const auto x = collect_series(history, "minute");
    const double xmin = 0.0;
    const double xmax = x.back();
    const double W = 1600.0, H = 2050.0;
    const double left = 90.0, right = 180.0, top = 50.0, plot_h = 180.0, gap = 30.0;
    const double plot_w = W - left - right;
    std::ofstream out(path);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << W << "\" height=\"" << H << "\" viewBox=\"0 0 " << W << ' ' << H << "\">\n";
    out << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
    out << "<text x=\"" << W/2 << "\" y=\"25\" text-anchor=\"middle\" font-size=\"22\" font-family=\"Arial\">cattle_farming_env_rl_mpc validation report</text>\n";
    struct Panel { std::string title; std::vector<std::pair<std::string,std::vector<double>>> series; double y0; double y1; };
    const std::vector<Panel> panels = {
        {"Temperature [C]", {{"#0055cc", collect_series(history, "outdoor_temp")}, {"#d62728", collect_series(history, "indoor_temp")}, {"#2ca02c", collect_series(history, "lct")}, {"#ff7f0e", collect_series(history, "uct")}}, 0.0, 0.0},
        {"Indoor RH [-]", {{"#d62728", collect_series(history, "rh")}}, 0.0, 1.0},
        {"Wind speed [m/s]", {{"#1f77b4", collect_series(history, "wind")}}, 0.0, 0.0},
        {"Solar radiation [W/m2]", {{"#ff7f0e", collect_series(history, "solar")}}, 0.0, 0.0},
        {"Internal heat [W]", {{"#9467bd", collect_series(history, "heat")}}, 0.0, 0.0},
        {"Damper actuator [%]", {{"#8c564b", collect_series(history, "damper")}}, 0.0, 100.0},
        {"Fan actuator [%]", {{"#17becf", collect_series(history, "fan")}}, 0.0, 100.0},
        {"Heater actuator [%]", {{"#7f7f7f", collect_series(history, "heater")}}, 0.0, 100.0}
    };
    for (std::size_t i = 0; i < panels.size(); ++i) {
        const double y = top + i * (plot_h + gap);
        double ymin = std::numeric_limits<double>::infinity();
        double ymax = -std::numeric_limits<double>::infinity();
        for (const auto& s : panels[i].series) {
            ymin = std::min(ymin, minv(s.second));
            ymax = std::max(ymax, maxv(s.second));
        }
        if (std::isfinite(panels[i].y0) && panels[i].y1 > panels[i].y0) { ymin = panels[i].y0; ymax = panels[i].y1; }
        if (ymax - ymin < 1e-9) { ymax += 1.0; ymin -= 1.0; }
        out << "<text x=\"" << left << "\" y=\"" << (y + 16) << "\" font-size=\"14\" font-family=\"Arial\">" << panels[i].title << "</text>\n";
        out << "<rect x=\"" << left << "\" y=\"" << (y + 25) << "\" width=\"" << plot_w << "\" height=\"" << plot_h << "\" fill=\"none\" stroke=\"black\"/>\n";
        out << "<text x=\"" << (left - 8) << "\" y=\"" << (y + 38) << "\" text-anchor=\"end\" font-size=\"11\">" << std::fixed << std::setprecision(2) << ymax << "</text>\n";
        out << "<text x=\"" << (left - 8) << "\" y=\"" << (y + plot_h + 25) << "\" text-anchor=\"end\" font-size=\"11\">" << std::fixed << std::setprecision(2) << ymin << "</text>\n";
        for (const auto& s : panels[i].series) out << polyline(x, s.second, left, y + 25, plot_w, plot_h, xmin, xmax, ymin, ymax, s.first) << '\n';
        out << "<text x=\"" << left << "\" y=\"" << (y + plot_h + 42) << "\" font-size=\"11\">0 min</text>\n";
        out << "<text x=\"" << (left + plot_w) << "\" y=\"" << (y + plot_h + 42) << "\" text-anchor=\"end\" font-size=\"11\">" << xmax << " min</text>\n";
        double legend_y = y + 40;
        for (const auto& s : panels[i].series) {
            out << "<line x1=\"" << (left + plot_w + 10) << "\" y1=\"" << legend_y << "\" x2=\"" << (left + plot_w + 30) << "\" y2=\"" << legend_y << "\" stroke=\"" << s.first << "\" stroke-width=\"2\"/>\n";
            out << "<text x=\"" << (left + plot_w + 35) << "\" y=\"" << (legend_y + 4) << "\" font-size=\"11\">";
            if (panels[i].title.find("Temperature") != std::string::npos) {
                if (s.first == "#0055cc") out << "outdoor";
                else if (s.first == "#d62728") out << "indoor";
                else if (s.first == "#2ca02c") out << "&lt;TNZ lower";
                else out << "&gt;TNZ upper";
            } else if (panels[i].title.find("Damper actuator") != std::string::npos) {
                out << "damper setpoint";
            } else if (panels[i].title.find("Fan actuator") != std::string::npos) {
                out << "fan setpoint";
            } else if (panels[i].title.find("Heater actuator") != std::string::npos) {
                out << "heater setpoint";
            } else {
                out << "series";
            }
            out << "</text>\n";
            legend_y += 16;
        }
    }
    out << "</svg>\n";
    return path;
}

int validate_controller(const ProjectSettings& project, const std::string& model_path,
                        bool automatic_weight_update, const std::string& manual_weight_file,
                        const std::string& start_datetime, const std::string& end_datetime,
                        const std::string& csv_path, const std::string& svg_path,
                        bool use_simulat_weights) {
    initialize_event_store(project.event_store);
    const WeatherDataset dataset = WeatherDataset::load_csv(project.training.dataset_path);
    ControllerModel model = load_controller_model(model_path);
    const int start = static_cast<int>(resolve_datetime_index(dataset, start_datetime, false));
    const int finish = static_cast<int>(resolve_datetime_index(dataset, end_datetime, true));
    if (finish < start) throw std::runtime_error("Validation end_datetime is earlier than start_datetime.");
    const auto manual_weights = automatic_weight_update ? std::vector<double>{} : load_manual_daily_weights(manual_weight_file);
    const auto snapshots = project.grpc_weight_feed.enabled ? load_herd_snapshots(project.grpc_weight_feed.snapshot_json_path, project.cattle.weight_rule) : std::vector<HerdSnapshot>{};

    std::vector<ValidationRecord> history;
    SimulationState current{};
    bool has_state = false;
    RLAction previous{};
    std::vector<HerdCow> herd = make_default_herd(project);
    std::size_t manual_day_index = 0;

    for (int idx = start; idx <= finish; ++idx) {
        const auto& sample = dataset.at(static_cast<std::size_t>(idx));
        const std::string current_dt = normalize_datetime_prefix(sample.dt_iso);
        if (use_simulat_weights) {
            load_herd_from_transformation(herd, project.transformation, project.cattle.weight_rule, current_dt);
        } else if (project.grpc_weight_feed.enabled && !project.grpc_weight_feed.snapshot_json_path.empty()) {
            apply_snapshot_for_datetime(herd, snapshots, current_dt);
        }

        const auto [action, score] = choose_action(model, project.simulator, dataset, idx, has_state ? &current : nullptr, herd, previous);
        const SimulationSettings sim_settings = settings_from_weather(project.simulator, sample, has_state ? &current : nullptr);
        const SingleZoneSimulator simulator(sim_settings);
        const SimulationResult result = simulator.estimate(to_command(action, sim_settings.timestep_seconds));
        current = result.states.back();
        has_state = true;
        current.minute = static_cast<double>(idx - start) * sim_settings.timestep_seconds / 60.0;

        double weight_delta_total = 0.0;
        for (auto& cow : herd) {
            const TNZRange tnz = tnz_for_cow(cow, sample, current);
            double weight_delta = automatic_weight_update ? comfort_weight_delta_kg(tnz, cow.weight_rule, 1.0) : 0.0;
            if (!automatic_weight_update && ((idx - start + 1) % 24 == 0) && manual_day_index < manual_weights.size()) {
                const double mean_before = mean_body_weight(herd);
                const double mean_target = manual_weights[manual_day_index++];
                weight_delta = mean_target - mean_before;
            }
            cow.body_weight_kg += weight_delta;
            weight_delta_total += weight_delta;
        }

        const auto [lct_mean, uct_mean] = mean_tnz_bounds(herd, sample, current, model.default_breed);
        ValidationRecord rec{};
        rec.step_index = static_cast<std::size_t>(idx - start);
        rec.dt_iso = sample.dt_iso;
        rec.minute = current.minute;
        rec.body_weight_kg = mean_body_weight(herd);
        rec.herd_size = static_cast<double>(herd.size());
        rec.outdoor_temp_c = sample.outdoor_temp_c;
        rec.outdoor_relative_humidity = sample.relative_humidity;
        rec.outdoor_wind_speed_m_s = sample.wind_speed_m_s;
        rec.solar_radiation_w_m2 = sample.direct_radiation_w_m2 + sample.diffuse_radiation_w_m2;
        rec.precipitation_mm = sample.precipitation_mm;
        rec.cloud_cover_fraction = sample.cloud_cover_fraction;
        rec.indoor_temp_c = current.indoor_temp_c;
        rec.indoor_relative_humidity = current.indoor_relative_humidity;
        rec.indoor_airflow_m3_s = current.diagnostics.total_ventilation_flow_m3_s;
        rec.internal_heat_generated_w = project.simulator.internal_loads.base_sensible_gains_w + current.diagnostics.heater_gain_w;
        rec.lct_c = lct_mean;
        rec.uct_c = uct_mean;
        rec.reward = score;
        rec.weight_delta_kg = herd.empty() ? 0.0 : (weight_delta_total / static_cast<double>(herd.size()));
        rec.action = action;
        rec.state = current;
        history.push_back(rec);
        log_event_record(project.event_store, make_event_log_record("validate", sample.dt_iso, action, current, sample, score, rec.body_weight_kg, rec.herd_size, lct_mean, uct_mean, sim_settings.timestep_seconds, rec.internal_heat_generated_w));
        previous = action;
    }

    write_validation_csv(history, csv_path);
    write_validation_svg(history, svg_path);
    std::cout << "validation_csv=" << csv_path << '\n';
    std::cout << "validation_svg=" << svg_path << '\n';
    if (use_simulat_weights || project.grpc_weight_feed.enabled) {
        std::cout << "ligaps_beef_endpoint=" << project.transformation.ligaps_beef_weights.endpoint << '\n';
        std::cout << "ligaps_beef_response_json_path=" << project.transformation.ligaps_beef_weights.response_json_path << '\n';
    }
    return 0;
}

int operate_controller(const ProjectSettings& project, const std::string& model_path,
                       const std::string& start_datetime, const std::string& end_datetime,
                       bool use_real_weights) {
    initialize_event_store(project.event_store);
    const WeatherDataset dataset = WeatherDataset::load_csv(project.training.dataset_path);
    const ControllerModel model = load_controller_model(model_path);
    const int start = static_cast<int>(resolve_datetime_index(dataset, start_datetime, false));
    const int finish = static_cast<int>(resolve_datetime_index(dataset, end_datetime, true));
    if (finish < start) throw std::runtime_error("Operate end_datetime is earlier than start_datetime.");

    std::vector<HerdCow> herd = make_default_herd(project);
    RLAction previous{};
    SimulationState current{};
    bool has_state = false;

    for (int idx = start; idx <= finish; ++idx) {
        WeatherSample sample = dataset.at(static_cast<std::size_t>(idx));
        if (use_real_weights) {
            load_herd_from_transformation(herd, project.transformation, project.cattle.weight_rule, sample.dt_iso);
        }
        sample = resolve_operational_weather(sample, project.transformation, sample.dt_iso);
        const auto [action, score] = choose_action(model, project.simulator, dataset, idx, has_state ? &current : nullptr, herd, previous);
        const SimulationSettings sim_settings = settings_from_weather(project.simulator, sample, has_state ? &current : nullptr);
        const SingleZoneSimulator simulator(sim_settings);
        const SimulationResult result = simulator.estimate(to_command(action, sim_settings.timestep_seconds));
        current = result.states.back();
        has_state = true;

        const auto [lct_mean, uct_mean] = mean_tnz_bounds(herd, sample, current, model.default_breed);
        ActuatorDispatchRecord rec{};
        rec.effective_datetime = sample.dt_iso;
        rec.action = action;
        rec.predicted_reward = score;
        rec.mean_body_weight_kg = mean_body_weight(herd);
        rec.herd_size = static_cast<double>(herd.size());
        rec.lct_c = lct_mean;
        rec.uct_c = uct_mean;
        if (project.transformation.actuators_interface.enabled) {
            write_actuator_dispatch_record(project.transformation.actuators_interface.response_json_path, rec, project.transformation.actuators_interface);
        }
        log_event_record(project.event_store, make_event_log_record("operate", sample.dt_iso, action, current, sample, score, rec.mean_body_weight_kg, rec.herd_size, lct_mean, uct_mean, sim_settings.timestep_seconds, project.simulator.internal_loads.base_sensible_gains_w + current.diagnostics.heater_gain_w));
        previous = action;
    }

    std::cout << "operate_model=" << model_path << '\n';
    if (use_real_weights) std::cout << "ligaps_beef_response_json_path=" << project.transformation.ligaps_beef_weights.response_json_path << '\n';
    if (project.transformation.sensors_interface.enabled) std::cout << "sensors_interface_response_json_path=" << project.transformation.sensors_interface.response_json_path << '\n';
    if (project.transformation.actuators_interface.enabled) std::cout << "actuators_dispatch_json_path=" << project.transformation.actuators_interface.response_json_path << '\n';
    return 0;
}

int export_estimator_report(const ProjectSettings& project, const std::string& start_datetime,
                            const std::string& end_datetime, int interval_minutes,
                            const std::string& mode_filter, const std::string& output_path) {
    initialize_event_store(project.event_store);
    const auto rows = query_estimator_aggregate(project.event_store, start_datetime, end_datetime, interval_minutes, mode_filter);
    EstimatorAggregateDispatch dispatch{};
    dispatch.generated_at = end_datetime;
    dispatch.start_datetime = start_datetime;
    dispatch.end_datetime = end_datetime;
    dispatch.interval_minutes = interval_minutes;
    dispatch.mode_filter = mode_filter;
    dispatch.source_sqlite_path = project.event_store.sqlite_path;
    for (const auto& row : rows) {
        dispatch.rows.push_back(EstimatorAggregateDispatch::Row{row.interval_start, row.interval_end, row.decision_count, row.damper_energy_kwh, row.fan_energy_kwh, row.heater_energy_kwh, row.total_energy_kwh, row.mean_body_weight_kg, row.mean_indoor_temp_c});
    }
    const std::string path = output_path.empty() ? project.transformation.estimator_interface.response_json_path : output_path;
    write_estimator_dispatch_record(path, dispatch, project.transformation.estimator_interface);
    std::cout << "estimator_endpoint=" << project.transformation.estimator_interface.endpoint << '\n';
    std::cout << "estimator_response_json_path=" << path << '\n';
    return 0;
}

int export_ligaps_climate_report(const ProjectSettings& project, const std::string& query_date,
                                 const std::string& mode_filter, const std::string& output_path) {
    initialize_event_store(project.event_store);
    const auto rows = query_day_night_climate_aggregate(project.event_store, query_date, mode_filter);
    LiGAPSDayNightClimateDispatch dispatch{};
    dispatch.generated_at = query_date + " 23:59:59";
    dispatch.query_date = query_date;
    dispatch.source_sqlite_path = project.event_store.sqlite_path;
    dispatch.mode_filter = mode_filter;
    for (const auto& row : rows) {
        dispatch.periods.push_back(LiGAPSDayNightClimateDispatch::PeriodSummary{row.period_label, row.sample_count, row.mean_indoor_temp_c, row.mean_indoor_relative_humidity, row.mean_indoor_airflow_m3_s, row.mean_internal_heat_generated_w});
    }
    const std::string path = output_path.empty() ? project.transformation.ligaps_beef_optimizer.response_json_path : output_path;
    write_ligaps_climate_dispatch_record(path, dispatch, project.transformation.ligaps_beef_optimizer);
    std::cout << "ligaps_beef_optimizer_endpoint=" << project.transformation.ligaps_beef_optimizer.endpoint << '\n';
    std::cout << "ligaps_beef_optimizer_response_json_path=" << path << '\n';
    return 0;
}

int run_controller_service(const ProjectSettings& project, const std::string& model_path) {
    initialize_event_store(project.event_store);
    const WeatherDataset dataset = WeatherDataset::load_csv(project.training.dataset_path);
    const ControllerModel model = load_controller_model(model_path.empty() ? project.runtime_service.model_path : model_path);
    const int data_size = static_cast<int>(dataset.size());
    if (data_size <= 0) throw std::runtime_error("Runtime service requires a non-empty dataset.");
    std::remove(project.runtime_service.stop_file_path.c_str());

    std::vector<HerdCow> herd = make_default_herd(project);
    RLAction previous{};
    SimulationState current{};
    bool has_state = false;
    int cycle_count = 0;

    while (true) {
        if (file_exists(project.runtime_service.stop_file_path)) break;
        const std::string current_dt = now_datetime_string();
        WeatherSample sample = dataset.at(static_cast<std::size_t>(cycle_count % data_size));
        sample.dt_iso = current_dt;
        load_herd_from_transformation(herd, project.transformation, project.cattle.weight_rule, current_dt);
        sample = resolve_operational_weather(sample, project.transformation, current_dt);
        const auto [action, score] = choose_action(model, project.simulator, dataset, cycle_count % data_size, has_state ? &current : nullptr, herd, previous);
        const SimulationSettings sim_settings = settings_from_weather(project.simulator, sample, has_state ? &current : nullptr);
        const SingleZoneSimulator simulator(sim_settings);
        const SimulationResult result = simulator.estimate(to_command(action, sim_settings.timestep_seconds));
        current = result.states.back();
        has_state = true;
        const auto [lct_mean, uct_mean] = mean_tnz_bounds(herd, sample, current, model.default_breed);
        const double mean_bw = mean_body_weight(herd);
        const double herd_size = static_cast<double>(herd.size());
        log_realtime_event_record(project.event_store, make_event_log_record("run", current_dt, action, current, sample, score, mean_bw, herd_size, lct_mean, uct_mean, sim_settings.timestep_seconds, project.simulator.internal_loads.base_sensible_gains_w + current.diagnostics.heater_gain_w));
        if (project.transformation.actuators_interface.enabled) {
            ActuatorDispatchRecord rec{}; rec.effective_datetime = current_dt; rec.action = action; rec.predicted_reward = score; rec.mean_body_weight_kg = mean_bw; rec.herd_size = herd_size; rec.lct_c = lct_mean; rec.uct_c = uct_mean;
            write_actuator_dispatch_record(service_temp_path("actuators_dispatch.json"), rec, project.transformation.actuators_interface);
        }
        if (project.transformation.estimator_interface.enabled && file_exists(project.transformation.estimator_interface.request_json_path)) {
            try {
                EstimatorRequest req = load_estimator_request(project.transformation.estimator_interface.request_json_path);
                const std::string start = req.start_datetime.empty() ? current_dt : req.start_datetime;
                const std::string end = req.end_datetime.empty() ? current_dt : req.end_datetime;
                const auto rows = query_estimator_aggregate(project.event_store, start, end, req.interval_minutes, "run");
                EstimatorAggregateDispatch dispatch{}; dispatch.generated_at = current_dt; dispatch.start_datetime = start; dispatch.end_datetime = end; dispatch.interval_minutes = req.interval_minutes; dispatch.mode_filter = "run"; dispatch.source_sqlite_path = project.event_store.sqlite_path;
                for (const auto& row : rows) dispatch.rows.push_back(EstimatorAggregateDispatch::Row{row.interval_start,row.interval_end,row.decision_count,row.damper_energy_kwh,row.fan_energy_kwh,row.heater_energy_kwh,row.total_energy_kwh,row.mean_body_weight_kg,row.mean_indoor_temp_c});
                write_estimator_dispatch_record(service_temp_path("estimator_energy_report.json"), dispatch, project.transformation.estimator_interface);
            } catch (...) {}
        }
        if (project.transformation.ligaps_beef_optimizer.enabled && file_exists(project.transformation.ligaps_beef_optimizer.request_json_path)) {
            try {
                LiGAPSClimateRequest req = load_ligaps_climate_request(project.transformation.ligaps_beef_optimizer.request_json_path);
                const std::string qd = req.query_date.empty() ? current_dt.substr(0,10) : req.query_date;
                const auto rows = query_day_night_climate_aggregate(project.event_store, qd, "run");
                LiGAPSDayNightClimateDispatch dispatch{}; dispatch.generated_at = current_dt; dispatch.query_date = qd; dispatch.source_sqlite_path = project.event_store.sqlite_path; dispatch.mode_filter = "run";
                for (const auto& row : rows) dispatch.periods.push_back(LiGAPSDayNightClimateDispatch::PeriodSummary{row.period_label,row.sample_count,row.mean_indoor_temp_c,row.mean_indoor_relative_humidity,row.mean_indoor_airflow_m3_s,row.mean_internal_heat_generated_w});
                write_ligaps_climate_dispatch_record(service_temp_path("ligaps_beef_optimizer_climate_report.json"), dispatch, project.transformation.ligaps_beef_optimizer);
            } catch (...) {}
        }
        previous = action;
        ++cycle_count;
        if (project.runtime_service.max_cycles > 0 && cycle_count >= project.runtime_service.max_cycles) break;
        std::this_thread::sleep_for(std::chrono::seconds(std::max(1, project.runtime_service.polling_interval_seconds)));
    }
    std::remove(project.runtime_service.stop_file_path.c_str());
    return 0;
}

int stop_controller_service(const ProjectSettings& project) {
    std::ofstream out(project.runtime_service.stop_file_path);
    if (!out) throw std::runtime_error("Could not create stop file: " + project.runtime_service.stop_file_path);
    out << now_datetime_string() << "\n";
    return 0;
}

} // namespace cattle_climate
