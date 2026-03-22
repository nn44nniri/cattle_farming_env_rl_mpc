#include "cattle_climate/rl_mpc.hpp"
#include "cattle_climate/event_store.hpp"

#include <cstdio>
#include <filesystem>
#include <stdexcept>

int main(int argc, char** argv) {
    using namespace cattle_climate;
    namespace fs = std::filesystem;
    if (argc < 2) throw std::runtime_error("Expected settings path.");
    const fs::path settings_path = fs::absolute(argv[1]);
    fs::current_path(settings_path.parent_path().parent_path());
    ProjectSettings project = load_project_settings_from_json(settings_path.string());
    project.runtime_service.max_cycles = 1;
    project.runtime_service.polling_interval_seconds = 1;
    project.runtime_service.stop_file_path = "/tmp/cattle_farming_env_rl_mpc_smoke.stop";
    project.event_store.sqlite_path = "output/runtime_service_smoke.sqlite";
    std::remove(project.event_store.sqlite_path.c_str());
    std::remove(project.runtime_service.stop_file_path.c_str());
    const int rc = run_controller_service(project, project.runtime_service.model_path);
    if (rc != 0) throw std::runtime_error("run_controller_service failed");
    const auto rows = query_estimator_aggregate(project.event_store, "2000-01-01 00:00:00", "2100-01-01 00:00:00", 60, "run");
    if (rows.empty()) throw std::runtime_error("Expected realtime aggregate rows");
    return 0;
}
