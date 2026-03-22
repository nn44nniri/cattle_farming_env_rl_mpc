#include "cattle_climate/simulator.hpp"

#include <algorithm>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

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

bool parse_bool_flag(const std::string& text) {
    if (text == "on" || text == "true" || text == "1") {
        return true;
    }
    if (text == "off" || text == "false" || text == "0") {
        return false;
    }
    throw std::runtime_error("Expected on/off, true/false, or 1/0 but got: " + text);
}

void print_help(const std::string& exe_name) {
    std::cout
        << "Usage: " << exe_name << " [options]\n\n"
        << "Options:\n"
        << "  --settings <path>                 JSON settings path (default: config/settings.json)\n"
        << "  --duration-seconds <value>        Total simulation duration in seconds\n"
        << "  --duration-minutes <value>        Total simulation duration in minutes\n"
        << "  --outside-temp-c <value>          Override outdoor temperature [deg C]\n"
        << "  --outside-rh <value>              Override outdoor relative humidity [0..1]\n"
        << "  --outside-wind-speed-m-s <value>  Override outdoor reference wind speed [m/s]\n"
        << "  --outside-wind-direction-deg <v>  Override outdoor wind direction [deg]\n"
        << "  --outside-direct-rad-w-m2 <v>     Override direct solar radiation [W/m^2]\n"
        << "  --outside-diffuse-rad-w-m2 <v>    Override diffuse solar radiation [W/m^2]\n"
        << "  --outside-co2-ppm <value>         Override outdoor CO2 concentration [ppm]\n"
        << "  --dampers <on|off>                Enable or disable dampers\n"
        << "  --damper-activity <0..100>        Damper activity percentage\n"
        << "  --dampers-active-seconds <value>  Damper active time in seconds\n"
        << "  --dampers-active-minutes <value>  Damper active time in minutes\n"
        << "  --fans <on|off>                   Enable or disable fans\n"
        << "  --fan-activity <0..100>           Fan activity percentage\n"
        << "  --fan-driver <0..100>             Alias for --fan-activity\n"
        << "  --fan-driver-percent <0..100>     Alias for --fan-activity\n"
        << "  --fans-active-seconds <value>     Fan active time in seconds\n"
        << "  --fans-active-minutes <value>     Fan active time in minutes\n"
        << "  --heaters <on|off>                Enable or disable heaters\n"
        << "  --heater-activity <0..100>        Heater activity percentage\n"
        << "  --heaters-active-seconds <value>  Heater active time in seconds\n"
        << "  --heaters-active-minutes <value>  Heater active time in minutes\n"
        << "  --csv <path>                      Output CSV path (default: simulation_output.csv)\n"
        << "  --graph <path>                    Write key-parameter SVG graph\n"
        << "  --graph-max-minutes <value>       Plot graph up to this minute horizon\n"
        << "  --help                            Show this message\n\n"
        << "Example: heater-only test at -10 C for 300 minutes\n"
        << "  " << exe_name
        << " --settings ../config/settings.json --duration-minutes 300 --outside-temp-c -10 "
        << "--dampers off --fans off --heaters on --heater-activity 100 --heaters-active-minutes 300\n\n"
        << "Example: fan-only hot test at 35 C with fan driver = 42% for 300 seconds\n"
        << "  " << exe_name
        << " --settings ../config/settings.json --duration-seconds 300 --outside-temp-c 35 "
        << "--dampers off --fans on --fan-driver 42 --fans-active-seconds 300 "
        << "--heaters off --csv fan_only_hot_300s.csv\n\n"
        << "Equivalent long run for 300 minutes:\n"
        << "  " << exe_name
        << " --settings ../config/settings.json --duration-minutes 300 --outside-temp-c 35 "
        << "--dampers off --fans on --fan-driver 42 --fans-active-minutes 300 "
        << "--heaters off --csv fan_only_hot_300m.csv\n";
}

} // namespace

