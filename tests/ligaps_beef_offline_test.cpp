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
    require_contains(request_text, "\"node_name\"", "LiGAPS request node name");
    require_contains(request_text, "liGRAPS-Beef", "LiGAPS request node value");
    require_contains(request_text, "\"current_datetime\"", "LiGAPS request datetime");

    const auto snapshots = load_weight_snapshots_from_bridge(argv[2]);
    if (snapshots.size() < 2) {
        throw std::runtime_error("Expected at least two herd weight snapshots.");
    }
    if (snapshots.front().effective_datetime != "2023-01-01 00:00:00") {
        throw std::runtime_error("Unexpected first effective datetime in herd snapshots.");
    }

    std::vector<HerdCowGrpcRecord> herd;
    if (!lookup_weight_snapshot_for_datetime(herd, snapshots, "2023-01-02 12:00:00")) {
        throw std::runtime_error("Expected herd lookup to succeed.");
    }
    if (herd.size() != 3) {
        throw std::runtime_error("Expected 3 cows in the selected herd snapshot.");
    }
    if (herd[0].cow_id != "charolais_real_001") {
        throw std::runtime_error("Unexpected first cow id in selected herd snapshot.");
    }
    if (herd[2].breed != "brahman_shorthorn") {
        throw std::runtime_error("Expected brahman_shorthorn in selected herd snapshot.");
    }
    if (herd[1].body_weight_kg <= 0.0) {
        throw std::runtime_error("Body weight should stay positive.");
    }

    std::vector<HerdCowGrpcRecord> earlier_herd;
    if (lookup_weight_snapshot_for_datetime(earlier_herd, snapshots, "2022-12-31 23:59:59")) {
        throw std::runtime_error("Lookup before the first available herd snapshot should fail.");
    }

    std::cout << "ligaps_beef_offline_test=ok\n";
    return 0;
}
