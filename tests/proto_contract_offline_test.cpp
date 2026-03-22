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
        throw std::runtime_error("Missing expected proto contract text for " + label + ": " + needle);
    }
}
}

int main(int argc, char** argv) {
    if (argc < 2) {
        throw std::runtime_error("Expected proto path.");
    }
    const std::string proto = read_all(argv[1]);
    require_contains(proto, "syntax = \"proto3\";", "proto3 syntax");
    require_contains(proto, "service LiGAPSBeefWeightService", "LiGAPS service");
    require_contains(proto, "rpc GetLatestWeightsByDate", "LiGAPS RPC");
    require_contains(proto, "service SensorsInterfaceService", "Sensors service");
    require_contains(proto, "rpc GetLatestSensorFrame", "Sensors RPC");
    require_contains(proto, "service ActuatorsInterfaceService", "Actuators service");
    require_contains(proto, "rpc PushActuatorDispatch", "Actuators RPC");
    require_contains(proto, "message HerdWeightsByDateRequest", "LiGAPS request message");
    require_contains(proto, "message SensorFrameResponse", "Sensors response message");
    require_contains(proto, "message ActuatorDispatchRequest", "Actuators request message");
    require_contains(proto, "string current_datetime = 2;", "current datetime field");
    require_contains(proto, "double heater_percent = 5;", "heater field number");

    std::cout << "proto_contract_offline_test=ok\n";
    return 0;
}
