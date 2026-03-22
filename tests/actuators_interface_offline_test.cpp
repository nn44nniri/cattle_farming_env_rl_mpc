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
    if (argc < 2) {
        throw std::runtime_error("Expected actuators request JSON path.");
    }

    const std::string request_text = read_all(argv[1]);
    require_contains(request_text, "\"node_name\"", "Actuators request node name");
    require_contains(request_text, "actuators-interface", "Actuators request node value");
    require_contains(request_text, "\"current_datetime\"", "Actuators request datetime");
    require_contains(request_text, "\"action\"", "Actuators request action block");
    require_contains(request_text, "\"damper_percent\"", "Actuators request damper field");
    require_contains(request_text, "\"fan_percent\"", "Actuators request fan field");
    require_contains(request_text, "\"heater_percent\"", "Actuators request heater field");

    GrpcNodeSettings node{};
    node.endpoint = "unix:///tmp/actuators-interface.sock";
    node.service = "cattle.farming.transformation.v1.ActuatorsInterfaceService";
    node.method = "PushActuatorDispatch";

    const ActuatorDispatchRecord record{
        "2023-01-01 00:00:00",
        RLAction{15.0, 35.0, 55.0},
        12.5,
        418.4,
        3.0,
        4.2,
        21.8
    };

    const std::string out_path = write_actuator_dispatch_record("build/test_offline_actuator_dispatch.json", record, node);
    const std::string out_text = read_all(out_path);
    require_contains(out_text, "uds-grpc-bridge", "Dispatch transport");
    require_contains(out_text, node.endpoint, "Dispatch endpoint");
    require_contains(out_text, node.service, "Dispatch service");
    require_contains(out_text, node.method, "Dispatch method");
    require_contains(out_text, "\"damper_percent\": 15", "Dispatch damper value");
    require_contains(out_text, "\"fan_percent\": 35", "Dispatch fan value");
    require_contains(out_text, "\"heater_percent\": 55", "Dispatch heater value");
    require_contains(out_text, "\"predicted_reward\": 12.5", "Dispatch reward value");

    std::cout << "actuators_interface_offline_test=ok\n";
    return 0;
}
