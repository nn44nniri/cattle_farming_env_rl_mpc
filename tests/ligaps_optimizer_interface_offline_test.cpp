#include "cattle_climate/transformation.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
std::string read_all(const std::string& path) { std::ifstream in(path); if (!in) throw std::runtime_error("Unable to open file"); std::ostringstream ss; ss << in.rdbuf(); return ss.str(); }
void require_contains(const std::string& text, const std::string& needle) { if (text.find(needle) == std::string::npos) throw std::runtime_error("Missing expected text: " + needle); }
}

int main(int argc, char** argv) {
    using namespace cattle_climate;
    if (argc < 2) throw std::runtime_error("Expected ligaps optimizer request JSON path.");
    const auto req = load_ligaps_climate_request(argv[1]);
    if (req.node_name != "liGAPS-Beef-optimizer") throw std::runtime_error("Unexpected node_name");
    if (req.query_date != "2023-01-01") throw std::runtime_error("Unexpected query_date");
    GrpcNodeSettings node{};
    node.endpoint = "unix:///tmp/ligaps-beef-optimizer.sock";
    node.service = "cattle.farming.transformation.v1.LiGAPSBeefOptimizerService";
    node.method = "GetIndoorClimateByDate";
    LiGAPSDayNightClimateDispatch dispatch{};
    dispatch.generated_at = "2023-01-01 23:59:59";
    dispatch.query_date = "2023-01-01";
    dispatch.source_sqlite_path = "output/test.sqlite";
    dispatch.mode_filter = "validate";
    dispatch.periods.push_back({"day", 12, 18.5, 0.61, 1.2, 4200.0});
    dispatch.periods.push_back({"night", 12, 15.2, 0.68, 0.8, 4100.0});
    const std::string out_path = write_ligaps_climate_dispatch_record("build/test_ligaps_optimizer_dispatch.json", dispatch, node);
    const std::string text = read_all(out_path);
    require_contains(text, "GetIndoorClimateByDate");
    require_contains(text, "\"period_label\": \"day\"");
    require_contains(text, "\"mean_indoor_airflow_m3_s\": 1.2");
    require_contains(text, "\"mean_internal_heat_generated_w\": 4100");
    std::cout << "ligaps_optimizer_interface_offline_test=ok\n";
    return 0;
}
