#include "cattle_climate/weather_dataset.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cattle_climate {
namespace {

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;
    for (char ch : line) {
        if (ch == '"') {
            in_quotes = !in_quotes;
        } else if (ch == ',' && !in_quotes) {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    fields.push_back(current);
    return fields;
}

double parse_double(const std::string& text, double default_value = 0.0) {
    if (text.empty()) return default_value;
    try {
        return std::stod(text);
    } catch (...) {
        return default_value;
    }
}

int parse_int(const std::string& text, int default_value = 0) {
    if (text.empty()) return default_value;
    try {
        return std::stoi(text);
    } catch (...) {
        return default_value;
    }
}

double compute_clear_sky_solar(int hour, int month, double cloud_cover_fraction) {
    const double seasonal = 0.75 + 0.25 * std::cos((static_cast<double>(month) - 6.0) * 3.141592653589793 / 6.0);
    const double angle = (static_cast<double>(hour) - 12.0) * 3.141592653589793 / 12.0;
    const double daylight = std::max(0.0, std::cos(angle));
    const double clear_sky = 850.0 * seasonal * daylight;
    return clear_sky * std::clamp(1.0 - 0.75 * cloud_cover_fraction, 0.05, 1.0);
}

} // namespace

WeatherDataset WeatherDataset::load_csv(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Could not open weather dataset: " + path);
    }

    std::string header;
    if (!std::getline(in, header)) {
        throw std::runtime_error("Weather dataset is empty: " + path);
    }

    const auto columns = split_csv_line(header);
    auto find_col = [&](const std::string& name) -> int {
        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (columns[i] == name) return static_cast<int>(i);
        }
        return -1;
    };

    const int idx_dt_iso = find_col("dt_iso");
    const int idx_temp = find_col("temp");
    const int idx_humidity = find_col("humidity");
    const int idx_wind = find_col("wind_speed");
    const int idx_rain_1h = find_col("rain_1h");
    const int idx_rain_3h = find_col("rain_3h");
    const int idx_clouds = find_col("clouds_all");
    if (idx_dt_iso < 0 || idx_temp < 0 || idx_humidity < 0 || idx_wind < 0 || idx_clouds < 0) {
        throw std::runtime_error("Weather dataset is missing one or more required columns.");
    }

    WeatherDataset dataset;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto fields = split_csv_line(line);
        auto get = [&](int idx) -> std::string {
            return (idx >= 0 && idx < static_cast<int>(fields.size())) ? fields[static_cast<std::size_t>(idx)] : std::string{};
        };

        WeatherSample s{};
        s.dt_iso = get(idx_dt_iso);
        if (s.dt_iso.size() >= 19) {
            s.year = parse_int(s.dt_iso.substr(0, 4));
            s.month = parse_int(s.dt_iso.substr(5, 2));
            s.day = parse_int(s.dt_iso.substr(8, 2));
            s.hour = parse_int(s.dt_iso.substr(11, 2));
        }
        s.outdoor_temp_c = parse_double(get(idx_temp));
        s.relative_humidity = std::clamp(parse_double(get(idx_humidity)) / 100.0, 0.0, 1.0);
        s.wind_speed_m_s = std::max(0.0, parse_double(get(idx_wind)));
        s.precipitation_mm = std::max(0.0, parse_double(get(idx_rain_1h)) + parse_double(get(idx_rain_3h)) / 3.0);
        s.cloud_cover_fraction = std::clamp(parse_double(get(idx_clouds)) / 100.0, 0.0, 1.0);
        const double total_solar = compute_clear_sky_solar(s.hour, s.month, s.cloud_cover_fraction);
        s.direct_radiation_w_m2 = 0.75 * total_solar;
        s.diffuse_radiation_w_m2 = 0.25 * total_solar;
        dataset.samples_.push_back(s);
    }

    if (dataset.samples_.empty()) {
        throw std::runtime_error("Weather dataset contains no data rows: " + path);
    }
    return dataset;
}

} // namespace cattle_climate
