#include "cattle_climate/transformation.hpp"

#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    using namespace cattle_climate;
    if (argc < 3) throw std::runtime_error("Expected weight snapshot and sensor snapshot paths.");

    const auto weights = load_weight_snapshots_from_bridge(argv[1]);
    if (weights.empty()) throw std::runtime_error("Weight snapshots should not be empty.");
    std::vector<HerdCowGrpcRecord> herd;
    if (!lookup_weight_snapshot_for_datetime(herd, weights, "2023-01-02 12:00:00")) {
        throw std::runtime_error("Could not resolve herd snapshot.");
    }
    if (herd.size() < 2) throw std::runtime_error("Resolved herd is unexpectedly small.");

    const auto sensors = load_sensor_snapshots_from_bridge(argv[2]);
    if (sensors.empty()) throw std::runtime_error("Sensor snapshots should not be empty.");
    SensorSnapshot snap{};
    if (!lookup_sensor_snapshot_for_datetime(snap, sensors, "2023-01-01 01:10:00")) {
        throw std::runtime_error("Could not resolve sensor snapshot.");
    }
    WeatherSample base{};
    base.outdoor_temp_c = -5.0;
    const WeatherSample applied = apply_sensor_snapshot_to_weather(base, &snap);
    if (applied.outdoor_temp_c == base.outdoor_temp_c) {
        throw std::runtime_error("Sensor override was not applied.");
    }

    GrpcNodeSettings node{};
    node.endpoint = "unix:///tmp/actuators-interface.sock";
    node.service = "svc";
    node.method = "m";
    const std::string out = write_actuator_dispatch_record("build/test_actuator_dispatch.json", ActuatorDispatchRecord{"2023-01-01 01:00:00", RLAction{10.0, 20.0, 30.0}, 1.5, 420.0, 3.0, 4.0, 21.0}, node);
    std::cout << "dispatch_path=" << out << '\n';
    return 0;
}
