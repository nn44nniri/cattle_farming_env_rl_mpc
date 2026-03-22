#include "cattle_climate/rl_mpc.hpp"

#include <exception>
#include <iostream>
#include <string>

namespace {

void print_help(const std::string& exe) {
    std::cout
        << "Usage:\n"
        << "  " << exe << " train --settings <path>\n"
        << "  " << exe << " validate --settings <path> [--model <path>] [--auto-weight true|false] [--manual-weight-file <path>] [--start-datetime <YYYY-MM-DD_HH:MM:SS>] [--end-datetime <YYYY-MM-DD_HH:MM:SS>] [--csv <path>] [--svg <path>] [--simulat-weights]\n"
        << "  " << exe << " operate --settings <path> [--model <path>] [--start-datetime <YYYY-MM-DD_HH:MM:SS>] [--end-datetime <YYYY-MM-DD_HH:MM:SS>] [--real-weights]\n"
        << "  " << exe << " service --settings <path> [--model <path>] --mode run|stop\n"
        << "  " << exe << " estimator-report --settings <path> [--start-datetime <YYYY-MM-DD_HH:MM:SS>] [--end-datetime <YYYY-MM-DD_HH:MM:SS>] [--interval-minutes <n>] [--mode train|validate|operate] [--output <path>]\n"
        << "  " << exe << " ligaps-climate-report --settings <path> [--date <YYYY-MM-DD>] [--mode train|validate|operate] [--output <path>]\n";
}

bool parse_bool(const std::string& value) {
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            print_help(argv[0]);
            return 1;
        }
        const std::string command = argv[1];
        std::string settings_path = "config/settings.json";
        std::string model_path;
        std::string manual_weight_file;
        std::string csv_path;
        std::string svg_path;
        std::string output_path;
        bool auto_weight = true;
        std::string start_datetime;
        std::string end_datetime;
        bool simulat_weights = false;
        bool real_weights = false;
        int interval_minutes = 60;
        std::string mode_filter;
        std::string query_date;

        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            auto require_value = [&](const std::string& opt) -> std::string {
                if (i + 1 >= argc) throw std::runtime_error("Missing value after " + opt);
                return argv[++i];
            };
            if (arg == "--settings") settings_path = require_value(arg);
            else if (arg == "--model") model_path = require_value(arg);
            else if (arg == "--auto-weight") auto_weight = parse_bool(require_value(arg));
            else if (arg == "--manual-weight-file") manual_weight_file = require_value(arg);
            else if (arg == "--start-datetime") start_datetime = require_value(arg);
            else if (arg == "--end-datetime") end_datetime = require_value(arg);
            else if (arg == "--csv") csv_path = require_value(arg);
            else if (arg == "--svg") svg_path = require_value(arg);
            else if (arg == "--output") output_path = require_value(arg);
            else if (arg == "--interval-minutes") interval_minutes = std::stoi(require_value(arg));
            else if (arg == "--mode") mode_filter = require_value(arg);
            else if (arg == "--date") query_date = require_value(arg);
            else if (arg == "--simulat-weights") simulat_weights = true;
            else if (arg == "--real-weights") real_weights = true;
            else if (arg == "--help") {
                print_help(argv[0]);
                return 0;
            } else {
                throw std::runtime_error("Unknown option: " + arg);
            }
        }

        const cattle_climate::ProjectSettings project = cattle_climate::load_project_settings_from_json(settings_path);
        if (command == "train") {
            return cattle_climate::train_controller(project);
        }
        if (command == "validate") {
            const std::string resolved_model = model_path.empty() ? project.training.model_output_path : model_path;
            const std::string resolved_csv = csv_path.empty() ? project.validation.csv_output_path : csv_path;
            const std::string resolved_svg = svg_path.empty() ? project.validation.svg_output_path : svg_path;
            const std::string resolved_start = start_datetime.empty() ? project.validation.start_datetime : start_datetime;
            const std::string resolved_end = end_datetime.empty() ? project.validation.end_datetime : end_datetime;
            const std::string resolved_manual = manual_weight_file.empty() ? project.validation.manual_weight_file : manual_weight_file;
            return cattle_climate::validate_controller(project, resolved_model, auto_weight, resolved_manual, resolved_start, resolved_end, resolved_csv, resolved_svg, simulat_weights);
        }
        if (command == "operate") {
            const std::string resolved_model = model_path.empty() ? project.training.model_output_path : model_path;
            const std::string resolved_start = start_datetime.empty() ? project.validation.start_datetime : start_datetime;
            const std::string resolved_end = end_datetime.empty() ? project.validation.end_datetime : end_datetime;
            return cattle_climate::operate_controller(project, resolved_model, resolved_start, resolved_end, real_weights);
        }
        if (command == "service") {
            const std::string resolved_model = model_path.empty() ? project.runtime_service.model_path : model_path;
            if (mode_filter == "run") return cattle_climate::run_controller_service(project, resolved_model);
            if (mode_filter == "stop") return cattle_climate::stop_controller_service(project);
            throw std::runtime_error("service requires --mode run or --mode stop");
        }
        if (command == "estimator-report") {
            cattle_climate::EstimatorRequest req{};
            if (!project.transformation.estimator_interface.request_json_path.empty()) {
                try { req = cattle_climate::load_estimator_request(project.transformation.estimator_interface.request_json_path); }
                catch (...) {}
            }
            const std::string resolved_start = start_datetime.empty() ? (req.start_datetime.empty() ? project.validation.start_datetime : req.start_datetime) : start_datetime;
            const std::string resolved_end = end_datetime.empty() ? (req.end_datetime.empty() ? project.validation.end_datetime : req.end_datetime) : end_datetime;
            const int resolved_interval = interval_minutes > 0 ? interval_minutes : req.interval_minutes;
            const std::string resolved_mode = mode_filter.empty() ? req.mode_filter : mode_filter;
            return cattle_climate::export_estimator_report(project, resolved_start, resolved_end, resolved_interval, resolved_mode, output_path);
        }
        if (command == "ligaps-climate-report") {
            cattle_climate::LiGAPSClimateRequest req{};
            if (!project.transformation.ligaps_beef_optimizer.request_json_path.empty()) {
                try { req = cattle_climate::load_ligaps_climate_request(project.transformation.ligaps_beef_optimizer.request_json_path); }
                catch (...) {}
            }
            const std::string resolved_date = query_date.empty() ? req.query_date : query_date;
            const std::string resolved_mode = mode_filter.empty() ? req.mode_filter : mode_filter;
            return cattle_climate::export_ligaps_climate_report(project, resolved_date, resolved_mode, output_path);
        }
        throw std::runtime_error("Unknown command: " + command);
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
}
