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
    if (argc < 2) throw std::runtime_error("Expected estimator request JSON path.");

    const EstimatorRequest req = load_estimator_request(argv[1]);
    if (req.node_name != "estimator-interface") throw std::runtime_error("Unexpected estimator node_name.");
    if (req.interval_minutes != 60) throw std::runtime_error("Unexpected estimator interval.");

    GrpcNodeSettings node{};
    node.endpoint = "unix:///tmp/estimator-interface.sock";
    node.service = "cattle.farming.transformation.v1.EstimatorInterfaceService";
    node.method = "PushEnergyAggregateReport";

    EstimatorAggregateDispatch dispatch{};
    dispatch.generated_at = "2023-01-02 00:00:00";
    dispatch.start_datetime = "2023-01-01 00:00:00";
    dispatch.end_datetime = "2023-01-01 23:59:59";
    dispatch.interval_minutes = 60;
    dispatch.mode_filter = "validate";
    dispatch.source_sqlite_path = "build/test_estimator.sqlite";
    dispatch.rows.push_back({"2023-01-01 00:00:00", "2023-01-01 01:00:00", 4, 0.1, 0.2, 0.3, 0.6, 450.0, 18.2});

    const std::string out_path = write_estimator_dispatch_record("build/test_estimator_dispatch.json", dispatch, node);
    const std::string out_text = read_all(out_path);
    require_contains(out_text, "uds-grpc-bridge", "Estimator transport");
    require_contains(out_text, node.endpoint, "Estimator endpoint");
    require_contains(out_text, "\"damper_energy_kwh\": 0.1", "Estimator damper energy");
    require_contains(out_text, "\"fan_energy_kwh\": 0.2", "Estimator fan energy");
    require_contains(out_text, "\"heater_energy_kwh\": 0.3", "Estimator heater energy");
    require_contains(out_text, "\"total_energy_kwh\": 0.6", "Estimator total energy");

    std::cout << "estimator_interface_offline_test=ok\n";
    return 0;
}
