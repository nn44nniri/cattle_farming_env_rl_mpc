#include "cattle_climate/env.hpp"
#include "cattle_climate/settings.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

namespace {

bool finite_or_throw(double value, const char* name) {
    if (!std::isfinite(value)) {
        throw std::runtime_error(std::string("Non-finite value detected for ") + name);
    }
    return true;
}

double parse_double(const std::string& text, const std::string& option_name) {
    try {
        std::size_t idx = 0;
        const double value = std::stod(text, &idx);
        if (idx != text.size()) {
            throw std::runtime_error("");
        }
        return value;
    } catch (...) {
        throw std::runtime_error("Invalid numeric value for " + option_name + ": " + text);
    }
}

std::size_t parse_size(const std::string& text, const std::string& option_name) {
    const double value = parse_double(text, option_name);
    if (value < 0.0) {
        throw std::runtime_error("Expected a non-negative integer for " + option_name);
    }
    return static_cast<std::size_t>(value);
}

void print_help(const std::string& exe_name) {
    std::cout
        << "Usage: " << exe_name << " [options]\n\n"
        << "Options:\n"
        << "  --settings <path>               JSON settings path (default: config/settings.json)\n"
        << "  --episodes <n>                  Number of random-test episodes (default: 3)\n"
        << "  --steps <n>                     Steps per episode / RL horizon (default: 12)\n"
        << "  --seed <n>                      RNG seed (default: 123456)\n"
        << "  --outside-temp-c <value>        Override outside temperature\n"
        << "  --outside-rh <0..1>             Override outside relative humidity\n"
        << "  --outside-wind-speed-m-s <v>    Override outside wind speed\n"
        << "  --initial-indoor-temp-c <v>     Override initial indoor temperature\n"
        << "  --initial-indoor-rh <0..1>      Override initial indoor relative humidity\n"
        << "  --temp-min <v>                  Target lower temperature bound\n"
        << "  --temp-max <v>                  Target upper temperature bound\n"
        << "  --rh-min <v>                    Target lower relative humidity bound\n"
        << "  --rh-max <v>                    Target upper relative humidity bound\n"
        << "  --airflow-min <v>               Target lower airflow bound\n"
        << "  --airflow-max <v>               Target upper airflow bound\n"
        << "  --damper-max <0..100>           Max random damper action (default: 100)\n"
        << "  --fan-max <0..100>              Max random fan action (default: 100)\n"
        << "  --heater-max <0..100>           Max random heater action (default: 100)\n"
        << "  --csv <path>                    Write RL rollout CSV from the last episode\n"
        << "  --graph <path>                  Write RL rollout SVG graph from the last episode\n"
        << "  --graph-max-minutes <value>     Limit RL graph horizon\n"
        << "  --help                          Show this message\n\n"
        << "Example:\n  " << exe_name
        << " --settings config/settings.json --episodes 2 --steps 30 --outside-temp-c 32 --fan-max 80"
        << " --csv build/rl_rollout.csv --graph build/rl_rollout.svg\n";
}

} // namespace

