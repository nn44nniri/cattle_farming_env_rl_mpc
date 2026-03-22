#include "cattle_climate/transformation.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace cattle_climate {
namespace {

std::string read_text_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Unable to open file: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
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
    bool in_string = false;
    char prev = '\0';
    std::size_t current_start = std::string::npos;
    for (std::size_t i = 0; i < array_text.size(); ++i) {
        const char ch = array_text[i];
        if (ch == '"' && prev != '\\') in_string = !in_string;
        if (!in_string) {
            if (ch == '{') {
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

double extract_number(const std::string& json, const std::string& key, double default_value) {
    const std::regex r("\\\"" + key + "\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)");
    std::smatch match;
    if (std::regex_search(json, match, r)) return std::stod(match[1].str());
    return default_value;
}

std::string extract_string(const std::string& json, const std::string& key, const std::string& default_value) {
    const std::regex r("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    if (std::regex_search(json, match, r)) return match[1].str();
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

void ensure_parent(const std::string& path) {
    const auto pos = path.find_last_of('/');
    if (pos == std::string::npos) return;
    const std::string dir = path.substr(0, pos);
    if (dir.empty()) return;
    std::system((std::string("mkdir -p \"") + dir + "\"").c_str());
}

} // namespace

std::vector<HerdWeightSnapshot> load_weight_snapshots_from_bridge(const std::string& path) {
    std::vector<HerdWeightSnapshot> snapshots;
    if (path.empty()) return snapshots;
    const std::string text = read_text_file(path);
    for (const auto& record_text : extract_array_of_objects(text, "records")) {
        HerdWeightSnapshot snapshot{};
        snapshot.effective_datetime = normalize_datetime_prefix(extract_string(record_text, "effective_datetime", ""));
        for (const auto& cow_text : extract_array_of_objects(record_text, "cows")) {
            HerdCowGrpcRecord cow{};
            cow.cow_id = extract_string(cow_text, "cow_id", "unknown_cow");
            cow.breed = extract_string(cow_text, "breed", "charolais");
            cow.body_weight_kg = extract_number(cow_text, "body_weight_kg", 450.0);
            cow.gain_when_comfortable_kg_per_day = extract_number(cow_text, "gain_when_comfortable_kg_per_day", cow.gain_when_comfortable_kg_per_day);
            cow.loss_when_stressed_kg_per_day = extract_number(cow_text, "loss_when_stressed_kg_per_day", cow.loss_when_stressed_kg_per_day);
            snapshot.cows.push_back(cow);
        }
        if (!snapshot.effective_datetime.empty() && !snapshot.cows.empty()) snapshots.push_back(snapshot);
    }
    std::sort(snapshots.begin(), snapshots.end(), [](const HerdWeightSnapshot& a, const HerdWeightSnapshot& b) {
        return a.effective_datetime < b.effective_datetime;
    });
    return snapshots;
}

bool lookup_weight_snapshot_for_datetime(std::vector<HerdCowGrpcRecord>& herd, const std::vector<HerdWeightSnapshot>& snapshots, const std::string& current_datetime) {
    if (snapshots.empty()) return false;
    const std::string target = normalize_datetime_prefix(current_datetime);
    const HerdWeightSnapshot* best = nullptr;
    for (const auto& s : snapshots) {
        if (s.effective_datetime <= target) best = &s; else break;
    }
    if (best == nullptr) return false;
    herd = best->cows;
    return true;
}

std::vector<SensorSnapshot> load_sensor_snapshots_from_bridge(const std::string& path) {
    std::vector<SensorSnapshot> snapshots;
    if (path.empty()) return snapshots;
    const std::string text = read_text_file(path);
    for (const auto& record_text : extract_array_of_objects(text, "records")) {
        SensorSnapshot s{};
        s.effective_datetime = normalize_datetime_prefix(extract_string(record_text, "effective_datetime", ""));
        const double temp = extract_number(record_text, "outdoor_temp_c", 0.0);
        if (record_text.find("\"outdoor_temp_c\"") != std::string::npos) { s.has_outdoor_temp_c = true; s.outdoor_temp_c = temp; }
        const double rh = extract_number(record_text, "relative_humidity", 0.0);
        if (record_text.find("\"relative_humidity\"") != std::string::npos) { s.has_relative_humidity = true; s.relative_humidity = rh; }
        const double wind = extract_number(record_text, "wind_speed_m_s", 0.0);
        if (record_text.find("\"wind_speed_m_s\"") != std::string::npos) { s.has_wind_speed_m_s = true; s.wind_speed_m_s = wind; }
        const double dr = extract_number(record_text, "direct_radiation_w_m2", 0.0);
        if (record_text.find("\"direct_radiation_w_m2\"") != std::string::npos) { s.has_direct_radiation_w_m2 = true; s.direct_radiation_w_m2 = dr; }
        const double dif = extract_number(record_text, "diffuse_radiation_w_m2", 0.0);
        if (record_text.find("\"diffuse_radiation_w_m2\"") != std::string::npos) { s.has_diffuse_radiation_w_m2 = true; s.diffuse_radiation_w_m2 = dif; }
        const double p = extract_number(record_text, "precipitation_mm", 0.0);
        if (record_text.find("\"precipitation_mm\"") != std::string::npos) { s.has_precipitation_mm = true; s.precipitation_mm = p; }
        const double cc = extract_number(record_text, "cloud_cover_fraction", 0.0);
        if (record_text.find("\"cloud_cover_fraction\"") != std::string::npos) { s.has_cloud_cover_fraction = true; s.cloud_cover_fraction = cc; }
        if (!s.effective_datetime.empty()) snapshots.push_back(s);
    }
    std::sort(snapshots.begin(), snapshots.end(), [](const SensorSnapshot& a, const SensorSnapshot& b) {
        return a.effective_datetime < b.effective_datetime;
    });
    return snapshots;
}

bool lookup_sensor_snapshot_for_datetime(SensorSnapshot& snapshot, const std::vector<SensorSnapshot>& snapshots, const std::string& current_datetime) {
    if (snapshots.empty()) return false;
    const std::string target = normalize_datetime_prefix(current_datetime);
    const SensorSnapshot* best = nullptr;
    for (const auto& s : snapshots) {
        if (s.effective_datetime <= target) best = &s; else break;
    }
    if (best == nullptr) return false;
    snapshot = *best;
    return true;
}

WeatherSample apply_sensor_snapshot_to_weather(const WeatherSample& base, const SensorSnapshot* snapshot) {
    if (snapshot == nullptr) return base;
    WeatherSample out = base;
    if (snapshot->has_outdoor_temp_c) out.outdoor_temp_c = snapshot->outdoor_temp_c;
    if (snapshot->has_relative_humidity) out.relative_humidity = snapshot->relative_humidity;
    if (snapshot->has_wind_speed_m_s) out.wind_speed_m_s = snapshot->wind_speed_m_s;
    if (snapshot->has_direct_radiation_w_m2) out.direct_radiation_w_m2 = snapshot->direct_radiation_w_m2;
    if (snapshot->has_diffuse_radiation_w_m2) out.diffuse_radiation_w_m2 = snapshot->diffuse_radiation_w_m2;
    if (snapshot->has_precipitation_mm) out.precipitation_mm = snapshot->precipitation_mm;
    if (snapshot->has_cloud_cover_fraction) out.cloud_cover_fraction = snapshot->cloud_cover_fraction;
    return out;
}

std::string write_actuator_dispatch_record(const std::string& path, const ActuatorDispatchRecord& record, const GrpcNodeSettings& node) {
    ensure_parent(path);
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Could not write actuator dispatch file: " + path);
    out << "{\n";
    out << "  \"transport\": \"uds-grpc-bridge\",\n";
    out << "  \"endpoint\": \"" << node.endpoint << "\",\n";
    out << "  \"service\": \"" << node.service << "\",\n";
    out << "  \"method\": \"" << node.method << "\",\n";
    out << "  \"effective_datetime\": \"" << record.effective_datetime << "\",\n";
    out << "  \"predicted_reward\": " << record.predicted_reward << ",\n";
    out << "  \"mean_body_weight_kg\": " << record.mean_body_weight_kg << ",\n";
    out << "  \"herd_size\": " << record.herd_size << ",\n";
    out << "  \"lct_c\": " << record.lct_c << ",\n";
    out << "  \"uct_c\": " << record.uct_c << ",\n";
    out << "  \"action\": {\n";
    out << "    \"damper_percent\": " << record.action.damper_percent << ",\n";
    out << "    \"fan_percent\": " << record.action.fan_percent << ",\n";
    out << "    \"heater_percent\": " << record.action.heater_percent << "\n";
    out << "  }\n";
    out << "}\n";
    return path;
}

EstimatorRequest load_estimator_request(const std::string& path) {
    const std::string text = read_text_file(path);
    EstimatorRequest req{};
    req.node_name = extract_string(text, "node_name", "estimator-interface");
    req.start_datetime = normalize_datetime_prefix(extract_string(text, "start_datetime", ""));
    req.end_datetime = normalize_datetime_prefix(extract_string(text, "end_datetime", ""));
    req.interval_minutes = static_cast<int>(extract_number(text, "interval_minutes", 60.0));
    req.mode_filter = extract_string(text, "mode_filter", "");
    return req;
}

std::string write_estimator_dispatch_record(const std::string& path, const EstimatorAggregateDispatch& dispatch, const GrpcNodeSettings& node) {
    ensure_parent(path);
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Could not open estimator dispatch output path: " + path);
    out << "{\n";
    out << "  \"transport\": \"uds-grpc-bridge\",\n";
    out << "  \"endpoint\": \"" << node.endpoint << "\",\n";
    out << "  \"service\": \"" << node.service << "\",\n";
    out << "  \"method\": \"" << node.method << "\",\n";
    out << "  \"request_window\": {\n";
    out << "    \"start_datetime\": \"" << dispatch.start_datetime << "\",\n";
    out << "    \"end_datetime\": \"" << dispatch.end_datetime << "\",\n";
    out << "    \"interval_minutes\": " << dispatch.interval_minutes << ",\n";
    out << "    \"mode_filter\": \"" << dispatch.mode_filter << "\"\n";
    out << "  },\n";
    out << "  \"response\": {\n";
    out << "    \"generated_at\": \"" << dispatch.generated_at << "\",\n";
    out << "    \"source_sqlite_path\": \"" << dispatch.source_sqlite_path << "\",\n";
    out << "    \"rows\": [\n";
    for (std::size_t i = 0; i < dispatch.rows.size(); ++i) {
        const auto& row = dispatch.rows[i];
        out << "      {\n";
        out << "        \"interval_start\": \"" << row.interval_start << "\",\n";
        out << "        \"interval_end\": \"" << row.interval_end << "\",\n";
        out << "        \"decision_count\": " << row.decision_count << ",\n";
        out << "        \"damper_energy_kwh\": " << row.damper_energy_kwh << ",\n";
        out << "        \"fan_energy_kwh\": " << row.fan_energy_kwh << ",\n";
        out << "        \"heater_energy_kwh\": " << row.heater_energy_kwh << ",\n";
        out << "        \"total_energy_kwh\": " << row.total_energy_kwh << ",\n";
        out << "        \"mean_body_weight_kg\": " << row.mean_body_weight_kg << ",\n";
        out << "        \"mean_indoor_temp_c\": " << row.mean_indoor_temp_c << "\n";
        out << "      }" << (i + 1 < dispatch.rows.size() ? "," : "") << "\n";
    }
    out << "    ]\n";
    out << "  }\n";
    out << "}\n";
    return path;
}


LiGAPSClimateRequest load_ligaps_climate_request(const std::string& path) {
    const std::string text = read_text_file(path);
    LiGAPSClimateRequest req{};
    req.node_name = extract_string(text, "node_name", "liGAPS-Beef-optimizer");
    req.query_date = extract_string(text, "query_date", "");
    req.mode_filter = extract_string(text, "mode_filter", "");
    return req;
}

std::string write_ligaps_climate_dispatch_record(const std::string& path, const LiGAPSDayNightClimateDispatch& dispatch, const GrpcNodeSettings& node) {
    ensure_parent(path);
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Could not open LiGAPS climate dispatch output path: " + path);
    out << "{\n";
    out << "  \"transport\": \"uds-grpc-bridge\",\n";
    out << "  \"endpoint\": \"" << node.endpoint << "\",\n";
    out << "  \"service\": \"" << node.service << "\",\n";
    out << "  \"method\": \"" << node.method << "\",\n";
    out << "  \"request\": {\n";
    out << "    \"query_date\": \"" << dispatch.query_date << "\",\n";
    out << "    \"mode_filter\": \"" << dispatch.mode_filter << "\"\n";
    out << "  },\n";
    out << "  \"response\": {\n";
    out << "    \"generated_at\": \"" << dispatch.generated_at << "\",\n";
    out << "    \"source_sqlite_path\": \"" << dispatch.source_sqlite_path << "\",\n";
    out << "    \"periods\": [\n";
    for (std::size_t i = 0; i < dispatch.periods.size(); ++i) {
        const auto& row = dispatch.periods[i];
        out << "      {\n";
        out << "        \"period_label\": \"" << row.period_label << "\",\n";
        out << "        \"sample_count\": " << row.sample_count << ",\n";
        out << "        \"mean_indoor_temp_c\": " << row.mean_indoor_temp_c << ",\n";
        out << "        \"mean_indoor_relative_humidity\": " << row.mean_indoor_relative_humidity << ",\n";
        out << "        \"mean_indoor_airflow_m3_s\": " << row.mean_indoor_airflow_m3_s << ",\n";
        out << "        \"mean_internal_heat_generated_w\": " << row.mean_internal_heat_generated_w << "\n";
        out << "      }" << (i + 1 < dispatch.periods.size() ? "," : "") << "\n";
    }
    out << "    ]\n";
    out << "  }\n";
    out << "}\n";
    return path;
}

} // namespace cattle_climate