int main(int argc, char** argv) {
    using namespace cattle_climate;
    try {
        std::string settings_path = "config/settings.json";
        std::string csv_path = "simulation_output.csv";
        std::string graph_path;
        double graph_max_minutes = -1.0;

        bool has_duration_override = false;
        double duration_seconds = 0.0;
        bool has_outside_temp_override = false;
        double outside_temp_override = 0.0;
        bool has_outside_rh_override = false;
        double outside_rh_override = 0.0;
        bool has_outside_wind_speed_override = false;
        double outside_wind_speed_override = 0.0;
        bool has_outside_wind_direction_override = false;
        double outside_wind_direction_override = 0.0;
        bool has_outside_direct_rad_override = false;
        double outside_direct_rad_override = 0.0;
        bool has_outside_diffuse_rad_override = false;
        double outside_diffuse_rad_override = 0.0;
        bool has_outside_co2_override = false;
        double outside_co2_override = 0.0;

        ActuatorCommand command{};

        bool dampers_set = false;
        bool fans_set = false;
        bool heaters_set = false;
        bool damper_activity_set = false;
        bool fan_activity_set = false;
        bool heater_activity_set = false;
        bool damper_time_set = false;
        bool fan_time_set = false;
        bool heater_time_set = false;

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
            } else if (arg == "--duration-seconds") {
                duration_seconds = parse_double(require_value(arg), arg);
                has_duration_override = true;
            } else if (arg == "--duration-minutes") {
                duration_seconds = parse_double(require_value(arg), arg) * 60.0;
                has_duration_override = true;
            } else if (arg == "--outside-temp-c") {
                outside_temp_override = parse_double(require_value(arg), arg);
                has_outside_temp_override = true;
            } else if (arg == "--outside-rh") {
                outside_rh_override = parse_double(require_value(arg), arg);
                has_outside_rh_override = true;
            } else if (arg == "--outside-wind-speed-m-s") {
                outside_wind_speed_override = parse_double(require_value(arg), arg);
                has_outside_wind_speed_override = true;
            } else if (arg == "--outside-wind-direction-deg") {
                outside_wind_direction_override = parse_double(require_value(arg), arg);
                has_outside_wind_direction_override = true;
            } else if (arg == "--outside-direct-rad-w-m2") {
                outside_direct_rad_override = parse_double(require_value(arg), arg);
                has_outside_direct_rad_override = true;
            } else if (arg == "--outside-diffuse-rad-w-m2") {
                outside_diffuse_rad_override = parse_double(require_value(arg), arg);
                has_outside_diffuse_rad_override = true;
            } else if (arg == "--outside-co2-ppm") {
                outside_co2_override = parse_double(require_value(arg), arg);
                has_outside_co2_override = true;
            } else if (arg == "--dampers") {
                command.dampers_on = parse_bool_flag(require_value(arg));
                dampers_set = true;
            } else if (arg == "--damper-activity") {
                command.damper_activity_percent = parse_double(require_value(arg), arg);
                damper_activity_set = true;
            } else if (arg == "--dampers-active-seconds") {
                command.dampers_active_minutes = parse_double(require_value(arg), arg) / 60.0;
                damper_time_set = true;
            } else if (arg == "--dampers-active-minutes") {
                command.dampers_active_minutes = parse_double(require_value(arg), arg);
                damper_time_set = true;
            } else if (arg == "--fans") {
                command.fans_on = parse_bool_flag(require_value(arg));
                fans_set = true;
            } else if (arg == "--fan-activity" || arg == "--fan-driver" || arg == "--fan-driver-percent") {
                command.fan_activity_percent = parse_double(require_value(arg), arg);
                fan_activity_set = true;
            } else if (arg == "--fans-active-seconds") {
                command.fans_active_minutes = parse_double(require_value(arg), arg) / 60.0;
                fan_time_set = true;
            } else if (arg == "--fans-active-minutes") {
                command.fans_active_minutes = parse_double(require_value(arg), arg);
                fan_time_set = true;
            } else if (arg == "--heaters") {
                command.heaters_on = parse_bool_flag(require_value(arg));
                heaters_set = true;
            } else if (arg == "--heater-activity") {
                command.heater_activity_percent = parse_double(require_value(arg), arg);
                heater_activity_set = true;
            } else if (arg == "--heaters-active-seconds") {
                command.heaters_active_minutes = parse_double(require_value(arg), arg) / 60.0;
                heater_time_set = true;
            } else if (arg == "--heaters-active-minutes") {
                command.heaters_active_minutes = parse_double(require_value(arg), arg);
                heater_time_set = true;
            } else if (arg == "--csv") {
                csv_path = require_value(arg);
            } else if (arg == "--graph") {
                graph_path = require_value(arg);
            } else if (arg == "--graph-max-minutes") {
                graph_max_minutes = parse_double(require_value(arg), arg);
            } else {
                throw std::runtime_error("Unknown option: " + arg + ". Use --help for usage.");
            }
        }

        SimulationSettings settings = load_settings_from_json(settings_path);

        if (has_duration_override) {
            if (duration_seconds <= 0.0) {
                throw std::runtime_error("Simulation duration must be greater than zero.");
            }
            settings.n_steps = std::max(1, static_cast<int>(duration_seconds / settings.timestep_seconds));
        }
        if (has_outside_temp_override) {
            settings.climate.outside_temp_c = outside_temp_override;
        }
        if (has_outside_rh_override) {
            settings.climate.outside_relative_humidity = std::clamp(outside_rh_override, 0.0, 1.0);
        }

        const double default_active_minutes = settings.n_steps * settings.timestep_seconds / 60.0;
        if (!damper_time_set) {
            command.dampers_active_minutes = default_active_minutes;
        }
        if (!fan_time_set) {
            command.fans_active_minutes = default_active_minutes;
        }
        if (!heater_time_set) {
            command.heaters_active_minutes = default_active_minutes;
        }

        if (!dampers_set) {
            command.dampers_on = true;
        }
        if (!fans_set) {
            command.fans_on = true;
        }
        if (!heaters_set) {
            command.heaters_on = true;
        }

        if (!damper_activity_set) {
            command.damper_activity_percent = command.dampers_on ? 75.0 : 0.0;
        }
        if (!fan_activity_set) {
            command.fan_activity_percent = command.fans_on ? 60.0 : 0.0;
        }
        if (!heater_activity_set) {
            command.heater_activity_percent = command.heaters_on ? 40.0 : 0.0;
        }

        const SingleZoneSimulator simulator(settings);
        const SimulationResult result = simulator.estimate(command);
        write_csv_report(result, csv_path);
        if (!graph_path.empty()) {
            const double max_minutes = (graph_max_minutes > 0.0) ? graph_max_minutes : (settings.n_steps * settings.timestep_seconds / 60.0);
            write_svg_graph_report(result, graph_path, max_minutes);
        }

        std::cout << std::fixed << std::setprecision(4);
        std::cout << "settings_path=" << settings_path << '\n';
        std::cout << "csv_path=" << csv_path << '\n';
        if (!graph_path.empty()) {
            std::cout << "graph_path=" << graph_path << '\n';
        }
        std::cout << "simulated_duration_seconds=" << settings.n_steps * settings.timestep_seconds << '\n';
        std::cout << "outside_temp_c=" << settings.climate.outside_temp_c << '\n';
        std::cout << "dampers_on=" << (command.dampers_on ? 1 : 0) << '\n';
        std::cout << "fans_on=" << (command.fans_on ? 1 : 0) << '\n';
        std::cout << "heaters_on=" << (command.heaters_on ? 1 : 0) << '\n';
        std::cout << "final_outside_temp_c=" << result.summary.final_outside_temp_c << '\n';
        std::cout << "final_inside_temp_c=" << result.summary.final_inside_temp_c << '\n';
        std::cout << "final_relative_humidity=" << result.summary.final_relative_humidity << '\n';
        std::cout << "final_airflow_m3_s=" << result.summary.final_airflow_m3_s << '\n';
        std::cout << "average_airflow_m3_s=" << result.summary.average_airflow_m3_s << '\n';
        std::cout << "damper_electrical_energy_kwh=" << result.summary.cumulative_damper_electrical_energy_kwh << '\n';
        std::cout << "fan_electrical_energy_kwh=" << result.summary.cumulative_fan_electrical_energy_kwh << '\n';
        std::cout << "heater_electrical_energy_kwh=" << result.summary.cumulative_heater_electrical_energy_kwh << '\n';
        std::cout << "damper_resource_units=" << result.summary.cumulative_damper_resource_units << ' ' << settings.actuators.damper_resource_unit << '\n';
        std::cout << "fan_resource_units=" << result.summary.cumulative_fan_resource_units << ' ' << settings.actuators.fan_resource_unit << '\n';
        std::cout << "heater_resource_units=" << result.summary.cumulative_heater_resource_units << ' ' << settings.actuators.heater_resource_unit << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
}