int main(int argc, char** argv) {
    using namespace cattle_climate;

    try {
        std::string settings_path = (argc > 1) ? argv[1] : "config/settings.json";
        bool settings_explicit = false;
        std::size_t episodes = 3;
        std::size_t steps = 12;
        unsigned int seed = 123456u;
        double damper_max = 100.0;
        double fan_max = 100.0;
        double heater_max = 100.0;
        bool has_outside_temp_override = false;
        bool has_outside_rh_override = false;
        bool has_outside_wind_override = false;
        bool has_initial_temp_override = false;
        bool has_initial_rh_override = false;
        bool has_graph_max_minutes = false;
        double outside_temp_override = 0.0;
        double outside_rh_override = 0.0;
        double outside_wind_override = 0.0;
        double initial_temp_override = 0.0;
        double initial_rh_override = 0.0;
        double graph_max_minutes = 0.0;
        std::string csv_path;
        std::string graph_path;

        RLEnvironmentOptions options{};
        options.max_episode_steps = steps;
        options.targets.indoor_temp_c = {8.0, 22.0};
        options.targets.indoor_relative_humidity = {0.45, 0.85};
        options.targets.airflow_m3_s = {0.02, 2.50};

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto require_value = [&](const std::string& option_name) -> std::string {
                if (i + 1 >= argc) {
                    throw std::runtime_error("Missing value after " + option_name);
                }
                return argv[++i];
            };

            if (arg == "--help") {
                print_help(argv[0]);
                return 0;
            } else if (arg == "--settings") {
                settings_path = require_value(arg);
                settings_explicit = true;
            } else if (!settings_explicit && i == 1 && !arg.empty() && arg.rfind("--", 0) != 0) {
                settings_path = arg;
            } else if (arg == "--episodes") {
                episodes = parse_size(require_value(arg), arg);
            } else if (arg == "--steps") {
                steps = parse_size(require_value(arg), arg);
                options.max_episode_steps = steps;
            } else if (arg == "--seed") {
                seed = static_cast<unsigned int>(parse_size(require_value(arg), arg));
            } else if (arg == "--outside-temp-c") {
                outside_temp_override = parse_double(require_value(arg), arg);
                has_outside_temp_override = true;
            } else if (arg == "--outside-rh") {
                outside_rh_override = parse_double(require_value(arg), arg);
                has_outside_rh_override = true;
            } else if (arg == "--outside-wind-speed-m-s") {
                outside_wind_override = parse_double(require_value(arg), arg);
                has_outside_wind_override = true;
            } else if (arg == "--initial-indoor-temp-c") {
                initial_temp_override = parse_double(require_value(arg), arg);
                has_initial_temp_override = true;
            } else if (arg == "--initial-indoor-rh") {
                initial_rh_override = parse_double(require_value(arg), arg);
                has_initial_rh_override = true;
            } else if (arg == "--temp-min") {
                options.targets.indoor_temp_c.min_value = parse_double(require_value(arg), arg);
            } else if (arg == "--temp-max") {
                options.targets.indoor_temp_c.max_value = parse_double(require_value(arg), arg);
            } else if (arg == "--rh-min") {
                options.targets.indoor_relative_humidity.min_value = parse_double(require_value(arg), arg);
            } else if (arg == "--rh-max") {
                options.targets.indoor_relative_humidity.max_value = parse_double(require_value(arg), arg);
            } else if (arg == "--airflow-min") {
                options.targets.airflow_m3_s.min_value = parse_double(require_value(arg), arg);
            } else if (arg == "--airflow-max") {
                options.targets.airflow_m3_s.max_value = parse_double(require_value(arg), arg);
            } else if (arg == "--damper-max") {
                damper_max = std::clamp(parse_double(require_value(arg), arg), 0.0, 100.0);
            } else if (arg == "--fan-max") {
                fan_max = std::clamp(parse_double(require_value(arg), arg), 0.0, 100.0);
            } else if (arg == "--heater-max") {
                heater_max = std::clamp(parse_double(require_value(arg), arg), 0.0, 100.0);
            } else if (arg == "--csv") {
                csv_path = require_value(arg);
            } else if (arg == "--graph") {
                graph_path = require_value(arg);
            } else if (arg == "--graph-max-minutes") {
                graph_max_minutes = parse_double(require_value(arg), arg);
                has_graph_max_minutes = true;
            } else {
                throw std::runtime_error("Unknown option: " + arg + ". Use --help for usage.");
            }
        }

        if (steps == 0 || episodes == 0) {
            throw std::runtime_error("Both --episodes and --steps must be greater than zero.");
        }
        if (options.targets.indoor_temp_c.max_value < options.targets.indoor_temp_c.min_value
            || options.targets.indoor_relative_humidity.max_value < options.targets.indoor_relative_humidity.min_value
            || options.targets.airflow_m3_s.max_value < options.targets.airflow_m3_s.min_value) {
            throw std::runtime_error("Target max values must be greater than or equal to target min values.");
        }

        SimulationSettings settings = load_settings_from_json(settings_path);
        if (has_outside_temp_override) {
            settings.climate.outside_temp_c = outside_temp_override;
        }
        if (has_outside_rh_override) {
            settings.climate.outside_relative_humidity = std::clamp(outside_rh_override, 0.0, 1.0);
        }
        if (has_outside_wind_override) {
            settings.climate.reference_wind_speed_m_s = std::max(0.0, outside_wind_override);
        }
        if (has_initial_temp_override) {
            settings.initial_indoor_temp_c = initial_temp_override;
        }
        if (has_initial_rh_override) {
            settings.initial_indoor_relative_humidity = std::clamp(initial_rh_override, 0.0, 1.0);
        }

        CattleVitalEnv env(settings, options);
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> damper_dist(0.0, damper_max);
        std::uniform_real_distribution<double> fan_dist(0.0, fan_max);
        std::uniform_real_distribution<double> heater_dist(0.0, heater_max);
        std::uniform_real_distribution<double> binary_dist(0.0, 1.0);

        double total_reward = 0.0;
        double last_episode_reward = 0.0;
        for (std::size_t episode = 0; episode < episodes; ++episode) {
            RLObservation obs = env.reset();
            finite_or_throw(obs.indoor_temp_c, "reset indoor_temp_c");
            finite_or_throw(obs.indoor_relative_humidity, "reset indoor_relative_humidity");

            double episode_reward = 0.0;
            for (std::size_t step = 0; step < options.max_episode_steps; ++step) {
                RLAction action{};
                action.damper_percent = (binary_dist(rng) > 0.30) ? damper_dist(rng) : 0.0;
                action.fan_percent = (binary_dist(rng) > 0.20) ? fan_dist(rng) : 0.0;
                action.heater_percent = (binary_dist(rng) > 0.65) ? heater_dist(rng) : 0.0;

                RLStepResult result = env.step(action);
                total_reward += result.reward;
                episode_reward += result.reward;

                finite_or_throw(result.observation.indoor_temp_c, "step indoor_temp_c");
                finite_or_throw(result.observation.indoor_relative_humidity, "step indoor_relative_humidity");
                finite_or_throw(result.observation.total_airflow_m3_s, "step total_airflow_m3_s");
                finite_or_throw(result.reward, "step reward");

                if (result.observation.indoor_relative_humidity < 0.0 || result.observation.indoor_relative_humidity > 1.5) {
                    throw std::runtime_error("Indoor relative humidity moved outside sanity bounds.");
                }
                if (step + 1 < options.max_episode_steps && result.truncated) {
                    throw std::runtime_error("Episode truncated earlier than expected.");
                }
                if (step + 1 == options.max_episode_steps && !result.truncated) {
                    throw std::runtime_error("Episode did not truncate at the configured horizon.");
                }
            }
            last_episode_reward = episode_reward;
        }

        if (!csv_path.empty()) {
            write_rl_csv_report(env.history(), csv_path);
        }
        if (!graph_path.empty()) {
            const double max_minutes = has_graph_max_minutes
                ? graph_max_minutes
                : (settings.timestep_seconds * static_cast<double>(options.max_episode_steps) / 60.0);
            write_rl_svg_graph_report(env.history(), graph_path, max_minutes);
        }

        std::cout << "random_env_test=PASS\n";
        std::cout << "settings_path=" << settings_path << "\n";
        std::cout << "episodes=" << episodes << "\n";
        std::cout << "steps_per_episode=" << options.max_episode_steps << "\n";
        std::cout << "seed=" << seed << "\n";
        std::cout << "outside_temp_c=" << settings.climate.outside_temp_c << "\n";
        std::cout << "outside_relative_humidity=" << settings.climate.outside_relative_humidity << "\n";
        std::cout << "outside_wind_speed_m_s=" << settings.climate.reference_wind_speed_m_s << "\n";
        std::cout << "initial_indoor_temp_c=" << settings.initial_indoor_temp_c << "\n";
        std::cout << "initial_indoor_relative_humidity=" << settings.initial_indoor_relative_humidity << "\n";
        std::cout << "history_size_last_episode=" << env.history().size() << "\n";
        std::cout << "last_episode_reward=" << last_episode_reward << "\n";
        std::cout << "total_reward=" << total_reward << "\n";
        if (!csv_path.empty()) {
            std::cout << "csv_path=" << csv_path << "\n";
        }
        if (!graph_path.empty()) {
            std::cout << "graph_path=" << graph_path << "\n";
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
}
