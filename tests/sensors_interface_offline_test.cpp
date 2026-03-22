#include "cattle_climate/transformation.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
std::string read_all(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Unable to open file: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void require_contains(const std::string& text, const std::string& needle, const std::string& label) {
    if (text.find(needle) == std::string::npos) {
        throw std::runtime_error("Missing expected text for " + label + ": " + needle);
    }
}
}

int main(int argc, char** argv) {
    using namespace cattle_climate;
    if (argc < 3) {
        throw std::runtime_error("Expected request JSON path and response JSON path.");
    }

    const std::string request_text = read_all(argv[1]);
    require_contains(request_text, "\"node_name\"", "Sensors request node name");
    require_contains(request_text, "sensors-interface", "Sensors request node value");
    require_contains(request_text, "\"current_datetime\"", "Sensors request datetime");

    const auto snapshots = load_sensor_snapshots_from_bridge(argv[2]);
    if (snapshots.size() < 2) {
        throw std::runtime_error("Expected at least two sensor snapshots.");
    }

    SensorSnapshot selected{};
    if (!lookup_sensor_snapshot_for_datetime(selected, snapshots, "2023-01-01 01:10:00")) {
        throw std::runtime_error("Expected sensor snapshot lookup to succeed.");
    }
    if (!selected.has_outdoor_temp_c || !selected.has_relative_humidity || !selected.has_wind_speed_m_s) {
        throw std::runtime_error("Resolved sensor snapshot is missing required fields.");
    }
    if (selected.outdoor_temp_c != 3.5) {
        throw std::runtime_error("Unexpected resolved outdoor temperature.");
    }

    WeatherSample base{};
    base.outdoor_temp_c = -5.0;
    base.relative_humidity = 0.4;
    base.wind_speed_m_s = 0.2;
    const WeatherSample applied = apply_sensor_snapshot_to_weather(base, &selected);
    if (applied.outdoor_temp_c == base.outdoor_temp_c || applied.relative_humidity == base.relative_humidity ||
        applied.wind_speed_m_s == base.wind_speed_m_s) {
        throw std::runtime_error("Sensor snapshot values were not applied to the weather sample.");
    }

    SensorSnapshot earlier{};
    if (lookup_sensor_snapshot_for_datetime(earlier, snapshots, "2022-12-31 23:59:59")) {
        throw std::runtime_error("Lookup before first sensor snapshot should fail.");
    }

    std::cout << "sensors_interface_offline_test=ok\n";
    return 0;
}
